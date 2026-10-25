/* The program image layer: build identification, the disk -> build offset mapping, routine translation in the
 * scripts and the load-time self-check (program.c, docs/editions.md). Synthetic images and synthetic build tables
 * only - no bytes from the game files (AGENTS.md). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "program.h"

static int failures;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            failures++;                                                            \
        }                                                                          \
    } while (0)

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v), put16(p + 2, v >> 16); }

/* A build that inserted 0x40 bytes at disk 0x100, has no counterpart for disk [0x2000, 0x2010), moved the rest by
 * +0x50, placed one translated table (disk 0x2004) at 0x10, and has two routine entries on a change point. */
static const ProgramSegment SEGS[] = {{0, 0x100, 0}, {0x100, 0x2000, 0x140}, {0x2010, 0x30000, 0x2060}};
static const ProgramPoint PTS[] = {{0x2004, 0x10}};
static const ProgramRoutine RTS[] = {{0x1FF8, 0x1234}, {0x1FFC, ROUTINE_CD_GOLF}};
static const ProgramBuild SHIFTED = {"shifted", 0, 0, 0, "T", "D", 0, SEGS, 3, PTS, 1, RTS, 2, 0};
static const ProgramBuild DISK = {"disk", 0, 0, 0, "T", "D", 0, NULL, 0, NULL, 0, NULL, 0, 0};

static void test_crc(void) {
    CHECK(program_crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u); /* the CRC-32 check value */
    CHECK(program_crc32(NULL, 0) == 0);
}

static void test_build_at(void) {
    CHECK(build_at(NULL, 0x1234) == 0x1234);
    CHECK(build_at(&DISK, 0x1234) == 0x1234);
    CHECK(build_at(&SHIFTED, 0) == 0 && build_at(&SHIFTED, 0xFF) == 0xFF);
    CHECK(build_at(&SHIFTED, 0x100) == 0x140); /* a segment's first byte */
    CHECK(build_at(&SHIFTED, 0x1FFF) == 0x203F); /* its last byte */
    CHECK(build_at(&SHIFTED, 0x2000) == PROGRAM_NOWHERE); /* the end is exclusive: a "differ" row */
    CHECK(build_at(&SHIFTED, 0x200F) == PROGRAM_NOWHERE);
    CHECK(build_at(&SHIFTED, 0x2004) == 0x10); /* a placed table start, inside the "differ" row */
    CHECK(build_at(&SHIFTED, 0x2010) == 0x2060);
    CHECK(build_at(&SHIFTED, 0x30000) == PROGRAM_NOWHERE);
}

static void test_build_routine(void) {
    CHECK(build_routine(NULL, 0x1234) == 0x1234);
    CHECK(build_routine(&DISK, 0x1234) == 0x1234);
    CHECK(build_routine(&SHIFTED, 0x140) == 0x100);
    CHECK(build_routine(&SHIFTED, 0x203F) == 0x1FFF);
    CHECK(build_routine(&SHIFTED, 0x2040) == (ROUTINE_UNMAPPED | 0x2040)); /* one past the segment, before the next */
    CHECK(build_routine(&SHIFTED, 0x2060) == 0x2010);
    /* the routine table goes first: 0x1FF8 also lies inside the second segment's build range */
    CHECK(build_routine(&SHIFTED, 0x1FF8) == 0x1234);
    CHECK(build_routine(&SHIFTED, 0x1FFC) == ROUTINE_CD_GOLF);
    CHECK(build_routine(&SHIFTED, 0x120) == (ROUTINE_UNMAPPED | 0x120)); /* the inserted bytes: no disk routine */
}

static void test_reads(void) {
    static uint8_t img[0x3000];
    for (size_t i = 0; i < sizeof img; i++) img[i] = (uint8_t)(i * 7 + 3);
    Program p;
    memset(&p, 0, sizeof p);
    p.image.data = img, p.image.size = sizeof img;
    CHECK(program_u8(&p, 0x123, 0) == img[0x123]); /* no build: the disk layout */
    p.build = &SHIFTED;
    CHECK(program_u8(&p, 0x123, 0) == img[0x163]);
    CHECK(program_word(&p, 0x123, 2) == (img[0x165] | img[0x166] << 8));
    CHECK(program_dword(&p, 0x100, 0) == (uint32_t)(img[0x140] | img[0x141] << 8 | img[0x142] << 16 |
                                                  (uint32_t)img[0x143] << 24));
    /* the table start is mapped and the index added in the build: a table that runs over a change point stays
     * contiguous (0x1FFE + 0x20 summed first would map through the third segment instead) */
    CHECK(program_u8(&p, 0x1FFE, 0x20) == img[0x203E + 0x20]);
    CHECK(program_u8(&p, 0x2004, 3) == img[0x13]); /* the placed table */
    CHECK(program_u8(&p, 0x2000, 0) == 0); /* missing in this build */
    CHECK(program_table(&p, 0x2000)[0] == 0 && !program_contains(&p, program_table(&p, 0x2000), 1));
    CHECK(program_table(&p, 0x123) == img + 0x163 && program_contains(&p, program_table(&p, 0x123), 4));
    CHECK(program_raw_u8(&p, 0x123) == img[0x123]); /* offsets of the build itself are not mapped */
    CHECK(program_raw_u16(&p, sizeof img - 1) == 0 && program_raw_u32(&p, sizeof img - 3) == 0);
    size_t left;
    CHECK(program_raw_ptr(&p, 0x2FFE, &left) == img + 0x2FFE && left == 2);
    CHECK(program_raw_ptr(&p, 0x3000, &left) == NULL && left == 0);
    CHECK(program_u8(&p, 0x2010, 0x2000) == 0); /* past the image */
}

/* ---- a whole synthetic BIPRO.EXE ------------------------------------------------------------------------------- */

#define SCREEN_TABLE 0xE72F
#define RECORD_LENGTHS 0xB95A
#define CHARMAP 0xB541
#define BASE 0x30 /* file offset of program offset 0 */
#define SHIFT 0x100 /* the synthetic build: everything 0x100 later than in the disk layout */
#define IMAGE 0x10400

static uint8_t g_file[BASE + IMAGE];
static const ProgramSegment LOAD_SEGS[] = {{0, 0x10000, SHIFT}};
static ProgramRoutine g_routines[2];
static ProgramBuild g_build[1];

/* script of every screen: a hotspot whose click routine is a build address, then the end record */
static void make_file(uint32_t click) {
    memset(g_file, 0, sizeof g_file);
    memcpy(g_file, "MZsynthetic", 11);
    uint8_t *im = g_file + BASE;
    put16(im + RECORD_LENGTHS + SHIFT + 2 * OP_HOTSPOT, 54);
    put16(im + RECORD_LENGTHS + SHIFT + 2 * OP_END, 2);
    for (int c = 32; c < 256; c++) im[CHARMAP + SHIFT + c - 32] = (uint8_t)(c % 97);
    uint32_t table = SCREEN_TABLE + SHIFT, script = table + SCREEN_COUNT * 4;
    for (int i = 1; i < SCREEN_COUNT; i++) put32(im + table + 4 * i, script);
    put16(im + script, OP_HOTSPOT);
    put16(im + script + 2, 7);
    put16(im + script + 40, 3); /* target screen */
    put32(im + script + 42, click);
    put16(im + script + 54, OP_END);
}

static void set_build(void) {
    g_routines[0] = (ProgramRoutine){0x3000, 0xE085};
    g_routines[1] = (ProgramRoutine){0x3004, ROUTINE_CD_GOLF};
    g_build[0] = (ProgramBuild){"synthetic", sizeof g_file, program_crc32(g_file, sizeof g_file), BASE, "T", "D",
                                program_crc32(g_file + BASE + CHARMAP + SHIFT, 224), LOAD_SEGS, 1, NULL, 0,
                                g_routines, 2, 0};
}

static int load_file(Program *p, const char *dir) {
    char path[300];
    snprintf(path, sizeof path, "%s/BIPRO.EXE", dir);
    FILE *f = fopen(path, "wb");
    if (!f) return -2;
    fwrite(g_file, 1, sizeof g_file, f);
    fclose(f);
    return program_load_builds(p, path, g_build, 1);
}

static void test_load(const char *dir) {
    Program p;
    make_file(0x1234 + SHIFT);
    set_build();
    CHECK(load_file(&p, dir) == 0);
    CHECK(p.build == &g_build[0]);
    CHECK(p.image.size == IMAGE);
    CHECK(p.screens[5].count == 1 && p.screens[5].rec[0].id == 7 && p.screens[5].rec[0].target == 3);
    CHECK(p.screens[5].rec[0].click == 0x1234); /* translated back to the disk offset */
    CHECK(p.screens[161].rec[0].click == 0x1234);
    CHECK(p.charmap == p.image.data + CHARMAP + SHIFT - 32);
    CHECK(p.charmap[65] == 65 % 97);
    CHECK(program_u8(&p, CHARMAP, 1) == 33 % 97);
    CHECK(p.edition == EDITION_DISK && !program_cd(&p)); /* a build of another name plays the disk rules */
    program_free(&p);
    static const char *const NAMES[3] = {"Disk version 1.4", "GOG German CD", "GOG English CD"};
    for (int ed = 0; ed < 3; ed++) { /* the edition follows the build's name */
        make_file(0x1234 + SHIFT);
        set_build();
        g_build[0].name = NAMES[ed];
        CHECK(load_file(&p, dir) == 0 && p.edition == (Edition)ed);
        CHECK(program_cd(&p) == (ed != 0) && program_german_cd(&p) == (ed == 1) && program_english(&p) == (ed == 2));
        program_free(&p);
    }

    make_file(0x3000); /* a routine entry on a change point: the routine table names its disk routine */
    set_build();
    CHECK(load_file(&p, dir) == 0 && p.screens[2].rec[0].click == 0xE085);
    program_free(&p);
    make_file(0x3004);
    set_build();
    CHECK(load_file(&p, dir) == 0 && p.screens[2].rec[0].click == ROUTINE_CD_GOLF);
    program_free(&p);

    make_file(0x10200); /* inside the image, in no segment: the self-check refuses the build */
    set_build();
    CHECK(load_file(&p, dir) != 0 && strstr(asset_error(), "no disk counterpart"));

    make_file(0x1234 + SHIFT); /* another file of the same size: not this build */
    set_build();
    g_file[100] ^= 1;
    CHECK(load_file(&p, dir) != 0 && strstr(asset_error(), "not a supported build") &&
          strstr(asset_error(), "synthetic"));
    char want[64];
    snprintf(want, sizeof want, "(%zu bytes, CRC-32 %08X)", sizeof g_file, (unsigned)program_crc32(g_file, sizeof g_file));
    CHECK(strstr(asset_error(), want) != NULL); /* the file's own size and checksum, for the user to compare */

    make_file(0x1234 + SHIFT); /* the character map is not the expected one */
    set_build();
    g_build[0].charmap_crc ^= 1;
    CHECK(load_file(&p, dir) != 0 && strstr(asset_error(), "character map"));

    make_file(0x1234 + SHIFT); /* the screen table does not lead to its first script */
    put32(g_file + BASE + SCREEN_TABLE + SHIFT + 4, 0x2000);
    set_build();
    CHECK(load_file(&p, dir) != 0 && strstr(asset_error(), "screen table"));
}

/* The real build list: three builds, distinct sizes, the disk build first and without segments. */
static void test_build_list(void) {
    CHECK(PROGRAM_BUILD_COUNT == 3);
    CHECK(PROGRAM_BUILDS[0].segment_count == 0 && PROGRAM_BUILDS[0].base == 0x11C0);
    /* the editions by name: disk, German CD, English CD (docs/editions.md section 3.5) */
    for (int i = 0; i < PROGRAM_BUILD_COUNT; i++) CHECK(program_edition_of(&PROGRAM_BUILDS[i]) == (Edition)i);
    CHECK(program_edition_of(NULL) == EDITION_DISK && !program_cd(NULL) && !program_english(NULL));
    for (int i = 0; i < PROGRAM_BUILD_COUNT; i++)
        for (int k = i + 1; k < PROGRAM_BUILD_COUNT; k++) CHECK(PROGRAM_BUILDS[i].file_size != PROGRAM_BUILDS[k].file_size);
    /* segments ascending and not overlapping on the disk side */
    for (int i = 0; i < PROGRAM_BUILD_COUNT; i++) {
        const ProgramBuild *b = &PROGRAM_BUILDS[i];
        for (int k = 0; k < b->segment_count; k++) {
            CHECK(b->segments[k].disk < b->segments[k].end);
            if (k) CHECK(b->segments[k - 1].end <= b->segments[k].disk);
        }
    }
}

int main(void) {
    /* Use CTest's writable working directory, also on Windows where /tmp need not exist. */
    char dir[] = "biing_test_program_XXXXXX";
    if (!mkdtemp(dir)) {
        fprintf(stderr, "[FAIL] no temporary directory\n");
        return 1;
    }
    test_crc();
    test_build_at();
    test_build_routine();
    test_reads();
    test_load(dir);
    test_build_list();
    char path[300];
    snprintf(path, sizeof path, "%s/BIPRO.EXE", dir);
    CHECK(remove(path) == 0);
    CHECK(rmdir(dir) == 0);
    if (failures) fprintf(stderr, "[FAIL] %d checks failed\n", failures);
    else printf("[OK] program\n");
    return failures ? 1 : 0;
}
