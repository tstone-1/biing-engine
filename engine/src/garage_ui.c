/* The garage's screens (docs/ui-garage.md): the garage 18, the car list 132 and the car card, sending a car to the
 * race 131, the OK box 69, the championship tables 135 and the race screen 133. The rules are in garage.c; the car
 * card 0x4D27B and the "next usable car" 0x502F2 are support_ui.c's (shared with the vehicle market's screen 134),
 * the staff stubs personnel_ui.c's, the inventory purchase_ui.c's. No sounds: the entries' sound requests and the
 * ambience frames are left out. */
#include "room_sound.h"
#include "garage_ui.h"

#include <stdio.h>

#include "garage.h"
#include "rooms.h"
#include "rooms_ui.h"
#include "sim_internal.h"

enum {
    IMG_GARAGE = 169, IMG_TABLE = 1048, IMG_TABLE_THUMB = 1049, IMG_FRAME = 1047, IMG_RACE = 1162, PIC_OK = 509,
    PIC_FULL = 512, PIC_NOT_YET = 513, PIC_NONE = 518, SCR_GARAGE = 18, SCR_LIST = 132, SCR_SEND = 131,
    SCR_GARAGE_BOX = 69, SCR_TABLES = 135, SCR_RACE = 133,
};
#define NAMES 0x3BCF /* hospital names, 30 bytes each, '$'-terminated; byte +0x1D the article */

static Sim *sim(Biing *b) { return &b->sim; }
static void draw(Biing *b, int image) { dulog_draw(&b->ui, image, 0, 0); }
static void draw_at(Biing *b, int image, int x, int y) { /* an image with its top-left corner at (x, y) */
    const Image *img = dulog_image(&b->ui, image);
    if (img) dulog_draw(&b->ui, image, x - img->x, y - img->y);
}
static void screen(Biing *b, int n) { dulog_set_screen(&b->ui, n); }
static void call(Biing *b, uint32_t r) { b->ui.call(&b->ui, r); }
static void clear(Biing *b) { rooms_ui_clear(b); }
static void text(Biing *b, int entry) { rooms_ui_text(b, entry); }
static void num(Biing *b, long v) { rooms_ui_num(b, v); }
static void chars(Biing *b, const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    msg_append(&b->msg, (const uint8_t *)s, n);
}
static void box(Biing *b, int picture, int entry) { rooms_ui_box_text(b, picture, entry, SCR_GARAGE_BOX); }

static TextLayout layout(Biing *b, int font, int space, int gap, int x, int y, int width) {
    return (TextLayout){&b->fonts[font], b->prog.charmap, x, y, width, gap, space, 1};
}
static void left(Biing *b, int font, int space, int gap, int x, int y, int width) {
    TextLayout l = layout(b, font, space, gap, x, y, width);
    text_left(&b->screen, &l, b->msg.buf, b->msg.len);
}
static void centred(Biing *b, int font, int space, int gap, int x, int y, int width) {
    TextLayout l = layout(b, font, space, gap, x, y, width);
    text_centred(&b->screen, &l, b->msg.buf, b->msg.len);
}
static void hospital_name(Biing *b, int h) {
    if (h < 0 || h > 16) return;
    const uint8_t *p = program_table(&b->prog, NAMES) + 30 * h;
    size_t n = 0;
    while (n < 29 && p[n] != '$' && p[n]) n++;
    msg_append(&b->msg, p, n);
}
static void rank_text(Biing *b, int rank) {
    char s[8];
    snprintf(s, sizeof s, "%02d.", rank % 100);
    chars(b, s);
}

/* ---- 18, the garage -------------------------------------------------------------------------------------- */

static void garage(Biing *b) { /* 0x36A1E: the site byte test (a lone return without a garage), 169, screen 18 */
    if (!rooms_site_click(sim(b), b->ui.flags, rooms_site_building(0x36A1E))) return;
    draw(b, IMG_GARAGE);
    room_sound_view(sim(b), 10); /* 0x36A4A */
    screen(b, SCR_GARAGE);
}

/* ---- 132, the car list; 131, sending a car ---------------------------------------------------------------- */

static void refusal(Biing *b, int r) {
    switch (r) {
    case GA_NO_RACE: case GA_NO_CAR: case GA_NO_DRIVER: box(b, PIC_NOT_YET, r); return;
    case GA_ENTERED: box(b, PIC_FULL, r); return;
    default: box(b, PIC_NONE, r); return;
    }
}

static void card_screen(Biing *b, int r, int scr) {
    if (r) {
        refusal(b, r);
        return;
    }
    call(b, 0x4D27B);
    screen(b, scr);
}

static void list_next(Biing *b) { /* 0x50344 */
    int r = garage_list_next(sim(b));
    if (r) refusal(b, r);
    else call(b, 0x4D27B);
}

static void send(Biing *b) { /* 0x50036 */
    int k, r = garage_send(sim(b), &k);
    if (r) {
        refusal(b, r);
        return;
    }
    const uint8_t *g = sim_room_record(sim(b), 0, 10, 0);
    clear(b);
    text(b, 1001);
    rooms_ui_staff_name(b, get16(g + GR_DRIVER));
    text(b, 1002);
    chars(b, "\r\r");
    if (k == 0) text(b, 1003);
    else if (k == 1) text(b, 1004);
    else if (k <= 3) text(b, 1005);
    else text(b, 1006), num(b, k + 1), text(b, 1007);
    rooms_ui_box(b, PIC_OK, SCR_GARAGE_BOX);
    sound_list(&b->sound, 0, 0xA807); /* 0x50172, 0x5017C */
    sound_list(&b->sound, 1, 0xA837);
}

/* ---- 135, the championship tables ---------------------------------------------------------------------- */

static void table_sheet(Biing *b, int title, int final_title) {
    draw(b, IMG_TABLE);
    draw(b, IMG_TABLE_THUMB);
    int n = garage_races_shown(sim(b));
    clear(b);
    if (n == RACES_PER_SEASON) text(b, final_title);
    else text(b, title), num(b, n), text(b, 1319);
    left(b, 1, 5, 1, 124, 32, 480);
}

static void team_table(Biing *b) { /* 0x6CF80 */
    table_sheet(b, 1320, 2473);
    uint8_t order[16];
    garage_team_order(sim(b), order);
    const uint8_t *pts = garage_team_points(sim(b));
    for (int k = 0, i = 0; k < 16; k++) {
        int h = order[k];
        if (h == 0) draw_at(b, IMG_FRAME, 105, 82 + 13 * i);
        if (cash_of(sim(b), h) < 0) continue;
        int y = 84 + 13 * i;
        clear(b), rank_text(b, i + 1), centred(b, 1, 5, 2, 114, y, 35);
        clear(b), hospital_name(b, h), left(b, 1, 5, 2, 154, y, 400);
        clear(b), num(b, get16(pts + 2 * h)), centred(b, 1, 5, 2, 382, y, 85);
        i++;
    }
    screen(b, SCR_TABLES);
}

static void driver_table(Biing *b) { /* 0x6D197 */
    table_sheet(b, 1318, 2474);
    int rows = garage_driver_rows(sim(b));
    const uint8_t *e = garage_driver_table(sim(b));
    for (int k = 0, i = 0; k < rows; k++) {
        int driver = (uint16_t)get16(e + 6 * k);
        int h = b->world.block[BLK_STAFF][driver * 32 + 3];
        /* engine choice: a driver with no employer (staff byte 3 = FF, hospital 255 past the records) is skipped */
        if (h >= 16) continue;
        if (h == 0) draw_at(b, IMG_FRAME, 105, 56 + 13 * i);
        if (cash_of(sim(b), h) < 0) continue;
        int y = 58 + 13 * i;
        clear(b), rank_text(b, i + 1), centred(b, 1, 5, 2, 114, y, 35);
        clear(b), rooms_ui_staff_name(b, driver), left(b, 1, 5, 2, 164, y, 400);
        clear(b), num(b, get16(e + 6 * k + 4)), centred(b, 1, 5, 2, 382, y, 85);
        i++;
    }
    screen(b, SCR_TABLES);
}

/* ---- 133, the race screen ------------------------------------------------------------------------------- */

static void race_draw(Biing *b) { /* 0x5AB2D from step 1 */
    Sim *s = sim(b);
    const uint8_t *rb = garage_race(s);
    int minute = get16(rb + RACE_MINUTE), over = rb[RACE_RESULT] != 0, running = minute >= 30;
    draw(b, IMG_RACE);
    clear(b), text(b, 1008), rooms_ui_patient_name(b, garage_race_word(s));
    left(b, 4, 5, 0, 296, 8, 350);
    int n = 0;
    while (n < RACE_LOOP_ENTRIES && (int8_t)garage_entry(s, n)[0] >= 0) n++;
    clear(b);
    if (over) text(b, 1020);
    else if (!running) text(b, 1009);
    else text(b, 1014), num(b, minute - 28), text(b, 1015);
    if (!running && !n) {
        left(b, 5, 4, 0, 296, 35, 335);
        clear(b), text(b, 1010), left(b, 5, 4, 0, 296, 48, 335);
        screen(b, SCR_RACE);
        return;
    }
    left(b, 5, 4, 0, 296, 35, 335);
    int rows = n < 10 ? n : 10, lead = n ? (uint16_t)get16(garage_entry(s, 0) + 2) : 0;
    for (int k = 0; k < rows; k++) {
        const uint8_t *e = garage_entry(s, k);
        int h = e[0], y = 52 + 28 * k;
        const uint8_t *g = h < 16 ? garage_of(s, h) : NULL;
        clear(b), rank_text(b, k + 1), left(b, 5, 4, 0, 296, y, 35);
        clear(b);
        if (g) rooms_ui_staff_name(b, get16(g + GR_DRIVER));
        left(b, 5, 4, 0, 318, y, 335);
        /* English CD: the name at x 443 (docs/editions.md 7.3, OTH-EN-4) */
        clear(b), hospital_name(b, h), left(b, 5, 4, 0, program_english(&b->prog) ? 443 : 456, y, 335);
        if (running || over) {
            clear(b);
            if (k == 0) text(b, 1018);
            else {
                char gap[16];
                /* the two random digits: the docs name no order; the first drawn is the first digit */
                int d1 = (int)sim_random(s, 10);
                int d2 = (int)sim_random(s, 10);
                garage_gap_text(lead - (uint16_t)get16(e + 2), d1, d2, gap);
                chars(b, gap);
            }
            left(b, 5, 4, 0, 580, y, 335);
        }
        clear(b);
        const uint8_t *car = g && e[1] < GR_PLACE_COUNT ? g + GR_PLACE + 4 * e[1] : NULL;
        if (car && car[0]) text(b, garage_model_u16(s, car[0] - 1, 0));
        if ((running || over) && car) {
            text(b, 350);
            if (car[1] < 15) text(b, 1174);
            else if (car[1] < 30) text(b, 1173);
            else if (car[1] < 40) text(b, 1172);
            else num(b, car[1]), text(b, 981);
        }
        left(b, 5, 4, 0, 318, y + 13, 335);
    }
    clear(b); /* the footer, 5 pixels below the rows (engine reading: below the last row's 28-pixel band) */
    if (over) {
        rooms_ui_patient_name(b, garage_race_word(s));
        text(b, 1021);
        int win = garage_entry(s, 0)[0];
        if (program_english(&b->prog)) { /* the clamped name helper (docs/editions.md 7.3, rule E) */
            int sb = (int8_t)(uint8_t)win;
            win = sb < 0 ? 1 : sb >= 16 ? 16 : sb;
        }
        if (win < 16 || program_english(&b->prog)) text(b, 2657 + program_u8(&b->prog, NAMES, 30u * (uint32_t)win + 0x1D)), hospital_name(b, win); /* 0x3B510 */
        text(b, 1022);
    } else if (!running) {
        int left_min = 30 - minute;
        if (left_min >= 2) text(b, 1012), num(b, left_min), text(b, 1013);
        else text(b, 1011);
    } else {
        int laps = rb[RACE_LAPS] / 4;
        if (laps) text(b, 1016), num(b, laps + 1), text(b, 1017);
        else text(b, 1019);
    }
    left(b, 5, 4, 0, 296, 52 + 28 * rows + 5, 335);
    screen(b, SCR_RACE);
}

static void race(Biing *b) { /* 0x5AB2D */
    sound_background(&b->sound, 0x9F, 0x140, 6); /* 0x5AB2D: the race poster's click */
    if (!garage_race_word(sim(b))) { /* no race: screen 133 with nothing drawn; its frame returns to the site map */
        screen(b, SCR_RACE);
        return;
    }
    race_draw(b);
}

static void race_frame(Biing *b) { /* 0x5AABB */
    if (!garage_race_word(sim(b))) {
        call(b, 0x3724E);
        return;
    }
    room_sound_frame(sim(b), 0x5AABB); /* the ambience by the race's phase */
    uint8_t *flag = &b->world.block[BLK_PLAYER0][0x2E];
    if (*flag) {
        *flag = 0;
        race_draw(b);
    }
}

/* ---- dispatch ------------------------------------------------------------------------------------------- */

int garage_routine(Biing *b, uint32_t r) {
    switch (r) {
    case 0x36A1E: garage(b); return 1;
    case 0x36FF7: room_sound_frame(sim(b), r); return 1; /* the garage's frame: the ambience */
    case 0x521C4: card_screen(b, garage_list_open(sim(b)), SCR_LIST); return 1;
    case 0x50344: list_next(b); return 1;
    case 0x52221: card_screen(b, garage_send_open(sim(b)), SCR_SEND); return 1;
    case 0x50036: send(b); return 1;
    case 0x6CF80: team_table(b); return 1;
    case 0x6D197: driver_table(b); return 1;
    case 0x5AB2D: race(b); return 1;
    case 0x5AABB: race_frame(b); return 1;
    default: return 0;
    }
}
