#!/usr/bin/env python3
"""
Raspberry Pi Standalone Auto-Refill Station

This script runs on Raspberry Pi and provides a completely standalone
cartridge refill station - no computer needed!

Features:
- Runs autonomously on Raspberry Pi
- Auto-detects cartridge insertion
- Automatically refills when quantity is low
- LED status indicators
- Optional OLED display
- Optional button for manual refill
- Systemd service for auto-start

Hardware:
- Raspberry Pi (Zero 2, 3, 4, etc.)
- DS2433/DS2432 EEPROM (cartridge)
- 4.7k pull-up resistor
- LED for status (optional)
- Button for manual refill (optional)
- SSD1306 OLED display (optional)

Wiring:
  GPIO17 (Pin 11)  →  EEPROM DQ
  GPIO27 (Pin 13)  →  Status LED (via 220Ω resistor)
  GPIO22 (Pin 15)  →  Button (to GND)
  GPIO2/3 (I2C)    →  OLED display (optional)

Usage:
    sudo python3 autorefill_rpi.py
    sudo python3 autorefill_rpi.py --threshold 15.0
    sudo python3 autorefill_rpi.py --machine prodigy --display
"""

import sys
import time
import argparse
import logging
from datetime import datetime

try:
    import pigpio
except ImportError:
    print("Error: pigpio not installed")
    print("Install with: sudo apt-get install python3-pigpio")
    sys.exit(1)

from stratatools.manager import Manager
from stratatools.crypto import Desx_Crypto
from stratatools.checksum import Crc16_Checksum
from stratatools import machine, cartridge_pb2
from google.protobuf.timestamp_pb2 import Timestamp

# GPIO Pins
ONEWIRE_PIN = 17  # GPIO17 - 1-wire data
LED_PIN = 27      # GPIO27 - Status LED
BUTTON_PIN = 22   # GPIO22 - Manual refill button

# Optional OLED display
try:
    from luma.core.interface.serial import i2c
    from luma.oled.device import ssd1306
    from luma.core.render import canvas
    from PIL import ImageFont
    OLED_AVAILABLE = True
except ImportError:
    OLED_AVAILABLE = False


class OneWireHandler:
    """1-Wire protocol handler using pigpio"""

    def __init__(self, pin=ONEWIRE_PIN):
        self.pin = pin
        self.pi = pigpio.pi()
        if not self.pi.connected:
            raise Exception("Failed to connect to pigpiod")
        self.rom_address = None

    def reset(self):
        """Reset 1-wire bus"""
        self.pi.set_mode(self.pin, pigpio.OUTPUT)
        self.pi.write(self.pin, 0)
        time.sleep(0.00048)
        self.pi.set_mode(self.pin, pigpio.INPUT)
        time.sleep(0.00007)
        presence = not self.pi.read(self.pin)
        time.sleep(0.00041)
        return presence

    def write_bit(self, bit):
        """Write single bit"""
        self.pi.set_mode(self.pin, pigpio.OUTPUT)
        if bit:
            self.pi.write(self.pin, 0)
            time.sleep(0.000006)
            self.pi.write(self.pin, 1)
            time.sleep(0.000064)
        else:
            self.pi.write(self.pin, 0)
            time.sleep(0.00006)
            self.pi.write(self.pin, 1)
            time.sleep(0.00001)

    def read_bit(self):
        """Read single bit"""
        self.pi.set_mode(self.pin, pigpio.OUTPUT)
        self.pi.write(self.pin, 0)
        time.sleep(0.000003)
        self.pi.set_mode(self.pin, pigpio.INPUT)
        time.sleep(0.000009)
        bit = self.pi.read(self.pin)
        time.sleep(0.000055)
        return bit

    def write_byte(self, byte):
        """Write byte"""
        for i in range(8):
            self.write_bit((byte >> i) & 1)

    def read_byte(self):
        """Read byte"""
        byte = 0
        for i in range(8):
            if self.read_bit():
                byte |= (1 << i)
        return byte

    def search(self):
        """Search for device and store ROM"""
        if not self.reset():
            return False

        self.write_byte(0x33)  # Read ROM
        rom = []
        for i in range(8):
            rom.append(self.read_byte())

        # Verify CRC
        crc = 0
        for byte in rom[:-1]:
            crc ^= byte
            for i in range(8):
                if crc & 1:
                    crc = (crc >> 1) ^ 0x8C
                else:
                    crc >>= 1

        if crc != rom[-1]:
            return False

        self.rom_address = rom
        return True

    def read_memory(self, addr, length):
        """Read from EEPROM"""
        if not self.reset():
            return None

        self.write_byte(0xCC)  # Skip ROM
        self.write_byte(0xF0)  # Read memory
        self.write_byte(addr & 0xFF)
        self.write_byte((addr >> 8) & 0xFF)

        data = []
        for i in range(length):
            data.append(self.read_byte())
        return bytes(data)

    def write_memory(self, addr, data):
        """Write to EEPROM"""
        offset = 0
        while offset < len(data):
            current_addr = addr + offset
            page_end = ((current_addr // 32) + 1) * 32
            block_size = min(len(data) - offset, page_end - current_addr, 32)

            if not self._write_block(current_addr, data[offset:offset + block_size]):
                return False

            offset += block_size
            time.sleep(0.05)
        return True

    def _write_block(self, addr, data):
        """Write single block"""
        if not self.reset():
            return False

        self.write_byte(0xCC)  # Skip ROM
        self.write_byte(0x0F)  # Write scratchpad
        self.write_byte(addr & 0xFF)
        self.write_byte((addr >> 8) & 0xFF)

        for byte in data:
            self.write_byte(byte)

        time.sleep(0.01)

        # Verify scratchpad
        if not self.reset():
            return False

        self.write_byte(0xCC)
        self.write_byte(0xAA)  # Read scratchpad

        ta1 = self.read_byte()
        ta2 = self.read_byte()
        es = self.read_byte()

        for byte in data:
            if self.read_byte() != byte:
                return False

        # Copy scratchpad
        if not self.reset():
            return False

        self.write_byte(0xCC)
        self.write_byte(0x55)  # Copy scratchpad
        self.write_byte(ta1)
        self.write_byte(ta2)
        self.write_byte(es)

        time.sleep(0.015)
        return True

    def get_rom_hex(self):
        """Get ROM as hex string"""
        if not self.rom_address:
            return None
        return ''.join(f'{b:02x}' for b in self.rom_address)

    def close(self):
        """Cleanup"""
        self.pi.stop()


class AutoRefillStation:
    """Standalone auto-refill station"""

    def __init__(self, machine_type='prodigy', threshold=10.0, use_display=False):
        self.machine_type = machine_type
        self.threshold = threshold
        self.use_display = use_display and OLED_AVAILABLE

        self.ow = OneWireHandler(ONEWIRE_PIN)
        self.pi = self.ow.pi
        self.manager = Manager(Desx_Crypto(), Crc16_Checksum())

        # Setup GPIO
        self.pi.set_mode(LED_PIN, pigpio.OUTPUT)
        self.pi.set_mode(BUTTON_PIN, pigpio.INPUT)
        self.pi.set_pull_up_down(BUTTON_PIN, pigpio.PUD_UP)

        # Setup display
        self.display = None
        if self.use_display:
            try:
                serial = i2c(port=1, address=0x3C)
                self.display = ssd1306(serial)
            except:
                self.display = None

        # State
        self.last_device_present = False
        self.button_pressed = False

        # Logging
        logging.basicConfig(
            level=logging.INFO,
            format='%(asctime)s - %(message)s',
            datefmt='%H:%M:%S'
        )
        self.log = logging.getLogger(__name__)

    def set_led(self, state):
        """Set LED state"""
        self.pi.write(LED_PIN, 1 if state else 0)

    def blink_led(self, count=1, delay=0.2):
        """Blink LED"""
        for i in range(count):
            self.set_led(True)
            time.sleep(delay)
            self.set_led(False)
            time.sleep(delay)

    def show_message(self, line1, line2="", line3="", line4=""):
        """Show message on display"""
        if not self.display:
            return

        with canvas(self.display) as draw:
            draw.text((0, 0), line1, fill="white")
            if line2:
                draw.text((0, 16), line2, fill="white")
            if line3:
                draw.text((0, 32), line3, fill="white")
            if line4:
                draw.text((0, 48), line4, fill="white")

    def refill_cartridge(self, rom_hex):
        """Refill cartridge"""
        try:
            self.log.info(f"Processing cartridge {rom_hex}")
            self.show_message("Reading", "cartridge...")
            self.blink_led(3, 0.1)

            # Read EEPROM
            data = self.ow.read_memory(0, 512)
            if not data:
                raise Exception("Read failed")

            # Decode
            machine_number = machine.get_number_from_type(self.machine_type)
            eeprom_uid = bytes.fromhex(rom_hex)

            cartridge = self.manager.decode(machine_number, eeprom_uid, bytearray(data))

            # Check quantity
            initial = cartridge.initial_material_quantity
            current = cartridge.current_material_quantity
            pct = (current / initial * 100) if initial > 0 else 0

            self.log.info(f"Material: {cartridge.material_name}")
            self.log.info(f"Current: {current:.2f} / {initial:.2f} cu.in ({pct:.0f}%)")

            self.show_message(
                cartridge.material_name[:16],
                f"{current:.1f}/{initial:.1f} in",
                f"{pct:.0f}% remaining"
            )
            time.sleep(2)

            if current >= self.threshold:
                self.log.info("Above threshold - no refill needed")
                self.show_message("Cartridge OK", f"{pct:.0f}% full", "No refill needed")
                self.set_led(True)
                time.sleep(3)
                return False

            # Refill
            self.log.info("REFILLING...")
            self.show_message("Refilling...", "Please wait")

            cartridge.current_material_quantity = initial
            now = Timestamp()
            now.GetCurrentTime()
            cartridge.last_use_date.CopyFrom(now)

            # Encode and write
            encoded = self.manager.encode(machine_number, eeprom_uid, cartridge)

            for i in range(3):
                self.blink_led(1, 0.1)
                time.sleep(0.1)

            if not self.ow.write_memory(0, bytes(encoded)):
                raise Exception("Write failed")

            # Verify
            time.sleep(1)
            verify = self.ow.read_memory(0, 512)

            if bytes(verify) == bytes(encoded):
                self.log.info("✓ REFILL SUCCESSFUL!")
                self.show_message("SUCCESS!", "Cartridge", "refilled to", "100%")
                self.blink_led(5, 0.2)
                self.set_led(True)
                time.sleep(3)
                return True
            else:
                raise Exception("Verification failed")

        except Exception as e:
            self.log.error(f"Refill failed: {e}")
            self.show_message("ERROR!", str(e)[:16])
            self.blink_led(10, 0.1)
            time.sleep(3)
            return False

    def run(self):
        """Main loop"""
        self.log.info("=" * 50)
        self.log.info("Raspberry Pi Auto-Refill Station v1.0")
        self.log.info("=" * 50)
        self.log.info(f"Machine type: {self.machine_type}")
        self.log.info(f"Threshold: {self.threshold:.2f} cu.in")
        self.log.info("=" * 50)

        self.show_message("Auto-Refill", "Station", "Ready")
        self.blink_led(2, 0.5)

        try:
            while True:
                # Check for cartridge
                device_present = self.ow.search()

                # Insertion detected
                if device_present and not self.last_device_present:
                    rom_hex = self.ow.get_rom_hex()
                    self.log.info(f"Cartridge detected: {rom_hex}")
                    self.show_message("Cartridge", "detected!")
                    time.sleep(1)
                    self.refill_cartridge(rom_hex)

                # Removal detected
                if not device_present and self.last_device_present:
                    self.log.info("Cartridge removed")
                    self.show_message("Cartridge", "removed")
                    self.set_led(False)
                    time.sleep(1)
                    self.show_message("Ready")

                self.last_device_present = device_present

                # Check button
                if not self.pi.read(BUTTON_PIN) and not self.button_pressed and device_present:
                    self.button_pressed = True
                    self.log.info("Manual refill triggered")
                    rom_hex = self.ow.get_rom_hex()
                    self.refill_cartridge(rom_hex)

                if self.pi.read(BUTTON_PIN):
                    self.button_pressed = False

                # Slow blink if waiting
                if not device_present:
                    self.blink_led(1, 0.05)
                    time.sleep(2)
                else:
                    time.sleep(0.5)

        except KeyboardInterrupt:
            self.log.info("Shutting down...")
        finally:
            self.set_led(False)
            self.ow.close()


def main():
    parser = argparse.ArgumentParser(description='Raspberry Pi Auto-Refill Station')
    parser.add_argument('-m', '--machine', default='prodigy', help='Machine type')
    parser.add_argument('-t', '--threshold', type=float, default=10.0,
                        help='Refill threshold (cu.in)')
    parser.add_argument('-d', '--display', action='store_true',
                        help='Enable OLED display')

    args = parser.parse_args()

    station = AutoRefillStation(
        machine_type=args.machine,
        threshold=args.threshold,
        use_display=args.display
    )

    station.run()


if __name__ == '__main__':
    main()
