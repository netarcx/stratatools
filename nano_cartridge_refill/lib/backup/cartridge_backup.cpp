/*
 * Cartridge recovery store -- see cartridge_backup.h.
 */
#include "cartridge_backup.h"
#include "stratasys_codec.h"    // stratasys_crc16_update
#include <string.h>

// ---- Layout ---------------------------------------------------------------
static const uint16_t OFF_MAGIC        = 0x000;
static const uint16_t OFF_BOOT_COUNTER = 0x004;
static const uint16_t OFF_RESERVED     = 0x008;
static const uint16_t OFF_SLOTS        = 0x010;

static const uint8_t  SLOT_SEQ   = 0;
static const uint8_t  SLOT_ROM   = 4;
static const uint8_t  SLOT_IMAGE = 12;
static const uint8_t  SLOT_CRC   = 125;
static const uint8_t  SLOT_SIZE  = 127;

// Bump this whenever BACKUP_SLOTS, SLOT_SIZE or the slot layout changes -- it is
// the only thing stopping a new build from reinterpreting an old store.
static const char MAGIC[4] = {'S', 'R', 'B', '2'};

// ---- Backing store --------------------------------------------------------
#ifdef __AVR__
  // avr-libc directly rather than the Arduino EEPROM wrapper: identical
  // semantics (eeprom_update_byte skips cells that already hold the value, which
  // is what EEPROM.update does), but it is part of the toolchain, so it needs no
  // library resolution -- the LDF does not reliably find framework-bundled
  // libraries when the include comes from a project library like this one.
  #include <avr/eeprom.h>
  static uint8_t eeRead(uint16_t a)             { return eeprom_read_byte((const uint8_t *)a); }
  static void    eeWrite(uint16_t a, uint8_t v) { eeprom_update_byte((uint8_t *)a, v); }
  // The store must fit the part's EEPROM. E2END is 1023 on the ATmega328P.
  static_assert(OFF_SLOTS + (uint32_t)BACKUP_SLOTS * SLOT_SIZE <= E2END + 1,
                "backup store does not fit in this chip's EEPROM");
#else
  uint8_t  backup_host_eeprom[BACKUP_EEPROM_SIZE];
  uint16_t backup_host_stuck_addr = BACKUP_HOST_NO_STUCK;
  long     backup_host_write_budget = -1;
  uint16_t backup_host_slot_base(uint8_t i) { return (uint16_t)(OFF_SLOTS + (uint16_t)i * SLOT_SIZE); }
  void backup_host_fill(uint8_t value) { memset(backup_host_eeprom, value, BACKUP_EEPROM_SIZE); }
  static uint8_t eeRead(uint16_t a) { return backup_host_eeprom[a]; }
  static void    eeWrite(uint16_t a, uint8_t v) {
      if (a == backup_host_stuck_addr) return;        // a cell that will not program
      if (backup_host_write_budget == 0) return;      // power died: no more writes land
      if (backup_host_write_budget > 0) backup_host_write_budget--;
      backup_host_eeprom[a] = v;
  }
#endif

static uint16_t slotBase(uint8_t i) { return (uint16_t)(OFF_SLOTS + (uint16_t)i * SLOT_SIZE); }

static uint32_t eeRead32(uint16_t a) {
    uint32_t v = 0;
    for (uint8_t i = 0; i < 4; i++) v |= (uint32_t)eeRead((uint16_t)(a + i)) << (8 * i);
    return v;
}
static void eeWrite32(uint16_t a, uint32_t v) {
    for (uint8_t i = 0; i < 4; i++) eeWrite((uint16_t)(a + i), (uint8_t)(v >> (8 * i)));
}

// CRC-16/ARC over the bytes a slot is *supposed* to hold. Checksumming the
// intended data rather than the read-back is what makes the stored CRC a real
// integrity check: a cell that fails to program then makes the slot read as
// invalid, instead of leaving it self-consistently wrong.
static uint16_t intendedCrc(uint32_t seq, const uint8_t rom[8], const uint8_t *image) {
    uint16_t crc = 0;
    for (uint8_t i = 0; i < 4; i++)  crc = stratasys_crc16_update(crc, (uint8_t)(seq >> (8 * i)));
    for (uint8_t i = 0; i < 8; i++)  crc = stratasys_crc16_update(crc, rom[i]);
    for (uint8_t i = 0; i < BACKUP_IMAGE_LEN; i++) crc = stratasys_crc16_update(crc, image[i]);
    return crc;
}

static void slotInvalidate(uint8_t i) { eeWrite32((uint16_t)(slotBase(i) + SLOT_SEQ), 0); }

// CRC-16/ARC over a slot's bytes 0..SLOT_CRC-1, read straight out of EEPROM.
static uint16_t slotCrc(uint8_t i) {
    uint16_t base = slotBase(i);
    uint16_t crc = 0;
    for (uint8_t o = 0; o < SLOT_CRC; o++) crc = stratasys_crc16_update(crc, eeRead((uint16_t)(base + o)));
    return crc;
}

// A slot counts as valid only if it has been written (seq != 0) and its stored
// CRC still matches -- so a half-written slot, or a blank 0xFF EEPROM, reads as
// free rather than as a bogus recovery image.
static bool slotValid(uint8_t i) {
    uint16_t base = slotBase(i);
    // 0 means "free". 0xFFFFFFFF is what a blank or stuck-at-1 cell reads as, and
    // admitting it would make maxSeq+1 wrap to 0 in backup_save(), which would
    // mark every future target slot free and lock the store out permanently.
    uint32_t seq = eeRead32((uint16_t)(base + SLOT_SEQ));
    if (seq == 0UL || seq == 0xFFFFFFFFUL) return false;
    uint16_t stored = (uint16_t)(eeRead((uint16_t)(base + SLOT_CRC)) |
                                 ((uint16_t)eeRead((uint16_t)(base + SLOT_CRC + 1)) << 8));
    return stored == slotCrc(i);
}

static bool slotRomMatches(uint8_t i, const uint8_t rom[8]) {
    uint16_t base = slotBase(i) + SLOT_ROM;
    for (uint8_t o = 0; o < 8; o++) {
        if (eeRead((uint16_t)(base + o)) != rom[o]) return false;
    }
    return true;
}

// ---- Public API -----------------------------------------------------------
static void format(void) {
    eeWrite32(OFF_BOOT_COUNTER, 0);
    for (uint8_t i = 0; i < 8; i++) eeWrite((uint16_t)(OFF_RESERVED + i), 0);
    // Clearing each slot's seq is enough to mark it free; the 113-byte payloads
    // are left alone so formatting costs 28 EEPROM writes rather than ~900.
    for (uint8_t i = 0; i < BACKUP_SLOTS; i++) slotInvalidate(i);
    // Magic goes down LAST: an interrupted format then still fails the magic
    // check next boot and is simply formatted again, rather than being adopted
    // with slots that were never cleared.
    for (uint8_t i = 0; i < 4; i++) eeWrite((uint16_t)(OFF_MAGIC + i), (uint8_t)MAGIC[i]);
}

bool backup_begin(void) {
    for (uint8_t i = 0; i < 4; i++) {
        if (eeRead((uint16_t)(OFF_MAGIC + i)) != (uint8_t)MAGIC[i]) {
            format();
            return false;
        }
    }
    return true;
}

uint32_t backup_bump_boot_counter(void) {
    uint32_t c = eeRead32(OFF_BOOT_COUNTER) + 1UL;
    eeWrite32(OFF_BOOT_COUNTER, c);
    return c;
}

uint8_t backup_count(void) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < BACKUP_SLOTS; i++) if (slotValid(i)) n++;
    return n;
}

bool backup_save(const uint8_t rom[8], const uint8_t image[BACKUP_IMAGE_LEN]) {
    // Survey the store: this cartridge's current slot, a free slot, the least
    // recently written slot, and the highest sequence number in use.
    uint8_t  match = 0xFF, freeSlot = 0xFF, oldest = 0xFF;
    uint32_t oldestSeq = 0, maxSeq = 0;

    for (uint8_t i = 0; i < BACKUP_SLOTS; i++) {
        if (!slotValid(i)) {
            if (freeSlot == 0xFF) freeSlot = i;
            continue;
        }
        uint32_t seq = eeRead32((uint16_t)(slotBase(i) + SLOT_SEQ));
        if (seq > maxSeq) maxSeq = seq;
        if (match == 0xFF && slotRomMatches(i, rom)) { match = i; continue; }
        if (oldest == 0xFF || seq < oldestSeq) { oldestSeq = seq; oldest = i; }
    }

    // Write the new copy somewhere OTHER than this cartridge's existing slot, so
    // the recovery image it already has stays intact until the new one is proven
    // good. Overwriting in place would mean a power cut, or a single bad cell,
    // destroys the only copy at the exact moment it is about to be needed.
    uint8_t target = (freeSlot != 0xFF) ? freeSlot
                   : (oldest   != 0xFF) ? oldest
                   : match;               // only when every slot belongs to this ROM
    if (target == 0xFF) return false;

    // slotValid() rejects 0xFFFFFFFF, so maxSeq < 0xFFFFFFFF and this cannot
    // wrap to 0 (which would silently mark the slot free).
    uint32_t seq = maxSeq + 1UL;
    uint16_t base = slotBase(target);

    eeWrite32((uint16_t)(base + SLOT_SEQ), seq);
    for (uint8_t o = 0; o < 8; o++) eeWrite((uint16_t)(base + SLOT_ROM + o), rom[o]);
    for (uint8_t o = 0; o < BACKUP_IMAGE_LEN; o++) eeWrite((uint16_t)(base + SLOT_IMAGE + o), image[o]);

    uint16_t crc = intendedCrc(seq, rom, image);
    eeWrite((uint16_t)(base + SLOT_CRC), (uint8_t)(crc & 0xFF));
    eeWrite((uint16_t)(base + SLOT_CRC + 1), (uint8_t)(crc >> 8));

    // Read the slot back the way a restore would. On any mismatch, invalidate
    // what we just wrote: a half-written slot must never linger looking valid,
    // and the cartridge's previous backup (still in `match`) stays untouched.
    bool ok = slotValid(target) && slotRomMatches(target, rom);
    for (uint8_t o = 0; ok && o < BACKUP_IMAGE_LEN; o++) {
        if (eeRead((uint16_t)(base + SLOT_IMAGE + o)) != image[o]) ok = false;
    }
    if (!ok) {
        slotInvalidate(target);
        return false;
    }

    // The new copy is committed and verified, so retire the old one.
    if (match != 0xFF && match != target) slotInvalidate(match);
    return true;
}

bool backup_load(const uint8_t rom[8], uint8_t image_out[BACKUP_IMAGE_LEN]) {
    uint8_t  best = 0xFF;
    uint32_t bestSeq = 0;
    for (uint8_t i = 0; i < BACKUP_SLOTS; i++) {
        if (!slotValid(i) || !slotRomMatches(i, rom)) continue;
        uint32_t seq = eeRead32((uint16_t)(slotBase(i) + SLOT_SEQ));
        if (best == 0xFF || seq > bestSeq) { best = i; bestSeq = seq; }
    }
    if (best == 0xFF) return false;

    uint16_t base = slotBase(best) + SLOT_IMAGE;
    for (uint8_t o = 0; o < BACKUP_IMAGE_LEN; o++) image_out[o] = eeRead((uint16_t)(base + o));
    return true;
}
