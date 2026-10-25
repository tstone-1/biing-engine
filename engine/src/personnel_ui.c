/* The personnel screens (docs/ui-personnel.md): the personnel office 10, the job ads 20/46, the job-group
 * chooser 99, the applications 51, the personnel sheet 33, the courses 128/129, the room staff roster 54 and
 * the OK boxes 50, 36 and 55. The rules are in personnel.c; this file draws, builds the texts and moves
 * between the screens. */
#include "room_sound.h"
#include "personnel_ui.h"

#include "personnel.h"
#include "garage.h"
#include "rooms_ui.h"

enum {
    IMG_OFFICE = 133, IMG_CHOOSER = 925, IMG_BLANK = 148, IMG_PUBLISH = 283, IMG_WAGE_ARROWS = 287,
    IMG_TRAINING_ARROWS = 298, IMG_FORM_TEXT = 292, IMG_LETTER = 279, IMG_HIRE = 500, IMG_ARROWS = 302,
    IMG_TEAR = 306, IMG_FRAME = 311, IMG_CLIPBOARD = 309, IMG_BACK = 641, IMG_PREV = 643, IMG_NEXT = 645,
    IMG_JAILED = 1070, IMG_OCCUPANT = 1131, IMG_COURSE = 1075, IMG_COURSE_ICON = 437, IMG_COURSE_OK = 1077,
    IMG_COURSE_NEXT = 1087,
    PIC_OK = 509, PIC_HOME = 510, PIC_FREE = 511, PIC_NO = 513, PIC_SORRY = 514, PIC_BAD = 515, PIC_NONE = 518,
};
enum { SCR_OFFICE = 10, SCR_ADS = 20, SCR_FORM = 46, SCR_BOX = 50, SCR_APPLICANTS = 51, SCR_SHEET = 33,
       SCR_SHEET_BOX = 36, SCR_CHOOSER = 99, SCR_ROSTER = 54, SCR_ROOM_BOX = 55, SCR_COURSES = 128,
       SCR_ENROL = 129 };
#define NURSE_CM 0xE519 /* "cm. Ge_zahlt wird " */
#define NURSE_MOODS 0xE52D /* five '$'-terminated strings by training */
#define CR_STRING 0xA73D

static Sim *sim(Biing *b) { return &b->sim; }
static int pb(Biing *b, int off) { return personnel_pb16(sim(b), off); }
static void pset(Biing *b, int off, int v) { personnel_set_pb16(sim(b), off, v); }
static void draw(Biing *b, int image, int dx, int dy) { dulog_draw(&b->ui, image, dx, dy); }
static void screen(Biing *b, int n) { dulog_set_screen(&b->ui, n); }
static void call(Biing *b, uint32_t r) { b->ui.call(&b->ui, r); }

/* ---- text ------------------------------------------------------------------------------------- */

static void clear(Biing *b) { msg_clear(&b->msg); }
static void text(Biing *b, int entry) { biing_msg_entry(b, entry); }
static void num(Biing *b, long v) { msg_append_number(&b->msg, v); }
static void pool(Biing *b, int which, int i) {
    size_t n;
    const uint8_t *p = datadb_pool_entry(&b->people, which, i, &n);
    msg_append(&b->msg, p, n);
}
/* the qualification and peculiarity strings: pool K / M at the staff record's u16 28 / 30 (docs/formats.md, the staff
 * record; docs/ui-personnel.md, the letter) */
#define POOL_K(st) ((st)[28] | (st)[29] << 8)
#define POOL_M(st) ((st)[30] | (st)[31] << 8)
static void name(Biing *b, int index) { pool(b, DAT_STAFF_NAMES, index); } /* 0x2BE61 */
/* a string at an offset of the build; nothing when the build has no counterpart (the English CD's nurse strings,
 * docs/editions.md section 3.5) */
static void raw_string(Biing *b, uint32_t at) {
    size_t left, n = 0;
    const uint8_t *p = program_raw_ptr(&b->prog, at, &left);
    while (n < left && n < 64 && p[n] != '$' && p[n]) n++;
    if (p) msg_append(&b->msg, p, n);
}
static void prog_string(Biing *b, uint32_t off) { raw_string(b, program_at(&b->prog, off)); }
/* the i-th '$'-terminated string from the disk offset off on */
static void prog_string_n(Biing *b, uint32_t off, int i) {
    uint32_t at = program_at(&b->prog, off);
    size_t left, k = 0;
    const uint8_t *p = program_raw_ptr(&b->prog, at, &left);
    if (!p) return;
    while (i-- > 0) {
        while (k < left && p[k] != '$') k++;
        k++;
    }
    raw_string(b, at + (uint32_t)k);
}

/* The sheet's name sets line spacing 0 and the left-aligned lines after it keep it ("left" keeps the caller's
 * spacing, docs/ui-personnel.md; the centred fields between them do not leave their 1 behind): a wrapped line is 13
 * px (font 1's height) below the one before, as the original's render shows (docs/engine.md, the comparison). */
enum { SHEET_GAP = 0 };

static TextLayout layout(Biing *b, int font, int x, int y, int width, int gap, int space) {
    return (TextLayout){&b->fonts[font], b->prog.charmap, x, y, width, gap, space, 1};
}
/* font 1, space 5; "left" keeps the caller's line spacing, "centred" forces 1 */
static void left(Biing *b, int x, int y, int width, int gap) {
    TextLayout l = layout(b, 1, x, y, width, gap, 5);
    text_left(&b->screen, &l, b->msg.buf, b->msg.len);
}
static void centred(Biing *b, int x, int y, int width) {
    TextLayout l = layout(b, 1, x, y, width, 1, 5);
    text_centred(&b->screen, &l, b->msg.buf, b->msg.len);
}
static void field(Biing *b, long v, int x, int y, int width) {
    clear(b);
    num(b, v);
    centred(b, x, y, width);
}

/* Message box over what is on screen, then the OK screen (50, 36 or 55). */
static void box(Biing *b, int picture, int scr) {
    biing_message_box(b, picture);
    screen(b, scr);
}
static void box_text(Biing *b, int picture, int entry, int scr) {
    clear(b);
    text(b, entry);
    box(b, picture, scr);
}
static void box_name(Biing *b, int picture, int before, int index, int after, int scr) {
    clear(b);
    text(b, before);
    name(b, index);
    text(b, after);
    box(b, picture, scr);
}

/* ---- 10: the personnel office ------------------------------------------------------------------ */

static void office(Biing *b) { /* 0x2EDB8 */
    draw(b, IMG_OFFICE, 0, 0);
    screen(b, SCR_OFFICE);
    room_sound_view(sim(b), 0); /* 0x2EDBD: the office loop 0xFC */
}

static void office_frame(Biing *b) { /* 0x371BB */
    if (!room_sound_frame(sim(b), 0x371BB)) sim_bits(sim(b), 31); /* the t&31 draw that times the ambience */
    personnel_pb(sim(b))[0x56] = 0;
}

/* ---- 33: the personnel sheet ------------------------------------------------------------------- */

static int filter(Biing *b) { return pb(b, PB_FILTER); }
static int sheet_group(Biing *b) { return pb(b, PB_SHEET_GROUP); }
static int cur(Biing *b) { return pb(b, PB_INDEX); }
static uint8_t *slot_of(Biing *b, int slot) { return personnel_slot(sim(b), slot); }
static int index_of(Biing *b, int slot) { return (uint16_t)(slot_of(b, slot)[0] | slot_of(b, slot)[1] << 8); }
static uint8_t *staff_at(Biing *b, int slot) { return personnel_staff(sim(b), index_of(b, slot)); }

static void diligence(Biing *b, int d) {
    static const int LIMIT[6] = {4, 8, 13, 18, 26, 35};
    int k = 0;
    while (k < 6 && d >= LIMIT[k]) k++;
    text(b, 1402 + k);
}

static int doctor(int g) { return g == 2 || g == 5 || g == 7 || g == 9 || g == 10; }

/* 0x225AB: "seit wenigen Minuten" ... */
static void duration(Biing *b, int hours) {
    if (hours == 0) text(b, 1970);
    else if (hours == 1) text(b, 1971);
    else num(b, hours), text(b, 1972);
}

static void mood_line(Biing *b, const uint8_t *st) {
    int g = st[1];
    clear(b);
    if (g == 8) text(b, 334);
    else if (g == 6) text(b, 333);
    else if (g == 3) text(b, 332);
    else if (g == 4) text(b, 331);
    else if (g == 1 && program_english(&b->prog)) text(b, 330); /* "Has little on" (docs/editions.md 7.3, rule J) */
    else if (g == 1) {
        int t = st[2];
        prog_string_n(b, NURSE_MOODS, t < 3 ? 0 : t < 5 ? 1 : t < 7 ? 2 : t < 9 ? 3 : 4);
    } else {
        int d = (int8_t)(uint8_t)(st[7] - st[5]);
        if (d < 0) text(b, 321);
        else text(b, 322 + d / 16);
    }
    int w = st[19] - st[18];
    if (w >= 0) text(b, 335 + (w < 5 ? w : 5));
    else text(b, 341 + (-w < 3 ? -w : 3));
    left(b, 254, 143, 256, SHEET_GAP);
}

/* The activity line (docs/ui-sheets.md, section 2): sheet.c builds it after the mood line, which a duty state
 * without a sentence leaves in the buffer; the garage's from garage_activity (docs/ui-garage.md, with the corrections
 * of docs/ui-sheets.md). */
static void activity_line(Biing *b, int index, const uint8_t *st, const uint8_t *sl, int slot) {
    GarageLine g;
    if (garage_activity(sim(b), index, st[3], &g)) {
        int person = garage_race_word(sim(b));
        clear(b);
        text(b, g.a);
        if (g.kind != 4) {
            if (g.kind == 0 || g.kind == 3) duration(b, sl[7]);
            else rooms_ui_patient_name(b, person);
            if (g.kind == 2) text(b, g.c), num(b, g.position);
            text(b, g.b);
        }
        left(b, 124, 190, 384, SHEET_GAP);
        return;
    }
    SheetCtx c = {sim(b), &b->texts, &b->people, &b->sheet, &b->msg};
    if (sheet_staff_activity(&c, slot) == 1) left(b, 124, 190, 384, SHEET_GAP);
}

static void qualification_line(Biing *b, const uint8_t *st) {
    int n = st[12] + st[13];
    clear(b);
    text(b, 1383);
    if (n == 0) text(b, 1392);
    else if (n == 1) text(b, 1393);
    else text(b, 1384), num(b, n), text(b, 1395);
    text(b, 1983);
    pool(b, DAT_QUALIFICATIONS, POOL_K(st));
    text(b, 1982);
    pool(b, DAT_PECULIARITIES, POOL_M(st));
    text(b, 1396);
    diligence(b, st[14]);
    left(b, 124, 242, 384, SHEET_GAP);
}

static void independence_line(Biing *b, uint8_t *st) {
    int x = personnel_independence(st);
    if (x >= 0) st[11] = 0; /* quirk: drawing the sheet clears the flag of someone who no longer qualifies */
    clear(b);
    if (st[11]) text(b, 117);
    else if (x < 0) text(b, 119);
    else text(b, x < 30 ? 124 : x < 60 ? 123 : x < 110 ? 122 : x < 170 ? 121 : x < 280 ? 120 : 118);
    left(b, 124, 320, 384, SHEET_GAP);
}

/* The sheet from its step 3: portrait, fields and the four text lines (shared with the roster). */
static void sheet_body(Biing *b, int slot, int number) {
    uint8_t *sl = slot_of(b, slot), *st = staff_at(b, slot);
    int index = index_of(b, slot), g = st[1];
    int portrait = st[24] | st[25] << 8;
    if (portrait) draw(b, portrait, -49, 36);
    clear(b);
    name(b, index);
    left(b, 254, 52, 264, SHEET_GAP);
    clear(b);
    text(b, 1302 + sl[2]);
    left(b, 254, 65, 260, SHEET_GAP);
    field(b, number, 478, 65, 50);
    field(b, st[8], 267, 78, 80);
    if (st[0] == 1) {
        field(b, st[5], 368, 78, 50);
        field(b, st[6], 416, 78, 50);
        field(b, st[7], 460, 78, 50);
        if (program_english(&b->prog)) { /* a woman's IQ is "--" (docs/editions.md 7.3, rule I) */
            clear(b);
            text(b, 2176);
            centred(b, 456, 91, 50);
        } else {
            field(b, (((st[5] + st[6] + st[7] + st[8] + st[9]) & 0xFF) & 15) + 70, 456, 91, 50);
        }
    } else {
        field(b, st[15], 456, 91, 50);
        if (g == 3 || g == 4 || g == 8 || g == 6) {
            draw(b, g == 3 ? 1267 : g == 4 ? 1268 : g == 8 ? 1269 : 1270, 0, 0);
            field(b, st[5], 416, 78, 50);
        } else {
            draw(b, 1271, 0, 0);
            clear(b);
            num(b, st[6]);
            text(b, 1647);
            num(b, st[5]);
            centred(b, 418, 78, 70);
        }
    }
    field(b, st[9], 267, 91, 80);
    field(b, st[10], 378, 91, 50);
    field(b, st[2], 312, 104, 50);
    field(b, (uint16_t)(st[16] | st[17] << 8), 454, 104, 50);
    field(b, (uint16_t)(sl[8] | sl[9] << 8) + 1, 312, 117, 50);
    field(b, st[21], 454, 117, 50);
    field(b, st[19], 312, 130, 50);
    if (sl[3] == 0x16) {
        draw(b, IMG_JAILED, 0, 0);
        field(b, sl[7], 454, 130, 50);
    } else {
        field(b, sl[10], 454, 130, 50);
    }
    mood_line(b, st);
    activity_line(b, index, st, sl, slot);
    qualification_line(b, st);
    independence_line(b, st);
}

void personnel_ui_sheet_body(Biing *b, int slot) { sheet_body(b, slot, slot + 1); }
void personnel_ui_sheet_body_nr(Biing *b, int slot, int number) { sheet_body(b, slot, number); }

static void draw_sheet(Biing *b) { /* 0x64C63 for the slot pb+0 */
    int f = filter(b), slot = cur(b);
    draw(b, IMG_CLIPBOARD, 0, 0);
    draw(b, IMG_BACK, 0, 0);
    draw(b, IMG_PREV, 0, 0);
    draw(b, IMG_NEXT, 0, 0);
    if (f == FILTER_DISMISS) {
        draw(b, 1102, 0, 0);
        draw(b, 1104, 0, 0);
        draw(b, 1106, 0, 0);
    } else if (f == FILTER_IDLE || f == FILTER_ROOMS) {
        draw(b, 647, 0, 0);
        draw(b, f == FILTER_IDLE ? 1266 : 649, 0, 0);
        draw(b, staff_at(b, slot)[11] ? 650 : 651, 0, 0);
    }
    sheet_body(b, slot, slot + 1);
}

static void show_sheet(Biing *b, int slot) {
    pset(b, PB_INDEX, slot);
    draw_sheet(b);
    screen(b, SCR_SHEET);
}

/* the five office buttons and the chooser: search from slot 0 */
static void sheet_from_start(Biing *b, int to_chooser) {
    int slot = personnel_search(sim(b), 0, 1, 210, filter(b), sheet_group(b));
    if (slot < 0) {
        box_text(b, PIC_NONE, personnel_nobody_text(filter(b)), SCR_BOX);
        return;
    }
    if (!to_chooser) {
        show_sheet(b, slot);
        return;
    }
    draw(b, IMG_CHOOSER, 0, 0);
    screen(b, SCR_CHOOSER);
}

static void sheet_button(Biing *b, int f) {
    pset(b, PB_FILTER, f);
    pset(b, PB_INDEX, 0);
    pset(b, PB_SHEET_GROUP, 0);
    sheet_from_start(b, 1);
}

static void dismissal_button(Biing *b) { /* 0x22935 */
    int refuse = personnel_dismissal_allowed(sim(b));
    if (refuse) {
        box_text(b, PIC_BAD, refuse, SCR_BOX);
        return;
    }
    sheet_button(b, FILTER_DISMISS);
}

static void choose_group(Biing *b, int g) { /* 0x22A47 */
    pset(b, PB_SHEET_GROUP, g);
    pset(b, PB_INDEX, 0);
    sheet_from_start(b, 0);
}

static void sheet_step(Biing *b, int step) { /* 0x22AB1 / 0x22AF3 */
    int slot = personnel_search(sim(b), cur(b) + step, step, 210, filter(b), sheet_group(b));
    if (slot < 0) {
        sheet_from_start(b, 0); /* 0x22A5A; engine choice: "previous" does the same */
        return;
    }
    show_sheet(b, slot);
}

static void sheet_box_thumb(Biing *b) { /* 0x3BF89: from the current slot, not the next one */
    int slot = personnel_search(sim(b), cur(b), 1, 210, filter(b), sheet_group(b));
    if (slot < 0) office(b);
    else show_sheet(b, slot);
}

static void sheet_frame(Biing *b) { /* 0x24618 */
    uint8_t *p = personnel_pb(sim(b));
    int slot = cur(b), race = garage_race_word(sim(b)) != 0;
    const uint8_t *sl = slot >= 0 && slot < 200 ? b->world.block[BLK_STAFF_SLOTS] + slot * 20 : NULL;
    if (p[0x33] || p[0x9E] || (race && p[0x2E] && sl && sl[2] == 6)) draw_sheet(b);
    p[0x2E] = p[0x33] = p[0x9E] = 0;
}

/* ---- the sheet's three action buttons ---------------------------------------------------------- */

static void send_home(Biing *b) {
    int slot = cur(b), index = index_of(b, slot);
    switch (personnel_send_home(sim(b), slot)) {
    case PERS_ALREADY_HOME: box_name(b, PIC_NO, PERS_ALREADY_HOME, index, 794, SCR_SHEET_BOX); return;
    case PERS_BUSY: box_name(b, PIC_BAD, PERS_BUSY, index, 798, SCR_SHEET_BOX); return;
    default: box_name(b, PIC_HOME, 795, index, 796, SCR_SHEET_BOX); return;
    }
}

static void release(Biing *b) {
    int slot = cur(b), index = index_of(b, slot);
    switch (personnel_release(sim(b), slot)) {
    case PERS_ALREADY_HOME: box_name(b, PIC_NO, PERS_ALREADY_HOME, index, 794, SCR_SHEET_BOX); return;
    case PERS_NOT_ASSIGNED: box_name(b, PIC_NO, PERS_NOT_ASSIGNED, index, 800, SCR_SHEET_BOX); return;
    case PERS_BUSY: box_name(b, PIC_BAD, PERS_BUSY, index, 798, SCR_SHEET_BOX); return;
    default: box_name(b, PIC_FREE, 791, index, 792, SCR_SHEET_BOX); return;
    }
}

static void independent(Biing *b) {
    int slot = cur(b), index = index_of(b, slot);
    switch (personnel_toggle_independent(sim(b), slot)) {
    case PERS_NOW_DEPENDENT: box_name(b, PIC_SORRY, 125, index, 126, SCR_SHEET_BOX); return;
    case PERS_NOT_YET: box_name(b, PIC_NONE, 127, index, 128, SCR_SHEET_BOX); return;
    default: box_name(b, PIC_OK, 129, index, 130, SCR_SHEET_BOX); return;
    }
}

static void dismiss(Biing *b) {
    int slot = cur(b), index = index_of(b, slot);
    if (!personnel_dismiss(sim(b), slot)) return;
    clear(b);
    text(b, 783);
    name(b, index);
    text(b, 784);
    num(b, PERSONNEL_SEVERANCE);
    text(b, 785);
    box(b, PIC_BAD, SCR_SHEET_BOX);
}

static void wage(Biing *b, int up) {
    if (up) personnel_raise(sim(b), cur(b));
    else personnel_cut(sim(b), cur(b));
    draw_sheet(b);
}

static void course_page(Biing *b, int e);

static void course_from_sheet(Biing *b) { /* 0x433A3 */
    int e = personnel_course_find(sim(b), 0, 20, slot_of(b, cur(b))[2]);
    if (e < 0) {
        box_text(b, PIC_NONE, PERS_NO_COURSE, SCR_SHEET_BOX);
        return;
    }
    pset(b, PB_COURSE, e);
    course_page(b, e);
    screen(b, SCR_ENROL);
}

static void action(Biing *b, int id) { /* 0x24671, 0x2469E, 0x246C7 */
    int f = filter(b);
    if (f == FILTER_DISMISS) {
        if (id == 4) wage(b, 1);
        else if (id == 5) wage(b, 0);
        else dismiss(b);
    } else if (f == FILTER_IDLE || f == FILTER_ROOMS) {
        if (id == 4) send_home(b);
        else if (id == 5) {
            if (f == FILTER_IDLE) course_from_sheet(b);
            else release(b);
        } else independent(b);
    }
}

static void action_hover(Biing *b, int id, int enter) {
    int f = filter(b), img = 0;
    if (f == FILTER_DISMISS) img = (id == 4 ? 1101 : id == 5 ? 1103 : 1105) + !enter;
    else if (f == FILTER_IDLE || f == FILTER_ROOMS) {
        if (id == 4) img = enter ? 646 : 647;
        else if (id == 5) img = f == FILTER_IDLE ? (enter ? 1265 : 1266) : (enter ? 648 : 649);
        else img = (staff_at(b, cur(b))[11] ? enter : !enter) ? 651 : 650;
    }
    if (img) draw(b, img, 0, 0);
}

/* ---- 20 / 46: job ads ------------------------------------------------------------------------- */

static const int FACE[11] = {0, 478, 479, 480, 481, 482, 483, 484, 485, 486, 488};
static const int PANEL[11] = {0, 289, 294, 369, 291, 294, 1273, 294, 266, 294, 294};

static int ad_group(Biing *b) { return pb(b, PB_GROUP); }

static void ads(Biing *b) { /* 0x3BECC */
    draw(b, IMG_CHOOSER, 0, 0);
    screen(b, SCR_ADS);
}

static void wage_phrase(Biing *b, int w, int t1, int t2, int t3) {
    text(b, w < t1 ? 1317 : w < t2 ? 1316 : w < t3 ? 1315 : 1314);
}

static void form_text(Biing *b) {
    int g = ad_group(b), w = pb(b, PB_WAGE), t = pb(b, PB_TRAINING), sc = pb(b, PB_CRITERION);
    draw(b, IMG_FORM_TEXT, 0, 0);
    clear(b);
    text(b, 2342);
    if (g == 1 && program_english(&b->prog)) { /* English 0x39D9B (docs/editions.md 7.4, OTH-3) */
        int bust = pb(b, PB_BUST);
        text(b, 2343);
        text(b, bust < 80 ? 2339 : bust < 90 ? 2340 : bust < 100 ? 2338 : bust < 110 ? 2337 : 2336);
        text(b, 2344), num(b, bust), text(b, 2345);
        text(b, t < 2 ? 2330 : t < 5 ? 2331 : t < 8 ? 2332 : t < 12 ? 2333 : 2334); /* the other groups' phrase */
        num(b, t), text(b, 2335);
        wage_phrase(b, w, 4, 7, 12);
        num(b, w), text(b, 2346), text(b, 2347);
    } else if (g == 1) { /* 0x3A150 */
        text(b, 2343);
        text(b, t < 3 ? 2339 : t < 5 ? 2340 : t < 7 ? 2338 : t < 9 ? 2337 : 2336);
        text(b, 2344), num(b, t), text(b, 2345), num(b, pb(b, PB_BUST));
        prog_string(b, NURSE_CM);
        wage_phrase(b, w, 4, 7, 12);
        num(b, w), text(b, 2346), text(b, 2347);
    } else { /* 0x39AC1 */
        int k, d = doctor(g);
        if (d) {
            k = sc < 40 ? 0 : sc < 60 ? 1 : sc < 80 ? 2 : sc < 100 ? 3 : 4;
            text(b, 2287 - k);
            text(b, g == 2 ? 2288 : g == 5 ? 2289 : g == 7 ? 2290 : g == 9 ? 2291 : 2292);
            text(b, 2293);
        } else {
            int base = g == 3 ? 2310 : g == 4 ? 2315 : g == 6 ? 2320 : 2325;
            k = sc < 40 ? 0 : sc < 55 ? 1 : sc < 70 ? 2 : sc < 85 ? 3 : 4;
            text(b, base + k);
            text(b, g == 3 ? 2305 : g == 4 ? 2306 : g == 6 ? 2307 : 2308);
        }
        num(b, sc);
        text(b, d ? 2294 : 2309);
        text(b, t < 2 ? 2330 : t < 5 ? 2331 : t < 8 ? 2332 : t < 12 ? 2333 : 2334);
        num(b, t), text(b, 2335);
        if (d) wage_phrase(b, w, 5, 8, 14);
        else wage_phrase(b, w, 3, 6, 9);
        num(b, w), text(b, 2346);
        if (d) text(b, 2300), text(b, g == 2 ? 2299 : g == 5 ? 2298 : g == 7 ? 2297 : g == 9 ? 2296 : 2295);
        else text(b, g == 3 ? 2301 : g == 4 ? 2302 : g == 6 ? 2303 : 2304);
    }
    centred(b, 185, 125, 252);
}

static void form(Biing *b, int g) { /* 0x3A2D0 */
    draw(b, FACE[g], 0, 0);
    draw(b, PANEL[g], 0, 0);
    personnel_ad_defaults(sim(b), g);
    form_text(b);
}

static void form_frame(Biing *b) {
    draw(b, IMG_BLANK, 0, 0);
    draw(b, IMG_PUBLISH, 0, 0);
    draw(b, IMG_WAGE_ARROWS, 0, 0);
    draw(b, IMG_TRAINING_ARROWS, 0, 0);
    clear(b);
    text(b, 2341);
    centred(b, 154, 62, 240);
}

static void ad_choose(Biing *b, int g) { /* 0x3A58F */
    pset(b, PB_AD_GROUP, g);
    form_frame(b);
    pset(b, PB_GROUP, g);
    screen(b, SCR_FORM);
    form(b, g);
}

static void ad_next_group(Biing *b) { /* 0x3A267 */
    form_frame(b);
    form(b, personnel_ad_next_group(ad_group(b)));
}

static void ad_arrow(Biing *b, int f, int up) {
    personnel_ad_arrow(sim(b), f, up);
    form_text(b);
}

/* hotspots 3 and 4: hover up, hover down, normal per group */
static void panel_hover(Biing *b, int which) {
    int g = ad_group(b);
    static const int UP[11] = {0, 288, 293, 368, 285, 293, 1274, 293, 265, 293, 293};
    static const int DOWN[11] = {0, 290, 295, 370, 281, 295, 1272, 295, 267, 295, 295};
    draw(b, which == 0 ? UP[g] : which == 1 ? DOWN[g] : PANEL[g], 0, 0);
}

static void face_hover(Biing *b, int enter) {
    int g = ad_group(b);
    draw(b, enter ? (g == 1 ? 487 : 487 + g) : FACE[g], 0, 0);
}

static void publish(Biing *b) { /* 0x3994B */
    if (personnel_publish(sim(b)) < 0) box_text(b, PIC_NO, PERS_MARKET_FULL, SCR_BOX);
    else box_text(b, PIC_OK, PERS_AD_PLACED, SCR_BOX);
}

/* ---- 51: applications ------------------------------------------------------------------------- */

static void letter(Biing *b) { /* 0x3A926 */
    int index = b->staff_list[cur(b)];
    const uint8_t *st = personnel_staff(sim(b), index), *o = personnel_opening(sim(b), st[27]);
    int portrait = st[24] | st[25] << 8;
    if (portrait) draw(b, portrait, 0, 0);
    draw(b, IMG_FRAME, 0, 0);
    clear(b);
    text(b, 1372), name(b, index), text(b, 1373), text(b, 1302 + o[1]), text(b, 1401);
    left(b, 306, 23, 160, 1);
    clear(b);
    text(b, 1375), num(b, st[8]), text(b, 1374);
    if (st[0] == 1) {
        text(b, 1379), num(b, st[5]), num(b, st[6]), num(b, st[7]);
    } else {
        text(b, st[1] == 3 ? 1377 : 1378), num(b, st[15]);
    }
    text(b, 1376), num(b, st[9]), text(b, 1380), num(b, st[10]), text(b, 1381);
    left(b, 306, 85, 160, 1);
    clear(b);
    int t = st[2], n = (uint8_t)(st[12] + st[13]);
    text(b, t < 2 ? 1385 : t < 5 ? 1386 : t < 9 ? 1387 : t < 15 ? 1388 : 1389);
    num(b, t), text(b, 1390);
    pool(b, DAT_QUALIFICATIONS, POOL_K(st));
    text(b, 1382);
    if (n == 0) text(b, 1391);
    else if (n == 1) text(b, 1394);
    else text(b, 3007), num(b, n), text(b, 1398);
    text(b, 1396);
    diligence(b, st[14]);
    pool(b, DAT_PECULIARITIES, POOL_M(st));
    text(b, 1399), num(b, o[2]), text(b, 1400);
    left(b, 174, 150, 301, 1);
}

static void applicant_page(Biing *b) {
    draw(b, IMG_LETTER, 0, 0);
    draw(b, IMG_HIRE, 0, 0);
    draw(b, IMG_ARROWS, 0, 0);
    draw(b, IMG_TEAR, 0, 0);
    letter(b);
}

static void applicants(Biing *b) { /* 0x3BF02 */
    pset(b, PB_ROSTER_GROUP, 0);
    pset(b, PB_INDEX, 0);
    int p = personnel_next_applicant(sim(b), b->staff_list, 0);
    pset(b, PB_INDEX, p);
    if (p < 0) {
        box_text(b, PIC_SORRY, 1371, SCR_BOX);
        return;
    }
    applicant_page(b);
    screen(b, SCR_APPLICANTS);
}

static void applicant_step(Biing *b, int next) { /* 0x3A85A / 0x3A822 */
    int p = next ? personnel_next_applicant(sim(b), b->staff_list, cur(b))
                 : personnel_prev_applicant(sim(b), b->staff_list, cur(b));
    if (p < 0) return; /* engine choice: nobody left (the ad expired meanwhile) keeps the page */
    pset(b, PB_INDEX, p);
    applicant_page(b);
}

static void hire(Biing *b) { /* 0x22BE7 */
    int index = b->staff_list[cur(b)];
    int refuse = personnel_hire(sim(b), index, NULL);
    if (refuse) {
        box_text(b, PIC_BAD, refuse, SCR_BOX);
        return;
    }
    clear(b);
    text(b, 744);
    if (personnel_staff(sim(b), index)[0] == 1) text(b, 746), name(b, index), text(b, 748);
    else text(b, 745), name(b, index), text(b, 747);
    text(b, 749);
    box(b, PIC_OK, SCR_BOX);
}

static void tear_up(Biing *b) { /* 0x3A6BA: the ad stays open */
    int index = b->staff_list[cur(b)];
    personnel_tear_up(sim(b), index);
    box_name(b, PIC_BAD, 556, index, 557, SCR_BOX);
}

/* ---- 128 / 129: courses ----------------------------------------------------------------------- */

static void hours(Biing *b, int n) {
    if (n == 1) text(b, 46);
    else num(b, n), text(b, 45);
}

static void course_page(Biing *b, int e) { /* 0x430B4 / 0x430A9 */
    const uint8_t *c = personnel_course(sim(b), e), *k = personnel_catalog(sim(b), c[4]);
    draw(b, IMG_COURSE, 0, 0);
    draw(b, IMG_COURSE_ICON, 0, 0);
    draw(b, IMG_COURSE_OK, 0, 0);
    draw(b, IMG_COURSE_NEXT, 0, 0);
    clear(b);
    text(b, k[0] | k[1] << 8);
    TextLayout l = layout(b, 4, 256, 40, 266, 2, 5);
    text_left(&b->screen, &l, b->msg.buf, b->msg.len);
    clear(b);
    text(b, k[10] | k[11] << 8);
    if (c[1]) {
        text(b, 44), hours(b, c[1]), text(b, 48), num(b, k[2]), text(b, 49), num(b, k[9]), text(b, 50);
    } else if (c[3]) {
        text(b, 47), hours(b, c[3]), text(b, 51), num(b, k[9]), text(b, 50);
    } else {
        text(b, 52);
    }
    l = layout(b, 5, 132, 110, 380, 2, 4);
    text_left(&b->screen, &l, b->msg.buf, b->msg.len);
}

static void courses(Biing *b) { /* 0x4305D */
    int e = personnel_course_in_use(sim(b), 0, 20);
    if (e < 0) {
        box_text(b, PIC_NONE, 13, SCR_BOX);
        return;
    }
    pset(b, PB_INDEX, e);
    course_page(b, e);
    screen(b, SCR_COURSES);
}

static void courses_next(Biing *b) { /* 0x43238 */
    int e = personnel_course_in_use(sim(b), cur(b) + 1, 29);
    if (e < 0) {
        box_text(b, PIC_NONE, 13, SCR_BOX);
        return;
    }
    pset(b, PB_INDEX, e);
    course_page(b, e);
}

static void enrol_next(Biing *b) { /* 0x42FAF */
    int e = personnel_course_find(sim(b), pb(b, PB_COURSE) + 1, 29, slot_of(b, cur(b))[2]);
    if (e < 0) {
        box_text(b, PIC_NONE, PERS_NO_COURSE, SCR_SHEET_BOX);
        return;
    }
    pset(b, PB_COURSE, e);
    course_page(b, e);
}

static void enrol(Biing *b) { /* 0x42E81 */
    int slot = cur(b), index = index_of(b, slot);
    int r = personnel_course_enrol(sim(b), slot, pb(b, PB_COURSE));
    if (r == PERS_NO_FEE) box_text(b, PIC_BAD, r, SCR_SHEET_BOX);
    else if (r) box_text(b, PIC_NONE, r, SCR_SHEET_BOX);
    else box_name(b, PIC_OK, 55, index, 56, SCR_SHEET_BOX);
}

/* ---- 54 / 55: room staff ---------------------------------------------------------------------- */

static const RosterStub *stub(Biing *b) { return &PERSONNEL_ROSTER[b->roster_stub]; }

/* the "nobody available" box: screen 55, but the OP stubs' boxes set 67, the torture stubs' 125 and the neuro-ward
 * stub's 45 (docs/ui-departments.md, "Message boxes and their return screens"), and the bank's pathologist stub's
 * (0x313CA) 82, the bank's OK box (docs/ui-bank.md) */
static void nobody(Biing *b) {
    int cat = stub(b)->cat, scr = cat == 7 ? 67 : cat == 8 ? 125 : cat == 16 ? 45 : cat == 10 ? 69 : SCR_ROOM_BOX;
    if (cat == 17 && stub(b)->group == 7) scr = 82;
    box_text(b, stub(b)->picture, stub(b)->text, scr);
}

static void draw_roster(Biing *b) { /* 0x46C08 */
    int slot = cur(b);
    draw(b, IMG_CLIPBOARD, 0, 0);
    if (slot == b->roster_occupant) draw(b, IMG_OCCUPANT, 0, 0);
    draw(b, IMG_BACK, 0, 0);
    draw(b, IMG_PREV, 0, 0);
    draw(b, IMG_NEXT, 0, 0);
    draw(b, 649, 0, 0);
    sheet_body(b, slot, slot + 1);
}

static void roster(Biing *b, const RosterStub *st) { /* the stub, then 0x1D2E6 */
    b->roster_stub = (int)(st - PERSONNEL_ROSTER);
    int r = personnel_roster_enter(sim(b), st, &b->roster_occupant);
    if (r == -2) { /* the garage's driver during a race (0x312FC): back to the garage */
        box_text(b, 515, 1669, 69);
        return;
    }
    if (r < 0) {
        nobody(b);
        return;
    }
    draw_roster(b);
    screen(b, SCR_ROSTER);
}

static void roster_step(Biing *b, int step) {
    personnel_roster_step(sim(b), step, b->roster_occupant);
    draw_roster(b);
}

static void assign(Biing *b) { /* 0x1D364 */
    int replaced, slot = cur(b);
    switch (personnel_assign(sim(b), b->roster_occupant, &replaced)) {
    case ROSTER_NOBODY: nobody(b); return;
    case ROSTER_BACK: call(b, 0x1AE66); return;
    default: break;
    }
    int room = pb(b, PB_ROOM_SHOWN);
    clear(b);
    text(b, 345), text(b, 1302 + pb(b, PB_ROSTER_GROUP)), text(b, 350), text(b, 350 + pb(b, PB_ROSTER_CAT));
    if (room >= 0) text(b, 386), num(b, room + 1);
    prog_string(b, CR_STRING);
    name(b, index_of(b, slot));
    text(b, 346);
    if (replaced) text(b, 347), name(b, replaced), text(b, 348);
    text(b, 349);
    biing_message_box(b, PIC_OK);
    screen(b, SCR_ROOM_BOX);
}

static void roster_release(Biing *b) { /* 0x1D8B6 */
    int occ = b->roster_occupant;
    int r = personnel_roster_release(sim(b), occ);
    if (r == ROSTER_BACK) {
        call(b, 0x1AE66);
        return;
    }
    int index = index_of(b, occ);
    if (r == PERS_NOT_ASSIGNED) box_name(b, PIC_NO, PERS_NOT_ASSIGNED, index, 800, SCR_ROOM_BOX);
    else if (r == PERS_BUSY) box_name(b, PIC_BAD, PERS_BUSY, index, 798, SCR_ROOM_BOX);
    else box_name(b, PIC_FREE, 791, index, 792, SCR_ROOM_BOX);
}

/* ---- dispatch --------------------------------------------------------------------------------- */

/* g = 1-10 if r is one of ten entries 6 bytes apart starting at base, else 0 */
static int group_of(uint32_t r, uint32_t base) {
    if (r < base || r > base + 6u * 9 || (r - base) % 6) return 0;
    return (int)((r - base) / 6) + 1;
}

int personnel_routine(Biing *b, uint32_t r) {
    int g;
    const RosterStub *st;
    switch (r) {
    case 0x2EDB8: office(b); return 1;
    case 0x371BB: office_frame(b); return 1;
    case 0x2297B: sheet_button(b, FILTER_HOME); return 1;
    case 0x22951: sheet_button(b, FILTER_AWAY); return 1;
    case 0x2296D: sheet_button(b, FILTER_ROOMS); return 1;
    case 0x2295F: sheet_button(b, FILTER_IDLE); return 1;
    case 0x22935: dismissal_button(b); return 1;
    case 0x3BECC: ads(b); return 1;
    case 0x3BF02: applicants(b); return 1;
    case 0x4305D: courses(b); return 1;
    case 0x22A07: choose_group(b, 0); return 1;
    case 0x22AB1: sheet_step(b, 1); return 1;
    case 0x22AF3: sheet_step(b, -1); return 1;
    case 0x433A3: course_from_sheet(b); return 1; /* the course button's routine itself (screen 33, id 5, F 0) */
    case 0x24671: action(b, 4); return 1;
    case 0x2469E: action(b, 5); return 1;
    case 0x246C7: action(b, 6); return 1;
    case 0x32434: action_hover(b, 4, 1); return 1;
    case 0x3236A: action_hover(b, 4, 0); return 1;
    case 0x323A6: action_hover(b, 5, 1); return 1;
    case 0x323ED: action_hover(b, 5, 0); return 1;
    case 0x33CC8: action_hover(b, 6, 1); return 1;
    case 0x33D10: action_hover(b, 6, 0); return 1;
    case 0x24618: sheet_frame(b); return 1;
    case 0x3BF89: sheet_box_thumb(b); return 1;
    case 0x3994B: publish(b); return 1;
    case 0x399A5: ad_arrow(b, AD_SECOND, 1); return 1;
    case 0x399FF: ad_arrow(b, AD_SECOND, 0); return 1;
    case 0x39A59: ad_arrow(b, AD_TRAINING, 1); return 1;
    case 0x39A73: ad_arrow(b, AD_TRAINING, 0); return 1;
    case 0x39A8D: ad_arrow(b, AD_WAGE, 1); return 1;
    case 0x39AA7: ad_arrow(b, AD_WAGE, 0); return 1;
    case 0x3A267: ad_next_group(b); return 1;
    case 0x3260A: panel_hover(b, 0); return 1;
    case 0x326EB: panel_hover(b, 1); return 1;
    case 0x3268B: case 0x32789: panel_hover(b, 2); return 1;
    case 0x32470: face_hover(b, 1); return 1;
    case 0x325AB: face_hover(b, 0); return 1;
    case 0x22BE7: hire(b); return 1;
    case 0x3A822: applicant_step(b, 0); return 1;
    case 0x3A85A: applicant_step(b, 1); return 1;
    case 0x3A6BA: tear_up(b); return 1;
    case 0x43238: courses_next(b); return 1;
    case 0x42E81: enrol(b); return 1;
    case 0x42FAF: enrol_next(b); return 1;
    case 0x1D364: assign(b); return 1;
    case 0x1D80E: roster_step(b, 1); return 1;
    case 0x1D85A: roster_step(b, -1); return 1;
    case 0x1D8B6: roster_release(b); return 1;
    default: break;
    }
    if ((g = group_of(r, 0x3A555))) ad_choose(b, g);
    else if ((g = group_of(r, 0x22A0D))) choose_group(b, g);
    else if ((st = personnel_roster_stub(r))) roster(b, st);
    else return 0;
    return 1;
}
