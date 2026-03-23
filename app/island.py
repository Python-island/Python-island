"""现代化灵动岛主窗口模块

实现灵动岛的主窗口，整合各个功能模块，提供完整的用户界面。
"""
from app.core.workshop import WorkshopManager
from app.ui.workshop_ui import WorkshopUI
from PySide6.QtCore import QEvent, QPropertyAnimation, QRect, QEasingCurve, Qt, QTimer
from PySide6.QtGui import QCursor
from PySide6.QtWidgets import QApplication, QWidget
from PySide6.QtCore import Qt

from app.animations.effects import AnimationManager, RoundedMaskHelper
from app.core.animation_controller import AnimationController
from app.core.config import (
    CLIPBOARD_CHECK_INTERVAL,
    COLLAPSED_HEIGHT,
    COLLAPSED_WIDTH,
    CONNECTION_AUTO_CLOSE_DELAY,
    DEBOUNCE_DELAY,
    DOCK_ANIMATION_DURATION,
    DRAG_IDLE_RETURN_DELAY,
    EXPANDED_HEIGHT,
    EXPANDED_WIDTH,
    MAX_EXPAND_HEIGHT_RATIO,
    SNAP_THRESHOLD,
    STATUS_UPDATE_INTERVAL,
    STYLES_PATH,
    TIME_UPDATE_INTERVAL,
    URL_AUTO_CLOSE_DELAY,
    VISIBLE_TOP_BORDER,
)
from app.core.event_handler import EventHandler
from app.core.service_coordinator import ServiceCoordinator
from app.core.state_manager import IslandState, IslandStateManager
from app.core.time_display_manager import TimeDisplayManager
from app.core.timer_manager import TimerManager
from app.core.ui_builder import IslandUIBuilder
from app.services.tray import TrayService


class ModernIsland(QWidget):
    """带展开式控制面板的现代化灵动岛小部件。

    整合各个功能模块，提供时间显示、亮度控制、系统状态显示、
    剪贴板URL检测等功能。
    """

    def __init__(self):
        super().__init__()
        self._init_managers()
        self._init_window()
        self._init_ui()
        self._init_timers()
        self._load_styles()
        self._register_state_callbacks()
        self.workshop_manager = WorkshopManager(self)
        self.workshop_ui = None

    def _init_managers(self):
        self.state_manager = IslandStateManager()
        self.timer_manager = TimerManager(self)
        self.service_coordinator = ServiceCoordinator()
        self.animation_manager = None
        self.animation_controller = None
        self.event_handler = None
        self.time_display_manager = None
        self.tray_service = TrayService()

    def _init_window(self):
        self.setWindowFlags(
            Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
        )
        self.setAttribute(Qt.WA_TranslucentBackground)
        self.setAttribute(Qt.WA_ShowWithoutActivating, False)

        self.screen_w = QApplication.primaryScreen().size().width()
        self.screen_h = QApplication.primaryScreen().size().height()
        self.max_expand_h = self.screen_h // MAX_EXPAND_HEIGHT_RATIO

        self.col_rect = QRect(
            (self.screen_w - COLLAPSED_WIDTH) // 2, 20,
            COLLAPSED_WIDTH, COLLAPSED_HEIGHT
        )
        self.setGeometry(self._clamp_rect_to_screen(self.col_rect))

        self.setMouseTracking(True)
        QApplication.instance().focusChanged.connect(self._on_focus_changed)

    def _get_current_screen(self):
        """获取灵动岛当前所在的屏幕（支持多屏幕）。"""
        center = self.geometry().center()
        screen = QApplication.screenAt(center)
        if screen is None:
            screen = QApplication.primaryScreen()
        return screen

    def _clamp_rect_to_screen(self, rect: QRect) -> QRect:
        """将矩形限制在当前屏幕边界内，防止超出屏幕。"""
        screen_geo = self._get_current_screen().geometry()
        x = max(screen_geo.left(), min(rect.left(), screen_geo.right() - rect.width()))
        y = max(screen_geo.top(), min(rect.top(), screen_geo.bottom() - rect.height()))
        return QRect(x, y, rect.width(), rect.height())

    def _clamp_position(self):
        """将当前窗口位置限制在屏幕边界内。"""
        current = self.geometry()
        clamped = self._clamp_rect_to_screen(current)
        if clamped.topLeft() != current.topLeft():
            self.move(clamped.topLeft())

    def _start_drag_idle_timer(self):
        """停止闲置计时器（用户重新开始拖拽时调用）。"""
        if getattr(self, "_drag_idle_timer", None) and self._drag_idle_timer.isActive():
            self._drag_idle_timer.stop()

    def _snap_to_top(self):
        """拖拽释放时吸附到屏幕顶部，只露出 VISIBLE_TOP_BORDER 像素的黑边。"""
        if self._is_dragging:
            return
        self._is_docked = True
        self._snap_preview = False
        current = self.geometry()
        screen_geo = self._get_current_screen().geometry()
        dock_y = screen_geo.top() - current.height() + VISIBLE_TOP_BORDER
        target_x = (screen_geo.width() - current.width()) // 2 + screen_geo.left()
        end_rect = QRect(target_x, dock_y, current.width(), current.height())

        anim = QPropertyAnimation(self, b"geometry")
        anim.setDuration(DOCK_ANIMATION_DURATION)
        anim.setStartValue(current)
        anim.setEndValue(end_rect)
        anim.setEasingCurve(QEasingCurve.InOutCubic)
        anim.start()
        self._dock_animation = anim

    def _return_to_top(self):
        """拖拽结束后恢复屏幕上方的固定位置。"""
        if self._is_dragging or self._is_docked:
            return
        current_rect = self.geometry()
        screen_geo = self._get_current_screen().geometry()
        target_x = screen_geo.left() + (screen_geo.width() - current_rect.width()) // 2
        target_y = screen_geo.top() + 20
        target_rect = QRect(target_x, target_y, current_rect.width(), current_rect.height())

        anim = QPropertyAnimation(self, b"geometry")
        anim.setDuration(DOCK_ANIMATION_DURATION)
        anim.setStartValue(current_rect)
        anim.setEndValue(target_rect)
        anim.setEasingCurve(QEasingCurve.InOutCubic)
        anim.start()
        self._return_animation = anim

    def _return_to_default(self):
        """从吸附状态恢复屏幕上方的固定位置。"""
        if not self._is_docked:
            return
        self._is_docked = False
        current = self.geometry()
        screen_geo = self._get_current_screen().geometry()
        target_x = screen_geo.left() + (screen_geo.width() - current.width()) // 2
        target_y = screen_geo.top() + 20
        target_rect = QRect(target_x, target_y, current.width(), current.height())

        anim = QPropertyAnimation(self, b"geometry")
        anim.setDuration(DOCK_ANIMATION_DURATION)
        anim.setStartValue(current)
        anim.setEndValue(target_rect)
        anim.setEasingCurve(QEasingCurve.InOutCubic)
        anim.start()
        self._dock_animation = anim

    def _ensure_not_docked(self):
        """解除吸附状态（瞬移到正常位置），任何状态切换前调用。"""
        if not self._is_docked:
            return
        self._is_docked = False
        current = self.geometry()
        screen_geo = self._get_current_screen().geometry()
        target_x = screen_geo.left() + (screen_geo.width() - current.width()) // 2
        target_y = screen_geo.top() + 20
        self.setGeometry(QRect(target_x, target_y, current.width(), current.height()))

    def _init_ui(self):
        ui_builder = IslandUIBuilder(self)
        (
            self.container,
            self.time_label,
            self.date_label,
            self.controls,
            self.status_bar,
            self.bright_slider,
            self.bright_val
        ) = ui_builder.build()

        self.animation_manager = AnimationManager(self)
        self._icon_cache = ui_builder.get_icon_cache()
        self.current_brightness = 50

        self.animation_controller = AnimationController(
            self.animation_manager,
            self.container,
            self.controls,
            self._update_rounded_mask,
            self._update_time_display
        )

        self.time_display_manager = TimeDisplayManager(
            self.time_label,
            self.date_label,
            IslandUIBuilder.position_label_center
        )

        self.event_handler = EventHandler(
            self.toggle_island,
            self.state_manager.is_expanded
        )
        self._is_dragging = False
        self._drag_start_pos = None
        self._window_start_pos = None
        self._is_docked = False
        self._snap_preview = False
        self._docked_before_notification = False

        self.bright_slider.valueChanged.connect(self._on_brightness_slider_changed)
        self._update_rounded_mask()

    def _init_timers(self):
        self.timer_manager.create_timer(
            "time_update", TIME_UPDATE_INTERVAL, self._update_time_display
        )
        self.timer_manager.create_timer(
            "status_update", STATUS_UPDATE_INTERVAL, self._start_status_update
        )
        self.timer_manager.create_debounce_timer(
            "brightness_debounce", DEBOUNCE_DELAY, self._apply_brightness
        )
        self.timer_manager.create_timer(
            "clipboard_check", CLIPBOARD_CHECK_INTERVAL, self._check_clipboard
        )

        self._start_status_update()
        self._load_initial_brightness()

    def _load_styles(self):
        try:
            with open(STYLES_PATH, "r", encoding="utf-8") as f:
                self.setStyleSheet(f.read())
        except Exception:
            pass

    def _register_state_callbacks(self):
        self.state_manager.register_callback(
            IslandState.COLLAPSED, self._on_state_collapsed
        )
        self.state_manager.register_callback(
            IslandState.HOVERING, self._on_state_hovering
        )
        self.state_manager.register_callback(
            IslandState.EXPANDED, self._on_state_expanded
        )

    def _on_state_collapsed(self, data):
        self.time_display_manager.show_time_only()
        self.controls.hide()

    def _on_state_hovering(self, data):
        self.time_display_manager.update_for_hover()

    def _on_state_expanded(self, data):
        self.time_display_manager.show_date_only()

    def _load_initial_brightness(self):
        self.service_coordinator.load_initial_brightness(
            self._on_brightness_loaded
        )

    def _on_brightness_loaded(self, brightness):
        if brightness is not None:
            brightness = max(0, min(100, brightness))
            self.bright_slider.setValue(brightness)
            self.bright_val.setText(f"{brightness}%")
            self.current_brightness = brightness

    def _on_brightness_slider_changed(self, value):
        self.bright_val.setText(f"{value}%")
        self.current_brightness = value
        self.timer_manager.trigger_debounce("brightness_debounce")

    def _apply_brightness(self):
        self.service_coordinator.apply_brightness(self.current_brightness)

    def _start_status_update(self):
        self.service_coordinator.update_system_status(
            self._on_status_updated,
            self._on_status_error
        )

    def _on_status_error(self, error):
        print(f"状态更新失败: {error}")

    def _on_status_updated(self, result):
        ssid, signal, bt_name, bt_status, charge, status, dns_connected = \
            self.service_coordinator.process_status_update(result)

        self.status_bar.update_wifi(ssid, signal)
        self.status_bar.update_bluetooth(bt_name, bt_status)
        self.status_bar.update_battery(charge, status)

        wifi_msg, bt_msg, battery_msg = self.service_coordinator.check_status_changes(
            ssid, dns_connected, result[1], status
        )

        if wifi_msg:
            self._show_connection_animation(wifi_msg, "internet")
        elif bt_msg:
            self._show_connection_animation(bt_msg, "bluetooth")
        elif battery_msg:
            self._show_connection_animation(battery_msg, "battery")

    def _update_time_display(self):
        self.time_display_manager.update(
            self.state_manager.is_expanded(),
            self.state_manager.is_hovering()
        )

    def _check_clipboard(self):
        has_new, urls = self.service_coordinator.check_clipboard()
        if has_new:
            self._show_url_notification(urls)

    def _show_url_notification(self, urls: list):
        self._docked_before_notification = self._is_docked
        self._ensure_not_docked()
        if len(urls) == 1:
            self.controls.setCurrentIndex(1)
            url_single_page = self.controls.widget(1)
            url_single_page.build_single_url_page(
                urls[0],
                self._open_url_and_close,
                self._close_url_page
            )
            target_height = EXPANDED_HEIGHT
        else:
            self.controls.setCurrentIndex(2)
            url_multi_page = self.controls.widget(2)
            target_height = url_multi_page.build_multi_url_page(
                urls,
                self._open_selected_and_close,
                self._close_url_page
            )
            target_height = min(target_height, self.max_expand_h)

        self._expand_to_url_page(target_height)
        self.timer_manager.create_auto_close_timer(
            "url_auto_close", URL_AUTO_CLOSE_DELAY, self._close_url_page
        )

    def _expand_to_url_page(self, target_height: int):
        if self.state_manager.is_expanded():
            current_h = self.geometry().height()
            if current_h != target_height:
                self.animation_controller.animate_height_change(
                    current_h, target_height,
                    self.geometry().width(),
                    self._set_controls_height
                )
            # 隐藏时间标签
            self.time_label.hide()
            self.date_label.hide()
            return

        self.state_manager.set_state(IslandState.URL_DISPLAY)
        # 隐藏时间标签
        self.time_label.hide()
        self.date_label.hide()

        self.animation_controller.animate_url_expand(
            target_height,
            on_finished=lambda: self._set_controls_height(target_height)
        )

    def _set_controls_height(self, container_h: int):
        controls_h = AnimationController.calculate_controls_height(container_h)
        self.controls.setFixedHeight(controls_h)

    def _close_url_page(self):
        self.controls.setCurrentIndex(0)
        self.controls.setFixedHeight(
            AnimationController.calculate_controls_height(EXPANDED_HEIGHT)
        )
        if self.state_manager.is_expanded():
            self._collapse_from_url_page()
        else:
            # 恢复时间标签显示
            self.time_display_manager.show_time_only()
            self._update_time_display()
            self.time_label.show()
            if self._docked_before_notification:
                self._snap_to_top()

    def _collapse_from_url_page(self):
        current_pos = self.pos()

        self.state_manager.set_state(IslandState.COLLAPSED)
        self.time_display_manager.hide_all()

        def on_finished():
            self.time_display_manager.show_time_only()
            self._update_time_display()
            self.time_label.show()
            self._clamp_position()
            if self._docked_before_notification:
                self._snap_to_top()

        self.animation_controller.animate_collapse(
            self.geometry(),
            current_pos.x(),
            self.rect().width(),
            on_finished
        )

        self.controls.setCurrentIndex(0)

    def _open_url_and_close(self, url: str):
        self.service_coordinator.open_url(url)
        self._close_url_page()

    def _open_selected_and_close(self):
        url_multi_page = self.controls.widget(2)
        selected_urls = url_multi_page.get_selected_urls()
        if selected_urls:
            self.service_coordinator.open_urls(selected_urls)
        self._close_url_page()

    def _show_connection_animation(self, message: str, icon: str = "📶"):
        self._docked_before_notification = self._is_docked
        self._ensure_not_docked()
        self.timer_manager.stop_timer("time_update")

        if not self.state_manager.is_expanded():
            self._do_hover_and_show_connection(message, icon)
        else:
            self._update_connection_display(message, icon)

        self.timer_manager.create_auto_close_timer(
            "connection_auto_close",
            CONNECTION_AUTO_CLOSE_DELAY,
            self._close_connection_page
        )

    def _do_hover_and_show_connection(self, message: str, icon: str):
        self.state_manager.set_state(IslandState.HOVERING)

        self.time_label.hide()
        self.date_label.hide()

        def on_finished():
            from app.core.config import HOVER_WIDTH, HOVER_HEIGHT
            self.time_display_manager.show_connection_message(message, icon, HOVER_WIDTH, HOVER_HEIGHT)
            self.time_label.hide()
            self.date_label.hide()
            self._clamp_position()

        self.animation_controller.animate_hover(
            self.geometry(), True, self.screen_w, on_finished
        )

    def _update_connection_display(self, message: str, icon: str):
        self.time_label.hide()
        self.date_label.hide()
        from app.core.config import EXPANDED_WIDTH, EXPANDED_HEIGHT
        self.time_display_manager.show_connection_message(message, icon, EXPANDED_WIDTH, EXPANDED_HEIGHT)

    def _close_connection_page(self):
        if self.state_manager.is_hovering():
            self.state_manager.set_state(IslandState.COLLAPSED)
            
            self.time_label.hide()
            self.date_label.hide()
            was_timer_running = self.timer_manager.is_timer_active("time_update")
            
            def on_finished():
                self.time_display_manager.show_time_only()
                self._update_time_display()
                self.time_label.show()
                self.date_label.hide()
                if was_timer_running:
                    self.timer_manager.start_timer("time_update")
                self._clamp_position()
                if self._docked_before_notification:
                    self._snap_to_top()

            self.animation_controller.animate_hover(
                self.geometry(), False, self.screen_w, on_finished
            )
        else:
            self.time_display_manager.show_time_only()
            self.timer_manager.start_timer("time_update")
            self._update_time_display()
            if self._docked_before_notification:
                self._snap_to_top()

    def _update_rounded_mask(self):
        RoundedMaskHelper.update_mask(self)

    def mousePressEvent(self, event):
        self._start_drag_idle_timer()
        if event.button() == Qt.LeftButton:
            if self._is_docked:
                self._is_docked = False
            self._is_dragging = True
            self._drag_start_pos = event.globalPos()
            self._window_start_pos = self.frameGeometry().topLeft()
        else:
            self.event_handler.handle_mouse_press(
                event, self.frameGeometry().topLeft
            )

    def mouseMoveEvent(self, event):
        if self._is_dragging:
            delta = event.globalPos() - self._drag_start_pos
            raw_pos = self._window_start_pos + delta
            clamped_rect = self._clamp_rect_to_screen(
                QRect(raw_pos.x(), raw_pos.y(), self.width(), self.height())
            )
            self.move(clamped_rect.topLeft())
            # 拖拽到屏幕顶部附近时显示吸附预览
            screen_geo = self._get_current_screen().geometry()
            near_top = clamped_rect.top() - screen_geo.top() < SNAP_THRESHOLD
            if near_top != self._snap_preview:
                self._snap_preview = near_top
                self._update_rounded_mask()
        else:
            self.event_handler.handle_mouse_move(event, self.move)

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.LeftButton:
            should_toggle = (
                self._is_dragging and
                self._drag_start_pos is not None and
                (event.globalPos() - self._drag_start_pos).manhattanLength() < 5
            )
            was_snapping = self._snap_preview
            self._is_dragging = False
            self._drag_start_pos = None
            self._window_start_pos = None
            self._snap_preview = False
            if should_toggle:
                self.toggle_island()
            elif was_snapping:
                self._snap_to_top()
            else:
                self._start_return_to_top_timer()
        else:
            self.event_handler.handle_mouse_release(event, self.rect)

    def _start_return_to_top_timer(self):
        """用户停止拖拽后，经过延迟自动返回屏幕顶部。"""
        if not hasattr(self, "_drag_idle_timer"):
            self._drag_idle_timer = QTimer(self)
            self._drag_idle_timer.setSingleShot(True)
            self._drag_idle_timer.timeout.connect(self._return_to_top)
        self._drag_idle_timer.start(DRAG_IDLE_RETURN_DELAY)

    def enterEvent(self, event):
        super().enterEvent(event)
        if self._is_docked:
            self._return_to_default()
            return
        self.event_handler.handle_enter_event(
            self.state_manager.is_collapsed(),
            self._start_hover_animation
        )

    def leaveEvent(self, event):
        super().leaveEvent(event)
        self.event_handler.handle_leave_event(
            self.state_manager.is_collapsed() or self.state_manager.is_hovering(),
            self._start_hover_animation
        )

    def _start_hover_animation(self, is_enter: bool):
        if self._is_docked:
            return
        if is_enter:
            if not self.state_manager.is_collapsed():
                return
            self.state_manager.set_state(IslandState.HOVERING)
        else:
            if not self.state_manager.is_hovering():
                return
            self.state_manager.set_state(IslandState.COLLAPSED)

        self.time_label.hide()
        was_timer_running = self.timer_manager.is_timer_active("time_update")
        if was_timer_running:
            self.timer_manager.stop_timer("time_update")

        def on_finished():
            if is_enter:
                self.time_display_manager.update_for_hover()
            else:
                self._update_time_display()
            self.time_label.show()
            # 确保定时器总是运行，无论之前是否活跃
            self.timer_manager.start_timer("time_update")
            self._clamp_position()

        self.animation_controller.animate_hover(
            self.geometry(), is_enter, self.screen_w, on_finished
        )

    def _on_focus_changed(self, old_widget, new_widget):
        self.event_handler.handle_focus_change(
            old_widget, new_widget, lambda: self
        )

    def toggle_island(self):
        self._ensure_not_docked()
        current_pos = self.pos()

        if not self.state_manager.is_expanded():
            self._do_expand(current_pos)
        else:
            self._do_collapse(current_pos)

    def _do_expand(self, current_pos):
        self.state_manager.set_state(IslandState.EXPANDED)
        self.time_display_manager.hide_all()

        def on_finished():
            self.time_display_manager.show_date_only()
            self._update_time_display()
            self._clamp_position()

        self.animation_controller.animate_expand(
            self.geometry(),
            current_pos.x(),
            self.rect().width(),
            on_finished
        )

    def _do_collapse(self, current_pos):
        self.state_manager.set_state(IslandState.COLLAPSED)
        self.time_display_manager.hide_all()

        def on_finished():
            self.time_display_manager.show_time_only()
            self._update_time_display()
            self._clamp_position()

        self.animation_controller.animate_collapse(
            self.geometry(),
            current_pos.x(),
            self.rect().width(),
            on_finished
        )

        self.controls.setCurrentIndex(0)

    def eventFilter(self, obj, event):
        if event.type() == QEvent.Close and obj == getattr(self, '_url_dialog', None):
            self._url_dialog = None
            return True
        return super().eventFilter(obj, event)
    
    def mousePressEvent(self, event):
        self._start_drag_idle_timer()
        
        # 1. 保留原生逻辑：左键负责拖拽与解除吸附
        if event.button() == Qt.LeftButton:
            if self._is_docked:
                self._is_docked = False
            self._is_dragging = True
            self._drag_start_pos = event.globalPos()
            self._window_start_pos = self.frameGeometry().topLeft()
            
        # 2. 注入高维法则：中键（滚轮点击）专门唤醒创意工坊
        elif event.button() == Qt.MiddleButton:
            self.toggle_workshop()
            
        # 3. 保留原生逻辑：其他按键（或左键快速点击后在 release 中触发的逻辑）交由 event_handler 处理
        else:
            self.event_handler.handle_mouse_press(
                event, self.frameGeometry().topLeft
            )
    
    def toggle_workshop(self):
        """工坊界面的显隐逻辑"""
        if self.workshop_ui and self.workshop_ui.isVisible():
            self.workshop_ui.hide()
        else:
            if not self.workshop_ui:
                self.workshop_ui = WorkshopUI(self.workshop_manager)
            # 传递当前岛屿的几何位置以计算动画起点
            self.workshop_ui.show_animated(self.geometry())