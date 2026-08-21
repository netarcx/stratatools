/*
 * IEEE-754 binary64 helpers -- see f64.h.
 */
#include "f64.h"

#include <math.h>
#include <string.h>

void f64_from_u32(uint8_t out[8], uint32_t v) {
    memset(out, 0, 8);
    if (v == 0) return;                       // +0.0 is all-zero bits

    // Normalise: find the highest set bit, which becomes the implicit leading 1.
    int8_t e = 31;
    while (((v >> e) & 1UL) == 0UL) e--;

    // Left-align so the implicit 1 sits at bit 31, then drop it. The remaining
    // 31 bits are the top of the 52-bit fraction, so shift them up by 21.
    uint32_t aligned = v << (31 - e);
    uint64_t frac = ((uint64_t)(aligned & 0x7FFFFFFFUL)) << 21;
    uint64_t bits = ((uint64_t)(uint16_t)(1023 + e) << 52) | frac;

    for (uint8_t i = 0; i < 8; i++) out[i] = (uint8_t)(bits >> (8 * i));
}

float f64_to_float(const uint8_t in[8]) {
    uint32_t hi = ((uint32_t)in[7] << 24) | ((uint32_t)in[6] << 16) |
                  ((uint32_t)in[5] << 8)  |  (uint32_t)in[4];
    uint32_t lo = ((uint32_t)in[3] << 24) | ((uint32_t)in[2] << 16) |
                  ((uint32_t)in[1] << 8)  |  (uint32_t)in[0];

    uint8_t  sign = (uint8_t)((hi >> 31) & 1UL);
    int16_t  exp  = (int16_t)((hi >> 20) & 0x7FFUL);
    uint32_t mant = hi & 0x000FFFFFUL;        // top 20 fraction bits

    if (exp == 0 || exp == 0x7FF) return 0.0f;   // zero/sub-normal, or NaN/Inf

    // Keep 23 fraction bits: the 20 from `hi` plus the top 3 from `lo`.
    uint32_t frac23 = (mant << 3) | (lo >> 29);
    float m = 1.0f + (float)frac23 / 8388608.0f;   // 2^23
    return ldexp(sign ? -m : m, exp - 1023);
}
