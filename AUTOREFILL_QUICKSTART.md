# Auto-Refill Quick Start Guide

Complete guide to setting up autonomous cartridge refilling with ESP32/ESP8266 or Raspberry Pi.

## Overview

Three ways to auto-refill cartridges:

1. **ESP32 + Computer Daemon** - Device detects, computer refills
2. **ESP8266 + Computer Daemon** - Device detects, computer refills
3. **Raspberry Pi Standalone** - Fully autonomous, no computer needed

## Method 1: ESP32/ESP8266 with Daemon

### Hardware Setup

```
ESP32/ESP8266         Cartridge EEPROM
──────────────────────────────────────────
GPIO4 (ESP32)     →   DQ (Pin 4)
  or D2 (ESP8266)
3.3V              →   4.7kΩ → GPIO4/D2
3.3V              →   VCC (Pin 8)
GND               →   GND (Pin 1)
```

### Software Setup

**1. Build and flash firmware**
```bash
cd esp32_autorefill

# ESP32
pio run -e esp32 --target upload --upload-port /dev/cu.usbserial-0001

# ESP8266
pio run -e esp8266 --target upload --upload-port /dev/cu.usbserial-0001
```

**2. Run the daemon on your computer**
```bash
# Basic usage
python3 autorefill_daemon.py /dev/cu.usbserial-0001

# With options
python3 autorefill_daemon.py /dev/cu.usbserial-0001 \
    --machine prodigy \
    --threshold 10.0 \
    --auto-detect
```

**3. Insert cartridge** - automatic refill if below threshold!

### LED Status

- **Slow blink**: Waiting for cartridge
- **Fast blink**: Reading cartridge
- **Solid ON**: Cartridge OK (above threshold)
- **Triple blink**: Refilling in progress
- **Rapid blink**: Error

## Method 2: Raspberry Pi Standalone

### Hardware Setup

```
Raspberry Pi          Cartridge EEPROM
──────────────────────────────────────────
GPIO17 (Pin 11)   →   DQ (Pin 4)
3.3V (Pin 1)      →   4.7kΩ → GPIO17
3.3V (Pin 1)      →   VCC (Pin 8)
GND (Pin 6)       →   GND (Pin 1)

Optional:
GPIO27 (Pin 13)   →   LED (via 220Ω)
GPIO22 (Pin 15)   →   Button to GND
GPIO2/3 (I2C)     →   OLED display
```

### Software Setup

**1. Install dependencies**
```bash
sudo apt-get update
sudo apt-get install python3-pigpio
sudo systemctl enable pigpiod
sudo systemctl start pigpiod

pip3 install pyserial protobuf pycryptodome
```

**2. Copy stratatools to Raspberry Pi**
```bash
# From your computer
scp -r stratatools/ pi@raspberrypi.local:~/

# Or on the Pi
git clone https://github.com/YOUR_REPO/stratatools
cd stratatools
pip3 install -e .
```

**3. Run the standalone refill station**
```bash
sudo python3 autorefill_rpi.py

# With options
sudo python3 autorefill_rpi.py \
    --machine prodigy \
    --threshold 10.0 \
    --display  # Enable OLED display
```

**4. Auto-start on boot (optional)**
```bash
sudo nano /etc/systemd/system/autorefill.service
```

Paste:
```ini
[Unit]
Description=Stratasys Auto-Refill Station
After=pigpiod.service
Requires=pigpiod.service

[Service]
Type=simple
User=root
WorkingDirectory=/home/pi/stratatools
ExecStart=/usr/bin/python3 /home/pi/stratatools/autorefill_rpi.py --machine prodigy --threshold 10.0
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Enable:
```bash
sudo systemctl daemon-reload
sudo systemctl enable autorefill
sudo systemctl start autorefill

# Check status
sudo systemctl status autorefill

# View logs
sudo journalctl -u autorefill -f
```

## Configuration

### Change Refill Threshold

**ESP32/ESP8266 (edit platformio.ini):**
```ini
build_flags =
    -DAUTO_REFILL_THRESHOLD=15.0
```

**Python daemon/Pi:**
```bash
python3 autorefill_daemon.py /dev/ttyUSB0 --threshold 15.0
```

### Change Machine Type

**Python daemon/Pi:**
```bash
python3 autorefill_daemon.py /dev/ttyUSB0 --machine fox
```

Supported types: `fox`, `prodigy`, `quantum`, `uprint`, `uprintse`, `dimension`, `fortus`

### Auto-Detect Machine Type

If you have cartridges from different machines:
```bash
python3 autorefill_daemon.py /dev/ttyUSB0 --auto-detect
```

This tries all machine types until one decodes successfully.

## Testing

### Test ESP32/ESP8266 Device

```bash
# Open serial monitor
pio device monitor --port /dev/cu.usbserial-0001

# Insert cartridge - should see:
# *** CARTRIDGE DETECTED ***
# ROM: 2389b7e90200005f
```

### Test Daemon

```bash
# Terminal 1: Run daemon with verbose output
python3 autorefill_daemon.py /dev/cu.usbserial-0001 --machine prodigy

# Terminal 2: Monitor serial
pio device monitor --port /dev/cu.usbserial-0001

# Insert cartridge below threshold
# Should see:
# 1. Device detects cartridge (fast LED blink)
# 2. Daemon reads EEPROM
# 3. Daemon checks quantity
# 4. Daemon refills if < threshold
# 5. Daemon writes back
# 6. Daemon verifies
# 7. Success! (LED celebration)
```

### Test Raspberry Pi

```bash
# Check pigpiod is running
sudo systemctl status pigpiod

# Test hardware
sudo python3 -c "
import pigpio
pi = pigpio.pi()
print('Connected:', pi.connected)
pi.set_mode(17, pigpio.INPUT)
print('GPIO17:', pi.read(17))
pi.stop()
"

# Run auto-refill
sudo python3 autorefill_rpi.py --machine prodigy
```

## Troubleshooting

### Device Not Found

**Linux:**
```bash
# Check USB devices
lsusb

# Check serial ports
ls -l /dev/ttyUSB* /dev/ttyACM*

# Add user to dialout group
sudo usermod -a -G dialout $USER
# Log out and back in
```

**macOS:**
```bash
ls -l /dev/cu.usbserial-* /dev/tty.usbserial-*
```

**Windows:**
```bash
# Device Manager → Ports (COM & LPT)
# Look for COM3, COM4, etc.
```

### Cartridge Not Detected

1. **Check wiring** - verify pull-up resistor
2. **Test with another cartridge**
3. **Check 1-wire bus:**
   ```bash
   # ESP32
   echo "DEBUG" > /dev/cu.usbserial-0001

   # Raspberry Pi
   sudo python3 -c "
   from rpi_bridge.onewire_bridge import OneWireHandler
   ow = OneWireHandler()
   print('Reset:', ow.reset())
   print('Search:', ow.search())
   ow.close()
   "
   ```

### Refill Fails

**Common causes:**
1. **Wrong machine type** - use `--auto-detect`
2. **Cartridge above threshold** - lower threshold or check quantity
3. **Cartridge EEPROM damaged** - try reading with GUI first
4. **Timing issues** - add delays, reduce USB load

**Check logs:**
```bash
# ESP32/ESP8266 daemon
python3 autorefill_daemon.py /dev/ttyUSB0 2>&1 | tee refill.log

# Raspberry Pi
sudo journalctl -u autorefill -f
```

### Permission Denied

**Linux:**
```bash
sudo chmod 666 /dev/ttyUSB0  # Temporary
# Or permanent:
sudo usermod -a -G dialout $USER
```

**Raspberry Pi pigpio:**
```bash
sudo systemctl status pigpiod  # Must be running
sudo systemctl restart pigpiod
```

## Performance

### ESP32/ESP8266 + Daemon
- **Detection time:** <1 second
- **Read time:** 1-2 seconds
- **Write time:** 20-30 seconds
- **Total refill:** ~35 seconds

### Raspberry Pi Standalone
- **Detection time:** ~5 seconds (slower polling)
- **Read time:** 2-3 seconds
- **Write time:** 30-60 seconds
- **Total refill:** ~70 seconds

## Workflow Examples

### Example 1: Bulk Refill

Refill 10 cartridges in sequence:

```bash
# Start daemon
python3 autorefill_daemon.py /dev/cu.usbserial-0001 --threshold 10.0

# Insert cartridge 1 → wait for refill → remove
# Insert cartridge 2 → wait for refill → remove
# ...
# Insert cartridge 10 → wait for refill → remove
```

Each cartridge takes ~35 seconds. Total: ~6 minutes for 10 cartridges.

### Example 2: Mixed Machine Types

Refill cartridges from different machines:

```bash
python3 autorefill_daemon.py /dev/cu.usbserial-0001 --auto-detect
```

Daemon tries all machine types until one works.

### Example 3: Custom Threshold per Material

Some materials need more headroom:

```bash
# Support material (higher threshold)
python3 autorefill_daemon.py /dev/ttyUSB0 --threshold 15.0

# Model material (lower threshold)
python3 autorefill_daemon.py /dev/ttyUSB0 --threshold 5.0
```

### Example 4: Raspberry Pi Production Station

Build a permanent refill station:

1. Flash ESP32 or setup Raspberry Pi
2. 3D print enclosure with cartridge holder
3. Add OLED display showing status
4. Add manual button for force refill
5. Enable auto-start on boot

Now you have a standalone refill station!

## Comparison

| Feature | ESP32 + Daemon | ESP8266 + Daemon | Raspberry Pi |
|---------|----------------|------------------|--------------|
| **Cost** | $7 + laptop | $5 + laptop | $15-35 |
| **Speed** | Fast | Fast | Medium |
| **Autonomous** | No | No | Yes |
| **Power** | <1W | <1W | ~2.5W |
| **Portability** | Need laptop | Need laptop | Standalone |
| **Setup** | Flash + Python | Flash + Python | Python only |
| **Best for** | Dev/test | Dev/test | Production |

## Files Overview

```
stratatools/
├── esp32_autorefill/           # ESP32/ESP8266 firmware
│   ├── platformio.ini          # Build config
│   ├── src/main.cpp            # Device firmware
│   └── README.md
├── autorefill_daemon.py        # Python daemon (computer)
├── autorefill_rpi.py           # Raspberry Pi standalone
├── AUTOREFILL_QUICKSTART.md    # This file
└── stratatools/                # Core library
    ├── helper/esp32_bridge.py  # ESP32 communication
    ├── manager.py              # Encode/decode
    └── ...
```

## Next Steps

1. **Build firmware** → Flash to ESP32/ESP8266
2. **Test device** → Insert cartridge, check serial output
3. **Run daemon** → Start auto-refill daemon
4. **Refill cartridges** → Insert and watch it work!

Or for production:
1. **Setup Raspberry Pi** → Install pigpio and stratatools
2. **Test standalone** → Run autorefill_rpi.py
3. **Enable auto-start** → Create systemd service
4. **Build enclosure** → 3D print holder and case

## Support

- **Hardware issues:** Check MULTI_PLATFORM_GUIDE.md
- **Software issues:** Check esp32_autorefill/README.md
- **Questions:** Open an issue on GitHub
- **Contributions:** Pull requests welcome!

## License

Same as main stratatools repository.

---

**Happy refilling!** 🎉
