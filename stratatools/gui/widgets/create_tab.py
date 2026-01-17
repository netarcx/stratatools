"""
Create Tab Widget - Simplified Version

Create new cartridge from scratch with simple form.
"""

from datetime import datetime
from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox, QLabel,
    QPushButton, QLineEdit, QDoubleSpinBox, QSpinBox, QComboBox,
    QMessageBox, QFormLayout
)
from PyQt5.QtCore import QDateTime

from stratatools import material, machine, cartridge_pb2, cartridge


class CreateTab(QWidget):
    """Simplified Create Tab for new cartridges"""

    def __init__(self, controller):
        super().__init__()
        self.controller = controller
        self.setup_ui()

    def setup_ui(self):
        """Setup the user interface"""
        layout = QVBoxLayout(self)

        # Instructions
        instructions = QLabel(
            "Create a new cartridge from scratch. Fill in the parameters below and click 'Create Cartridge'."
        )
        instructions.setWordWrap(True)
        layout.addWidget(instructions)

        # Basic Info Group
        basic_group = QGroupBox("Basic Information")
        basic_form = QFormLayout()

        serial_layout = QHBoxLayout()
        self.serial_spin = QDoubleSpinBox()
        self.serial_spin.setRange(1, 999999)
        self.serial_spin.setValue(1000)
        serial_layout.addWidget(self.serial_spin)

        generate_btn = QPushButton("Generate Random")
        generate_btn.clicked.connect(self.generate_serial)
        serial_layout.addWidget(generate_btn)
        serial_layout.addStretch()

        basic_form.addRow("Serial Number:", serial_layout)

        self.material_combo = QComboBox()
        self.material_combo.setEditable(True)
        # Add all materials
        for mat_id in range(256):
            mat_name = material.get_name_from_id(mat_id)
            if mat_name and mat_name != "unknown":
                self.material_combo.addItem(f"{mat_name} ({mat_id})")
        basic_form.addRow("Material:", self.material_combo)

        self.lot_edit = QLineEdit()
        self.lot_edit.setMaxLength(20)
        self.lot_edit.setText("000001")
        basic_form.addRow("Manufacturing Lot:", self.lot_edit)

        basic_group.setLayout(basic_form)
        layout.addWidget(basic_group)

        # Material Quantities Group
        qty_group = QGroupBox("Material Quantities")
        qty_form = QFormLayout()

        self.initial_spin = QDoubleSpinBox()
        self.initial_spin.setRange(0, 200)
        self.initial_spin.setValue(56.5)
        self.initial_spin.setSuffix(" cu.in")
        qty_form.addRow("Initial Quantity:", self.initial_spin)

        self.current_spin = QDoubleSpinBox()
        self.current_spin.setRange(0, 200)
        self.current_spin.setValue(56.5)
        self.current_spin.setSuffix(" cu.in")
        qty_form.addRow("Current Quantity:", self.current_spin)

        # Link initial to current
        self.initial_spin.valueChanged.connect(
            lambda v: self.current_spin.setValue(v)
        )

        qty_group.setLayout(qty_form)
        layout.addWidget(qty_group)

        # Advanced Group
        adv_group = QGroupBox("Advanced (Optional)")
        adv_form = QFormLayout()

        self.version_spin = QSpinBox()
        self.version_spin.setRange(0, 65535)
        self.version_spin.setValue(1)
        adv_form.addRow("Version:", self.version_spin)

        self.signature_edit = QLineEdit()
        self.signature_edit.setMaxLength(9)
        self.signature_edit.setText("STRATASYS")
        adv_form.addRow("Signature:", self.signature_edit)

        adv_group.setLayout(adv_form)
        layout.addWidget(adv_group)

        # Target Settings Group
        target_group = QGroupBox("Target Cartridge")
        target_form = QFormLayout()

        self.machine_combo = QComboBox()
        for mtype in machine.get_machine_types():
            self.machine_combo.addItem(mtype)
        self.machine_combo.setCurrentText("prodigy")
        target_form.addRow("Machine Type:", self.machine_combo)

        self.rom_edit = QLineEdit()
        self.rom_edit.setPlaceholderText("Search for device first")
        target_form.addRow("ROM Address:", self.rom_edit)

        target_group.setLayout(target_form)
        layout.addWidget(target_group)

        # Actions
        actions_layout = QHBoxLayout()
        actions_layout.addStretch()

        self.create_btn = QPushButton("Create && Write to Cartridge")
        self.create_btn.clicked.connect(self.create_cartridge)
        actions_layout.addWidget(self.create_btn)

        layout.addLayout(actions_layout)
        layout.addStretch()

        # Connect to device_found signal
        self.controller.device_found.connect(self.set_rom_address)

    def generate_serial(self):
        """Generate random serial number"""
        serial = cartridge.get_random_serialnumber()
        self.serial_spin.setValue(serial)

    def set_rom_address(self, rom):
        """Set ROM address from device search"""
        self.rom_edit.setText(rom)

    def create_cartridge(self):
        """Create new cartridge and write to device"""
        if not self.controller.is_connected():
            QMessageBox.warning(self, "Error", "Not connected to ESP32")
            return

        rom = self.rom_edit.text()
        if not rom:
            QMessageBox.warning(
                self, "Error",
                "Please search for device first or enter ROM address"
            )
            return

        # Build cartridge
        c = cartridge_pb2.Cartridge()
        c.serial_number = self.serial_spin.value()

        # Extract material name from combo text
        mat_text = self.material_combo.currentText()
        if " (" in mat_text:
            mat_name = mat_text.split(" (")[0]
        else:
            mat_name = mat_text
        c.material_name = mat_name

        c.manufacturing_lot = self.lot_edit.text()

        now = datetime.now()
        c.manufacturing_date.FromDatetime(now)
        c.last_use_date.FromDatetime(now)

        c.initial_material_quantity = self.initial_spin.value()
        c.current_material_quantity = self.current_spin.value()
        c.version = self.version_spin.value()
        c.signature = self.signature_edit.text()

        # Generate random key fragment
        import os
        c.key_fragment = os.urandom(8)

        # Confirm
        reply = QMessageBox.question(
            self, "Confirm Create",
            f"Create new cartridge?\n\n"
            f"Serial: {c.serial_number}\n"
            f"Material: {c.material_name}\n"
            f"Quantity: {c.current_material_quantity} cu.in\n\n"
            f"This will write to ROM: {rom}",
            QMessageBox.Yes | QMessageBox.No
        )

        if reply == QMessageBox.Yes:
            machine_type = self.machine_combo.currentText()

            if self.controller.write_cartridge(c, rom, machine_type):
                QMessageBox.information(
                    self, "Success",
                    "New cartridge created and written successfully!"
                )
