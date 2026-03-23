# app/core/workshop_manager.py
import os
import sys
import importlib.util
from PySide6.QtCore import QObject, Signal

class WorkshopManager(QObject):
    """
    全知之眼：负责洞察并加载 plugins 目录下的所有模块
    """
    plugin_loaded = Signal(str)
    plugin_unloaded = Signal(str)

    def __init__(self, island_instance):
        super().__init__()
        self.island = island_instance
        self.plugins_dir = os.path.join(os.getcwd(), "plugins")
        self.active_plugins = {}
        
        if not os.path.exists(self.plugins_dir):
            os.makedirs(self.plugins_dir)

    def scan_plugins(self):
        """扫描可用的灵感碎片"""
        if not os.path.exists(self.plugins_dir):
            return []
        return [f[:-3] for f in os.listdir(self.plugins_dir) 
                if f.endswith(".py") and not f.startswith("__")]

    def load_plugin(self, plugin_name):
        """赋予插件实体与生命"""
        if plugin_name in self.active_plugins:
            return True

        try:
            file_path = os.path.join(self.plugins_dir, f"{plugin_name}.py")
            spec = importlib.util.spec_from_file_location(plugin_name, file_path)
            module = importlib.util.module_from_spec(spec)
            sys.modules[plugin_name] = module
            spec.loader.exec_module(module)

            if hasattr(module, 'Plugin'):
                # 注入岛屿的上下文
                plugin_instance = module.Plugin(self.island)
                self.active_plugins[plugin_name] = plugin_instance
                self.plugin_loaded.emit(plugin_name)
                print(f"储君提示：插件 [{plugin_name}] 已成功挂载。")
                return True
        except Exception as e:
            print(f"储君警告：挂载插件 [{plugin_name}] 失败 -> {e}")
        return False

    def unload_plugin(self, plugin_name):
        """剥离插件，使其重归虚无"""
        if plugin_name in self.active_plugins:
            plugin = self.active_plugins[plugin_name]
            if hasattr(plugin, 'cleanup'):
                plugin.cleanup()
            del self.active_plugins[plugin_name]
            self.plugin_unloaded.emit(plugin_name)
            print(f"储君提示：插件 [{plugin_name}] 已卸载。")