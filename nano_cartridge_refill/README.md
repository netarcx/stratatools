# Standalone Cartridge Refill (Arduino Nano)

Refill/reset a Stratasys cartridge with **no PC** — an Arduino Nano
(ATmega328P) does the read, decode, refill, re-encrypt, write, and verify
entirely on-device. Press a button; it refills the cartridge to full and writes
a fresh random serial number.

This is a port of [`esp32_cartridge_refill`](../esp32_cartridge_refill) to 8-bit
AVR. Configured for a **Prodigy / P-class** printer.

## How it works

**Tap** the button to *inspect* a cartridge — it reads, decodes and prints what
is on it and writes nothing. **Hold** the button for 1 second to actually refill
it. The LED blinks while you hold and goes solid once the refill is committed,
so a knocked button can never modify a cartridge.

On a 1-second hold the firmware:
1. Searches the 1-Wire bus for the cartridge (gets its UID).
2. Reads the 113-byte EEPROM structure.
3. Decodes + validates it (every checksum the printer checks) — if it isn't a
   genuine Prodigy cartridge, it aborts and does **not** write.
4. **Saves the original image to the Nano's internal EEPROM** as a recovery
   copy. If this fails, the refill is aborted and the cartridge is untouched.
5. Sets current quantity = initial quantity and assigns a new random serial
   number.
6. Re-encrypts and writes it back (per-32-byte scratchpad → verify → copy,
   with up to 3 attempts per block).
7. Reads it back and confirms a byte-exact match.

LED feedback (onboard LED on D13):
- **solid** while working
- **3 slow blinks** = refill success
- **2 slow blinks** = dry run OK, nothing written
- **one long blink + three short, repeated 3×** = the cartridge may be
  **half-written** — power-cycle holding the button to restore it
- **2 fast blinks** = restore: no backup stored for this cartridge
- **3 fast blinks** = 1-Wire bus or read failure (cartridge untouched)
- **4 fast blinks** = could not save the recovery backup (refill aborted)
- **5 fast blinks** = image rejected — wrong printer type, or restore found
  nothing to repair
- **continuous fast blink at boot** = crypto self-test failed (won't operate)

The half-written code is deliberately unlike the others: it is the only outcome
that needs you to do something, and it must not be mistaken for a benign bus
error. If a refill is interrupted, the *next* button press reports it with this
code rather than claiming the cartridge is the wrong printer type — the firmware
checks whether it holds a recovery image for that exact cartridge to tell the
two apart.

USB serial (115200) prints details of each step.

## Recovering an interrupted write

The 113-byte image goes out as four scratchpad→verify→copy cycles. If power
drops, the board resets, or the cartridge is pulled **between** cycles, the
cartridge is left half-old/half-new: its checksums fail, the printer rejects it,
and this firmware also refuses to refill it (it validates before writing). That
is the one genuine failure mode, and it exists on every microcontroller.

So before every write, the pre-write image is stored in the ATmega's own 1 KB
EEPROM, keyed by the cartridge's 1-Wire ROM. To put it back:

> **Hold the button down while powering up the Nano.** The LED blinks while it
> counts down — release within 3 seconds to cancel. After 3 seconds it goes
> solid; release the button and the last known-good image for the cartridge on
> the bus is written back verbatim, **skipping validation**, then verified.

The store holds **7 cartridges**. Re-refilling a cartridge replaces its own
entry — for recovery you always want the image that was on the cartridge
immediately before the write that got interrupted. Once all 7 slots are used,
the least recently saved is evicted.

The replacement is not done in place: the new copy goes to a *different* slot
and is verified before the old one is retired, so the cartridge always has at
least one intact recovery image even if power is lost mid-save. Each slot's CRC
covers the bytes it was *meant* to hold, not a read-back of itself, so an EEPROM
cell that fails to program makes the slot read as empty rather than as a
self-consistent wrong image. Both `0` and `0xFFFFFFFF` are reserved as "empty"
sequence numbers, so blank or stuck-at-1 cells can't be mistaken for data.

Restore does two checks before it writes anything: it refuses if the cartridge
on the bus **already validates** (nothing to repair — rolling it back would be a
destructive write for no gain), and it refuses if the **stored image** doesn't
itself decode for this printer. Validation is bypassed against the cartridge,
never against the backup.

EEPROM map: 16-byte header (magic + power-up counter), then 7 × 127-byte slots
(sequence number, ROM, image, CRC-16) — 905 bytes of the 1024 available.

## Wiring

| Signal      | Pin | Notes                                              |
|-------------|-----|----------------------------------------------------|
| 1-Wire data | D3  | **4.7 kΩ pull-up to +5 V** required                |
| Button      | D2  | momentary button to GND (internal pull-up is used) |
| LED         | D13 | onboard LED (active-high)                          |
| GND / 5V    | —   | shared with the cartridge EEPROM                   |

Change the pins / machine type at the top of `src/main.cpp`. The Prodigy key is
`5394D7657CED641D`; other printers' keys are in `stratatools/machine.py`.

> **5 V, not 3.3 V.** A classic Nano runs its I/O at 5 V. The DS2433 in the
> cartridge is rated 2.8–5.25 V, so pull the 1-Wire line up to the Nano's **+5V**
> rail. Don't wire a 3.3 V board onto the same bus at the same time.

Analog pins A0–A5 are read as noise to seed the random number generator, so
leave them unconnected.

> **Power the cartridge properly.** `OneWire::write()` is called with its
> default `power = 0`, so the firmware never asserts a strong pull-up during the
> copy-scratchpad programming window. Connect the cartridge EEPROM's VCC to +5 V
> — don't run it parasite-powered off the data line, or the programming step
> will be marginal.

## Build

Verified with PlatformIO 6.1.19 / avr-gcc (Arduino AVR core). Current footprint:

| Env        | Board            | RAM          | Flash           |
|------------|------------------|--------------|-----------------|
| `nano`     | nanoatmega328new | 487 / 2048 B | 17112 / 30720 B |
| `nano_old` | nanoatmega328    | 487 / 2048 B | 17112 / 30720 B |
| `uno`      | uno              | 487 / 2048 B | 17112 / 32256 B |

That 487 B is static allocation; peak stack is roughly another 550 B, leaving
about 1 KB of headroom.

```
pio run -t upload          # flash (new-bootloader Nano, 115200)
pio run -e nano_old -t upload   # older boards / clones (57600)
pio device monitor         # watch serial output
```

If `avrdude` reports "not in sync" / stk500 timeouts, you picked the wrong
bootloader — switch between the `nano` and `nano_old` environments. An
`uno` environment is also defined; the Uno and 5V/16MHz Pro Mini are the same
chip and run this unchanged.

Arduino IDE: run `./make_arduino_sketch.sh` to flatten the tree into
`arduino/nano_cartridge_refill/`, install the **OneWire** library by Paul
Stoffregen, then open and upload the generated `.ino`.

Host-test the codec first (recommended, no hardware needed):
```
g++ -std=c++11 -I lib/stratasys -I test \
    lib/stratasys/des.cpp lib/stratasys/stratasys_codec.cpp lib/stratasys/f64.cpp \
    test/host_test.cpp -o /tmp/host_test && /tmp/host_test
```

## Safety

- The crypto codec (`lib/stratasys`) is a port of the proven `stratatools`
  Python encoder and is **validated byte-for-byte against it** by
  `test/host_test.cpp` — run it on a PC before flashing.
- A **power-on self-test** runs DES/DESX/CRC/binary64 known-answer tests; if the
  build is bad it refuses to operate (no write can happen).
- Only **reversible** DS2433 commands are used — `F0` read-memory, `0F`
  write-scratchpad, `AA` read-scratchpad, `55` copy-scratchpad, and `55`
  match-ROM for addressing. There is no lock, protect, or write-once command
  anywhere in the path, so no write this firmware makes can be permanent. The
  `55` copy byte matches `stratatools/helper/bp_write.py`, the PC tooling
  already proven against real cartridges.
- Every write is verified by read-back — per block, immediately after its
  copy-scratchpad, so a failed row is caught and retried before the following
  rows are committed on top of it — and again over the whole image at the end.
  Each 32-byte block is retried up to 3 times before giving up.
- Before copying the scratchpad to EEPROM, the E-S register's **ending offset**
  is checked as well as its PF and AA bits. Without the ending-offset check a
  write-scratchpad cut short mid-sequence would silently commit a partial row:
  PF only flags an incomplete *byte*, and the data compare can't catch it either
  because bytes past the ending offset read back as leftover scratchpad content
  — which, on a retry, is the previous attempt's identical data.
- The re-encoded image is decoded and validated one last time immediately before
  it is written, so nothing reaches the cartridge that the printer would reject.
- The pre-write image is saved to internal EEPROM first, and the refill is
  abandoned if that save doesn't stick. See *Recovering an interrupted write*.
- Nothing is written unless the cartridge first decodes and validates cleanly
  for the configured printer.
- The cartridge must be **out of the printer** when you press the button so the
  printer and this module never drive the 1-Wire bus at the same time.

## Flashing from WSL2

WSL2 doesn't pass USB through by default, so the Nano won't appear as
`/dev/ttyUSB0` on its own. Note that the `/dev/ttyS0`–`ttyS7` already present
are **not** your Windows COM ports — WSL1 mapped those, WSL2 does not.

The usual blocker (a WSL kernel without USB-serial drivers) does not apply to
recent kernels: 6.18 ships `ch341`, `cp210x`, `ftdi_sio` and `pl2303` as
modules. So `usbipd-win` works:

```powershell
winget install usbipd                 # once, admin PowerShell
usbipd list                           # note the Nano's BUSID
usbipd bind --busid <X-Y>             # once
usbipd attach --wsl --busid <X-Y>     # after each unplug / wsl shutdown
```

You also need to be in the `dialout` group or `/dev/ttyUSB0` is root-only:

```bash
sudo usermod -aG dialout $USER        # then: wsl.exe --shutdown, and reopen
```

If `usbipd attach` reports a missing usbip client inside the distro:
```bash
sudo apt install linux-tools-virtual hwdata
sudo update-alternatives --install /usr/local/bin/usbip usbip /usr/lib/linux-tools/*/usbip 20
```

Alternatively, skip passthrough entirely: build here, then flash
`.pio/build/nano/firmware.hex` from Windows over a native COM port.

## First power-up

Watch `pio device monitor` at 115200. Expect the crypto self-test to pass and
the recovery store to format itself. Then, **with no cartridge connected**, tap
the button: it should report *no cartridge found* (3 fast blinks). That
exercises the button, LED, EEPROM store and self-test with nothing at risk.

Next, connect a cartridge and **tap** — a dry run reads and prints its serial
and quantities without writing. Only once that looks right is it worth holding
the button to refill.

## Known-good behaviour worth checking on first use

`OneWire::search()` latches an internal "last device" flag and skips its entire
search body while that flag is set, so calling it twice in a row returns false
the second time. `OneWireHandler::search()` resets that state before every
search; without it, every second button press reports "no cartridge found" on a
perfectly good cartridge. If you port this code elsewhere, keep the
`ow.reset_search()` at the top of `search()`.

## What changed for AVR

The ATmega328P is a very different target from the ESP32-C3 — 8-bit, 2 KB of
SRAM, no hardware RNG — and three of these are correctness issues, not just
optimisations:

**1. `double` is 32-bit on avr-gcc.** The cartridge stores its serial number and
both material quantities as 8-byte IEEE-754 binary64 values. The ESP32 code does
`memcpy(&plain[0x00], &new_serial, 8)` on a `double`; on AVR that reads four
bytes past the end of the variable and would write garbage into the cartridge.
The codec here never uses `double` — those fields are passed as raw 8-byte
buffers, and `lib/stratasys/f64.h` encodes/decodes binary64 by hand. The
power-on self-test checks that encoder against a known vector.

**2. Lookup tables would not fit in RAM.** avr-gcc copies every `const` array
into SRAM at startup, and the DES/CRC/whitening tables total ~1.6 KB of the
Nano's 2 KB. They now live in flash (`PROGMEM`) and are read through the macros
in `lib/stratasys/pgm_compat.h`, which reduce to plain array indexing on a host
build so the same source is still testable on a PC.

**3. Buffers halved.** Decode and encode run **in place** on one 113-byte
buffer, instead of the ESP32 version's separate plaintext and ciphertext
buffers. Static SRAM use is ~240 bytes.

The Nano also gains something the ESP32 build doesn't have: 1 KB of real
internal EEPROM, which is what the recovery store above is built on.

Two smaller changes:

**No hardware RNG.** `esp_random()` has no AVR equivalent, so serial numbers
come from a xorshift32 seeded by analog-input noise, a boot counter kept in the
ATmega's internal EEPROM (so two power-ups can't produce the same sequence), and
the microsecond timestamp of the button press itself.

**Namespace collisions.** `<avr/io.h>` defines `PC0`..`PC7` (port C bit numbers)
as plain integer macros, so DES's `PC1` and `PC2` permutation tables expanded to
numeric constants and failed to compile — while building perfectly on a host.
Every table in `des.cpp` now carries a `DES_` prefix. Similarly, the backup store
uses avr-libc's `<avr/eeprom.h>` rather than the Arduino `EEPROM` wrapper: the
library dependency finder doesn't reliably locate framework-bundled libraries
when the include comes from a project library, and `eeprom_update_byte` has the
same skip-unchanged-cells semantics anyway.

**Speed.** The ATmega328P has no barrel shifter, so a variable-distance 64-bit
shift compiles to a loop of up to 64 single-bit shifts — and DES's `permute()`
does about 1400 of them per block. `permute()` now spreads its input into a byte
array and picks each source bit with one byte load and a shift of at most 7.
A full refill takes well under a second.

The rewritten DES and codec are verified equivalent to the ESP32 originals over
200,000 random keys and 20,000 random cartridge images, in addition to the
Python-derived vectors in `test/vectors.h`.
