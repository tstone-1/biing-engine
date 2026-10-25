/* The screen layer ("DULOG" in the game): screen scripts, hover, click, the "Hilfe!" strip and the image
 * drawing with hover invalidation. Specified in docs/screens.md, "Per-frame order" to "Click". */
#ifndef BIING_DULOG_H
#define BIING_DULOG_H

#include "gfx.h"
#include "program.h"

#define HOVER_NONE 0
#define HOVER_HELP 0xFFFE

typedef struct Dulog Dulog;
/* Runs a routine of the original by its program offset; the game layer implements the ones it knows. */
typedef void (*RoutineFn)(Dulog *d, uint32_t routine);

typedef struct {
    int x0, y0, x1, y1; /* stored ends are x + width/y + height; touching ends overlap */
} Rect;

struct Dulog {
    const Game *game;
    const Program *prog;
    Surface *screen;
    Image *cache[IXY_COUNT];

    int current; /* screen number set by the game */
    int set_up; /* screen whose setup has run */
    unsigned hovered; /* hotspot id, HOVER_NONE or HOVER_HELP */
    Rect hover_rect; /* last automatic hover bounds; retained across leaves, setup and invalidation */
    int help_enabled, help_shown; /* help_shown: state drawn last, -1 = never */
    uint8_t flags[256]; /* the player flag block that op 8 tests */

    int mouse_x, mouse_y; /* 0-639 x 0-439 */
    int click_pending, click_x, click_y;

    RoutineFn call;
    void *user;
    /* called with the image number before every draw (the blitter's load of the image, docs/screens.md "Image
     * blitter"), for the CD audio's stop on a CD read (sound_resource_read); NULL none */
    void (*load_hook)(void *ctx, int index);
    void *load_ctx;
};

void dulog_init(Dulog *d, const Game *g, const Program *p, Surface *s, RoutineFn call, void *user);
void dulog_free(Dulog *d);

/* Image cache; NULL (and a log line) if the image cannot be loaded. */
const Image *dulog_image(Dulog *d, int index);
/* Draw image at IXY + (dx, dy); invalidates the hover if it overlaps the retained automatic bounds. */
void dulog_draw(Dulog *d, int index, int dx, int dy);
/* The same without hover invalidation. */
void dulog_draw_quiet(Dulog *d, int index, int dx, int dy);
void dulog_set_screen(Dulog *d, int screen);
/* 0xBE21: no hovered hotspot, rows 400-439 (the "Hilfe!" strip) cleared. */
void dulog_clear_strip(Dulog *d);

/* One pass of the per-frame order: setup, help strip, hover, click, the screen's per-frame routine. */
void dulog_frame(Dulog *d);

/* Hit test of one hotspot record at (x, y): left/top exclusive, right/bottom inclusive. */
int dulog_hit(const Dulog *d, const Record *r, int x, int y);

#endif
