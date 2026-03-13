"""
现代化灵动岛 - Windows上的动态岛式小部件。
"""

import os
from datetime import datetime

from PySide6.QtCore import (
    QEvent,
    QThread,
    QEasingCurve,
    QPropertyAnimation,
    QPoint,
    QRect,
    Qt,
    QTimer,
    Signal,
)
from PySide6.QtGui import QPixmap
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QFrame,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSlider,
    QStackedWidget,
    QVBoxLayout,
    QWidget,
)

from app.utils import (
    extract_urls,
    get_all_status,
    get_clipboard_text,
    get_system_brightness,
    open_url,
    open_urls,
    set_brightness,
)


class WorkerThread(QThread):
    """后台工作线程，用于执行耗时操作。"""

    finished_signal = Signal(object)
    error_signal = Signal(str)

    def __init__(self, task_func, *args, **kwargs):
        super().__init__()
        self.task_func = task_func
        self.args = args
        self.kwargs = kwargs

    def run(self):
        try:
            result = self.task_func(*self.args, **self.kwargs)
            self.finished_signal.emit(result)
        except Exception as e:
            self.error_signal.emit(str(e))


class ModernIsland(QWidget):
    """带展开式控制面板的现代化灵动岛小部件。"""

    def __init__(self):
        super().__init__()
        self.setWindowFlags(
            Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
        )
        self.setAttribute(Qt.WA_TranslucentBackground)
        self.setAttribute(Qt.WA_ShowWithoutActivating, False)

        # 状态与尺寸
        self.is_expanded = False
        self.screen_w = QApplication.primaryScreen().size().width()
        self.col_rect = QRect((self.screen_w - 180) // 2, 20, 180, 40)
        self.exp_rect = QRect((self.screen_w - 360) // 2, 20, 360, 160)
        self.setGeometry(self.col_rect)

        # 拖动相关变量
        self.dragging = False
        self.drag_start_pos = None
        self.window_start_pos = None

        # 监听焦点变化
        QApplication.instance().focusChanged.connect(self.on_focus_changed)

        # 主容器
        self.container = QFrame(self)
        self.container.setObjectName("IslandContainer")
        self.container.setFixedSize(180, 40)

        self.layout = QVBoxLayout(self.container)
        self.layout.setContentsMargins(15, 0, 15, 0)

        # 1. 折叠态内容：时间
        self.time_label = QLabel("")
        self.time_label.setObjectName("TimeLabel")
        self.time_label.setAlignment(Qt.AlignCenter)
        self.time_label.setFixedHeight(40)

        # 1.5 展开态内容：日期+时间（与时间标签重叠）
        self.date_label = QLabel("")
        self.date_label.setObjectName("DateLabel")
        self.date_label.setAlignment(Qt.AlignCenter)
        self.date_label.setFixedHeight(40)
        self.date_label.hide()
        self.date_label.setParent(self.container)

        # 2. 展开态内容：使用 StackedWidget 管理多个页面
        self.controls = QStackedWidget()
        self.controls.hide()

        # 页面0: 控制面板
        self.ctrl_page = QWidget()
        self.ctrl_layout = QVBoxLayout(self.ctrl_page)
        self.ctrl_layout.setContentsMargins(5, 20, 5, 10)
        self.ctrl_layout.setSpacing(15)

        # 页面1: 单URL提示页面
        self.url_single_page = QWidget()
        self.url_single_layout = QVBoxLayout(self.url_single_page)
        self.url_single_layout.setContentsMargins(10, 15, 10, 15)
        self.url_single_layout.setSpacing(10)

        # 页面2: 多URL提示页面
        self.url_multi_page = QWidget()
        self.url_multi_layout = QVBoxLayout(self.url_multi_page)
        self.url_multi_layout.setContentsMargins(10, 15, 10, 15)
        self.url_multi_layout.setSpacing(10)

        # 将页面添加到 stacked widget
        self.controls.addWidget(self.ctrl_page)
        self.controls.addWidget(self.url_single_page)
        self.controls.addWidget(self.url_multi_page)

        # 创建亮度控制行
        # 先初始化图标缓存
        self._icon_cache = {}
        self._preload_icons()

        self.bright_row, self.bright_slider, self.bright_val = \
            self.create_ctrl_row("resources/icons/light.png", "亮度")

        # 绑定事件
        self.bright_slider.valueChanged.connect(
            lambda v: self.update_val(self.bright_val, v, "bright")
        )

        # 3. 状态栏
        self.status_bar = QWidget()
        self.status_layout = QHBoxLayout(self.status_bar)
        self.status_layout.setContentsMargins(10, 5, 10, 5)
        self.status_layout.setSpacing(15)

        # WiFi信息
        self.wifi_icon = QLabel()
        self.wifi_icon.setObjectName("IconLabel")
        if "resources/icons/internet.png" in self._icon_cache:
            self.wifi_icon.setPixmap(self._icon_cache["resources/icons/internet.png"])
        else:
            self.wifi_icon.setText("📶")

        self.wifi_label = QLabel("未连接")
        self.wifi_label.setObjectName("StatusLabel")

        # 蓝牙信息
        self.bluetooth_icon = QLabel()
        self.bluetooth_icon.setObjectName("IconLabel")
        if "resources/icons/bluetooth.png" in self._icon_cache:
            self.bluetooth_icon.setPixmap(self._icon_cache["resources/icons/bluetooth.png"])
        else:
            self.bluetooth_icon.setText("🔵")

        self.bluetooth_label = QLabel("未连接")
        self.bluetooth_label.setObjectName("StatusLabel")

        # 电池信息
        self.battery_icon = QLabel()
        self.battery_icon.setObjectName("IconLabel")
        if "resources/icons/battery.png" in self._icon_cache:
            self.battery_icon.setPixmap(self._icon_cache["resources/icons/battery.png"])
        else:
            self.battery_icon.setText("🔋")

        self.battery_label = QLabel("未知")
        self.battery_label.setObjectName("StatusLabel")

        # 创建WiFi组合
        wifi_layout = QHBoxLayout()
        wifi_layout.setSpacing(5)
        wifi_layout.addWidget(self.wifi_icon)
        wifi_layout.addWidget(self.wifi_label)

        # 创建蓝牙组合
        bluetooth_layout = QHBoxLayout()
        bluetooth_layout.setSpacing(5)
        bluetooth_layout.addWidget(self.bluetooth_icon)
        bluetooth_layout.addWidget(self.bluetooth_label)

        # 创建电池组合
        battery_layout = QHBoxLayout()
        battery_layout.setSpacing(5)
        battery_layout.addWidget(self.battery_icon)
        battery_layout.addWidget(self.battery_label)

        self.status_layout.addLayout(wifi_layout)
        self.status_layout.addLayout(bluetooth_layout)
        self.status_layout.addLayout(battery_layout)

        self.ctrl_layout.addLayout(self.bright_row)
        self.ctrl_layout.addWidget(self.status_bar)

        # 设置 stacked widget 高度
        self.controls.setFixedHeight(120)

        # 添加到主布局
        self.layout.addWidget(self.time_label)
        self.layout.addWidget(self.controls)

        # 时间更新定时器
        self.time_timer = QTimer(self)
        self.time_timer.timeout.connect(self.update_time)
        self.time_timer.start(1000)
        self.update_time()

        # 状态栏信息更新定时器
        self.status_timer = QTimer(self)
        self.status_timer.timeout.connect(self._start_status_update)
        self.status_timer.start(5000)
        self._start_status_update()

        # 亮度调节防抖计时器
        self.debounce_timer = QTimer(self)
        self.debounce_timer.setSingleShot(True)
        self.debounce_timer.timeout.connect(self._start_brightness_apply)
        self.current_brightness = 50

        # 剪贴板监听相关
        self._last_clipboard_text = ""
        self._clipboard_timer = QTimer(self)
        self._clipboard_timer.timeout.connect(self._check_clipboard)
        self._clipboard_timer.start(1500)  # 每1.5秒检查一次剪贴板

        # URL 对话框跟踪
        self._url_dialog = None        # 加载初始值（异步）
        self._start_initial_values_load()
        # 初始化亮度值
        self.current_brightness = 50

        self.load_qss()

    def _preload_icons(self):
        """预加载图标以提高性能。"""
        icon_files = ["resources/icons/light.png", "resources/icons/volume.png",
                     "resources/icons/internet.png", "resources/icons/bluetooth.png",
                     "resources/icons/battery.png"]
        for path in icon_files:
            if os.path.exists(path):
                pixmap = QPixmap(path)
                self._icon_cache[path] = pixmap.scaled(
                    20, 20, Qt.KeepAspectRatio, Qt.SmoothTransformation
                )

    def _start_initial_values_load(self):
        """异步加载初始值。"""
        # 创建线程获取亮度
        self._brightness_thread = WorkerThread(get_system_brightness)
        self._brightness_thread.finished_signal.connect(
            self._on_brightness_loaded
        )
        self._brightness_thread.start()

    def _on_brightness_loaded(self, brightness):
        """亮度加载完成回调。"""
        if brightness is not None:
            brightness = max(0, min(100, brightness))
            self.bright_slider.setValue(brightness)
            self.bright_val.setText(f"{brightness}%")
            self.current_brightness = brightness



    def _start_status_update(self):
        """启动异步状态更新。"""
        if hasattr(self, '_status_thread') and self._status_thread.isRunning():
            return
        self._status_thread = WorkerThread(get_all_status)
        self._status_thread.finished_signal.connect(self._on_status_updated)
        self._status_thread.error_signal.connect(self._on_status_error)
        self._status_thread.start()
    
    def _on_status_error(self, error):
        """状态更新错误回调。"""
        print(f"状态更新失败: {error}")
        # 可以在这里添加错误处理逻辑，例如显示错误通知

    def _on_status_updated(self, result):
        """状态更新完成回调。"""
        wifi_info, bluetooth_devices, battery_info = result

        ssid, signal, dns_connected = wifi_info
        if ssid and ssid != "未连接":
            self.wifi_label.setText(f"{ssid} ({signal})")
            # 检查DNS连接状态并显示通知
            if dns_connected and not hasattr(self, '_wifi_notification_shown'):
                self.show_notification_on_time("已连接到网络", "📶")
                self._wifi_notification_shown = True
        else:
            self.wifi_label.setText("未连接")
            # 重置通知标志
            if hasattr(self, '_wifi_notification_shown'):
                delattr(self, '_wifi_notification_shown')

        if bluetooth_devices:
            device_name, status = bluetooth_devices[0]
            self.bluetooth_label.setText(f"{device_name} ({status})")
            # 检查蓝牙连接状态并显示通知
            if status in ["已开启", "Connected", "已连接"] and not hasattr(self, '_bluetooth_notification_shown'):
                self.show_notification_on_time("蓝牙已连接", "🔵")
                self._bluetooth_notification_shown = True
        else:
            self.bluetooth_label.setText("未连接")
            # 重置通知标志
            if hasattr(self, '_bluetooth_notification_shown'):
                delattr(self, '_bluetooth_notification_shown')

        charge, status = battery_info
        if charge:
            self.battery_label.setText(f"{charge}% ({status})")
        else:
            self.battery_label.setText("未知")

    def create_ctrl_row(self, icon_path, label_text):
        """创建包含图标、标签、滑动条和数值控件的行。"""
        row = QHBoxLayout()
        row.setSpacing(12)

        # 图标（使用缓存或备用符号）
        icon = QLabel()
        icon.setObjectName("IconLabel")

        if icon_path in self._icon_cache:
            icon.setPixmap(self._icon_cache[icon_path])
        elif label_text == "亮度":
            icon.setText("\u0f0a0")
        else:
            icon.setText("\u0f05a")

        # 标签文本
        label = QLabel(label_text)
        label.setObjectName("ValueLabel")
        label.setFixedWidth(30)

        # 现代滑动条
        slider = QSlider(Qt.Horizontal)
        slider.setRange(0, 100)
        slider.setFixedHeight(32)
        slider.setObjectName("CapsuleSlider")
        slider.setFixedWidth(180)

        # 百分比数值
        val_label = QLabel("50%")
        val_label.setFixedWidth(40)
        val_label.setObjectName("ValueLabel")

        row.addWidget(icon)
        row.addWidget(label)
        row.addWidget(slider)
        row.addWidget(val_label)

        return row, slider, val_label

    def update_val(self, label, value, val_type):
        """更新数值标签并触发防抖应用。"""
        label.setText(f"{value}%")
        if val_type == "bright":
            self.current_brightness = value
            self.debounce_timer.stop()
            self.debounce_timer.start(180)

    def _start_brightness_apply(self):
        """异步应用亮度。"""
        if hasattr(self, '_brightness_apply_thread') and \
                self._brightness_apply_thread.isRunning():
            return
        self._brightness_apply_thread = WorkerThread(
            set_brightness, self.current_brightness
        )
        self._brightness_apply_thread.start()



    def mousePressEvent(self, event):
        """处理鼠标按下事件用于拖动。"""
        if event.button() == Qt.LeftButton:
            self.dragging = True
            self.drag_start_pos = event.globalPos()
            self.window_start_pos = self.frameGeometry().topLeft()

    def mouseMoveEvent(self, event):
        """处理鼠标移动事件用于拖动。"""
        if self.dragging:
            delta = event.globalPos() - self.drag_start_pos
            self.move(self.window_start_pos + delta)

    def mouseReleaseEvent(self, event):
        """处理鼠标释放事件 - 点击时切换，结束时停止拖动。"""
        if event.button() == Qt.LeftButton:
            if self.dragging and \
                    (event.globalPos() - self.drag_start_pos).manhattanLength() < 5:
                self.toggle_island()
            self.dragging = False

    def on_focus_changed(self, old_widget, new_widget):
        """失去焦点时自动收缩。"""
        if self.is_expanded:
            current_widget = new_widget
            while current_widget:
                if current_widget == self:
                    return
                current_widget = current_widget.parent()
            self.toggle_island()

    def toggle_island(self):
        """在展开和折叠状态之间切换"""
        # 清理旧动画
        if hasattr(self, 'ani') and self.ani:
            self.ani.stop()
            self.ani.deleteLater()

        current_pos = self.pos()

        if not self.is_expanded:
            # 立即标记为展开状态，防止重复触发
            self.is_expanded = True

            # 展开动画 - 立即隐藏时间，动画结束后显示日期+时间
            self.time_label.hide()
            self.date_label.hide()

            self.ani = QPropertyAnimation(self, b"geometry")
            self.ani.setDuration(200)
            self.ani.setEasingCurve(QEasingCurve.InOutCubic)

            start = self.geometry()
            end = QRect(
                current_pos.x(), current_pos.y(),
                360, 160
            )
            self.ani.setStartValue(start)
            self.ani.setEndValue(end)

            # 动画结束后显示日期+时间（居中显示）
            self.ani.finished.connect(lambda: (
                self.date_label.show(),
                self.update_time_display()
            ))

            # 切换显示内容
            self.controls.show()
            self.container.setFixedSize(360, 160)

            self.ani.start()

        else:
            # 立即标记为折叠状态
            self.is_expanded = False

            # 折叠动画 - 立即隐藏日期，动画结束后显示时间
            self.date_label.hide()
            self.time_label.hide()

            self.ani = QPropertyAnimation(self, b"geometry")
            self.ani.setDuration(350)
            self.ani.setEasingCurve(QEasingCurve.InOutCubic)

            start = self.geometry()
            end = QRect(
                current_pos.x(), current_pos.y(),
                180, 40
            )
            self.ani.setStartValue(start)
            self.ani.setEndValue(end)

            # 切换显示内容 - 先切回控制面板页面
            self.controls.setCurrentWidget(self.ctrl_page)
            self.controls.hide()

            # 动画结束后显示时间并调整容器大小
            self.ani.finished.connect(lambda: (
                self.time_label.show(),
                self.update_time_display(),
                self.container.setFixedSize(180, 40)
            ))

            self.ani.start()

    def update_time(self):
        """更新时间显示。"""
        self.update_time_display()

    def update_time_display(self):
        """根据展开/收起状态更新时间或日期显示。"""
        current_time = datetime.now().strftime("%H:%M")
        current_date = datetime.now().strftime("%m/%d")

        self.time_label.setText(current_time)
        self.date_label.setText(f"{current_date} {current_time}")

        # 如果已展开，日期标签居中显示
        if self.is_expanded:
            # 创建临时标签获取宽度
            temp_label = QLabel(f"{current_date} {current_time}")
            temp_label.setObjectName("DateLabel")
            temp_label.setStyleSheet(self.date_label.styleSheet())
            temp_label.setFont(self.date_label.font())
            temp_label.adjustSize()
            width = temp_label.width()
            x = (360 - width) // 2
            self.date_label.setFixedWidth(width)
            self.date_label.move(x, 0)

    def show_notification_on_time(self, message, icon="📶"):
        """在灵动岛的时间位置显示通知，然后过几秒再恢复时间显示。"""
        # 保存当前时间文本，用于稍后恢复
        if hasattr(self, 'time_label'):
            self._original_time_text = self.time_label.text()
            
            # 暂停时钟更新定时器
            if hasattr(self, 'time_timer'):
                self.time_timer.stop()
            
            # 显示通知内容
            self.time_label.setText(f"{icon} {message}")
            
            # 设置定时器，2秒后恢复时间显示
            self._notification_timer = QTimer(self)
            self._notification_timer.setSingleShot(True)
            self._notification_timer.timeout.connect(self._restore_time_display)
            self._notification_timer.start(2000)  # 2秒后恢复
    
    def _restore_time_display(self):
        """恢复时间显示。"""
        if hasattr(self, 'time_label') and hasattr(self, '_original_time_text'):
            # 恢复时间显示
            self.update_time()  # 直接更新到当前时间
            # 恢复时钟更新定时器
            if hasattr(self, 'time_timer'):
                self.time_timer.start(1000)
            # 清理临时属性
            if hasattr(self, '_original_time_text'):
                delattr(self, '_original_time_text')
            if hasattr(self, '_notification_timer'):
                delattr(self, '_notification_timer')

    def eventFilter(self, obj, event):
        """事件过滤器，用于处理对话框关闭事件。"""
        if event.type() == QEvent.Close and obj == self._url_dialog:
            self._url_dialog = None
            return True
        return super().eventFilter(obj, event)

    def _check_clipboard(self):
        """检查剪贴板是否有新的 URL。"""
        current_text = get_clipboard_text()
        if not current_text:
            return

        # 提取 URL
        urls = extract_urls(current_text)
        if not urls:
            # 无 URL 时重置剪贴板记录，允许重复检测非 URL 内容
            self._last_clipboard_text = current_text
            return

        # 有 URL 时，检查是否与上次检测到的 URL 相同
        if current_text == self._last_clipboard_text:
            return

        self._last_clipboard_text = current_text

        # 显示通知让用户选择
        self._show_url_notification(urls)

    def _show_url_notification(self, urls: list):
        """显示 URL 通知在灵动岛内部。"""
        if len(urls) == 1:
            # 单个 URL - 切换到单URL页面
            self.controls.setCurrentWidget(self.url_single_page)
            self._build_single_url_page(urls[0])
        else:
            # 多个 URL - 切换到多URL页面
            self.controls.setCurrentWidget(self.url_multi_page)
            self._build_multi_url_page(urls)

        # 展开灵动岛
        self._expand_to_url_page()

        # 5秒后自动关闭
        if hasattr(self, '_url_auto_close_timer') and self._url_auto_close_timer.isActive():
            self._url_auto_close_timer.stop()

        self._url_auto_close_timer = QTimer(self)
        self._url_auto_close_timer.setSingleShot(True)
        self._url_auto_close_timer.timeout.connect(self._close_url_page)
        self._url_auto_close_timer.start(5000)

    def _build_single_url_page(self, url: str):
        """构建单个 URL 的页面。"""
        # 清空之前的内容
        while self.url_single_layout.count():
            item = self.url_single_layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()

        # 标题
        title = QLabel("检测到链接")
        title.setObjectName("DialogTitle")
        self.url_single_layout.addWidget(title)

        # URL 显示
        url_label = QLabel(url[:45] + "..." if len(url) > 45 else url)
        url_label.setObjectName("UrlLabel")
        url_label.setWordWrap(True)
        self.url_single_layout.addWidget(url_label)

        # 按钮区域
        btn_layout = QHBoxLayout()
        btn_layout.setSpacing(10)

        cancel_btn = QPushButton("忽略")
        cancel_btn.setObjectName("DialogButton")
        cancel_btn.clicked.connect(self._close_url_page)

        open_btn = QPushButton("打开链接")
        open_btn.setObjectName("DialogButton")
        open_btn.clicked.connect(lambda: self._open_url_and_close(url))

        btn_layout.addWidget(cancel_btn)
        btn_layout.addWidget(open_btn)
        self.url_single_layout.addLayout(btn_layout)

    def _build_multi_url_page(self, urls: list):
        """构建多个 URL 的选择页面。"""
        # 清空之前的内容
        while self.url_multi_layout.count():
            item = self.url_multi_layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()

        # 标题
        title = QLabel(f"检测到 {len(urls)} 个链接")
        title.setObjectName("DialogTitle")
        self.url_multi_layout.addWidget(title)

        # URL 列表（只显示前3个，超出提示）
        for i, url in enumerate(urls[:3]):
            url_text = url[:40] + "..." if len(url) > 40 else url
            url_label = QLabel(f"{i+1}. {url_text}")
            url_label.setObjectName("UrlLabel")
            url_label.setWordWrap(True)
            self.url_multi_layout.addWidget(url_label)

        if len(urls) > 3:
            more_label = QLabel(f"...还有 {len(urls) - 3} 个链接")
            more_label.setObjectName("StatusLabel")
            self.url_multi_layout.addWidget(more_label)

        # 按钮区域
        btn_layout = QHBoxLayout()
        btn_layout.setSpacing(10)

        ignore_btn = QPushButton("忽略")
        ignore_btn.setObjectName("DialogButton")
        ignore_btn.clicked.connect(self._close_url_page)

        open_all_btn = QPushButton("全部打开")
        open_all_btn.setObjectName("DialogButton")
        open_all_btn.clicked.connect(lambda: self._open_all_and_close(urls))

        btn_layout.addWidget(ignore_btn)
        btn_layout.addWidget(open_all_btn)
        self.url_multi_layout.addLayout(btn_layout)

    def _expand_to_url_page(self):
        """展开灵动岛。"""
        # 如果已经展开，直接返回
        if self.is_expanded:
            return

        # 未展开，先展开
        self._do_expand_and_show_url()

    def _do_expand_and_show_url(self):
        """执行展开动画并显示链接页面。"""
        # 获取当前位置
        current_pos = self.geometry().topLeft()

        # 展开动画 - 向两边展开
        self.time_label.hide()
        self.date_label.show()
        self.update_time_display()

        self.ani = QPropertyAnimation(self, b"geometry")
        self.ani.setDuration(200)
        self.ani.setEasingCurve(QEasingCurve.InOutCubic)

        start = QRect(
            current_pos.x() + 90, current_pos.y(),
            0, 40
        )
        end = QRect(
            current_pos.x(), current_pos.y(),
            360, 160
        )
        self.ani.setStartValue(start)
        self.ani.setEndValue(end)

        # 动画结束后显示控制区域
        self.ani.finished.connect(lambda: (
            self.controls.show(),
            self.container.setFixedSize(360, 160)
        ))

        self.ani.start()
        self.is_expanded = True

    def _close_url_page(self):
        """关闭链接页面，收起灵动岛。"""
        # 切换回控制面板页面
        self.controls.setCurrentWidget(self.ctrl_page)
        # 收起灵动岛
        if self.is_expanded:
            self.toggle_island()

    def _open_url_and_close(self, url: str):
        """打开 URL 并收起灵动岛。"""
        open_url(url)
        self._close_url_page()

    def _open_all_and_close(self, urls: list):
        """打开所有 URL 并收起灵动岛。"""
        open_urls(urls)
        self._close_url_page()

    def load_qss(self):
        """加载QSS样式表。"""
        with open("resources/styles/style.qss", "r", encoding="utf-8") as f:
            self.setStyleSheet(f.read())