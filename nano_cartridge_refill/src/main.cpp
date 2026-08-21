/*
 * Standalone Stratasys cartridge refill -- Arduino Nano (ATmega328P)
 *
 * Designed to be embedded in the cartridge and operated with no computer
 * attached: every outcome is reported on an external LED as a two-part blink
 * code, and the last code is kept in EEPROM so it survives a power cycle and
 * can be replayed on demand.
 *
 * CONTROLS
 *   ACTION button  tap    -> dry run: read, decode and report; writes nothing
 *                  hold 1s-> refill (LED blinks while held, solid once armed)
 *   STATUS button  tap    -> replay the last result code, then blink the number
 *                            of recovery backups held
 *                  hold 3s-> restore this cartridge from its saved image
 *   (Holding ACTION through power-up also enters restore, as a fallback if the
 *    STATUS button is not wired.)
 *
 * RESULT CODES -- MAJOR long blinks, then MINOR short blinks, repeated 3x:
 *   1-1 no device on the 1-Wire bus        1-2 read failed
 *   1-3 bus busy: another master active (cartridge still in the printer?)
 *   2-1 not a valid cartridge for this printer
 *   2-2 does not validate but a backup exists -- likely half-written, restore it
 *   2-3 restore refused: the cartridge is already valid
 *   3-1 could not save the recovery backup  3-2 write failed
 *   3-3 write verify mismatch               3-4 re-encoded image failed checks
 *   4-1 no backup stored for this cartridge 4-2 stored backup does not validate
 * Success is a different shape -- slow even blinks, no long preamble:
 *   2 = dry run OK, 3 = refill OK, 4 = restore OK.
 * Continuous fast blinking at power-up = crypto self-test failed; it will not
 * operate at all in that state.
 *
 * WIRING (Arduino Nano / Uno / Pro Mini -- any ATmega328P board):
 *   D3  1-Wire data, 4.7k pull-up to +5V   (see the README before adding a
 *                                           second pull-up alongside a printer)
 *   D2  ACTION button to GND               (internal pull-up)
 *   D4  STATUS button to GND               (internal pull-up; optional)
 *   D5  external LED, via ~330R to GND
 *   D13 onboard LED, mirrors D5
 *
 * NOTE ON 5V: a Nano runs its I/O at 5 V. The DS2433 in the cartridge is rated
 * 2.8-5.25 V, so pull the 1-Wire line up to the Nano's +5V rail -- NOT to 3.3V.
 *
 * BEFORE EMBEDDING THIS IN A CARTRIDGE, read the "Embedded in a cartridge"
 * section of the README. Sharing the 1-Wire bus with the printer needs care
 * beyond what firmware alone can guarantee.
 */
#include <Arduino.h>
#include <string.h>

#include "onewire_handler.h"
#include "cartridge_backup.h"
#include "stratasys_codec.h"
#include "f64.h"
#include "des.h"

// ---- Configuration --------------------------------------------------------
static const uint8_t ONEWIRE_PIN = 3;
static const uint8_t BUTTON_PIN  = 2;   // ACTION button to GND; pressed = LOW
static const uint8_t STATUS_PIN  = 4;   // STATUS button to GND (optional -- reads
                                        // high and stays inert if not wired)
static const uint8_t LED_PIN     = LED_BUILTIN;   // D13, onboard
static const uint8_t EXT_LED_PIN = 5;   // external LED + series resistor to GND
static const bool    LED_ACTIVE_LOW = false;      // both LEDs are active-high

// Prodigy / P-class machine key (machine.py "prodigy" = 5394D7657CED641D)
static const uint8_t MACHINE[8] PROGMEM = {0x53,0x94,0xD7,0x65,0x7C,0xED,0x64,0x1D};

// Serial numbers are drawn from 1..999999, matching the ESP32 firmware.
static const uint32_t SERIAL_MODULUS = 999999UL;

// How long the button must be held at power-up to enter restore mode.
static const unsigned long RESTORE_HOLD_MS = 3000;

// A quick tap inspects the cartridge and writes nothing; a refill takes a
// deliberate hold. Making the destructive action the one that needs intent
// means a knocked button can never modify a cartridge.
static const unsigned long REFILL_HOLD_MS = 1000;

// ---- Crypto power-on self-test vectors (small known-answer tests) ----------
static const uint8_t KAT_DES_KEY[8] PROGMEM  = {0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef};
static const uint8_t KAT_DES_PT[8] PROGMEM   = {0x4e,0x6f,0x77,0x20,0x69,0x73,0x20,0x74};
static const uint8_t KAT_DES_CT[8] PROGMEM   = {0x3f,0xa4,0x0e,0x8a,0x98,0x4d,0x48,0x15};
static const uint8_t KAT_DESX_KEY[16] PROGMEM= {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
static const uint8_t KAT_DESX_PT[16] PROGMEM = {0x74,0x68,0x69,0x73,0x20,0x69,0x73,0x20,
                                                0x61,0x20,0x74,0x65,0x73,0x74,0x2e,0x2e};
static const uint8_t KAT_DESX_CT[16] PROGMEM = {0x38,0xdb,0x9b,0xe0,0x9d,0x1b,0x24,0xa0,
                                                0x7c,0x77,0x49,0x26,0xaf,0x94,0xe8,0xd5};
// binary64 encoding of 1.0, to prove f64_from_u32 works on this build
static const uint8_t KAT_F64_ONE[8] PROGMEM = {0,0,0,0,0,0,0xF0,0x3F};

// ---- State ----------------------------------------------------------------
OneWireHandler ow(ONEWIRE_PIN);

// The single working buffer: EEPROM image -> plaintext -> EEPROM image again.
// The codec runs in place, so one buffer covers the whole refill.
static uint8_t g_buf[STRATASYS_EEPROM_LEN];
static uint8_t g_verify[STRATASYS_EEPROM_LEN];
static uint8_t g_machine[8];        // MACHINE copied out of flash
static uint32_t g_rng;

// ---- LED helpers ----------------------------------------------------------
static void led(bool on) {
    uint8_t level = (on ^ LED_ACTIVE_LOW) ? HIGH : LOW;
    digitalWrite(LED_PIN, level);
    digitalWrite(EXT_LED_PIN, level);   // external LED mirrors the onboard one
}
static void blink(uint8_t times, uint16_t on_ms, uint16_t off_ms) {
    for (uint8_t i = 0; i < times; i++) { led(true); delay(on_ms); led(false); delay(off_ms); }
}

// ---- Result codes ---------------------------------------------------------
// This unit lives inside a cartridge with no serial console attached, so the
// LED is the only diagnostic. Every outcome is a two-part code: MAJOR long
// blinks (the category) then MINOR short blinks (the detail), repeated. Two
// short groups are far easier to count correctly than one long run of eleven.
//
//   1-x  bus / hardware      1 no device   2 read failed   3 bus busy
//   2-x  cartridge content   1 wrong printer type   2 half-written (restore it)
//                            3 restore refused: cartridge is already valid
//   3-x  write / backup      1 backup save failed   2 write failed
//                            3 verify mismatch      4 encode check failed
//   4-x  restore             1 no backup stored     2 stored backup invalid
//
// Success is deliberately different in shape -- slow even blinks, no long
// preamble: 2 = dry run OK, 3 = refill OK, 4 = restore OK.
#define RC_OK_DRYRUN   2
#define RC_OK_REFILL   3
#define RC_OK_RESTORE  4

#define RC_BUS         1
#define RC_BUS_NODEV     1
#define RC_BUS_READ      2
#define RC_BUS_BUSY      3

#define RC_CONTENT     2
#define RC_CONTENT_TYPE  1
#define RC_CONTENT_HALF  2
#define RC_CONTENT_OK    3

#define RC_WRITE       3
#define RC_WRITE_BACKUP  1
#define RC_WRITE_FAILED  2
#define RC_WRITE_VERIFY  3
#define RC_WRITE_ENCODE  4

#define RC_RESTORE     4
#define RC_RESTORE_NONE  1
#define RC_RESTORE_BAD   2

// Emit one two-part code, once.
static void emitCode(uint8_t major, uint8_t minor) {
    blink(major, 700, 300);
    delay(500);
    blink(minor, 150, 250);
}

// Report a failure: remember it, say it on serial for anyone who has a console,
// and blink it three times so it can be read and confirmed.
static void failCode(uint8_t major, uint8_t minor, const __FlashStringHelper *msg) {
    Serial.print(F("ERROR ")); Serial.print((unsigned long)major);
    Serial.print(F("-"));      Serial.print((unsigned long)minor);
    Serial.print(F(": "));     Serial.println(msg);
    backup_set_last_result(major, minor);
    led(false);
    delay(400);
    for (uint8_t i = 0; i < 3; i++) { emitCode(major, minor); delay(1200); }
}

static void okCode(uint8_t blinks, const __FlashStringHelper *msg) {
    Serial.println(msg);
    backup_set_last_result(0, blinks);   // major 0 = success, minor = which
    led(false);
    delay(300);
    blink(blinks, 400, 400);
}

// A single HIGH sample is not a release: contacts bounce. Require the button to
// read high continuously before treating it as let go.
static bool buttonReleasedStable(uint8_t pin) {
    for (uint8_t i = 0; i < 5; i++) {
        if (digitalRead(pin) == LOW) return false;
        delay(10);
    }
    return true;
}

// ---- Bus contention guard -------------------------------------------------
// With our pin high-Z an idle 1-Wire bus sits high, held there by the pull-up.
// Anything pulling it low while we are not driving it is a second master --
// almost certainly the printer. Starting a write then would collide with a
// printer transaction and leave the cartridge half-written, so refuse.
//
// This catches an ACTIVE printer, not merely a connected idle one. It is a
// backstop, not a substitute for isolating the bus while the cartridge is in
// the machine -- see the README.
static bool busIdle(uint16_t ms) {
    pinMode(ONEWIRE_PIN, INPUT);
    unsigned long start = millis();
    while (millis() - start < ms) {
        if (digitalRead(ONEWIRE_PIN) == LOW) return false;
        delayMicroseconds(50);          // 1-Wire slots are ~60us; this catches them
    }
    return true;
}

// ---- Random serial numbers -------------------------------------------------
// The ATmega328P has no hardware RNG, so seed a xorshift32 from three sources:
// the noise on the floating analog inputs, a boot counter in the internal
// EEPROM, and (at press time) the microsecond timestamp of the button press.
static uint32_t rngNext() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static void rngStir(uint32_t v) {
    g_rng ^= v;
    if (g_rng == 0) g_rng = 1;      // xorshift32 must never reach zero
    rngNext();
}

static void rngInit() {
    uint32_t counter = backup_bump_boot_counter();  // one EEPROM write per power-up

    uint32_t s = 2166136261UL;                     // FNV-1a offset basis
    for (uint8_t i = 0; i < 6; i++) {              // A0..A5 read as noise
        s ^= (uint32_t)analogRead(A0 + i);
        s *= 16777619UL;
    }
    s ^= counter;
    s *= 16777619UL;
    g_rng = s ? s : 1;

    Serial.print(F("RNG seeded (boot #")); Serial.print(counter); Serial.println(F(")"));
}

// ---- Crypto self-test (never write if this fails) -------------------------
static bool selfTest() {
    uint8_t key[16], in[16], expect[16], out[16];
    uint64_t sk[16];

    memcpy_P(key, KAT_DES_KEY, 8);
    memcpy_P(in, KAT_DES_PT, 8);
    memcpy_P(expect, KAT_DES_CT, 8);
    des_key_schedule(key, sk);
    des_process_block(sk, in, out, false);
    if (memcmp(out, expect, 8) != 0) return false;

    memcpy_P(key, KAT_DESX_KEY, 16);
    memcpy_P(in, KAT_DESX_PT, 16);
    memcpy_P(expect, KAT_DESX_CT, 16);
    stratasys_desx_encrypt(key, in, out, 16);
    if (memcmp(out, expect, 16) != 0) return false;

    stratasys_desx_decrypt(key, expect, out, 16);
    if (memcmp(out, in, 16) != 0) return false;

    if (stratasys_crc16((const uint8_t *)"abcd", 4) != 14743) return false;

    // avr-gcc's `double` is 32-bit, so the cartridge's 64-bit fields are built
    // by hand. Prove that hand-rolled encoder is correct before writing with it.
    memcpy_P(expect, KAT_F64_ONE, 8);
    f64_from_u32(out, 1UL);
    if (memcmp(out, expect, 8) != 0) return false;

    return true;
}

// ---- Reporting ------------------------------------------------------------
static void printQty(const __FlashStringHelper *label, const uint8_t *f64) {
    Serial.print(label);
    Serial.print(f64_to_float(f64), 2);
}

// Report a decoded (plaintext) cartridge image.
static void printCartridge(const __FlashStringHelper *label, const uint8_t *plain) {
    Serial.print(label);
    Serial.print(F(" serial="));
    Serial.print(f64_to_float(&plain[STRATASYS_OFF_SERIAL]), 0);
    printQty(F("  current="), &plain[STRATASYS_OFF_CURRENT_QTY]);
    printQty(F(" / initial="), &plain[STRATASYS_OFF_INITIAL_QTY]);
    Serial.println(F(" cu.in"));
}

// ---- Button (active-low, debounced) ---------------------------------------
// What a press meant, decided by how long it was held.
enum PressAction { PRESS_NONE, PRESS_DRYRUN, PRESS_REFILL };

static PressAction readPress() {
    if (digitalRead(BUTTON_PIN) != LOW) return PRESS_NONE;
    delay(30);                                   // debounce
    if (digitalRead(BUTTON_PIN) != LOW) return PRESS_NONE;

    // The exact microsecond a human presses a button is genuinely
    // unpredictable, so it makes a good entropy source on a chip with no RNG.
    rngStir(micros());

    unsigned long start = millis();
    bool committed = false;
    while (!buttonReleasedStable(BUTTON_PIN)) {
        unsigned long held = millis() - start;
        if (held > 6000) {                       // stuck button: give up
            Serial.println(F("WARN: button held >6s - ignoring (stuck?)"));
            led(false);
            return PRESS_NONE;
        }
        if (committed) {
            // solid: releasing now performs the refill
        } else if (held >= REFILL_HOLD_MS) {
            committed = true;
            led(true);
        } else {
            led((held / 100) & 1);               // blink: still just a dry run
        }
        delay(10);
    }
    rngStir(micros());
    led(false);
    return committed ? PRESS_REFILL : PRESS_DRYRUN;
}

// ---- Dry run: read and report, write nothing ------------------------------
static void doDryRun() {
    Serial.println(F("\n=== Dry run (read only - nothing will be written) ==="));
    led(true);

    if (!ow.search()) {
        failCode(RC_BUS, RC_BUS_NODEV, F("no cartridge found on 1-Wire bus"));
        return;
    }
    char rom[17];
    ow.getRomAddress(rom);
    Serial.print(F("Cartridge ROM: ")); Serial.println(rom);
    if (ow.romBytes()[0] != 0x23) {
        Serial.println(F("WARNING: 1-Wire family code is not 0x23 (DS2433)."));
    }

    if (!ow.read(0, g_buf, STRATASYS_EEPROM_LEN)) {
        failCode(RC_BUS, RC_BUS_READ, F("read failed"));
        return;
    }

    if (!stratasys_decode(g_buf, g_machine, ow.romBytes())) {
        if (backup_load(ow.romBytes(), g_verify)) {
            failCode(RC_CONTENT, RC_CONTENT_HALF, F("does not validate but a backup exists - likely half-written"));
        } else {
            failCode(RC_CONTENT, RC_CONTENT_TYPE, F("not a valid PRODIGY cartridge (wrong printer type?)"));
        }
        return;
    }

    printCartridge(F("Contents:"), g_buf);
    Serial.println(F("Hold the button 1s to actually refill it."));
    okCode(RC_OK_DRYRUN, F("Dry run complete - cartridge NOT modified."));
}

// ---- Status button --------------------------------------------------------
// The whole point of this button: get the unit's state out of it with no serial
// console. Tap replays the last result code and then reports how many recovery
// backups are stored. Hold 3s runs a restore, so repairing a half-written
// cartridge does not require a power cycle once the unit is sealed in.
static void doStatusReport() {
    uint8_t major = 0, minor = 0;
    backup_get_last_result(&major, &minor);

    Serial.print(F("\nLast result: "));
    if (major == 0 && minor == 0) {
        Serial.println(F("(none recorded yet)"));
    } else if (major == 0) {
        Serial.print(F("success, code ")); Serial.println((unsigned long)minor);
    } else {
        Serial.print((unsigned long)major); Serial.print(F("-")); Serial.println((unsigned long)minor);
    }
    Serial.print(F("Recovery backups stored: ")); Serial.print((unsigned long)backup_count());
    Serial.print(F(" of ")); Serial.println((unsigned long)BACKUP_SLOTS);

    led(false);
    delay(600);
    if (major == 0 && minor == 0) {
        blink(1, 150, 250);                  // nothing recorded
    } else if (major == 0) {
        blink(minor, 400, 400);              // replay the success pattern
    } else {
        emitCode(major, minor);              // replay the fault code
    }

    // Then the store level: a pause, then one short blink per stored backup
    // (or one long blink if the store is empty).
    delay(1500);
    uint8_t n = backup_count();
    if (n == 0) blink(1, 700, 300);
    else        blink(n, 150, 250);
}

// ---- Refill sequence ------------------------------------------------------
static void doRefill() {
    Serial.println(F("\n=== Refill requested ==="));
    led(true);   // solid = busy

    if (!busIdle(300)) {
        failCode(RC_BUS, RC_BUS_BUSY,
                 F("another 1-Wire master is active - cartridge still in the printer?"));
        return;
    }

    if (!ow.search()) {
        failCode(RC_BUS, RC_BUS_NODEV, F("no cartridge found on 1-Wire bus"));
        return;
    }
    char rom[17];
    ow.getRomAddress(rom);
    Serial.print(F("Cartridge ROM: ")); Serial.println(rom);
    if (ow.romBytes()[0] != 0x23) {
        // Not fatal -- the decode below is the real gate -- but worth saying,
        // since it means the bus enumerated something that isn't a DS2433.
        Serial.println(F("WARNING: 1-Wire family code is not 0x23 (DS2433)."));
    }

    if (!ow.read(0, g_buf, STRATASYS_EEPROM_LEN)) {
        failCode(RC_BUS, RC_BUS_READ, F("read failed"));
        return;
    }

    // Keep the image exactly as it came off the cartridge: decoding runs in
    // place, so this is the only chance to capture it. g_verify is free until
    // the read-back check at the end, so no third buffer is needed.
    memcpy(g_verify, g_buf, STRATASYS_EEPROM_LEN);

    // Decode in place and confirm it's a genuine prodigy cartridge. On failure
    // g_buf holds garbage, but nothing is written, so the cartridge is untouched.
    if (!stratasys_decode(g_buf, g_machine, ow.romBytes())) {
        // Two very different causes look identical here. If we are holding a
        // recovery image for this exact cartridge, the likely explanation is an
        // interrupted write -- and telling the user "wrong printer type" would
        // steer them away from the restore that fixes it.
        if (backup_load(ow.romBytes(), g_verify)) {
            failCode(RC_CONTENT, RC_CONTENT_HALF, F("does not validate but a backup exists - likely half-written"));
        } else {
            failCode(RC_CONTENT, RC_CONTENT_TYPE, F("not a valid PRODIGY cartridge (wrong printer type?)"));
        }
        return;
    }

    // Only back up images that decoded cleanly -- a garbled read must never
    // become the recovery copy. Refuse to write if the backup didn't stick:
    // writing without a way back is the thing this whole path exists to avoid.
    if (!backup_save(ow.romBytes(), g_verify)) {
        failCode(RC_WRITE, RC_WRITE_BACKUP, F("could not save recovery backup - refill aborted"));
        return;
    }
    Serial.println(F("Recovery backup saved to internal EEPROM."));

    // g_buf is now the plaintext image.
    printCartridge(F("Before:"), g_buf);

    // Refill: current quantity = initial quantity, plus a fresh serial number.
    uint32_t newSerial = (rngNext() % SERIAL_MODULUS) + 1UL;
    f64_from_u32(&g_buf[STRATASYS_OFF_SERIAL], newSerial);
    memcpy(&g_buf[STRATASYS_OFF_CURRENT_QTY], &g_buf[STRATASYS_OFF_INITIAL_QTY], 8);

    Serial.print(F("Refilling: new serial=")); Serial.print(newSerial);
    printQty(F(", current set to "), &g_buf[STRATASYS_OFF_CURRENT_QTY]);
    Serial.println(F(" cu.in"));

    // Re-encrypt in place, then write it back.
    stratasys_encode(g_buf, g_machine, ow.romBytes());

    // Last guard before any byte reaches the cartridge: decode what we are about
    // to write and confirm the printer will accept it. g_verify is free here --
    // the recovery copy is already committed to internal EEPROM.
    memcpy(g_verify, g_buf, STRATASYS_EEPROM_LEN);
    if (!stratasys_decode(g_verify, g_machine, ow.romBytes())) {
        failCode(RC_WRITE, RC_WRITE_ENCODE, F("re-encoded image failed validation - nothing written"));
        return;
    }

    if (!ow.write(0, g_buf, STRATASYS_EEPROM_LEN)) {
        failCode(RC_WRITE, RC_WRITE_FAILED, F("write failed"));
        return;
    }

    // Verify the read-back matches exactly
    if (!ow.read(0, g_verify, STRATASYS_EEPROM_LEN) ||
        memcmp(g_verify, g_buf, STRATASYS_EEPROM_LEN) != 0) {
        failCode(RC_WRITE, RC_WRITE_VERIFY, F("write verification mismatch"));
        return;
    }

    okCode(RC_OK_REFILL, F("SUCCESS: cartridge refilled and verified."));
}

// ---- Restore sequence -----------------------------------------------------
// Writes a stored image back verbatim, deliberately skipping validation: the
// whole point is to repair a cartridge whose checksums no longer verify because
// a previous write was interrupted part-way through.
static void doRestore() {
    Serial.println(F("\n=== RESTORE requested ==="));
    led(true);   // solid = busy

    if (!busIdle(300)) {
        failCode(RC_BUS, RC_BUS_BUSY,
                 F("another 1-Wire master is active - cartridge still in the printer?"));
        return;
    }

    if (!ow.search()) {
        failCode(RC_BUS, RC_BUS_NODEV, F("no cartridge found on 1-Wire bus"));
        return;
    }
    char rom[17];
    ow.getRomAddress(rom);
    Serial.print(F("Cartridge ROM: ")); Serial.println(rom);

    // Restore is the only unvalidated write in the firmware, so establish that
    // it is actually needed. A cartridge that still decodes cleanly does not
    // need repairing, and rolling it back to an older image would be a
    // destructive write with nothing to gain.
    if (ow.read(0, g_verify, STRATASYS_EEPROM_LEN) &&
        stratasys_decode(g_verify, g_machine, ow.romBytes())) {
        failCode(RC_CONTENT, RC_CONTENT_OK, F("cartridge already validates - nothing to repair"));
        return;
    }

    if (!backup_load(ow.romBytes(), g_buf)) {
        failCode(RC_RESTORE, RC_RESTORE_NONE, F("no backup stored for this cartridge"));
        return;
    }

    // Validation is bypassed against the *cartridge*, not against the backup:
    // confirm the stored image is itself a sane cartridge for this printer and
    // UID before committing it. A backup that cannot decode is not a repair.
    memcpy(g_verify, g_buf, STRATASYS_EEPROM_LEN);
    if (!stratasys_decode(g_verify, g_machine, ow.romBytes())) {
        failCode(RC_RESTORE, RC_RESTORE_BAD, F("stored backup does not validate - refusing to write it"));
        return;
    }

    Serial.println(F("Writing last known-good image (cartridge-side validation bypassed)..."));
    if (!ow.write(0, g_buf, STRATASYS_EEPROM_LEN)) {
        failCode(RC_WRITE, RC_WRITE_FAILED, F("write failed"));
        return;
    }
    if (!ow.read(0, g_verify, STRATASYS_EEPROM_LEN) ||
        memcmp(g_verify, g_buf, STRATASYS_EEPROM_LEN) != 0) {
        failCode(RC_WRITE, RC_WRITE_VERIFY, F("write verification mismatch"));
        return;
    }

    // Report what the cartridge now holds. g_verify is the read-back, and
    // decoding it is destructive, but it has served its purpose by this point.
    if (stratasys_decode(g_verify, g_machine, ow.romBytes())) {
        printCartridge(F("Restored:"), g_verify);
    } else {
        Serial.println(F("NOTE: restored bytes verified, but they do not decode "
                         "with the configured printer key."));
    }

    okCode(RC_OK_RESTORE, F("SUCCESS: cartridge restored."));
}

// The restore gesture: button already down at power-up, held for 3 seconds.
// It blinks while counting down so an accidental press can be released in time,
// then goes solid once committed. Returns true after the button is released.
static bool restoreGesture() {
    if (digitalRead(BUTTON_PIN) != LOW) return false;

    Serial.println(F("Button held at boot - keep holding 3s for RESTORE, release to cancel."));
    unsigned long start = millis();
    while (digitalRead(BUTTON_PIN) == LOW) {
        if (millis() - start >= RESTORE_HOLD_MS) {
            led(true);                         // solid = committed
            Serial.println(F("RESTORE armed - release the button."));
            unsigned long held = millis();
            while (!buttonReleasedStable(BUTTON_PIN)) {
                if (millis() - held > 10000) { // stuck button: don't run blind
                    Serial.println(F("WARN: button still held after 10s - cancelling restore."));
                    led(false);
                    return false;
                }
                delay(10);
            }
            return true;
        }
        led(((millis() - start) / 150) & 1);   // blink while counting down
        delay(10);
    }
    led(false);
    Serial.println(F("Restore cancelled - continuing in normal refill mode."));
    return false;
}

void setup() {
    Serial.begin(115200);
    delay(300);
    pinMode(LED_PIN, OUTPUT);
    pinMode(EXT_LED_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(STATUS_PIN, INPUT_PULLUP);   // unwired reads high and stays inert
    led(false);

    memcpy_P(g_machine, MACHINE, 8);

    Serial.println(F("\nStratasys standalone refill (Arduino Nano) - PRODIGY"));

    if (!selfTest()) {
        Serial.println(F("FATAL: crypto self-test FAILED - refusing to operate."));
        // Fast continuous blink forever: do not let a miscompiled build write.
        for (;;) blink(1, 60, 60);
    }
    Serial.println(F("Crypto self-test passed."));

    if (backup_begin()) {
        Serial.print(F("Recovery store: ")); Serial.print((unsigned long)backup_count());
        Serial.print(F(" of ")); Serial.print((unsigned long)BACKUP_SLOTS);
        Serial.println(F(" slots in use."));
    } else {
        Serial.println(F("Recovery store formatted (no backups yet)."));
    }

    rngInit();

    if (restoreGesture()) {
        doRestore();
        Serial.println(F("Ready for next cartridge."));
        return;
    }

    Serial.println(F("ACTION button: tap = inspect (read only), hold 1s = refill."));
    Serial.println(F("STATUS button: tap = replay last code + store level, hold 3s = restore."));
}

void loop() {
    // STATUS button: tap = report, hold 3s = restore.
    if (digitalRead(STATUS_PIN) == LOW) {
        delay(30);
        if (digitalRead(STATUS_PIN) == LOW) {
            unsigned long start = millis();
            bool restore = false;
            while (!buttonReleasedStable(STATUS_PIN)) {
                if (millis() - start > 10000) break;      // stuck: treat as a tap
                if (!restore && millis() - start >= RESTORE_HOLD_MS) {
                    restore = true;
                    led(true);                            // solid = restore armed
                }
                delay(10);
            }
            led(false);
            if (restore) doRestore();
            else         doStatusReport();
            while (!buttonReleasedStable(STATUS_PIN)) delay(10);
        }
    }

    PressAction action = readPress();
    if (action == PRESS_DRYRUN) {
        doDryRun();
    } else if (action == PRESS_REFILL) {
        doRefill();
        Serial.println(F("Ready for next cartridge."));
    }
    if (action != PRESS_NONE) {
        // A press made *during* the operation would otherwise be waiting here
        // and immediately trigger another one.
        while (!buttonReleasedStable(BUTTON_PIN)) delay(10);
    }
    delay(10);
}
