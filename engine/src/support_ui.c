/* The support rooms' and administration extras' screens (docs/ui-support.md): the kitchen 9 with the menu 103, the
 * club room 8 with the close-up 24, the golf room 42 with the report 148, the advertising campaign 119 / 121, the
 * vehicle market 130 / 134 and the thugs 110. The rules are in support.c; this file draws and moves between the
 * screens. The message boxes are rooms_ui.c's, the staff stubs and inventories are personnel_ui.c's and
 * purchase_ui.c's. No sounds: the entries' sound requests and the rooms' ambience frame routines are left out (the
 * ambience frames make random draws only for the sound; they draw nothing). */
#include "room_sound.h"
#include "support_ui.h"

#include "personnel.h"
#include "personnel_ui.h"
#include "rooms.h"
#include "rooms_ui.h"
#include "sim_internal.h"
#include "support.h"

enum {
    IMG_KITCHEN = 126, IMG_CLUB = 123, IMG_GOLF = 243, IMG_MENU = 1004, IMG_MENU_ON = 1008, IMG_MENU_OFF = 1009,
    IMG_PAD = 1075, IMG_THUMB = 1077, IMG_PORTRAIT = 1277, IMG_PLUS = 1079, IMG_MINUS = 1082, IMG_PAPERS = 1087,
    IMG_SIGN = 1084, IMG_BANNER = 1083, IMG_LEAFLETS = 1085, IMG_ADMIN = 104, IMG_NO_CAMPAIGN = 154,
    IMG_NO_CAMPAIGN_LEAVE = 158, IMG_SHEET = 309, IMG_FIST = 641, IMG_PREV = 643, IMG_NEXT = 645,
    PIC_OK = 509, PIC_BACK = 510, PIC_OWN = 511, PIC_FULL = 512, PIC_NOT_YET = 513, PIC_SORRY = 514, PIC_BAD = 515,
    PIC_NONE = 518,
};
enum { SCR_KITCHEN = 9, SCR_MENU = 103, SCR_CLUB = 8, SCR_GOLF = 42, SCR_GOLF_REPORT = 148, SCR_ROOM_BOX = 55,
       SCR_ADMIN_BOX = 27, SCR_CAMPAIGN = 119, SCR_CAMPAIGN_STATUS = 121, SCR_BUY = 130, SCR_SELL = 134,
       SCR_GARAGE_BOX = 69, SCR_THUGS = 110, SCR_SHEET_BOX = 36 };
#define NAMES 0x3BCF /* hospital names, 30 bytes each, '$'-terminated */
#define RECIPE_NAMES 0xA8E6 /* 33 u32 addresses of '$'-terminated recipe names */
#define UTENSIL_NAMES 0xAD18 /* 6 names of 20 bytes */

static Sim *sim(Biing *b) { return &b->sim; }
static void draw(Biing *b, int image) { dulog_draw(&b->ui, image, 0, 0); }
static void screen(Biing *b, int n) { dulog_set_screen(&b->ui, n); }
static void call(Biing *b, uint32_t r) { b->ui.call(&b->ui, r); }
static void clear(Biing *b) { rooms_ui_clear(b); }
static void text(Biing *b, int entry) { rooms_ui_text(b, entry); }
static void num(Biing *b, long v) { rooms_ui_num(b, v); }
static int u16(const uint8_t *p) { return (uint16_t)get16(p); }
static uint8_t *pb(Biing *b) { return rooms_pb(sim(b)); }

static void left(Biing *b, int font, int space, int x, int y, int width, int gap) {
    TextLayout l = {&b->fonts[font], b->prog.charmap, x, y, width, gap, space, 1};
    text_left(&b->screen, &l, b->msg.buf, b->msg.len);
}
/* a string at an offset of the build (a recipe name pointer) */
static void raw_string(Biing *b, uint32_t off, size_t max) {
    size_t left, n = 0;
    const uint8_t *p = program_raw_ptr(&b->prog, off, &left);
    if (!p) return;
    while (n < max && n < left && p[n] != '$' && p[n]) n++;
    msg_append(&b->msg, p, n);
}
/* a string in a table at a disk offset: the table start is mapped, the index is not */
static void table_string(Biing *b, uint32_t table, uint32_t index, size_t max) {
    uint32_t at = program_at(&b->prog, table);
    if (at != PROGRAM_NOWHERE) raw_string(b, at + index, max);
}
static void hospital_name(Biing *b, int h) {
    if (h >= 0 && h <= 16) table_string(b, NAMES, 30u * (uint32_t)h, 29);
}
static void box(Biing *b, int picture, int entry, int scr) { rooms_ui_box_text(b, picture, entry, scr); }
static void set_continue(Biing *b, uint32_t r) {
    for (int i = 0; i < 4; i++) pb(b)[6 + i] = (uint8_t)(r >> (8 * i));
}
/* the site map's click: the site byte test and the player-block selection of rooms.c, then the background */
static void room_entry(Biing *b, uint32_t r, int background, int scr) {
    if (!rooms_site_click(sim(b), b->ui.flags, rooms_site_building(r))) return;
    draw(b, background);
    screen(b, scr);
    room_sound_view(sim(b), rooms_site_building(r)->cat); /* 0x36AA8, 0x36B05, 0x3BD8A */
}

/* ---- kitchen 9 and menu 103 --------------------------------------------------------------------------------- */

static void menu_draw(Biing *b) { /* 0x4D709 */
    const uint8_t *k = support_kitchen(sim(b));
    draw(b, IMG_MENU);
    draw(b, k[KR_COOKING] ? IMG_MENU_ON : IMG_MENU_OFF);
    clear(b);
    text(b, 2891);
    int recipe = k[KR_RECIPE] < 33 ? k[KR_RECIPE] : 0; /* engine guard: a recipe outside the table names recipe 0 */
    uint32_t a = (uint32_t)program_word(&b->prog, RECIPE_NAMES, 4u * (uint32_t)recipe) |
                 (uint32_t)program_word(&b->prog, RECIPE_NAMES, 4u * (uint32_t)recipe + 2) << 16;
    raw_string(b, a, 64); /* a pointer of the build */
    text(b, 2892);
    int u[4], n = support_utensils(sim(b), recipe, u);
    for (int i = 0; i < n; i++) {
        if (i) text(b, 1063);
        if (u[i] < 6) table_string(b, UTENSIL_NAMES, 20u * (uint32_t)u[i], 19);
    }
    text(b, 2893);
    num(b, u16(k + KR_LEVEL));
    text(b, 2894);
    if (!k[KR_COOKING]) text(b, 2896);
    else {
        text(b, 2895);
        if (!get16(k + KR_COOK)) text(b, 2890);
        else rooms_ui_staff_name(b, get16(k + KR_COOK));
    }
    left(b, 0, 9, 136, 27, 370, 2);
}

static void menu(Biing *b) { /* 0x4D6F4 */
    menu_draw(b);
    screen(b, SCR_MENU);
}

/* ---- golf report 148 ------------------------------------------------------------------------------------------ */

static void golf_report(Biing *b) { /* 0x3ACA8 */
    const uint8_t *g = sim_room_record(sim(b), 0, 12, 0);
    set_continue(b, 0x36A7B);
    if (!g[GR_PRESENT]) {
        if (get16(g + GR_ATTENDANT)) box(b, PIC_NONE, SU_GOLF_IDLE, SCR_ROOM_BOX);
        else box(b, PIC_SORRY, SU_GOLF_NO_NURSE, SCR_ROOM_BOX);
        return;
    }
    draw(b, IMG_PAD);
    draw(b, IMG_THUMB);
    draw(b, IMG_PORTRAIT);
    clear(b);
    text(b, 1650);
    left(b, 4, 5, 260, 54, 330, 2);
    int list[64], n = g[GR_PRESENT], got = support_golfers(sim(b), list, 64);
    clear(b);
    if (n == 1) {
        text(b, 1651);
        if (got) rooms_ui_staff_name(b, list[0]);
        text(b, 1652);
        rooms_ui_staff_name(b, get16(g + GR_ATTENDANT));
        text(b, 1653);
    } else {
        text(b, 1654);
        for (int i = 0; i < got; i++) {
            if (i) text(b, i == n - 1 ? 1655 : 1063);
            rooms_ui_staff_name(b, list[i]);
        }
        text(b, 1656);
        if (got) rooms_ui_staff_name(b, list[0]);
        text(b, 1657);
        if (n == 2) {
            text(b, 1658);
            if (got > 1) rooms_ui_staff_name(b, list[1]);
            text(b, 1659);
        } else {
            text(b, 1660);
        }
    }
    left(b, 5, 4, 132, 140, 400, 2);
    screen(b, SCR_GOLF_REPORT);
}

/* ---- advertising 119 / 121 ----------------------------------------------------------------------------------- */

static int campaign_type(Biing *b) { return hospital(sim(b), 0)[AD_TYPE]; }
static void type_picture(Biing *b) {
    int t = campaign_type(b);
    draw(b, t == 1 ? IMG_SIGN : t == 3 ? IMG_LEAFLETS : IMG_BANNER);
}
static void slogan(Biing *b) {
    hospital_name(b, 0);
    int k = hospital(sim(b), 0)[AD_SLOGAN];
    text(b, 2404 + (k < 12 ? k : 0));
}

/* 0x4D513. The type text, the slogan and the duration lines keep the heading's line spacing 2: the left renderer keeps
 * the caller's spacing (docs/ui-support.md, 119; the original's render shows 14 / 20 px line pitches in fonts 5 / 4). */
static void campaign_draw(Biing *b) {
    const uint8_t *r = hospital(sim(b), 0);
    int t = campaign_type(b);
    draw(b, IMG_PAD), draw(b, IMG_THUMB), draw(b, IMG_PLUS), draw(b, IMG_MINUS), draw(b, IMG_PAPERS);
    type_picture(b);
    clear(b);
    text(b, 1463);
    left(b, 4, 5, 244, 45, 260, 2);
    clear(b);
    text(b, t == 1 ? 1466 : t == 3 ? 1465 : 1464);
    left(b, 5, 4, 136, 110, 370, 2);
    clear(b);
    slogan(b);
    left(b, 4, 5, 136, 180, 370, 2);
    clear(b);
    text(b, 1468), num(b, u16(r + AD_DURATION)), text(b, 1469), num(b, u16(r + AD_COST)), text(b, 1470);
    left(b, 5, 4, 136, 236, 370, 2);
}

static void campaign_status(Biing *b) { /* 0x4DA2E */
    const uint8_t *r = hospital(sim(b), 0);
    int t = campaign_type(b);
    draw(b, IMG_PAD), draw(b, IMG_THUMB);
    type_picture(b);
    int n = support_campaign_reached(sim(b));
    int running = get16(r + AD_LEFT) >= 0;
    clear(b);
    text(b, running ? 1475 : 1474);
    left(b, 4, 5, 244, 45, 260, 1);
    clear(b);
    if (running) {
        if (t == 1) text(b, 1476), slogan(b), text(b, 2438);
        else if (t == 3) text(b, 1479), slogan(b), text(b, 1480);
        else text(b, 1477), slogan(b), text(b, 1478);
    } else {
        text(b, 2421);
    }
    if (!n) {
        text(b, t == 1 ? 1481 : t == 3 ? 1483 : 1482);
    } else { /* 0x4DC4E: the counts start with 2422 (2421 is only the ended state's body, docs/ui-support.md) */
        int good = u16(r + AD_GOOD), neutral = u16(r + AD_NEUTRAL), bad = u16(r + AD_BAD);
        text(b, 2422);
        if (n == 1) text(b, 2425);
        else text(b, 2423), num(b, n), text(b, 2424);
        text(b, 2427);
        if (!good) text(b, 2430);
        else if (good == 1) text(b, 2429);
        else text(b, 2428), num(b, good), text(b, 2424);
        text(b, 2431);
        if (!neutral) text(b, 2433);
        else if (neutral == 1) text(b, 2432);
        else num(b, neutral), text(b, 2424);
        text(b, 2434);
        if (!bad) text(b, 2436);
        else if (bad == 1) text(b, 2435);
        else if (program_english(&b->prog)) num(b, bad), text(b, 2424); /* no 2437 (docs/editions.md 7.3, rule H) */
        else text(b, 2437), num(b, bad), text(b, 2424);
    }
    left(b, 5, 4, 136, 110, 370, 2);
    screen(b, SCR_CAMPAIGN_STATUS);
}

static void campaign_open(Biing *b) { /* 0x4D3EE */
    if (support_campaign_open(sim(b)) == SCR_CAMPAIGN) {
        campaign_draw(b);
        screen(b, SCR_CAMPAIGN);
    } else {
        campaign_status(b);
    }
}

static void campaign_button(Biing *b, void (*f)(Sim *)) {
    f(sim(b));
    campaign_draw(b);
}

static void campaign_hover(Biing *b, int enter) { /* 0x4DD9E / 0x4DDE2 */
    int t = campaign_type(b);
    if (enter) draw(b, t >= 1 && t <= 3 ? 474 + t : IMG_NO_CAMPAIGN);
    else draw(b, t ? IMG_ADMIN : IMG_NO_CAMPAIGN_LEAVE);
}

/* ---- vehicle market 130 / 134 ------------------------------------------------------------------------------- */

static int car_u16(Biing *b, int id, int off) {
    return id >= 0 && id < CAR_IDS ? (uint16_t)program_word(&b->prog, CAR_CATALOG, 20u * (uint32_t)id + (uint32_t)off) : 0;
}

static void car_card(Biing *b, int id) { /* the shared part of 0x4D0C0 / 0x4D27B */
    draw(b, IMG_PAD), draw(b, IMG_THUMB), draw(b, IMG_PAPERS);
    if (car_u16(b, id, 10)) draw(b, car_u16(b, id, 10));
    clear(b);
    if (car_u16(b, id, 0)) text(b, car_u16(b, id, 0));
    left(b, 4, 5, 324, 40, 240, 2);
}

static void buy_draw(Biing *b) { /* 0x4D0C0 */
    int i = get16(pb(b) + SU_P_SHOWN);
    const uint8_t *o = support_car_offer(sim(b), i < 0 || i >= CAR_OFFERS ? 0 : i);
    int id = o[CO_CAR];
    car_card(b, id);
    clear(b);
    text(b, 980), num(b, o[CO_CONDITION]), text(b, 981);
    left(b, 4, 5, 324, 90, 330, 2);
    clear(b);
    text(b, 982), num(b, u16(o + CO_PRICE)), text(b, 983);
    left(b, 4, 5, 324, 110, 330, 2);
    clear(b);
    text(b, 986), num(b, i + 1);
    left(b, 4, 5, 324, 130, 330, 2);
    clear(b);
    text(b, 984);
    hospital_name(b, (int8_t)o[CO_SELLER]);
    if (car_u16(b, id, 8)) text(b, car_u16(b, id, 8));
    left(b, 4, 5, 120, 188, 415, 0);
}

static void buy_open(Biing *b) { /* 0x4CEDD */
    int r = support_buy_open(sim(b));
    if (r) {
        box(b, r == SU_NO_GARAGE ? PIC_NOT_YET : PIC_NONE, r, SCR_ADMIN_BOX);
        if (r == SU_NO_GARAGE) sound_request(&b->sound, 0xB3, 0x140, 60); /* 0x4CFE7 */
        return;
    }
    buy_draw(b);
    screen(b, SCR_BUY);
}

static void buy_next(Biing *b) {
    if (support_buy_next(sim(b))) box(b, PIC_NONE, SU_NO_OFFER, SCR_ADMIN_BOX);
    else buy_draw(b);
}

static void buy(Biing *b) { /* 0x4ECD5 */
    CarResult o;
    int r = support_buy(sim(b), &o);
    if (r == SU_GARAGE_FULL) box(b, PIC_FULL, r, SCR_ADMIN_BOX);
    else if (r == SU_NO_CASH) box(b, PIC_SORRY, r, SCR_ADMIN_BOX), sound_request(&b->sound, 0xB3, 0x140, 60); /* 0x4CFB7 */
    else {
        /* 0x4EE0C: a car bought from another seller; 0x4EE5C: the player's own offer bought */
        sound_request(&b->sound, r == SU_BOUGHT ? 0xCB : 0xB3, 0x140, 60);
        clear(b);
        text(b, r == SU_BOUGHT ? 990 : 2749);
        if (car_u16(b, o.car, 0)) text(b, car_u16(b, o.car, 0));
        if (r == SU_BOUGHT) text(b, 991), num(b, o.price), text(b, 992);
        else text(b, 2750);
        rooms_ui_box(b, r == SU_BOUGHT ? PIC_OK : PIC_FULL, SCR_ADMIN_BOX);
    }
}

static void sell_draw(Biing *b) { /* 0x4D27B */
    int k = get16(pb(b) + SU_P_SHOWN);
    const uint8_t *p = support_garage(sim(b)) + GA_PLACE + 4 * (k < 0 || k >= GA_PLACE_COUNT ? 0 : k);
    int id = p[0] - 1;
    car_card(b, id);
    clear(b);
    text(b, 980), num(b, p[1]), text(b, 981);
    left(b, 4, 5, 324, 90, 330, 2);
    clear(b);
    text(b, 999), num(b, k + 1);
    left(b, 4, 5, 324, 130, 330, 2);
    clear(b);
    text(b, 1000);
    if (car_u16(b, id, 8)) text(b, car_u16(b, id, 8));
    left(b, 4, 5, 120, 188, 415, 0);
}

static void sell_open(Biing *b) { /* 0x4D045 */
    int r = support_sell_open(sim(b));
    if (r) {
        box(b, r == SU_SELL_NO_GARAGE ? PIC_NOT_YET : r == SU_NO_CAR ? PIC_BAD : PIC_NONE, r, SCR_ADMIN_BOX);
        if (r == SU_SELL_NO_GARAGE) sound_request(&b->sound, 0xB3, 0x140, 60); /* 0x4D017 */
        return;
    }
    sell_draw(b);
    screen(b, SCR_SELL);
}

static void sell_next(Biing *b) { /* 0x502F2 */
    if (support_sell_next(sim(b))) box(b, PIC_NOT_YET, SU_NO_CARS, SCR_GARAGE_BOX);
    else sell_draw(b);
}

static void sell(Biing *b) { /* 0x50182 */
    CarResult o;
    int r = support_sell(sim(b), &o);
    box(b, r == SU_MARKET_FULL ? PIC_NONE : PIC_OWN, r, SCR_ADMIN_BOX);
}

/* ---- thugs 110 ---------------------------------------------------------------------------------------------- */

static void thug_draw(Biing *b) { /* 0x4867E: the personnel sheet without its action buttons */
    draw(b, IMG_SHEET), draw(b, IMG_FIST), draw(b, IMG_PREV), draw(b, IMG_NEXT);
    /* "Nr." is 17 for every thug in the original (docs/ui-support.md, section 110); FIX-2 (docs/fixes.md) */
    int slot = get16(pb(b) + SU_P_SLOT);
    personnel_ui_sheet_body_nr(b, slot, support_thug_sheet_nr(slot));
}

static void thugs(Biing *b) { /* 0x48600 */
    if (support_thugs_open(sim(b))) {
        box(b, PIC_NONE, SU_NO_THUG, SCR_ADMIN_BOX);
        return;
    }
    thug_draw(b);
    screen(b, SCR_THUGS);
}

static void thug_step(Biing *b, int step) {
    if (support_thug_step(sim(b), step)) thugs(b);
    else thug_draw(b);
}

static void thug_send(Biing *b) { /* 0x4833A */
    int slot = get16(pb(b) + SU_P_SLOT);
    support_thug_send(sim(b));
    rooms_ui_box_staff(b, PIC_BACK, 911, get16(staff_slots(sim(b), 0) + slot * STAFF_SLOT_SIZE), 912, SCR_ADMIN_BOX);
}

static void thug_independent(Biing *b) { /* 0x251FB: the personnel sheet's routine, its boxes on screen 36 */
    int slot = get16(pb(b) + SU_P_SLOT), index = get16(staff_slots(sim(b), 0) + slot * STAFF_SLOT_SIZE);
    switch (personnel_toggle_independent(sim(b), slot)) {
    case PERS_NOW_DEPENDENT: rooms_ui_box_staff(b, PIC_SORRY, 125, index, 126, SCR_SHEET_BOX); return;
    case PERS_NOT_YET: rooms_ui_box_staff(b, PIC_NONE, 127, index, 128, SCR_SHEET_BOX); return;
    default: rooms_ui_box_staff(b, PIC_OK, 129, index, 130, SCR_SHEET_BOX); return;
    }
}

static void thug_frame(Biing *b) { /* 0x48518 */
    if (!pb(b)[0x33]) return;
    thug_draw(b);
    pb(b)[0x33] = 0;
}

/* ---- dispatch ------------------------------------------------------------------------------------------------ */

int support_routine(Biing *b, uint32_t r) {
    Sim *s = sim(b);
    switch (r) {
    /* kitchen and menu */
    case 0x36AD9: room_entry(b, r, IMG_KITCHEN, SCR_KITCHEN); return 1;
    case 0x3710B: case 0x3702D: case 0x3717D: case 0x3706B: room_sound_frame(s, r); return 1; /* ambience */
    case 0x4D6F4: menu(b); return 1;
    case 0x4D709: menu_draw(b); return 1;
    case 0x4DEE3: support_next_dish(s), menu_draw(b); return 1;
    case 0x4DF2B: if (support_cheaper(s)) menu_draw(b); return 1;
    case 0x4DF9E: if (support_dearer(s)) menu_draw(b); return 1;
    case 0x4DFE7: support_cooking(s), menu_draw(b); return 1;
    case 0x36B5C: draw(b, support_kitchen(s)[KR_COOKING] ? IMG_MENU_OFF : IMG_MENU_ON); return 1;
    case 0x36B89: draw(b, support_kitchen(s)[KR_COOKING] ? IMG_MENU_ON : IMG_MENU_OFF); return 1;
    /* club room and golf room */
    case 0x3BD5E: room_entry(b, r, IMG_CLUB, SCR_CLUB); return 1;
    case 0x36A7B: room_entry(b, r, IMG_GOLF, SCR_GOLF); return 1;
    case 0x3ACA8: golf_report(b); return 1;
    /* advertising */
    case 0x4D3EE: campaign_open(b); return 1;
    case 0x4DD9E: campaign_hover(b, 1); return 1;
    case 0x4DDE2: campaign_hover(b, 0); return 1;
    case 0x4D513: campaign_draw(b); return 1;
    case 0x4E105: campaign_button(b, support_campaign_shorter); return 1;
    case 0x4E0E0: campaign_button(b, support_campaign_longer); return 1;
    case 0x4E0BA: campaign_button(b, support_campaign_slogan); return 1;
    case 0x4E094: campaign_button(b, support_campaign_type); return 1;
    case 0x4E014: box(b, PIC_OK, support_campaign_start(s), SCR_ADMIN_BOX); return 1;
    case 0x4E127: support_campaign_discard(s), call(b, 0x2EDEE); return 1;
    case 0x4DA2E: campaign_status(b); return 1;
    case 0x4DA07: if (support_campaign_changed(s)) campaign_status(b); return 1;
    case 0x4E16D: support_campaign_confirm(s), call(b, 0x2EDEE); return 1;
    /* vehicle market */
    case 0x4CEDD: buy_open(b); return 1;
    case 0x4D0C0: buy_draw(b); return 1;
    case 0x4EC7F: buy_next(b); return 1;
    case 0x4ECD5: buy(b); return 1;
    case 0x4D045: sell_open(b); return 1;
    case 0x4D27B: sell_draw(b); return 1;
    case 0x502F2: sell_next(b); return 1;
    case 0x50182: sell(b); return 1;
    /* thugs */
    case 0x48600: thugs(b); return 1;
    case 0x4867E: thug_draw(b); return 1;
    case 0x48518: thug_frame(b); return 1;
    case 0x4833A: thug_send(b); return 1;
    case 0x4852F: thug_step(b, 1); return 1;
    case 0x4859A: thug_step(b, -1); return 1;
    case 0x251FB: thug_independent(b); return 1;
    default: return 0;
    }
}
