#!/usr/bin/env python3
"""
Raspberry Pi 1-Wire Bridge for Stratasys Cartridge Programmer

This script provides the same serial protocol as the ESP32/ESP8266 firmware,
but runs on Raspberry Pi (Zero 2, 3, 4, etc.)

Hardware:
- GPIO17 (Pin 11): 1-wire data line (with 4.7k pull-up to 3.3V)
- USB Serial or GPIO UART: Command interface (115200 baud)

Requirements:
    sudo apt-get install python3-pigpio
    sudo systemctl enable pigpiod
    sudo systemctl start pigpiod

Usage:
    # Run as serial daemon on /dev/ttyUSB0 (USB-Serial adapter)
    sudo python3 onewire_bridge.py /dev/ttyUSB0

    # Or run on hardware UART
    sudo python3 onewire_bridge.py /dev/ttyAMA0

    # Or run as TCP server (for testing)
    sudo python3 onewire_bridge.py --tcp 5000
"""

import sys
import time
import pigpio
import serial
import socket

# Configuration
ONEWIRE_PIN = 17  # GPIO17 (Pin 11)
BAUDRATE = 115200

class OneWireHandler:
    """1-Wire protocol handler using pigpio"""

    # DS2433 Commands
    CMD_READ_MEMORY = 0xF0
    CMD_WRITE_SCRATCHPAD = 0x0F
    CMD_READ_SCRATCHPAD = 0xAA
    CMD_COPY_SCRATCHPAD = 0x55
    CMD_MATCH_ROM = 0x55
    CMD_SEARCH_ROM = 0xF0
    CMD_READ_ROM = 0x33
    CMD_SKIP_ROM = 0xCC

    def __init__(self, pin=ONEWIRE_PIN):
        self.pin = pin
        self.pi = pigpio.pi()
        if not self.pi.connected:
            raise Exception("Failed to connect to pigpiod. Is it running? (sudo systemctl start pigpiod)")

        self.rom_address = None

    def reset(self):
        """Reset the 1-wire bus and check for presence"""
        # Pull line low for 480us
        self.pi.set_mode(self.pin, pigpio.OUTPUT)
        self.pi.write(self.pin, 0)
        time.sleep(0.00048)  # 480us

        # Release and wait for presence pulse
        self.pi.set_mode(self.pin, pigpio.INPUT)
        time.sleep(0.00007)  # 70us

        # Check for presence (device pulls low)
        presence = not self.pi.read(self.pin)

        # Wait for presence pulse to complete
        time.sleep(0.00041)  # 410us

        return presence

    def write_bit(self, bit):
        """Write a single bit to the bus"""
        self.pi.set_mode(self.pin, pigpio.OUTPUT)

        if bit:
            # Write 1: pull low for 6us, then release
            self.pi.write(self.pin, 0)
            time.sleep(0.000006)  # 6us
            self.pi.write(self.pin, 1)
            time.sleep(0.000064)  # 64us
        else:
            # Write 0: pull low for 60us, then release
            self.pi.write(self.pin, 0)
            time.sleep(0.00006)  # 60us
            self.pi.write(self.pin, 1)
            time.sleep(0.00001)  # 10us

    def read_bit(self):
        """Read a single bit from the bus"""
        self.pi.set_mode(self.pin, pigpio.OUTPUT)

        # Pull low for 3us to start read slot
        self.pi.write(self.pin, 0)
        time.sleep(0.000003)  # 3us

        # Release and read within 15us
        self.pi.set_mode(self.pin, pigpio.INPUT)
        time.sleep(0.000009)  # 9us

        bit = self.pi.read(self.pin)

        # Wait for slot to complete
        time.sleep(0.000055)  # 55us

        return bit

    def write_byte(self, byte):
        """Write a byte to the bus"""
        for i in range(8):
            self.write_bit((byte >> i) & 1)

    def read_byte(self):
        """Read a byte from the bus"""
        byte = 0
        for i in range(8):
            if self.read_bit():
                byte |= (1 << i)
        return byte

    def search(self):
        """Search for 1-wire device and store ROM address"""
        if not self.reset():
            return False

        self.write_byte(self.CMD_READ_ROM)  # Read ROM (works with single device)

        rom = []
        for i in range(8):
            rom.append(self.read_byte())

        # Verify CRC8
        if self.crc8(rom[:-1]) != rom[-1]:
            return False

        self.rom_address = rom
        return True

    def crc8(self, data):
        """Calculate CRC8 for 1-wire"""
        crc = 0
        for byte in data:
            crc ^= byte
            for i in range(8):
                if crc & 1:
                    crc = (crc >> 1) ^ 0x8C
                else:
                    crc >>= 1
        return crc

    def read_memory(self, addr, length):
        """Read from EEPROM"""
        if not self.rom_address:
            return None

        if not self.reset():
            return None

        # Select device
        self.write_byte(self.CMD_SKIP_ROM)

        # Read memory command
        self.write_byte(self.CMD_READ_MEMORY)
        self.write_byte(addr & 0xFF)
        self.write_byte((addr >> 8) & 0xFF)

        # Read data
        data = []
        for i in range(length):
            data.append(self.read_byte())

        return bytes(data)

    def write_block(self, addr, data):
        """Write a block (up to 32 bytes) to EEPROM"""
        if len(data) > 32:
            return False

        if not self.reset():
            return False

        # Select device
        self.write_byte(self.CMD_SKIP_ROM)

        # Write scratchpad
        self.write_byte(self.CMD_WRITE_SCRATCHPAD)
        self.write_byte(addr & 0xFF)
        self.write_byte((addr >> 8) & 0xFF)

        for byte in data:
            self.write_byte(byte)

        time.sleep(0.01)  # 10ms for scratchpad write

        # Read scratchpad to verify
        if not self.reset():
            return False

        self.write_byte(self.CMD_SKIP_ROM)
        self.write_byte(self.CMD_READ_SCRATCHPAD)

        ta1 = self.read_byte()
        ta2 = self.read_byte()
        es = self.read_byte()

        # Verify address
        if ta1 != (addr & 0xFF) or ta2 != ((addr >> 8) & 0xFF):
            return False

        # Verify data
        for byte in data:
            if self.read_byte() != byte:
                return False

        # Copy scratchpad to EEPROM
        if not self.reset():
            return False

        self.write_byte(self.CMD_SKIP_ROM)
        self.write_byte(self.CMD_COPY_SCRATCHPAD)
        self.write_byte(ta1)
        self.write_byte(ta2)
        self.write_byte(es)

        # Wait for copy to complete (up to 10ms)
        time.sleep(0.015)

        return True

    def write_memory(self, addr, data):
        """Write data to EEPROM in 32-byte blocks"""
        offset = 0
        while offset < len(data):
            # Calculate block size respecting page boundaries
            current_addr = addr + offset
            page_end = ((current_addr // 32) + 1) * 32
            bytes_to_page_end = page_end - current_addr

            block_size = min(len(data) - offset, bytes_to_page_end, 32)
            block_data = data[offset:offset + block_size]

            if not self.write_block(current_addr, block_data):
                return False

            offset += block_size
            time.sleep(0.05)  # 50ms between blocks

        return True

    def close(self):
        """Clean up pigpio connection"""
        self.pi.stop()


class SerialProtocol:
    """Serial protocol handler"""

    def __init__(self, ow_handler):
        self.ow = ow_handler

    def process_command(self, command):
        """Process a command and return response"""
        command = command.strip().upper()

        if command == "VERSION":
            return "Raspberry Pi 1-Wire Bridge v1.0"

        elif command == "RESET":
            if self.ow.reset():
                return "OK"
            else:
                return "ERROR Reset failed"

        elif command == "SEARCH":
            if self.ow.search():
                rom_hex = ''.join(f'{b:02x}' for b in self.ow.rom_address)
                return f"ROM:{rom_hex}"
            else:
                return "ERROR No device found"

        elif command.startswith("READ "):
            try:
                size = int(command.split()[1])
                if size <= 0 or size > 512:
                    return "ERROR Invalid size"

                if not self.ow.rom_address:
                    return "ERROR No device found, run SEARCH first"

                data = self.ow.read_memory(0, size)
                if data:
                    return f"DATA:{data.hex()}"
                else:
                    return "ERROR Read failed"
            except (ValueError, IndexError):
                return "ERROR Invalid READ command"

        elif command.startswith("WRITE "):
            try:
                parts = command.split()
                size = int(parts[1])
                hex_data = parts[2]

                if size <= 0 or size > 512:
                    return "ERROR Invalid size"

                if not self.ow.rom_address:
                    return "ERROR No device found, run SEARCH first"

                data = bytes.fromhex(hex_data)
                if len(data) != size:
                    return "ERROR Size mismatch"

                if self.ow.write_memory(0, data):
                    return "OK"
                else:
                    return "ERROR Write failed"
            except (ValueError, IndexError):
                return "ERROR Invalid WRITE command"

        elif command == "DEBUG":
            # Check GPIO state
            self.ow.pi.set_mode(self.ow.pin, pigpio.INPUT)
            pin_state = self.ow.pi.read(self.ow.pin)

            output = []
            output.append(f"DEBUG: Testing 1-wire bus on GPIO{self.ow.pin}...")
            output.append("  Required: 4.7k pullup to 3.3V + EEPROM data line")
            output.append("")
            output.append(f"  GPIO{self.ow.pin} state (idle): {'HIGH (good - pullup present)' if pin_state else 'LOW (BAD - no pullup or short to ground!)'}")
            output.append("")

            # Try reset multiple times
            for i in range(5):
                presence = self.ow.reset()
                output.append(f"  Reset #{i+1}: {'PRESENCE DETECTED (device found!)' if presence else 'NO PRESENCE (no device responding)'}")

            output.append("")
            output.append(f"DEBUG: If GPIO{self.ow.pin}=LOW, add 4.7k resistor from GPIO{self.ow.pin} to 3.3V")
            output.append(f"DEBUG: If GPIO{self.ow.pin}=HIGH but no presence, check EEPROM connection")

            return "\n".join(output)

        else:
            return "ERROR Unknown command"


def run_serial_daemon(port):
    """Run as serial daemon on specified port"""
    print(f"Starting 1-Wire Bridge on {port}...")

    try:
        ow = OneWireHandler(ONEWIRE_PIN)
        protocol = SerialProtocol(ow)

        ser = serial.Serial(port, BAUDRATE, timeout=1)

        print("Raspberry Pi 1-Wire Bridge v1.0")
        print("Ready")

        while True:
            if ser.in_waiting:
                line = ser.readline().decode('ascii', errors='ignore').strip()
                if line:
                    response = protocol.process_command(line)
                    ser.write((response + '\n').encode('ascii'))
                    ser.flush()

    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        ow.close()
        ser.close()


def run_tcp_server(port):
    """Run as TCP server (for testing)"""
    print(f"Starting 1-Wire Bridge TCP server on port {port}...")

    try:
        ow = OneWireHandler(ONEWIRE_PIN)
        protocol = SerialProtocol(ow)

        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(('0.0.0.0', port))
        server.listen(1)

        print("Raspberry Pi 1-Wire Bridge v1.0")
        print(f"Listening on port {port}...")

        while True:
            client, addr = server.accept()
            print(f"Client connected: {addr}")

            client.send(b"Raspberry Pi 1-Wire Bridge v1.0\nReady\n")

            try:
                while True:
                    data = client.recv(1024)
                    if not data:
                        break

                    line = data.decode('ascii', errors='ignore').strip()
                    if line:
                        response = protocol.process_command(line)
                        client.send((response + '\n').encode('ascii'))
            except:
                pass
            finally:
                client.close()
                print(f"Client disconnected: {addr}")

    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        ow.close()
        server.close()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage:")
        print("  sudo python3 onewire_bridge.py /dev/ttyUSB0       # Serial mode")
        print("  sudo python3 onewire_bridge.py /dev/ttyAMA0       # UART mode")
        print("  sudo python3 onewire_bridge.py --tcp 5000         # TCP server mode")
        sys.exit(1)

    if sys.argv[1] == "--tcp":
        port = int(sys.argv[2]) if len(sys.argv) > 2 else 5000
        run_tcp_server(port)
    else:
        run_serial_daemon(sys.argv[1])
