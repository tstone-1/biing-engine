/* The purchasing screens (docs/ui-purchasing.md): the administration's frame (7), equipment and furniture purchase
 * 151/152, the offers 26/28, the sale 111, the inventory 65 with the item information 147, the warehouse 22 and the
 * OK boxes 27, 41 and 55. The rules are in purchase.c; this file draws, builds the texts and moves between the
 * screens, with the warehouse animation (docs/ui-sheets.md, section 3) and room_sound.c's requests; message boxes do
 * not stop the clock, as on the other screens behind the office. */
#include "room_sound.h"
#include "purchase_ui.h"

#include "purchase.h"
#include "rooms.h"
#include "sim_internal.h"

enum {
    IMG_WAREHOUSE = 181, IMG_EQUIPMENT_GRID = 1363, IMG_FURNITURE_GRID = 1379, IMG_PAD = 417, IMG_LIST = 418,
    IMG_ONE_ROW = 419, IMG_TAB_FURNITURE = 420, IMG_TAB_EQUIPMENT = 421, IMG_TAB_INTO_STORE = 422,
    IMG_TAB_OUT_OF_STORE = 423, IMG_SALE = 406, IMG_PURCHASE = 407, IMG_TITLE = 424, IMG_ROW_FRAME = 427,
    IMG_UP = 431, IMG_DOWN = 433, IMG_INFO = 438, IMG_PREVIOUS = 463, IMG_NEXT = 467, IMG_THUMB = 190,
    IMG_NAME_FIELD = 1364, IMG_CELL_RED = 1365, IMG_CELL_GREY = 1366, IMG_PICTURE_GREY = 1380, IMG_PICTURE_RED = 1381,
    PIC_DONE = 511, PIC_NO = 512, PIC_NOT_YET = 513, PIC_SORRY = 514, PIC_BAD = 515, PIC_NONE = 518,
};
enum { SCR_EQUIPMENT = 151, SCR_FURNITURE = 152, SCR_EQUIPMENT_OFFER = 26, SCR_FURNITURE_OFFER = 28, SCR_OK = 27,
       SCR_SALE = 111, SCR_INVENTORY = 65, SCR_INVENTORY_BOX = 41, SCR_ROOM_BOX = 55, SCR_INFO = 147,
       SCR_WAREHOUSE = 22 };
#define CAPABILITY_TABLE 0x55F4 /* 65 x (u16 capability id, u16 dx, u16 dy), the cells of 151's rows 1-6 */
#define NAMES 0x3BCF /* hospital names, 30 bytes each; 16 = the dealer */
#define NAME_SIZE 30
/* 151's row 0 (ids 3-14) and 152's eight pictures (ids 3-10): the kinds their click stubs write to P+0x55 */
static const uint8_t EQUIPMENT_KINDS[12] = {3, 11, 17, 21, 19, 16, 9, 10, 12, 13, 14, 15};
static const uint8_t FURNITURE_KINDS[8] = {1, 5, 8, 18, 2, 6, 4, 7};

static Sim *sim(Biing *b) { return &b->sim; }
static int pb(Biing *b, int off) { return rooms_pb16(sim(b), off); }
static void draw(Biing *b, int image, int dx, int dy) { dulog_draw(&b->ui, image, dx, dy); }
static void screen(Biing *b, int n) { dulog_set_screen(&b->ui, n); }
static void call(Biing *b, uint32_t r) { b->ui.call(&b->ui, r); }
static int equipment_shown(Biing *b) { return pb(b, PU_EQUIPMENT) != 0; }

/* ---- text ------------------------------------------------------------------------------------------- */

static void clear(Biing *b) { msg_clear(&b->msg); }
static void text(Biing *b, int entry) { biing_msg_entry(b, entry); }
static void num(Biing *b, long v) { msg_append_number(&b->msg, v); }
static void render_left(Biing *b, int x, int y, int width) {
    TextLayout l = {&b->fonts[1], b->prog.charmap, x, y, width, 0, 5, 1};
    text_left(&b->screen, &l, b->msg.buf, b->msg.len);
}
static void render_centred(Biing *b, int x, int y, int width) {
    TextLayout l = {&b->fonts[1], b->prog.charmap, x, y, width, 1, 5, 1};
    text_centred(&b->screen, &l, b->msg.buf, b->msg.len);
}
static void title(Biing *b) { render_left(b, 229, 48, 167); } /* in field 424 */
static void title_text(Biing *b, int entry) {
    clear(b);
    text(b, entry);
    title(b);
}

static const uint8_t *item(Biing *b, int eq, int id) { return purchase_item(sim(b), eq, id); }
static int item_u16(const uint8_t *it, int off) { return it[off] | it[off + 1] << 8; }
static void item_name(Biing *b, int eq, int id) {
    const uint8_t *it = item(b, eq, id);
    text(b, (int)(item_u16(it, 0) | (uint32_t)item_u16(it, 2) << 16));
}
static void article(Biing *b, int eq, int id) { /* 0x2F610, 0x391D2, 0x486E2 */
    const uint8_t *it = item(b, eq, id);
    int a = it[7];
    if (a >= 3) return; /* 3: no article */
    if (program_english(&b->prog)) /* English CD: "a "/"an " by the name (docs/editions.md 7.3, rule A) */
        biing_english_article(b, (int)(item_u16(it, 0) | (uint32_t)item_u16(it, 2) << 16));
    else
        text(b, 1611 + a);
}
static void hospital_name(Biing *b, int h) {
    const uint8_t *p = program_table(&b->prog, NAMES) + h * NAME_SIZE;
    size_t n = 0;
    while (n < NAME_SIZE - 1 && p[n] != '$' && p[n]) n++;
    msg_append(&b->msg, p, n);
}

/* item name, condition and icon in row k of the list */
static void row(Biing *b, int eq, int k, int id, int condition) {
    if (!purchase_item_valid(eq, id)) return;
    clear(b);
    item_name(b, eq, id);
    render_left(b, 179, 100 + 26 * k, 250);
    clear(b);
    num(b, condition);
    render_centred(b, 424, 101 + 26 * k, 80);
    int icon = item_u16(item(b, eq, id), 14);
    if (icon) draw(b, icon, 0, 26 * k);
}

/* "Verwendung", for equipment "Speziell", "Wirkung auf Patienten", "Effektiver Nutzen" (offer and info) */
static void properties(Biing *b, int eq, int id) {
    const uint8_t *it = item(b, eq, id);
    text(b, 1565), text(b, 3925 + it[6]);
    if (eq && (it[10] || it[11] || it[12] || it[13])) {
        text(b, 1566), text(b, 3947 + it[10]);
        for (int k = 11; k <= 13 && it[k]; k++) text(b, 1063), text(b, 3947 + it[k]);
    }
    text(b, 1567), text(b, 1571 + it[4]);
    text(b, 1568), text(b, 1587 + it[5]);
}
static void description(Biing *b) { render_left(b, 155, 139, 300); }

/* the message box over what is on screen, then its OK screen */
static void box(Biing *b, int picture, int scr) {
    biing_message_box(b, picture);
    screen(b, scr);
}
static void refusal(Biing *b, int picture, int entry, int scr) {
    clear(b);
    text(b, entry);
    box(b, picture, scr);
}

/* the hotspot of the current screen whose click (or enter, or leave) routine is r */
static const Record *hotspot(Biing *b, uint32_t r, int *how) {
    if (b->ui.current <= 0 || b->ui.current >= SCREEN_COUNT) return NULL;
    const Script *sc = &b->prog.screens[b->ui.current];
    for (int i = 0; i < sc->count; i++) {
        const Record *rec = &sc->rec[i];
        if (rec->op != OP_HOTSPOT) continue;
        if (rec->click == r) return *how = 0, rec;
        if (rec->enter == r) return *how = 1, rec;
        if (rec->leave == r) return *how = 2, rec;
    }
    return NULL;
}

/* ---- 7, 151, 152 ------------------------------------------------------------------------------------- */

static void purchase_screen(Biing *b, int eq) { /* 0x3B332 / 0x3B3CB */
    purchase_set_filters(sim(b), 0, 0);
    draw(b, eq ? IMG_EQUIPMENT_GRID : IMG_FURNITURE_GRID, 0, 0);
    draw(b, IMG_PURCHASE, 0, 0);
    draw(b, eq ? IMG_TAB_EQUIPMENT : IMG_TAB_FURNITURE, 0, 0);
    draw(b, IMG_TITLE, 0, 0);
    draw(b, IMG_THUMB, 0, 0);
    title_text(b, eq ? 2549 : 2550);
    screen(b, eq ? SCR_EQUIPMENT : SCR_FURNITURE);
}

static void draw_offer(Biing *b, int eq) { /* 0x3B117 / 0x2549C */
    int i = pb(b, PU_INDEX);
    const uint8_t *o = purchase_offer(sim(b), eq, i);
    int id = o[2] | o[3] << 8;
    draw(b, IMG_TITLE, 0, 0);
    clear(b);
    text(b, eq ? 633 : 634), num(b, i + 1); /* the market slot + 1 */
    title(b);
    draw(b, IMG_ONE_ROW, 0, 0);
    row(b, eq, 0, id, o[4]);
    clear(b);
    text(b, 1562), hospital_name(b, o[0]);
    properties(b, eq, id);
    text(b, 1570), num(b, o[1]), text(b, 1470);
    description(b);
}

static void search(Biing *b, int eq) { /* 0x3AF1F / 0x3B02E */
    int r = purchase_search(sim(b), eq);
    if (r) {
        refusal(b, r == PU_NO_STORE_BUY ? PIC_NOT_YET : r == PU_STORE_FULL ? PIC_NO : PIC_NONE, r, SCR_OK);
        return;
    }
    draw(b, IMG_PAD, 0, 0);
    draw(b, IMG_PURCHASE, 0, 0);
    draw(b, eq ? IMG_TAB_EQUIPMENT : IMG_TAB_FURNITURE, 0, 0);
    draw(b, IMG_PREVIOUS, 0, 0);
    draw(b, IMG_NEXT, 0, 0);
    draw(b, IMG_THUMB, 0, 0);
    draw_offer(b, eq);
    screen(b, eq ? SCR_EQUIPMENT_OFFER : SCR_FURNITURE_OFFER);
}

static void all_offers(Biing *b, int eq) { /* 0x3AF14 / 0x3B023 */
    purchase_set_filters(sim(b), 0, 0);
    search(b, eq);
}

/* a cell of 151 or 152: click (filter, search), enter (name, red frame), leave (empty name, grey frame) */
static int cell(Biing *b, uint32_t r) {
    int eq = b->ui.current == SCR_EQUIPMENT, how;
    if (!eq && b->ui.current != SCR_FURNITURE) return 0;
    const Record *rec = hotspot(b, r, &how);
    if (!rec || rec->id < 3 || rec->id > (eq ? 79 : 10)) return 0;
    int n = rec->id - 3, kind = 0, capability = 0;
    if (!eq) kind = FURNITURE_KINDS[n];
    else if (n < 12) kind = EQUIPMENT_KINDS[n];
    else capability = program_word(&b->prog, CAPABILITY_TABLE, 6u * (uint32_t)(n - 12));
    if (how == 0) {
        purchase_set_filters(sim(b), capability, kind);
        search(b, eq);
        return 1;
    }
    draw(b, IMG_NAME_FIELD, 0, 0);
    if (how == 1) {
        clear(b);
        text(b, capability ? 3947 + capability : 3925 + kind);
        render_left(b, 162, 291, 250);
    }
    int frame = eq ? (how == 1 ? IMG_CELL_RED : IMG_CELL_GREY) : (how == 1 ? IMG_PICTURE_RED : IMG_PICTURE_GREY);
    draw(b, frame, rec->ax, rec->ay);
    return 1;
}

/* ---- 26 / 28 ---------------------------------------------------------------------------------------- */

static void offer_back(Biing *b, int eq) { /* 0x3936A / 0x39382 */
    purchase_leave(sim(b), eq);
    call(b, 0x2EDEE);
}

static void offer_step(Biing *b, int eq, int step) {
    purchase_step(sim(b), eq, step);
    draw_offer(b, eq);
}

static void buy(Biing *b, int eq) { /* 0x2F606 / 0x391C8 */
    PurchaseResult o;
    int r = purchase_buy(sim(b), eq, &o);
    if (r == PU_NO_CASH) {
        refusal(b, PIC_NONE, r, SCR_OK);
        return;
    }
    clear(b);
    if (r == PU_OWN) {
        text(b, 1619), article(b, eq, o.id), item_name(b, eq, o.id), text(b, 1620);
        box(b, PIC_NOT_YET, SCR_OK);
        return;
    }
    text(b, 1615), article(b, eq, o.id), item_name(b, eq, o.id), text(b, 1616), hospital_name(b, o.seller);
    text(b, 1617), num(b, o.price), text(b, 1618);
    box(b, PIC_DONE, SCR_OK);
}

/* ---- the list (65, 111) ------------------------------------------------------------------------------ */

static void list(Biing *b) { /* 0x2645C */
    int eq = equipment_shown(b), first = pb(b, PU_INDEX);
    draw(b, IMG_LIST, 0, 0);
    draw(b, IMG_ROW_FRAME, 0, 26 * pb(b, PU_ROW));
    for (int k = 0; k < 8; k++) {
        int c, id = purchase_list_item(sim(b), first + k, &c);
        if (id > 0) row(b, eq, k, id, c);
    }
}

static void page_down(Biing *b) {
    if (purchase_page_down(sim(b))) list(b);
}

/* ---- 111 ----------------------------------------------------------------------------------------------- */

static void sale_title(Biing *b) { /* 0x269C9 */
    int eq = equipment_shown(b);
    draw(b, IMG_TITLE, 0, 0);
    draw(b, eq ? IMG_TAB_EQUIPMENT : IMG_TAB_FURNITURE, 0, 0);
    title_text(b, eq ? 1560 : 1561);
}

static void sale_screen(Biing *b, int eq) { /* 0x4899D / 0x48A55 */
    int r = purchase_sell_open(sim(b), eq);
    if (r) {
        refusal(b, PIC_NOT_YET, r, SCR_OK);
        return;
    }
    draw(b, IMG_PAD, 0, 0);
    draw(b, IMG_SALE, 0, 0);
    draw(b, IMG_DOWN, 0, 0);
    draw(b, IMG_UP, 0, 0);
    draw(b, IMG_THUMB, 0, 0);
    sale_title(b);
    list(b);
    screen(b, SCR_SALE);
}

static void sell(Biing *b) { /* 0x486E2 */
    PurchaseResult o;
    int eq = equipment_shown(b), r = purchase_sell(sim(b), &o);
    if (!r) return;
    if (r != PU_OFFERED) {
        refusal(b, PIC_NONE, r, SCR_OK);
        return;
    }
    clear(b);
    text(b, 635), article(b, eq, o.id), item_name(b, eq, o.id), text(b, 636), num(b, o.condition);
    text(b, 637), num(b, o.price), text(b, eq ? 638 : 639);
    box(b, PIC_DONE, SCR_OK);
}

/* ---- 65 and 147 ------------------------------------------------------------------------------------- */

static void inventory_title(Biing *b) { /* 0x26AA5 */
    int eq = equipment_shown(b), store = pb(b, PU_VIEW) == PU_STORE_VIEW, c = pb(b, PU_CAT);
    int several = c >= 0 && c < 100 && rooms_count(sim(b), c) >= 2;
    draw(b, eq ? IMG_TAB_EQUIPMENT : IMG_TAB_FURNITURE, 0, 0);
    draw(b, store ? IMG_TAB_OUT_OF_STORE : IMG_TAB_INTO_STORE, 0, 0);
    draw(b, IMG_TITLE, 0, 0);
    clear(b);
    text(b, eq ? 380 : 379);
    if (!store) {
        text(b, 936 + c);
        if (several) text(b, 381), num(b, pb(b, PU_ROOM) + 1);
        else text(b, 386);
        text(b, 382);
    } else if (c == 4) {
        text(b, 1788);
    } else {
        text(b, 383), text(b, 954 + c);
        if (several) text(b, 381), num(b, pb(b, PU_ROOM) + 1);
        text(b, 384);
    }
    title(b);
}

static void inventory_screen(Biing *b) { /* 0x26FA2 and 0x26BE4 */
    draw(b, IMG_PAD, 0, 0);
    draw(b, IMG_DOWN, 0, 0);
    draw(b, IMG_UP, 0, 0);
    draw(b, IMG_INFO, 0, 0);
    draw(b, IMG_THUMB, 0, 0);
    inventory_title(b);
    list(b);
    screen(b, SCR_INVENTORY);
}

static void inventory_open(Biing *b) { /* 0x26F75 */
    purchase_inventory_open(sim(b));
    inventory_screen(b);
}

static void inventory_open_furniture(Biing *b) { /* 0x26F7B: the same with the furniture list first (pb +0x26 = 0) */
    purchase_inventory_open(sim(b));
    purchase_toggle_kind(sim(b));
    inventory_screen(b);
}

/* the inventory buttons of the room screens the engine does not draw yet (the core rooms call rooms_inventory) */
static const struct {
    uint32_t stub, back;
    uint8_t cat, box;
} BUTTONS[] = {
    {0x26E30, 0x2EEE5, 4, 0},    {0x26EE3, 0x3649A, 7, 67},  {0x26F2C, 0x3677D, 8, 125}, {0x26D21, 0x31D44, 9, 0},
    {0x26C9A, 0x36A1E, 10, 0},   {0x26DA9, 0x31A16, 11, 71}, {0x26D43, 0x36A7B, 12, 0},  {0x26C78, 0x36AD9, 13, 0},
    {0x26D87, 0x3BD5E, 14, 0},   {0x26CDE, 0x31EE3, 15, 74}, {0x26EBC, 0x3BCCB, 16, 0},  {0x26D65, 0x3BE59, 17, 0},
    {0x26CBC, 0x3BDE0, 18, 0},
};

static int inventory_button(Biing *b, uint32_t r) {
    for (size_t i = 0; i < sizeof BUTTONS / sizeof BUTTONS[0]; i++) {
        if (BUTTONS[i].stub != r) continue;
        int refuse = purchase_inventory_button(sim(b), BUTTONS[i].cat, BUTTONS[i].back);
        if (refuse) refusal(b, PIC_BAD, refuse, BUTTONS[i].box);
        else inventory_open(b);
        return 1;
    }
    return 0;
}

static void toggle_kind(Biing *b) { /* 0x26A3A */
    purchase_toggle_kind(sim(b));
    inventory_title(b);
    list(b);
}

static void toggle_view(Biing *b) { /* 0x26A6A */
    if (!purchase_toggle_view(sim(b))) return;
    inventory_title(b);
    list(b);
}

static void move(Biing *b) { /* 0x3C3BC */
    int r = purchase_move(sim(b));
    switch (r) {
    case PU_MOVED: inventory_screen(b); return;
    case PU_BACK_ONLY: call(b, 0x1AE66); return; /* as the back button: the room screen at P+6 */
    case PU_RECOUNTED: return;
    case PU_NO_STORE_MOVE: refusal(b, PIC_NOT_YET, r, SCR_ROOM_BOX); return;
    case PU_NO_CLERK_MOVE: refusal(b, PIC_NO, r, SCR_ROOM_BOX); return;
    case PU_STORE_PACKED: refusal(b, PIC_BAD, r, SCR_ROOM_BOX); return;
    case PU_ROOM_FULL_EQUIPMENT: case PU_ROOM_FULL_FURNITURE: refusal(b, PIC_SORRY, r, SCR_INVENTORY_BOX); return;
    default: refusal(b, PIC_NONE, r, SCR_INVENTORY_BOX); return; /* 628 / 629 */
    }
}

static void info(Biing *b) { /* 0x25E58 */
    int c, eq = equipment_shown(b), id = purchase_info(sim(b), &c);
    if (id <= 0 || !purchase_item_valid(eq, id)) {
        inventory_screen(b);
        return;
    }
    draw(b, IMG_PAD, 0, 0);
    draw(b, IMG_THUMB, 0, 0);
    inventory_title(b);
    draw(b, IMG_ONE_ROW, 0, 0);
    row(b, eq, 0, id, c);
    clear(b);
    text(b, eq ? 1563 : 1564);
    properties(b, eq, id);
    text(b, 1569), num(b, item_u16(item(b, eq, id), 8) + 1), text(b, 1470);
    description(b);
    screen(b, SCR_INFO);
}

/* ---- 22 ---------------------------------------------------------------------------------------------- */

static void warehouse(Biing *b) { /* 0x2EEE5 */
    draw(b, IMG_WAREHOUSE, 0, 0);
    screen(b, SCR_WAREHOUSE);
    room_sound_view(sim(b), 4); /* 0x2EF01 / 0x2EF1F: the clerk's loop or the empty room */
}

static void warehouse_click(Biing *b) { /* the site map's 0x2EEC4 */
    if (rooms_site_click(sim(b), b->ui.flags, rooms_site_building(0x2EEC4))) warehouse(b);
}

/* The warehouse animation (docs/ui-sheets.md, section 3): on the animation tick pb+0x3C the next image of the spider
 * loop (32 words at 0x60FF), with r15 = 0 the next of the hand loop (16 words at 0x6147), and with a clerk and r3 = 0
 * (0x29738 on 0xE50B) on r1 = 0 the mouth table[r3], drawn only when it differs from the last one. Each image at its
 * IXY position and only with ANIMS on; positions and draws advance either way. Engine reading: the image at the
 * position is drawn, then the position advances (so the loops start with their first image). */
#define SPIDER_LOOP 0x60FF
#define HAND_LOOP 0x6147
#define MOUTH_TABLE 0xE50B
static void warehouse_frame(Biing *b) { /* 0x4A2AB */
    uint8_t *p = rooms_pb(sim(b));
    if (p[RB_TICK]) {
        biing_anim_list(b, SPIDER_LOOP, 32, &b->warehouse_spider);
        if (sim_bits(sim(b), 15) == 0) biing_anim_list(b, HAND_LOOP, 16, &b->warehouse_hand);
        if ((rooms_record(sim(b), 4, 0)[4] | rooms_record(sim(b), 4, 0)[5]) && sim_bits(sim(b), 3) == 0)
            biing_anim_pick(b, MOUTH_TABLE, &b->warehouse_mouth);
        p[RB_TICK] = 0;
    }
    room_sound_frame(sim(b), 0x4A2AB); /* the ambience pair with a clerk, every frame */
    p[0x56] = 0; /* 0x371E9 */
}

/* ---- dispatch ---------------------------------------------------------------------------------------- */

int purchase_routine(Biing *b, uint32_t r) {
    switch (r) {
    case 0x2535A: room_sound_frame(sim(b), r), rooms_pb(sim(b))[0x56] = 0; return 1; /* administration frame */
    case 0x3B332: purchase_screen(b, 1); return 1;
    case 0x3B3CB: purchase_screen(b, 0); return 1;
    case 0x3AF14: all_offers(b, 1); return 1;
    case 0x3B023: all_offers(b, 0); return 1;
    case 0x3936A: offer_back(b, 1); return 1;
    case 0x39382: offer_back(b, 0); return 1;
    case 0x3D274: offer_step(b, 1, -1); return 1;
    case 0x25388: offer_step(b, 0, -1); return 1;
    case 0x3C16A: offer_step(b, 1, 1); return 1;
    case 0x253E7: offer_step(b, 0, 1); return 1;
    case 0x2F606: buy(b, 1); return 1;
    case 0x391C8: buy(b, 0); return 1;
    case 0x4899D: sale_screen(b, 1); return 1;
    case 0x48A55: sale_screen(b, 0); return 1;
    case 0x486E2: sell(b); return 1;
    case 0x26405: purchase_page_up(sim(b)), list(b); return 1;
    case 0x263B1: page_down(b); return 1;
    case 0x26F75: inventory_open(b); return 1;
    case 0x26F7B: inventory_open_furniture(b); return 1;
    case 0x3AF1F: search(b, 1); return 1; /* the offer search with the filters P +0x54 / +0x55 as set (the advice jump) */
    case 0x3B02E: search(b, 0); return 1;
    case 0x26FA2: case 0x26BE4: inventory_screen(b); return 1;
    case 0x26A3A: toggle_kind(b); return 1;
    case 0x32A93: draw(b, equipment_shown(b) ? IMG_TAB_FURNITURE : IMG_TAB_EQUIPMENT, 0, 0); return 1;
    case 0x32AB6: draw(b, equipment_shown(b) ? IMG_TAB_EQUIPMENT : IMG_TAB_FURNITURE, 0, 0); return 1;
    case 0x26A2D: call(b, 0x1AE66); return 1; /* back: the room screen at P+6 */
    case 0x26A6A: toggle_view(b); return 1;
    case 0x32A4D: draw(b, pb(b, PU_VIEW) == PU_STORE_VIEW ? IMG_TAB_INTO_STORE : IMG_TAB_OUT_OF_STORE, 0, 0); return 1;
    case 0x32A70: draw(b, pb(b, PU_VIEW) == PU_STORE_VIEW ? IMG_TAB_OUT_OF_STORE : IMG_TAB_INTO_STORE, 0, 0); return 1;
    case 0x3C3BC: move(b); return 1;
    case 0x25E58: info(b); return 1;
    case 0x2EEC4: warehouse_click(b); return 1;
    case 0x2EEE5: warehouse(b); return 1;
    case 0x4A2AB: warehouse_frame(b); return 1;
    default: break;
    }
    if (r >= 0x26424 && r <= 0x2644E && (r - 0x26424) % 6 == 0) { /* select row k */
        purchase_select(sim(b), (int)(r - 0x26424) / 6);
        list(b);
        return 1;
    }
    if (inventory_button(b, r)) return 1;
    return cell(b, r);
}
