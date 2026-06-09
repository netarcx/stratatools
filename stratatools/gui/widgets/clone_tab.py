"""
Clone Tab Widget

Copy a genuine cartridge onto another (e.g. a blank/refillable) cartridge:
capture the source, swap cartridges, then write it to the target. The source
data is re-encoded with the target's ROM/UID so the clone is valid on it.
"""

from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox, QLabel,
    QPushButton, QComboBox, QTextEdit, QCheckBox, QMessageBox, QFormLayout
)
from PyQt5.QtGui import QFont

from stratatools import machine, cartridge_pb2, cartridge as cartridge_lib
from stratatools.gui.controllers.worker import run_async


class CloneTab(QWidget):
    """Capture a source cartridge and write it onto a target cartridge."""

    def __init__(self, controller):
        super().__init__()
        self.controller = controller
        self.source_cartridge = None
        self.setup_ui()

    def setup_ui(self):
        layout = QVBoxLayout(self)

        instructions = QLabel(
            "Clone a cartridge in two steps: capture a source cartridge, then "
            "swap to the target cartridge and write. The data is re-encoded for "
            "the target's ROM so the clone works on it.")
        instructions.setWordWrap(True)
        layout.addWidget(instructions)

        # Common settings
        settings_group = QGroupBox("Settings")
        settings_form = QFormLayout()

        self.machine_combo = QComboBox()
        for mtype in machine.get_machine_types():
            self.machine_combo.addItem(mtype)
        self.machine_combo.setCurrentText("prodigy")
        settings_form.addRow("Machine Type:", self.machine_combo)

        self.auto_detect_chk = QCheckBox("Auto-detect machine type when capturing")
        self.auto_detect_chk.setChecked(True)
        settings_form.addRow("", self.auto_detect_chk)

        settings_group.setLayout(settings_form)
        layout.addWidget(settings_group)

        # Step 1: capture source
        source_group = QGroupBox("Step 1 - Source Cartridge")
        source_layout = QVBoxLayout()

        self.capture_btn = QPushButton("Capture Source Cartridge")
        self.capture_btn.clicked.connect(self.capture_source)
        self.capture_btn.setEnabled(False)
        source_layout.addWidget(self.capture_btn)

        self.source_info = QTextEdit()
        self.source_info.setReadOnly(True)
        self.source_info.setFont(QFont("Courier", 9))
        self.source_info.setMinimumHeight(140)
        self.source_info.setPlaceholderText("No source captured yet.")
        source_layout.addWidget(self.source_info)

        source_group.setLayout(source_layout)
        layout.addWidget(source_group)

        # Step 2: write to target
        target_group = QGroupBox("Step 2 - Target Cartridge")
        target_layout = QVBoxLayout()

        opts_layout = QHBoxLayout()
        self.refill_chk = QCheckBox("Refill (set current = initial)")
        self.refill_chk.setChecked(True)
        opts_layout.addWidget(self.refill_chk)

        self.randomize_chk = QCheckBox("Randomize serial number")
        opts_layout.addWidget(self.randomize_chk)
        opts_layout.addStretch()
        target_layout.addLayout(opts_layout)

        self.write_btn = QPushButton("Write to Target Cartridge")
        self.write_btn.clicked.connect(self.write_target)
        self.write_btn.setEnabled(False)
        target_layout.addWidget(self.write_btn)

        target_group.setLayout(target_layout)
        layout.addWidget(target_group)

        layout.addStretch()

        # Capture is only possible once connected
        self.controller.connection_changed.connect(self.on_connection_changed)

    def on_connection_changed(self, connected):
        self.capture_btn.setEnabled(connected)
        if not connected:
            self.write_btn.setEnabled(False)

    # ----- Step 1: capture ----------------------------------------------
    def capture_source(self):
        if not self.controller.is_connected():
            QMessageBox.warning(self, "Error", "Not connected to ESP32")
            return
        self.source_info.setPlainText("Searching for source cartridge...")
        run_async(self, self._capture_worker,
                  busy=[self.capture_btn, self.write_btn],
                  on_result=self._on_capture_done)

    def _capture_worker(self):
        """Worker: search the source device and read/decode it."""
        rom = self.controller.search_device()
        if not rom:
            return None

        if self.auto_detect_chk.isChecked():
            for mtype in machine.get_machine_types():
                c = self.controller.read_cartridge(rom, mtype, silent=True)
                if c is not None:
                    return (mtype, c)
            return None

        mtype = self.machine_combo.currentText()
        c = self.controller.read_cartridge(rom, mtype, silent=True)
        return (mtype, c) if c is not None else None

    def _on_capture_done(self, result):
        if not result:
            self.source_info.setPlainText(
                "✗ Could not capture source. Check the cartridge and machine type.")
            return

        mtype, c = result
        # Keep an independent copy of the source data
        self.source_cartridge = cartridge_pb2.Cartridge()
        self.source_cartridge.CopyFrom(c)
        self.machine_combo.setCurrentText(mtype)

        self.source_info.setPlainText(
            f"✓ Source captured (machine type: {mtype})\n\n"
            f"Serial:    {c.serial_number}\n"
            f"Material:  {c.material_name}\n"
            f"Initial:   {c.initial_material_quantity:.2f} cu.in\n"
            f"Current:   {c.current_material_quantity:.2f} cu.in\n\n"
            "Now insert the TARGET cartridge and click 'Write to Target Cartridge'.")
        self.write_btn.setEnabled(True)

    # ----- Step 2: write target -----------------------------------------
    def write_target(self):
        if not self.controller.is_connected():
            QMessageBox.warning(self, "Error", "Not connected to ESP32")
            return
        if self.source_cartridge is None:
            QMessageBox.warning(self, "Error", "Capture a source cartridge first")
            return

        reply = QMessageBox.question(
            self, "Confirm Clone",
            "Write the captured cartridge to the TARGET cartridge now?\n\n"
            "The target's current contents are backed up first.",
            QMessageBox.Yes | QMessageBox.No)
        if reply != QMessageBox.Yes:
            return

        run_async(self, self._write_target_worker,
                  busy=[self.write_btn, self.capture_btn],
                  on_result=self._on_write_done)

    def _write_target_worker(self):
        """Worker: find the target ROM and write the (adjusted) source onto it."""
        target_rom = self.controller.search_device()
        if not target_rom:
            return ("no_device", None)

        clone = cartridge_pb2.Cartridge()
        clone.CopyFrom(self.source_cartridge)

        if self.refill_chk.isChecked():
            clone.current_material_quantity = clone.initial_material_quantity
        if self.randomize_chk.isChecked():
            clone.serial_number = cartridge_lib.get_random_serialnumber()

        mtype = self.machine_combo.currentText()
        ok = self.controller.write_cartridge(clone, target_rom, mtype)
        return ("ok" if ok else "write_failed", target_rom)

    def _on_write_done(self, result):
        status, rom = result
        if status == "ok":
            if self.controller.last_write_verified:
                QMessageBox.information(
                    self, "Clone complete",
                    f"Cartridge cloned and verified to target ROM {rom} "
                    "(read-back matches).")
            else:
                QMessageBox.warning(
                    self, "Cloned but NOT verified",
                    f"The clone was written to {rom}, but could not be read back "
                    "to confirm it. Re-read the target cartridge to check.")
        elif status == "no_device":
            QMessageBox.warning(
                self, "No target",
                "No target cartridge found. Insert the target and try again.")
        # write_failed already surfaces an error via the controller signal
