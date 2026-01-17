"""
Cartridge Controller

Business logic layer for cartridge operations.
"""

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

    def read_cartridge(self, rom_address, machine_type):
        """
        Read and decode cartridge from EEPROM.

        Args:
            rom_address (str): ROM address from search
            machine_type (str): Machine type (fox, prodigy, etc.)

        Returns:
            Cartridge: Decoded cartridge object or None on failure

        Emits:
            cartridge_read(cartridge) on success
            error_occurred(message) on failure
        """
        if not self.is_connected():
            self.error_occurred.emit("Not connected to ESP32. Please connect first.")
            return None

        try:
            self.log(f"Reading cartridge (machine type: {machine_type})...")
            self.progress_updated.emit("Reading EEPROM...", 25)

            # Read EEPROM data
            data = self.bridge.onewire_read(512)

            if data is None:
                raise Exception("Failed to read EEPROM")

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

        try:
            self.log(f"Writing cartridge (machine type: {machine_type})...")
            self.progress_updated.emit("Encoding cartridge...", 10)

            # Encode cartridge
            machine_number = machine.get_number_from_type(machine_type)
            eeprom_uid = bytes.fromhex(rom_address)

            encoded = self.manager.encode(machine_number, eeprom_uid, cartridge)
            self.log(f"Encoded cartridge: {len(encoded)} bytes")

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
                self.log("WARNING: Could not verify write")
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
                self.log("Verification successful - data matches")

            self.current_cartridge = cartridge
            self.machine_type = machine_type
            self.cartridge_written.emit()
            self.log("Cartridge written successfully")
            self.progress_updated.emit("Write complete", 100)
            return True

        except Exception as e:
            error_msg = self._get_user_friendly_error(str(e))
            self.error_occurred.emit(f"Failed to write cartridge: {error_msg}")
            self.log(f"Write failed: {e}")
            return False

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
