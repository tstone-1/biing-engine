/* Shared parts of the programs before the game (intro.h). Written from docs/intro.md and docs/formats-data.md. */
#include "intro.h"
#include "assets.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t widen(uint8_t v) {
    v &= 63;
    return (uint8_t)(v << 2 | v >> 4);
}

void intro_compose(const IntroScreen *s, uint32_t *display) {
    uint32_t lut[256];
    for (int i = 0; i < 256; i++)
        lut[i] = 0xFF000000u | (uint32_t)widen(s->dac[3 * i]) << 16 | (uint32_t)widen(s->dac[3 * i + 1]) << 8 |
                 widen(s->dac[3 * i + 2]);
    for (int y = 0; y < INTRO_MAX_H; y++) {
        uint32_t *out = display + (size_t)y * INTRO_MAX_W;
        if (s->w == INTRO_MAX_W && s->h == INTRO_MAX_H) {
            const uint8_t *row = s->px + (size_t)y * s->w;
            for (int x = 0; x < INTRO_MAX_W; x++) out[x] = lut[row[x]];
        } else {
            int sy = y * s->h / INTRO_MAX_H;
            const uint8_t *row = s->px + (size_t)sy * s->w;
            for (int x = 0; x < INTRO_MAX_W; x++) out[x] = lut[row[x * s->w / INTRO_MAX_W]];
        }
    }
}

static int be16(const uint8_t *p) { return p[0] << 8 | p[1]; }

int hsprite_parse(HSprite *s, const uint8_t *d, size_t n) {
    memset(s, 0, sizeof *s);
    if (n < 12) return asset_fail("sprite shorter than its header");
    s->flags = (uint16_t)be16(d);
    s->planes = (uint16_t)be16(d + 2);
    s->w = be16(d + 4);
    s->h = be16(d + 6);
    s->x = be16(d + 8);
    s->y = be16(d + 10);
    size_t px = (size_t)s->w * s->h, rest;
    if (12 + px > n) return asset_fail("sprite %dx%d: %zu bytes", s->w, s->h, n);
    rest = n - 12 - px;
    if (rest == 768 || rest == 769) s->pal_n = 256;
    else if (rest == 96 || rest == 97) s->pal_n = 32;
    else if (rest > 1) return asset_fail("sprite %dx%d: %zu bytes after the pixels", s->w, s->h, rest);
    s->px = malloc(px ? px : 1);
    if (!s->px) return asset_fail("out of memory");
    memcpy(s->px, d + 12, px);
    /* the palette is the file's last 96 or 768 bytes: an odd pixel count is padded with one byte BEFORE it
     * (docs/formats-data.md, "Sprite with header"; docs/intro.md, BINT, "Where the palette is") */
    memcpy(s->pal, d + n - (size_t)s->pal_n * 3, (size_t)s->pal_n * 3);
    return 0;
}

int intro_read(const char *root, const char *rel, uint8_t **data, size_t *n) {
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    Blob b;
    if (blob_read(path, &b)) return -1;
    *data = b.data;
    *n = b.size;
    return 0;
}

int hsprite_load(HSprite *s, const char *path) {
    Blob b;
    memset(s, 0, sizeof *s);
    if (blob_read(path, &b)) return -1;
    int rc = hsprite_parse(s, b.data, b.size);
    blob_free(&b);
    if (rc) asset_fail("%s: bad sprite", path);
    return rc;
}

void hsprite_free(HSprite *s) {
    free(s->px);
    s->px = NULL;
}

void intro_resource_name(int n, char out[4]) {
    static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (n < 0) n = -n;
    int a = n / 100 % 36;
    out[0] = digits[a];
    out[1] = digits[n / 10 % 10];
    out[2] = digits[n % 10];
    out[3] = 0;
}

uint8_t intro_bitrev(uint8_t b) {
    uint8_t r = 0;
    for (int i = 0; i < 8; i++)
        if (b & 1 << i) r = (uint8_t)(r | 0x80 >> i);
    return r;
}

void intro_sine_circle(const int16_t q[65], int16_t out[256]) {
    for (int a = 0; a < 256; a++) {
        int i = a & 63;
        switch (a >> 6) {
        case 0: out[a] = q[i]; break;
        case 1: out[a] = q[64 - i]; break;
        case 2: out[a] = (int16_t)-q[i]; break;
        default: out[a] = (int16_t)-q[64 - i]; break;
        }
    }
}

/* The quarter table of RELO and BINT: Q15, 0 ... 32767 in 65 steps. It equals round(32767 sin(i pi / 128)) entry for
 * entry (checked against the user's RELO.EXE at 0x3BE8 and BINT.EXE at 0x43F6, 2026-10-19), so the engine builds it
 * instead of reading it. */
const int16_t *intro_quarter(void) {
    static int16_t q[65];
    static int built;
    if (!built) {
        for (int i = 0; i < 65; i++) q[i] = (int16_t)lround(32767.0 * sin(i * 3.14159265358979323846 / 128.0));
        built = 1;
    }
    return q;
}
