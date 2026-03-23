# app/ui/workshop_ui.py
from PySide6.QtCore import Qt, QPropertyAnimation, QEasingCurve, QRect
from PySide6.QtWidgets import QVBoxLayout, QHBoxLayout, QWidget
from qfluentwidgets import (
    ListWidget, PrimaryPushButton, PushButton, SubtitleLabel, 
    BodyLabel, isDarkTheme, setTheme, Theme
)
from qframelesswindow import FramelessWindow, StandardTitleBar, WindowEffect

class WorkshopUI(FramelessWindow):
    """
    创意工坊宏观管理界面（Fluent Design - 亚克力/云母高阶拟态）
    """
    def __init__(self, manager, parent=None):
        super().__init__(parent=parent)
        self.manager = manager
        
        # 强制深色模式，以彰显极客与深邃之美
        setTheme(Theme.DARK)
        
        # === 魔法核心：召唤 Windows 底层材质 ===
        self.windowEffect = WindowEffect(self)
        # 尝试开启 Windows 11 的 Mica (云母) 效果，若系统不支持则降级为 Acrylic (亚克力)
        self.windowEffect.setMicaEffect(self.winId(), isDarkMode=True)
        # 移除背景色，让系统底层的玻璃质感透射上来
        self.setStyleSheet("WorkshopUI { background: transparent; }")
        
        # 设置窗口属性：无边框、工具窗口、置顶
        self.setWindowFlags(Qt.WindowType.Tool | Qt.WindowType.WindowStaysOnTopHint | Qt.WindowType.FramelessWindowHint)
        
        # 配置标题栏（隐藏最大化/最小化，保留关闭按钮以备不时之需）
        self.setTitleBar(StandardTitleBar(self))
        self.titleBar.maxBtn.hide()
        self.titleBar.minBtn.hide()
        # 让标题栏自身也透明
        self.titleBar.setStyleSheet("background: transparent;")
        
        self.init_ui()
        
    def init_ui(self):
        self.resize(450, 360)
        
        # 主布局
        self.vBoxLayout = QVBoxLayout(self)
        self.vBoxLayout.setContentsMargins(24, 48, 24, 24) # 顶部留出 48px 给标题栏
        self.vBoxLayout.setSpacing(12)
        
        # 标题与副标题
        self.titleLabel = SubtitleLabel("✨ 创意工坊", self)
        self.descLabel = BodyLabel("维度扩展，重塑数字孤岛的无限可能", self)
        self.descLabel.setTextColor("#A0A0A0", "#A0A0A0")
        
        self.vBoxLayout.addWidget(self.titleLabel)
        self.vBoxLayout.addWidget(self.descLabel)
        self.vBoxLayout.addSpacing(8)

        # 高阶列表组件 (自带平滑滚动与 Fluent 悬停效果)
        self.list_widget = ListWidget(self)
        self.list_widget.setStyleSheet("""
            QListView {
                background-color: rgba(0, 0, 0, 40);
                border-radius: 8px;
                border: 1px solid rgba(255, 255, 255, 15);
            }
        """)
        self.vBoxLayout.addWidget(self.list_widget)

        # 底部按钮区
        self.btn_layout = QHBoxLayout()
        self.btn_layout.setSpacing(16)
        
        # 使用 Fluent-Widgets 提供的精致按钮
        self.btn_refresh = PushButton("感知周围阵列", self)
        self.btn_toggle = PrimaryPushButton("装载 / 剥离 核心", self)
        
        self.btn_layout.addWidget(self.btn_refresh)
        self.btn_layout.addWidget(self.btn_toggle)
        self.vBoxLayout.addLayout(self.btn_layout)

        # 神经元连接（信号槽）
        self.btn_refresh.clicked.connect(self.refresh_list)
        self.btn_toggle.clicked.connect(self.toggle_plugin)
        
        self.refresh_list()

    def refresh_list(self):
        self.list_widget.clear()
        plugins = self.manager.scan_plugins()
        for p in plugins:
            status = "  [🔵 运转中]" if p in self.manager.active_plugins else "  [⚪ 沉睡]"
            self.list_widget.addItem(f"{p}{status}")

    def toggle_plugin(self):
        item = self.list_widget.currentItem()
        if not item: return
        # 解析真正的插件名
        plugin_name = item.text().replace("  [🔵 运转中]", "").replace("  [⚪ 沉睡]", "")
        
        if plugin_name in self.manager.active_plugins:
            self.manager.unload_plugin(plugin_name)
        else:
            self.manager.load_plugin(plugin_name)
        self.refresh_list()

    def show_animated(self, island_rect):
        """带有贝塞尔曲线质感的缓动出现动画"""
        # 初始位置：在岛屿正下方，略微缩小且全透明
        start_rect = QRect(
            island_rect.center().x() - 225, 
            island_rect.bottom() - 10, 
            450, 360
        )
        # 终点位置：自然下落一段距离
        end_rect = QRect(
            island_rect.center().x() - 225, 
            island_rect.bottom() + 20, 
            450, 360
        )
        
        self.setGeometry(start_rect)
        self.setWindowOpacity(0.0)
        self.show()
        
        # 1. 位置下落动画
        self.anim_pos = QPropertyAnimation(self, b"geometry")
        self.anim_pos.setDuration(500)
        self.anim_pos.setStartValue(start_rect)
        self.anim_pos.setEndValue(end_rect)
        self.anim_pos.setEasingCurve(QEasingCurve.Type.OutExpo) # 极具动感的缓动曲线
        
        # 2. 透明度渐变动画
        self.anim_opacity = QPropertyAnimation(self, b"windowOpacity")
        self.anim_opacity.setDuration(400)
        self.anim_opacity.setStartValue(0.0)
        self.anim_opacity.setEndValue(1.0)
        self.anim_opacity.setEasingCurve(QEasingCurve.Type.InOutSine)
        
        self.anim_pos.start()
        self.anim_opacity.start()