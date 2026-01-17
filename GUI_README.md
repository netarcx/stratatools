# Stratatools GUI

Modern PyQt5 graphical interface for reading, editing, and writing Stratasys 3D printer cartridge EEPROMs via ESP32 bridge.

## Features

- **Read Tab**: Connect to ESP32, search for cartridges, read and display cartridge information
- **Edit Tab**: Modify cartridge parameters, save/load files, write back to cartridge
- **Create Tab**: Create new cartridges from scratch with wizard interface
- **Advanced Tab**: Hex viewer, debug console, raw file operations

## Installation

The GUI is included with stratatools. Install with:

```bash
pip3 install -e .
```

Dependencies:
- PyQt5 >= 5.15.0
- pyserial
- pycryptodome
- protobuf

## Launching the GUI

```bash
# Option 1: Using installed command
stratatools_gui

# Option 2: Direct Python execution
python3 stratatools_gui.py

# Option 3: As module
python3 -m stratatools.gui.main_window
```

## Quick Start

### Reading a Cartridge

1. **Connect to ESP32**:
   - Go to "Read" tab
   - Select your ESP32 serial port (should be auto-detected)
   - Click "Connect"

2. **Search for Device**:
   - Click "Search Device"
   - ROM address will appear in the field

3. **Read Cartridge**:
   - Select machine type (fox, prodigy, quantum, etc.)
   - Click "Read Cartridge"
   - Cartridge information will be displayed

### Editing a Cartridge

1. Read a cartridge first (see above) OR load from file
2. Go to "Edit" tab
3. Modify any parameters (serial, material, quantity, etc.)
4. Click "Refill Cartridge" to reset to full
5. Click "Save to File..." to save or "Write to Cartridge" to write back

### Creating a New Cartridge

1. Go to "Create" tab
2. Click "Generate Random" for serial number
3. Select material from dropdown
4. Enter manufacturing lot
5. Set initial quantity (will be full)
6. Enter ROM address or search for device first
7. Click "Create & Write to Cartridge"

### Advanced Operations

1. Go to "Advanced" tab
2. **Hex Viewer**: Import/export raw .bin files, view hex dump
3. **Debug Console**: View all operations, send DEBUG command to ESP32
4. Useful for troubleshooting hardware issues

## Hardware Setup

### ESP32 Connection

- ESP32 connected via USB
- GPIO4 connected to EEPROM data line
- 4.7kΩ pull-up resistor from GPIO4 to 3.3V
- EEPROM ground connected to ESP32 ground

### Troubleshooting Hardware

If "No device found":
1. Click "Send DEBUG Command" in Advanced tab
2. Check GPIO4 state - should be HIGH with pull-up
3. Verify EEPROM connections
4. Check 1-wire bus for shorts

## Keyboard Shortcuts

- `Ctrl+O` - Open cartridge file
- `Ctrl+S` - Save cartridge file
- `Ctrl+Q` - Quit application
- `F5` - Refresh serial ports

## Machine Types

Supported Stratasys printers:
- **fox** - Fox T-class
- **fox2** - Fox T-class (variant 2)
- **ktype** - K-Type
- **prodigy** - Prodigy P-class
- **quantum** - Quantum
- **uprint** - uPrint
- **uprintse** - uPrint SE

## Material Database

The GUI includes 250+ Stratasys materials including:
- ABS variants (standard, engineering, colors)
- PC (Polycarbonate)
- PPSF
- Nylon
- Support materials
- And many more

## File Format

Cartridge files are saved as `.bin` files containing:
- Encrypted EEPROM data (113 bytes for standard cartridges)
- Can be loaded/saved for backup and modification
- ROM address must match when loading

## Settings Persistence

The GUI automatically saves:
- Window size and position
- Last used serial port
- Recent file paths
- Machine type selection

Settings stored in platform-specific location:
- **macOS**: `~/Library/Preferences/com.stratatools.Stratatools GUI.plist`
- **Linux**: `~/.config/Stratatools/Stratatools GUI.conf`
- **Windows**: Registry under `HKEY_CURRENT_USER\Software\Stratatools\Stratatools GUI`

## Workflow Examples

### Refill an Empty Cartridge

1. Read → Connect → Search → Read Cartridge
2. Edit → Click "Refill Cartridge"
3. Edit → Click "Write to Cartridge"
4. Confirm write operation
5. Done! Cartridge is now full

### Clone a Cartridge

1. Read → Read original cartridge
2. Edit → Click "Save to File..." → Save as `original.bin`
3. Remove original cartridge, insert blank EEPROM
4. Read → Search Device (get new ROM address)
5. Edit → Enter new ROM address
6. Edit → Click "Load from File..." → Load `original.bin`
7. Edit → Modify serial number (must be unique)
8. Edit → Click "Write to Cartridge"

### Create Custom Material Cartridge

1. Create → Generate random serial
2. Create → Select material from dropdown
3. Create → Set quantity (e.g., 56.5 cu.in for standard)
4. Read → Search for blank EEPROM
5. Create → ROM address auto-filled
6. Create → Click "Create & Write"

## Error Messages

Common errors and solutions:

| Error | Cause | Solution |
|-------|-------|----------|
| "No device found" | No cartridge on 1-wire bus | Check connections, pull-up resistor |
| "Invalid checksum" | Wrong machine type selected | Try different machine type |
| "Cannot open serial port" | Port in use or permissions | Close other programs, check permissions |
| "Verification failed" | Write error or bad EEPROM | Try writing again, check hardware |

## Logging

All operations are logged in the Advanced tab console:
- Connection events
- Device searches
- Read/write operations
- Errors with details

Use this for troubleshooting and verifying operations.

## Safety Features

- **Write confirmation**: Always asks before writing to cartridge
- **Verification**: Automatically verifies writes by reading back
- **Validation**: Checks cartridge parameters before encoding
- **Error handling**: User-friendly error messages with recovery suggestions

## Architecture

```
┌─────────────────────────────────────────┐
│         Stratatools GUI (PyQt5)         │
│  ┌────────┐ ┌────────┐ ┌──────────┐    │
│  │  Read  │ │  Edit  │ │  Create  │    │
│  └────────┘ └────────┘ └──────────┘    │
│                                         │
│  CartridgeController (Business Logic)  │
│  ESP32Bridge (Serial Communication)    │
└─────────────────────────────────────────┘
              │
              ▼
      ┌───────────────┐
      │     ESP32     │ ──1-wire──► EEPROM
      └───────────────┘

```

## Development

### Running from Source

```bash
cd /Users/trentfox/Code/stratatools
python3 stratatools_gui.py
```

### Key Components

- `stratatools/gui/main_window.py` - Main window with tabs
- `stratatools/gui/widgets/` - Tab implementations
- `stratatools/gui/controllers/` - Business logic
- `stratatools/gui/models/` - Data models

### Extending the GUI

To add new features:

1. Modify appropriate tab widget in `gui/widgets/`
2. Add controller methods in `gui/controllers/cartridge_controller.py`
3. Wire up signals in tab's `connect_signals()` method
4. Test with real hardware

## Credits

- **Original stratatools**: Benjamin Vanheuverzwijn
- **GUI Implementation**: Built on stratatools Python 3 conversion
- **ESP32 Bridge**: Custom firmware for 1-wire EEPROM access

## License

Same as stratatools (MIT License)

## Support

For issues and questions:
- GitHub Issues: https://github.com/bvanheu/stratatools/issues
- Check Advanced tab debug console for detailed error info
- Include debug console output when reporting issues
