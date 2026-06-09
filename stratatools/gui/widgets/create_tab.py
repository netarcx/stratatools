"""
Create Tab Widget - Simplified Version

Create new cartridge from scratch with simple form.
"""

import json
from datetime import datetime
from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox, QLabel,
    QPushButton, QLineEdit, QDoubleSpinBox, QSpinBox, QComboBox,
    QMessageBox, QFormLayout, QInputDialog
)
from PyQt5.QtCore import QDateTime, QSettings

from stratatools import material, machine, cartridge_pb2, cartridge
from stratatools.gui.models.cartridge_model import CartridgeModel
from stratatools.gui.controllers.worker import run_async


class CreateTab(QWidget):
    """Simplified Create Tab for new cartridges"""

    def __init__(self, controller):
        super().__init__()
        self.controller = controller
        self.settings = QSettings()
        self.setup_ui()
        self.reload_presets()

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
        self.serial_spin.setDecimals(0)
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

        # Presets Group
        preset_group = QGroupBox("Presets")
        preset_layout = QHBoxLayout()
        preset_layout.addWidget(QLabel("Preset:"))
        self.preset_combo = QComboBox()
        self.preset_combo.setMinimumWidth(180)
        preset_layout.addWidget(self.preset_combo)

        self.load_preset_btn = QPushButton("Load")
        self.load_preset_btn.clicked.connect(self.load_preset)
        preset_layout.addWidget(self.load_preset_btn)

        self.save_preset_btn = QPushButton("Save As...")
        self.save_preset_btn.clicked.connect(self.save_preset)
        preset_layout.addWidget(self.save_preset_btn)

        self.delete_preset_btn = QPushButton("Delete")
        self.delete_preset_btn.clicked.connect(self.delete_preset)
        preset_layout.addWidget(self.delete_preset_btn)

        preset_layout.addStretch()
        preset_group.setLayout(preset_layout)
        layout.addWidget(preset_group)

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

    # ----- Presets -------------------------------------------------------
    def _load_presets_dict(self):
        raw = self.settings.value("create/presets", "{}")
        try:
            return json.loads(raw) if raw else {}
        except (ValueError, TypeError):
            return {}

    def reload_presets(self):
        """Refresh the preset dropdown from saved settings."""
        self.preset_combo.clear()
        presets = self._load_presets_dict()
        if not presets:
            self.preset_combo.addItem("(no presets)")
            self.preset_combo.setEnabled(False)
            self.load_preset_btn.setEnabled(False)
            self.delete_preset_btn.setEnabled(False)
        else:
            self.preset_combo.setEnabled(True)
            self.load_preset_btn.setEnabled(True)
            self.delete_preset_btn.setEnabled(True)
            for name in sorted(presets):
                self.preset_combo.addItem(name)

    def save_preset(self):
        """Save the current form values as a named preset."""
        name, ok = QInputDialog.getText(self, "Save Preset", "Preset name:")
        if not ok or not name.strip():
            return
        name = name.strip()
        presets = self._load_presets_dict()
        presets[name] = {
            "material": self.material_combo.currentText(),
            "initial": self.initial_spin.value(),
            "current": self.current_spin.value(),
            "version": self.version_spin.value(),
            "signature": self.signature_edit.text(),
            "machine_type": self.machine_combo.currentText(),
            "lot": self.lot_edit.text(),
        }
        self.settings.setValue("create/presets", json.dumps(presets))
        self.reload_presets()
        self.preset_combo.setCurrentText(name)

    def load_preset(self):
        """Load the selected preset into the form."""
        presets = self._load_presets_dict()
        data = presets.get(self.preset_combo.currentText())
        if not data:
            return
        self.material_combo.setCurrentText(data.get("material", ""))
        self.initial_spin.setValue(data.get("initial", 0.0))
        self.current_spin.setValue(data.get("current", 0.0))
        self.version_spin.setValue(int(data.get("version", 1)))
        self.signature_edit.setText(data.get("signature", "STRATASYS"))
        self.lot_edit.setText(data.get("lot", ""))
        mt = data.get("machine_type")
        if mt:
            self.machine_combo.setCurrentText(mt)

    def delete_preset(self):
        """Delete the selected preset."""
        name = self.preset_combo.currentText()
        presets = self._load_presets_dict()
        if name in presets:
            del presets[name]
            self.settings.setValue("create/presets", json.dumps(presets))
            self.reload_presets()

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

        try:
            material.get_id_from_name(mat_name)
        except KeyError:
            QMessageBox.warning(self, "Error", f"Unknown material: {mat_name}")
            return
        c.material_name = mat_name

        c.manufacturing_lot = self.lot_edit.text()

        now = datetime.now()
        c.manufacturing_date.FromDatetime(now)
        c.last_use_date.FromDatetime(now)

        c.initial_material_quantity = self.initial_spin.value()
        c.current_material_quantity = self.current_spin.value()
        c.version = self.version_spin.value()
        c.signature = self.signature_edit.text()

        # Generate random key fragment (stored as a 16-char ASCII hex string,
        # the representation the manager/encoder expects)
        import os
        c.key_fragment = os.urandom(8).hex().encode("ascii")

        # Validate before doing anything destructive
        errors = CartridgeModel(c).validate()
        if errors:
            QMessageBox.warning(
                self, "Invalid cartridge",
                "Please fix the following before writing:\n\n - "
                + "\n - ".join(errors))
            return

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

            run_async(
                self, self.controller.write_cartridge, c, rom, machine_type,
                busy=[self.create_btn],
                on_result=self._on_write_done)

    def _on_write_done(self, ok):
        """Report the write result, distinguishing verified vs unverified."""
        if not ok:
            return
        if self.controller.last_write_verified:
            QMessageBox.information(
                self, "Success",
                "New cartridge created, written and verified (read-back matches).")
        else:
            QMessageBox.warning(
                self, "Written but NOT verified",
                "The cartridge was written, but the data could not be read back "
                "to confirm it. Re-read the cartridge to check before relying on it.")
