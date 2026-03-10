import ctypes
import platform
import sys


from PyIsland.Display.Island import DynamicIslandWindow

from PyQt5.QtWidgets import QApplication

def main():

    if platform.system() == "Windows":
        # noinspection PyUnresolvedReferences
        ctypes.windll.shcore.SetProcessDpiAwareness(1)

    app = QApplication(sys.argv)
    app.setQuitOnLastWindowClosed(False)

    window = DynamicIslandWindow()
    window.show()

    sys.exit(app.exec_())

main()
