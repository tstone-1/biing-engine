/* PRE.EXE, the disclaimer (intro_pre.h). Written from docs/intro.md, "PRE.EXE - the disclaimer". */
#include "intro_pre.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PRE_TEXT 0x0ADA
#define PRE_MAP 0x0989

int pre_find_text(const uint8_t *exe, size_t n, int marker, PreText *out) {
    static const struct { uint32_t base, cut; int end, english; } COPIES[] = {
        {0x11C0, 0x1055, 1115, 0}, /* disk and German CD */
        {0x1130, 0x0FF8, 1075, 1}, /* English CD */
    };
    memset(out, 0, sizeof *out);
    for (size_t k = 0; k < sizeof COPIES / sizeof *COPIES; k++) {
        uint32_t b = COPIES[k].base;
        if (n <= b + COPIES[k].cut || n <= b + PRE_MAP + 256) continue;
        const uint8_t *t = exe + b + PRE_TEXT;
        if (exe[b + COPIES[k].cut] != 0x0D || exe[b + COPIES[k].cut - 1] != 0x0D) continue;
        const uint8_t *z = memchr(t, 0, n - (b + PRE_TEXT));
        if (!z || z < exe + b + COPIES[k].cut) continue;
        out->text = t;
        out->len = marker ? COPIES[k].cut - PRE_TEXT : (size_t)(z - t);
        out->map = exe + b + PRE_MAP;
        out->end_line = COPIES[k].end - (marker ? PRE_MARKER_CUT : 0);
        out->english = COPIES[k].english;
        return 0;
    }
    return asset_fail("PRE.EXE: disclaimer text not found");
}

int pre_char_width(const Font *f, const uint8_t map[256], uint8_t c) {
    if (c == ' ') return PRE_SPACE_W;
    int slot = map[c];
    return slot < 128 ? f->width[slot] + 1 : 1;
}

int pre_layout(const uint8_t *text, size_t len, const Font *f, const uint8_t map[256], PreLine *lines, int max) {
    int n = 0;
    size_t i = 0;
    int hyphen_w = pre_char_width(f, map, '-');
    while (i < len && n < max) {
        size_t j = i;
        int width = 0;
        int have = 0, b_hyph = 0, b_width = 0;
        size_t b_end = 0, b_next = 0;
        PreLine L = {(int)i, 0, 0, 0, 0};
        size_t next;
        for (;;) {
            if (j >= len || text[j] == 0x0D) { /* end of a paragraph: compared like a break point */
                if (width >= PRE_LIMIT && have) goto use_break;
                L.end = (int)j, L.width = width;
                next = j < len ? j + 1 : len;
                break;
            }
            uint8_t c = text[j];
            if (c == ' ') {
                if (width >= PRE_LIMIT) {
                    if (have) goto use_break;
                    L.end = (int)j, L.width = width, next = j + 1;
                    break;
                }
                have = 1, b_end = j, b_next = j + 1, b_width = width, b_hyph = 0;
                width += PRE_SPACE_W;
                j++;
                continue;
            }
            if (c == '_') { /* soft hyphen: compared with the hyphen that a break there draws; not drawn otherwise */
                int hw = width + hyphen_w;
                if (hw >= PRE_LIMIT) {
                    if (have) goto use_break;
                    L.end = (int)j, L.width = hw, L.hyphen = 1, next = j + 1;
                    break;
                }
                have = 1, b_end = j, b_next = j + 1, b_width = hw, b_hyph = 1;
                j++;
                continue;
            }
            width += pre_char_width(f, map, c);
            j++;
            if (c == '-') {
                if (width >= PRE_LIMIT) {
                    if (have) goto use_break;
                    L.end = (int)j, L.width = width, next = j;
                    break;
                }
                have = 1, b_end = j, b_next = j, b_width = width, b_hyph = 0;
            }
            continue;
        use_break:
            L.end = (int)b_end, L.width = b_width, L.hyphen = b_hyph;
            next = b_next;
            break;
        }
        int d = PRE_LIMIT - L.width;
        L.x = d >= 0 ? d / 2 : -((-d + 1) / 2); /* rounded down */
        lines[n++] = L;
        i = next;
    }
    return n;
}

static void put_glyph(uint8_t *strip, int strip_h, const Font *f, int slot, int x, int y) {
    if (slot >= 128 || !f->glyph[slot]) return;
    int w = f->width[slot];
    for (int col = 0; col < w; col++) /* column by column, colour 0 skipped */
        for (int row = 0; row < f->height; row++) {
            uint8_t c = f->glyph[slot][row * w + col];
            int sx = x + col, sy = y + row;
            if (c && sx >= 0 && sx < PRE_STRIP_W && sy >= 0 && sy < strip_h) strip[sy * PRE_STRIP_W + sx] = c;
        }
}

void pre_render_strip(uint8_t *strip, int strip_h, const uint8_t *text, const PreLine *lines, int nlines,
                      const Font *f, const uint8_t map[256]) {
    memset(strip, 1, (size_t)PRE_STRIP_W * strip_h);
    int advance = f->height + PRE_LINE_ADVANCE_EXTRA;
    for (int k = 0; k < nlines; k++) {
        int x = lines[k].x, y = PRE_TEXT_TOP + k * advance;
        for (int i = lines[k].start; i < lines[k].end; i++) {
            uint8_t c = text[i];
            if (c == '_') continue;
            if (c != ' ') put_glyph(strip, strip_h, f, map[c], x, y);
            x += pre_char_width(f, map, c);
        }
        if (lines[k].hyphen) put_glyph(strip, strip_h, f, map['-'], x, y);
    }
}

void pre_marker_path(const char *root, char *out, size_t n) { snprintf(out, n, "%s/SECRET.MRK", root); }

int pre_marker_exists(const char *root) {
    char path[1200];
    pre_marker_path(root, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

int pre_marker_write(const char *root) {
    char path[1200];
    pre_marker_path(root, path, sizeof path);
    FILE *f = fopen(path, "wb");
    if (!f) return asset_fail("cannot write %s", path);
    fwrite("SECRET.MRK", 1, 10, f);
    return fclose(f) ? asset_fail("cannot write %s", path) : 0;
}

int pre_open(Pre *p, const char *root, int marker) {
    memset(p, 0, sizeof *p);
    p->shown = -1;
    p->marker = marker;
    uint8_t *col = NULL;
    size_t n = 0;
    if (intro_read(root, "PRE.EXE", &p->exe, &p->exe_n)) return -1;
    if (pre_find_text(p->exe, p->exe_n, marker, &p->text)) return -1;
    char path[1200];
    snprintf(path, sizeof path, "%s/DATA/ZEI1.FNT", root);
    if (font_load(&p->font, path)) return -1;
    if (intro_read(root, "DATA/COL", &col, &n)) return -1;
    if (n != 768) {
        free(col);
        return asset_fail("DATA/COL: %zu bytes", n);
    }
    for (int i = 0; i < 768; i++) p->dac[i] = col[i] >> 2;
    free(col);
    static PreLine lines[512];
    int nl = pre_layout(p->text.text, p->text.len, &p->font, p->text.map, lines, 512);
    pre_render_strip(p->strip, PRE_STRIP_H, p->text.text, lines, nl, &p->font, p->text.map);
    return 0;
}

void pre_free(Pre *p) {
    free(p->exe);
    p->exe = NULL;
    font_free(&p->font);
}

void pre_advance(Pre *p, IntroInput *in) {
    if (p->done) return;
    if (++p->unit % INTRO_U60) return;
    p->ticks++;
    /* keys are read in the copy loop; only with the marker do they (and either button) end the text */
    int key = in && in->key;
    if (in) in->key = 0;
    if (p->marker && in && (key || in->left || in->right)) {
        p->done = 1;
        return;
    }
    if (++p->tick_count < PRE_TICKS_PER_LINE) return;
    p->tick_count = 0;
    p->shown = p->window++;
    p->copies++;
    if (p->window >= p->text.end_line) p->done = 1;
}

void pre_render(const Pre *p, IntroScreen *s) {
    s->w = 320, s->h = 200;
    memcpy(s->dac, p->dac, 768);
    if (p->shown < 0) {
        memset(s->px, 0, 320 * 200);
        return;
    }
    for (int y = 0; y < 200; y++) {
        int sy = p->shown + y;
        if (sy < PRE_STRIP_H) memcpy(s->px + y * 320, p->strip + sy * PRE_STRIP_W, 320);
        else memset(s->px + y * 320, 1, 320);
    }
}
