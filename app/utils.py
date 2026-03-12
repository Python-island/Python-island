
import json
import subprocess

# 尝试导入亮度控制库
try:
    import screen_brightness_control as sbc
    brightness_available = True
except ImportError:
    brightness_available = False

# 尝试导入pycaw库用于音量控制
try:
    from pycaw.pycaw import AudioUtilities, IAudioEndpointVolume
    from comtypes import CLSCTX_ALL
    volume_available = True
except ImportError:
    volume_available = False

# 尝试导入Windows API用于模拟按键
try:
    import win32api
    import win32con
    import win32com.client
    import pythoncom
    windows_api_available = True
    volume_initialized = False
    volume_object = None
    mute_state = False
    current_volume = 0.5

    # 初始化音量控制
    try:
        pythoncom.CoInitialize()
        try:
            from pycaw.pycaw import AudioUtilities, IAudioEndpointVolume
            devices = AudioUtilities.GetSpeakers()
            endpoint = devices.EndpointVolume
            current_volume = endpoint.GetMasterVolumeLevelScalar()
            mute_state = endpoint.GetMute()
            volume_initialized = True
        except Exception:
            shell = win32com.client.Dispatch("WScript.Shell")
            volume_object = shell
            volume_initialized = True
    except Exception:
        volume_initialized = False
        volume_object = None
except ImportError:
    windows_api_available = False


def get_system_brightness():
    """获取系统当前亮度。"""
    if brightness_available:
        try:
            brightness = sbc.get_brightness()[0]
            return brightness
        except Exception:
            pass
    return 50


def set_brightness(value):
    """设置系统亮度。"""
    if brightness_available:
        try:
            sbc.set_brightness(value)
        except Exception:
            pass


def get_system_volume():
    """获取系统当前音量。"""
    if volume_available:
        try:
            devices = AudioUtilities.GetSpeakers()
            interface = devices.Activate(
                IAudioEndpointVolume._iid_, CLSCTX_ALL, None
            )
            volume = interface.QueryInterface(IAudioEndpointVolume)
            return int(volume.GetMasterVolumeLevelScalar() * 100)
        except Exception:
            pass

    try:
        cmd = "(Get-SoundVolume).VolumeLevel"
        result = subprocess.run(
            ["powershell", "-Command", cmd],
            capture_output=True, text=True, check=True
        )
        volume = int(result.stdout.strip())
        return max(0, min(100, volume))
    except Exception:
        pass

    if windows_api_available and volume_initialized:
        try:
            return int(current_volume * 100)
        except Exception:
            pass

    return 50


def set_volume(value):
    """设置系统音量。"""
    global current_volume

    if volume_available:
        try:
            devices = AudioUtilities.GetSpeakers()
            interface = devices.Activate(
                IAudioEndpointVolume._iid_, CLSCTX_ALL, None
            )
            volume = interface.QueryInterface(IAudioEndpointVolume)
            volume.SetMasterVolumeLevelScalar(value / 100, None)
            if windows_api_available:
                current_volume = value / 100.0
            return
        except Exception:
            pass

    try:
        cmd = f"Set-SoundVolume -VolumeLevel {value}"
        subprocess.run(
            ["powershell", "-Command", cmd],
            capture_output=True, text=True, check=True
        )
        if windows_api_available:
            current_volume = value / 100.0
        return
    except Exception:
        pass

    if windows_api_available and volume_initialized:
        try:
            if value == 0:
                win32api.keybd_event(win32con.VK_VOLUME_MUTE, 0, 0, 0)
                win32api.keybd_event(
                    win32con.VK_VOLUME_MUTE, 0, win32con.KEYEVENTF_KEYUP, 0
                )
            else:
                win32api.keybd_event(win32con.VK_VOLUME_MUTE, 0, 0, 0)
                win32api.keybd_event(
                    win32con.VK_VOLUME_MUTE, 0, win32con.KEYEVENTF_KEYUP, 0
                )
                steps = min(20, int(value / 5) + 1)
                for _ in range(steps):
                    win32api.keybd_event(win32con.VK_VOLUME_UP, 0, 0, 0)
                    win32api.keybd_event(
                        win32con.VK_VOLUME_UP, 0, win32con.KEYEVENTF_KEYUP, 0
                    )
            current_volume = value / 100.0
            return
        except Exception:
            pass


def get_all_status():
    """
    一次性获取所有状态信息（WiFi、蓝牙、电池）。

    返回:
        tuple: (wifi_info, bluetooth_devices, battery_info)
    """
    wifi_info = ("", "")
    bluetooth_devices = []
    battery_info = ("", "")

    try:
        script = '''
        $result = @{}
        try {
            $wifi = netsh wlan show interfaces | Select-String 'SSID', 'Signal'
            $ssid = ""
            $signal = ""
            foreach ($line in $wifi) {
                if ($line -match "SSID.*:") { $ssid = ($line -split ":")[-1].Trim() }
                if ($line -match "Signal.*:") { $signal = ($line -split ":")[-1].Trim() }
            }
            $result["wifi_ssid"] = $ssid
            $result["wifi_signal"] = $signal
        } catch {
            $result["wifi_ssid"] = ""
            $result["wifi_signal"] = ""
        }
        try {
            $bt = Get-PnpDevice -Class Bluetooth | Select-Object FriendlyName, Status
            $devices = @()
            foreach ($d in $bt) {
                if ($d.FriendlyName) { $devices += @($d.FriendlyName, $d.Status) }
            }
            $result["bluetooth"] = $devices
        } catch {
            $result["bluetooth"] = @()
        }
        try {
            $batt = Get-WmiObject -Class Win32_Battery | Select-Object EstimatedChargeRemaining, BatteryStatus
            if ($batt) {
                $result["battery_charge"] = $batt.EstimatedChargeRemaining
                $result["battery_status"] = $batt.BatteryStatus
            } else {
                $result["battery_charge"] = ""
                $result["battery_status"] = ""
            }
        } catch {
            $result["battery_charge"] = ""
            $result["battery_status"] = ""
        }
        $result | ConvertTo-Json -Compress
        '''
        result = subprocess.run(
            ["powershell", "-Command", script],
            capture_output=True, text=True, check=True, timeout=10
        )
        data = json.loads(result.stdout.strip())

        wifi_info = (data.get("wifi_ssid", ""), data.get("wifi_signal", ""))

        bt_list = data.get("bluetooth", [])
        if bt_list:
            bluetooth_devices = [
                (bt_list[i], bt_list[i + 1])
                for i in range(0, len(bt_list), 2)
            ]
        else:
            bluetooth_devices = []

        charge = data.get("battery_charge", "")
        status = data.get("battery_status", "")
        if charge:
            status_map = {
                "1": "放电", "2": "接通电源", "3": "完全充电",
                "4": "低", "5": "临界", "6": "充电",
                "7": "充电过高", "8": "未知"
            }
            battery_info = (str(charge), status_map.get(str(status), str(status)))
    except Exception:
        pass

    return wifi_info, bluetooth_devices, battery_info
