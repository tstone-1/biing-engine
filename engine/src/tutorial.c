/* ENCODE.EXE, the CD video tutorial (tutorial.h). Written from docs/intro.md, "ENCODE.EXE - the CD video tutorial", and
 * docs/cdrom.md, "Video tutorial: VPX/". */
#include "tutorial.h"
#include "intro_pre.h"
#include "sound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TSCR_W 640
#define TSCR_H 480
#define FRAME_W (VPX_W + 8)
#define FRAME_H (VPX_H + 8)

/* ---- the pieces ---- */

int tutorial_buttons(int scene, int english, TutButton out[6]) {
    static const TutButton PREVIOUS = {TUT_CMD_PREVIOUS, 0, 1, 20, 330};
    static const TutButton NEXT = {TUT_CMD_NEXT, 2, 3, 112, 330};
    static const TutButton GAME = {TUT_CMD_GAME, 4, 5, 214, 330};
    static const TutButton DOS = {TUT_CMD_QUIT, 6, 7, 214, 385};
    static const TutButton EXPLAIN = {TUT_CMD_NEXT, 8, 9, 164, 330}; /* "Video-Erklärung" = next */
    static const TutButton NO_VIDEO = {TUT_CMD_GAME, 10, 11, 396, 330}; /* "Spiel ohne Videos" = game */
    int n = 0;
    if (scene == 1) {
        out[n++] = EXPLAIN;
        out[n++] = NO_VIDEO;
        return n;
    }
    if (scene >= 3) out[n++] = PREVIOUS;
    if (!(english && scene == TUT_SCENES)) out[n++] = NEXT;
    out[n++] = GAME;
    out[n++] = DOS;
    return n;
}

int tutorial_hit(const TutButton *b, int n, int x, int y) {
    for (int i = 0; i < n; i++)
        if (x > b[i].x && x <= b[i].x + TUT_BUTTON_W && y > b[i].y && y <= b[i].y + TUT_BUTTON_H) return i;
    return -1;
}

void tutorial_video_pos(int scene, int *x, int *y) {
    *x = scene == 1 ? 160 : 316;
    *y = scene == 1 ? 24 : 236;
}

int tutorial_pointer_xmax(int scene) { return scene == 1 ? 639 : 311; }

void tutorial_pointer_move(int *qx, int *qy, int mx, int my, int xmax) {
    /* the quarter-pixel clamps: x 0 .. 4 (xmax + 1) - 1, y 1,064 .. 1,919 */
    int x = *qx + 2 * mx, y = *qy + 2 * my;
    if (x < 0) x = 0;
    if (x > 4 * (xmax + 1) - 1) x = 4 * (xmax + 1) - 1;
    if (y < TUT_PTR_YMIN * 4) y = TUT_PTR_YMIN * 4;
    if (y > 4 * (TUT_PTR_YMAX + 1) - 1) y = 4 * (TUT_PTR_YMAX + 1) - 1;
    *qx = x, *qy = y;
}

int tutorial_halves(uint64_t bytes, int fps) {
    /* 25 fps: 50 half-frames per 22,000 bytes = one per 440; 15 fps: 30 per 22,000 = one per 733.33; the threshold
     * starts at 0, so the first half-frame is due at once */
    return (fps == 25 ? (int)(bytes / 440) : (int)(bytes * 3 / 2200)) + 1;
}

void tutorial_clock_step(int *clock, int *w, int fps) {
    ++*clock;
    if (fps == 25) return;
    if (*w < 3) ++*clock, *w += 3;
    *w -= 2;
}

void tutorial_draw_frame(uint8_t *screen, int x, int y) {
    static const uint8_t RING[4] = {2, 0x0C, 0, 0};
    for (int r = 0; r < FRAME_H; r++) {
        int sy = y + r;
        if (sy < 0 || sy >= TSCR_H) continue;
        for (int c = 0; c < FRAME_W; c++) {
            int sx = x + c;
            if (sx < 0 || sx >= TSCR_W) continue;
            int d = r < c ? r : c;
            if (FRAME_H - 1 - r < d) d = FRAME_H - 1 - r;
            if (FRAME_W - 1 - c < d) d = FRAME_W - 1 - c;
            screen[sy * TSCR_W + sx] = d < 4 ? RING[d] : VPX_FILL;
        }
    }
}

void tutorial_draw_icon(uint8_t *screen, const uint8_t *icondat, int i, int x, int y) {
    const uint8_t *src = icondat + (size_t)(i / 8) * TSCR_W * TUT_BUTTON_H + (i % 8) * TUT_BUTTON_W;
    for (int r = 0; r < TUT_BUTTON_H; r++) {
        if (y + r < 0 || y + r >= TSCR_H) continue;
        for (int c = 0; c < TUT_BUTTON_W; c++)
            if (x + c >= 0 && x + c < TSCR_W) screen[(y + r) * TSCR_W + x + c] = src[r * TSCR_W + c];
    }
}

#define SUB_SPACE 6
#define SUB_GAP 1
#define SUB_BAND_X 320
#define SUB_BAND_Y 190
#define SUB_BAND_H 15
#define SUB_TEXT_Y 192
#define SUB_COPY_FROM 206 /* backdrop rows 206-221 ... */
#define SUB_COPY_TO 220 /* ... replace video rows 220-235 */
#define SUB_COPY_ROWS 16

static int glyph_slot(const uint8_t map[256], uint8_t c) { return map[c]; }

int tutorial_text_width(const Font *f, const uint8_t map[256], const char *s) {
    int w = 0;
    for (; *s; s++) {
        uint8_t c = (uint8_t)*s;
        if (c == ' ') {
            w += SUB_SPACE;
            continue;
        }
        int slot = glyph_slot(map, c);
        w += slot < 128 ? f->width[slot] + SUB_GAP : SUB_GAP;
    }
    return w;
}

static void draw_text(uint8_t *dst, const Font *f, const uint8_t map[256], const char *s, int y) {
    int w = tutorial_text_width(f, map, s);
    int d = SUB_BAND_X - w;
    int x = SUB_BAND_X + (d >= 0 ? d / 2 : -((-d + 1) / 2)); /* centred in 320 pixels, rounded down */
    for (; *s; s++) {
        uint8_t c = (uint8_t)*s;
        if (c == ' ') {
            x += SUB_SPACE;
            continue;
        }
        int slot = glyph_slot(map, c);
        if (slot >= 128) {
            x += SUB_GAP;
            continue;
        }
        const uint8_t *g = f->glyph[slot];
        int gw = f->width[slot];
        for (int r = 0; g && r < f->height; r++) {
            if (y + r < 0 || y + r >= TSCR_H) continue;
            for (int k = 0; k < gw; k++) {
                uint8_t v = g[r * gw + k];
                if (v && x + k >= 0 && x + k < TSCR_W) dst[(y + r) * TSCR_W + x + k] = v;
            }
        }
        x += gw + SUB_GAP;
    }
}

/* ---- the program ---- */

static void put_video(Tutorial *t) {
    int vx, vy;
    tutorial_video_pos(t->scene, &vx, &vy);
    /* the band shows while the current line's first byte is at least 0x21 (a " " line shows none) */
    int sub = t->sub_line[1] >= 0 && (uint8_t)t->subs.text[t->sub_line[1]][0] >= 0x21;
    for (int r = 0, src_row = 0; r < VPX_H; r++) {
        const uint8_t *src = t->video + src_row * VPX_W;
        if (sub && r >= SUB_COPY_TO && r < SUB_COPY_TO + SUB_COPY_ROWS)
            src = t->back + (r - SUB_COPY_TO + SUB_COPY_FROM) * TSCR_W + SUB_BAND_X;
        else src_row++; /* the video source stays on row 220 while the band is copied: rows 236-239 show 220-223 */
        memcpy(t->screen + (vy + r) * TSCR_W + vx, src, VPX_W);
    }
}

static void draw_buttons(Tutorial *t) {
    for (int i = 0; i < t->nbuttons; i++) {
        const TutButton *b = &t->button[i];
        tutorial_draw_icon(t->screen, t->icondat, i == t->hover ? b->hover : b->normal, b->x, b->y);
    }
}

static void pointer_off(Tutorial *t);

/* Hover: the pointer taken off, then the newly hovered button drawn with its hover image and the previous one
 * restored (only those two); the next poll draws the pointer again, moved or not (measured 2026-10-20 against
 * tools/compare_tutorial.py's pointer and click cases: the pointer vanishes for the rest of the tick, no piece of
 * the other image stays behind). */
static void set_hover(Tutorial *t, int h) {
    if (h == t->hover) return;
    int old = t->hover;
    t->hover = h;
    pointer_off(t);
    if (old >= 0) tutorial_draw_icon(t->screen, t->icondat, t->button[old].normal, t->button[old].x, t->button[old].y);
    if (h >= 0) tutorial_draw_icon(t->screen, t->icondat, t->button[h].hover, t->button[h].x, t->button[h].y);
}

/* The pointer in the screen: put back the saved bytes ... */
static void pointer_off(Tutorial *t) {
    if (!t->shown) return;
    for (int r = 0; r < t->uh; r++) memcpy(t->screen + (t->uy + r) * TSCR_W + t->ux, t->under + r * TUT_PTR_W, (size_t)t->uw);
    t->shown = 0;
}

/* ... and draw it at its position: the bytes under it saved, clipped at x below xmax + 1 and y below 480. */
static void pointer_on(Tutorial *t) {
    int xlim = tutorial_pointer_xmax(t->scene) + 1;
    t->ux = t->px, t->uy = t->py, t->uimg = t->img;
    t->uw = xlim - t->px < TUT_PTR_W ? xlim - t->px : TUT_PTR_W;
    t->uh = TSCR_H - t->py < TUT_PTR_H ? TSCR_H - t->py : TUT_PTR_H;
    if (t->uw < 0) t->uw = 0;
    if (t->uh < 0) t->uh = 0;
    const uint8_t *img = t->pointdat + (size_t)(t->img & 1) * 2048; /* busy (bit 1) only while loading: never here */
    for (int r = 0; r < t->uh; r++) {
        uint8_t *row = t->screen + (t->py + r) * TSCR_W + t->px;
        memcpy(t->under + r * TUT_PTR_W, row, (size_t)t->uw);
        for (int c = 0; c < t->uw; c++)
            if (img[r * TUT_PTR_W + c]) row[c] = img[r * TUT_PTR_W + c];
    }
    t->shown = 1;
}

/* A poll (every second tick): the motion read and clamped, the pointer redrawn if its position or image changed (or
 * it is off after a scene start), then the button: its state becomes the image's held bit, and a poll with it down
 * latches a press (so a held button latches again after each command). */
static void poll(Tutorial *t, IntroInput *in) {
    int mx = in ? in->mx : 0, my = in ? in->my : 0;
    if (in) in->mx = in->my = 0;
    tutorial_pointer_move(&t->qx, &t->qy, mx, my, tutorial_pointer_xmax(t->scene));
    int x = t->qx / 4, y = t->qy / 4;
    if (!t->shown || x != t->px || y != t->py || t->img != t->uimg) {
        pointer_off(t);
        t->px = x, t->py = y;
        pointer_on(t);
    }
    t->held = in && in->left;
    t->img = t->held;
    if (t->held) t->latched = 1;
}

static void free_scene(Tutorial *t) {
    vpx_free(&t->vpx);
    free(t->vpx_raw);
    free(t->dlt_raw);
    t->vpx_raw = t->dlt_raw = NULL;
    memset(&t->dlt, 0, sizeof t->dlt);
}

int tutorial_scene(Tutorial *t, int scene) {
    free_scene(t);
    t->scene = scene;
    /* 1. backdrop and its palette (6-bit) */
    memcpy(t->dac, t->backdat, 768);
    memcpy(t->screen, t->backdat + 768, sizeof t->screen);
    memcpy(t->back, t->screen, sizeof t->back);
    /* 2. buttons */
    t->nbuttons = tutorial_buttons(scene, t->english, t->button);
    t->hover = -1;
    t->shown = 0; /* the pointer was taken off; the first poll draws it again */
    draw_buttons(t);
    /* 3. the screen deltas, scenes 2-13 */
    char rel[64];
    if (scene >= 2) {
        snprintf(rel, sizeof rel, "VPX/PSCEN%02d.DLT", scene);
        if (intro_read(t->root, rel, &t->dlt_raw, &t->dlt_n) || dlt_parse(&t->dlt, t->dlt_raw, t->dlt_n)) {
            free_scene(t);
            return -1;
        }
    }
    /* 4. frame, first record, video buffer */
    int vx, vy;
    tutorial_video_pos(scene, &vx, &vy);
    tutorial_draw_frame(t->screen, vx - 4, vy - 4);
    if (scene >= 2) dlt_apply(t->dlt.rec[0], t->dlt.rec_n[0], t->screen);
    memset(t->video, VPX_FILL, sizeof t->video);
    /* 5. the video and its palette (8-bit, shifted right by 2) */
    snprintf(rel, sizeof rel, "VPX/VSCEN%c%02d.VPX", t->quad ? 'Q' : 'D', scene);
    if (intro_read(t->root, rel, &t->vpx_raw, &t->vpx_n) || vpx_parse(&t->vpx, t->vpx_raw, t->vpx_n)) {
        free_scene(t);
        return -1;
    }
    for (int i = 0; i < 768; i++) t->dac[i] = (uint8_t)(t->vpx.pal[i] >> 2);
    /* 6. clocks to 0 */
    t->units = t->bytes = t->audio_out = 0;
    t->halves = t->decoded = t->clock = t->next_dlt = t->next_sub = t->frozen = 0;
    t->sub_line[0] = t->sub_line[1] = t->sub_line[2] = -1;
    return 0;
}

int tutorial_open(Tutorial *t, const char *root, struct Sound *sound, int quad, int scene) {
    memset(t, 0, sizeof *t);
    snprintf(t->root, sizeof t->root, "%s", root);
    t->sound = sound;
    t->quad = quad;
    t->fps = quad ? 25 : 15;
    t->tick_units = quad ? INTRO_U50 : INTRO_UNITS / 30; /* timer 50 Hz at 25 fps, 30 Hz at 15 fps */
    t->sound_clock = sound && sound->dcard != 0;
    t->qx = TUT_PTR_START_X * 4, t->qy = TUT_PTR_START_Y * 4;
    t->px = TUT_PTR_START_X, t->py = TUT_PTR_START_Y;
    size_t n;
    if (intro_read(root, "VPX/BACKDAT", &t->backdat, &n) || n < TUT_BACKDAT_SIZE ||
        intro_read(root, "VPX/ICONDAT", &t->icondat, &n) || n < TUT_ICONDAT_SIZE ||
        intro_read(root, "VPX/POINTDAT", &t->pointdat, &n) || n < TUT_POINTDAT_SIZE) {
        asset_fail("the tutorial's VPX/BACKDAT, ICONDAT or POINTDAT is missing or short");
        tutorial_free(t);
        return -1;
    }
    /* The English copy (CDC "English texts") has subtitles in scene 13 and no "next" there. */
    uint8_t *cdc;
    if (intro_read(root, "CDC", &cdc, &n) == 0) {
        for (size_t i = 0; i + 7 <= n && !t->english; i++)
            if (!memcmp(cdc + i, "English", 7)) t->english = 1;
        free(cdc);
    }
    if (t->english) {
        uint8_t *txt, *exe;
        size_t tn, en;
        char path[1200];
        snprintf(path, sizeof path, "%s/DATA/ZEI1.FNT", root);
        if (intro_read(root, "VPX/TEXT13.TXT", &txt, &tn) == 0) {
            if (subtitles_parse(&t->subs, txt, tn) == 0) t->have_subs = 1;
            free(txt);
        }
        /* engine reading: the character map is PRE.EXE's (docs/intro.md, PRE "Text"; same glyph order in all fonts) */
        PreText pt;
        int have_map = 0;
        if (t->have_subs && intro_read(root, "PRE.EXE", &exe, &en) == 0) {
            if (pre_find_text(exe, en, 0, &pt) == 0) memcpy(t->map, pt.map, 256), have_map = 1;
            free(exe);
        }
        if (have_map && font_load(&t->font, path) == 0) t->have_font = 1;
        else t->have_subs = 0;
    }
    if (sound) {
        sound->stream = tutorial_audio;
        sound->stream_ctx = t;
    }
    if (tutorial_scene(t, scene < 1 || scene > TUT_SCENES ? 1 : scene)) {
        tutorial_free(t);
        return asset_fail("the tutorial's scene %d is missing", scene);
    }
    return 0;
}

void tutorial_free(Tutorial *t) {
    free_scene(t);
    free(t->backdat);
    free(t->icondat);
    free(t->pointdat);
    t->backdat = t->icondat = t->pointdat = NULL;
    if (t->have_font) font_free(&t->font);
    t->have_font = 0;
    if (t->sound && t->sound->stream_ctx == t) t->sound->stream = NULL, t->sound->stream_ctx = NULL;
}

static void subtitle_mark(Tutorial *t) {
    int i = t->next_sub++;
    t->sub_line[0] = i - 1;
    t->sub_line[1] = i;
    t->sub_line[2] = i + 1 < t->subs.n ? i + 1 : -1;
    for (int r = 0; r < 3 * SUB_BAND_H; r++) /* the three bands cleared, then only the current line drawn */
        memset(t->back + (SUB_BAND_Y + r) * TSCR_W + SUB_BAND_X, VPX_FILL, 320);
    draw_text(t->back, &t->font, t->map, t->subs.text[i], SUB_TEXT_Y + SUB_BAND_H);
}

static void command(Tutorial *t, int cmd) {
    int next = 0;
    switch (cmd) {
    case TUT_CMD_PREVIOUS: next = t->scene > 1 ? t->scene - 1 : 1; break;
    case TUT_CMD_NEXT: next = t->scene + 1; break;
    case TUT_CMD_GAME: t->done = 1; return;
    case TUT_CMD_QUIT: t->done = t->quit = 1; return;
    default: return;
    }
    if (next > TUT_SCENES) { /* next after scene 13 leaves to the game */
        t->done = 1;
        return;
    }
    if (tutorial_scene(t, next)) {
        fprintf(stderr, "[WARN] %s\n", asset_error());
        t->done = 1;
    }
}

void tutorial_tick(Tutorial *t, IntroInput *in) {
    if (t->done) return;
    t->ticks++;
    if (in) in->key = 0; /* keys are read and thrown away */
    /* 1. the half-frames due and the scene clock (the sound position, or one per tick without a sound card) */
    if (!t->frozen) {
        int due = t->halves + 1;
        if (t->sound_clock) {
            t->bytes = t->units * TUT_SOUND_RATE / INTRO_UNITS;
            due = tutorial_halves(t->bytes, t->fps);
        }
        while (t->halves < due) t->halves++, tutorial_clock_step(&t->clock, &t->clock_word, t->fps);
    }
    /* 2. the pointer and the button, every second tick */
    if (!(t->ticks & 1)) poll(t, in);
    /* 3. one frame if at least 3 half-frames are due (else the tick ends here), shown if fewer than 4 remain; the
     * subtitle mark is tested after a shown frame is put */
    if (!t->frozen) {
        if (t->halves - 2 * t->decoded < 3) return;
        if (t->decoded >= t->vpx.nframes) { /* the end of the video acts as "next" */
            command(t, TUT_CMD_NEXT);
            return;
        }
        const VpxFrame *f = &t->vpx.frame[t->decoded];
        if (vpx_decode(t->video, f->code, f->data, f->n, t->decoded & 1) < 0)
            fprintf(stderr, "[WARN] tutorial scene %d frame %d: data cut short\n", t->scene, t->decoded);
        t->decoded++;
        if (t->halves - 2 * t->decoded < 4) {
            put_video(t);
            if (t->have_subs && t->scene == TUT_SCENES && t->next_sub < t->subs.n &&
                t->clock >= t->subs.time[t->next_sub])
                subtitle_mark(t);
        }
    }
    /* 4. hover, a latched press's command (the latch cleared), one DLT record, scene 1's stop test */
    set_hover(t, tutorial_hit(t->button, t->nbuttons, t->px, t->py));
    if (t->latched) {
        t->latched = 0;
        if (t->hover >= 0) {
            command(t, t->button[t->hover].cmd);
            return;
        }
    }
    if (t->scene >= 2 && t->next_dlt < t->dlt.ntimes && t->clock > t->dlt.time[t->next_dlt]) {
        t->next_dlt++;
        dlt_apply(t->dlt.rec[t->next_dlt], t->dlt.rec_n[t->next_dlt], t->screen);
    }
    if (t->scene == 1 && t->clock >= TUT_SCENE1_STOP) t->frozen = 1;
}

void tutorial_advance(Tutorial *t, IntroInput *in) {
    if (t->done) return;
    t->units++;
    if (t->units % (uint64_t)t->tick_units == 0) tutorial_tick(t, in);
}

void tutorial_render(const Tutorial *t, IntroScreen *s) {
    s->w = TSCR_W, s->h = TSCR_H;
    memcpy(s->px, t->screen, sizeof t->screen);
    memcpy(s->dac, t->dac, 768); /* the pointer is in the screen (drawn at the polls) */
}

void tutorial_audio(void *ctx, int16_t *out, int n) {
    Tutorial *t = ctx;
    if (!t || t->done || t->frozen || !t->sound_clock) return;
    for (int i = 0; i < n; i++, t->audio_out++) {
        /* output at 1,000,000 / 46 Hz, source at 22,222 Hz: nearest sample */
        uint64_t src = t->audio_out * TUT_SOUND_RATE * 46 / 1000000;
        int v = src < t->vpx.pcm_n ? t->vpx.pcm[src] : 0x80;
        for (int ch = 0; ch < 2; ch++) {
            int m = out[2 * i + ch] + (v - 128) * 256;
            out[2 * i + ch] = (int16_t)(m > 32767 ? 32767 : m < -32768 ? -32768 : m);
        }
    }
}
