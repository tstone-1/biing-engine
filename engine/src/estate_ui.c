/* The estate agent's screens (docs/ui-estate.md): the office 6, the plot contracts 11/13/14/104, the unit
 * grids 3/4/105/107, the unit contracts 16/17 and 106/108, and the OK box 12. The money rules are in
 * estate.c; this file draws, builds the texts and moves between the screens. */
#include "room_sound.h"
#include "estate_ui.h"

#include "estate.h"

enum {
    IMG_AGENT = 87, IMG_CONTRACT = 148, IMG_CONTRACT_TEXT = 168, IMG_PLOT_ICON = 473, IMG_HOUSE_ICON = 474,
    IMG_FIST = 156, IMG_MONEY = 164, IMG_HOUSE_ARROWS = 160, IMG_GRID = 895, IMG_CELL_HOVER = 896,
    IMG_CELL_BLOCKED = 897,
    /* portraits */
    PIC_DONE = 509, PIC_REPORT = 511, PIC_NO_CASH = 513, PIC_NO_SLOT = 514, PIC_NONE = 515, PIC_SOLD = 516,
    PIC_CANCELLED = 517, PIC_NO_TYPE = 518,
};
enum {
    SCR_AGENT = 6, SCR_OK = 12, SCR_RENT_PLOTS = 11, SCR_BUY_PLOTS = 13, SCR_CANCEL_PLOTS = 14,
    SCR_SELL_PLOTS = 104, SCR_GRID_RENT = 3, SCR_GRID_BUY = 4, SCR_GRID_CANCEL = 105, SCR_GRID_SELL = 107,
    SCR_RENT_UNIT = 16, SCR_BUY_UNIT = 17, SCR_CANCEL_UNIT = 106, SCR_SELL_UNIT = 108,
};
#define CELL_TABLE 0xA777 /* 18 x (u16 room type, u16 category, u16 dx, u16 dy) */
#define TWO_CRS 0x55E6 /* the string the cancel and sell plot texts insert: CR CR, 0-terminated */
#define P_AMOUNT 0 /* player block: amount / category (u16) */
#define P_TYPE 2 /* room type on the unit screens (u16) */

static Sim *sim(Biing *b) { return &b->sim; }
static int pget(Biing *b, int off) {
    const uint8_t *p = b->world.block[BLK_PLAYER0] + off;
    return (int16_t)(p[0] | p[1] << 8);
}
static void pset(Biing *b, int off, int v) {
    uint8_t *p = b->world.block[BLK_PLAYER0] + off;
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void draw(Biing *b, int image, int dx, int dy) { dulog_draw(&b->ui, image, dx, dy); }
static void screen(Biing *b, int n) { dulog_set_screen(&b->ui, n); }

/* ---- text ------------------------------------------------------------------------------------- */

static void text(Biing *b, int entry) { biing_msg_entry(b, entry); }
static void num(Biing *b, long v) { msg_append_number(&b->msg, v); }
static void name(Biing *b, int type) {
    size_t n;
    const uint8_t *p = estate_room_name(sim(b), type, &n);
    msg_append(&b->msg, p, n);
}
/* "eine " for one, else the number */
static void count(Biing *b, int n) {
    if (n == 1) text(b, 312);
    else num(b, n);
}

/* Contract title (x 170, y 47, 240 wide) and body (x 183, y 90, 252 wide): font 1, centred, line spacing 1. */
static void render(Biing *b, int x, int y, int width) {
    TextLayout l = {&b->fonts[1], b->prog.charmap, x, y, width, 1, 5, 1};
    text_centred(&b->screen, &l, b->msg.buf, b->msg.len);
}
static void title(Biing *b, int entry) {
    msg_clear(&b->msg);
    text(b, entry);
    render(b, 170, 47, 240);
    msg_clear(&b->msg);
}
static void body(Biing *b) { render(b, 183, 90, 252); }

/* Message box over what is on screen (or over the estate agent's office), then the OK box 12. */
static void box(Biing *b, int over_agent, int picture) {
    if (over_agent) draw(b, IMG_AGENT, 0, 0);
    biing_message_box(b, picture);
    screen(b, SCR_OK);
}
static void refusal(Biing *b, int over_agent, int picture, int entry) {
    msg_clear(&b->msg);
    text(b, entry);
    box(b, over_agent, picture);
}

/* ---- 6: the estate agent and its two overviews ------------------------------------------------ */

static void agent(Biing *b) {
    /* player 0, font 1 with space width 5: the engine passes the font with every text */
    draw(b, IMG_AGENT, 0, 0);
    screen(b, SCR_AGENT);
    room_sound_view(&b->sim, 0); /* 0x2EEA7: the office loop 0xFC */
}

/* 0x37EB1 */
static void plot_report(Biing *b) {
    Sim *s = sim(b);
    int o = estate_owned(s), r = estate_rented(s), f = estate_free(s);
    if (o > 0) {
        text(b, 294), count(b, o), text(b, 309);
        if (r > 0) {
            text(b, 305);
            if (r == 1) text(b, 312), text(b, 303);
            else num(b, r), text(b, 304);
        } else {
            text(b, o == 1 ? 307 : 308);
        }
    } else if (r > 0) {
        text(b, 294);
        if (r == 1) text(b, 312), text(b, 300);
        else num(b, r), text(b, 301);
    } else {
        text(b, 302);
    }
    if (o || r) {
        if (f == 0) text(b, 295);
        else if (f == 1) text(b, 298), text(b, 312), text(b, 296);
        else text(b, 299), num(b, f), text(b, 297);
    }
    text(b, 306), num(b, biing_cash(b)), text(b, 1370);
}

static void plot_overview(Biing *b) { /* 0x21774 */
    msg_clear(&b->msg);
    text(b, 285);
    int none = estate_owned(sim(b)) + estate_rented(sim(b)) == 0;
    if (none) text(b, 284);
    else plot_report(b);
    box(b, 0, none ? PIC_NONE : PIC_REPORT);
}

static void building_overview(Biing *b) { /* 0x217D4 */
    int e, g;
    estate_unit_counts(sim(b), &e, &g);
    msg_clear(&b->msg);
    text(b, 286);
    if (e + g == 0) {
        text(b, 283);
        box(b, 0, PIC_NONE);
        return;
    }
    text(b, 287);
    if (e > 0 && g > 0) {
        if (e == 1) text(b, 289);
        else num(b, e), text(b, 288);
    }
    if (e > 0 && g == 0) {
        if (e == 1) text(b, 293);
        else num(b, e), text(b, 292);
    } else {
        if (g == 1) text(b, 291);
        else num(b, g), text(b, 290);
    }
    box(b, 0, PIC_REPORT);
}

/* ---- plot contracts 11, 13, 14, 104 ----------------------------------------------------------- */

static void plot_frame(Biing *b) {
    draw(b, IMG_CONTRACT, 0, 0);
    draw(b, IMG_PLOT_ICON, 0, 0);
    draw(b, IMG_FIST, 0, 0);
    draw(b, IMG_MONEY, 0, 0);
}

static void plural(Biing *b, int n) { /* "freie Grundstücksparzelle" + "n" */
    text(b, 1369);
    if (n != 1) text(b, 1366);
    text(b, 1365);
}

static void rent_plots_text(Biing *b) { /* 0x37DA1 */
    int n = pget(b, P_AMOUNT);
    title(b, 267);
    text(b, 265), num(b, 22), text(b, 1370);
    plot_report(b);
    text(b, 1367), num(b, n), plural(b, n);
    text(b, 1363), num(b, estate_plot_cost(0, n)), text(b, 1370);
    body(b);
}

static void buy_plots_text(Biing *b) { /* 0x38336 */
    int n = pget(b, P_AMOUNT);
    title(b, 266);
    text(b, 264), num(b, 300), text(b, 1370);
    plot_report(b);
    text(b, 1368), num(b, n), plural(b, n);
    text(b, 1364), num(b, estate_plot_cost(1, n)), text(b, 1370);
    body(b);
}

static void two_crs(Biing *b) {
    const uint8_t *p = program_table(&b->prog, TWO_CRS);
    size_t n = 0;
    while (p[n] && p[n] != '$' && n < 8) n++;
    msg_append(&b->msg, p, n);
}

static void free_line(Biing *b) {
    int f = estate_free(sim(b));
    if (f == 1) text(b, 298), text(b, 312), text(b, 296);
    else text(b, 299), num(b, f), text(b, 297);
}

static void cancel_plots_drawing(Biing *b) { /* 0x3845B */
    int n = pget(b, P_AMOUNT), r = estate_rented(sim(b));
    plot_frame(b);
    title(b, 268);
    text(b, 294);
    if (r == 1) text(b, 250);
    else num(b, r), text(b, 251);
    free_line(b);
    two_crs(b);
    text(b, 254);
    if (n < 2) text(b, 255);
    else num(b, n), text(b, 256);
    text(b, 257), num(b, 4 * n), text(b, 1370);
    body(b);
}

static void sell_plots_drawing(Biing *b) { /* 0x21F7E */
    int n = pget(b, P_AMOUNT), o = estate_owned(sim(b));
    plot_frame(b);
    title(b, 269);
    text(b, 294);
    if (o == 1) text(b, 252);
    else num(b, o), text(b, 253);
    free_line(b);
    two_crs(b);
    text(b, 258);
    if (n < 2) text(b, 259);
    else num(b, n), text(b, 260);
    text(b, 261), num(b, 286 * n), text(b, 1370);
    body(b);
}

static void open_plot_contract(Biing *b, int scr) {
    if (scr == SCR_CANCEL_PLOTS || scr == SCR_SELL_PLOTS) {
        int refuse = scr == SCR_CANCEL_PLOTS ? estate_cancel_plots_allowed(sim(b)) : estate_sell_plots_allowed(sim(b));
        if (refuse) {
            refusal(b, 0, PIC_NONE, refuse);
            return;
        }
    }
    pset(b, P_AMOUNT, 1);
    screen(b, scr);
    if (scr == SCR_CANCEL_PLOTS) cancel_plots_drawing(b);
    else if (scr == SCR_SELL_PLOTS) sell_plots_drawing(b);
    else {
        plot_frame(b);
        if (scr == SCR_RENT_PLOTS) rent_plots_text(b);
        else buy_plots_text(b);
    }
}

static void plot_amount(Biing *b, int more) {
    int scr = b->ui.current, n = pget(b, P_AMOUNT);
    int m = more ? estate_more(sim(b), scr, n) : estate_fewer(n);
    pset(b, P_AMOUNT, m);
    if (scr == SCR_RENT_PLOTS || scr == SCR_BUY_PLOTS) {
        if (!more && m == n) return;
        draw(b, IMG_CONTRACT_TEXT, 0, 0); /* "more" redraws even when the cash stopped it */
        if (scr == SCR_RENT_PLOTS) rent_plots_text(b);
        else buy_plots_text(b);
        return;
    }
    if (m == n) return;
    if (scr == SCR_CANCEL_PLOTS) cancel_plots_drawing(b);
    else sell_plots_drawing(b);
}

static void rent_plots(Biing *b) { /* 0x3807A */
    int n = pget(b, P_AMOUNT);
    if (estate_rent_plots(sim(b), n)) {
        refusal(b, 0, PIC_NO_CASH, ESTATE_NO_CASH_RENT);
        return;
    }
    msg_clear(&b->msg);
    text(b, 306);
    if (program_english(&b->prog)) msg_append(&b->msg, (const uint8_t *)"rented ", 7); /* docs/editions.md 7.3, F */
    if (n == 1) text(b, 312), text(b, 311);
    else num(b, n), text(b, 310);
    num(b, estate_plot_cost(0, n)), text(b, 1370);
    box(b, 1, PIC_DONE);
}

static void buy_plots(Biing *b) { /* 0x38135 */
    int n = pget(b, P_AMOUNT), c;
    if (estate_buy_plots(sim(b), n, &c)) {
        refusal(b, 0, PIC_NO_CASH, ESTATE_NO_CASH_BUY);
        return;
    }
    msg_clear(&b->msg);
    text(b, 306);
    if (program_english(&b->prog)) msg_append(&b->msg, (const uint8_t *)"bought ", 7); /* docs/editions.md 7.3, F */
    int fresh = n - c;
    if (c > 0 && fresh > 0) {
        if (c == 1) text(b, 313);
        else num(b, c), text(b, 314);
        if (fresh == 1) text(b, 312), text(b, 320);
        else num(b, fresh), text(b, 316);
    } else if (c == n) {
        if (n == 1) text(b, 312), text(b, 317);
        else num(b, n), text(b, 315);
    } else {
        if (n == 1) text(b, 312), text(b, 320);
        else num(b, n), text(b, 316);
    }
    num(b, estate_plot_cost(1, n)), text(b, 1370);
    box(b, 1, PIC_DONE);
}

static void cancel_plots(Biing *b) { /* 0x4D816 */
    int n = pget(b, P_AMOUNT);
    estate_cancel_plots(sim(b), n);
    msg_clear(&b->msg);
    text(b, 3498);
    if (n == 1) text(b, 3499);
    else num(b, n), text(b, 3696);
    text(b, 3697), num(b, 4 * n), text(b, 1370);
    box(b, 0, PIC_SOLD);
}

static void sell_plots(Biing *b) { /* 0x4DE06 */
    int n = pget(b, P_AMOUNT);
    estate_sell_plots(sim(b), n);
    msg_clear(&b->msg);
    text(b, 3698);
    if (n == 1) text(b, 3699);
    else num(b, n), text(b, 3789);
    text(b, 3790), num(b, 286 * n), text(b, 1370);
    box(b, 0, PIC_SOLD);
}

/* ---- the unit grids 3, 4, 105, 107 ------------------------------------------------------------ */

typedef struct {
    int type, dx, dy;
} Cell;

static Cell cell(Biing *b, int k) {
    uint32_t at = CELL_TABLE + 8u * (uint32_t)(k - 1);
    return (Cell){program_u16(&b->prog, at), (int16_t)program_u16(&b->prog, at + 4),
                  (int16_t)program_u16(&b->prog, at + 6)};
}

static int build_blocked(Biing *b, int k) { /* 0x1FE90 with P+0 = k, P+2 = the cell's type */
    Cell c = cell(b, k);
    pset(b, P_AMOUNT, k);
    pset(b, P_TYPE, c.type);
    return estate_build_blocked(sim(b), k, c.type);
}

static void build_grid(Biing *b) { /* 0x221C4 */
    draw(b, IMG_GRID, 0, 0);
    for (int k = 1; k <= ESTATE_CELLS; k++)
        if (build_blocked(b, k)) draw(b, IMG_CELL_BLOCKED, cell(b, k).dx, cell(b, k).dy);
}

static int removal_rented(Biing *b) { return b->ui.current == SCR_GRID_CANCEL; }

static int removal_open(Biing *b, int k) {
    return estate_removal_slot(sim(b), k, removal_rented(b), NULL) >= 0 && estate_counts(sim(b))[k] > 0;
}

static void removal_grid(Biing *b) { /* 0x21E65 */
    draw(b, IMG_GRID, 0, 0);
    for (int k = 1; k <= ESTATE_CELLS; k++)
        if (!removal_open(b, k)) draw(b, IMG_CELL_BLOCKED, cell(b, k).dx, cell(b, k).dy);
}

static int buy_cell_open(Biing *b, int k) {
    return !estate_build_blocked(sim(b), k, cell(b, k).type) || estate_rented_slot_of_category(sim(b), k) >= 0;
}

/* Hover and leave of a cell; the highlight only where the cell can be chosen. */
static int cell_open(Biing *b, int k) {
    switch (b->ui.current) {
    case SCR_GRID_RENT: return !estate_build_blocked(sim(b), k, cell(b, k).type);
    case SCR_GRID_BUY: return buy_cell_open(b, k);
    default: return removal_open(b, k);
    }
}

static void redraw_grid(Biing *b) {
    if (b->ui.current == SCR_GRID_RENT || b->ui.current == SCR_GRID_BUY) build_grid(b);
    else removal_grid(b);
}

static void cell_enter(Biing *b, int k) {
    if (b->ui.current == SCR_GRID_CANCEL || b->ui.current == SCR_GRID_SELL) removal_grid(b);
    if (cell_open(b, k)) draw(b, IMG_CELL_HOVER, cell(b, k).dx, cell(b, k).dy);
}

static void cell_leave(Biing *b, int k) {
    if (cell_open(b, k)) redraw_grid(b);
}

static void open_grid(Biing *b, int scr) {
    screen(b, scr); /* set first: the removal checks ask which grid this is (P+4 in the original) */
    if (scr == SCR_GRID_RENT || scr == SCR_GRID_BUY) build_grid(b);
    else removal_grid(b);
}

/* ---- unit contracts 16 and 17 ----------------------------------------------------------------- */

static void unit_frame(Biing *b) {
    draw(b, IMG_CONTRACT, 0, 0);
    draw(b, IMG_HOUSE_ICON, 0, 0);
    draw(b, IMG_FIST, 0, 0);
    draw(b, IMG_HOUSE_ARROWS, 0, 0);
}

static void unit_text(Biing *b, int buy) {
    Sim *s = sim(b);
    int t = pget(b, P_TYPE), plots = estate_unit_plots(s, t);
    title(b, buy ? 587 : 586);
    text(b, buy ? 544 : 543), name(b, t), text(b, buy ? 546 : 545);
    num(b, buy ? estate_unit_price(s, t) : estate_unit_rent(s, t)), text(b, 1370);
    text(b, 588 + t), text(b, 585);
    if (plots == 1) text(b, 584);
    else num(b, plots), text(b, 583);
    body(b);
}

static int no_plots(Biing *b) {
    if (estate_owned(sim(b)) || estate_rented(sim(b))) return 0;
    refusal(b, 1, PIC_NONE, ESTATE_NO_PLOTS);
    return 1;
}

static void open_rent_unit(Biing *b) { /* 0x39587 */
    if (no_plots(b)) return;
    int t = pget(b, P_TYPE), found = estate_type_search(sim(b), &t, pget(b, P_AMOUNT));
    pset(b, P_TYPE, t);
    if (!found) {
        refusal(b, 1, PIC_NO_TYPE, ESTATE_NO_TYPE);
        return;
    }
    draw(b, IMG_AGENT, 0, 0);
    unit_frame(b);
    unit_text(b, 0);
    screen(b, SCR_RENT_UNIT);
}

static void open_buy_unit(Biing *b, int buy_out) { /* 0x21901; buy_out is [0x5BE1] */
    if (no_plots(b)) return;
    unit_frame(b);
    if (!buy_out) {
        int k = pget(b, P_AMOUNT), t0 = pget(b, P_TYPE), t = t0;
        if (estate_type_search(sim(b), &t, k)) {
            pset(b, P_TYPE, t);
        } else if (estate_rented_slot_of_category(sim(b), k) < 0) {
            refusal(b, 1, PIC_NO_TYPE, ESTATE_NO_TYPE);
            return;
        } /* else: restored P+0/P+2, opened anyway */
    }
    unit_text(b, 1);
    screen(b, SCR_BUY_UNIT);
}

static void step_type(Biing *b, int step) { /* the arrows of 16/17 */
    int t = pget(b, P_TYPE) + step;
    if (t < 0) t = ESTATE_TYPES - 1;
    if (t > ESTATE_TYPES - 1) t = 0;
    estate_type_search(sim(b), &t, step);
    pset(b, P_TYPE, t);
    draw(b, IMG_CONTRACT_TEXT, 0, 0);
    unit_text(b, b->ui.current == SCR_BUY_UNIT);
}

static void unit_refusal(Biing *b, int entry) {
    refusal(b, 1, entry == ESTATE_NO_SLOT ? PIC_NO_SLOT : PIC_NONE, entry);
}

static void rent_unit(Biing *b) { /* 0x1DD55 */
    int t = pget(b, P_TYPE), refuse = estate_rent_unit(sim(b), t);
    if (refuse) {
        unit_refusal(b, refuse);
        return;
    }
    msg_clear(&b->msg);
    text(b, 1627), name(b, t), text(b, 1628), num(b, estate_unit_rent(sim(b), t)), text(b, 1370);
    box(b, 1, PIC_DONE);
}

static void buy_unit(Biing *b) { /* 0x1CAAC */
    int t = pget(b, P_TYPE), bought_out, price;
    int refuse = estate_buy_unit(sim(b), t, &bought_out, &price);
    if (refuse) {
        unit_refusal(b, refuse);
        return;
    }
    msg_clear(&b->msg);
    text(b, bought_out ? 1626 : 1627), name(b, t), text(b, 1629), num(b, price), text(b, 1370);
    box(b, 1, PIC_DONE);
}

/* ---- grid clicks, and the contracts 106 and 108 ----------------------------------------------- */

static void rent_cell(Biing *b, int k) { /* 0x67DA2 + 4 (k - 1) */
    if (build_blocked(b, k)) return;
    open_rent_unit(b);
}

static void buy_cell(Biing *b, int k) { /* 0x67F31 + 4 (k - 1) */
    if (!build_blocked(b, k)) {
        open_buy_unit(b, 0);
        return;
    }
    int i = estate_rented_slot_of_category(sim(b), k);
    if (i < 0) return;
    pset(b, P_TYPE, estate_slots(sim(b))[2 * i]); /* the buy-out of a rented unit */
    open_buy_unit(b, 1);
}

static void removal_cell(Biing *b, int k, int rented) { /* 0x3CBF9 / 0x3CF75 + 4 (k - 1) */
    Sim *s = sim(b);
    int r, i = estate_removal_pick(s, k, rented, &r);
    if (i < 0) return;
    b->estate_slot = i;
    pset(b, P_AMOUNT, k - 1);
    draw(b, IMG_CONTRACT, 0, 0);
    draw(b, IMG_HOUSE_ICON, 0, 0);
    draw(b, IMG_FIST, 0, 0);
    title(b, rented ? 271 : 270);
    if (rented) {
        int rent = estate_unit_rent(s, r);
        text(b, 280), name(b, r), text(b, 281), num(b, rent), text(b, 282), num(b, 2 * rent + 8), text(b, 1370);
        text(b, 279);
    } else {
        int base = estate_unit_base(s, r);
        text(b, 274), name(b, r), text(b, 273), num(b, base), text(b, 272), num(b, base - base / 16 - 8);
        text(b, 1370);
    }
    body(b);
    screen(b, rented ? SCR_CANCEL_UNIT : SCR_SELL_UNIT);
}

static void removal_back(Biing *b) { /* 0x48465: no check that a decrement happened */
    estate_removal_back(sim(b), pget(b, P_AMOUNT));
    agent(b);
}

static void removal_confirm(Biing *b, int rented) { /* 0x4F2D9 / 0x4847D */
    int t = estate_slots(sim(b))[2 * b->estate_slot];
    int amount = rented ? estate_cancel_unit(sim(b), b->estate_slot) : estate_sell_unit(sim(b), b->estate_slot);
    msg_clear(&b->msg);
    text(b, rented ? 277 : 275), name(b, t), text(b, rented ? 278 : 276), num(b, amount), text(b, 1370);
    box(b, 1, rented ? PIC_CANCELLED : PIC_SOLD);
}

/* ---- dispatch --------------------------------------------------------------------------------- */

/* k = 1-18 if r is one of the 18 per-cell entries starting at base (4 bytes apart), else 0 */
static int cell_of(uint32_t r, uint32_t base) {
    if (r < base || r > base + 4u * (ESTATE_CELLS - 1) || (r - base) % 4) return 0;
    return (int)((r - base) / 4) + 1;
}

int estate_routine(Biing *b, uint32_t r) {
    int k;
    switch (r) {
    case 0x2EE72: agent(b); return 1;
    case 0x3BE9E: room_sound_frame(&b->sim, r); return 1; /* per frame: the room ambience */
    case 0x21774: plot_overview(b); return 1;
    case 0x217D4: building_overview(b); return 1;
    case 0x37D5F: open_plot_contract(b, SCR_RENT_PLOTS); return 1;
    case 0x38419: open_plot_contract(b, SCR_BUY_PLOTS); return 1;
    case 0x216C4: open_plot_contract(b, SCR_CANCEL_PLOTS); return 1;
    case 0x2168B: open_plot_contract(b, SCR_SELL_PLOTS); return 1;
    case 0x382B8: case 0x4D8A3: case 0x4DEA3: plot_amount(b, 1); return 1;
    case 0x38311: case 0x4D8CF: case 0x4DECF: plot_amount(b, 0); return 1;
    case 0x3807A: rent_plots(b); return 1;
    case 0x38135: buy_plots(b); return 1;
    case 0x4D816: cancel_plots(b); return 1;
    case 0x4DE06: sell_plots(b); return 1;
    case 0x2219A: open_grid(b, SCR_GRID_RENT); return 1;
    case 0x221AF: open_grid(b, SCR_GRID_BUY); return 1;
    case 0x2172D: open_grid(b, SCR_GRID_CANCEL); return 1;
    case 0x2174B: open_grid(b, SCR_GRID_SELL); return 1;
    case 0x20107: draw(b, IMG_CELL_HOVER, 264, 272); return 1; /* "ins Büro" */
    case 0x20125: draw(b, IMG_CELL_HOVER, 176, 272); return 1; /* "zum Makler" */
    case 0x1FEE9: build_grid(b); return 1;
    case 0x21769: removal_grid(b); return 1;
    case 0x220F6: case 0x1CBF8: step_type(b, -1); return 1;
    case 0x22110: case 0x1CBE3: step_type(b, 1); return 1;
    case 0x1DD55: rent_unit(b); return 1;
    case 0x1CAAC: buy_unit(b); return 1;
    case 0x48465: removal_back(b); return 1;
    case 0x4F2D9: removal_confirm(b, 1); return 1;
    case 0x4847D: removal_confirm(b, 0); return 1;
    default: break;
    }
    if ((k = cell_of(r, 0x67DA2))) rent_cell(b, k);
    else if ((k = cell_of(r, 0x67F31))) buy_cell(b, k);
    else if ((k = cell_of(r, 0x3CBF9))) removal_cell(b, k, 1);
    else if ((k = cell_of(r, 0x3CF75))) removal_cell(b, k, 0);
    else if ((k = cell_of(r, 0x67E21)) || (k = cell_of(r, 0x68003)) || (k = cell_of(r, 0x3CE57))) cell_enter(b, k);
    else if ((k = cell_of(r, 0x67EB2)) || (k = cell_of(r, 0x680D2)) || (k = cell_of(r, 0x3CEF9))) cell_leave(b, k);
    else return 0;
    return 1;
}
