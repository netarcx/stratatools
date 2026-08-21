/*
 * Minimal, portable single-block DES (FIPS 46-3).
 *
 * AVR notes: every permutation/S-box table lives in flash (PROGMEM) so it does
 * not consume any of the ATmega328P's 2 KB of SRAM. The same source compiles on
 * a host for validation against the Python encoder.
 */
#ifndef STRATASYS_DES_H
#define STRATASYS_DES_H

#include <stdint.h>

// Expand an 8-byte key into 16 48-bit round subkeys (each stored in a uint64_t).
void des_key_schedule(const uint8_t key[8], uint64_t subkeys[16]);

// Encrypt (decrypt=false) or decrypt (decrypt=true) one 8-byte ECB block.
void des_process_block(const uint64_t subkeys[16], const uint8_t in[8],
                       uint8_t out[8], bool decrypt);

#endif
