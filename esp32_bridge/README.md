# ESP32-C3 1-Wire Bridge for Stratasys Cartridge Programmer

This firmware turns an ESP32-C3 into a USB-to-1-wire bridge for reading and writing Stratasys cartridge EEPROMs.

## Hardware Requirements

- ESP32-C3 development board (e.g., ESP32-C3-DevKitM-1)
- 4.7kΩ resistor (pull-up)
- DS2433 or DS2432 EEPROM cartridge

## Hardware Setup

```
ESP32-C3                          DS2433 EEPROM
                                  ┌──────────┐
GPIO4 ──────┬──────────────────── │ Data     │
            │                     │          │
          4.7kΩ                   │          │
            │                     │          │
3.3V ───────┴                     │          │
                                  │          │
GND ──────────────────────────────│ Gnd      │
                                  └──────────┘

USB ────────────────────────────── ESP32-C3 (for Serial communication)
```

**Notes:**
- ESP32-C3 uses built-in USB-Serial bridge or UART
- No external USB-Serial adapter needed
- GPIO4 is used for 1-wire data line

## Firmware Build & Upload

### Using PlatformIO CLI

```bash
cd esp32_bridge

# Build firmware
pio run

# Upload to ESP32-C3
pio run --target upload

# Monitor serial output (optional)
pio device monitor
```

### Using PlatformIO IDE (VSCode)

1. Open the `esp32_bridge` folder in VSCode
2. Click the PlatformIO icon in the sidebar
3. Select "Build" to compile
4. Select "Upload" to flash the firmware
5. Use "Monitor" to view debug output

## Testing the Bridge

After uploading firmware:

1. Connect ESP32-C3 via USB
2. Note the serial port (e.g., `/dev/ttyUSB0` or `/dev/ttyACM0`)
3. Use Python tools to communicate:

```bash
# Test connection
python3 -c "from stratatools.helper.esp32_bridge import ESP32Bridge; \
            b = ESP32Bridge('/dev/ttyUSB0'); \
            print('OK' if b.initialize() else 'FAIL')"

# Search for device
python3 -c "from stratatools.helper.esp32_bridge import ESP32Bridge; \
            b = ESP32Bridge('/dev/ttyUSB0'); \
            print(b.onewire_macro_search())"
```

## Using with Stratatools

### Read a cartridge

```bash
stratatools_esp32_read /dev/ttyUSB0 cartridge_dump.bin
```

### Decode cartridge data

```bash
stratatools eeprom_decode \
    --machine-type fox \
    --eeprom-uid 2362474d0100006b \
    cartridge_dump.bin
```

### Create new cartridge data

```bash
stratatools eeprom_create \
    --serial-number 1234.0 \
    --material-name ABS \
    --manufacturing-lot 1234 \
    --manufacturing-date "2024-01-01 01:01:01" \
    --use-date "2024-02-02 02:02:02" \
    --initial-material 100.0 \
    --current-material 100.0 \
    --key-fragment 4141414141414141 \
    --version 1 \
    --signature STRATASYS | \
    stratatools eeprom_encode -t fox -e 2362474d0100006b > cartridge_new.bin
```

### Write to cartridge

```bash
stratatools_esp32_write /dev/ttyUSB0 cartridge_new.bin
```

## Serial Protocol

The ESP32 firmware implements a simple ASCII protocol:

### Commands

| Command | Description | Response |
|---------|-------------|----------|
| `SEARCH` | Find 1-wire device | `ROM:<address>` or `ERROR` |
| `READ <size>` | Read EEPROM | `DATA:<hex>` or `ERROR` |
| `WRITE <size> <hex>` | Write EEPROM | `OK` or `ERROR` |
| `RESET` | Reset 1-wire bus | `OK` or `ERROR` |
| `VERSION` | Get firmware version | Version string |

### Example Communication

```
PC: SEARCH\n
ESP32: ROM:2362474d0100006b\n

PC: READ 512\n
ESP32: DATA:00010203...\n

PC: WRITE 113 0001020304...\n
ESP32: OK\n
```

## Troubleshooting

### Device not found

- Check wiring and pull-up resistor
- Verify 1-wire device is DS2433 or DS2432
- Try `RESET` command first

### Write verification fails

- EEPROM might be write-protected
- Check power supply stability
- Increase write delays in firmware if needed

### Serial connection issues

- On Linux, user must be in `dialout` group: `sudo usermod -a -G dialout $USER`
- On macOS, use `/dev/tty.usbmodem*` or `/dev/cu.usbmodem*`
- Windows: Use `COMx` port from Device Manager

## GPIO Configuration

Default pin: GPIO4 (configurable in `main.cpp`)

To change the pin, edit `esp32_bridge/src/main.cpp`:

```cpp
#define ONEWIRE_PIN 4  // Change to your preferred GPIO
```

## Performance

- Read 512 bytes: ~2-3 seconds
- Write 512 bytes: ~20-30 seconds (due to EEPROM write cycles)
- Much faster than Raspberry Pi due to dedicated firmware

## License

Same as parent stratatools project (MIT License)
