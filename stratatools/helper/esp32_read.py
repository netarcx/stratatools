#!/usr/bin/env python3

# Copyright (c) 2024, ESP32-C3 Bridge Support
# All rights reserved.

"""
Read EEPROM using ESP32-C3 Bridge

This script reads a Stratasys cartridge EEPROM via the ESP32-C3 1-wire bridge
and saves it to a file.

Usage:
    stratatools_esp32_read /dev/ttyUSB0 output.bin
"""

import sys
from stratatools.helper.esp32_bridge import ESP32Bridge

def main():
    if len(sys.argv) != 3:
        print("usage: esp32_read.py <serial port> <output eeprom>")
        sys.exit(1)

    port = sys.argv[1]
    output_file = sys.argv[2]

    try:
        # Connect to ESP32 bridge
        print(f"Connecting to ESP32 on {port}...")
        bridge = ESP32Bridge(port=port, timeout=2)

        # Verify connection
        if not bridge.initialize():
            print("ERROR: Failed to initialize ESP32 bridge")
            sys.exit(1)

        print("ESP32 bridge initialized successfully")

        # Search for device (don't reset first - search handles it internally)
        print("Searching for device...")
        rom_address = bridge.onewire_macro_search()

        if rom_address is None:
            print("ERROR: No device found on 1-wire bus")
            bridge.close()
            sys.exit(1)

        print(f"Device found: {rom_address}")

        # Read EEPROM (512 bytes)
        print("Reading EEPROM...")
        data = bridge.onewire_read(512)

        if data is None:
            print("ERROR: Failed to read EEPROM")
            bridge.close()
            sys.exit(1)

        print(f"Read {len(data)} bytes successfully")

        # Write to file
        with open(output_file, "wb") as f:
            f.write(data)

        print(f"EEPROM data saved to {output_file}")

        bridge.close()
        sys.exit(0)

    except Exception as e:
        print(f"ERROR: {str(e)}")
        sys.exit(1)

if __name__ == "__main__":
    main()
