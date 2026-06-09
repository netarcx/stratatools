/*
 * Host validation of the Stratasys codec against vectors produced by the
 * Python encoder. Compile and run on a PC (no hardware needed):
 *
 *   g++ -I lib/stratasys lib/stratasys/des.cpp lib/stratasys/stratasys_codec.cpp \
 *       test/host_test.cpp -o /tmp/host_test && /tmp/host_test
 */
#include "stratasys_codec.h"
#include "des.h"
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

    // 3. CRC-16
    check(stratasys_crc16((const uint8_t *)"abcd", 4) == 14743, "CRC16 'abcd' == 14743");

    auto machine = hx(PRODIGY), uid = hx(UID);
    auto packed = hx(PACKED), encoded = hx(ENCODED), refilled = hx(REFILLED);

    // 4. encode(plaintext) reproduces the Python-encrypted EEPROM
    {
        uint8_t out[STRATASYS_EEPROM_LEN];
        stratasys_encode(packed.data(), machine.data(), uid.data(), out);
        check(eq(out, encoded.data(), STRATASYS_EEPROM_LEN),
              "encode(PACKED) == Python ENCODED (full encrypt + checksums)");
    }

    // 5. decode recovers the plaintext content and validates
    {
        uint8_t plain[STRATASYS_EEPROM_LEN];
        bool ok = stratasys_decode(encoded.data(), machine.data(), uid.data(), plain);
        check(ok, "decode(ENCODED) validates as a genuine prodigy cartridge");
        check(eq(&plain[0x00], &packed[0x00], 0x40), "decoded content == PACKED[0x00:0x40]");
        check(eq(&plain[0x58], &packed[0x58], 0x08), "decoded qty == PACKED[0x58:0x60]");
    }

    // 6. full on-device operation: refill in place == Python refill
    {
        uint8_t img[STRATASYS_EEPROM_LEN];
        memcpy(img, encoded.data(), STRATASYS_EEPROM_LEN);
        bool ok = stratasys_refill(img, machine.data(), uid.data(), 424242.0);
        check(ok, "refill(ENCODED, serial=424242) succeeds");
        check(eq(img, refilled.data(), STRATASYS_EEPROM_LEN),
              "refilled image == Python REFILLED (BYTE-IDENTICAL)");
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

    printf("\n%s (%d failure%s)\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED",
           fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
