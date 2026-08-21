/*
 * Portable single-block DES (FIPS 46-3). See des.h.
 *
 * Ported from the ESP32 version for 8-bit AVR:
 *   - tables are uint8_t (not int) and live in PROGMEM  -> ~1 KB of SRAM saved
 *   - permute() extracts source bits by byte index instead of with a variable
 *     64-bit shift. The ATmega328P has no barrel shifter, so `x >> n` for a
 *     runtime n compiles to a loop of up to 64 single-bit shifts of an 8-byte
 *     value; doing that ~1400 times per block would take seconds.
 */
#include "des.h"
#include "pgm_compat.h"

// NOTE: every table below carries a DES_ prefix on purpose. <avr/io.h> defines
// PC0..PC7 (port C bit numbers) as plain integer macros, so a table literally
// named PC1 or PC2 expands to a numeric constant and fails to compile for AVR
// while building fine on a host. Keep the prefix.

// Initial permutation
static const uint8_t DES_IP[64] PROGMEM = {
    58,50,42,34,26,18,10,2, 60,52,44,36,28,20,12,4,
    62,54,46,38,30,22,14,6, 64,56,48,40,32,24,16,8,
    57,49,41,33,25,17,9,1,  59,51,43,35,27,19,11,3,
    61,53,45,37,29,21,13,5, 63,55,47,39,31,23,15,7
};

// Final permutation (inverse of DES_IP)
static const uint8_t DES_FP[64] PROGMEM = {
    40,8,48,16,56,24,64,32, 39,7,47,15,55,23,63,31,
    38,6,46,14,54,22,62,30, 37,5,45,13,53,21,61,29,
    36,4,44,12,52,20,60,28, 35,3,43,11,51,19,59,27,
    34,2,42,10,50,18,58,26, 33,1,41,9,49,17,57,25
};

// Expansion (32 -> 48)
static const uint8_t DES_E[48] PROGMEM = {
    32,1,2,3,4,5,    4,5,6,7,8,9,
    8,9,10,11,12,13, 12,13,14,15,16,17,
    16,17,18,19,20,21, 20,21,22,23,24,25,
    24,25,26,27,28,29, 28,29,30,31,32,1
};

// Permutation (32 -> 32)
static const uint8_t DES_P[32] PROGMEM = {
    16,7,20,21,29,12,28,17, 1,15,23,26,5,18,31,10,
    2,8,24,14,32,27,3,9,    19,13,30,6,22,11,4,25
};

// Permuted choice 1 (64 -> 56)
static const uint8_t DES_PC1[56] PROGMEM = {
    57,49,41,33,25,17,9,  1,58,50,42,34,26,18,
    10,2,59,51,43,35,27,  19,11,3,60,52,44,36,
    63,55,47,39,31,23,15, 7,62,54,46,38,30,22,
    14,6,61,53,45,37,29,  21,13,5,28,20,12,4
};

// Permuted choice 2 (56 -> 48)
static const uint8_t DES_PC2[48] PROGMEM = {
    14,17,11,24,1,5,   3,28,15,6,21,10,
    23,19,12,4,26,8,   16,7,27,20,13,2,
    41,52,31,37,47,55, 30,40,51,45,33,48,
    44,49,39,56,34,53, 46,42,50,36,29,32
};

static const uint8_t DES_SHIFTS[16] PROGMEM = {1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1};

// S-boxes, flattened as [box][row*16 + col]
static const uint8_t DES_SBOX[8][64] PROGMEM = {
    { 14,4,13,1,2,15,11,8,3,10,6,12,5,9,0,7,
      0,15,7,4,14,2,13,1,10,6,12,11,9,5,3,8,
      4,1,14,8,13,6,2,11,15,12,9,7,3,10,5,0,
      15,12,8,2,4,9,1,7,5,11,3,14,10,0,6,13 },
    { 15,1,8,14,6,11,3,4,9,7,2,13,12,0,5,10,
      3,13,4,7,15,2,8,14,12,0,1,10,6,9,11,5,
      0,14,7,11,10,4,13,1,5,8,12,6,9,3,2,15,
      13,8,10,1,3,15,4,2,11,6,7,12,0,5,14,9 },
    { 10,0,9,14,6,3,15,5,1,13,12,7,11,4,2,8,
      13,7,0,9,3,4,6,10,2,8,5,14,12,11,15,1,
      13,6,4,9,8,15,3,0,11,1,2,12,5,10,14,7,
      1,10,13,0,6,9,8,7,4,15,14,3,11,5,2,12 },
    { 7,13,14,3,0,6,9,10,1,2,8,5,11,12,4,15,
      13,8,11,5,6,15,0,3,4,7,2,12,1,10,14,9,
      10,6,9,0,12,11,7,13,15,1,3,14,5,2,8,4,
      3,15,0,6,10,1,13,8,9,4,5,11,12,7,2,14 },
    { 2,12,4,1,7,10,11,6,8,5,3,15,13,0,14,9,
      14,11,2,12,4,7,13,1,5,0,15,10,3,9,8,6,
      4,2,1,11,10,13,7,8,15,9,12,5,6,3,0,14,
      11,8,12,7,1,14,2,13,6,15,0,9,10,4,5,3 },
    { 12,1,10,15,9,2,6,8,0,13,3,4,14,7,5,11,
      10,15,4,2,7,12,9,5,6,1,13,14,0,11,3,8,
      9,14,15,5,2,8,12,3,7,0,4,10,1,13,11,6,
      4,3,2,12,9,5,15,10,11,14,1,7,6,0,8,13 },
    { 4,11,2,14,15,0,8,13,3,12,9,7,5,10,6,1,
      13,0,11,7,4,9,1,10,14,3,5,12,2,15,8,6,
      1,4,11,13,12,3,7,14,10,15,6,8,0,5,9,2,
      6,11,13,8,1,4,10,7,9,5,0,15,14,2,3,12 },
    { 13,2,8,4,6,15,11,1,10,9,3,14,5,0,12,7,
      1,15,13,8,10,3,7,4,12,5,6,11,0,14,9,2,
      7,11,4,1,9,12,14,2,0,6,10,13,15,3,5,8,
      2,1,14,7,4,10,8,13,15,12,9,0,3,5,6,11 }
};

// Permute the low `inWidth` bits of `in` according to a 1-indexed PROGMEM table
// (bit 1 = MSB of the inWidth-bit value), producing nOut bits.
//
// `in` is first spread into a big-endian byte array so each source bit can be
// picked out with one byte load and a shift of at most 7 -- see the file header
// for why a variable 64-bit shift would be far too slow on AVR.
static uint64_t permute(uint64_t in, const uint8_t *table, uint8_t nOut, uint8_t inWidth) {
    uint8_t b[8];
    for (int8_t i = 7; i >= 0; i--) { b[i] = (uint8_t)(in & 0xFF); in >>= 8; }

    uint64_t out = 0;
    for (uint8_t i = 0; i < nOut; i++) {
        uint8_t p = (uint8_t)(inWidth - TBL_U8(table, i));   // bit index from LSB
        out = (out << 1) | (uint64_t)((b[7 - (p >> 3)] >> (p & 7)) & 1);
    }
    return out;
}

static uint64_t bytesToU64(const uint8_t *b, uint8_t n) {
    uint64_t v = 0;
    for (uint8_t i = 0; i < n; i++) v = (v << 8) | b[i];
    return v;
}

static void u64ToBytes(uint64_t v, uint8_t *b, uint8_t n) {
    for (int8_t i = (int8_t)n - 1; i >= 0; i--) { b[i] = (uint8_t)(v & 0xFF); v >>= 8; }
}

void des_key_schedule(const uint8_t key[8], uint64_t subkeys[16]) {
    uint64_t k = bytesToU64(key, 8);
    uint64_t permuted = permute(k, DES_PC1, 56, 64);
    uint32_t C = (uint32_t)((permuted >> 28) & 0x0FFFFFFFUL);
    uint32_t D = (uint32_t)(permuted & 0x0FFFFFFFUL);
    for (uint8_t i = 0; i < 16; i++) {
        uint8_t s = TBL_U8(DES_SHIFTS, i);
        C = ((C << s) | (C >> (28 - s))) & 0x0FFFFFFFUL;
        D = ((D << s) | (D >> (28 - s))) & 0x0FFFFFFFUL;
        uint64_t CD = ((uint64_t)C << 28) | D;
        subkeys[i] = permute(CD, DES_PC2, 48, 56);
    }
}

static uint32_t feistel(uint32_t R, uint64_t subkey) {
    uint64_t x = permute(R, DES_E, 48, 32) ^ subkey;   // 48 bits
    uint8_t  xb[7];
    xb[6] = 0;                                     // pad, so the last group's
    for (int8_t i = 5; i >= 0; i--) {              // 16-bit window stays in range
        xb[i] = (uint8_t)(x & 0xFF);
        x >>= 8;
    }

    uint32_t out = 0;
    for (uint8_t i = 0; i < 8; i++) {
        // The i'th 6-bit group starts at bit 6*i (counting from the MSB) of the
        // 48-bit value. Read the 16-bit window containing it and shift it down.
        uint8_t  bit  = (uint8_t)(6 * i);
        uint16_t pair = (uint16_t)(((uint16_t)xb[bit >> 3] << 8) | xb[(bit >> 3) + 1]);
        uint8_t  six  = (uint8_t)((pair >> (10 - (bit & 7))) & 0x3F);

        uint8_t row = (uint8_t)(((six & 0x20) >> 4) | (six & 0x01));
        uint8_t col = (uint8_t)((six >> 1) & 0x0F);
        out = (out << 4) | TBL_U8(DES_SBOX[i], row * 16 + col);
    }
    return (uint32_t)permute(out, DES_P, 32, 32);
}

void des_process_block(const uint64_t subkeys[16], const uint8_t in[8],
                       uint8_t out[8], bool decrypt) {
    uint64_t block = permute(bytesToU64(in, 8), DES_IP, 64, 64);
    uint32_t L = (uint32_t)((block >> 32) & 0xFFFFFFFFUL);
    uint32_t R = (uint32_t)(block & 0xFFFFFFFFUL);
    for (uint8_t i = 0; i < 16; i++) {
        uint8_t idx = decrypt ? (uint8_t)(15 - i) : i;
        uint32_t newR = L ^ feistel(R, subkeys[idx]);
        L = R;
        R = newR;
    }
    // Final swap: preoutput is R then L
    uint64_t pre = ((uint64_t)R << 32) | L;
    u64ToBytes(permute(pre, DES_FP, 64, 64), out, 8);
}
