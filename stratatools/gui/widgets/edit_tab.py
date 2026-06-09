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
from PyQt5.QtCore import QDateTime, QDate, QTime

from stratatools import material, machine, cartridge_pb2, cartridge as cartridge_lib
from stratatools.gui.models.cartridge_model import CartridgeModel
from stratatools.gui.controllers.worker import run_async


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

        serial_row = QHBoxLayout()
        self.serial_spin = QDoubleSpinBox()
        self.serial_spin.setRange(1, 999999)
        self.serial_spin.setDecimals(0)
        self.serial_spin.setValue(1000)
        serial_row.addWidget(self.serial_spin)
        self.gen_serial_btn = QPushButton("Generate")
        self.gen_serial_btn.clicked.connect(self.generate_serial)
        serial_row.addWidget(self.gen_serial_btn)
        serial_row.addStretch()
        form.addRow("Serial Number:", serial_row)

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

        # Live validation warning (hidden when everything is valid)
        self.warning_label = QLabel("")
        self.warning_label.setStyleSheet("color: #c0392b; font-weight: bold;")
        self.warning_label.setWordWrap(True)
        self.warning_label.setVisible(False)
        layout.addWidget(self.warning_label)

        # Re-validate whenever quantities change
        self.initial_spin.valueChanged.connect(self.update_validation)
        self.current_spin.valueChanged.connect(self.update_validation)

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

    def generate_serial(self):
        """Fill the serial field with a random serial number."""
        self.serial_spin.setValue(cartridge_lib.get_random_serialnumber())

    def update_validation(self):
        """Show a live warning if current quantity exceeds initial."""
        if self.current_spin.value() > self.initial_spin.value():
            self.warning_label.setText(
                "⚠ Current quantity exceeds initial quantity.")
            self.warning_label.setVisible(True)
        else:
            self.warning_label.setVisible(False)

    def validate_cartridge(self, c):
        """Validate a built cartridge; show all errors and return False if any."""
        errors = CartridgeModel(c).validate()
        if errors:
            QMessageBox.warning(
                self, "Invalid cartridge",
                "Please fix the following before writing:\n\n - "
                + "\n - ".join(errors))
            return False
        return True

    def get_cartridge_from_fields(self):
        """Build cartridge from UI fields.

        Returns None (after showing a warning) if the entered material name is
        not known, since the encoder would otherwise raise a KeyError.
        """
        mat_name = self.material_combo.currentText()
        try:
            material.get_id_from_name(mat_name)
        except KeyError:
            QMessageBox.warning(self, "Error", f"Unknown material: {mat_name}")
            return None

        c = cartridge_pb2.Cartridge()
        c.serial_number = self.serial_spin.value()
        c.material_name = mat_name
        c.manufacturing_lot = self.lot_edit.text()

        mfg_dt = self.mfg_date.dateTime().toPyDateTime()
        c.manufacturing_date.FromDatetime(mfg_dt)

        use_dt = self.use_date.dateTime().toPyDateTime()
        c.last_use_date.FromDatetime(use_dt)

        c.initial_material_quantity = self.initial_spin.value()
        c.current_material_quantity = self.current_spin.value()
        c.version = self.version_spin.value()
        c.signature = self.signature_edit.text()

        # Generate random key fragment (stored as a 16-char ASCII hex string,
        # the representation the manager/encoder expects)
        import os
        c.key_fragment = os.urandom(8).hex().encode("ascii")

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
            self.mfg_date.setDateTime(QDateTime(
                QDate(mfg_dt.year, mfg_dt.month, mfg_dt.day),
                QTime(mfg_dt.hour, mfg_dt.minute, mfg_dt.second)))

        if cartridge.HasField("last_use_date"):
            use_dt = cartridge.last_use_date.ToDatetime()
            self.use_date.setDateTime(QDateTime(
                QDate(use_dt.year, use_dt.month, use_dt.day),
                QTime(use_dt.hour, use_dt.minute, use_dt.second)))

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
            if cartridge is None or not self.validate_cartridge(cartridge):
                return
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
            if cartridge is None or not self.validate_cartridge(cartridge):
                return
            machine_type = self.machine_combo.currentText()

            run_async(
                self, self.controller.write_cartridge, cartridge, rom, machine_type,
                busy=[self.write_btn, self.refill_btn, self.save_btn, self.load_btn],
                on_result=self._on_write_done)

    def _on_write_done(self, ok):
        """Report the write result, distinguishing verified vs unverified."""
        if not ok:
            return
        if self.controller.last_write_verified:
            QMessageBox.information(
                self, "Success",
                "Cartridge written and verified (read-back matches).")
        else:
            QMessageBox.warning(
                self, "Written but NOT verified",
                "The write completed, but the data could not be read back to "
                "confirm it. Re-read the cartridge to check before relying on it.")
