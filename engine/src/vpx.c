/* The tutorial's files (vpx.h). Written from docs/cdrom.md and docs/intro.md. */
#include "vpx.h"
#include "assets.h"

#include <stdlib.h>
#include <string.h>

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

#define VPX_HEAD_TEXT 0x78
#define VPX_COLS 0x80
#define VPX_CHUNKS 0x384

int vpx_parse(Vpx *v, const uint8_t *d, size_t n) {
    memset(v, 0, sizeof *v);
    if (n < VPX_CHUNKS || memcmp(d, "VPX1", 4) || memcmp(d + VPX_COLS, "COLS", 4)) return asset_fail("not a VPX1 file");
    v->w = (int)le16(d + VPX_HEAD_TEXT);
    v->h = (int)le16(d + VPX_HEAD_TEXT + 2);
    v->fps = (int)le16(d + VPX_HEAD_TEXT + 4);
    v->rate = (int)le16(d + VPX_HEAD_TEXT + 6);
    if (v->w != VPX_W || v->h != VPX_H) return asset_fail("VPX %dx%d, not 320x240", v->w, v->h);
    memcpy(v->pal, d + VPX_COLS + 4, 768);
    /* two passes: count, then fill */
    for (int pass = 0; pass < 2; pass++) {
        size_t p = VPX_CHUNKS, pcm = 0;
        int frames = 0, have_code = 0;
        while (p < n) {
            if (n - p < 4) return asset_fail("VPX: %zu stray bytes at the end", n - p);
            const uint8_t *t = d + p;
            if (!memcmp(t, "CODE", 4)) {
                if (n - p < 4 + VPX_CODE_BYTES) return asset_fail("VPX: CODE cut short");
                if (pass) v->frame[frames].code = t + 4;
                have_code = 1;
                p += 4 + VPX_CODE_BYTES;
                continue;
            }
            if (n - p < 8) return asset_fail("VPX: chunk header cut short");
            uint32_t sz = le32(t + 4);
            if (sz > n - p - 8) return asset_fail("VPX: chunk of %u bytes runs past the end", sz);
            if (!memcmp(t, "SOUN", 4)) {
                if (pass) memcpy(v->pcm + pcm, t + 8, sz);
                pcm += sz;
            } else if (!memcmp(t, "VIDE", 4)) {
                if (!have_code) return asset_fail("VPX: VIDE without CODE");
                if (pass) v->frame[frames].data = t + 8, v->frame[frames].n = sz;
                frames++;
                have_code = 0;
            } else {
                return asset_fail("VPX: unknown chunk at %zu", p);
            }
            p += 8 + sz;
        }
        if (!pass) {
            v->nframes = frames;
            v->pcm_n = pcm;
            v->frame = calloc((size_t)(frames ? frames : 1), sizeof *v->frame);
            v->pcm = malloc(pcm ? pcm : 1);
            if (!v->frame || !v->pcm) {
                vpx_free(v);
                return asset_fail("out of memory");
            }
        }
    }
    return 0;
}

void vpx_free(Vpx *v) {
    free(v->frame);
    free(v->pcm);
    v->frame = NULL;
    v->pcm = NULL;
    v->nframes = 0;
}

int vpx_code_bytes(int code) {
    static const int BYTES[8] = {0, 16, 32, 64, 1, 16, 32, 64};
    return code >= 0 && code < 8 ? BYTES[code] : 0;
}

void vpx_phase(int parity, int group, int *row, int *col) {
    static const int8_t PH[2][4][2] = {{{0, 0}, {1, 1}, {0, 1}, {1, 0}}, {{0, 1}, {1, 0}, {0, 0}, {1, 1}}};
    *row = PH[parity & 1][group & 3][0];
    *col = PH[parity & 1][group & 3][1];
}

/* One 4 x 4 group onto every other row and column of the block at (bx, by), at phase group g. */
static void put_group(uint8_t *buf, int bx, int by, int parity, int g, const uint8_t *src, int solid) {
    int r, c;
    vpx_phase(parity, g, &r, &c);
    uint8_t *o = buf + (size_t)(by * 8 + r) * VPX_W + bx * 8 + c;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) o[(size_t)(2 * i) * VPX_W + 2 * j] = solid ? src[0] : src[4 * i + j];
}

long vpx_decode(uint8_t *buf, const uint8_t code[VPX_CODE_BYTES], const uint8_t *data, size_t n, int parity) {
    size_t p = 0;
    for (int k = 0; k < VPX_BW * VPX_BH; k++) {
        int c = k & 1 ? code[k >> 1] & 15 : code[k >> 1] >> 4;
        if (!c) continue;
        size_t need = (size_t)vpx_code_bytes(c);
        if (need > n - p) return -1;
        const uint8_t *s = data + p;
        int bx = k % VPX_BW, by = k / VPX_BW;
        switch (c) {
        case 1:
            put_group(buf, bx, by, parity, 0, s, 0);
            put_group(buf, bx, by, parity, 1, s, 0);
            break;
        case 2:
            put_group(buf, bx, by, parity, 0, s, 0);
            put_group(buf, bx, by, parity, 1, s + 16, 0);
            break;
        case 3:
            for (int g = 0; g < 4; g++) put_group(buf, bx, by, parity, g, s + 16 * g, 0);
            break;
        case 4:
            put_group(buf, bx, by, parity, 0, s, 1);
            put_group(buf, bx, by, parity, 1, s, 1);
            break;
        default: break; /* 5-7: bytes taken, nothing drawn (no file uses them) */
        }
        p += need;
    }
    return (long)p;
}

/* ---- PSCENnn.DLT ---- */

long dlt_apply(const uint8_t *rec, size_t n, uint8_t *screen) {
    size_t p = 0, at = 0, end = (size_t)DLT_SCREEN_W * DLT_SCREEN_H;
    for (int first = 1;; first = 0) {
        if (n - p < 2) return -1;
        uint32_t skip = le16(rec + p);
        p += 2;
        if (!skip && !first) break;
        at += (size_t)skip * 4;
        if (n - p < 2) return -1;
        uint32_t copy = le16(rec + p);
        p += 2;
        if (!copy) break;
        size_t bytes = (size_t)copy * 4;
        if (bytes > n - p || at + bytes > end) return -1;
        if (screen) memcpy(screen + at, rec + p, bytes);
        at += bytes;
        p += bytes;
    }
    return (long)p;
}

int dlt_parse(Dlt *t, const uint8_t *d, size_t n) {
    memset(t, 0, sizeof *t);
    if (n < DLT_DATA) return asset_fail("DLT shorter than its header");
    while (t->ntimes < DLT_TIMES && le16(d + DLT_HEADER + 2 * t->ntimes) != 0xFFFF) {
        t->time[t->ntimes] = (uint16_t)le16(d + DLT_HEADER + 2 * t->ntimes);
        t->ntimes++;
    }
    size_t p = DLT_DATA;
    t->nrec = t->ntimes + 1;
    for (int k = 0; k < t->nrec; k++) {
        long used = dlt_apply(d + p, n - p, NULL);
        if (used < 0) return asset_fail("DLT record %d is cut short or leaves the screen", k);
        t->rec[k] = d + p;
        t->rec_n[k] = (size_t)used;
        p += (size_t)used;
    }
    return 0;
}

/* ---- TEXT13.TXT ---- */

int subtitles_parse(Subtitles *s, const uint8_t *d, size_t n) {
    memset(s, 0, sizeof *s);
    size_t p = 0;
    while (p < n && s->n < SUB_MAX) {
        while (p < n && d[p] != '*') p++;
        if (p + 5 > n) break;
        int num = 0;
        for (int i = 1; i <= 4; i++) {
            if (d[p + i] < '0' || d[p + i] > '9') return asset_fail("subtitles: bad mark at %zu", p);
            num = num * 10 + (d[p + i] - '0');
        }
        if (num == 9999) break;
        p += 5;
        while (p < n && d[p] != '*') p++; /* the closing star (a comment may come before it) */
        if (p >= n) return asset_fail("subtitles: mark without its closing star");
        p++;
        while (p < n && (d[p] == 0x0D || d[p] == 0x0A)) {
            if (d[p++] == 0x0A) break;
        }
        size_t q = p;
        while (q < n && d[q] != 0x0D && d[q] != 0x0A) q++;
        size_t len = q - p < SUB_LINE - 1 ? q - p : SUB_LINE - 1;
        s->time[s->n] = (num + 1) * 4;
        memcpy(s->text[s->n], d + p, len);
        s->text[s->n][len] = 0;
        s->n++;
        p = q;
    }
    return 0;
}
