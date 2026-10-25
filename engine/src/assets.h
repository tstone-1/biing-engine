/* Game data access: palette, image index, images, texts, people data, fonts.
 * Every format here is specified in docs/formats.md and docs/formats-data.md. */
#ifndef BIING_ASSETS_H
#define BIING_ASSETS_H

#include <stddef.h>
#include <stdint.h>

#define SCREEN_W 640
#define SCREEN_H 440 /* frame buffer: 400 picture rows, then the "Hilfe!" strip (docs/screens.md) */
#define IXY_COUNT 2000
#define TEXT_COUNT 4230 /* BITEXPZ offset table: 16,920 bytes */

typedef struct {
    uint8_t *data;
    size_t size;
} Blob;

/* Last error message of any function below that returned non-zero. */
const char *asset_error(void);
/* Sets that message (printf format) and returns -1. */
int asset_fail(const char *fmt, ...);

int blob_read(const char *path, Blob *out);
void blob_free(Blob *b);

typedef struct {
    uint16_t w, h, x, y;
} IxyEntry;

typedef struct {
    char root[1024]; /* the BIING! directory */
    uint8_t palette[768]; /* DATA/COL, 8-bit RGB */
    IxyEntry ixy[IXY_COUNT];
} Game;

int game_open(Game *g, const char *root);
/* root-relative path of a game file into buf, e.g. "DATA/COL". */
void game_path(const Game *g, const char *rel, char *buf, size_t n);

/* Images: DATA/<a>/<b>/<c>, index a*100 + b*10 + c with a in 0-9, A-Z. */
enum { IMG_RAW = 0x0000, IMG_RAW_40 = 0x0040, IMG_RLE = 0x0080 };

typedef struct {
    int index;
    int w, h, x, y; /* x/y: screen position from IXY */
    uint16_t flags;
    uint8_t *px; /* w*h colour indices, row-major */
    /* RLE images only (NULL otherwise): w*h bytes, 1 where the pixel came from a span, 0 for a gap. The blitter
     * draws every span pixel, colour 0 included, and leaves the gaps (docs/screens.md, "Image blitter", SCR-6).
     * Kept apart from px so the pixel digests of the asset parity stay as they are. */
    uint8_t *mask;
} Image;

/* "DATA/a/b/c" for an index below 3,600; returns 0 on success. */
int image_rel_path(int index, char *buf, size_t n);
/* Decodes the file body (flag word included) into out[w*h]; returns 0 on success. */
int image_decode(const uint8_t *raw, size_t n, int w, int h, uint8_t *out, uint16_t *flags);
/* As image_decode; for an RLE image also writes the span mask into mask[w*h] (1 span, 0 gap) when mask is not NULL.
 * A raw image leaves mask untouched. */
int image_decode_mask(const uint8_t *raw, size_t n, int w, int h, uint8_t *out, uint8_t *mask, uint16_t *flags);
int image_load(const Game *g, int index, Image *img);
void image_free(Image *img);

/* BITEXPZ: running-sum scramble, key 0x2C, restarted for table and text block. */
void unscramble_sum(uint8_t *buf, size_t n);

typedef struct {
    Blob file;
    const uint8_t *text; /* unscrambled text block */
    uint32_t text_size;
    uint32_t offs[TEXT_COUNT];
} TextDb;

int textdb_load(TextDb *db, const char *path);
int textdb_parse(TextDb *db, Blob file); /* takes ownership of file */
/* Entry i as Latin-1 bytes without the '$' terminator; *len gets its length. */
const uint8_t *textdb_entry(const TextDb *db, int i, size_t *len);
/* Entry i as the game copies it into its message buffer: from the entry's start up to the next '$' or 0,
 * which for the three entries without a terminator (858, 1789, 2779) runs into the following entry
 * (docs/gui.md, "The message buffer"). */
const uint8_t *textdb_text(const TextDb *db, int i, size_t *len);
/* Entry i byte for byte, terminator included: for entry 4014, which is a byte table (docs/sim-patients.md). */
const uint8_t *textdb_raw(const TextDb *db, int i, size_t *len);
void textdb_free(TextDb *db);

/* BIDATP: six u32 pool sizes, then 14 blocks scrambled with ror8(byte - 0x4D, 3). */
void unscramble_ror(uint8_t *buf, size_t n);

enum { DAT_PATIENTS, DAT_STAFF, DAT_PATIENT_NAMES, DAT_PROFESSIONS, DAT_TRAITS,
       DAT_STAFF_NAMES, DAT_QUALIFICATIONS, DAT_PECULIARITIES, DAT_POOLS_END };
#define PATIENT_SIZE 106
#define PATIENT_COUNT 1009
#define STAFF_SIZE 32
#define STAFF_COUNT 1041

typedef struct {
    const uint8_t *text; /* pool text, unscrambled */
    size_t text_size;
    const uint8_t *table; /* LE u32 offsets */
    int count;
} TextPool;

typedef struct {
    Blob file; /* unscrambled in place */
    const uint8_t *patients; /* PATIENT_COUNT x PATIENT_SIZE */
    const uint8_t *staff; /* STAFF_COUNT x STAFF_SIZE */
    TextPool pools[6]; /* index = DAT_PATIENT_NAMES.. minus 2 */
} DataDb;

int datadb_load(DataDb *db, const char *path);
int datadb_parse(DataDb *db, Blob file); /* takes ownership of file */
const uint8_t *datadb_pool_entry(const DataDb *db, int pool, int i, size_t *len);
void datadb_free(DataDb *db);

/* .FNT: "PC", BE u16 height, 12 zero bytes, 128 BE offsets (u16, or u32 in BI2PC.FNT),
 * glyph = BE u16 width + width*height bytes column by column. */
typedef struct {
    int height;
    int width[128]; /* 0 = no glyph; the byte at offset + 1 (the low byte of the BE width, docs/gui.md "Glyphs") */
    uint8_t *glyph[128]; /* converted to row-major; NULL where the slot's offset is 0 */
    /* The header glyph a slot with offset 0 draws (docs/gui.md, "Unchecked slots"): width = font byte 1 (67 for "PC"),
     * column height = font byte 3, the font's bytes from +2 on column by column (past the file's end 0), row-major. */
    int head_w, head_h;
    uint8_t *head;
} Font;

int font_parse(Font *f, const uint8_t *raw, size_t n);
int font_load(Font *f, const char *path);
void font_free(Font *f);

#endif
