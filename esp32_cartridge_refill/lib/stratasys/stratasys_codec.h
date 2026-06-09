/*
 * Stratasys cartridge codec — portable C++ port of stratatools' Python
 * encoder (manager.py / crypto.py / checksum.py).
 *
 * Lets an embedded device decode, refill, and re-encode a cartridge EEPROM
 * image entirely on its own. No platform dependencies; validated byte-for-byte
 * against the Python encoder (see test/host_test.cpp).
 */
#ifndef STRATASYS_CODEC_H
#define STRATASYS_CODEC_H

#include <stdint.h>

#define STRATASYS_EEPROM_LEN 113   // 0x71 — the cartridge structure size

// CRC-16/ARC over `len` bytes (init 0), matching checksum.py.
uint16_t stratasys_crc16(const uint8_t *data, int len);

// Derive the 16-byte DESX key from the cartridge key region (8 bytes at 0x48),
// the machine number (8 bytes) and the cartridge UID / 1-Wire ROM (8 bytes).
void stratasys_build_key(const uint8_t cart_key[8], const uint8_t machine[8],
                         const uint8_t uid[8], uint8_t key[16]);

// DESX encrypt/decrypt of `len` bytes (len must be a multiple of 8).
void stratasys_desx_encrypt(const uint8_t key[16], const uint8_t *pt, uint8_t *ct, int len);
void stratasys_desx_decrypt(const uint8_t key[16], const uint8_t *ct, uint8_t *pt, int len);

// Decrypt + validate. Fills plain_out (113 bytes) with the decrypted image and
// returns true only if every checksum the printer validates is correct for the
// given machine/uid (i.e. this is a genuine cartridge for that printer).
bool stratasys_decode(const uint8_t eeprom[STRATASYS_EEPROM_LEN],
                      const uint8_t machine[8], const uint8_t uid[8],
                      uint8_t plain_out[STRATASYS_EEPROM_LEN]);

// Re-encode a plaintext image: recompute all checksums, encrypt the content and
// current-quantity regions, in place into eeprom_out (113 bytes).
void stratasys_encode(const uint8_t plain[STRATASYS_EEPROM_LEN],
                      const uint8_t machine[8], const uint8_t uid[8],
                      uint8_t eeprom_out[STRATASYS_EEPROM_LEN]);

// Validate without keeping the plaintext.
bool stratasys_validate(const uint8_t eeprom[STRATASYS_EEPROM_LEN],
                        const uint8_t machine[8], const uint8_t uid[8]);

// Refill in place: decode+validate, set current quantity = initial quantity and
// serial number = new_serial, then re-encode. Returns false (leaving eeprom
// unchanged) if the cartridge does not validate for this machine/uid.
bool stratasys_refill(uint8_t eeprom[STRATASYS_EEPROM_LEN],
                      const uint8_t machine[8], const uint8_t uid[8],
                      double new_serial);

// Read the decoded serial number / quantities (for display). Returns false if
// the cartridge does not validate.
bool stratasys_read_info(const uint8_t eeprom[STRATASYS_EEPROM_LEN],
                         const uint8_t machine[8], const uint8_t uid[8],
                         double *serial, double *initial_qty, double *current_qty);

#endif
