"""
Stratatools Main Window

Main application window with tabbed interface for reading, editing,
creating, and managing Stratasys cartridge EEPROMs.
"""

from PyQt5.QtWidgets import (
    QMainWindow, QTabWidget, QWidget, QVBoxLayout, QHBoxLayout,
    QStatusBar, QMenuBar, QMenu, QAction, QMessageBox, QProgressBar,
    QApplication, QLabel, QDialog, QCheckBox, QLineEdit, QPushButton,
    QFileDialog, QDialogButtonBox, QFormLayout
)
from PyQt5.QtCore import Qt, QSettings
from PyQt5.QtGui import QIcon

from stratatools.gui.controllers.cartridge_controller import CartridgeController
from stratatools.gui.widgets.read_tab import ReadTab
from stratatools.gui.widgets.edit_tab import EditTab
from stratatools.gui.widgets.create_tab import CreateTab
from stratatools.gui.widgets.clone_tab import CloneTab
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
        self.clone_tab = CloneTab(self.controller)
        self.advanced_tab = AdvancedTab(self.controller)

        self.tabs.addTab(self.read_tab, "Read")
        self.tabs.addTab(self.edit_tab, "Edit")
        self.tabs.addTab(self.create_tab, "Create")
        self.tabs.addTab(self.clone_tab, "Clone")
        self.tabs.addTab(self.advanced_tab, "Advanced")

        # Create menu bar
        self.setup_menu_bar()

        # Create status bar with progress
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)

        # Persistent device-info panel (right side of the status bar)
        self.device_label = QLabel()
        self.device_label.setStyleSheet("padding: 0 8px;")
        self.status_bar.addPermanentWidget(self.device_label)
        self.update_device_panel()

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
        open_action.triggered.connect(self.edit_tab.load_from_file)
        file_menu.addAction(open_action)

        save_action = QAction("&Save Cartridge File...", self)
        save_action.setShortcut("Ctrl+S")
        save_action.triggered.connect(self.edit_tab.save_to_file)
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

        tools_menu.addSeparator()

        prefs_action = QAction("&Preferences...", self)
        prefs_action.triggered.connect(self.show_preferences)
        tools_menu.addAction(prefs_action)

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
        self.controller.busy_changed.connect(self.on_busy_changed)
        self.controller.device_found.connect(self.on_device_found)
        self.controller.firmware_info.connect(self.on_firmware_info)
        self.controller.cartridge_read.connect(lambda c: self.update_device_panel())

    def on_connection_changed(self, connected):
        """Update status bar on connection change"""
        if connected:
            self.status_bar.showMessage("Connected to ESP32")
        else:
            self.status_bar.showMessage("Disconnected")
        self.update_device_panel()

    def on_device_found(self, rom_address):
        """Refresh the device panel when a ROM is found"""
        self.update_device_panel()

    def on_firmware_info(self, firmware):
        """Refresh the device panel when firmware is reported"""
        self.update_device_panel()

    def on_busy_changed(self, busy):
        """Show a wait cursor and lock the tabs while an operation runs"""
        if busy:
            QApplication.setOverrideCursor(Qt.WaitCursor)
            self.tabs.setEnabled(False)
        else:
            QApplication.restoreOverrideCursor()
            self.tabs.setEnabled(True)

    def update_device_panel(self):
        """Render the persistent device-info panel from controller state"""
        c = self.controller
        if c.is_connected():
            parts = ["● Connected"]
            if c.current_rom:
                parts.append(f"ROM: {c.current_rom}")
            if c.machine_type:
                parts.append(f"Type: {c.machine_type}")
            if c.firmware:
                fw = c.firmware if len(c.firmware) <= 24 else c.firmware[:24] + "…"
                parts.append(f"FW: {fw}")
            self.device_label.setText("   |   ".join(parts))
            self.device_label.setStyleSheet("padding: 0 8px; color: green;")
        else:
            self.device_label.setText("○ Not connected")
            self.device_label.setStyleSheet("padding: 0 8px; color: gray;")

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

    def show_preferences(self):
        """Show the preferences dialog (backup settings)."""
        dialog = QDialog(self)
        dialog.setWindowTitle("Preferences")
        layout = QVBoxLayout(dialog)

        form = QFormLayout()

        backup_chk = QCheckBox("Back up the cartridge before every write")
        backup_chk.setChecked(self.controller.backup_enabled)
        form.addRow(backup_chk)

        dir_row = QHBoxLayout()
        dir_edit = QLineEdit(self.controller.backup_dir)
        dir_row.addWidget(dir_edit)
        browse_btn = QPushButton("Browse...")
        dir_row.addWidget(browse_btn)
        form.addRow("Backup folder:", dir_row)

        def browse():
            chosen = QFileDialog.getExistingDirectory(
                dialog, "Select Backup Folder", dir_edit.text())
            if chosen:
                dir_edit.setText(chosen)
        browse_btn.clicked.connect(browse)

        layout.addLayout(form)

        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.accepted.connect(dialog.accept)
        buttons.rejected.connect(dialog.reject)
        layout.addWidget(buttons)

        if dialog.exec_() == QDialog.Accepted:
            self.controller.backup_enabled = backup_chk.isChecked()
            self.controller.backup_dir = dir_edit.text().strip() or self.controller.backup_dir
            self.settings.setValue("backup/enabled", self.controller.backup_enabled)
            self.settings.setValue("backup/dir", self.controller.backup_dir)

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
        """Restore window + backup settings from previous session"""
        geometry = self.settings.value("window/geometry")
        if geometry:
            self.restoreGeometry(geometry)

        state = self.settings.value("window/state")
        if state:
            self.restoreState(state)

        # Backup preferences
        enabled = self.settings.value("backup/enabled")
        if enabled is not None:
            # QSettings may return strings ("true"/"false") depending on platform
            self.controller.backup_enabled = enabled in (True, "true", "True", 1, "1")
        backup_dir = self.settings.value("backup/dir")
        if backup_dir:
            self.controller.backup_dir = backup_dir

    def closeEvent(self, event):
        """Save settings and cleanup before closing"""
        # Wait for any in-flight background operations so we don't tear down a
        # running worker thread (or close the serial port it is still using).
        for tab in (self.read_tab, self.edit_tab, self.create_tab,
                    self.clone_tab, self.advanced_tab):
            for worker in list(getattr(tab, "_active_workers", ())):
                worker.wait(3000)

        # Disconnect from ESP32 if connected
        if self.controller.is_connected():
            self.controller.disconnect()

        # Save window state
        self.settings.setValue("window/geometry", self.saveGeometry())
        self.settings.setValue("window/state", self.saveState())

        event.accept()
