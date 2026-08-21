/*
 * IEEE-754 binary64 ("double") helpers for 8-bit AVR.
 *
 * WHY THIS EXISTS: the cartridge stores the serial number and both material
 * quantities as little-endian 8-byte binary64 values. On avr-gcc `double` is
 * only 32 bits by default (it is an alias for `float`), so the ESP32 firmware's
 * `memcpy(&plain[0x00], &new_serial, 8)` would read four bytes past the end of
 * the variable and write garbage into the cartridge. This module therefore
 * encodes/decodes binary64 by hand, byte by byte, and the codec never uses the
 * `double` type at all.
 */
#ifndef STRATASYS_F64_H
#define STRATASYS_F64_H

#include <stdint.h>

// Store `v` as a little-endian binary64. Exact for every uint32_t (binary64 has
// a 53-bit significand, so no rounding can occur).
void f64_from_u32(uint8_t out[8], uint32_t v);

// Decode a little-endian binary64 to a 32-bit float, for display only. Values
// up to 2^24 (16777216) keep full integer precision, which covers every serial
// number and material quantity a cartridge carries. Sub-normals, NaN and Inf
// all decode to 0.0f -- they never appear in a valid cartridge.
float f64_to_float(const uint8_t in[8]);

#endif
