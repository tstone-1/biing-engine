/* BINT.EXE, the story intro and the credits (docs/intro.md, "BINT.EXE - the story intro and credits", "What the engine
 * needs (BINT)"). The scene script, the motion table, the caption table and texts and the character map come from the
 * user's BINT.EXE; the pictures from DATA/N; the music is N/BINTROPA.SCT then N/BFANF.SCT (English CD: N/BINTROPA.RAW and
 * N/BFANF.RAW, docs/intro.md "The module branch and the English `.RAW` modules") through the engine's module
 * player. Time is the music sequencer's quarter-row counter, advanced by the program's own 50 Hz timer. */
#ifndef BIING_INTRO_BINT_H
#define BIING_INTRO_BINT_H

#include "assets.h"
#include "intro.h"

struct Sound;

#define BINT_ENTRIES 27
#define BINT_LIST_MAX 48
#define BINT_MOTIONS 256
#define BINT_CAPTIONS 64
#define BINT_STARS 47
#define BINT_RES 2400 /* resource numbers 0 ... 2399 */
#define BINT_TEMPO_STORY 144
#define BINT_TEMPO_CREDITS 194
#define BINT_PART2_CLOCK 21960 /* quarter-rows: row 5,490 */
#define BINT_PART2_PAUSE 60 /* retraces */
#define BINT_FASTFORWARD 20 /* quarter-rows per main-loop pass while the right button is held */
#define BINT_SCROLL_START 3540 /* row */
/* CD mode (MCARD CD with the tracks; docs/intro.md, "CD audio of the start programs"): track 3 for the story, 4 for
 * the credits, and after track 3's start the main loop waits until the clock is 45 quarter-rows or more */
#define BINT_CD_STORY 3
#define BINT_CD_CREDITS 4
#define BINT_CD_LEAD 45
/* With MCARD ROL / GM the scores through the MIDI driver, every edition (docs/intro.md, the song files table) */
#define BINT_SCORE_STORY "DATA/M/BINTRO.SCT"
#define BINT_SCORE_CREDITS "DATA/M/BFANF.SCT"
#define BINT_MAP_W 2560
/* The composed picture and the background are strips of 320-byte rows; the view is the 64,000 bytes from the scroll
 * on (docs/intro.md, "Settled 2026-10-20": scene 9 composes in a strip; every other scene has the scroll at 0). */
#define BINT_STRIP (320 * 200 + BINT_MAP_W + 2)

typedef struct {
    uint32_t start; /* rows */
    int stars;
    int n; /* list length (background first), 0 for the flash and the end */
    int16_t list[BINT_LIST_MAX]; /* resource numbers, signs dropped (they mean nothing) */
} BintEntry;

typedef struct {
    uint32_t time; /* rows; 1 = right after the previous entry; 0xFFFFFFFF ends the table */
    const uint8_t *text; /* NULL clears both lines */
    int line2;
} BintCaption;

/* Part 1's caption step (docs/intro.md, "What the engine must change (INT-15)"): takes at most one table entry per
 * main-loop pass - the next one when due (time 1 always, 0xFFFFFFFF never, else rows >= time) - into line[]; returns
 * 1 when it took one (the strip is redrawn), 0 otherwise. */
int bint_caption_step(const BintCaption *caps, int ncaps, int *next, uint32_t rows, const uint8_t *line[2]);

/* The tables of a BINT.EXE image (German/disk: program offset = file - 0x11C0, script 0x46F4, captions 0x4548,
 * motion 0x4A00, character map 0x4458; English CD: file - 0x1130, every table 0x140 higher). */
typedef struct {
    BintEntry script[BINT_ENTRIES];
    int16_t motion[BINT_MOTIONS][11];
    int nmotion;
    BintCaption caps[BINT_CAPTIONS];
    int ncaps;
    const uint8_t *map; /* 256 bytes */
    int english;
} BintTables;

int bint_parse_tables(const uint8_t *exe, size_t n, BintTables *t);

/* The loader's step after reading a module file (docs/intro.md, OTH-EN-5): the English loader adds 0x80 to every byte
 * of a file that starts with 0xC2 (the disk / German one has no such step); then the length is cut to the header's
 * sections (0x40 + the four sizes) when the file is longer, so a trailing byte is never read. Returns the length. */
size_t bint_module_prepare(uint8_t *d, size_t n, int english);
/* The song files (k 0 story, 1 credits): DATA/N/BINTROPA.SCT and DATA/N/BFANF.SCT, on the English CD .RAW. */
const char *bint_song_file(int english, int k);
/* The module branch's tempo: the header's big-endian word at +0x22, or `fallback` for a file shorter than the header
 * word (docs/intro.md, INT-14: BFANF 195, BINTROPA 144). */
int bint_module_tempo(const uint8_t *d, size_t n, int fallback);

/* Clock: one 50 Hz tick adds 4 x tempo to the accumulator, the counter goes up by one per 187. */
void bint_clock_tick(uint32_t *clock, uint32_t *acc, int tempo);
/* A value of a motion record: v0 + trunc((v1 - v0) t / (4 (end - start))), 16-bit. */
int bint_interp(int v0, int v1, int t, int start, int end);
/* The frame of a flat or orbit record (frames, period): obj + (t div (4 period)) mod frames; frames 0 = obj, 1 = obj
 * while the quotient is even, else -1 (not shown). */
int bint_frame(int obj, int frames, int period, int t);
/* Projection of a w x h sprite at (x, y, z) with the eye at distance 300: left, right, top. */
void bint_project(int x, int y, int z, int w, int h, int *left, int *right, int *top);
/* BINT's own orbit table (docs/intro.md, "the orbit table (INT-13)"), 321 words: the five writes per i = 0 ... 64 in
 * BINT's order, so the third quarter slips one step (T[128 + j] = -q[j + 1]); RELO keeps intro_sine_circle. */
void bint_orbit_table(const int16_t q[65], int16_t out[321]);
/* The orbit position: k = floor(3t/2) mod 256, x = cx - floor(ax T[k] / 65536), y = cy + floor(ay T[k + 64] / 65536). */
void bint_orbit(int cx, int cy, int ax, int ay, int t, int *x, int *y);
/* The PHAS frame index of the two PBM scenes from rows: N/011 trunc((rows - 970) x 8 / 60) below 29, N/012
 * trunc((rows - 1943) x 8 / 34) below 56; -1 while rows is below the start (the index is not computed). */
int bint_phas_index(int which, uint32_t rows);
/* Lays a caption string out into a 320 x 24 strip row (line 1 at row 0, line 2 at row 11): advance = glyph width
 * - 1, a space 4; a pixel v drawn as v + 15, v = 15 as 255 (pbm: v = 15 as colour 1, v 1-14 still v + 15). */
void bint_caption_draw(uint8_t *strip, const uint8_t *text, int line2, const Font *f, const uint8_t map[256], int pbm);

typedef struct {
    int x; /* 1/64 pixel */
    int speed; /* 2-9 */
    uint8_t colour; /* 17-19 */
} BintStar;

/* A decoded PHAS stream: the frame starts (by stream position) and their references. */
typedef struct {
    const uint8_t *d;
    size_t n;
    int count;
    uint32_t start[128]; /* data start of each frame in file order */
    int number[128], ref[128];
    int wide; /* 2-byte run counts (N/011) */
} BintPhas;

int bint_phas_parse(BintPhas *p, const uint8_t *d, size_t n, int wide);
/* Draws stream frame k (0-based, file order; a reference draws the referenced frame's data) onto a 640-wide buffer,
 * rows relative to the picture plus top. */
void bint_phas_draw(const BintPhas *p, int k, uint8_t *buf, int top, int rows);
/* IFF PBM: BMHD, CMAP, BODY (ByteRun1, up to 256,000 bytes) into out[640 x 400]; pal gets CMAP (768, 8-bit). */
int bint_pbm_decode(const uint8_t *d, size_t n, uint8_t *out, uint8_t pal[768]);

enum { BINT_RUN, BINT_FLASH, BINT_PAUSE, BINT_DONE };

/* A dirty rectangle of the 320 x 200 composition: linear offset of the first visible pixel, width, rows. */
typedef struct {
    int off, w, rows;
} BintRect;

/* The flat drawer (types 2 and 4, the backgrounds; docs/intro.md, "Type 2"): draws a w x h sprite at (x, y) into a
 * 320 x 200 buffer, clipped on the left only; a sprite with x + w > 320 is not drawn at all (returns 0). Rows outside
 * 0-199 are not written (the original does not clip them; engine: nothing outside the buffer). On a draw, *last gets
 * the offset of the first visible pixel, the visible width and h rows. transparent: colour 0 not drawn. */
int bint_draw_flat(uint8_t *px, const HSprite *s, int x, int y, int transparent, BintRect *last);
/* The scaled drawer (type 3): projection of (x, y, z), step trunc(256 w / W) / 256 (W = |right - left|, W = 0 gives
 * step 16), drawn from left, rows 0-198 and columns 0-319, colour 0 transparent; skipped (returns 0) when left and
 * left + W are both >= 320 as unsigned values. On a draw, *last gets the offset of the first drawn pixel, the visible
 * columns of the last drawn row and the drawn rows. */
int bint_draw_scaled(uint8_t *px, const HSprite *s, int x, int y, int z, BintRect *last);
/* Restores a rectangle from src into dst, width rounded up to even, linear (a width past the row end runs on into
 * the next row, as offsets do), pixels outside 0 ... 63,999 skipped. */
void bint_restore(uint8_t *dst, const uint8_t *src, BintRect r);
/* The same within a strip of n bytes (offsets are strip offsets). */
void bint_restore_n(uint8_t *dst, const uint8_t *src, BintRect r, int n);

typedef struct {
    uint8_t *exe;
    size_t exe_n;
    BintTables t;
    Font font;
    struct Sound *sound;
    HSprite *spr[BINT_RES];
    uint8_t *pbm_raw[3];
    size_t pbm_n[3];
    uint8_t *phas_raw[3];
    size_t phas_n[3];
    BintPhas phas[3]; /* [1] = N/011, [2] = N/012 */
    uint8_t *map801;
    size_t map801_n;
    uint8_t *song[2];
    size_t song_n[2];

    int state;
    int part; /* 1 story, 2 credits */
    uint64_t unit;
    uint32_t clock, acc; /* quarter-rows */
    int tempo;
    int next_entry; /* the next script entry to run */
    int entry; /* the scene shown, -1 none */
    int vesa; /* the scene is a PBM picture in 640 x 480 */
    int stars_on;
    BintStar star[BINT_STARS];
    uint32_t seed;
    int scroll; /* scene 9: map column at the left edge */
    int phas_index; /* never reset */
    int phas_drawn; /* stream frames drawn in this PBM scene */
    int next_caption;
    const uint8_t *line[2];
    int wait; /* retraces left in the flash or the pause */
    int flash_step;
    uint8_t dac[768];
    /* the strip offset a star was drawn on, -1 none: two lists swapped by the show like the rectangle lists, so a
     * star's pixel is restored two passes after it was drawn */
    int star_at[2][BINT_STARS];
    int strip_restore; /* passes left that restore the caption area from the background */
    int cd_hold; /* CD mode, part 1: no main-loop pass until the clock reaches BINT_CD_LEAD (then clock 0) */
    /* the 320 x 200 scenes (docs/intro.md, "Screen updates"): each pass shows the picture composed in the previous
     * pass, then restores the rectangles recorded two passes ago from the background and draws the objects */
    uint8_t screen[320 * 200]; /* what the player sees */
    int view; /* the strip offset of the view: the map columns written so far in scene 9, else 0 */
    uint8_t comp[BINT_STRIP]; /* the persistent composed picture (a strip) */
    BintRect rect[2][BINT_MOTIONS];
    int nrect[2], cur; /* the two rectangle lists; cur = the one restored and written this pass */
    BintRect last; /* the drawers' shared variables: the rectangle drawn last */
    uint8_t bg[BINT_STRIP]; /* background of a mode-13h scene (a strip) */
    uint8_t pbm[640 * 440]; /* the PBM scenes: the picture from row 20 */
    uint8_t strip[320 * 24]; /* captions */
    int done;
} Bint;

/* Opens BINT (loads every resource the script names; the stars seeded with seed). */
int bint_open(Bint *b, const char *root, struct Sound *sound, uint32_t seed);
void bint_free(Bint *b);
/* Part 1 from row 0 (bint_open calls it; tests call it on tables they built). */
void bint_start(Bint *b);
void bint_advance(Bint *b, IntroInput *in);
void bint_render(const Bint *b, IntroScreen *s);
/* rows of the clock */
static inline uint32_t bint_rows(const Bint *b) { return b->clock / 4; }

#endif
