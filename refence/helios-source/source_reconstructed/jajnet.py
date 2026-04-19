# jajnet.py — already plain source at net/jajnet.py
# NBA 2K Net GUI launcher

import os
import sys
from PyQt6.QtWidgets import QApplication
import jajnet_gui


def main():
    app = QApplication(sys.argv)
    window = jajnet_gui.GUI()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
