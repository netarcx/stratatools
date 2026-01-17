# Stratasys Auto-Refill Device

Autonomous cartridge refill device that automatically resets filament quantity when cartridges are inserted.

## Features

- **Automatic Detection**: Detects cartridge insertion via 1-wire
- **Auto-Refill**: Automatically refills cartridges below threshold
- **LED Status**: Visual feedback with 5 different blink patterns
- **Manual Button**: Optional button for manual refill
- **Multi-Platform**: Works on ESP32 and ESP8266

## Hardware

### Required
- **ESP32** or **ESP8266** development board
- **DS2433/DS2432 EEPROM** (Stratasys cartridge)
- **4.7kΩ pull-up resistor**

### Optional
- LED for status indication (built-in LED works)
- Button for manual refill

## Wiring

### ESP32
```
ESP32              Connection
─────────────────────────────────
GPIO4   →  EEPROM DQ (Pin 4)
3.3V    →  4.7kΩ resistor → GPIO4
3.3V    →  EEPROM VCC (Pin 8)
GND     →  EEPROM GND (Pin 1)
GPIO2   →  Status LED (optional)
GPIO0   →  Button to GND (optional)
```

### ESP8266
```
ESP8266            Connection
─────────────────────────────────
D2      →  EEPROM DQ (Pin 4)
3.3V    →  4.7kΩ resistor → D2
3.3V    →  EEPROM VCC (Pin 8)
GND     →  EEPROM GND (Pin 1)
D4      →  Status LED (optional)
D3      →  Button to GND (optional)
```

## LED Status Patterns

- **Slow blink (1 Hz)**: Waiting for cartridge
- **Fast blink (5 Hz)**: Reading cartridge
- **Solid ON**: Cartridge OK (above threshold)
- **Triple blink**: Refilling in progress
- **Rapid blink (10 Hz)**: Error occurred

## Building Firmware

### Install PlatformIO
```bash
pip3 install platformio
```

### Build for ESP32
```bash
cd esp32_autorefill
pio run -e esp32
```

### Build for ESP8266
```bash
cd esp32_autorefill
pio run -e esp8266
```

### Flash to Device
```bash
# ESP32
pio run -e esp32 --target upload --upload-port /dev/cu.usbserial-0001

# ESP8266
pio run -e esp8266 --target upload --upload-port /dev/cu.usbserial-0001
```

Replace `/dev/cu.usbserial-0001` with your device's serial port:
- macOS: `/dev/cu.usbserial-*` or `/dev/tty.usbserial-*`
- Linux: `/dev/ttyUSB0` or `/dev/ttyACM0`
- Windows: `COM3`, `COM4`, etc.

## Usage

### Two Operating Modes

#### Mode 1: With Computer (Daemon)

The device detects cartridges and notifies a computer daemon to perform refills.

**1. Flash firmware to ESP32/ESP8266**
```bash
pio run -e esp32 --target upload --upload-port /dev/cu.usbserial-0001
```

**2. Run the Python daemon on your computer**
```bash
python3 autorefill_daemon.py /dev/cu.usbserial-0001
```

**3. Insert cartridge** - automatic refill if below threshold

**Daemon Options:**
```bash
# Custom threshold (default: 10.0 cubic inches)
python3 autorefill_daemon.py /dev/cu.usbserial-0001 --threshold 15.0

# Specify machine type (default: prodigy)
python3 autorefill_daemon.py /dev/cu.usbserial-0001 --machine fox

# Auto-detect machine type
python3 autorefill_daemon.py /dev/cu.usbserial-0001 --auto-detect

# Run as background daemon (Linux only)
sudo python3 autorefill_daemon.py /dev/ttyUSB0 --daemon
```

**Supported machine types:** fox, prodigy, quantum, uprint, uprintse, dimension, fortus

#### Mode 2: Standalone (Raspberry Pi Only)

For truly autonomous operation without a computer, use the Raspberry Pi version:

**See:** `autorefill_rpi.py` and the Raspberry Pi setup guide

The Pi version runs completely standalone with:
- Direct 1-wire control via pigpio
- On-device cartridge decode/encode
- LED status indicators
- Optional OLED display
- Optional manual refill button

## Configuration

### Adjust Threshold

Edit `platformio.ini`:
```ini
build_flags =
    -DAUTO_REFILL_THRESHOLD=15.0  ; Change from 10.0 to 15.0
```

### Change GPIO Pins

Edit `platformio.ini`:
```ini
[env:esp32]
build_flags =
    -DONEWIRE_PIN=4      ; Change 1-wire pin
    -DSTATUS_LED=2       ; Change LED pin
    -DBUTTON_PIN=0       ; Change button pin
```

### Rebuild after changes
```bash
pio run --target clean
pio run -e esp32
```

## Serial Protocol

The device communicates via serial (115200 baud) with the following protocol:

### Device → Daemon

| Message | Description |
|---------|-------------|
| `CARTRIDGE_INSERTED:ROM` | Cartridge detected with ROM address |
| `Cartridge removed` | Cartridge was removed |

### Daemon → Device

| Command | Response | Description |
|---------|----------|-------------|
| `STATUS` | Device status | Query current state |
| `REFILLING` | LED: Triple blink | Notify refill starting |
| `REFILL_DONE:SUCCESS` | LED: Celebration | Refill completed |
| `REFILL_DONE:NO_REFILL_NEEDED` | LED: Solid | Above threshold |
| `ERROR:message` | LED: Rapid blink | Error occurred |

## Testing

### Test Device Detection
```bash
# Open serial monitor
pio device monitor --port /dev/cu.usbserial-0001

# Insert cartridge - should see:
# *** CARTRIDGE DETECTED ***
# ROM: 2389b7e90200005f
# Waiting for refill daemon...
```

### Test with Daemon
```bash
# Terminal 1: Run daemon
python3 autorefill_daemon.py /dev/cu.usbserial-0001 --machine prodigy

# Terminal 2: Monitor serial output
pio device monitor --port /dev/cu.usbserial-0001

# Insert cartridge below threshold
# - Device: LED fast blink
# - Daemon: Reads, decodes, checks quantity
# - Daemon: Refills if < threshold
# - Device: LED celebration blink
```

## Troubleshooting

### Device Not Detected
```bash
# List available ports
pio device list

# Check if device is connected
ls -l /dev/cu.usbserial-* # macOS
ls -l /dev/ttyUSB* # Linux
```

### Build Errors
```bash
# Clean and rebuild
pio run --target clean
pio run -e esp32
```

### Upload Errors

**Permission denied:**
```bash
# Linux: Add user to dialout group
sudo usermod -a -G dialout $USER
# Log out and back in

# Or temporary fix
sudo chmod 666 /dev/ttyUSB0
```

**Device in use:**
- Close serial monitor before uploading
- Close any programs using the serial port

### Cartridge Not Detected

1. **Check wiring** - verify 4.7kΩ pull-up resistor
2. **Test with known good cartridge**
3. **Check 1-wire bus with DEBUG command:**
   ```bash
   echo "DEBUG" | nc localhost 115200
   ```

### Refill Not Working

1. **Check machine type** - wrong type causes checksum errors
2. **Try auto-detect mode:**
   ```bash
   python3 autorefill_daemon.py /dev/cu.usbserial-0001 --auto-detect
   ```
3. **Check threshold** - cartridge may be above threshold
4. **Monitor serial output** for error messages

### LED Not Working

- ESP32: Built-in LED is GPIO2
- ESP8266: Built-in LED is GPIO2 (D4 on NodeMCU)
- Some boards have inverted LEDs (LOW=ON)

## Memory Usage

**ESP32:**
- Flash: 311 KB (23.8%)
- RAM: 20 KB (6.3%)

**ESP8266:**
- Flash: 272 KB (26.1%)
- RAM: 28 KB (35.2%)

Both have plenty of headroom for future features.

## Files

```
esp32_autorefill/
├── platformio.ini         # Build configuration
├── src/
│   └── main.cpp          # Device firmware
└── README.md             # This file

autorefill_daemon.py       # Python daemon (computer-based)
autorefill_rpi.py         # Standalone Raspberry Pi version
```

## Comparison: ESP32/ESP8266 vs Raspberry Pi

| Feature | ESP32/ESP8266 | Raspberry Pi |
|---------|---------------|--------------|
| **Cost** | $5-10 | $15-35 |
| **Power** | ~80mA | ~500mA |
| **Autonomous** | No (needs computer) | Yes (fully standalone) |
| **Setup** | Flash firmware + run daemon | Install Python + pigpio |
| **Portability** | Requires laptop nearby | Standalone device |
| **Speed** | Fast (hardware timing) | Slower (Linux timing) |
| **Best for** | Development, testing | Production, embedded |

**Recommendation:**
- **Development/Testing:** ESP32/ESP8266 with daemon
- **Production/Embedded:** Raspberry Pi standalone

## Future Enhancements

- [ ] OLED display showing cartridge info
- [ ] WiFi configuration portal
- [ ] OTA firmware updates
- [ ] Web interface for monitoring
- [ ] Batch refill mode
- [ ] Material usage statistics
- [ ] Email/SMS notifications

## License

Same as main stratatools repository.

## Support

For issues or questions:
- Open an issue on GitHub
- See main stratatools README.md
- Check MULTI_PLATFORM_GUIDE.md for hardware setup
