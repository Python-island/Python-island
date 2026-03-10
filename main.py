import sys
from PySide6.QtWidgets import QApplication, QWidget, QVBoxLayout
from PySide6.QtCore import (
    Qt,
    QEvent,
    QPropertyAnimation,
    QEasingCurve,
    QRect,
    QUrl,
)  # 关键：添加QUrl导入
from PySide6.QtGui import QColor, QPainter, QBrush
from PySide6.QtWebEngineWidgets import QWebEngineView
from PySide6.QtWebEngineCore import QWebEngineSettings

import os  # 新增os模块导入

# 替换原有加载本地HTML的代码
# 获取当前脚本所在目录的绝对路径
script_dir = os.path.dirname(os.path.abspath(__file__))
# 拼接file.html的绝对路径
html_path = os.path.join(script_dir, "island.html")
# 加载本地HTML文件


class WebFloatingWidget(QWidget):
    def __init__(self):
        super().__init__()
        # 基础窗口配置（保留悬浮窗特性）
        self.setWindowFlags(
            Qt.FramelessWindowHint  # 无边框（自己画边框）
            | Qt.WindowStaysOnTopHint  # 置顶显示
            | Qt.Tool  # 无任务栏图标
        )
        self.setAttribute(Qt.WA_TranslucentBackground)  # 透明背景

        # 尺寸配置
        self.normal_size = (100, 30)  # 正常尺寸
        self.hover_size = (130, 40)  # 悬浮尺寸
        self.is_hovered = False

        # 初始化UI（自定义边框 + 网页内容）
        self.init_ui()

        # 动画配置
        self.size_animation = QPropertyAnimation(self, b"geometry")
        self.size_animation.setDuration(300)
        self.size_animation.setEasingCurve(QEasingCurve.InOutCubic)

        # 鼠标追踪
        self.setMouseTracking(True)

        # 初始位置（屏幕顶部居中）
        self.move_to_center_top()

    def init_ui(self):
        # 主布局（留出边框空间）
        main_layout = QVBoxLayout(self)
        main_layout.setContentsMargins(5, 5, 5, 5)  # 5px 边框间距

        # 1. 创建网页视图组件
        self.web_view = QWebEngineView()
        # 启用网页设置（支持JS、图片等）
        self.web_view.settings().setAttribute(
            QWebEngineSettings.JavascriptEnabled, True
        )
        self.web_view.settings().setAttribute(QWebEngineSettings.PluginsEnabled, True)

        # 加载网页（可选：本地HTML/远程URL）
        # 方式1：加载远程URL（比如百度）
        # self.web_view.load("https://www.baidu.com")
        # 方式2：加载本地HTML文件（取消注释使用）
        self.web_view.load(QUrl.fromLocalFile(html_path))
        # 方式3：直接加载HTML字符串（取消注释使用）
        # html_content = "<html><body><h1>内嵌网页内容</h1><p>自定义边框测试</p></body></html>"
        # self.web_view.setHtml(html_content)

        # 2. 将网页视图加入布局
        main_layout.addWidget(self.web_view)
        self.setLayout(main_layout)

        # 初始尺寸
        self.resize(*self.normal_size)

    def move_to_center_top(self):
        """窗口定位到屏幕顶部居中"""
        screen_width = QApplication.primaryScreen().geometry().width()
        target_x = int(screen_width / 2 - self.width() / 2)
        self.move(target_x, 0)

    def animate_size(self, target_size):
        """尺寸过渡动画"""
        if self.size_animation.state() == QPropertyAnimation.Running:
            self.size_animation.stop()

        # 计算目标位置（保持居中）
        screen_width = QApplication.primaryScreen().geometry().width()
        target_x = int(screen_width / 2 - target_size[0] / 2)

        # 设置动画
        self.size_animation.setStartValue(self.geometry())
        self.size_animation.setEndValue(
            QRect(target_x, 0, target_size[0], target_size[1])
        )
        self.size_animation.start()

    def enterEvent(self, event: QEvent):
        """鼠标进入 - 放大（带动画）"""
        if not self.is_hovered:
            self.is_hovered = True
            self.animate_size(self.hover_size)

    def leaveEvent(self, event: QEvent):
        """鼠标离开 - 缩小（带动画）"""
        if self.is_hovered:
            self.is_hovered = False
            self.animate_size(self.normal_size)

    def paintEvent(self, event):
        """自定义绘制边框（核心：自己控制边框样式）"""
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)  # 抗锯齿

        # 1. 绘制背景（半透明黑色）
        bg_brush = QBrush(QColor(0, 0, 0, 180))
        painter.setBrush(bg_brush)
        painter.setPen(Qt.NoPen)
        painter.drawRoundedRect(self.rect(), 10, 10)  # 圆角背景

        # 2. 绘制自定义边框（白色细边框）
        painter.setBrush(Qt.NoBrush)
        painter.setPen(QColor(255, 255, 255, 200))  # 白色半透明边框
        painter.drawRoundedRect(self.rect().adjusted(1, 1, -1, -1), 10, 10)


if __name__ == "__main__":
    app = QApplication(sys.argv)
    # 高DPI适配
    app.setAttribute(Qt.AA_EnableHighDpiScaling)
    app.setAttribute(Qt.AA_UseHighDpiPixmaps)

    # 创建并显示悬浮网页窗口
    floating_web = WebFloatingWidget()
    floating_web.show()

    sys.exit(app.exec())
