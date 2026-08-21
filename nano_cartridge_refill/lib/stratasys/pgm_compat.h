/*
 * PROGMEM compatibility shim.
 *
 * The ATmega328P has only 2 KB of SRAM, and avr-gcc copies every `const` array
 * into SRAM at startup. The lookup tables in this codec total ~1.6 KB, which
 * alone would exhaust the Nano's RAM, so on AVR they live in flash and are read
 * through pgm_read_*. On a host build the macros are plain array indexing, so
 * the exact same source can be validated against the Python encoder.
 */
#ifndef STRATASYS_PGM_COMPAT_H
#define STRATASYS_PGM_COMPAT_H

#include <stdint.h>

#ifdef __AVR__
  #include <avr/pgmspace.h>
  #define TBL_U8(t, i)  ((uint8_t)pgm_read_byte(&(t)[i]))
  #define TBL_U16(t, i) ((uint16_t)pgm_read_word(&(t)[i]))
#else
  #ifndef PROGMEM
    #define PROGMEM
  #endif
  #define TBL_U8(t, i)  ((uint8_t)(t)[i])
  #define TBL_U16(t, i) ((uint16_t)(t)[i])
#endif

#endif
