/* The specialist departments' screens (docs/ui-departments.md): dental station 21 with 70, 71, 93; neurology 31 with
 * 75, 74, 94 and the neuro-bed screen 81; neuro ward 40 with 43, 45, 83, 96; intensive care 39 with 84, 72; OP 30
 * with 73, 67, 68; torture chamber 32 with 126, 125, 127; pathology 44 with 90, 91. The rules are in depts.c (and the
 * player branches of sim_rooms.c); the patient sheet and the message boxes are rooms_ui.c's. The sound requests and
 * ambience are room_sound.c's, called from the views and frames. The room screens redraw when their patient or show changes, standing in for
 * the "something happened here" bytes the engine's simulation does not raise (engine choice, as in rooms_ui.c). */
#include "room_sound.h"
#include "depts_ui.h"

#include "depts.h"
#include "rooms_ui.h"
#include "sim_internal.h"

enum { PIC_OK = 509, PIC_BACK = 510, PIC_INVOICE = 511, PIC_NO = 512, PIC_NOT_YET = 513, PIC_SORRY = 514,
       PIC_BAD = 515, PIC_NIGHT = 517, PIC_NONE = 518 };
enum { P_SEX = 0, P_STATE = 4, P_ROOM = 66, P_DEAD = 104 };

static Sim *sim(Biing *b) { return &b->sim; }
static uint8_t *pbb(Biing *b) { return rooms_pb(sim(b)); }
static uint8_t *fb(Biing *b) { return b->ui.flags; }
static void draw(Biing *b, int image) { dulog_draw(&b->ui, image, 0, 0); }
static void screen(Biing *b, int n) { dulog_set_screen(&b->ui, n); }
static int u16(const uint8_t *p) { return (uint16_t)get16(p); }
static uint8_t *room(Biing *b, int cat) { return depts_room(sim(b), cat); }
static int patient_in(Biing *b, int cat) { return u16(room(b, cat) + 12); }
static int woman_in(Biing *b, int cat) { return patient_in(b, cat) && rooms_person(sim(b), patient_in(b, cat))[P_SEX]; }
static int man_in(Biing *b, int cat) { return patient_in(b, cat) && !woman_in(b, cat); }

static void clear(Biing *b) { rooms_ui_clear(b); }
static void text(Biing *b, int entry) { rooms_ui_text(b, entry); }
static void num(Biing *b, long v) { rooms_ui_num(b, v); }
static void name(Biing *b, int index) { rooms_ui_patient_name(b, index); }
static void staff(Biing *b, int index) { rooms_ui_staff_name(b, index); }
static void box(Biing *b, int picture, int scr) { rooms_ui_box(b, picture, scr); }
static void box_text(Biing *b, int picture, int entry, int scr) { rooms_ui_box_text(b, picture, entry, scr); }
static void sheet(Biing *b, int scr) {
    screen(b, scr);
    rooms_ui_sheet(b);
}

/* a room's selector flag (1 room: 0, several: FF) and the selector itself */
static void selector(Biing *b, int cat, int flag, int sx, int sy) {
    fb(b)[flag] = rooms_count(sim(b), cat) == 1 ? 0 : 0xFF;
    rooms_ui_selector(b, flag, sx, sy);
}

static uint32_t drawn; /* the signature the room screen was drawn with */
static uint32_t signature(Biing *b, int cat, int show_off) {
    const uint8_t *r = room(b, cat);
    if (cat == 16) return rooms_ui_occupants(r) ^ (uint32_t)(r[0x0F] != 0);
    return (uint32_t)u16(r + 12) | (uint32_t)(show_off && r[show_off] != 0) << 16;
}

/* ---- the message boxes shared by the rooms ---------------------------------------------------------------- */

/* "Alles klar! " + nurse + 1341 + P + 1343 " und zerrt" + " ihn "/" sie " + 510 + waiting room + 513 / 512 */
static void moved_box(Biing *b, const RoomsResult *o, int last, int scr) {
    clear(b);
    text(b, 1340), staff(b, o->staff), text(b, 1341), name(b, o->index), text(b, 1343);
    rooms_ui_him_her(b, o->index), text(b, 510), num(b, o->room + 1), text(b, last);
    box(b, PIC_OK, scr);
}

static void call_box(Biing *b, int cat, int code, const RoomsResult *o) {
    int scr = cat == 11 ? 71 : 74;
    switch (code) {
    case 1340: moved_box(b, o, cat == 11 ? 513 : 512, scr); break;
    case 539: rooms_ui_box_staff(b, PIC_NO, 539, o->staff, 540, scr); break;
    case 500: box_text(b, PIC_NONE, 500, scr); break;
    case 419: case 411: case 409: case 410: box_text(b, PIC_NOT_YET, code, scr); break;
    default: box_text(b, PIC_NO, code, scr); break; /* 415, 418, 414, 406 */
    }
}

/* the decisions of the dental and neurology sheets (70, 75): refusals return to the sheet through 93 / 94 */
static void clinic_box(Biing *b, int cat, int code, const RoomsResult *o) {
    int room_box = cat == 11 ? 71 : 74, sheet_box = cat == 11 ? 93 : 94;
    switch (code) {
    case -1: break;
    case 0: rooms_ui_sheet(b); break; /* in-room treatment, other proposal: the sheet redrawn */
    case 42: rooms_ui_box_staff(b, PIC_NO, 42, o->staff, 43, sheet_box); break;
    case 89: case 396: case 87: case 88: box_text(b, PIC_NO, code, sheet_box); break;
    case 78: case 80: case 81: box_text(b, PIC_BAD, code, sheet_box); break;
    case 84: case 85: box_text(b, PIC_NOT_YET, code, sheet_box); break;
    case 14: rooms_ui_invoice(b, o, PIC_SORRY, room_box); break;
    case 32:
        clear(b);
        text(b, 32), name(b, o->index), text(b, 33), num(b, o->room + 1), text(b, 349);
        box(b, PIC_SORRY, room_box);
        break;
    case 34: rooms_ui_box_name(b, PIC_SORRY, 34, o->index, 35, room_box); break;
    case 77: case 1462: box_text(b, PIC_SORRY, code, room_box); break;
    default: break;
    }
}

/* ---- 21 - dental station ------------------------------------------------------------------------------------- */

static int dental_show(Biing *b) { return room(b, 11)[0x19] != 0; }

static void dental_view(Biing *b) { /* 0x31AEA */
    int show = dental_show(b);
    draw(b, 178);
    if (woman_in(b, 11)) draw(b, 152);
    if (man_in(b, 11)) draw(b, 589);
    if (show) {
        draw(b, 586), draw(b, 598);
        if (woman_in(b, 11)) draw(b, 975);
        if (man_in(b, 11)) draw(b, 592);
    }
    drawn = signature(b, 11, 0x19);
}

static void dental_back(Biing *b) { /* 0x31A26, also 0x31A16 */
    pbb(b)[DB_DENTAL_NEWS] = 0;
    dental_view(b);
    room_sound_view(sim(b), 11); /* 0x31A57, 0x31A75 */
    selector(b, 11, FB_DENTAL, 400, 0);
    screen(b, 21);
}

static void dental_frame(Biing *b) { /* 0x23E5A */
    if (pbb(b)[DB_DENTAL_NEWS] || signature(b, 11, 0x19) != drawn) {
        pbb(b)[DB_DENTAL_NEWS] = 0;
        dental_view(b);
        selector(b, 11, FB_DENTAL, 400, 0);
    }
    room_sound_frame(sim(b), 0x23E5A); /* show music and ambience */
}

/* ---- 31 - neurology --------------------------------------------------------------------------------------------- */

static void neurology_back(Biing *b) { /* 0x31EE3 = 0x31EF3 + screen 31 */
    pbb(b)[DB_NEUROLOGY_NEWS] = 0;
    draw(b, 210);
    selector(b, 15, FB_NEUROLOGY, 112, 0);
    if (woman_in(b, 15)) draw(b, 149);
    if (man_in(b, 15)) draw(b, 376);
    drawn = signature(b, 15, 0);
    room_sound_view(sim(b), 15); /* 0x31F41, 0x31F5F */
    screen(b, 31);
}

static void neurology_frame(Biing *b) { /* 0x25604 */
    if (pbb(b)[DB_NEUROLOGY_NEWS] || signature(b, 15, 0) != drawn) neurology_back(b);
    room_sound_frame(sim(b), 0x25604);
}

static void clinic_patient(Biing *b, int cat) { /* 0x363AC / 0x2446A */
    int r = depts_clinic_patient(sim(b), cat);
    if (r) box_text(b, PIC_NO, r, cat == 11 ? 71 : 74);
    else sheet(b, cat == 11 ? 70 : 75);
}

static void clinic_call(Biing *b, int cat) {
    RoomsResult o;
    call_box(b, cat, depts_clinic_call(sim(b), cat, &o), &o);
}

static void neuro_bed_view(Biing *b) { /* 0x36081 */
    rooms_ui_assign(b, u16(room(b, 15) + 12), 16);
}

static void clinic_decision(Biing *b, int cat, int (*rule)(Sim *, int, RoomsResult *)) {
    RoomsResult o;
    int code = rule(sim(b), cat, &o);
    if (code == 81) { /* the neuro-bed assignment */
        neuro_bed_view(b);
        screen(b, 81);
        return;
    }
    clinic_box(b, cat, code, &o);
}

static void clinic_sheet_back(Biing *b, int cat) { /* 0x363D5 / 0x24499 */
    int f = cat == 11 ? DB_DENTAL_SHEET : DB_NEURO_SHEET;
    pbb(b)[f] = pbb(b)[f + 1] = 0;
    sheet(b, cat == 11 ? 70 : 75);
}

static void neuro_sheet_frame(Biing *b) { /* 0x25C63 */
    if (pbb(b)[DB_NEURO_BACK]) {
        pbb(b)[DB_NEURO_BACK] = 0;
        neurology_back(b);
        return;
    }
    rooms_ui_sheet_frame(b);
}

static void neuro_bed(Biing *b) { /* 0x235ED */
    RoomsResult o;
    if (depts_neuro_bed(sim(b), &o) < 0) return;
    clear(b);
    text(b, 2362), staff(b, o.staff), text(b, 1341), name(b, o.index), text(b, 2363);
    rooms_ui_him_her(b, o.index), text(b, 2364), num(b, o.room + 1), text(b, 2366);
    box(b, PIC_OK, 74);
}

/* ---- 40 - neuro ward ---------------------------------------------------------------------------------------------- */

static int neuro_show(Biing *b) { return room(b, 16)[0x0F] != 0; } /* the byte K+0xF */
static int neuro_bed_patient(Biing *b, int i) {
    int index = u16(room(b, 16) + 0x14 + 4 * i);
    return index && rooms_person(sim(b), index)[P_STATE] == 16 ? index : 0;
}

static void neuro_stare(Biing *b) {
    for (int i = 0; i < 10; i++)
        if (neuro_bed_patient(b, i)) draw(b, 385 + 2 * i);
    draw(b, 1134), draw(b, 568);
}

static void neuro_view(Biing *b) { /* 0x3BD2A */
    draw(b, 219);
    for (int i = 0; i < 10; i++)
        if (neuro_bed_patient(b, i)) draw(b, 558 + i);
    if (neuro_show(b)) neuro_stare(b);
    drawn = signature(b, 16, 0);
}

static void neuro_back(Biing *b) { /* 0x3BCCB = 0x3BCDB + screen 40 */
    pbb(b)[DB_NEURO_WARD_NEWS] = 0;
    neuro_view(b);
    room_sound_view(sim(b), 16); /* 0x3BCF4 */
    selector(b, 16, FB_NEURO_WARD, 184, 0);
    screen(b, 40);
}

static void neuro_frame(Biing *b) { /* 0x275E0 */
    if (pbb(b)[DB_NEURO_WARD_NEWS] || signature(b, 16, 0) != drawn) {
        pbb(b)[DB_NEURO_WARD_NEWS] = 0;
        neuro_view(b);
        selector(b, 16, FB_NEURO_WARD, 184, 0);
    }
    room_sound_frame(sim(b), 0x275E0);
    pbb(b)[0x56] = 0; /* 0x371E9 */
}

static void neuro_discharge(Biing *b) { /* 0x4EEEB */
    RoomsResult o;
    int r = depts_neuro_discharge(sim(b), &o);
    if (r == 14) rooms_ui_invoice(b, &o, PIC_INVOICE, 83);
    else if (r == 2280) rooms_ui_box_name(b, PIC_BAD, 2280, o.index, 2281, 96);
    else if (r == 2282) box_text(b, PIC_NIGHT, 2282, 96);
}

/* ---- 39 - intensive care ---------------------------------------------------------------------------------------- */

static int icu_show(Biing *b) { return room(b, 9)[0x0F] != 0; }

static void icu_view(Biing *b) { /* 0x31E47 */
    draw(b, 225);
    if (woman_in(b, 9)) draw(b, 470);
    if (man_in(b, 9)) draw(b, 796);
    if (icu_show(b)) {
        draw(b, 626);
        if (woman_in(b, 9)) draw(b, 471);
        if (man_in(b, 9)) draw(b, 797);
    }
    drawn = signature(b, 9, 0x0F);
}

static void icu_back(Biing *b) { /* 0x31D44 = 0x31D8A + screen 39 */
    icu_view(b);
    room_sound_view(sim(b), 9); /* 0x31DA5, 0x31DF4, 0x31E12 */
    selector(b, 9, FB_ICU, 0, 0);
    screen(b, 39);
}

static void icu_frame(Biing *b) { /* 0x5B3D0 */
    if (signature(b, 9, 0x0F) != drawn) icu_view(b), selector(b, 9, FB_ICU, 0, 0);
    room_sound_frame(sim(b), 0x5B3D0); /* the heartbeat's level, show music and ambience */
    pbb(b)[0x56] = 0;
}

static void icu_to_reception(Biing *b) { /* 0x520DC */
    RoomsResult o;
    int r = depts_icu_back(sim(b), &o);
    if (r == 421) rooms_ui_box_name(b, PIC_INVOICE, 421, o.index, 422, 72);
    else box_text(b, PIC_BAD, r, 72);
}

/* ---- 30 - OP and 32 - torture chamber --------------------------------------------------------------------------- */

static int op_show(Biing *b) { return room(b, 7)[0x17] != 0; }

static void op_view(Biing *b) { /* 0x364AA */
    pbb(b)[DB_OP_NEWS] = 0;
    draw(b, 201);
    if (op_show(b)) draw(b, 979);
    if (woman_in(b, 7)) draw(b, 978);
    if (man_in(b, 7)) draw(b, 977);
    selector(b, 7, FB_OP, 380, 15);
    drawn = signature(b, 7, 0x17);
}

static void op_back(Biing *b) { /* 0x3649A */
    op_view(b);
    room_sound_view(sim(b), 7); /* 0x364D6, 0x364F4 */
    screen(b, 30);
}

static void op_frame(Biing *b) { /* 0x4A392 */
    if (pbb(b)[DB_OP_NEWS] || signature(b, 7, 0x17) != drawn) op_view(b);
    room_sound_frame(sim(b), 0x4A392); /* on the ambience tick */
    pbb(b)[RB_TICK] = 0;
    pbb(b)[0x56] = 0;
}

static void torture_back(Biing *b) { /* 0x3677D = 0x3657F + screen 32 */
    draw(b, 213);
    if (woman_in(b, 8)) draw(b, 149);
    if (man_in(b, 8)) draw(b, 376);
    selector(b, 8, FB_TORTURE, 240, 0);
    drawn = signature(b, 8, 0);
    room_sound_view(sim(b), 8); /* 0x36593 */
    screen(b, 32);
}

static void torture_frame(Biing *b) { /* 0x37153 */
    if (signature(b, 8, 0) != drawn) torture_back(b);
    room_sound_frame(sim(b), 0x37153);
}

static void theatre_patient(Biing *b, int cat) { /* 0x344E2 / 0x34FCC */
    int r = depts_theatre_patient(sim(b), cat);
    if (r < 0) rooms_ui_site_map(b);
    else if (r) box_text(b, PIC_NO, r, 125);
    else sheet(b, cat == 7 ? 73 : 126);
}

static void theatre_fetch(Biing *b, int cat) { /* 0x4E63E / 0x4E912 */
    RoomsResult o;
    int r = depts_theatre_fetch(sim(b), cat, &o), scr = cat == 7 ? 67 : 125;
    switch (r) {
    case 0: sheet(b, 126); break;
    case 539: rooms_ui_box_staff(b, PIC_NO, 539, o.staff, 540, scr); break;
    case 416: case 417: case 412: box_text(b, PIC_NOT_YET, r, scr); break;
    case 501:
        clear(b);
        text(b, 501), staff(b, o.staff), text(b, 502), name(b, o.index), text(b, 503);
        rooms_ui_him_her(b, o.index), text(b, cat == 7 ? 505 : 508), num(b, o.room + 1), text(b, cat == 7 ? 506 : 509);
        box(b, PIC_OK, scr);
        break;
    default: box_text(b, PIC_NO, r, scr); break; /* 407, 408 */
    }
}

static void theatre_toggle(Biing *b, int cat) { /* 0x4FD90 / 0x526FF */
    RoomsResult o;
    int r = depts_theatre_toggle(sim(b), cat, &o), scr = cat == 7 ? 67 : 125;
    switch (r) {
    case 0: rooms_ui_sheet(b); break;
    case 3017: box_text(b, PIC_BAD, 3017, 68); break;
    case 3018: box_text(b, PIC_BAD, 3018, 127); break;
    case 530: box_text(b, PIC_BAD, 530, 67); break;
    default: /* 531 / 533 + the capability's name (text 3858 + id, 0x272F5) + 532 / 534 */
        clear(b);
        text(b, r), text(b, 3858 + o.item), text(b, r + 1);
        box(b, PIC_SORRY, scr);
        break;
    }
}

static void theatre_back(Biing *b, int cat) { /* 0x4FBBF / 0x52541 */
    RoomsResult o;
    int r = depts_theatre_back(sim(b), cat, &o), op = cat == 7;
    int pic = r == 2269 || r == 1331 ? PIC_BAD : r == 2275 || r == 1336 ? PIC_NO : PIC_OK;
    int last = op ? (r == 2269 ? 2272 : r == 2275 ? 2276 : 2268) : (r == 1331 ? 1334 : r == 1336 ? 1337 : 1339);
    clear(b);
    text(b, r), staff(b, o.staff), text(b, op ? 2270 : 1332), name(b, o.index), text(b, op ? 2271 : 1333);
    rooms_ui_him_her(b, o.index), text(b, last);
    /* the ward: 2273 / 2274 (OP), 1335 for state 16, else 1336 again - an original fault (torture) */
    text(b, op ? (o.total == 6 ? 2273 : 2274) : (o.total == 16 ? 1335 : 1336));
    num(b, o.room + 1);
    box(b, pic, op ? 67 : 125);
}

static void theatre_box_back(Biing *b, int cat) { /* 68: 0x344E2; 127: 0x34FCC */
    if (cat == 8 && !patient_in(b, 8)) {
        box_text(b, PIC_NO, 83, 125);
        return;
    }
    theatre_patient(b, cat);
}

/* ---- 44 - pathology ---------------------------------------------------------------------------------------------- */

static void pathology(Biing *b) { /* 0x3BDE0 */
    if (!rooms_count(sim(b), 18)) return;
    draw(b, 237);
    screen(b, 44);
    room_sound_view(sim(b), 18); /* 0x3BE1A, 0x3BE3C: after the screen, as in the original */
}

static void path_fetch(Biing *b) { /* 0x50FC7 */
    RoomsResult o;
    int r = depts_path_fetch(sim(b), &o);
    if (r != 1490) {
        box_text(b, PIC_NO, r, 91);
        return;
    }
    clear(b);
    text(b, 1490), staff(b, o.staff), text(b, 1491), name(b, o.index), text(b, 1492);
    box(b, PIC_BACK, 91);
}

static void path_open(Biing *b) { /* 0x4AE9B */
    int r = depts_path_open(sim(b));
    if (r) box_text(b, PIC_NO, r, 91);
    else sheet(b, 90);
}

static void path_throw(Biing *b) { /* 0x52452 */
    RoomsResult o;
    int r = depts_path_throw(sim(b), &o);
    if (r < 0) return;
    clear(b);
    text(b, r), staff(b, o.staff), text(b, r + 1);
    box(b, r == 1488 ? PIC_BACK : PIC_BAD, 91);
}

static void path_sheet_frame(Biing *b) { /* 0x52416 */
    if (pbb(b)[DB_PATH_SHEET]) {
        pbb(b)[DB_PATH_SHEET] = 0;
        rooms_ui_sheet(b);
        return;
    }
    rooms_ui_sheet_frame(b);
}

/* ---- shared: shows and corpses ------------------------------------------------------------------------------------ */

static void show(Biing *b, int cat) { /* 0x3A892 / 0x2EFAE / 0x3A8DE / 0x367A7 */
    int r = depts_show(sim(b), cat);
    if (r) {
        box_text(b, PIC_BAD, r, cat == 11 ? 71 : cat == 16 ? 45 : cat == 9 ? 72 : 67);
        return;
    }
    if (cat == 11) dental_view(b), selector(b, 11, FB_DENTAL, 400, 0);
    else if (cat == 16) neuro_stare(b), selector(b, 16, FB_NEURO_WARD, 184, 0);
    else if (cat == 9) icu_view(b), selector(b, 9, FB_ICU, 0, 0);
    else op_view(b);
}

static void corpse(Biing *b, int cat, int scr) {
    RoomsResult o;
    if (cat == 16 && !rooms_person(sim(b), rooms_pb16(sim(b), RB_PATIENT))[P_DEAD]) return; /* 0x4F1D2 -> 0x5A562 */
    rooms_ui_corpse_box(b, depts_corpse(sim(b), cat, &o), &o, scr);
}

static void select_room(Biing *b, int cat, int step, void (*back)(Biing *)) {
    rooms_selector_step(sim(b), cat, step);
    back(b);
}

static void entry(Biing *b, uint32_t r, void (*back)(Biing *)) { /* the site map's building clicks */
    if (rooms_site_click(sim(b), fb(b), rooms_site_building(r))) back(b);
}

/* ---- hover routines ------------------------------------------------------------------------------------------- */

static int hover(Biing *b, uint32_t r) {
    int ds = dental_show(b), ns = neuro_show(b), is = icu_show(b), os = op_show(b);
    switch (r) {
    /* dental station */
    case 0x3293C: if (!ds) draw(b, 596); return 1;
    case 0x3197F: draw(b, ds ? 586 : 597), selector(b, 11, FB_DENTAL, 400, 0); return 1;
    case 0x3287C: if (!ds) draw(b, 634); return 1;
    case 0x319BC: if (!ds) dental_back(b); return 1;
    case 0x3605B: draw(b, ds ? 587 : 179); return 1;
    case 0x36115: draw(b, ds ? 588 : 180); return 1;
    case 0x362CF: if (!patient_in(b, 11)) draw(b, 590); return 1;
    case 0x362EB: if (!patient_in(b, 11)) draw(b, 591); return 1;
    case 0x36263: if (woman_in(b, 11)) draw(b, 975); else if (man_in(b, 11)) draw(b, 592); return 1;
    case 0x36299: if (woman_in(b, 11)) draw(b, 976); else if (man_in(b, 11)) draw(b, 593); return 1;
    /* neurology */
    case 0x31FAC: if (man_in(b, 15)) draw(b, 723); return 1;
    case 0x31FDC: if (man_in(b, 15)) draw(b, 724); return 1;
    case 0x36BB6: if (woman_in(b, 15)) draw(b, 150); return 1;
    case 0x36BE0: if (woman_in(b, 15)) draw(b, 151); return 1;
    case 0x3622B: if (!patient_in(b, 15)) draw(b, 727); return 1;
    case 0x36247: if (!patient_in(b, 15)) draw(b, 728); return 1;
    case 0x32BE1: if (!patient_in(b, 15)) draw(b, 204); return 1;
    case 0x32C03: if (!patient_in(b, 15)) draw(b, 205); return 1;
    /* neuro ward */
    case 0x3299F: if (!ns) draw(b, 383); return 1;
    case 0x318E9: draw(b, ns ? 568 : 384), selector(b, 16, FB_NEURO_WARD, 184, 0); return 1;
    case 0x32900: if (!ns) draw(b, 381); return 1;
    case 0x4A418: if (!ns) neuro_back(b); return 1;
    case 0x32AD9: if (!ns) draw(b, 448); return 1;
    case 0x32AFA: if (!ns) draw(b, 449); return 1;
    case 0x32B1B: if (ns) draw(b, 1135); return 1;
    case 0x32B3C: if (ns) draw(b, 1134); return 1;
    /* intensive care */
    case 0x328BE: if (!is) draw(b, 798); return 1;
    case 0x31CC6: if (!is) icu_back(b); return 1;
    case 0x31854: draw(b, is ? 627 : 226); return 1;
    case 0x31880: draw(b, is ? 628 : 227); return 1;
    case 0x3295D: if (!is) draw(b, 629); return 1;
    case 0x31CDE: draw(b, is ? 626 : 630), selector(b, 9, FB_ICU, 0, 0); return 1;
    case 0x36307: if (woman_in(b, 9)) draw(b, 471); else if (man_in(b, 9)) draw(b, 797); return 1;
    case 0x3633D: if (woman_in(b, 9)) draw(b, 472); else if (man_in(b, 9)) draw(b, 796); return 1;
    /* OP */
    case 0x366D7: draw(b, woman_in(b, 7) ? 982 : man_in(b, 7) ? 202 : 980); return 1;
    case 0x36719: draw(b, woman_in(b, 7) ? 983 : man_in(b, 7) ? 203 : 981); return 1;
    case 0x369E8: if (!os) draw(b, 456); return 1;
    case 0x36A03: if (!os) draw(b, 457); return 1;
    case 0x3162D: if (!os) draw(b, 460); return 1;
    case 0x3164E: if (!os) draw(b, 461); return 1;
    case 0x365FF: if (woman_in(b, 7)) draw(b, 986); else if (man_in(b, 7)) draw(b, 464); return 1;
    case 0x36635: if (woman_in(b, 7)) draw(b, 987); else if (man_in(b, 7)) draw(b, 465); return 1;
    case 0x315DD: if (u16(room(b, 7) + 8) && !os) draw(b, 984); return 1;
    case 0x31605: if (u16(room(b, 7) + 8) && !os) draw(b, 985); return 1;
    /* torture chamber; the patient hover 0x3666B / 0x366A1 is not described: as the neurology's (engine choice) */
    case 0x36853: if (!patient_in(b, 8)) draw(b, 1124); return 1;
    case 0x3686F: if (!patient_in(b, 8)) draw(b, 1125); return 1;
    case 0x3166F: if (!patient_in(b, 8)) draw(b, 1122); return 1;
    case 0x31691: if (!patient_in(b, 8)) draw(b, 1123); return 1;
    case 0x3666B: if (man_in(b, 8)) draw(b, 723); else if (woman_in(b, 8)) draw(b, 150); return 1;
    case 0x366A1: if (man_in(b, 8)) draw(b, 724); else if (woman_in(b, 8)) draw(b, 151); return 1;
    default: break;
    }
    if (r >= 0x2774D && r <= 0x27771 && (r - 0x2774D) % 4 == 0) { /* bed i enter */
        int i = (int)(r - 0x2774D) / 4;
        if (b->ui.current == 40 && neuro_bed_patient(b, i) && !ns) draw(b, 385 + 2 * i);
        return 1;
    }
    if (r >= 0x27638 && r <= 0x2765C && (r - 0x27638) % 4 == 0) {
        int i = (int)(r - 0x27638) / 4;
        if (b->ui.current == 40 && neuro_bed_patient(b, i) && !ns) draw(b, 386 + 2 * i);
        return 1;
    }
    return 0;
}

/* ---- dispatch ---------------------------------------------------------------------------------------------------- */

int depts_routine(Biing *b, uint32_t r) {
    switch (r) {
    /* dental station 21, 70, 71, 93 */
    case 0x319F5: entry(b, r, dental_back); return 1;
    case 0x31A26: case 0x31A16: dental_back(b); return 1;
    case 0x23E5A: dental_frame(b); return 1;
    case 0x1D189: select_room(b, 11, 1, dental_back); return 1;
    case 0x1D1BB: select_room(b, 11, -1, dental_back); return 1;
    case 0x3A892: show(b, 11); return 1;
    case 0x24406: clinic_call(b, 11); return 1;
    case 0x363AC: clinic_patient(b, 11); return 1;
    case 0x363D5: clinic_sheet_back(b, 11); return 1;
    case 0x25A51: rooms_ui_sheet_frame(b); return 1;
    case 0x25AC4: clinic_decision(b, 11, depts_clinic_other); return 1;
    case 0x25B0E: clinic_decision(b, 11, depts_clinic_accept); return 1;
    case 0x25BAF: clinic_decision(b, 11, depts_clinic_abort); return 1;
    case 0x25B58: clinic_decision(b, 11, depts_clinic_bill); return 1;
    case 0x58DB9: corpse(b, 11, 71); return 1;
    /* neurology 31, 75, 74, 94, 81 */
    case 0x31EC7: entry(b, r, neurology_back); return 1;
    case 0x31EE3: neurology_back(b); return 1;
    case 0x25604: neurology_frame(b); return 1;
    case 0x2455A: select_room(b, 15, 1, neurology_back); return 1;
    case 0x24589: select_room(b, 15, -1, neurology_back); return 1;
    case 0x2446A: clinic_patient(b, 15); return 1;
    case 0x245B4: clinic_call(b, 15); return 1;
    case 0x24499: clinic_sheet_back(b, 15); return 1;
    case 0x25C63: neuro_sheet_frame(b); return 1;
    case 0x25CEB: clinic_decision(b, 15, depts_clinic_other); return 1;
    case 0x25D81: clinic_decision(b, 15, depts_clinic_accept); return 1;
    case 0x25DC5: clinic_decision(b, 15, depts_clinic_abort); return 1;
    case 0x25E12: clinic_decision(b, 15, depts_clinic_bill); return 1;
    case 0x55A3A: corpse(b, 15, 74); return 1;
    case 0x235ED: neuro_bed(b); return 1;
    case 0x3C28C: depts_neuro_bed_step(sim(b), 1), neuro_bed_view(b); return 1;
    case 0x3C2DD: depts_neuro_bed_step(sim(b), -1), neuro_bed_view(b); return 1;
    /* neuro ward 40, 43, 45, 83, 96 */
    case 0x3BCAF: entry(b, r, neuro_back); return 1;
    case 0x3BCCB: case 0x3BCDB: neuro_back(b); return 1;
    case 0x275E0: neuro_frame(b); return 1;
    case 0x4EC4D: select_room(b, 16, 1, neuro_back); return 1;
    case 0x4EC1C: select_room(b, 16, -1, neuro_back); return 1;
    case 0x2EFAE: show(b, 16); return 1;
    case 0x4EE6E: rooms_ui_sheet_frame(b); return 1;
    case 0x4EEEB: neuro_discharge(b); return 1;
    case 0x4F145: depts_neuro_step(sim(b)), rooms_ui_sheet(b); return 1;
    case 0x4F1D2: corpse(b, 16, 45); return 1;
    /* intensive care 39, 84, 72 */
    case 0x31D28: entry(b, r, icu_back); return 1;
    case 0x31D44: case 0x31D8A: icu_back(b); return 1;
    case 0x5B3D0: icu_frame(b); return 1;
    case 0x371F3: select_room(b, 9, 1, icu_back); return 1;
    case 0x37225: select_room(b, 9, -1, icu_back); return 1;
    case 0x3A8DE: show(b, 9); return 1;
    case 0x36373: {
        int code = depts_icu_patient(sim(b));
        if (code) box_text(b, PIC_NO, code, 72);
        else sheet(b, 84);
        return 1;
    }
    case 0x1F4C9: rooms_ui_sheet_frame(b); return 1;
    case 0x520DC: icu_to_reception(b); return 1;
    case 0x5232D: corpse(b, 9, 72); return 1;
    /* OP 30, 73, 67, 68 */
    case 0x36474: entry(b, r, op_back); return 1;
    case 0x3649A: case 0x364AA: op_back(b); return 1;
    case 0x4A392: op_frame(b); return 1;
    case 0x4E8B8: select_room(b, 7, 1, op_back); return 1;
    case 0x4E8E7: select_room(b, 7, -1, op_back); return 1;
    case 0x367A7: show(b, 7); return 1;
    case 0x4E63E: theatre_fetch(b, 7); return 1;
    case 0x344E2: theatre_box_back(b, 7); return 1;
    case 0x1F540: rooms_ui_sheet_frame(b); return 1;
    case 0x4FBBF: theatre_back(b, 7); return 1;
    case 0x4FD90: theatre_toggle(b, 7); return 1;
    case 0x5A640: corpse(b, 7, 67); return 1;
    /* torture chamber 32, 126, 125, 127 */
    case 0x3675B: entry(b, r, torture_back); return 1;
    case 0x3677D: torture_back(b); return 1;
    case 0x37153: torture_frame(b); return 1;
    case 0x4EB91: select_room(b, 8, 1, torture_back); return 1;
    case 0x4EBC0: select_room(b, 8, -1, torture_back); return 1;
    case 0x4E912: theatre_fetch(b, 8); return 1;
    case 0x34FCC: theatre_box_back(b, 8); return 1;
    case 0x1F5B7: rooms_ui_sheet_frame(b); return 1;
    case 0x52541: theatre_back(b, 8); return 1;
    case 0x526FF: theatre_toggle(b, 8); return 1;
    case 0x528A1: corpse(b, 8, 125); return 1;
    /* pathology 44, 90, 91 */
    case 0x3BDE0: pathology(b); return 1;
    case 0x370CD: room_sound_frame(sim(b), r); return 1; /* the ambience */
    case 0x50FC7: path_fetch(b); return 1;
    case 0x4AE9B: path_open(b); return 1;
    case 0x52416: path_sheet_frame(b); return 1;
    case 0x52452: path_throw(b); return 1;
    default: break;
    }
    if (r >= 0x274EA && r <= 0x2750E && (r - 0x274EA) % 4 == 0) { /* the beds act on their own screen only */
        if (b->ui.current == 40 && depts_neuro_ward_bed(sim(b), (int)(r - 0x274EA) / 4)) sheet(b, 43);
        return 1;
    }
    return hover(b, r);
}
