#include "dulog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HELP_NORMAL 1278
#define HELP_HOVER 873
#define HELP_DY 39 /* the button is drawn and hit-tested 39 rows below its IXY position */
#define STRIP_Y 400

void dulog_init(Dulog *d, const Game *g, const Program *p, Surface *s, RoutineFn call, void *user) {
    memset(d, 0, sizeof *d);
    d->game = g;
    d->prog = p;
    d->screen = s;
    d->call = call;
    d->user = user;
    d->help_enabled = 0; /* only the advice routine 0x5B47B switches it on (docs/screens.md) */
    d->help_shown = -1;
}

void dulog_free(Dulog *d) {
    for (int i = 0; i < IXY_COUNT; i++)
        if (d->cache[i]) {
            image_free(d->cache[i]);
            free(d->cache[i]);
        }
}

const Image *dulog_image(Dulog *d, int index) {
    if (index <= 0 || index >= IXY_COUNT) return NULL;
    if (!d->cache[index]) {
        Image *img = malloc(sizeof *img);
        if (!img || image_load(d->game, index, img)) {
            fprintf(stderr, "[ERROR] %s\n", img ? asset_error() : "out of memory");
            free(img);
            return NULL;
        }
        d->cache[index] = img;
    }
    return d->cache[index];
}

static Rect image_rect(const Image *img, int dx, int dy) {
    return (Rect){img->x + dx, img->y + dy, img->x + dx + img->w, img->y + dy + img->h};
}

static int overlap(Rect a, Rect b) {
    return a.x0 <= a.x1 && b.x0 <= b.x1 && a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1;
}

static const Image *draw_raw(Dulog *d, int index, int dx, int dy) {
    if (d->load_hook) d->load_hook(d->load_ctx, index);
    const Image *img = dulog_image(d, index);
    if (img) blit(d->screen, img, img->x + dx, img->y + dy, image_is_transparent(img));
    return img;
}

void dulog_draw_quiet(Dulog *d, int index, int dx, int dy) { draw_raw(d, index, dx, dy); }

void dulog_draw(Dulog *d, int index, int dx, int dy) {
    const Image *img = draw_raw(d, index, dx, dy);
    if (img && overlap(image_rect(img, dx, dy), d->hover_rect)) {
        d->hovered = HOVER_NONE; /* found again next frame, which redraws the hover image on top */
    }
}

static void draw_hover_image(Dulog *d, int index, int dx, int dy) {
    const Image *img = draw_raw(d, index, dx, dy);
    if (img) d->hover_rect = image_rect(img, dx, dy);
}

void dulog_set_screen(Dulog *d, int screen) { d->current = screen; }

void dulog_clear_strip(Dulog *d) {
    d->hovered = HOVER_NONE;
    memset(d->screen->px + STRIP_Y * d->screen->w, 0, (size_t)(d->screen->h - STRIP_Y) * d->screen->w);
}

static const Script *script(const Dulog *d) {
    return d->current > 0 && d->current < SCREEN_COUNT ? &d->prog->screens[d->current] : NULL;
}

int dulog_hit(const Dulog *d, const Record *r, int x, int y) {
    if (r->hover <= 0 || r->hover >= IXY_COUNT) return 0;
    const IxyEntry *e = &d->game->ixy[r->hover];
    int x0 = e->x + r->ax, y0 = e->y + r->ay;
    return x0 < x && x <= x0 + e->w + r->aw && y0 < y && y <= y0 + e->h + r->ah;
}

static int help_hit(const Dulog *d, int x, int y) {
    const IxyEntry *e = &d->game->ixy[HELP_NORMAL];
    return e->x < x && x <= e->x + e->w && e->y + HELP_DY < y && y <= e->y + HELP_DY + e->h;
}

/* Hotspots in search order, honouring op 8: a condition whose flag is 0 hides the next record. */
static const Record *find_hit(const Dulog *d, int x, int y) {
    const Script *s = script(d);
    if (!s) return NULL;
    for (int i = 0; i < s->count; i++) {
        const Record *r = &s->rec[i];
        if (r->op == OP_CONDITION) {
            if (!d->flags[r->flag & 0xFF]) i++;
            continue;
        }
        if (r->op == OP_HOTSPOT && dulog_hit(d, r, x, y)) return r;
    }
    return NULL;
}

static const Record *find_id(const Dulog *d, unsigned id) {
    const Script *s = script(d);
    for (int i = 0; s && i < s->count; i++)
        if (s->rec[i].op == OP_HOTSPOT && s->rec[i].id == id) return &s->rec[i];
    return NULL;
}

static void call(Dulog *d, uint32_t routine) {
    if (routine && d->call) d->call(d, routine);
}

static void hover_update(Dulog *d, int x, int y) {
    if (d->hovered == HOVER_HELP) {
        if (help_hit(d, x, y)) return;
        dulog_draw(d, HELP_NORMAL, 0, HELP_DY);
        d->hovered = HOVER_NONE;
    } else if (d->hovered != HOVER_NONE) {
        const Record *r = find_id(d, d->hovered);
        if (r && dulog_hit(d, r, x, y)) return;
        d->hovered = HOVER_NONE;
        if (r) {
            if (r->normal) dulog_draw(d, r->normal, r->normal_dx, r->normal_dy);
            call(d, r->leave);
        }
    }
    const Record *r = find_hit(d, x, y);
    if (r) {
        if (!r->enter && r->hover) draw_hover_image(d, r->hover, r->hover_dx, r->hover_dy);
        call(d, r->enter);
        d->hovered = r->id;
        return;
    }
    if (d->help_enabled && help_hit(d, x, y)) {
        dulog_draw(d, HELP_HOVER, 0, HELP_DY);
        d->hovered = HOVER_HELP;
    }
}

static void setup(Dulog *d) {
    if (d->current == d->set_up) return;
    d->hovered = HOVER_NONE;
    d->set_up = d->current;
    const Script *s = script(d);
    for (int i = 0; s && i < s->count; i++)
        if (s->rec[i].op == OP_DRAW) dulog_draw(d, s->rec[i].image, s->rec[i].dx, s->rec[i].dy);
}

static void help_strip(Dulog *d) {
    if (d->help_enabled == d->help_shown) return;
    d->help_shown = d->help_enabled;
    if (d->help_enabled) {
        dulog_draw(d, HELP_NORMAL, 0, HELP_DY);
    } else {
        memset(d->screen->px + STRIP_Y * d->screen->w, 0, (size_t)(d->screen->h - STRIP_Y) * d->screen->w);
    }
}

static void click(Dulog *d) {
    if (!d->click_pending) return;
    d->click_pending = 0;
    hover_update(d, d->click_x, d->click_y); /* a click resolves where the button went down */
    if (d->hovered == HOVER_NONE) return;
    if (d->hovered == HOVER_HELP) {
        call(d, 0x5B4D1); /* the routine the game installs for the "Hilfe!" button */
        return;
    }
    const Record *r = find_id(d, d->hovered);
    if (!r) return;
    if (r->target) dulog_set_screen(d, r->target);
    else call(d, r->click);
}

void dulog_frame(Dulog *d) {
    setup(d);
    help_strip(d);
    hover_update(d, d->mouse_x, d->mouse_y);
    click(d);
    const Script *s = script(d);
    if (s) call(d, s->frame);
}
