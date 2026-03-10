import webview
import threading
import time
import socket
import subprocess
import re
import win32api
import win32gui
import win32con
import os
import ctypes
import logging
from PIL import Image, ImageDraw
import pystray
import sys

# ─── 日志配置 ───
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[logging.StreamHandler()],
)
log = logging.getLogger("DynamicIsland")

# 引入 DWM API 用于强制透明
dwmapi = ctypes.WinDLL("dwmapi")


def get_resource_path(relative_path):
    """获取资源绝对路径，兼容开发环境和 PyInstaller 打包后的环境"""
    if hasattr(sys, '_MEIPASS'):
        return os.path.join(sys._MEIPASS, relative_path)
    return os.path.join(os.path.abspath("."), relative_path)


def _ensure_single_instance():
    """通过命名 Mutex 防止多开"""
    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    mutex = kernel32.CreateMutexW(None, False, "Global\\PythonDynamicIslandMutex")
    if ctypes.get_last_error() == 183:  # ERROR_ALREADY_EXISTS
        log.warning("检测到已有实例正在运行，退出")
        sys.exit(0)
    return mutex  # 必须保持引用，否则 mutex 被回收


class DynamicIsland:
    # 通知显示时长（秒），Python 端与 JS 端共用此值
    NOTICE_DURATION = 4

    def __init__(self):
        self.window = None
        self.width = 400
        self.height = 120
        self.is_active = False
        self._notify_lock = threading.Lock()
        self._is_notifying = False
        self.running = True
        self._window_ready = threading.Event()

        # 初始硬件状态
        self.was_online = self._check_internet()
        self.last_ssid = self._get_wifi_ssid()
        self.last_bt_devices = self._get_bt_devices()
        self.last_battery = self._get_battery_percent()

    # ─── 属性：线程安全的 is_notifying ───
    @property
    def is_notifying(self):
        with self._notify_lock:
            return self._is_notifying

    @is_notifying.setter
    def is_notifying(self, value):
        with self._notify_lock:
            self._is_notifying = value

    # ─── 安全调用 JS ───
    def _safe_eval_js(self, code: str):
        """等待窗口就绪后再调用 evaluate_js，避免崩溃"""
        if not self._window_ready.wait(timeout=10):
            log.warning("窗口未就绪，跳过 JS 调用: %s", code)
            return
        try:
            self.window.evaluate_js(code)
        except Exception as e:
            log.error("evaluate_js 失败: %s  code=%s", e, code)

    # ─── 探测逻辑 ───
    def _check_internet(self, host="8.8.8.8", port=53, timeout=2):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(timeout)
            s.connect((host, port))
            s.close()
            return True
        except OSError:
            return False

    def _get_wifi_ssid(self):
        try:
            out = subprocess.check_output(
                'netsh wlan show interfaces', shell=True,
                stderr=subprocess.DEVNULL
            ).decode('gbk', errors='ignore')
            # 使用 MULTILINE + 限定行首空白，避免匹配 BSSID 行
            match = re.search(r'^\s+SSID\s+:\s(.+)', out, re.MULTILINE)
            return match.group(1).strip() if match else None
        except Exception as e:
            log.debug("获取 WiFi SSID 失败: %s", e)
            return None

    def _get_bt_devices(self):
        try:
            ps_cmd = (
                'Get-PnpDevice -Class Bluetooth '
                '| Where-Object {$_.Status -eq "OK"} '
                '| Select-Object -ExpandProperty FriendlyName'
            )
            out = subprocess.check_output(
                ['powershell', '-NoProfile', '-Command', ps_cmd],
                stderr=subprocess.DEVNULL, timeout=8
            ).decode('gbk', errors='ignore')
            devices = [d.strip() for d in out.splitlines() if d.strip()]
            exclude = ['枚举器', 'Enumerator', 'Adapter', '适配器']
            return set(d for d in devices if not any(k in d for k in exclude))
        except Exception as e:
            log.debug("获取蓝牙设备失败: %s", e)
            return set()

    def _get_battery_percent(self):
        """获取电池电量百分比，台式机返回 None"""
        try:
            class SYSTEM_POWER_STATUS(ctypes.Structure):
                _fields_ = [
                    ('ACLineStatus', ctypes.c_byte),
                    ('BatteryFlag', ctypes.c_byte),
                    ('BatteryLifePercent', ctypes.c_byte),
                    ('SystemStatusFlag', ctypes.c_byte),
                    ('BatteryLifeTime', ctypes.c_ulong),
                    ('BatteryFullLifeTime', ctypes.c_ulong),
                ]
            status = SYSTEM_POWER_STATUS()
            ctypes.windll.kernel32.GetSystemPowerStatus(ctypes.byref(status))
            pct = status.BatteryLifePercent
            return pct if pct <= 100 else None
        except Exception:
            return None

    # ─── 线程任务 ───
    def hardware_monitor(self):
        while self.running:
            try:
                # 网络检测
                is_online = self._check_internet()
                current_ssid = self._get_wifi_ssid()

                if is_online and (not self.was_online or (current_ssid != self.last_ssid and current_ssid)):
                    self._notify_async(f"🌐 {current_ssid if current_ssid else '网络已连接'}")
                elif not is_online and self.was_online:
                    self._notify_async("📡 网络已断开")

                self.was_online, self.last_ssid = is_online, current_ssid

                # 蓝牙检测
                current_bt = self._get_bt_devices()
                new_bt = current_bt - self.last_bt_devices
                removed_bt = self.last_bt_devices - current_bt
                if new_bt:
                    self._notify_async(f"🎧 {list(new_bt)[0]} 已连接")
                elif removed_bt:
                    self._notify_async(f"🎧 {list(removed_bt)[0]} 已断开")
                self.last_bt_devices = current_bt

                # 电池检测
                pct = self._get_battery_percent()
                if pct is not None:
                    if self.last_battery is not None and self.last_battery > 20 and pct <= 20:
                        self._notify_async(f"🔋 电量不足 {pct}%")
                    self.last_battery = pct

            except Exception as e:
                log.error("hardware_monitor 异常: %s", e)

            time.sleep(5)

    def monitor_mouse(self):
        # 支持多显示器：获取虚拟屏幕参数
        vscreen_left = win32api.GetSystemMetrics(76)   # SM_XVIRTUALSCREEN
        vscreen_width = win32api.GetSystemMetrics(78)  # SM_CXVIRTUALSCREEN
        primary_width = win32api.GetSystemMetrics(0)
        center_x = primary_width // 2

        while self.running:
            try:
                if not self.is_notifying:
                    x, y = win32api.GetCursorPos()
                    in_zone = (center_x - 65 < x < center_x + 65) and (y < 5)
                    if in_zone and not self.is_active:
                        self._safe_eval_js("window.setExpand(true)")
                        self.is_active = True
                    elif y > 85 and self.is_active:
                        self._safe_eval_js("window.setExpand(false)")
                        self.is_active = False
                time.sleep(0.1)
            except Exception as e:
                log.debug("monitor_mouse 异常: %s", e)

    # ─── UI 辅助 ───
    def _notify_async(self, message):
        """在独立线程中发送通知，避免阻塞硬件检测循环"""
        threading.Thread(target=self.trigger_notification, args=(message,), daemon=True).start()

    def trigger_notification(self, message):
        if not self.window:
            return
        # 转义消息中的引号，防止 JS 注入/语法错误
        safe_msg = message.replace("\\", "\\\\").replace("'", "\\'")
        self.is_notifying = True
        self._safe_eval_js(f"window.showNotice('{safe_msg}')")
        self._safe_eval_js("window.setExpand(true)")
        time.sleep(self.NOTICE_DURATION)
        self.is_notifying = False
        try:
            _, y = win32api.GetCursorPos()
            if y > 25:
                self._safe_eval_js("window.setExpand(false)")
        except Exception:
            self._safe_eval_js("window.setExpand(false)")

    def fix_styles(self):
        """强制透明与隐藏任务栏图标"""
        time.sleep(1.5)
        hwnd = win32gui.FindWindow(None, 'DynamicIsland')
        if not hwnd:
            log.warning("未找到 DynamicIsland 窗口句柄")
            return
        # 隐藏任务栏 - 移除 WS_EX_APPWINDOW，添加 WS_EX_TOOLWINDOW
        ex_style = win32gui.GetWindowLong(hwnd, win32con.GWL_EXSTYLE)
        ex_style = (ex_style & ~win32con.WS_EX_APPWINDOW) | win32con.WS_EX_TOOLWINDOW
        win32gui.SetWindowLong(hwnd, win32con.GWL_EXSTYLE, ex_style)
        win32gui.SetWindowPos(
            hwnd, None, 0, 0, 0, 0,
            win32con.SWP_NOMOVE | win32con.SWP_NOSIZE | win32con.SWP_NOZORDER | win32con.SWP_FRAMECHANGED
        )
        # DWM 强制透明
        margins = (ctypes.c_int * 4)(-1, -1, -1, -1)
        dwmapi.DwmExtendFrameIntoClientArea(hwnd, margins)
        log.info("窗口样式已修复 (hwnd=%s)", hwnd)

    def create_tray(self):
        img = Image.new('RGBA', (64, 64), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        d.ellipse([10, 10, 54, 54], fill="white")

        def on_exit(icon, item):
            self.running = False
            icon.stop()
            if self.window:
                self.window.destroy()

        icon = pystray.Icon(
            "Island", img, "蟒蛇岛",
            pystray.Menu(pystray.MenuItem('退出', on_exit))
        )
        icon.run()


def on_window_ready(window, island):
    """当 webview 引擎完全加载后执行的操作"""
    # 1. 修复样式
    island.fix_styles()

    # 2. 标记窗口就绪
    island._window_ready.set()

    # 3. 启动逻辑监听
    threading.Thread(target=island.monitor_mouse, daemon=True).start()
    threading.Thread(target=island.hardware_monitor, daemon=True).start()

    # 4. 强制窗口显示并置顶
    hwnd = win32gui.FindWindow(None, 'DynamicIsland')
    if hwnd:
        win32gui.ShowWindow(hwnd, win32con.SW_SHOW)
        win32gui.SetWindowPos(
            hwnd, win32con.HWND_TOPMOST, 0, 0, 0, 0,
            win32con.SWP_NOMOVE | win32con.SWP_NOSIZE
        )
    log.info("灵动岛已启动")


def start_island():
    mutex = _ensure_single_instance()  # noqa: F841 - 必须保持引用

    island = DynamicIsland()
    html_path = get_resource_path('island.html')
    window = webview.create_window(
        'DynamicIsland', url=html_path, width=island.width, height=island.height,
        x=(win32api.GetSystemMetrics(0) - island.width) // 2, y=0,
        frameless=True, transparent=True, on_top=True, easy_drag=False,
        background_color='#000000'
    )
    island.window = window

    # 启动系统托盘线程
    threading.Thread(target=island.create_tray, daemon=True).start()

    # 将所有逻辑初始化交给 func 参数，确保浏览器内核启动后执行
    webview.start(func=on_window_ready, args=(window, island))


if __name__ == '__main__':
    start_island()