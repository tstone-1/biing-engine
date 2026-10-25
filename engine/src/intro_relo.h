/* RELO.EXE, the Magic Bytes and reLINE logos (docs/intro.md, "RELO.EXE - the Magic Bytes and reLINE logos", "What
 * the engine needs (RELO)"): a text-mode credit, the Magic Bytes picture, then 13.52 s on the 50 Hz clock that also
 * drives the music: the flat-shaded 3D flight (phase 1, n = 40 ... 842) and the reLINE logo pieces (phase 2). The
 * scene data (objects, models, palette, start times, target rectangles) are loaded from the user's RELO.EXE; the
 * sprites are DATA/L/I00-I18 and the music DATA/REFANF. */
#ifndef BIING_INTRO_RELO_H
#define BIING_INTRO_RELO_H

#include "intro.h"
#include "music.h"

struct Sound;

#define RELO_W 320
#define RELO_H 200
#define RELO_N_START 40
#define RELO_N_LAST 1390 /* the loop runs while n <= 1390, so it ends at n = 1392 */
#define RELO_PIECES 16
#define RELO_CD_TRACK 2 /* with MCARD CD and the tracks: CD track 2 from the music start to the animation's end */
#define RELO_SCORE "DATA/M/REFANF.SCT" /* with MCARD ROL / GM: the score through the MIDI driver (docs/music.md) */

/* ---- phase 1: the 3D flight ---------------------------------------------------------------- */

typedef struct {
    int z, y, pitch, roll; /* camera z and y, pitch a1 and roll a3 (x and yaw a2 are 0) */
    int single; /* from n = 650: the one-quad runway (model 8) without culling */
} ReloCamera;

typedef struct { int colour, model, x, y, z; } ReloObject;
typedef struct {
    ReloObject objects[17], single;
    int models[9][4][3];
    uint8_t palette[24];
    int piece_start[RELO_PIECES];
    int rect[RELO_PIECES + 1][4];
} ReloData;

/* Parses the three supported program layouts, bounds-checking every data pointer.
 * The result owns its values; no original image bytes or pointers are retained. */
int relo_parse_data(const uint8_t *exe, size_t n, ReloData *out);

void relo_camera(int n, ReloCamera *c);
/* sin of a 256-step angle from the program's quarter table (Q15). */
int relo_sin(int a);
/* One vertex: object position + vertex - camera, rotated by pitch, yaw, roll (Q15 products doubled, sums >> 16,
 * kept as 16 bits) and projected (d = Z + 50, 1 when <= 0; 160 + 50 X / d, 100 + 50 Y / d, truncating). */
void relo_project(int vx, int vy, int vz, int ox, int oy, int oz, const ReloCamera *c, int *sx, int *sy);
/* Fills one polygon of n vertices (screen coordinates) with colour into a 320 x 200 buffer by the edge-walker rules
 * of docs/intro.md, "Polygon fill": rows from the top vertex to 198 at most, columns clipped to 1 ... 317, spans with
 * right < left not drawn. */
void relo_fill(uint8_t *buf, const int *xs, const int *ys, int n, uint8_t colour);
/* An object of the 17 (n < 650) is skipped when its origin's z - camera z < -67. */
int relo_culled(int oz, int camz);
/* The whole phase-1 picture for counter n (black background, objects in list order). */
void relo_scene(const ReloData *data, int n, uint8_t *buf);

/* ---- phase 2: the pieces ------------------------------------------------------------------- */

typedef struct {
    int left, top, right, w_drawn; /* placement; w_drawn = |right - left| */
    int step; /* 8.8 source pixels per screen pixel */
    int visible; /* left or right inside 0 ... 319 */
} ReloPlace;

/* Placement of a w x h sprite whose target rectangle is (x1, y1, x2, y2) at depth Z. */
void relo_place(int w, int h, int x1, int y1, int x2, int y2, int z, ReloPlace *p);
/* Draws an L sprite's pixels (bit-reversed, masked with 0x1F, 0 not drawn) at a placement. */
void relo_draw_sprite(uint8_t *buf, const HSprite *s, const ReloPlace *p);

/* ---- the program --------------------------------------------------------------------------- */

enum { RELO_CREDIT, RELO_LOGO, RELO_LOGO_RELEASE, RELO_ANIM, RELO_TAIL, RELO_DONE };

typedef struct {
    ReloData data;
    HSprite spr[19];
    uint8_t *music_data;
    size_t music_n;
    struct Sound *sound; /* the engine's sound (music player and mixer); NULL = silent */
    int stage;
    uint64_t unit;
    int wait; /* retraces or ticks left in the current stage */
    int n; /* animation counter */
    int phase2; /* the plate has been drawn */
    uint8_t back[RELO_W * RELO_H]; /* phase 2 background: plate and landed pieces */
    uint8_t screen[RELO_W * RELO_H];
    uint8_t dac[768];
    int next_piece, flying; /* the next piece to start; the piece in flight, -1 none */
    int done;
} Relo;

int relo_open(Relo *r, const char *root, struct Sound *sound);
void relo_free(Relo *r);
void relo_advance(Relo *r, IntroInput *in);
void relo_render(const Relo *r, IntroScreen *s);
/* One animation frame for counter n (phase-2 pieces advance as one frame per tick does). */
void relo_frame(Relo *r, int n);

#endif
