# Cartridge backups

Raw pre-write images, exactly as stored on each cartridge's DS2433 — encrypted,
113 bytes, no transformation applied. Restoring one means writing these bytes
back verbatim.

Files are named `<rom>-<date>.bin`, with a `.txt` alongside recording the ROM,
SHA-256, decoded contents and how the image was captured.

## Before trusting one

A backup nobody has decoded is not a backup. Verify with:

```sh
tools/verify_dump/build.sh
tools/verify_dump/verify_dump backups/<rom>-<date>.bin <rom>
```

It must print `validate : PASS` and `decode : PASS`, and the quantities must
match what the cartridge actually reported.

## Note on the firmware's own backups

The refill firmware separately saves a recovery image to the ATmega's internal
EEPROM before every write, and a STATUS hold restores from it. These files are
the independent copy: they survive the board being replaced, reflashed with a
chip erase, or lost entirely.
