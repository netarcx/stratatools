# Python 3 Migration and ESP32-C3 Support

## Overview

This branch has been updated to:
1. **Python 3 compatibility** - Converted from Python 2.7 to Python 3.6+
2. **ESP32-C3 hardware support** - New firmware for ESP32-C3 as 1-wire bridge

## Python 3 Changes

### What Changed

- String/bytes handling updated throughout
- `.decode("hex")` → `bytes.fromhex()`
- `.encode("hex")` → `.hex()`
- `print` statements → `print()` functions
- Integer division: `/` → `//`
- `buffer()` → `memoryview()`
- Removed Python 2 shebangs

### Installation

```bash
# Python 3.6+ required
pip3 install -e .

# Or install from source
python3 setup.py install
```

### Dependencies

- Python 3.6 or higher
- pycryptodome
- pyserial
- protobuf

Optional:
- pyudev (for Raspberry Pi daemon only)

### Testing

Basic tests run successfully:
```bash
PYTHONPATH=. python3 stratatools/crypto_test.py
PYTHONPATH=. python3 stratatools/checksum_test.py
```

**Note:** The protobuf file (`cartridge_pb2.py`) needs to be regenerated with your protoc version:
```bash
pip3 install --upgrade protobuf
protoc --python_out=stratatools protobuf/cartridge.proto
```

## ESP32-C3 Hardware Bridge

### Features

- **Compact**: Single ESP32-C3 chip replaces Raspberry Pi or Bus Pirate
- **Fast**: Dedicated firmware optimized for 1-wire operations
- **USB powered**: No external power supply needed
- **Simple protocol**: ASCII commands over USB serial
- **Cost effective**: ~$3-5 per board

### Hardware Setup

```
ESP32-C3 GPIO4 ──┬── DS2433 Data
                 │
               4.7kΩ
                 │
ESP32-C3 3.3V ───┴

ESP32-C3 GND ────── DS2433 Ground
```

### Quick Start

1. Build and flash firmware:
```bash
cd esp32_bridge
pio run --target upload
```

2. Read a cartridge:
```bash
stratatools_esp32_read /dev/ttyUSB0 cartridge.bin
```

3. Decode cartridge:
```bash
stratatools eeprom_decode -t fox -e 2362474d0100006b cartridge.bin
```

4. Create and write new cartridge:
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
    stratatools eeprom_encode -t fox -e 2362474d0100006b > new_cart.bin

stratatools_esp32_write /dev/ttyUSB0 new_cart.bin
```

See `esp32_bridge/README.md` for detailed documentation.

## Architecture

```
┌─────────────────┐      USB Serial      ┌──────────────────┐      1-wire      ┌──────────────┐
│   PC (Python3)  │◄────────────────────►│  ESP32-C3 Bridge │◄────────────────►│ DS2433 EEPROM│
│                 │   ASCII Protocol      │                  │    Hardware      │  Cartridge   │
│ • Crypto (DESX) │                       │ • 1-wire driver  │                  └──────────────┘
│ • Cartridge Mgr │                       │ • Serial parser  │
│ • CLI Tools     │                       │ • Commands:      │
│ • Material DB   │                       │   - SEARCH       │
└─────────────────┘                       │   - READ         │
                                          │   - WRITE        │
                                          └──────────────────┘
```

The hybrid architecture keeps all cryptography and cartridge logic in Python (proven and portable) while offloading the timing-critical 1-wire protocol to dedicated ESP32-C3 firmware.

## Backwards Compatibility

All existing tools continue to work:

- **Bus Pirate**: `stratatools_bp_read`, `stratatools_bp_write`
- **Raspberry Pi**: `stratatools_rpi_daemon`
- **ESP32-C3**: `stratatools_esp32_read`, `stratatools_esp32_write` (new!)

## Command Reference

### New Commands

- `stratatools_esp32_read <port> <output_file>` - Read cartridge via ESP32
- `stratatools_esp32_write <port> <input_file>` - Write cartridge via ESP32

### Existing Commands (Python 3 compatible)

- `stratatools eeprom_decode` - Decode cartridge EEPROM
- `stratatools eeprom_encode` - Encode cartridge EEPROM
- `stratatools eeprom_create` - Create cartridge configuration
- `stratatools material --list` - List supported materials
- `stratatools setupcode_decode` - Decode setup code
- `stratatools setupcode_create` - Create setup code

## Known Issues

1. **Protobuf version mismatch**: If you get protobuf errors, regenerate the file:
   ```bash
   pip3 install --upgrade protobuf
   protoc --python_out=stratatools protobuf/cartridge.proto
   ```

2. **Serial port permissions** (Linux): Add user to dialout group:
   ```bash
   sudo usermod -a -G dialout $USER
   ```

## Contributing

When contributing:
- Use Python 3.6+ compatible syntax
- Test with both Bus Pirate and ESP32 hardware if possible
- Maintain backwards compatibility with existing tools
- Update documentation

## Version History

- **v3.1** - Python 3 migration + ESP32-C3 support
- **v3.0** - Original Python 2.7 version

## Support

For issues:
- Python 3 conversion bugs: Open GitHub issue
- ESP32 firmware: Check `esp32_bridge/README.md`
- Hardware setup: See main README.md

## License

MIT License (same as original project)
