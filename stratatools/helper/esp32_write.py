#!/usr/bin/env python3

# Copyright (c) 2024, ESP32-C3 Bridge Support
# All rights reserved.

"""
Write EEPROM using ESP32-C3 Bridge

This script writes data to a Stratasys cartridge EEPROM via the ESP32-C3
1-wire bridge.

Usage:
    stratatools_esp32_write /dev/ttyUSB0 input.bin
"""

import sys
from stratatools.helper.esp32_bridge import ESP32Bridge

def main():
    if len(sys.argv) != 3:
        print("usage: esp32_write.py <serial port> <input eeprom>")
        sys.exit(1)

    port = sys.argv[1]
    input_file = sys.argv[2]

    try:
        # Read input file
        with open(input_file, "rb") as f:
            data = bytearray(f.read())

        # Pad to 512 bytes if needed
        if len(data) < 512:
            data += b'\x00' * (512 - len(data))

        print(f"Loaded {len(data)} bytes from {input_file}")

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

        # Write EEPROM
        print("Writing EEPROM... (this may take a minute)")
        if not bridge.onewire_write(bytes(data)):
            print("ERROR: Failed to write EEPROM")
            bridge.close()
            sys.exit(1)

        print("Write successful!")

        # Verify by reading back
        print("Verifying...")
        verify_data = bridge.onewire_read(len(data))

        if verify_data is None:
            print("WARNING: Could not verify write")
        elif verify_data != data:
            print("WARNING: Verification failed - data mismatch")
        else:
            print("Verification successful!")

        bridge.close()
        print("Done!")
        sys.exit(0)

    except Exception as e:
        print(f"ERROR: {str(e)}")
        sys.exit(1)

if __name__ == "__main__":
    main()
