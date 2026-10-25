#include "world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* docs/saves.md, "Blocks in file order"; the static blocks name their program offset. */
const BlockInfo WORLD_LAYOUT[WORLD_BLOCKS + 1] = {
    {0x1A1CA, 0}, {0x8220, 0}, {0x1900, 0}, {0xAE0, 0}, {0x9E0, 0}, {0xB20, 0}, {0x9E0, 0}, {0xDA0, 0},
    {0x760, 0}, {0x760, 0}, {0x640, 0}, {0x7080, 0}, {0x2DA0, 0}, {0x578, 0}, {0xFA00, 0}, {0x7080, 0},
    {0x9600, 0}, {0x9600, 0}, {0x5100, 0}, {0x5100, 0}, {0x7440, 0}, {0x7080, 0}, {0x7080, 0}, {0x3E8, 0},
    {0x960, 0}, {0x960, 0}, {0x2134, 0}, {0x1F4, 0x631B}, {0x1F4, 0x6569}, {0x190, 0x3DCD}, {0x1B58, 0},
    {0xC8, 0}, {0x190, 0}, {0x60, 0x552A}, {0x20, 0}, {0x258, 0},
    {0x5E, 0x694B}, /* tail */
};

#define SCRAMBLE_KEY 0x0C
#define HEADER_SIZE 12
/* Block 30 bytes 0-35 hold byte 0 of the 36 ten-byte records at this program offset (docs/saves.md). */
#define BLOCK30_RECORDS 0x3F71

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

uint8_t *world_hospital(World *w, int i) { return w->block[BLK_HOSPITALS] + (size_t)i * HOSPITAL_SIZE; }
int32_t world_cash(const World *w, int hospital) {
    return (int32_t)rd32(w->block[BLK_HOSPITALS] + (size_t)hospital * HOSPITAL_SIZE + 0x16);
}
void world_set_cash(World *w, int hospital, int32_t v) { wr32(world_hospital(w, hospital) + 0x16, (uint32_t)v); }

int world_tail(const World *w, int off) {
    const uint8_t *p = w->block[WORLD_TAIL] + off;
    return (int16_t)(p[0] | p[1] << 8);
}
void world_set_tail(World *w, int off, int v) {
    uint8_t *p = w->block[WORLD_TAIL] + off;
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

int world_alloc(World *w) {
    memset(w, 0, sizeof *w);
    for (int i = 0; i <= WORLD_BLOCKS; i++) {
        w->size[i] = WORLD_LAYOUT[i].size;
        w->block[i] = calloc(1, w->size[i]);
        if (!w->block[i]) {
            world_free(w);
            return asset_fail("out of memory");
        }
    }
    return 0;
}

void world_free(World *w) {
    for (int i = 0; i <= WORLD_BLOCKS; i++) free(w->block[i]);
    memset(w, 0, sizeof *w);
}

static void mark_empty(uint8_t *table, int count, int stride) {
    for (int i = 0; i < count; i++) table[(size_t)i * stride] = 0xFF;
}

/* Price index of docs/mainloop.md, "Price index". */
void world_price_index(World *w, const Program *p) {
    int16_t v[10];
    for (int i = 0; i < 10; i++) v[i] = (int16_t)program_word(p, 0x465E, 2 * i);
    const uint8_t *rooms = w->block[BLK_ROOM_COUNTS]; /* [0x5A85]: 100 room counts per hospital */
    for (int h = 0; h < 16; h++) {
        uint8_t *r = world_hospital(w, h);
        const uint8_t *c = rooms + h * 100;
        int k_ward = (uint8_t)(c[6] + c[16]) ? r[0x20] : 3;
        int k_icu = c[9] ? r[0x21] : 0;
        int k_op = (uint8_t)(c[7] + c[8]) ? r[0x22] : 3;
        int keys[7] = {k_ward, k_icu, k_op, r[0x23], r[0x24], r[0x25], r[0x94]};
        int sum = 0;
        /* No copy-protection penalty (+0x172 >= 4, docs/mainloop.md): the engine has no manual query */
        for (int k = 0; k < 7; k++) sum += keys[k] < 10 ? v[keys[k]] : 0;
        sum -= 2 * r[0x95];
        if (sum < 0) sum = 0;
        r[0x10A] = (uint8_t)sum;
        r[0x10B] = (uint8_t)(sum >> 8);
    }
}

int world_new(World *w, const Program *p, const uint8_t *patients, const uint8_t *staff) {
    if (world_alloc(w)) return -1;
    for (int i = 0; i <= WORLD_BLOCKS; i++) {
        uint32_t off = WORLD_LAYOUT[i].image_offset;
        size_t left;
        const uint8_t *src = off ? program_raw_ptr(p, program_at(p, off), &left) : NULL;
        if (src && w->size[i] <= left) memcpy(w->block[i], src, w->size[i]);
    }
    for (int i = 0; i < 36; i++) w->block[BLK_30][i] = (uint8_t)program_u8(p, BLOCK30_RECORDS, 10u * (uint32_t)i);
    memcpy(w->block[BLK_PATIENTS], patients, w->size[BLK_PATIENTS]);
    memcpy(w->block[BLK_STAFF], staff, w->size[BLK_STAFF]);

    /* 0x19B81 */
    memset(w->block[BLK_ROOM_SLOTS], 0xFF, w->size[BLK_ROOM_SLOTS]);
    mark_empty(w->block[BLK_JOB_OPENINGS], 200, 7);
    mark_empty(w->block[BLK_MARKET], 100, 10);
    mark_empty(w->block[BLK_EQUIPMENT_MARKET], 300, 8);
    mark_empty(w->block[BLK_FURNITURE_MARKET], 300, 8);
    for (int i = 0; i < 1041; i++) {
        uint8_t *s = w->block[BLK_STAFF] + i * 32;
        s[11] = 0;
        if (s[0] == 0) s[7] = 100;
    }
    for (int i = 0; i < 1009; i++) w->block[BLK_PATIENTS][i * 106 + 40] = 0;

    /* 0x38C92 */
    world_hospital(w, 0)[0x14] = 2;
    for (int h = 0; h < HOSPITAL_COUNT; h++) {
        uint8_t *r = world_hospital(w, h);
        world_set_cash(w, h, 920);
        r[0x15] = 0xFF;
        for (int k = 0x20; k <= 0x25; k++) r[k] = 2;
        r[0x94] = 2;
        r[0x95] = 9;
        r[0x109] = 0;
        r[0xDD] = (uint8_t)(h % 4);
    }
    world_price_index(w, p);
    for (int h = 0; h < 16; h++) {
        uint8_t *r = world_hospital(w, h);
        r[0x10C] = r[0x10A];
        r[0x10D] = r[0x10B];
    }
    world_set_cash(w, 0, world_cash(w, 0) + 64);

    /* 0x19D45, 0x19D5D */
    mark_empty(w->block[BLK_COURSES], 20, 10);
    mark_empty(w->block[BLK_CARS], 20, 20);

    /* Program start (docs/mainloop.md, section 2, "No two-player mode"): player 1's screen word is 1 (the office,
     * 0x19855) and the main loop's end copies it into player block 2 +4 ([0x656D]) on every pass; there is no
     * two-player mode, so it stays 1. The two start calls of the direct play (0x198F9, 0x1991A) clear the "play once"
     * arm bytes of player block 1 +0xB0 / +0xB1 (docs/sound.md, "0x2785D and the bytes 0x63CB/0x63CC"); block 2's
     * stay as the image has them. */
    w->block[BLK_PLAYER1][4] = 1;
    w->block[BLK_PLAYER0][4] = 0x38; /* [0x631F], the main loop's copy of the screen: the player default 0x38 (SAV-1) */
    w->block[BLK_PLAYER0][5] = 0;
    w->block[BLK_PLAYER0][0xB0] = w->block[BLK_PLAYER0][0xB1] = 0;
    return 0;
}

/* ---- save game format ------------------------------------------------------------------------ */

static uint8_t decode_block(uint8_t *dst, const uint8_t *src, size_t n, uint16_t *checksum) {
    uint8_t key = SCRAMBLE_KEY;
    for (size_t i = 0; i < n; i++) {
        dst[i] = (uint8_t)(src[i] - key);
        key = (uint8_t)(key + src[i]);
        if (checksum) *checksum = (uint16_t)(*checksum + key);
    }
    return key;
}

static void encode_block(uint8_t *dst, const uint8_t *src, size_t n, uint16_t *checksum) {
    uint8_t key = SCRAMBLE_KEY;
    for (size_t i = 0; i < n; i++) {
        dst[i] = (uint8_t)(src[i] + key);
        key = (uint8_t)(key + dst[i]);
        if (checksum) *checksum = (uint16_t)(*checksum + key);
    }
}

size_t world_encoded_size(const World *w) {
    size_t n = HEADER_SIZE + 2;
    for (int i = 0; i <= WORLD_BLOCKS; i++) n += w->size[i];
    return n;
}

void world_encode(const World *w, uint8_t *out) {
    wr32(out, 1041 - 1);
    wr32(out + 4, 1009 - 1);
    wr32(out + 8, SAVE_VERSION);
    size_t pos = HEADER_SIZE;
    uint16_t sum = 0;
    for (int i = 0; i < WORLD_BLOCKS; i++) {
        encode_block(out + pos, w->block[i], w->size[i], &sum);
        pos += w->size[i];
    }
    out[pos] = (uint8_t)sum;
    out[pos + 1] = (uint8_t)(sum >> 8);
    encode_block(out + pos + 2, w->block[WORLD_TAIL], w->size[WORLD_TAIL], NULL);
}

int world_decode(World *w, const uint8_t *data, size_t n) {
    if (n < HEADER_SIZE) return asset_fail("save game shorter than its header");
    if (rd32(data + 8) != SAVE_VERSION) return asset_fail("save game version 0x%X, expected 0x10", rd32(data + 8));
    if (rd32(data) != 1041 - 1 || rd32(data + 4) != 1009 - 1)
        return asset_fail("save game with %u staff / %u patients is not supported", rd32(data) + 1, rd32(data + 4) + 1);
    if (world_alloc(w)) return -1;
    size_t want = world_encoded_size(w);
    if (n != want) {
        world_free(w);
        return asset_fail("save game has %zu bytes, expected %zu", n, want);
    }
    size_t pos = HEADER_SIZE;
    uint16_t sum = 0;
    for (int i = 0; i < WORLD_BLOCKS; i++) {
        decode_block(w->block[i], data + pos, w->size[i], &sum);
        pos += w->size[i];
    }
    uint16_t stored = (uint16_t)(data[pos] | data[pos + 1] << 8);
    decode_block(w->block[WORLD_TAIL], data + pos + 2, w->size[WORLD_TAIL], NULL);
    if (stored != sum) {
        world_free(w);
        return asset_fail("save game checksum 0x%04X, computed 0x%04X", stored, sum);
    }
    return 0;
}

int world_load(World *w, const char *path) {
    Blob b;
    if (blob_read(path, &b)) return -1;
    int rc = world_decode(w, b.data, b.size);
    blob_free(&b);
    return rc;
}

int world_save(const World *w, const char *path) {
    size_t n = world_encoded_size(w);
    uint8_t *buf = malloc(n);
    if (!buf) return asset_fail("out of memory");
    world_encode(w, buf);
    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(buf, 1, n, f) == n;
    if (f && fclose(f)) ok = 0;
    free(buf);
    return ok ? 0 : asset_fail("cannot write %s", path);
}

static uint32_t continue_get(const World *w) {
    const uint8_t *c = w->block[BLK_PLAYER0] + WORLD_CONTINUE;
    return c[0] | c[1] << 8 | (uint32_t)c[2] << 16 | (uint32_t)c[3] << 24;
}
static void continue_set(World *w, uint32_t v) {
    uint8_t *c = w->block[BLK_PLAYER0] + WORLD_CONTINUE;
    for (int i = 0; i < 4; i++) c[i] = (uint8_t)(v >> (8 * i));
}

void world_continue_to_disk(World *w, const Program *p) {
    if (!p || !p->build || !w->block[BLK_PLAYER0]) return;
    uint32_t v = continue_get(w), d = build_routine(p->build, v);
    if (v && !(d & ROUTINE_UNMAPPED)) continue_set(w, d); /* ROUTINE_CD_GOLF carries the bit too */
}

void world_continue_to_build(World *w, const Program *p) {
    if (!p || !p->build || !w->block[BLK_PLAYER0]) return;
    uint32_t v = continue_get(w), b = PROGRAM_NOWHERE;
    if (!v) return;
    for (int i = 0; i < p->build->routine_count; i++) /* a routine on a change point: its exception row */
        if (p->build->routines[i].disk == v) b = p->build->routines[i].build;
    if (b == PROGRAM_NOWHERE) b = build_at(p->build, v);
    if (b != PROGRAM_NOWHERE) continue_set(w, b);
}

int world_load_edition(World *w, const char *path, const Program *p) {
    if (world_load(w, path)) return -1;
    world_continue_to_disk(w, p);
    return 0;
}

int world_save_edition(World *w, const char *path, const Program *p) {
    if (!w->block[BLK_PLAYER0]) return world_save(w, path);
    uint32_t keep = continue_get(w);
    world_continue_to_build(w, p);
    int r = world_save(w, path);
    continue_set(w, keep); /* the engine keeps running on the disk offset */
    return r;
}
