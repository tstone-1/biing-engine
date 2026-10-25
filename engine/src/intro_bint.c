/* BINT.EXE, the story intro and the credits (intro_bint.h). Written from docs/intro.md, "BINT.EXE - the story intro
 * and credits", and docs/formats-data.md (sprites, PBM, PHAS, the N/801 tile map). Engine readings are marked. */
#include "fixes.h"
#include "intro_bint.h"
#include "sound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int16_t le16s(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }
static int be16(const uint8_t *p) { return p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t *p) { return (uint32_t)be16(p) << 16 | (uint32_t)be16(p + 2); }
static int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/* ---- the tables ------------------------------------------------------------------------------ */

int bint_parse_tables(const uint8_t *exe, size_t n, BintTables *t) {
    static const struct { uint32_t base, shift; int english; } COPIES[] = {{0x11C0, 0, 0}, {0x1130, 0x140, 1}};
    memset(t, 0, sizeof *t);
    for (size_t c = 0; c < sizeof COPIES / sizeof *COPIES; c++) {
        uint32_t base = COPIES[c].base, sh = COPIES[c].shift;
        uint32_t script = base + 0x46F4 + sh, caps = base + 0x4548 + sh, motion = base + 0x4A00 + sh;
        uint32_t map = base + 0x4458 + sh;
        if (n < script + 16 * BINT_ENTRIES || n < motion + 22) continue;
        /* the shape of the script: entry 0 at row 0, entry 26 (the end) after entry 25, both without a list */
        if (le32(exe + script) != 0 || le32(exe + script + 4) == 0 || le32(exe + script + 16) == 0 ||
            le32(exe + script + 16 * 10 + 4) != 0 || le32(exe + script + 16 * 26 + 4) != 0)
            continue;
        memset(t, 0, sizeof *t);
        int ok = 1;
        for (int i = 0; i < BINT_ENTRIES && ok; i++) {
            const uint8_t *e = exe + script + 16 * i;
            BintEntry *E = &t->script[i];
            E->start = le32(e);
            E->stars = le32(e + 8) != 0;
            uint32_t list = le32(e + 4);
            if (i && E->start < t->script[i - 1].start) ok = 0;
            if (!list) continue;
            if (list > n - base || n - base - list < 2) { ok = 0; break; }
            for (size_t p = (size_t)base + list; n - p >= 2 && E->n < BINT_LIST_MAX; p += 2) {
                int16_t v = le16s(exe + p);
                if (!v) break;
                E->list[E->n++] = (int16_t)(v < 0 ? -v : v);
            }
        }
        if (!ok) continue;
        for (int i = 0; i < BINT_MOTIONS && motion + 22 * (i + 1) <= n; i++) {
            const uint8_t *r = exe + motion + 22 * i;
            if (le16s(r) < 0) break;
            for (int k = 0; k < 11; k++) t->motion[i][k] = le16s(r + 2 * k);
            t->nmotion++;
        }
        for (int i = 0; i < BINT_CAPTIONS && caps + 8 * (i + 1) <= n; i++) {
            BintCaption *C = &t->caps[i];
            C->time = le32(exe + caps + 8 * i);
            uint32_t p = le32(exe + caps + 8 * i + 4);
            t->ncaps++;
            if (C->time == 0xFFFFFFFFu) break;
            if (p) {
                if (p >= n - base) { ok = 0; break; }
                size_t left = n - base - p;
                const uint8_t *s = exe + base + (size_t)p;
                if (!*s) C->line2 = 1, s++, left--; /* a leading 0 selects line 2 */
                if (!left || !memchr(s, 0, left)) { ok = 0; break; }
                C->text = s;
            }
        }
        if (!ok) continue;
        t->map = exe + map;
        t->english = COPIES[c].english;
        return 0;
    }
    memset(t, 0, sizeof *t);
    return asset_fail("BINT.EXE: scene script not found or invalid");
}

/* ---- deterministic rules --------------------------------------------------------------------- */

void bint_clock_tick(uint32_t *clock, uint32_t *acc, int tempo) {
    *acc += 4u * (uint32_t)tempo;
    while (*acc >= 187) { /* engine reading: "every 187" as >= 187, as in the module player */
        *acc -= 187;
        ++*clock;
    }
}

int bint_interp(int v0, int v1, int t, int start, int end) {
    int span = 4 * (end - start);
    if (span == 0) return v0;
    return (int16_t)(v0 + (int)((long long)(v1 - v0) * t / span));
}

int bint_frame(int obj, int frames, int period, int t) {
    if (frames == 0) return obj;
    int q = period > 0 ? t / (4 * period) : 0;
    if (frames == 1) return q % 2 == 0 ? obj : -1;
    return obj + q % frames;
}

void bint_project(int x, int y, int z, int w, int h, int *left, int *right, int *top) {
    int d = z + 300;
    if (d == 0) d = 1;
    *left = 160 + (int)((long long)(x - 2 * w) * 300 / d);
    *right = 160 + (int)((long long)(x + 2 * w) * 300 / d);
    *top = 100 + (int)((long long)(y - 2 * h) * 300 / d);
}

void bint_orbit_table(const int16_t q[65], int16_t out[321]) {
    if (fix_on(31)) {
        /* FIX-31 (docs/fixes.md): original writes T[127 + i] = -q[i] (entries 128-191 one step late); fixed is the
         * true sine circle, as RELO's */
        int16_t circle[256];
        intro_sine_circle(q, circle);
        for (int a = 0; a <= 320; a++) out[a] = circle[a & 255];
        return;
    }
    for (int i = 0; i <= 64; i++) {
        out[i] = q[i];
        out[i + 256] = q[i];
        out[128 - i] = q[i];
        out[127 + i] = (int16_t)-q[i];
        out[256 - i] = (int16_t)-q[i];
    }
}

void bint_orbit(int cx, int cy, int ax, int ay, int t, int *x, int *y) {
    static int16_t tables[2][321]; /* the original's and FIX-31's (docs/fixes.md) */
    static int built[2];
    int fixed = fix_on(31);
    int16_t *sine = tables[fixed];
    if (!built[fixed]) bint_orbit_table(intro_quarter(), sine), built[fixed] = 1;
    int k = floordiv(3 * t, 2) & 255;
    *x = cx - (int)(((long long)ax * sine[k]) >> 16);
    *y = cy + (int)(((long long)ay * sine[k + 64]) >> 16);
}

int bint_phas_index(int which, uint32_t rows) {
    if (which == 1) {
        if (rows < 970) return -1;
        int i = (int)((rows - 970) * 8 / 60);
        return i > 28 ? 28 : i;
    }
    if (rows < 1943) return -1;
    int i = (int)((rows - 1943) * 8 / 34);
    return i > 55 ? 55 : i;
}

void bint_caption_draw(uint8_t *strip, const uint8_t *text, int line2, const Font *f, const uint8_t map[256], int pbm) {
    int x = 0, y0 = line2 ? 11 : 0;
    for (const uint8_t *c = text; *c; c++) {
        if (*c == ' ') {
            x += 4;
            continue;
        }
        int slot = map[*c];
        if (slot >= 128 || !f->glyph[slot]) continue;
        int w = f->width[slot];
        for (int gy = 0; gy < f->height; gy++)
            for (int gx = 0; gx < w; gx++) {
                uint8_t v = f->glyph[slot][gy * w + gx];
                int sx = x + gx, sy = y0 + gy;
                if (!v || sx < 0 || sx >= 320 || sy < 0 || sy >= 24) continue;
                strip[sy * 320 + sx] = v == 15 ? (pbm ? 1 : 255) : (uint8_t)(v + 15);
            }
        x += w - 1;
    }
}

/* ---- PHAS and PBM ---------------------------------------------------------------------------- */

int bint_phas_parse(BintPhas *p, const uint8_t *d, size_t n, int wide) {
    memset(p, 0, sizeof *p);
    p->d = d, p->n = n, p->wide = wide;
    for (size_t o = 0; o + 8 <= n && p->count < 128; o += 2)
        if (!memcmp(d + o, "PHAS", 4)) {
            p->number[p->count] = be16(d + o + 4);
            p->ref[p->count] = (int16_t)be16(d + o + 6);
            p->start[p->count] = (uint32_t)(o + 8);
            p->count++;
            o += 6; /* the next tag is at least 8 bytes on */
        }
    return p->count ? 0 : asset_fail("PHAS: no frames");
}

void bint_phas_draw(const BintPhas *p, int k, uint8_t *buf, int top, int rows) {
    if (k < 0 || k >= p->count) return;
    if (p->ref[k] > 0) { /* draw frame ref's data again */
        for (int j = 0; j < p->count; j++)
            if (p->number[j] == p->ref[k] && p->ref[j] <= 0) {
                k = j;
                break;
            }
        if (p->ref[k] > 0) return;
    }
    const uint8_t *d = p->d;
    size_t o = p->start[k], n = p->n;
    for (int x = 0; x < 640; x++) {
        if (o + (size_t)(p->wide ? 2 : 1) > n) return;
        int count = p->wide ? be16(d + o) : d[o];
        o += p->wide ? 2 : 1;
        if (!count) continue;
        if (o + 2 > n) return;
        int row = be16(d + o);
        o += 2;
        for (int i = 0; i < count && o < n; i++) {
            uint8_t b = d[o++];
            if (b < 0x80) {
                row += b + 1;
                continue;
            }
            int len = (b & 0x7F) + 1;
            for (int j = 0; j < len && o < n; j++, row++) {
                uint8_t v = d[o++];
                /* the drawer's bank test is off by one: a pixel at a 64 KB bank boundary reached from the row above
                 * (within a run) is written past the window and lost, so it keeps the previous frame's value
                 * (docs/intro.md, "Settled 2026-10-20": (256, 102), (512, 204), (128, 307), (384, 409)) */
                long at = (long)(row + top) * 640 + x;
                /* FIX-32 (docs/fixes.md): original skips that pixel; fixed writes it */
                if (j > 0 && at % 65536 == 0 && !fix_on(32)) continue;
                if (row >= 0 && row + top < rows) buf[at] = v;
            }
        }
    }
}

int bint_pbm_decode(const uint8_t *d, size_t n, uint8_t *out, uint8_t pal[768]) {
    if (n < 12 || memcmp(d, "FORM", 4) || memcmp(d + 8, "PBM ", 4)) return asset_fail("not an IFF PBM");
    int w = 0, h = 0, comp = 0, have_body = 0;
    memset(out, 0, 640 * 400);
    for (size_t o = 12; o + 8 <= n;) {
        uint32_t len = be32(d + o + 4);
        const uint8_t *c = d + o + 8;
        if (o + 8 + len > n) len = (uint32_t)(n - o - 8);
        if (!memcmp(d + o, "BMHD", 4) && len >= 11) {
            w = be16(c), h = be16(c + 2), comp = c[10];
        } else if (!memcmp(d + o, "CMAP", 4)) {
            memcpy(pal, c, len < 768 ? len : 768);
        } else if (!memcmp(d + o, "BODY", 4)) {
            have_body = 1;
            size_t i = 0, op = 0, limit = 256000, total = (size_t)w * h;
            if (total > 640 * 400) total = 640 * 400;
            if (limit > total) limit = total;
            if (!comp) {
                memcpy(out, c, len < limit ? len : limit);
            } else {
                while (i < len && op < limit) {
                    int8_t ctl = (int8_t)c[i++];
                    if (ctl >= 0) {
                        for (int k = 0; k <= ctl && i < len && op < limit; k++) out[op++] = c[i++];
                    } else if (ctl != -128) {
                        uint8_t v = i < len ? c[i++] : 0;
                        for (int k = 0; k < 1 - ctl && op < limit; k++) out[op++] = v;
                    }
                }
            }
        }
        o += 8 + len + (len & 1);
    }
    if (w != 640 || !have_body) return asset_fail("PBM: %dx%d, body %d", w, h, have_body);
    return 0;
}

/* ---- the program ----------------------------------------------------------------------------- */

static void compose(Bint *b);

static uint32_t rnd(Bint *b, uint32_t n) { /* the stars' random numbers; the seed is free (docs/intro.md) */
    b->seed = b->seed * 1103515245u + 12345u;
    return (b->seed >> 16) % n;
}

static int load_res(Bint *b, const char *root, int num) {
    if (num <= 0 || num >= BINT_RES || b->spr[num]) return 0;
    char name[4], path[1200];
    intro_resource_name(num, name);
    snprintf(path, sizeof path, "%s/DATA/N/%s", root, name);
    if (num == 1 || num == 2) return intro_read(root, num == 1 ? "DATA/N/001" : "DATA/N/002", &b->pbm_raw[num], &b->pbm_n[num]);
    if (num == 11 || num == 12) {
        int k = num - 10;
        if (intro_read(root, num == 11 ? "DATA/N/011" : "DATA/N/012", &b->phas_raw[k], &b->phas_n[k])) return -1;
        return bint_phas_parse(&b->phas[k], b->phas_raw[k], b->phas_n[k], num == 11);
    }
    if (num == 801) return b->map801 ? 0 : intro_read(root, "DATA/N/801", &b->map801, &b->map801_n);
    HSprite *s = calloc(1, sizeof *s);
    if (!s) return asset_fail("out of memory");
    if (hsprite_load(s, path)) {
        free(s);
        return -1;
    }
    b->spr[num] = s;
    return 0;
}

int bint_open(Bint *b, const char *root, struct Sound *sound, uint32_t seed) {
    memset(b, 0, sizeof *b);
    b->sound = sound;
    b->seed = seed;
    if (intro_read(root, "BINT.EXE", &b->exe, &b->exe_n)) return -1;
    if (bint_parse_tables(b->exe, b->exe_n, &b->t)) return -1;
    char path[1200];
    snprintf(path, sizeof path, "%s/DATA/N/BI2PC.FNT", root);
    if (font_load(&b->font, path)) return -1;
    for (int i = 0; i < BINT_ENTRIES; i++)
        for (int k = 0; k < b->t.script[i].n; k++)
            if (load_res(b, root, b->t.script[i].list[k])) return -1;
    for (int k = 0; k < 2; k++) {
        if (intro_read(root, bint_song_file(b->t.english, k), &b->song[k], &b->song_n[k])) b->song[k] = NULL;
        else b->song_n[k] = bint_module_prepare(b->song[k], b->song_n[k], b->t.english);
    }
    for (int i = 0; i < BINT_STARS; i++) {
        b->star[i].x = (int)rnd(b, 320) * 64;
        b->star[i].speed = 2 + (int)rnd(b, 8);
        b->star[i].colour = (uint8_t)(17 + rnd(b, 3));
    }
    sound_midi_program(sound, SOUND_PROG_BINT); /* the sound start-up: with a MIDI card the MPU set-up */
    bint_start(b);
    return 0;
}

const char *bint_song_file(int english, int k) { /* the English CD's BINT.EXE names the .RAW files (OTH-EN-5) */
    static const char *const SONGS[2][2] = {{"DATA/N/BINTROPA.SCT", "DATA/N/BFANF.SCT"},
                                            {"DATA/N/BINTROPA.RAW", "DATA/N/BFANF.RAW"}};
    return SONGS[english ? 1 : 0][k ? 1 : 0];
}

size_t bint_module_prepare(uint8_t *d, size_t n, int english) {
    if (english && n && d[0] == 0xC2)
        for (size_t i = 0; i < n; i++) d[i] = (uint8_t)(d[i] + 0x80);
    if (n >= 0x40) {
        uint64_t total = 0x40;
        for (int k = 0; k < 4; k++) {
            const uint8_t *z = d + 0x10 + 4 * k;
            total += (uint32_t)z[0] << 24 | (uint32_t)z[1] << 16 | (uint32_t)z[2] << 8 | z[3];
        }
        if (total < n) n = (size_t)total;
    }
    return n;
}

int bint_module_tempo(const uint8_t *d, size_t n, int fallback) {
    return d && n >= 0x24 ? d[0x22] << 8 | d[0x23] : fallback;
}

/* The module instruments BINT lists before each song (its copy at 0xC5E): with a MIDI card only these play from the
 * module, the speech and effects (docs/music.md, "The module's instruments with a MIDI card"). */
static const uint8_t BINT_DIGITAL_STORY[] = {0x14, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x09, 0x0B,
                                             0x0C, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x25};
static const uint8_t BINT_DIGITAL_CREDITS[] = {0x0C};

/* The module branch (docs/intro.md, INT-14): with a digital card (DCARD not OFF) outside CD mode (MCARD CD zeroes the
 * DCARD word) and the song's file read (engine reading: a missing file, no branch). It takes the tempo from the
 * module header and clears the accumulator; the notes sound only with a music card (MCARD not OFF); with ROL / GM
 * only the listed instruments (SND-9). Returns 1 when it ran. */
static int module_branch(Bint *b, int k) {
    if (!b->sound || !b->sound->dcard || !b->song[k]) return 0; /* CD mode: the callers test it first */
    b->tempo = bint_module_tempo(b->song[k], b->song_n[k], b->tempo);
    b->acc = 0;
    int midi = sound_midi_device(b->sound->mcard) >= 0;
    if (b->sound->mcard && !music_start(&b->sound->music, b->song[k], b->song_n[k]) && midi) {
        if (k == 0) music_set_midi(&b->sound->music, BINT_DIGITAL_STORY, (int)sizeof BINT_DIGITAL_STORY);
        else music_set_midi(&b->sound->music, BINT_DIGITAL_CREDITS, (int)sizeof BINT_DIGITAL_CREDITS);
    }
    return 1;
}

/* The song start's MIDI part with a MIDI card, after the module branch, whatever DCARD says: the score plays and its
 * tempo becomes the driver's, so the clock and the module's rows run at it (BFANF 194, not the module's 195;
 * docs/music.md, "The start programs"). */
static void midi_song(Bint *b, int k) {
    if (!b->sound || sound_midi_device(b->sound->mcard) < 0) return;
    int tempo = sound_midi_score(b->sound, k ? BINT_SCORE_CREDITS : BINT_SCORE_STORY);
    if (tempo < 0) return;
    b->tempo = tempo;
    if (b->sound->music.playing && b->sound->music.m.d == b->song[k]) b->sound->music.m.tempo = tempo;
}

void bint_start(Bint *b) { /* part 1: the song starts and the clock is set to 0 */
    b->part = 1;
    b->tempo = BINT_TEMPO_STORY;
    b->clock = b->acc = 0;
    b->entry = -1;
    b->state = BINT_RUN;
    for (int k = 0; k < BINT_STARS; k++) b->star_at[0][k] = b->star_at[1][k] = -1;
    b->view = 0;
    /* CD mode: track 3 and no module, then the lead (docs/intro.md, "CD audio of the start programs") */
    b->cd_hold = sound_program_track(b->sound, BINT_CD_STORY);
    if (!b->cd_hold) module_branch(b, 0), midi_song(b, 0);
}

void bint_free(Bint *b) {
    if (b->sound) music_stop(&b->sound->music, b->sound->voice);
    if (!b->done && b->state != BINT_PAUSE) sound_program_track_stop(b->sound); /* closed while a track played */
    if (b->sound && b->sound->midi_prog == SOUND_PROG_BINT) sound_midi_finish(b->sound); /* closed before its end */
    for (int i = 0; i < BINT_RES; i++)
        if (b->spr[i]) hsprite_free(b->spr[i]), free(b->spr[i]);
    for (int k = 0; k < 3; k++) free(b->pbm_raw[k]), free(b->phas_raw[k]);
    free(b->map801);
    free(b->song[0]);
    free(b->song[1]);
    free(b->exe);
    font_free(&b->font);
    memset(b->spr, 0, sizeof b->spr);
}

int bint_caption_step(const BintCaption *caps, int ncaps, int *next, uint32_t rows, const uint8_t *line[2]) {
    if (*next >= ncaps) return 0;
    const BintCaption *c = &caps[*next];
    if (c->time == 0xFFFFFFFFu || (c->time != 1 && rows < c->time)) return 0;
    if (!c->text) line[0] = line[1] = NULL;
    else line[c->line2] = c->text;
    ++*next; /* one entry per pass (INT-15): a second line due as well waits for the next pass */
    return 1;
}

static void clear_captions(Bint *b) {
    b->line[0] = b->line[1] = NULL;
    memset(b->strip, 0, sizeof b->strip);
}

static void redraw_strip(Bint *b) {
    memset(b->strip, 0, sizeof b->strip);
    b->strip_restore = 2; /* the old caption's area is restored from the background for two frames */
    for (int k = 0; k < 2; k++)
        if (b->line[k]) bint_caption_draw(b->strip, b->line[k], k, &b->font, b->t.map, b->vesa);
}

static void set_palette(Bint *b, const uint8_t *pal8) {
    for (int i = 0; i < 768; i++) b->dac[i] = pal8[i] >> 2;
}

static void run_entry(Bint *b, int i) {
    const BintEntry *e = &b->t.script[i];
    b->stars_on = e->stars;
    b->entry = i;
    int bgn = e->list[0];
    if (bgn < 100) { /* a PBM picture and its PHAS animation, in VESA */
        b->vesa = 1;
        memset(b->pbm, 0, sizeof b->pbm);
        uint8_t pal[768] = {0};
        if (b->pbm_raw[bgn]) bint_pbm_decode(b->pbm_raw[bgn], b->pbm_n[bgn], b->pbm + 20 * 640, pal);
        set_palette(b, pal);
        b->dac[3] = b->dac[4] = b->dac[5] = 63; /* entry 1 forced to white */
        b->phas_drawn = 0;
        redraw_strip(b);
        return;
    }
    if (b->vesa) memset(b->screen, 0, sizeof b->screen); /* back to mode 13h: the screen is black until the next show */
    b->vesa = 0;
    memset(b->bg, 0, sizeof b->bg);
    const HSprite *s = b->spr[bgn];
    if (s && s->pal_n == 256) set_palette(b, s->pal);
    if (s) { /* drawn by the flat drawer (so it sets the shared rectangle); entries 3 and 9 clear it again */
        bint_draw_flat(b->bg, s, s->x, s->y, 0, &b->last);
        if (i == 3 || i == 9) memset(b->bg, 0, sizeof b->bg);
    }
    memcpy(b->comp, b->bg, sizeof b->comp); /* a new entry cleans the composed picture */
    for (int k = 0; k < BINT_STARS; k++) b->star_at[0][k] = b->star_at[1][k] = -1;
    b->view = 0;
    if (i == 9) b->scroll = 0;
    redraw_strip(b);
}

static void clear_pictures(Bint *b) { /* mode 13h set again (the screen black) and the buffers cleared */
    memset(b->bg, 0, sizeof b->bg);
    memset(b->comp, 0, sizeof b->comp);
    memset(b->screen, 0, sizeof b->screen);
}

static void start_flash(Bint *b) {
    b->stars_on = 0;
    b->vesa = 0;
    b->entry = 10;
    clear_pictures(b);
    clear_captions(b);
    b->state = BINT_FLASH;
    b->flash_step = 0;
    b->dac[3] = 0; /* the 4-byte clear also zeroes colour 1's red */
    b->dac[0] = b->dac[1] = b->dac[2] = 63; /* each value is set before its retrace wait: 63 from the start */
}

static void stop_music(Bint *b) {
    if (b->sound) music_stop(&b->sound->music, b->sound->voice);
    sound_program_track_stop(b->sound); /* the CD track: immediate */
    sound_midi_stop(b->sound); /* with a MIDI card the song stop (Bn 7B 00 x 16) */
}

static void part_change(Bint *b) { /* music off, captions cleared, mode 13h, buffers cleared, then 60 retraces */
    stop_music(b);
    clear_captions(b);
    b->vesa = 0;
    b->stars_on = 0;
    b->entry = -1;
    clear_pictures(b);
    b->state = BINT_PAUSE;
    b->wait = BINT_PART2_PAUSE;
    b->next_entry = 11;
}

static void end_program(Bint *b) {
    stop_music(b);
    sound_midi_end(b->sound); /* the sound shut-down */
    b->state = BINT_DONE;
    b->done = 1;
}

/* One 50 Hz tick in the order docs/intro.md gives: the stars, the scene-9 scroll and the PHAS frame index, and only
 * then the sound driver's tick, which advances the clock; so scroll and index see the rows before this tick's step
 * (measured: with the clock first, the PHAS frames and the map's leading edge came one tick early against the
 * original's frames). */
static void timer_tick(Bint *b) {
    if (b->stars_on)
        for (int i = 0; i < BINT_STARS; i++) { /* twice per tick; right, in scene 9 left; wrap at 320 */
            int dx = 2 * b->star[i].speed * (b->entry == 9 ? -1 : 1);
            b->star[i].x = ((b->star[i].x + dx) % (320 * 64) + 320 * 64) % (320 * 64);
        }
    uint32_t rows = bint_rows(b);
    if (b->entry == 9 && rows >= BINT_SCROLL_START && b->scroll < BINT_MAP_W) b->scroll++;
    if (b->vesa && (b->entry == 2 || b->entry == 4)) {
        int which = b->entry == 2 ? 1 : 2;
        int idx = bint_phas_index(which, rows);
        if (idx >= 0 && idx != b->phas_index) { /* the index changed: the next frame of the stream */
            b->phas_index = idx;
            bint_phas_draw(&b->phas[which], b->phas_drawn++, b->pbm, 20, 440);
        }
    }
    /* between the songs (the part-change pause) the music is stopped and the driver's pause word holds the clock
     * (docs/intro.md, "The time unit": starting a song clears it); the accumulator keeps its part-1 value */
    if (b->state != BINT_PAUSE) bint_clock_tick(&b->clock, &b->acc, b->tempo);
}

static void main_pass(Bint *b, IntroInput *in) {
    int key = in && in->key, left = in && in->left, right = in && in->right;
    if (in) in->key = 0;
    if (b->part == 1) {
        if (key || left) { /* straight to the part-2 set-up */
            part_change(b);
            return;
        }
        if (right) b->clock += BINT_FASTFORWARD;
    } else if (key || left) {
        end_program(b);
        return;
    }
    if (!b->vesa) { /* 1. show: swap the rectangle lists, the picture composed in the previous pass to the screen */
        b->cur ^= 1;
        memcpy(b->screen, b->comp + b->view, sizeof b->screen); /* the 64,000 bytes from the scroll on */
        if (b->part == 1) /* the captions over the shown picture: strip pixel (x, y) at screen (x - 2, 176 + y), colour
                           * 0 transparent */
            for (int y = 0; y < 24; y++)
                for (int x = 2; x < 320; x++) {
                    uint8_t v = b->strip[y * 320 + x];
                    if (v) b->screen[(176 + y) * 320 + x - 2] = v;
                }
    }
    uint32_t rows = bint_rows(b);
    while (b->next_entry < BINT_ENTRIES && rows >= b->t.script[b->next_entry].start) {
        int i = b->next_entry;
        if (i == 11 && b->part == 1) {
            part_change(b);
            return;
        }
        b->next_entry++;
        if (i == 10) {
            start_flash(b);
            return;
        }
        if (i == 26) {
            end_program(b);
            return;
        }
        run_entry(b, i);
    }
    if (b->part == 1 && bint_caption_step(b->t.caps, b->t.ncaps, &b->next_caption, rows, b->line)) redraw_strip(b);
    if (!b->vesa) compose(b);
}

void bint_advance(Bint *b, IntroInput *in) {
    if (b->done) return;
    b->unit++;
    int tick = b->unit % INTRO_U50 == 0, retrace = b->unit % INTRO_U70 == 0;
    if (tick) timer_tick(b); /* the timer runs in every state, the flash and the pause included */
    switch (b->state) {
    case BINT_RUN:
        if (!tick) break;
        if (b->cd_hold) { /* the lead: the timer runs, the main loop waits; then clock 0, the accumulator kept */
            if (b->clock < BINT_CD_LEAD) break;
            b->clock = 0;
            b->cd_hold = 0;
        }
        main_pass(b, in);
        break;
    case BINT_FLASH:
        if (!retrace) break;
        { /* 20 flashes of 7 retraces: 63, 53, 43, 33, 23, then 0 for two; step k's value is set when the wait of
           * step k - 1 ends, i.e. before its own wait (step 0's by start_flash) */
            if (++b->flash_step >= 140) {
                b->dac[0] = b->dac[1] = b->dac[2] = 0;
                b->state = BINT_RUN;
                break;
            }
            int k = b->flash_step % 7;
            uint8_t v = k < 5 ? (uint8_t)(63 - 10 * k) : 0;
            b->dac[0] = b->dac[1] = b->dac[2] = v;
        }
        break;
    case BINT_PAUSE:
        if (retrace && --b->wait <= 0) { /* song 2 and the clock at row 5,490 */
            b->part = 2;
            b->tempo = BINT_TEMPO_CREDITS;
            b->clock = BINT_PART2_CLOCK;
            /* only the module branch's song start sets the tempo from the header and clears the accumulator; without
             * it (DCARD OFF, CD audio, a missing file) 194 stays and the part-1 value carries over (docs/intro.md,
             * "The time unit", "The module branch and the English `.RAW` modules") */
            if (!sound_program_track(b->sound, BINT_CD_CREDITS)) /* CD mode: no branch, no lead */
                module_branch(b, 1), midi_song(b, 1);
            b->state = BINT_RUN;
            main_pass(b, in); /* so entry 11 runs at once */
        }
        break;
    default:
        break;
    }
}

/* ---- drawing --------------------------------------------------------------------------------- */

static const HSprite *scene_object(const Bint *b, int obj) {
    if (b->entry < 0 || b->entry >= BINT_ENTRIES) return NULL;
    const BintEntry *e = &b->t.script[b->entry];
    if (obj < 0 || obj + 1 >= e->n) return NULL;
    int num = e->list[obj + 1];
    return num > 0 && num < BINT_RES ? b->spr[num] : NULL;
}

int bint_draw_flat(uint8_t *px, const HSprite *s, int x, int y, int transparent, BintRect *last) {
    if (x + s->w > 320) return 0; /* the right edge would pass column 320: not drawn at all */
    int cut = x < 0 ? -x : 0, vw = s->w - cut;
    if (vw < 0) vw = 0; /* engine reading: a sprite wholly left of the screen draws (and records) nothing */
    for (int r = 0; r < s->h; r++) {
        int sy = y + r;
        if (sy < 0 || sy >= 200) continue;
        for (int c = cut; c < s->w; c++) {
            uint8_t v = s->px[r * s->w + c];
            if (transparent && !v) continue;
            px[sy * 320 + x + c] = v;
        }
    }
    *last = (BintRect){y * 320 + x + cut, vw, s->h};
    return 1;
}

int bint_draw_scaled(uint8_t *px, const HSprite *s, int x, int y, int z, BintRect *last) {
    int left, right, top;
    bint_project(x, y, z, s->w, s->h, &left, &right, &top);
    int W = abs(right - left);
    if ((unsigned)left >= 320 && (unsigned)(left + W) >= 320) return 0;
    uint32_t step = W ? (uint32_t)(256LL * s->w / W) : 16 * 256; /* 8 fraction bits */
    int first = -1, cols = 0, rows = 0;
    uint32_t sy = 0;
    for (int yy = top; (sy >> 8) < (uint32_t)s->h && yy < 199; yy++, sy += step) {
        if (yy < 0) continue; /* rows 0-198 only */
        int n = 0;
        uint32_t sx = 0;
        for (int xx = left; (sx >> 8) < (uint32_t)s->w && xx < 320; xx++, sx += step) {
            if (xx < 0) continue;
            if (first < 0) first = yy * 320 + xx;
            n++;
            uint8_t v = s->px[(sy >> 8) * s->w + (sx >> 8)];
            if (v) px[yy * 320 + xx] = v;
        }
        cols = n;
        rows++;
    }
    *last = (BintRect){first < 0 ? 0 : first, cols, rows};
    return 1;
}

void bint_restore_n(uint8_t *dst, const uint8_t *src, BintRect r, int n) {
    int w = (r.w + 1) & ~1;
    for (int k = 0; k < r.rows; k++)
        for (int i = 0; i < w; i++) {
            int p = r.off + k * 320 + i;
            if (p >= 0 && p < n) dst[p] = src[p];
        }
}

void bint_restore(uint8_t *dst, const uint8_t *src, BintRect r) { bint_restore_n(dst, src, r, 320 * 200); }

/* 4. draw: every object writes the drawers' shared rectangle into the current list - an object the drawer does not
 * draw the rectangle drawn last; a width or row count of 0 records nothing */
static void record(Bint *b) {
    if (b->last.w <= 0 || b->last.rows <= 0 || b->nrect[b->cur] >= BINT_MOTIONS) return;
    BintRect r = b->last;
    r.off += b->view; /* stored as a strip offset */
    b->rect[b->cur][b->nrect[b->cur]++] = r;
}

static void draw_objects(Bint *b) {
    uint8_t *px = b->comp + b->view; /* objects are placed on the view, at strip offset scroll + 320 y + x */
    int ox[BINT_MOTIONS], oy[BINT_MOTIONS], norbit = 0;
    for (int i = 0; i < b->t.nmotion; i++) {
        const int16_t *m = b->t.motion[i];
        int start4 = 4 * m[1], end4 = 4 * m[2];
        if (!((int)b->clock > start4 && (int)b->clock < end4)) continue;
        int t = (int)b->clock - start4;
        if (m[0] == 2) {
            int f = bint_frame(m[3], m[8], m[9], t);
            const HSprite *s = scene_object(b, f);
            if (!s) continue;
            bint_draw_flat(px, s, s->x + bint_interp(m[4], m[6], t, m[1], m[2]),
                           s->y + bint_interp(m[5], m[7], t, m[1], m[2]), m[10] == 1 && (s->flags & 0x8000), &b->last);
            record(b);
        } else if (m[0] == 3) {
            const HSprite *s = scene_object(b, m[3]);
            if (!s) continue;
            bint_draw_scaled(px, s, bint_interp(m[4], m[7], t, m[1], m[2]), bint_interp(m[5], m[8], t, m[1], m[2]),
                             bint_interp(m[6], m[9], t, m[1], m[2]), &b->last);
            record(b);
        } else if (m[0] == 4) {
            int cx = m[4], cy = m[5], tt = t;
            if (cx < 0) { /* a satellite of the (-cx)-th orbit drawn earlier in this frame */
                int k = -cx - 1;
                if (k >= norbit) continue;
                cx = ox[k] + 11, cy = oy[k] + 7, tt = 2 * t;
            }
            int x, y;
            bint_orbit(cx, cy, m[6], m[7], tt, &x, &y);
            int f = bint_frame(m[3], m[8], m[9], t);
            const HSprite *s = scene_object(b, f);
            ox[norbit] = x, oy[norbit] = y, norbit++; /* engine reading: the position before the header offset */
            if (!s) continue;
            bint_draw_flat(px, s, s->x + x, s->y + y, m[10] == 1 && (s->flags & 0x8000), &b->last);
            record(b);
        }
    }
}

/* Scene 9: per scrolled pixel the view moves one byte on in the strips and one new column enters at its right edge,
 * written into both strips at strip offset scroll + 319 + 320 r (r 0-191 the tiles, 192-199 black); nothing else
 * of the map is written (docs/intro.md, "Settled 2026-10-20"). The column is map column scroll: the view's left edge
 * is map column scroll - 319, and map column 0 never shows (measured against the original's frames, 2026-10-19). */
static void scroll_map(Bint *b) {
    const HSprite *sheet = b->spr[800];
    while (b->view < b->scroll && b->view < BINT_MAP_W) {
        int mx = ++b->view, col = mx / 16;
        for (int r = 0; r < 200; r++) {
            uint8_t v = 0;
            size_t mo = ((size_t)(r / 16) * 160 + (size_t)col) * 2;
            if (r < 192 && col < 160 && mo + 1 < b->map801_n) {
                int t = b->map801[mo + 1]; /* the low byte of the big-endian word */
                int tx = (t % 20) * 16 + mx % 16, ty = (t / 20) * 16 + r % 16;
                if (tx < sheet->w && ty < sheet->h) v = sheet->px[ty * sheet->w + tx];
            }
            int o = b->view + 319 + 320 * r;
            b->comp[o] = b->bg[o] = v;
        }
    }
}

/* Steps 3 and 4 of a pass in a 320 x 200 scene (step 1, the show, and step 2, the script, ran before). */
static void compose(Bint *b) {
    if (b->state != BINT_RUN || b->entry < 0) return; /* entry 10 (after the flash) still shows its caption */
    if (b->entry == 9 && b->map801 && b->spr[800]) scroll_map(b);
    for (int k = 0; k < b->nrect[b->cur]; k++) /* 3. restore, at strip offsets */
        bint_restore_n(b->comp, b->bg, b->rect[b->cur][k], BINT_STRIP);
    b->nrect[b->cur] = 0;
    uint8_t *view = b->comp + b->view;
    const uint8_t *bgv = b->bg + b->view;
    if (b->part == 1 && b->strip_restore > 0) { /* a changed caption: its area from the background */
        b->strip_restore--;
        for (int y = 176; y < 200; y++) memcpy(view + y * 320, bgv + y * 320, 318);
    }
    if (b->part == 1) /* the stars at strip offset 320 x 4i + x (the scroll not added): the pixel drawn two passes ago
                       * from the background, the new one only over background colour 0 (engine reading: under the
                       * objects) */
        for (int i = 0; i < BINT_STARS; i++) {
            int *at = &b->star_at[b->cur][i];
            if (*at >= 0) b->comp[*at] = b->bg[*at], *at = -1;
            int p = 4 * i * 320 + b->star[i].x / 64;
            if (b->stars_on && b->bg[p] == 0) b->comp[p] = b->star[i].colour, *at = p;
        }
    draw_objects(b);
}

void bint_render(const Bint *b, IntroScreen *s) {
    memcpy(s->dac, b->dac, 768);
    if (b->done) {
        s->w = 320, s->h = 200;
        memset(s->px, 0, 320 * 200);
        memset(s->dac, 0, 3);
        return;
    }
    if (b->vesa) { /* rows 0-429 from the picture buffer, the captions 2 x 2 at (2x, 420 + 2y); drawn at once */
        s->w = 640, s->h = 480;
        memset(s->px, 0, 640 * 480);
        memcpy(s->px, b->pbm, 640 * 430);
        for (int y = 0; y < 24; y++)
            for (int x = 0; x < 320; x++) {
                uint8_t v = b->strip[y * 320 + x];
                if (!v) continue;
                for (int k = 0; k < 4; k++) s->px[(420 + 2 * y + k / 2) * 640 + 2 * x + k % 2] = v;
            }
        return;
    }
    s->w = 320, s->h = 200; /* the picture of the last show, with the current DAC */
    memcpy(s->px, b->screen, 320 * 200);
}
