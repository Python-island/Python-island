from threading import Lock
from .EventDefine import EventCode, EVENT_TEMPLATES
from collections import deque
from PyQt5.QtCore import QTimer


# noinspection PyAttributeOutsideInit
class EventManager:
    _instance = None
    _lock = Lock()

    def __new__(cls):
        with cls._lock:
            if not cls._instance:
                cls._instance = super().__new__(cls)
                cls._instance.event_queue = deque()
                cls._instance.queue_lock = Lock()
                cls._instance.subscribers = {}
                cls._instance.is_processing = False
                cls._instance.queue_timer = QTimer()
                cls._instance.queue_timer.setInterval(10)
                cls._instance.queue_timer.timeout.connect(cls._instance.process_queue)
                cls._instance.queue_timer.start()
        return cls._instance

    def subscribe(self, event_code: EventCode, callback):
        with self._lock:
            if event_code not in self.subscribers:
                self.subscribers[event_code] = []
            if callback not in self.subscribers[event_code]:
                self.subscribers[event_code].append(callback)

    def publish(self, event_code: EventCode, data=None):
        event_data = EVENT_TEMPLATES.get(event_code, {}).copy()
        if data:
            event_data.update(data)
        with self.queue_lock:
            self.event_queue.append((event_code, event_data))

    def process_queue(self):
        if self.is_processing:
            return

        with self.queue_lock:
            if not self.event_queue:
                return
            event_code, event_data = self.event_queue.popleft()

        self.is_processing = True
        try:
            if event_code in self.subscribers:
                for callback in self.subscribers[event_code]:
                    callback(event_data)
        finally:
            self.is_processing = False
