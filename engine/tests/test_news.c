/* Module H, the newspaper simulation (docs/mainloop.md, module H). Synthetic world and program image. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void fresh(void) {
    world_free(&g_world);
    world_alloc(&g_world);
    memset(g_image, 0, sizeof g_image);
    g_prog.image.data = g_image;
    g_prog.image.size = sizeof g_image;
    world_set_tail(&g_world, TAIL_DAY, 1);
    world_set_tail(&g_world, TAIL_MONTH, 1);
    sim_init(&g_sim, &g_world, &g_prog, 11);
}

static uint8_t *person(int i) { return g_world.block[BLK_PATIENTS] + i * 106; }
static uint8_t *item(int i) { return sim_news_item(&g_sim, i); }

static uint8_t *plant(int i, int cls, int type, int weight, int age, int chance, int change, int h) {
    uint8_t *it = item(i);
    memset(it, 0, 70);
    it[0] = (uint8_t)cls, it[20] = (uint8_t)type, it[4] = (uint8_t)weight, it[6] = (uint8_t)chance;
    it[5] = (uint8_t)(int8_t)change, it[8] = (uint8_t)h;
    put16(it + 2, age);
    return it;
}

static void print_issue(int slots) { /* items 0-3 printed in slots 0-3 */
    uint8_t *issue = sim_news_issue(&g_sim);
    for (int j = 0; j < slots; j++) {
        item(j)[1] = 1, item(j)[7] = (uint8_t)j;
        issue[j] = (uint8_t)(j + 1);
    }
}

static void set_opinions(int h, int v) {
    for (int i = 0; i < 1009; i++) person(i)[20 + h] = (uint8_t)v;
}

static long opinion_sum(int h) {
    long sum = 0;
    for (int i = 1; i < 1009; i++) sum += person(i)[20 + h];
    return sum;
}

static int changed(int h, int base) {
    int n = 0;
    for (int i = 0; i < 1009; i++) n += person(i)[20 + h] != base;
    return n;
}

/* 0x42C1D: only with all four slots filled; chance (b6 + 1) / 8; one random person 1-1,008; clamp 0-100 */
static void test_opinion_effect(void) {
    fresh();
    for (int j = 0; j < 4; j++) plant(j, j ? 2 : 1, 10 + j, 5, 0, 3, 1, 1 + j), set_opinions(1 + j, 50);
    print_issue(3);
    for (int n = 0; n < 1000; n++) sim_call(&g_sim, 0x42C1D);
    for (int j = 0; j < 4; j++) CHECK(changed(1 + j, 50) == 0); /* three slots: nothing */

    print_issue(4);
    item(3)[6] = 7; /* chance 8/8 */
    sim_call(&g_sim, 0x42C1D);
    CHECK(changed(4, 50) == 1 && opinion_sum(4) == 1008L * 50 + 1); /* one person per item and call */
    CHECK(person(0)[24] == 50);

    set_opinions(4, 50);
    const int calls = 8000;
    for (int n = 0; n < calls; n++) sim_call(&g_sim, 0x42C1D);
    CHECK(opinion_sum(4) - 1008L * 50 == calls); /* b6 7: every call */
    CHECK(person(0)[24] == 50); /* record 0 is never drawn */
    for (int j = 0; j < 3; j++) {
        long d = opinion_sum(1 + j) - 1008L * 50; /* b6 3: half the calls, sd about 45 */
        CHECK(d > calls / 2 - 250 && d < calls / 2 + 250);
    }

    fresh();
    for (int j = 0; j < 4; j++) plant(j, j ? 2 : 1, 10 + j, 5, 0, 7, j == 0 ? 3 : j == 1 ? -7 : 0, 1 + j);
    print_issue(4);
    set_opinions(1, 100), set_opinions(2, 2), set_opinions(3, 50);
    for (int n = 0; n < 3000; n++) sim_call(&g_sim, 0x42C1D);
    CHECK(opinion_sum(1) == 1008L * 100); /* clamped at 100 */
    CHECK(changed(2, 2) > 900 && opinion_sum(2) == 0 + 2L * (1008 - changed(2, 2))); /* clamped at 0 */
    CHECK(changed(3, 50) == 0); /* d 0 changes nothing */
}

/* 0x46E0C / 0x46EA4: weight x 16 + age + b6 x 4, per class, the type rule with the 0x0E exemption */
static void test_selection(void) {
    fresh();
    plant(10, 1, 3, 2, 0, 1, 1, 1);    /* class 1: 32 + 0 + 4 = 36 */
    plant(11, 1, 4, 2, 5, 0, 1, 1);    /* class 1: 37, wins slot 0 */
    plant(20, 2, 5, 5, 10, 1, -1, 1);  /* X: 80 + 10 + 4 = 94 */
    plant(21, 2, 6, 4, 20, 4, -1, 1);  /* Y: 64 + 20 + 16 = 100 */
    plant(22, 2, 7, 6, 0, 0, -1, 1);   /* Z: 96 */
    plant(23, 2, 8, 1, 5, 1, -1, 1)[1] = 1; /* printed already: not a candidate */
    plant(24, 2, 9, 0, 0, 0, -1, 1);   /* score 0: never chosen */
    sim_call(&g_sim, 0x46E0C);
    const uint8_t *issue = sim_news_issue(&g_sim);
    CHECK(issue[0] == 12 && issue[1] == 22 && issue[2] == 23 && issue[3] == 21);
    CHECK(item(21)[1] == 1 && item(21)[7] == 1 && get16(item(21) + 2) == 0);
    CHECK(item(20)[7] == 3 && item(10)[1] == 0 && item(24)[1] == 0);

    fresh(); /* a score of 0 is never chosen, even alone */
    plant(5, 2, 9, 0, 0, 0, -1, 1);
    sim_call(&g_sim, 0x46E0C);
    CHECK(sim_news_issue(&g_sim)[1] == 0 && item(5)[1] == 0);

    fresh(); /* equal scores: the first; a type already in the issue is skipped */
    plant(30, 2, 9, 5, 0, 0, -1, 1);
    plant(31, 2, 9, 5, 10, 0, -1, 1);
    plant(32, 2, 10, 5, 0, 0, -1, 1);
    plant(33, 2, 11, 5, 0, 0, -1, 1);
    sim_call(&g_sim, 0x46E0C);
    issue = sim_news_issue(&g_sim);
    CHECK(issue[0] == 0 && issue[1] == 32 && issue[2] == 33 && issue[3] == 34);
    CHECK(item(30)[1] == 0);

    fresh(); /* type 0x0E: no type test once slot 0 is filled */
    plant(0, 1, 3, 1, 0, 0, 1, 1);
    plant(1, 2, 0x0E, 9, 0, 0, 1, 1);
    plant(2, 2, 0x0E, 8, 0, 0, 1, 1);
    plant(3, 2, 6, 1, 0, 0, 1, 1);
    sim_call(&g_sim, 0x46E0C);
    issue = sim_news_issue(&g_sim);
    CHECK(issue[0] == 1 && issue[1] == 2 && issue[2] == 3 && issue[3] == 4);

    fresh(); /* without slot 0 the 0x0E items are tested like any other */
    plant(1, 2, 0x0E, 9, 0, 0, 1, 1);
    plant(2, 2, 0x0E, 8, 0, 0, 1, 1);
    plant(3, 2, 6, 1, 0, 0, 1, 1);
    sim_call(&g_sim, 0x46E0C);
    issue = sim_news_issue(&g_sim);
    CHECK(issue[1] == 2 && issue[2] == 4 && issue[3] == 0);
}

/* 0x474BC: unprinted items go at age 150 except the listed types; printed ones age only with a full issue
 * and leave at 30, one at a time */
static void test_ageing(void) {
    static const int KEEP[] = {7, 8, 0x23, 0x24, 0x25, 0x33, 0x35};
    fresh();
    plant(10, 2, 5, 1, 148, 0, -1, 1);
    plant(11, 2, 5, 1, 149, 0, -1, 1);
    for (int k = 0; k < 7; k++) plant(20 + k, 2, KEEP[k], 1, 149, 0, -1, 1);
    plant(30, 2, 6, 1, 149, 0, -1, 1);
    for (int j = 0; j < 4; j++) plant(j, 2, 40 + j, 1, 20, 0, -1, 1);
    print_issue(3);
    sim_call(&g_sim, 0x474BC);
    CHECK(item(10)[0] && get16(item(10) + 2) == 149);
    CHECK(!item(11)[0] && !item(30)[0]);
    for (int k = 0; k < 7; k++) CHECK(item(20 + k)[0] && get16(item(20 + k) + 2) == 150);
    for (int j = 0; j < 3; j++) CHECK(get16(item(j) + 2) == 20); /* issue not full: printed items wait */

    print_issue(4);
    put16(item(1) + 2, 29), put16(item(2) + 2, 29);
    sim_call(&g_sim, 0x474BC);
    const uint8_t *issue = sim_news_issue(&g_sim);
    CHECK(!item(1)[0] && issue[1] == 0); /* left at 30, its slot cleared */
    CHECK(item(2)[0] && issue[2] == 3 && get16(item(2) + 2) == 29); /* the issue was no longer full */
    CHECK(get16(item(0) + 2) == 21 && get16(item(3) + 2) == 21);
}

/* 0x42982 / 0x42AEA: thresholds and opinion changes of the bill items */
static int bill_item(int bill, int ward, int *change, int *weight, int *chance, int *param) {
    fresh();
    uint8_t *r = world_hospital(&g_world, 3);
    put16(r + (ward ? 0x110 : 0x10E), 999); /* no record */
    person(5)[81] = 3;
    sim_news_bill(&g_sim, 3, 5, bill, ward);
    for (int i = 0; i < 100; i++)
        if (item(i)[0]) {
            *change = (int8_t)item(i)[5], *weight = item(i)[4], *chance = item(i)[6], *param = item(i)[21];
            CHECK(item(i)[0] == 2 && item(i)[20] == 5 && item(i)[8] == 3 && get16(item(i) + 16) == 5);
            CHECK(get16(item(i) + 22) == bill);
            return 1;
        }
    return 0;
}

static void test_bills(void) {
    static const struct { int bill, ward, d; } CASES[] = {
        {29, 0, 0}, {30, 0, -1}, {34, 0, -1}, {35, 0, -2}, {39, 0, -2}, {40, 0, -4}, {44, 0, -4}, {45, 0, -7},
        {53, 0, -7}, {119, 1, 0}, {120, 1, -1}, {149, 1, -1}, {150, 1, -2}, {189, 1, -2}, {190, 1, -4},
        {229, 1, -4}, {230, 1, -7}, {300, 1, -7}, {100, 0, -7},
    };
    for (size_t k = 0; k < sizeof CASES / sizeof *CASES; k++) {
        int d = 0, w = 0, b6 = 0, b21 = -1;
        int made = bill_item(CASES[k].bill, CASES[k].ward, &d, &w, &b6, &b21);
        CHECK(made == (CASES[k].d != 0));
        if (made) CHECK(d == CASES[k].d && w == 5 && b6 == 4 && b21 == CASES[k].ward);
        if (made != (CASES[k].d != 0) || (made && d != CASES[k].d))
            fprintf(stderr, "  bill %d ward %d: item %d d %d\n", CASES[k].bill, CASES[k].ward, made, d);
    }

    /* records: a new city record, then a tie */
    for (int ward = 0; ward < 2; ward++) {
        fresh();
        uint8_t *r = world_hospital(&g_world, 4);
        person(6)[81] = 4;
        int b = ward ? 200 : 40, field = ward ? 0x110 : 0x10E, rec = ward ? 0x28 : 0x26, rh = ward ? 0x2C : 0x2A;
        world_set_tail(&g_world, rec, b - 1);
        sim_news_bill(&g_sim, 4, 6, b, ward);
        CHECK(get16(r + field) == b && world_tail(&g_world, rec) == b && world_tail(&g_world, rh) == 4);
        CHECK(item(0)[21] == (ward ? 4 : 2) && item(0)[4] == 14 && (int8_t)item(0)[5] == -2 && item(0)[6] == 5);
        put16(r + field, 0);
        sim_news_bill(&g_sim, 4, 6, b, ward);
        CHECK(item(1)[21] == (ward ? 5 : 3) && item(1)[4] == 12 && (int8_t)item(1)[5] == -2 && item(1)[6] == 4);
        CHECK(world_tail(&g_world, rec) == b);
        put16(r + field, b + 1); /* below the hospital's own maximum: no record test at all */
        sim_news_bill(&g_sim, 4, 6, b, ward);
        CHECK(item(2)[21] == ward && get16(r + field) == b + 1);
    }

    /* the gate: treatment-room items up to 30 in use, ward items up to 80, fewer than 5 always */
    fresh();
    world_set_tail(&g_world, 0x26, 999), world_set_tail(&g_world, 0x28, 999);
    put16(world_hospital(&g_world, 3) + 0x10E, 999), put16(world_hospital(&g_world, 3) + 0x110, 999);
    for (int i = 0; i < 31; i++) plant(i, 2, 6, 1, 0, 0, 0, 1);
    sim_news_bill(&g_sim, 3, 5, 40, 0);
    CHECK(sim_news_count(&g_sim) == 31); /* 31 in use */
    sim_news_bill(&g_sim, 3, 5, 200, 1);
    CHECK(sim_news_count(&g_sim) == 32 && item(31)[20] == 5);
    for (int i = 30; i < 32; i++) item(i)[0] = 0;
    sim_news_bill(&g_sim, 3, 5, 40, 0);
    CHECK(sim_news_count(&g_sim) == 31 && item(30)[20] == 5); /* 30 in use: written */
    for (int i = 0; i < 81; i++) plant(i, 2, 6, 1, 0, 0, 0, 1);
    sim_news_bill(&g_sim, 3, 5, 200, 1);
    CHECK(sim_news_count(&g_sim) == 81);
}

/* the other generators the engine calls: the give-up duplicate rule and the failed home treatment's b21 */
static void test_give_up_and_home_failure(void) {
    fresh();
    sim_news_gave_up(&g_sim, 2, 7, 2);
    sim_news_gave_up(&g_sim, 2, 8, 0);
    sim_news_gave_up(&g_sim, 2, 9, 1);
    sim_news_gave_up(&g_sim, 3, 9, 0);
    CHECK(sim_news_count(&g_sim) == 3 && item(1)[21] == 0 && item(2)[8] == 3);
    CHECK(item(0)[20] == 0x17 && (int8_t)item(0)[5] == -1 && item(0)[4] == 5 && item(0)[6] == 4);

    fresh();
    uint8_t *p = person(12);
    p[81] = 6, p[56] = 3;
    sim_news_home_failure(&g_sim, 12, 1);
    p[100] = 1;
    sim_news_home_failure(&g_sim, 12, 2);
    put16(p + 56, 0);
    sim_news_home_failure(&g_sim, 12, 0);
    CHECK(item(0)[21] == 2 && item(1)[21] == 6 && item(2)[21] == 5);
    CHECK(item(0)[20] == 4 && item(0)[8] == 6 && (int8_t)item(0)[5] == -1 && item(0)[4] == 6 && item(0)[6] == 3);
}

/* SIM-55 and FIX-54 (docs/mainloop.md, module H item 2, "Who leaves the value"): the give-up item's duplicate test
 * compares b21 with the scratch byte [0x45CF] (Sim.dup_byte), not with the new item's b21. Hospital 2 has a waiting-room
 * item (b21 0): with the byte at 1 a second one is written, at 0 a queue item (b21 1) is skipped; FIX-54 compares the new
 * item's own b21. The course item leaves its course id in the byte. */
static void test_sim55_give_up_duplicate(void) {
    for (int fix = 0; fix <= 1; fix++) {
        fixes_set(fix ? 1ull << 53 : 0);
        fresh();
        sim_news_gave_up(&g_sim, 2, 7, 0);
        CHECK(sim_news_count(&g_sim) == 1 && item(0)[21] == 0);
        g_sim.dup_byte = 1;
        sim_news_gave_up(&g_sim, 2, 8, 0); /* off: no item with b21 1, written; on: b21 0 exists, skipped */
        CHECK(sim_news_count(&g_sim) == (fix ? 1 : 2));
        g_sim.dup_byte = 0;
        sim_news_gave_up(&g_sim, 2, 9, 1); /* off: b21 0 exists, skipped; on: no item with b21 1, written */
        CHECK(sim_news_count(&g_sim) == 2);
        CHECK(item(1)[21] == (fix ? 1 : 0) && get16(item(1) + 16) == (fix ? 9 : 8));
    }
    fixes_set(0);
    fresh();
    g_sim.dup_byte = 0x55;
    sim_news_course(&g_sim, 7, 321, 6);
    CHECK(sim_news_count(&g_sim) == 1 && g_sim.dup_byte == 6);
}

/* SIM-52 (docs/mainloop.md, module H item 11): the price key draws s = r3 first, then k = r7 (one step each, after the
 * random(16) of the hospital), and writes an item when s + 2 < v with v the key as a signed byte. Keys: 0x24 = 4 in
 * hospital 5 (table 0x5516: k = 1 or 5 gives 0x24), every other 0. Script 5, 0, 5: s 0, k 5, an item (b21 0x24, b22 4,
 * w 9, d +1, b6 5); 5, 5, 0: s 1, k 0 (0x23), none - the reverse order would read them the other way round. A key of
 * 0x90 (-112) gives no item. */
static void test_sim52_price_key_order(void) {
    static const uint16_t TABLE[8] = {0x23, 0x24, 0x25, 0x20, 0x23, 0x24, 0x94, 0x25};
    static const struct { uint32_t v[3]; int key, item; } C[3] = {{{5, 0, 5}, 4, 1}, {{5, 5, 0}, 4, 0}, {{5, 0, 5}, 0x90, 0}};
    for (int c = 0; c < 3; c++) {
        fresh();
        for (int k = 0; k < 8; k++) put16(g_image + 0x5516 + 2 * k, TABLE[k]);
        uint8_t *r = world_hospital(&g_world, 5);
        r[0x15] = 0xFF, r[0x24] = (uint8_t)C[c].key;
        uint32_t v[16];
        for (int i = 0; i < 16; i++) v[i] = i < 3 ? C[c].v[i] : 7;
        SimScript sc = {v, 16, 0, 0, NULL};
        g_sim.rng_script = &sc;
        sim_call(&g_sim, 0x4C597);
        g_sim.rng_script = NULL;
        CHECK(sc.used == 3 && sc.extra == 0);
        CHECK(sim_news_count(&g_sim) == C[c].item);
        if (C[c].item) {
            const uint8_t *it = item(0);
            CHECK(it[0] == 1 && it[20] == 0x27 && it[8] == 5 && it[21] == 0x24 && it[22] == 4);
            CHECK(it[4] == 9 && (int8_t)it[5] == 1 && it[6] == 5);
        }
    }
}

/* SIM-66 (docs/mainloop.md, module H item 9): the race result writes b21 = 0, whatever the free slot's old item left */
static void test_sim66_race_item_b21(void) {
    fresh();
    item(0)[21] = 8; /* a free slot with an old b21 */
    sim_news_race(&g_sim, 3, 4, 10, 11, 2, 77);
    const uint8_t *it = item(0);
    CHECK(sim_news_count(&g_sim) == 1 && it[20] == 0x23 && it[21] == 0 && it[8] == 3 && it[9] == 4 && it[22] == 2);
}

/* 0x4C904 with the name copy 0x4C8D6 (docs/mainloop.md, module H, deaths; docs/ui-reports.md, SCR-9 (f)): the
 * person's name from the name pool to the item's bytes 29 on, ended by '$', over whatever the slot's previous item left
 * there; types 7 (at home) and 8 alike; a name longer than 40 bytes is cut at the item's end; no pool, an empty copy. */
static void test_death_name_copy(void) {
    static const char text[] = "Xyz$Anna Probe$Abcdefghijabcdefghijabcdefghijabcdefghijabcde$";
    static uint8_t table[12];
    put32(table, 0), put32(table + 4, 4), put32(table + 8, 15);
    static DataDb db;
    memset(&db, 0, sizeof db);
    db.pools[DAT_PATIENT_NAMES - DAT_PATIENT_NAMES] = (TextPool){(const uint8_t *)text, sizeof text - 1, table, 3};
    for (int at_home = 1; at_home >= 0; at_home--) {
        fresh();
        g_sim.people = &db;
        memset(item(0) + 20, 'Q', 50); /* a previous item's bytes */
        item(0)[20] = 0;
        person(1)[3] = at_home ? 0xFF : 0;
        sim_news_death(&g_sim, 2, 1);
        CHECK(item(0)[20] == (at_home ? 7 : 8) && get16(item(0) + 16) == 1);
        CHECK(memcmp(item(0) + 29, "Anna Probe$", 11) == 0 && item(0)[40] == 'Q');
    }
    fresh();
    g_sim.people = &db;
    person(2)[3] = 0xFF;
    sim_news_death(&g_sim, 2, 2);
    CHECK(memcmp(item(0) + 29, "Abcdefghijabcdefghijabcdefghijabcdefghij$", 41) == 0);
    fresh();
    g_sim.people = NULL;
    memset(item(0) + 29, 'Q', 41);
    person(1)[3] = 0xFF;
    sim_news_death(&g_sim, 2, 1);
    CHECK(item(0)[29] == '$');
}

/* SIM-18 (docs/sim-rooms2.md, "The death item of 0x70321 without a pathology place", item 5's test values): the walk's
 * index c and the name copy's address from the record's place (h, k), for every record of the table. */
static void test_icu_death_walk(void) {
    static const struct { int h, k; uint32_t c; int u16; } W[] = {
        {8, 0, 769851824u, 432}, {0, 0, 1012963040u, 38624}, {15, 9, 1012963319u, 38903},
        {3, 3, 2147482761u, 64649}};
    for (size_t i = 0; i < sizeof W / sizeof W[0]; i++) {
        IcuDeathWalk w = sim_news_icu_walk(W[i].h, W[i].k);
        CHECK(w.c == W[i].c && (int)(w.c & 0xFFFF) == W[i].u16);
    }
    CHECK(sim_news_icu_walk(8, 0).linear == 0xB7AB03F2u);
    CHECK((sim_news_icu_walk(2, 7).c & 0xFFFF) == 339 && (sim_news_icu_walk(13, 3).c & 0xFFFF) == 525);
    CHECK(sim_news_icu_walk(3, 3).offset - 0x1A1CA == 29764);
    CHECK(sim_news_icu_walk(8, 6).offset - 0x1A1CA == 30136);
    CHECK(sim_news_icu_walk(13, 9).offset - 0x1A1CA == 30508);
    int below = 0, in_staff = 0;
    for (int h = 0; h < 16; h++)
        for (int k = 0; k < 10; k++) {
            IcuDeathWalk w = sim_news_icu_walk(h, k);
            CHECK(w.c * 106u == (uint32_t)(-100160 + 1860 * h + 186 * k) && w.c < 0x80000000u); /* meets R, mod 2^31 */
            int is_below = w.linear < 0x1000000u, is_staff = w.offset >= 0x1A1CA && w.offset < 140266;
            CHECK(is_below == is_staff); /* below the RAM's end only the staff table */
            below += is_below, in_staff += is_staff;
        }
    CHECK(below == 3 && in_staff == 3); /* the other 157 lie above the RAM */
}

/* The item itself (items 3 and 4, test values of item 5): synthetic pool "Ab$Cd Ef$", staff block zeros unless set.
 * Bytes 37-69 keep the slot's previous contents; R byte 3 negative gives type 7; type 8's b22 is R +4, or R +0x42 when
 * the hospital has a pathology (site byte 18) and R +4 is 0x12. */
static void test_icu_death_item(void) {
    static const char text[] = "Ab$Cd Ef$";
    static uint8_t table[8];
    put32(table, 0), put32(table + 4, 3);
    static DataDb db;
    memset(&db, 0, sizeof db);
    db.pools[0] = (TextPool){(const uint8_t *)text, sizeof text - 1, table, 2};
    uint8_t rec[186];
    memset(rec, 0, sizeof rec);
    fresh();
    g_sim.people = &db;
    memset(item(0) + 20, 'Q', 50);
    item(0)[20] = 0;
    sim_news_icu_death(&g_sim, 8, 0, rec);
    const uint8_t *it = item(0);
    CHECK(it[0] == 2 && it[20] == 8 && it[4] == 13 && (int8_t)it[5] == -2 && it[6] == 4 && it[8] == 8 && it[28] == 0);
    CHECK(get16(it + 16) == 432 && it[22] == 0);
    CHECK(memcmp(it + 29, "\0Ab$", 4) == 0 && it[33] == 'Q' && it[69] == 'Q');
    /* the three records whose dword lies in the staff table: the live staff dword decides */
    fresh(), g_sim.people = &db;
    uint8_t *staff = g_world.block[BLK_STAFF];
    put32(staff + 29764, 3);
    sim_news_icu_death(&g_sim, 3, 3, rec);
    CHECK((uint16_t)get16(item(0) + 16) == 64649 && memcmp(item(0) + 29, "Cd Ef$", 6) == 0);
    fresh(), g_sim.people = &db, staff = g_world.block[BLK_STAFF];
    put32(staff + 29764, -1);
    sim_news_icu_death(&g_sim, 3, 3, rec);
    CHECK(memcmp(item(0) + 29, "\0Ab$", 4) == 0);
    fresh(), g_sim.people = &db, staff = g_world.block[BLK_STAFF];
    put32(staff + 29764, 0x68439612);
    memset(item(0) + 29, 'Q', 41);
    sim_news_icu_death(&g_sim, 3, 3, rec);
    CHECK(item(0)[29] == '$' && item(0)[30] == 'Q'); /* the departure: an empty name */
    fresh(), g_sim.people = &db; /* pool offset 0 (staff zeros): the entry there, without the 0 byte */
    sim_news_icu_death(&g_sim, 8, 6, rec);
    CHECK(memcmp(item(0) + 29, "Ab$", 3) == 0);
    fresh(), g_sim.people = &db, staff = g_world.block[BLK_STAFF];
    put32(staff + 30508, 3);
    sim_news_icu_death(&g_sim, 13, 9, rec);
    CHECK(memcmp(item(0) + 29, "Cd Ef$", 6) == 0);
    /* type 7 by R byte 3 */
    fresh(), g_sim.people = &db;
    rec[3] = 0xFF;
    sim_news_icu_death(&g_sim, 2, 7, rec);
    CHECK(item(0)[20] == 7 && (int8_t)item(0)[5] == -3 && get16(item(0) + 16) == 339);
    /* type 8's b22 */
    rec[3] = 0, rec[4] = 0x12, rec[0x42] = 7;
    fresh(), g_sim.people = &db;
    sim_news_icu_death(&g_sim, 13, 3, rec);
    CHECK(item(0)[20] == 8 && item(0)[22] == 0x12 && get16(item(0) + 16) == 525);
    fresh(), g_sim.people = &db;
    g_world.block[BLK_ROOM_COUNTS][13 * 100 + 18] = 1;
    sim_news_icu_death(&g_sim, 13, 3, rec);
    CHECK(item(0)[22] == 7);
    /* the draws are those of a normal type-8 death: r3, r3 */
    fresh(), g_sim.people = &db;
    rec[4] = 0;
    for (int i = 0; i < 20; i++) {
        memset(item(0), 0, 70);
        Sim twin = g_sim;
        sim_news_icu_death(&g_sim, 13, 3, rec);
        CHECK(item(0)[21] == sim_bits(&twin, 3) && item(0)[23] == sim_bits(&twin, 3));
        CHECK(!item(1)[0]);
    }
    /* then 0x4CA9B as for every death: hospital 13 with the most deaths gets its item 0x33 */
    fresh(), g_sim.people = &db;
    put16(world_hospital(&g_world, 13) + 0x38, 20);
    sim_news_icu_death(&g_sim, 13, 3, rec);
    CHECK(item(1)[0] == 2 && item(1)[20] == 0x33 && item(1)[8] == 13 && get16(item(1) + 24) == 20);
    g_sim.people = NULL;
}

/* 0x46D40 (docs/mainloop.md, module H item 11a): h = random(16), k = r7, o = table 0x5506[k]; an item only for a
 * hospital in business whose byte at o is exactly 1, for 0x20 with a Krankenzimmer or neuro ward, for 0x22 with an OP
 * or torture chamber; class 1, type 0x26, w 8, d -1, b6 3, byte 8 = h, b21 = o, b28 = 0. The copy-protection counter
 * +0x16E / +0x172 is never touched. The twin predicts h and k from the same random stream. */
static const uint16_t KEYS[8] = {0x20, 0x24, 0x22, 0x23, 0x24, 0x25, 0x94, 0x25};
static void key_world(int key_value, int in_business, int wards, int theatres) {
    fresh();
    for (int k = 0; k < 8; k++) put16(g_image + 0x5506 + 2 * k, KEYS[k]);
    for (int h = 0; h < 16; h++) {
        uint8_t *r = world_hospital(&g_world, h), *c = g_world.block[BLK_ROOM_COUNTS] + h * 100;
        r[0x15] = (uint8_t)in_business;
        for (int k = 0; k < 8; k++) r[KEYS[k]] = (uint8_t)key_value;
        c[6] = (uint8_t)wards, c[16] = 0, c[7] = (uint8_t)theatres, c[8] = 0;
    }
}
static int predict(int *h) { /* the draws of 0x46D40 with fewer than 5 items (no gate draw) */
    Sim twin = g_sim;
    *h = (int)sim_random(&twin, 16);
    return KEYS[sim_bits(&twin, 7)];
}
static void test_dearest_price_key(void) {
    int seen20 = 0, seen22 = 0, seen = 0;
    for (int n = 0; n < 200; n++) {
        key_world(1, 1, 1, 1);
        sim_seed(&g_sim, 1000u + (uint64_t)n); /* a stream of its own per case */
        for (int i = 0; i < n % 7; i++) sim_random(&g_sim, 2); /* move the stream */
        int h, o = predict(&h);
        sim_call(&g_sim, 0x46D40);
        const uint8_t *it = item(0);
        CHECK(it[0] == 1 && it[20] == 0x26 && it[8] == h && it[21] == o && it[4] == 8 && (int8_t)it[5] == -1);
        CHECK(it[6] == 3 && it[28] == 0 && sim_news_count(&g_sim) == 1);
        for (int k = 0; k < 16; k++)
            CHECK(get32(world_hospital(&g_world, k) + 0x16E) == 0 && get16(world_hospital(&g_world, k) + 0x172) == 0);
        seen20 += o == 0x20, seen22 += o == 0x22, seen++;
    }
    CHECK(seen20 > 0 && seen22 > 0 && seen == 200);
    /* the refusals: a key of 2 or 0, a hospital not in business, 0x20 without a ward, 0x22 without a theatre */
    int refused[4] = {0, 0, 0, 0}, tried20 = 0, tried22 = 0;
    for (int n = 0; n < 400; n++) {
        int kase = n % 4;
        key_world(kase == 0 ? (n % 8 ? 2 : 0) : 1, kase != 1, kase != 2, kase != 3);
        sim_seed(&g_sim, 2000u + (uint64_t)n);
        for (int i = 0; i < n % 7; i++) sim_random(&g_sim, 2);
        int h, o = predict(&h);
        sim_call(&g_sim, 0x46D40);
        int want = kase == 0 || kase == 1 ? 0 : kase == 2 ? o != 0x20 : o != 0x22;
        tried20 += kase == 2 && o == 0x20, tried22 += kase == 3 && o == 0x22;
        CHECK(sim_news_count(&g_sim) == want);
        refused[kase] += !want;
    }
    CHECK(refused[0] == 100 && refused[1] == 100 && tried20 > 0 && tried22 > 0 && refused[2] == tried20 && refused[3] == tried22);
    /* the gate 50/50: with 5-50 items in use one call in 50 passes (the draw), with more than 50 none */
    int written = 0;
    for (int n = 0; n < 40; n++) {
        key_world(1, 1, 1, 1);
        for (int i = 0; i < 50; i++) plant(i + 10, 2, 9, 1, 0, 0, 0, 1);
        for (int i = 0; i < n; i++) sim_random(&g_sim, 2);
        sim_call(&g_sim, 0x46D40);
        written += sim_news_count(&g_sim) == 51;
    }
    CHECK(written <= 6);
    key_world(1, 1, 1, 1);
    for (int i = 0; i < 51; i++) plant(i + 10, 2, 9, 1, 0, 0, 0, 1);
    for (int n = 0; n < 2000; n++) sim_call(&g_sim, 0x46D40);
    CHECK(sim_news_count(&g_sim) == 51);
}

/* 0x495A0 (docs/sim-staff2.md): gate 0x42DD5 with limit 90, no draw in it; r1, and a 0 ends it for the player's
 * hospital only; class 1, type 0x1F, w 8, d +1, b6 3, byte 8 = h, b21 = the course id, b22 = a new r1, u16 16 = the
 * employee, b28 = 0. The twin predicts both draws. */
static void test_course_item(void) {
    int player_items = 0, player_tries = 0, b22[2] = {0, 0};
    for (int n = 0; n < 400; n++) {
        fresh();
        sim_seed(&g_sim, 3000u + (uint64_t)n);
        int h = n % 2 ? 7 : 0;
        Sim twin = g_sim;
        int first = (int)sim_bits(&twin, 1), second = (int)sim_bits(&twin, 1);
        item(0)[22] = 0x55, item(0)[28] = 9; /* a previous item's leftovers */
        sim_news_course(&g_sim, h, 321, 6);
        int want = h || first;
        CHECK(sim_news_count(&g_sim) == want);
        if (!h) player_tries++, player_items += want;
        if (!want) continue;
        const uint8_t *it = item(0);
        CHECK(it[0] == 1 && it[20] == 0x1F && it[4] == 8 && (int8_t)it[5] == 1 && it[6] == 3 && it[8] == h);
        CHECK(it[21] == 6 && it[22] == second && get16(it + 16) == 321 && it[28] == 0 && it[1] == 0);
        b22[second]++;
    }
    CHECK(player_tries == 200 && player_items > 70 && player_items < 130 && b22[0] > 0 && b22[1] > 0);
    /* the gate: more than 90 in use - nothing and no draw; 90 in use - an item (no gate draw: no chance) */
    for (int used = 90; used <= 91; used++) {
        fresh();
        for (int i = 0; i < used; i++) plant(i, 2, 9, 1, 0, 0, 0, 1);
        Sim twin = g_sim;
        sim_news_course(&g_sim, 5, 1, 2);
        CHECK(sim_news_count(&g_sim) == 91); /* 90 + the new item, or the 91 planted ones */
        if (used == 90) CHECK(item(90)[20] == 0x1F && item(90)[8] == 5);
        else CHECK(item(91)[0] == 0 && sim_bits(&twin, 31) == sim_bits(&g_sim, 31)); /* the stream did not move */
    }
}

/* 0x4E58A (docs/news-generators.md, section 8): the campaign end. Gate 85, no draw; class 2, w 11, d 0, b6 0, byte 8 =
 * the hospital, u10 = 2 x (positive AND 255) - negative, b21 = 0 normal end / 1 otherwise, b22 type, b23 slogan, b24-b26
 * the counts' low bytes, b28 0, bytes 1-19 cleared, b27 and b29-69 the slot's leftovers. */
static void campaign(int h, int type, int slogan, int left, int good, int neutral, int bad) {
    uint8_t *r = world_hospital(&g_world, h);
    r[0xF4] = (uint8_t)type, r[0xF5] = (uint8_t)slogan;
    put16(r + 0xFA, left), put16(r + 0xFC, good), put16(r + 0xFE, neutral), put16(r + 0x100, bad);
}

static void test_campaign_item(void) {
    /* both ends in one minute through the campaign minute 0x4E368: the player's hospital and an AI one, two items */
    fresh();
    for (int i = 0; i < 5; i++) plant(i, 2, 9, 1, 0, 0, 0, 1);
    memset(item(5), 0x77, 70), memset(item(6), 0x77, 70); /* a previous item's leftovers, the slots free */
    item(5)[0] = item(6)[0] = 0;
    campaign(0, 1, 5, 1, 7, 4, 3);
    campaign(5, 3, 11, 1, 1, 0, 9);
    Sim twin = g_sim;
    sim_call(&g_sim, 0x4E368);
    CHECK(sim_news_count(&g_sim) == 7 && g_sim.news_written[0x15] == 2);
    const uint8_t *it = item(5), *r0 = world_hospital(&g_world, 0);
    CHECK(it[0] == 2 && it[20] == 0x15 && it[4] == 11 && it[5] == 0 && it[6] == 0 && it[8] == 0);
    CHECK(get16(it + 10) == 11 && it[21] == 0 && it[22] == 1 && it[23] == 5);
    CHECK(it[24] == 7 && it[25] == 4 && it[26] == 3 && it[28] == 0 && it[27] == 0x77 && it[29] == 0x77 && it[69] == 0x77);
    int cleared = 1;
    for (int b = 1; b < 20; b++) cleared &= b == 4 || b == 8 || b == 10 || b == 11 || it[b] == 0;
    CHECK(cleared);
    CHECK(get16(r0 + 0xFA) == -1 && get16(r0 + 0x106) == -1 && r0[0x108] == 30 && r0[0xF4] == 1 && get16(r0 + 0xFC) == 7);
    it = item(6);
    CHECK(it[20] == 0x15 && it[8] == 5 && get16(it + 10) == -7 && it[21] == 0 && it[22] == 3 && it[23] == 11);
    CHECK(it[24] == 1 && it[25] == 0 && it[26] == 9);
    CHECK(sim_bits(&twin, 31) == sim_bits(&g_sim, 31)); /* no draw */
    /* a running campaign writes nothing */
    fresh();
    campaign(3, 2, 0, 5, 1, 1, 1);
    sim_call(&g_sim, 0x4E368);
    CHECK(sim_news_count(&g_sim) == 0 && get16(world_hospital(&g_world, 3) + 0xFA) == 4);
    /* the scandal (+0xFA = -2) and any other value give b21 1; no reactions give u10 0 */
    static const struct { int left, good, neutral, bad, b21, u10, b24, b26; } C[] = {
        {-2, 6, 0, 2, 1, 10, 6, 2}, {3, 6, 0, 2, 1, 10, 6, 2}, {-1, 0, 0, 0, 0, 0, 0, 0},
        {-1, 300, 0, 2, 0, 86, 44, 2},    /* the low byte of the positive count: 2 x 44 - 2, not 598 */
        {-1, 1, 0, 300, 0, -298, 1, 44}, /* the whole word of the negative count */
        {-2, 256, 7, 1, 1, -1, 0, 1}};
    for (int k = 0; k < 6; k++) {
        fresh();
        campaign(9, 2, 7, C[k].left, C[k].good, C[k].neutral, C[k].bad);
        sim_news_campaign(&g_sim, 9);
        it = item(0);
        CHECK(it[20] == 0x15 && it[8] == 9 && it[21] == C[k].b21 && get16(it + 10) == C[k].u10);
        CHECK(it[24] == C[k].b24 && it[25] == C[k].neutral && it[26] == C[k].b26 && it[22] == 2 && it[23] == 7);
    }
    /* the gate: 85 items in use - an item; 86 - none; fewer than 5 always */
    for (int used = 85; used <= 86; used++) {
        fresh();
        for (int i = 0; i < used; i++) plant(i, 2, 9, 1, 0, 0, 0, 1);
        campaign(2, 1, 1, -1, 1, 1, 1);
        sim_news_campaign(&g_sim, 2);
        CHECK(sim_news_count(&g_sim) == 86);
        CHECK(used == 85 ? item(85)[20] == 0x15 : item(86)[0] == 0);
    }
}

/* ---- the empty slot (docs/news-generators.md section 9.4, SIM-15) and the four writers of section 9.3 (SIM-30) ---- */

static void mark(int i) { memset(item(i) + 1, 0x5A, 69), item(i)[0] = 0; } /* a free slot with the marker in 1-69 */
static int marked(const uint8_t *it, int from, int to) {
    for (int b = from; b <= to; b++)
        if (it[b] != 0x5A) return 0;
    return 1;
}
static int zero_but(const uint8_t *it, const int *keep, int n) { /* bytes 1-19 are 0 except the listed ones */
    for (int b = 1; b < 20; b++) {
        int listed = 0;
        for (int i = 0; i < n; i++) listed |= keep[i] == b;
        if (!listed && it[b]) return 0;
    }
    return 1;
}

/* row 3: day 25, the season's flag 100, the leader 30 points: the team leaders leave d 1, b6 2, b20 0x24, b21 0; the
 * drivers d 1, w 15, b20 0x25, b21 0; bytes 22-69 keep the marker. SIM-30: the drivers write u14 = the contest day and
 * b6 3 in both branches. */
static void contest_world(int day, int flag_a, int flag_b) {
    fresh();
    put16(g_world.block[34] + 2 * 3, 30); /* team points: hospital 3 */
    uint8_t *e = g_world.block[35]; /* driver entries: driver 77 of hospital 3 with 30 points */
    put16(e, 77), put16(e + 2, 3), put16(e + 4, 30);
    world_set_tail(&g_world, 0x2E, day);
    g_world.block[WORLD_TAIL][0x30] = (uint8_t)flag_a, g_world.block[WORLD_TAIL][0x31] = (uint8_t)flag_b;
    mark(0);
}
static void test_empty_contest(void) {
    contest_world(25, 100, 0);
    sim_call(&g_sim, 0x515CB);
    const uint8_t *it = item(0);
    CHECK(sim_news_count(&g_sim) == 0 && it[0] == 0 && it[5] == 1 && it[6] == 2 && it[4] == 0);
    CHECK(zero_but(it, (const int[]){5, 6}, 2) && it[20] == 0x24 && it[21] == 0 && marked(it, 22, 69));
    contest_world(25, 0, 100);
    sim_call(&g_sim, 0x5176C);
    it = item(0);
    CHECK(sim_news_count(&g_sim) == 0 && it[0] == 0 && it[4] == 15 && it[5] == 1 && it[6] == 0);
    CHECK(zero_but(it, (const int[]){4, 5}, 2) && it[20] == 0x25 && it[21] == 0 && marked(it, 22, 69));
    /* controls: the flag 0 writes the season's last item (and sets the flag); before day 25 the running item */
    for (int day = 10; day <= 25; day += 15) {
        contest_world(day, 0, 0);
        sim_call(&g_sim, 0x5176C);
        it = item(0);
        CHECK(sim_news_count(&g_sim) == 1 && it[0] == 2 && it[20] == 0x25 && it[21] == (day == 25));
        CHECK(get16(it + 14) == day && it[6] == 3 && it[8] == 3 && get16(it + 16) == 77 && get16(it + 10) == 30);
        CHECK(g_world.block[WORLD_TAIL][0x31] == (day == 25 ? 100 : 0));
        contest_world(day, 0, 0);
        sim_call(&g_sim, 0x515CB);
        CHECK(sim_news_count(&g_sim) == 1 && item(0)[20] == 0x24 && get16(item(0) + 14) == day);
    }
}

/* row 4: the price keys with nobody in business (and 0x4C597 with every key at 1): bytes 1-19 = 0, 20-69 the marker */
static void test_empty_price_keys(void) {
    for (int k = 0; k < 3; k++) {
        key_world(1, k == 2, 1, 1);
        if (k == 2)
            for (int i = 0; i < 8; i++) put16(g_image + 0x5516 + 2 * i, KEYS[i]); /* 0x4C597's table; every key 1 */
        mark(0);
        sim_call(&g_sim, k == 1 ? 0x46D40 : 0x4C597);
        const uint8_t *it = item(0);
        CHECK(sim_news_count(&g_sim) == 0 && it[0] == 0 && zero_but(it, NULL, 0) && marked(it, 20, 69));
    }
}

/* row 5: a death of hospital 3 with every +0x38 = 0: the death item in slot 0, slot 1 cleared in 1-19 */
static void test_empty_most_deaths(void) {
    fresh();
    mark(0), mark(1), mark(2);
    sim_news_death(&g_sim, 3, 5);
    CHECK(sim_news_count(&g_sim) == 1 && item(0)[0] == 2 && item(0)[8] == 3);
    CHECK(item(1)[0] == 0 && zero_but(item(1), NULL, 0) && marked(item(1), 20, 69) && marked(item(2), 1, 69));
}

/* row 6: the price news at hour 2 for the b21 = 1 case (+0x109 0, +0x10A 100, +0x10C 90): byte 8 = h, b20 0x16 */
static void test_empty_price_news(void) {
    for (int hour = 2; hour <= 10; hour += 8) {
        fresh();
        world_set_tail(&g_world, TAIL_HOUR, hour);
        uint8_t *r = world_hospital(&g_world, 6);
        r[0x109] = 0, put16(r + 0x10A, 100), put16(r + 0x10C, 90);
        mark(0);
        sim_news_price(&g_sim, 6);
        const uint8_t *it = item(0);
        CHECK(sim_news_count(&g_sim) == (hour == 10) && it[8] == 6 && it[20] == 0x16);
        if (hour == 2) CHECK(it[0] == 0 && zero_but(it, (const int[]){8}, 1) && marked(it, 21, 69));
        else CHECK(it[0] == 2 && it[21] == 1 && it[4] == 8);
    }
}

/* SIM-30 item 1: the long stay clears nothing; u16 16 = 0; in the slot of a deleted printed item (byte 1 = 1, age 30,
 * byte 7 = 2) with a full issue the next ageing clears issue place 2 */
static void test_long_stay(void) {
    fresh();
    mark(0);
    Sim twin = g_sim;
    int b21 = (int)sim_bits(&twin, 1), b22 = (int)sim_bits(&twin, 1);
    sim_news_long_stay(&g_sim, 4);
    const uint8_t *it = item(0);
    CHECK(sim_news_count(&g_sim) == 1 && it[0] == 2 && it[20] == 0x1A && it[4] == 13 && (int8_t)it[5] == -2);
    CHECK(it[6] == 6 && it[8] == 4 && get16(it + 16) == 0 && it[21] == b21 && it[22] == b22 && it[28] == 0);
    CHECK(marked(it, 1, 3) && it[7] == 0x5A && marked(it, 9, 15) && marked(it, 18, 19) && marked(it, 23, 27));
    CHECK(marked(it, 29, 69) && g_sim.news_written[0x1A] == 1);
    /* the deleted printed item's slot */
    fresh();
    uint8_t *old = plant(0, 0, 0x33, 3, 30, 4, -3, 2);
    old[1] = 1, old[7] = 2;
    for (int j = 0; j < 4; j++) plant(j + 1, j ? 2 : 1, 9 + j, 1, 5, 0, 0, 1);
    uint8_t *issue = sim_news_issue(&g_sim);
    for (int j = 0; j < 4; j++) item(j + 1)[1] = 1, item(j + 1)[7] = (uint8_t)j, issue[j] = (uint8_t)(j + 2);
    sim_news_long_stay(&g_sim, 4);
    CHECK(item(0)[0] == 2 && item(0)[1] == 1 && get16(item(0) + 2) == 30 && item(0)[7] == 2);
    sim_call(&g_sim, 0x474BC);
    CHECK(item(0)[0] == 0 && issue[2] == 0 && issue[0] == 2 && issue[1] == 3 && issue[3] == 5);
    CHECK(item(3)[0] == 2 && item(3)[1] == 1); /* the item that held place 2 stays, marked printed */
}

/* SIM-30 item 2: the price comparison writes b22 = r1 (after b28) */
static void test_price_compare_b22(void) {
    int seen[2] = {0, 0};
    for (int n = 0; n < 40; n++) {
        int highest = n % 2;
        fresh();
        world_set_tail(&g_world, TAIL_HOUR, 10);
        for (int i = 0; i < 10; i++) put16(g_image + 0x465E + 2 * i, 10 * i + 10);
        for (int h = 0; h < 16; h++) world_hospital(&g_world, h)[0x15] = 1;
        world_hospital(&g_world, 4)[0x23] = 9; /* 220, the highest */
        world_hospital(&g_world, 6)[0x95] = 20; /* 90, the lowest; the others 130, average 133 */
        sim_seed(&g_sim, 5000u + (uint64_t)n);
        mark(0);
        Sim twin = g_sim;
        int b22 = (int)sim_bits(&twin, 1);
        sim_call(&g_sim, highest ? 0x62728 : 0x625D3);
        const uint8_t *it = item(0);
        CHECK(sim_news_count(&g_sim) == 1 && it[20] == 0x16 && it[8] == (highest ? 4 : 6) && it[21] == (highest ? 3 : 4));
        CHECK(it[22] == b22 && it[28] == 0 && it[23] == 0x5A);
        seen[b22]++;
    }
    CHECK(seen[0] > 0 && seen[1] > 0);
}

/* SIM-30 item 4: the refusal items: gate 95, class 2; 0x1B w 9, d -1, b6 4; 0x1C w 13, d -2, b6 6; b21 = r1 */
static void test_refusal(void) {
    int seen[2] = {0, 0};
    for (int n = 0; n < 40; n++) {
        int type = n % 2 ? 0x1C : 0x1B;
        fresh();
        sim_seed(&g_sim, 6000u + (uint64_t)n);
        mark(0);
        Sim twin = g_sim;
        int b21 = (int)sim_bits(&twin, 1);
        sim_news_refusal(&g_sim, 7, 123, type);
        const uint8_t *it = item(0);
        CHECK(it[0] == 2 && it[20] == type && it[8] == 7 && get16(it + 16) == 123 && it[21] == b21 && it[28] == 0);
        if (type == 0x1B) CHECK(it[4] == 9 && (int8_t)it[5] == -1 && it[6] == 4);
        else CHECK(it[4] == 13 && (int8_t)it[5] == -2 && it[6] == 6);
        seen[b21]++;
    }
    CHECK(seen[0] > 0 && seen[1] > 0);
    for (int used = 95; used <= 96; used++) {
        fresh();
        for (int i = 0; i < used; i++) plant(i, 2, 9, 1, 0, 0, 0, 1);
        sim_news_refusal(&g_sim, 7, 123, 0x1B);
        CHECK(sim_news_count(&g_sim) == 96);
        CHECK(used == 95 ? item(95)[20] == 0x1B : item(96)[0] == 0);
    }
}

int main(void) {
    test_empty_contest();
    test_empty_price_keys();
    test_empty_most_deaths();
    test_empty_price_news();
    test_long_stay();
    test_price_compare_b22();
    test_refusal();
    test_course_item();
    test_campaign_item();
    test_opinion_effect();
    test_selection();
    test_ageing();
    test_bills();
    test_give_up_and_home_failure();
    test_sim55_give_up_duplicate();
    test_sim52_price_key_order();
    test_sim66_race_item_b21();
    test_dearest_price_key();
    test_death_name_copy();
    test_icu_death_walk();
    test_icu_death_item();
    world_free(&g_world);
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] all checks passed\n");
    return 0;
}
