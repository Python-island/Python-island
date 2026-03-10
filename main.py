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
from PIL import Image, ImageDraw
import pystray
import sys

# 引入 DWM API 用于强制透明
dwmapi = ctypes.WinDLL("dwmapi")

def get_resource_path(relative_path):
    """ 获取资源绝对路径，兼容开发环境和 PyInstaller 打包后的环境 """
    if hasattr(sys, '_MEIPASS'):
        # PyInstaller 打包后的临时路径
        return os.path.join(sys._MEIPASS, relative_path)
    return os.path.join(os.path.abspath("."), relative_path)




class DynamicIsland:
    def __init__(self):
        self.window = None
        self.width = 400
        self.height = 120
        self.is_active = False
        self.is_notifying = False
        self.running = True

        # 初始硬件状态
        self.was_online = self._check_internet()
        self.last_ssid = self._get_wifi_ssid()
        self.last_bt_devices = self._get_bt_devices()

    # --- 探测逻辑 ---
    def _check_internet(self, host="8.8.8.8", port=53, timeout=2):
        try:
            socket.setdefaulttimeout(timeout)
            socket.socket(socket.AF_INET, socket.SOCK_STREAM).connect((host, port))
            return True
        except:
            return False

    def _get_wifi_ssid(self):
        try:
            out = subprocess.check_output('netsh wlan show interfaces', shell=True).decode('gbk')
            match = re.search(r'SSID\s+:\s(.*)\r', out)
            return match.group(1).strip() if match else None
        except:
            return None

    def _get_bt_devices(self):
        try:
            ps_cmd = 'Get-PnpDevice -Class Bluetooth | Where-Object {$_.Status -eq "OK"} | Select-Object -ExpandProperty FriendlyName'
            out = subprocess.check_output(['powershell', '-Command', ps_cmd], shell=True).decode('gbk')
            devices = [d.strip() for d in out.split('\r\n') if d.strip()]
            exclude = ['枚举器', 'Enumerator', 'Adapter', '适配器']
            return set([d for d in devices if not any(k in d for k in exclude)])
        except:
            return set()

    # --- 线程任务 ---
    def hardware_monitor(self):
        while self.running:
            # 网络检测
            is_online = self._check_internet()
            current_ssid = self._get_wifi_ssid()
            if is_online and (not self.was_online or (current_ssid != self.last_ssid and current_ssid)):
                self.trigger_notification(f"🌐 {current_ssid if current_ssid else '网络已连接'}")
            self.was_online, self.last_ssid = is_online, current_ssid

            # 蓝牙检测
            current_bt = self._get_bt_devices()
            new_bt = current_bt - self.last_bt_devices
            if new_bt:
                self.trigger_notification(f"🎧 {list(new_bt)[0]} 已连接")
            self.last_bt_devices = current_bt

            time.sleep(4)

    def monitor_mouse(self):
        screen_w = win32api.GetSystemMetrics(0)
        center_x = screen_w // 2
        while self.running:
            try:
                if not self.is_notifying:
                    x, y = win32api.GetCursorPos()
                    in_zone = (center_x - 65 < x < center_x + 65) and (y < 5)
                    if in_zone and not self.is_active:
                        self.window.evaluate_js("window.setExpand(true)")
                        self.is_active = True
                    elif y > 85 and self.is_active:
                        self.window.evaluate_js("window.setExpand(false)")
                        self.is_active = False
                time.sleep(0.1)
            except:
                pass

    # --- UI 辅助 ---
    def trigger_notification(self, message):
        if self.window:
            self.is_notifying = True
            self.window.evaluate_js(f"window.showNotice('{message}')")
            self.window.evaluate_js("window.setExpand(true)")
            time.sleep(5)
            self.is_notifying = False
            _, y = win32api.GetCursorPos()
            if y > 25: self.window.evaluate_js("window.setExpand(false)")

    def fix_styles(self):
        """强制透明与隐藏任务栏"""
        time.sleep(1.5)
        hwnd = win32gui.FindWindow(None, 'DynamicIsland')
        if hwnd:
            # 隐藏任务栏 - 移除WS_EX_APPWINDOW样式，添加WS_EX_TOOLWINDOW样式
            ex_style = win32gui.GetWindowLong(hwnd, win32con.GWL_EXSTYLE)
            ex_style = ex_style & ~win32con.WS_EX_APPWINDOW  # 移除应用窗口样式
            ex_style = ex_style | win32con.WS_EX_TOOLWINDOW  # 添加工具窗口样式
            win32gui.SetWindowLong(hwnd, win32con.GWL_EXSTYLE, ex_style)
            # 刷新窗口
            win32gui.SetWindowPos(hwnd, None, 0, 0, 0, 0, win32con.SWP_NOMOVE | win32con.SWP_NOSIZE | win32con.SWP_NOZORDER | win32con.SWP_FRAMECHANGED)
            # DWM 强制透明
            margins = (ctypes.c_int * 4)(-1, -1, -1, -1)
            dwmapi.DwmExtendFrameIntoClientArea(hwnd, margins)

    def create_tray(self):
        img = Image.new('RGBA', (64, 64), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        d.ellipse([10, 10, 54, 54], fill="white")  # 托盘用白色小点

        def on_exit(icon, item):
            self.running = False
            icon.stop()
            if self.window: self.window.destroy()

        icon = pystray.Icon("Island", img, "蟒蛇岛", pystray.Menu(pystray.MenuItem('退出', on_exit)))
        icon.run()


def on_window_ready(window, island):
    """当 webview 引擎完全加载后执行的操作"""
    # 1. 此时执行样式修复最为安全
    island.fix_styles()

    # 2. 启动逻辑监听
    threading.Thread(target=island.monitor_mouse, daemon=True).start()
    threading.Thread(target=island.hardware_monitor, daemon=True).start()

    # 3. 强制窗口显示并置顶 (防止被其他窗口压住)
    hwnd = win32gui.FindWindow(None, 'DynamicIsland')
    if hwnd:
        win32gui.ShowWindow(hwnd, win32con.SW_SHOW)
        win32gui.SetWindowPos(hwnd, win32con.HWND_TOPMOST, 0, 0, 0, 0,
                              win32con.SWP_NOMOVE | win32con.SWP_NOSIZE)


def start_island():
    island = DynamicIsland()
    # ... html_path 定义保持不变 ...
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

    # 关键改动：将所有逻辑初始化交给 func 参数
    # 这确保了代码在浏览器内核启动后才运行
    webview.start(func=on_window_ready, args=(window, island))


if __name__ == '__main__':
    start_island()