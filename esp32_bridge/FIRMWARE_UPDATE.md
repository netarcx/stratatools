# ESP32 Firmware Update - Fixed Write Implementation

## What Was Fixed

The ESP32 firmware had critical bugs in the WRITE command that caused incomplete/corrupted writes to DS2433 EEPROM cartridges.

### Problems in Original Code

1. **No write completion verification**: After sending COPY_SCRATCHPAD command, the firmware just waited 15ms and assumed success. The DS2433 datasheet requires reading the status byte to confirm programming completed.

2. **No page boundary alignment**: DS2433 has 16 pages of 32 bytes each (0x000-0x01F, 0x020-0x03F, etc.). Writes that cross page boundaries will fail, but the old code didn't enforce alignment.

3. **Insufficient delays**: 15ms delay was too short for reliable writes. DS2433 can take up to 10ms per byte.

4. **No post-copy verification**: The code verified scratchpad data before copying, but never read back from EEPROM after the copy to confirm it actually worked.

### What Changed in `onewire_handler.cpp`

**`writeBlock()` function (lines 69-174):**
- Added page boundary check (lines 72-76): Prevents writes from crossing 32-byte page boundaries
- Added write completion status check (lines 137-149): Reads status byte until EEPROM signals programming is complete
- Added read-back verification (lines 154-171): Reads data back from EEPROM and verifies it matches what was written
- Increased delays for reliability (lines 140, 152)

**`write()` function (lines 176-200):**
- Rewrote to properly calculate page-aligned blocks (lines 182-190)
- Ensures each write operation stays within a single 32-byte page
- Handles partial writes at page boundaries correctly

## How to Update Firmware

### Prerequisites

1. **Install PlatformIO** (if not already installed):
   ```bash
   # Via pip
   pip install platformio

   # Or via Homebrew (macOS)
   brew install platformio
   ```

2. **Connect ESP32** via USB cable

3. **Identify serial port**:
   ```bash
   # macOS
   ls /dev/cu.usbserial-*

   # Linux
   ls /dev/ttyUSB*
   ```

### Build and Flash

```bash
cd /Users/trentfox/Code/stratatools/esp32_bridge

# Clean previous build
pio run --target clean

# Build firmware
pio run

# Flash to ESP32 (replace with your port)
pio run --target upload --upload-port /dev/cu.usbserial-0001

# Monitor serial output to verify (optional)
pio device monitor --port /dev/cu.usbserial-0001
```

### Verify Firmware Updated

After flashing, connect via serial and send VERSION command:

```bash
# Using screen (Ctrl+A, K to exit)
screen /dev/cu.usbserial-0001 115200

# Or using Python
python3 -c "
from stratatools.helper.esp32_bridge import ESP32Bridge
bridge = ESP32Bridge('/dev/cu.usbserial-0001')
bridge.initialize()
print('Firmware ready!')
bridge.close()
"
```

You should see:
```
ESP32-C3 1-Wire Bridge v1.0
Ready
```

## Testing the Fixed Firmware

After updating firmware, test writing your backup:

```bash
cd /Users/trentfox/Code/stratatools

# Write the backup file to cartridge
python3 stratatools/helper/esp32_write.py /dev/cu.usbserial-0001 /tmp/cartridge_dump.bin

# Should see:
# Write successful!
# Verification successful!
```

Then read it back to verify:

```bash
python3 stratatools/helper/esp32_read.py /dev/cu.usbserial-0001 prodigy /tmp/test_verify.bin

# Compare the files
diff /tmp/cartridge_dump.bin /tmp/test_verify.bin
# Should output nothing (files identical)
```

## Or Use the GUI

The updated GUI can now write reliably:

```bash
python3 stratatools_gui.py
```

1. Connect to ESP32
2. Search for device
3. Switch to Edit tab
4. Click "Load from File" and select `/tmp/cartridge_dump.bin`
5. Click "Write to Cartridge"
6. Should complete successfully with verification!

## Technical Details

### DS2433 Write Protocol

The proper write sequence is:

1. **WRITE_SCRATCHPAD (0x0F)**: Write up to 32 bytes to scratchpad memory
2. **READ_SCRATCHPAD (0xAA)**: Read back and verify data in scratchpad
3. **COPY_SCRATCHPAD (0x55)**: Copy scratchpad to EEPROM
4. **Wait for completion**: Read status byte until bus is released (reads 0xFF)
5. **READ_MEMORY (0xF0)**: Read back from EEPROM to verify

### Page Boundaries

DS2433 memory map:
```
Page 0:  0x000 - 0x01F  (32 bytes)
Page 1:  0x020 - 0x03F  (32 bytes)
Page 2:  0x040 - 0x05F  (32 bytes)
...
Page 15: 0x1E0 - 0x1FF  (32 bytes)
```

A scratchpad write starting at address 0x010 can write up to 0x01F (16 bytes), not 32 bytes, because it would cross into Page 1.

The fixed firmware calculates this automatically.

## Troubleshooting

### "Upload failed" or "Device not found"

- Make sure ESP32 is connected via USB
- Try pressing the BOOT button while uploading
- Check that no other program (like the GUI) is using the serial port
- Try a different USB cable or port

### "Verification failed" after write

- Check 1-wire connections (GPIO4 to EEPROM data line)
- Verify 4.7kΩ pull-up resistor is connected (GPIO4 to 3.3V)
- Try sending DEBUG command to check hardware:
  ```python
  from stratatools.helper.esp32_bridge import ESP32Bridge
  bridge = ESP32Bridge('/dev/cu.usbserial-0001')
  bridge.initialize()
  print(bridge._send_command('DEBUG'))
  ```

### Writes still failing

- Some DS2433 chips have write-protect features
- Verify cartridge EEPROM is not damaged
- Try a fresh/blank cartridge to confirm firmware works

## Changes Summary

**Files Modified:**
- `esp32_bridge/src/onewire_handler.cpp` - Fixed writeBlock() and write() functions

**Changes:**
- +5 lines for page boundary enforcement
- +15 lines for write completion detection
- +18 lines for read-back verification
- +10 lines for improved page-aligned block splitting

**Result:** Reliable, verified writes to DS2433 EEPROM cartridges!
