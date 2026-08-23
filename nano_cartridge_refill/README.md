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
The LED is solid while working. See **Controls and result codes** below for the
full code table.

If a refill is interrupted, the next attempt reports **2-2** rather than
claiming the cartridge is the wrong printer type — the firmware checks whether
it holds a recovery image for that exact cartridge to tell the two apart.

USB serial (115200) prints details of each step.

> **Bench reference:** [`TROUBLESHOOTING.md`](TROUBLESHOOTING.md) has the full
> LED code table, recovery procedure, and first bring-up checklist.

## Recovering an interrupted write

The 113-byte image goes out as four scratchpad→verify→copy cycles. If power
drops, the board resets, or the cartridge is pulled **between** cycles, the
cartridge is left half-old/half-new: its checksums fail, the printer rejects it,
and this firmware also refuses to refill it (it validates before writing). That
is the one genuine failure mode, and it exists on every microcontroller.

So before every write, the pre-write image is stored in the ATmega's own 1 KB
EEPROM, keyed by the cartridge's 1-Wire ROM. To put it back:

> **Hold STATUS for 3 seconds** (or, if STATUS isn't wired, hold ACTION down
> while powering up the Nano). The LED blinks while it
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

| Signal        | Pin | Notes                                                      |
|---------------|-----|------------------------------------------------------------|
| 1-Wire data   | D7  | **4.7 kΩ pull-up to +5 V** — but see *Embedded in a cartridge* |
| ACTION button | D9  | momentary to GND (internal pull-up)                        |
| STATUS button | D2  | momentary to GND (internal pull-up); optional — unwired it reads high and is inert |
| Green LED     | D6  | LED + ~330 Ω to GND — success, and solid while busy         |
| Red LED       | D8  | LED + ~330 Ω to GND — failures; **stays lit** until the next run |
| Onboard LED   | D13 | mirrors whichever external LED is active                    |
| GND / 5V      | —   | shared with the cartridge EEPROM                           |

### Prove the wiring first

`tools/onewire_wiring_test/` is a standalone, **read-only** sketch that checks
this wiring before the refill firmware ever touches the bus: idle level,
pull-up, bus contention, twenty presence pulses, ROM search, and eight repeated
reads compared against each other. It reports on serial and repeats the verdict
on the LED using the same codes as the firmware. Nothing else from this project
is needed to build it — see that folder's README.

Worth it because the faults it catches are the ones a single successful read
hides: an intermittent connection that answers 18 resets out of 20, or a data
line that isn't reaching the chip and reads a convincing block of `0xFF`.

## Controls and result codes

Designed to be operated with no computer attached.

| Control | Action | Result |
|---------|--------|--------|
| ACTION  | tap    | Dry run — read, decode, report. **Writes nothing.** |
| ACTION  | hold 1 s | Refill. LED blinks while held, solid once armed. |
| STATUS  | tap    | Replay the last result code, then blink the number of stored backups |
| STATUS  | hold 3 s | Restore this cartridge from its saved image |

### Power-on lamp test

Every boot starts with **green ×2, then red ×2, then both together** with the
onboard LED. It takes about 2.5 seconds and runs before the crypto self-test.

Sealed in a cartridge the LEDs are the only output, and a burnt-out LED looks
exactly like "nothing went wrong" — so this proves the reporting path itself
still works before you rely on it. The order matters too: **if red comes first,
the two LEDs are wired the opposite way round** from what the firmware believes,
and every code you read afterwards will be inverted.

Which LED blinks tells you the category before you count anything: **green (D6)
for success, red (D8) for failure**. Red then stays lit until the next operation
starts, so a failure is still visible long after the blinking stops — which
matters when there is no STATUS button to replay it.

Every outcome is a two-part code: **MAJOR long blinks, then MINOR short blinks**,
repeated three times. Two short groups are much easier to count correctly than
one run of eleven blinks.

| Code | Meaning |
|------|---------|
| 1-1 | No device on the 1-Wire bus |
| 1-2 | Read failed |
| 1-3 | Bus busy — another master active (cartridge still in the printer?) |
| 2-1 | Not a valid cartridge for this printer |
| 2-2 | Doesn't validate but a backup exists — **likely half-written, restore it** |
| 2-3 | Restore refused: the cartridge is already valid |
| 3-1 | Could not save the recovery backup (refill aborted, cartridge untouched) |
| 3-2 | Write failed |
| 3-3 | Write verify mismatch |
| 3-4 | Re-encoded image failed its final check (nothing written) |
| 3-5 | Written, but the cartridge's read-back does not decode — **restore it** |
| 4-1 | No backup stored for this cartridge |
| 4-2 | Stored backup does not validate |

Success is a deliberately different shape — slow even blinks, no long preamble:
**2** = dry run OK, **3** = refill OK, **4** = restore OK. Continuous fast
blinking at power-up means the crypto self-test failed; it will not operate.

The last code is kept in the ATmega's EEPROM, so it survives a power cycle and a
STATUS tap will replay it — you can find out what went wrong hours later with
nothing but the LED.

## Embedded in a cartridge

Putting the Nano *inside* the cartridge means it and the printer share the
1-Wire bus. Firmware alone cannot make that safe, so read this first.

**The firmware's part.** Before any write, both the refill and restore paths
sample the bus for 300 ms with the Nano's pin high-impedance. If anything pulls
the line low in that window, another master is talking and the operation is
refused with code **1-3**. That catches an *actively communicating* printer. It
does **not** catch a printer that is merely connected and idle, and then starts
a transaction a moment later.

**What hardware has to do.** Fit a **SERVICE / RUN switch** that physically
disconnects D7 *and* the Nano's pull-up from the cartridge's 1-Wire line in the
RUN position. Two reasons, and the second is the one that bites:

1. Only a physical break removes the two-master race entirely.
2. An **unpowered** Nano left connected is worse than a powered one. The AVR's
   ESD clamp diode conducts from the I/O pin into its own VCC rail, so the pin
   loads the line and can parasitically half-power the chip — and a 4.7 kΩ
   pull-up to a rail sitting at 0 V actively **holds the bus low**, which stops
   the printer reading the cartridge at all. Never leave the module connected
   and unpowered.

**Do not stack pull-ups.** If the printer already provides the bus pull-up,
adding a second 4.7 kΩ in parallel gives ~2.35 kΩ. Put the pull-up on the
switched side so only one is ever present.

A DPDT slide switch handles both jobs: one pole for the data line and pull-up,
the other for the Nano's power. If you have a spare pin, wiring the switch
position to it so the firmware can refuse outright in RUN is a cheap extra
interlock — but the physical break is what actually guarantees safety.

Change the pins / machine type at the top of `src/main.cpp`. The Prodigy key is
`5394D7657CED641D`; other printers' keys are in `stratatools/machine.py`.

> **5 V, not 3.3 V.** A classic Nano runs its I/O at 5 V. The DS2433 in the
> cartridge is rated 2.8–5.25 V, so pull the 1-Wire line up to the Nano's **+5V**
> rail. Don't wire a 3.3 V board onto the same bus at the same time.

Analog pins A0–A5 are read as noise to seed the random number generator, so
leave them unconnected.

> **Parasite power is supported.** Where the chip has only two conductors
> soldered to it — data and ground — it must draw its supply from the data line,
> and a 4.7 kΩ pull-up cannot feed the ~10 ms `copy-scratchpad` programming
> window. So `writeBlock()` sends the E-S byte with `power = 1`, which leaves the
> AVR pin *driving* the line high for the whole of `tPROG`, then calls
> `depower()`. That is the strong pull-up the DS2433 datasheet asks for.
>
> Connect VCC to +5 V if the part has a VCC wire — it is still the better
> arrangement. The strong pull-up is what makes two-wire cartridges work, not a
> reason to prefer them.

## Build

Verified with PlatformIO 6.1.19 / avr-gcc (Arduino AVR core). Current footprint:

| Env          | Board              | RAM           | Flash           |
|--------------|--------------------|---------------|-----------------|
| `nano`       | nanoatmega328new   | 487 / 2048 B  | 18624 / 30720 B |
| `nano_old`   | nanoatmega328      | 487 / 2048 B  | 18624 / 30720 B |
| `uno`        | uno                | 487 / 2048 B  | 18624 / 32256 B |
| `promicro16` | sparkfun_promicro16| 452 / 2560 B  | 20674 / 28672 B |

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

The wiring test in `tools/onewire_wiring_test/` is its own small project with
its own `platformio.ini`, and is a single file in the Arduino IDE — no
flattening step. It builds to 9756 B flash / 446 B RAM.

Host-test the codec first (recommended, no hardware needed):
```
g++ -std=c++11 -I lib/stratasys -I lib/backup -I test \
    lib/stratasys/des.cpp lib/stratasys/stratasys_codec.cpp lib/stratasys/f64.cpp \
    lib/backup/cartridge_backup.cpp \
    test/host_test.cpp -o /tmp/host_test && /tmp/host_test
```

### What "verified" means after a write

Success is never reported from the write call returning. `verifyWritten()` gates
it on three separate checks, because on a parasite-powered part they fail
differently:

1. A fresh read-back of all 113 bytes matches the image we meant to write —
   catching a row that never programmed.
2. A **second** read-back 500 ms later still matches. This is the one that
   matters for two-wire cartridges: a starved programming window can leave a
   cell that reads back correctly moments after the copy and decays shortly
   after. One immediate read cannot see that; two separated reads can.
3. The bytes now on the cartridge **decode** as a valid cartridge for this
   printer. Comparing against our own buffer only proves we wrote what we
   intended — this proves the intention is something the printer will accept.

Only then does the LED show 3 slow blinks. If step 3 fails, the code is **3-5**:
something *was* written and it does not decode, so restore that cartridge.

## Pro Micro (ATmega32U4)

The same firmware runs on a Pro Micro unchanged, and **the wiring is identical** —
it brings out D2–D10, so every pin is available at the same number. `pio run -e
promicro16 -t upload`, or compile it in the Arduino IDE as a Leonardo.

Three things differ, all handled by the `__AVR_ATmega32U4__` block at the top of
`src/main.cpp`:

- **No onboard LED mirror.** A Pro Micro does not bring D13 out to a pad, so
  `ONBOARD_LED_PIN` is set to 255 and `ledWrite()` treats that as a no-op. The
  green and red LEDs work exactly as before — only the onboard mirror is gone.
- **Native USB.** Never add `while (!Serial)`: with no host attached that never
  becomes true, and a sealed unit would sit waiting instead of refilling on a
  button press. Writes to an unattached CDC port are discarded, which is fine.
- **Less flash.** Caterina takes 4 KB and the USB stack ~2 KB, so the ceiling is
  28672 B rather than 30720 B. Currently 72% full.

Use the **5 V / 16 MHz** variant. On a parasite-powered cartridge the strong
pull-up that carries the chip through its programming window has meaningfully
more to give at 5 V than at 3.3 V.

Uploading also differs: the 32U4 enters its bootloader on a 1200-baud touch and
re-enumerates on a different port number. `arduino-cli` and PlatformIO both
handle this, but the port you upload to is not the port you monitor.

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
the button: it should report *no cartridge found* (code **1-1**, repeated three
times). That exercises the button, LED, EEPROM store and self-test with nothing
at risk.

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
