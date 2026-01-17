#!/usr/bin/env python3
"""
Auto-Refill Daemon for Stratasys Cartridges

This daemon monitors the ESP32 auto-refill device and automatically
refills cartridges when they are inserted and below threshold.

Usage:
    python3 autorefill_daemon.py /dev/cu.usbserial-0001

    # With custom threshold
    python3 autorefill_daemon.py /dev/cu.usbserial-0001 --threshold 15.0

    # Specify machine type
    python3 autorefill_daemon.py /dev/cu.usbserial-0001 --machine prodigy

    # Run on Raspberry Pi with auto-start
    sudo python3 autorefill_daemon.py /dev/ttyUSB0 --daemon
"""

import serial
import time
import sys
import argparse
import logging
from datetime import datetime

from stratatools.helper.esp32_bridge import ESP32Bridge
from stratatools.manager import Manager
from stratatools.crypto import Desx_Crypto
from stratatools.checksum import Crc16_Checksum
from stratatools import machine, cartridge_pb2
from google.protobuf.timestamp_pb2 import Timestamp


class AutoRefillDaemon:
    """Monitors ESP32 and auto-refills cartridges"""

    def __init__(self, port, machine_type='prodigy', threshold=10.0, auto_detect=False):
        self.port = port
        self.machine_type = machine_type
        self.threshold = threshold
        self.auto_detect = auto_detect
        self.bridge = None
        self.manager = Manager(Desx_Crypto(), Crc16_Checksum())
        self.running = False

        # Setup logging
        logging.basicConfig(
            level=logging.INFO,
            format='%(asctime)s - %(levelname)s - %(message)s',
            datefmt='%Y-%m-%d %H:%M:%S'
        )
        self.log = logging.getLogger(__name__)

    def connect(self):
        """Connect to ESP32 device"""
        try:
            self.log.info(f"Connecting to {self.port}...")
            self.bridge = ESP32Bridge(self.port, timeout=5)

            if not self.bridge.initialize():
                raise Exception("Failed to initialize bridge")

            self.log.info("Connected to auto-refill device")
            return True
        except Exception as e:
            self.log.error(f"Connection failed: {e}")
            return False

    def refill_cartridge(self, rom_address):
        """Read, refill, and write back cartridge"""
        try:
            self.log.info(f"Processing cartridge {rom_address}")

            # Notify device we're starting
            self.bridge.serial.write(b'REFILLING\n')

            # Reset bus before operations
            time.sleep(0.3)
            self.bridge.onewire_reset_bus()
            time.sleep(0.3)

            # Search to ensure device is still there
            found_rom = self.bridge.onewire_macro_search()
            if not found_rom or found_rom != rom_address:
                raise Exception("Cartridge removed or ROM mismatch")

            # Read EEPROM
            self.log.info("Reading EEPROM...")
            data = self.bridge.onewire_read(512)
            if not data:
                raise Exception("Failed to read EEPROM")

            # Try to decode with specified machine type
            machine_types = [self.machine_type]

            # If auto-detect enabled, try all types
            if self.auto_detect:
                machine_types = list(machine.get_machine_types())

            cartridge = None
            working_machine_type = None

            for mtype in machine_types:
                try:
                    machine_number = machine.get_number_from_type(mtype)
                    eeprom_uid = bytes.fromhex(rom_address)

                    cartridge = self.manager.decode(machine_number, eeprom_uid, bytearray(data))
                    working_machine_type = mtype
                    self.log.info(f"Decoded successfully with machine type: {mtype}")
                    break
                except Exception as e:
                    if not self.auto_detect:
                        raise
                    continue

            if not cartridge:
                raise Exception("Failed to decode with any machine type")

            # Display current info
            self.log.info("=" * 60)
            self.log.info("CARTRIDGE INFORMATION")
            self.log.info("=" * 60)
            self.log.info(f"Material:       {cartridge.material_name}")
            self.log.info(f"Serial:         {cartridge.serial_number}")
            self.log.info(f"Machine Type:   {working_machine_type}")

            initial = cartridge.initial_material_quantity
            current = cartridge.current_material_quantity
            remaining_pct = (current / initial * 100) if initial > 0 else 0

            self.log.info(f"Initial:        {initial:.2f} cu.in")
            self.log.info(f"Current:        {current:.2f} cu.in")
            self.log.info(f"Remaining:      {remaining_pct:.1f}%")
            self.log.info("=" * 60)

            # Check if refill needed
            if current >= self.threshold:
                self.log.info(f"Cartridge above threshold ({self.threshold:.2f} cu.in)")
                self.log.info("No refill needed")
                self.bridge.serial.write(b'REFILL_DONE:NO_REFILL_NEEDED\n')
                return False

            # Perform refill
            self.log.info(f"Cartridge below threshold ({self.threshold:.2f} cu.in)")
            self.log.info("REFILLING CARTRIDGE...")

            # Reset quantity to initial value
            cartridge.current_material_quantity = cartridge.initial_material_quantity

            # Update dates
            now = Timestamp()
            now.GetCurrentTime()
            cartridge.last_use_date.CopyFrom(now)

            # Encode cartridge
            self.log.info("Encoding cartridge...")
            machine_number = machine.get_number_from_type(working_machine_type)
            eeprom_uid = bytes.fromhex(rom_address)
            encoded = self.manager.encode(machine_number, eeprom_uid, cartridge)

            # Write to EEPROM
            self.log.info("Writing to EEPROM...")
            time.sleep(0.5)

            if not self.bridge.onewire_write(bytes(encoded)):
                raise Exception("Write failed")

            # Wait for EEPROM to commit
            time.sleep(2)

            # Verify
            self.log.info("Verifying write...")
            verify_data = self.bridge.onewire_read(512)

            if verify_data and bytes(verify_data) == bytes(encoded):
                self.log.info("✓ REFILL SUCCESSFUL!")
                self.log.info(f"New quantity: {cartridge.current_material_quantity:.2f} cu.in (100%)")
                self.bridge.serial.write(b'REFILL_DONE:SUCCESS\n')
                return True
            else:
                raise Exception("Verification failed")

        except Exception as e:
            self.log.error(f"Refill failed: {e}")
            self.bridge.serial.write(f'ERROR:{str(e)}\n'.encode())
            return False

    def run(self):
        """Main daemon loop"""
        self.log.info("=" * 60)
        self.log.info("Stratasys Auto-Refill Daemon v1.0")
        self.log.info("=" * 60)
        self.log.info(f"Port: {self.port}")
        self.log.info(f"Machine type: {self.machine_type}")
        self.log.info(f"Auto-detect: {'Enabled' if self.auto_detect else 'Disabled'}")
        self.log.info(f"Threshold: {self.threshold:.2f} cu.in")
        self.log.info("=" * 60)
        self.log.info("")

        if not self.connect():
            return False

        self.running = True
        self.log.info("Monitoring for cartridges...")
        self.log.info("Press Ctrl+C to stop")
        self.log.info("")

        try:
            while self.running:
                # Read from device
                if self.bridge.serial.in_waiting:
                    line = self.bridge.serial.readline().decode('ascii', errors='ignore').strip()

                    # Check for cartridge insertion notification
                    if line.startswith("CARTRIDGE_INSERTED:"):
                        rom_address = line.split(':')[1].strip()
                        self.log.info("")
                        self.log.info("*" * 60)
                        self.log.info("CARTRIDGE DETECTED!")
                        self.log.info("*" * 60)
                        self.log.info("")

                        # Wait a moment for cartridge to settle
                        time.sleep(1)

                        # Process refill
                        self.refill_cartridge(rom_address)

                        self.log.info("")
                        self.log.info("Waiting for next cartridge...")
                        self.log.info("")

                    # Echo other messages
                    elif line and not line.startswith("Waiting"):
                        self.log.debug(f"Device: {line}")

                time.sleep(0.1)

        except KeyboardInterrupt:
            self.log.info("")
            self.log.info("Shutting down...")
            self.running = False

        finally:
            if self.bridge:
                self.bridge.close()

        return True


def main():
    parser = argparse.ArgumentParser(
        description='Auto-Refill Daemon for Stratasys Cartridges',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 autorefill_daemon.py /dev/cu.usbserial-0001
  python3 autorefill_daemon.py COM3 --machine prodigy
  python3 autorefill_daemon.py /dev/ttyUSB0 --threshold 15.0 --auto-detect
  sudo python3 autorefill_daemon.py /dev/ttyUSB0 --daemon

Machine Types:
  fox, prodigy, quantum, uprint, uprintse, dimension, fortus
        """
    )

    parser.add_argument('port', help='Serial port (e.g., /dev/cu.usbserial-0001, COM3)')
    parser.add_argument('-m', '--machine', default='prodigy',
                        help='Machine type (default: prodigy)')
    parser.add_argument('-t', '--threshold', type=float, default=10.0,
                        help='Refill threshold in cubic inches (default: 10.0)')
    parser.add_argument('-a', '--auto-detect', action='store_true',
                        help='Auto-detect machine type (tries all types)')
    parser.add_argument('-d', '--daemon', action='store_true',
                        help='Run as background daemon (Linux/Pi only)')

    args = parser.parse_args()

    # Validate machine type
    if args.machine not in machine.get_machine_types() and not args.auto_detect:
        print(f"Error: Invalid machine type '{args.machine}'")
        print(f"Valid types: {', '.join(machine.get_machine_types())}")
        sys.exit(1)

    # Create and run daemon
    daemon = AutoRefillDaemon(
        port=args.port,
        machine_type=args.machine,
        threshold=args.threshold,
        auto_detect=args.auto_detect
    )

    # Run as daemon on Linux/Raspberry Pi
    if args.daemon:
        try:
            import daemon as daemon_lib
            with daemon_lib.DaemonContext():
                daemon.run()
        except ImportError:
            print("Error: python-daemon not installed")
            print("Install with: pip3 install python-daemon")
            sys.exit(1)
    else:
        # Run normally
        success = daemon.run()
        sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
