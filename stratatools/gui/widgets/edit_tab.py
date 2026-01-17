"""
Edit Tab Widget - Simplified Version

Edit cartridge parameters and save/load files.
"""

from datetime import datetime
from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox, QLabel,
    QPushButton, QLineEdit, QDoubleSpinBox, QSpinBox, QComboBox,
    QDateTimeEdit, QFileDialog, QMessageBox, QFormLayout
)
from PyQt5.QtCore import QDateTime

from stratatools import material, machine, cartridge_pb2
from stratatools.gui.models.cartridge_model import CartridgeModel


class EditTab(QWidget):
    """Simplified Edit Tab for modifying cartridge data"""

    def __init__(self, controller):
        super().__init__()
        self.controller = controller
        self.model = CartridgeModel()
        self.setup_ui()
        self.connect_signals()

    def setup_ui(self):
        """Setup the user interface"""
        layout = QVBoxLayout(self)

        # Fields Group
        fields_group = QGroupBox("Cartridge Parameters")
        form = QFormLayout()

        self.serial_spin = QDoubleSpinBox()
        self.serial_spin.setRange(1, 999999)
        self.serial_spin.setValue(1000)
        form.addRow("Serial Number:", self.serial_spin)

        self.material_combo = QComboBox()
        self.material_combo.setEditable(True)
        # Add common materials
        for mat_id in range(256):
            mat_name = material.get_name_from_id(mat_id)
            if mat_name and mat_name != "unknown":
                self.material_combo.addItem(mat_name)
        form.addRow("Material:", self.material_combo)

        self.lot_edit = QLineEdit()
        self.lot_edit.setMaxLength(20)
        form.addRow("Manufacturing Lot:", self.lot_edit)

        self.mfg_date = QDateTimeEdit()
        self.mfg_date.setCalendarPopup(True)
        self.mfg_date.setDateTime(QDateTime.currentDateTime())
        form.addRow("Manufacturing Date:", self.mfg_date)

        self.use_date = QDateTimeEdit()
        self.use_date.setCalendarPopup(True)
        self.use_date.setDateTime(QDateTime.currentDateTime())
        form.addRow("Last Use Date:", self.use_date)

        self.initial_spin = QDoubleSpinBox()
        self.initial_spin.setRange(0, 200)
        self.initial_spin.setValue(56.5)
        self.initial_spin.setSuffix(" cu.in")
        form.addRow("Initial Quantity:", self.initial_spin)

        self.current_spin = QDoubleSpinBox()
        self.current_spin.setRange(0, 200)
        self.current_spin.setValue(56.5)
        self.current_spin.setSuffix(" cu.in")
        form.addRow("Current Quantity:", self.current_spin)

        self.version_spin = QSpinBox()
        self.version_spin.setRange(0, 65535)
        self.version_spin.setValue(1)
        form.addRow("Version:", self.version_spin)

        self.signature_edit = QLineEdit()
        self.signature_edit.setMaxLength(9)
        self.signature_edit.setText("STRATASYS")
        form.addRow("Signature:", self.signature_edit)

        fields_group.setLayout(form)
        layout.addWidget(fields_group)

        # Actions Group
        actions_group = QGroupBox("Actions")
        actions_layout = QHBoxLayout()

        self.refill_btn = QPushButton("Refill Cartridge")
        self.refill_btn.clicked.connect(self.refill_cartridge)
        actions_layout.addWidget(self.refill_btn)

        actions_layout.addStretch()

        self.load_btn = QPushButton("Load from File...")
        self.load_btn.clicked.connect(self.load_from_file)
        actions_layout.addWidget(self.load_btn)

        self.save_btn = QPushButton("Save to File...")
        self.save_btn.clicked.connect(self.save_to_file)
        actions_layout.addWidget(self.save_btn)

        self.write_btn = QPushButton("Write to Cartridge")
        self.write_btn.clicked.connect(self.write_to_cartridge)
        actions_layout.addWidget(self.write_btn)

        actions_group.setLayout(actions_layout)
        layout.addWidget(actions_group)

        # Settings Group
        settings_group = QGroupBox("Write Settings")
        settings_layout = QHBoxLayout()

        settings_layout.addWidget(QLabel("ROM Address:"))
        self.rom_edit = QLineEdit()
        self.rom_edit.setPlaceholderText("Read cartridge first or enter manually")
        settings_layout.addWidget(self.rom_edit)

        settings_layout.addWidget(QLabel("Machine Type:"))
        self.machine_combo = QComboBox()
        for mtype in machine.get_machine_types():
            self.machine_combo.addItem(mtype)
        self.machine_combo.setCurrentText("prodigy")
        settings_layout.addWidget(self.machine_combo)

        settings_group.setLayout(settings_layout)
        layout.addWidget(settings_group)

        layout.addStretch()

    def connect_signals(self):
        """Connect controller signals"""
        self.controller.cartridge_read.connect(self.load_cartridge)
        self.controller.device_found.connect(self.set_rom_address)

    def get_cartridge_from_fields(self):
        """Build cartridge from UI fields"""
        c = cartridge_pb2.Cartridge()
        c.serial_number = self.serial_spin.value()
        c.material_name = self.material_combo.currentText()
        c.manufacturing_lot = self.lot_edit.text()

        mfg_dt = self.mfg_date.dateTime().toPyDateTime()
        c.manufacturing_date.FromDatetime(mfg_dt)

        use_dt = self.use_date.dateTime().toPyDateTime()
        c.last_use_date.FromDatetime(use_dt)

        c.initial_material_quantity = self.initial_spin.value()
        c.current_material_quantity = self.current_spin.value()
        c.version = self.version_spin.value()
        c.signature = self.signature_edit.text()

        # Generate random key fragment
        import os
        c.key_fragment = os.urandom(8)

        return c

    def load_cartridge(self, cartridge):
        """Load cartridge data into fields"""
        self.serial_spin.setValue(cartridge.serial_number)

        idx = self.material_combo.findText(cartridge.material_name)
        if idx >= 0:
            self.material_combo.setCurrentIndex(idx)
        else:
            self.material_combo.setCurrentText(cartridge.material_name)

        self.lot_edit.setText(cartridge.manufacturing_lot)

        if cartridge.HasField("manufacturing_date"):
            mfg_dt = cartridge.manufacturing_date.ToDatetime()
            self.mfg_date.setDateTime(QDateTime(mfg_dt))

        if cartridge.HasField("last_use_date"):
            use_dt = cartridge.last_use_date.ToDatetime()
            self.use_date.setDateTime(QDateTime(use_dt))

        self.initial_spin.setValue(cartridge.initial_material_quantity)
        self.current_spin.setValue(cartridge.current_material_quantity)
        self.version_spin.setValue(cartridge.version)
        self.signature_edit.setText(cartridge.signature)

    def set_rom_address(self, rom):
        """Set ROM address from device search"""
        self.rom_edit.setText(rom)

    def refill_cartridge(self):
        """Refill cartridge to full"""
        self.current_spin.setValue(self.initial_spin.value())
        self.mfg_date.setDateTime(QDateTime.currentDateTime())
        self.use_date.setDateTime(QDateTime.currentDateTime())
        QMessageBox.information(self, "Refill", "Cartridge refilled to initial quantity")

    def load_from_file(self):
        """Load cartridge from file"""
        filepath, _ = QFileDialog.getOpenFileName(
            self, "Load Cartridge File", "", "Binary Files (*.bin);;All Files (*)"
        )

        if filepath:
            rom = self.rom_edit.text()
            machine_type = self.machine_combo.currentText()

            if not rom:
                QMessageBox.warning(self, "Error", "Please enter ROM address first")
                return

            cartridge = self.controller.load_from_file(filepath, rom, machine_type)
            if cartridge:
                self.load_cartridge(cartridge)

    def save_to_file(self):
        """Save cartridge to file"""
        filepath, _ = QFileDialog.getSaveFileName(
            self, "Save Cartridge File", "", "Binary Files (*.bin);;All Files (*)"
        )

        if filepath:
            rom = self.rom_edit.text()
            machine_type = self.machine_combo.currentText()

            if not rom:
                QMessageBox.warning(self, "Error", "Please enter ROM address first")
                return

            cartridge = self.get_cartridge_from_fields()
            if self.controller.save_to_file(cartridge, filepath, rom, machine_type):
                QMessageBox.information(self, "Success", f"Cartridge saved to {filepath}")

    def write_to_cartridge(self):
        """Write cartridge to EEPROM"""
        if not self.controller.is_connected():
            QMessageBox.warning(self, "Error", "Not connected to ESP32")
            return

        rom = self.rom_edit.text()
        if not rom:
            QMessageBox.warning(self, "Error", "Please search for device first or enter ROM address")
            return

        reply = QMessageBox.question(
            self, "Confirm Write",
            "Are you sure you want to write to the cartridge?\nThis will overwrite existing data.",
            QMessageBox.Yes | QMessageBox.No
        )

        if reply == QMessageBox.Yes:
            cartridge = self.get_cartridge_from_fields()
            machine_type = self.machine_combo.currentText()

            if self.controller.write_cartridge(cartridge, rom, machine_type):
                QMessageBox.information(self, "Success", "Cartridge written successfully")
