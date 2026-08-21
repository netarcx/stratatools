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
# Keep the firmware as a real .cpp. The Arduino IDE runs its own preprocessor
# ONLY on the .ino, hoisting auto-generated function prototypes to the top of
# the file -- above `enum PressAction`, which breaks the build. Files compiled
# as .cpp are passed to the compiler untouched, so the sketch is a stub and the
# firmware lives in main.cpp beside it.
cp src/main.cpp "$OUT"/main.cpp
cat > "$OUT"/nano_cartridge_refill.ino <<'INO'
// Sketch stub -- the firmware is in main.cpp in this folder (setup()/loop()
// are defined there). See make_arduino_sketch.sh for why.
INO
echo "Sketch written to $OUT/"
