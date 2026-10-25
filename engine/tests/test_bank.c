/* The player's blood and organ bank (docs/ui-bank.md). Synthetic world and program image (see AGENTS.md): the organ
 * table holds made-up values except where a case repeats one of the spec's oracle runs (the heart's base 31, the
 * dealer's offer of 500 cl for 22, the brain for 48 with 2 organs held), whose expected numbers come from the spec. */
#include <stdio.h>
#include <string.h>

#include "fixes.h"
#include "bank.h"
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

static uint8_t *H(int h) { return world_hospital(&g_world, h); }
static uint8_t *M(int i) { return sim_market(s, i); }
static uint8_t *B(void) { return bank_record(s); }
static uint8_t *pb(void) { return rooms_pb(s); }
static int u16(const uint8_t *p) { return (uint16_t)get16(p); }
static int shown(void) { return u16(pb() + BANK_P_OFFER); }
static int32_t cash(int h) { return get32(H(h) + H_CASH); }
static int next_r7(void) { /* the r7 the next draw of the rules will give */
    Sim twin = g_sim;
    return (int)sim_bits(&twin, 7);
}

static void fresh(void) {
    world_free(&g_world);
    world_alloc(&g_world);
    memset(g_world.block[BLK_ROOM_SLOTS], 0xFF, g_world.size[BLK_ROOM_SLOTS]);
    memset(g_image, 0, sizeof g_image);
    g_prog.image.data = g_image;
    g_prog.image.size = sizeof g_image;
    sim_init(&g_sim, &g_world, &g_prog, 11);
    memset(g_world.block[BLK_MARKET], 0, g_world.size[BLK_MARKET]);
    for (int i = 0; i < 100; i++) M(i)[0] = 0xFF;
    for (int id = 1; id <= 11; id++) g_image[0x4712 + 2 * id] = (uint8_t)(10 + id); /* base */
    g_image[0x4712 + 2 * 4] = 31, g_image[0x4712 + 2 * 5] = 45; /* heart, brain: the spec's examples */
    g_world.block[BLK_ROOM_COUNTS][17] = 1; /* the player has a bank */
    put32(B() + BR_CAPACITY, 2000);
    put16(B() + BR_PLACES, 8);
    put32(H(0) + H_CASH, 984);
}

static void blood(int i, int seller, int q, int price) {
    memset(M(i), 0, 10);
    M(i)[0] = (uint8_t)seller;
    put16(M(i) + 2, q), put16(M(i) + 4, price), put16(M(i) + 6, 5);
}
static void organ(int i, int seller, int id, int quality, int price) {
    memset(M(i), 0, 10);
    M(i)[0] = (uint8_t)seller, M(i)[1] = 1, M(i)[2] = (uint8_t)id, M(i)[3] = (uint8_t)quality;
    put16(M(i) + 4, price), put16(M(i) + 6, 5);
}
static void slot(int j, int id, int quality) { B()[BR_SLOTS + 2 * j] = (uint8_t)id, B()[BR_SLOTS + 2 * j + 1] = (uint8_t)quality; }

static void test_buy_search(void) {
    fresh();
    CHECK(bank_buy_open(s, 0) == BANK_NO_BLOOD_OFFER);
    CHECK(bank_buy_open(s, 1) == BANK_NO_ORGAN_OFFER);
    organ(3, DEALER, 5, 81, 48);
    CHECK(bank_buy_open(s, 0) == BANK_NO_BLOOD_OFFER); /* an organ is not blood */
    blood(10, 5, 400, 20);
    blood(20, DEALER, 500, 22);
    blood(40, 0, 300, 15); /* the player's own offer is listed */
    blood(50, 6, 300, 15);
    M(50)[8] = 0xFF; /* reserved by somebody: skipped */
    blood(60, -1, 300, 15); /* free slot */
    M(60)[0] = 0xFF;
    CHECK(bank_buy_open(s, 0) == 0 && shown() == 10 && M(10)[8] == 0xFF);
    CHECK(bank_buy_step(s, 0, 1) == 0 && shown() == 20 && M(10)[8] == 0 && M(20)[8] == 0xFF);
    CHECK(bank_buy_step(s, 0, 1) == 0 && shown() == 40);
    CHECK(bank_buy_step(s, 0, 1) == 0 && shown() == 10); /* wraps over 99 -> 0 */
    CHECK(bank_buy_step(s, 0, -1) == 0 && shown() == 40); /* 0 -> 99 */
    CHECK(M(40)[8] == 0xFF && M(10)[8] == 0);
    bank_back(s);
    CHECK(M(40)[8] == 0 && M(40)[0] == 0); /* a published offer is not freed by back */
    /* the only offer is found again; when it vanished: 1642 */
    fresh();
    blood(7, DEALER, 500, 22);
    CHECK(bank_buy_open(s, 0) == 0 && bank_buy_step(s, 0, 1) == 0 && shown() == 7);
    M(7)[0] = 0xFF;
    CHECK(bank_buy_step(s, 0, -1) == BANK_NO_BLOOD_OFFER);
    /* organs: kind not 0 */
    fresh();
    blood(1, DEALER, 500, 22);
    organ(5, DEALER, 5, 81, 48);
    organ(9, 3, 4, 60, 39);
    CHECK(bank_buy_open(s, 1) == 0 && shown() == 5);
    CHECK(bank_buy_step(s, 1, 1) == 0 && shown() == 9);
    CHECK(bank_buy_step(s, 1, 1) == 0 && shown() == 5);
}

static void test_buy_blood(void) {
    BankResult o;
    /* the spec's oracle run: dealer offer 500 cl for 22, stock 1900 of 2000, organ slots 7 and 8 = (1, 50) */
    fresh();
    put32(B() + BR_STOCK, 1900);
    slot(7, 1, 50), slot(8, 1, 50);
    blood(20, DEALER, 500, 22);
    int32_t dealer = cash(DEALER);
    CHECK(bank_buy_open(s, 0) == 0);
    CHECK(bank_buy_blood(s, &o) == BANK_BLOOD_BOUGHT);
    CHECK(o.seller == DEALER && o.quantity == 500 && o.price == 22);
    CHECK(cash(0) == 962 && get32(H(0) + 0x6E) == 22 && get32(H(0) + 0x72) == 22 && get32(H(0) + 0x62) == 500);
    CHECK(cash(DEALER) == dealer + 22 && get32(H(DEALER) + 0x76) == 22 && get32(H(DEALER) + 0x7A) == 22);
    CHECK(get32(H(DEALER) + 0x5E) == 500);
    CHECK(get32(B() + BR_STOCK) == 2000);
    CHECK(get32(H(0) + 0x66) == 0); /* the hospital's blood lost is untouched (fault) */
    CHECK(B()[BR_SLOTS + 14] == 145 && B()[BR_SLOTS + 15] == 51); /* +0x66 + 400: slot 7 became (145, 51) */
    CHECK(M(20)[0] == 0xFF && M(20)[8] == 0xFF);
    /* within the capacity: all of it, from hospital 5 */
    fresh();
    blood(3, 5, 400, 20);
    bank_buy_open(s, 0);
    CHECK(bank_buy_blood(s, &o) == BANK_BLOOD_BOUGHT && get32(B() + BR_STOCK) == 400 && cash(5) == 20);
    CHECK(get32(B() + BR_LOST) == 0);
    /* cash 10: 1634, the offer unreserved and kept */
    fresh();
    put32(H(0) + H_CASH, 10);
    blood(3, DEALER, 500, 22);
    bank_buy_open(s, 0);
    CHECK(bank_buy_blood(s, &o) == BANK_BLOOD_NO_CASH);
    CHECK(M(3)[8] == 0 && M(3)[0] == DEALER && cash(0) == 10 && get32(B() + BR_STOCK) == 0);
    /* price = cash is affordable; a negative cash refuses */
    fresh();
    put32(H(0) + H_CASH, 22);
    blood(3, DEALER, 500, 22);
    bank_buy_open(s, 0);
    CHECK(bank_buy_blood(s, &o) == BANK_BLOOD_BOUGHT && cash(0) == 0);
    fresh();
    put32(H(0) + H_CASH, -5);
    blood(3, DEALER, 500, 0);
    bank_buy_open(s, 0);
    CHECK(bank_buy_blood(s, &o) == BANK_BLOOD_NO_CASH);
    /* the own offer: 1631, the blood back in the stock, no money */
    fresh();
    put32(B() + BR_STOCK, 100);
    blood(3, 0, 500, 22);
    bank_buy_open(s, 0);
    CHECK(bank_buy_blood(s, &o) == BANK_BLOOD_OWN);
    CHECK(get32(B() + BR_STOCK) == 600 && cash(0) == 984 && get32(H(0) + 0x62) == 0 && get32(H(0) + 0x6E) == 0);
    CHECK(M(3)[0] == 0xFF);
}

static void test_buy_organ(void) {
    BankResult o;
    /* the spec's oracle run: a dealer brain for 48 with 2 organs held pays 2 */
    fresh();
    slot(0, 1, 90), slot(1, 3, 70);
    put16(B() + BR_HELD, 2);
    organ(6, DEALER, 5, 81, 48);
    int32_t dealer = cash(DEALER);
    CHECK(bank_buy_open(s, 1) == 0);
    CHECK(bank_buy_organ(s, &o) == BANK_ORGAN_BOUGHT && o.price == 48 && o.id == 5 && o.seller == DEALER);
    CHECK(cash(0) == 982 && get32(H(0) + 0x6E) == 2 && get32(H(0) + 0x72) == 2 && u16(H(0) + 0x7E) == 1);
    CHECK(cash(DEALER) == dealer + 2 && get32(H(DEALER) + 0x76) == 2 && get32(H(DEALER) + 0x7A) == 2);
    CHECK(u16(H(DEALER) + 0x80) == 1);
    CHECK(get16(B() + BR_HELD) == 3 && B()[BR_SLOTS + 4] == 5 && B()[BR_SLOTS + 5] == 81);
    CHECK(M(6)[0] == 0xFF);
    /* with 0 organs held the purchase is free */
    fresh();
    organ(6, 7, 4, 60, 39);
    bank_buy_open(s, 1);
    CHECK(bank_buy_organ(s, &o) == BANK_ORGAN_BOUGHT && cash(0) == 984 && u16(H(7) + 0x80) == 1);
    CHECK(u16(H(0) + 0x7E) == 1 && get16(B() + BR_HELD) == 1);
    /* the price is tested against the cash, not the booked count: 1633 */
    fresh();
    put32(H(0) + H_CASH, 47);
    organ(6, DEALER, 5, 81, 48);
    bank_buy_open(s, 1);
    CHECK(bank_buy_organ(s, &o) == BANK_ORGAN_NO_CASH && M(6)[8] == 0 && get16(B() + BR_HELD) == 0);
    put32(H(0) + H_CASH, 48); /* price = cash is affordable */
    bank_buy_open(s, 1);
    CHECK(bank_buy_organ(s, &o) == BANK_ORGAN_BOUGHT);
    /* held = places: 1632 */
    fresh();
    put16(B() + BR_PLACES, 2), put16(B() + BR_HELD, 2);
    slot(0, 1, 90), slot(1, 3, 70);
    organ(6, DEALER, 5, 81, 48);
    bank_buy_open(s, 1);
    CHECK(bank_buy_organ(s, &o) == BANK_ORGAN_FULL && M(6)[8] == 0 && M(6)[0] == DEALER && cash(0) == 984);
    /* the slot after the held count is written whatever it held */
    fresh();
    put16(B() + BR_HELD, 1);
    slot(0, 0, 0), slot(1, 9, 40);
    organ(6, 0, 2, 75, 30); /* the own offer: back for nothing, no counters */
    bank_buy_open(s, 1);
    CHECK(bank_buy_organ(s, &o) == BANK_ORGAN_BOUGHT && cash(0) == 984 && u16(H(0) + 0x7E) == 0);
    CHECK(u16(H(0) + 0x80) == 0 && B()[BR_SLOTS + 2] == 2 && B()[BR_SLOTS + 3] == 75 && get16(B() + BR_HELD) == 2);
}

static void test_sell_blood(void) {
    fresh();
    put32(B() + BR_STOCK, 99);
    CHECK(bank_sell_blood_open(s) == BANK_LOW_STOCK);
    put32(B() + BR_STOCK, 1500);
    for (int i = 0; i < 100; i++) blood(i, 3, 400, 20);
    CHECK(bank_sell_blood_open(s) == BANK_MARKET_FULL);
    M(37)[0] = 0xFF;
    int r7 = next_r7();
    CHECK(bank_sell_blood_open(s) == 0 && shown() == 37);
    uint8_t *m = M(37);
    CHECK(m[0] == 0 && m[1] == 0 && get16(m + 6) == -1 && m[8] == 0xFF && u16(m + 2) == 500);
    CHECK(u16(m + 4) == 500 / 32 + 6 + r7);
    CHECK(get32(B() + BR_STOCK) == 1500); /* not touched yet */
    /* the spec's run: twelve "more" -> 1500, "less" -> 1400, publish -> stock 100 */
    for (int k = 0; k < 12; k++) bank_blood_more(s);
    CHECK(u16(m + 2) == 1500);
    bank_blood_less(s);
    CHECK(u16(m + 2) == 1400);
    CHECK(bank_publish_blood(s) == BANK_BLOOD_PUBLISHED);
    CHECK(get32(B() + BR_STOCK) == 100 && get16(m + 6) == 2 && m[8] == 0 && m[0] == 0);
    /* stock 150: q 150, "less" -> 200, publish -> -50 */
    fresh();
    put32(B() + BR_STOCK, 150);
    r7 = next_r7();
    CHECK(bank_sell_blood_open(s) == 0 && u16(M(shown()) + 2) == 150);
    CHECK(u16(M(shown()) + 4) == 150 / 32 + 6 + r7);
    bank_blood_less(s);
    CHECK(u16(M(shown()) + 2) == 200);
    bank_publish_blood(s);
    CHECK(get32(B() + BR_STOCK) == -50);
    /* stock 100 is enough */
    fresh();
    put32(B() + BR_STOCK, 100);
    CHECK(bank_sell_blood_open(s) == 0 && u16(M(shown()) + 2) == 100);
    /* the 10,000 cap and the steps of 100 */
    fresh();
    put32(B() + BR_STOCK, 20000);
    bank_sell_blood_open(s);
    m = M(shown());
    for (int k = 0; k < 94; k++) bank_blood_more(s);
    CHECK(u16(m + 2) == 9900);
    bank_blood_more(s);
    CHECK(u16(m + 2) == 10000);
    bank_blood_more(s);
    CHECK(u16(m + 2) == 10000);
    bank_blood_less(s);
    CHECK(u16(m + 2) == 9900);
    put16(m + 2, 300);
    bank_blood_less(s);
    CHECK(u16(m + 2) == 200);
    /* price: +1 at most 600, -1 at least 1 */
    put16(m + 4, 599);
    bank_price_step(s, 1);
    CHECK(u16(m + 4) == 600);
    bank_price_step(s, 1);
    CHECK(u16(m + 4) == 600);
    put16(m + 4, 2);
    bank_price_step(s, -1);
    CHECK(u16(m + 4) == 1);
    bank_price_step(s, -1);
    CHECK(u16(m + 4) == 1);
    /* back frees the draft */
    int i = shown();
    bank_back(s);
    CHECK(M(i)[0] == 0xFF && get16(M(i) + 6) == 0 && M(i)[8] == 0 && get32(B() + BR_STOCK) == 20000);
}

static void test_sell_organ(void) {
    fresh();
    CHECK(bank_sell_organ_open(s) == BANK_NO_ORGANS);
    for (int i = 0; i < 100; i++) blood(i, 3, 400, 20);
    CHECK(bank_sell_organ_open(s) == BANK_NO_ORGANS); /* the empty bank is tested before the market */
    fresh();
    /* the spec's run: slots (0,0), (4,60), (8,70), (5,12), held 3 */
    slot(1, 4, 60), slot(2, 8, 70), slot(3, 5, 12);
    put16(B() + BR_HELD, 3);
    for (int i = 0; i < 100; i++) blood(i, 3, 400, 20);
    CHECK(bank_sell_organ_open(s) == BANK_MARKET_FULL);
    M(12)[0] = 0xFF;
    int r7 = next_r7();
    CHECK(bank_sell_organ_open(s) == 0 && shown() == 12);
    uint8_t *m = M(12);
    CHECK(u16(pb() + BANK_P_SLOT) == 1 && m[0] == 0 && m[1] == 1 && m[2] == 4 && m[3] == 60);
    CHECK(get16(m + 6) == -1 && m[8] == 0xFF);
    CHECK(u16(m + 4) == 31 + r7 * (31 / 16 + 1));
    CHECK(bank_organ_step(s, 1) == 0 && u16(pb() + BANK_P_SLOT) == 2 && m[2] == 8 && m[3] == 70);
    r7 = next_r7();
    CHECK(bank_organ_step(s, 1) == 0 && u16(pb() + BANK_P_SLOT) == 3 && m[2] == 5 && m[3] == 12);
    CHECK(u16(m + 4) == 45 + r7 * (45 / 16 + 1)); /* a new random price */
    CHECK(bank_organ_step(s, 1) == 0 && u16(pb() + BANK_P_SLOT) == 1); /* 29 -> 0, empty slot 0 skipped */
    CHECK(bank_organ_step(s, -1) == 0 && u16(pb() + BANK_P_SLOT) == 3); /* 0 -> 29 */
    CHECK(bank_publish_organ(s) == BANK_ORGAN_PUBLISHED);
    CHECK(get16(m + 6) == 2 && m[8] == 0 && m[2] == 5);
    CHECK(get16(B() + BR_HELD) == 2);
    CHECK(B()[BR_SLOTS] == 4 && B()[BR_SLOTS + 1] == 60 && B()[BR_SLOTS + 2] == 8 && B()[BR_SLOTS + 3] == 70);
    CHECK(B()[BR_SLOTS + 4] == 0 && B()[BR_SLOTS + 6] == 0); /* compacted */
    /* the only organ is found again; all slots empty: -1 (back) */
    fresh();
    slot(5, 2, 50);
    put16(B() + BR_HELD, 1);
    CHECK(bank_sell_organ_open(s) == 0 && u16(pb() + BANK_P_SLOT) == 5);
    CHECK(bank_organ_step(s, 1) == 0 && u16(pb() + BANK_P_SLOT) == 5);
    slot(5, 0, 50);
    CHECK(bank_organ_step(s, -1) == -1);
    /* engine choice: held not 0 but every slot empty ends (the original hangs) with 2547, the draft freed */
    fresh();
    put16(B() + BR_HELD, 2);
    CHECK(bank_sell_organ_open(s) == BANK_NO_ORGANS);
    CHECK(M(0)[0] == 0xFF && M(0)[8] == 0);
    /* back frees the draft and keeps the organ */
    fresh();
    slot(0, 2, 50);
    put16(B() + BR_HELD, 1);
    bank_sell_organ_open(s);
    bank_back(s);
    CHECK(M(0)[0] == 0xFF && get16(B() + BR_HELD) == 1 && B()[BR_SLOTS] == 2);
}

static void test_offer_numbers(void) {
    /* the low-byte divisor: 500 cl -> 500 & 255 = 244 */
    CHECK(bank_value_per_5000(22, 500) == (uint32_t)(22 * 16384 / 244) * 5000u / 16384u);
    CHECK(bank_value_per_5000(22, 500) == 450);
    CHECK(bank_value_per_5000(10, 300) == (uint32_t)(10 * 16384 / 44) * 5000u / 16384u);
    /* engine choice: a multiple of 256 (the original's division by zero) divides by 1 */
    CHECK(bank_value_per_5000(22, 512) == 22u * 16384u * 5000u / 16384u);
    CHECK(bank_value_per_5000(22, 256) == bank_value_per_5000(22, 1));
    CHECK(bank_value_per_5000(22, 0) == bank_value_per_5000(22, 1));
    /* the average: published blood only (drafts and organs left out), unsigned 32-bit */
    fresh();
    uint32_t last = 50;
    CHECK(bank_average_blood(s, &last) == 50); /* none: the old value */
    blood(1, 3, 400, 20);
    blood(2, DEALER, 600, 30);
    blood(3, 0, 1000, 99);
    put16(M(3) + 6, -1); /* a draft */
    organ(4, 5, 2, 70, 300);
    CHECK(bank_average_blood(s, &last) == 50u * 16384u / 1000u * 5000u / 16384u && last == 249);
    blood(9, 6, 300, 0); /* the last matching offer has price 0: the old value stays */
    last = 77;
    CHECK(bank_average_blood(s, &last) == 77);
    M(9)[0] = 0xFF;
    blood(9, 6, 0, 10); /* engine choice: a quantity sum of 0 keeps the old value (the original divides by it) */
    M(1)[0] = M(2)[0] = 0xFF;
    CHECK(bank_average_blood(s, &last) == 77);
    /* organ offers of one id: reserved ones and drafts included */
    fresh();
    organ(1, 3, 5, 70, 40);
    organ(2, DEALER, 5, 70, 50);
    M(2)[8] = 0xFF;
    organ(3, 0, 5, 70, 60);
    put16(M(3) + 6, -1);
    organ(4, 3, 4, 70, 99);
    organ(5, 3, 5, 70, 99);
    M(5)[0] = 0xFF;
    blood(6, 3, 5, 99); /* a blood offer whose quantity's low byte is 5 is not an organ */
    int sum;
    CHECK(bank_organ_offers(s, 5, &sum) == 3 && sum == 150);
    CHECK(bank_organ_offers(s, 4, &sum) == 1 && sum == 99);
}

static void test_reports(void) {
    BankOrganReport r;
    fresh();
    bank_organ_report(s, &r);
    CHECK(r.held == 0 && r.places == 8 && r.free_places == 8 && r.average == 0 && r.minimum == 100);
    /* the first n slots, empty ones counting 0 */
    slot(0, 1, 90), slot(1, 0, 33), slot(2, 3, 60), slot(3, 7, 30); /* an empty slot with a stale quality byte */
    put16(B() + BR_HELD, 3);
    bank_organ_report(s, &r);
    CHECK(r.held == 3 && r.free_places == 5 && r.average == 50 && r.minimum == 0);
    put16(B() + BR_HELD, 1);
    bank_organ_report(s, &r);
    CHECK(r.average == 90 && r.minimum == 90);
    /* pages: 0 -> 1 -> 2 -> 0, and back from 0 to 2 */
    put16(pb() + BANK_P_SLOT, 0);
    bank_report_page(s, 1);
    CHECK(get16(pb() + BANK_P_SLOT) == 1);
    bank_report_page(s, 1), bank_report_page(s, 1);
    CHECK(get16(pb() + BANK_P_SLOT) == 0);
    bank_report_page(s, -1);
    CHECK(get16(pb() + BANK_P_SLOT) == 2);
    /* page 1 stops at the first empty slot; page 2 only from 19 organs */
    int first;
    put16(B() + BR_HELD, 4);
    CHECK(bank_report_list(s, 1, &first) == 1 && first == 0);
    CHECK(bank_report_list(s, 2, &first) == 0);
    fresh();
    put16(B() + BR_PLACES, 30);
    for (int j = 0; j < 20; j++) slot(j, 1 + j % 11, 50);
    put16(B() + BR_HELD, 20);
    CHECK(bank_report_list(s, 1, &first) == 18 && first == 0);
    CHECK(bank_report_list(s, 2, &first) == 2 && first == 18);
    put16(B() + BR_HELD, 18);
    CHECK(bank_report_list(s, 2, &first) == 0 && first == 0);
    put16(B() + BR_HELD, 17);
    CHECK(bank_report_list(s, 2, &first) == 0 && first == 0);
    put16(B() + BR_HELD, 19);
    CHECK(bank_report_list(s, 2, &first) == 1 && first == 18);
}

static void test_add32_wraps(void) { /* the original's add wraps at 32 bits */
    uint8_t b[4];
    put32(b, INT32_MAX);
    add32(b, 1);
    CHECK(get32(b) == INT32_MIN);
    add32(b, -1);
    CHECK(get32(b) == INT32_MAX);
}

/* FIX-18 (docs/fixes.md): stock 1,900 of 2,000, buy 500 cl, slots 7 and 8 = (1, 50): off slot 7 = (145, 51) and the
 * hospital's +0x66 unchanged; on the slots unchanged and +0x66 + 400. Stock 2,000 in both. */
static void test_fix18_blood_excess(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 17 : 0);
        BankResult o;
        fresh();
        put32(B() + BR_STOCK, 1900);
        slot(7, 1, 50), slot(8, 1, 50);
        blood(20, DEALER, 500, 22);
        CHECK(bank_buy_open(s, 0) == 0 && bank_buy_blood(s, &o) == BANK_BLOOD_BOUGHT);
        CHECK(get32(B() + BR_STOCK) == 2000);
        CHECK(get32(H(0) + 0x66) == (on ? 400 : 0));
        CHECK(B()[BR_SLOTS + 14] == (on ? 1 : 145) && B()[BR_SLOTS + 15] == (on ? 50 : 51));
        CHECK(B()[BR_SLOTS + 16] == 1 && B()[BR_SLOTS + 17] == 50);
    }
    fixes_set(0);
}

/* FIX-19 (docs/fixes.md): a dealer brain at 48 with 2 held, cash 984: off cash 982 and the dealer + 2, on cash 936 and
 * the dealer + 48; 0 held: off free, on 48 */
static void test_fix19_organ_price(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 18 : 0);
        BankResult o;
        fresh();
        slot(0, 1, 90), slot(1, 3, 70);
        put16(B() + BR_HELD, 2);
        organ(6, DEALER, 5, 81, 48);
        int32_t dealer = cash(DEALER);
        CHECK(bank_buy_open(s, 1) == 0 && bank_buy_organ(s, &o) == BANK_ORGAN_BOUGHT && o.price == 48);
        CHECK(cash(0) == (on ? 936 : 982) && cash(DEALER) == dealer + (on ? 48 : 2));
        CHECK(get32(H(0) + 0x6E) == (on ? 48 : 2) && get32(H(DEALER) + 0x76) == (on ? 48 : 2));
        fresh();
        organ(6, DEALER, 5, 81, 48);
        CHECK(bank_buy_open(s, 1) == 0 && bank_buy_organ(s, &o) == BANK_ORGAN_BOUGHT);
        CHECK(cash(0) == (on ? 936 : 984));
    }
    fixes_set(0);
}

/* FIX-20 (docs/fixes.md): q 500 price 22: off 450, on 219; q 512 price 23: off 115,000 (the divisor-1 guard), on 224;
 * q 0 divides by 1 in both */
static void test_fix20_value_per_5000(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 19 : 0);
        CHECK(bank_value_per_5000(22, 500) == (on ? 219u : 450u));
        CHECK(bank_value_per_5000(23, 512) == (on ? 224u : 115000u));
        CHECK(bank_value_per_5000(23, 0) == 115000u);
    }
    fixes_set(0);
}

/* FIX-21 (docs/fixes.md): stock 150 (draft 150), "less", publish: off q 200 and stock -50, on q 150 and stock 0;
 * stock 1,500: 1,500 -> 1,400 in both */
static void test_fix21_less_blood(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 20 : 0);
        fresh();
        put32(B() + BR_STOCK, 150);
        CHECK(bank_sell_blood_open(s) == 0 && u16(M(shown()) + 2) == 150);
        bank_blood_less(s);
        CHECK(u16(M(shown()) + 2) == (on ? 150 : 200));
        bank_publish_blood(s);
        CHECK(get32(B() + BR_STOCK) == (on ? 0 : -50));
        fresh();
        put32(B() + BR_STOCK, 1500);
        CHECK(bank_sell_blood_open(s) == 0);
        uint8_t *m = M(shown());
        for (int k = 0; k < 12; k++) bank_blood_more(s);
        bank_blood_less(s);
        CHECK(u16(m + 2) == 1400);
        put16(m + 2, 300); /* stock 1,500: the floor stays 200 in both */
        bank_blood_less(s);
        CHECK(u16(m + 2) == 200);
    }
    fixes_set(0);
}

int main(void) {
    test_fix18_blood_excess();
    test_fix19_organ_price();
    test_fix20_value_per_5000();
    test_fix21_less_blood();
    test_add32_wraps();
    test_buy_search();
    test_buy_blood();
    test_buy_organ();
    test_sell_blood();
    test_sell_organ();
    test_offer_numbers();
    test_reports();
    world_free(&g_world);
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] all checks passed\n");
    return 0;
}
