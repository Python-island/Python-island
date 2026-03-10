from PyIsland.Configure import CONFIG_MANAGER
from PyIsland.EventBus.Bus import EventManager
from PyIsland.EventBus.EventDefine import EventCode
from PyIsland.EventBus.Monitor import AsyncMonitorThread
from PyIsland.Display.Container import CapsuleWidget

import ctypes
import time
import platform

from PyQt5.QtWidgets import (
    QApplication, QWidget, QLabel, QSystemTrayIcon, QMenu, QAction, QStyle
)
# noinspection PyUnresolvedReferences
from PyQt5.QtCore import (
    Qt, QThread, QTimer, QPropertyAnimation, QEasingCurve, QRect, pyqtProperty
)
from PyQt5.QtGui import (
    QFont, QColor, QPainter, QBrush, QFontMetrics
)


# noinspection PyAttributeOutsideInit, PyUnresolvedReferences
class DynamicIslandWindow(QWidget):
    def __init__(self):
        super().__init__()
        self.drag_pos = None
        self.is_locked = False
        self.is_click_through = False
        self.is_topmost = False
        self.is_notification_active = False
        self.is_hovered = False
        self.animation_running = False
        self.current_notification_font_size = CONFIG_MANAGER.CONTENT_FONT_SIZE_INIT

        self.event_manager = EventManager()
        self._subscribe_events()

        self.init_ui()
        self.init_tray()
        self.center_top()
        self.init_time_update()
        self.init_monitors()
        self.init_animations()

    def _subscribe_events(self):
        notification_events = [
            EventCode.NETWORK_RESTORE,
            EventCode.BLUETOOTH_CONNECT,
            EventCode.TEST_NETWORK,
            EventCode.TEST_BLUETOOTH
        ]
        for event_code in notification_events:
            self.event_manager.subscribe(event_code, self._handle_notification)

        self.event_manager.subscribe(EventCode.MOUSE_HOVER, self._handle_mouse_hover)
        self.event_manager.subscribe(EventCode.MOUSE_LEAVE, self._handle_mouse_leave)

    def init_ui(self):
        self.base_flags = Qt.FramelessWindowHint | Qt.Tool
        self.setWindowFlags(self.base_flags)
        self.setAttribute(Qt.WA_TranslucentBackground)
        self.resize(CONFIG_MANAGER.ISLAND_INIT_WIDTH, CONFIG_MANAGER.ISLAND_INIT_HEIGHT)

        self.capsule = CapsuleWidget(self)
        self.capsule.setGeometry(self.rect())
        self.capsule.setAttribute(Qt.WA_TransparentForMouseEvents, True)

        self.content_label = QLabel(self.capsule)
        self.content_label.setAlignment(Qt.AlignCenter)
        self.content_label.setTextInteractionFlags(Qt.NoTextInteraction)
        self.content_label.setStyleSheet(f"""
            QLabel {{
                color: white;
                font-family: "Microsoft YaHei", sans-serif;
                font-weight: 600;
                background: transparent;
            }}
        """)
        self.content_label.setGeometry(self.rect())
        self._set_content_font_size(CONFIG_MANAGER.CONTENT_FONT_SIZE_INIT)
        self.content_label.setWindowOpacity(1)

        self.interactive_zone = QWidget(self)
        self.interactive_zone.setGeometry(0, 0, CONFIG_MANAGER.ISLAND_INIT_WIDTH, 40)
        self.interactive_zone.setMouseTracking(True)
        self.setMouseTracking(True)

    def _set_content_font_size(self, size):
        font = self.content_label.font()
        font.setPointSize(size)
        font.setLetterSpacing(QFont.AbsoluteSpacing, 0)
        self.content_label.setFont(font)

    def _set_notification_font_style(self, color):
        font = self.content_label.font()
        font.setLetterSpacing(QFont.AbsoluteSpacing, 1)
        self.content_label.setFont(font)
        self.content_label.setStyleSheet(f"""
            QLabel {{
                color: {color};
                font-family: "Microsoft YaHei", sans-serif;
                font-weight: 600;
                background: transparent;
            }}
        """)

    def get_text_width(self, text, font_size):
        font = QFont(self.content_label.font())
        font.setPointSize(font_size)
        font.setLetterSpacing(QFont.AbsoluteSpacing, 1)  # 匹配通知的字间距
        metrics = QFontMetrics(font)
        return metrics.horizontalAdvance(text)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self.capsule.setGeometry(self.rect())
        self.content_label.setGeometry(self.rect())
        self.interactive_zone.setGeometry(0, 0, self.width(), 40)

    def init_tray(self):
        self.tray_icon = QSystemTrayIcon(self)
        icon = self.style().standardIcon(QStyle.SP_ComputerIcon)
        self.tray_icon.setIcon(icon)
        self.tray_icon.setToolTip("动态岛")

        self.tray_menu = QMenu()

        self.action_topmost = QAction("置顶窗口 (关)", self)
        self.action_topmost.setCheckable(True)
        self.action_topmost.triggered.connect(self.toggle_topmost)
        self.tray_menu.addAction(self.action_topmost)

        self.action_click_through = QAction("点击穿透 (关)", self)
        self.action_click_through.setCheckable(True)
        self.action_click_through.triggered.connect(self.toggle_click_through)
        self.tray_menu.addAction(self.action_click_through)

        self.action_lock = QAction("位置锁定 (关)", self)
        self.action_lock.setCheckable(True)
        self.action_lock.triggered.connect(self.toggle_lock)
        self.tray_menu.addAction(self.action_lock)

        self.tray_menu.addSeparator()

        self.action_test_network = QAction("测试网络通知", self)
        self.action_test_network.triggered.connect(
            lambda: self.event_manager.publish(EventCode.TEST_NETWORK)
        )
        self.tray_menu.addAction(self.action_test_network)

        self.action_test_bluetooth = QAction("测试蓝牙通知", self)
        self.action_test_bluetooth.triggered.connect(
            lambda: self.event_manager.publish(EventCode.TEST_BLUETOOTH)
        )
        self.tray_menu.addAction(self.action_test_bluetooth)

        self.tray_menu.addSeparator()
        action_quit = QAction("退出程序", self)
        action_quit.triggered.connect(QApplication.quit)
        self.tray_menu.addAction(action_quit)

        self.tray_icon.setContextMenu(self.tray_menu)
        self.tray_icon.show()

    def init_time_update(self):
        self.time_timer = QTimer(self)
        self.time_timer.timeout.connect(self.update_time)
        self.time_timer.start(1000)
        self.update_time()

    def init_monitors(self):
        self.async_monitor_thread = AsyncMonitorThread()
        self.async_monitor_thread.start()

    def init_animations(self):
        self.size_animation = QPropertyAnimation(self, b"geometry")
        easing_curve = QEasingCurve(QEasingCurve.OutBack)
        easing_curve.setOvershoot(1.275)
        self.size_animation.setEasingCurve(easing_curve)
        self.size_animation.setDuration(CONFIG_MANAGER.ANIMATION_DURATION)

        self.radius_animation = QPropertyAnimation(self.capsule, b"radius")
        self.radius_animation.setEasingCurve(easing_curve)
        self.radius_animation.setDuration(CONFIG_MANAGER.ANIMATION_DURATION)

        self.font_size_animation = QPropertyAnimation(self, b"content_font_size")
        self.font_size_animation.setEasingCurve(easing_curve)
        self.font_size_animation.setDuration(CONFIG_MANAGER.ANIMATION_DURATION)

        self.opacity_animation = QPropertyAnimation(self.content_label, b"windowOpacity")
        self.opacity_animation.setDuration(CONFIG_MANAGER.CONTENT_ANIMATION_DURATION)
        self.opacity_animation.setEasingCurve(QEasingCurve.InOutQuad)

    def get_content_font_size(self):
        return self.content_label.font().pointSize()

    def set_content_font_size(self, size):
        font = self.content_label.font()
        font.setPointSize(size)
        self.content_label.setFont(font)

    content_font_size = pyqtProperty(int, get_content_font_size, set_content_font_size)

    def center_top(self):
        screen = QApplication.primaryScreen().availableGeometry()
        x = (screen.width() - CONFIG_MANAGER.ISLAND_INIT_WIDTH) // 2
        y = screen.top() + CONFIG_MANAGER.SCREEN_OFFSET_Y
        self.move(x, y)

    def update_time(self):
        if not self.is_notification_active:
            now = time.localtime()
            self.content_label.setText(time.strftime("%H:%M:%S", now))

    def _handle_notification(self, event_data):
        self.show_notification(event_data)

    def _handle_mouse_hover(self, _):
        if not self.is_notification_active and not self.is_click_through and not self.animation_running:
            self.is_hovered = True
            self.expand_capsule(animate_font=True)

    def _handle_mouse_leave(self, _):
        if not self.is_notification_active and self.is_hovered and not self.animation_running:
            self.is_hovered = False
            self.shrink_capsule(animate_font=True)

    def show_notification(self, event_data):
        if self.animation_running:
            return

        self.animation_running = True

        for timer_name in ['notification_timer', 'delay_timer', 'reset_delay_timer']:
            if hasattr(self, timer_name):
                getattr(self, timer_name).stop()

        self.is_notification_active = True
        self.content_label.setWindowOpacity(0)

        def show_content():
            icon = event_data.get("icon", "")
            text = event_data.get("text", "")
            color = event_data.get("color", "white")
            full_text = f"{icon} {text}" if icon else text

            self._set_content_font_size(CONFIG_MANAGER.CONTENT_FONT_SIZE_INIT)

            self._set_notification_font_style(color)
            self.content_label.setText(full_text)

            available_width = CONFIG_MANAGER.ISLAND_EXPAND_WIDTH - CONFIG_MANAGER.CAPSULE_PADDING
            target_font_size = CONFIG_MANAGER.CONTENT_FONT_SIZE_EXPAND
            text_width = self.get_text_width(full_text, target_font_size)

            if text_width > available_width:
                target_font_size = int(target_font_size * (available_width / text_width))

                target_font_size = max(target_font_size, CONFIG_MANAGER.CONTENT_FONT_SIZE_INIT)

            self.current_notification_font_size = target_font_size

            self.expand_capsule(animate_font=True, target_font_size=target_font_size)
            QTimer.singleShot(CONFIG_MANAGER.CONTENT_ANIMATION_DELAY, lambda: self._animate_content_opacity(0, 1))

        self.delay_timer = QTimer(self)
        self.delay_timer.singleShot(200, show_content)

        def shrink_and_reset():
            self.shrink_capsule(
                animate_font=True,
                start_font_size=self.current_notification_font_size
            )

            def final_reset():
                self.content_label.setStyleSheet(f"""
                    QLabel {{
                        color: white;
                        font-family: "Microsoft YaHei", sans-serif;
                        font-weight: 600;
                        background: transparent;
                    }}
                """)
                self._set_content_font_size(CONFIG_MANAGER.CONTENT_FONT_SIZE_INIT)
                self.is_notification_active = False
                self.update_time()
                self.content_label.setWindowOpacity(1)
                self.animation_running = False

            self.reset_delay_timer = QTimer(self)
            self.reset_delay_timer.singleShot(CONFIG_MANAGER.ANIMATION_DURATION, final_reset)

        self.notification_timer = QTimer(self)
        self.notification_timer.singleShot(CONFIG_MANAGER.NOTIFICATION_DURATION, shrink_and_reset)

    def _animate_content_opacity(self, start, end):
        self.opacity_animation.stop()
        self.opacity_animation.setStartValue(start)
        self.opacity_animation.setEndValue(end)
        self.opacity_animation.start()

    def expand_capsule(self, animate_font=True, target_font_size=None):
        if target_font_size is None:
            target_font_size = CONFIG_MANAGER.CONTENT_FONT_SIZE_EXPAND

        self.size_animation.stop()
        self.radius_animation.stop()
        if animate_font:
            self.font_size_animation.stop()

        start_geo = self.geometry()
        center_x = start_geo.center().x()
        top_y = start_geo.top()

        end_geo = QRect(
            int(center_x - CONFIG_MANAGER.ISLAND_EXPAND_WIDTH / 2),
            top_y,
            CONFIG_MANAGER.ISLAND_EXPAND_WIDTH,
            CONFIG_MANAGER.ISLAND_EXPAND_HEIGHT
        )

        self.size_animation.setStartValue(start_geo)
        self.size_animation.setEndValue(end_geo)
        self.size_animation.start()

        self.radius_animation.setStartValue(CONFIG_MANAGER.CAPSULE_INIT_RADIUS)
        self.radius_animation.setEndValue(CONFIG_MANAGER.CAPSULE_EXPAND_RADIUS)
        self.radius_animation.start()

        if animate_font:
            self.font_size_animation.setStartValue(CONFIG_MANAGER.CONTENT_FONT_SIZE_INIT)
            self.font_size_animation.setEndValue(target_font_size)
            self.font_size_animation.start()

    def shrink_capsule(self, animate_font=True, start_font_size=None):
        if start_font_size is None:
            start_font_size = CONFIG_MANAGER.CONTENT_FONT_SIZE_EXPAND

        self.size_animation.stop()
        self.radius_animation.stop()
        if animate_font:
            self.font_size_animation.stop()

        start_geo = self.geometry()
        center_x = start_geo.center().x()
        top_y = start_geo.top()

        end_geo = QRect(
            int(center_x - CONFIG_MANAGER.ISLAND_INIT_WIDTH / 2),
            top_y,
            CONFIG_MANAGER.ISLAND_INIT_WIDTH,
            CONFIG_MANAGER.ISLAND_INIT_HEIGHT
        )

        self.size_animation.setStartValue(start_geo)
        self.size_animation.setEndValue(end_geo)
        self.size_animation.start()

        self.radius_animation.setStartValue(CONFIG_MANAGER.CAPSULE_EXPAND_RADIUS)
        self.radius_animation.setEndValue(CONFIG_MANAGER.CAPSULE_INIT_RADIUS)
        self.radius_animation.start()

        if animate_font:
            self.font_size_animation.setStartValue(start_font_size)
            self.font_size_animation.setEndValue(CONFIG_MANAGER.CONTENT_FONT_SIZE_INIT)
            self.font_size_animation.start()

    def toggle_topmost(self, checked):
        self.is_topmost = checked
        is_visible = self.isVisible()

        if checked:
            self.setWindowFlags(self.base_flags | Qt.WindowStaysOnTopHint)
        else:
            self.setWindowFlags(self.base_flags)

        self.setAttribute(Qt.WA_TranslucentBackground)
        if is_visible:
            self.show()

        self.action_topmost.setText(f"置顶窗口 ({'开' if checked else '关'})")

    def toggle_click_through(self, checked):
        self.is_click_through = checked
        self.setAttribute(Qt.WA_TransparentForMouseEvents, checked)

        if platform.system() == "Windows":
            self.set_windows_click_through(checked)

        self.action_click_through.setText(f"点击穿透 ({'开' if checked else '关'})")

    def set_windows_click_through(self, enable):
        try:
            hwnd = int(self.winId())
            gwl_style = -20
            ws_ex_transparent = 0x00000020
            ws_ex_layered = 0x00080000

            ex_style = ctypes.windll.user32.GetWindowLongW(hwnd, gwl_style)

            if enable:
                ctypes.windll.user32.SetWindowLongW(hwnd, gwl_style,
                                                    ex_style | ws_ex_transparent | ws_ex_layered)
            else:
                ctypes.windll.user32.SetWindowLongW(hwnd, gwl_style,
                                                    (ex_style & ~ws_ex_transparent) | ws_ex_layered)
        except Exception as e:
            print(f"Windows API Error: {e}")

    def toggle_lock(self, checked):
        self.is_locked = checked
        self.action_lock.setText(f"位置锁定 ({'开' if checked else '关'})")

    def mousePressEvent(self, event):
        if self.is_click_through or self.is_locked:
            return
        if event.button() == Qt.LeftButton:
            self.drag_pos = event.globalPos() - self.frameGeometry().topLeft()
            event.accept()

    def mouseMoveEvent(self, event):
        if self.is_click_through or self.is_locked:
            return
        if event.buttons() == Qt.LeftButton and self.drag_pos:
            self.move(event.globalPos() - self.drag_pos)
            event.accept()

    def enterEvent(self, event):
        self.event_manager.publish(EventCode.MOUSE_HOVER)

    def leaveEvent(self, event):
        self.event_manager.publish(EventCode.MOUSE_LEAVE)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        painter.setBrush(QBrush(QColor(0, 0, 0, 0)))
        painter.setPen(Qt.NoPen)
        painter.drawRect(self.rect())
