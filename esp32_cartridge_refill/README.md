# Standalone Cartridge Refill (ESP32-C3)

Refill/reset a Stratasys cartridge with **no PC** — the ESP32-C3 does the read,
decode, refill, re-encrypt, write, and verify entirely on-device. Press a
button; it refills the cartridge to full and writes a fresh random serial
number.

Configured for a **Prodigy / P-class** printer.

## How it works

On a button press the firmware:
1. Searches the 1-Wire bus for the cartridge (gets its UID).
2. Reads the 113-byte EEPROM structure.
3. Decodes + validates it (every checksum the printer checks) — if it isn't a
   genuine Prodigy cartridge, it aborts and does **not** write.
4. Sets current quantity = initial quantity and assigns a new random serial
   number (ESP32 hardware RNG).
5. Re-encrypts and writes it back (per-32-byte scratchpad → verify → copy).
6. Reads it back and confirms a byte-exact match.

LED feedback (onboard LED):
- **solid** while working
- **3 slow blinks** = success
- **3 fast blinks** = bus/read/write/verify error
- **5 fast blinks** = not a valid Prodigy cartridge (wrong printer type)
- **continuous fast blink at boot** = crypto self-test failed (won't operate)

USB serial (115200) prints details of each step.

## Safety

- The crypto codec (`lib/stratasys`) is a port of the proven `stratatools`
  Python encoder and is **validated byte-for-byte against it** by
  `test/host_test.cpp` — run it on a PC before flashing.
- A **power-on self-test** runs DES/DESX/CRC known-answer tests; if the build is
  bad it refuses to operate (no write can happen).
- Only **reversible** DS2433 commands are used (no lock/protect commands), and
  every write is verified by read-back.
- The cartridge must be **out of the printer** when you press the button so the
  printer and this module never drive the 1-Wire bus at the same time.

## Wiring (generic ESP32-C3 devkit)

| Signal      | GPIO | Notes                                            |
|-------------|------|--------------------------------------------------|
| 1-Wire data | 4    | **4.7 kΩ pull-up to 3.3 V** required             |
| Button      | 9    | BOOT button to GND, or your own momentary button |
| LED         | 8    | onboard LED (active-low on most C3 boards)       |
| GND / 3V3   | —    | shared with the cartridge EEPROM                 |

Change the pins / machine type at the top of `src/main.cpp`. The Prodigy key is
`5394D7657CED641D`; other printers' keys are in `stratatools/machine.py`.

**Two board variants — pick the right serial config:** this firmware defaults
(in `platformio.ini`) to routing `Serial` over the chip's **native USB**, which is
what the common "C3 SuperMini"-style generic devkits (onboard LED on GPIO8, BOOT
button on GPIO9) need — otherwise you get *no serial output at all*. If your board
has a separate USB-UART bridge instead (CP2102 / CH340, e.g. the genuine Espressif
DevKitM-1), delete the two `ARDUINO_USB_*` flags in `platformio.ini`.

**Strapping pins:** GPIO9 (button) and GPIO8 (LED) are both ESP32-C3 strapping
pins. Don't hold the button down while resetting/powering on — that puts the chip
in download mode instead of running. The LED idles *off* (GPIO8 high), which is the
safe boot level.

## Build

```
pio run -t upload          # flash
pio device monitor         # watch serial output
```

Host-test the codec first (recommended):
```
g++ -std=c++11 -I lib/stratasys -I test \
    lib/stratasys/des.cpp lib/stratasys/stratasys_codec.cpp test/host_test.cpp \
    -o /tmp/host_test && /tmp/host_test
```
