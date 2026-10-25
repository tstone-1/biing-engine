/* The programs before the game (docs/intro.md, docs/programs.md): the start sequence, PRE's layout and clock, RELO's
 * camera, rotation, projection, fill and pieces, BINT's clock, motion rules, tables, PHAS, PBM, captions and program
 * flow. Every fixture is built here from synthetic fonts, sprites, scene geometry, EXE images and files in a
 * temporary directory. Original-data render comparisons remain outside the tracked tests. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fixes.h"
#include "portable.h"
#include "sound.h"
#include "startup.h"

static int failures;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            failures++;                                                            \
        }                                                                          \
    } while (0)

/* ---- a synthetic font: slot 0 9 wide ('a'), slot 1 4 wide ('-'), slot 2 1 wide ('b'); height 3 ---------------- */

static Font font;
static uint8_t map[256];

static void make_font(void) {
    static const int W[3] = {9, 4, 1};
    memset(&font, 0, sizeof font);
    font.height = 3;
    for (int s = 0; s < 3; s++) {
        font.width[s] = W[s];
        font.glyph[s] = malloc((size_t)W[s] * 3);
        memset(font.glyph[s], 255, (size_t)W[s] * 3);
    }
    font.glyph[0][0] = 0; /* top-left transparent */
    memset(map, 0, sizeof map);
    map['a'] = 0, map['-'] = 1, map['b'] = 2;
}

static int layout(const char *t, PreLine *lines) {
    return pre_layout((const uint8_t *)t, strlen(t), &font, map, lines, 64);
}

static char *rep(char *dst, char c, int n) {
    memset(dst, c, (size_t)n);
    return dst + n;
}

static void test_pre_layout(void) {
    PreLine L[64];
    CHECK(pre_char_width(&font, map, 'a') == 10 && pre_char_width(&font, map, ' ') == 8);
    CHECK(layout("aaa", L) == 1 && L[0].width == 30 && L[0].x == 145);

    char t[256], *p = t;
    for (int w = 0; w < 7; w++) { /* seven words of 40 and six spaces: 328, broken after six words */
        if (w) *p++ = ' ';
        p = rep(p, 'a', 4);
    }
    *p = 0;
    CHECK(layout(t, L) == 2);
    CHECK(L[0].start == 0 && L[0].end == 29 && L[0].width == 280 && L[0].x == 20);
    CHECK(L[1].start == 30 && L[1].width == 40 && L[1].x == 140);

    p = rep(t, 'a', 16); /* 160 + 8 + 150 + 2 = 320 at the paragraph end: >= counts as too wide */
    *p++ = ' ';
    p = rep(p, 'a', 15);
    *p++ = 'b';
    *p = 0;
    CHECK(layout(t, L) == 2 && L[0].width == 160 && L[1].width == 152 && L[1].x == 84);

    p = rep(t, 'a', 32); /* an overlong word is not split */
    *p = 0;
    CHECK(layout(t, L) == 1 && L[0].width == 320 && L[0].x == 0);
    p = rep(t, 'a', 33);
    *p = 0;
    CHECK(layout(t, L) == 1 && L[0].width == 330 && L[0].x == -5);
    p = rep(t, 'a', 32);
    *p++ = '-';
    *p = 0;
    CHECK(layout(t, L) == 1 && L[0].width == 325 && L[0].x == -3); /* rounded down, not toward zero */

    p = rep(t, 'a', 30); /* a break after a hyphen */
    *p++ = '-';
    p = rep(p, 'a', 5);
    *p = 0;
    CHECK(layout(t, L) == 2 && L[0].end == 31 && L[0].width == 305 && !L[0].hyphen && L[1].width == 50);

    p = rep(t, 'a', 30); /* a soft hyphen used as a break draws '-' */
    *p++ = '_';
    p = rep(p, 'a', 5);
    *p = 0;
    CHECK(layout(t, L) == 2 && L[0].end == 30 && L[0].hyphen && L[0].width == 305 && L[1].start == 31);
    CHECK(layout("aa_aa", L) == 1 && L[0].width == 40 && !L[0].hyphen); /* not used: not drawn */

    CHECK(layout("a\r\ra", L) == 3 && L[0].width == 10 && L[1].width == 0 && L[1].x == 160 && L[2].width == 10);

    /* rendering: strip colour 1, glyphs from line 180, colour 0 of the glyph transparent, 17-line advance */
    static uint8_t strip[PRE_STRIP_W * 300];
    int n = layout("a\ra", L);
    pre_render_strip(strip, 300, (const uint8_t *)"a\ra", L, n, &font, map);
    int x = L[0].x;
    CHECK(strip[179 * 320 + x + 1] == 1 && strip[180 * 320 + x] == 1 && strip[180 * 320 + x + 1] == 255);
    CHECK(strip[(180 + 5) * 320 + x + 1] == 255 && strip[(180 + 3) * 320 + x + 1] == 1);
    CHECK(strip[(180 + 4) * 320 + x + 1] == 1 && strip[(180 + 7) * 320 + x + 1] == 255); /* line 2 at 185-187 */
    CHECK(strip[180 * 320 + x + 9] == 1); /* width 9: column 9 is the gap */
}

static void test_pre_find_text(void) {
    static uint8_t de[0x11C0 + 0x1100], en[0x1130 + 0x1100];
    memset(de, 0, sizeof de);
    memset(en, 0, sizeof en);
    memset(de + 0x11C0 + 0x0ADA, 'a', 0x1054 - 0x0ADA);
    de[0x11C0 + 0x1054] = de[0x11C0 + 0x1055] = 0x0D;
    memcpy(de + 0x11C0 + 0x1056, "zz\r", 3);
    PreText t;
    CHECK(pre_find_text(de, sizeof de, 0, &t) == 0 && !t.english && t.end_line == 1115 && t.len == 0x1059 - 0x0ADA);
    CHECK(pre_find_text(de, sizeof de, 1, &t) == 0 && t.end_line == 995 && t.len == 0x1055 - 0x0ADA);
    CHECK(t.map == de + 0x11C0 + 0x0989);
    memset(en + 0x1130 + 0x0ADA, 'a', 0x0FF7 - 0x0ADA);
    en[0x1130 + 0x0FF7] = en[0x1130 + 0x0FF8] = 0x0D;
    CHECK(pre_find_text(en, sizeof en, 0, &t) == 0 && t.english && t.end_line == 1075);
    CHECK(pre_find_text(en, sizeof en, 1, &t) == 0 && t.end_line == 955 && t.len == 0x0FF8 - 0x0ADA);
    memset(en, 0, sizeof en);
    CHECK(pre_find_text(en, sizeof en, 0, &t) != 0);
}

static Pre pre;

static void test_pre_clock(void) {
    IntroInput in = {0};
    memset(&pre, 0, sizeof pre);
    pre.shown = -1;
    pre.text.end_line = 5;
    for (int u = 0; u < 6 * INTRO_U60 - 1; u++) pre_advance(&pre, &in);
    CHECK(pre.copies == 0 && pre.shown == -1); /* the first copy at tick 6 */
    pre_advance(&pre, &in);
    CHECK(pre.copies == 1 && pre.shown == 0 && pre.ticks == 6);
    in.key = 1; /* without the marker a key is read and thrown away */
    for (int u = 0; u < INTRO_U60; u++) pre_advance(&pre, &in);
    CHECK(!pre.done && in.key == 0);
    in.right = 1;
    for (int u = 0; u < INTRO_U60; u++) pre_advance(&pre, &in);
    CHECK(!pre.done);
    in.right = 0;
    while (!pre.done) pre_advance(&pre, &in);
    CHECK(pre.copies == 5 && pre.ticks == 30 && pre.shown == 4); /* stops when the window reaches the end line */

    memset(&pre, 0, sizeof pre);
    pre.shown = -1, pre.marker = 1, pre.text.end_line = 100;
    for (int u = 0; u < 20 * INTRO_U60; u++) pre_advance(&pre, &in);
    CHECK(!pre.done && pre.copies == 3);
    in.left = 1;
    for (int u = 0; u < INTRO_U60; u++) pre_advance(&pre, &in);
    CHECK(pre.done && pre.copies == 3);
    in.left = 0;
    memset(&pre, 0, sizeof pre);
    pre.shown = -1, pre.marker = 1, pre.text.end_line = 100;
    in.key = 1;
    for (int u = 0; u < INTRO_U60; u++) pre_advance(&pre, &in);
    CHECK(pre.done);
    memset(&pre, 0, sizeof pre);
    pre.shown = -1, pre.marker = 1, pre.text.end_line = 100;
    in.key = 0, in.right = 1;
    for (int u = 0; u < INTRO_U60; u++) pre_advance(&pre, &in);
    CHECK(pre.done); /* the right button too */
    in.right = 0;
    in.key = 0;

    /* render: black before the first copy, the strip window after */
    static IntroScreen s;
    memset(&pre, 0, sizeof pre);
    pre.shown = -1;
    pre.strip[10 * PRE_STRIP_W + 7] = 99;
    pre_render(&pre, &s);
    CHECK(s.w == 320 && s.h == 200 && s.px[0] == 0);
    pre.shown = 3;
    pre_render(&pre, &s);
    CHECK(s.px[7 * 320 + 7] == 99);
}

/* ---- RELO ------------------------------------------------------------------------------------------------------ */

static uint32_t fnv(const uint8_t *p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static void test_relo_camera(void) {
    ReloCamera c;
    relo_camera(40, &c);
    CHECK(c.z == -120 && c.y == 0 && c.pitch == 0 && c.roll == 176 && !c.single);
    relo_camera(39, &c);
    CHECK(c.roll == 175); /* (n - 200) / 2 rounds toward minus infinity */
    relo_camera(649, &c);
    CHECK(c.z == 184 && !c.single);
    relo_camera(650, &c);
    CHECK(c.z == 100 && c.y == 0 && c.pitch == 0 && c.roll == 225 && c.single);
    relo_camera(700, &c);
    CHECK(c.y == -12 && c.pitch == 25 && c.roll == 250);
    relo_camera(780, &c);
    CHECK(c.pitch == 64 && c.y == -32);
    relo_camera(802, &c);
    CHECK(c.y == -38);
    relo_camera(804, &c);
    CHECK(c.y == -36 && c.pitch == 64);
    relo_camera(840, &c);
    CHECK(c.roll == 64);
    relo_camera(1000, &c);
    CHECK(c.roll == 64 && c.y == -36 && c.z == 100);
    CHECK(relo_culled(0, 67) == 0 && relo_culled(0, 68) == 1); /* z - camera z < -67 */
    CHECK(relo_sin(0) == 0 && relo_sin(64) == 32767 && relo_sin(128) == 0 && relo_sin(192) == -32767);
    CHECK(relo_sin(32) == 23170 && relo_sin(96) == 23170 && relo_sin(256 + 1) == relo_sin(1));
    CHECK(intro_quarter()[1] == 804 && intro_quarter()[64] == 32767);
}

static void test_relo_project(void) {
    ReloCamera c = {-100, 0, 0, 0, 0};
    int x, y;
    relo_project(10, 15, 0, 0, 0, 0, &c, &x, &y); /* Q15 doubled: 15 -> 14, 100 -> 99 -> 98; d = 148 */
    CHECK(x == 157 && y == 104);
    c.roll = 64;
    relo_project(10, 15, 0, 0, 0, 0, &c, &x, &y);
    CHECK(x == 156 && y == 97);
    c.roll = 0;
    relo_project(10, 15, 0, 0, 0, -160, &c, &x, &y); /* behind the eye: d = 1 */
    CHECK(x == 160 - 500 && y == 100 + 650); /* Z = -60, d = -10: thrown far out, not clipped */
}

static uint8_t buf[RELO_W * RELO_H];

static int count_row(int r, int c, int *lo, int *hi) {
    int n = 0;
    *lo = 999, *hi = -1;
    for (int x = 0; x < RELO_W; x++)
        if (buf[r * RELO_W + x] == c) {
            n++;
            if (x < *lo) *lo = x;
            if (x > *hi) *hi = x;
        }
    return n;
}

static void test_relo_fill(void) {
    int lo, hi;
    { /* TL TR BR BL: the top vertex is TR (the last of the equal ones) */
        int xs[4] = {10, 20, 20, 10}, ys[4] = {10, 10, 20, 20};
        memset(buf, 0, sizeof buf);
        relo_fill(buf, xs, ys, 4, 5);
        CHECK(count_row(10, 5, &lo, &hi) == 1 && lo == 20);
        for (int r = 11; r <= 21; r++) CHECK(count_row(r, 5, &lo, &hi) == 11 && lo == 10 && hi == 20);
        CHECK(count_row(9, 5, &lo, &hi) == 0 && count_row(22, 5, &lo, &hi) == 0); /* one row past the end vertex */
    }
    { /* the other winding: seen from the back, only the top pixel and the closing jump */
        int xs[4] = {10, 10, 20, 20}, ys[4] = {10, 20, 20, 10};
        memset(buf, 0, sizeof buf);
        relo_fill(buf, xs, ys, 4, 5);
        int total = 0;
        for (int r = 11; r <= 20; r++) total += count_row(r, 5, &lo, &hi);
        CHECK(total == 0 && count_row(10, 5, &lo, &hi) == 1);
        CHECK(count_row(21, 5, &lo, &hi) == 1 && lo == 10); /* the jump to a vertex above the row draws at once */
    }
    { /* columns clipped to 1 ... 317, rows to 198 */
        int xs[4] = {-50, 400, 400, -50}, ys[4] = {190, 190, 250, 250};
        memset(buf, 0, sizeof buf);
        relo_fill(buf, xs, ys, 4, 3);
        CHECK(count_row(195, 3, &lo, &hi) == 317 && lo == 1 && hi == 317);
        CHECK(count_row(198, 3, &lo, &hi) == 317 && count_row(199, 3, &lo, &hi) == 0);
    }
    { /* a top at y >= 199 is skipped; rows < 0 are not drawn */
        int xs[4] = {10, 20, 20, 10}, ys[4] = {199, 199, 210, 210};
        memset(buf, 0, sizeof buf);
        relo_fill(buf, xs, ys, 4, 3);
        int ys2[4] = {-30, -30, 2, 2};
        relo_fill(buf, xs, ys2, 4, 4);
        CHECK(count_row(0, 4, &lo, &hi) == 11 && count_row(3, 4, &lo, &hi) == 11 && count_row(4, 4, &lo, &hi) == 0);
        CHECK(count_row(198, 3, &lo, &hi) == 0);
    }
    { /* a slanted edge: 16.16 with 8 bits of fraction, x from the half */
        int xs[3] = {100, 110, 90}, ys[3] = {50, 60, 60};
        memset(buf, 0, sizeof buf);
        relo_fill(buf, xs, ys, 3, 2);
        CHECK(count_row(50, 2, &lo, &hi) == 1 && lo == 100);
        CHECK(count_row(55, 2, &lo, &hi) > 1 && lo < 100 && hi > 100);
    }
    { /* painter's order: a later span overwrites */
        int xs[4] = {10, 30, 30, 10}, ys[4] = {10, 10, 20, 20};
        int xs2[4] = {15, 25, 25, 15};
        memset(buf, 0, sizeof buf);
        relo_fill(buf, xs, ys, 4, 1);
        relo_fill(buf, xs2, ys, 4, 2);
        CHECK(buf[15 * RELO_W + 20] == 2 && buf[15 * RELO_W + 12] == 1);
    }
}

/* Synthetic scene images exercise pointers, layouts and rendering without original artwork. */
static void put32le(uint8_t *p, uint32_t v);
static void put16le(uint8_t *p, int v);
static void make_relo_image(uint8_t *exe, size_t size, unsigned base, unsigned shift) {
    memset(exe, 0, size);
    uint8_t *img = exe + base;
    for (int i = 0; i < 17; i++) {
        unsigned at = 0x4700u + (unsigned)i * 10;
        put32le(img + 0x42B8 + shift + 4 * i, at);
        put16le(img + at, 4);
        put16le(img + at + 2, 1);
    }
    put16le(img + 0x43AA + shift, 6);
    put16le(img + 0x43AC + shift, 1);
    put32le(img + 0x43B8 + shift, 0x4900);
    put16le(img + 0x4900, 4);
    const int vertices[4][3] = {{-8, 8, 0}, {8, 8, 0}, {8, -8, 0}, {-8, -8, 0}};
    for (int v = 0; v < 4; v++)
        for (int j = 0; j < 3; j++) put16le(img + 0x4902 + 6 * v + 2 * j, vertices[v][j]);
    for (int i = 0; i < 24; i++) img[0x416A + shift + (unsigned)i] = (uint8_t)(i * 5);
    for (int i = 0; i < RELO_PIECES; i++) put16le(img + 0x4296 + shift + 2 * i, 900 + 24 * i);
    for (int i = 0; i <= RELO_PIECES; i++)
        for (int j = 0; j < 4; j++) put16le(img + 0x420E + shift + 8 * i + 2 * j, 30 + 5 * i + j);
}

static void test_relo_scene(void) {
    static uint8_t exe[0x7000];
    const unsigned bases[] = {0x11C0, 0x11C0, 0x1130}, shifts[] = {0, 0x160, 0x140};
    ReloData data;
    for (int k = 0; k < 3; k++) {
        unsigned base = bases[k], sh = shifts[k];
        make_relo_image(exe, sizeof exe, base, sh);
        CHECK(relo_parse_data(exe, sizeof exe, &data) == 0);
        CHECK(data.objects[16].colour == 4 && data.single.colour == 6);
        CHECK(data.models[1][0][0] == -8 && data.models[1][2][1] == -8);
        CHECK(data.palette[23] == 115 && data.piece_start[15] == 1260 && data.rect[16][3] == 113);
        relo_scene(&data, 40, buf);
        uint32_t first = fnv(buf, sizeof buf);
        int painted = 0;
        for (size_t i = 0; i < sizeof buf; i++) if (buf[i]) painted++;
        CHECK(painted > 10);
        /* Changing the input's last object must change the rendered scene. */
        put16le(exe + base + 0x47A0, 2);
        CHECK(relo_parse_data(exe, sizeof exe, &data) == 0);
        relo_scene(&data, 40, buf);
        CHECK(fnv(buf, sizeof buf) != first);
        /* Every prefix short of the last used model byte is rejected. */
        for (size_t n = 0; n < base + 0x491A; n++) CHECK(relo_parse_data(exe, n, &data) != 0);
        const unsigned where[] = {0x42B8 + sh, 0x43B8 + sh, 0x42B8 + sh + 17 * 4};
        for (int i = 0; i < 3; i++) {
            make_relo_image(exe, sizeof exe, base, sh);
            put32le(exe + base + where[i], 0xFFFFFFFFu);
            CHECK(relo_parse_data(exe, sizeof exe, &data) != 0);
            CHECK(data.single.colour == 0); /* rejected candidates never leak partial output */
        }
        make_relo_image(exe, sizeof exe, base, sh);
        put16le(exe + base + 0x4900, 3);
        CHECK(relo_parse_data(exe, sizeof exe, &data) != 0);
        make_relo_image(exe, sizeof exe, base, sh);
        put16le(exe + base + 0x4702, 9);
        CHECK(relo_parse_data(exe, sizeof exe, &data) != 0);
        make_relo_image(exe, sizeof exe, base, sh);
        put16le(exe + base + 0x4700, 8);
        CHECK(relo_parse_data(exe, sizeof exe, &data) != 0);
    }
}

static void test_relo_place(void) {
    ReloPlace p;
    relo_place(80, 52, 111, 67, 190, 118, 150, &p);
    CHECK(p.left == 110 && p.right == 190 && p.top == 66 && p.w_drawn == 80 && p.step == 256 && p.visible);
    relo_place(80, 52, 111, 67, 190, 118, -10, &p); /* five times the size at the start */
    CHECK(p.left == -90 && p.right == 310 && p.w_drawn == 400 && p.step == 51 && p.visible);
    relo_place(10, 10, 0, 0, 10, 10, -10, &p);
    CHECK(!p.visible);

    HSprite s = {0};
    uint8_t px[3] = {0x80, 0x04, 0x08};
    s.w = 3, s.h = 1, s.px = px;
    ReloPlace one = {5, 5, 8, 3, 256, 1};
    memset(buf, 0, sizeof buf);
    relo_draw_sprite(buf, &s, &one);
    CHECK(buf[5 * RELO_W + 5] == 1 && buf[5 * RELO_W + 6] == 0 && buf[5 * RELO_W + 7] == 16);
    ReloPlace half = {5, 10, 11, 6, 128, 1}; /* step 0.5: each source pixel twice, in both directions */
    memset(buf, 0, sizeof buf);
    relo_draw_sprite(buf, &s, &half);
    CHECK(buf[10 * RELO_W + 5] == 1 && buf[10 * RELO_W + 6] == 1 && buf[11 * RELO_W + 5] == 1);
    CHECK(buf[10 * RELO_W + 9] == 16 && buf[10 * RELO_W + 10] == 16 && buf[10 * RELO_W + 11] == 0 && buf[12 * RELO_W + 5] == 0);
}

static Relo relo;

static void piece_run(int step, int *start_n, int *land_n) {
    memset(&relo, 0, sizeof relo);
    static uint8_t one = 0;
    for (int i = 0; i < 19; i++) relo.spr[i].w = relo.spr[i].h = 1, relo.spr[i].px = &one;
    relo.flying = -1;
    for (int k = 0; k < RELO_PIECES; k++) relo.data.piece_start[k] = 900 + 24 * k;
    for (int k = 0; k < RELO_PIECES; k++) start_n[k] = land_n[k] = -1;
    for (int n = 40; n <= 1900; n += step) {
        int before_next = relo.next_piece, before_fly = relo.flying;
        relo_frame(&relo, n);
        if (relo.next_piece != before_next) start_n[relo.next_piece - 1] = n;
        if (before_fly >= 0 && relo.flying != before_fly) land_n[before_fly] = n;
        if (relo.next_piece != before_next && relo.flying < 0) land_n[relo.next_piece - 1] = n;
    }
}

static void test_relo_pieces(void) {
    int s[RELO_PIECES], l[RELO_PIECES];
    piece_run(2, s, l); /* one frame per tick */
    CHECK(s[0] == 900 && l[0] == 918);
    CHECK(s[1] == 924 && l[1] == 942);
    for (int k = 1; k < RELO_PIECES; k++) CHECK(l[k] == relo.data.piece_start[k] + 18);
    CHECK(l[RELO_PIECES - 1] == 1278 && relo.flying < 0 && relo.next_piece == RELO_PIECES);
    piece_run(50, s, l); /* a slow machine: frames every 25 ticks */
    CHECK(s[0] == 940 && l[0] == 940);
    CHECK(s[1] == 990 && l[1] == 990); /* started late, lands in its own frame */
    CHECK(s[2] == 1040 && l[9] == 1390);
}

/* ---- BINT ------------------------------------------------------------------------------------------------------ */

static void test_bint_rules(void) {
    uint32_t clock = 0, acc = 0;
    for (int i = 0; i < 50; i++) bint_clock_tick(&clock, &acc, 144);
    CHECK(clock == 154);
    clock = acc = 0;
    for (int i = 0; i < 187; i++) bint_clock_tick(&clock, &acc, 144);
    CHECK(clock == 576 && acc == 0);
    clock = acc = 0;
    for (int i = 0; i < 187; i++) bint_clock_tick(&clock, &acc, 194);
    CHECK(clock == 776);

    CHECK(bint_interp(0, 100, 2, 0, 1) == 50 && bint_interp(0, -100, 1, 0, 1) == -25);
    CHECK(bint_interp(0, -10, 1, 0, 1) == -2 && bint_interp(7, 9, 0, 5, 5) == 7); /* toward zero; empty span */
    CHECK(bint_interp(-8800, 2800, 460, 3350, 3500) == 93);

    CHECK(bint_frame(5, 0, 3, 100) == 5);
    CHECK(bint_frame(5, 1, 2, 0) == 5 && bint_frame(5, 1, 2, 8) == -1 && bint_frame(5, 1, 2, 16) == 5);
    CHECK(bint_frame(5, 3, 2, 8) == 6 && bint_frame(5, 3, 2, 15) == 6 && bint_frame(5, 3, 2, 16) == 7 && bint_frame(5, 3, 2, 24) == 5);

    int l, r, t;
    bint_project(0, 0, 0, 10, 5, &l, &r, &t);
    CHECK(l == 140 && r == 180 && t == 90); /* four times the size at z = 0 */
    bint_project(-299, 0, 0, 0, 0, &l, &r, &t);
    CHECK(l == -139); /* the eye at distance 300 exactly */
    bint_project(0, 0, 900, 10, 5, &l, &r, &t);
    CHECK(l == 155 && r == 165 && t == 98); /* its own size at z = 900 */

    int x, y;
    bint_orbit(100, 50, 80, 60, 0, &x, &y);
    CHECK(x == 100 && y == 79);
    bint_orbit(100, 50, 80, 60, 43, &x, &y); /* k = 64, cos index 128: BINT's slipped -804, not 0 */
    CHECK(x == 61 && y == 49);
    bint_orbit(134, 90, 80, 60, 43, &x, &y); /* record 97 (INT-13): a true circle gives (95, 90) */
    CHECK(x == 95 && y == 89);
    {
        int16_t T[321];
        bint_orbit_table(intro_quarter(), T);
        CHECK(T[127] == 804 && T[128] == -804 && T[129] == -1608 && T[190] == -32757);
        CHECK(T[191] == -32767 && T[192] == -32767 && T[255] == -804 && T[256] == 0 && T[320] == 32767);
        CHECK(T[0] == 0 && T[64] == 32767 && T[126] == 1608);
    }
    bint_orbit(100, 50, 80, 60, 128, &x, &y); /* k = 192: floor of a negative value */
    CHECK(x == 140 && y == 50);

    CHECK(bint_phas_index(1, 969) == -1 && bint_phas_index(1, 970) == 0 && bint_phas_index(1, 977) == 0);
    CHECK(bint_phas_index(1, 978) == 1 && bint_phas_index(1, 2000) == 28);
    CHECK(bint_phas_index(2, 1942) == -1 && bint_phas_index(2, 1943) == 0 && bint_phas_index(2, 1948) == 1);
    CHECK(bint_phas_index(2, 1947) == 0 && bint_phas_index(2, 5000) == 55);
}

static void test_bint_caption(void) {
    Font f = {0};
    uint8_t g[6] = {1, 15, 0, 1, 1, 1}; /* 3 x 2 */
    f.height = 2, f.width[0] = 3, f.glyph[0] = g;
    uint8_t m[256] = {0};
    static uint8_t strip[320 * 24];
    memset(strip, 0, sizeof strip);
    bint_caption_draw(strip, (const uint8_t *)"a a", 0, &f, m, 0);
    CHECK(strip[0] == 16 && strip[1] == 255 && strip[2] == 0 && strip[320] == 16);
    CHECK(strip[6] == 16 && strip[7] == 255 && strip[5] == 0); /* advance width - 1 = 2, a space 4 */
    memset(strip, 0, sizeof strip);
    bint_caption_draw(strip, (const uint8_t *)"a", 1, &f, m, 1);
    /* line 2 at row 11; PBM: only v = 15 becomes colour 1 (white), v 1-14 keep v + 15 (the dark outline) */
    CHECK(strip[11 * 320] == 16 && strip[11 * 320 + 1] == 1 && strip[12 * 320 + 2] == 16 && strip[0] == 0);
}

/* INT-15 (docs/intro.md, "What the engine must change (INT-15)" 2): one caption entry per main-loop pass */
static void test_bint_caption_step(void) {
    const uint8_t *A = (const uint8_t *)"A", *B = (const uint8_t *)"B";
    BintCaption caps[4] = {{100, A, 0}, {1, B, 1}, {130, NULL, 0}, {0xFFFFFFFFu, NULL, 0}};
    const uint8_t *line[2] = {NULL, NULL};
    int next = 0;
    CHECK(bint_caption_step(caps, 4, &next, 99, line) == 0 && !line[0] && !line[1]); /* row 99: nothing */
    CHECK(bint_caption_step(caps, 4, &next, 105, line) == 1 && line[0] == A && !line[1]); /* line 1 "A" only */
    CHECK(bint_caption_step(caps, 4, &next, 105, line) == 1 && line[0] == A && line[1] == B); /* next pass: "B" */
    CHECK(bint_caption_step(caps, 4, &next, 129, line) == 0 && line[1] == B);
    CHECK(bint_caption_step(caps, 4, &next, 131, line) == 1 && !line[0] && !line[1]); /* cleared */
    CHECK(bint_caption_step(caps, 4, &next, 60000, line) == 0 && next == 3); /* 0xFFFFFFFF never */
    BintCaption same[3] = {{100, A, 0}, {100, B, 1}, {0xFFFFFFFFu, NULL, 0}}; /* two entries of the same time */
    line[0] = line[1] = NULL, next = 0;
    CHECK(bint_caption_step(same, 3, &next, 100, line) == 1 && line[0] == A && !line[1]);
    CHECK(bint_caption_step(same, 3, &next, 100, line) == 1 && line[1] == B);
}

/* ---- sprites with header: the palette is the file's last 96 or 768 bytes (a pad byte before it when w x h is odd) */

static size_t make_hsprite(uint8_t *d, int w, int h, int pal_bytes) {
    memset(d, 0, 12);
    d[0] = 0x80, d[5] = (uint8_t)w, d[7] = (uint8_t)h;
    size_t o = 12;
    for (int i = 0; i < w * h; i++) d[o++] = (uint8_t)(i + 1);
    if ((w * h) % 2) d[o++] = 0xEE; /* the pad, before the palette */
    for (int i = 0; i < pal_bytes; i++) d[o++] = (uint8_t)(i * 7 + 3);
    return o;
}

static void test_hsprite_palette(void) {
    static uint8_t d[12 + 64 + 1 + 768];
    HSprite s;
    size_t n = make_hsprite(d, 3, 1, 768); /* odd: pad, then the palette */
    CHECK(hsprite_parse(&s, d, n) == 0 && s.pal_n == 256 && s.pal[0] == 3 && s.pal[1] == 10 && s.pal[767] == (uint8_t)(767 * 7 + 3));
    CHECK(s.px[2] == 3);
    hsprite_free(&s);
    n = make_hsprite(d, 2, 1, 768); /* even: no pad */
    CHECK(hsprite_parse(&s, d, n) == 0 && s.pal_n == 256 && s.pal[0] == 3 && s.pal[767] == (uint8_t)(767 * 7 + 3));
    hsprite_free(&s);
    n = make_hsprite(d, 1, 1, 96); /* the 96-byte palette of L/ */
    CHECK(hsprite_parse(&s, d, n) == 0 && s.pal_n == 32 && s.pal[0] == 3 && s.pal[95] == (uint8_t)(95 * 7 + 3));
    hsprite_free(&s);
}

/* ---- BINT's drawers and the dirty-rectangle restore ------------------------------------------------------------ */

static uint8_t scr[320 * 200];

static void test_bint_drawers(void) {
    uint8_t p8[8] = {1, 2, 3, 4, 5, 6, 7, 0};
    HSprite s = {0};
    s.w = 4, s.h = 2, s.px = p8;
    BintRect last = {5, 6, 7};
    memset(scr, 0, sizeof scr);
    CHECK(bint_draw_flat(scr, &s, 317, 10, 0, &last) == 0); /* x + w = 321 > 320: not drawn at all */
    CHECK(last.off == 5 && last.w == 6 && last.rows == 7 && scr[10 * 320 + 317] == 0 && scr[10 * 320 + 319] == 0);
    CHECK(bint_draw_flat(scr, &s, 316, 10, 0, &last) == 1); /* x + w = 320 is drawn */
    CHECK(scr[10 * 320 + 316] == 1 && scr[11 * 320 + 319] == 0 && last.off == 10 * 320 + 316 && last.w == 4 && last.rows == 2);
    memset(scr, 0, sizeof scr);
    CHECK(bint_draw_flat(scr, &s, -1, 20, 1, &last) == 1); /* clipped on the left; colour 0 transparent */
    CHECK(scr[20 * 320] == 2 && scr[20 * 320 + 2] == 4 && scr[21 * 320 + 2] == 0 && scr[21 * 320 + 1] == 7);
    CHECK(last.off == 20 * 320 && last.w == 3 && last.rows == 2);
    scr[30 * 320 + 3] = 9;
    bint_draw_flat(scr, &s, 0, 30, 0, &last); /* opaque: colour 0 drawn */
    CHECK(scr[31 * 320 + 3] == 0 && scr[30 * 320 + 3] == 4);
    memset(scr, 0, sizeof scr);
    bint_draw_flat(scr, &s, 0, 199, 0, &last); /* rows are not clipped: the rectangle keeps h rows */
    CHECK(scr[199 * 320] == 1 && last.rows == 2 && last.off == 199 * 320);

    /* scaled: at z = 0 a w x h sprite is 4 times its size, step trunc(256 w / W) / 256 = 0.25 */
    uint8_t p2[2] = {7, 8};
    HSprite t = {0};
    t.w = 2, t.h = 1, t.px = p2;
    memset(scr, 0, sizeof scr);
    CHECK(bint_draw_scaled(scr, &t, 0, 0, 0, &last) == 1); /* left 156, right 164, top 98 */
    CHECK(scr[98 * 320 + 156] == 7 && scr[98 * 320 + 159] == 7 && scr[98 * 320 + 160] == 8 && scr[101 * 320 + 163] == 8);
    CHECK(scr[98 * 320 + 164] == 0 && scr[102 * 320 + 156] == 0 && scr[97 * 320 + 156] == 0);
    CHECK(last.off == 98 * 320 + 156 && last.w == 8 && last.rows == 4);
    memset(scr, 0, sizeof scr);
    bint_draw_scaled(scr, &t, 0, 98, 0, &last); /* top 196: rows 196-198 only, row 199 is never drawn */
    CHECK(scr[198 * 320 + 156] == 7 && scr[199 * 320 + 156] == 0 && last.rows == 3);
    memset(scr, 0, sizeof scr);
    bint_draw_scaled(scr, &t, 0, 0, -600, &last); /* behind the eye: right < left, drawn from left */
    CHECK(scr[102 * 320 + 164] == 7 && scr[102 * 320 + 171] == 8 && scr[102 * 320 + 156] == 0 && last.off == 102 * 320 + 164);
    BintRect keep = last;
    CHECK(bint_draw_scaled(scr, &t, -160, 0, -600, &last) == 0); /* left 324, right 316: left and left + W off screen */
    CHECK(last.off == keep.off && last.w == keep.w && last.rows == keep.rows);
    CHECK(bint_draw_scaled(scr, &t, -170, 0, 0, &last) == 0); /* left -14, left + W -6: both >= 320 unsigned */
    uint8_t p10[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 9};
    HSprite u = {0};
    u.w = 10, u.h = 1, u.px = p10;
    memset(scr, 0, sizeof scr);
    CHECK(bint_draw_scaled(scr, &u, -165, 0, 0, &last) == 1); /* left -25, left + W 15: drawn, columns 0-14 */
    CHECK(scr[98 * 320] == 1 && scr[98 * 320 + 14] == 9 && scr[98 * 320 + 15] == 0 && last.off == 98 * 320 && last.w == 15);
    static uint8_t p400[400];
    for (int i = 0; i < 400; i++) p400[i] = (uint8_t)(1 + i % 200);
    HSprite v = {0};
    v.w = 20, v.h = 20, v.px = p400;
    memset(scr, 0, sizeof scr);
    CHECK(bint_draw_scaled(scr, &v, 0, 0, 29700, &last) == 1); /* W = 0: step 16, still drawn, one pixel in 16 */
    CHECK(scr[100 * 320 + 160] == p400[0] && scr[100 * 320 + 161] == p400[16] && scr[101 * 320 + 160] == p400[16 * 20]);
    CHECK(scr[101 * 320 + 161] == p400[16 * 20 + 16] && scr[100 * 320 + 162] == 0 && scr[102 * 320 + 160] == 0);
    CHECK(last.off == 100 * 320 + 160 && last.w == 2 && last.rows == 2);

    /* restore: width rounded up to even, linear (past the row end into the next row), inside the picture only */
    static uint8_t src[320 * 200];
    memset(src, 4, sizeof src);
    memset(scr, 0, sizeof scr);
    bint_restore(scr, src, (BintRect){10 * 320 + 5, 3, 2});
    CHECK(scr[10 * 320 + 5] == 4 && scr[10 * 320 + 8] == 4 && scr[10 * 320 + 9] == 0 && scr[11 * 320 + 8] == 4 && scr[12 * 320 + 5] == 0);
    bint_restore(scr, src, (BintRect){20 * 320 + 319, 1, 1});
    CHECK(scr[20 * 320 + 319] == 4 && scr[21 * 320] == 4 && scr[21 * 320 + 1] == 0);
    bint_restore(scr, src, (BintRect){63999, 2, 2}); /* nothing past the end */
    CHECK(scr[63999] == 4);
    bint_restore(scr, src, (BintRect){0, 1, 1});
    CHECK(scr[0] == 4 && scr[1] == 4 && scr[2] == 0);

    /* the step is truncated to 8 fraction bits: w 2 over W 3 gives 170/256, so each source column covers 2 pixels */
    memset(scr, 0, sizeof scr);
    bint_draw_scaled(scr, &t, 1, 0, 300, &last); /* left 159, right 162, top 99 */
    CHECK(scr[99 * 320 + 160] == 7 && scr[99 * 320 + 161] == 8 && scr[99 * 320 + 162] == 8 && scr[100 * 320 + 162] == 8);
    CHECK(last.off == 99 * 320 + 159 && last.w == 4 && last.rows == 2);
    /* a row above the screen is skipped and not counted: top -1 draws rows 0-2 */
    memset(scr, 0, sizeof scr);
    bint_draw_scaled(scr, &t, 0, -99, 0, &last);
    CHECK(last.off == 156 && last.rows == 3 && scr[156] == 7 && scr[2 * 320 + 163] == 8);
}

/* ---- the composition of the 320 x 200 scenes: one pass late, persistent, trails --------------------------------- */

static Bint cb;
static uint8_t bgpx[4 * 4], objpx[2 * 2];
static HSprite bgs, bgs2, objs;

static void comp_setup(void) {
    memset(&cb, 0, sizeof cb);
    for (int i = 0; i < BINT_ENTRIES; i++) cb.t.script[i].start = 100000;
    cb.t.script[0].start = 0, cb.t.script[0].n = 2, cb.t.script[0].list[0] = 100, cb.t.script[0].list[1] = 101;
    cb.t.script[1].start = 20, cb.t.script[1].n = 1, cb.t.script[1].list[0] = 102;
    static uint8_t m[256];
    cb.t.map = m;
    memset(bgpx, 9, sizeof bgpx);
    memset(objpx, 5, sizeof objpx);
    bgs = (HSprite){0}, bgs.w = 4, bgs.h = 4, bgs.px = bgpx, bgs.pal_n = 256;
    bgs.pal[0] = 4, bgs.pal[3 * 9] = 200;
    bgs2 = bgs, bgs2.x = 100, bgs2.y = 100, bgs2.pal[0] = 8;
    objs = (HSprite){0}, objs.w = 2, objs.h = 2, objs.px = objpx;
    cb.spr[100] = &bgs, cb.spr[101] = &objs, cb.spr[102] = &bgs2;
    /* a flat object moving right one pixel per quarter-row on row 50, opaque */
    static const int16_t M[11] = {2, 0, 1000, 0, 0, 50, 4000, 50, 0, 0, 0};
    memcpy(cb.t.motion[0], M, sizeof M);
    cb.t.nmotion = 1;
    /* a caption from row 2, cleared at row 4; standing stars (star 0 over the background, star 1 over colour 0) */
    static const uint8_t a[] = "a";
    cb.font = font, cb.t.map = map;
    cb.t.caps[0] = (BintCaption){2, a, 0};
    cb.t.caps[1] = (BintCaption){4, NULL, 0};
    cb.t.caps[2] = (BintCaption){0xFFFFFFFFu, NULL, 0};
    cb.t.ncaps = 3;
    cb.t.script[0].stars = 1;
    for (int i = 0; i < BINT_STARS; i++) cb.star[i] = (BintStar){300 * 64, 0, 17};
    cb.star[0].x = 1 * 64, cb.star[1].x = 10 * 64;
    bint_start(&cb);
}

static void comp_pass(void) {
    for (int u = 0; u < INTRO_U50; u++) bint_advance(&cb, NULL);
}

static int count_val(const uint8_t *p, int y, uint8_t v) {
    int n = 0;
    for (int x = 0; x < 320; x++) n += p[y * 320 + x] == v;
    return n;
}

static void test_bint_compose(void) {
    static uint8_t prev[320 * 200];
    static IntroScreen out;
    comp_setup();
    comp_pass(); /* pass 1: entry 0 runs; the screen still shows the previous (empty) picture */
    CHECK(cb.entry == 0 && cb.dac[0] == 1 && cb.dac[27] == 50);
    bint_render(&cb, &out);
    CHECK(out.w == 320 && out.px[0] == 0 && count_val(out.px, 50, 5) == 0);
    CHECK(cb.comp[0] == 9 && cb.bg[0] == 9 && cb.bg[4] == 0);
    memcpy(prev, cb.comp, sizeof prev);
    comp_pass(); /* pass 2 shows pass 1's picture */
    bint_render(&cb, &out);
    CHECK(!memcmp(out.px, prev, sizeof prev) && out.px[0] == 9);
    int trails = 0;
    for (int k = 0; k < 6; k++) {
        memcpy(prev, cb.comp, sizeof prev);
        comp_pass();
        CHECK(!memcmp(cb.screen, prev, 176 * 320)); /* the rows above the captions: the composed picture */
        if (count_val(cb.comp, 50, 5) == 4) trails++; /* this pass's object and last pass's, 3 pixels apart */
        if (k == 0) CHECK(bint_rows(&cb) == 2 && cb.screen[177 * 320 + 3] == 0);
        /* the caption read in pass k = 0 shows over the next pass's picture at (x - 2, 176 + y), not composed */
        if (k == 1) CHECK(cb.screen[177 * 320 + 3] != 0 && cb.comp[177 * 320 + 3] == 0);
    }
    CHECK(trails == 6);
    int strip_clean = 1; /* the cleared caption's area is restored from the background */
    for (int i = 176 * 320; i < 200 * 320; i++) strip_clean &= cb.comp[i] == 0 || i % 320 >= 100; /* stars: x 300 */
    CHECK(bint_rows(&cb) >= 4 && strip_clean);
    CHECK(cb.comp[1] == 9 && cb.comp[4 * 320 + 10] == 17); /* a star only over background colour 0 */
    cb.star[1].x = 20 * 64;
    comp_pass(); /* its old pixel from the background (drawn there two passes ago as well) */
    CHECK(cb.comp[4 * 320 + 10] == 0 && cb.comp[4 * 320 + 20] == 17);
    cb.star[1].x = 30 * 64;
    comp_pass(); /* the star lists are swapped by the show: a pixel drawn in the last pass only stays one more pass */
    CHECK(cb.comp[4 * 320 + 20] == 17 && cb.comp[4 * 320 + 30] == 17);
    comp_pass();
    CHECK(cb.comp[4 * 320 + 20] == 0 && cb.comp[4 * 320 + 30] == 17);
    /* the rectangle restored is two passes old: last pass's copy stays, the one before is gone */
    int x0 = -1;
    for (int x = 0; x < 320; x++)
        if (cb.comp[50 * 320 + x] == 5) { x0 = x; break; }
    CHECK(x0 > 0 && cb.comp[50 * 320 + x0 - 1] == 0 && cb.comp[50 * 320 + x0 - 3] != 5);
    CHECK(cb.nrect[cb.cur] == 1 && cb.rect[cb.cur][0].w == 2 && cb.rect[cb.cur][0].rows == 2);
    /* a second record whose sprite overhangs the right edge is not drawn and records the rectangle drawn last */
    static const int16_t R[11] = {2, 0, 1000, 0, 400, 60, 400, 60, 0, 0, 0};
    memcpy(cb.t.motion[1], R, sizeof R);
    cb.t.nmotion = 2;
    comp_pass();
    CHECK(cb.nrect[cb.cur] == 2 && cb.rect[cb.cur][1].off == cb.rect[cb.cur][0].off && cb.rect[cb.cur][1].w == 2);
    CHECK(count_val(cb.comp, 60, 5) == 0);
    /* a sprite wholly left of the screen: a rectangle of width 0, recorded as nothing */
    static const int16_t L[11] = {2, 0, 1000, 0, -10, 70, -10, 70, 0, 0, 0};
    memcpy(cb.t.motion[2], L, sizeof L);
    cb.t.nmotion = 3;
    comp_pass();
    CHECK(cb.nrect[cb.cur] == 2 && cb.last.w == 0 && cb.last.rows == 2);
    /* entry 1: the new palette at once with the old picture for one pass, then the new background alone */
    while (bint_rows(&cb) < 20) {
        memcpy(prev, cb.comp, sizeof prev);
        comp_pass();
    }
    CHECK(cb.entry == 1 && cb.dac[0] == 2 && !memcmp(cb.screen, prev, sizeof prev));
    CHECK(!memcmp(cb.comp, cb.bg, sizeof cb.bg) && cb.comp[100 * 320 + 100] == 9 && cb.comp[0] == 0);
    comp_pass();
    CHECK(cb.screen[0] == 0 && cb.screen[100 * 320 + 103] == 9 && count_val(cb.screen, 50, 5) == 0);
    /* from a PBM scene back to mode 13h: the mode set blanks the screen for the entry's pass */
    cb.t.script[2].start = bint_rows(&cb) + 1, cb.t.script[2].n = 1, cb.t.script[2].list[0] = 100;
    cb.vesa = 1;
    memset(cb.screen, 0x33, sizeof cb.screen);
    while (cb.entry != 2) comp_pass();
    CHECK(cb.screen[0] == 0 && cb.screen[100 * 320 + 103] == 0 && cb.comp[0] == 9);
}

static void put32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}
static void put16le(uint8_t *p, int v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }

static void build_bint(uint8_t *exe, uint32_t base, uint32_t sh) {
    uint32_t list = 0x4C00 + sh, texts = 0x4D00 + sh;
    put16le(exe + base + list, -100), put16le(exe + base + list + 2, 111), put16le(exe + base + list + 4, -151);
    for (int i = 0; i < BINT_ENTRIES; i++) {
        uint8_t *e = exe + base + 0x46F4 + sh + 16 * i;
        put32le(e, (uint32_t)(10 * i));
        if (i != 10 && i != 26) put32le(e + 4, list);
        put32le(e + 8, i == 0);
    }
    memcpy(exe + base + texts, "\0two", 5);
    memcpy(exe + base + texts + 5, "one", 4);
    uint8_t *c = exe + base + 0x4548 + sh;
    put32le(c, 20), put32le(c + 4, texts);
    put32le(c + 8, 1), put32le(c + 12, texts + 5);
    put32le(c + 16, 40), put32le(c + 20, 0);
    put32le(c + 24, 0xFFFFFFFFu);
    uint8_t *m = exe + base + 0x4A00 + sh;
    for (int k = 0; k < 11; k++) put16le(m + 2 * k, k + 2);
    put16le(m + 22, -1);
}

static void test_bint_tables(void) {
    static uint8_t de[0x11C0 + 0x5200], en[0x1130 + 0x5200];
    memset(de, 0, sizeof de);
    memset(en, 0, sizeof en);
    build_bint(de, 0x11C0, 0);
    build_bint(en, 0x1130, 0x140);
    static BintTables t;
    CHECK(bint_parse_tables(de, sizeof de, &t) == 0 && !t.english);
    CHECK(t.script[3].start == 30 && t.script[0].stars && !t.script[1].stars);
    CHECK(t.script[1].n == 3 && t.script[1].list[0] == 100 && t.script[1].list[1] == 111 && t.script[1].list[2] == 151);
    CHECK(t.script[10].n == 0 && t.script[26].n == 0);
    CHECK(t.nmotion == 1 && t.motion[0][0] == 2 && t.motion[0][10] == 12);
    CHECK(t.ncaps == 4 && t.caps[0].line2 && !memcmp(t.caps[0].text, "two", 3) && !t.caps[1].line2);
    CHECK(t.caps[1].time == 1 && !memcmp(t.caps[1].text, "one", 3) && !t.caps[2].text && t.caps[3].time == 0xFFFFFFFFu);
    CHECK(t.map == de + 0x11C0 + 0x4458);
    CHECK(bint_parse_tables(en, sizeof en, &t) == 0 && t.english && t.script[2].start == 20);
    /* Captions must fit, including the optional line-2 marker and terminator. */
    for (int last = 0; last < 2; last++) {
        memset(de, 0, sizeof de);
        build_bint(de, 0x11C0, 0);
        put32le(de + 0x11C0 + 0x4548 + 4, 0x51FF);
        de[sizeof de - 1] = last ? 'X' : 0;
        CHECK(bint_parse_tables(de, sizeof de, &t) != 0);
    }
    memset(de, 0, sizeof de);
    build_bint(de, 0x11C0, 0);
    put32le(de + 0x11C0 + 0x46F4 + 4, 0xFFFFFFFFu);
    CHECK(bint_parse_tables(de, sizeof de, &t) != 0);
    memset(de, 0, sizeof de);
    build_bint(de, 0x11C0, 0);
    put32le(de + 0x11C0 + 0x4548 + 4, 0u - 0x11C0u);
    CHECK(bint_parse_tables(de, sizeof de, &t) != 0);
    CHECK(t.map == NULL);
    memset(en, 0, sizeof en);
    CHECK(bint_parse_tables(en, sizeof en, &t) != 0);
}

static void test_bint_phas_pbm(void) {
    /* two frames: frame 1 with data, frame 2 a reference to 1; narrow counts */
    static uint8_t d[700];
    uint8_t w[64] = {0};
    size_t n = 0;
    memcpy(d, "PHAS", 4), d[5] = 1, d[6] = 0xFF, d[7] = 0xFF;
    n = 8;
    d[n++] = 2; /* column 0: two ops from row 3 */
    d[n++] = 0, d[n++] = 3;
    d[n++] = 0x81, d[n++] = 7, d[n++] = 8; /* copy 2 */
    d[n++] = 0x00; /* skip 1 */
    n += 639; /* columns 1 ... 639: count 0 */
    n += n & 1; /* padded to an even length */
    memcpy(d + n, "PHAS", 4), d[n + 5] = 2, d[n + 7] = 1;
    BintPhas p;
    CHECK(bint_phas_parse(&p, d, n + 8, 0) == 0 && p.count == 2 && p.ref[0] == -1 && p.ref[1] == 1 && p.number[1] == 2);
    static uint8_t img[640 * 440];
    memset(img, 0, sizeof img);
    bint_phas_draw(&p, 0, img, 20, 440);
    CHECK(img[23 * 640] == 7 && img[24 * 640] == 8 && img[25 * 640] == 0 && img[22 * 640] == 0);
    memset(img, 0, sizeof img);
    bint_phas_draw(&p, 1, img, 20, 440); /* the reference draws frame 1's data again */
    CHECK(img[23 * 640] == 7);
    /* wide counts (N/011): the copy follows a skip of 3 */
    memcpy(w, "PHAS", 4), w[5] = 1, w[6] = w[7] = 0xFF;
    n = 8;
    w[n++] = 0, w[n++] = 2, w[n++] = 0, w[n++] = 1, w[n++] = 0x02, w[n++] = 0x80, w[n++] = 9;
    CHECK(bint_phas_parse(&p, w, 40, 1) == 0 && p.wide);
    memset(img, 0, sizeof img);
    bint_phas_draw(&p, 0, img, 0, 440);
    CHECK(img[4 * 640] == 9 && img[1 * 640] == 0);
    /* the bank test is off by one (INT-3a): a run that reaches a 64 KB boundary from the row above loses that pixel,
     * which keeps its old value; a run that starts on a boundary writes it. Column 256 from row 101 over (256, 102) =
     * offset 65,536; column 512 starting at row 204 = offset 131,072. */
    static uint8_t b[1400];
    memset(b, 0, sizeof b);
    memcpy(b, "PHAS", 4), b[5] = 1, b[6] = b[7] = 0xFF;
    n = 8 + 256; /* columns 0-255: count 0 */
    b[n++] = 1, b[n++] = 0, b[n++] = 101, b[n++] = 0x82, b[n++] = 4, b[n++] = 5, b[n++] = 6;
    n += 255; /* columns 257-511 */
    b[n++] = 1, b[n++] = 0, b[n++] = 204, b[n++] = 0x81, b[n++] = 7, b[n++] = 8;
    CHECK(bint_phas_parse(&p, b, n + 200, 0) == 0);
    memset(img, 3, sizeof img);
    bint_phas_draw(&p, 0, img, 0, 440);
    CHECK(img[101 * 640 + 256] == 4 && img[102 * 640 + 256] == 3 && img[103 * 640 + 256] == 6);
    CHECK(img[204 * 640 + 512] == 7 && img[205 * 640 + 512] == 8);

    /* PBM: BMHD 640 x 400 compressed, CMAP, BODY with a run, literals and the no-op */
    static uint8_t f[2000];
    memset(f, 0, sizeof f);
    size_t o = 0;
    memcpy(f, "FORM", 4), memcpy(f + 8, "PBM ", 4);
    o = 12;
    memcpy(f + o, "BMHD", 4), f[o + 7] = 20, f[o + 8] = 0x02, f[o + 9] = 0x80, f[o + 10] = 0x01, f[o + 11] = 0x90;
    f[o + 18] = 1;
    o += 28;
    memcpy(f + o, "CMAP", 4), f[o + 6] = 0x03, f[o + 8 + 3] = 77;
    o += 8 + 768;
    memcpy(f + o, "BODY", 4), f[o + 7] = 6;
    f[o + 8] = (uint8_t)-127, f[o + 9] = 5, f[o + 10] = 0x80, f[o + 11] = 1, f[o + 12] = 9, f[o + 13] = 10;
    o += 14;
    static uint8_t out[640 * 400];
    uint8_t pal[768];
    CHECK(bint_pbm_decode(f, o, out, pal) == 0);
    CHECK(out[0] == 5 && out[127] == 5 && out[128] == 9 && out[129] == 10 && out[130] == 0 && pal[3] == 77);
}

static Bint bint;

static void bint_synthetic(void) {
    memset(&bint, 0, sizeof bint);
    static const uint32_t START[BINT_ENTRIES] = {0,    100,  200,  300,  400,  500,  600,  700,  800,
                                                 900,  1000, 5490, 5500, 5510, 5520, 5530, 5540, 5550,
                                                 5560, 5570, 5580, 5590, 5600, 5610, 5620, 5630, 5640};
    for (int i = 0; i < BINT_ENTRIES; i++) {
        bint.t.script[i].start = START[i];
        if (i != 10 && i != 26) bint.t.script[i].n = 1, bint.t.script[i].list[0] = (int16_t)(100 + i);
    }
    bint.t.script[0].stars = 1;
    static const uint8_t one[] = "one", two[] = "two";
    bint.t.caps[0] = (BintCaption){5, one, 0};
    bint.t.caps[1] = (BintCaption){1, two, 1};
    bint.t.caps[2] = (BintCaption){10, NULL, 0};
    bint.t.caps[3] = (BintCaption){0xFFFFFFFFu, NULL, 0};
    bint.t.ncaps = 4;
    static uint8_t m[256];
    bint.t.map = m;
    for (int i = 0; i < BINT_STARS; i++) bint.star[i] = (BintStar){0, 2 + i % 8, 17};
    bint_start(&bint);
}

static void run_until(IntroInput *in, int (*cond)(void), long max) {
    for (long u = 0; u < max && !cond(); u++) bint_advance(&bint, in);
}
static int is_flash(void) { return bint.state == BINT_FLASH; }
static int is_run(void) { return bint.state == BINT_RUN; }
static int is_pause(void) { return bint.state == BINT_PAUSE; }
static int is_done(void) { return bint.done; }
static int at_entry10(void) { return bint.next_entry == 10; }

static void test_bint_flow(void) {
    IntroInput in = {0};
    bint_synthetic();
    for (int u = 0; u < INTRO_U50; u++) bint_advance(&bint, &in);
    CHECK(bint.entry == 0 && bint.stars_on && bint.next_entry == 1); /* entry 0 on the first pass */
    int x0 = bint.star[0].x;
    for (int u = 0; u < INTRO_U50; u++) bint_advance(&bint, &in);
    CHECK(bint.star[0].x == x0 + 2 * bint.star[0].speed); /* twice per tick, to the right */
    bint.dac[3] = 40;
    run_until(&in, at_entry10, 10000000);
    bint.comp[1000] = 77; /* in scene 9's picture; the flash's mode set and buffer clear leave nothing of it */
    run_until(&in, is_flash, 10000000);
    CHECK(bint.state == BINT_FLASH && bint_rows(&bint) >= 1000 && bint.next_entry == 11);
    CHECK(bint.line[0] == NULL && bint.line[1] == NULL);
    CHECK(bint.dac[0] == 63 && bint.dac[2] == 63 && bint.dac[3] == 0); /* each value set before its wait: 63 at once */
    uint32_t clock_at_flash = bint.clock;
    int max0 = 0;
    long u = 0;
    while (bint.flash_step == 0) bint_advance(&bint, &in), u++;
    CHECK(bint.dac[0] == 53 && u <= INTRO_U70); /* the second value from the first retrace on */
    while (bint.state == BINT_FLASH) { /* 20 flashes of 7 retraces */
        bint_advance(&bint, &in);
        u++;
        if (bint.dac[0] > max0) max0 = bint.dac[0];
    }
    CHECK(u > 139 * INTRO_U70 && u <= 140 * INTRO_U70 && bint.flash_step == 140 && max0 == 63 && bint.dac[0] == 0 && bint.dac[3] == 0);
    CHECK(bint.clock > clock_at_flash + 70); /* the clock keeps running */
    for (int k = 0; k < 2 * INTRO_U50; k++) bint_advance(&bint, &in);
    CHECK(bint.state == BINT_RUN && bint.screen[1000] == 0 && bint.comp[1000] == 0);
    run_until(&in, is_pause, 10000000);
    CHECK(bint.state == BINT_PAUSE && bint.part == 1 && bint_rows(&bint) < 5500 && bint.entry != 11);
    uint32_t acc_at_pause = bint.acc, clock_at_pause = bint.clock;
    for (int k = 0; k < 5 * INTRO_U50; k++) bint_advance(&bint, &in);
    CHECK(bint.clock == clock_at_pause && bint.acc == acc_at_pause); /* the stopped music holds the clock */
    u = 5 * INTRO_U50;
    while (bint.state == BINT_PAUSE) bint_advance(&bint, &in), u++;
    CHECK(u > 59 * INTRO_U70 && u <= 60 * INTRO_U70);
    CHECK(bint.part == 2 && bint.tempo == 194 && bint.entry == 11 && bint.clock == BINT_PART2_CLOCK);
    CHECK(acc_at_pause != 0 && bint.acc == acc_at_pause); /* no module played: the part-1 accumulator carries over */
    run_until(&in, is_done, 10000000);
    CHECK(bint.done && bint.entry == 25 && bint_rows(&bint) >= 5640);

    /* captions: "one" on line 1 at row 5, "two" right after it on line 2, both cleared at row 10 */
    bint_synthetic();
    while (bint_rows(&bint) < 5) bint_advance(&bint, &in);
    for (int k = 0; k < INTRO_U50; k++) bint_advance(&bint, &in);
    CHECK(bint.line[0] && bint.line[0][0] == 'o' && bint.line[1] && bint.line[1][0] == 't');
    while (bint_rows(&bint) < 11) bint_advance(&bint, &in);
    CHECK(!bint.line[0] && !bint.line[1]);

    /* the right button fast-forwards 20 quarter-rows per pass; a key goes to the credits, a second one ends */
    bint_synthetic();
    in.right = 1;
    for (int k = 0; k < 10 * INTRO_U50; k++) bint_advance(&bint, &in);
    CHECK(bint.clock == 10 * 20 + 30); /* 10 ticks of 4 x 144 / 187 = 30 quarter-rows */
    in.right = 0;
    in.key = 1;
    for (int k = 0; k < INTRO_U50; k++) bint_advance(&bint, &in);
    CHECK(bint.state == BINT_PAUSE && bint.next_entry == 11 && in.key == 0);
    run_until(&in, is_run, 10000000);
    CHECK(bint.part == 2 && bint.entry == 11);
    in.left = 1;
    for (int k = 0; k < INTRO_U50; k++) bint_advance(&bint, &in);
    CHECK(bint.done);
    in.left = 0;
    bint_synthetic(); /* the left button skips the story too */
    in.left = 1;
    for (int k = 0; k < INTRO_U50; k++) bint_advance(&bint, &in);
    CHECK(bint.state == BINT_PAUSE);
    in.left = 0;

    /* stars move left in scene 9 and wrap at 320 */
    bint_synthetic();
    bint.stars_on = 1, bint.entry = 9, bint.star[0].x = 0, bint.star[0].speed = 3;
    for (int k = 0; k < INTRO_U50; k++) bint_advance(&bint, &in);
    CHECK(bint.star[0].x == 320 * 64 - 6);

    /* the song-2 start with the module clears the accumulator */
    static Sound snd;
    static const uint8_t junk[4] = {1, 2, 3, 4};
    bint_synthetic();
    memset(&snd, 0, sizeof snd);
    snd.dcard = snd.mcard = 1;
    bint.sound = &snd, bint.song[1] = (uint8_t *)junk, bint.song_n[1] = sizeof junk;
    in.key = 1;
    for (int k = 0; k < 3 * INTRO_U50; k++) bint_advance(&bint, &in);
    CHECK(bint.state == BINT_PAUSE && bint.acc != 0);
    run_until(&in, is_run, 10000000);
    CHECK(bint.part == 2 && bint.acc == 0);
    bint.song[1] = NULL, bint.sound = NULL;

    /* the timer steps scroll and PHAS index before the clock: both see the rows before this tick */
    bint_synthetic();
    bint.entry = 9, bint.next_entry = 10, bint.clock = 4 * BINT_SCROLL_START - 1;
    bint.t.script[10].start = 100000;
    for (int k = 0; k < INTRO_U50; k++) bint_advance(&bint, &in);
    CHECK(bint_rows(&bint) == BINT_SCROLL_START && bint.scroll == 0);
    for (int k = 0; k < INTRO_U50; k++) bint_advance(&bint, &in);
    CHECK(bint.scroll == 1);
    bint_synthetic();
    bint.entry = 2, bint.vesa = 1, bint.next_entry = 10, bint.clock = 4 * 978 - 1;
    for (int k = 0; k < INTRO_U50; k++) bint_advance(&bint, &in);
    CHECK(bint_rows(&bint) == 978 && bint.phas_drawn == 0 && bint.phas_index == 0);
    for (int k = 0; k < INTRO_U50; k++) bint_advance(&bint, &in);
    CHECK(bint.phas_drawn == 1 && bint.phas_index == 1);

    /* scene 9's map: the view's left edge is map column scroll - 319, column 0 never shows */
    static uint8_t sheetpx[320 * 16], map801[160 * 12 * 2];
    static HSprite sheet;
    memset(sheetpx, 3, sizeof sheetpx);
    sheet = (HSprite){0}, sheet.w = 320, sheet.h = 16, sheet.px = sheetpx;
    bint_synthetic();
    bint.spr[800] = &sheet, bint.map801 = map801, bint.map801_n = sizeof map801;
    bint.entry = 9, bint.next_entry = 10, bint.scroll = 2;
    for (int k = 0; k < INTRO_U50; k++) bint_advance(&bint, &in);
    /* the view is the strip from the scroll on: one new column per scrolled pixel at its right edge (INT-1) */
    const uint8_t *v9 = bint.comp + bint.view;
    CHECK(bint.view == 2 && v9[319] == 3 && v9[318] == 3 && v9[317] == 0 && v9[191 * 320 + 319] == 3);
    CHECK(v9[192 * 320 + 319] == 0 && bint.bg[bint.view + 319] == 3);
    /* nothing else of the map is written: a pixel planted in the composed strip moves left with the view */
    bint.comp[bint.view + 100 * 320 + 50] = 77;
    bint.scroll = 3;
    for (int k = 0; k < INTRO_U50; k++) bint_advance(&bint, &in);
    CHECK(bint.view == 3 && bint.screen[100 * 320 + 50] == 77); /* shown: the picture of the last pass */
    for (int k = 0; k < INTRO_U50; k++) bint_advance(&bint, &in);
    CHECK(bint.screen[100 * 320 + 49] == 77 && bint.screen[100 * 320 + 50] != 77 && bint.screen[319] == 3);
    /* objects at strip offsets: the rectangle is stored with the scroll added */
    static uint8_t opx[4] = {5, 5, 5, 5};
    static HSprite o9;
    o9 = (HSprite){0}, o9.w = 2, o9.h = 2, o9.px = opx;
    bint.spr[901] = &o9;
    bint.t.script[9].n = 2, bint.t.script[9].list[0] = 800, bint.t.script[9].list[1] = 901;
    static const int16_t M9[11] = {2, 0, 30000, 0, 40, 60, 40, 60, 0, 0, 0};
    memcpy(bint.t.motion[0], M9, sizeof M9);
    bint.t.nmotion = 1;
    for (int k = 0; k < INTRO_U50; k++) bint_advance(&bint, &in);
    CHECK(bint.comp[bint.view + 60 * 320 + 40] == 5 && bint.nrect[bint.cur] == 1 &&
          bint.rect[bint.cur][0].off == bint.view + 60 * 320 + 40);
    /* the stars at strip offset 320 x 4i + x, the scroll not added (INT-2) */
    bint.stars_on = 1;
    for (int i = 0; i < BINT_STARS; i++) bint.star[i] = (BintStar){0, 0, 18};
    bint.star[10].x = 100 * 64;
    for (int k = 0; k < INTRO_U50; k++) bint_advance(&bint, &in);
    CHECK(bint.comp[40 * 320 + 100] == 18 && bint.comp[bint.view + 40 * 320 + 100] != 18);
    /* rectangles are restored anywhere in the strip, past 64,000 too; the caption area at the view */
    bint.stars_on = 0;
    bint.rect[bint.cur ^ 1][0] = (BintRect){64500, 2, 1}, bint.nrect[bint.cur ^ 1] = 1;
    bint.comp[64500] = 77;
    bint.comp[bint.view + 180 * 320 + 316] = 77; /* view 3: beyond the unscrolled area's 318 columns */
    bint.strip_restore = 1;
    for (int k = 0; k < INTRO_U50; k++) bint_advance(&bint, &in);
    CHECK(bint.comp[64500] == 0 && bint.view == 3 && bint.comp[bint.view + 180 * 320 + 316] == 0);
    bint.spr[800] = NULL, bint.map801 = NULL, bint.spr[901] = NULL;
}

/* ---- SND-13: the start programs' CD tracks (docs/intro.md, "CD audio of the start programs") ------------------- */

static Sound cds;
static int16_t cd_pcm[4];
static uint8_t cd_mod[0x40 + 512 + 1088 + 12 + 512];

/* a Sound in CD mode with tracks 2-4 "loaded" (long, so they play through every test; nothing renders them here) and
 * a minimal module that parses, so a module that is started really plays */
static void cd_sound(int cd_mode) {
    memset(&cds, 0, sizeof cds);
    cds.dcard = 1, cds.mcard = cd_mode ? 8 : 1, cds.cd_mode = cd_mode, cds.cd.level = 15;
    for (int t = 2; t <= 4; t++) cds.cd.state[t] = 1, cds.cd.pcm[t] = cd_pcm, cds.cd.frames[t] = 1u << 30;
    memset(cd_mod, 0, sizeof cd_mod);
    memcpy(cd_mod, "BONGTEST        ", 16);
    cd_mod[0x12] = 512 >> 8, cd_mod[0x16] = 1088 >> 8, cd_mod[0x17] = 1088 & 255, cd_mod[0x1B] = 12, cd_mod[0x1E] = 512 >> 8;
    cd_mod[0x23] = 120;
    cd_mod[0x40 + 512 + 2] = 0x04;
}
static void ticks(int n, IntroInput *in) {
    for (long u = 0; u < (long)n * INTRO_U50; u++) bint_advance(&bint, in);
}
static int cd_playing(int track) { return cds.cd.busy && cds.cd.track == track; }

static void test_cd_tracks(void) {
    IntroInput in = {0};
    /* without CD mode the story's module plays, as before */
    cd_sound(0);
    bint_synthetic();
    bint.sound = &cds, bint.song[0] = bint.song[1] = cd_mod, bint.song_n[0] = bint.song_n[1] = sizeof cd_mod;
    bint_start(&bint);
    CHECK(cds.music.playing && !cds.cd.busy && !bint.cd_hold);
    ticks(1, &in);
    CHECK(bint.entry == 0);

    /* CD mode: track 3 from tick 0, no module; the main loop waits for the 45-quarter-row lead */
    cd_sound(1);
    bint_synthetic();
    bint.sound = &cds, bint.song[0] = bint.song[1] = cd_mod, bint.song_n[0] = bint.song_n[1] = sizeof cd_mod;
    bint_start(&bint);
    CHECK(cd_playing(3) && !cds.music.playing && cds.cd_song == 0);
    ticks(14, &in);
    CHECK(bint.entry == -1 && bint.clock == 43); /* no main-loop pass before tick 15; the clock runs */
    ticks(1, &in);
    CHECK(bint.entry == 0 && bint.clock == 0 && bint.acc == 38 && !bint.cd_hold); /* 46 at tick 15: clock 0, acc kept */
    ticks(5, &in);
    CHECK(bint.clock == 15); /* tick 20 */
    ticks(3, &in);
    CHECK(bint.clock == 24 && cd_playing(3) && !cds.music.playing); /* tick 23 */
    /* the threshold is "45 or more": from the planted clock 41 one tick gives 44 (still held), from 42 it gives 45 */
    for (int c0 = 41; c0 <= 42; c0++) {
        bint_synthetic();
        bint.sound = &cds;
        bint_start(&bint);
        bint.clock = (uint32_t)c0;
        ticks(1, &in);
        CHECK(bint.cd_hold == (c0 == 41) && bint.entry == (c0 == 41 ? -1 : 0));
    }
    bint_synthetic();
    bint.sound = &cds;
    bint_start(&bint);
    ticks(23, &in);
    /* a key at tick 2,000 stops track 3 at 2,001; track 4 starts after the 60-retrace pause, at 2,043 */
    ticks(2000 - 23, &in);
    CHECK(cd_playing(3) && bint.state == BINT_RUN && bint.part == 1);
    in.key = 1;
    ticks(1, &in); /* tick 2,001 */
    CHECK(bint.state == BINT_PAUSE && !cds.cd.busy);
    uint32_t acc = bint.acc;
    CHECK(acc != 0);
    ticks(42, &in); /* tick 2,043 */
    CHECK(bint.state == BINT_PAUSE && !cds.cd.busy);
    ticks(1, &in); /* the pause ends between the ticks 2,043 and 2,044 */
    CHECK(bint.part == 2 && cd_playing(4) && !cds.music.playing && bint.clock > BINT_PART2_CLOCK);
    CHECK(!bint.cd_hold && bint.entry == 11); /* no lead in part 2 */
    /* part 2 in CD mode: no accumulator clear (CD mode counts as no module); one tick of 4 x 194 since */
    CHECK(bint.clock == BINT_PART2_CLOCK + (acc + 776) / 187 && bint.acc == (acc + 776) % 187);
    /* the exit key stops track 4; the main game's loop check does not restart it (the track word stays clear) */
    in.key = 1;
    ticks(1, &in);
    CHECK(bint.done && !cds.cd.busy && cds.cd_song == 0);
    sound_cd_frame(&cds);
    CHECK(!cds.cd.busy);
    /* the end of the credits stops it too, and a game song's track (track word set) is left alone */
    bint.done = 0, bint.state = BINT_RUN, bint.next_entry = 26, bint.t.script[26].start = 0;
    cds.cd.busy = 1, cds.cd.track = 4;
    ticks(1, &in);
    CHECK(bint.done && !cds.cd.busy);
    cds.cd.busy = 1, cds.cd.track = 7, cds.cd_song = 7;
    sound_program_track_stop(&cds);
    CHECK(cds.cd.busy);
    cds.cd_song = 0;
    /* the story's end at the part change stops track 3 too (no key) */
    cd_sound(1);
    bint_synthetic();
    bint.sound = &cds;
    bint_start(&bint);
    run_until(&in, is_pause, 10000000);
    CHECK(bint.part == 1 && !cds.cd.busy);
    run_until(&in, is_run, 10000000);
    CHECK(cd_playing(4));
    /* a missing TRACK03.WAV: silence (the drive stands), still no module and the same lead */
    cd_sound(1);
    cds.cd.state[3] = -1;
    bint_synthetic();
    bint.sound = &cds, bint.song[0] = cd_mod, bint.song_n[0] = sizeof cd_mod;
    bint_start(&bint);
    CHECK(!cds.cd.busy && !cds.music.playing && bint.cd_hold);
    bint.song[0] = bint.song[1] = NULL, bint.sound = NULL;

    /* RELO: track 2 from the music start (tick 0) to the animation's end at tick 676 (n > 1390), no module */
    for (int mode = 0; mode < 3; mode++) {
        cd_sound(mode != 0);
        memset(&relo, 0, sizeof relo);
        static uint8_t one = 0;
        for (int i = 0; i < 19; i++) relo.spr[i].w = relo.spr[i].h = 1, relo.spr[i].px = &one;
        relo.flying = -1, relo.sound = &cds, relo.music_data = cd_mod, relo.music_n = sizeof cd_mod;
        relo.stage = RELO_LOGO_RELEASE;
        while (relo.stage != RELO_ANIM) relo_advance(&relo, &in);
        if (mode == 0) {
            CHECK(cds.music.playing && !cds.cd.busy);
            relo.music_data = NULL;
            continue;
        }
        CHECK(cd_playing(2) && !cds.music.playing && cds.cd_song == 0);
        int t = 0;
        while (relo.stage == RELO_ANIM) {
            relo_advance(&relo, mode == 2 && t == 100 ? (in.key = 1, &in) : &in);
            if (relo.unit % INTRO_U50 == 0) {
                t++;
                if (relo.stage == RELO_ANIM) CHECK(cd_playing(2));
            }
        }
        CHECK(t == (mode == 1 ? 676 : 101) && !cds.cd.busy && relo.stage == RELO_TAIL);
        relo.music_data = NULL;
    }
}

/* ---- OTH-EN-5 and INT-14: the module branch and the English .RAW modules (docs/intro.md) ---------------------- */

static uint8_t mb_mod[sizeof cd_mod + 1], mb_raw[sizeof cd_mod + 1];

static void test_module_branch(void) {
    IntroInput in = {0};
    /* the file names */
    CHECK(!strcmp(bint_song_file(0, 0), "DATA/N/BINTROPA.SCT") && !strcmp(bint_song_file(0, 1), "DATA/N/BFANF.SCT"));
    CHECK(!strcmp(bint_song_file(1, 0), "DATA/N/BINTROPA.RAW") && !strcmp(bint_song_file(1, 1), "DATA/N/BFANF.RAW"));
    /* a module m with tempo 195, stored as (b - 0x80) mod 256 plus one byte 0x80: the English loader gives m back */
    cd_sound(0);
    memcpy(mb_mod, cd_mod, sizeof cd_mod);
    mb_mod[0x22] = 0, mb_mod[0x23] = 195;
    for (size_t i = 0; i < sizeof cd_mod; i++) mb_raw[i] = (uint8_t)(mb_mod[i] - 0x80);
    mb_raw[sizeof cd_mod] = 0x80;
    CHECK(mb_raw[0] == 0xC2);
    size_t n = bint_module_prepare(mb_raw, sizeof mb_raw, 1);
    CHECK(n == sizeof cd_mod && !memcmp(mb_raw, mb_mod, sizeof cd_mod) && mb_raw[sizeof cd_mod] == 0);
    CHECK(bint_module_tempo(mb_raw, n, 194) == 195 && bint_module_tempo(mb_raw, 0x23, 194) == 194);
    /* a file starting with B is taken unchanged (the trailing byte cut by the header's sizes) */
    memcpy(mb_raw, mb_mod, sizeof cd_mod), mb_raw[sizeof cd_mod] = 0x55;
    CHECK(bint_module_prepare(mb_raw, sizeof mb_raw, 1) == sizeof cd_mod && !memcmp(mb_raw, mb_mod, sizeof cd_mod) &&
          mb_raw[sizeof cd_mod] == 0x55);
    /* the disk / German loader has no conversion step */
    for (size_t i = 0; i < sizeof cd_mod; i++) mb_raw[i] = (uint8_t)(mb_mod[i] - 0x80);
    CHECK(bint_module_prepare(mb_raw, sizeof cd_mod, 0) == sizeof cd_mod && mb_raw[0] == 0xC2);
    /* the credits: a digital card and a music card -> tempo 195 from the header, the accumulator cleared, notes */
    for (int c = 0; c < 4; c++) { /* 0: DCARD SB MCARD SB, 1: MCARD OFF, 2: DCARD OFF, 3: MCARD GM (the module, SND-9) */
        cd_sound(0);
        cds.mcard = c == 1 ? 0 : c == 3 ? 7 : 1;
        cds.dcard = c == 2 ? 0 : 1;
        bint_synthetic();
        mb_mod[0x23] = 150; /* the story's header word: taken too */
        static uint8_t story[sizeof cd_mod];
        memcpy(story, mb_mod, sizeof cd_mod);
        mb_mod[0x23] = 195;
        bint.sound = &cds, bint.song[0] = story, bint.song[1] = mb_mod, bint.song_n[0] = bint.song_n[1] = sizeof cd_mod;
        bint_start(&bint);
        CHECK(bint.tempo == (c == 2 ? 144 : 150) && cds.music.playing == (c != 1 && c != 2));
        in.key = 1;
        for (int k = 0; k < 3 * INTRO_U50; k++) bint_advance(&bint, &in);
        CHECK(bint.state == BINT_PAUSE && bint.acc != 0 && !cds.music.playing);
        uint32_t acc = bint.acc;
        run_until(&in, is_run, 10000000);
        CHECK(bint.part == 2 && bint.tempo == (c == 2 ? 194 : 195));
        /* the pause ends inside a tick and the main pass follows: no clock tick since the song start */
        CHECK(bint.acc == (c == 2 ? acc : 0) && cds.music.playing == (c != 1 && c != 2));
        bint.song[0] = bint.song[1] = NULL, bint.sound = NULL;
    }
    /* a missing credits file: no branch (194, the accumulator carried) */
    cd_sound(0);
    bint_synthetic();
    bint.sound = &cds, bint.song[1] = NULL;
    in.key = 1;
    for (int k = 0; k < 3 * INTRO_U50; k++) bint_advance(&bint, &in);
    uint32_t acc = bint.acc;
    run_until(&in, is_run, 10000000);
    CHECK(bint.part == 2 && bint.tempo == 194 && bint.acc == acc && acc != 0);
    bint.sound = NULL;
}

/* ---- the start sequence ---------------------------------------------------------------------------------------- */

static Startup st;

/* A minimal SCMF score (docs/music.md, "SCMF score"): DATA with the tempo, 16 empty track lists, empty tables. */
static void write_score(const char *dir, const char *rel, int tempo) {
    uint8_t f[12 + 8 + 98 + 8 + 128 + 5 * 8];
    memset(f, 0, sizeof f);
    size_t o = 0;
    memcpy(f, "SCMF\4\0\0\0\5\0\0\0", 12), o = 12;
    memcpy(f + o, "DATA", 4), f[o + 4] = 98, f[o + 8] = (uint8_t)tempo, f[o + 9] = (uint8_t)(tempo >> 8), o += 8 + 98;
    memcpy(f + o, "TRAK", 4), f[o + 4] = 128, o += 8;
    for (int k = 0; k < 16; k++, o += 8) f[o + 1] = 0xEE, f[o + 2] = 0x02, memset(f + o + 4, 0xFF, 4);
    const char *tags[5] = {"PTAB", "PDAT", "ITAB", "IDAT", "END "};
    for (int c = 0; c < 5; c++, o += 8) memcpy(f + o, tags[c], 4);
    char path[512];
    snprintf(path, sizeof path, "%s/DATA", dir), test_mkdir(path);
    snprintf(path, sizeof path, "%s/DATA/M", dir), test_mkdir(path);
    snprintf(path, sizeof path, "%s/%s", dir, rel);
    FILE *fp = fopen(path, "wb");
    if (fp) fwrite(f, 1, o, fp), fclose(fp);
}

/* SND-9 in the start programs (docs/music.md, "The start programs", "The module's instruments with a MIDI card"):
 * with MCARD GM BINT plays its scores through the MIDI driver at the scores' tempo (also when a digital card loads the
 * module), the module only with the listed instruments; RELO plays its score and no module. */
static void test_midi_programs(void) {
    /* Use CTest's writable working directory, also on Windows where /tmp need not exist. */
    char dir[] = "biing_test_midi_XXXXXX";
    if (!mkdtemp(dir)) {
        CHECK(0);
        return;
    }
    write_score(dir, "DATA/M/BINTRO.SCT", 150);
    write_score(dir, "DATA/M/BFANF.SCT", 190);
    write_score(dir, "DATA/M/REFANF.SCT", 170);
    IntroInput in = {0};
    for (int dcard = 0; dcard < 2; dcard++) {
        cd_sound(0);
        snprintf(cds.root, sizeof cds.root, "%s", dir);
        cds.mcard = 7, cds.dcard = dcard;
        bint_synthetic();
        bint.sound = &cds, bint.song[0] = bint.song[1] = cd_mod, bint.song_n[0] = bint.song_n[1] = sizeof cd_mod;
        sound_midi_program(&cds, SOUND_PROG_BINT);
        bint_start(&bint);
        CHECK(cds.midi_prog == SOUND_PROG_BINT && cds.midi.playing && midi_tempo(&cds.midi) == 150);
        CHECK(bint.tempo == 150); /* the score's tempo, not the module's 120 */
        if (dcard) CHECK(cds.music.playing && cds.music.m.tempo == 150 && cds.music.midi_card && cds.music.only_n == 15);
        else CHECK(!cds.music.playing);
        run_until(&in, is_pause, 10000000);
        CHECK(!cds.midi.playing && cds.midi_prog == SOUND_PROG_BINT); /* the part change's song stop */
        run_until(&in, is_run, 10000000);
        CHECK(bint.part == 2 && bint.tempo == 190 && cds.midi.playing);
        if (dcard) CHECK(cds.music.m.tempo == 190 && cds.music.only_n == 1 && cds.music.only[0] == 0x0C);
        run_until(&in, is_done, 10000000);
        CHECK(cds.midi_prog == SOUND_PROG_NONE && !cds.midi.playing); /* the program end */
        bint.song[0] = bint.song[1] = NULL, bint.sound = NULL;
    }
    /* RELO: its score from the music start, no module; its timer starts with the song */
    cd_sound(0);
    snprintf(cds.root, sizeof cds.root, "%s", dir);
    cds.mcard = 7;
    memset(&relo, 0, sizeof relo);
    static uint8_t one = 0;
    for (int i = 0; i < 19; i++) relo.spr[i].w = relo.spr[i].h = 1, relo.spr[i].px = &one;
    relo.flying = -1, relo.sound = &cds, relo.music_data = cd_mod, relo.music_n = sizeof cd_mod;
    sound_midi_program(&cds, SOUND_PROG_RELO);
    CHECK(!cds.midi.timer);
    relo.stage = RELO_LOGO_RELEASE;
    while (relo.stage != RELO_ANIM) relo_advance(&relo, &in);
    CHECK(cds.midi.playing && cds.midi.timer && midi_tempo(&cds.midi) == 170 && !cds.music.playing);
    while (relo.stage == RELO_ANIM) relo_advance(&relo, &in);
    CHECK(cds.midi_prog == SOUND_PROG_NONE);
    relo.music_data = NULL;
    const char *rm[] = {"DATA/M/BINTRO.SCT", "DATA/M/BFANF.SCT", "DATA/M/REFANF.SCT", "DATA/M", "DATA", ""};
    for (int i = 0; i < 6; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s", dir, rm[i]);
        if (i < 3) remove(path);
        else rmdir(path);
    }
    /* the start programs' sound interrupt: every 42nd unit of the running program, before that unit's step */
    Startup su;
    memset(&su, 0, sizeof su);
    su.units = 40;
    CHECK(!startup_sound_due(&su));
    su.units = 41;
    CHECK(startup_sound_due(&su));
}

static void test_startup(void) {
    int p[8];
    CHECK(startup_plan(1, 0, 0, p) == 4 && p[0] == START_PRE && p[1] == START_RELO && p[2] == START_BINT && p[3] == START_GAME);
    CHECK(startup_plan(0, 1, 0, p) == 3 && p[2] == START_GAME); /* no intro; no CD, no tutorial */
    CHECK(startup_plan(1, 1, 1, p) == 5 && p[3] == START_ENCODE);
    CHECK(startup_plan(1, 0, 1, p) == 4 && p[3] == START_GAME);

    char dir[] = "biing_test_intro_XXXXXX";
    if (!mkdtemp(dir)) {
        CHECK(0);
        return;
    }
    char path[256];
    CHECK(!pre_marker_exists(dir));
    startup_config(&st, dir); /* no BIING!.CFG: the intro on, the tutorial off */
    CHECK(st.intro_on && !st.tutorial_on && !st.cd);
    snprintf(path, sizeof path, "%s/BIING!.CFG", dir);
    FILE *f = fopen(path, "wb");
    fputs("INTRO      -N | x\r\nTUTORIAL   -J\r\nQUAD -J\r\n", f);
    fclose(f);
    snprintf(path, sizeof path, "%s/CDC", dir);
    f = fopen(path, "wb");
    fclose(f);
    startup_config(&st, dir);
    CHECK(!st.intro_on && st.tutorial_on && st.quad && st.cd);

    /* the marker is written before the text; programs whose files are missing are skipped */
    startup_open(&st, dir, NULL, 1, -1, 1);
    CHECK(pre_marker_exists(dir) && startup_done(&st) && st.failed == 3); /* PRE, RELO and ENCODE */
    startup_free(&st);
    snprintf(path, sizeof path, "%s/SECRET.MRK", dir);
    f = fopen(path, "rb");
    char mark[16] = {0};
    CHECK(f && fread(mark, 1, 16, f) == 10 && !memcmp(mark, "SECRET.MRK", 10));
    if (f) fclose(f);
    remove(path);
    startup_open(&st, dir, NULL, 0, -1, 1); /* a --shot run writes nothing */
    CHECK(!pre_marker_exists(dir));
    startup_free(&st);
    snprintf(path, sizeof path, "%s/BIING!.CFG", dir);
    remove(path);
    snprintf(path, sizeof path, "%s/CDC", dir);
    remove(path);
    rmdir(dir);
}

/* FIX-31 (docs/fixes.md): off T[128] = -804, T[191] = T[192] = -32,767; on T[128] = 0, T[191] = -32,757, T[192] =
 * -32,767. Record 97 (134, 90, 80, 60) at t = 43: off (95, 89), on (95, 90). */
static void test_fix31_orbit_table(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 30 : 0);
        int16_t T[321];
        bint_orbit_table(intro_quarter(), T);
        CHECK(T[128] == (on ? 0 : -804) && T[191] == (on ? -32757 : -32767) && T[192] == -32767);
        CHECK(T[0] == 0 && T[64] == 32767 && T[256] == 0 && T[320] == 32767);
        int x, y;
        bint_orbit(134, 90, 80, 60, 43, &x, &y);
        CHECK(x == 95 && y == (on ? 90 : 89));
    }
    fixes_set(0);
}

/* FIX-32 (docs/fixes.md): a run reaching the 64 KB boundary (256, 102) from the row above: off the pixel keeps its old
 * value, on it is written */
static void test_fix32_bank_pixel(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 31 : 0);
        static uint8_t b[1400], img[640 * 440];
        BintPhas p;
        memset(b, 0, sizeof b);
        memcpy(b, "PHAS", 4), b[5] = 1, b[6] = b[7] = 0xFF;
        size_t n = 8 + 256;
        b[n++] = 1, b[n++] = 0, b[n++] = 101, b[n++] = 0x82, b[n++] = 4, b[n++] = 5, b[n++] = 6;
        n += 255;
        b[n++] = 1, b[n++] = 0, b[n++] = 204, b[n++] = 0x81, b[n++] = 7, b[n++] = 8;
        CHECK(bint_phas_parse(&p, b, n + 200, 0) == 0);
        memset(img, 3, sizeof img);
        bint_phas_draw(&p, 0, img, 0, 440);
        CHECK(img[101 * 640 + 256] == 4 && img[102 * 640 + 256] == (on ? 5 : 3) && img[103 * 640 + 256] == 6);
        CHECK(img[204 * 640 + 512] == 7 && img[205 * 640 + 512] == 8);
    }
    fixes_set(0);
}

int main(void) {
    test_fix31_orbit_table();
    test_fix32_bank_pixel();
    make_font();
    test_pre_layout();
    test_pre_find_text();
    test_pre_clock();
    test_relo_camera();
    test_relo_project();
    test_relo_fill();
    test_relo_scene();
    test_relo_place();
    test_relo_pieces();
    test_bint_rules();
    test_bint_caption();
    test_bint_caption_step();
    test_bint_tables();
    test_bint_phas_pbm();
    test_bint_flow();
    test_cd_tracks();
    test_module_branch();
    test_midi_programs();
    test_hsprite_palette();
    test_bint_drawers();
    test_bint_compose();
    test_startup();
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] intro tests passed\n");
    return 0;
}
