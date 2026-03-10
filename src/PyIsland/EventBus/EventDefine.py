import enum


class EventCode(enum.IntEnum):
    NETWORK_RESTORE = 0b0001    # 1
    BLUETOOTH_CONNECT = 0b0010  # 2
    MOUSE_HOVER = 0b0100        # 4
    MOUSE_LEAVE = 0b0101        # 5
    TEST_NETWORK = 0b1000       # 8
    TEST_BLUETOOTH = 0b1001     # 9

EVENT_TEMPLATES = {
    EventCode.NETWORK_RESTORE: {
        "text": "已恢复网络连接",
        "color": "#4CAF50",
        "icon": "🟢"
    },
    EventCode.BLUETOOTH_CONNECT: {
        "text": "已连接蓝牙设备",
        "color": "#2196F3",
        "icon": "🔵"
    },
    EventCode.TEST_NETWORK: {
        "text": "已恢复网络连接",
        "color": "#4CAF50",
        "icon": "🟢"
    },
    EventCode.TEST_BLUETOOTH: {
        "text": "已连接蓝牙设备",
        "color": "#2196F3",
        "icon": "🔵"
    }
}
