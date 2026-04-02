"""亮度控制服务模块

提供系统亮度获取和设置功能。
"""

try:
    import screen_brightness_control as sbc

    brightness_available = True
except ImportError:
    brightness_available = False


class BrightnessService:
    """亮度控制服务，提供系统亮度的获取和设置功能。"""

    @staticmethod
    def get_brightness() -> int:
        """获取系统当前亮度。

        Returns:
            int: 当前亮度值（0-100），如果无法获取则返回50
        """
        if brightness_available:
            try:
                brightness = sbc.get_brightness()[0]
                return brightness
            except Exception:
                pass
        return 50

    @staticmethod
    def set_brightness(value: int) -> bool:
        """设置系统亮度。

        Args:
            value: 亮度值（0-100）

        Returns:
            bool: 是否设置成功
        """
        if brightness_available:
            try:
                sbc.set_brightness(value)
                return True
            except Exception:
                pass
        return False

    @staticmethod
    def is_available() -> bool:
        """检查亮度控制是否可用。

        Returns:
            bool: 是否可用
        """
        return brightness_available
