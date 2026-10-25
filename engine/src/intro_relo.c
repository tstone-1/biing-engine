/* RELO.EXE, the logos (intro_relo.h). Written from docs/intro.md, "RELO.EXE - the Magic Bytes and reLINE logos". */
#include "intro_relo.h"
#include "assets.h"
#include "sound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- scene assets from the player's executable (docs/intro.md) ---------------------------- */

static unsigned le16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }

static int read_object(const uint8_t *image, size_t size, uint32_t at, ReloObject *o) {
    if (at > size || size - at < 10) return -1;
    const uint8_t *p = image + at;
    o->colour = (int)le16(p), o->model = (int)le16(p + 2);
    o->x = (int16_t)le16(p + 4), o->y = (int16_t)le16(p + 6), o->z = (int16_t)le16(p + 8);
    return o->colour < 8 && o->model > 0 && o->model < 9 ? 0 : -1;
}

int relo_parse_data(const uint8_t *exe, size_t n, ReloData *out) {
    /* Base and table shifts are layout measurements, not scene content. */
    static const struct { size_t base; uint32_t shift; } layouts[] = {
        {0x11C0, 0}, {0x11C0, 0x160}, {0x1130, 0x140}
    };
    memset(out, 0, sizeof *out);
    for (size_t k = 0; k < sizeof layouts / sizeof *layouts; k++) {
        size_t base = layouts[k].base;
        uint32_t sh = layouts[k].shift;
        if (n < base || n - base < 0x43D8u + sh) continue;
        const uint8_t *image = exe + base;
        size_t size = n - base;
        if (le32(image + 0x42B8 + sh + 17 * 4)) continue;
        ReloData t = {0};
        int ok = 1;
        for (int i = 0; i < 18 && ok; i++) {
            ReloObject *o = i == 17 ? &t.single : &t.objects[i];
            uint32_t at = i == 17 ? 0x43AA + sh : le32(image + 0x42B8 + sh + 4 * i);
            if (!at || read_object(image, size, at, o)) { ok = 0; break; }
            uint32_t model = le32(image + 0x43B4 + sh + 4 * o->model);
            if (model > size || size - model < 26 || le16(image + model) != 4) { ok = 0; break; }
            for (int v = 0; v < 4; v++)
                for (int axis = 0; axis < 3; axis++)
                    t.models[o->model][v][axis] = (int16_t)le16(image + model + 2 + 6 * v + 2 * axis);
        }
        if (!ok) continue;
        memcpy(t.palette, image + 0x416A + sh, sizeof t.palette);
        for (int i = 0; i < RELO_PIECES; i++) {
            t.piece_start[i] = (int)le16(image + 0x4296 + sh + 2 * i);
            if (i && t.piece_start[i] < t.piece_start[i - 1]) ok = 0;
        }
        if (!ok) continue;
        for (int i = 0; i <= RELO_PIECES; i++)
            /* The file table starts with I00, then I01 (the plate), then the pieces. */
            for (int j = 0; j < 4; j++) t.rect[i][j] = (int16_t)le16(image + 0x420E + sh + 8 * i + 2 * j);
        *out = t;
        return 0;
    }
    return asset_fail("RELO.EXE: scene data not found or invalid");
}

/* ---- camera, rotation, projection ---------------------------------------------------------- */

static int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

void relo_camera(int n, ReloCamera *c) {
    c->roll = floordiv(n - 200, 2) & 255;
    if (n < 650) {
        c->z = floordiv(n, 2) - 140, c->y = 0, c->pitch = 0, c->single = 0;
        return;
    }
    c->z = 100, c->single = 1;
    if (n <= 802) c->y = -floordiv(n - 650, 4);
    else c->y = -36;
    c->pitch = n <= 778 ? floordiv(n - 650, 2) : 64;
    if (n >= 842) c->roll = 64;
}

int relo_sin(int a) {
    static int16_t circle[256];
    static int built;
    if (!built) intro_sine_circle(intro_quarter(), circle), built = 1;
    return circle[a & 255];
}
static int relo_cos(int a) { return relo_sin(a + 64); }

/* (a x ta x 2 +/- b x tb x 2) >> 16 in 32-bit arithmetic, kept as 16 bits */
static int rot(int a, int ta, int b, int tb, int sub) {
    uint32_t p1 = (uint32_t)((int64_t)a * ta * 2), p2 = (uint32_t)((int64_t)b * tb * 2);
    int32_t s = (int32_t)(sub ? p1 - p2 : p1 + p2);
    return (int16_t)(s >> 16);
}

void relo_project(int vx, int vy, int vz, int ox, int oy, int oz, const ReloCamera *c, int *sx, int *sy) {
    int X = (int16_t)(-(vx + ox) - 0), Y = (int16_t)(vy + oy - c->y), Z = (int16_t)(vz + oz - c->z);
    int a1 = c->pitch, a2 = 0, a3 = c->roll;
    int Y1 = rot(Y, relo_cos(a1), Z, relo_sin(a1), 1);
    int Z1 = rot(Z, relo_cos(a1), Y, relo_sin(a1), 0);
    int X2 = rot(X, relo_cos(a2), Z1, relo_sin(a2), 0);
    int Z2 = rot(Z1, relo_cos(a2), X, relo_sin(a2), 1);
    int X3 = rot(X2, relo_cos(a3), Y1, relo_sin(a3), 1);
    int Y3 = rot(Y1, relo_cos(a3), X2, relo_sin(a3), 0);
    int d = (int16_t)(Z2 + 50);
    if (d <= 0) d = 1;
    /* engine reading: the quotient is kept in full; the spec stores 16-bit words, which the scene's values do not
     * exceed except right behind the eye (docs/engine.md) */
    *sx = 160 + 50 * X3 / d;
    *sy = 100 + 50 * Y3 / d;
}

/* ---- the polygon fill ---------------------------------------------------------------------- */

typedef struct {
    int64_t x, step; /* 16.16 */
    int vi, count;
} Walker;

typedef struct {
    const int *xs, *ys;
    int n;
    uint8_t *buf;
    uint8_t colour;
    Walker w[2]; /* 0 left (to the previous vertices), 1 right (to the next) */
    int ended;
} Fill;

static int64_t fx(int x) { return (int64_t)x * 65536 + 0x8000; }

static void fill_span(Fill *f, int r) {
    if (r < 0 || r > 198) return;
    int64_t a = f->w[0].x >> 16, b = f->w[1].x >> 16;
    if (b < a) return; /* seen from the back */
    if (a < 1) a = 1;
    if (b > 317) b = 317;
    if (a <= b) memset(f->buf + r * RELO_W + a, f->colour, (size_t)(b - a + 1));
}

static void take_edge(Fill *f, int k, int r) {
    Walker *W = &f->w[k];
    int dir = k ? 1 : -1;
    for (int guard = 0; guard <= f->n; guard++) {
        int nv = (W->vi + dir + f->n) % f->n;
        W->vi = nv;
        if (f->ys[nv] < r) { /* above the current row: x jumps there, a span at once, the next edge */
            W->x = fx(f->xs[nv]);
            fill_span(f, r);
            if (W->vi == f->w[!k].vi) break;
            continue;
        }
        W->count = f->ys[nv] - r + 1;
        int64_t dx = (int64_t)f->xs[nv] - (W->x >> 16);
        W->step = 256 * dx / W->count * 256;
        return;
    }
    f->ended = 1;
}

static void count_down(Fill *f, int k, int r) {
    Walker *W = &f->w[k];
    if (--W->count > 0) return;
    W->x = fx(f->xs[W->vi]);
    if (W->vi == f->w[!k].vi) {
        f->ended = 1;
        return;
    }
    take_edge(f, k, r);
}

void relo_fill(uint8_t *buf, const int *xs, const int *ys, int n, uint8_t colour) {
    if (n < 2) return;
    int top = 0;
    for (int i = 1; i < n; i++)
        if (ys[i] <= ys[top]) top = i; /* the smallest y, the last among equal ones */
    if (ys[top] >= 199) return;
    Fill f = {xs, ys, n, buf, colour, {{fx(xs[top]), 0, top, 0}, {fx(xs[top]), 0, top, 1}}, 0};
    int r = ys[top];
    fill_span(&f, r);
    take_edge(&f, 0, r);
    if (!f.ended) count_down(&f, 1, r);
    while (!f.ended && ++r <= 198) {
        f.w[0].x += f.w[0].step;
        f.w[1].x += f.w[1].step;
        fill_span(&f, r);
        count_down(&f, 0, r);
        if (!f.ended) count_down(&f, 1, r);
    }
}

int relo_culled(int oz, int camz) { return oz - camz < -67; } /* by the object's origin only */

void relo_scene(const ReloData *data, int n, uint8_t *buf) {
    memset(buf, 0, RELO_W * RELO_H);
    ReloCamera c;
    relo_camera(n, &c);
    int count = c.single ? 1 : 17;
    for (int i = 0; i < count; i++) {
        const ReloObject *o = c.single ? &data->single : &data->objects[i];
        if (!c.single && relo_culled(o->z, c.z)) continue;
        int xs[4], ys[4];
        for (int v = 0; v < 4; v++)
            relo_project(data->models[o->model][v][0], data->models[o->model][v][1], data->models[o->model][v][2], o->x, o->y, o->z, &c,
                         &xs[v], &ys[v]);
        relo_fill(buf, xs, ys, 4, (uint8_t)o->colour);
    }
}

/* ---- phase 2 ------------------------------------------------------------------------------- */

void relo_place(int w, int h, int x1, int y1, int x2, int y2, int z, ReloPlace *p) {
    int cx = x1 + floordiv(x2 - x1, 2), cy = y1 + floordiv(y2 - y1, 2);
    int X = 4 * (cx - 160), Y = 4 * (cy - 100), d = z + 50;
    if (d == 0) d = 1;
    p->left = 160 + 50 * (X - 2 * w) / d;
    p->right = 160 + 50 * (X + 2 * w) / d;
    p->top = 100 + 50 * (Y - 2 * h) / d;
    p->w_drawn = abs(p->right - p->left);
    p->step = p->w_drawn ? 256 * w / p->w_drawn : 16 * 256;
    if (p->step < 1) p->step = 1;
    p->visible = !((p->left < 0 || p->left > 319) && (p->right < 0 || p->right > 319));
}

void relo_draw_sprite(uint8_t *buf, const HSprite *s, const ReloPlace *p) {
    if (!p->visible || !s->px) return;
    int sy = 0;
    for (int y = p->top; (sy >> 8) < s->h; y++, sy += p->step) {
        int sx = 0;
        for (int x = p->left; (sx >> 8) < s->w; x++, sx += p->step) {
            if (y < 0 || y > 198 || x < 0 || x > 319) continue;
            uint8_t v = intro_bitrev(s->px[(sy >> 8) * s->w + (sx >> 8)]) & 0x1F;
            if (v) buf[y * RELO_W + x] = v;
        }
    }
}

static void set_dac_from(uint8_t *dac, const HSprite *s) {
    memset(dac, 0, 768);
    for (int i = 0; i < s->pal_n * 3; i++) dac[i] = s->pal[i] >> 2;
}

static void draw_piece(Relo *r, uint8_t *buf, int k, int z) {
    const HSprite *s = &r->spr[2 + k];
    const int *rc = r->data.rect[k + 1];
    ReloPlace p;
    relo_place(s->w, s->h, rc[0], rc[1], rc[2], rc[3], z, &p);
    relo_draw_sprite(buf, s, &p);
}

void relo_frame(Relo *r, int n) {
    if (n <= 842) {
        relo_scene(&r->data, n, r->screen);
        return;
    }
    if (!r->phase2) { /* screen cleared, I00's palette, the plate at its place as the background */
        r->phase2 = 1;
        memset(r->back, 0, sizeof r->back);
        set_dac_from(r->dac, &r->spr[0]);
        /* 1:1 at the rectangle's top-left (43, 63): measured against a render of the original, where the zoom rule at
         * Z = 150 would give (42, 62) */
        const int *rect = r->data.rect[0];
        ReloPlace p = {rect[0], rect[1], rect[0] + r->spr[1].w, r->spr[1].w, 256, 1};
        relo_draw_sprite(r->back, &r->spr[1], &p);
        r->flying = -1;
    }
    /* One piece action per frame: the piece in flight moves or lands, or else the next piece starts (and lands at
     * once when its depth from its start time is already 150). Measured against renders of the original sampled every
     * 25 ticks: a landing frame starts no new piece, and a late start lands in its own frame. */
    if (r->flying < 0 && r->next_piece < RELO_PIECES && n >= r->data.piece_start[r->next_piece])
        r->flying = r->next_piece++;
    if (r->flying >= 0 && 9 * (n - r->data.piece_start[r->flying]) - 10 >= 150) { /* lands: drawn at Z = 150 */
        draw_piece(r, r->back, r->flying, 150);
        r->flying = -1;
    }
    memcpy(r->screen, r->back, sizeof r->screen);
    if (r->flying >= 0) draw_piece(r, r->screen, r->flying, 9 * (n - r->data.piece_start[r->flying]) - 10);
}

/* ---- the program --------------------------------------------------------------------------- */

int relo_open(Relo *r, const char *root, struct Sound *sound) {
    memset(r, 0, sizeof *r);
    r->sound = sound;
    r->flying = -1;
    uint8_t *exe;
    size_t n;
    if (intro_read(root, "RELO.EXE", &exe, &n)) return -1;
    int parsed = relo_parse_data(exe, n, &r->data);
    free(exe);
    if (parsed) return -1;
    for (int i = 0; i < 19; i++) {
        char path[1200];
        snprintf(path, sizeof path, "%s/DATA/L/I%02d", root, i);
        if (hsprite_load(&r->spr[i], path)) return -1;
    }
    if (intro_read(root, "DATA/REFANF", &r->music_data, &r->music_n)) {
        r->music_data = NULL; /* no module: silent */
        r->music_n = 0;
    }
    r->stage = RELO_CREDIT;
    r->wait = 20; /* retraces */
    /* the sound start-up (step 3): with a MIDI card the MPU set-up; RELO's timer starts with the music */
    sound_midi_program(sound, SOUND_PROG_RELO);
    return 0;
}

void relo_free(Relo *r) {
    if (r->sound && r->sound->music.m.d == r->music_data) music_stop(&r->sound->music, r->sound->voice);
    if (r->stage == RELO_ANIM) sound_program_track_stop(r->sound); /* closed while the animation ran */
    if (r->sound && r->sound->midi_prog == SOUND_PROG_RELO) sound_midi_finish(r->sound); /* closed before its end */
    for (int i = 0; i < 19; i++) hsprite_free(&r->spr[i]);
    free(r->music_data);
    r->music_data = NULL;
}

static void start_logo(Relo *r) {
    r->stage = RELO_LOGO;
    r->wait = 100;
    set_dac_from(r->dac, &r->spr[18]);
    memset(r->screen, 0, sizeof r->screen);
    const HSprite *s = &r->spr[18];
    ReloPlace p = {58, 50, 58 + s->w, s->w, 256, 1}; /* drawn 1:1 at (58, 50); the header's (58, 35) is not used */
    relo_draw_sprite(r->screen, s, &p);
}

static void start_anim(Relo *r) {
    memset(r->screen, 0, sizeof r->screen);
    memset(r->dac, 0, 768);
    for (size_t i = 0; i < sizeof r->data.palette; i++) r->dac[i] = r->data.palette[i] >> 2;
    /* the music start: in CD mode CD track 2 and no module (docs/intro.md, "CD audio of the start programs"); with a
     * MIDI card the score and no module note (RELO lists no instrument, docs/music.md "The start programs") */
    int cd = sound_program_track(r->sound, RELO_CD_TRACK);
    if (!cd && r->sound && sound_midi_device(r->sound->mcard) >= 0) sound_midi_score(r->sound, RELO_SCORE);
    else if (!cd && r->sound && r->sound->mcard && r->music_data) music_start(&r->sound->music, r->music_data, r->music_n);
    r->stage = RELO_ANIM;
    r->n = RELO_N_START;
    relo_frame(r, r->n);
}

static void end_anim(Relo *r) {
    if (r->sound) music_stop(&r->sound->music, r->sound->voice);
    sound_program_track_stop(r->sound); /* before the 10 closing retraces */
    sound_midi_finish(r->sound); /* music stopped, sound shut down: the song stop and the program end */
    r->stage = RELO_TAIL;
    r->wait = 10;
}

static int take_key(IntroInput *in) {
    if (!in || !in->key) return 0;
    in->key = 0;
    return 1;
}

void relo_advance(Relo *r, IntroInput *in) {
    if (r->done) return;
    r->unit++;
    int retrace = r->unit % INTRO_U70 == 0, tick = r->unit % INTRO_U50 == 0;
    int left = in && in->left;
    switch (r->stage) {
    case RELO_CREDIT: /* keys are not read here; one pressed now stays for the next poll */
        if (retrace && --r->wait <= 0) start_logo(r);
        break;
    case RELO_LOGO:
        if (!retrace) break;
        if (take_key(in) || left || --r->wait <= 0) r->stage = RELO_LOGO_RELEASE;
        break;
    case RELO_LOGO_RELEASE: /* until the left button is up, so the click does not also skip the animation */
        if (retrace && !left) start_anim(r);
        break;
    case RELO_ANIM:
        if (!tick) break;
        r->n += 2; /* the timer interrupt */
        if (r->n > RELO_N_LAST || take_key(in) || left) {
            end_anim(r);
            break;
        }
        relo_frame(r, r->n);
        break;
    case RELO_TAIL:
        if (retrace && --r->wait <= 0) r->stage = RELO_DONE, r->done = 1;
        break;
    default:
        break;
    }
}

void relo_render(const Relo *r, IntroScreen *s) {
    s->w = RELO_W, s->h = RELO_H;
    memcpy(s->dac, r->dac, 768);
    if (r->stage == RELO_CREDIT || r->stage == RELO_DONE) { /* text mode: shown black (no text-mode font) */
        memset(s->px, 0, RELO_W * RELO_H);
        memset(s->dac, 0, 3);
        return;
    }
    memcpy(s->px, r->screen, RELO_W * RELO_H);
}
