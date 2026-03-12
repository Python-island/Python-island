import os
import subprocess
import ctypes

# 尝试从comtypes导入COMError
try:
    from comtypes import COMError
except ImportError:
    # 如果comtypes不可用，定义一个假的COMError类
    class COMError(Exception):
        pass

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
    # 初始化音量控制变量
    volume_initialized = False
    volume_object = None
    mute_state = False
    current_volume = 0.5  # 默认音量50%
    
    # 初始化音量控制
    try:
        # 初始化COM
        pythoncom.CoInitialize()
        # 尝试获取Core Audio API接口
        try:
            from ctypes import cast, POINTER
            from comtypes import CLSCTX_ALL
            from pycaw.pycaw import AudioUtilities, IAudioEndpointVolume
            # 获取音频设备
            devices = AudioUtilities.GetSpeakers()
            # 获取音频端点（使用EndpointVolume属性）
            endpoint = devices.EndpointVolume
            # 获取实际音量和静音状态
            current_volume = endpoint.GetMasterVolumeLevelScalar()
            mute_state = endpoint.GetMute()
            volume_initialized = True
        except Exception:
            # 如果Core Audio API失败，使用模拟按键方式
            shell = win32com.client.Dispatch("WScript.Shell")
            volume_object = shell
            volume_initialized = True
    except Exception:
        volume_initialized = False
        volume_object = None
except ImportError:
    windows_api_available = False

# 暂时禁用Core Audio Controller，避免访问冲突错误
class CoreAudioController:
    def __init__(self):
        self.volume_interface = None
    
    def get_volume(self):
        # 直接返回默认值，避免使用ctypes
        return 0.5
    
    def set_volume(self, level):
        # 直接返回False，避免使用ctypes
        return False

# 创建CoreAudioController实例
core_audio_controller = CoreAudioController()


def get_system_brightness():
    """获取系统当前亮度"""
    if brightness_available:
        try:
            brightness = sbc.get_brightness()[0]
            return brightness
        except:
            pass
    return 50  # 默认值


def set_brightness(value):
    """设置系统亮度"""
    if brightness_available:
        try:
            sbc.set_brightness(value)
        except:
            pass


def get_system_volume():
    """获取系统当前音量"""
    # 首先尝试使用pycaw
    if volume_available:
        try:
            devices = AudioUtilities.GetSpeakers()
            interface = devices.Activate(IAudioEndpointVolume._iid_, CLSCTX_ALL, None)
            volume = interface.QueryInterface(IAudioEndpointVolume)
            # 将音量从0-1范围转换为0-100
            return int(volume.GetMasterVolumeLevelScalar() * 100)
        except:
            pass
    
    # 如果pycaw失败，尝试使用PowerShell
    try:
        # 使用PowerShell获取音量
        cmd = "(Get-SoundVolume).VolumeLevel"
        result = subprocess.run(["powershell", "-Command", cmd], 
                              capture_output=True, text=True, check=True)
        volume = int(result.stdout.strip())
        # 确保音量在0-100范围内
        return max(0, min(100, volume))
    except:
        pass
    
    # 如果PowerShell失败，尝试使用Windows API获取音量
    if windows_api_available and volume_initialized:
        try:
            global current_volume
            # 将音量从0-1范围转换为0-100
            return int(current_volume * 100)
        except:
            pass
    
    return 50  # 默认值


def set_volume(value):
    """设置系统音量"""
    # 声明全局变量
    global current_volume
    
    # 首先尝试使用pycaw
    if volume_available:
        try:
            devices = AudioUtilities.GetSpeakers()
            interface = devices.Activate(IAudioEndpointVolume._iid_, CLSCTX_ALL, None)
            volume = interface.QueryInterface(IAudioEndpointVolume)
            # 将音量从0-100范围转换为0-1
            volume.SetMasterVolumeLevelScalar(value / 100, None)
            # 更新全局变量
            if windows_api_available:
                current_volume = value / 100.0
            return
        except:
            pass
    
    # 如果pycaw失败，尝试使用PowerShell
    try:
        # 使用PowerShell设置音量
        cmd = f"Set-SoundVolume -VolumeLevel {value}"
        subprocess.run(["powershell", "-Command", cmd], 
                      capture_output=True, text=True, check=True)
        # 更新全局变量
        if windows_api_available:
            current_volume = value / 100.0
        return
    except:
        pass
    
    # 如果PowerShell失败，尝试使用Windows API模拟按键
    if windows_api_available and volume_initialized:
        try:
            # 直接设置系统音量到目标值，而不是基于当前值调整
            # 首先将音量静音，然后逐步增加到目标值
            # 这样可以确保音量准确设置
            
            if value == 0:
                # 如果目标音量是0，直接静音
                win32api.keybd_event(win32con.VK_VOLUME_MUTE, 0, 0, 0)
                win32api.keybd_event(win32con.VK_VOLUME_MUTE, 0, win32con.KEYEVENTF_KEYUP, 0)
            else:
                # 先静音
                win32api.keybd_event(win32con.VK_VOLUME_MUTE, 0, 0, 0)
                win32api.keybd_event(win32con.VK_VOLUME_MUTE, 0, win32con.KEYEVENTF_KEYUP, 0)
                
                # 然后增加到目标值（每步5%）
                # 对于100%，我们需要确保按20次
                steps = min(20, int(value / 5) + 1)
                for _ in range(steps):
                    win32api.keybd_event(win32con.VK_VOLUME_UP, 0, 0, 0)
                    win32api.keybd_event(win32con.VK_VOLUME_UP, 0, win32con.KEYEVENTF_KEYUP, 0)
            
            # 更新全局变量
            current_volume = value / 100.0
            return
        except:
            pass


def get_all_status():
    wifi_info = ("", "")
    bluetooth_devices = []
    battery_info = ("", "")

    try:
        # 合并所有查询到一个 PowerShell 脚本
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
        import json
        data = json.loads(result.stdout.strip())

        wifi_info = (data.get("wifi_ssid", ""), data.get("wifi_signal", ""))

        bt_list = data.get("bluetooth", [])
        if bt_list:
            bluetooth_devices = [(bt_list[i], bt_list[i+1]) for i in range(0, len(bt_list), 2)]
        else:
            bluetooth_devices = []

        charge = data.get("battery_charge", "")
        status = data.get("battery_status", "")
        if charge:
            status_map = {"1": "放电", "2": "接通电源", "3": "完全充电", "4": "低", "5": "临界", "6": "充电", "7": "充电过高", "8": "未知"}
            battery_info = (str(charge), status_map.get(str(status), str(status)))
    except:
        pass

    return wifi_info, bluetooth_devices, battery_info


def get_wifi_info():
    """获取WiFi信息"""
    try:
        # 使用PowerShell获取WiFi信息
        cmd = "netsh wlan show interfaces | Select-String 'SSID', 'State', 'Signal'"
        result = subprocess.run(["powershell", "-Command", cmd], 
                              capture_output=True, text=True, check=True)
        output = result.stdout.strip()
        lines = output.split('\n')
        
        ssid = ""
        signal = ""
        
        for line in lines:
            if 'SSID' in line:
                ssid = line.split(':')[1].strip()
            elif 'Signal' in line:
                signal = line.split(':')[1].strip()
        
        return ssid, signal
    except:
        return "", ""


def get_bluetooth_devices():
    """获取蓝牙设备信息"""
    try:
        # 使用PowerShell获取蓝牙设备信息
        cmd = "Get-PnpDevice -Class Bluetooth | Select-Object FriendlyName, Status | ConvertTo-Csv -NoTypeInformation"
        result = subprocess.run(["powershell", "-Command", cmd], 
                              capture_output=True, text=True, check=True)
        output = result.stdout.strip()
        lines = output.split('\n')[1:]  # 跳过标题行
        
        devices = []
        for line in lines:
            if line:
                parts = line.strip('"').split('","')
                if len(parts) >= 2:
                    devices.append((parts[0], parts[1]))
        
        return devices
    except:
        return []


def get_battery_info():
    try:
        # 使用PowerShell获取电池信息
        cmd = "Get-WmiObject -Class Win32_Battery | Select-Object EstimatedChargeRemaining, BatteryStatus | ConvertTo-Csv -NoTypeInformation"
        result = subprocess.run(["powershell", "-Command", cmd], 
                              capture_output=True, text=True, check=True)
        output = result.stdout.strip()
        lines = output.split('\n')[1:]  # 跳过标题行
        
        if lines:
            parts = lines[0].strip('"').split('","')
            if len(parts) >= 2:
                charge = parts[0]
                status = parts[1]
                return charge, status
        
        return "", ""
    except:
        return "", ""