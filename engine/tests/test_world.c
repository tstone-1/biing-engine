/* Save game format and world fields. Synthetic data only (see AGENTS.md). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "world.h"

static int failures;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            failures++;                                                            \
        }                                                                          \
    } while (0)

static void fill(World *w) {
    for (int i = 0; i <= WORLD_BLOCKS; i++)
        for (size_t k = 0; k < w->size[i]; k++) w->block[i][k] = (uint8_t)(k * 7 + (size_t)i);
    world_set_cash(w, 0, -500);
    world_set_tail(w, TAIL_YEAR, 1995);
}

static void test_round_trip(void) {
    World a, b;
    CHECK(world_alloc(&a) == 0);
    fill(&a);
    size_t n = world_encoded_size(&a);
    CHECK(n == 530666); /* docs/saves.md */
    uint8_t *buf = malloc(n);
    world_encode(&a, buf);
    CHECK(buf[8] == 0x10 && buf[0] == 0x10 && buf[1] == 0x04 && buf[4] == 0xF0 && buf[5] == 0x03);
    CHECK(world_decode(&b, buf, n) == 0);
    for (int i = 0; i <= WORLD_BLOCKS; i++) CHECK(memcmp(a.block[i], b.block[i], a.size[i]) == 0);
    CHECK(world_cash(&b, 0) == -500 && world_tail(&b, TAIL_YEAR) == 1995);
    world_free(&b);
    free(buf);
    world_free(&a);
}

static void test_zero_block_scramble(void) {
    /* A zero byte stores as the key doubling: 0C 18 30 60 C0 80 00 (docs/saves.md). */
    World a;
    CHECK(world_alloc(&a) == 0);
    uint8_t *buf = malloc(world_encoded_size(&a));
    world_encode(&a, buf);
    const uint8_t want[7] = {0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00};
    CHECK(memcmp(buf + 12, want, 7) == 0);
    /* every block restarts the key: block 2 starts right after block 1 */
    CHECK(memcmp(buf + 12 + a.size[0], want, 7) == 0);
    free(buf);
    world_free(&a);
}

static void test_rejects_damage(void) {
    World a, b;
    CHECK(world_alloc(&a) == 0);
    fill(&a);
    size_t n = world_encoded_size(&a);
    uint8_t *buf = malloc(n);
    world_encode(&a, buf);
    buf[100] ^= 0x40; /* inside block 1 */
    CHECK(world_decode(&b, buf, n) != 0);
    world_encode(&a, buf);
    buf[8] = 0x0F; /* version */
    CHECK(world_decode(&b, buf, n) != 0);
    world_encode(&a, buf);
    CHECK(world_decode(&b, buf, n - 1) != 0);
    world_encode(&a, buf);
    buf[n - 1] ^= 1; /* the tail is not covered by the checksum */
    CHECK(world_decode(&b, buf, n) == 0);
    world_free(&b);
    free(buf);
    world_free(&a);
}

/* The new game's player blocks (docs/mainloop.md, section 2, "No two-player mode"; docs/sound.md, "0x2785D and the
 * bytes 0x63CB/0x63CC"): player block 2 +4 (player 1's screen word) is 1; player block 1 +0xB0 / +0xB1 are cleared,
 * +0xB2 / +0xB3 and block 2's four keep the image's 1. A synthetic image with those ones planted. */
static void test_new_game_player_blocks(void) {
    static uint8_t image[0x80000];
    static Program prog;
    static uint8_t patients[1009 * 106], staff[1041 * 32];
    memset(image + 0x631B + 0xB0, 1, 4), memset(image + 0x6569 + 0xB0, 1, 4);
    prog.image.data = image, prog.image.size = sizeof image;
    World w;
    CHECK(world_new(&w, &prog, patients, staff) == 0);
    const uint8_t *b28 = w.block[BLK_PLAYER0], *b29 = w.block[BLK_PLAYER1];
    CHECK(b29[4] == 1 && b29[5] == 0 && b28[4] == 0x38 && b28[5] == 0); /* P +4: the player default 0x38 (SAV-1) */
    CHECK(b28[0xB0] == 0 && b28[0xB1] == 0 && b28[0xB2] == 1 && b28[0xB3] == 1);
    CHECK(b29[0xB0] == 1 && b29[0xB1] == 1 && b29[0xB2] == 1 && b29[0xB3] == 1);
    world_free(&w);
}

/* OTH-4 (docs/editions.md 7.5): player block +6, the continue routine, holds a code address of the build that wrote
 * the save; the engine translates it build -> disk at load and disk -> build at save, through the build's segments and
 * its routine exception rows. A build that inserted 0x50 bytes at disk 0x100 and has one routine row (build 0x5000 is
 * disk 0x4000, which the segments would put at 0x4050). */
static const ProgramSegment CSEGS[] = {{0, 0x100, 0}, {0x100, 0x30000, 0x150}};
static const ProgramRoutine CRTS[] = {{0x5000, 0x4000}, {0x5004, ROUTINE_CD_GOLF}};
static const ProgramBuild CBUILD = {"GOG English CD", 0, 0, 0, "T", "D", 0, CSEGS, 2, NULL, 0, CRTS, 2, 0};
static uint32_t cont(const World *w) {
    const uint8_t *c = w->block[BLK_PLAYER0] + WORLD_CONTINUE;
    return c[0] | c[1] << 8 | (uint32_t)c[2] << 16 | (uint32_t)c[3] << 24;
}
static void set_cont(World *w, uint32_t v) {
    for (int i = 0; i < 4; i++) w->block[BLK_PLAYER0][WORLD_CONTINUE + i] = (uint8_t)(v >> (8 * i));
}
static void test_continue_routine(void) {
    World w, r;
    CHECK(world_alloc(&w) == 0);
    Program cd = {0}, disk = {0}, plain = {0};
    cd.build = &CBUILD;
    static const ProgramBuild DISKB = {"Disk version 1.4", 0, 0, 0, "T", "D", 0, NULL, 0, NULL, 0, NULL, 0, 0};
    disk.build = &DISKB;
    static const uint32_t BUILD[] = {0x2CE3B, 0x80, 0x5000, 0x120, 0x5004, 0};
    static const uint32_t DISK[] = {0x2CDEB, 0x80, 0x4000, 0x120, 0x5004, 0}; /* unmapped and golf: kept */
    for (int i = 0; i < 6; i++) {
        set_cont(&w, BUILD[i]);
        world_continue_to_disk(&w, &cd);
        CHECK(cont(&w) == DISK[i]);
        set_cont(&w, BUILD[i]);
        world_continue_to_disk(&w, &disk); /* the disk build and a synthetic program: unchanged */
        world_continue_to_disk(&w, &plain);
        world_continue_to_disk(&w, NULL);
        CHECK(cont(&w) == BUILD[i]);
    }
    set_cont(&w, 0x2CDEB);
    world_continue_to_build(&w, &cd);
    CHECK(cont(&w) == 0x2CE3B);
    set_cont(&w, 0x4000); /* the routine row first */
    world_continue_to_build(&w, &cd);
    CHECK(cont(&w) == 0x5000);
    set_cont(&w, 0x30010); /* past every segment: kept */
    world_continue_to_build(&w, &cd);
    CHECK(cont(&w) == 0x30010);
    set_cont(&w, 0x2CDEB);
    world_continue_to_build(&w, &disk);
    CHECK(cont(&w) == 0x2CDEB);
    /* save and load through the edition: the file holds the build's address, the world keeps the disk offset */
    set_cont(&w, 0x2CDEB);
    CHECK(world_save_edition(&w, "test_world_continue.sav", &cd) == 0 && cont(&w) == 0x2CDEB);
    CHECK(world_load(&r, "test_world_continue.sav") == 0 && cont(&r) == 0x2CE3B);
    world_free(&r);
    CHECK(world_load_edition(&r, "test_world_continue.sav", &cd) == 0 && cont(&r) == 0x2CDEB);
    world_free(&r);
    CHECK(world_save_edition(&w, "test_world_continue.sav", &disk) == 0);
    CHECK(world_load(&r, "test_world_continue.sav") == 0 && cont(&r) == 0x2CDEB);
    world_free(&r);
    remove("test_world_continue.sav");
    world_free(&w);
}

int main(void) {
    test_round_trip();
    test_zero_block_scramble();
    test_rejects_damage();
    test_new_game_player_blocks();
    test_continue_routine();
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] all checks passed\n");
    return 0;
}
