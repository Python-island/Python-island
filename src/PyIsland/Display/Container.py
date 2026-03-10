from PyIsland.Configure import CONFIG_MANAGER

# noinspection PyUnresolvedReferences
from PyQt5.QtCore import (
    Qt, QThread, QTimer, QPropertyAnimation, QEasingCurve, QRect, pyqtProperty
)
from PyQt5.QtGui import (
    QColor, QPainter, QBrush
)
from PyQt5.QtWidgets import (
    QWidget
)


class CapsuleWidget(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._radius = CONFIG_MANAGER.CAPSULE_INIT_RADIUS

    def get_radius(self):
        return self._radius

    def set_radius(self, radius):
        self._radius = radius
        self.update()

    radius = pyqtProperty(int, get_radius, set_radius)

    # noinspection PyUnresolvedReferences
    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        painter.setBrush(QBrush(QColor(0, 0, 0, CONFIG_MANAGER.CAPSULE_BG_ALPHA)))
        painter.setPen(Qt.NoPen)
        rect = self.rect()
        painter.drawRoundedRect(rect, self._radius, self._radius)
