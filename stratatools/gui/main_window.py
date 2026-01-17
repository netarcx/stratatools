"""
Stratatools Main Window

Main application window with tabbed interface for reading, editing,
creating, and managing Stratasys cartridge EEPROMs.
"""

from PyQt5.QtWidgets import (
    QMainWindow, QTabWidget, QWidget, QVBoxLayout,
    QStatusBar, QMenuBar, QMenu, QAction, QMessageBox, QProgressBar
)
from PyQt5.QtCore import Qt, QSettings
from PyQt5.QtGui import QIcon

from stratatools.gui.controllers.cartridge_controller import CartridgeController
from stratatools.gui.widgets.read_tab import ReadTab
from stratatools.gui.widgets.edit_tab import EditTab
from stratatools.gui.widgets.create_tab import CreateTab
from stratatools.gui.widgets.advanced_tab import AdvancedTab


class StratatoolsMainWindow(QMainWindow):
    """Main application window"""

    def __init__(self):
        super().__init__()
        self.settings = QSettings()
        self.controller = CartridgeController()
        self.setup_ui()
        self.connect_signals()
        self.restore_settings()

    def setup_ui(self):
        """Initialize the user interface"""
        self.setWindowTitle("Stratatools GUI - Cartridge Reader/Writer")
        self.setMinimumSize(1000, 750)

        # Create central widget with tab container
        central_widget = QWidget()
        self.setCentralWidget(central_widget)

        layout = QVBoxLayout(central_widget)
        layout.setContentsMargins(0, 0, 0, 0)

        # Create tab widget
        self.tabs = QTabWidget()
        layout.addWidget(self.tabs)

        # Create tabs with shared controller
        self.read_tab = ReadTab(self.controller)
        self.edit_tab = EditTab(self.controller)
        self.create_tab = CreateTab(self.controller)
        self.advanced_tab = AdvancedTab(self.controller)

        self.tabs.addTab(self.read_tab, "Read")
        self.tabs.addTab(self.edit_tab, "Edit")
        self.tabs.addTab(self.create_tab, "Create")
        self.tabs.addTab(self.advanced_tab, "Advanced")

        # Create menu bar
        self.setup_menu_bar()

        # Create status bar with progress
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)

        self.progress_bar = QProgressBar()
        self.progress_bar.setMaximumWidth(200)
        self.progress_bar.setVisible(False)
        self.status_bar.addPermanentWidget(self.progress_bar)

        self.status_bar.showMessage("Ready")

    def setup_menu_bar(self):
        """Setup the menu bar"""
        menubar = self.menuBar()

        # File menu
        file_menu = menubar.addMenu("&File")

        open_action = QAction("&Open Cartridge File...", self)
        open_action.setShortcut("Ctrl+O")
        file_menu.addAction(open_action)

        save_action = QAction("&Save Cartridge File...", self)
        save_action.setShortcut("Ctrl+S")
        file_menu.addAction(save_action)

        file_menu.addSeparator()

        exit_action = QAction("E&xit", self)
        exit_action.setShortcut("Ctrl+Q")
        exit_action.triggered.connect(self.close)
        file_menu.addAction(exit_action)

        # Tools menu
        tools_menu = menubar.addMenu("&Tools")

        refresh_action = QAction("&Refresh Ports", self)
        refresh_action.setShortcut("F5")
        refresh_action.triggered.connect(self.read_tab.refresh_ports)
        tools_menu.addAction(refresh_action)

        # Help menu
        help_menu = menubar.addMenu("&Help")

        about_action = QAction("&About", self)
        about_action.triggered.connect(self.show_about)
        help_menu.addAction(about_action)

    def connect_signals(self):
        """Connect controller signals to UI"""
        self.controller.error_occurred.connect(self.show_error)
        self.controller.progress_updated.connect(self.update_progress)
        self.controller.connection_changed.connect(self.on_connection_changed)

    def on_connection_changed(self, connected):
        """Update status bar on connection change"""
        if connected:
            self.status_bar.showMessage("Connected to ESP32")
        else:
            self.status_bar.showMessage("Disconnected")

    def update_progress(self, message, percent):
        """Update progress bar and status message"""
        self.status_bar.showMessage(message)

        if percent >= 0:
            self.progress_bar.setVisible(True)
            self.progress_bar.setValue(percent)

            if percent >= 100:
                # Hide progress bar after a short delay
                from PyQt5.QtCore import QTimer
                QTimer.singleShot(1000, lambda: self.progress_bar.setVisible(False))
        else:
            self.progress_bar.setVisible(False)

    def show_about(self):
        """Show about dialog"""
        QMessageBox.about(
            self,
            "About Stratatools GUI",
            "<h3>Stratatools GUI v1.0</h3>"
            "<p>Cartridge Reader/Writer for Stratasys 3D Printers</p>"
            "<p>Read, edit, and write cartridge EEPROMs via ESP32 bridge.</p>"
            "<p><a href='https://github.com/bvanheu/stratatools'>GitHub Project</a></p>"
        )

    def show_error(self, message: str):
        """Display error message dialog"""
        QMessageBox.critical(self, "Error", message)
        self.status_bar.showMessage(f"Error: {message[:50]}")

    def show_info(self, message: str):
        """Display information message dialog"""
        QMessageBox.information(self, "Information", message)

    def show_warning(self, message: str):
        """Display warning message dialog"""
        QMessageBox.warning(self, "Warning", message)

    def restore_settings(self):
        """Restore window settings from previous session"""
        geometry = self.settings.value("window/geometry")
        if geometry:
            self.restoreGeometry(geometry)

        state = self.settings.value("window/state")
        if state:
            self.restoreState(state)

    def closeEvent(self, event):
        """Save settings and cleanup before closing"""
        # Disconnect from ESP32 if connected
        if self.controller.is_connected():
            self.controller.disconnect()

        # Save window state
        self.settings.setValue("window/geometry", self.saveGeometry())
        self.settings.setValue("window/state", self.saveState())

        event.accept()
