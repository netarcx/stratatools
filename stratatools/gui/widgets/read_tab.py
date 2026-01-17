"""
Read Tab Widget - Simplified Version

Connect to ESP32, search for device, read cartridge, and display info.
"""

from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox, QLabel,
    QPushButton, QComboBox, QLineEdit, QTextEdit, QFormLayout
)
from PyQt5.QtCore import Qt
from PyQt5.QtGui import QFont

from stratatools import machine
from stratatools.gui.controllers.serial_scanner import SerialPortScanner
from stratatools.gui.controllers.cartridge_controller import CartridgeController


class ReadTab(QWidget):
    """Simplified Read Tab for connecting and reading cartridges"""

    def __init__(self, controller):
        super().__init__()
        self.controller = controller
        self.setup_ui()
        self.connect_signals()

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
                self.controller.connect(port)

    def on_connection_changed(self, connected):
        """Handle connection status change"""
        if connected:
            self.status_label.setText("✓ Connected")
            self.status_label.setStyleSheet("color: green; font-weight: bold;")
            self.connect_btn.setText("Disconnect")
            self.search_btn.setEnabled(True)
            self.port_combo.setEnabled(False)
            self.refresh_btn.setEnabled(False)
        else:
            self.status_label.setText("✗ Not connected")
            self.status_label.setStyleSheet("color: red;")
            self.connect_btn.setText("Connect")
            self.search_btn.setEnabled(False)
            self.read_btn.setEnabled(False)
            self.port_combo.setEnabled(True)
            self.refresh_btn.setEnabled(True)
            self.rom_edit.clear()

    def search_device(self):
        """Search for 1-wire device"""
        rom = self.controller.search_device()

    def on_device_found(self, rom_address):
        """Handle device found"""
        self.rom_edit.setText(rom_address)
        self.read_btn.setEnabled(True)
        self.auto_detect_btn.setEnabled(True)

    def read_cartridge(self):
        """Read cartridge from device"""
        rom = self.rom_edit.text()
        machine_type = self.machine_combo.currentText()

        if rom:
            self.controller.read_cartridge(rom, machine_type)

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
Key Fragment:           {cartridge.key_fragment.hex()}

"""
        self.info_text.setText(info)

    def auto_detect_machine_type(self):
        """Try all machine types until one works"""
        rom = self.rom_edit.text()
        if not rom:
            return

        # Get all machine types
        machine_types = list(machine.get_machine_types())

        self.info_text.setText("Auto-detecting machine type...\n")

        for mtype in machine_types:
            self.info_text.append(f"Trying {mtype}...")

            # Try to read with this machine type
            cartridge = self.controller.read_cartridge(rom, mtype)

            if cartridge is not None:
                # Success!
                self.machine_combo.setCurrentText(mtype)
                self.info_text.append(f"\n✓ Success! Machine type is: {mtype.upper()}\n")
                return

        # No machine type worked
        self.info_text.append("\n✗ Could not auto-detect machine type. Cartridge may be corrupted or incompatible.")
