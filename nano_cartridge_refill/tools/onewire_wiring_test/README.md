# 1-Wire wiring test

A bench tool for proving the wiring to the cartridge's DS2433 **before** running
the refill firmware at it. Same pins as the firmware, so a pass here is a pass
there.

**Read-only by construction.** After a reset the sketch issues only ROM-phase
commands — search, read ROM, match ROM, skip ROM — and exactly one memory-phase
command: read memory (`0xF0`). Write-scratchpad (`0x0F`) never appears. `0x55`
does appear, but only as the ROM command straight after a reset, where it means
match ROM; it is copy-scratchpad only in the memory phase, which this sketch
never enters. No button press or bus glitch can modify a cartridge while this is
loaded.

## Running it

Single self-contained file — nothing else from `nano_cartridge_refill` is needed.

**Arduino IDE:** install the *OneWire* library by Paul Stoffregen, open
`onewire_wiring_test.ino`, select Nano / ATmega328P (switch Processor to
*Old Bootloader* if upload fails), upload, open Serial Monitor at **115200**.

**PlatformIO:** from this directory,

```bash
pio run -t upload          # or: pio run -e nano_old -t upload
pio device monitor
```

**arduino-cli:**

```bash
arduino-cli compile --fqbn arduino:avr:nano:cpu=atmega328old .
arduino-cli upload  --fqbn arduino:avr:nano:cpu=atmega328old -p COM12 .
```

It runs once at power-up, then repeats the result on the LED forever. Tap
**ACTION (D9)** or send any character on serial to run again.

## What each stage proves

| Stage | Checks | Failure means |
|-------|--------|---------------|
| 1 Idle line level | Line idles high with our pin high-Z | No pull-up, short to GND, wrong pin, or a connected-but-unpowered module holding the bus down |
| 2 Pull-up strength | How fast the line recovers after being discharged | Missing pull-up (never recovers) or one far weaker than 4.7 kΩ |
| 3 Bus quiet | Nothing else drives the line for 300 ms | A second master — the printer — is active |
| 4 Presence pulse ×20 | Every reset gets an answer | 0/20: no device. 1–19/20: **intermittent** — cold joint, loose crimp, over-long leads |
| 5 ROM search | Enumerate, CRC-check, cross-check against read ROM | Presence works but timing doesn't: signal integrity, not a missing chip |
| 6 Read stability ×8 | Eight reads of the firmware's 113-byte span agree | The bus works but isn't dependable — do not write over it |

Stage 6 also rejects a uniform result: a data line that isn't reaching the chip
reads a stable block of `0xFF` every single time, which stages 1–5 cannot catch.

**Stage 1 passing does not prove the cartridge is on the bus.** The Nano's own
pull-up holds the line high whether or not anything is attached to the far end,
so an open data lead and a healthy bus look identical there. Probe mode below
is what tells them apart.

**A 0/20 in stage 4 is not a power fault.** The DS2433 answers a reset even
parasite-powered, so a missing VCC gives marginal *writes*, not silence.
Nothing answering at all means the chip is not electrically on the bus —
suspect the data lead or the ground return, not the supply.

A correct dump looks like noise — the cartridge image is encrypted. Decoding it
is the refill firmware's dry run, not this tool's job.

## Probe mode

After a 0/20 in stage 4 — and only then, since it cannot discriminate anything
else — the sketch drops into a 20-second probe. It watches the line and prints
every transition, with the LED following the level so it works with no console
attached.

Briefly touch the **data line to GND at the cartridge end** of the harness, not
at the Nano. Shorting 1-Wire data to GND is exactly what a bus reset does, so
this is safe on a live cartridge.

- **Transitions appear** → the data lead is continuous all the way back to D7.
  The wire is fine, so the ground return becomes the prime suspect.
- **Nothing appears** → that lead never reaches D7: an open wire, the wrong
  contact on the cartridge, or a SERVICE/RUN switch sitting in RUN.

Tap ACTION to skip the probe.

## Reading the LED

Three quick blinks at power-up confirm the LED itself works. After that the
result repeats using the **same codes as the refill firmware**, so
`../../TROUBLESHOOTING.md` applies unchanged:

| LED | Meaning |
|-----|---------|
| 2 slow blinks | All stages passed |
| 1-1 | No device on the bus |
| 1-2 | Read failed or unstable |
| 1-3 | Line held low, or another master active |

## One thing it cannot do

It proves a pull-up is **present and fast enough**. It cannot measure the
resistor — 4.7 kΩ and 47 kΩ both recover faster than one poll on a short lead.
Confirm the value with a meter.
