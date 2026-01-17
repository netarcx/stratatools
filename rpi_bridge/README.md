# Raspberry Pi 1-Wire Bridge

Python implementation of the 1-wire bridge protocol for Raspberry Pi (Zero 2, 3, 4, etc.)

## Features

- Same serial protocol as ESP32/ESP8266 firmware
- Works with existing stratatools Python code
- No Arduino/firmware flashing needed
- Three operating modes: Serial, UART, TCP server

## Hardware Requirements

- **Raspberry Pi:** Zero 2, 3, 4, or any model with GPIO
- **EEPROM:** DS2433 or DS2432 (Stratasys cartridge)
- **Resistor:** 4.7kΩ pull-up resistor
- **Optional:** USB-Serial adapter (if not using hardware UART)

## Installation

### 1. Install Dependencies

```bash
sudo apt-get update
sudo apt-get install python3-pigpio
sudo systemctl enable pigpiod
sudo systemctl start pigpiod
pip3 install pyserial
```

### 2. Copy Script

```bash
# Copy from computer to Pi
scp onewire_bridge.py pi@raspberrypi.local:~/

# Or download directly
wget https://raw.githubusercontent.com/YOUR_REPO/stratatools/main/rpi_bridge/onewire_bridge.py
chmod +x onewire_bridge.py
```

### 3. Test pigpiod

```bash
# Check if running
sudo systemctl status pigpiod

# If not running
sudo systemctl start pigpiod
```

## Wiring

```
Raspberry Pi          Connection
──────────────────────────────────────
GPIO17 (Pin 11)    →  EEPROM DQ (Pin 4)
3.3V (Pin 1)       →  4.7kΩ resistor → GPIO17
3.3V (Pin 1)       →  EEPROM VCC (Pin 8)
GND (Pin 6)        →  EEPROM GND (Pin 1)
```

**Physical Pin Layout:**

```
     ┌─────────┐
3.3V [1]  [2]  5V
     [3]  [4]  5V
     [5]  [6]  GND ← Connect EEPROM GND here
     [7]  [8]
 GND [9]  [10]
→ 17 [11] [12] ← Connect EEPROM DQ here (via 4.7kΩ to 3.3V)
     [13] [14]
     ...
```

## Usage

### Mode 1: USB-Serial Adapter (Recommended)

Connect a USB-serial adapter (FTDI, CH340, etc.) to the Raspberry Pi's USB port.

```bash
sudo python3 onewire_bridge.py /dev/ttyUSB0
```

From your computer:

```python
from stratatools.helper.esp32_bridge import ESP32Bridge

# Connect to Raspberry Pi via USB-Serial
bridge = ESP32Bridge('/dev/ttyUSB0')  # Or COM port on Windows
bridge.initialize()
rom = bridge.onewire_macro_search()
print(f'Device: {rom}')
bridge.close()
```

### Mode 2: Hardware UART

Enable the Raspberry Pi's hardware UART:

```bash
# Edit boot config
sudo nano /boot/config.txt

# Add this line
enable_uart=1

# Save and exit, then:
sudo raspi-config
# → Interface Options → Serial Port
# → Login shell over serial? NO
# → Serial hardware enabled? YES

# Reboot
sudo reboot
```

Run the bridge:

```bash
sudo python3 onewire_bridge.py /dev/ttyAMA0
```

### Mode 3: TCP Server (Testing/Development)

```bash
sudo python3 onewire_bridge.py --tcp 5000
```

Connect from any computer on the network:

```python
import socket

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(('raspberrypi.local', 5000))

s.send(b'VERSION\n')
print(s.recv(1024).decode())
# Output: Raspberry Pi 1-Wire Bridge v1.0

s.send(b'SEARCH\n')
print(s.recv(1024).decode())
# Output: ROM:2389b7e90200005f

s.close()
```

## Auto-Start on Boot

Create a systemd service to run the bridge automatically:

```bash
sudo nano /etc/systemd/system/onewire-bridge.service
```

Paste this configuration:

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

## Protocol Commands

The bridge supports the same commands as the ESP32/ESP8266 firmware:

| Command | Response | Description |
|---------|----------|-------------|
| `VERSION` | `Raspberry Pi 1-Wire Bridge v1.0` | Get version |
| `RESET` | `OK` or `ERROR` | Reset 1-wire bus |
| `SEARCH` | `ROM:XXXXXXXX` | Find device |
| `READ 512` | `DATA:XXXX...` | Read EEPROM |
| `WRITE 512 XXXX...` | `OK` | Write EEPROM |
| `DEBUG` | Multi-line | Hardware diagnostics |

## Testing

### Basic Connection Test

```bash
# In one terminal
sudo python3 onewire_bridge.py /dev/ttyUSB0

# In another terminal on your computer
python3 << 'EOF'
from stratatools.helper.esp32_bridge import ESP32Bridge

bridge = ESP32Bridge('/dev/ttyUSB0', timeout=3)  # Adjust port

if bridge.initialize():
    print('✓ Connected')
    rom = bridge.onewire_macro_search()
    if rom:
        print(f'✓ Device: {rom}')
    else:
        print('✗ No device')
else:
    print('✗ Connection failed')

bridge.close()
EOF
```

### Hardware Diagnostic

```bash
echo "DEBUG" | sudo python3 -c "
import sys
from onewire_bridge import OneWireHandler, SerialProtocol

ow = OneWireHandler()
protocol = SerialProtocol(ow)

for line in sys.stdin:
    response = protocol.process_command(line)
    print(response)

ow.close()
"
```

## Troubleshooting

### pigpiod not connecting

```bash
# Check if running
sudo systemctl status pigpiod

# Restart it
sudo systemctl restart pigpiod

# Check logs
sudo journalctl -u pigpiod -n 50
```

### Permission denied on /dev/ttyUSB0

```bash
# Add user to dialout group
sudo usermod -a -G dialout $USER

# Log out and back in, or:
sudo chmod 666 /dev/ttyUSB0  # Temporary fix
```

### Timing issues / unreliable reads

Raspberry Pi's Linux kernel can cause timing jitter. Solutions:

1. **Run with higher priority:**
   ```bash
   sudo nice -n -20 python3 onewire_bridge.py /dev/ttyUSB0
   ```

2. **Use a real-time kernel:**
   ```bash
   sudo apt-get install raspi-config
   sudo raspi-config
   # → Advanced Options → Real-time kernel
   ```

3. **Or use ESP32/ESP8266 instead** - they have more reliable timing

### GPIO not responding

```bash
# Test GPIO directly
python3 << 'EOF'
import pigpio

pi = pigpio.pi()
if pi.connected:
    pi.set_mode(17, pigpio.INPUT)
    print(f'GPIO17 state: {pi.read(17)}')
    pi.stop()
else:
    print('pigpiod not running')
EOF
```

## Performance Notes

- **Read speed:** ~1-2 seconds for 512 bytes
- **Write speed:** ~30-60 seconds for 512 bytes (with verification)
- **CPU usage:** <5% on Raspberry Pi Zero 2

Slower than ESP32 due to:
- Python overhead
- Linux scheduling jitter
- pigpio daemon communication

For production use, ESP32/ESP32-C3 is recommended.

## Advanced: Custom GPIO Pin

To use a different GPIO pin, edit the script:

```python
# Change this line (default is GPIO17)
ONEWIRE_PIN = 17  # Change to your desired GPIO number
```

Available pins: 2, 3, 4, 17, 27, 22, 10, 9, 11, 5, 6, 13, 19, 26, etc.

Avoid: 14, 15 (UART), 0, 1 (I2C), 8, 7 (SPI)

## Comparison with ESP32

| Feature | Raspberry Pi | ESP32 |
|---------|-------------|--------|
| **Cost** | $15-35 | $5-10 |
| **Speed** | Slower | Faster |
| **Reliability** | Good | Excellent |
| **Setup** | Easier (Python) | Requires flashing |
| **Power** | ~500mA | ~80mA |
| **Best for** | Development | Production |

## License

Same as main stratatools repository.

## Support

For issues specific to Raspberry Pi bridge:
- Check the main [MULTI_PLATFORM_GUIDE.md](../MULTI_PLATFORM_GUIDE.md)
- Open an issue on GitHub

For general stratatools questions:
- See main README.md
