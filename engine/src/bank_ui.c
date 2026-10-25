/* The blood and organ bank's screens (docs/ui-bank.md): the bank 25, buy blood 88, buy an organ 89, sell blood 87,
 * sell an organ 150, the blood report 85, the organ report 86 and the OK box 82. The rules are in bank.c; this file
 * draws the notepad with its offer text and button row, builds the texts and moves between the screens. The message
 * boxes are rooms_ui.c's. No sounds: the room ambience of the frame routine and the entry's sound request are left
 * out. The hover and normal images of the hotspots are drawn by the screen layer (no hotspot has an enter or leave
 * routine). */
#include "room_sound.h"
#include "bank_ui.h"

#include "bank.h"
#include "rooms.h"
#include "rooms_ui.h"
#include "sim_internal.h"

enum {
    IMG_BANK = 192, IMG_PAD = 832, IMG_BLOOD = 833, IMG_ORGAN = 834, IMG_MORE = 835, IMG_LESS = 837,
    IMG_DEARER = 839, IMG_CHEAPER = 841, IMG_NEXT = 845, IMG_PREVIOUS = 847, IMG_PREV_ORGAN = 1356,
    IMG_NEXT_ORGAN = 1358, IMG_FIST = 844,
    PIC_OK = 509, PIC_BACK = 510, PIC_NOT_YET = 513, PIC_BAD = 515, PIC_NONE = 518,
};
enum { SCR_BANK = 25, SCR_BOX = 82, SCR_BLOOD_REPORT = 85, SCR_ORGAN_REPORT = 86, SCR_SELL_BLOOD = 87,
       SCR_BUY_BLOOD = 88, SCR_BUY_ORGAN = 89, SCR_SELL_ORGAN = 150 };
enum { H_DONORS = 0x3E, H_DONATED = 0x40, H_USED = 0x5A, H_SOLD = 0x5E, H_BOUGHT = 0x62, H_LOST = 0x66,
       H_VANISHED = 0x6A, H_ORGANS_BOUGHT = 0x7E, H_ORGANS_SOLD = 0x80, H_ORGANS_DESTROYED = 0x82,
       H_ORGANS_USED = 0x84, H_ORGANS_OWN = 0x88 };
#define NAMES 0x3BCF /* hospital names, 30 bytes each; 16 = the dealer */
#define NAME_SIZE 30
#define ORGAN_GENDER 0x4713 /* gen(id) = the byte at 0x4713 + 2 id */
#define AVERAGE_START 0xAEA0 /* the average blood price kept between offers; its program-image value at the start */

static Sim *sim(Biing *b) { return &b->sim; }
static void draw(Biing *b, int image) { dulog_draw(&b->ui, image, 0, 0); }
static void screen(Biing *b, int n) { dulog_set_screen(&b->ui, n); }
static int u16(const uint8_t *p) { return (uint16_t)get16(p); }
static void clear(Biing *b) { rooms_ui_clear(b); }
static void text(Biing *b, int entry) { rooms_ui_text(b, entry); }
static void num(Biing *b, long v) { rooms_ui_num(b, v); }
static void raw(Biing *b, const char *t) {
    size_t n = 0;
    while (t[n]) n++;
    msg_append(&b->msg, (const uint8_t *)t, n);
}
static void box_text(Biing *b, int picture, int entry) { rooms_ui_box_text(b, picture, entry, SCR_BOX); }

static uint32_t average_price(Biing *b) { /* [0xAEA0] */
    static uint32_t value;
    static int loaded;
    if (!loaded) value = (uint16_t)program_u16(&b->prog, AVERAGE_START), loaded = 1;
    return bank_average_blood(sim(b), &value);
}

/* font 1, the left renderer with word wrap, line spacing 0 */
static void render(Biing *b, int x, int y, int width) {
    TextLayout l = {&b->fonts[1], b->prog.charmap, x, y, width, 0, 5, 1};
    text_left(&b->screen, &l, b->msg.buf, b->msg.len);
}
static void line(Biing *b, int y) { render(b, 184, y, 295); }
static void title(Biing *b) { render(b, 262, 45, 260); }

static void hospital_name(Biing *b, int h) {
    if (h < 0 || h > 16) return;
    const uint8_t *p = program_table(&b->prog, NAMES) + h * NAME_SIZE;
    size_t n = 0;
    while (n < NAME_SIZE - 1 && p[n] != '$' && p[n]) n++;
    msg_append(&b->msg, p, n);
}
/* title, (number,) CR, a hospital name. The report titles 2610 and 2551 end in a CR of their own, which is taken as
 * the spec's CR (engine choice: a second one would leave an empty line) */
static void title_with(Biing *b, int entry, long number, int h) {
    clear(b);
    text(b, entry);
    if (number >= 0) num(b, number);
    if (!b->msg.len || b->msg.buf[b->msg.len - 1] != '\r') raw(b, "\r");
    hospital_name(b, h);
    title(b);
}

static int valid_organ(int id) { return id >= 1 && id <= 11; }
static int gender(Biing *b, int id) { return valid_organ(id) ? program_u8(&b->prog, ORGAN_GENDER, 2u * (uint32_t)id) : 0; }
static void organ_name(Biing *b, int id) {
    if (valid_organ(id)) text(b, 3662 + id);
}
static void organ_plural(Biing *b, int id) {
    if (valid_organ(id)) text(b, 3674 + id);
}
static void adjective(Biing *b, int quality) { /* 2911 + quality / 8; engine choice: above 103 the last one */
    int k = quality / 8;
    text(b, 2911 + (k > 12 ? 12 : k));
}

/* ---- the offer drawing 0x4AFB7 ---------------------------------------------------------------------------------- */

static void age_line(Biing *b, const uint8_t *m) {
    clear(b);
    if (get16(m + BM_AGE) >= 0) text(b, 2621), num(b, get16(m + BM_AGE)), text(b, 2622);
    else text(b, 2623);
    line(b, 253);
}

static void blood_offer(Biing *b, const uint8_t *m) {
    int seller = (int8_t)m[BM_SELLER], q = u16(m + BM_QUANTITY), price = u16(m + BM_PRICE);
    draw(b, IMG_BLOOD);
    title_with(b, 2628, bank_slot_shown(sim(b)) + 1, seller);
    clear(b);
    hospital_name(b, seller), text(b, 2629), num(b, q), text(b, 2630);
    line(b, 110);
    uint32_t v = bank_value_per_5000(price, q);
    int k = v / 32 > 2 ? 2 : (int)(v / 32);
    clear(b);
    text(b, 2631), text(b, 2632 + k), num(b, price), text(b, 2635 + k), text(b, 2626), num(b, (long)v), text(b, 2627);
    line(b, 149);
    uint32_t avg = average_price(b);
    clear(b);
    text(b, 2625), num(b, (long)avg), text(b, 2624);
    line(b, 201);
    age_line(b, m);
}

static void organ_offer(Biing *b, const uint8_t *m) {
    int seller = (int8_t)m[BM_SELLER], id = m[BM_ID], q = m[BM_QUALITY], price = u16(m + BM_PRICE), g = gender(b, id);
    draw(b, IMG_ORGAN);
    title_with(b, 2620, bank_slot_shown(sim(b)) + 1, seller);
    clear(b);
    hospital_name(b, seller), text(b, 2629), text(b, 1611 + g), adjective(b, q);
    if (g == 0) text(b, 2907);
    if (g == 2) text(b, 2906);
    raw(b, " ");
    organ_name(b, id), text(b, 2905);
    line(b, 110);
    clear(b);
    text(b, 2924 + g), organ_name(b, id), text(b, 2904), num(b, q), text(b, 2903), num(b, price), text(b, 2902);
    line(b, 149);
    int sum, n = bank_organ_offers(sim(b), id, &sum);
    clear(b);
    if (n < 2) {
        text(b, 2901), text(b, g == 1 ? 2909 : 2908), raw(b, " "), organ_name(b, id), text(b, 2900);
        organ_plural(b, id), text(b, 349);
    } else {
        text(b, 2899), num(b, n), organ_plural(b, id), text(b, 2898), num(b, sum / n), text(b, 2897);
    }
    line(b, 201);
    age_line(b, m);
}

static void offer(Biing *b) {
    const uint8_t *m = bank_offer(sim(b));
    draw(b, IMG_PAD);
    if (m[BM_KIND] == 0) blood_offer(b, m);
    else organ_offer(b, m);
}

static void row(Biing *b, const int *images, int n) {
    for (int i = 0; i < n; i++) draw(b, images[i]);
}

/* ---- 25 - the bank ---------------------------------------------------------------------------------------------- */

static void bank(Biing *b) { /* 0x3BE59: the site map's click and the return of every screen here */
    if (!rooms_site_click(sim(b), b->ui.flags, rooms_site_building(0x3BE59))) return; /* no bank: nothing */
    draw(b, IMG_BANK);
    screen(b, SCR_BANK);
    room_sound_view(sim(b), 17); /* 0x3BE81, after the screen */
}

static void back(Biing *b) { /* 0x4B5B6 */
    bank_back(sim(b));
    bank(b);
}

/* ---- 88 / 89 - buying ------------------------------------------------------------------------------------------- */

static void buy_open(Biing *b, int organ) { /* 0x4D8E3 / 0x4AEDE */
    int r = bank_buy_open(sim(b), organ);
    if (r) {
        box_text(b, PIC_NONE, r);
        return;
    }
    offer(b);
    row(b, (const int[]){IMG_FIST, IMG_NEXT + 1, IMG_PREVIOUS + 1}, 3);
    screen(b, organ ? SCR_BUY_ORGAN : SCR_BUY_BLOOD);
}

static void buy_step(Biing *b, int organ, int step) { /* 0x4F4B7 / 0x4F5A5, 0x4F52E / 0x4F61C */
    int r = bank_buy_step(sim(b), organ, step);
    if (r) {
        box_text(b, PIC_NONE, r);
        return;
    }
    offer(b);
    if (step > 0) row(b, (const int[]){IMG_FIST, IMG_NEXT, IMG_PREVIOUS + 1}, 3);
    else row(b, (const int[]){IMG_FIST, IMG_NEXT + 1, IMG_PREVIOUS}, 3);
}

static void bought(Biing *b, const BankResult *o, int organ) {
    clear(b);
    if (organ) {
        text(b, 1639), text(b, 1611 + gender(b, o->id)), organ_name(b, o->id), text(b, 1640);
    } else {
        text(b, 1635), num(b, o->quantity), text(b, 1636);
    }
    hospital_name(b, o->seller), text(b, 1637), num(b, o->price), text(b, 1638);
    rooms_ui_box(b, PIC_BACK, SCR_BOX);
}

static void buy_blood(Biing *b) { /* 0x4F777 */
    BankResult o;
    int r = bank_buy_blood(sim(b), &o);
    if (r == BANK_BLOOD_BOUGHT) bought(b, &o, 0);
    else if (r == BANK_BLOOD_OWN) box_text(b, PIC_NOT_YET, r);
    else box_text(b, PIC_BAD, r);
}

static void buy_organ(Biing *b) { /* 0x4F693 */
    BankResult o;
    int r = bank_buy_organ(sim(b), &o);
    if (r == BANK_ORGAN_BOUGHT) bought(b, &o, 1);
    else box_text(b, PIC_BAD, r);
}

/* ---- 87 / 150 - selling ----------------------------------------------------------------------------------------- */

/* the row of 87; lit = the clicked button (its hover image), 0 = none */
static void blood_row(Biing *b, int lit) {
    const int normal[4] = {IMG_MORE, IMG_LESS, IMG_DEARER, IMG_CHEAPER};
    for (int i = 0; i < 4; i++) draw(b, normal[i] == lit ? lit : normal[i] + 1);
    draw(b, IMG_FIST);
}
static void organ_row(Biing *b) {
    row(b, (const int[]){IMG_PREV_ORGAN + 1, IMG_NEXT_ORGAN + 1, IMG_DEARER + 1, IMG_CHEAPER + 1, IMG_FIST}, 5);
}

static void sell_open(Biing *b, int organ) { /* 0x4D947 / 0x4B606 */
    int r = organ ? bank_sell_organ_open(sim(b)) : bank_sell_blood_open(sim(b));
    if (r) {
        box_text(b, r == BANK_MARKET_FULL ? PIC_NONE : PIC_BAD, r);
        return;
    }
    offer(b);
    if (organ) organ_row(b);
    else blood_row(b, 0);
    screen(b, organ ? SCR_SELL_ORGAN : SCR_SELL_BLOOD);
}

static void blood_button(Biing *b, int image) { /* 0x4C6E0, 0x4C74B, 0x4C7A6, 0x4C800 */
    if (image == IMG_MORE) bank_blood_more(sim(b));
    else if (image == IMG_LESS) bank_blood_less(sim(b));
    else bank_price_step(sim(b), image == IMG_DEARER ? 1 : -1);
    offer(b);
    blood_row(b, image);
}

static void organ_button(Biing *b, int step, int price) { /* 0x4F919 / 0x4F8CF; 0x4C87C / 0x4C859 */
    if (price) bank_price_step(sim(b), step);
    else if (bank_organ_step(sim(b), step) < 0) { /* all slots empty: back */
        back(b);
        return;
    }
    offer(b);
    organ_row(b);
}

/* ---- 85 - blood report ------------------------------------------------------------------------------------------ */

static void blood_report(Biing *b) { /* 0x2790F */
    const uint8_t *h = world_hospital(&b->world, 0), *B = bank_record(sim(b));
    int32_t stock = B ? get32(B + BR_STOCK) : 0, cap = B ? get32(B + BR_CAPACITY) : 0;
    int32_t donated = get32(h + H_DONATED), bought = get32(h + H_BOUGHT), used = get32(h + H_USED);
    int32_t sold = get32(h + H_SOLD), lost = get32(h + H_LOST), vanished = get32(h + H_VANISHED);
    int donors = u16(h + H_DONORS);
    draw(b, IMG_PAD), draw(b, IMG_BLOOD), draw(b, IMG_FIST);
    title_with(b, 2610, -1, 0);
    clear(b);
    if (!stock) text(b, 2613);
    else text(b, 2611), num(b, stock), text(b, 2612);
    line(b, 97);
    clear(b);
    text(b, 2614), num(b, cap), text(b, 2615);
    line(b, 136);
    clear(b);
    if (!donors) text(b, 2601);
    else if (donors == 1) text(b, 2602), num(b, donated), text(b, 2618);
    else text(b, 2616), num(b, donors), text(b, 2617), num(b, donated), text(b, 2618);
    line(b, 175);
    clear(b);
    if (!bought) text(b, 2595);
    else text(b, 2606), num(b, bought), text(b, 2605);
    line(b, 214);
    clear(b);
    if (donated + bought == 0) {
        text(b, 2598);
    } else {
        if (!used) text(b, 2599);
        else text(b, 2600), num(b, used), text(b, 2609);
        if (!sold) text(b, 2597);
        else num(b, sold), text(b, 2608);
        if (!lost) text(b, 2596);
        else num(b, lost), text(b, 2607);
    }
    line(b, 240);
    clear(b);
    if (!vanished) text(b, 2594); /* +0x6A has no writer: always this line */
    else text(b, 2604), num(b, vanished), text(b, 2603);
    line(b, 292);
    screen(b, SCR_BLOOD_REPORT);
}

/* ---- 86 - organ report ------------------------------------------------------------------------------------------ */

static void organ_counts(Biing *b, const uint8_t *h) { /* lines 266 and 292 */
    int bought = u16(h + H_ORGANS_BOUGHT), own = u16(h + H_ORGANS_OWN), sold = u16(h + H_ORGANS_SOLD);
    int used = u16(h + H_ORGANS_USED), destroyed = u16(h + H_ORGANS_DESTROYED);
    clear(b);
    if (bought + own == 0) {
        text(b, 2582);
    } else {
        text(b, 2581);
        if (!bought) text(b, 2580);
        else if (bought == 1) text(b, 2579);
        else text(b, 2578), num(b, bought), text(b, 2577);
        if (!own) text(b, 2576);
        else if (own == 1) text(b, 2575);
        else text(b, 1063), num(b, own), text(b, 2574);
    }
    line(b, 266);
    clear(b);
    text(b, 2593);
    if (!sold) text(b, 2592);
    else if (sold == 1) text(b, 2591);
    else text(b, 2590), num(b, sold), text(b, 2589);
    if (!used) text(b, 2588);
    else if (used == 1) text(b, 2587);
    else num(b, used), text(b, 2586);
    if (!destroyed) text(b, 2585);
    else if (destroyed == 1) text(b, 2584);
    else num(b, destroyed), text(b, 2583);
    line(b, 292);
}

static void organ_summary(Biing *b) { /* page 0 */
    BankOrganReport r;
    bank_organ_report(sim(b), &r);
    clear(b);
    if (r.held == 0) text(b, 2553);
    else if (r.held == 1) text(b, 2552);
    else text(b, 2554), num(b, r.held), text(b, 2555);
    line(b, 97);
    clear(b);
    if (r.places == 0) text(b, 2556);
    else if (r.free_places == 0) text(b, 2557);
    else if (r.free_places == 1) text(b, 2558);
    else text(b, 2559), num(b, r.free_places), text(b, 2560);
    line(b, 136);
    clear(b);
    if (r.held == 0) text(b, 2561);
    else text(b, 2562), num(b, r.average), text(b, 2563), text(b, 2564 + (r.average / 28 > 3 ? 3 : r.average / 28));
    line(b, 188);
    clear(b);
    if (r.held == 0) text(b, 2568);
    else if (r.minimum >= 60) text(b, 2569);
    else if (r.minimum >= 40) text(b, r.average >= 80 ? 2571 : 2570);
    else text(b, r.average >= 70 ? 2572 : 2573);
    line(b, 240);
    organ_counts(b, world_hospital(&b->world, 0));
}

static void organ_list(Biing *b, int page) { /* pages 1 and 2 */
    const uint8_t *B = bank_record(sim(b));
    int first, n = bank_report_list(sim(b), page, &first);
    clear(b);
    for (int i = 0; i < n; i++) {
        const uint8_t *slot = B + BR_SLOTS + 2 * (first + i);
        organ_name(b, slot[0]), raw(b, ", "), num(b, slot[1]), text(b, 2910);
        adjective(b, slot[1]);
        if (b->msg.len) b->msg.len--; /* the adjective without its last letter */
        raw(b, "\r");
    }
    line(b, 110);
}

static void organ_page(Biing *b) { /* 0x4B6E7 */
    row(b, (const int[]){IMG_PAD, IMG_ORGAN, IMG_NEXT + 1, IMG_PREVIOUS + 1, IMG_FIST}, 5);
    title_with(b, 2551, -1, 0);
    int page = get16(rooms_pb(sim(b)) + BANK_P_SLOT);
    if (page == 0) organ_summary(b);
    else organ_list(b, page);
}

static void organ_report(Biing *b) { /* 0x4B7AD */
    put16(rooms_pb(sim(b)) + BANK_P_SLOT, 0);
    organ_page(b);
    screen(b, SCR_ORGAN_REPORT);
}

/* ---- dispatch --------------------------------------------------------------------------------------------------- */

int bank_routine(Biing *b, uint32_t r) {
    switch (r) {
    case 0x3BE59: bank(b); return 1;
    case 0x370A8: room_sound_frame(sim(b), r); return 1; /* the room ambience */
    case 0x4B5B6: back(b); return 1;
    /* 88 buy blood, 89 buy an organ */
    case 0x4D8E3: buy_open(b, 0); return 1;
    case 0x4AEDE: buy_open(b, 1); return 1;
    case 0x4F4B7: buy_step(b, 0, 1); return 1;
    case 0x4F5A5: buy_step(b, 0, -1); return 1;
    case 0x4F52E: buy_step(b, 1, 1); return 1;
    case 0x4F61C: buy_step(b, 1, -1); return 1;
    case 0x4F777: buy_blood(b); return 1;
    case 0x4F693: buy_organ(b); return 1;
    /* 87 sell blood */
    case 0x4D947: sell_open(b, 0); return 1;
    case 0x4C6E0: blood_button(b, IMG_MORE); return 1;
    case 0x4C74B: blood_button(b, IMG_LESS); return 1;
    case 0x4C7A6: blood_button(b, IMG_DEARER); return 1;
    case 0x4C800: blood_button(b, IMG_CHEAPER); return 1;
    case 0x4BF19: box_text(b, PIC_OK, bank_publish_blood(sim(b))); return 1;
    /* 150 sell an organ */
    case 0x4B606: sell_open(b, 1); return 1;
    case 0x4F919: organ_button(b, 1, 0); return 1;
    case 0x4F8CF: organ_button(b, -1, 0); return 1;
    case 0x4C87C: organ_button(b, 1, 1); return 1;
    case 0x4C859: organ_button(b, -1, 1); return 1;
    case 0x4BF52: box_text(b, PIC_OK, bank_publish_organ(sim(b))); return 1;
    /* 85 and 86 */
    case 0x2790F: blood_report(b); return 1;
    case 0x4B7AD: organ_report(b); return 1;
    case 0x4B7EA: bank_report_page(sim(b), 1), organ_page(b); return 1;
    case 0x4B7CE: bank_report_page(sim(b), -1), organ_page(b); return 1;
    default: return 0;
    }
}
