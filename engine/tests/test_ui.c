/* Screen layer, button timing and text output. Synthetic fixtures only (see AGENTS.md). Images are never
 * found on disk here, so the draws print "[ERROR] cannot open" lines; that is expected. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dulog.h"
#include "fixes.h"
#include "game.h"
#include "text.h"

static int failures;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            failures++;                                                            \
        }                                                                          \
    } while (0)

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void put32(uint8_t *p, uint32_t v) {
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}

static void test_script_parse(void) {
    static uint8_t img[0x10000];
    memset(img, 0, sizeof img);
    put16(img + 0xB95A + 2 * 1, 8);
    put16(img + 0xB95A + 2 * 8, 4);
    put16(img + 0xB95A + 2 * 9, 6);
    put16(img + 0xB95A + 2 * 16, 54);
    put16(img + 0xB95A + 2 * 17, 2);
    uint8_t *p = img + 0x100;
    put16(p, 9), put32(p + 2, 0x1234), p += 6;
    put16(p, 1), put16(p + 2, 7), put16(p + 4, 2), put16(p + 6, (uint16_t)-3), p += 8;
    put16(p, 8), put16(p + 2, 5), p += 4;
    put16(p, 16), put16(p + 2, 3), put16(p + 8, (uint16_t)-2), put16(p + 12, 10), put16(p + 18, 11);
    put16(p + 40, 0), put32(p + 42, 0x2222), put32(p + 50, 0x3333), p += 54;
    put16(p, 17);
    Script s;
    CHECK(script_parse(img, sizeof img, 0x100, &s) == 0);
    CHECK(s.frame == 0x1234);
    CHECK(s.count == 3);
    CHECK(s.rec[0].op == OP_DRAW && s.rec[0].image == 7 && s.rec[0].dx == 2 && s.rec[0].dy == -3);
    CHECK(s.rec[1].op == OP_CONDITION && s.rec[1].flag == 5);
    CHECK(s.rec[2].op == OP_HOTSPOT && s.rec[2].id == 3 && s.rec[2].aw == -2 && s.rec[2].hover == 10);
    CHECK(s.rec[2].normal == 11 && s.rec[2].click == 0x2222 && s.rec[2].enter == 0 && s.rec[2].leave == 0x3333);
    put16(img + 0x100, 12); /* opcode with length 0 = invalid */
    CHECK(script_parse(img, sizeof img, 0x100, &s) != 0);
}

/* A one-screen world: hotspot 1 (hover image 10 at 100,50, 10 x 5) and, behind a condition on flag 4,
 * hotspot 2 (hover image 12 at 300,50). */
static Game g_game;
static Program g_prog;
static Surface g_screen;
static uint32_t g_calls[16];
static int g_ncalls;

static void record_call(Dulog *d, uint32_t r) {
    (void)d;
    if (g_ncalls < 16) g_calls[g_ncalls++] = r;
}

static Record hotspot(uint16_t id, uint16_t hover, uint32_t click, uint32_t enter, uint32_t leave) {
    Record r = {0};
    r.op = OP_HOTSPOT;
    r.id = id;
    r.hover = hover;
    r.click = click;
    r.enter = enter;
    r.leave = leave;
    return r;
}

static void world(Dulog *d) {
    memset(&g_game, 0, sizeof g_game);
    snprintf(g_game.root, sizeof g_game.root, "/nonexistent-biing-test");
    g_game.ixy[10] = (IxyEntry){10, 5, 100, 50};
    g_game.ixy[12] = (IxyEntry){10, 5, 300, 50};
    memset(&g_prog, 0, sizeof g_prog);
    Script *s = &g_prog.screens[1];
    s->rec[0] = hotspot(1, 10, 0xC1, 0xE1, 0x11);
    s->rec[1] = (Record){.op = OP_CONDITION, .flag = 4};
    s->rec[2] = hotspot(2, 12, 0xC2, 0, 0);
    s->count = 3;
    surface_clear(&g_screen, 0);
    g_ncalls = 0;
    dulog_init(d, &g_game, &g_prog, &g_screen, record_call, NULL);
    dulog_set_screen(d, 1);
}

static void test_hit_edges(void) {
    Dulog d;
    world(&d);
    const Record *r = &g_prog.screens[1].rec[0];
    CHECK(!dulog_hit(&d, r, 100, 51)); /* left edge exclusive */
    CHECK(dulog_hit(&d, r, 101, 51));
    CHECK(dulog_hit(&d, r, 110, 55)); /* right and bottom inclusive */
    CHECK(!dulog_hit(&d, r, 111, 55));
    CHECK(!dulog_hit(&d, r, 105, 50)); /* top exclusive */
    CHECK(!dulog_hit(&d, r, 105, 56));
    Record wider = *r;
    wider.aw = 2;
    wider.ax = -1;
    CHECK(dulog_hit(&d, &wider, 100, 51) && dulog_hit(&d, &wider, 111, 51) && !dulog_hit(&d, &wider, 112, 51));
    dulog_free(&d);
}

static void test_condition_hides_next_hotspot(void) {
    Dulog d;
    world(&d);
    d.mouse_x = 305, d.mouse_y = 52;
    dulog_frame(&d);
    CHECK(d.hovered == HOVER_NONE);
    d.flags[4] = 1;
    dulog_frame(&d);
    CHECK(d.hovered == 2);
    dulog_free(&d);
}

static void test_enter_leave_and_click_at_press_position(void) {
    Dulog d;
    world(&d);
    d.mouse_x = 105, d.mouse_y = 52;
    dulog_frame(&d);
    CHECK(d.hovered == 1 && g_ncalls == 1 && g_calls[0] == 0xE1);
    d.mouse_x = 200; /* leave */
    dulog_frame(&d);
    CHECK(d.hovered == HOVER_NONE && g_ncalls == 2 && g_calls[1] == 0x11);
    /* pressed on hotspot 1, mouse already elsewhere when the frame runs: the click still lands there */
    g_ncalls = 0;
    d.click_pending = 1, d.click_x = 105, d.click_y = 52;
    dulog_frame(&d);
    int clicked = 0;
    for (int i = 0; i < g_ncalls; i++) clicked |= g_calls[i] == 0xC1;
    CHECK(clicked);
    /* a target screen switches without calling the click routine */
    g_prog.screens[1].rec[0].target = 7;
    g_ncalls = 0;
    d.click_pending = 1, d.click_x = 105, d.click_y = 52;
    dulog_frame(&d);
    CHECK(d.current == 7);
    for (int i = 0; i < g_ncalls; i++) CHECK(g_calls[i] != 0xC1);
    dulog_free(&d);
}

static void test_button_repeat_timing(void) {
    static Biing b;
    memset(&b, 0, sizeof b);
    int clicks[8], n = 0;
    for (int t = 1; t <= 40; t++) {
        biing_interrupt(&b, 1);
        if (b.ui.click_pending) {
            if (n < 8) clicks[n++] = t;
            b.ui.click_pending = 0;
        }
    }
    CHECK(n == 4 && clicks[0] == 1 && clicks[1] == 22 && clicks[2] == 30 && clicks[3] == 38);
    biing_interrupt(&b, 0);
    biing_interrupt(&b, 1);
    CHECK(b.ui.click_pending); /* a new press clicks at once */
}

/* docs/screens.md: retained ends are x + width/y + height, and touching either end invalidates.
 * Establish bounds through automatic hover rather than planting renderer state. */
static void test_retained_hover_edges(void) {
    Dulog d;
    world(&d);
    CHECK(d.hover_rect.x0 == 0 && d.hover_rect.y0 == 0 && d.hover_rect.x1 == 0 && d.hover_rect.y1 == 0);
    static uint8_t pixels[50], dot[1];
    Image hover = {10, 10, 5, 100, 50, 0, pixels, NULL};
    Image probe = {11, 1, 1, 0, 0, 0, dot, NULL};
    d.cache[10] = &hover, d.cache[11] = &probe;
    Record *r = &g_prog.screens[1].rec[0];
    r->enter = r->leave = 0;
    r->hover_dx = 2, r->hover_dy = 3;
    d.mouse_x = 105, d.mouse_y = 52;
    dulog_frame(&d);
    CHECK(d.hovered == 1 && d.hover_rect.x0 == 102 && d.hover_rect.y0 == 53 &&
          d.hover_rect.x1 == 112 && d.hover_rect.y1 == 58);
    static const struct { int x, y, overlaps; } C[] = {
        {112, 54, 1}, {113, 54, 0}, {101, 54, 1}, {100, 54, 0},
        {105, 58, 1}, {105, 59, 0}, {105, 52, 1}, {105, 51, 0},
        {112, 58, 1}, {1, 1, 0}
    };
    for (size_t i = 0; i < sizeof C / sizeof *C; i++) {
        probe.x = C[i].x, probe.y = C[i].y;
        dulog_draw_quiet(&d, 11, 0, 0);
        CHECK(d.hovered == 1);
        dulog_draw(&d, 11, 0, 0);
        CHECK(d.hovered == (C[i].overlaps ? HOVER_NONE : 1));
        CHECK(d.hover_rect.x0 == 102 && d.hover_rect.y0 == 53 && d.hover_rect.x1 == 112 && d.hover_rect.y1 == 58);
        dulog_frame(&d);
        CHECK(d.hovered == 1);
    }
    d.cache[10] = d.cache[11] = NULL; /* synthetic stack-owned images */
    dulog_free(&d);
}

/* Idle blanking (docs/gui.md, "Odds and ends", "Idle blanking", "What the engine must change"; SCR-7): the mouse
 * step clears the count on every interrupt in which the quarter-pixel x differs from y, so only a cursor on the
 * diagonal blanks; the button (held or not) does not matter. The doc's test values. */
static void idle_reset(Biing *b) {
    memset(b, 0, sizeof *b);
    for (int i = 0; i < 768; i++) b->assets.palette[i] = (uint8_t)(i % 251 + 1);
}
static void idle_run(Biing *b, int n) {
    for (int t = 0; t < n; t++) biing_interrupt(b, t % 100 < 3);
}
static void test_idle_blanking(void) {
    static Biing b;
    idle_reset(&b); /* (0, 0) at program start with no move: black after 15,000 */
    idle_run(&b, 14999);
    CHECK(!b.blanked && biing_palette(&b) == b.assets.palette);
    idle_run(&b, 1);
    CHECK(b.blanked);
    const uint8_t *pal = biing_palette(&b);
    int black = 1;
    for (int i = 0; i < 768; i++) black &= pal[i] == 0;
    CHECK(black && pal != b.assets.palette);
    idle_run(&b, 50000);
    CHECK(b.blanked); /* stays black on the diagonal */

    idle_reset(&b); /* (80, 1720) for 20,000: never black */
    b.mouse_qx = 80, b.mouse_qy = 1720;
    idle_run(&b, 20000);
    CHECK(!b.blanked);
    idle_reset(&b); /* (401, 400) for 20,000: never black, though the display pixel is (100, 100) */
    b.mouse_qx = 401, b.mouse_qy = 400;
    idle_run(&b, 20000);
    CHECK(!b.blanked);

    idle_reset(&b); /* (400, 400) from an off-diagonal start: picture after 14,998, black after 14,999 */
    b.mouse_qx = 80, b.mouse_qy = 1720;
    idle_run(&b, 10000);
    b.mouse_qx = b.mouse_qy = 400; /* the arrival, as a --shot move: then one interrupt */
    idle_run(&b, 1);
    idle_run(&b, 14998);
    CHECK(!b.blanked);
    idle_run(&b, 1);
    CHECK(b.blanked);
    b.mouse_qx = b.mouse_qy = 800; /* along the diagonal: stays black */
    idle_run(&b, 3);
    CHECK(b.blanked);
    b.mouse_qx = 480, b.mouse_qy = 400; /* off it: the picture again after 2 */
    idle_run(&b, 1);
    CHECK(b.blanked);
    idle_run(&b, 1);
    CHECK(!b.blanked && biing_palette(&b) == b.assets.palette);
    b.mouse_qx = b.mouse_qy = 400; /* back on it: the count runs from the clear again */
    idle_run(&b, 14999);
    CHECK(!b.blanked);
    idle_run(&b, 1);
    CHECK(b.blanked);

    idle_reset(&b); /* the count carries across moves along the diagonal: 10,000 at (400, 400), 5,000 at (800, 800) */
    b.mouse_qx = b.mouse_qy = 400;
    idle_run(&b, 10000);
    b.mouse_qx = b.mouse_qy = 800;
    idle_run(&b, 4999);
    CHECK(!b.blanked);
    idle_run(&b, 1);
    CHECK(b.blanked);
}

/* FIX-1 (docs/fixes.md): the doc's test values, switch off (the original's diagonal rule) and on (motion clears) */
static void test_fix1_idle(void) {
    static Biing b;
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 0 : 0);
        idle_reset(&b); /* resting at (80, 1720) for 20,000: off never black, on black 15,000 after the arrival */
        b.mouse_qx = 80, b.mouse_qy = 1720;
        idle_run(&b, 1);
        idle_run(&b, 14999);
        CHECK(!b.blanked);
        idle_run(&b, 1);
        CHECK(b.blanked == on);
        idle_run(&b, 4999);
        CHECK(b.blanked == on);

        idle_reset(&b); /* (400, 400) after arriving from (80, 1720): off black after 14,999, on after 15,000 */
        b.mouse_qx = 80, b.mouse_qy = 1720;
        idle_run(&b, 10000);
        b.mouse_qx = b.mouse_qy = 400;
        idle_run(&b, 1);
        idle_run(&b, 14998);
        CHECK(!b.blanked);
        idle_run(&b, 1);
        CHECK(b.blanked == !on);
        idle_run(&b, 1);
        CHECK(b.blanked);
        b.mouse_qx = b.mouse_qy = 800; /* black at (400, 400), then a move to (800, 800) */
        idle_run(&b, 1);
        CHECK(b.idle_ticks == (on ? 0 : -1));
        idle_run(&b, 1);
        CHECK(b.blanked == !on); /* off stays black, on the picture again */

        idle_reset(&b); /* (401, 400) at rest: off never black, on black after 15,000 */
        b.mouse_qx = 401, b.mouse_qy = 400;
        idle_run(&b, 1);
        idle_run(&b, 14999);
        CHECK(!b.blanked);
        idle_run(&b, 1);
        CHECK(b.blanked == on);
    }
    fixes_set(0);
}

/* 0x3B464 (docs/desktop.md, "The desktop"): the clock runs, image 1040, screen 97, the background sound 0xFB */
static void test_desktop_entry(void) {
    static Biing b;
    static uint8_t one = 1;
    static Image desk = {1040, 1, 1, 0, 0, 0, &one, NULL};
    memset(&b, 0, sizeof b);
    b.screen.w = SCREEN_W, b.screen.h = SCREEN_H;
    b.ui.screen = &b.screen;
    b.ui.hover_rect = (Rect){0, 0, -1, -1};
    b.ui.cache[1040] = &desk;
    b.clock_stopped = 1;
    biing_desktop(&b);
    CHECK(!b.clock_stopped && b.ui.current == 97 && b.screen.px[0] == 1 && b.sound.bg_request == 0xFB);
}

/* SCR-15: office entry clears player 0's door flag, but leaves its pending animation to the next frame.
 * A saved hover must not suppress all future animations; player 1's state is unrelated. */
static void test_office_entry_hover(void) {
    static Biing b;
    static uint8_t pixel = 1;
    static Image image = {1, 1, 1, 0, 0, 0, &pixel, NULL};
    static const uint8_t door[] = {0, 1, 127, 255};
    static const uint32_t values[] = {1, 1, 1, 1, 1};
    memset(&b, 0, sizeof b);
    world_alloc(&b.world);
    sim_init(&b.sim, &b.world, &b.prog, 0);
    dulog_init(&b.ui, &b.assets, &b.prog, &b.screen, NULL, &b);
    b.ui.cache[1] = b.ui.cache[24] = b.ui.cache[25] = b.ui.cache[501] = &image;
    b.screen.w = SCREEN_W, b.screen.h = SCREEN_H;
    uint8_t *p = b.world.block[BLK_PLAYER0], *other = b.world.block[BLK_PLAYER1];
    memset(other, 0xA5, b.world.size[BLK_PLAYER1]);
    for (size_t i = 0; i < sizeof door; i++) {
        p[0x36] = door[i];
        p[0x3B] = 0xFF;
        b.clock_stopped = 1;
        b.screen.px[0] = 0;
        biing_routine(&b, 0x2C79B);
        CHECK(p[0x36] == 0 && p[0x3B] == 0xFF);
        CHECK(!b.clock_stopped && b.ui.current == 1 && b.screen.px[0] == 1 && b.sound.bg_request == 0xFC);
        SimScript script = {values, 5, 0, 0, NULL};
        b.sim.rng_script = &script;
        biing_routine(&b, 0x2984A);
        CHECK(p[0x36] == 0 && p[0x3B] == 0 && script.used == 5 && script.extra == 0);
        b.sim.rng_script = NULL;
        for (size_t j = 0; j < b.world.size[BLK_PLAYER1]; j++) CHECK(other[j] == 0xA5);
    }
    world_free(&b.world);
}

static uint32_t office_ambience_probe(void *ctx, uint32_t mask) {
    CHECK(mask == 63);
    ++*(int *)ctx;
    return 0; /* the ambience helper stops at its own gate, before selecting any sound assets */
}

/* SCR-16: bits outside each mask do not cause retries. The disabled phone gate consumes no raw value. */
static void test_office_sound_gates(void) {
    static Biing b;
    static const uint32_t values[2][5] = {{8, 128, 1, 1, 1}, {9, 129, 1, 1, 1}};
    memset(&b, 0, sizeof b);
    world_alloc(&b.world);
    sim_init(&b.sim, &b.world, &b.prog, 0);
    dulog_init(&b.ui, &b.assets, &b.prog, &b.screen, NULL, &b);
    b.sound.ambience = 1;
    b.sound.bits = office_ambience_probe;
    for (int event = 0; event < 2; event++) {
        for (int reject = 0; reject < 2; reject++) {
            int ambience_calls = 0;
            b.sound.random_ctx = &ambience_calls;
            b.sound.queue[1][0].id = 42;
            b.sound.queue[1][1].id = 43;
            world_set_tail(&b.world, 0x12, event);
            SimScript script = {values[reject], 5, 0, 0, tmpfile()};
            CHECK(script.trace != NULL);
            b.sim.rng_script = &script;
            biing_routine(&b, 0x2984A);
            b.sim.rng_script = NULL;
            CHECK(script.used == (size_t)(1 + event) && script.extra == 0);
            CHECK(ambience_calls == !reject);
            CHECK(b.sound.queue[1][0].id == (event && !reject ? 0xC6 : 42));
            CHECK(b.sound.queue[1][1].id == (event && !reject ? 0 : 43));
            if (script.trace) {
                char got[80] = {0}, expected[80];
                rewind((FILE *)script.trace);
                size_t n = fread(got, 1, sizeof got - 1, (FILE *)script.trace);
                snprintf(expected, sizeof expected, event ? "0 b7 %d 1\n0 b127 %d 1\n" : "0 b7 %d 1\n",
                         reject, reject);
                CHECK(n > 0 && !strcmp(got, expected));
                fclose((FILE *)script.trace);
            }
        }
    }
    world_free(&b.world);
}

static void test_number_format(void) {
    Message m;
    msg_clear(&m);
    msg_append_number(&m, 984);
    msg_append_number(&m, -500);
    msg_append_number(&m, 2000000000L);
    CHECK(m.len == strlen("984 -500 999999999 ") && memcmp(m.buf, "984 -500 999999999 ", m.len) == 0);
}

/* Font: every slot 4 px wide and 2 high in colour 1, except '-' (slot 79) in colour 9. */
static Font g_font;
static uint8_t g_charmap[256];
static uint8_t g_pixels[128][8];

static TextLayout layout(int width) {
    memset(&g_font, 0, sizeof g_font);
    g_font.height = 2;
    for (int k = 0; k < 99; k++) {
        g_font.width[k] = 4;
        memset(g_pixels[k], k == 79 ? 9 : 1, 8);
        g_font.glyph[k] = g_pixels[k];
    }
    for (int c = 0; c < 256; c++) g_charmap[c] = 0;
    g_charmap['-'] = 79;
    return (TextLayout){&g_font, g_charmap, 10, 20, width, 1, 3, 1};
}

static void test_left_wraps_at_spaces(void) {
    TextLayout l = layout(30);
    surface_clear(&g_screen, 0);
    const char *t = "aaa aaa aaa$ignored";
    CHECK(text_left(&g_screen, &l, (const uint8_t *)t, strlen(t)) == 3);
}

static void test_left_hyphenates_at_underscore(void) {
    TextLayout l = layout(30);
    surface_clear(&g_screen, 0);
    const char *t = "aaaa_aaaa";
    CHECK(text_left(&g_screen, &l, (const uint8_t *)t, strlen(t)) == 2);
    CHECK(g_screen.px[20 * SCREEN_W + 10 + 20] == 9); /* '-' at the end of line 1 (4 glyphs of 5 px) */
    CHECK(g_screen.px[23 * SCREEN_W + 10] == 1); /* line 2 starts at the left: line height 2 + gap 1 */
}

static void test_centred_indent(void) {
    TextLayout l = layout(40);
    surface_clear(&g_screen, 0);
    const char *t = "aa";
    CHECK(text_centred(&g_screen, &l, (const uint8_t *)t, strlen(t)) == 1);
    /* width 2 x 5 = 10, D = 10 + space 3, indent (40 - 13 + 3) / 2 = 15 */
    CHECK(g_screen.px[20 * SCREEN_W + 10 + 14] == 0);
    CHECK(g_screen.px[20 * SCREEN_W + 10 + 15] == 1);
}

static void test_centred_space_break(void) {
    TextLayout l = layout(30);
    surface_clear(&g_screen, 0);
    const char *t = "a_a_a aa";
    CHECK(text_centred(&g_screen, &l, (const uint8_t *)t, strlen(t)) == 2);
    /* line 1 "a_a_a ": 3 glyphs of 5 px + 1 px per hyphenation mark (measured, docs/gui.md) + space 3 = D 20,
     * indent (30 - 20 + 3) / 2 = 6; line 2 "aa" at the end: D = 10 + 3, indent 10 */
    CHECK(g_screen.px[20 * SCREEN_W + 10 + 5] == 0);
    CHECK(g_screen.px[20 * SCREEN_W + 10 + 6] == 1);
    CHECK(g_screen.px[23 * SCREEN_W + 10 + 9] == 0);
    CHECK(g_screen.px[23 * SCREEN_W + 10 + 10] == 1);
}

static void test_centred_hyphen_break(void) {
    TextLayout l = layout(30);
    surface_clear(&g_screen, 0);
    const char *t = "aaa_aaa";
    CHECK(text_centred(&g_screen, &l, (const uint8_t *)t, strlen(t)) == 2);
    /* line 1 "aaa-": 15 + 1 for the break's own mark = D 16, no further gap (measured, docs/gui.md);
     * hyphen 5: indent (30 - 16 - 5 + 3) / 2 = 6, the hyphen after the third glyph */
    CHECK(g_screen.px[20 * SCREEN_W + 10 + 5] == 0);
    CHECK(g_screen.px[20 * SCREEN_W + 10 + 6] == 1);
    CHECK(g_screen.px[20 * SCREEN_W + 10 + 6 + 15] == 9);
}

static void test_dac_levels(void) {
    /* COL >> 2 into the 6-bit DAC, shown expanded (docs/formats.md): entry 135 (7, 255, 0) is (4, 255, 0) */
    CHECK(dac_level(7) == 4 && dac_level(255) == 255 && dac_level(0) == 0);
    CHECK(dac_level(19) == 16 && dac_level(35) == 32 && dac_level(139) == 138);
    uint8_t pal[768] = {0};
    pal[3 * 135] = 7, pal[3 * 135 + 1] = 255;
    CHECK(palette_rgb(pal, 135) == 0xFF04FF00u);
}

/* the oracle's case (docs/gui.md): with font-1 numbers (letter 1, space 5, dash 8) "ab_cd_ef_gh ij" in a 23-px box
 * breaks after "ab" (D 15, indent 2): the _ test comes before the mark adds its spacing. The test font here has
 * 4-px glyphs (advance 5) and space 3, so the same edge sits at "a_b" in a 19-px box: width 5 at the mark, 5 + 5 < 19
 * passes, D = 6, indent (19 - 6 - 5 + 3) / 2 = 5; with the old order (6 + 5 < 19 too) both pass, so the edge case is
 * W = 11: 5 + 5 < 11 breaks (new), 6 + 5 < 11 does not (old). */
static void test_centred_mark_edge(void) {
    TextLayout l = layout(11);
    surface_clear(&g_screen, 0);
    const char *t = "a_b";
    CHECK(text_centred(&g_screen, &l, (const uint8_t *)t, strlen(t)) == 2);
    /* line 1 "a-": D = 6, h = 5, indent (11 - 6 - 5 + 3) / 2 = 1 */
    CHECK(g_screen.px[20 * SCREEN_W + 10 + 0] == 0);
    CHECK(g_screen.px[20 * SCREEN_W + 10 + 1] == 1);
    CHECK(g_screen.px[20 * SCREEN_W + 10 + 6] == 9); /* the hyphen */
}

/* a line with no break point draws the rest up to the end on one line, D = 0 (docs/gui.md) */
static void test_centred_no_break(void) {
    TextLayout l = layout(8);
    surface_clear(&g_screen, 0);
    const char *t = "aaaa";
    CHECK(text_centred(&g_screen, &l, (const uint8_t *)t, strlen(t)) == 1);
    /* D = 0, indent (8 - 0 + 3) / 2 = 5; the four glyphs run over the edge from x 5 */
    CHECK(g_screen.px[20 * SCREEN_W + 10 + 4] == 0);
    CHECK(g_screen.px[20 * SCREEN_W + 10 + 5] == 1);
    CHECK(g_screen.px[20 * SCREEN_W + 10 + 5 + 15] == 1);
}

int main(void) {
    test_script_parse();
    test_hit_edges();
    test_condition_hides_next_hotspot();
    test_enter_leave_and_click_at_press_position();
    test_button_repeat_timing();
    test_retained_hover_edges();
    test_idle_blanking();
    test_fix1_idle();
    test_number_format();
    test_desktop_entry();
    test_office_entry_hover();
    test_office_sound_gates();
    test_left_wraps_at_spaces();
    test_left_hyphenates_at_underscore();
    test_centred_indent();
    test_centred_space_break();
    test_centred_hyphen_break();
    test_centred_mark_edge();
    test_centred_no_break();
    test_dac_levels();
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] all checks passed\n");
    return 0;
}
