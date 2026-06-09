"""
Cartridge Controller

Business logic layer for cartridge operations.
"""

import datetime
import os
from PyQt5.QtCore import QObject, pyqtSignal

from stratatools.helper.esp32_bridge import ESP32Bridge
from stratatools.manager import Manager
from stratatools.crypto import Desx_Crypto
from stratatools.checksum import Crc16_Checksum
from stratatools import machine, cartridge_pb2


class CartridgeController(QObject):
    """
    Business logic controller for cartridge operations.

    Manages ESP32 bridge connection, encoding/decoding cartridges,
    file operations, and error handling.
    """

    # Signals
    connection_changed = pyqtSignal(bool)  # connected: bool
    device_found = pyqtSignal(str)  # rom_address: str
    cartridge_read = pyqtSignal(object)  # cartridge: Cartridge
    cartridge_written = pyqtSignal()
    progress_updated = pyqtSignal(str, int)  # message: str, percent: int
    error_occurred = pyqtSignal(str)  # error_message: str
    log_message = pyqtSignal(str)  # log_message: str
    raw_data_read = pyqtSignal(bytes)  # raw EEPROM bytes from the last read
    firmware_info = pyqtSignal(str)  # firmware/version banner from the bridge
    busy_changed = pyqtSignal(bool)  # True while a background operation runs

    # Error messages mapping
    ERROR_MESSAGES = {
        "No device found": "No cartridge detected on 1-wire bus. Please check connections and try again.",
        "ERROR: No device found": "No cartridge detected on 1-wire bus. Please check connections and try again.",
        "invalid content checksum": "Wrong machine type selected. Try another type: prodigy, fox, quantum, uprint, etc.",
        "invalid crypted content checksum": "Wrong machine type selected. Try another type: prodigy, fox, quantum, uprint, etc.",
        "invalid current material quantity checksum": "Wrong machine type selected. Try another type: prodigy, fox, quantum, uprint, etc.",
        "Serial port could not be opened": "Cannot open serial port. Check if ESP32 is connected and not in use by another application.",
        "Permission denied": "Permission denied accessing serial port. You may need to add your user to the dialout group (Linux) or run with appropriate permissions.",
    }

    def __init__(self):
        super().__init__()
        self.bridge = None
        self.manager = Manager(Desx_Crypto(), Crc16_Checksum())
        self.current_cartridge = None
        self.current_rom = None
        self.machine_type = "prodigy"  # Default
        self.connected = False
        self.last_raw_data = None  # raw bytes from the most recent EEPROM read
        self.last_write_verified = None  # True/False/None for the last write
        self.firmware = None  # bridge firmware/version banner
        self._busy_count = 0

        # Auto-backup: dump the existing EEPROM to a timestamped file before
        # every (destructive) write so a write can always be undone.
        self.backup_enabled = True
        self.backup_dir = os.path.join(
            os.path.expanduser("~"), ".stratatools", "backups")

    def set_busy(self, busy):
        """Track in-flight background work and emit busy_changed on edge.

        Uses a counter so overlapping tasks don't prematurely clear the busy
        state; busy_changed fires only on the 0<->1 transitions.
        """
        if busy:
            self._busy_count += 1
            if self._busy_count == 1:
                self.busy_changed.emit(True)
        else:
            self._busy_count = max(0, self._busy_count - 1)
            if self._busy_count == 0:
                self.busy_changed.emit(False)

    def connect(self, port):
        """
        Connect to ESP32 bridge on specified port.

        Args:
            port (str): Serial port path (e.g., "/dev/ttyUSB0", "COM3")

        Returns:
            bool: True if connected successfully

        Emits:
            connection_changed(True) on success
            error_occurred(message) on failure
        """
        try:
            self.log(f"Connecting to ESP32 on {port}...")

            self.bridge = ESP32Bridge(port=port, timeout=2)

            if not self.bridge.initialize():
                raise Exception("Failed to initialize ESP32 bridge")

            # Capture the firmware/version banner for the device panel
            try:
                self.firmware = self.bridge._send_command("VERSION")
            except Exception:
                self.firmware = None
            if self.firmware:
                self.firmware_info.emit(self.firmware)

            self.connected = True
            self.connection_changed.emit(True)
            self.log(f"Connected to ESP32 on {port}")
            return True

        except Exception as e:
            self.connected = False
            error_msg = self._get_user_friendly_error(str(e))
            self.error_occurred.emit(error_msg)
            self.log(f"Connection failed: {e}")
            return False

    def disconnect(self):
        """
        Disconnect from ESP32 bridge.

        Emits:
            connection_changed(False)
        """
        if self.bridge:
            try:
                self.bridge.close()
            except:
                pass
            self.bridge = None

        self.connected = False
        self.current_rom = None
        self.connection_changed.emit(False)
        self.log("Disconnected from ESP32")

    def is_connected(self):
        """Check if connected to ESP32"""
        return self.connected and self.bridge is not None

    def search_device(self):
        """
        Search for 1-wire device and return ROM address.

        Returns:
            str: ROM address or None if not found

        Emits:
            device_found(rom_address) on success
            error_occurred(message) on failure
        """
        if not self.is_connected():
            self.error_occurred.emit("Not connected to ESP32. Please connect first.")
            return None

        try:
            self.log("Searching for 1-wire device...")
            self.progress_updated.emit("Searching for device...", 0)

            # Clear buffer before operations
            self.bridge._clear_buffer()
            import time
            time.sleep(0.2)

            # Reset bus before searching for better reliability
            self.bridge.onewire_reset_bus()
            time.sleep(0.3)

            rom_address = self.bridge.onewire_macro_search()

            if rom_address is None:
                raise Exception("No device found")

            self.current_rom = rom_address
            self.device_found.emit(rom_address)
            self.log(f"Device found: {rom_address}")
            self.progress_updated.emit("Device found", 100)
            return rom_address

        except Exception as e:
            error_msg = self._get_user_friendly_error(str(e))
            self.error_occurred.emit(error_msg)
            self.log(f"Device search failed: {e}")
            return None

    def read_cartridge(self, rom_address, machine_type, silent=False):
        """
        Read and decode cartridge from EEPROM.

        Args:
            rom_address (str): ROM address from search
            machine_type (str): Machine type (fox, prodigy, etc.)
            silent (bool): When True, suppress the error_occurred signal on
                failure (used by auto-detect, which probes many machine types).

        Returns:
            Cartridge: Decoded cartridge object or None on failure

        Emits:
            cartridge_read(cartridge) on success
            error_occurred(message) on failure (unless silent)
        """
        if not self.is_connected():
            if not silent:
                self.error_occurred.emit("Not connected to ESP32. Please connect first.")
            return None

        try:
            self.log(f"Reading cartridge (machine type: {machine_type})...")
            self.progress_updated.emit("Reading EEPROM...", 25)

            # Read EEPROM data. Retry once with a fresh bus reset + device
            # search if the first attempt fails: rapid back-to-back reads can
            # leave the bus, or the firmware's device-presence flag, unsettled
            # (the firmware requires a SEARCH before each READ).
            data = self.bridge.onewire_read(512)
            if data is None:
                self.log("Read returned no data; resetting bus and re-searching...")
                try:
                    self.bridge.onewire_reset_bus()
                    rescan = self.bridge.onewire_macro_search()
                    if rescan:
                        self.current_rom = rescan
                except Exception as retry_err:
                    self.log(f"Re-search before retry failed: {retry_err}")
                data = self.bridge.onewire_read(512)

            if data is None:
                raise Exception("Failed to read EEPROM")

            # Keep the raw image so the hex viewer reflects the actual read
            self.last_raw_data = bytes(data)
            self.raw_data_read.emit(self.last_raw_data)

            self.progress_updated.emit("Decoding cartridge...", 50)

            # Decode cartridge
            machine_number = machine.get_number_from_type(machine_type)
            eeprom_uid = bytes.fromhex(rom_address)

            self.log(f"Attempting decode with machine type: {machine_type}, ROM: {rom_address}")

            cartridge = self.manager.decode(machine_number, eeprom_uid, bytearray(data))

            self.current_cartridge = cartridge
            self.machine_type = machine_type
            self.cartridge_read.emit(cartridge)
            self.log("Cartridge read successfully")
            self.progress_updated.emit("Cartridge read successfully", 100)
            return cartridge

        except Exception as e:
            error_msg = self._get_user_friendly_error(str(e))
            if not silent:
                self.error_occurred.emit(f"Failed to read cartridge: {error_msg}")
            self.log(f"Read failed: {e}")
            return None

    def write_cartridge(self, cartridge, rom_address, machine_type):
        """
        Encode and write cartridge to EEPROM.

        Args:
            cartridge (Cartridge): Cartridge object to write
            rom_address (str): ROM address from search
            machine_type (str): Machine type (fox, prodigy, etc.)

        Returns:
            bool: True if write successful

        Emits:
            cartridge_written() on success
            error_occurred(message) on failure
        """
        if not self.is_connected():
            self.error_occurred.emit("Not connected to ESP32. Please connect first.")
            return False

        self.last_write_verified = None
        try:
            self.log(f"Writing cartridge (machine type: {machine_type})...")
            self.progress_updated.emit("Encoding cartridge...", 10)

            # Encode cartridge
            machine_number = machine.get_number_from_type(machine_type)
            eeprom_uid = bytes.fromhex(rom_address)

            encoded = self.manager.encode(machine_number, eeprom_uid, cartridge)
            self.log(f"Encoded cartridge: {len(encoded)} bytes")

            # Safety: back up whatever is currently on the cartridge first
            self._backup_eeprom(rom_address)

            self.progress_updated.emit("Writing to EEPROM...", 30)

            # Write to EEPROM
            if not self.bridge.onewire_write(bytes(encoded)):
                raise Exception("Failed to write EEPROM")

            # Give EEPROM time to commit write
            import time
            time.sleep(0.5)

            self.progress_updated.emit("Verifying write...", 80)

            # Verify by reading back
            verify_data = self.bridge.onewire_read(len(encoded))

            if verify_data is None:
                self.last_write_verified = False
                self.log("WARNING: Could not read back to verify write")
            elif bytes(verify_data) != bytes(encoded):
                # Compare as bytes to handle bytearray vs bytes
                self.log(f"Verification mismatch: read {len(verify_data)} bytes, expected {len(encoded)} bytes")

                # Show first differences for debugging
                for i in range(min(len(verify_data), len(encoded))):
                    if verify_data[i] != encoded[i]:
                        self.log(f"First mismatch at byte {i}: read 0x{verify_data[i]:02x}, wrote 0x{encoded[i]:02x}")
                        break

                raise Exception("Verification failed - data mismatch")
            else:
                self.last_write_verified = True
                self.log("Verification successful - data matches")

            self.current_cartridge = cartridge
            self.machine_type = machine_type
            self.last_raw_data = bytes(encoded)
            self.raw_data_read.emit(self.last_raw_data)
            self.cartridge_written.emit()
            self.log("Cartridge written successfully")
            self.progress_updated.emit("Write complete", 100)
            return True

        except Exception as e:
            error_msg = self._get_user_friendly_error(str(e))
            self.error_occurred.emit(f"Failed to write cartridge: {error_msg}")
            self.log(f"Write failed: {e}")
            return False

    def _backup_eeprom(self, rom_address):
        """Read the current EEPROM and save it to a timestamped .bin file.

        Best-effort: a blank or unreadable cartridge (e.g. when creating a brand
        new one) is not an error, so failures are logged and the write proceeds.

        Returns:
            str: path of the backup file, or None if no backup was made.
        """
        if not self.backup_enabled:
            return None

        try:
            data = self.bridge.onewire_read(512)
            if not data:
                self.log("Backup skipped: nothing readable on cartridge")
                return None

            os.makedirs(self.backup_dir, exist_ok=True)
            stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
            rom = (rom_address or "unknown").replace(" ", "")
            path = os.path.join(self.backup_dir, f"backup_{rom}_{stamp}.bin")
            with open(path, "wb") as f:
                f.write(bytes(data))

            self.log(f"Backed up current cartridge to {path}")
            return path

        except Exception as e:
            self.log(f"Backup failed (continuing): {e}")
            return None

    def write_raw(self, data, rom_address=None):
        """Write a raw EEPROM image (already-encoded bytes) to the cartridge.

        Used by the Advanced tab's hex editor for byte-level operations. Backs
        up first and verifies the read-back, like write_cartridge.

        Returns:
            bool: True if the write (and verification) succeeded.
        """
        if not self.is_connected():
            self.error_occurred.emit("Not connected to ESP32. Please connect first.")
            return False

        data = bytes(data)
        self.last_write_verified = None
        try:
            self.log(f"Writing raw image ({len(data)} bytes)...")

            self._backup_eeprom(rom_address)

            self.progress_updated.emit("Writing raw image...", 30)
            if not self.bridge.onewire_write(data):
                raise Exception("Failed to write EEPROM")

            import time
            time.sleep(0.5)

            self.progress_updated.emit("Verifying write...", 80)
            verify_data = self.bridge.onewire_read(len(data))
            if verify_data is None:
                self.last_write_verified = False
                self.log("WARNING: Could not read back to verify raw write")
            elif bytes(verify_data) != data:
                raise Exception("Verification failed - data mismatch")
            else:
                self.last_write_verified = True
                self.log("Raw write verified")

            self.last_raw_data = data
            self.raw_data_read.emit(data)
            self.cartridge_written.emit()
            self.progress_updated.emit("Write complete", 100)
            return True

        except Exception as e:
            error_msg = self._get_user_friendly_error(str(e))
            self.error_occurred.emit(f"Failed to write raw image: {error_msg}")
            self.log(f"Raw write failed: {e}")
            return False

    # Byte ranges the printer actually validates (encrypted content + the
    # checksums over it + the key). Anything outside these is ignored by the
    # printer, so a difference there cannot make a cartridge be rejected.
    _COVERED_RANGES = ((0x00, 0x42), (0x46, 0x52), (0x58, 0x64))

    def _covered_byte(self, n):
        """Return a bool list marking checksum/encryption-covered offsets."""
        covered = [False] * n
        for lo, hi in self._COVERED_RANGES:
            for i in range(lo, min(hi, n)):
                covered[i] = True
        return covered

    def verify_encode_roundtrip(self, rom_address, machine_type):
        """Non-destructive self-test. NEVER writes to the cartridge.

        Reads the genuine cartridge, decodes it (which validates every checksum
        the printer checks), re-encodes the decoded data unchanged, and compares
        the result against the original. If the re-encode is bit-identical — or
        decodes to identical data with differences only outside the validated
        regions — then this tool's encoder reproduces printer-valid data for
        this exact cartridge, and a real write can be trusted.

        Returns:
            dict with keys: verdict ('identical' | 'equivalent' | 'fail'),
            messages (list[str]), diffs (list[int] offsets), cartridge.
        """
        report = {"verdict": "fail", "messages": [], "diffs": [], "cartridge": None}

        def msg(s):
            report["messages"].append(s)
            self.log(s)

        if not self.is_connected():
            msg("Not connected to ESP32.")
            return report

        try:
            msg("Self-test: reading cartridge (NO write will be performed)...")
            raw = self.bridge.onewire_read(512)
            if raw is None:
                msg("✗ Could not read the cartridge.")
                return report
            raw = bytes(raw)
            self.last_raw_data = raw
            self.raw_data_read.emit(raw)

            machine_number = machine.get_number_from_type(machine_type)
            eeprom_uid = bytes.fromhex(rom_address)

            # Decode validates all checksums; raises if not a genuine cartridge
            # for this machine type.
            cartridge = self.manager.decode(machine_number, eeprom_uid, bytearray(raw))
            report["cartridge"] = cartridge
            msg(f"✓ Decoded genuine cartridge — all checksums valid (type: {machine_type}).")

            # Re-encode the decoded data unchanged and re-decode to re-validate.
            reencoded = bytes(self.manager.encode(machine_number, eeprom_uid, cartridge))
            cartridge2 = self.manager.decode(machine_number, eeprom_uid, bytearray(reencoded))
            fields_match = (cartridge == cartridge2)

            n = len(reencoded)
            original = raw[:n]
            diffs = [i for i in range(n) if original[i] != reencoded[i]]
            report["diffs"] = diffs

            if original == reencoded:
                report["verdict"] = "identical"
                msg(f"✓ Re-encoded image is BIT-IDENTICAL to the genuine cartridge ({n} bytes).")
                msg("→ Writing this data back is guaranteed safe; the printer cannot tell it apart.")
            elif fields_match:
                covered = self._covered_byte(n)
                covered_diffs = [i for i in diffs if covered[i]]
                msg("✓ Re-encoded image decodes to IDENTICAL data and passes all checksums.")
                msg(f"  {len(diffs)} byte(s) differ at: " + ", ".join(hex(i) for i in diffs))
                if covered_diffs:
                    report["verdict"] = "fail"
                    msg("✗ Differences fall INSIDE validated/encrypted regions: "
                        + ", ".join(hex(i) for i in covered_diffs) + " — DO NOT WRITE.")
                else:
                    report["verdict"] = "equivalent"
                    msg("  All differences are OUTSIDE the encrypted/checksummed regions, "
                        "which the printer does not validate. → Safe to write.")
            else:
                report["verdict"] = "fail"
                msg("✗ Re-encoded cartridge does NOT match the original data. DO NOT WRITE.")

            return report

        except Exception as e:
            msg(f"✗ Decode failed: {e}")
            msg("  Usually means the wrong machine type, or not a genuine cartridge.")
            return report

    def save_to_file(self, cartridge, filepath, rom_address, machine_type):
        """
        Encode cartridge and save to file.

        Args:
            cartridge (Cartridge): Cartridge to save
            filepath (str): Output file path
            rom_address (str): ROM address
            machine_type (str): Machine type

        Returns:
            bool: True if saved successfully
        """
        try:
            self.log(f"Saving cartridge to {filepath}...")

            machine_number = machine.get_number_from_type(machine_type)
            eeprom_uid = bytes.fromhex(rom_address)

            encoded = self.manager.encode(machine_number, eeprom_uid, cartridge)

            with open(filepath, "wb") as f:
                f.write(encoded)

            self.log(f"Cartridge saved to {filepath}")
            return True

        except Exception as e:
            self.error_occurred.emit(f"Failed to save file: {str(e)}")
            self.log(f"Save failed: {e}")
            return False

    def load_from_file(self, filepath, rom_address, machine_type):
        """
        Load and decode cartridge from file.

        Args:
            filepath (str): Input file path
            rom_address (str): ROM address
            machine_type (str): Machine type

        Returns:
            Cartridge: Decoded cartridge or None on failure
        """
        try:
            self.log(f"Loading cartridge from {filepath}...")

            if not os.path.exists(filepath):
                raise Exception(f"File not found: {filepath}")

            with open(filepath, "rb") as f:
                data = f.read()

            machine_number = machine.get_number_from_type(machine_type)
            eeprom_uid = bytes.fromhex(rom_address)

            cartridge = self.manager.decode(machine_number, eeprom_uid, bytearray(data))

            self.current_cartridge = cartridge
            self.machine_type = machine_type
            self.log(f"Cartridge loaded from {filepath}")
            return cartridge

        except Exception as e:
            error_msg = self._get_user_friendly_error(str(e))
            self.error_occurred.emit(f"Failed to load file: {error_msg}")
            self.log(f"Load failed: {e}")
            return None

    def send_debug_command(self):
        """
        Send DEBUG command to ESP32 and return output.

        Returns:
            str: Debug output or None on failure
        """
        if not self.is_connected():
            self.error_occurred.emit("Not connected to ESP32. Please connect first.")
            return None

        try:
            self.log("Sending DEBUG command...")
            output = self.bridge._send_command("DEBUG")
            return output

        except Exception as e:
            self.error_occurred.emit(f"DEBUG command failed: {str(e)}")
            return None

    def log(self, message):
        """
        Emit log message.

        Args:
            message (str): Log message
        """
        self.log_message.emit(message)

    def _get_user_friendly_error(self, error_str):
        """
        Convert technical error to user-friendly message.

        Args:
            error_str (str): Technical error message

        Returns:
            str: User-friendly error message
        """
        error_str_lower = error_str.lower()

        # Check for known error patterns
        for pattern, friendly_msg in self.ERROR_MESSAGES.items():
            if pattern.lower() in error_str_lower:
                return friendly_msg

        # Default: return original error
        return error_str
