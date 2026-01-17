# Raspberry Pi Pico 2 Auto-Refill Device

Automatic Stratasys cartridge refill station using Raspberry Pi Pico 2 (RP2350).

This device works with the `autorefill_daemon.py` to automatically detect and refill cartridges when inserted.

## Features

- Automatic cartridge detection via 1-Wire bus
- LED status indicators
- Optional button for manual refill
- Works with existing autorefill_daemon.py
- Low cost (~$6 for Pico 2)
- USB-powered

## Hardware Requirements

### Required Components

- **Raspberry Pi Pico 2** (RP2350-based board)
- **DS2433/DS2432 EEPROM** (Stratasys cartridge)
- **4.7kΩ pull-up resistor** (1/4W or 1/8W)
- **USB cable** (for power and communication)

### Optional Components

- **Push button** (for manual refill trigger)
- **External LED** (if you want a brighter status indicator)
- **220Ω resistor** (for external LED current limiting)

## Wiring Diagram

```
┌─────────────────────────────────┐
│   Raspberry Pi Pico 2           │
│                                 │
│  GP16 (Pin 21) ────┬────────────┼─── 1-Wire Data
│                    │            │     (to cartridge)
│                   ┌┴┐           │
│                4.7k│ │           │
│                   └┬┘           │
│                    │            │
│  3V3 (Pin 36) ─────┴────────────┼─── +3.3V
│                                 │     (to cartridge)
│  GND (Pin 38) ──────────────────┼─── Ground
│                                 │     (to cartridge)
│                                 │
│  GP15 (Pin 20) ─────[Button]────┼─── GND
│                                 │     (manual refill)
│                                 │
│  GP25 (Built-in) ── LED ────────┤     Status LED
│                                 │
└─────────────────────────────────┘

Cartridge Connection (DS2433):
┌──────────────┐
│  DS2433      │
│  EEPROM      │
│              │
│  Pin 1 ──────┼─── Ground
│  Pin 2 ──────┼─── 1-Wire Data (with 4.7k pull-up to 3.3V)
│  Pin 3 ──────┼─── +3.3V
└──────────────┘
```

### Pin Mapping

| Function      | GPIO Pin | Physical Pin | Notes                        |
|---------------|----------|--------------|------------------------------|
| 1-Wire Data   | GPIO16   | Pin 21       | Requires 4.7kΩ pull-up      |
| Status LED    | GPIO25   | Built-in LED | Also available on Pin 16    |
| Button        | GPIO15   | Pin 20       | Active low (internal pull-up)|
| 3.3V Power    | 3V3      | Pin 36       | Power for cartridge          |
| Ground        | GND      | Pin 38       | Common ground                |

## Software Setup

### 1. Install PlatformIO

If you don't have PlatformIO installed:

```bash
# Install PlatformIO CLI
pip install platformio

# Or use VS Code extension
# Install "PlatformIO IDE" from VS Code marketplace
```

### 2. Build and Upload Firmware

```bash
cd pico2_autorefill

# Build the firmware
pio run

# Upload to Pico 2 (hold BOOTSEL button while plugging in USB)
pio run --target upload

# Or use upload with auto-reset if supported
pio run -t upload

# Monitor serial output
pio device monitor
```

### 3. Set Up Python Daemon

The Pico 2 requires the `autorefill_daemon.py` running on your computer:

```bash
# From the main stratatools directory
cd ..

# Install dependencies (if not already installed)
pip install pyserial pycryptodome protobuf

# Find your Pico 2's serial port
# Linux: /dev/ttyACM0 or /dev/ttyUSB0
# macOS: /dev/cu.usbmodem* (e.g., /dev/cu.usbmodem14201)
# Windows: COM3, COM4, etc.

# Run the daemon
python3 autorefill_daemon.py /dev/ttyACM0

# Or let it auto-detect
python3 autorefill_daemon.py
```

## Usage

### Normal Operation

1. Connect Pico 2 to computer via USB
2. Run the autorefill daemon: `python3 autorefill_daemon.py`
3. Insert cartridge into reader
4. LED will blink fast while reading
5. If below threshold, automatically refills
6. LED does celebration pattern when complete
7. Remove cartridge and insert next one

### Manual Refill

Press the button (GPIO15) while cartridge is inserted to force a refill regardless of current quantity.

### LED Status Indicators

| Pattern       | Meaning                              |
|---------------|--------------------------------------|
| Slow blink    | Waiting for cartridge (1 sec)       |
| Fast blink    | Reading cartridge (200ms)           |
| Solid on      | Cartridge OK / Processing           |
| Triple blink  | Refilling in progress               |
| Rapid blink   | Error occurred (100ms)              |
| 5 fast flashes| Refill complete (celebration!)      |

## Troubleshooting

### Pico 2 Not Detected

- Hold BOOTSEL button while plugging in USB
- Check USB cable (some are power-only)
- Try a different USB port
- Install drivers if on Windows

### No Cartridge Detected

- Check 4.7kΩ pull-up resistor is connected
- Verify 3.3V power to cartridge
- Check ground connection
- Verify GPIO16 connection
- Check cartridge is DS2433 compatible

### Daemon Not Connecting

- Verify correct serial port
- Close any other serial monitors
- Check baud rate is 115200
- Restart Pico 2
- Check USB cable integrity

### Read/Write Errors

- Clean cartridge contacts
- Check all connections are secure
- Verify 4.7kΩ resistor value
- Try reducing wire length
- Ensure good ground connection

## Advanced Configuration

### Changing Pins

Edit `platformio.ini` to change pin assignments:

```ini
build_flags =
    -DONEWIRE_PIN=16      # 1-Wire data pin
    -DSTATUS_LED=25       # Status LED pin
    -DBUTTON_PIN=15       # Button pin
    -DAUTO_REFILL_THRESHOLD=10.0  # Threshold in cu.in
```

### Adjusting Check Interval

Edit `src/main.cpp` line 46:

```cpp
#define CHECK_INTERVAL 5000  // Check every 5 seconds
```

### Serial Commands

The Pico 2 accepts these commands from the daemon:

- `STATUS` - Report current device status
- `REFILLING` - Acknowledge refill start (triple blink)
- `REFILL_DONE` - Acknowledge refill complete (celebration)
- `ERROR` - Acknowledge error (rapid blink)

The Pico 2 sends these events to the daemon:

- `CARTRIDGE_INSERTED:[rom_hex]` - Cartridge detected

## Specifications

| Specification     | Value                |
|-------------------|----------------------|
| Microcontroller   | RP2350 (Pico 2)     |
| Clock Speed       | 150 MHz (ARM)       |
| Flash Memory      | 2MB - 16MB          |
| RAM               | 520KB SRAM          |
| Operating Voltage | 3.3V                |
| USB               | USB 1.1 Device      |
| Power Consumption | ~30mA typical       |
| Detection Speed   | ~5 seconds          |
| Read Speed        | ~2 seconds          |
| Write Speed       | ~20-30 seconds      |

## Comparison with Other Platforms

| Platform       | Read   | Write    | Total  | Autonomous | Cost  |
|----------------|--------|----------|--------|------------|-------|
| ESP32 + Daemon | 1-2s   | 20-30s   | ~35s   | No         | ~$4   |
| Pico 2 + Daemon| 1-2s   | 20-30s   | ~35s   | No         | ~$6   |
| Raspberry Pi   | 2-3s   | 30-60s   | ~70s   | Yes        | ~$15  |

## Why Use Pico 2?

**Advantages:**
- Very affordable (~$6)
- USB-powered (no external power supply)
- Built-in LED
- Robust USB connection
- Good Arduino library support
- RP2350 is fast and capable

**Disadvantages:**
- Requires computer + daemon (not autonomous)
- Slightly more expensive than ESP32
- Newer platform (less mature ecosystem)

## License

Same as parent stratatools project.

## Support

For issues specific to Pico 2 implementation:
- Check wiring diagram carefully
- Verify firmware uploaded successfully
- Test with known-good cartridge
- Compare with ESP32 behavior

For general cartridge issues:
- See main stratatools README
- Check AUTOREFILL_QUICKSTART.md
