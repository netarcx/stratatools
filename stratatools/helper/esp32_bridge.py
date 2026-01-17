#!/usr/bin/env python3

# Copyright (c) 2024, ESP32-C3 Bridge Support
# All rights reserved.

"""
ESP32-C3 1-Wire Bridge Interface

This module provides a Python interface to the ESP32-C3 firmware
that handles 1-wire protocol operations for Stratasys cartridge programming.
"""

import serial
import time

class ESP32Bridge:
    """
    Interface to ESP32-C3 1-Wire Bridge

    Provides methods to communicate with DS2433/DS2432 EEPROMs via
    an ESP32-C3 running the bridge firmware.
    """

    def __init__(self, port="/dev/ttyUSB0", baudrate=115200, timeout=2):
        """
        Initialize the ESP32 bridge connection

        Args:
            port: Serial port device path
            baudrate: Serial communication speed (default 115200)
            timeout: Read timeout in seconds
        """
        self.serial = serial.Serial(port, baudrate, timeout=timeout)

        # Don't reset on connection - ESP32 may already be running
        # Just give it a moment and clear the buffer
        time.sleep(0.5)

        # Clear any startup messages or pending data
        self._clear_buffer()
        time.sleep(0.3)

    def _clear_buffer(self):
        """Clear the serial input buffer"""
        while self.serial.in_waiting:
            self.serial.readline()

    def _send_command(self, command):
        """
        Send a command to the ESP32

        Args:
            command: Command string to send

        Returns:
            Response string from ESP32
        """
        self.serial.write((command + "\n").encode())
        response = self.serial.readline().decode('ascii', errors='ignore').strip()
        return response

    def initialize(self):
        """
        Initialize and verify connection to ESP32

        Returns:
            True if connection is successful
        """
        # Try multiple times in case ESP32 is still booting
        for attempt in range(3):
            response = self._send_command("VERSION")
            if response and ("ESP32" in response or "1-Wire Bridge" in response or "v1.0" in response):
                return True
            if attempt < 2:
                time.sleep(0.5)

        return False

    def onewire_reset_bus(self):
        """
        Reset the 1-wire bus

        Returns:
            True if reset successful
        """
        response = self._send_command("RESET")
        return response.startswith("OK")

    def onewire_macro_search(self):
        """
        Search for a 1-wire device and return its ROM address

        Returns:
            ROM address as hex string (e.g., "2362474d0100006b") or None if not found
        """
        response = self._send_command("SEARCH")
        if response.startswith("ROM:"):
            return response[4:].strip()
        return None

    def onewire_read(self, length):
        """
        Read data from the EEPROM

        Args:
            length: Number of bytes to read (up to 512)

        Returns:
            bytes object containing the read data, or None on error
        """
        if length > 512:
            length = 512

        response = self._send_command(f"READ {length}")
        if response.startswith("DATA:"):
            hex_data = response[5:].strip()
            try:
                return bytes.fromhex(hex_data)
            except ValueError as e:
                print(f"ERROR: Failed to parse hex data: {e}")
                print(f"Response was: {response[:100]}")
                return None
        else:
            print(f"ERROR: ESP32 read failed. Response: {response[:100]}")
        return None

    def onewire_write(self, data):
        """
        Write data to the EEPROM

        Args:
            data: bytes object to write (up to 512 bytes)

        Returns:
            True if write successful
        """
        if len(data) > 512:
            return False

        hex_data = data.hex()
        response = self._send_command(f"WRITE {len(data)} {hex_data}")
        return response.startswith("OK")

    def close(self):
        """Close the serial connection"""
        if self.serial and self.serial.is_open:
            self.serial.close()

    def __del__(self):
        """Destructor to ensure serial port is closed"""
        self.close()

    # Compatibility methods matching BusPirate interface
    def onewire_search(self):
        """Alias for onewire_macro_search for compatibility"""
        return self.onewire_macro_search()
