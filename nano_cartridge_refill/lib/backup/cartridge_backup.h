/*
 * Cartridge recovery store -- keeps the pre-write EEPROM image of each
 * cartridge in the ATmega's own 1 KB internal EEPROM.
 *
 * WHY: the 113-byte cartridge write goes out as four scratchpad->verify->copy
 * cycles. Losing power, resetting, or pulling the cartridge between cycles
 * leaves a half-old/half-new image whose checksums fail. The printer rejects
 * it, and the refill firmware also refuses to touch it (it validates before
 * writing), so without a copy of the original the only way back is the PC
 * tooling. This module is that copy, and it survives power loss.
 *
 * The store holds BACKUP_SLOTS cartridges, keyed by 1-Wire ROM. Re-refilling a
 * cartridge overwrites its own slot -- what you want for recovery is always the
 * image that was on the cartridge immediately before the interrupted write.
 * When every slot is taken, the least recently saved one is evicted.
 *
 * EEPROM map (byte offsets):
 *   0x000  magic[4]        "SRB1"
 *   0x004  boot counter    uint32 LE
 *   0x008  reserved[8]     zeroed at format
 *   0x010  slot[0..6]      127 bytes each -> last byte at 904 of 1023
 *
 * Slot layout (127 bytes):
 *   +0    seq    uint32 LE   -- 0 and 0xFFFFFFFF both mean empty; higher = newer
 *   +4    rom[8]             -- 1-Wire ROM of the cartridge this belongs to
 *   +12   image[113]         -- the raw EEPROM image, exactly as read
 *   +125  crc16  uint16 LE   -- CRC-16/ARC over bytes 0..124
 */
#ifndef CARTRIDGE_BACKUP_H
#define CARTRIDGE_BACKUP_H

#include <stdint.h>

#define BACKUP_IMAGE_LEN 113
#define BACKUP_SLOTS     7

// Validate the store, formatting it if the magic is absent or the layout
// changed. Returns true if an existing store was adopted, false if it was
// (re)formatted -- in which case no backups are available yet.
bool backup_begin(void);

// Increment and return the power-up counter. Kept here because this module owns
// the EEPROM map; main.cpp folds the value into its RNG seed.
uint32_t backup_bump_boot_counter(void);

// Save `image` as the recovery copy for cartridge `rom`, replacing any existing
// entry for the same cartridge. The new copy is written to a different slot and
// verified before the old one is retired, so this cartridge always has at least
// one intact recovery image -- even if power is lost mid-save. Returns false if
// the new slot does not read back intact (it is invalidated again in that case,
// and any previous copy survives); the caller must then not write the cartridge.
bool backup_save(const uint8_t rom[8], const uint8_t image[BACKUP_IMAGE_LEN]);

// Load the recovery copy for `rom`. Returns false if there isn't one.
bool backup_load(const uint8_t rom[8], uint8_t image_out[BACKUP_IMAGE_LEN]);

// How many slots currently hold a valid backup.
uint8_t backup_count(void);

#ifndef __AVR__
// Host builds keep the store in RAM so test/host_test.cpp can exercise save,
// load, eviction and corruption handling with no hardware.
#define BACKUP_EEPROM_SIZE 1024
extern uint8_t backup_host_eeprom[BACKUP_EEPROM_SIZE];
void backup_host_fill(uint8_t value);   // 0xFF simulates a blank/erased EEPROM

// Address of a simulated stuck cell that refuses to accept writes, for testing
// the failing-EEPROM paths. BACKUP_HOST_NO_STUCK disables it.
#define BACKUP_HOST_NO_STUCK 0xFFFF
extern uint16_t backup_host_stuck_addr;

// Number of further EEPROM byte-writes that will be honoured; once it reaches 0
// every subsequent write is dropped, modelling power loss part-way through an
// operation. -1 disables the limit.
extern long backup_host_write_budget;

// EEPROM offset of slot `i`, so tests can inject faults at a known location.
uint16_t backup_host_slot_base(uint8_t i);
#endif

#endif
