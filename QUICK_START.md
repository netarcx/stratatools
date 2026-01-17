# Quick Start Guide - Multi-Platform 1-Wire Bridge

Choose your platform and follow the steps below:

---

## ESP32 (Recommended)

**1. Flash Firmware**
```bash
cd esp32_bridge
pio run -e esp32 --target upload --upload-port /dev/cu.usbserial-0001
```

**2. Wire It Up**
```
ESP32 GPIO4  →  EEPROM DQ
ESP32 3.3V   →  4.7kΩ resistor → GPIO4
ESP32 3.3V   →  EEPROM VCC
ESP32 GND    →  EEPROM GND
```

**3. Test**
```bash
python3 stratatools_gui.py
```

---

## ESP32-C3

**1. Flash Firmware**
```bash
cd esp32_bridge
pio run -e esp32c3 --target upload --upload-port /dev/cu.usbserial-0001
```

**2. Wiring**
Same as ESP32 above (GPIO4 → EEPROM DQ)

---

## ESP8266 (NodeMCU)

**1. Flash Firmware**
```bash
cd esp32_bridge
pio run -e esp8266 --target upload --upload-port /dev/cu.usbserial-0001
```

**2. Wire It Up**
```
ESP8266 D2 (GPIO4)  →  EEPROM DQ
ESP8266 3.3V        →  4.7kΩ resistor → D2
ESP8266 3.3V        →  EEPROM VCC
ESP8266 GND         →  EEPROM GND
```

**⚠️ Warning:** ESP8266 is 3.3V only - do NOT use 5V!

---

## Raspberry Pi Zero 2

**1. Install Dependencies**
```bash
sudo apt-get update
sudo apt-get install python3-pigpio
sudo systemctl enable pigpiod
sudo systemctl start pigpiod
pip3 install pyserial
```

**2. Copy Script**
```bash
scp rpi_bridge/onewire_bridge.py pi@raspberrypi.local:~/
```

**3. Wire It Up**
```
Pi GPIO17 (Pin 11)  →  EEPROM DQ
Pi 3.3V (Pin 1)     →  4.7kΩ resistor → GPIO17
Pi 3.3V (Pin 1)     →  EEPROM VCC
Pi GND (Pin 6)      →  EEPROM GND
```

**4. Run**
```bash
# With USB-Serial adapter
sudo python3 onewire_bridge.py /dev/ttyUSB0

# Or as TCP server (for testing)
sudo python3 onewire_bridge.py --tcp 5000
```

---

## Which Platform Should I Use?

| Platform | Choose If... |
|----------|-------------|
| **ESP32** | You want the best performance and reliability |
| **ESP32-C3** | You want lowest cost ($3-5) |
| **ESP8266** | You already have one lying around |
| **Raspberry Pi** | You need Python integration or are prototyping |

---

## Testing All Platforms

```python
from stratatools.helper.esp32_bridge import ESP32Bridge

# Change port based on your platform:
# ESP32/ESP8266/ESP32-C3: /dev/cu.usbserial-0001 (macOS), COM3 (Windows)
# Raspberry Pi: /dev/ttyUSB0 or /dev/ttyAMA0

bridge = ESP32Bridge('/dev/cu.usbserial-0001', timeout=3)

if bridge.initialize():
    print('✓ Connected')

    rom = bridge.onewire_macro_search()
    if rom:
        print(f'✓ Device: {rom}')

        data = bridge.onewire_read(32)
        if data:
            print(f'✓ Read: {data[:16].hex()}')

bridge.close()
```

---

## Troubleshooting

**Problem:** Build failed

```bash
# Clean and retry
pio run -e esp32 --target clean
pio run -e esp32
```

**Problem:** Device not found

```bash
# Check wiring with DEBUG command
python3 -c "
from stratatools.helper.esp32_bridge import ESP32Bridge
bridge = ESP32Bridge('/dev/cu.usbserial-0001')
bridge.initialize()
print(bridge._send_command('DEBUG'))
"
```

Expected output:
```
GPIO4 state (idle): HIGH (good - pullup present)
Reset #1: PRESENCE DETECTED (device found!)
```

**Problem:** Permission denied (Raspberry Pi)

```bash
sudo usermod -a -G dialout $USER
# Then log out and back in
```

---

## Next Steps

- **Read the full guide:** [MULTI_PLATFORM_GUIDE.md](MULTI_PLATFORM_GUIDE.md)
- **Use the GUI:** `python3 stratatools_gui.py`
- **Read firmware update docs:** [esp32_bridge/FIRMWARE_UPDATE.md](esp32_bridge/FIRMWARE_UPDATE.md)

---

## Pin Reference Card

Print this and keep it handy:

```
┌──────────────────────────────────────────────┐
│         1-WIRE BRIDGE PIN REFERENCE          │
├──────────────────────────────────────────────┤
│                                              │
│  ESP32 / ESP32-C3:                           │
│    Data:    GPIO4                            │
│    Pull-up: 3.3V → 4.7kΩ → GPIO4            │
│                                              │
│  ESP8266 (NodeMCU):                          │
│    Data:    D2 (GPIO4)                       │
│    Pull-up: 3.3V → 4.7kΩ → D2               │
│                                              │
│  Raspberry Pi:                               │
│    Data:    GPIO17 (Physical Pin 11)        │
│    Pull-up: 3.3V → 4.7kΩ → GPIO17           │
│                                              │
│  DS2433/DS2432 EEPROM:                       │
│    Pin 1: GND                                │
│    Pin 4: DQ (Data - connect to GPIO)       │
│    Pin 8: VCC (3.3V only!)                   │
│                                              │
│  ⚠️  IMPORTANT:                              │
│    • Always use 3.3V (never 5V!)             │
│    • 4.7kΩ pull-up is required               │
│    • Connect GND between all devices         │
│                                              │
└──────────────────────────────────────────────┘
```
