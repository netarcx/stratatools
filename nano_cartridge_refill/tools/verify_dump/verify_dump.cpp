// Verify a saved cartridge image really is restorable: decode it with the same
// codec the firmware uses, and confirm it validates and yields the expected
// contents. A backup nobody has decoded is not a backup.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "stratasys_codec.h"
#include "f64.h"

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: verify_dump <image.bin> <rom-hex-16>\n"); return 2; }

    uint8_t buf[STRATASYS_EEPROM_LEN];
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 2; }
    size_t n = fread(buf, 1, sizeof buf, f);
    long extra = 0;
    { int c; while ((c = fgetc(f)) != EOF) extra++; }
    fclose(f);
    if (n != STRATASYS_EEPROM_LEN || extra) {
        fprintf(stderr, "FAIL: expected exactly %d bytes, got %zu(+%ld)\n",
                STRATASYS_EEPROM_LEN, n, extra);
        return 1;
    }

    uint8_t rom[8];
    for (int i = 0; i < 8; i++) {
        char t[3] = { argv[2][i*2], argv[2][i*2+1], 0 };
        rom[i] = (uint8_t)strtoul(t, nullptr, 16);
    }

    // Prodigy / P-class, same key the firmware compiles in.
    const uint8_t MACHINE[8] = {0x53,0x94,0xD7,0x65,0x7C,0xED,0x64,0x1D};

    printf("image    : %s (%d bytes)\n", argv[1], STRATASYS_EEPROM_LEN);
    printf("rom      : ");
    for (int i = 0; i < 8; i++) printf("%02x", rom[i]);
    printf("\n");

    bool valid = stratasys_validate(buf, MACHINE, rom);
    printf("validate : %s\n", valid ? "PASS" : "FAIL");

    if (!stratasys_decode(buf, MACHINE, rom)) {
        printf("decode   : FAIL -- this image is NOT restorable\n");
        return 1;
    }
    printf("decode   : PASS\n\n");
    printf("serial   : %.0f\n", (double)f64_to_float(&buf[STRATASYS_OFF_SERIAL]));
    printf("initial  : %.2f cu.in\n", (double)f64_to_float(&buf[STRATASYS_OFF_INITIAL_QTY]));
    printf("current  : %.2f cu.in\n", (double)f64_to_float(&buf[STRATASYS_OFF_CURRENT_QTY]));
    return valid ? 0 : 1;
}
