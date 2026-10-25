/* Executable regression for the real savetool.c argument/environment path. Only loading the owned
 * program image is substituted: every table and save is synthetic; clock/advice/save code stays real. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "portable.h"
#include "sim_internal.h"

int savetool_main(int argc, char **argv);
#undef main

int program_load(Program *p, const char *path) {
    static const ProgramBuild build = {.name = "synthetic", .texts = "DATA/TEXTS", .data = "DATA/PEOPLE"};
    memset(p, 0, sizeof *p);
    p->build = &build;
    return blob_read(path, &p->image);
}

static int write_bytes(const char *root, const char *name, const uint8_t *bytes, size_t size) {
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", root, name);
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    int bad = fwrite(bytes, 1, size, f) != size;
    return fclose(f) != 0 || bad;
}

static int fixture(const char *root) {
    char path[1200];
    test_mkdir(root);
    snprintf(path, sizeof path, "%s/DATA", root);
    test_mkdir(path);
    static uint8_t image[0x10000];
    /* Furniture satisfies the earlier scheduled checks; missing equipment reaches advice kind 3. */
    put16(image + 0x4758 + 14 * 2, 0x20);
    put16(image + 0x4758 + 14 * 2 + 2, 2);
    put16(image + 0x4758 + 14 * 2 + 4, 0x24);
    put16(image + 0x4758 + 14 * 2 + 6, 2);
    put16(image + 0x4758 + 14 * 2 + 8, 0x30);
    image[0xEE0 + 18 + 6] = 1, image[0xEE0 + 36 + 6] = 5;
    image[0xEE0 + 18 + 4] = image[0xEE0 + 36 + 4] = 100;
    if (write_bytes(root, "BIPRO.EXE", image, sizeof image) ||
        write_bytes(root, "DATA/COL", image, 768) || write_bytes(root, "DATA/IXY", image, IXY_COUNT * 8)) return 1;
    /* Empty text entries, encoded through the documented rolling-sum format. */
    uint8_t texts[4 + TEXT_COUNT * 4] = {0};
    uint8_t key = 0x2C;
    for (size_t i = 4; i < sizeof texts; i++) texts[i] = key, key = (uint8_t)(key + texts[i]);
    if (write_bytes(root, "DATA/TEXTS", texts, sizeof texts)) return 1;
    enum { PEOPLE_SIZE = 24 + PATIENT_COUNT * PATIENT_SIZE + STAFF_COUNT * STAFF_SIZE +
                        0xFC4 + 0x400 + 0xFC4 + 0x1044 * 3 };
    uint8_t *people = calloc(PEOPLE_SIZE, 1);
    if (!people) return 1;
    memset(people + 24, 0x4D, PEOPLE_SIZE - 24); /* encoded zero records and empty pool offsets */
    int bad = write_bytes(root, "DATA/PEOPLE", people, PEOPLE_SIZE);
    free(people);
    if (bad) return 1;
    World w;
    world_alloc(&w);
    memset(w.block[BLK_ROOM_SLOTS], 0xFF, w.size[BLK_ROOM_SLOTS]);
    memset(w.block[BLK_JOB_OPENINGS], 0xFF, w.size[BLK_JOB_OPENINGS]);
    /* Empty offers have an absent owner and zero payload. A refill without a matching synthetic
     * capability may activate the slot without replacing its item ID; FFFF would then be out of range. */
    for (int i = 0; i < MARKET_OFFERS; i++)
        w.block[BLK_EQUIPMENT_MARKET][i * 8] = w.block[BLK_FURNITURE_MARKET][i * 8] = 0xFF;
    for (int i = 0; i < PATIENT_COUNT; i++) w.block[BLK_PATIENTS][i * PATIENT_SIZE + 3] = 16;
    for (int i = 0; i < STAFF_COUNT; i++) w.block[BLK_STAFF][i * STAFF_SIZE + 3] = 0xFF;
    uint8_t *h = world_hospital(&w, 0);
    h[0x14] = 1, h[0x15] = 0xFF;
    world_set_cash(&w, 0, 984);
    world_hospital(&w, 15)[0x15] = 0xFF;
    world_set_cash(&w, 15, -100);
    w.block[BLK_ROOM_COUNTS][2] = w.block[BLK_ROOM_COUNTS][4] = 1;
    put16(w.block[3] + 8, 41); /* receptionist */
    put16(w.block[3] + 0x20, 1), put16(w.block[3] + 0x22, 2);
    w.block[3][0] = 200, w.block[3][0x30] = w.block[3][0x31] = 100;
    put16(w.block[12] + STORE_CLERK, 42);
    w.block[BLK_STAFF][41 * STAFF_SIZE + 11] = 0xFF;
    world_set_tail(&w, TAIL_DAY, 3);
    world_set_tail(&w, TAIL_MONTH, 5);
    world_set_tail(&w, TAIL_HOUR, 12);
    snprintf(path, sizeof path, "%s/input.sav", root);
    bad = world_save(&w, path);
    world_free(&w);
    return bad;
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "fixture")) return fixture(argv[2]);
    if (argc == 4 && !strcmp(argv[1], "check")) {
        World w;
        if (world_load(&w, argv[2])) return 1;
        int got = world_hospital(&w, 0)[0xDB], expected = atoi(argv[3]);
        if (got != expected) fprintf(stderr, "[FAIL] scheduled advice request: %d, expected %d (reception %d, clerk %d)\n",
                                    got, expected, get16(w.block[3] + 8), get16(w.block[12] + STORE_CLERK));
        world_free(&w);
        return got != expected;
    }
    return savetool_main(argc, argv);
}
