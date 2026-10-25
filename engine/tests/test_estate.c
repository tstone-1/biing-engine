/* The estate agent's transactions (docs/ui-estate.md). Synthetic world and program image (see AGENTS.md); the
 * catalog values are chosen so that the results are the oracle's numbers in the spec ("Checked with the
 * oracle"), e.g. one plot rented with 30 Lümmel leaves -14. */
#include <stdio.h>
#include <string.h>

#include "fixes.h"
#include "estate.h"
#include "sim_internal.h"

static int failures;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            failures++;                                                            \
        }                                                                          \
    } while (0)

static World g_world;
static Program g_prog;
static uint8_t g_image[0x10000];
static Sim g_sim;
static Sim *s = &g_sim;

/* Room types of the synthetic catalog: name, category, rent, base price, plots, prerequisite, maximum. */
enum { GRUND = 0, AUFNAHME = 1, AUFNAHME_LUX = 2, WARTE = 3, WARTE_LUX = 4, LAGER_S = 5, LAGER = 6, LAGER_L = 7,
       KRANKEN = 11, NEURO = 17, NEURO_K = 32 };
static void type(int t, int cat, int rent, int base, int plots, int pre, int max) {
    uint8_t *c = g_image + CATALOG + t * CATALOG_SIZE;
    memset(c, 0, CATALOG_SIZE);
    snprintf((char *)c, 30, "Typ %d$", t);
    put16(c + CATALOG_RENT, rent);
    put16(c + CATALOG_PRICE, base);
    c[CATALOG_PLOTS] = (uint8_t)plots;
    c[0x27] = (uint8_t)pre;
    c[0x28] = (uint8_t)max;
    c[CATALOG_CATEGORY] = (uint8_t)cat;
}

static uint8_t *h(void) { return world_hospital(&g_world, 0); }
static int32_t cash(void) { return world_cash(&g_world, 0); }
static uint8_t *slot(int i) { return g_world.block[BLK_ROOM_SLOTS] + 2 * i; }
static uint8_t *counts(void) { return g_world.block[BLK_ROOM_COUNTS]; }
static void plots(int owned, int rented, int built) {
    put16(h() + H_PLOTS_OWNED, owned);
    put16(h() + H_PLOTS_RENTED, rented);
    put16(h() + H_PLOTS_BUILT, built);
}

static void fresh(int32_t money) {
    world_free(&g_world);
    world_alloc(&g_world);
    memset(g_world.block[BLK_ROOM_SLOTS], 0xFF, g_world.size[BLK_ROOM_SLOTS]);
    memset(g_image, 0, sizeof g_image);
    /* room descriptions: furniture 0x20, equipment 0x28, conditions 0x30/0x34, type byte 0x38; no slots */
    for (int cat = 0; cat < 19; cat++) {
        uint8_t *d = g_image + 0x4758 + 14 * cat;
        put16(d, 0x20), put16(d + 4, 0x28), put16(d + 8, 0x30), put16(d + 10, 0x34), put16(d + 12, 0x38);
    }
    for (int t = 0; t < ESTATE_TYPES; t++) type(t, 5, 10, 100, 1, 3, 10); /* default: needs a waiting room */
    type(GRUND, 1, 50, 500, 3, 0xFF, 1);
    type(AUFNAHME, 2, 47, 380, 1, 1, 1);
    type(AUFNAHME_LUX, 2, 70, 600, 1, 1, 1);
    type(WARTE, 3, 40, 430, 1, 2, 10);
    type(WARTE_LUX, 3, 45, 500, 1, 2, 10);
    type(LAGER_S, 4, 40, 400, 5, 1, 1);
    type(LAGER, 4, 60, 600, 5, 1, 1);
    type(LAGER_L, 4, 80, 800, 5, 1, 1);
    type(NEURO, 15, 30, 300, 1, 3, 10);
    type(NEURO_K, 16, 30, 300, 1, 15, 10);
    g_prog.image.data = g_image;
    g_prog.image.size = sizeof g_image;
    sim_init(&g_sim, &g_world, &g_prog, 3);
    world_set_cash(&g_world, 0, money);
}

/* ---- plots ------------------------------------------------------------------------------------- */

static void test_rent_plots(void) {
    fresh(30);
    CHECK(estate_rent_plots(s, 1) == 0); /* 22 <= 30 passes the check, 44 is charged */
    CHECK(cash() == -14 && estate_rented(s) == 1);
    CHECK(get32(h() + 0x9E) == 0 && get32(h() + 0xA2) == 0); /* nothing booked */
    fresh(21);
    CHECK(estate_rent_plots(s, 1) == ESTATE_NO_CASH_RENT && cash() == 21 && estate_rented(s) == 0);
    CHECK(estate_rent_plots(s, 0) == ESTATE_NO_CASH_RENT);
    fresh(984);
    int n = 1;
    for (int i = 0; i < 40; i++) n = estate_more(s, 11, n);
    CHECK(n == 22); /* 22 x 44 = 968; 23 would be 1,012 */
    CHECK(estate_rent_plots(s, 21) == 0 && cash() == 984 - 924 && estate_rented(s) == 21);
}

static void test_buy_plots(void) {
    fresh(984);
    int n = 1;
    for (int i = 0; i < 10; i++) n = estate_more(s, 13, n);
    CHECK(n == 2); /* 750; three would be 1,125 */
    CHECK(estate_fewer(n) == 1 && estate_fewer(1) == 1);
    fresh(2000);
    plots(0, 2, 0);
    int c = -1;
    CHECK(estate_buy_plots(s, 3, &c) == 0 && c == 2);
    CHECK(estate_owned(s) == 3 && estate_rented(s) == 0 && cash() == 2000 - 1125);
    CHECK(get32(h() + 0x14E) == 1125 && get32(h() + 0x152) == 1125);
    fresh(2000);
    plots(0, 3, 0);
    CHECK(estate_buy_plots(s, 1, &c) == 0 && c == 1 && estate_rented(s) == 2 && estate_owned(s) == 1);
    fresh(374);
    CHECK(estate_buy_plots(s, 1, &c) == ESTATE_NO_CASH_BUY && cash() == 374 && estate_owned(s) == 0);
}

static void test_cancel_plots(void) {
    fresh(984);
    CHECK(estate_cancel_plots_allowed(s) == ESTATE_NOTHING_TO_CANCEL);
    plots(1, 0, 0);
    CHECK(estate_cancel_plots_allowed(s) == ESTATE_NOTHING_TO_CANCEL); /* owned only */
    plots(1, 3, 2);
    CHECK(estate_cancel_plots_allowed(s) == 0);
    int n = 1;
    for (int i = 0; i < 5; i++) n = estate_more(s, 14, n);
    CHECK(n == 2); /* free = 2 */
    estate_cancel_plots(s, n);
    CHECK(estate_rented(s) == 1 && cash() == 984 - 8);
    plots(5, 2, 0); /* free 7: now the rented plots are the limit */
    n = 1;
    for (int i = 0; i < 5; i++) n = estate_more(s, 14, n);
    CHECK(n == 2);
    fresh(0);
    plots(0, 2, 2);
    CHECK(estate_cancel_plots_allowed(s) == ESTATE_NOTHING_TO_CANCEL); /* all built on */
    plots(0, 2, 0);
    estate_cancel_plots(s, 1);
    CHECK(cash() == -4); /* no cash check */
}

static void test_sell_plots(void) {
    fresh(984);
    plots(3, 1, 1);
    CHECK(estate_sell_plots_allowed(s) == 0);
    int n = 1;
    for (int i = 0; i < 5; i++) n = estate_more(s, 104, n);
    CHECK(n == 3);
    estate_sell_plots(s, 3);
    CHECK(estate_owned(s) == 0 && cash() == 984 + 858 && get32(h() + 0x152) == 0);
    plots(0, 2, 0);
    CHECK(estate_sell_plots_allowed(s) == ESTATE_NOTHING_TO_SELL);
}

/* ---- units ------------------------------------------------------------------------------------- */

static void test_build_check(void) {
    fresh(0);
    CHECK(!estate_build_blocked(s, 1, GRUND));
    CHECK(estate_build_blocked(s, 2, AUFNAHME)); /* needs the Grundeinheit */
    counts()[1] = 1;
    CHECK(estate_build_blocked(s, 1, GRUND)); /* maximum one */
    CHECK(!estate_build_blocked(s, 2, AUFNAHME));
    counts()[2] = 1;
    counts()[3] = 9;
    CHECK(!estate_build_blocked(s, 3, WARTE));
    counts()[3] = 10;
    CHECK(estate_build_blocked(s, 3, WARTE));
    type(WARTE, 3, 40, 430, 1, 2, 0);
    counts()[3] = 0;
    CHECK(!estate_build_blocked(s, 3, WARTE));
    counts()[3] = 1;
    CHECK(estate_build_blocked(s, 3, WARTE)); /* maximum 0 means one */
}

static void test_type_search(void) {
    fresh(1000);
    plots(0, 10, 0);
    CHECK(estate_rent_unit(s, GRUND) == 0);
    /* "next" from the Aufnahme: Standard, Luxus, then the three Lager (the waiting rooms need an Aufnahme) */
    int seen[5], t = AUFNAHME;
    for (int i = 0; i < 5; i++) {
        estate_type_search(s, &t, 1);
        seen[i] = t;
        t = t + 1 > 37 ? 0 : t + 1;
    }
    CHECK(seen[0] == AUFNAHME && seen[1] == AUFNAHME_LUX && seen[2] == LAGER_S && seen[3] == LAGER &&
          seen[4] == LAGER_L);
    /* the wrap goes to 0, not by the step: from 36 with step 5 the next try is type 0, not 3 (41 - 38) */
    fresh(0);
    type(3, 7, 10, 100, 1, 0xFF, 10);
    t = 36;
    CHECK(estate_type_search(s, &t, 5) && t == GRUND);
    /* a category above its maximum still passes: the search tests "differs", not "below" */
    fresh(0);
    slot(0)[0] = GRUND, slot(0)[1] = 1;
    counts()[1] = 1, counts()[2] = 2;
    t = AUFNAHME;
    CHECK(estate_type_search(s, &t, 1) && t == AUFNAHME);
    /* nothing buildable after 300 tries */
    counts()[1] = 1;
    slot(0)[0] = GRUND, slot(0)[1] = 0;
    for (int k = 0; k < ESTATE_TYPES; k++) type(k, 5, 10, 100, 1, 9, 10);
    t = 4;
    CHECK(!estate_type_search(s, &t, 1));
}

static void test_rent_units(void) {
    fresh(984);
    CHECK(estate_rent_unit(s, GRUND) == ESTATE_TOO_FEW_PLOTS); /* no plots */
    plots(0, 2, 0);
    CHECK(estate_rent_unit(s, GRUND) == ESTATE_TOO_FEW_PLOTS && cash() == 984); /* 2 plots for 3 */
    plots(0, 10, 0);
    CHECK(estate_rent_unit(s, GRUND) == 0);
    CHECK(cash() == 934 && get32(h() + 0x96) == 50 && get32(h() + 0x9A) == 50);
    CHECK(get16(h() + H_PLOTS_BUILT) == 3 && slot(0)[0] == GRUND && slot(0)[1] == 1 && counts()[1] == 1);
    CHECK(estate_rent_unit(s, AUFNAHME) == 0 && cash() == 887 && counts()[2] == 1);
    CHECK(sim_room_record(s, 0, 2, 0)[0x38] == AUFNAHME); /* the room was created with its type */
    world_set_cash(&g_world, 0, 39);
    CHECK(estate_rent_unit(s, WARTE) == ESTATE_TOO_EXPENSIVE && cash() == 39 && counts()[3] == 0);
    world_set_cash(&g_world, 0, 40);
    CHECK(estate_rent_unit(s, WARTE) == 0 && cash() == 0);
    for (int i = 0; i < 200; i++)
        if (slot(i)[0] == 0xFF) slot(i)[0] = 0x0F, slot(i)[1] = 0;
    world_set_cash(&g_world, 0, 1000);
    CHECK(estate_rent_unit(s, WARTE) == ESTATE_NO_SLOT);
}

static void test_buy_units(void) {
    fresh(5000);
    plots(0, 20, 0);
    CHECK(estate_rent_unit(s, GRUND) == 0 && estate_rent_unit(s, AUFNAHME) == 0);
    int out, price;
    int32_t before = cash();
    CHECK(estate_buy_unit(s, WARTE, &out, &price) == 0 && !out && price == 537);
    CHECK(cash() == before - 537 && get32(h() + 0x14E) == 537 && get32(h() + 0x152) == 537);
    CHECK(slot(2)[0] == WARTE && slot(2)[1] == 0 && counts()[3] == 1 && get16(h() + H_PLOTS_BUILT) == 5);
    /* the buy-out of a rented unit: the price and booking, the rented byte 0, nothing else */
    CHECK(estate_rent_unit(s, LAGER) == 0 && counts()[4] == 1);
    before = cash();
    int built = get16(h() + H_PLOTS_BUILT);
    CHECK(estate_buy_unit(s, LAGER, &out, &price) == 0 && out && price == 750);
    CHECK(cash() == before - 750 && slot(3)[0] == LAGER && slot(3)[1] == 0);
    CHECK(counts()[4] == 1 && get16(h() + H_PLOTS_BUILT) == built && get32(h() + 0x96) == 50 + 47 + 60);
    world_set_cash(&g_world, 0, 536);
    CHECK(estate_buy_unit(s, WARTE, &out, &price) == ESTATE_TOO_EXPENSIVE && cash() == 536);
    plots(0, 20, 20);
    world_set_cash(&g_world, 0, 5000);
    CHECK(estate_buy_unit(s, WARTE, &out, &price) == ESTATE_TOO_FEW_PLOTS);
    /* a rented unit is bought out even without free plots */
    CHECK(estate_rent_unit(s, AUFNAHME_LUX) == ESTATE_TOO_FEW_PLOTS);
    slot(4)[0] = AUFNAHME_LUX, slot(4)[1] = 1;
    world_set_cash(&g_world, 0, 1000);
    CHECK(estate_buy_unit(s, AUFNAHME_LUX, &out, &price) == 0 && out && cash() == 1000 - 750);
    world_set_cash(&g_world, 0, 100);
    slot(4)[1] = 1;
    CHECK(estate_buy_unit(s, AUFNAHME_LUX, &out, &price) == ESTATE_TOO_EXPENSIVE && slot(4)[1] == 1);
}

/* Grundeinheit and Aufnahme rented, a Wartezimmer bought, a Lager rented, a second Wartezimmer rented. */
static void built_world(void) {
    fresh(5000);
    plots(0, 20, 0);
    int out, price;
    CHECK(estate_rent_unit(s, GRUND) == 0 && estate_rent_unit(s, AUFNAHME) == 0);
    CHECK(estate_buy_unit(s, WARTE, &out, &price) == 0 && estate_rent_unit(s, LAGER) == 0);
    CHECK(estate_rent_unit(s, WARTE) == 0 && counts()[3] == 2);
}

static void test_cancel_unit(void) {
    built_world();
    int t;
    CHECK(estate_removal_slot(s, 1, 1, &t) < 0); /* the Grundeinheit never goes */
    uint8_t *lager = sim_room_record(s, 0, 4, 0);
    put16(lager + 4, 7); /* the clerk */
    CHECK(estate_removal_pick(s, 4, 1, &t) < 0 && counts()[4] == 1);
    put16(lager + 4, 0);
    sim_room_record(s, 0, 13, 0)[5] = 1; /* the kitchen's byte +5 */
    CHECK(estate_removal_slot(s, 4, 1, &t) < 0);
    sim_room_record(s, 0, 13, 0)[5] = 0;
    CHECK(estate_removal_slot(s, 4, 0, &t) < 0); /* the Lager is rented: not for sale */
    /* the count goes down on the click, "back" restores it, a second click and the confirm */
    int i = estate_removal_pick(s, 4, 1, &t);
    CHECK(i == 3 && t == LAGER && counts()[4] == 0);
    estate_removal_back(s, 4 - 1);
    CHECK(counts()[4] == 1);
    i = estate_removal_pick(s, 4, 1, &t);
    int32_t before = cash();
    int built = get16(h() + H_PLOTS_BUILT);
    CHECK(estate_cancel_unit(s, i) == 128); /* 2 x 60 + 8 */
    CHECK(cash() == before - 128 && get16(h() + H_PLOTS_BUILT) == built - 5);
    CHECK(slot(3)[0] == 0xFF && slot(3)[1] == 1 && counts()[4] == 0); /* FF 01 */
    /* the last Wartezimmer record is the rented unit (slot 4), but the first slot of its type (slot 2) is
     * owned: the cancel cell is blocked */
    i = estate_removal_pick(s, 3, 1, &t);
    CHECK(i < 0 && counts()[3] == 2);
}

static void test_sell_unit(void) {
    built_world();
    int t;
    /* the last Wartezimmer record is the rented one, the first slot of its type the owned one */
    CHECK(estate_removal_slot(s, 3, 1, &t) < 0 && estate_removal_slot(s, 3, 0, &t) == 2);
    put16(sim_room_record(s, 0, 3, 1) + 0x0C, 2); /* occupied seats */
    CHECK(estate_removal_slot(s, 3, 0, &t) < 0);
    put16(sim_room_record(s, 0, 3, 1) + 0x0C, 0);
    put16(sim_room_record(s, 0, 2, 0) + 8, 5); /* the reception is staffed */
    CHECK(estate_removal_slot(s, 3, 0, &t) < 0);
    put16(sim_room_record(s, 0, 2, 0) + 8, 0);
    int i = estate_removal_pick(s, 3, 0, &t);
    int32_t before = cash(), invest = get32(h() + 0x152);
    CHECK(i == 2 && counts()[3] == 1);
    CHECK(estate_sell_unit(s, i) == 396); /* 430 - 26 - 8 */
    CHECK(cash() == before + 396 && get32(h() + 0x152) == invest);
    CHECK(slot(2)[0] == 0xFF && slot(2)[1] == 0); /* FF 00 */
    /* the remaining Wartezimmer is rented: not for sale, but it can be cancelled */
    CHECK(estate_removal_slot(s, 3, 0, &t) < 0);
    i = estate_removal_pick(s, 3, 1, &t);
    CHECK(i == 4 && estate_cancel_unit(s, i) == 88);
}

static void test_removal_rules(void) {
    built_world();
    int t;
    CHECK(estate_removal_slot(s, 2, 1, &t) == 1); /* the Aufnahme may go */
    counts()[5] = 1;
    CHECK(estate_removal_slot(s, 2, 1, &t) < 0); /* not with a treatment room */
    counts()[5] = 0;
    /* the last waiting room may not go while a department needs it */
    int i = estate_removal_pick(s, 3, 0, &t);
    CHECK(i == 2);
    estate_sell_unit(s, i);
    counts()[15] = 1;
    CHECK(estate_removal_slot(s, 3, 1, &t) < 0);
    counts()[15] = 0;
    CHECK(estate_removal_slot(s, 3, 1, &t) == 4);
    /* the Neuro-Krankenzimmer needs every neurology record idle */
    fresh(5000);
    plots(0, 20, 0);
    counts()[1] = counts()[2] = counts()[3] = 1;
    CHECK(estate_rent_unit(s, NEURO) == 0 && estate_rent_unit(s, NEURO) == 0 && estate_rent_unit(s, NEURO_K) == 0);
    CHECK(estate_removal_slot(s, 16, 1, &t) == 2);
    put16(sim_room_record(s, 0, 15, 1) + 0x0A, 1);
    CHECK(estate_removal_slot(s, 16, 1, &t) < 0);
}

static void test_freed_slots(void) {
    /* a freed slot (FF 01 / FF 00) is taken only by a purchase, never by a rent */
    fresh(5000);
    plots(0, 20, 0);
    for (int i = 0; i < 200; i++) slot(i)[0] = 0x0F, slot(i)[1] = 0;
    slot(7)[0] = 0xFF, slot(7)[1] = 1;
    CHECK(estate_rent_unit(s, GRUND) == ESTATE_NO_SLOT);
    int out, price;
    CHECK(estate_buy_unit(s, GRUND, &out, &price) == 0 && slot(7)[0] == GRUND && slot(7)[1] == 0);
    /* the overview counts only slots with a room */
    fresh(0);
    slot(0)[0] = GRUND, slot(0)[1] = 1;
    slot(1)[0] = 0xFF, slot(1)[1] = 1;
    slot(2)[0] = WARTE, slot(2)[1] = 0;
    g_image[CATALOG + 0xFF * CATALOG_SIZE + CATALOG_CATEGORY] = 3; /* what an FF slot would read as its category */
    int e, g;
    estate_unit_counts(s, &e, &g);
    CHECK(e == 1 && g == 1);
    CHECK(estate_rented_slot_of_category(s, 1) == 0 && estate_rented_slot_of_category(s, 3) < 0);
}

static void test_room_name(void) {
    fresh(0);
    size_t n;
    const uint8_t *p = estate_room_name(s, LAGER, &n);
    CHECK(n == 5 && memcmp(p, "Typ 6", 5) == 0);
}

/* 0x46CC4 from the room creation 0x1DE67 (docs/mainloop.md, module H routine table; docs/sim-ai.md, build step 5): a
 * new room of category 5-11, 13 or 15-18 writes item 0x0E (class 2, w 4, d +1, b6 3, byte 8 = the hospital, b21 = the
 * category, b22 = the instance, b23 / b24 = r1, b28 = 0) through the gate 40; categories 0-4, 12 and 14 write none. */
static int news_in_use(void) { return sim_news_count(s); }
static void clear_news(void) { memset(sim_news_item(s, 0), 0, 70 * 100); }
static void test_room_news(void) {
    fresh(100000);
    type(20, 12, 10, 100, 1, 3, 10);
    type(21, 14, 10, 100, 1, 3, 10);
    plots(0, 60, 0);
    CHECK(estate_rent_unit(s, GRUND) == 0 && estate_rent_unit(s, AUFNAHME) == 0 && estate_rent_unit(s, WARTE) == 0);
    CHECK(estate_rent_unit(s, LAGER) == 0 && news_in_use() == 0);
    CHECK(estate_rent_unit(s, KRANKEN) == 0 && news_in_use() == 1);
    const uint8_t *it = sim_news_item(s, 0);
    CHECK(it[0] == 2 && it[20] == 0x0E && it[8] == 0 && it[21] == 5 && it[22] == 0 && it[4] == 4 && it[5] == 1);
    CHECK(it[6] == 3 && it[28] == 0 && it[23] <= 1 && it[24] <= 1);
    CHECK(estate_rent_unit(s, KRANKEN) == 0 && news_in_use() == 2 && sim_news_item(s, 1)[22] == 1);
    CHECK(estate_rent_unit(s, 20) == 0 && estate_rent_unit(s, 21) == 0 && news_in_use() == 2);
    CHECK(estate_rent_unit(s, NEURO) == 0 && news_in_use() == 3);
    CHECK(sim_news_item(s, 2)[21] == 15 && sim_news_item(s, 2)[22] == 0);
    /* every category, called directly */
    for (int cat = 0; cat < 19; cat++) {
        clear_news();
        sim_news_new_room(s, 9, cat, 3);
        int want = !(cat <= 4 || cat == 12 || cat == 14);
        CHECK(news_in_use() == want);
        if (want) CHECK(sim_news_item(s, 0)[8] == 9 && sim_news_item(s, 0)[21] == cat && sim_news_item(s, 0)[22] == 3);
    }
    int ones23 = 0, ones24 = 0;
    for (int n = 0; n < 200; n++) {
        clear_news();
        sim_news_new_room(s, 1, 7, 0);
        ones23 += sim_news_item(s, 0)[23], ones24 += sim_news_item(s, 0)[24];
        CHECK(sim_news_item(s, 0)[23] <= 1 && sim_news_item(s, 0)[24] <= 1);
    }
    CHECK(ones23 > 60 && ones23 < 140 && ones24 > 60 && ones24 < 140);
    /* the gate: 40 in use, written; 41, not */
    for (int n = 40; n <= 41; n++) {
        clear_news();
        for (int i = 0; i < n; i++) sim_news_item(s, i)[0] = 2, sim_news_item(s, i)[20] = 9;
        sim_news_new_room(s, 1, 7, 0);
        CHECK(news_in_use() == 41);
        CHECK(sim_news_item(s, 40)[20] == (n == 40 ? 0x0E : 9));
    }
}

/* FIX-24 (docs/fixes.md): cash 30, n 1: off rented with cash -14, on refused with cash 30; cash 44, n 1: rented, 0 */
static void test_fix24_rent_check(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 23 : 0);
        fresh(30);
        CHECK(estate_rent_plots(s, 1) == (on ? ESTATE_NO_CASH_RENT : 0));
        CHECK(cash() == (on ? 30 : -14) && estate_rented(s) == !on);
        fresh(44);
        CHECK(estate_rent_plots(s, 1) == 0 && cash() == 0 && estate_rented(s) == 1);
    }
    fixes_set(0);
}

static void test_rating_construction(void) {
    for (int buy = 0; buy <= 1; buy++)
        for (int branch = 0; branch < 4; branch++) {
            fresh(branch == 2 ? 0 : 10000);
            plots(branch == 1 ? 0 : 100, 0, 0);
            h()[1] = 77;
            sim_rating_record(s, h());
            g_image[CATALOG + LAGER * CATALOG_SIZE + 1] = 39;
            if (!branch) memset(slot(0), 0, 400); /* no free slot */
            int result = buy ? estate_buy_unit(s, LAGER, NULL, NULL) : estate_rent_unit(s, LAGER);
            CHECK(result == (branch == 0 ? ESTATE_NO_SLOT : branch == 1 ? ESTATE_TOO_FEW_PLOTS :
                             branch == 2 ? ESTATE_TOO_EXPENSIVE : 0));
            CHECK(sim_rating_byte(s) == (branch ? 39 : 77));
            if (branch) {
                g_image[CATALOG + LAGER * CATALOG_SIZE + 1] = 91;
                CHECK(sim_rating_byte(s) == 91);
            }
        }
    for (int allowed = 0; allowed <= 1; allowed++) {
        fresh(allowed ? 10000 : 0);
        slot(0)[0] = LAGER, slot(0)[1] = 1;
        h()[1] = 77;
        sim_rating_record(s, h());
        CHECK(estate_buy_unit(s, LAGER, NULL, NULL) == (allowed ? 0 : ESTATE_TOO_EXPENSIVE));
        CHECK(sim_rating_byte(s) == 77); /* buy-out never enters the new-build writer */
    }
    fresh(10000);
    plots(100, 0, 0);
    CHECK(estate_rent_unit(s, WARTE) == 0);
    CHECK(s->rating_ref.kind == SIM_RATING_WORLD && s->rating_ref.block == 12 && s->rating_ref.offset == 180);
}

int main(void) {
    test_rating_construction();
    test_fix24_rent_check();
    test_rent_plots();
    test_buy_plots();
    test_cancel_plots();
    test_sell_plots();
    test_build_check();
    test_type_search();
    test_rent_units();
    test_buy_units();
    test_cancel_unit();
    test_sell_unit();
    test_removal_rules();
    test_freed_slots();
    test_room_name();
    test_room_news();
    world_free(&g_world);
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] all checks passed\n");
    return 0;
}
