import sys
import time
import json
import os
import gc
import asyncio
import ctypes
from PySide6.QtWidgets import (QApplication, QMainWindow, QSystemTrayIcon,
                               QMenu, QStyle)
from PySide6.QtGui import QAction, QIcon
from PySide6.QtWebEngineWidgets import QWebEngineView
from PySide6.QtCore import Qt, QUrl, QTimer, QPropertyAnimation, QEasingCurve, QRect, QThread, Signal, QObject, Slot
from PySide6.QtWebEngineCore import QWebEngineProfile, QWebEngineSettings, QWebEnginePage
import subprocess
import platform
import pyautogui
import time


# 导入你的检测工具类 (请确保路径正确)
try:
    from method.getbattery import BatteryChecker
    from method.getinternet import InternetChecker
    from method.sendtoast import send_startup_notification
    from method.health import start_health_reminders
    import windows_bluetooth_watcher as wbw
except ImportError:
    # 防止因缺少自定义模块导致演示代码无法运行
    class BatteryChecker:
        def check_battery(self): return "AC", 100


    class InternetChecker:
        def check_internet(self): return "已连接"


class StatusWorker(QThread):
    """后台状态检测线程 (保持不变)"""
    status_updated = Signal(dict)

    def __init__(self):
        super().__init__()
        self.battery_checker = BatteryChecker()
        self.internet_checker = InternetChecker()
        self.loop = asyncio.new_event_loop()
        self.running = True

    async def get_bt_status(self):
        try:
            listener = wbw.Listener()
            devices = await listener.get_all()
            connected_devices = [d.name for d in devices if d.status == "Connected"]
            return connected_devices if connected_devices else False
        except:
            return False

    def run(self):
        while self.running:
            bat_status, bat_level = self.battery_checker.check_battery()
            net_status = self.internet_checker.check_internet()
            try:
                bt_active = self.loop.run_until_complete(self.get_bt_status())
            except:
                bt_active = False

            data = {
                "battery": {
                    "level": bat_level if bat_level is not None else 100,
                    "status": bat_status if bat_status else "AC Power"
                },
                "wifi": "online" if "已连接" in net_status else "offline",
                "bluetooth": {
                    "status": "on" if bt_active else "off",
                    "devices": bt_active if isinstance(bt_active, list) else []
                }
            }
            self.status_updated.emit(data)
            time.sleep(3)

    def stop(self):
        self.running = False
        self.loop.close()


class PyIslandBridge(QObject):
    """JavaScript桥接对象，用于前端调用后端方法"""
    resizeSignal = Signal(int, int)
    @Slot(bool)
    def enableMousePassthrough(self, enabled=True):
        print("开启穿透" if enabled else "关闭穿透")
        window = QApplication.activeWindow()
        if not window:
            return
        if hasattr(window, "set_mouse_penetration"):
            window.set_mouse_penetration(enabled)
            return
        flags = window.windowFlags()
        if enabled:
            flags |= Qt.WindowTransparentForInput
        else:
            flags &= ~Qt.WindowTransparentForInput
        window.setWindowFlags(flags)
        window.show()

    @Slot(int,int)
    def resizeIsland(self,w,h):
        self.resizeSignal.emit(w,h)

    @Slot(bool)
    def collapseWindow(self):
        print("收起窗口")

    @Slot(str)
    def openWindowsSettings(self, setting_type):
        """打开Windows设置页面 / 通知中心

        Args:
            setting_type (str): 设置类型，如 'network', 'bluetooth', 'battery', 'notifications'
        """
        settings_map = {
            'network': 'ms-settings:network',
            'bluetooth': 'ms-settings:bluetooth',
            'battery': 'ms-settings:batterysaver',
            # notifications 不再跳设置页，下面会单独处理
        }

        # ====================== 核心修改：打开通知中心 ======================
        if setting_type == "notifications":
            try:
                # 判断系统版本
                win_ver = platform.release()

                if win_ver == "10":
                    # Win10：Win+A 打开通知中心
                    pyautogui.hotkey("win", "a")
                elif win_ver == "11":
                    # Win11：Win+N 打开通知中心
                    pyautogui.hotkey("win", "n")
                else:
                    print("不支持的Windows版本")

                time.sleep(0.1)
                return
            except Exception as e:
                print(f"打开通知中心失败: {e}")
                return
        # =================================================================

        # 其他设置（网络/蓝牙/电池）保持原来逻辑不变
        if setting_type in settings_map:
            try:
                subprocess.run(
                    ['explorer.exe', settings_map[setting_type]],
                    shell=True,
                    check=False,
                    creationflags=subprocess.CREATE_NO_WINDOW
                )
            except Exception as e:
                print(f"打开设置失败: {e}")
                try:
                    os.startfile(settings_map[setting_type])
                except Exception as e2:
                    print(f"后备方案也失败: {e2}")

                    if setting_type == 'battery':
                        try:
                            print("尝试打开电源设置作为替代")
                            subprocess.run(
                                ['explorer.exe', 'ms-settings:powersleep'],
                                shell=True,
                                check=False,
                                creationflags=subprocess.CREATE_NO_WINDOW
                            )
                        except Exception as e3:
                            print(f"打开电源设置也失败: {e3}")
                            try:
                                os.startfile('ms-settings:powersleep')
                            except:
                                pass

class IslandWindow(QMainWindow):
    def __init__(self):
        super().__init__()

        # 初始窗口属性：置顶、无边框、工具窗口（不在任务栏显示）
        self.base_flags = (
                Qt.FramelessWindowHint
                | Qt.WindowStaysOnTopHint
                | Qt.Tool
                | Qt.NoDropShadowWindowHint
        )
        self.setWindowFlags(self.base_flags)
        self.setAttribute(Qt.WA_TranslucentBackground)
        self.setAttribute(Qt.WA_NoSystemBackground, True)
        self.setAttribute(Qt.WA_TransparentForMouseEvents, True)
        self.setStyleSheet("background: transparent;")
        self.setContentsMargins(0, 0, 0, 0)

        # --- QWebEngineView 优化配置 ---
        self.web_view = QWebEngineView()
        self.web_view.setStyleSheet("background: transparent; border: none;")
        self.web_view.setContentsMargins(0, 0, 0, 0)
        settings = self.web_view.settings()
        settings.setAttribute(QWebEngineSettings.WebAttribute.PluginsEnabled, False)
        settings.setAttribute(QWebEngineSettings.WebAttribute.ShowScrollBars, False)

        profile = self.web_view.page().profile()
        profile.setHttpCacheType(QWebEngineProfile.HttpCacheType.MemoryHttpCache)
        profile.setHttpCacheMaximumSize(10 * 1024 * 1024)

        # 创建并设置JavaScript桥接对象
        from PySide6.QtWebChannel import QWebChannel
        self.bridge = PyIslandBridge()
        self.channel = QWebChannel()
        self.channel.registerObject('pyisland', self.bridge)
        self.web_view.page().setWebChannel(self.channel)
        self.bridge.resizeSignal.connect(lambda w, h: self.setFixedSize(w, h))

        self.setCentralWidget(self.web_view)
        self.web_view.setAttribute(Qt.WA_TranslucentBackground)
        self.web_view.page().setBackgroundColor(Qt.transparent)
        self.web_view.setContextMenuPolicy(Qt.NoContextMenu)

        # 尺寸配置
        self.fixed_width = 300
        self.height_small = 45
        self.height_large = 140
        self.is_expanded = False
        self.last_status = None

        self.animation = QPropertyAnimation(self, b"geometry")
        self.animation.setDuration(400)
        self.animation.setEasingCurve(QEasingCurve.OutCubic)

        # 加载 HTML
        # 获取资源文件路径，支持 PyInstaller 打包
        import sys
        if hasattr(sys, '_MEIPASS'):
            # 打包后的环境
            current_dir = sys._MEIPASS
        else:
            # 开发环境
            current_dir = os.path.dirname(os.path.abspath(__file__))
        island_html_path = os.path.join(current_dir, "assets", "island.html")
        self.web_view.load(QUrl.fromLocalFile(island_html_path))

        # 初始化系统托盘
        self.setup_tray()

        # 定时器与工作线程
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.refresh_time)
        self.timer.start(1000)

        self.worker = StatusWorker()
        self.worker.status_updated.connect(self.update_web_status)
        self.worker.start()

        QApplication.instance().aboutToQuit.connect(self.cleanup)

        self.update_geometry(self.height_large, animate=False)
        QTimer.singleShot(0, self.apply_native_window_fixes)

        # 发送启动通知
        send_startup_notification()

    def setup_tray(self):
        """配置系统托盘"""
        self.tray = QSystemTrayIcon(self)
        # 使用指定的SVG图标作为系统托盘图标
        # 获取资源文件路径，支持 PyInstaller 打包
        import sys
        if hasattr(sys, '_MEIPASS'):
            # 打包后的环境
            current_dir = sys._MEIPASS
        else:
            # 开发环境
            current_dir = os.path.dirname(os.path.abspath(__file__))
        icon_path = os.path.join(current_dir, "assets", "public", "svg", "pyisland.svg")
        if os.path.exists(icon_path):
            self.tray.setIcon(QIcon(icon_path))
        else:
            # 如果图标文件不存在，使用系统默认图标作为后备
            self.tray.setIcon(self.style().standardIcon(QStyle.SP_ComputerIcon))
            print(f"警告: 系统托盘图标文件不存在: {icon_path}")

        # 创建菜单
        tray_menu = QMenu()

        # 1. 鼠标穿透开关
        self.penetrate_action = QAction("开启鼠标穿透", self, checkable=True)
        self.penetrate_action.triggered.connect(self.set_mouse_penetration)
        tray_menu.addAction(self.penetrate_action)

        tray_menu.addSeparator()

        # 2. 退出
        exit_action = QAction("退出程序", self)
        exit_action.triggered.connect(QApplication.instance().quit)
        tray_menu.addAction(exit_action)

        self.tray.setContextMenu(tray_menu)
        self.tray.setToolTip("Python Island - 灵动岛")
        self.tray.show()

    def set_mouse_penetration(self, enabled):
        """控制窗口点击穿透"""
        if enabled:
            # 增加穿透标志
            self.setWindowFlags(self.base_flags | Qt.WindowTransparentForInput)
            # 开启穿透后，hover事件将失效，强制收起灵动岛
            self.reset_animation()
            self.penetrate_action.setText("关闭鼠标穿透")
        else:
            # 恢复原始标志
            self.setWindowFlags(self.base_flags)
            self.penetrate_action.setText("开启鼠标穿透")

        # 改变 Flags 后必须显式调用 show()，否则窗口会消失
        self.show()
        QTimer.singleShot(0, self.apply_native_window_fixes)

    def update_web_status(self, data):
        """处理状态变化及通知 (保持不变)"""
        if self.last_status is not None:
            if self.last_status.get('wifi') != data.get('wifi'):
                msg = '已连接到互联网' if data.get('wifi') == 'online' else '网络连接已断开'
                self.show_notification('网络连接', msg)

            curr_bt = data.get('bluetooth', {})
            last_bt = self.last_status.get('bluetooth', {})
            if last_bt.get('status') != curr_bt.get('status') or last_bt.get('devices') != curr_bt.get('devices'):
                if curr_bt.get('status') == 'on' and curr_bt.get('devices'):
                    devs = curr_bt.get('devices')
                    msg = f'已连接到: {devs[0]}' if len(devs) == 1 else f'已连接 {len(devs)} 个设备'
                    self.show_notification('蓝牙', msg)

        self.last_status = data
        json_str = json.dumps(data)
        self.web_view.page().runJavaScript(f"updateSystemStatus({json_str});")

    def show_notification(self, title, message):
        self.web_view.page().runJavaScript(f"showNotification('{title}', '{message}');")

    def refresh_time(self):
        t = time.strftime("%H:%M:%S")
        self.web_view.page().runJavaScript(f"updateTime('{t}');")

    def update_geometry(self, target_h, animate=True):
        screen = QApplication.primaryScreen().geometry()
        target_x = (screen.width() - self.fixed_width) // 2
        target_rect = QRect(target_x, 10, self.fixed_width, target_h)
        if animate:
            self.animation.stop()
            self.animation.setEndValue(target_rect)
            self.animation.start()
        else:
            self.setGeometry(target_rect)


    def focusOutEvent(self, event):
        # 失去焦点时收起
        self.reset_animation()
        # 确保事件被正确处理
        super().focusOutEvent(event)

    def reset_animation(self):
        if self.is_expanded:
            self.is_expanded = False
            self.update_geometry(self.height_small)
            gc.collect()
            QTimer.singleShot(500, self.deep_clean_engine)

    def deep_clean_engine(self):
        self.web_view.page().runJavaScript("if(window.gc) { window.gc(); }")



    def apply_native_window_fixes(self):
        if sys.platform != "win32":
            return

        hwnd = int(self.winId())
        dwmapi = getattr(ctypes.windll, "dwmapi", None)
        if dwmapi is None:
            return

        DWMWA_WINDOW_CORNER_PREFERENCE = 33
        DWMWA_BORDER_COLOR = 34
        DWMWCP_DONOTROUND = 1
        DWMWA_COLOR_NONE = 0xFFFFFFFE

        try:
            dwmapi.DwmSetWindowAttribute(
                hwnd,
                DWMWA_WINDOW_CORNER_PREFERENCE,
                ctypes.byref(ctypes.c_int(DWMWCP_DONOTROUND)),
                ctypes.sizeof(ctypes.c_int),
            )
            dwmapi.DwmSetWindowAttribute(
                hwnd,
                DWMWA_BORDER_COLOR,
                ctypes.byref(ctypes.c_uint(DWMWA_COLOR_NONE)),
                ctypes.sizeof(ctypes.c_uint),
            )
        except Exception:
            pass

    def cleanup(self):
        self.worker.stop()
        self.worker.wait()


if __name__ == "__main__":
    # 强制软件渲染，解决特定机型驱动冲突
    QApplication.setAttribute(Qt.AA_UseSoftwareOpenGL)

    # 性能与内存限制标志
    os.environ["QTWEBENGINE_CHROMIUM_FLAGS"] = (
        "--disable-gpu-compositing "
        "--renderer-process-limit=1 "
        "--disable-extensions "
        "--no-sandbox "
        "--js-flags='--max-old-space-size=128 --expose-gc'"
    )

    # 启动健康提醒
    try:
        start_health_reminders()
    except Exception as e:
        print(f"启动健康提醒失败: {e}")

    app = QApplication(sys.argv)
    # 统一设置应用退出时不自动关闭（防止主窗口隐藏时退出）
    app.setQuitOnLastWindowClosed(False)

    window = IslandWindow()
    window.show()
    sys.exit(app.exec())
