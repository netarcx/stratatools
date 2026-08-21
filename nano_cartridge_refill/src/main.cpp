/*
 * Standalone Stratasys cartridge refill -- Arduino Nano (ATmega328P)
 *
 * TAP the button to inspect a cartridge -- it reads, decodes and prints what is
 * on it, and writes nothing. HOLD the button for 1 second to actually refill it
 * (current quantity = initial, plus a fresh random serial number), re-encrypt,
 * write it back and verify the read-back -- all with no PC. The LED blinks
 * while you hold and goes solid once the refill is committed.
 *
 * Before every write the pre-write image is saved to the ATmega's internal
 * EEPROM. If a write is ever interrupted -- power loss, reset, cartridge pulled
 * mid-write -- the cartridge is left with failing checksums, which the printer
 * rejects and which this firmware also refuses to refill. To recover, hold the
 * button while powering up: after 3 seconds the LED goes solid, and on release
 * the last known-good image for the cartridge on the bus is written back
 * verbatim, skipping validation.
 *
 * Configured for a PRODIGY / P-class printer. The cartridge MUST be out of the
 * printer when you press the button (the printer and this module must not drive
 * the 1-Wire bus at the same time).
 *
 * LED codes: 3 slow blinks = refill success; 2 slow = dry run OK (nothing
 * written); 2 fast = no backup for this cartridge;
 * 3 fast = 1-Wire bus/read failure (cartridge untouched); 4 fast = could not
 * save the recovery backup (refill aborted); 5 fast = image rejected (wrong
 * printer type, or nothing to repair); one long blink + three short, repeated
 * = the cartridge may be HALF-WRITTEN, restore it; continuous fast = crypto
 * self-test failed.
 *
 * Wiring (Arduino Nano / Uno / Pro Mini -- any ATmega328P board):
 *   ONEWIRE_PIN (D3)  -- EEPROM data, with a 4.7k pull-up to +5V
 *   BUTTON_PIN  (D2)  -- momentary button to GND (uses the internal pull-up)
 *   LED_PIN     (D13) -- the onboard LED (active-high)
 *
 * NOTE ON 5V: a Nano runs its I/O at 5 V. The DS2433 in the cartridge is rated
 * 2.8-5.25 V, so pull the 1-Wire line up to the Nano's +5V rail -- NOT to 3.3V,
 * and do not mix this with a 3.3 V board on the same bus.
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
static const uint8_t BUTTON_PIN  = 2;   // to GND; pressed = LOW
static const uint8_t LED_PIN     = LED_BUILTIN;   // D13
static const bool    LED_ACTIVE_LOW = false;      // Nano's onboard LED is active-high

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
    digitalWrite(LED_PIN, (on ^ LED_ACTIVE_LOW) ? HIGH : LOW);
}
static void blink(uint8_t times, uint16_t on_ms, uint16_t off_ms) {
    for (uint8_t i = 0; i < times; i++) { led(true); delay(on_ms); led(false); delay(off_ms); }
}
static void fail(const __FlashStringHelper *msg, uint8_t blinks, uint16_t ms) {
    Serial.print(F("ERROR: ")); Serial.println(msg);
    led(false);
    blink(blinks, ms, ms);
}

// The one outcome that needs the user to act: the cartridge may be half-written.
// Deliberately unlike every other code -- a long blink then three short, three
// times -- because it must not be mistaken for a benign bus error.
static void failDamaged(const __FlashStringHelper *msg) {
    Serial.print(F("ERROR: ")); Serial.println(msg);
    Serial.println(F("       The cartridge may be half-written. Power-cycle holding the"));
    Serial.println(F("       button for 3s to restore the saved image."));
    led(false);
    for (uint8_t i = 0; i < 3; i++) {
        led(true); delay(800); led(false); delay(250);
        blink(3, 100, 100);
        delay(400);
    }
}

// A single HIGH sample is not a release: contacts bounce. Require the button to
// read high continuously before treating it as let go.
static bool buttonReleasedStable() {
    for (uint8_t i = 0; i < 5; i++) {
        if (digitalRead(BUTTON_PIN) == LOW) return false;
        delay(10);
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
    while (!buttonReleasedStable()) {
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
        fail(F("no cartridge found on 1-Wire bus"), 3, 120);
        return;
    }
    char rom[17];
    ow.getRomAddress(rom);
    Serial.print(F("Cartridge ROM: ")); Serial.println(rom);
    if (ow.romBytes()[0] != 0x23) {
        Serial.println(F("WARNING: 1-Wire family code is not 0x23 (DS2433)."));
    }

    if (!ow.read(0, g_buf, STRATASYS_EEPROM_LEN)) {
        fail(F("read failed"), 3, 120);
        return;
    }

    if (!stratasys_decode(g_buf, g_machine, ow.romBytes())) {
        if (backup_load(ow.romBytes(), g_verify)) {
            failDamaged(F("cartridge does not validate, but a recovery backup exists for it"));
        } else {
            fail(F("not a valid PRODIGY cartridge (wrong printer type?)"), 5, 80);
        }
        return;
    }

    printCartridge(F("Contents:"), g_buf);
    Serial.println(F("Dry run complete - cartridge NOT modified."));
    Serial.println(F("Hold the button 1s to actually refill it."));
    led(false);
    blink(2, 300, 200);   // 2 slow blinks = read OK, nothing written
}

// ---- Refill sequence ------------------------------------------------------
static void doRefill() {
    Serial.println(F("\n=== Refill requested ==="));
    led(true);   // solid = busy

    if (!ow.search()) {
        fail(F("no cartridge found on 1-Wire bus"), 3, 120);
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
        fail(F("read failed"), 3, 120);
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
            failDamaged(F("cartridge does not validate, but a recovery backup exists for it"));
        } else {
            fail(F("not a valid PRODIGY cartridge (wrong printer type?)"), 5, 80);
        }
        return;
    }

    // Only back up images that decoded cleanly -- a garbled read must never
    // become the recovery copy. Refuse to write if the backup didn't stick:
    // writing without a way back is the thing this whole path exists to avoid.
    if (!backup_save(ow.romBytes(), g_verify)) {
        fail(F("could not save recovery backup - refill aborted, cartridge untouched"), 4, 80);
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
        fail(F("re-encoded image failed validation - nothing written"), 5, 80);
        return;
    }

    if (!ow.write(0, g_buf, STRATASYS_EEPROM_LEN)) {
        failDamaged(F("write failed"));
        return;
    }

    // Verify the read-back matches exactly
    if (!ow.read(0, g_verify, STRATASYS_EEPROM_LEN) ||
        memcmp(g_verify, g_buf, STRATASYS_EEPROM_LEN) != 0) {
        failDamaged(F("write verification mismatch"));
        return;
    }

    Serial.println(F("SUCCESS: cartridge refilled and verified."));
    led(false);
    blink(3, 300, 200);   // 3 slow blinks = success
}

// ---- Restore sequence -----------------------------------------------------
// Writes a stored image back verbatim, deliberately skipping validation: the
// whole point is to repair a cartridge whose checksums no longer verify because
// a previous write was interrupted part-way through.
static void doRestore() {
    Serial.println(F("\n=== RESTORE requested ==="));
    led(true);   // solid = busy

    if (!ow.search()) {
        fail(F("no cartridge found on 1-Wire bus"), 3, 120);
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
        fail(F("cartridge already validates - nothing to repair, refusing to overwrite"), 5, 80);
        return;
    }

    if (!backup_load(ow.romBytes(), g_buf)) {
        fail(F("no backup stored for this cartridge"), 2, 80);
        return;
    }

    // Validation is bypassed against the *cartridge*, not against the backup:
    // confirm the stored image is itself a sane cartridge for this printer and
    // UID before committing it. A backup that cannot decode is not a repair.
    memcpy(g_verify, g_buf, STRATASYS_EEPROM_LEN);
    if (!stratasys_decode(g_verify, g_machine, ow.romBytes())) {
        fail(F("stored backup does not validate - refusing to write it"), 5, 80);
        return;
    }

    Serial.println(F("Writing last known-good image (cartridge-side validation bypassed)..."));
    if (!ow.write(0, g_buf, STRATASYS_EEPROM_LEN)) {
        fail(F("write failed"), 3, 120);
        return;
    }
    if (!ow.read(0, g_verify, STRATASYS_EEPROM_LEN) ||
        memcmp(g_verify, g_buf, STRATASYS_EEPROM_LEN) != 0) {
        fail(F("write verification mismatch"), 3, 120);
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

    Serial.println(F("SUCCESS: cartridge restored."));
    led(false);
    blink(3, 300, 200);
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
            while (!buttonReleasedStable()) {
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
    pinMode(BUTTON_PIN, INPUT_PULLUP);
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

    Serial.println(F("Insert cartridge (out of printer). TAP the button to inspect it,"));
    Serial.println(F("or HOLD 1s to refill it."));
}

void loop() {
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
        while (!buttonReleasedStable()) delay(10);
    }
    delay(10);
}
