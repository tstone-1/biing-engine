/* Purchasing and the store room for the player (docs/ui-purchasing.md). Synthetic world and program image (see
 * AGENTS.md): every catalog entry, room description and offer below is made up for the test; the expected values
 * follow the rules of the spec (e.g. an own offer at (pc + 2) / 2, the page-down limits 8, 16, 22 of its oracle run). */
#include <stdio.h>
#include <string.h>

#include "fixes.h"
#include "purchase.h"
#include "rooms.h"
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

#define EQ(id) (g_image + 0x144A + (id) * 20)
#define FU(id) (g_image + 0xEE0 + (id) * 18)
enum { MEAL = 0x2B };

static uint8_t *h(int i) { return world_hospital(&g_world, i); }
static int32_t cash(int i) { return world_cash(&g_world, i); }
static uint8_t *pb(void) { return rooms_pb(s); }
static int P(int off) { return rooms_pb16(s, off); }
static void setP(int off, int v) { rooms_set_pb16(s, off, v); }
static uint8_t *counts(int i) { return g_world.block[BLK_ROOM_COUNTS] + 100 * i; }
static uint8_t *S(void) { return purchase_store(s); }
static uint8_t *EM(int i) { return purchase_offer(s, 1, i); }
static uint8_t *FM(int i) { return purchase_offer(s, 0, i); }
static int id_at(const uint8_t *p, int off, int k) { return (uint16_t)get16(p + off + 2 * k); }

static void describe(int cat, int f, int fs, int e, int es, int ca, int cb, int type) {
    const int d[7] = {f, fs, e, es, ca, cb, type};
    for (int i = 0; i < 7; i++) put16(g_image + 0x4758 + 14 * cat + 2 * i, d[i]);
}

static void offer(uint8_t *o, int seller, int price, int id, int condition) {
    o[0] = (uint8_t)seller, o[1] = (uint8_t)price, put16(o + 2, id), o[4] = (uint8_t)condition, o[5] = 0;
    put16(o + 6, 0);
}

/* Hospital 0 with a store of capacity 40 holding nothing, clerk 30, a treatment room with 4 + 4 slots and an
 * intensive-care room (cat 9) with 4 furniture and 30 equipment slots; both markets empty. */
static void fresh(void) {
    world_free(&g_world);
    world_alloc(&g_world);
    memset(g_image, 0, sizeof g_image);
    g_prog.image.data = g_image;
    g_prog.image.size = sizeof g_image;
    sim_init(&g_sim, &g_world, &g_prog, 5);
    memset(g_world.block[BLK_EQUIPMENT_MARKET], 0xFF, g_world.size[BLK_EQUIPMENT_MARKET]);
    memset(g_world.block[BLK_FURNITURE_MARKET], 0xFF, g_world.size[BLK_FURNITURE_MARKET]);
    describe(4, 0x0A, 120, 0xFA, 120, 0x1EA, 0x262, 6);
    describe(5, 0x80, 4, 0x88, 4, 0x90, 0x94, 0x98);
    describe(9, 0x20, 4, 0x30, 30, 0x70, 0x74, 0x98);
    counts(0)[1] = counts(0)[4] = counts(0)[5] = counts(0)[9] = 1;
    S()[STORE_CAPACITY] = 40;
    put16(S() + STORE_CLERK, 30);
    world_set_cash(&g_world, 0, 984);
    world_set_cash(&g_world, DEALER, 5000);
    for (int i = 1; i < 20; i++) EQ(i)[6] = 9, EQ(i)[8] = 4, EQ(i)[7] = 0; /* "Untersuchung", price class 4 */
    for (int i = 1; i < 20; i++) FU(i)[6] = 2, FU(i)[8] = 1;
    EQ(5)[10] = 7, EQ(5)[11] = 12; /* capabilities 7 and 12 */
    EQ(6)[10] = MEAL;
    EQ(7)[6] = 3; /* another kind */
    FU(8)[6] = 5;
}

/* ---- search and paging of the offers -------------------------------------------------------------------- */

static void test_search_refusals(void) {
    fresh();
    offer(EM(3), DEALER, 24, 4, 100);
    counts(0)[4] = 0;
    CHECK(purchase_search(s, 1) == PU_NO_STORE_BUY && EM(3)[5] == 0);
    counts(0)[4] = 1;
    S()[STORE_FURNITURE_HELD] = 30, S()[STORE_EQUIPMENT_HELD] = 10; /* 40 - 30 - 10 = 0 */
    CHECK(purchase_search(s, 1) == PU_STORE_FULL);
    S()[STORE_FURNITURE_HELD] = 0xF0, S()[STORE_EQUIPMENT_HELD] = 0x38; /* byte arithmetic: 40 - 240 - 56 = 0 mod 256 */
    CHECK(purchase_search(s, 0) == PU_STORE_FULL);
    S()[STORE_FURNITURE_HELD] = 39, S()[STORE_EQUIPMENT_HELD] = 0;
    purchase_set_filters(s, 0, 3); /* nothing of kind 3 */
    CHECK(purchase_search(s, 1) == PU_NO_EQUIPMENT_OFFER && P(PU_INDEX) == 300);
    CHECK(purchase_search(s, 0) == PU_NO_FURNITURE_OFFER);
    purchase_set_filters(s, 0, 0);
    CHECK(purchase_search(s, 1) == 0 && P(PU_INDEX) == 3 && EM(3)[5] == 0xFF);
}

static void test_search_filters(void) {
    fresh();
    offer(EM(0), DEALER, 5, 4, 90);
    EM(0)[5] = 0xFF; /* reserved */
    offer(EM(1), -2, 5, 5, 90); /* free slot (seller negative) */
    offer(EM(2), 3, 5, 4, 90);
    offer(EM(4), 3, 5, 5, 90); /* capabilities 7 and 12 */
    offer(EM(6), 3, 5, 7, 90); /* kind 3 */
    CHECK(purchase_search(s, 1) == 0 && P(PU_INDEX) == 2);
    EM(2)[5] = 0;
    purchase_set_filters(s, 12, 0);
    CHECK(purchase_search(s, 1) == 0 && P(PU_INDEX) == 4); /* the second capability counts */
    EM(4)[5] = 0;
    purchase_set_filters(s, 0, 3);
    CHECK(purchase_search(s, 1) == 0 && P(PU_INDEX) == 6);
    EM(6)[5] = 0;
    purchase_set_filters(s, 12, 9); /* both filters: kind 9 and capability 12 */
    CHECK(purchase_search(s, 1) == 0 && P(PU_INDEX) == 4);
    EM(4)[5] = 0;
    purchase_set_filters(s, 13, 9);
    CHECK(purchase_search(s, 1) == PU_NO_EQUIPMENT_OFFER);
    offer(FM(7), DEALER, 3, 8, 50); /* furniture kind 5 */
    offer(FM(9), DEALER, 3, 9, 50); /* kind 2 */
    purchase_set_filters(s, 13, 2); /* the capability filter does not apply to furniture */
    CHECK(purchase_search(s, 0) == 0 && P(PU_INDEX) == 9);
}

static void test_step(void) {
    fresh();
    offer(EM(0), DEALER, 5, 4, 90);
    offer(EM(5), DEALER, 5, 7, 90); /* kind 3: filtered out below */
    offer(EM(299), DEALER, 5, 4, 90);
    CHECK(purchase_search(s, 1) == 0 && P(PU_INDEX) == 0);
    purchase_step(s, 1, 1);
    CHECK(P(PU_INDEX) == 5 && EM(5)[5] == 0xFF && EM(0)[5] == 0);
    purchase_step(s, 1, 1); /* wraps over 299 */
    CHECK(P(PU_INDEX) == 299 && EM(5)[5] == 0);
    purchase_step(s, 1, 1);
    CHECK(P(PU_INDEX) == 0);
    purchase_step(s, 1, -1);
    CHECK(P(PU_INDEX) == 299 && EM(299)[5] == 0xFF && EM(0)[5] == 0);
    purchase_set_filters(s, 0, 9);
    purchase_step(s, 1, -1); /* skips slot 5 (kind 3) */
    CHECK(P(PU_INDEX) == 0);
    EM(299)[0] = 0xFF; /* only one match left: shown again */
    purchase_step(s, 1, 1);
    CHECK(P(PU_INDEX) == 0 && EM(0)[5] == 0xFF);
    purchase_leave(s, 1);
    CHECK(EM(0)[5] == 0);
}

/* ---- buying ---------------------------------------------------------------------------------------- */

static void test_buy_equipment(void) {
    fresh();
    put16(S() + STORE_EQUIPMENT, 6), S()[STORE_EQUIPMENT_COND] = 70; /* a meal in slot 0 */
    S()[STORE_EQUIPMENT_HELD] = 1;
    offer(EM(0), DEALER, 24, 4, 100);
    CHECK(purchase_search(s, 1) == 0);
    PurchaseResult o;
    CHECK(purchase_buy(s, 1, &o) == PU_BOUGHT && o.id == 4 && o.price == 24 && o.seller == DEALER && o.condition == 100);
    CHECK(cash(0) == 960 && get16(h(0) + 0xAE) == 1 && get32(h(0) + 0xBA) == 24 && get32(h(0) + 0xBE) == 24);
    CHECK(cash(DEALER) == 5024 && get16(h(DEALER) + 0xB0) == 1 && get32(h(DEALER) + 0xCA) == 24 &&
          get32(h(DEALER) + 0xCE) == 24);
    CHECK(EM(0)[0] == 0xFF && EM(0)[2] == 4); /* freed, the other bytes stay */
    CHECK(S()[STORE_EQUIPMENT_HELD] == 2);
    /* bought into slot 1, then the tidy-up moved the meal behind it */
    CHECK(id_at(S(), STORE_EQUIPMENT, 0) == 4 && S()[STORE_EQUIPMENT_COND] == 100);
    CHECK(id_at(S(), STORE_EQUIPMENT, 1) == 6 && S()[STORE_EQUIPMENT_COND + 1] == 70);
}

static void test_buy_furniture(void) {
    fresh();
    put16(S() + STORE_EQUIPMENT, 6), put16(S() + STORE_EQUIPMENT + 2, 4); /* meal before an item: no tidy */
    put16(S() + STORE_FURNITURE + 2, 9), S()[STORE_FURNITURE_HELD] = 1; /* furniture gap at slot 0 */
    offer(FM(2), 5, 3, 3, 80);
    world_set_cash(&g_world, 5, 100);
    CHECK(purchase_search(s, 0) == 0 && P(PU_INDEX) == 2);
    PurchaseResult o;
    CHECK(purchase_buy(s, 0, &o) == PU_BOUGHT);
    CHECK(cash(0) == 981 && get16(h(0) + 0xB4) == 1 && get32(h(0) + 0xC2) == 3 && get32(h(0) + 0xC6) == 3);
    CHECK(get16(h(0) + 0xAE) == 0 && get32(h(0) + 0xBA) == 0);
    CHECK(cash(5) == 103 && get16(h(5) + 0xB6) == 1 && get32(h(5) + 0xD2) == 3 && get32(h(5) + 0xD6) == 3);
    CHECK(id_at(S(), STORE_FURNITURE, 0) == 3 && S()[STORE_FURNITURE_COND] == 80 && S()[STORE_FURNITURE_HELD] == 2);
    CHECK(id_at(S(), STORE_EQUIPMENT, 0) == 6 && id_at(S(), STORE_EQUIPMENT, 1) == 4); /* 0x391D2: no 0x2A873 */
    CHECK(FM(2)[0] == 0xFF);
}

static void test_buy_refusal(void) {
    fresh();
    offer(EM(0), DEALER, 24, 4, 100);
    world_set_cash(&g_world, 0, 23);
    CHECK(purchase_search(s, 1) == 0 && EM(0)[5] == 0xFF);
    PurchaseResult o;
    CHECK(purchase_buy(s, 1, &o) == PU_NO_CASH);
    CHECK(EM(0)[5] == 0 && EM(0)[0] == DEALER && cash(0) == 23 && S()[STORE_EQUIPMENT_HELD] == 0);
    CHECK(id_at(S(), STORE_EQUIPMENT, 0) == 0 && cash(DEALER) == 5000);
    world_set_cash(&g_world, 0, 24); /* the price exactly: enough */
    CHECK(purchase_buy(s, 1, &o) == PU_BOUGHT && cash(0) == 0 && S()[STORE_EQUIPMENT_HELD] == 1);
    world_set_cash(&g_world, 0, -5); /* signed comparison */
    offer(FM(0), DEALER, 1, 3, 50);
    setP(PU_INDEX, 0);
    CHECK(purchase_buy(s, 0, &o) == PU_NO_CASH);
}

static void test_buy_own(void) {
    fresh();
    put16(S() + STORE_EQUIPMENT, 6), S()[STORE_EQUIPMENT_HELD] = 1; /* a meal: the tidy-up would move it */
    offer(EM(0), 0, 12, 4, 100); /* the player's own offer */
    CHECK(purchase_search(s, 1) == 0);
    PurchaseResult o;
    CHECK(purchase_buy(s, 1, &o) == PU_OWN);
    CHECK(cash(0) == 984 && get16(h(0) + 0xAE) == 0 && get16(h(0) + 0xB0) == 0 && get32(h(0) + 0xBA) == 0 &&
          get32(h(0) + 0xCA) == 0);
    CHECK(EM(0)[0] == 0xFF && S()[STORE_EQUIPMENT_HELD] == 2 && id_at(S(), STORE_EQUIPMENT, 1) == 4);
    CHECK(id_at(S(), STORE_EQUIPMENT, 0) == 6); /* no tidy-up on this path */
}

/* the refund rule, through the AI's call of the shared routine ([0x5ABD] = FF) */
static void test_refund(void) {
    fresh();
    counts(3)[4] = 1;
    uint8_t *st = sim_room_record(s, 3, 4, 0);
    world_set_cash(&g_world, 3, 5);
    world_set_cash(&g_world, 7, 50);
    offer(EM(0), 7, 7, 4, 66);
    CHECK(sim_buy_offer(s, 3, 0, 1, 0) == BUY_FREE);
    CHECK(cash(3) == 5 && cash(7) == 50 && get16(h(3) + 0xAE) == 0 && get16(h(7) + 0xB0) == 0);
    CHECK(get32(h(3) + 0xBA) == 0 && get32(h(7) + 0xCA) == 0);
    CHECK(EM(0)[0] == 0xFF && id_at(st, STORE_EQUIPMENT, 0) == 4 && st[STORE_EQUIPMENT_HELD] == 1);
    offer(FM(0), 7, 5, 3, 66);
    CHECK(sim_buy_offer(s, 3, 0, 0, 0) == BUY_DONE && cash(3) == 0 && cash(7) == 55); /* cash 0 afterwards: paid */
    offer(FM(1), 7, 1, 3, 66);
    CHECK(sim_buy_offer(s, 3, 1, 0, 0) == BUY_FREE && cash(3) == 0 && get16(h(3) + 0xB4) == 1);
    world_set_cash(&g_world, 3, 0);
    offer(EM(1), 3, 9, 4, 66); /* the AI's own offer: no counters either */
    CHECK(sim_buy_offer(s, 3, 1, 1, 0) == BUY_FREE);
    world_set_cash(&g_world, 3, 20);
    offer(EM(2), 3, 9, 4, 66);
    CHECK(sim_buy_offer(s, 3, 2, 1, 0) == BUY_OWN && cash(3) == 20 && get16(h(3) + 0xAE) == 0);
    /* the AI's equipment purchase (0x2F610 with [0x5ABD] = FF) skips the tidy-up */
    memset(st + STORE_EQUIPMENT, 0, 2 * STORE_SLOTS);
    put16(st + STORE_EQUIPMENT, 6); /* a meal: the tidy-up would move it behind the bought item */
    world_set_cash(&g_world, 3, 100);
    offer(EM(3), DEALER, 9, 4, 66);
    CHECK(sim_buy_offer(s, 3, 3, 1, 0) == BUY_DONE && id_at(st, STORE_EQUIPMENT, 0) == 6 &&
          id_at(st, STORE_EQUIPMENT, 1) == 4);
}

/* the free-slot search has no failure path: a full list writes one past it (docs/ui-purchasing.md) */
static void test_buy_overflow(void) {
    fresh();
    for (int k = 0; k < 120; k++) put16(S() + STORE_EQUIPMENT + 2 * k, 1), put16(S() + STORE_FURNITURE + 2 * k, 1);
    offer(EM(0), DEALER, 1, 9, 77);
    offer(FM(0), DEALER, 1, 8, 55);
    sim_buy_offer(s, 0, 0, 1, 1);
    CHECK(get16(S() + 0x1EA) == 9 && sim_room_record(s, 1, 4, 0)[0] == 77);
    sim_buy_offer(s, 0, 0, 0, 1);
    CHECK(get16(S() + STORE_EQUIPMENT) == 8 && S()[STORE_EQUIPMENT_COND] == 55);
}

/* ---- 111 ----------------------------------------------------------------------------------------- */

static void test_sell_open(void) {
    fresh();
    counts(0)[4] = 0;
    CHECK(purchase_sell_open(s, 1) == PU_NO_STORE_SELL);
    counts(0)[4] = 1;
    put16(S() + STORE_CLERK, 0);
    CHECK(purchase_sell_open(s, 1) == PU_NO_CLERK_SELL);
    put16(S() + STORE_CLERK, 30);
    S()[STORE_FURNITURE_HELD] = 1;
    CHECK(purchase_sell_open(s, 1) == PU_NO_EQUIPMENT_HELD);
    S()[STORE_FURNITURE_HELD] = 0, S()[STORE_EQUIPMENT_HELD] = 1;
    CHECK(purchase_sell_open(s, 0) == PU_NO_FURNITURE_HELD);
    setP(PU_ROW, 5), setP(PU_INDEX, 9), setP(PU_VIEW, 1), pb()[PU_MISSING] = 0xFF;
    CHECK(purchase_sell_open(s, 1) == 0);
    CHECK(P(PU_CAT) == 4 && P(PU_VIEW) == 0 && P(PU_EQUIPMENT) == 1 && P(PU_ROW) == 0 && P(PU_INDEX) == 0 &&
          pb()[PU_MISSING] == 0);
    S()[STORE_FURNITURE_HELD] = 1;
    CHECK(purchase_sell_open(s, 0) == 0 && P(PU_EQUIPMENT) == 0);
}

static void test_sell(void) {
    fresh();
    put16(S() + STORE_EQUIPMENT, 4), S()[STORE_EQUIPMENT_COND] = 100;
    put16(S() + STORE_EQUIPMENT + 2, 6), S()[STORE_EQUIPMENT_COND + 1] = 40; /* a meal... */
    put16(S() + STORE_EQUIPMENT + 4, 5), S()[STORE_EQUIPMENT_COND + 2] = 50; /* ...and an item behind it */
    S()[STORE_EQUIPMENT_HELD] = 3;
    offer(EM(0), DEALER, 5, 4, 10);
    CHECK(purchase_sell_open(s, 1) == 0);
    PurchaseResult o;
    purchase_select(s, 3);
    CHECK(purchase_sell(s, &o) == 0 && EM(1)[0] == 0xFF && S()[STORE_EQUIPMENT_HELD] == 3); /* empty row */
    purchase_select(s, 0);
    CHECK(purchase_sell(s, &o) == PU_OFFERED && o.id == 4 && o.price == 3 && o.condition == 100); /* (4 + 2) / 2 */
    const uint8_t *e = EM(1);
    CHECK(e[0] == 0 && e[1] == 3 && id_at(e, 2, 0) == 4 && e[4] == 100 && e[5] == 0 && get16(e + 6) == 0);
    CHECK(S()[STORE_EQUIPMENT_HELD] == 2 && cash(0) == 984);
    /* the tidy-up: the gap closed, then the meal moved behind the item */
    CHECK(id_at(S(), STORE_EQUIPMENT, 0) == 5 && S()[STORE_EQUIPMENT_COND] == 50 && id_at(S(), STORE_EQUIPMENT, 1) == 6);
    put16(EQ(5) + 8, 300);
    CHECK(purchase_sell(s, &o) == PU_OFFERED && o.price == 151 && EM(2)[1] == 151);
    put16(EQ(6) + 8, 600); /* (602) / 2 = 301, kept as a byte: 45 */
    setP(PU_ROW, 0);
    CHECK(purchase_sell(s, &o) == PU_OFFERED && o.price == 45 && EM(3)[1] == 45);
    /* furniture to the furniture market */
    put16(S() + STORE_FURNITURE, 3), S()[STORE_FURNITURE_COND] = 66, S()[STORE_FURNITURE_HELD] = 1;
    CHECK(purchase_sell_open(s, 0) == 0);
    CHECK(purchase_sell(s, &o) == PU_OFFERED && FM(0)[0] == 0 && FM(0)[1] == 1 && id_at(FM(0), 2, 0) == 3 &&
          FM(0)[4] == 66 && o.condition == 66 && S()[STORE_FURNITURE_HELD] == 0);
    /* market full */
    for (int i = 0; i < 300; i++) EM(i)[0] = DEALER;
    put16(S() + STORE_EQUIPMENT, 4), S()[STORE_EQUIPMENT_HELD] = 1;
    CHECK(purchase_sell_open(s, 1) == 0 && purchase_sell(s, &o) == PU_MARKET_FULL_EQUIPMENT);
    CHECK(id_at(S(), STORE_EQUIPMENT, 0) == 4);
    for (int i = 0; i < 300; i++) FM(i)[0] = 2;
    put16(S() + STORE_FURNITURE, 3), S()[STORE_FURNITURE_HELD] = 1;
    CHECK(purchase_sell_open(s, 0) == 0 && purchase_sell(s, &o) == PU_MARKET_FULL_FURNITURE);
}

/* ---- lists, paging, information ------------------------------------------------------------------- */

static void test_paging(void) {
    fresh();
    CHECK(purchase_inventory_button(s, 9, 0x31D44) == 0);
    purchase_inventory_open(s);
    CHECK(P(PU_EQUIPMENT) == 1 && P(PU_INDEX) == 0 && P(PU_VIEW) == 0);
    int seen[5], n = 0;
    for (int i = 0; i < 5; i++) purchase_page_down(s), seen[n++] = P(PU_INDEX);
    CHECK(seen[0] == 8 && seen[1] == 16 && seen[2] == 22 && seen[3] == 22 && seen[4] == 22); /* 30 slots */
    CHECK(!purchase_page_down(s));
    purchase_page_up(s);
    CHECK(P(PU_INDEX) == 14);
    purchase_page_up(s), purchase_page_up(s);
    CHECK(P(PU_INDEX) == 0);
    setP(PU_INDEX, 7);
    purchase_page_up(s);
    CHECK(P(PU_INDEX) == 0);
    purchase_toggle_kind(s); /* furniture: 4 slots, limit -4 */
    CHECK(P(PU_EQUIPMENT) == 0 && !purchase_page_down(s) && P(PU_INDEX) == 0);
    CHECK(purchase_toggle_view(s) && P(PU_VIEW) == 1); /* store, capacity 40 */
    for (int i = 0; i < 6; i++) purchase_page_down(s);
    CHECK(P(PU_INDEX) == 40); /* the limit is the capacity, not capacity - 8 */
    CHECK(!purchase_page_down(s));
    S()[STORE_CAPACITY] = 45;
    CHECK(purchase_page_down(s) && P(PU_INDEX) == 45); /* min(L - i, 8) */
}

static void test_lists(void) {
    fresh();
    uint8_t *icu = sim_room_record(s, 0, 9, 0);
    put16(icu + 0x30 + 2 * 3, 5), icu[0x74 + 3] = 81;
    put16(icu + 0x20 + 2, 8), icu[0x70 + 1] = 62;
    put16(S() + STORE_EQUIPMENT + 2 * 9, 7), S()[STORE_EQUIPMENT_COND + 9] = 44;
    put16(S() + STORE_FURNITURE + 2 * 2, 3), S()[STORE_FURNITURE_COND + 2] = 33;
    purchase_inventory_button(s, 9, 0);
    purchase_inventory_open(s);
    int c;
    CHECK(purchase_list_item(s, 3, &c) == 5 && c == 81);
    purchase_toggle_kind(s);
    CHECK(purchase_list_item(s, 1, &c) == 8 && c == 62);
    purchase_toggle_view(s);
    CHECK(purchase_list_item(s, 2, &c) == 3 && c == 33);
    purchase_toggle_kind(s);
    CHECK(purchase_list_item(s, 9, &c) == 7 && c == 44);
    CHECK(purchase_list_item(s, 127, &c) == get16(S() + STORE_EQUIPMENT + 254)); /* page 40-47 and beyond: raw */
    /* information: equipment in the store view shows S + id (the original's fault) */
    setP(PU_INDEX, 8), purchase_select(s, 1);
    S()[7] = 40;
    CHECK(purchase_info(s, &c) == 7 && c == S()[7]);
    purchase_select(s, 0);
    CHECK(purchase_info(s, &c) == 0);
    purchase_toggle_kind(s); /* furniture in the store view: the right condition */
    setP(PU_INDEX, 0), purchase_select(s, 2);
    CHECK(purchase_info(s, &c) == 3 && c == 33);
    purchase_toggle_view(s), purchase_toggle_kind(s); /* equipment in the room view: the right condition */
    purchase_select(s, 3);
    CHECK(purchase_info(s, &c) == 5 && c == 81);
}

static void test_toggles(void) {
    fresh();
    purchase_inventory_button(s, 5, 0x36D3C);
    purchase_inventory_open(s);
    setP(PU_ROW, 3), setP(PU_INDEX, 8);
    purchase_toggle_kind(s);
    CHECK(P(PU_EQUIPMENT) == 0 && P(PU_ROW) == 0 && P(PU_INDEX) == 0);
    purchase_toggle_kind(s);
    CHECK(P(PU_EQUIPMENT) == 1);
    setP(PU_ROW, 3);
    counts(0)[4] = 0;
    CHECK(!purchase_toggle_view(s) && P(PU_VIEW) == 0 && P(PU_ROW) == 3); /* no store room: nothing */
    counts(0)[4] = 1;
    CHECK(purchase_toggle_view(s) && P(PU_VIEW) == 1 && P(PU_ROW) == 0);
    counts(0)[4] = 0;
    CHECK(purchase_toggle_view(s) && P(PU_VIEW) == 0); /* back to the room view always works */
}

static void test_inventory_buttons(void) {
    fresh();
    CHECK(purchase_inventory_button(s, 4, 0x2EEE5) == 0 && P(PU_VIEW) == 1 && pb()[PU_MISSING] == 0xFF &&
          P(PU_CAT) == 4 && get32(pb() + 6) == 0x2EEE5);
    CHECK(purchase_inventory_button(s, 2, 0x33D58) == 0 && P(PU_VIEW) == 0 && pb()[PU_MISSING] == 0);
    counts(0)[7] = counts(0)[11] = counts(0)[15] = 1;
    uint8_t *op = sim_room_record(s, 0, 7, 0), *dental = sim_room_record(s, 0, 11, 0);
    put16(op + 12, 9), put16(op + 14, 3);
    setP(PU_CAT, 2);
    CHECK(purchase_inventory_button(s, 7, 0x3649A) == PU_OPERATION && P(PU_CAT) == 2);
    put16(op + 14, 999); /* the operation is over */
    CHECK(purchase_inventory_button(s, 7, 0x3649A) == 0 && P(PU_CAT) == 7);
    put16(op + 12, 0), put16(op + 14, 3);
    CHECK(purchase_inventory_button(s, 7, 0x3649A) == 0);
    put16(dental + 12, 4);
    CHECK(purchase_inventory_button(s, 11, 0x31A16) == PU_DENTIST_BUSY);
    dental[0x44] = 1;
    CHECK(purchase_inventory_button(s, 11, 0x31A16) == 0 && P(PU_VIEW) == 1 && pb()[PU_MISSING] == 0xFF);
    put16(sim_room_record(s, 0, 15, 0) + 12, 4);
    CHECK(purchase_inventory_button(s, 15, 0x31EE3) == PU_NEURO_BUSY);
    put16(sim_room_record(s, 0, 8, 0) + 12, 4);
    CHECK(purchase_inventory_button(s, 8, 0x3677D) == PU_NEURO_BUSY);
}

/* ---- moves ------------------------------------------------------------------------------------------ */

static uint8_t *treatment(void) { return sim_room_record(s, 0, 5, 0); }
static int clerk_work(void) { return get16(rooms_staff(s, 30) + 0x10); }

static void open_treatment(int store_view) {
    purchase_inventory_button(s, 5, 0x36D3C);
    purchase_inventory_open(s);
    if (store_view) purchase_toggle_view(s);
}

static void test_move_to_room(void) {
    fresh();
    uint8_t *t = treatment();
    put16(t + 0x88, 1), put16(t + 0x88 + 2, 2), put16(t + 0x88 + 6, 3); /* slot 2 free */
    put16(S() + STORE_EQUIPMENT, 6), put16(S() + STORE_EQUIPMENT + 2, 4), S()[STORE_EQUIPMENT_COND + 1] = 91;
    S()[STORE_EQUIPMENT_HELD] = 2;
    EQ(4)[4] = 7, EQ(4)[5] = 3; /* ratings: the room is recomputed */
    open_treatment(1);
    purchase_select(s, 1);
    CHECK(purchase_move(s) == PU_MOVED);
    CHECK(id_at(t, 0x88, 2) == 4 && t[0x94 + 2] == 91 && id_at(S(), STORE_EQUIPMENT, 1) == 0);
    CHECK(S()[STORE_EQUIPMENT_HELD] == 1 && get16(h(0) + 0x11A) == 1 && clerk_work() == 1);
    CHECK(t[0] == 7 && t[1] == 3); /* sim_recompute (base 0 in the synthetic catalog) */
    put16(h(0) + 0x11A, -3); /* moves back outweigh: the clerk's counter does not grow */
    purchase_select(s, 0);
    put16(t + 0x88 + 4, 0);
    put16(S() + STORE_EQUIPMENT, 5), S()[STORE_EQUIPMENT_HELD] = 1;
    CHECK(purchase_move(s) == PU_MOVED && get16(h(0) + 0x11A) == -2 && clerk_work() == 1);
    put16(h(0) + 0x11A, -1);
    put16(t + 0x88 + 4, 0);
    put16(S() + STORE_EQUIPMENT, 5), S()[STORE_EQUIPMENT_HELD] = 1;
    CHECK(purchase_move(s) == PU_MOVED && get16(h(0) + 0x11A) == 0 && clerk_work() == 2); /* >= 0 counts */
    /* refusals */
    CHECK(S()[STORE_EQUIPMENT_HELD] == 0 && purchase_move(s) == PU_STORE_EMPTY_EQUIPMENT);
    purchase_toggle_kind(s), purchase_toggle_view(s), purchase_toggle_view(s); /* furniture, store view */
    CHECK(P(PU_EQUIPMENT) == 0 && P(PU_VIEW) == 1 && purchase_move(s) == PU_STORE_EMPTY_FURNITURE);
    for (int k = 0; k < 4; k++) put16(t + 0x80 + 2 * k, 9);
    put16(S() + STORE_FURNITURE, 3), S()[STORE_FURNITURE_HELD] = 1;
    CHECK(purchase_move(s) == PU_ROOM_FULL_FURNITURE && S()[STORE_FURNITURE_HELD] == 1);
    purchase_toggle_kind(s), purchase_toggle_view(s), purchase_toggle_view(s);
    put16(S() + STORE_EQUIPMENT, 4), S()[STORE_EQUIPMENT_HELD] = 1;
    CHECK(id_at(t, 0x88, 0) && id_at(t, 0x88, 3) && purchase_move(s) == PU_ROOM_FULL_EQUIPMENT);
}

static void test_move_to_store(void) {
    fresh();
    uint8_t *t = treatment();
    put16(t + 0x88, 1), put16(t + 0x88 + 2, 2), t[0x94 + 1] = 64, put16(t + 0x88 + 4, 3);
    put16(S() + STORE_EQUIPMENT, 4), S()[STORE_EQUIPMENT_HELD] = 1;
    open_treatment(0);
    purchase_select(s, 1);
    CHECK(purchase_move(s) == PU_MOVED);
    CHECK(id_at(S(), STORE_EQUIPMENT, 1) == 2 && S()[STORE_EQUIPMENT_COND + 1] == 64 && S()[STORE_EQUIPMENT_HELD] == 2);
    CHECK(id_at(t, 0x88, 1) == 3 && id_at(t, 0x88, 2) == 0); /* the room's list compacted */
    CHECK(get16(h(0) + 0x11A) == -2 && clerk_work() == 0);
    /* 0x2BFB7 / 0x2B210 run for every hospital and category: an AI room's first gap is closed (one per call), and
     * hospital 0's intensive-care room is recomputed */
    counts(2)[9] = 1;
    uint8_t *ai = sim_room_record(s, 2, 9, 0), *icu = sim_room_record(s, 0, 9, 0);
    put16(ai + 0x22, 7), put16(ai + 0x26, 8); /* furniture: gap, 7, gap, 8 */
    put16(icu + 0x30, 4); /* equipment item 4 (ratings 0 / 0 so far) */
    EQ(4)[4] = 2;
    /* furniture */
    purchase_toggle_kind(s);
    put16(t + 0x80, 8), t[0x90] = 21;
    purchase_select(s, 1);
    CHECK(purchase_move(s) == PU_RECOUNTED); /* row 1 is empty */
    purchase_select(s, 0);
    CHECK(purchase_move(s) == PU_MOVED && id_at(S(), STORE_FURNITURE, 0) == 8 && S()[STORE_FURNITURE_COND] == 21);
    CHECK(S()[STORE_FURNITURE_HELD] == 1 && get16(h(0) + 0x11A) == -4 && id_at(t, 0x80, 0) == 0);
    CHECK(id_at(ai, 0x20, 0) == 7 && id_at(ai, 0x20, 1) == 0 && id_at(ai, 0x20, 2) == 8 && icu[0] == 2);
    /* store packed: free 0 and negative */
    S()[STORE_CAPACITY] = 3;
    put16(t + 0x80, 8);
    CHECK(purchase_move(s) == PU_STORE_PACKED && id_at(t, 0x80, 0) == 8);
    S()[STORE_CAPACITY] = 2; /* 2 - 1 - 2 < 0: the original hangs; refused here */
    CHECK(purchase_move(s) == PU_STORE_PACKED && id_at(t, 0x80, 0) == 8);
}

/* a move runs the store tidy-up twice (0x2A873, then again inside 0x2BFB7) and recounts the store */
static void test_move_tidy_twice(void) {
    fresh();
    put16(S() + STORE_EQUIPMENT, 4), put16(S() + STORE_EQUIPMENT + 4, 5), S()[STORE_EQUIPMENT_COND + 2] = 55;
    S()[STORE_EQUIPMENT_HELD] = 9; /* wrong: the recount repairs it */
    open_treatment(1);
    CHECK(purchase_move(s) == PU_MOVED && id_at(treatment(), 0x88, 0) == 4);
    CHECK(id_at(S(), STORE_EQUIPMENT, 0) == 5 && S()[STORE_EQUIPMENT_COND] == 55 && id_at(S(), STORE_EQUIPMENT, 2) == 0);
    CHECK(S()[STORE_EQUIPMENT_HELD] == 1);
}

static void test_move_refusals(void) {
    fresh();
    put16(S() + STORE_EQUIPMENT, 4), S()[STORE_EQUIPMENT_HELD] = 1;
    purchase_inventory_button(s, 4, 0x2EEE5); /* the warehouse: view-only */
    purchase_inventory_open(s);
    CHECK(purchase_move(s) == PU_BACK_ONLY && S()[STORE_EQUIPMENT_HELD] == 1);
    open_treatment(1);
    put16(S() + STORE_CLERK, 0);
    CHECK(purchase_move(s) == PU_NO_CLERK_MOVE);
    counts(0)[4] = 0;
    CHECK(purchase_move(s) == PU_NO_STORE_MOVE);
    counts(0)[4] = 1;
    put16(S() + STORE_CLERK, 30);
    purchase_select(s, 4); /* an empty row in the store view: +9 - 1, then the recount repairs it */
    CHECK(purchase_move(s) == PU_RECOUNTED && S()[STORE_EQUIPMENT_HELD] == 1 && id_at(treatment(), 0x88, 0) == 0);
}

static void test_recount(void) {
    fresh();
    counts(2)[4] = 1;
    uint8_t *other = sim_room_record(s, 2, 4, 0);
    for (int k = 0; k < 70; k++) put16(S() + STORE_EQUIPMENT + 2 * k, 1 + k % 5);
    put16(S() + STORE_FURNITURE + 2 * 3, 0x0101); /* an id above 255 counts twice */
    put16(other + STORE_FURNITURE + 2 * 59, 2);
    put16(other + STORE_FURNITURE + 2 * 60, 2);
    S()[STORE_EQUIPMENT_HELD] = 70;
    purchase_recount(s);
    CHECK(S()[STORE_EQUIPMENT_HELD] == 60 && S()[STORE_FURNITURE_HELD] == 2); /* slots 0-59 only */
    CHECK(other[STORE_FURNITURE_HELD] == 1 && other[STORE_EQUIPMENT_HELD] == 0); /* every hospital with a store */
    counts(2)[4] = 0;
    other[STORE_FURNITURE_HELD] = 9;
    purchase_recount(s);
    CHECK(other[STORE_FURNITURE_HELD] == 9);
}

/* ---- the CD builds (docs/editions.md section 7) ---------------------------------------------------------------- */

/* German CD (OTH-DE-5): every furniture search first frees all 300 furniture offers, before the store-room test (the
 * original's run: four planted reservations and no store room, German 0 left, disk and English 4); the equipment
 * search has no such step */
static void test_german_furniture_reservations(void) {
    for (int ed = 0; ed < 3; ed++) {
        fresh();
        g_prog.edition = (Edition)ed;
        static const int AT[4] = {0, 7, 150, 299};
        for (int k = 0; k < 4; k++) offer(FM(AT[k]), DEALER, 3, 8, 50), FM(AT[k])[5] = 0xFF;
        offer(EM(5), DEALER, 3, 4, 50), EM(5)[5] = 0xFF;
        counts(0)[4] = 0;
        CHECK(purchase_search(s, 0) == PU_NO_STORE_BUY);
        int left = 0;
        for (int k = 0; k < 4; k++) left += FM(AT[k])[5] == 0xFF;
        CHECK(left == (ed == EDITION_GERMAN_CD ? 0 : 4));
        for (int k = 0; k < 4; k++) FM(AT[k])[5] = 0xFF;
        CHECK(purchase_search(s, 1) == PU_NO_STORE_BUY && EM(5)[5] == 0xFF);
        CHECK(FM(0)[5] == 0xFF && FM(299)[5] == 0xFF); /* the equipment search frees no furniture offer */
        counts(0)[4] = 1; /* with a store room the search reserves the first match again */
        for (int k = 0; k < 4; k++) FM(AT[k])[5] = 0xFF;
        CHECK(purchase_search(s, 0) == (ed == EDITION_GERMAN_CD ? 0 : PU_NO_FURNITURE_OFFER));
        if (ed == EDITION_GERMAN_CD) CHECK(P(PU_INDEX) == 0 && FM(0)[5] == 0xFF && FM(7)[5] == 0);
    }
    g_prog.edition = EDITION_DISK;
}

/* SIM-76 (docs/ui-purchasing.md): close only the first gap, and leave a last-slot gap's condition alone.
 * Enumerate the gap position, including full lists, with sentinel bytes beyond each list. */
static void test_close_gap_boundaries(void) {
    for (int n = 0; n <= STORE_SLOTS; n++) {
        for (int gap = 0; gap <= n; gap++) {
            uint8_t ids[2 * (STORE_SLOTS + 1)], cond[STORE_SLOTS + 1];
            uint8_t expected_ids[sizeof ids], expected_cond[sizeof cond];
            for (int i = 0; i <= STORE_SLOTS; i++) put16(ids + 2 * i, 300 + i), cond[i] = (uint8_t)(100 + i);
            if (gap < n) put16(ids + 2 * gap, 0);
            /* A second gap proves one call shifts once, rather than packing the list. */
            if (gap + 2 < n) put16(ids + 2 * (gap + 2), 0);
            memcpy(expected_ids, ids, sizeof ids), memcpy(expected_cond, cond, sizeof cond);
            if (gap < n - 1) {
                for (int i = gap; i < n - 1; i++) {
                    put16(expected_ids + 2 * i, get16(ids + 2 * (i + 1)));
                    expected_cond[i] = cond[i + 1];
                }
                put16(expected_ids + 2 * (n - 1), 0), expected_cond[n - 1] = 0;
            }
            sim_close_gap(ids, cond, n);
            CHECK(!memcmp(ids, expected_ids, sizeof ids) && !memcmp(cond, expected_cond, sizeof cond));
        }
    }
}

/* Both list kinds reach the shared helper through the store tidy-up and the room-wide sweep. */
static void test_tidy_last_slot(void) {
    fresh();
    static const int cats[] = {4, 5, 9};
    for (int c = 0; c < 3; c++) {
        Description d = sim_description(s, cats[c]);
        uint8_t *r = sim_room_record(s, 0, cats[c], 0);
        for (int i = 0; i < d.furniture_slots - 1; i++) put16(r + d.furniture + 2 * i, 4);
        for (int i = 0; i < d.equipment_slots - 1; i++) put16(r + d.equipment + 2 * i, 4);
        r[d.cond_a + d.furniture_slots - 1] = 99;
        r[d.cond_b + d.equipment_slots - 1] = 100;
    }
    sim_store_tidy(s);
    CHECK(S()[STORE_FURNITURE_COND + STORE_SLOTS - 1] == 99);
    CHECK(S()[STORE_EQUIPMENT_COND + STORE_SLOTS - 1] == 100);
    sim_tidy_lists(s);
    for (int c = 0; c < 3; c++) {
        Description d = sim_description(s, cats[c]);
        uint8_t *r = sim_room_record(s, 0, cats[c], 0);
        CHECK(r[d.cond_a + d.furniture_slots - 1] == 99 && r[d.cond_b + d.equipment_slots - 1] == 100);
        CHECK(!get16(r + d.furniture + 2 * (d.furniture_slots - 1)));
        CHECK(!get16(r + d.equipment + 2 * (d.equipment_slots - 1)));
    }
}

/* the store tidy-up's meal pass: the first 119 slots (disk and German), the first 118 on the English CD */
static void test_meal_pass_slots(void) {
    for (int ed = 0; ed < 3; ed++) {
        fresh();
        g_prog.edition = (Edition)ed;
        for (int i = 0; i < 120; i++) put16(S() + STORE_EQUIPMENT + 2 * i, 4), S()[STORE_EQUIPMENT_COND + i] = 50;
        put16(S() + STORE_EQUIPMENT + 2 * 118, 6), S()[STORE_EQUIPMENT_COND + 118] = 70; /* a meal in slot 118 */
        sim_store_tidy(s);
        int en = ed == EDITION_ENGLISH_CD;
        CHECK(id_at(S(), STORE_EQUIPMENT, 118) == (en ? 6 : 4) && id_at(S(), STORE_EQUIPMENT, 119) == (en ? 4 : 6));
        CHECK(S()[STORE_EQUIPMENT_COND + 119] == (en ? 50 : 70));
    }
    g_prog.edition = EDITION_DISK;
}

/* FIX-13 (docs/fixes.md): store slot 3 holds equipment 5 at condition 80, the byte S + 5 is 0: off "0 %", on 80 */
static void test_fix13_store_condition(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 12 : 0);
        fresh();
        put16(S() + STORE_EQUIPMENT + 2 * 3, 5), S()[STORE_EQUIPMENT_COND + 3] = 80;
        S()[5] = 0;
        purchase_inventory_button(s, 9, 0);
        purchase_inventory_open(s);
        purchase_toggle_view(s);
        purchase_select(s, 3);
        int c = -1;
        CHECK(P(PU_VIEW) == PU_STORE_VIEW && P(PU_EQUIPMENT) == 1);
        CHECK(purchase_info(s, &c) == 5 && c == (on ? 80 : 0));
    }
    fixes_set(0);
}

/* FIX-14 (docs/fixes.md): the store view's page-down: capacity 40 off 0 ... 40, on 0 ... 32; capacity 120 off up to
 * 120, on up to 112 */
static void test_fix14_store_paging(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 13 : 0);
        for (int cap = 40; cap <= 120; cap += 80) {
            fresh();
            purchase_inventory_button(s, 9, 0);
            purchase_inventory_open(s);
            CHECK(purchase_toggle_view(s) && P(PU_VIEW) == PU_STORE_VIEW);
            S()[STORE_CAPACITY] = (uint8_t)cap;
            int last = 0, n = 0;
            while (purchase_page_down(s) && n < 40) last = P(PU_INDEX), n++;
            CHECK(last == (on ? cap - 8 : cap) && n == (on ? cap / 8 - 1 : cap / 8));
        }
    }
    fixes_set(0);
}

/* FIX-15 (docs/fixes.md): 70 equipment items in slots 0-69: off +9 = 60, on 70; 40 items in slots 0-39: 40 both */
static void test_fix15_recount(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 14 : 0);
        fresh();
        for (int k = 0; k < 70; k++) put16(S() + STORE_EQUIPMENT + 2 * k, 1 + k % 5);
        put16(S() + STORE_FURNITURE + 2 * 3, 0x0101); /* an id above 255: off counts twice, on once */
        purchase_recount(s);
        CHECK(S()[STORE_EQUIPMENT_HELD] == (on ? 70 : 60) && S()[STORE_FURNITURE_HELD] == (on ? 1 : 2));
        fresh();
        for (int k = 0; k < 40; k++) put16(S() + STORE_EQUIPMENT + 2 * k, 1 + k % 5);
        purchase_recount(s);
        CHECK(S()[STORE_EQUIPMENT_HELD] == 40);
    }
    fixes_set(0);
}

/* FIX-16 (docs/fixes.md): the offer's price byte for price class 600: off 45, on 255; 300: 151 and 508: 255 in both
 * (the helper and the player's sale) */
static void test_fix16_price_byte(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 15 : 0);
        CHECK(sim_offer_price(600) == (on ? 255 : 45) && sim_offer_price(300) == 151 && sim_offer_price(508) == 255);
        CHECK(sim_offer_price(0) == 1 && sim_offer_price(98) == 50);
        fresh();
        put16(S() + STORE_EQUIPMENT, 6), S()[STORE_EQUIPMENT_COND] = 40;
        S()[STORE_EQUIPMENT_HELD] = 1;
        put16(EQ(6) + 8, 600);
        CHECK(purchase_sell_open(s, 1) == 0);
        PurchaseResult o;
        purchase_select(s, 0);
        CHECK(purchase_sell(s, &o) == PU_OFFERED && o.price == (on ? 255 : 45) && EM(0)[1] == (on ? 255 : 45));
    }
    fixes_set(0);
}

/* FIX-17 (docs/fixes.md): the AI's equipment sale, item of price class 98; the following offer's bytes 0-1 = 16: off
 * price 9, on 50; into the last offer slot: off 1, on 50 */
static void test_fix17_ai_equipment_price(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 16 : 0);
        for (int last = 0; last < 2; last++) {
            fresh();
            put16(EQ(5) + 8, 98);
            uint8_t *rec = sim_room_record(s, 3, 5, 0);
            for (int i = 0; i < 4; i++) put16(rec + 0x88 + 2 * i, 5), rec[0x94 + i] = 70;
            uint8_t *mk = g_world.block[BLK_EQUIPMENT_MARKET];
            int slot = last ? 299 : 0;
            if (last)
                for (int i = 0; i < 299; i++) mk[i * 8] = 1, mk[i * 8 + 1] = 7;
            else
                mk[8] = 0x10, mk[9] = 0; /* the following offer's u16 = 16 */
            CHECK(sim_equipment_room(s, 3, 5, rec) == 1);
            const uint8_t *o = mk + slot * 8;
            CHECK(o[0] == 3 && get16(o + 2) == 5 && o[4] == 70);
            CHECK(o[1] == (on ? 50 : last ? 1 : 9));
        }
    }
    fixes_set(0);
}

int main(void) {
    test_fix13_store_condition();
    test_fix14_store_paging();
    test_fix15_recount();
    test_fix16_price_byte();
    test_fix17_ai_equipment_price();
    test_search_refusals();
    test_search_filters();
    test_step();
    test_buy_equipment();
    test_buy_furniture();
    test_buy_refusal();
    test_buy_own();
    test_refund();
    test_buy_overflow();
    test_sell_open();
    test_sell();
    test_paging();
    test_lists();
    test_toggles();
    test_inventory_buttons();
    test_move_to_room();
    test_move_to_store();
    test_move_tidy_twice();
    test_move_refusals();
    test_recount();
    test_german_furniture_reservations();
    test_meal_pass_slots();
    test_close_gap_boundaries();
    test_tidy_last_slot();
    world_free(&g_world);
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] all checks passed\n");
    return 0;
}
