# Troubleshooting

Bench reference for the embedded Nano refill module. Everything here can be
diagnosed from the LED alone — no computer required.

---

## 1. Reading the LED

The LED is **solid** while the module is working.

**Success** is a run of slow, even blinks with no long preamble:

| Blinks | Meaning |
|--------|---------|
| 2 slow | Dry run finished — cartridge read and valid, **nothing written** |
| 3 slow | Refill finished and verified |
| 4 slow | Restore finished and verified |

**Faults** are a two-part code — *MAJOR* long blinks, a pause, then *MINOR*
short blinks — repeated three times. Count the long ones first.

| Code | Meaning | What to do |
|------|---------|-----------|
| **1-1** | No device found on the 1-Wire bus | Check D3 wiring, the pull-up, and cartridge GND. See §3. |
| **1-2** | Read failed | Same as 1-1; usually a marginal connection. |
| **1-3** | Bus busy — another master is driving the line | The cartridge is still in the printer, or the SERVICE/RUN switch is in RUN. See §5. |
| **2-1** | Not a valid cartridge for this printer | Wrong machine type, or not a Stratasys cartridge. The module writes nothing. See §4. |
| **2-2** | Doesn't validate, **but a backup exists for this exact cartridge** | Almost certainly a half-written cartridge. **Restore it** — see §2. |
| **2-3** | Restore refused: the cartridge is already valid | Nothing to repair. This is a refusal, not a failure. |
| **3-1** | Could not save the recovery backup | Refill was aborted; the cartridge was **not** touched. See §6. |
| **3-2** | Write failed | The cartridge may be half-written. **Restore it** — see §2. |
| **3-3** | Write verify mismatch | As 3-2. Can also mean the cartridge was unplugged right after a good write. |
| **3-4** | Re-encoded image failed its final check | Nothing was written. Should not happen; see §6. |
| **4-1** | No backup stored for this cartridge | The module has never successfully refilled this cartridge. See §2. |
| **4-2** | Stored backup does not validate | The backup is unusable; do not rely on it. See §2. |

**Continuous fast blinking from power-up** = the crypto self-test failed. The
module refuses to operate at all in this state and will never write. It means a
bad flash or a corrupted chip — reflash it (§7). This is a safe failure.

### Getting the code back later

The last result is stored in the Nano's EEPROM and survives power cycles.

**Tap STATUS** and the module will:
1. Replay the last result code (success pattern or fault code), then
2. after a pause, blink **once per recovery backup** it currently holds
   (one *long* blink means the store is empty).

So you can walk away, come back, and still find out what happened.

---

## 2. Recovering a half-written cartridge

This is the failure the module is built around. A refill writes the cartridge in
four chunks; if power drops or the cartridge is disconnected between chunks, its
checksums no longer match. The printer rejects it, and the module refuses to
refill it — so it looks bricked.

**It is not bricked.** The module saved the cartridge's original image to its own
EEPROM before writing.

**Symptom:** code **2-2** on a tap, or **3-2** / **3-3** during a refill.

**Fix:** hold **STATUS for 3 seconds**. The LED blinks while counting down, then
goes solid — release it and the saved image is written back verbatim. Expect
**4 slow blinks**.

If STATUS isn't wired: hold **ACTION** while powering the module up. Same
3-second countdown.

### If restore refuses

- **4-1 (no backup stored)** — the module has no image for this cartridge. It
  only saves one during a refill it started. A cartridge damaged by something
  else, or by a *different* module, cannot be recovered here; you need the PC
  tooling (`stratatools`) and a known-good image.
- **4-2 (stored backup does not validate)** — the saved image itself is bad. The
  module refuses to write it rather than make things worse. Same answer: PC
  tooling.
- **2-3 (already valid)** — the cartridge is fine. Restore only repairs damage;
  it will not roll a healthy cartridge back to an older state.

The store holds **7 cartridges**, keyed by their 1-Wire serial. Refilling an
8th evicts the least recently saved. If you have a damaged cartridge waiting to
be restored, restore it before refilling seven others.

---

## 3. "No cartridge found" (1-1 / 1-2)

In likely order:

1. **SERVICE/RUN switch is in RUN** — the data line is disconnected by design.
2. **Missing or wrong pull-up.** The 1-Wire line needs 4.7 kΩ to **+5 V**. With
   no pull-up the bus never idles high and nothing is detected.
3. **No shared ground.** The cartridge EEPROM's GND must connect to the Nano's
   GND. This is the single most common wiring mistake.
4. **Cartridge VCC not connected.** Do not run the EEPROM parasite-powered off
   the data line — the module does not assert a strong pull-up during the
   EEPROM's programming window, so writes will be marginal or fail.
5. **Wrong pin.** Data is **D3**.

A quick check: with everything connected and the module idle, the 1-Wire line
should sit at ~5 V. If it reads ~0 V, something is holding it low — most often
an unpowered Nano still connected to the bus (§5).

---

## 4. "Not a valid cartridge" (2-1)

The firmware is built for a **Prodigy / P-class** printer. Its machine key is
compiled in. A cartridge for a different Stratasys machine will decode to
garbage and be rejected — correctly, and without writing anything.

To target a different printer, change `MACHINE[]` at the top of `src/main.cpp`;
the other machines' keys are in `stratatools/machine.py`. Rebuild and reflash.

If you also see a serial warning about the family code not being `0x23`, the bus
enumerated something that isn't a DS2433 at all.

---

## 5. Bus conflicts with the printer (1-3)

With the module embedded in the cartridge, it shares the 1-Wire bus with the
printer. Two masters on one bus corrupt each other's transactions.

**What the firmware does:** before any write, it samples the line for 300 ms
with its own pin high-impedance. If anything pulls the line low in that window,
it reports **1-3** and refuses.

**What that does not cover:** a printer that is connected but idle during those
300 ms and starts talking a moment later. The firmware cannot see that coming.

**So the hardware must break the connection.** Fit a SERVICE/RUN switch that
physically disconnects **D3 and the pull-up** from the cartridge line in RUN.

> ### Never leave the module connected but unpowered
>
> This is worse than leaving it powered. Two things happen:
> - The 4.7 kΩ pull-up now goes to a rail sitting at **0 V**, so it actively
>   **holds the bus low** and the printer cannot read the cartridge at all.
> - The AVR's ESD clamp diode conducts from the I/O pin into its own VCC rail,
>   loading the line and partially powering the chip through the signal.
>
> Symptom: the printer reports a missing or unreadable cartridge, and the
> 1-Wire line measures near 0 V. Switch to RUN, or power the module.

**Do not stack pull-ups.** If the printer already supplies the bus pull-up,
adding a second 4.7 kΩ in parallel gives ~2.35 kΩ. Put yours on the *switched*
side so only one is ever on the bus.

---

## 6. Backup problems (3-1, 3-4)

**3-1 — could not save the recovery backup.** The refill was aborted before the
cartridge was touched, so the cartridge is fine. The module will not write
without a way back. Causes:

- Worn internal EEPROM. The ATmega's EEPROM is rated ~100,000 writes per cell;
  the module writes ~127 bytes per refill and one boot counter per power-up.
  Reaching this takes a very long time in normal use.
- A genuinely failing chip.

The store detects this itself: each slot's checksum covers the bytes it was
*meant* to hold, so a cell that won't program makes the slot read as empty
rather than as a plausible but wrong image.

**3-4 — re-encoded image failed its final check.** The module encrypts the
refilled image, decodes it again, and confirms the printer would accept it
before writing a byte. Failing this means something is wrong at a level the
self-test didn't catch. Nothing was written. Reflash (§7); if it persists,
stop using the module.

---

## 7. Reflashing

The board is an FTDI Nano on **COM9** using the **old bootloader (57600)**.
Tools live in `C:\Users\trent\nano-flash`.

Build in WSL, flash from Windows:

```bash
cd ~/stratatools/nano_cartridge_refill
~/.pio-venv/bin/pio run -e nano_old
cp .pio/build/nano_old/firmware.hex /mnt/c/Users/trent/nano-flash/
cd /mnt/c/Users/trent/nano-flash
./avrdude.exe -C avrdude.conf -c arduino -P COM9 -b 57600 -p atmega328p \
  -U flash:w:firmware.hex:i
```

Watch the serial output:
```bash
cd /mnt/c/Users/trent/nano-flash
powershell.exe -NoProfile -ExecutionPolicy Bypass -File monitor.ps1 -Seconds 30
```

Notes:
- PlatformIO is at `~/.pio-venv/bin/pio`, not on `PATH`.
- If avrdude says "not in sync" / stk500 timeout, try `-b 115200` and build with
  `-e nano` instead.
- **Reflashing does not erase the recovery backups** — avrdude does not touch
  EEPROM unless told to. Your stored cartridge images survive a firmware update.
- WSL cannot see COM9 directly; that's why flashing happens from Windows. USB
  passthrough via `usbipd-win` is possible but needs admin and adds nothing here.

---

## 8. Expected serial output

At 115200 baud, a healthy boot looks like:

```
Stratasys standalone refill (Arduino Nano) - PRODIGY
Crypto self-test passed.
Recovery store: 0 of 7 slots in use.
RNG seeded (boot #5)
ACTION button: tap = inspect (read only), hold 1s = refill.
STATUS button: tap = replay last code + store level, hold 3s = restore.
```

If `Crypto self-test passed` is missing, the module is in the refuse-to-operate
state and will fast-blink forever.

---

## 9. First bring-up checklist

Work through this in order; each step only risks what the one before it proved.

1. **Power only, nothing else wired.** Expect the boot banner and a steady idle.
2. **Tap ACTION with no cartridge connected.** Expect **1-1**. This proves the
   button, LED, EEPROM store and self-test all work, with nothing at risk.
3. **Tap STATUS.** Expect a replay of 1-1, then one long blink (empty store).
4. **Connect a cartridge.** Check the data line idles near 5 V.
5. **Tap ACTION.** Expect **2 slow blinks** and, on serial, the cartridge's
   serial number and quantities. Nothing has been written.
6. **Only now** hold ACTION for 1 second to refill. Expect **3 slow blinks**.

If step 5 reports the wrong quantities or 2-1, stop — do not refill.
