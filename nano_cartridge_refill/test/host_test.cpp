/*
 * Host validation of the Stratasys codec against vectors produced by the
 * Python encoder. Compile and run on a PC (no hardware needed):
 *
 *   g++ -std=c++11 -I lib/stratasys -I lib/backup -I test \
 *       lib/stratasys/des.cpp lib/stratasys/stratasys_codec.cpp lib/stratasys/f64.cpp \
 *       lib/backup/cartridge_backup.cpp test/host_test.cpp -o /tmp/host_test && /tmp/host_test
 *
 * This exercises the exact source the Nano runs: the PROGMEM macros in
 * pgm_compat.h reduce to plain array indexing off-AVR, so the table lookups,
 * the AVR-tuned permute()/S-box extraction, and the in-place codec are all the
 * same code paths here as on the chip.
 */
#include "stratasys_codec.h"
#include "des.h"
#include "f64.h"
#include "cartridge_backup.h"
#include "vectors.h"   // auto-generated from the Python encoder
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static const char *DES_KEY  = V_DES_KEY;
static const char *DES_PT   = V_DES_PT;
static const char *DES_CT   = V_DES_CT;
static const char *DESX_KEY = V_DESX_KEY;
static const char *DESX_PT  = V_DESX_PT;
static const char *DESX_CT  = V_DESX_CT;
static const char *PRODIGY  = V_PRODIGY;
static const char *UID      = V_UID;
static const char *PACKED   = V_PACKED;
static const char *ENCODED  = V_ENCODED;
static const char *REFILLED = V_REFILLED;

static std::vector<uint8_t> hx(const char *s) {
    std::vector<uint8_t> v;
    for (size_t i = 0; s[i] && s[i + 1]; i += 2)
        v.push_back((uint8_t)strtol(std::string(s + i, 2).c_str(), nullptr, 16));
    return v;
}

static int fails = 0;
static void check(bool ok, const char *name) {
    printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) fails++;
}
static bool eq(const uint8_t *a, const uint8_t *b, int n) { return memcmp(a, b, n) == 0; }

int main() {
    // 1. DES single block
    {
        auto key = hx(DES_KEY), pt = hx(DES_PT), ct = hx(DES_CT);
        uint64_t sk[16]; des_key_schedule(key.data(), sk);
        uint8_t out[8], back[8];
        des_process_block(sk, pt.data(), out, false);
        des_process_block(sk, ct.data(), back, true);
        check(eq(out, ct.data(), 8), "DES encrypt matches FIPS vector");
        check(eq(back, pt.data(), 8), "DES decrypt round-trips");
    }

    // 2. DESX (whitening) encrypt/decrypt
    {
        auto key = hx(DESX_KEY), pt = hx(DESX_PT), ct = hx(DESX_CT);
        uint8_t out[16], back[16];
        stratasys_desx_encrypt(key.data(), pt.data(), out, 16);
        stratasys_desx_decrypt(key.data(), ct.data(), back, 16);
        check(eq(out, ct.data(), 16), "DESX encrypt matches Python vector");
        check(eq(back, pt.data(), 16), "DESX decrypt matches Python vector");
    }

    // 2b. DESX in place -- the Nano codec relies on src and dst aliasing.
    {
        auto key = hx(DESX_KEY), pt = hx(DESX_PT), ct = hx(DESX_CT);
        uint8_t buf[16];
        memcpy(buf, pt.data(), 16);
        stratasys_desx_encrypt(key.data(), buf, buf, 16);
        check(eq(buf, ct.data(), 16), "DESX encrypt in place (src == dst)");
        stratasys_desx_decrypt(key.data(), buf, buf, 16);
        check(eq(buf, pt.data(), 16), "DESX decrypt in place (src == dst)");
    }

    // 3. CRC-16
    check(stratasys_crc16((const uint8_t *)"abcd", 4) == 14743, "CRC16 'abcd' == 14743");

    // 3b. Hand-rolled binary64 encoder -- the Nano cannot use `double` for this.
    {
        bool ok = true;
        static const uint32_t probes[] = {0, 1, 2, 3, 5, 42, 255, 256, 1000,
                                          424242, 999999, 16777215, 0x7FFFFFFFUL,
                                          0xFFFFFFFFUL};
        for (uint32_t v : probes) {
            uint8_t got[8];
            double want = (double)v;          // host double IS binary64
            f64_from_u32(got, v);
            if (memcmp(got, &want, 8) != 0) { ok = false; break; }
            if (f64_to_float(got) != (float)v && v <= 16777215UL) { ok = false; break; }
        }
        check(ok, "f64_from_u32 / f64_to_float match host binary64");
    }

    auto machine = hx(PRODIGY), uid = hx(UID);
    auto packed = hx(PACKED), encoded = hx(ENCODED), refilled = hx(REFILLED);

    // 4. encode(plaintext) reproduces the Python-encrypted EEPROM
    {
        uint8_t buf[STRATASYS_EEPROM_LEN];
        memcpy(buf, packed.data(), STRATASYS_EEPROM_LEN);
        stratasys_encode(buf, machine.data(), uid.data());
        check(eq(buf, encoded.data(), STRATASYS_EEPROM_LEN),
              "encode(PACKED) == Python ENCODED (full encrypt + checksums)");
    }

    // 5. decode recovers the plaintext content and validates
    {
        uint8_t buf[STRATASYS_EEPROM_LEN];
        memcpy(buf, encoded.data(), STRATASYS_EEPROM_LEN);
        bool ok = stratasys_decode(buf, machine.data(), uid.data());
        check(ok, "decode(ENCODED) validates as a genuine prodigy cartridge");
        check(eq(&buf[0x00], &packed[0x00], 0x40), "decoded content == PACKED[0x00:0x40]");
        check(eq(&buf[0x58], &packed[0x58], 0x08), "decoded qty == PACKED[0x58:0x60]");
    }

    // 6. full on-device operation: refill in place == Python refill
    {
        uint8_t img[STRATASYS_EEPROM_LEN];
        uint8_t serial[8];
        memcpy(img, encoded.data(), STRATASYS_EEPROM_LEN);
        f64_from_u32(serial, (uint32_t)V_REFILL_SERIAL);
        bool ok = stratasys_refill(img, machine.data(), uid.data(), serial);
        check(ok, "refill(ENCODED, serial=424242) succeeds");
        check(eq(img, refilled.data(), STRATASYS_EEPROM_LEN),
              "refilled image == Python REFILLED (BYTE-IDENTICAL)");
    }

    // 6b. the exact sequence main.cpp runs: decode in place, patch the fields,
    // encode in place -- must land on the same bytes as stratasys_refill().
    {
        uint8_t img[STRATASYS_EEPROM_LEN];
        memcpy(img, encoded.data(), STRATASYS_EEPROM_LEN);
        bool ok = stratasys_decode(img, machine.data(), uid.data());
        f64_from_u32(&img[STRATASYS_OFF_SERIAL], (uint32_t)V_REFILL_SERIAL);
        memcpy(&img[STRATASYS_OFF_CURRENT_QTY], &img[STRATASYS_OFF_INITIAL_QTY], 8);
        stratasys_encode(img, machine.data(), uid.data());
        check(ok && eq(img, refilled.data(), STRATASYS_EEPROM_LEN),
              "firmware's in-place decode/patch/encode == Python REFILLED");
    }

    // 7. validation rejects the wrong machine type
    {
        auto wrong = hx(V_FOX);   // fox, not prodigy
        check(stratasys_validate(encoded.data(), machine.data(), uid.data()) == true,
              "validate(ENCODED, prodigy) == true");
        check(stratasys_validate(encoded.data(), wrong.data(), uid.data()) == false,
              "validate(ENCODED, fox) == false (wrong machine rejected)");
        check(stratasys_validate(refilled.data(), machine.data(), uid.data()) == true,
              "validate(REFILLED, prodigy) == true");
    }

    // 8. a corrupted image must be rejected, not silently re-encoded
    {
        uint8_t img[STRATASYS_EEPROM_LEN];
        memcpy(img, encoded.data(), STRATASYS_EEPROM_LEN);
        img[0x20] ^= 0x01;
        check(stratasys_validate(img, machine.data(), uid.data()) == false,
              "validate rejects a single flipped bit in the content region");
    }

    // 9. recovery store: the backup that makes an interrupted write survivable.
    {
        backup_host_fill(0xFF);                 // blank/erased EEPROM
        check(backup_begin() == false, "backup_begin() formats a blank EEPROM");
        check(backup_count() == 0, "a freshly formatted store holds no backups");
        check(backup_begin() == true, "backup_begin() adopts an existing store");

        uint8_t rom[8]; memcpy(rom, uid.data(), 8);
        uint8_t out[STRATASYS_EEPROM_LEN];
        check(backup_load(rom, out) == false, "load with no backup stored fails");

        check(backup_save(rom, encoded.data()), "save a backup");
        check(backup_count() == 1, "store reports 1 backup");
        check(backup_load(rom, out) && eq(out, encoded.data(), STRATASYS_EEPROM_LEN),
              "load returns the exact bytes that were saved");

        // A different cartridge must not resolve to this backup.
        uint8_t other[8]; memcpy(other, rom, 8); other[3] ^= 0x11;
        check(backup_load(other, out) == false, "load for a different ROM fails");

        // Re-refilling a cartridge replaces its own slot rather than consuming
        // a new one -- the useful copy is always the most recent pre-write image.
        check(backup_save(rom, refilled.data()), "re-save for the same ROM");
        check(backup_count() == 1, "re-saving the same ROM reuses its slot");
        check(backup_load(rom, out) && eq(out, refilled.data(), STRATASYS_EEPROM_LEN),
              "load returns the newer image");

        // The boot counter must survive alongside the backups.
        uint32_t c1 = backup_bump_boot_counter();
        uint32_t c2 = backup_bump_boot_counter();
        check(c2 == c1 + 1, "boot counter increments and persists");
        check(backup_load(rom, out), "backup still intact after counter writes");

        // Corruption must read as "no backup", never as a bogus recovery image.
        // The re-save above rotated the live copy to slot 1, so corrupt that.
        backup_host_eeprom[backup_host_slot_base(1) + 12 + 40] ^= 0x01;
        check(backup_load(rom, out) == false, "a corrupted slot is rejected, not returned");
        check(backup_count() == 0, "a corrupted slot counts as free");
    }

    // 10. eviction: filling every slot must not lose the newest backups.
    {
        backup_host_fill(0xFF);
        backup_begin();
        uint8_t img[STRATASYS_EEPROM_LEN], out[STRATASYS_EEPROM_LEN];
        memcpy(img, encoded.data(), STRATASYS_EEPROM_LEN);

        bool allSaved = true, allLoad = true;
        for (int i = 0; i < BACKUP_SLOTS; i++) {
            uint8_t rom[8]; memcpy(rom, uid.data(), 8); rom[1] = (uint8_t)i;
            img[0x10] = (uint8_t)i;                       // make each image distinct
            if (!backup_save(rom, img)) allSaved = false;
        }
        check(allSaved && backup_count() == BACKUP_SLOTS, "store fills to capacity");

        for (int i = 0; i < BACKUP_SLOTS; i++) {
            uint8_t rom[8]; memcpy(rom, uid.data(), 8); rom[1] = (uint8_t)i;
            img[0x10] = (uint8_t)i;
            if (!backup_load(rom, out) || !eq(out, img, STRATASYS_EEPROM_LEN)) allLoad = false;
        }
        check(allLoad, "every slot round-trips its own image");

        // One more cartridge evicts the oldest (slot 0), leaving the rest.
        uint8_t newRom[8]; memcpy(newRom, uid.data(), 8); newRom[1] = 0xAA;
        img[0x10] = 0xAA;
        check(backup_save(newRom, img), "saving past capacity succeeds");
        check(backup_count() == BACKUP_SLOTS, "capacity is unchanged after eviction");
        check(backup_load(newRom, out) && eq(out, img, STRATASYS_EEPROM_LEN),
              "the newest backup is retrievable");

        uint8_t oldest[8]; memcpy(oldest, uid.data(), 8); oldest[1] = 0;
        check(backup_load(oldest, out) == false, "the least recently saved backup was evicted");

        uint8_t survivor[8]; memcpy(survivor, uid.data(), 8); survivor[1] = BACKUP_SLOTS - 1;
        img[0x10] = (uint8_t)(BACKUP_SLOTS - 1);
        check(backup_load(survivor, out) && eq(out, img, STRATASYS_EEPROM_LEN),
              "more recent backups survive the eviction");
    }

    // 11. the restore path writes stored bytes back verbatim -- including an
    //     image the refill path would refuse, which is the point of restoring.
    {
        backup_host_fill(0xFF);
        backup_begin();
        uint8_t rom[8]; memcpy(rom, uid.data(), 8);
        uint8_t broken[STRATASYS_EEPROM_LEN], out[STRATASYS_EEPROM_LEN];
        memcpy(broken, encoded.data(), STRATASYS_EEPROM_LEN);
        backup_save(rom, encoded.data());

        broken[0x20] ^= 0xFF;   // stand-in for a half-written cartridge
        check(stratasys_validate(broken, machine.data(), uid.data()) == false,
              "a half-written image fails validation (so refill would refuse it)");
        check(backup_load(rom, out) && eq(out, encoded.data(), STRATASYS_EEPROM_LEN),
              "restore recovers the original image byte-for-byte");
        check(stratasys_validate(out, machine.data(), uid.data()) == true,
              "the restored image validates again");
    }

    // 12. the failure modes an adversarial review turned up in the backup store.
    {
        uint8_t rom[8]; memcpy(rom, uid.data(), 8);
        uint8_t out[STRATASYS_EEPROM_LEN];

        // (a) A stuck EEPROM cell must not leave a slot that validates but holds
        //     neither image -- and must not destroy the good copy it replaces.
        backup_host_fill(0xFF); backup_begin();
        bool saved = backup_save(rom, encoded.data());
        check(saved, "baseline save before fault injection");

        // The save rotates into the first free slot, so put the bad cell there.
        backup_host_stuck_addr = (uint16_t)(backup_host_slot_base(1) + 12 + 40);
        bool second = backup_save(rom, refilled.data());
        backup_host_stuck_addr = BACKUP_HOST_NO_STUCK;
        check(second == false, "save onto a stuck cell reports failure");
        check(backup_load(rom, out) && eq(out, encoded.data(), STRATASYS_EEPROM_LEN),
              "a failed save leaves the PREVIOUS good backup intact");
        check(backup_count() == 1, "the half-written slot was invalidated, not left valid");

        // (b) A slot whose seq reads 0xFFFFFFFF (blank/stuck-at-1 cells) must not
        //     poison maxSeq and lock the store out for every future save.
        backup_host_fill(0xFF); backup_begin();
        backup_save(rom, encoded.data());
        for (int i = 0; i < 4; i++) backup_host_eeprom[0x010 + i] = 0xFF;
        uint16_t c = 0;
        for (int o = 0; o < 125; o++) c = stratasys_crc16_update(c, backup_host_eeprom[0x010 + o]);
        backup_host_eeprom[0x010 + 125] = (uint8_t)(c & 0xFF);
        backup_host_eeprom[0x010 + 126] = (uint8_t)(c >> 8);
        uint8_t rom2[8]; memcpy(rom2, rom, 8); rom2[0] = 0x99;
        check(backup_save(rom2, refilled.data()), "a 0xFFFFFFFF seq slot does not lock out saves");
        check(backup_load(rom2, out) && eq(out, refilled.data(), STRATASYS_EEPROM_LEN),
              "saves still round-trip afterwards");

        // (c) An all-zero EEPROM must present as empty, not as 7 valid backups
        //     (CRC-16/ARC over 125 zero bytes is itself 0).
        backup_host_fill(0x00);
        check(backup_begin() == false, "an all-zero EEPROM is not adopted as a store");
        check(backup_count() == 0, "an all-zero EEPROM yields no valid slots");

        // (d) Power loss anywhere inside a save must not cost the cartridge its
        //     recovery copy. Cut power after every possible number of writes.
        backup_host_fill(0xFF); backup_begin();
        backup_save(rom, encoded.data());
        uint8_t snapshot[BACKUP_EEPROM_SIZE];
        memcpy(snapshot, backup_host_eeprom, BACKUP_EEPROM_SIZE);

        bool survivedAll = true;
        int worstCut = -1;
        for (int cut = 0; cut < 200 && survivedAll; cut++) {
            memcpy(backup_host_eeprom, snapshot, BACKUP_EEPROM_SIZE);
            backup_host_write_budget = cut;            // power dies after `cut` writes
            backup_save(rom, refilled.data());
            backup_host_write_budget = -1;
            // Whatever happened, some intact image for this cartridge must remain.
            if (!backup_load(rom, out) ||
                (!eq(out, encoded.data(), STRATASYS_EEPROM_LEN) &&
                 !eq(out, refilled.data(), STRATASYS_EEPROM_LEN))) {
                survivedAll = false; worstCut = cut;
            }
        }
        if (!survivedAll) printf("      (lost the backup when power died after %d writes)\n", worstCut);
        check(survivedAll, "power loss at ANY point in a save leaves an intact backup");

        // (e) Formatting writes the magic last, so an interrupted format is not
        //     adopted with uncleared slots.
        backup_host_fill(0xFF); backup_begin();
        backup_save(rom, encoded.data());
        backup_host_eeprom[0x000] = 'X';               // magic damaged
        check(backup_begin() == false, "a damaged magic reformats rather than adopts");
        check(backup_count() == 0, "reformat clears every slot");
    }

    printf("\n%s (%d failure%s)\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED",
           fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
