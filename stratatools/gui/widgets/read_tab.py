"""
Read Tab Widget - Simplified Version

Connect to ESP32, search for device, read cartridge, and display info.
"""

from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox, QLabel,
    QPushButton, QComboBox, QLineEdit, QTextEdit, QFormLayout, QProgressBar
)
from PyQt5.QtCore import Qt, QSettings
from PyQt5.QtGui import QFont

from stratatools import machine
from stratatools.gui.controllers.serial_scanner import SerialPortScanner
from stratatools.gui.controllers.cartridge_controller import CartridgeController
from stratatools.gui.controllers.worker import run_async


class ReadTab(QWidget):
    """Simplified Read Tab for connecting and reading cartridges"""

    def __init__(self, controller):
        super().__init__()
        self.controller = controller
        self.settings = QSettings()
        self.setup_ui()
        self.connect_signals()
        self.restore_preferences()

    def setup_ui(self):
        """Setup the user interface"""
        layout = QVBoxLayout(self)

        # Connection Group
        conn_group = QGroupBox("ESP32 Connection")
        conn_layout = QVBoxLayout()

        port_layout = QHBoxLayout()
        port_layout.addWidget(QLabel("Serial Port:"))
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(200)
        port_layout.addWidget(self.port_combo)

        self.refresh_btn = QPushButton("Refresh")
        self.refresh_btn.clicked.connect(self.refresh_ports)
        port_layout.addWidget(self.refresh_btn)

        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self.toggle_connection)
        port_layout.addWidget(self.connect_btn)

        port_layout.addStretch()

        self.status_label = QLabel("Not connected")
        port_layout.addWidget(self.status_label)

        conn_layout.addLayout(port_layout)
        conn_group.setLayout(conn_layout)
        layout.addWidget(conn_group)

        # Device Group
        device_group = QGroupBox("Cartridge Device")
        device_layout = QVBoxLayout()

        device_row1 = QHBoxLayout()
        self.search_btn = QPushButton("Search Device")
        self.search_btn.clicked.connect(self.search_device)
        self.search_btn.setEnabled(False)
        device_row1.addWidget(self.search_btn)

        device_row1.addWidget(QLabel("ROM Address:"))
        self.rom_edit = QLineEdit()
        self.rom_edit.setReadOnly(True)
        self.rom_edit.setMinimumWidth(200)
        device_row1.addWidget(self.rom_edit)
        device_row1.addStretch()

        device_layout.addLayout(device_row1)

        device_row2 = QHBoxLayout()
        device_row2.addWidget(QLabel("Machine Type:"))
        self.machine_combo = QComboBox()
        for mtype in machine.get_machine_types():
            self.machine_combo.addItem(mtype)
        self.machine_combo.setCurrentText("prodigy")
        device_row2.addWidget(self.machine_combo)

        self.read_btn = QPushButton("Read Cartridge")
        self.read_btn.clicked.connect(self.read_cartridge)
        self.read_btn.setEnabled(False)
        device_row2.addWidget(self.read_btn)

        self.auto_detect_btn = QPushButton("Auto-Detect Type")
        self.auto_detect_btn.setToolTip("Try all machine types until one works")
        self.auto_detect_btn.clicked.connect(self.auto_detect_machine_type)
        self.auto_detect_btn.setEnabled(False)
        device_row2.addWidget(self.auto_detect_btn)

        self.quick_read_btn = QPushButton("Quick Read")
        self.quick_read_btn.setToolTip(
            "Search for the device and auto-detect its machine type in one step")
        self.quick_read_btn.clicked.connect(self.quick_read)
        self.quick_read_btn.setEnabled(False)
        device_row2.addWidget(self.quick_read_btn)

        self.self_test_btn = QPushButton("Safety Self-Test")
        self.self_test_btn.setToolTip(
            "Read and re-encode this cartridge WITHOUT writing, to prove the\n"
            "tool reproduces printer-valid data. Run this before any real write.")
        self.self_test_btn.clicked.connect(self.run_self_test)
        self.self_test_btn.setEnabled(False)
        device_row2.addWidget(self.self_test_btn)

        device_row2.addStretch()

        device_layout.addLayout(device_row2)
        device_group.setLayout(device_layout)
        layout.addWidget(device_group)

        # Info Group
        info_group = QGroupBox("Cartridge Information")
        info_layout = QVBoxLayout()

        self.info_text = QTextEdit()
        self.info_text.setReadOnly(True)
        self.info_text.setMinimumHeight(300)
        font = QFont("Courier")
        self.info_text.setFont(font)
        info_layout.addWidget(self.info_text)

        # Fill-level gauge
        gauge_layout = QHBoxLayout()
        gauge_layout.addWidget(QLabel("Material remaining:"))
        self.fill_gauge = QProgressBar()
        self.fill_gauge.setRange(0, 100)
        self.fill_gauge.setValue(0)
        self.fill_gauge.setFormat("%p%")
        gauge_layout.addWidget(self.fill_gauge)
        info_layout.addLayout(gauge_layout)

        info_group.setLayout(info_layout)
        layout.addWidget(info_group)

        # Initial port scan
        self.refresh_ports()

    def connect_signals(self):
        """Connect controller signals"""
        self.controller.connection_changed.connect(self.on_connection_changed)
        self.controller.device_found.connect(self.on_device_found)
        self.controller.cartridge_read.connect(self.on_cartridge_read)

    def refresh_ports(self):
        """Refresh available serial ports"""
        self.port_combo.clear()
        ports = SerialPortScanner.scan_ports()

        for port in ports:
            display_name = SerialPortScanner.get_port_display_name(port)
            self.port_combo.addItem(display_name, port["port"])

        if self.port_combo.count() == 0:
            self.port_combo.addItem("No ports found")

    def toggle_connection(self):
        """Connect or disconnect from ESP32"""
        if self.controller.is_connected():
            self.controller.disconnect()
        else:
            port = self.port_combo.currentData()
            if port:
                self.settings.setValue("read/last_port", port)
                self.connect_btn.setText("Connecting...")
                run_async(
                    self, self.controller.connect, port,
                    busy=[self.connect_btn],
                    on_result=lambda ok: self.connect_btn.setText(
                        "Disconnect" if ok else "Connect"))

    def on_connection_changed(self, connected):
        """Handle connection status change"""
        if connected:
            self.status_label.setText("✓ Connected")
            self.status_label.setStyleSheet("color: green; font-weight: bold;")
            self.connect_btn.setText("Disconnect")
            self.search_btn.setEnabled(True)
            self.quick_read_btn.setEnabled(True)
            self.port_combo.setEnabled(False)
            self.refresh_btn.setEnabled(False)
        else:
            self.status_label.setText("✗ Not connected")
            self.status_label.setStyleSheet("color: red;")
            self.connect_btn.setText("Connect")
            self.search_btn.setEnabled(False)
            self.read_btn.setEnabled(False)
            self.auto_detect_btn.setEnabled(False)
            self.quick_read_btn.setEnabled(False)
            self.self_test_btn.setEnabled(False)
            self.port_combo.setEnabled(True)
            self.refresh_btn.setEnabled(True)
            self.rom_edit.clear()

    def search_device(self):
        """Search for 1-wire device"""
        run_async(self, self.controller.search_device,
                  busy=[self.search_btn, self.read_btn, self.auto_detect_btn,
                        self.quick_read_btn])

    def on_device_found(self, rom_address):
        """Handle device found"""
        self.rom_edit.setText(rom_address)
        self.read_btn.setEnabled(True)
        self.auto_detect_btn.setEnabled(True)
        self.self_test_btn.setEnabled(True)

    def read_cartridge(self):
        """Read cartridge from device"""
        rom = self.rom_edit.text()
        machine_type = self.machine_combo.currentText()
        self.settings.setValue("read/machine_type", machine_type)

        if rom:
            run_async(self, self.controller.read_cartridge, rom, machine_type,
                      busy=[self.read_btn, self.auto_detect_btn, self.quick_read_btn])

    def on_cartridge_read(self, cartridge):
        """Display cartridge information"""
        # Format cartridge info
        mfg_date = cartridge.manufacturing_date.ToDatetime() if cartridge.HasField("manufacturing_date") else "N/A"
        use_date = cartridge.last_use_date.ToDatetime() if cartridge.HasField("last_use_date") else "N/A"

        initial = cartridge.initial_material_quantity
        current = cartridge.current_material_quantity
        remaining_pct = (current / initial * 100) if initial > 0 else 0

        info = f"""
╔══════════════════════════════════════════════════════════════╗
║              CARTRIDGE INFORMATION                           ║
╚══════════════════════════════════════════════════════════════╝

Serial Number:          {cartridge.serial_number}
Material:               {cartridge.material_name}
Manufacturing Lot:      {cartridge.manufacturing_lot}

Manufacturing Date:     {mfg_date}
Last Use Date:          {use_date}

Initial Quantity:       {initial:.2f} cubic inches
Current Quantity:       {current:.2f} cubic inches
Remaining:              {remaining_pct:.1f}%

Version:                {cartridge.version}
Signature:              {cartridge.signature}
Key Fragment:           {cartridge.key_fragment.decode("ascii", errors="ignore")}

"""
        self.info_text.setText(info)
        self.update_gauge(remaining_pct)

    def update_gauge(self, remaining_pct):
        """Update the material fill-level gauge and color it by level."""
        pct = int(max(0, min(100, round(remaining_pct))))
        self.fill_gauge.setValue(pct)
        if pct <= 10:
            color = "#c0392b"   # red - nearly empty
        elif pct <= 33:
            color = "#e67e22"   # orange - low
        else:
            color = "#27ae60"   # green - healthy
        self.fill_gauge.setStyleSheet(
            "QProgressBar::chunk { background-color: %s; }" % color)

    def auto_detect_machine_type(self):
        """Try all machine types until one works (runs off the GUI thread)."""
        rom = self.rom_edit.text()
        if not rom:
            return

        self.info_text.setText("Auto-detecting machine type (trying all types)...\n")
        run_async(
            self, self._detect_machine_type, rom,
            busy=[self.read_btn, self.auto_detect_btn, self.quick_read_btn,
                  self.search_btn],
            on_result=self._on_auto_detect_done)

    def _detect_machine_type(self, rom):
        """Worker: probe each machine type, return the one that decodes."""
        for mtype in machine.get_machine_types():
            # silent: don't spam error dialogs for the types that don't match
            cartridge = self.controller.read_cartridge(rom, mtype, silent=True)
            if cartridge is not None:
                return mtype
        return None

    def _on_auto_detect_done(self, mtype):
        """GUI thread: report auto-detect result."""
        if mtype is not None:
            self.machine_combo.setCurrentText(mtype)
            self.settings.setValue("read/machine_type", mtype)
            self.info_text.append(f"\n✓ Success! Machine type is: {mtype.upper()}\n")
        else:
            self.info_text.append(
                "\n✗ Could not auto-detect machine type. "
                "Cartridge may be corrupted or incompatible.")

    def quick_read(self):
        """One-click: search for the device, then auto-detect + read it."""
        if not self.controller.is_connected():
            return
        self.info_text.setText("Quick Read: searching for device...\n")
        run_async(
            self, self._quick_read_worker,
            busy=[self.read_btn, self.auto_detect_btn, self.quick_read_btn,
                  self.search_btn],
            on_result=self._on_auto_detect_done)

    def _quick_read_worker(self):
        """Worker: search for the device, then auto-detect its machine type."""
        rom = self.controller.search_device()
        if not rom:
            return None
        return self._detect_machine_type(rom)

    def run_self_test(self):
        """Non-destructive encode self-test on the current cartridge."""
        rom = self.rom_edit.text()
        if not rom:
            return
        machine_type = self.machine_combo.currentText()
        self.info_text.setText(
            "Running safety self-test (reads and re-encodes; does NOT write)...\n")
        run_async(
            self, self.controller.verify_encode_roundtrip, rom, machine_type,
            busy=[self.read_btn, self.auto_detect_btn, self.quick_read_btn,
                  self.search_btn, self.self_test_btn],
            on_result=self._on_self_test_done)

    def _on_self_test_done(self, report):
        """Render the self-test report and a clear verdict."""
        verdict = report.get("verdict")
        lines = list(report.get("messages", []))
        lines.append("")
        if verdict == "identical":
            lines.append("VERDICT: ✅ SAFE — this tool reproduces the cartridge exactly.")
        elif verdict == "equivalent":
            lines.append("VERDICT: ✅ SAFE — re-encoded data is printer-valid and equivalent.")
        else:
            lines.append("VERDICT: ⛔ DO NOT WRITE — encode did not reproduce a valid cartridge.")
        self.info_text.setText("\n".join(lines))

    def restore_preferences(self):
        """Restore the last-used port and machine type from settings."""
        last_type = self.settings.value("read/machine_type")
        if last_type:
            idx = self.machine_combo.findText(last_type)
            if idx >= 0:
                self.machine_combo.setCurrentIndex(idx)

        last_port = self.settings.value("read/last_port")
        if last_port:
            for i in range(self.port_combo.count()):
                if self.port_combo.itemData(i) == last_port:
                    self.port_combo.setCurrentIndex(i)
                    break
