/*
 * Stratasys cartridge codec -- portable C++ port of stratatools' Python
 * encoder (manager.py / crypto.py / checksum.py), tuned for 8-bit AVR.
 *
 * Differences from the ESP32 version, both forced by the Arduino Nano:
 *
 *  1. NO `double` IN THE API. avr-gcc's `double` is 32-bit, so passing the
 *     cartridge's 64-bit serial number or quantities through it would corrupt
 *     them. Those fields are handled as raw 8-byte little-endian binary64
 *     buffers instead; see f64.h to convert to/from numbers.
 *
 *  2. IN-PLACE OPERATION. Decode and encode transform one 113-byte buffer
 *     rather than filling a second one, which keeps the whole refill inside a
 *     single buffer -- 113 bytes instead of 339 out of the ATmega328P's 2 KB.
 *
 * Validated byte-for-byte against the Python encoder by test/host_test.cpp.
 */
#ifndef STRATASYS_CODEC_H
#define STRATASYS_CODEC_H

#include <stdint.h>

#define STRATASYS_EEPROM_LEN 113   // 0x71 -- the cartridge structure size

// Field offsets inside the decoded (plaintext) image.
#define STRATASYS_OFF_SERIAL       0x00   // binary64
#define STRATASYS_OFF_MATERIAL     0x08   // binary64
#define STRATASYS_OFF_INITIAL_QTY  0x38   // binary64
#define STRATASYS_OFF_CURRENT_QTY  0x58   // binary64

// CRC-16/ARC over `len` bytes (init 0), matching checksum.py.
uint16_t stratasys_crc16(const uint8_t *data, uint8_t len);

// Fold one byte into a running CRC-16/ARC (start from 0). Lets callers checksum
// a stream they never hold in RAM -- the backup store uses it over internal
// EEPROM contents.
uint16_t stratasys_crc16_update(uint16_t crc, uint8_t b);

// Derive the 16-byte DESX key from the cartridge key region (8 bytes at 0x48),
// the machine number (8 bytes) and the cartridge UID / 1-Wire ROM (8 bytes).
void stratasys_build_key(const uint8_t cart_key[8], const uint8_t machine[8],
                         const uint8_t uid[8], uint8_t key[16]);

// DESX encrypt/decrypt of `len` bytes (len must be a multiple of 8).
// `pt`/`ct` may alias, which is what makes the in-place codec possible.
void stratasys_desx_encrypt(const uint8_t key[16], const uint8_t *pt, uint8_t *ct, uint8_t len);
void stratasys_desx_decrypt(const uint8_t key[16], const uint8_t *ct, uint8_t *pt, uint8_t len);

// Decrypt + validate IN PLACE: `buf` goes from EEPROM image to plaintext image.
// Returns true only if every checksum the printer validates is correct for the
// given machine/uid (i.e. this is a genuine cartridge for that printer).
//
// On failure `buf` holds partially-decrypted garbage -- the caller must discard
// it and must NOT write it back to the cartridge.
bool stratasys_decode(uint8_t buf[STRATASYS_EEPROM_LEN],
                      const uint8_t machine[8], const uint8_t uid[8]);

// Re-encode IN PLACE: `buf` goes from plaintext image back to EEPROM image,
// recomputing every checksum along the way.
void stratasys_encode(uint8_t buf[STRATASYS_EEPROM_LEN],
                      const uint8_t machine[8], const uint8_t uid[8]);

// Validate an EEPROM image without disturbing it (works on a scratch copy).
bool stratasys_validate(const uint8_t eeprom[STRATASYS_EEPROM_LEN],
                        const uint8_t machine[8], const uint8_t uid[8]);

// Refill in place: decode+validate, set current quantity = initial quantity and
// the serial number to `new_serial` (8-byte little-endian binary64), then
// re-encode. Returns false if the cartridge does not validate for this
// machine/uid, in which case `buf` is garbage and must not be written.
bool stratasys_refill(uint8_t buf[STRATASYS_EEPROM_LEN],
                      const uint8_t machine[8], const uint8_t uid[8],
                      const uint8_t new_serial[8]);

#endif
