
import os
from datetime import datetime

from PySide6.QtCore import (
    QEasingCurve,
    QPropertyAnimation,
    QRect,
    Qt,
    QTimer,
)
from PySide6.QtGui import QPixmap
from PySide6.QtWidgets import (
    QFrame,
    QHBoxLayout,
    QLabel,
    QSlider,
    QVBoxLayout,
    QWidget,
)

from app.utils import (
    get_all_status,
    get_system_brightness,
    get_system_volume,
    set_brightness,
    set_volume,
)


class ModernIsland(QWidget):

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
        self.layout.addWidget(self.time_label)

        # 2. 展开态内容：控制组
        self.controls = QWidget()
        self.controls.hide()
        self.ctrl_layout = QVBoxLayout(self.controls)
        self.ctrl_layout.setContentsMargins(5, 20, 5, 10)
        self.ctrl_layout.setSpacing(15)

        # 创建亮度与音量控制行
        # 先初始化图标缓存
        self._icon_cache = {}
        self._preload_icons()

        self.bright_row, self.bright_slider, self.bright_val = \
            self.create_ctrl_row("resources/icons/light.png", "亮度")
        self.volume_row, self.volume_slider, self.volume_val = \
            self.create_ctrl_row("resources/icons/volume.png", "音量")

        # 绑定事件
        self.bright_slider.valueChanged.connect(
            lambda v: self.update_val(self.bright_val, v, "bright")
        )
        self.volume_slider.valueChanged.connect(
            lambda v: self.update_val(self.volume_val, v, "volume")
        )

        # 3. 状态栏
        self.status_bar = QWidget()
        self.status_layout = QHBoxLayout(self.status_bar)
        self.status_layout.setContentsMargins(10, 5, 10, 5)
        self.status_layout.setSpacing(15)

        # WiFi信息
        self.wifi_label = QLabel("WiFi: 未连接")
        self.wifi_label.setObjectName("StatusLabel")

        # 蓝牙信息
        self.bluetooth_label = QLabel("蓝牙: 未连接")
        self.bluetooth_label.setObjectName("StatusLabel")

        # 电池信息
        self.battery_label = QLabel("电池: 未知")
        self.battery_label.setObjectName("StatusLabel")

        self.status_layout.addWidget(self.wifi_label)
        self.status_layout.addWidget(self.bluetooth_label)
        self.status_layout.addWidget(self.battery_label)

        self.ctrl_layout.addLayout(self.bright_row)
        self.ctrl_layout.addLayout(self.volume_row)
        self.ctrl_layout.addWidget(self.status_bar)
        self.layout.addWidget(self.controls)

        # 时间更新定时器
        self.time_timer = QTimer(self)
        self.time_timer.timeout.connect(self.update_time)
        self.time_timer.start(1000)
        self.update_time()

        # 状态栏信息更新定时器
        self.status_timer = QTimer(self)
        self.status_timer.timeout.connect(self.update_status)
        self.status_timer.start(5000)
        self.update_status()

        # 亮度调节防抖计时器
        self.debounce_timer = QTimer(self)
        self.debounce_timer.setSingleShot(True)
        self.debounce_timer.timeout.connect(self.apply_brightness)
        self.current_brightness = 50

        # 音量调节防抖计时器
        self.volume_debounce_timer = QTimer(self)
        self.volume_debounce_timer.setSingleShot(True)
        self.volume_debounce_timer.timeout.connect(self.apply_volume)
        self.current_volume = 50

        # 获取并设置系统当前亮度和音量
        self.set_initial_values()

        self.load_qss()

    def _preload_icons(self):
        """预加载图标以提高性能。"""
        icon_files = ["resources/icons/light.png", "resources/icons/volume.png"]
        for path in icon_files:
            if os.path.exists(path):
                pixmap = QPixmap(path)
                self._icon_cache[path] = pixmap.scaled(
                    20, 20, Qt.KeepAspectRatio, Qt.SmoothTransformation
                )

    def set_initial_values(self):
        """设置滑块初始值。"""
        brightness = get_system_brightness()
        self.bright_slider.setValue(brightness)
        self.bright_val.setText(f"{brightness}%")
        self.current_brightness = brightness

        volume = get_system_volume()
        self.volume_slider.setValue(volume)
        self.volume_val.setText(f"{volume}%")
        self.current_volume = volume

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
            self.debounce_timer.start(300)
        elif val_type == "volume":
            self.current_volume = value
            self.volume_debounce_timer.stop()
            self.volume_debounce_timer.start(300)

    def apply_brightness(self):
        """应用亮度更改到系统。"""
        set_brightness(self.current_brightness)

    def apply_volume(self):
        """应用音量更改到系统。"""
        set_volume(self.current_volume)

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
        """在展开和折叠状态之间切换。"""
        self.ani = QPropertyAnimation(self, b"geometry")
        self.ani.setDuration(450)
        self.ani.setEasingCurve(QEasingCurve.OutQuart)

        current_pos = self.pos()

        if not self.is_expanded:
            new_rect = QRect(
                current_pos.x(), current_pos.y(),
                self.exp_rect.width(), self.exp_rect.height()
            )
            self.ani.setEndValue(new_rect)
            self.time_label.hide()
            self.controls.show()
        else:
            new_rect = QRect(
                current_pos.x(), current_pos.y(),
                self.col_rect.width(), self.col_rect.height()
            )
            self.ani.setEndValue(new_rect)
            self.controls.hide()
            self.time_label.show()

        self.ani.valueChanged.connect(
            lambda g: self.container.setFixedSize(g.width(), g.height())
        )
        self.ani.start()
        self.is_expanded = not self.is_expanded

    def update_time(self):
        """更新时间显示。"""
        current_time = datetime.now().strftime("%H:%M")
        self.time_label.setText(current_time)

    def update_status(self):
        """使用统一批量查询更新状态栏信息。"""
        wifi_info, bluetooth_devices, battery_info = get_all_status()

        ssid, signal = wifi_info
        if ssid:
            self.wifi_label.setText(f"WiFi: {ssid} ({signal})")
        else:
            self.wifi_label.setText("WiFi: 未连接")

        if bluetooth_devices:
            device_name, status = bluetooth_devices[0]
            self.bluetooth_label.setText(f"蓝牙: {device_name} ({status})")
        else:
            self.bluetooth_label.setText("蓝牙: 未连接")

        charge, status = battery_info
        if charge:
            self.battery_label.setText(f"电池: {charge}% ({status})")
        else:
            self.battery_label.setText("电池: 未知")

    def load_qss(self):
        """加载QSS样式表。"""
        with open("resources/styles/style.qss", "r", encoding="utf-8") as f:
            self.setStyleSheet(f.read())
