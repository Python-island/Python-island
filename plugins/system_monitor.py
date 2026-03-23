# plugins/system_monitor.py
from PySide6.QtCore import QTimer
import psutil

class Plugin:
    """
    系统洞察模组：通过方法劫持，将脉搏刻印于时间长河之畔
    """
    def __init__(self, island):
        self.island = island
        self.current_cpu = 0.0
        
        # 1. 记录原初的法则：保存时间管理器原本的 update 方法
        self.original_time_update = self.island.time_display_manager.update
        
        # 2. 施展移花接木：将岛屿的时间更新枢纽，替换为吾等定制的法术
        self.island.time_display_manager.update = self.hijacked_update
        
        # 3. 开启心跳探针
        self.timer = QTimer()
        self.timer.timeout.connect(self.measure_pulse)
        self.timer.start(2000) # 每2秒测量一次
        
        print("储君洞察：系统监控神经已连接，时间法则已接管。")

    def measure_pulse(self):
        """探测 CPU 脉搏并强制刷新岛屿表面"""
        self.current_cpu = psutil.cpu_percent()
        # 强制呼叫灵动岛刷新时间，借此触发我们的 hijacked_update
        self.island._update_time_display()

    def hijacked_update(self, is_expanded, is_hovering):
        """
        被劫持的时间更新枢纽
        """
        # 第一步：先让原初的法则运行，让岛屿计算并写下当前的时间
        self.original_time_update(is_expanded, is_hovering)
        
        # 第二步：在岛屿未展开、未悬停的静谧状态下，注入系统脉搏
        if not is_expanded and not is_hovering:
            # 截取刚刚生成的时间文本
            current_time_text = self.island.time_label.text()
            
            # 将 CPU 数据与时间缝合
            # 汝可随意更改此处的排版符（如用 "|" 或双空格分割）
            fusion_text = f"💻 {self.current_cpu}%   |   {current_time_text}"
            
            # 重新刻印到岛屿表面
            self.island.time_label.setText(fusion_text)

    def cleanup(self):
        """当插件被剥离时，必须将世界恢复原状"""
        self.timer.stop()
        
        # 归还时间法则的控制权
        self.island.time_display_manager.update = self.original_time_update
        
        # 强制刷新一次，抹除残影
        self.island._update_time_display()
        print("储君洞察：系统监控神经已剥离，时间长河恢复平静。")