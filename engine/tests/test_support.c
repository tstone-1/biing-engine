/* The player's support rooms and administration extras (docs/ui-support.md) and the thug missions 0x4A822. Synthetic
 * world and program image (see AGENTS.md): recipe levels, car list prices, the injury tables and staff values are
 * made up, except where a case repeats one of the spec's oracle runs (the campaign costs 30 / 50 / 60 / 40 / 93, the
 * start from cash 984, the Meddwurst of list price 280 at condition 70 offered for 250, cash = price refused). */
#include <stdio.h>
#include <string.h>

#include "fixes.h"
#include "rooms.h"
#include "sound.h"
#include "sim_internal.h"
#include "support.h"

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

static uint8_t *H(int h) { return world_hospital(&g_world, h); }
static uint8_t *pb(void) { return g_world.block[BLK_PLAYER0]; }
static int pb16(int off) { return get16(pb() + off); }
static int u16(const uint8_t *p) { return (uint16_t)get16(p); }
static int32_t cash(int h) { return get32(H(h) + H_CASH); }
static uint8_t *site(int h) { return g_world.block[BLK_ROOM_COUNTS] + h * 100; }
static uint8_t *staff(int i) { return g_world.block[BLK_STAFF] + i * 32; }
static uint8_t *slot(int h, int i) { return staff_slots(s, h) + i * STAFF_SLOT_SIZE; }
static uint8_t *person(int i) { return g_world.block[BLK_PATIENTS] + i * 106; }
static int next_bits(uint32_t mask) { /* what the next draw of the rules will give */
    Sim twin = g_sim;
    return (int)sim_bits(&twin, mask);
}

static void fresh(void) {
    world_free(&g_world);
    world_alloc(&g_world);
    memset(g_world.block[BLK_ROOM_SLOTS], 0xFF, g_world.size[BLK_ROOM_SLOTS]);
    memset(g_image, 0, sizeof g_image);
    g_prog.image.data = g_image;
    g_prog.image.size = sizeof g_image;
    sim_init(&g_sim, &g_world, &g_prog, 7);
    put32(H(0) + H_CASH, 984);
    world_set_tail(&g_world, TAIL_HOUR, 12);
    for (int i = 0; i < 20; i++) g_world.block[BLK_CARS][i * 20] = 0xFF;
}

/* ---- kitchen -------------------------------------------------------------------------------------------------- */

static uint8_t *K(void) { return support_kitchen(s); }
static void recipe(int i, int level) { g_image[0xAD90 + 8 * i] = (uint8_t)level; }

static void kitchen(void) {
    fresh();
    for (int i = 0; i < 33; i++) recipe(i, 9); /* no recipe of these levels */
    recipe(0, 0), recipe(1, 1), recipe(8, 2), recipe(9, 2), recipe(22, 1), recipe(15, 5), recipe(30, 3);
    site(0)[13] = 1;
}
static void cook(int level) { /* group 4: (256 + bust + skill + u16 16 / 2) / 128 */
    uint8_t *c = staff(40);
    c[1] = 4, c[5] = 0, c[15] = 0;
    put16(c + 16, (level * 128 - 256) * 2);
    put16(K() + KR_COOK, 40);
}

static void test_kitchen(void) {
    kitchen();
    cook(2);
    CHECK(sim_cook_level(s, 40) == 2);
    put16(K() + KR_MEAL, 55), put16(K() + KR_LEFT, 7);
    CHECK(support_dearer(s) == 1 && u16(K() + KR_LEVEL) == 1 && K()[KR_RECIPE] == 1);
    CHECK(u16(K() + KR_MEAL) == 0 && u16(K() + KR_LEFT) == 0); /* the dish in progress dropped */
    CHECK(support_dearer(s) == 1 && u16(K() + KR_LEVEL) == 2 && K()[KR_RECIPE] == 8);
    put16(K() + KR_MEAL, 55);
    CHECK(support_dearer(s) == 0 && u16(K() + KR_LEVEL) == 2 && K()[KR_RECIPE] == 8 && u16(K() + KR_MEAL) == 55);
    support_next_dish(s);
    CHECK(K()[KR_RECIPE] == 9 && u16(K() + KR_MEAL) == 0);
    support_next_dish(s);
    CHECK(K()[KR_RECIPE] == 8); /* round over 32 -> 0 */
    K()[KR_RECIPE] = 9;
    CHECK(support_cheaper(s) == 1 && u16(K() + KR_LEVEL) == 1 && K()[KR_RECIPE] == 22);
    CHECK(support_cheaper(s) == 1 && u16(K() + KR_LEVEL) == 0 && K()[KR_RECIPE] == 0);
    put16(K() + KR_MEAL, 55);
    CHECK(support_cheaper(s) == 0 && u16(K() + KR_MEAL) == 55 && K()[KR_RECIPE] == 0); /* level 0: nothing */
    /* a cook of level 5 stops at 5 */
    cook(5);
    for (int i = 0; i < 8; i++) support_dearer(s);
    CHECK(u16(K() + KR_LEVEL) == 5 && K()[KR_RECIPE] == 15);
    /* without a cook both buttons set level 0 */
    put16(K() + KR_COOK, 0);
    put16(K() + KR_LEVEL, 3), K()[KR_RECIPE] = 30;
    CHECK(support_dearer(s) == 1 && u16(K() + KR_LEVEL) == 0 && K()[KR_RECIPE] == 0);
    put16(K() + KR_LEVEL, 3), K()[KR_RECIPE] = 30;
    CHECK(support_cheaper(s) == 1 && u16(K() + KR_LEVEL) == 0 && K()[KR_RECIPE] == 0);
    /* engine choice: a level with no recipe ends after one round on the recipe it started from */
    put16(K() + KR_LEVEL, 7), K()[KR_RECIPE] = 4;
    support_recipe_search(s);
    CHECK(K()[KR_RECIPE] == 4);
    /* the search compares the level's low byte */
    put16(K() + KR_LEVEL, 0x102), K()[KR_RECIPE] = 0;
    support_recipe_search(s);
    CHECK(K()[KR_RECIPE] == 8);
    /* the switch */
    put16(K() + KR_MEAL, 55), put16(K() + KR_LEFT, 7);
    support_cooking(s);
    CHECK(K()[KR_COOKING] == 0xFF && u16(K() + KR_MEAL) == 0 && u16(K() + KR_LEFT) == 0);
    support_cooking(s);
    CHECK(K()[KR_COOKING] == 0);
    /* the utensils end at a negative byte */
    uint8_t *rc = g_image + 0xAD90 + 8 * 15;
    rc[4] = 1, rc[5] = 2, rc[6] = 5, rc[7] = 0xFF;
    int u[4];
    CHECK(support_utensils(s, 15, u) == 3 && u[0] == 1 && u[1] == 2 && u[2] == 5);
    rc[7] = 4;
    CHECK(support_utensils(s, 15, u) == 4 && u[3] == 4);
    rc[4] = 0x80;
    CHECK(support_utensils(s, 15, u) == 0);
    CHECK(support_utensils(s, 33, u) == 0);
}

/* the switch alone makes the player's kitchen cook in 0x6EDA1 */
static void test_kitchen_switch(void) {
    kitchen();
    cook(2);
    K()[KR_RECIPE] = 8;
    put16(K() + KR_LEVEL, 2);
    put16(K() + KR_LEFT, 5), put16(K() + KR_MEAL, 3);
    sim_call(s, 0x6EDA1);
    CHECK(u16(K() + KR_LEFT) == 5);
    support_cooking(s);
    put16(K() + KR_LEFT, 5), put16(K() + KR_MEAL, 3);
    sim_call(s, 0x6EDA1);
    CHECK(u16(K() + KR_LEFT) == 4);
}

/* ---- golf -------------------------------------------------------------------------------------------------------- */

static void test_golfers(void) {
    fresh();
    uint8_t *g = sim_room_record(s, 0, 12, 0);
    int out[64];
    CHECK(support_golfers(s, out, 64) == 0);
    put16(g + GR_GOLFERS + 2 * 1, 11), put16(g + GR_GOLFERS + 2 * 4, 22), put16(g + GR_GOLFERS + 2 * 8, 33);
    g[GR_PRESENT] = 1;
    CHECK(support_golfers(s, out, 64) == 1 && out[0] == 11);
    g[GR_PRESENT] = 2;
    CHECK(support_golfers(s, out, 64) == 2 && out[0] == 11 && out[1] == 22);
    g[GR_PRESENT] = 3;
    CHECK(support_golfers(s, out, 64) == 3 && out[2] == 33);
    /* +5 larger than the list: the walk reads on past the nine entries (kept) */
    put16(g + GR_GOLFERS + 2 * 12, 44);
    g[GR_PRESENT] = 4;
    CHECK(support_golfers(s, out, 64) == 4 && out[3] == 44);
    /* engine choice: it stops at the record's end */
    g[GR_PRESENT] = 9;
    CHECK(support_golfers(s, out, 64) == 4);
    put16(g + 116, 55);
    CHECK(support_golfers(s, out, 64) == 5 && out[4] == 55);
    CHECK(support_golfers(s, out, 2) == 2);
}

/* ---- advertising --------------------------------------------------------------------------------------------- */

static void test_campaign(void) {
    fresh();
    uint8_t *r = H(0);
    CHECK(support_campaign_cost(1, 0, 20) == 30 && support_campaign_cost(2, 0, 20) == 50);
    CHECK(support_campaign_cost(3, 0, 20) == 60 && support_campaign_cost(1, 1, 20) == 40);
    CHECK(support_campaign_cost(1, 0, 62) == 93);
    CHECK(support_campaign_cost(0, 0, 20) == 50 && support_campaign_cost(7, 1, 20) == 60); /* other types count as 2 */
    CHECK(support_campaign_cost(3, 11, 4000) == ((17 * 4000) & 0xFFFF) >> 1); /* 16 bits */
    put16(r + AD_GOOD, 9), put16(r + AD_LEFT, 4);
    CHECK(support_campaign_open(s) == 119);
    CHECK(r[AD_TYPE] == 1 && r[AD_SLOGAN] == 0 && u16(r + AD_DURATION) == 20 && u16(r + AD_COST) == 30);
    CHECK(u16(r + AD_LEFT) == 0 && u16(r + AD_GOOD) == 0);
    support_campaign_type(s);
    CHECK(r[AD_TYPE] == 2 && u16(r + AD_COST) == 50);
    support_campaign_type(s);
    CHECK(r[AD_TYPE] == 3 && u16(r + AD_COST) == 60);
    support_campaign_type(s);
    CHECK(r[AD_TYPE] == 1 && u16(r + AD_COST) == 30); /* 4 becomes 1 */
    support_campaign_slogan(s);
    CHECK(r[AD_SLOGAN] == 1 && u16(r + AD_COST) == 40);
    r[AD_SLOGAN] = 11;
    support_campaign_slogan(s);
    CHECK(r[AD_SLOGAN] == 0 && u16(r + AD_COST) == 30); /* 12 becomes 0 */
    for (int i = 0; i < 3; i++) support_campaign_shorter(s);
    CHECK(u16(r + AD_DURATION) == 11);
    support_campaign_shorter(s);
    CHECK(u16(r + AD_DURATION) == 8 && u16(r + AD_COST) == 12);
    support_campaign_shorter(s);
    CHECK(u16(r + AD_DURATION) == 8); /* below 10: nothing */
    put16(r + AD_DURATION, 9), support_campaign_shorter(s);
    CHECK(u16(r + AD_DURATION) == 9);
    put16(r + AD_DURATION, 10), support_campaign_shorter(s);
    CHECK(u16(r + AD_DURATION) == 7);
    put16(r + AD_DURATION, 20);
    for (int i = 0; i < 20; i++) support_campaign_longer(s);
    CHECK(u16(r + AD_DURATION) == 62 && u16(r + AD_COST) == 93);
    put16(r + AD_DURATION, 60), support_campaign_longer(s);
    CHECK(u16(r + AD_DURATION) == 60);
    put16(r + AD_DURATION, 59), support_campaign_longer(s);
    CHECK(u16(r + AD_DURATION) == 62);
    /* start: no cash test */
    r[AD_COOL] = 30;
    CHECK(support_campaign_start(s) == SU_AD_SIGN);
    CHECK(cash(0) == 891 && get32(r + AD_DAY) == 93 && get32(r + AD_TOTAL) == 93 && u16(r + AD_LEFT) == 62);
    CHECK(r[AD_COOL] == 30);
    r[AD_TYPE] = 2;
    CHECK(support_campaign_start(s) == SU_AD_BANNER);
    r[AD_TYPE] = 3;
    CHECK(support_campaign_start(s) == SU_AD_LEAFLETS);
    r[AD_TYPE] = 0;
    CHECK(support_campaign_start(s) == SU_AD_BANNER);
    /* a campaign exists: 121, nothing changed */
    r[AD_TYPE] = 1;
    CHECK(support_campaign_open(s) == 121 && u16(r + AD_DURATION) == 62);
    /* the counts and the frame's test */
    put16(r + AD_GOOD, 44), put16(r + AD_NEUTRAL, 84), put16(r + AD_BAD, 73);
    CHECK(support_campaign_changed(s));
    CHECK(support_campaign_reached(s) == 201 && u16(r + AD_SHOWN) == 201);
    CHECK(!support_campaign_changed(s));
    put16(r + AD_SHOWN, 0xFFFF);
    CHECK(support_campaign_changed(s)); /* the end (+0x106 = -1) redraws once */
    /* the upper thumb of 121: running -> kept; ended -> cleared, +0x106 and +0x108 kept */
    support_campaign_reached(s);
    put16(r + AD_LEFT, 0);
    support_campaign_confirm(s);
    CHECK(r[AD_TYPE] == 1);
    put16(r + AD_LEFT, -1);
    support_campaign_confirm(s);
    CHECK(r[AD_TYPE] == 0 && u16(r + AD_GOOD) == 0 && u16(r + AD_BAD) == 0 && u16(r + AD_DURATION) == 0);
    CHECK(u16(r + AD_SHOWN) == 201 && r[AD_COOL] == 30 && get32(r + AD_TOTAL) == 93 + 3 * 93);
    /* discard */
    CHECK(support_campaign_open(s) == 119);
    r[0x102 - 1] = 0;
    put16(r + AD_BAD, 5);
    uint8_t after = r[0x102];
    support_campaign_discard(s);
    for (int i = AD_TYPE; i <= 0x101; i++) CHECK(r[i] == 0);
    CHECK(r[0x102] == after);
    /* start with cash 5 */
    support_campaign_open(s);
    put32(r + H_CASH, 5);
    support_campaign_start(s);
    CHECK(cash(0) == -25);
    /* 0x4E368 runs the player's campaign like the AI's */
    world_set_tail(&g_world, TAIL_HOUR, 3);
    sim_call(s, 0x4E368);
    CHECK(u16(r + AD_LEFT) == 19);
}

/* ---- vehicle market ------------------------------------------------------------------------------------------ */

static uint8_t *O(int i) { return support_car_offer(s, i); }
static uint8_t *G(void) { return support_garage(s); }
static void offer(int i, int seller, int car, int price, int cond) {
    O(i)[CO_SELLER] = (uint8_t)seller, O(i)[CO_CAR] = (uint8_t)car;
    put16(O(i) + CO_PRICE, price);
    O(i)[CO_CONDITION] = (uint8_t)cond, O(i)[CO_AGE] = 3;
}
static void market(void) {
    fresh();
    for (int id = 0; id < 7; id++) put16(g_image + CAR_CATALOG + 20 * id + 6, 100 + 100 * id);
    put16(g_image + CAR_CATALOG + 20 + 6, 280); /* the Meddwurst */
    site(0)[10] = 1;
    G()[GA_PLACES] = 2;
}

static void test_buy(void) {
    CarResult o;
    market();
    site(0)[10] = 0;
    CHECK(support_buy_open(s) == SU_NO_GARAGE);
    site(0)[10] = 1;
    put16(g_world.block[BLK_34], 5);
    CHECK(support_buy_open(s) == SU_RACE);
    put16(g_world.block[BLK_34], 0);
    CHECK(support_buy_open(s) == SU_NO_OFFER && pb16(SU_P_SHOWN) == -1);
    offer(3, DEALER, 1, 250, 70);
    offer(7, 5, 2, 999, 60);
    offer(12, 0, 4, 10, 40);
    CHECK(support_buy_open(s) == 0 && pb16(SU_P_SHOWN) == 3);
    CHECK(support_buy_next(s) == 0 && pb16(SU_P_SHOWN) == 7);
    CHECK(support_buy_next(s) == 0 && pb16(SU_P_SHOWN) == 12);
    CHECK(support_buy_next(s) == 0 && pb16(SU_P_SHOWN) == 3); /* 20 becomes 0 */
    /* offer 7 at 999: refused with cash 984 */
    put16(pb() + SU_P_SHOWN, 7);
    CHECK(support_buy(s, &o) == SU_NO_CASH && (int8_t)O(7)[0] == 5 && cash(0) == 984);
    /* bought from the dealer */
    put16(pb() + SU_P_SHOWN, 3);
    int32_t dealer = cash(DEALER);
    CHECK(support_buy(s, &o) == SU_BOUGHT && o.price == 250 && o.car == 1);
    CHECK(cash(0) == 734 && get32(H(0) + 0x120) == 250 && get32(H(0) + 0x13A) == 250);
    CHECK(cash(DEALER) == dealer + 250 && get32(H(DEALER) + 0x124) == 250 && get32(H(DEALER) + 0x12E) == 250);
    CHECK(G()[GA_PLACE] == 2 && G()[GA_PLACE + 1] == 70 && G()[GA_PLACE + 2] == 0 && G()[GA_CARS] == 1);
    CHECK(O(3)[0] == 0xFF);
    /* one's own offer: free, 2749; the second place */
    put16(pb() + SU_P_SHOWN, 12);
    CHECK(support_buy(s, &o) == SU_OWN_OFFER && cash(0) == 734 && get32(H(0) + 0x120) == 250);
    CHECK(G()[GA_PLACE + 4] == 5 && G()[GA_PLACE + 5] == 40 && G()[GA_CARS] == 2 && O(12)[0] == 0xFF);
    /* the garage full */
    offer(5, 6, 0, 10, 50);
    put16(pb() + SU_P_SHOWN, 5);
    CHECK(support_buy(s, &o) == SU_GARAGE_FULL && (int8_t)O(5)[0] == 6);
    /* engine choice: places free by the counts but none empty -> 977 (the original loops for ever) */
    G()[GA_PLACES] = 3;
    for (int k = 0; k < 10; k++) G()[GA_PLACE + 4 * k] = 1;
    CHECK(support_buy(s, &o) == SU_GARAGE_FULL && (int8_t)O(5)[0] == 6);
    /* cash = price refused, price + 1 bought (cash 1); a hospital seller is paid */
    G()[GA_PLACE + 4 * 9] = 0;
    put32(H(0) + H_CASH, 10);
    CHECK(support_buy(s, &o) == SU_NO_CASH);
    put32(H(0) + H_CASH, 11);
    int32_t six = cash(6);
    CHECK(support_buy(s, &o) == SU_BOUGHT && cash(0) == 1 && cash(6) == six + 10 && G()[GA_PLACE + 36] == 1);
    /* the counts' difference is 8-bit */
    G()[GA_PLACES] = 1, G()[GA_CARS] = 1;
    CHECK(support_buy(s, &o) == SU_GARAGE_FULL);
    /* next with no offer in 60 steps */
    for (int i = 0; i < 20; i++) O(i)[0] = 0xFF;
    CHECK(support_buy_next(s) == SU_NO_OFFER);
    offer(19, DEALER, 0, 5, 50);
    put16(pb() + SU_P_SHOWN, 19);
    CHECK(support_buy_next(s) == 0 && pb16(SU_P_SHOWN) == 19); /* the only offer found again */
}

static void test_sell(void) {
    CarResult o;
    market();
    site(0)[10] = 0;
    CHECK(support_sell_open(s) == SU_SELL_NO_GARAGE);
    site(0)[10] = 1;
    put16(g_world.block[BLK_34], 1);
    CHECK(support_sell_open(s) == SU_NO_CAR); /* no car before the race */
    G()[GA_CARS] = 1;
    CHECK(support_sell_open(s) == SU_RACE);
    put16(g_world.block[BLK_34], 0);
    CHECK(support_sell_open(s) == SU_NO_CAR); /* no place filled */
    uint8_t *p = G() + GA_PLACE;
    p[4 * 2] = 2, p[4 * 2 + 1] = 70, p[4 * 2 + 2] = 1; /* byte 2 set: not for sale */
    CHECK(support_sell_open(s) == SU_NO_CAR);
    p[4 * 5] = 2, p[4 * 5 + 1] = 70;
    p[4 * 7] = 4, p[4 * 7 + 1] = 20; /* below 25: shown by the entry, skipped by "next" */
    p[4 * 8] = 6, p[4 * 8 + 1] = 25;
    G()[GA_CARS] = 4;
    CHECK(support_sell_open(s) == 0 && pb16(SU_P_SHOWN) == 5);
    CHECK(support_sell_next(s) == 0 && pb16(SU_P_SHOWN) == 8);
    CHECK(support_sell_next(s) == 0 && pb16(SU_P_SHOWN) == 5);
    put16(pb() + SU_P_SHOWN, 6);
    CHECK(support_sell_next(s) == 0 && pb16(SU_P_SHOWN) == 8);
    /* engine choice: no car qualifies -> stays */
    p[4 * 5 + 1] = 10, p[4 * 8 + 1] = 0x90; /* condition read signed */
    put16(pb() + SU_P_SHOWN, 7);
    CHECK(support_sell_next(s) == 0 && pb16(SU_P_SHOWN) == 7);
    p[4 * 5 + 1] = 70, p[4 * 8 + 1] = 25;
    G()[GA_CARS] = 0;
    CHECK(support_sell_next(s) == SU_NO_CARS);
    G()[GA_CARS] = 4;
    /* offer for sale: the Meddwurst (list 280) at 70 -> 250 */
    put16(pb() + SU_P_SHOWN, 5);
    O(0)[0] = 3;
    O(1)[5] = 9; /* a stale age in the free offer */
    CHECK(support_sell(s, &o) == SU_OFFERED && o.price == 250);
    CHECK(O(1)[0] == 0 && O(1)[1] == 1 && u16(O(1) + 2) == 250 && O(1)[4] == 70 && O(1)[5] == 0);
    CHECK(p[4 * 5] == 0 && p[4 * 5 + 1] == 0 && G()[GA_CARS] == 3 && cash(0) == 984);
    CHECK(support_car_sale_price(280, 70) == 250 && support_car_sale_price(1650, 37) == 1650 - 16 * 63 / 2);
    CHECK(support_car_sale_price(99, 0) == 99);
    for (int i = 0; i < 20; i++) O(i)[0] = 1;
    put16(pb() + SU_P_SHOWN, 8);
    CHECK(support_sell(s, &o) == SU_MARKET_FULL && p[4 * 8] == 6 && G()[GA_CARS] == 3);
}

/* ---- thugs: the screen ------------------------------------------------------------------------------------------ */

static void thug(int sl, int index, int state) {
    put16(slot(0, sl), index);
    slot(0, sl)[2] = 8, slot(0, sl)[3] = (uint8_t)state;
    staff(index)[1] = 8, staff(index)[4] = (uint8_t)state;
}

static void test_thug_screen(void) {
    fresh();
    CHECK(support_thugs_open(s) == SU_NO_THUG && pb16(SU_P_GROUP) == 8);
    int busy[] = {0x13, 0x14, 0x15, 0x16, 0x19};
    for (int i = 0; i < 5; i++) thug(10 + i, 100 + i, busy[i]);
    CHECK(support_thugs_open(s) == SU_NO_THUG);
    thug(20, 120, 0x18);
    put16(slot(0, 30), 130), slot(0, 30)[2] = 2; /* a doctor */
    thug(40, 140, 0x17);
    thug(50, 0, 0); /* no staff index */
    slot(0, 50)[2] = 8;
    CHECK(support_thugs_open(s) == 0 && pb16(SU_P_SLOT) == 20);
    CHECK(support_thug_step(s, 1) == 0 && pb16(SU_P_SLOT) == 40);
    CHECK(support_thug_step(s, 1) == 0 && pb16(SU_P_SLOT) == 20); /* 200 becomes 0 */
    CHECK(support_thug_step(s, -1) == 0 && pb16(SU_P_SLOT) == 40); /* -1 becomes 199 */
    thug(0, 150, 0);
    put16(pb() + SU_P_SLOT, 199);
    CHECK(support_thug_step(s, 1) == 0 && pb16(SU_P_SLOT) == 0);
    CHECK(support_thug_step(s, -1) == 0 && pb16(SU_P_SLOT) == 40);
    put16(pb() + SU_P_GROUP, 2); /* the group comes from pb+0x6C */
    CHECK(support_thug_step(s, 1) == 0 && pb16(SU_P_SLOT) == 30);
    put16(pb() + SU_P_GROUP, 9);
    CHECK(support_thug_step(s, 1) == -1);
}

static void test_thug_send(void) {
    fresh();
    for (int r = 0; r < 3; r++)
        for (int t = 0; t < 8; t++) g_image[0x5FDB + 8 * r + t] = (uint8_t)(r + 3 * (t & 1));
    g_image[0xA8E1] = 5, g_image[0xA8E2] = 11, g_image[0xA8E3] = 15;
    thug(3, 77, 0);
    slot(0, 3)[4] = 9, staff(77)[26] = 9, slot(0, 3)[7] = 9, put16(slot(0, 3) + 12, 99);
    site(0)[15] = 1; /* only a neurology: r = 2, injury 2 or 5 */
    put16(pb() + SU_P_SLOT, 3);
    support_thug_send(s);
    uint8_t *sl = slot(0, 3);
    CHECK(sl[3] == 0x14 && staff(77)[4] == 0x14 && sl[0x0E] == 30 && (sl[0x0F] == 2 || sl[0x0F] == 5));
    CHECK(sl[4] == 0 && staff(77)[26] == 0 && sl[7] == 0 && u16(sl + 12) == 0);
    site(0)[15] = 0, site(0)[5] = 1;
    int both[4] = {0};
    for (int k = 0; k < 20; k++) {
        support_thug_send(s);
        CHECK(sl[0x0F] == 0 || sl[0x0F] == 3);
        both[sl[0x0F] & 3] = 1;
    }
    CHECK(both[0] && both[3]); /* the entry by t&7 */
    site(0)[5] = 0, site(0)[11] = 1;
    support_thug_send(s);
    CHECK(sl[0x0F] == 1 || sl[0x0F] == 4);
    /* the hours: 4 and 18 person missions, 3 and 19 staff missions */
    world_set_tail(&g_world, TAIL_HOUR, 4);
    support_thug_send(s);
    CHECK(sl[3] == 0x14);
    world_set_tail(&g_world, TAIL_HOUR, 18);
    support_thug_send(s);
    CHECK(sl[3] == 0x14);
    world_set_tail(&g_world, TAIL_HOUR, 19);
    support_thug_send(s);
    CHECK(sl[3] == 0x15 && staff(77)[4] == 0x15 && sl[0x0E] == 50 && sl[0x0F] < 4);
    world_set_tail(&g_world, TAIL_HOUR, 3);
    int seen[4] = {0};
    for (int k = 0; k < 200; k++) {
        support_thug_send(s);
        CHECK(sl[0x0F] < 4);
        if (sl[0x0F] < 4) seen[sl[0x0F]] = 1;
    }
    CHECK(seen[0] && seen[1] && seen[2] && seen[3]);
    /* the sound lists 0xA881 / 0xA8B1 only for the player's send ([0x5ABD] = 0); an AI send is silent */
    static Sound snd;
    sound_init(&snd, NULL, &g_prog, NULL, NULL);
    put16(g_image + 0xA881, 0x21), put16(g_image + 0xA8B1, 0x22);
    s->sound = &snd;
    sim_thug_send(s, 0, 3, 0);
    CHECK(!snd.queue[0][0].id && !snd.queue[1][0].id);
    support_thug_send(s);
    CHECK(snd.queue[0][0].id == 0x21 && snd.queue[1][0].id == 0x22);
    s->sound = NULL;
    sound_free(&snd);
}

/* ---- thugs: the missions 0x4A822 --------------------------------------------------------------------------------- */

static uint8_t *T(void) { return staff(77); }
static uint8_t *S(void) { return slot(0, 3); }

static void missions_world(int state) {
    fresh();
    for (int i = 0; i < 6; i++) {
        g_image[0x5FC3 + 4 * i] = (uint8_t)(11 + i), g_image[0x5FC3 + 4 * i + 1] = (uint8_t)(i), g_image[0x5FC3 + 4 * i + 2] = (uint8_t)(40 + i);
    }
    thug(3, 77, state);
    S()[0x0F] = 3;
    S()[0x0E] = state == 0x14 ? 30 : 50;
}

static int strength(void) { /* the thug's strength 0x4AE32 without the draw */
    return 16 * (T()[12] + T()[13]) + T()[15] + 2 * T()[5] + (uint8_t)(T()[9] - 140) + (uint8_t)(T()[10] - 45) + u16(T() + 16) -
           16 * S()[7] + (T()[3] ? 50 : 0);
}

static void test_strength(void) {
    missions_world(0x14);
    T()[12] = 2, T()[13] = 3, T()[15] = 40, T()[5] = 30, T()[9] = 150, T()[10] = 40, put16(T() + 16, 300);
    S()[7] = 2, T()[3] = 0;
    int d = next_bits(63);
    CHECK(sim_thug_strength(s, T(), S()) == 16 * 5 + 40 + 60 + 10 + 251 + 300 - 32 - d);
    T()[3] = 4;
    d = next_bits(63);
    CHECK(sim_thug_strength(s, T(), S()) == strength() - d);
    CHECK(strength() == 16 * 5 + 40 + 60 + 10 + 251 + 300 - 32 + 50);
    T()[9] = 130; /* a height below 140 adds about 250: an 8-bit difference */
    d = next_bits(63);
    CHECK(sim_thug_strength(s, T(), S()) == 16 * 5 + 40 + 60 + 246 + 251 + 300 - 32 + 50 - d);
}

static int run_until(int (*done)(void), int max) {
    for (int i = 0; i < max; i++) {
        sim_call(s, 0x4A822);
        if (done()) return i + 1;
    }
    return 0;
}
static int has_victim(void) { return u16(S() + 12) != 0; }
static void all_people(int qualify) {
    for (int i = 1; i < 1009; i++) {
        uint8_t *p = person(i);
        p[1] = 1, p[3] = 0xFF;
        p[104] = (uint8_t)(qualify ? 0 : 1);
        p[48] = 9, p[79] = p[41] = p[57] = p[105] = 0;
    }
}
static uint8_t *news_of(int type) {
    for (int i = 0; i < 100; i++) {
        uint8_t *it = sim_news_item(s, i);
        if (it[0] && it[20] == type) return it;
    }
    return NULL;
}

static void test_person_mission(void) {
    missions_world(0x14);
    CHECK(sim_call(s, 0x4A822) == 1);
    /* nobody qualifies: no victim, also when each test alone fails */
    static const int FIELD[] = {104, 1, 79, 41, 3, 57, 105};
    for (int f = 0; f < 7; f++) {
        all_people(1);
        for (int i = 1; i < 1009; i++) person(i)[FIELD[f]] = (uint8_t)(FIELD[f] == 1 ? 0 : FIELD[f] == 3 ? 2 : 1);
        CHECK(!run_until(has_victim, 2000));
    }
    all_people(1);
    pb()[0x9E] = 0;
    put16(T() + 16, 10);
    /* only with t&127 = 13 */
    int n = 0;
    while (next_bits(127) == 13) sim_bits(s, 127);
    while (next_bits(127) != 13 && n < 5000) { sim_call(s, 0x4A822), n++; CHECK(!has_victim()); }
    sim_call(s, 0x4A822);
    CHECK(has_victim() && n > 0);
    uint8_t *p = person(u16(S() + 12));
    CHECK(p[3] == 0 && p[4] == 2 && p[66] == 1 && p[44] >= 70 && p[44] <= 85);
    CHECK(p[56] == 14 && p[80] == 3 && p[41] == 43 && p[48] == 0);
    CHECK(u16(T() + 16) == 12 && pb()[0x9E] == 0xFF);
    /* the countdown: redraws at 20 and 10 only */
    for (int c = 29; c >= 1; c--) {
        pb()[0x9E] = 0;
        sim_call(s, 0x4A822);
        CHECK(S()[0x0E] == c && pb()[0x9E] == ((c == 20 || c == 10) ? 0xFF : 0));
    }
    int victim = u16(S() + 12);
    /* reaching 0 with no reception: the thug waits */
    sim_call(s, 0x4A822);
    sim_call(s, 0x4A822);
    CHECK(S()[0x0E] == 0 && u16(S() + 12) == victim && S()[3] == 0x14);
    /* a reception without clerk: the victim goes home, no news */
    site(0)[2] = 1;
    uint8_t *rec = sim_room_record(s, 0, 2, 0);
    sim_call(s, 0x4A822);
    CHECK(p[3] == 0xFF && p[4] == 0xFF && p[66] == 0 && S()[3] == 0 && T()[4] == 0 && u16(T() + 16) == 13);
    CHECK(u16(S() + 12) == 0 && !news_of(6) && !news_of(0x0B));
    /* a clerk, the queue full: nothing */
    put16(rec + 8, 500);
    for (int k = 0; k < 4; k++) put16(rec + 0x0A + 4 * k, 900 + k);
    S()[3] = 0x14, S()[0x0E] = 0, put16(S() + 12, victim), p[3] = 0, p[66] = 1;
    sim_call(s, 0x4A822);
    CHECK(S()[3] == 0x14 && u16(S() + 12) == victim);
    /* a strong thug: the victim into the free place */
    put16(rec + 0x0A + 4 * 2, 0), put16(rec + 0x0C + 4 * 2, 7);
    put16(T() + 16, 2000);
    sim_call(s, 0x4A822);
    CHECK(u16(rec + 0x0A + 8) == victim && u16(rec + 0x0C + 8) == 0 && u16(H(0) + 0xE6) == 1 && p[66] == 0);
    CHECK(S()[3] == 0 && u16(T() + 16) == 2003 && u16(S() + 12) == 0 && p[3] == 0);
    uint8_t *it = news_of(6);
    CHECK(it && it[21] == 0 && it[8] == 0 && u16(it + 10) == victim && u16(it + 12) == 0 && it[22] == 3 && it[5] == 0);
    CHECK(it[4] == 3 && it[6] == 0 && it[28] == 1 && it[9] == 0);
    /* a weak thug: custody, the victim home */
    put16(rec + 0x0A + 8, 0);
    put16(T() + 16, 0), T()[21] = 50, S()[0x0B] = 5;
    S()[3] = 0x14, S()[0x0E] = 0, put16(S() + 12, victim), p[3] = 0, p[66] = 1, p[2] = 3; /* 64 x field 2 */
    sim_call(s, 0x4A822);
    CHECK(S()[3] == 0x16 && T()[4] == 0x16 && S()[0x0A] >= 20 && S()[0x0A] <= 35 && T()[21] == 0 && S()[0x0B] == 0);
    CHECK(p[3] == 0xFF && p[66] == 0 && u16(H(0) + 0xF0) == 1 && u16(S() + 12) == 0 && u16(H(0) + 0xE6) == 1);
    it = news_of(0x0B);
    CHECK(it && it[21] == 0 && it[28] == 1 && it[4] == 9 && (int8_t)it[5] == -1 && it[6] == 4 && u16(it + 10) == victim);
    CHECK(u16(it + 12) == 77 && it[22] == 3 && it[23] < 4);
}

/* the person test is strength >= the victim's value: equal wins */
static void test_person_tie(void) {
    for (int k = 0; k < 2; k++) {
        missions_world(0x14);
        site(0)[2] = 1;
        uint8_t *rec = sim_room_record(s, 0, 2, 0);
        put16(rec + 8, 500);
        uint8_t *p = person(5);
        p[3] = 0, p[66] = 1, p[5] = 0, p[9] = 130, p[10] = 30, p[2] = 0;
        put16(S() + 12, 5), S()[0x0E] = 1;
        Sim twin = g_sim;
        int a = sim_thug_strength(&twin, T(), S()), d = (int)sim_bits(&twin, 63);
        /* victim = -d; make the thug equal (k 0) or one lower (k 1) by its u16 16 */
        int target = -d - k;
        put16(T() + 16, target - (a - u16(T() + 16)));
        sim_call(s, 0x4A822);
        CHECK(k == 0 ? u16(rec + 0x0A) == 5 : S()[3] == 0x16);
    }
}

static void all_staff(int hospital_of, int state) {
    for (int i = 1; i <= 1040; i++) {
        uint8_t *v = staff(i);
        if (i == 77) continue;
        v[1] = 2, v[3] = (uint8_t)hospital_of, v[4] = (uint8_t)state, v[23] = 0;
    }
}

static void test_staff_mission(void) {
    missions_world(0x15);
    /* each test alone keeps the search empty */
    all_staff(0, 0x13); /* this hospital */
    CHECK(!run_until(has_victim, 1500));
    all_staff(0xFF, 0x13); /* nobody's */
    CHECK(!run_until(has_victim, 1500));
    all_staff(7, 0x00); /* on duty */
    CHECK(!run_until(has_victim, 1500));
    all_staff(7, 0x13);
    put32(H(7) + H_CASH, -1);
    CHECK(!run_until(has_victim, 1500));
    put32(H(7) + H_CASH, 0);
    for (int i = 1; i <= 1040; i++) staff(i)[1] = 8;
    CHECK(!run_until(has_victim, 1500));
    all_staff(7, 0x13);
    for (int m = 0xF9; m <= 0xFE; m++) {
        for (int i = 1; i <= 1040; i++)
            if (i != 77) staff(i)[23] = (uint8_t)m;
        CHECK(!run_until(has_victim, 300));
    }
    all_staff(7, 0x13);
    for (int i = 1; i <= 1040; i++)
        if (i != 77) staff(i)[23] = 0xF8;
    put16(T() + 16, 1000);
    int n = 0; /* only with t&31 = 7 */
    while (next_bits(31) == 7) sim_bits(s, 31); /* at least one call that draws something else */
    while (next_bits(31) != 7 && n < 3000) { sim_call(s, 0x4A822), n++; CHECK(!has_victim()); }
    sim_call(s, 0x4A822);
    CHECK(has_victim() && n > 0);
    uint8_t *v = staff(u16(S() + 12));
    int victim = u16(S() + 12);
    CHECK(v[23] == 0xFE && v[26] == 3 && u16(T() + 16) == 1002 && pb()[0x9E] == 0xFF);
    v[21] = 60, put16(v + 16, 0), v[13] = 0, v[15] = 0, v[9] = 150, v[10] = 50;
    /* the marks by the count: FD at 40, the fight at 30 (strong thug: FC), FB at 20, FA at 10 */
    for (int c = 49; c >= 1; c--) {
        pb()[0x9E] = 0;
        sim_call(s, 0x4A822);
        CHECK(S()[0x0E] == c);
        if (c == 40) CHECK(v[23] == 0xFD && pb()[0x9E]);
        if (c == 30) CHECK(v[23] == 0xFC && pb()[0x9E]);
        if (c == 20) CHECK(v[23] == 0xFB && pb()[0x9E]);
        if (c == 10) CHECK(v[23] == 0xFA && pb()[0x9E]);
        if (c == 41 || c == 35) CHECK(!pb()[0x9E]);
    }
    sim_call(s, 0x4A822); /* the beating */
    CHECK(v[21] == 0 && v[23] == 0xF9 && S()[3] == 0 && T()[4] == 0 && u16(T() + 16) == 1005);
    CHECK(u16(H(0) + 0xE8) == 1 && u16(S() + 12) == 0);
    uint8_t *it = news_of(6);
    CHECK(it && it[21] == 1 && it[8] == 7 && it[9] == 0 && u16(it + 10) == victim && u16(it + 12) == 0 && it[22] == 3);
    CHECK(it[4] == 3 && it[5] == 0 && it[6] == 0 && it[28] == 1);
    /* a weak thug loses the fight at 30 */
    missions_world(0x15);
    all_staff(7, 0x13);
    v = staff(500);
    v[23] = 0xFD, put16(v + 16, 3000);
    put16(S() + 12, 500), S()[0x0E] = 31, S()[0x0B] = 5;
    T()[21] = 50;
    sim_call(s, 0x4A822);
    CHECK(S()[3] == 0x16 && T()[4] == 0x16 && S()[0x0A] >= 20 && S()[0x0A] <= 35 && T()[21] == 0);
    CHECK(S()[0x0B] == 5 && v[23] == 0 && u16(v + 16) == 3003 && u16(H(0) + 0xF2) == 1 && u16(S() + 12) == 0);
    it = news_of(0x0B);
    CHECK(it && it[21] == 1 && it[28] == 1 && it[4] == 6 && it[6] == 2 && it[9] == 7 && u16(it + 10) == 500 && it[23] < 4);
    /* a count already 0: the beating at once */
    missions_world(0x15);
    v = staff(600);
    v[3] = 7;
    put16(S() + 12, 600), S()[0x0E] = 0;
    sim_call(s, 0x4A822);
    CHECK(v[23] == 0xF9 && u16(H(0) + 0xE8) == 1);
    /* only group 8 and the two mission states */
    missions_world(0x14);
    T()[1] = 2;
    put16(S() + 12, 5), S()[0x0E] = 9;
    sim_call(s, 0x4A822);
    CHECK(S()[0x0E] == 9);
    T()[1] = 8, S()[3] = 0x16;
    sim_call(s, 0x4A822);
    CHECK(S()[0x0E] == 9);
}

/* fight draws: strength (t&63 inside), t&127, t&127, victim t&63, t&31, then the thug's t&63; lower is signed */
static void test_fight_order(void) {
    for (int k = 0; k < 32; k++) { /* 16 seeds: the draws' high bits vary */
        missions_world(0x15);
        sim_init(&g_sim, &g_world, &g_prog, (uint64_t)(100 + k / 2));
        uint8_t *v = staff(500);
        v[3] = 7, v[9] = 150, v[10] = 50;
        put16(S() + 12, 500), S()[0x0E] = 31;
        Sim twin = g_sim;
        int a = sim_thug_strength(&twin, T(), S());
        a += (int)sim_bits(&twin, 127), a += (int)sim_bits(&twin, 127);
        int d = (int)sim_bits(&twin, 63);
        d += (int)sim_bits(&twin, 31);
        a += (int)sim_bits(&twin, 63);
        /* victim's u16 16 so that d' = a (k 0: not lower, FC) or a + 1 (k 1: lower) */
        put16(v + 16, a - d + k % 2);
        sim_call(s, 0x4A822);
        CHECK(k % 2 == 0 ? v[23] == 0xFC : S()[3] == 0x16);
    }
}

/* the thug items' gates: 0x4C1BF 70, 0x4C244 80, both type-6 writers 30 (docs/mainloop.md, module H item 5); and the
 * type-6 fields: byte 8 = the victim's hospital for b21 1, the loop hospital for b21 0; nothing in byte 9 or u16 12 */
static void fill_news(int n) {
    fresh();
    for (int i = 0; i < n; i++) sim_news_item(s, i)[0] = 2, sim_news_item(s, i)[20] = 9;
}
static void test_thug_news(void) {
    static const struct { int type, b21, limit; } G[] = {{0x0B, 1, 70}, {0x0B, 0, 80}, {6, 0, 30}, {6, 1, 30}};
    for (unsigned g = 0; g < 4; g++)
        for (int n = G[g].limit; n <= G[g].limit + 1; n++) {
            fill_news(n);
            sim_news_thug(s, G[g].type, G[g].b21, 3, 7, 500, 77, 2);
            CHECK(!news_of(G[g].type) == (n > G[g].limit));
        }
    for (int b21 = 0; b21 <= 1; b21++) {
        fill_news(0);
        uint8_t *slot = sim_news_item(s, 0);
        memset(slot, 0xAA, 70); /* a stale slot: only bytes 1-19 are cleared */
        slot[0] = 0;
        sim_news_thug(s, 6, b21, 3, 7, 500, 77, 2);
        uint8_t *it = news_of(6);
        CHECK(it == slot && it[0] == 2 && it[21] == b21 && it[8] == (b21 ? 7 : 3) && it[9] == 0 && u16(it + 10) == 500);
        CHECK(u16(it + 12) == 0 && it[4] == 3 && it[5] == 0 && it[6] == 0 && it[22] == 2 && it[28] == 1);
    }
    fill_news(71);
    sim_news_thug(s, 0x0B, 0, 0, 0, 5, 77, 2);
    CHECK(news_of(0x0B) && news_of(0x0B)[21] == 0 && news_of(0x0B)[9] == 0 && u16(news_of(0x0B) + 12) == 77);
}

/* FIX-2 (docs/fixes.md): the thug sheet's "Nr.": off 17 for every thug, on 1 + the shown slot */
static void test_fix2_thug_nr(void) {
    fixes_set(0);
    CHECK(support_thug_sheet_nr(24) == 17 && support_thug_sheet_nr(0) == 17);
    fixes_set(1u << 1);
    CHECK(support_thug_sheet_nr(24) == 25 && support_thug_sheet_nr(0) == 1);
    fixes_set(0);
}

int main(void) {
    test_fix2_thug_nr();
    test_thug_news();
    test_kitchen();
    test_kitchen_switch();
    test_golfers();
    test_campaign();
    test_buy();
    test_sell();
    test_thug_screen();
    test_thug_send();
    test_strength();
    test_person_mission();
    test_person_tie();
    test_staff_mission();
    test_fight_order();
    world_free(&g_world);
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] support\n");
    return 0;
}
