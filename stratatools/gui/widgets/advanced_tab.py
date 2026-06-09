"""
Advanced Tab Widget - Simplified Version

Hex viewer, debug console, and raw operations.
"""

from PyQt5.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox, QLabel,
    QPushButton, QTextEdit, QFileDialog, QMessageBox, QInputDialog
)
from PyQt5.QtGui import QFont
from PyQt5.QtCore import Qt

from stratatools.gui.controllers.worker import run_async


class AdvancedTab(QWidget):
    """Simplified Advanced Tab for debugging and raw operations"""

    def __init__(self, controller):
        super().__init__()
        self.controller = controller
        self.last_raw_data = None
        self.setup_ui()
        self.connect_signals()

    def setup_ui(self):
        """Setup the user interface"""
        layout = QVBoxLayout(self)

        # Hex Viewer Group
        hex_group = QGroupBox("Raw EEPROM Data (Hex)")
        hex_layout = QVBoxLayout()

        self.hex_text = QTextEdit()
        self.hex_text.setReadOnly(True)
        self.hex_text.setFont(QFont("Courier", 9))
        self.hex_text.setMinimumHeight(200)
        hex_layout.addWidget(self.hex_text)

        hex_btn_layout = QHBoxLayout()

        self.import_btn = QPushButton("Import Raw .bin")
        self.import_btn.clicked.connect(self.import_raw)
        hex_btn_layout.addWidget(self.import_btn)

        self.export_btn = QPushButton("Export Raw .bin")
        self.export_btn.clicked.connect(self.export_raw)
        hex_btn_layout.addWidget(self.export_btn)

        self.copy_btn = QPushButton("Copy to Clipboard")
        self.copy_btn.clicked.connect(self.copy_hex)
        hex_btn_layout.addWidget(self.copy_btn)

        self.edit_bytes_btn = QPushButton("Edit Bytes...")
        self.edit_bytes_btn.setToolTip("Edit the raw bytes as a continuous hex string")
        self.edit_bytes_btn.clicked.connect(self.edit_bytes)
        hex_btn_layout.addWidget(self.edit_bytes_btn)

        self.write_image_btn = QPushButton("Write Image to EEPROM...")
        self.write_image_btn.setToolTip("Write the displayed raw bytes directly to the cartridge")
        self.write_image_btn.clicked.connect(self.write_image)
        hex_btn_layout.addWidget(self.write_image_btn)

        hex_btn_layout.addStretch()

        hex_layout.addLayout(hex_btn_layout)
        hex_group.setLayout(hex_layout)
        layout.addWidget(hex_group)

        # Debug Console Group
        debug_group = QGroupBox("Debug Console")
        debug_layout = QVBoxLayout()

        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setFont(QFont("Courier", 9))
        self.log_text.setMinimumHeight(200)
        debug_layout.addWidget(self.log_text)

        debug_btn_layout = QHBoxLayout()

        self.debug_cmd_btn = QPushButton("Send DEBUG Command")
        self.debug_cmd_btn.clicked.connect(self.send_debug)
        debug_btn_layout.addWidget(self.debug_cmd_btn)

        self.test_read_btn = QPushButton("Test Read EEPROM")
        self.test_read_btn.setToolTip("Test reading first 32 bytes to diagnose issues")
        self.test_read_btn.clicked.connect(self.test_read)
        debug_btn_layout.addWidget(self.test_read_btn)

        self.save_log_btn = QPushButton("Save Log...")
        self.save_log_btn.clicked.connect(self.save_log)
        debug_btn_layout.addWidget(self.save_log_btn)

        self.clear_log_btn = QPushButton("Clear Log")
        self.clear_log_btn.clicked.connect(self.clear_log)
        debug_btn_layout.addWidget(self.clear_log_btn)

        debug_btn_layout.addStretch()

        debug_layout.addLayout(debug_btn_layout)
        debug_group.setLayout(debug_layout)
        layout.addWidget(debug_group)

    def connect_signals(self):
        """Connect controller signals"""
        self.controller.log_message.connect(self.append_log)
        self.controller.raw_data_read.connect(self.on_raw_data)

    def on_raw_data(self, data):
        """Display the raw EEPROM image from an actual read/write."""
        self.last_raw_data = bytes(data)
        self.hex_text.setText(self.format_hex_dump(self.last_raw_data))
        self.append_log(f"Hex view updated ({len(self.last_raw_data)} bytes)")

    def format_hex_dump(self, data):
        """Format binary data as hex dump"""
        lines = []
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]

            # Offset
            offset = f"{i:04x}"

            # Hex bytes
            hex_part = " ".join(f"{b:02x}" for b in chunk)
            hex_part = hex_part.ljust(47)  # 16 bytes * 3 chars - 1

            # ASCII representation
            ascii_part = "".join(
                chr(b) if 32 <= b < 127 else "."
                for b in chunk
            )

            lines.append(f"{offset}  {hex_part}  {ascii_part}")

        return "\n".join(lines)

    def import_raw(self):
        """Import raw binary file and display as hex"""
        filepath, _ = QFileDialog.getOpenFileName(
            self, "Import Raw Binary", "", "Binary Files (*.bin);;All Files (*)"
        )

        if filepath:
            try:
                with open(filepath, "rb") as f:
                    data = f.read()

                self.last_raw_data = data
                hex_dump = self.format_hex_dump(data)
                self.hex_text.setText(hex_dump)
                self.append_log(f"Imported {len(data)} bytes from {filepath}")

            except Exception as e:
                QMessageBox.critical(self, "Error", f"Failed to import file: {e}")

    def export_raw(self):
        """Export current hex data to file"""
        if not self.last_raw_data:
            QMessageBox.warning(self, "Error", "No data to export")
            return

        filepath, _ = QFileDialog.getSaveFileName(
            self, "Export Raw Binary", "", "Binary Files (*.bin);;All Files (*)"
        )

        if filepath:
            try:
                with open(filepath, "wb") as f:
                    f.write(self.last_raw_data)

                self.append_log(f"Exported {len(self.last_raw_data)} bytes to {filepath}")
                QMessageBox.information(self, "Success", f"Exported to {filepath}")

            except Exception as e:
                QMessageBox.critical(self, "Error", f"Failed to export file: {e}")

    def copy_hex(self):
        """Copy hex dump to clipboard"""
        from PyQt5.QtWidgets import QApplication

        hex_text = self.hex_text.toPlainText()
        if hex_text:
            QApplication.clipboard().setText(hex_text)
            self.append_log("Hex data copied to clipboard")

    def edit_bytes(self):
        """Edit the raw bytes as a continuous hex string."""
        current = (self.last_raw_data or b"").hex()
        # Group into space-separated byte pairs for readability
        spaced = " ".join(current[i:i + 2] for i in range(0, len(current), 2))
        text, ok = QInputDialog.getMultiLineText(
            self, "Edit Bytes",
            "Hex bytes (whitespace/':' ignored):", spaced)
        if not ok:
            return
        cleaned = text.replace(" ", "").replace(":", "").replace("\n", "").replace("\r", "")
        try:
            data = bytes.fromhex(cleaned)
        except ValueError as e:
            QMessageBox.critical(self, "Invalid hex", f"Could not parse hex: {e}")
            return
        self.last_raw_data = data
        self.hex_text.setText(self.format_hex_dump(data))
        self.append_log(f"Edited buffer is now {len(data)} bytes")

    def write_image(self):
        """Write the displayed raw bytes directly to the cartridge."""
        if not self.controller.is_connected():
            QMessageBox.warning(self, "Error", "Not connected to ESP32")
            return
        if not self.last_raw_data:
            QMessageBox.warning(self, "Error", "No data to write. Import or read first.")
            return

        reply = QMessageBox.question(
            self, "Confirm Raw Write",
            f"Write {len(self.last_raw_data)} raw bytes directly to the cartridge?\n\n"
            "This bypasses encoding and overwrites the EEPROM. "
            "The current contents are backed up first.",
            QMessageBox.Yes | QMessageBox.No)
        if reply != QMessageBox.Yes:
            return

        run_async(
            self, self.controller.write_raw,
            self.last_raw_data, self.controller.current_rom,
            busy=[self.write_image_btn, self.import_btn, self.edit_bytes_btn],
            on_result=lambda ok: self.append_log(
                "Raw image written successfully" if ok else "Raw write failed"))

    def save_log(self):
        """Save the debug log to a text file."""
        text = self.log_text.toPlainText()
        if not text:
            QMessageBox.warning(self, "Error", "Log is empty")
            return
        filepath, _ = QFileDialog.getSaveFileName(
            self, "Save Log", "stratatools_log.txt", "Text Files (*.txt);;All Files (*)")
        if filepath:
            try:
                with open(filepath, "w") as f:
                    f.write(text)
                self.append_log(f"Log saved to {filepath}")
            except Exception as e:
                QMessageBox.critical(self, "Error", f"Failed to save log: {e}")

    def send_debug(self):
        """Send DEBUG command to ESP32 (off the GUI thread)."""
        if not self.controller.is_connected():
            QMessageBox.warning(self, "Error", "Not connected to ESP32")
            return

        self.append_log("Sending DEBUG command...")
        run_async(self, self.controller.send_debug_command,
                  busy=[self.debug_cmd_btn, self.test_read_btn],
                  on_result=self._on_debug_output)

    def _on_debug_output(self, output):
        if output:
            self.append_log("--- DEBUG OUTPUT ---")
            self.append_log(output)
            self.append_log("--- END DEBUG OUTPUT ---")

    def test_read(self):
        """Test reading EEPROM to diagnose issues (off the GUI thread)."""
        if not self.controller.is_connected():
            QMessageBox.warning(self, "Error", "Not connected to ESP32")
            return

        self.append_log("\n=== EEPROM Read Test ===")
        run_async(self, self._test_read_worker,
                  busy=[self.debug_cmd_btn, self.test_read_btn],
                  on_result=self._on_test_read_done)

    def _test_read_worker(self):
        """Worker: search + read 32 bytes, capturing the bridge's stdout."""
        import sys
        from io import StringIO

        rom = self.controller.bridge.onewire_macro_search()
        if not rom:
            return {"rom": None, "data": None, "captured": ""}

        old_stdout = sys.stdout
        captured = StringIO()
        sys.stdout = captured
        try:
            data = self.controller.bridge.onewire_read(32)
        finally:
            sys.stdout = old_stdout

        return {"rom": rom, "data": data, "captured": captured.getvalue()}

    def _on_test_read_done(self, result):
        """GUI thread: report the test-read outcome."""
        rom = result.get("rom")
        if not rom:
            self.append_log("✗ No device found")
            self.append_log("=== End Test ===\n")
            return

        self.append_log(f"✓ Device found: {rom}")
        self.append_log("\nTrying to read 32 bytes...")

        captured_text = result.get("captured") or ""
        if captured_text.strip():
            self.append_log(captured_text.strip())

        data = result.get("data")
        if data:
            self.append_log(f"✓ Read successful: {len(data)} bytes")
            self.append_log(f"First 32 bytes (hex): {data.hex()}")
            self.hex_text.setText(self.format_hex_dump(data))
        else:
            self.append_log("✗ Read failed - check ESP32 DEBUG output for details")
            self.append_log("\nPossible issues:")
            self.append_log("  1. EEPROM not responding to read commands")
            self.append_log("  2. 1-wire bus timing issues")
            self.append_log("  3. EEPROM may be write-protected or damaged")
            self.append_log("\nTry 'Send DEBUG Command' to check hardware connections")

        self.append_log("=== End Test ===\n")

    def append_log(self, message):
        """Append message to debug log"""
        self.log_text.append(message)
        # Auto-scroll to bottom
        scrollbar = self.log_text.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())

    def clear_log(self):
        """Clear debug log"""
        self.log_text.clear()
        self.append_log("Log cleared")
