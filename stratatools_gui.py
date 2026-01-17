#!/usr/bin/env python3

# Copyright (c) 2025, Stratatools GUI
# All rights reserved.

"""
Stratatools GUI Entry Point

Launch the PyQt5 GUI application for reading, editing, and writing
Stratasys cartridge EEPROMs.

Usage:
    python3 stratatools_gui.py
    or
    stratatools_gui (after pip install)
"""

import sys
from PyQt5.QtWidgets import QApplication
from stratatools.gui.main_window import StratatoolsMainWindow


def main():
    """Launch the Stratatools GUI application"""
    app = QApplication(sys.argv)
    app.setApplicationName("Stratatools GUI")
    app.setOrganizationName("Stratatools")
    app.setOrganizationDomain("stratatools.org")

    # Create and show main window
    window = StratatoolsMainWindow()
    window.show()

    # Run the application event loop
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
