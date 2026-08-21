#!/bin/sh
# Flatten this PlatformIO project into an Arduino IDE sketch folder.
#
# The Arduino IDE has no equivalent of PlatformIO's lib/ directory: every source
# file must sit next to the .ino. This copies them into ./arduino/nano_cartridge_refill/.
#
# Then, in the Arduino IDE:
#   1. Library Manager -> install "OneWire" by Paul Stoffregen
#   2. Tools -> Board -> Arduino Nano; Processor -> ATmega328P
#      (if uploading fails, switch Processor to "ATmega328P (Old Bootloader)")
#   3. Open arduino/nano_cartridge_refill/nano_cartridge_refill.ino and upload
set -e
cd "$(dirname "$0")"
OUT=arduino/nano_cartridge_refill
mkdir -p "$OUT"
cp lib/stratasys/*.h lib/stratasys/*.cpp "$OUT"/
cp lib/onewire/*.h lib/onewire/*.cpp "$OUT"/
cp lib/backup/*.h lib/backup/*.cpp "$OUT"/
cp src/main.cpp "$OUT"/nano_cartridge_refill.ino
echo "Sketch written to $OUT/"
