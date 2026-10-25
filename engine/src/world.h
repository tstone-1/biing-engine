/* The game state, kept in the same 36 blocks plus tail block that the original writes to a save game
 * (docs/saves.md), so that saves are byte-compatible in both directions. Field meanings: docs/formats.md,
 * docs/economy.md. */
#ifndef BIING_WORLD_H
#define BIING_WORLD_H

#include "assets.h"
#include "program.h"

#define WORLD_BLOCKS 36
#define WORLD_TAIL WORLD_BLOCKS /* index of the tail block */
#define SAVE_VERSION 0x10

enum { /* block indices (block number - 1) */
    BLK_PATIENTS = 0, BLK_STAFF = 1, BLK_ROOM_SLOTS = 2, BLK_ROOM_COUNTS = 10, BLK_JOB_OPENINGS = 13, BLK_STAFF_SLOTS = 14, BLK_MARKET = 23,
    BLK_FURNITURE_MARKET = 24, BLK_EQUIPMENT_MARKET = 25, BLK_HOSPITALS = 26, BLK_PLAYER0 = 27, BLK_PLAYER1 = 28, BLK_30 = 29,
    BLK_NEWS = 30, BLK_COURSES = 31, BLK_CARS = 32, BLK_34 = 33,
};

typedef struct {
    uint8_t *block[WORLD_BLOCKS + 1];
    size_t size[WORLD_BLOCKS + 1];
} World;

/* Size and origin of each block: a static block starts from the program image at `image_offset`,
 * a heap block (image_offset 0) starts zeroed. */
typedef struct {
    uint32_t size;
    uint32_t image_offset;
} BlockInfo;
extern const BlockInfo WORLD_LAYOUT[WORLD_BLOCKS + 1];

/* Hospital records: 17 x 500 bytes, record 0 is the player. */
#define HOSPITAL_SIZE 500
#define HOSPITAL_COUNT 17
uint8_t *world_hospital(World *w, int i);
int32_t world_cash(const World *w, int hospital);
void world_set_cash(World *w, int hospital, int32_t v);

/* Tail block words (the static area at 0x694B): +2 hours played, +6 year ... +14 minute. */
enum { TAIL_HOURS = 2, TAIL_YEAR = 6, TAIL_MONTH = 8, TAIL_DAY = 10, TAIL_HOUR = 12, TAIL_MINUTE = 14 };
int world_tail(const World *w, int off);
void world_set_tail(World *w, int off, int v);

int world_alloc(World *w);
void world_free(World *w);
/* A new game: blocks from the program image and BIDATP, then the new-game initialisation of
 * docs/mainloop.md, section 1. The daily-report snapshots start at 0, as in the original's new game (report.c). */
int world_new(World *w, const Program *p, const uint8_t *patients, const uint8_t *staff);
/* 0x19C13: the price index +0x10A of hospitals 0-15 (docs/mainloop.md, "Price index"). */
void world_price_index(World *w, const Program *p);

/* Save game format of docs/saves.md. */
int world_load(World *w, const char *path);
int world_save(const World *w, const char *path);
/* Player block +6 (block 28 bytes 6-9), the continue routine, is the only field of a save that holds a code address of
 * the build that wrote it (docs/editions.md 7.5, OTH-4). The engine keeps it as a disk offset: a save of the game
 * folder's edition is translated build -> disk at load and disk -> build at save. A value with no counterpart is kept
 * as it is. Identity on the disk build and on a synthetic program. */
#define WORLD_CONTINUE 6
void world_continue_to_disk(World *w, const Program *p);
void world_continue_to_build(World *w, const Program *p);
/* world_load / world_save with that translation (the game's own load and save, the --shot steps) */
int world_load_edition(World *w, const char *path, const Program *p);
int world_save_edition(World *w, const char *path, const Program *p);
int world_decode(World *w, const uint8_t *data, size_t n);
/* Encodes into a buffer of world_encoded_size() bytes. */
size_t world_encoded_size(const World *w);
void world_encode(const World *w, uint8_t *out);

#endif
