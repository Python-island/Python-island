
import sys

from PySide6.QtWidgets import QApplication

from app.island import ModernIsland


if __name__ == "__main__":
    app = QApplication(sys.argv)
    island = ModernIsland()
    island.show()
    sys.exit(app.exec())