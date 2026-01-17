# Multi-Platform 1-Wire Bridge Guide

This guide covers setting up the 1-wire bridge on **ESP32**, **ESP32-C3**, **ESP8266**, and **Raspberry Pi Zero 2**.

---

## Table of Contents

1. [Hardware Requirements](#hardware-requirements)
2. [ESP32 Setup](#esp32-setup)
3. [ESP32-C3 Setup](#esp32-c3-setup)
4. [ESP8266 Setup](#esp8266-setup)
5. [Raspberry Pi Zero 2 Setup](#raspberry-pi-zero-2-setup)
6. [Wiring Diagrams](#wiring-diagrams)
7. [Testing](#testing)
8. [Troubleshooting](#troubleshooting)

---

## Hardware Requirements

### For All Platforms
- **EEPROM:** DS2433 or DS2432 (Stratasys cartridge)
- **Resistor:** 4.7kΩ pull-up resistor
- **Wires:** Jumper wires for connections

### Platform-Specific

| Platform | Additional Requirements |
|----------|------------------------|
| **ESP32** | USB cable |
| **ESP32-C3** | USB cable |
| **ESP8266** | NodeMCU or similar board, USB cable |
| **Raspberry Pi Zero 2** | USB-Serial adapter OR use hardware UART |

---

## ESP32 Setup

### 1. Install PlatformIO

```bash
# Via pip
pip install platformio

# Or via Homebrew (macOS)
brew install platformio
```

### 2. Build and Flash Firmware

```bash
cd esp32_bridge

# Build for ESP32
pio run -e esp32

# Flash to ESP32 (replace port as needed)
pio run -e esp32 --target upload --upload-port /dev/cu.usbserial-0001

# Monitor serial output
pio device monitor --port /dev/cu.usbserial-0001
```

### 3. Wiring

```
ESP32 Pin    →  Connection
─────────────────────────────
GPIO4        →  EEPROM DQ (data line)
3.3V         →  4.7kΩ resistor → GPIO4 (pull-up)
3.3V         →  EEPROM VCC
GND          →  EEPROM GND
```

### 4. Verify

```bash
python3 -c "
from stratatools.helper.esp32_bridge import ESP32Bridge
bridge = ESP32Bridge('/dev/cu.usbserial-0001')
print('✓ Connected' if bridge.initialize() else '✗ Failed')
bridge.close()
"
```

---

## ESP32-C3 Setup

The ESP32-C3 uses the RISC-V architecture instead of Xtensa.

### 1. Build and Flash

```bash
cd esp32_bridge

# Build for ESP32-C3
pio run -e esp32c3

# Flash to ESP32-C3
pio run -e esp32c3 --target upload --upload-port /dev/cu.usbserial-0001
```

### 2. Wiring

**Same as ESP32:**

```
ESP32-C3 Pin →  Connection
─────────────────────────────
GPIO4        →  EEPROM DQ
3.3V         →  4.7kΩ → GPIO4
3.3V         →  EEPROM VCC
GND          →  EEPROM GND
```

---

## ESP8266 Setup

The ESP8266 (NodeMCU) uses different GPIO naming.

### 1. Build and Flash

```bash
cd esp32_bridge

# Build for ESP8266
pio run -e esp8266

# Flash to ESP8266
pio run -e esp8266 --target upload --upload-port /dev/cu.usbserial-0001
```

### 2. Wiring

**Note:** ESP8266 uses **D2** (GPIO4) for the data line.

```
ESP8266 Pin  →  Connection
─────────────────────────────
D2 (GPIO4)   →  EEPROM DQ
3.3V         →  4.7kΩ → D2
3.3V         →  EEPROM VCC
GND          →  EEPROM GND
```

**Important:** ESP8266 GPIO pins are **3.3V only**. Do not use 5V!

---

## Raspberry Pi Zero 2 Setup

The Raspberry Pi runs a Python daemon instead of Arduino firmware.

### 1. Install Dependencies

```bash
# Update system
sudo apt-get update
sudo apt-get upgrade

# Install pigpio library
sudo apt-get install python3-pigpio

# Enable and start pigpio daemon
sudo systemctl enable pigpiod
sudo systemctl start pigpiod

# Install pyserial
pip3 install pyserial
```

### 2. Copy the Bridge Script

```bash
# Copy from repository
cd /Users/trentfox/Code/stratatools/rpi_bridge
scp onewire_bridge.py pi@raspberrypi.local:~/

# Or download directly on Pi
wget https://raw.githubusercontent.com/YOUR_REPO/stratatools/main/rpi_bridge/onewire_bridge.py
```

### 3. Wiring

```
Raspberry Pi →  Connection
─────────────────────────────
GPIO17 (Pin 11) →  EEPROM DQ
3.3V (Pin 1)    →  4.7kΩ → GPIO17
3.3V (Pin 1)    →  EEPROM VCC
GND (Pin 6)     →  EEPROM GND
```

**Pin Layout (BCM numbering):**

```
     3.3V [1]  [2]  5V
    GPIO2 [3]  [4]  5V
    GPIO3 [5]  [6]  GND
    GPIO4 [7]  [8]  GPIO14
      GND [9]  [10] GPIO15
→ GPIO17 [11] [12] GPIO18  ← 1-Wire data line here
   GPIO27 [13] [14] GND
   ...
```

### 4. Run the Bridge

#### Option A: USB-Serial Adapter

Connect a USB-serial adapter to the Pi and run:

```bash
sudo python3 onewire_bridge.py /dev/ttyUSB0
```

#### Option B: Hardware UART

Enable UART on Raspberry Pi:

```bash
# Edit config
sudo nano /boot/config.txt

# Add this line:
enable_uart=1

# Disable serial console
sudo raspi-config
# → Interface Options → Serial Port
# → "Would you like a login shell over serial?" → No
# → "Would you like serial port hardware to be enabled?" → Yes

# Reboot
sudo reboot
```

Then run:

```bash
sudo python3 onewire_bridge.py /dev/ttyAMA0
```

#### Option C: TCP Server (for testing)

```bash
sudo python3 onewire_bridge.py --tcp 5000
```

Then connect from your computer:

```python
import socket
s = socket.socket()
s.connect(('raspberrypi.local', 5000))
s.send(b'VERSION\n')
print(s.recv(1024).decode())
s.close()
```

### 5. Create Systemd Service (Auto-start)

```bash
sudo nano /etc/systemd/system/onewire-bridge.service
```

Add:

```ini
[Unit]
Description=1-Wire Bridge for Stratasys Cartridges
After=pigpiod.service
Requires=pigpiod.service

[Service]
Type=simple
User=root
WorkingDirectory=/home/pi
ExecStart=/usr/bin/python3 /home/pi/onewire_bridge.py /dev/ttyUSB0
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable onewire-bridge
sudo systemctl start onewire-bridge

# Check status
sudo systemctl status onewire-bridge

# View logs
sudo journalctl -u onewire-bridge -f
```

---

## Wiring Diagrams

### DS2433/DS2432 EEPROM Pinout

```
     ┌─────┐
GND [1    8] VCC (3.3V)
     [2    7]
     [3    6]
 DQ [4    5]
     └─────┘
```

- **Pin 1:** GND
- **Pin 4:** DQ (Data) - connect to GPIO + 4.7kΩ pull-up
- **Pin 8:** VCC (3.3V)

### Complete Wiring (ESP32 Example)

```
EEPROM          4.7kΩ        ESP32
DQ (Pin 4) ─┬─── Resistor ─── 3.3V
            └───────────────── GPIO4

VCC (Pin 8) ─────────────────── 3.3V

GND (Pin 1) ─────────────────── GND
```

---

## Testing

### 1. Test Connection

```bash
python3 << 'EOF'
from stratatools.helper.esp32_bridge import ESP32Bridge
import time

# Change port as needed
# ESP32/ESP8266: /dev/cu.usbserial-0001 or COM3
# Raspberry Pi with USB-Serial: /dev/ttyUSB0
# Raspberry Pi UART: /dev/ttyAMA0

bridge = ESP32Bridge('/dev/cu.usbserial-0001', timeout=3)

if bridge.initialize():
    print('✓ Connected')

    rom = bridge.onewire_macro_search()
    if rom:
        print(f'✓ Device found: {rom}')

        data = bridge.onewire_read(32)
        if data:
            print(f'✓ Read {len(data)} bytes: {data[:16].hex()}')
        else:
            print('✗ Read failed')
    else:
        print('✗ No device found')
else:
    print('✗ Connection failed')

bridge.close()
EOF
```

### 2. Test with GUI

```bash
python3 stratatools_gui.py
```

---

## Troubleshooting

### ESP32/ESP8266

**Problem:** Device not found

**Solutions:**
1. Check wiring - especially the 4.7kΩ pull-up resistor
2. Verify correct GPIO pin (GPIO4 for ESP32/ESP32-C3, D2/GPIO4 for ESP8266)
3. Use DEBUG command:
   ```python
   from stratatools.helper.esp32_bridge import ESP32Bridge
   bridge = ESP32Bridge('/dev/cu.usbserial-0001')
   bridge.initialize()
   print(bridge._send_command('DEBUG'))
   ```
4. Check that EEPROM is getting 3.3V power

**Problem:** Upload failed

**Solutions:**
1. Hold BOOT button while uploading (ESP32-C3 especially)
2. Try different USB cable
3. Check that no other program is using the serial port
4. Try lower upload speed: `upload_speed = 115200` in platformio.ini

### Raspberry Pi

**Problem:** `Failed to connect to pigpiod`

**Solutions:**
```bash
# Start pigpiod
sudo systemctl start pigpiod

# Check if running
sudo systemctl status pigpiod

# If not installed
sudo apt-get install pigpio python3-pigpio
```

**Problem:** `Permission denied` on /dev/ttyUSB0

**Solutions:**
```bash
# Add user to dialout group
sudo usermod -a -G dialout $USER

# Or run with sudo
sudo python3 onewire_bridge.py /dev/ttyUSB0
```

**Problem:** Timing issues / unreliable reads

**Solutions:**
1. Raspberry Pi's Linux can have timing jitter
2. Use `sudo` to run with higher priority
3. Consider using a real-time kernel
4. Or use ESP32/ESP8266 for more reliable timing

---

## Performance Comparison

| Platform | Speed | Reliability | Cost | Power | Best For |
|----------|-------|-------------|------|-------|----------|
| **ESP32** | Fast | Excellent | $5-10 | Low | Production use |
| **ESP32-C3** | Fast | Excellent | $3-5 | Very Low | Budget builds |
| **ESP8266** | Medium | Good | $2-4 | Low | Legacy projects |
| **Raspberry Pi** | Slow | Good* | $15+ | Medium | Development/prototyping |

\* Timing-sensitive operations may be less reliable on Pi due to Linux scheduling

---

## Protocol Reference

All platforms use the same serial protocol:

| Command | Response | Description |
|---------|----------|-------------|
| `VERSION` | `<BOARD> 1-Wire Bridge v1.0` | Get firmware version |
| `RESET` | `OK` or `ERROR` | Reset 1-wire bus |
| `SEARCH` | `ROM:XXXXXXXX` or `ERROR` | Search for device |
| `READ 512` | `DATA:XXXX...` or `ERROR` | Read 512 bytes |
| `WRITE 512 XXXX...` | `OK` or `ERROR` | Write 512 bytes |
| `DEBUG` | Multi-line output | Hardware diagnostics |

---

## Example: Multi-Platform Setup Script

```python
#!/usr/bin/env python3
"""Auto-detect platform and connect"""

import serial.tools.list_ports

def detect_bridge():
    """Auto-detect 1-wire bridge on any platform"""
    ports = serial.tools.list_ports.comports()

    for port in ports:
        try:
            from stratatools.helper.esp32_bridge import ESP32Bridge
            bridge = ESP32Bridge(port.device, timeout=2)

            if bridge.initialize():
                version = bridge._send_command('VERSION')
                print(f'Found bridge on {port.device}: {version}')
                return bridge
        except:
            pass

    return None

if __name__ == '__main__':
    bridge = detect_bridge()
    if bridge:
        rom = bridge.onewire_macro_search()
        if rom:
            print(f'Device ROM: {rom}')
    else:
        print('No bridge found')
