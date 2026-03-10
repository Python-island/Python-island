import json
from pathlib import Path
from threading import Lock

# noinspection PyUnresolvedReferences

class ConfigManager:
    _instance = None
    _lock = Lock()

    def __new__(cls):
        with cls._lock:
            if not cls._instance:
                cls._instance = super().__new__(cls)
                cls._instance._initialize_config()
        return cls._instance

    def _initialize_config(self):
        self.CONFIG_PATH = Path.home() / ".island.config"

        self.DEFAULT_CONFIG = {
            "ISLAND_INIT_WIDTH": 140,
            "ISLAND_INIT_HEIGHT": 50,
            "ISLAND_EXPAND_WIDTH": 300,
            "ISLAND_EXPAND_HEIGHT": 80,
            "SCREEN_OFFSET_Y": 10,
            "NOTIFICATION_DURATION": 3500,
            "ANIMATION_DURATION": 450,
            "CONTENT_ANIMATION_DURATION": 300,
            "CONTENT_ANIMATION_DELAY": 100,
            "CHECK_INTERVAL_NET": 3000,
            "CHECK_INTERVAL_BT": 4000,
            "CAPSULE_INIT_RADIUS": 20,
            "CAPSULE_EXPAND_RADIUS": 35,
            "CAPSULE_BG_ALPHA": 128,
            "CONTENT_FONT_SIZE_INIT": 14,
            "CONTENT_FONT_SIZE_EXPAND": 26,
            "NOTIFICATION_FONT_SIZE": 18,
            "CAPSULE_PADDING": 20
        }

        self._load_or_create_config()

        for key, value in self.config.items():
            setattr(self, key, value)

        self.SCALE_RATIO = self.ISLAND_EXPAND_WIDTH / self.ISLAND_INIT_WIDTH

    def _load_or_create_config(self):
        if not self.CONFIG_PATH.exists():
            with open(self.CONFIG_PATH, "w", encoding="utf-8") as f:
                json.dump(self.DEFAULT_CONFIG, f, indent=4, ensure_ascii=False)
            self.config = self.DEFAULT_CONFIG.copy()
            return

        try:
            with open(self.CONFIG_PATH, "r", encoding="utf-8") as f:
                self.config = json.load(f)

            for key, value in self.DEFAULT_CONFIG.items():
                if key not in self.config:
                    self.config[key] = value

            with open(self.CONFIG_PATH, "w", encoding="utf-8") as f:
                json.dump(self.config, f, indent=4, ensure_ascii=False)

        except Exception as e:
            print(f"配置文件加载失败, 使用默认配置: {e}")
            self.config = self.DEFAULT_CONFIG.copy()


CONFIG_MANAGER = ConfigManager()
