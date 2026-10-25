/* The site map (screen 5, docs/ui-sitemap.md) and the core rooms with the patient sheet (docs/ui-rooms.md):
 * reception 19 with 57, 64, 149, 109; waiting room 29 with 59; treatment room 23 with 61 and 98; Krankenzimmer 38
 * with 62; the OK boxes 53, 58, 60, 63, 92, 95. The rules are in rooms.c (and the player branches of sim_rooms.c);
 * this file draws, builds the texts and moves between the screens. Sounds: room_sound.c's requests and ambience, called
 * where the original's view and frame routines make them (docs/sound.md). Formerly: no sounds: the sound requests of the specs
 * are left out. Message boxes do not stop the clock (as on the estate agent's and personnel screens). */
#include "room_sound.h"
#include "rooms_ui.h"

#include <stdio.h>
#include <stdlib.h>

#include "rooms.h"
#include "sheet.h"
#include "sim_internal.h"

enum {
    IMG_SITE_MAP = 44, IMG_POSTER = 1161, IMG_RACE = 1162, IMG_SELECTOR = 332, IMG_DIGIT_0 = 25,
    IMG_RECEPTION = 172, IMG_DESK_PATIENT = 162, IMG_QUEUE_PATIENT = 371, IMG_RECEPTION_SHOW = 519,
    IMG_WAITING = 198, IMG_SEAT_0 = 525, IMG_STARE_0 = 348, IMG_WAITING_SHOW = 522, IMG_TREATMENT = 184,
    IMG_WOMAN = 149, IMG_MAN = 376, IMG_DOCTOR_FACE = 1317, IMG_WARD = 219, IMG_BED_0 = 558, IMG_WARD_STARE_0 = 385,
    IMG_WARD_SHOW = 568, IMG_FOLDER = 535, IMG_PAGE_0 = 536, IMG_PAGE_1 = 542, IMG_PAGE_2 = 545, IMG_FRAME = 311,
    IMG_TICK = 537, IMG_DOT = 16, IMG_BAR = 898, IMG_CORPSE = 908, IMG_STRIP = 1140, IMG_REMARKS = 576,
    IMG_CHART = 577, IMG_MOOD = 949, IMG_ASSIGN = 405, IMG_OCCUPANCY = 1333, IMG_BED_BUTTON = 962, IMG_ADVICE_BOARD = 1001,
    IMG_ADVICE_THUMB = 1003,
    PIC_OK = 509, PIC_BACK = 510, PIC_NO = 512, PIC_NOT_YET = 513, PIC_SORRY = 514, PIC_BAD = 515, PIC_NIGHT = 517,
    PIC_NONE = 518,
};
enum {
    SCR_SITE_MAP = 5, SCR_RECEPTION = 19, SCR_WAITING = 29, SCR_TREATMENT = 23, SCR_WARD = 38, SCR_DESK = 57,
    SCR_SEAT = 59, SCR_COUCH = 61, SCR_BEDSIDE = 62, SCR_ASSIGN = 64, SCR_OCCUPANCY = 149, SCR_OVERVIEW = 109,
    SCR_BED_BOX = 98, SCR_RECEPTION_BOX = 53, SCR_WAITING_BOX = 58, SCR_TREATMENT_BOX = 60, SCR_WARD_BOX = 63,
    SCR_COUCH_BOX = 92, SCR_BEDSIDE_BOX = 95, SCR_RACE = 133, SCR_ADVICE = 2,
};
enum { P_SEX = 0, P_PROFESSION = 1, P_PRIVATE = 2, P_STATE = 4, P_AGE = 8, P_HEIGHT = 9, P_WEIGHT = 10,
       P_OPINION = 20, P_HUNGER = 40, P_AILMENT = 41, P_DIAGNOSIS = 42, P_TREATMENT = 43, P_HEALTH = 44,
       P_PORTRAIT = 46, P_ILLNESS = 56, P_WISH = 57, P_MARKS = 64, P_ROOM = 66, P_PREV_HEALTH = 67, P_MOOD = 78,
       P_IN_TREATMENT = 79, P_IN_STEP = 93, P_FIELD100 = 100, P_RECOVERY = 102, P_DEAD = 104 };
#define ILLNESSES 0x954

static Sim *sim(Biing *b) { return &b->sim; }
static int pb(Biing *b, int off) { return rooms_pb16(sim(b), off); }
static uint8_t *pbb(Biing *b) { return rooms_pb(sim(b)); }
static uint8_t *fb(Biing *b) { return b->ui.flags; }
static void draw(Biing *b, int image, int dx, int dy) { dulog_draw(&b->ui, image, dx, dy); }
static void quiet(Biing *b, int image, int dx, int dy) { dulog_draw_quiet(&b->ui, image, dx, dy); }
static void screen(Biing *b, int n) { dulog_set_screen(&b->ui, n); }
static void call(Biing *b, uint32_t r) { b->ui.call(&b->ui, r); }
static int u16(const uint8_t *p) { return (uint16_t)get16(p); }
static uint8_t *person(Biing *b, int index) { return rooms_person(sim(b), index); }
static uint8_t *patient(Biing *b) { return person(b, pb(b, RB_PATIENT)); }
static int hour(Biing *b) { return biing_clock(b, TAIL_HOUR); }
static const uint8_t *illness(Biing *b, int id) { return program_table(&b->prog, ILLNESSES) + id * 20; }

/* draw an image with its top-left corner at (x, y) instead of its IXY position */
static void draw_at(Biing *b, int image, int x, int y) {
    const Image *img = dulog_image(&b->ui, image);
    if (img) dulog_draw(&b->ui, image, x - img->x, y - img->y);
}

/* floor(a / b) for b > 0 */
static int floor_div(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/* The chart's lines: one pixel per step of the longer axis, the other axis at start + k d / n rounded half up
 * (floor(k d / n + 1/2)). Fitted to the original's renders (22 segments with |dy| <= dx = 8, two patients;
 * docs/ui-rooms.md, the health chart); Bresenham differs at the ties. */
static void line(Biing *b, int x0, int y0, int x1, int y1, uint8_t colour) {
    int dx = x1 - x0, dy = y1 - y0, n = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    for (int k = 0; k <= n; k++) {
        int x = n ? x0 + floor_div(2 * k * dx + n, 2 * n) : x0, y = n ? y0 + floor_div(2 * k * dy + n, 2 * n) : y0;
        if (x >= 0 && x < SCREEN_W && y >= 0 && y < 400) b->screen.px[y * SCREEN_W + x] = colour;
    }
}

/* ---- text ------------------------------------------------------------------------------------------- */

static void clear(Biing *b) { msg_clear(&b->msg); }
static void text(Biing *b, int entry) { biing_msg_entry(b, entry); }
static void num(Biing *b, long v) { msg_append_number(&b->msg, v); }
static void num_tight(Biing *b, long v) { /* "n-": the trailing space removed */
    num(b, v);
    if (b->msg.len) b->msg.len--;
}
static void pool(Biing *b, int which, int i) {
    size_t n;
    const uint8_t *p = datadb_pool_entry(&b->people, which, i, &n);
    if (p) msg_append(&b->msg, p, n);
}
static void staff_name(Biing *b, int index) { pool(b, DAT_STAFF_NAMES, index); }
static void patient_name(Biing *b, int index) { pool(b, DAT_PATIENT_NAMES, index); } /* 0x2BE79 */
static void him_her(Biing *b, int index) { text(b, person(b, index)[P_SEX] ? 515 : 514); }

static TextLayout layout(Biing *b, int x, int y, int width, int gap) {
    return (TextLayout){&b->fonts[1], b->prog.charmap, x, y, width, gap, 5, 1};
}
/* docs/ui-rooms.md's "plain" is 0x37E9E, the word-wrap renderer on one line at an address (docs/gui.md), not the
 * plain renderer 0xC770 that shows '_' as a glyph: the names' hyphenation marks are skipped */
static void plain(Biing *b, int x, int y) {
    TextLayout l = layout(b, x, y, 280, 0);
    text_left(&b->screen, &l, b->msg.buf, b->msg.len);
}
static void left(Biing *b, int x, int y, int width) {
    TextLayout l = layout(b, x, y, width, 0);
    text_left(&b->screen, &l, b->msg.buf, b->msg.len);
}
static void centred(Biing *b, int x, int y, int width) {
    TextLayout l = layout(b, x, y, width, 0);
    text_centred(&b->screen, &l, b->msg.buf, b->msg.len);
}
static void field(Biing *b, long v, int x, int y, int width) {
    clear(b);
    num(b, v);
    centred(b, x, y, width);
}
static void entry_at(Biing *b, int entry, int x, int y, int width) {
    clear(b);
    text(b, entry);
    centred(b, x, y, width);
}

/* the message box over what is on screen, then its OK screen */
static void box(Biing *b, int picture, int scr) {
    biing_message_box(b, picture);
    screen(b, scr);
}
static void box_text(Biing *b, int picture, int entry, int scr) {
    clear(b);
    text(b, entry);
    box(b, picture, scr);
}
static void box_name(Biing *b, int picture, int before, int index, int after, int scr) { /* a patient's name */
    clear(b);
    text(b, before);
    patient_name(b, index);
    text(b, after);
    box(b, picture, scr);
}
static void box_staff(Biing *b, int picture, int before, int staff, int after, int scr) {
    clear(b);
    text(b, before);
    staff_name(b, staff);
    text(b, after);
    box(b, picture, scr);
}

/* 0x322E3: the invoice, text 14 + 2c + name + 15 + 2c + " " */
static void invoice_box(Biing *b, const RoomsResult *o, int scr) {
    clear(b);
    text(b, 14 + 2 * o->reaction);
    patient_name(b, o->index);
    text(b, 15 + 2 * o->reaction);
    text(b, 386);
    box(b, PIC_SORRY, scr);
    sound_invoice(&b->sound, o->reaction); /* 0x32336: the list 0x4A2A + 48 c */
}

/* ---- the room selector -------------------------------------------------------------------------------- */

static void selector(Biing *b, int flag, int sx, int sy) {
    if (!fb(b)[flag]) return;
    int n = rooms_shown(sim(b)) + 1;
    quiet(b, IMG_SELECTOR, sx, sy);
    quiet(b, IMG_DIGIT_0 + n / 10 % 10, sx - 123, sy - 93);
    quiet(b, IMG_DIGIT_0 + n % 10, sx - 110, sy - 93);
}

/* ---- the site map (screen 5) ---------------------------------------------------------------------------- */

static const uint16_t SITE_PART[19] = {0, 35, 36, 37, 38, 40, 42, 76, 85, 86, 39, 41, 75, 19, 43, 21, 20, 23, 22};
/* by hotspot id: the category and the leave image of the 17 buildings (docs/ui-sitemap.md, "Hotspots") */
static const struct { uint8_t cat; uint16_t leave; } SITE_HOT[22] = {
    [1] = {2, 46},   [2] = {3, 50},   [3] = {4, 38},   [4] = {7, 76},   [5] = {8, 56},   [6] = {6, 42},
    [7] = {9, 86},   [8] = {11, 41},  [9] = {15, 64},  [10] = {16, 66}, [13] = {12, 72}, [15] = {13, 78},
    [16] = {5, 80},  [17] = {14, 43}, [18] = {18, 22}, [20] = {17, 54}, [21] = {10, 39},
};

static int race_word(Biing *b) { return u16(b->world.block[BLK_34]); } /* [0x552A], save block 34 */

static void site_map_draw(Biing *b) { /* 0x37253: the map and the site-map song */
    sound_background(&b->sound, SOUND_SITE_MAP_SONG, 0x140, 16); /* 0x37262: CDREAM, "Piano Dreams (Karte)" */
    draw(b, IMG_SITE_MAP, 0, 0);
    if (race_word(b)) draw(b, IMG_POSTER, 0, 0);
    if (rooms_count(sim(b), 1))
        for (int k = 1; k <= 18; k++)
            if (rooms_count(sim(b), k)) draw(b, SITE_PART[k], 0, 0);
    screen(b, SCR_SITE_MAP);
}

static void site_map(Biing *b) { /* 0x3724E: 0x2FA07 (sound clear) + 0x37253; 0x2EE24 is left out by design */
    sound_clear(&b->sound);
    site_map_draw(b);
}

static void site_map_frame(Biing *b) { /* 0x1F3BC */
    Sound *s = &b->sound;
    /* 0x1F3E8: the song would block the queues, so a pending help jingle or race announcement gets 0xF2 at level 1
     * (the period is not named in docs/sound.md: 0x140, engine reading); the [0x5CAA] request has no writer */
    if ((s->help_pending || s->race_pending) && s->bg_playing == SOUND_SITE_MAP_SONG) sound_background(s, 0xF2, 0x140, 1);
    if (race_word(b) && b->sim.race_redraw) site_map_draw(b), b->sim.race_redraw = 0; /* the race-start redraw */
}

/* the record of the current screen whose enter or leave routine is r */
static const Record *hotspot_of(Biing *b, uint32_t r, int *enter) {
    if (b->ui.current <= 0 || b->ui.current >= SCREEN_COUNT) return NULL;
    const Script *sc = &b->prog.screens[b->ui.current];
    for (int i = 0; i < sc->count; i++) {
        const Record *rec = &sc->rec[i];
        if (rec->op != OP_HOTSPOT) continue;
        if (rec->enter == r) return *enter = 1, rec;
        if (rec->leave == r) return *enter = 0, rec;
    }
    return NULL;
}

static int building_hover(Biing *b, uint32_t r) { /* the 34 enter/leave routines 0x31AB4-0x369B4 */
    int enter;
    const Record *rec = b->ui.current == SCR_SITE_MAP ? hotspot_of(b, r, &enter) : NULL;
    if (!rec || rec->id >= 22 || !SITE_HOT[rec->id].cat) return 0;
    if (rooms_count(sim(b), SITE_HOT[rec->id].cat)) draw(b, enter ? rec->hover : SITE_HOT[rec->id].leave, 0, 0);
    return 1;
}

/* a building without its room screen in the engine: the generic click (selection, news, background, selector) */
static int building_click(Biing *b, uint32_t r) {
    const SiteBuilding *bl = rooms_site_building(r);
    if (!bl) return 0;
    if (!rooms_site_click(sim(b), fb(b), bl)) return 1;
    draw(b, bl->background, 0, 0);
    if (bl->fb >= 0) selector(b, bl->fb, bl->sx, bl->sy);
    screen(b, bl->screen);
    fprintf(stderr, "[TODO] the room screen %d (overlays, hotspot routines): its own spec\n", bl->screen);
    return 1;
}

/* ---- the patient sheet (0x43547) -------------------------------------------------------------------------- */

/* a symptom text whose first byte is 0 (docs/ui-rooms.md, the complaints field) */
static int bare_symptom(Biing *b, const uint8_t *p) {
    size_t n;
    const uint8_t *t = textdb_text(&b->texts, 3291 + p[P_AILMENT], &n);
    return n == 0 && t[0] == 0;
}

static void severity_symptom(Biing *b, const uint8_t *p) {
    size_t n;
    const uint8_t *t = textdb_text(&b->texts, 3291 + p[P_AILMENT], &n);
    if (n == 0 && t[0] == 0) { /* printed from its second byte, without the severity word (textdb_text stops at the 0) */
        t = textdb_raw(&b->texts, 3291 + p[P_AILMENT], &n);
        size_t k = 1;
        while (k < n && t[k] != '$' && t[k] != 0) k++;
        n = k;
        msg_append(&b->msg, t + 1, n - 1);
        return;
    }
    text(b, 2376 + (100 - p[P_HEALTH]) / 16);
    msg_append(&b->msg, t, n);
}

static int psychological(Biing *b, const uint8_t *p) { return p[P_ILLNESS] && illness(b, p[P_ILLNESS])[8] == 2; }

/* the hours in bed of a ward patient (the bed word +2) */
static int bed_hours(Biing *b, int cat) {
    const uint8_t *k = rooms_record(sim(b), cat, rooms_shown(sim(b)));
    return u16(k + 0x16 + 4 * pb(b, RB_PLACE));
}

/* the record of the room P is in (docs/ui-departments.md: category = state, index = field 66) */
static const uint8_t *room_of(Biing *b, const uint8_t *p) {
    int state = p[P_STATE];
    return rooms_record(sim(b), state, state == 18 ? 0 : p[P_ROOM]);
}

static void hours_phrase(Biing *b, int h) {
    if (h == 0) text(b, 2228);
    else if (h == 1) text(b, 2229);
    else if (h == 24) text(b, 2230);
    else if (h < 24) num(b, h), text(b, 2242);
    else {
        if (h / 24 == 1) text(b, 2226);
        else num(b, h / 24), text(b, 2241);
        if (h % 24) hours_phrase(b, h % 24);
    }
}

static int ends_sentence(Biing *b) {
    size_t n = b->msg.len;
    while (n && b->msg.buf[n - 1] == ' ') n--;
    return n && (b->msg.buf[n - 1] == '!' || b->msg.buf[n - 1] == '.');
}

static void remarks_sentence(Biing *b, const uint8_t *p, int state) {
    int n = rooms_shown(sim(b)) + 1;
    if (state == 2) {
        text(b, 2244), num(b, u16(rooms_reception(sim(b)) + 12) + 2), text(b, 2247);
    } else if (state == 3) {
        const uint8_t *w = rooms_record(sim(b), 3, p[P_ROOM]);
        text(b, 2243), num(b, u16(w + 0x16 + 4 * pb(b, RB_PLACE)) + 2), text(b, 2246), num(b, p[P_ROOM] + 1), text(b, 2245);
    } else if (state == 5) {
        const uint8_t *t = rooms_record(sim(b), 5, rooms_shown(sim(b)));
        int diag = u16(t + 14), treat = u16(t + 16), cnt = u16(t + 18), blood = u16(t + 20);
        if (diag == 999) text(b, 2179), num(b, n), text(b, 2180);
        else if (diag) text(b, 2208), num(b, n), text(b, 2206);
        else if (!treat) text(b, 2219), num(b, n), text(b, 2218);
        else if (cnt == 999) text(b, 2215), num(b, n), text(b, 2216);
        else if (cnt && treat == 12) text(b, 2194), num(b, n), text(b, 2195), num(b, blood), text(b, 2196);
        else if (cnt && treat == 2) text(b, 2200), num(b, n), text(b, 2201);
        else if (cnt) text(b, 2202), num(b, n), text(b, 2203);
        else if (treat == 12) text(b, 2197), num(b, n), text(b, 2198), num(b, blood), text(b, 2196);
        else if (treat == 2) text(b, 2197), num(b, n), text(b, 2199);
        else text(b, 2209), num(b, n), text(b, 2210);
    } else if (state == 6) {
        text(b, 2227), hours_phrase(b, bed_hours(b, 6)), text(b, 2232), num(b, rooms_shown(sim(b)) + 1), text(b, 2231);
    } else if (state == 16) {
        text(b, 2227), hours_phrase(b, bed_hours(b, 16)), text(b, 2234), num(b, n), text(b, 2231);
    } else if (state == 9) {
        text(b, 2227), hours_phrase(b, u16(room_of(b, p) + 0x10)), text(b, 2233), num(b, n), text(b, 2231);
    } else if (state == 7 || state == 8) {
        const uint8_t *r = room_of(b, p);
        int op = state == 7;
        text(b, op ? 2227 : 2177), num(b, u16(r + 0x14));
        if (p[P_DEAD]) text(b, op ? 2220 : 2147);
        else if (u16(r + 14) != 999) text(b, op ? 2222 : 2145);
        else if (p[101]) text(b, op ? 2224 : 2146);
        else if (get16(r + 0x12) == -1) text(b, op ? 2223 : 2144);
        else text(b, op ? 2221 : 2148);
    } else if (state == 11 || state == 15) {
        const uint8_t *r = room_of(b, p);
        int diag = u16(r + 14), treat = u16(r + 16), cnt = u16(r + 18), d = state == 11;
        int a, z;
        if (diag == 999) a = d ? 2178 : 2191, z = d ? 2180 : 2190;
        else if (diag) a = d ? 2207 : 2189, z = d ? 2204 : 2188; /* the dental instrument (0x28F5E) is open */
        else if (!treat) a = d ? 2217 : 2187, z = d ? 2218 : 2186;
        else if (cnt == 999) a = d ? 2213 : 2185, z = d ? 2214 : 2184;
        else if (!cnt) a = d ? 2211 : 2187, z = d ? 2212 : 2181;
        else a = d ? 2192 : 2183, z = d ? 2193 : 2182;
        text(b, a), num(b, n), text(b, z);
        if (d && diag && diag != 999) text(b, 2205);
    } else if (state == 18) {
        text(b, 2227), num(b, u16(room_of(b, p) + 0x10)), text(b, 2225);
    }
}

static void remarks_clause(Biing *b, const uint8_t *p) { /* 0x287DA */
    if (ends_sentence(b)) return;
    int state = p[P_STATE], h = hour(b);
    if (p[P_DEAD]) text(b, 2255);
    else if (p[P_HEALTH] < 25) text(b, 2256);
    else if (p[P_RECOVERY]) text(b, state == 6 || state == 7 || state == 9 ? 2252 : 2253);
    else if (state == 6 && (h >= 22 || h < 3)) text(b, 2254);
    else if (p[P_WISH]) text(b, (int8_t)p[P_FIELD100] >= 2 ? 2248 : 3281 + p[P_WISH]);
    else if (!u16(p + P_ILLNESS)) text(b, 2250);
    else if (p[P_HEALTH] == 100) text(b, 2249);
    else if (bare_symptom(b, p)) severity_symptom(b, p); /* no 2251 "klagt über " either (docs/ui-rooms.md) */
    else text(b, 2251), severity_symptom(b, p);
}

static void remarks(Biing *b, const uint8_t *p, int state) {
    clear(b);
    remarks_sentence(b, p, state);
    remarks_clause(b, p);
    left(b, 214, 262, 253);
}

/* the state the sheet is drawn for: screen 57 uses the desk's sentence whatever P's state */
static int sheet_state(Biing *b, const uint8_t *p) { return b->ui.current == SCR_DESK ? 2 : p[P_STATE]; }

static void chart(Biing *b, const uint8_t *p) {
    int h[12];
    for (int i = 0; i < 11; i++) h[i] = p[77 - i];
    h[11] = p[P_HEALTH];
    /* dot i, then the line from it to dot i + 1: each dot covers the end of the line arriving at it and lies under the
     * line leaving it, as the original's render shows (docs/ui-rooms.md, the health chart) */
    for (int i = 0; i < 12; i++) {
        draw_at(b, IMG_DOT, 369 + 8 * i, 137 - h[i]);
        if (i < 11) line(b, 370 + 8 * i, 139 - h[i], 378 + 8 * i, 139 - h[i + 1], 0x95);
    }
}

static void measurements(Biing *b, const uint8_t *p, int y, int iq_y, int sheet) {
    if (p[P_SEX]) {
        field(b, p[5], 330, y, 50), field(b, p[6], 378, y, 50), field(b, p[7], 422, y, 50);
    } else {
        entry_at(b, 2176, 330, y, 50), entry_at(b, 2176, 378, y, 50), entry_at(b, 2176, 422, y, 50);
    }
    /* the "blöd" of a psychological case only on the patient sheet; the bed assignment's 0x24CD1 always prints the
     * number (docs/ui-rooms.md, section 64) */
    if (sheet && psychological(b, p)) entry_at(b, 2372, 422, iq_y, 50);
    else if (p[P_SEX]) entry_at(b, 2176, 422, iq_y, 50);
    else field(b, p[5], 422, iq_y, 50);
}

static void buttons(Biing *b, const uint8_t *p);
static void dead_bar(Biing *b, const uint8_t *p);

static void page_personal(Biing *b, const uint8_t *p, int index) {
    draw(b, IMG_PAGE_0, 0, 0);
    int portrait = u16(p + P_PORTRAIT);
    if (portrait) draw(b, portrait, 40, 6), draw(b, IMG_FRAME, 40, 6);
    chart(b, p);
    clear(b), patient_name(b, index), plain(b, 244, 158);
    clear(b), pool(b, DAT_PROFESSIONS, p[P_PROFESSION]), plain(b, 244, 171);
    draw_at(b, IMG_TICK, p[P_PRIVATE] ? 429 : 464, 171);
    field(b, p[P_AGE], 230, 184, 80);
    measurements(b, p, 184, 197, 1);
    field(b, p[P_HEIGHT], 230, 197, 80);
    field(b, p[P_WEIGHT], 346, 197, 50);
    clear(b), pool(b, DAT_TRAITS, u16(p + P_MARKS)), left(b, 214, 223, 250);
    buttons(b, p);
    dead_bar(b, p); /* 0x46BD6: before the remarks text, so it lies over the bar */
    remarks(b, p, sheet_state(b, p));
}

static void room_field(Biing *b, const uint8_t *p) {
    int state = p[P_STATE];
    if (state == 6 || state == 16 || state == 9) field(b, rooms_shown(sim(b)) + 1, 434, 28, 50);
    else if (state == 7 || state == 8) field(b, p[99] + 1, 434, 28, 50);
    else entry_at(b, 2176, 434, 28, 50);
}

static void line_at(Biing *b, int y) { left(b, 214, y, 250); }

/* 0x29391 on pb+0x49 (0x2837E / 0x2821F): the mood and opinion numbers and both text lines over image 949 (every
 * caller passes two non-zero arguments, docs/ui-rooms.md, "Sheet frame routines") */
static void mood_opinion(Biing *b, const uint8_t *p) {
    int mood = p[P_MOOD], opinion = p[P_OPINION];
    field(b, mood, 410, 262, 80);
    field(b, opinion, 410, 301, 80);
    clear(b);
    if (p[P_DEAD]) text(b, 2080);
    else if (p[P_HEALTH] < 25) text(b, 2081);
    else text(b, (p[P_HEALTH] < 65 ? 2390 : 2397) + mood / 16);
    line_at(b, 275);
    clear(b);
    if (p[P_DEAD]) text(b, 2082);
    else if (p[P_HEALTH] < 25) text(b, 2083 + opinion / 16);
    else text(b, 2383 + opinion / 16);
    line_at(b, 314);
}

/* Page 1: the mood and opinion numbers, then the seven lines of sheet.c (docs/ui-sheets.md, section 1) */
static void page_state(Biing *b, const uint8_t *p, int index) {
    draw(b, IMG_PAGE_1, 0, 0);
    clear(b), patient_name(b, index), plain(b, 244, 28);
    SheetCtx c = {sim(b), &b->texts, &b->people, &b->sheet, &b->msg};
    static SheetPage page;
    sheet_page1(&c, index, &page);
    /* the line at y 54 and the numbers, then the state's bar and buttons, then the room field and the other lines,
     * so a line that reaches y 338 lies over the bar (docs/ui-sheets.md 6.8) */
    for (int k = 0; k < SHEET_LINES; k++) {
        if (k == 1) {
            field(b, p[P_MOOD], 410, 262, 80);
            field(b, p[P_OPINION], 410, 301, 80);
            buttons(b, p);
            room_field(b, p);
        }
        if (!page.drawn[k]) continue; /* a guarded hang of the original: nothing drawn (docs/engine.md) */
        b->msg = page.line[k];
        line_at(b, SHEET_LINE_Y[k]);
    }
}

static int billed(int u, int priv) { return u && priv ? u + u / 2 + 1 : u; } /* 0x2E217 */

static void illness_name(Biing *b, const uint8_t *p) { text(b, 3210 + p[P_ILLNESS]); } /* 0x27F9C, variant left out */

/* the pathology's verdict in words (docs/ui-departments.md, "Page 2"): 2757 = the clinic's fault, 2758 = none */
static void autopsy(Biing *b, const uint8_t *p) {
    clear(b);
    int blood = (int16_t)get16(p + 94);
    if (p[P_WISH]) text(b, 2755), text(b, blood >= 680 ? 2756 : 2759), text(b, 2757);
    else if (!p[P_DIAGNOSIS]) text(b, 2760), illness_name(b, p), text(b, 2761), text(b, 2758);
    else if (p[P_DIAGNOSIS] != p[P_ILLNESS]) text(b, 2763), illness_name(b, p), text(b, 2764), text(b, 2757);
    else if (!p[P_TREATMENT]) text(b, 2762), illness_name(b, p), text(b, 2761), text(b, 2758);
    else if (p[P_TREATMENT] != illness(b, p[P_ILLNESS])[11]) text(b, 2765), text(b, 2757);
    else if (blood < 680) text(b, 2766), text(b, 2757);
    else if (p[P_ROOM] == 6 && !p[P_IN_STEP]) text(b, 2768), text(b, 2757);
    else text(b, 2767), text(b, 2758);
    left(b, 214, 132, 250);
}

static void page_bill(Biing *b, const uint8_t *p, int index) {
    int state = p[P_STATE];
    draw(b, IMG_PAGE_2, 0, 0);
    clear(b), patient_name(b, index), plain(b, 244, 28);
    clear(b);
    if (!p[11]) text(b, 2143);
    for (int k = 0; k < 9 && p[11 + k]; k++) { /* 0x4365D: each illness with its variant, no separator before it */
        if (k) text(b, 1063);
        int id = p[11 + k], v = p[84 + k];
        if (id == 13) {
            text(b, 3118 + v);
            continue;
        }
        text(b, 3127 + id);
        int kind = illness(b, id)[0x10]; /* 0 or 2: none; 3: 3203 + v; else 3197 + v (v = 0 not tested) */
        if (kind != 0 && kind != 2) text(b, (kind == 3 ? 3203 : 3197) + v);
    }
    line_at(b, 54);
    if (p[P_WISH] != 5) {
        static const uint8_t FIELDS[7] = {50, 51, 52, 53, 48, 54, 55};
        for (int i = 0; i < 7; i++) field(b, billed(p[FIELDS[i]], p[P_PRIVATE]), 406, 132 + 13 * i, 80);
        int bill = sim_invoice(sim(b), 0, p);
        field(b, bill, 406, 236, 80);
        pbb(b)[RB_BILL] = (uint8_t)bill;
    }
    /* then the state's bar and buttons, then the lower lines (docs/ui-sheets.md 6.8) */
    if (state == 18) { /* the autopsy report once the examination is over (0x43AF5): image 1362 before the bar */
        int over = u16(rooms_record(sim(b), 18, 0) + 14) == 999;
        if (over) draw(b, 1362, 0, 0);
        buttons(b, p);
        if (over) autopsy(b, p);
        return;
    }
    buttons(b, p);
    /* the room field: "--" in the branches of states 2, 3, 5, 11 and 15 only, nothing for the others
     * (docs/ui-sheets.md 6.1) */
    if (state == 2 || state == 3 || state == 5 || state == 11 || state == 15) entry_at(b, 2176, 434, 28, 50);
    clear(b);
    int ward = state == 6 || state == 16;
    int due = ((state == 5 || state == 11 || state == 15) && p[P_IN_TREATMENT]) ||
              (ward && !u16(p + P_ILLNESS) && hour(b) >= 4 && hour(b) <= 21);
    if (due) {
        /* The lower-text helpers select P only on these branches (SIM-82 follow-up writer audit). */
        if (ward || p[P_WISH] != 5) sim_rating_record(sim(b), p);
        int v = sim_bill_reaction(p, sim_invoice(sim(b), 0, p), ward);
        text(b, 1742 + (v < -5 ? -5 : v > 3 ? 3 : v) + 5);
    } else {
        text(b, 1752);
    }
    line_at(b, 275);
}

/* the state's bar and buttons; a state not listed draws no bar (docs/ui-sheets.md 6.8) */
static void buttons(Biing *b, const uint8_t *p) {
    switch (p[P_STATE]) {
    case 2: case 3: case 5: case 6: case 7: case 8: case 9: case 11: case 15: case 16: case 18: break;
    default: return;
    }
    draw(b, IMG_BAR, 0, 0);
    switch (p[P_STATE]) {
    case 2: draw(b, 901, 0, 0), draw(b, 903, 0, 0); break;
    case 5: draw(b, 910, 0, 0), draw(b, 912, 0, 0), draw(b, 914, 0, 0), draw(b, 916, 0, 0); break;
    case 6:
        draw(b, p[P_IN_STEP] ? 914 : 913, 0, 0);
        draw(b, u16(p + P_ILLNESS) ? 903 : 1143, 0, 0);
        draw(b, 918, 0, 0);
        break;
    case 7: case 8: { /* docs/ui-departments.md, "Button bars" */
        int started = u16(room_of(b, p) + 14) != 999;
        draw(b, 922, 0, 0);
        draw(b, p[P_STATE] == 7 ? (started ? 923 : 924) : (started ? 1352 : 1351), 0, 0);
        break;
    }
    case 9: draw(b, 922, 0, 0); break;
    case 11: case 15: draw(b, 910, 0, 0), draw(b, 912, 0, 0), draw(b, 914, 0, 0), draw(b, 916, 0, 0); break;
    case 16:
        draw(b, p[P_IN_STEP] ? 914 : 913, 0, 0);
        draw(b, u16(p + P_ILLNESS) ? 903 : 1143, 0, 0);
        break;
    case 18: draw(b, IMG_CORPSE, 0, 0); break;
    default: break; /* 3: none */
    }
}

/* 0x27EC7: a dead P gets the bar again over the state's buttons, then the corpse button (docs/ui-sheets.md 6.6);
 * pages 1 and 2 run it after their lines, page 0 before its remarks (6.8) */
static void dead_bar(Biing *b, const uint8_t *p) {
    if (p[P_DEAD]) draw(b, IMG_BAR, 0, 0), draw(b, IMG_CORPSE, 0, 0);
}

static void sheet(Biing *b) { /* 0x43547 with pb+0x2C = P and pb+0xE = the page */
    int index = pb(b, RB_PATIENT);
    const uint8_t *p = person(b, index);
    draw(b, IMG_FOLDER, 0, 0);
    rooms_sheet_flags(sim(b), fb(b), 1);
    /* each page draws the state's bar and buttons itself, between its upper and lower lines (docs/ui-sheets.md 6.8) */
    switch (pb(b, RB_PAGE)) {
    case 1: page_state(b, p, index), dead_bar(b, p); break;
    case 2:
        page_bill(b, p, index);
        if (p[P_STATE] != 18) dead_bar(b, p); /* state 18 leaves page 2 without 0x27EC7 */
        break;
    default: page_personal(b, p, index); break;
    }
}

/* the specialist sheets 70, 75, 43, 84, 73, 126, 90 and the neuro-bed screen 81 (docs/ui-departments.md) */
static int specialist_sheet(int n) { return n == 70 || n == 75 || n == 43 || n == 84 || n == 73 || n == 126 || n == 90; }
static int sheet_screen(int n) {
    return n == SCR_DESK || n == SCR_SEAT || n == SCR_COUCH || n == SCR_BEDSIDE || n == SCR_ASSIGN ||
           n == SCR_OVERVIEW || n == SCR_OCCUPANCY || specialist_sheet(n) || n == 81;
}

/* the hover help of the sheet buttons (0x2956F ... 0x29551, leave 0x295C3 ... 0x29647) by the hover image */
static int hint_of(Biing *b, int hover) {
    switch (hover) {
    case 899: {
        int n = b->ui.current;
        switch (n) {
        case SCR_SEAT: return 777;
        case SCR_COUCH: return 778;
        case SCR_BEDSIDE: return 803;
        case 70: return 801;
        case 75: case 81: return 802;
        case 43: return 804;
        case 84: return 884;
        case 73: return 840;
        case 126: return 3015;
        case 90: return 2754;
        default: return 773;
        }
    }
    case 1141: return b->ui.current == 81 ? 802 : 773;
    case 900: return 774;
    case 902: return 775;
    case 907: return 776;
    case 543: return 772;
    case 909: return 779;
    case 911: return 780;
    case 913: return 781;
    case 915: return 782;
    case 917: return 808;
    case 921: return b->ui.current == 73 || b->ui.current == 126 ? 841 : 885;
    case 408: return 1645;
    case 410: return 1646;
    default: return 0;
    }
}

static void hint_strip(Biing *b, int hint) {
    draw(b, IMG_STRIP, 0, 0);
    if (!hint) return;
    clear(b), text(b, hint), left(b, 214, 366, 250);
}

static int sheet_hover(Biing *b, uint32_t r) {
    int enter;
    const Record *rec = sheet_screen(b->ui.current) ? hotspot_of(b, r, &enter) : NULL;
    if (!rec) return 0;
    int hover = rec->hover, normal = rec->normal, hint = hint_of(b, hover);
    const uint8_t *p = patient(b);
    if (r == 0x350A0 || r == 0x3511D) { /* 62, discharge: "rausschmeißen" with a case, else "entlassen" */
        int cured = !u16(p + P_ILLNESS);
        hover = cured ? 1142 : 902, normal = cured ? 1143 : 903, hint = cured ? 805 : 775;
    } else if (r == 0x3514F || r == 0x351B7) { /* 62 and 43, the step: the hover shows the other image */
        int running = p[P_IN_STEP] != 0;
        hover = running ? 913 : 914, normal = running ? 914 : 913, hint = running ? 807 : 806;
    } else if (r == 0x3524E || r == 0x352B8 || r == 0x352E9 || r == 0x35353) { /* 73 / 126: start or stop */
        int op = r == 0x3524E || r == 0x352B8, started = u16(room_of(b, p) + 14) != 999;
        if (op) hover = started ? 924 : 923, normal = started ? 923 : 924, hint = started ? 843 : 842;
        else hover = started ? 1351 : 1352, normal = started ? 1352 : 1351, hint = started ? 3014 : 3013;
    } else if (!normal) {
        normal = hover == 899 || hover == 1141 ? 904 : hover == 543 ? 544 : hover + 1;
    }
    if (enter) {
        draw(b, hover, rec->hover_dx, rec->hover_dy);
        hint_strip(b, hint);
    } else {
        draw(b, normal, 0, 0);
        hint_strip(b, 0);
    }
    return 1;
}

/* 0x29391, the page-1 part (docs/ui-rooms.md, "Sheet frame routines"): run by 57 on page 1, 59 on pages 1 and 2 and 75
 * on page 1 (before its own page-1 redraw); 61 and 62 do not run it. The mood box draws both text lines on every
 * caller. The redraw on pb+0x47 (or on a health change with the chart request pb+0x2F) clears pb+0x47 and pb+0x2F
 * after it, so the redraw's own step 3, which sets pb+0x47 again, makes one redraw (docs/ui-sheets.md 6.7). */
static void page1_part(Biing *b, const uint8_t *p) {
    uint8_t *q = pbb(b);
    if (q[RB_MOOD]) {
        draw(b, IMG_MOOD, 0, 0);
        mood_opinion(b, p);
        q[RB_MOOD] = 0;
    }
    if ((p[P_HEALTH] != p[P_PREV_HEALTH] && q[0x2F]) || q[RB_LOW_HEALTH]) {
        sheet(b);
        q[RB_LOW_HEALTH] = q[0x2F] = 0;
    }
}

/* the common frame of the sheets 57, 59, 61, 62 and the specialist sheets (docs/ui-rooms.md, "Sheet frame routines";
 * docs/ui-departments.md, "Sheet frame routines") */
static void sheet_frame(Biing *b) {
    /* Context-specific requests (docs/ui-rooms.md, UI-113). Zero means no independent page-1 request. */
    static const struct { int screen, remarks, chart, progress; } REQUESTS[] = {
        {57, 0x2E, 0x2F, 0}, {59, 0x2E, 0x30, 0}, {61, 0x32, 0x31, 0x32}, {62, 0x33, 0x34, 0x35},
        {43, 0x33, 0x37, 0x38}, {70, 0x3A, 0x39, 0x3A}, {73, 0x2E, 0x3E, 0x3F},
        {75, 0x41, 0x40, 0x41}, {84, 0x33, 0x42, 0x43}, {126, 0x2E, 0x9F, 0xA0}
    };
    uint8_t *q = pbb(b);
    const uint8_t *p = patient(b);
    /* 0x292D5 belongs to these frame paths, not to the shared renderer or its flag helper. */
    switch (b->ui.current) {
    case 43: case 57: case 59: case 61: case 62: case 70: case 73: case 75: case 84: case 126:
        sim_rating_record(sim(b), p);
        break;
    default: break;
    }
    rooms_sheet_flags(sim(b), fb(b), 0);
    int page = pb(b, RB_PAGE), n = b->ui.current;
    int remarks_flag = RB_MINUTE, chart_flag = 0, progress_flag = 0, contextual = 0;
    for (size_t i = 0; i < sizeof REQUESTS / sizeof *REQUESTS; i++) {
        if (REQUESTS[i].screen != n) continue;
        remarks_flag = REQUESTS[i].remarks, chart_flag = REQUESTS[i].chart, progress_flag = REQUESTS[i].progress;
        contextual = 1;
        break;
    }
    if (page == 0) {
        if (q[remarks_flag]) {
            draw(b, IMG_REMARKS, 0, 0);
            remarks(b, p, sheet_state(b, p));
            q[remarks_flag] = 0;
            if (progress_flag) q[progress_flag] = 0;
            if (n == 43) q[chart_flag] = 0; /* neuro ward: remarks consumes the chart request too */
        }
        if (chart_flag && q[chart_flag]) {
            draw(b, IMG_CHART, 0, 0);
            chart(b, p);
            q[chart_flag] = 0;
        }
    } else if (page == 1 && progress_flag) {
        if (n == 75) page1_part(b, p); /* 0x25C63: before its page-1 byte */
        if (q[progress_flag]) {
            sheet(b);
            q[progress_flag] = 0;
        }
    } else if (page == 1 && !contextual && specialist_sheet(n)) {
        /* Pathology keeps its existing handling; it is outside the contextual request table. */
        if (q[RB_MINUTE]) q[RB_MINUTE] = 0, sheet(b);
    } else if ((page == 1 && n != SCR_COUCH && n != SCR_BEDSIDE) || (page == 2 && n == SCR_SEAT)) {
        page1_part(b, p); /* 57 and 59 on page 1, 59 also on page 2 (0x24079 tests only page 0) */
    } else if (page == 2 && (n == SCR_COUCH || n == SCR_BEDSIDE || specialist_sheet(n)) && p[P_WISH] != 5 &&
               (uint8_t)sim_invoice(sim(b), 0, p) != q[RB_BILL]) {
        /* 0x24171's measured callers already selected P above; pathology's frame does not call that helper. */
        sheet(b);
    }
    if (p[P_DEAD] && q[RB_DIED]) { /* 0x29302 */
        sheet(b);
        draw(b, IMG_CORPSE, 0, 0);
        q[RB_DIED] = 0;
    }
    q[0x56] = 0; /* 0x371E9 */
}

/* ---- reception (19) and its screens -------------------------------------------------------------------- */

static void reception_view(Biing *b) { /* 0x33DF0 */
    const uint8_t *r = rooms_reception(sim(b));
    draw(b, IMG_RECEPTION, 0, 0);
    if (r[0x45]) draw(b, IMG_RECEPTION_SHOW, 0, 0);
    pbb(b)[RB_DESK_ICON] = 0;
    if (u16(r + 10)) draw(b, IMG_DESK_PATIENT, 0, 0), pbb(b)[RB_DESK_ICON] = 0xFF;
    if (u16(r + 0x46)) draw(b, IMG_QUEUE_PATIENT, 0, 0);
    room_sound_view(sim(b), 2);
}

static void reception(Biing *b) { /* 0x33D58 */
    pbb(b)[RB_DESK_HOVER] = 0;
    if (!rooms_count(sim(b), 2)) return;
    reception_view(b);
    screen(b, SCR_RECEPTION);
}

/* The reception frame (docs/ui-rooms.md, screen 19, "Frame"): the icons, on the animation tick the five tables (list
 * A the desk patient's head, list B the clerk, pick C the desk patient's face, pick D the undressed receptionist, list
 * E), then the sound (its draws after the animation's), then 0x371E9. */
#define RECEPTION_LIST_A 0x4C2A
#define RECEPTION_LIST_B 0x4BDA
#define RECEPTION_PICK_C 0x4CAE
#define RECEPTION_PICK_D 0x4C1E
#define RECEPTION_LIST_E 0x60B7
static void reception_frame(Biing *b) { /* 0x1FEF4 */
    uint8_t *q = pbb(b);
    const uint8_t *r = rooms_reception(sim(b));
    if (q[RB_DESK_ICON]) draw(b, IMG_DESK_PATIENT, 0, 0), q[RB_DESK_ICON] = 0;
    if (q[RB_QUEUE_ICON]) draw(b, IMG_QUEUE_PATIENT, 0, 0), q[RB_QUEUE_ICON] = 0;
    if (q[RB_TICK]) {
        int desk = u16(r + 10) != 0;
        if (desk && !q[RB_DESK_HOVER]) biing_anim_list(b, RECEPTION_LIST_A, 64, &b->reception_a);
        if (!r[0x45] && !q[RB_STAFF_HOVER]) { /* the quiet branch */
            if (u16(r + 8) && sim_bits(sim(b), 1) == 0) biing_anim_list(b, RECEPTION_LIST_B, 32, &b->reception_b);
            if (desk && sim_bits(sim(b), 7) == 0) biing_anim_pick(b, RECEPTION_PICK_C, &b->reception_c);
        } else { /* the show branch */
            if (r[0x45] && sim_bits(sim(b), 3) == 0) biing_anim_pick(b, RECEPTION_PICK_D, &b->reception_d);
            if (sim_bits(sim(b), 7) == 3) biing_anim_list(b, RECEPTION_LIST_E, 32, &b->reception_e);
        }
        q[RB_TICK] = 0;
    }
    room_sound_frame(sim(b), 0x1FEF4); /* every frame, after the animation's draws */
    q[0x56] = 0;                       /* 0x371E9 */
}

static void reception_box(Biing *b, int code, const RoomsResult *o) {
    switch (code) {
    case 975: box_text(b, PIC_BAD, 975, SCR_RECEPTION_BOX); break;
    case 539: box_staff(b, PIC_NO, 539, o->staff, 540, SCR_RECEPTION_BOX); break;
    case 974: box_text(b, PIC_SORRY, 974, SCR_RECEPTION_BOX); break;
    case 2371: box_text(b, PIC_NO, 2371, SCR_RECEPTION_BOX); break;
    case 2370: box_text(b, PIC_NOT_YET, 2370, SCR_RECEPTION_BOX); break;
    case 541: box_text(b, PIC_NO, 541, SCR_RECEPTION_BOX); break;
    case 2367:
        clear(b);
        text(b, 2367), num_tight(b, o->total), text(b, 2368), num_tight(b, o->total - o->free_beds), text(b, 2369);
        num(b, o->free_beds);
        box(b, PIC_NOT_YET, SCR_RECEPTION_BOX);
        break;
    default: break;
    }
}

static void desk(Biing *b) { /* 0x29167 */
    RoomsResult o = {0};
    int r = rooms_desk(sim(b), &o);
    if (r) {
        reception_box(b, r, &o);
        return;
    }
    sheet(b);
    screen(b, SCR_DESK);
}

static void admit(Biing *b, int to_waiting) { /* 0x2B966 / 0x2B976 -> 0x2B989 */
    RoomsResult o = {0};
    int r = rooms_admit(sim(b), to_waiting, &o);
    switch (r) {
    case 2266: box_name(b, PIC_SORRY, 2266, o.index, 2267, SCR_RECEPTION_BOX); break;
    case 2265: box_name(b, PIC_BAD, 2265, o.index, 2264, SCR_RECEPTION_BOX); break;
    case 2259: box_name(b, PIC_SORRY, 2259, o.index, 2260, SCR_RECEPTION_BOX); break;
    case 2257: case 2258: box_text(b, PIC_NONE, r, SCR_RECEPTION_BOX); break;
    default:
        clear(b);
        text(b, 2261), patient_name(b, o.index), text(b, 2262), num(b, o.room + 1), text(b, 2263);
        box(b, PIC_OK, SCR_RECEPTION_BOX);
    }
}

static void ward_beds(Biing *b, const uint8_t *k, int y, int with_health) { /* 0x24944 */
    int n = 0;
    for (int i = 0; i < 10; i++) {
        int index = u16(k + 0x14 + 4 * i);
        if (!index) continue;
        clear(b), patient_name(b, index), plain(b, 232, y + 13 * n);
        if (with_health) {
            clear(b), num(b, person(b, index)[P_HEALTH]), text(b, 981), centred(b, 384, y + 13 * n, 80);
            field(b, u16(k + 0x16 + 4 * i), 423, y + 13 * n, 80);
        }
        n++;
    }
    for (; n < k[10]; n++) clear(b), text(b, 2348), plain(b, 232, y + 13 * n);
}

static void ward_quality(Biing *b, const uint8_t *k, int y) {
    clear(b), text(b, 2349 + (k[0] / 8 > 10 ? 10 : k[0] / 8)), left(b, 214, y, 250);
}

static void assign_draw(Biing *b, int index, int cat) { /* 0x24C37 (64) and 0x36081 (81, the neuro ward) */
    const uint8_t *p = person(b, index);
    draw(b, IMG_FOLDER, 0, 0), draw(b, IMG_ASSIGN, 0, 0), draw(b, IMG_BAR, 0, 0);
    draw(b, 409, 0, 0), draw(b, 411, 0, 0), draw(b, 904, 0, 0), draw(b, 922, 0, 0);
    field(b, pb(b, RB_WARD) + 1, 434, 28, 50);
    clear(b), patient_name(b, index), plain(b, 244, 54);
    clear(b), pool(b, DAT_PROFESSIONS, p[P_PROFESSION]), plain(b, 244, 67);
    draw_at(b, IMG_TICK, p[P_PRIVATE] ? 429 : 464, 67);
    field(b, p[P_AGE], 230, 80, 80);
    measurements(b, p, 80, 93, 0);
    field(b, p[P_HEIGHT], 230, 93, 80);
    field(b, p[P_WEIGHT], 346, 93, 50);
    clear(b);
    if (p[P_WISH]) text(b, 3281 + p[P_WISH]);
    else text(b, 3210 + p[P_DIAGNOSIS]);
    left(b, 214, 132, 250);
    const uint8_t *k = rooms_record(sim(b), cat, pb(b, RB_WARD));
    ward_beds(b, k, 171, 0);
    ward_quality(b, k, 314);
}
static void assign_view(Biing *b) { assign_draw(b, u16(rooms_reception(sim(b)) + pb(b, RB_QUEUE)), 6); }

static void assign_open(Biing *b) { /* 0x24356 */
    RoomsResult o = {0};
    int r = rooms_bed_open(sim(b), &o);
    if (r) {
        reception_box(b, r, &o);
        return;
    }
    assign_view(b);
    screen(b, SCR_ASSIGN);
}

static void assign_step(Biing *b, int step) {
    rooms_bed_ward_step(sim(b), step);
    assign_view(b);
}

static void assign(Biing *b) { /* 0x23366 */
    RoomsResult o = {0};
    if (rooms_bed_assign(sim(b), &o) < 0) return;
    clear(b);
    text(b, 2362), staff_name(b, o.staff), text(b, 1341), patient_name(b, o.index), text(b, 2363);
    him_her(b, o.index), text(b, 2365), num(b, o.room + 1), text(b, 2366);
    box(b, PIC_OK, o.screen);
}

static void occupancy_view(Biing *b) { /* 0x2478A */
    int neuro = pb(b, RB_QUEUE) != 0, k_index = pb(b, RB_WARD);
    const uint8_t *k = rooms_record(sim(b), neuro ? 16 : 6, k_index);
    draw(b, IMG_FOLDER, 0, 0), draw(b, IMG_OCCUPANCY, 0, 0), draw(b, IMG_BAR, 0, 0);
    draw(b, 409, 0, 0), draw(b, 411, 0, 0), draw(b, 904, 0, 0);
    clear(b), text(b, neuro ? 2453 : 2454), num(b, k_index + 1), left(b, 214, 93, 250);
    field(b, rooms_count(sim(b), 6), 390, 54, 50);
    field(b, rooms_count(sim(b), 16), 390, 67, 50);
    clear(b);
    if (u16(k + 8)) staff_name(b, u16(k + 8));
    else text(b, 2452);
    left(b, 214, 314, 250);
    ward_beds(b, k, 132, 1);
    ward_quality(b, k, 275);
}

static void occupancy(Biing *b) { /* 0x242AF */
    int r = rooms_occupancy_open(sim(b));
    if (r) {
        box_text(b, PIC_NO, r, SCR_RECEPTION_BOX);
        return;
    }
    occupancy_view(b);
    screen(b, SCR_OCCUPANCY);
}

static void occupancy_frame(Biing *b) { /* 0x3D129 */
    if (!pbb(b)[RB_MINUTE]) return;
    pbb(b)[RB_MINUTE] = 0;
    occupancy_view(b);
}

static void overview(Biing *b, int first) { /* 0x2387C / 0x238CA */
    int r = rooms_overview(sim(b), first);
    if (r) {
        box_text(b, PIC_NO, r, SCR_RECEPTION_BOX);
        return;
    }
    screen(b, SCR_OVERVIEW); /* before the drawing: the remarks follow the state, not screen 57 */
    sheet(b);
    draw(b, IMG_BAR, 0, 0);
}

static void overview_frame(Biing *b) { /* 0x23BE8 */
    if (!pbb(b)[RB_MINUTE] || pb(b, RB_PAGE)) return;
    pbb(b)[RB_MINUTE] = 0;
    const uint8_t *p = patient(b);
    draw(b, IMG_REMARKS, 0, 0);
    remarks(b, p, p[P_STATE]);
}

static void show_reception(Biing *b) { /* 0x317B9 */
    if (rooms_show(sim(b), 2)) {
        box_text(b, PIC_BAD, 550, SCR_RECEPTION_BOX);
        return;
    }
    draw(b, IMG_RECEPTION_SHOW, 0, 0);
}

static void advice(Biing *b, int from) { /* 0x5B5D6 (reception) and 0x5B4D1 (the "Hilfe!" strip) */
    b->advice_from = from;
    draw(b, IMG_ADVICE_BOARD, 0, 0);
    draw(b, IMG_ADVICE_THUMB, 0, 0);
    clear(b), text(b, 2442);
    TextLayout l = {&b->fonts[0], b->prog.charmap, 136, 27, 370, 2, 9, 1};
    text_left(&b->screen, &l, b->msg.buf, b->msg.len);
    /* the work rules from 0x5B83D on are not specified yet: no advice, the advice number stays none */
    pbb(b)[0xA2] = pbb(b)[0xA3] = 0xFF;
    screen(b, SCR_ADVICE);
}

/* ---- waiting room (29, 59) ----------------------------------------------------------------------------- */

static uint8_t *waiting_room(Biing *b) { return rooms_record(sim(b), 3, rooms_shown(sim(b))); }
static uint32_t drawn_signature; /* engine choice: the rooms redraw when their occupants change (below) */

static uint32_t occupants(const uint8_t *rec) { /* the patient words of the ten places */
    uint32_t h = 2166136261u;
    for (int i = 0; i < 10; i++) h = (h ^ (uint32_t)get16(rec + 0x14 + 4 * i)) * 16777619u;
    return h;
}

static void waiting_view(Biing *b) { /* 0x32DA9 */
    uint8_t *w = waiting_room(b);
    pbb(b)[RB_WAITING_NEWS] = 0;
    draw(b, IMG_WAITING, 0, 0);
    for (int i = 0; i < 10; i++)
        if (u16(w + 0x14 + 4 * i)) draw(b, IMG_SEAT_0 + i, 0, 0);
    if (w[0x0F]) {
        for (int i = 0; i < 10; i++)
            if (u16(w + 0x14 + 4 * i)) draw(b, IMG_STARE_0 + 2 * i, 0, 0);
        draw(b, IMG_WAITING_SHOW, 0, 0);
    }
    selector(b, FB_WAITING, 0, 0);
    drawn_signature = occupants(w);
}

static void waiting(Biing *b) { /* 0x32D77 */
    if (!rooms_site_click(sim(b), fb(b), rooms_site_building(0x32D77))) return;
    waiting_view(b);
    room_sound_view(sim(b), 3); /* the view's request, made where the original enters the view (not on the engine's
                                   occupant redraws) */
    screen(b, SCR_WAITING);
}
static void waiting_back(Biing *b) { /* 0x3C07A / 0x3C08E */
    waiting_view(b);
    room_sound_view(sim(b), 3);
    screen(b, SCR_WAITING);
}

static void waiting_frame(Biing *b) { /* 0x23E02 */
    if (pbb(b)[RB_WAITING_NEWS] || occupants(waiting_room(b)) != drawn_signature) waiting_view(b);
    room_sound_frame(sim(b), 0x23E02); /* the level again, show music and ambience */
}

static void waiting_select(Biing *b, int step) {
    rooms_selector_step(sim(b), 3, step);
    waiting_view(b);
    room_sound_view(sim(b), 3);
}

static void show_waiting(Biing *b) { /* 0x316B3 */
    if (rooms_show(sim(b), 3)) {
        box_text(b, PIC_BAD, 551, SCR_WAITING_BOX);
        return;
    }
    const uint8_t *w = waiting_room(b);
    for (int i = 0; i < 10; i++)
        if (u16(w + 0x14 + 4 * i)) draw(b, IMG_STARE_0 + 2 * i, 0, 0);
    draw(b, IMG_WAITING_SHOW, 0, 0);
    selector(b, FB_WAITING, 0, 0);
}

static void seat(Biing *b, int i) { /* 0x34461 + 4 i */
    if (!rooms_seat(sim(b), i)) return;
    screen(b, SCR_SEAT);
    sheet(b);
}

static void seat_frame(Biing *b) { /* 0x24079 */
    if (patient(b)[P_STATE] != 3) {
        waiting_back(b);
        return;
    }
    sheet_frame(b);
}

static void corpse_box(Biing *b, int code, const RoomsResult *o, int scr) {
    if (code == 1340) {
        clear(b);
        text(b, 1340), staff_name(b, o->staff), text(b, 1347), patient_name(b, o->index), text(b, 1348);
        box(b, PIC_OK, scr);
    } else {
        box_text(b, PIC_SORRY, code, scr);
    }
}

static void corpse(Biing *b, int cat, int scr) { /* 0x55898, 0x55969, 0x58CAC, 0x4F26A */
    RoomsResult o = {0};
    if (cat == 6 && !patient(b)[P_DEAD]) return; /* 0x4F26A -> 0x5A484 only for a dead P */
    corpse_box(b, rooms_corpse(sim(b), cat, &o), &o, scr);
}

/* ---- treatment room (23, 61, 98) ------------------------------------------------------------------------- */

static uint8_t *treatment_room(Biing *b) { return rooms_record(sim(b), 5, rooms_shown(sim(b))); }
static int treated(Biing *b) { return u16(treatment_room(b) + 12); }

static void treatment_view(Biing *b) { /* 0x31BBE */
    const uint8_t *t = treatment_room(b);
    pbb(b)[RB_NURSE_HOVER] = pbb(b)[RB_TREAT_NEWS] = 0;
    draw(b, IMG_TREATMENT, 0, 0);
    selector(b, FB_TREATMENT, 215, 0);
    if (u16(t + 12)) draw(b, person(b, u16(t + 12))[P_SEX] ? IMG_WOMAN : IMG_MAN, 0, 0);
    drawn_signature = (uint32_t)u16(t + 12);
}

static void treatment_back(Biing *b) { /* 0x36D3C / 0x3C098 */
    treatment_view(b);
    room_sound_view(sim(b), 5); /* 0x31C79 */
    screen(b, SCR_TREATMENT);
}

static void treatment_entry(Biing *b) { /* 0x36D20 */
    if (!rooms_site_click(sim(b), fb(b), rooms_site_building(0x36D20))) return;
    treatment_back(b);
}

static void treatment_frame(Biing *b) { /* 0x2007F */
    uint8_t *q = pbb(b);
    room_sound_frame(sim(b), 0x2007F);
    const uint8_t *t = treatment_room(b);
    if (q[RB_TREAT_NEWS] || (uint32_t)u16(t + 12) != drawn_signature) treatment_view(b);
    if (!q[RB_NURSE_HOVER] && u16(t + 8) && u16(t + 10) && q[RB_TICK] && !sim_bits(sim(b), 3))
        quiet(b, IMG_DOCTOR_FACE + (int)sim_bits(sim(b), 1), 0, 0);
    q[RB_TICK] = 0;
}

static void treatment_select(Biing *b, int step) {
    rooms_selector_step(sim(b), 5, step);
    treatment_back(b); /* re-runs the entry 0x36D3C */
}

static void couch_sheet(Biing *b) {
    screen(b, SCR_COUCH);
    sheet(b);
}

static void couch(Biing *b, int fallback) { /* 0x23E9D (fallback: the site map) / 0x23EB3 */
    if (rooms_couch(sim(b))) {
        if (fallback) site_map(b);
        else box_text(b, PIC_NO, 398, SCR_TREATMENT_BOX);
        return;
    }
    couch_sheet(b);
}

static void treatment_box(Biing *b, int code, const RoomsResult *o) {
    int scr = SCR_TREATMENT_BOX;
    switch (code) {
    case 418: case 414: case 406: case 2371: box_text(b, PIC_NO, code, scr); break;
    case 410: box_text(b, PIC_NOT_YET, code, scr); break;
    case 500: box_text(b, PIC_NONE, code, scr); break;
    case 427: box_text(b, PIC_BAD, code, scr); break;
    case 77: box_text(b, PIC_SORRY, code, scr); break;
    case 539: box_staff(b, PIC_NO, 539, o->staff, 540, scr); break;
    case 1340:
        clear(b);
        text(b, 1340), staff_name(b, o->staff), text(b, 1341), patient_name(b, o->index), text(b, 1343);
        him_her(b, o->index), text(b, 510), num(b, o->room + 1), text(b, 511);
        box(b, PIC_OK, scr);
        break;
    case 14: invoice_box(b, o, scr); break;
    case 32:
        clear(b);
        text(b, 32), patient_name(b, o->index), text(b, 33), num(b, o->room + 1), text(b, 349);
        box(b, PIC_SORRY, scr);
        break;
    case 34: box_name(b, PIC_SORRY, 34, o->index, 35, scr); break;
    case 36: box_name(b, PIC_OK, 36, o->index, 37, scr); break;
    case 38: box_name(b, PIC_SORRY, 38, o->index, 39, scr); break;
    case 40: box_name(b, PIC_SORRY, 40, o->index, 41, scr); break;
    case 425:
        box_name(b, PIC_BACK, 425, o->index, 426, o->screen);
        if (o->bed_button) draw(b, IMG_BED_BUTTON, 0, 0);
        break;
    /* the sheet's refusals return to the sheet through screen 92 */
    case 42: box_staff(b, PIC_NO, 42, o->staff, 43, SCR_COUCH_BOX); break;
    case 89: case 87: case 86: box_text(b, PIC_NO, code, SCR_COUCH_BOX); break;
    case 84: box_text(b, PIC_NOT_YET, code, SCR_COUCH_BOX); break;
    case 78: case 79: box_text(b, PIC_BAD, code, SCR_COUCH_BOX); break;
    default: break;
    }
}

static void call_next(Biing *b) { /* 0x24015 */
    RoomsResult o = {0};
    treatment_box(b, rooms_call(sim(b), &o), &o);
}

static void decision(Biing *b, int (*rule)(Sim *, RoomsResult *)) {
    RoomsResult o = {0};
    int code = rule(sim(b), &o);
    if (code == 0) sheet(b); /* "treat in the room" and "other proposal" only redraw the sheet */
    else treatment_box(b, code, &o);
}

static void couch_frame(Biing *b) { /* 0x240F4 */
    if (pbb(b)[RB_MOVED]) {
        pbb(b)[RB_MOVED] = 0;
        treatment_back(b);
        return;
    }
    if (!treated(b)) { /* engine guard: the patient left the room while the sheet was open */
        treatment_back(b);
        return;
    }
    sheet_frame(b);
}

static void bed_box_assign(Biing *b) { /* 0x3C72A */
    int clerk = u16(rooms_reception(sim(b)) + 8);
    if (!rooms_count(sim(b), 6) || !clerk || rooms_independent(sim(b), clerk)) return;
    pbb(b)[RB_FROM_98] = 0xFF;
    assign_open(b);
}

static void bed_box_hover(Biing *b, int enter) { /* 0x3D208 / 0x3D23E */
    int clerk = u16(rooms_reception(sim(b)) + 8);
    if (rooms_count(sim(b), 6) && clerk && !rooms_independent(sim(b), clerk)) draw(b, enter ? 961 : 962, 0, 0);
}

static void inventory(Biing *b, int cat, uint32_t back) { /* 0x26DEC, 0x26E0E, 0x26E52, 0x26E95 */
    if (rooms_inventory(sim(b), cat, back)) {
        box_text(b, PIC_BAD, 375, SCR_TREATMENT_BOX);
        return;
    }
    call(b, 0x26F75); /* the inventory, screen 65 (purchase_ui.c) */
}

/* ---- Krankenzimmer (38, 62) -------------------------------------------------------------------------------- */

static uint8_t *ward_room(Biing *b) { return rooms_record(sim(b), 6, rooms_shown(sim(b))); }
static int ward_show(Biing *b) { return u16(ward_room(b) + 0x0F) != 0; } /* the word at K+0xF */
static int bed_patient(Biing *b, int i) {
    int index = u16(ward_room(b) + 0x14 + 4 * i);
    return index && person(b, index)[P_STATE] == 6 ? index : 0;
}

static void ward_view(Biing *b) { /* 0x36F0D */
    draw(b, IMG_WARD, 0, 0);
    for (int i = 0; i < 10; i++)
        if (bed_patient(b, i)) draw(b, IMG_BED_0 + i, 0, 0);
    if (ward_show(b)) {
        for (int i = 0; i < 10; i++)
            if (bed_patient(b, i)) draw(b, IMG_WARD_STARE_0 + 2 * i, 0, 0);
        draw(b, IMG_WARD_SHOW, 0, 0);
    }
    drawn_signature = occupants(ward_room(b));
}

static void ward_back(Biing *b) { /* 0x36D68 = 0x36D78 + screen 38 */
    pbb(b)[RB_WARD_NEWS] = 0;
    ward_view(b);
    room_sound_view(sim(b), 6); /* 0x36D97 */
    selector(b, FB_WARD, 184, 0);
    screen(b, SCR_WARD);
}

static void ward_entry(Biing *b) { /* 0x36D4C */
    if (!rooms_site_click(sim(b), fb(b), rooms_site_building(0x36D4C))) return;
    ward_back(b);
}

static void ward_frame(Biing *b) { /* 0x27588 */
    if (pbb(b)[RB_WARD_NEWS] || occupants(ward_room(b)) != drawn_signature) {
        pbb(b)[RB_WARD_NEWS] = 0;
        ward_view(b);
        selector(b, FB_WARD, 184, 0);
    }
    room_sound_frame(sim(b), 0x27588);
    pbb(b)[0x56] = 0;
}

static void ward_select(Biing *b, int step) {
    rooms_selector_step(sim(b), 6, step);
    ward_back(b);
}

static void show_ward(Biing *b) { /* 0x2EF50 */
    if (rooms_show(sim(b), 6)) {
        box_text(b, PIC_BAD, 548, SCR_WARD_BOX);
        return;
    }
    for (int i = 0; i < 10; i++)
        if (bed_patient(b, i)) draw(b, IMG_WARD_STARE_0 + 2 * i, 0, 0);
    draw(b, IMG_WARD_SHOW, 0, 0);
    selector(b, FB_WARD, 184, 0);
}

static void bed(Biing *b, int i) { /* 0x27C45 + 4 i */
    if (!rooms_bed_click(sim(b), i)) return;
    screen(b, SCR_BEDSIDE);
    sheet(b);
}

static void bedside_back(Biing *b) { /* 0x27CBF */
    pbb(b)[0x33] = pbb(b)[0x34] = pbb(b)[0x35] = 0;
    screen(b, SCR_BEDSIDE);
    sheet(b);
}

static void discharge(Biing *b) { /* 0x4F229 */
    RoomsResult o = {0};
    int r = rooms_discharge(sim(b), &o);
    if (r == 14) invoice_box(b, &o, SCR_WARD_BOX);
    else if (r == 2280) box_name(b, PIC_BAD, 2280, o.index, 2281, SCR_BEDSIDE_BOX);
    else box_text(b, r == 2282 ? PIC_NIGHT : PIC_BAD, r, SCR_BEDSIDE_BOX);
}

static void to_icu(Biing *b) { /* 0x4F383 */
    RoomsResult o = {0};
    int r = rooms_icu(sim(b), &o);
    if (r != 1340) {
        box_text(b, PIC_SORRY, r, SCR_BEDSIDE_BOX);
        return;
    }
    clear(b);
    text(b, 1340), staff_name(b, o.staff), text(b, 1341), patient_name(b, o.index), text(b, 1343), text(b, 1342);
    num(b, o.room + 1);
    box(b, PIC_OK, SCR_WARD_BOX);
}

static void bedside_frame(Biing *b) { /* 0x1F452 */
    if (patient(b)[P_STATE] != 6 && !patient(b)[P_DEAD]) { /* engine guard: P left the bed (ICU, discharge) */
        ward_back(b);
        return;
    }
    sheet_frame(b);
}

/* ---- hover routines of the room screens ---------------------------------------------------------------- */

static int show_runs(Biing *b, int cat) {
    if (cat == 2) return rooms_reception(sim(b))[0x45] != 0;
    if (cat == 3) return waiting_room(b)[0x0F] != 0;
    return ward_show(b);
}

static int room_hover(Biing *b, uint32_t r) {
    uint8_t *q = pbb(b);
    int woman = treated(b) && person(b, treated(b))[P_SEX], man = treated(b) && !woman;
    switch (r) {
    /* reception */
    case 0x33E60: draw(b, 257, 0, 0), q[RB_DESK_HOVER] = 0xFF; return 1;
    case 0x33E77: draw(b, 258, 0, 0), q[RB_DESK_HOVER] = 0; return 1;
    case 0x33E8E:
        draw(b, 264, 0, 0);
        if (u16(rooms_reception(sim(b)) + 0x46)) draw(b, IMG_QUEUE_PATIENT, 0, 0);
        return 1;
    case 0x329C0: q[RB_STAFF_HOVER] = 0xFF; if (!show_runs(b, 2)) draw(b, 613, 0, 0), draw(b, 322, 0, 0); return 1;
    case 0x32A1E: q[RB_STAFF_HOVER] = 0xFF; if (!show_runs(b, 2)) draw(b, 613, 0, 0), draw(b, 520, 0, 0); return 1;
    case 0x297CF: q[RB_STAFF_HOVER] = 0, draw(b, show_runs(b, 2) ? IMG_RECEPTION_SHOW : 323, 0, 0); return 1;
    /* waiting room */
    case 0x32921: if (!show_runs(b, 3)) draw(b, 316, 0, 0); return 1;
    case 0x31926: draw(b, show_runs(b, 3) ? IMG_WAITING_SHOW : 317, 0, 0), selector(b, FB_WAITING, 0, 0); return 1;
    case 0x3285B: if (!show_runs(b, 3)) draw(b, 326, 0, 0); return 1;
    case 0x31963:
        if (!show_runs(b, 3)) draw(b, 327, 0, 0), draw(b, 317, 0, 0);
        selector(b, FB_WAITING, 0, 0);
        return 1;
    case 0x317FC: if (show_runs(b, 3)) draw(b, 523, 0, 0); return 1;
    case 0x31828: if (show_runs(b, 3)) draw(b, 524, 0, 0); return 1;
    /* treatment room */
    case 0x36C4C: if (man) draw(b, 723, 0, 0); return 1;
    case 0x36C81: if (man) draw(b, 724, 0, 0); return 1;
    case 0x36CB6: if (woman) draw(b, 150, 0, 0); return 1;
    case 0x36CEB: if (woman) draw(b, 151, 0, 0); return 1;
    case 0x3613B: if (!treated(b)) draw(b, 185, 0, 0); return 1;
    case 0x3617B: if (!treated(b)) draw(b, 186, 0, 0); return 1;
    case 0x36C0A: draw(b, 1313, 0, 0), draw(b, 374, 0, 0), q[RB_NURSE_HOVER] = 0xFF; return 1;
    case 0x36C2B: draw(b, 1313, 0, 0), draw(b, 375, 0, 0), q[RB_NURSE_HOVER] = 0; return 1;
    case 0x361F3: if (!treated(b)) draw(b, 377, 0, 0); return 1;
    case 0x3620F: if (!treated(b)) draw(b, 378, 0, 0); return 1;
    case 0x361BB: if (!treated(b)) draw(b, 446, 0, 0); return 1;
    case 0x361D7: if (!treated(b)) draw(b, 447, 0, 0); return 1;
    /* ward */
    case 0x3297E: if (!show_runs(b, 6)) draw(b, 383, 0, 0); return 1;
    case 0x318AC: draw(b, show_runs(b, 6) ? IMG_WARD_SHOW : 384, 0, 0), selector(b, FB_WARD, 184, 0); return 1;
    case 0x328DF: if (!show_runs(b, 6)) draw(b, 381, 0, 0); return 1;
    case 0x4A3F9: if (!show_runs(b, 6)) ward_back(b); return 1;
    case 0x32B9F: if (!show_runs(b, 6)) draw(b, 448, 0, 0); return 1;
    case 0x32BC0: if (!show_runs(b, 6)) draw(b, 449, 0, 0); return 1;
    case 0x32B5D: if (!show_runs(b, 6)) draw(b, 1135, 0, 0); return 1;
    case 0x32B7E: if (!show_runs(b, 6)) draw(b, 1134, 0, 0); return 1;
    case 0x3D208: bed_box_hover(b, 1); return 1;
    case 0x3D23E: bed_box_hover(b, 0); return 1;
    default: break;
    }
    if (r >= 0x343A9 && r <= 0x343CD && (r - 0x343A9) % 4 == 0) { /* seat i enter */
        int i = (int)(r - 0x343A9) / 4;
        if (u16(waiting_room(b) + 0x14 + 4 * i) && !show_runs(b, 3)) draw(b, IMG_STARE_0 + 2 * i, 0, 0);
        return 1;
    }
    if (r >= 0x34405 && r <= 0x34429 && (r - 0x34405) % 4 == 0) {
        int i = (int)(r - 0x34405) / 4;
        if (u16(waiting_room(b) + 0x14 + 4 * i) && !show_runs(b, 3)) draw(b, IMG_STARE_0 + 1 + 2 * i, 0, 0);
        return 1;
    }
    if (r >= 0x277D5 && r <= 0x277F9 && (r - 0x277D5) % 4 == 0) { /* bed i enter */
        int i = (int)(r - 0x277D5) / 4;
        if (bed_patient(b, i) && !show_runs(b, 6)) draw(b, IMG_WARD_STARE_0 + 2 * i, 0, 0);
        return 1;
    }
    if (r >= 0x276C3 && r <= 0x276E7 && (r - 0x276C3) % 4 == 0) {
        int i = (int)(r - 0x276C3) / 4;
        if (bed_patient(b, i) && !show_runs(b, 6)) draw(b, IMG_WARD_STARE_0 + 1 + 2 * i, 0, 0);
        return 1;
    }
    return 0;
}

/* ---- shared with depts_ui.c (rooms_ui.h) -------------------------------------------------------------- */

void rooms_ui_clear(Biing *b) { clear(b); }
void rooms_ui_text(Biing *b, int entry) { text(b, entry); }
void rooms_ui_num(Biing *b, long v) { num(b, v); }
void rooms_ui_patient_name(Biing *b, int index) { patient_name(b, index); }
void rooms_ui_staff_name(Biing *b, int index) { staff_name(b, index); }
void rooms_ui_him_her(Biing *b, int index) { him_her(b, index); }
void rooms_ui_box(Biing *b, int picture, int scr) { box(b, picture, scr); }
void rooms_ui_box_text(Biing *b, int picture, int entry, int scr) { box_text(b, picture, entry, scr); }
void rooms_ui_box_name(Biing *b, int picture, int before, int index, int after, int scr) {
    box_name(b, picture, before, index, after, scr);
}
void rooms_ui_box_staff(Biing *b, int picture, int before, int staff, int after, int scr) {
    box_staff(b, picture, before, staff, after, scr);
}
void rooms_ui_invoice(Biing *b, const RoomsResult *o, int picture, int scr) {
    clear(b);
    text(b, 14 + 2 * o->reaction);
    patient_name(b, o->index);
    text(b, 15 + 2 * o->reaction);
    text(b, 386);
    box(b, picture, scr);
    sound_invoice(&b->sound, o->reaction); /* 0x32336 */
}
void rooms_ui_corpse_box(Biing *b, int code, const RoomsResult *o, int scr) { corpse_box(b, code, o, scr); }
void rooms_ui_selector(Biing *b, int flag, int sx, int sy) { selector(b, flag, sx, sy); }
void rooms_ui_site_map(Biing *b) { site_map(b); }
void rooms_ui_sheet(Biing *b) { sheet(b); }
void rooms_ui_sheet_frame(Biing *b) { sheet_frame(b); }
void rooms_ui_assign(Biing *b, int index, int cat) { assign_draw(b, index, cat); }
uint32_t rooms_ui_occupants(const uint8_t *rec) { return occupants(rec); }

/* ---- dispatch ---------------------------------------------------------------------------------------- */

int rooms_routine(Biing *b, uint32_t r) {
    switch (r) {
    /* site map */
    case 0x3724E: site_map(b); return 1;
    case 0x1F3BC: site_map_frame(b); return 1;
    case 0x3724D: return 1; /* hotspot 22's enter/leave: a lone return */
    /* reception */
    case 0x33D58: reception(b); return 1;
    case 0x1FEF4: reception_frame(b); return 1;
    case 0x29167: desk(b); return 1;
    case 0x29269: sheet_frame(b); return 1;
    case 0x2B966: admit(b, 1); return 1;
    case 0x2B976: admit(b, 0); return 1;
    case 0x55898: corpse(b, 2, SCR_RECEPTION_BOX); return 1;
    case 0x29373: rooms_page_turn(sim(b)), sheet(b); return 1;
    case 0x24356: assign_open(b); return 1;
    case 0x3C1E7: assign_step(b, 1); return 1;
    case 0x3C238: assign_step(b, -1); return 1;
    case 0x23366: assign(b); return 1;
    case 0x242AF: occupancy(b); return 1;
    case 0x3D13E: rooms_occupancy_step(sim(b), 1), occupancy_view(b); return 1;
    case 0x3D191: rooms_occupancy_step(sim(b), -1), occupancy_view(b); return 1;
    case 0x3D129: occupancy_frame(b); return 1;
    case 0x2387C: overview(b, 1); return 1;
    case 0x238CA: overview(b, 0); return 1;
    case 0x23BE8: overview_frame(b); return 1;
    case 0x317B9: show_reception(b); return 1;
    case 0x5B5D6: advice(b, 1); return 1;
    case 0x5B4D1: advice(b, 2); return 1;
    case 0x26DEC: inventory(b, 2, 0x33D58); return 1;
    /* waiting room */
    case 0x32D77: waiting(b); return 1;
    case 0x3C07A: case 0x3C08E: waiting_back(b); return 1;
    case 0x23E02: waiting_frame(b); return 1;
    case 0x23DD0: waiting_select(b, 1); return 1;
    case 0x23D9F: waiting_select(b, -1); return 1;
    case 0x316B3: show_waiting(b); return 1;
    case 0x24079: seat_frame(b); return 1;
    case 0x55969: corpse(b, 3, SCR_WAITING_BOX); return 1;
    case 0x26E0E: inventory(b, 3, 0x3C08E); return 1;
    /* treatment room */
    case 0x36D20: treatment_entry(b); return 1;
    case 0x36D3C: case 0x3C098: treatment_back(b); return 1;
    case 0x2007F: treatment_frame(b); return 1;
    case 0x1D1E3: treatment_select(b, 1); return 1;
    case 0x1D212: treatment_select(b, -1); return 1;
    case 0x23E9D: couch(b, 1); return 1;
    case 0x23EB3: couch(b, 0); return 1;
    case 0x24015: call_next(b); return 1;
    case 0x240F4: couch_frame(b); return 1;
    case 0x23EFF: decision(b, rooms_other); return 1;
    case 0x241A8: decision(b, rooms_accept); return 1;
    case 0x241EC: decision(b, rooms_abort); return 1;
    case 0x24256: decision(b, rooms_hand_bill); return 1;
    case 0x58CAC: corpse(b, 5, SCR_TREATMENT_BOX); return 1;
    case 0x23EE2: couch_sheet(b); return 1;
    case 0x3C72A: bed_box_assign(b); return 1;
    case 0x26E52: inventory(b, 5, 0x36D3C); return 1;
    /* ward */
    case 0x36D4C: ward_entry(b); return 1;
    case 0x36D68: case 0x3C084: ward_back(b); return 1;
    case 0x27588: ward_frame(b); return 1;
    case 0x2564A: ward_select(b, 1); return 1;
    case 0x4EBEB: ward_select(b, -1); return 1;
    case 0x2EF50: show_ward(b); return 1;
    case 0x1F452: bedside_frame(b); return 1;
    case 0x4F229: discharge(b); return 1;
    case 0x4F36E: rooms_step(sim(b)), sheet(b); return 1;
    case 0x4F383: to_icu(b); return 1;
    case 0x4F26A: corpse(b, 6, SCR_WARD_BOX); return 1;
    case 0x27CBF: bedside_back(b); return 1;
    case 0x26E95: inventory(b, 6, 0x3C084); return 1;
    default: break;
    }
    if (r >= 0x34461 && r <= 0x34485 && (r - 0x34461) % 4 == 0) return seat(b, (int)(r - 0x34461) / 4), 1;
    if (r >= 0x27C45 && r <= 0x27C69 && (r - 0x27C45) % 4 == 0) return bed(b, (int)(r - 0x27C45) / 4), 1;
    if (room_hover(b, r)) return 1;
    if (building_hover(b, r)) return 1;
    if (sheet_hover(b, r)) return 1;
    return building_click(b, r);
}
