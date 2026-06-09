/*
 * Standalone Stratasys cartridge refill — ESP32-C3
 *
 * Press the button and the ESP32 reads the cartridge EEPROM, validates it,
 * refills it (current quantity = initial, plus a fresh random serial number),
 * re-encrypts it, writes it back, and verifies the read-back — with no PC.
 *
 * Configured for a PRODIGY / P-class printer. The cartridge MUST be out of the
 * printer when you press the button (the printer and this module must not drive
 * the 1-Wire bus at the same time).
 *
 * Wiring (generic ESP32-C3 devkit):
 *   ONEWIRE_PIN (GPIO4) -- EEPROM data, with a 4.7k pull-up to 3.3V
 *   BUTTON_PIN  (GPIO9) -- BOOT button (to GND), or your own momentary button
 *   LED_PIN     (GPIO8) -- onboard LED (active-low on most C3 boards)
 */
#include <Arduino.h>
#include <esp_random.h>
#include <string.h>

#include "onewire_handler.h"
#include "stratasys_codec.h"
#include "des.h"

// ---- Configuration --------------------------------------------------------
static const uint8_t ONEWIRE_PIN = 4;
static const uint8_t BUTTON_PIN  = 9;   // BOOT button; pressed = LOW
static const uint8_t LED_PIN     = 8;
static const bool    LED_ACTIVE_LOW = true;

// Prodigy / P-class machine key (machine.py "prodigy" = 5394D7657CED641D)
static const uint8_t MACHINE[8] = {0x53,0x94,0xD7,0x65,0x7C,0xED,0x64,0x1D};

// ---- Crypto power-on self-test vectors (small known-answer tests) ----------
static const uint8_t KAT_DES_KEY[8]  = {0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef};
static const uint8_t KAT_DES_PT[8]   = {0x4e,0x6f,0x77,0x20,0x69,0x73,0x20,0x74};
static const uint8_t KAT_DES_CT[8]   = {0x3f,0xa4,0x0e,0x8a,0x98,0x4d,0x48,0x15};
static const uint8_t KAT_DESX_KEY[16]= {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
static const uint8_t KAT_DESX_PT[16] = {0x74,0x68,0x69,0x73,0x20,0x69,0x73,0x20,
                                        0x61,0x20,0x74,0x65,0x73,0x74,0x2e,0x2e};
static const uint8_t KAT_DESX_CT[16] = {0x38,0xdb,0x9b,0xe0,0x9d,0x1b,0x24,0xa0,
                                        0x7c,0x77,0x49,0x26,0xaf,0x94,0xe8,0xd5};

OneWireHandler ow(ONEWIRE_PIN);
static bool cryptoOk = false;

// ---- LED helpers ----------------------------------------------------------
static void led(bool on) {
    digitalWrite(LED_PIN, (on ^ LED_ACTIVE_LOW) ? HIGH : LOW);
}
static void blink(int times, int on_ms, int off_ms) {
    for (int i = 0; i < times; i++) { led(true); delay(on_ms); led(false); delay(off_ms); }
}

// ---- Crypto self-test (never write if this fails) -------------------------
static bool selfTest() {
    uint64_t sk[16];
    uint8_t out[16];
    des_key_schedule(KAT_DES_KEY, sk);
    des_process_block(sk, KAT_DES_PT, out, false);
    if (memcmp(out, KAT_DES_CT, 8) != 0) return false;

    stratasys_desx_encrypt(KAT_DESX_KEY, KAT_DESX_PT, out, 16);
    if (memcmp(out, KAT_DESX_CT, 16) != 0) return false;

    stratasys_desx_decrypt(KAT_DESX_KEY, KAT_DESX_CT, out, 16);
    if (memcmp(out, KAT_DESX_PT, 16) != 0) return false;

    if (stratasys_crc16((const uint8_t *)"abcd", 4) != 14743) return false;
    return true;
}

// ---- Button (active-low, debounced) ---------------------------------------
static bool buttonPressed() {
    if (digitalRead(BUTTON_PIN) != LOW) return false;
    delay(30);                                   // debounce
    if (digitalRead(BUTTON_PIN) != LOW) return false;
    // Wait for release, but don't hang forever on a stuck/shorted button
    // (GPIO9 is the BOOT strapping pin — a held-low line must not wedge loop()).
    unsigned long start = millis();
    while (digitalRead(BUTTON_PIN) == LOW) {
        if (millis() - start > 5000) {           // stuck button: give up
            Serial.println(F("WARN: button held >5s — ignoring (stuck?)"));
            return false;
        }
        delay(10);
    }
    return true;
}

// ---- Refill sequence ------------------------------------------------------
static void doRefill() {
    Serial.println(F("\n=== Refill requested ==="));
    led(true);   // solid = busy

    if (!ow.search()) {
        Serial.println(F("ERROR: no cartridge found on 1-Wire bus"));
        led(false); blink(3, 120, 120);
        return;
    }
    Serial.print(F("Cartridge ROM: ")); Serial.println(ow.getRomAddress());

    uint8_t eeprom[STRATASYS_EEPROM_LEN];
    if (!ow.read(0, eeprom, STRATASYS_EEPROM_LEN)) {
        Serial.println(F("ERROR: read failed"));
        led(false); blink(3, 120, 120);
        return;
    }

    // Show what's on the cartridge (and confirm it's a genuine prodigy cartridge)
    double serial = 0, initial = 0, current = 0;
    if (!stratasys_read_info(eeprom, MACHINE, ow.romBytes(), &serial, &initial, &current)) {
        Serial.println(F("ERROR: not a valid PRODIGY cartridge (wrong printer type?)"));
        led(false); blink(5, 80, 80);
        return;
    }
    Serial.printf("Before: serial=%.0f  current=%.2f / initial=%.2f cu.in\n",
                  serial, current, initial);

    // Refill in place with a fresh random serial number (hardware RNG)
    double newSerial = (double)((esp_random() % 999999u) + 1u);
    if (!stratasys_refill(eeprom, MACHINE, ow.romBytes(), newSerial)) {
        Serial.println(F("ERROR: validation failed during refill"));
        led(false); blink(5, 80, 80);
        return;
    }
    Serial.printf("Refilling: new serial=%.0f, current set to %.2f cu.in\n", newSerial, initial);

    // Write it back
    if (!ow.write(0, eeprom, STRATASYS_EEPROM_LEN)) {
        Serial.println(F("ERROR: write failed"));
        led(false); blink(3, 120, 120);
        return;
    }

    // Verify the read-back matches exactly
    uint8_t check[STRATASYS_EEPROM_LEN];
    if (!ow.read(0, check, STRATASYS_EEPROM_LEN) ||
        memcmp(check, eeprom, STRATASYS_EEPROM_LEN) != 0) {
        Serial.println(F("ERROR: write verification mismatch"));
        led(false); blink(3, 120, 120);
        return;
    }

    Serial.println(F("SUCCESS: cartridge refilled and verified."));
    led(false);
    blink(3, 300, 200);   // 3 slow blinks = success
}

void setup() {
    Serial.begin(115200);
    delay(300);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    led(false);

    Serial.println(F("\nStratasys standalone refill (ESP32-C3) — PRODIGY"));

    cryptoOk = selfTest();
    if (!cryptoOk) {
        Serial.println(F("FATAL: crypto self-test FAILED — refusing to operate."));
        // Fast continuous blink forever: do not let a miscompiled build write.
        for (;;) blink(1, 60, 60);
    }
    Serial.println(F("Crypto self-test passed. Insert cartridge (out of printer) and press the button."));
}

void loop() {
    if (buttonPressed()) {
        doRefill();
        Serial.println(F("Ready for next cartridge."));
    }
    delay(10);
}
