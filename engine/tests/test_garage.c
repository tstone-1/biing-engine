/* The garage, the ambulance race, the AI garage and the rest of the desktop (docs/ui-garage.md, docs/desktop.md,
 * docs/saves.md "High scores"). Synthetic world and program image (see AGENTS.md): the car catalog, the room
 * descriptions, the item catalogs, names and staff values are made up, except where a case repeats one of the spec's
 * oracle measurements (the high-score entry on day 20: 1,385 + 400 / 8 = 1,435 below 1,850; 7,385 + 2,000 / 8 = 7,635
 * inserted as entry 10 with days 19 and rank 5). */
#include <stdio.h>
#include <string.h>

#include "desktop.h"
#include "estate.h"
#include "personnel.h"
#include "garage.h"
#include "ranking.h"
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
static uint8_t *pb(void) { return g_world.block[BLK_PLAYER0]; }
static int u16(const uint8_t *p) { return (uint16_t)get16(p); }
static uint8_t *site(int h) { return g_world.block[BLK_ROOM_COUNTS] + h * 100; }
static uint8_t *staff(int i) { return g_world.block[BLK_STAFF] + i * 32; }
static uint8_t *slot(int h, int i) { return staff_slots(s, h) + i * STAFF_SLOT_SIZE; }
static uint8_t *person(int i) { return g_world.block[BLK_PATIENTS] + i * 106; }
static uint8_t *G(int h) { return sim_room_record(s, h, 10, 0); }
static uint8_t *RB(void) { return g_world.block[BLK_34]; }
static uint8_t *E(int k) { return RB() + 0x0C + 4 * k; }
static uint8_t *place(int h, int k) { return G(h) + GR_PLACE + 4 * k; }
static uint8_t *T(void) { return g_world.block[WORLD_TAIL]; }
static int team(int h) { return get16(g_world.block[34] + 2 * h); }
static uint8_t *drv(int i) { return g_world.block[35] + 6 * i; }
static int next_bits(uint32_t mask) { /* what the next draw of the rules will give */
    Sim twin = g_sim;
    return (int)sim_bits(&twin, mask);
}
/* a generator state whose next draw AND mask is v */
static void seed_for(uint32_t mask, int v) {
    for (uint64_t seed = 1;; seed++) {
        sim_seed(&g_sim, seed * 0x9E3779B97F4A7C15ull);
        if (next_bits(mask) == v) return;
    }
}

/* car catalog: model m = speed 100 + 10 m, PS 40 + 10 m, list price 100 + 100 m, name text 900 + m */
static void catalog(void) {
    for (int m = 0; m < 7; m++) {
        uint8_t *c = g_image + CAR_CATALOG_AT + 20 * m;
        put16(c, 900 + m), put16(c + 2, 100 + 10 * m), put16(c + 4, 40 + 10 * m), put16(c + 6, 100 + 100 * m);
    }
    /* the garage's description (category 10): furniture +0x1C x 10, equipment +0x30 x 20, conditions +0x80 / +0x8A */
    uint8_t *d = g_image + 0x4758 + 14 * 10;
    put16(d, 0x1C), put16(d + 2, 10), put16(d + 4, 0x30), put16(d + 6, 20), put16(d + 8, 0x80), put16(d + 10, 0x8A);
    put16(d + 12, 0x1B);
    /* the store's (category 4) is not needed: the store record has its own layout */
}

static void fresh(void) {
    world_free(&g_world);
    world_alloc(&g_world);
    memset(g_world.block[BLK_ROOM_SLOTS], 0xFF, g_world.size[BLK_ROOM_SLOTS]);
    memset(g_image, 0, sizeof g_image);
    g_prog.image.data = g_image;
    g_prog.image.size = sizeof g_image;
    sim_init(&g_sim, &g_world, &g_prog, 7);
    catalog();
    for (int h = 0; h < 17; h++) put32(H(h) + H_CASH, 1000);
    world_set_tail(&g_world, TAIL_HOUR, 12);
    world_set_tail(&g_world, TAIL_MONTH, 2);
    world_set_tail(&g_world, TAIL_DAY, 10);
    for (int i = 0; i < 20; i++) g_world.block[BLK_CARS][i * 20] = 0xFF;
    memset(RB(), 0, 0x60);
    for (int k = 0; k < 17; k++) E(k)[0] = 0xFF;
    for (int i = 1; i < 1009; i++) person(i)[3] = person(i)[4] = 0xFF;
}

static void car(int h, int k, int model, int cond) {
    uint8_t *p = place(h, k);
    p[0] = (uint8_t)(model + 1), p[1] = (uint8_t)cond, p[2] = p[3] = 0;
}
/* an employee index of hospital h in slot i, group g */
static void employ(int h, int i, int index, int group) {
    put16(slot(h, i), index);
    slot(h, i)[2] = (uint8_t)group;
    staff(index)[1] = (uint8_t)group;
    staff(index)[3] = (uint8_t)h;
    staff(index)[21] = 100;
}
static void garage_for(int h, int driver, int mechanic) {
    site(h)[10] = 1;
    if (driver) employ(h, 10, driver, 6), put16(G(h) + GR_DRIVER, driver);
    if (mechanic) employ(h, 11, mechanic, 6), put16(G(h) + GR_MECHANIC, mechanic);
}
static void race_on(int p, int minute) {
    put16(RB(), p);
    put16(RB() + 2, minute);
}
static void enter(int k, int h, int pl, int dist) {
    E(k)[0] = (uint8_t)h, E(k)[1] = (uint8_t)pl;
    put16(E(k) + 2, dist);
}

/* ---- the car list and sending a car ------------------------------------------------------------------- */

static void test_list(void) {
    fresh();
    garage_for(0, 0, 0);
    CHECK(garage_list_open(s) == 996);
    G(0)[GR_CARS] = 2;
    CHECK(garage_list_open(s) == 997); /* counted, no place occupied */
    car(0, 3, 2, 50), car(0, 7, 5, 10);
    CHECK(garage_list_open(s) == 0 && u16(pb() + 0x0E) == 3);
    CHECK(garage_list_next(s) == 0 && u16(pb() + 0x0E) == 7);
    CHECK(garage_list_next(s) == 0 && u16(pb() + 0x0E) == 3); /* 10 wraps to 0 */
    put16(pb() + 0x0E, 9);
    car(0, 0, 0, 50); /* model 0: byte 1 */
    CHECK(garage_list_next(s) == 0 && u16(pb() + 0x0E) == 0);
    G(0)[GR_CARS] = 0;
    CHECK(garage_list_next(s) == 996);
}

static void test_send(void) {
    fresh();
    garage_for(0, 0, 0);
    G(0)[GR_CARS] = 3;
    car(0, 0, 1, 24), car(0, 1, 2, 90), car(0, 2, 3, 25);
    place(0, 1)[2] = 1; /* byte 2 set: not offered */
    CHECK(garage_send_open(s) == 994); /* no race */
    race_on(118, 40);
    enter(0, 5, 0, 300);
    CHECK(garage_send_open(s) == 998); /* no driver */
    employ(0, 4, 77, 6), put16(G(0) + GR_DRIVER, 77);
    CHECK(garage_send_open(s) == 0 && u16(pb() + 0x0E) == 2); /* 24 and byte 2 skipped, 25 taken */
    G(0)[GR_CARS] = 0;
    CHECK(garage_send_open(s) == 996);
    G(0)[GR_CARS] = 3;
    place(0, 2)[1] = 24;
    CHECK(garage_send_open(s) == 997);
    /* the send: no test of the car, the first free entry, distance 0, G+0x0C = FF */
    put16(pb() + 0x0E, 1);
    int k = -1;
    CHECK(garage_send(s, &k) == 0 && k == 1);
    CHECK(E(1)[0] == 0 && E(1)[1] == 1 && u16(E(1) + 2) == 0 && G(0)[GR_ENTERED] == 0xFF);
    CHECK(garage_send_open(s) == 995 && garage_send(s, &k) == 995); /* already entered */
    /* an entry of the player anywhere among the 16 refuses; a hospital byte of 0 is the player */
    fresh();
    garage_for(0, 77, 0);
    employ(0, 4, 77, 6);
    G(0)[GR_CARS] = 1, car(0, 0, 1, 80);
    race_on(118, 5);
    for (int i = 0; i < 16; i++) enter(i, i + 1 <= 15 ? i + 1 : 15, 0, 0);
    CHECK(garage_send_open(s) == 995); /* engine choice: no free entry refuses */
    E(15)[0] = 0xFF, E(9)[0] = 0;
    CHECK(garage_send_open(s) == 995);
    E(9)[0] = 0xFF;
    CHECK(garage_send(s, &k) == 0 && k == 9);
    /* the start place in the box is the entry slot */
    CHECK(G(0)[GR_ENTERED] == 0xFF);
}

static void test_tables(void) {
    fresh();
    world_set_tail(&g_world, T_RACES, 7);
    CHECK(garage_races_shown(s) == 7);
    race_on(5, 0);
    CHECK(garage_races_shown(s) == 6);
    uint8_t *pts = g_world.block[34];
    put16(pts + 2 * 3, 20), put16(pts + 2 * 5, 20), put16(pts + 2 * 9, 30), put16(pts + 2 * 1, -4);
    uint8_t o[16];
    garage_team_order(s, o);
    CHECK(o[0] == 9 && o[1] == 3 && o[2] == 5 && o[3] == 0 && o[4] == 2 && o[15] == 1); /* ties in order, signed */
    /* the driver table: entries with driver 0 lose their hospital and points, sorted by points (signed) */
    put16(drv(0), 0), put16(drv(0) + 2, 4), put16(drv(0) + 4, 99);
    put16(drv(1), 11), put16(drv(1) + 2, 3), put16(drv(1) + 4, 6);
    put16(drv(2), 12), put16(drv(2) + 2, 5), put16(drv(2) + 4, 10);
    put16(drv(3), 13), put16(drv(3) + 2, 5), put16(drv(3) + 4, -2);
    CHECK(garage_driver_rows(s) == 2); /* the rows stop at the first entry with driver 0 */
    CHECK(u16(drv(0)) == 12 && u16(drv(1)) == 11 && u16(drv(2)) == 0 && u16(drv(99)) == 13); /* -2 sorts last */
    int zero_ok = 1;
    for (int i = 2; i < 99; i++) zero_ok &= !u16(drv(i)) && !u16(drv(i) + 2) && !u16(drv(i) + 4);
    CHECK(zero_ok); /* the entry with driver 0 lost its hospital 4 and its 99 points */
    /* at most 19 rows */
    for (int i = 0; i < 100; i++) put16(drv(i), 100 + i), put16(drv(i) + 4, 200 - i);
    CHECK(garage_driver_rows(s) == 19);
}

static void test_activity(void) {
    fresh();
    garage_for(3, 50, 51);
    staff(50)[4] = staff(51)[4] = 10;
    GarageLine l;
    CHECK(garage_activity(s, 50, 3, &l) && l.kind == 0 && l.a == 1023 && l.b == 1024); /* no race entered */
    CHECK(garage_activity(s, 51, 3, &l) && l.kind == 3 && l.a == 1055 && l.b == 1056);
    race_on(9, 3);
    CHECK(garage_activity(s, 51, 3, &l) && l.kind == 4 && l.a == 2475); /* 2475 alone while the race word is set */
    CHECK(garage_activity(s, 50, 3, &l) && l.a == 1023); /* AI drivers never get +0x0C */
    G(3)[GR_ENTERED] = 0xFF;
    CHECK(garage_activity(s, 50, 3, &l) && l.kind == 1 && l.a == 1025 && l.b == 1026);
    put16(RB() + 2, 29);
    CHECK(garage_activity(s, 50, 3, &l) && l.a == 1025);
    put16(RB() + 2, 30);
    CHECK(garage_activity(s, 50, 3, &l) && l.a == 1027 && l.b == 1028);
    put16(RB() + 2, 40), RB()[5] = 12;
    CHECK(garage_activity(s, 50, 3, &l) && l.a == 1042 && l.b == 1043); /* the player not entered: position 20 */
    static const int A[11] = {0, 1029, 1031, 1033, 1031, 1031, 1031, 1031, 1031, 1031, 1031};
    static const int B[11] = {0, 1030, 1032, 1034, 1035, 1036, 1037, 1038, 1039, 1040, 1041};
    for (int pos = 1; pos <= 11; pos++) {
        for (int k = 0; k < 17; k++) E(k)[0] = 0xFF;
        for (int k = 0; k < pos - 1; k++) enter(k, 5, 0, 0);
        enter(pos - 1, 0, 0, 0);
        CHECK(garage_player_position(s) == pos);
        CHECK(garage_activity(s, 50, 3, &l) && l.kind == 1 && l.a == (pos <= 10 ? A[pos] : 1042) &&
              l.b == (pos <= 10 ? B[pos] : 1043));
        /* the English CD writes 1042 where the German builds write 1031 (docs/editions.md 7.3, rule K) */
        g_prog.edition = EDITION_ENGLISH_CD;
        CHECK(garage_activity(s, 50, 3, &l) && l.a == (pos <= 10 && A[pos] != 1031 ? A[pos] : 1042) &&
              l.b == (pos <= 10 ? B[pos] : 1043));
        g_prog.edition = EDITION_GERMAN_CD;
        CHECK(garage_activity(s, 50, 3, &l) && l.a == (pos <= 10 ? A[pos] : 1042));
        g_prog.edition = EDITION_DISK;
    }
    RB()[5] = 0; /* after the last lap */
    for (int pos = 1; pos <= 9; pos++) {
        for (int k = 0; k < 17; k++) E(k)[0] = 0xFF;
        for (int k = 0; k < pos - 1; k++) enter(k, 5, 0, 0);
        enter(pos - 1, 0, 0, 0);
        garage_activity(s, 50, 3, &l);
        if (pos == 1) CHECK(l.a == 1044 && l.b == 1045);
        else if (pos <= 7) CHECK(l.kind == 1 && l.a == 1046 && l.b == 1045 + pos);
        else CHECK(l.kind == 2 && l.a == 1046 && l.c == 1053 && l.position == pos && l.b == 1054);
    }
    staff(50)[4] = 0;
    CHECK(!garage_activity(s, 50, 3, &l)); /* not in the garage */
    staff(50)[4] = 10;
    site(3)[10] = 0;
    CHECK(!garage_activity(s, 50, 3, &l));
}

static void test_gap(void) {
    char t[16];
    garage_gap_text(0, 4, 7, t);
    CHECK(!strcmp(t, "    0,047s") || !strcmp(t, "   0,047s"));
    garage_gap_text(53, 1, 2, t);
    CHECK(!strcmp(t, "   5,312s"));
    garage_gap_text(123, 0, 9, t);
    CHECK(!strcmp(t, "  12,309s"));
    garage_gap_text(725, 3, 3, t);
    CHECK(!strcmp(t, "1:12,533s"));
    garage_gap_text(640, 0, 0, t);
    CHECK(!strcmp(t, "1:04,000s")); /* the tens digit kept once the minute is shown */
}

/* a name pool for the long-name test: every name 17 characters except person 777's 16 */
static uint8_t g_names[1009 * 20];
static uint8_t g_table[1009 * 4];
static DataDb g_people;
static void names(void) {
    for (int i = 0; i < 1009; i++) {
        int n = i == 777 ? 16 : 17;
        memset(g_names + 20 * i, 0, 20);
        memset(g_names + 20 * i, 'a', (size_t)n);
        g_names[20 * i + n] = '$';
        put32(g_table + 4 * i, 20 * i);
    }
    memset(&g_people, 0, sizeof g_people);
    g_people.pools[0] = (TextPool){g_names, sizeof g_names, g_table, 1009};
    s->people = &g_people;
}

/* ---- the race start ------------------------------------------------------------------------------------ */

static void city(void) { /* 5 OPs, 4 garages, 4 cars */
    for (int h = 1; h <= 5; h++) site(h)[7] = 1;
    for (int h = 1; h <= 4; h++) site(h)[10] = 1, G(h)[GR_CARS] = 1;
}
static void eligible(int p) {
    uint8_t *q = person(p);
    memset(q, 0, 106);
    q[1] = 1, q[3] = 0xFF, q[4] = 0xFF;
}

/* the race start reads the city counts the last onset stored (SIM-51): store the fixture's counts first */
static int start_stored(void) {
    sim_store_city(s);
    return sim_race_start(s);
}
static int try_start(void) {
    seed_for(63, 4);
    return start_stored();
}

static void test_start(void) {
    fresh();
    city();
    for (int i = 1; i < 1009; i++) eligible(i);
    CHECK(try_start() == 1);
    int p = u16(RB());
    CHECK(p >= 1 && p <= 1008 && u16(RB() + 2) == 0 && world_tail(&g_world, T_RACES) == 1 && RB()[5] == 0);
    CHECK(RB()[4] <= 3 && s->race_redraw == 1);
    uint8_t *q = person(p);
    CHECK(q[3] == 16 && q[4] == 6 && q[40] == 0 && q[41] == 0x4A && q[42] == 0x2A && q[43] == 0x2C);
    CHECK(q[44] >= 35 && q[44] <= 66 && q[45] == 0 && q[56] == 0x2A && q[57] == 0 && q[66] == 0 && q[78] == 3);
    CHECK(u16(q + 94) >= 270 && u16(q + 94) <= 524 && (u16(q + 94) - 270) % 2 == 0);
    /* no second race while one is announced */
    CHECK(try_start() == 0);
    /* the gates one by one */
    static const int CASES = 9;
    for (int c = 0; c < CASES; c++) {
        fresh();
        city();
        for (int i = 1; i < 1009; i++) eligible(i);
        switch (c) {
        case 0: world_set_tail(&g_world, T_RACES, 25); break;
        case 1: world_set_tail(&g_world, TAIL_HOUR, 3); break;
        case 2: world_set_tail(&g_world, TAIL_HOUR, 18); break;
        case 3: site(5)[7] = 0; break;
        case 4: site(4)[10] = 0, G(1)[GR_CARS] = 2; break; /* 3 garages, still 4 cars */
        case 5: G(4)[GR_CARS] = 0; break;
        case 6: break; /* the r63 draw */
        case 7: world_set_tail(&g_world, TAIL_HOUR, 4); break; /* allowed */
        case 8: world_set_tail(&g_world, TAIL_HOUR, 17), world_set_tail(&g_world, T_RACES, 24); break; /* allowed */
        }
        int r;
        if (c == 6) {
            seed_for(63, 5);
            r = start_stored();
        } else {
            r = try_start();
        }
        CHECK(r == (c >= 7));
    }
    /* the cars are summed as a byte: 256 + 3 is 3 */
    fresh();
    city();
    for (int i = 1; i < 1009; i++) eligible(i);
    G(1)[GR_CARS] = 255, G(2)[GR_CARS] = 1, G(3)[GR_CARS] = 1, G(4)[GR_CARS] = 2; /* 259 -> 3 */
    CHECK(try_start() == 0);
    G(4)[GR_CARS] = 3;
    CHECK(try_start() == 1);
    /* the person's conditions: every person fails one of them except 777 */
    for (int field = 0; field < 7; field++) {
        fresh();
        city();
        for (int i = 1; i < 1009; i++) {
            eligible(i);
            uint8_t *pq = person(i);
            switch (i % 7) {
            case 0: pq[104] = 1; break;
            case 1: pq[1] = 0; break;
            case 2: pq[79] = 1; break;
            case 3: pq[41] = 2; break;
            case 4: pq[3] = 4; break;
            case 5: pq[57] = 1; break;
            case 6: pq[105] = 1; break;
            }
        }
        /* only the condition `field` fails for the persons i % 7 == field; the others are made eligible again */
        for (int i = 1; i < 1009; i++)
            if (i % 7 != field) eligible(i);
        int started = 0;
        for (int t = 0; t < 20 && !started; t++) started = try_start();
        CHECK(started); /* most persons are eligible */
        CHECK(u16(RB()) % 7 != field);
        /* every person fails this one condition: no race in any of ten tries */
        fresh();
        city();
        for (int i = 1; i < 1009; i++) {
            eligible(i);
            uint8_t *pq = person(i);
            switch (field) {
            case 0: pq[104] = 1; break;
            case 1: pq[1] = 0; break;
            case 2: pq[79] = 1; break;
            case 3: pq[41] = 2; break;
            case 4: pq[3] = 4; break;
            case 5: pq[57] = 1; break;
            case 6: pq[105] = 1; break;
            }
        }
        started = 0;
        for (int t = 0; t < 10; t++) started += try_start();
        CHECK(started == 0);
    }
    /* the drawn fields over 300 starts: field 44 = (t&31) + 35 covers 35-66, fields 94-95 = (t&127) x 2 + 270 */
    int lo44 = 999, hi44 = 0, lo94 = 9999, hi94 = 0;
    for (int t = 0; t < 300; t++) {
        fresh();
        city();
        eligible(500);
        for (int i = 1; i < 1009; i++)
            if (i != 500) person(i)[105] = 1;
        names();
        g_names[20 * 500 + 16] = '$', g_names[20 * 500 + 17] = 0; /* only 500 has a short name (16) */
        g_names[20 * 777 + 16] = 'a', g_names[20 * 777 + 17] = '$';
        sim_seed(&g_sim, (uint64_t)(t + 1) * 0x9E3779B97F4A7C15ull);
        while (next_bits(63) != 4) sim_bits(s, 1);
        if (!start_stored()) continue;
        uint8_t *q5 = person(500);
        if (q5[44] < lo44) lo44 = q5[44];
        if (q5[44] > hi44) hi44 = q5[44];
        if (u16(q5 + 94) < lo94) lo94 = u16(q5 + 94);
        if (u16(q5 + 94) > hi94) hi94 = u16(q5 + 94);
    }
    s->people = NULL;
    CHECK(lo44 == 35 && hi44 == 66 && lo94 <= 276 && lo94 >= 270 && hi94 >= 518 && hi94 <= 524);
    /* no person fits: no race after 100 tries */
    fresh();
    city();
    CHECK(try_start() == 0 && u16(RB()) == 0 && world_tail(&g_world, T_RACES) == 0);
}

/* SIM-51 (docs/news-generators.md, "The city test"): the race start reads [0x5BCD] / [0x5BD1] as the last onset stored
 * them, not the live room counts: after the program start (0) no race although the city has 5 OPs and 4 garages; with
 * the counts stored and then two OPs closed, the race starts all the same; stored 4 OPs: none. */
static void test_sim51_stored_city(void) {
    fresh();
    city();
    for (int i = 1; i < 1009; i++) eligible(i);
    seed_for(63, 4);
    CHECK(sim_race_start(s) == 0 && u16(RB()) == 0); /* nothing stored yet */
    sim_store_city(s);
    CHECK(s->city[CITY_OP] == 5 && s->city[CITY_GARAGE] == 4);
    site(5)[7] = 0, site(1)[7] = 0; /* two OPs closed: 3 live (the garages count the cars too) */
    seed_for(63, 4);
    CHECK(sim_race_start(s) == 1);
    fresh();
    city();
    for (int i = 1; i < 1009; i++) eligible(i);
    site(5)[7] = 0;
    sim_store_city(s); /* 4 OPs stored */
    site(5)[7] = 1;
    seed_for(63, 4);
    CHECK(sim_race_start(s) == 0);
}

static void test_start_names(void) {
    fresh();
    city();
    for (int i = 1; i < 1009; i++) eligible(i);
    names();
    for (int t = 0; t < 5; t++) {
        eligible(777);
        put16(RB(), 0);
        world_set_tail(&g_world, T_RACES, 0);
        CHECK(try_start() == 1 && u16(RB()) == 777); /* a long name is redrawn without using up a try */
    }
    person(777)[105] = 1;
    put16(RB(), 0);
    CHECK(try_start() == 0);
    s->people = NULL;
}

/* ---- the race minute ------------------------------------------------------------------------------------ */

static void racers(void) {
    fresh();
    for (int h = 1; h <= 3; h++) {
        garage_for(h, 100 + h, 0);
        G(h)[GR_CARS] = 1;
        car(h, 0, 6, 100);
        site(h)[7] = 1, site(h)[6] = 0;
    }
}

static void test_minute(void) {
    racers();
    race_on(55, 0);
    enter(0, 1, 0, 0);
    for (int i = 0; i < 24; i++) sim_race_minute(s);
    CHECK(u16(RB() + 2) == 24);
    sim_race_minute(s); /* 25 with one entry: back to 10 */
    CHECK(u16(RB() + 2) == 10);
    for (int i = 0; i < 14; i++) sim_race_minute(s);
    CHECK(u16(RB() + 2) == 24);
    enter(1, 2, 0, 0);
    for (int i = 0; i < 5; i++) sim_race_minute(s);
    CHECK(u16(RB() + 2) == 29 && RB()[5] == 0 && u16(E(0) + 2) == 0);
    sim_race_minute(s); /* 30: 50 laps, minus one, the first round */
    CHECK(u16(RB() + 2) == 30 && RB()[5] == 49 && u16(E(0) + 2) > 0 && u16(E(1) + 2) > 0);
    for (int i = 0; i < 48; i++) sim_race_minute(s);
    CHECK(u16(RB() + 2) == 78 && RB()[5] == 1 && RB()[6] == 0);
    uint16_t d0 = (uint16_t)u16(E(0) + 2);
    sim_race_minute(s); /* 79: the last lap flag, no round */
    CHECK(u16(RB() + 2) == 79 && RB()[5] == 0 && RB()[6] == 8 && u16(E(0) + 2) == d0);
    for (int i = 0; i < 7; i++) sim_race_minute(s);
    CHECK(RB()[6] == 1 && u16(RB()) == 55 && u16(RB() + 2) == 79);
    sim_race_minute(s); /* the finish */
    CHECK(u16(RB()) == 0 && u16(RB() + 2) == 79 && E(0)[0] == 0xFF && E(1)[0] == 0xFF);
    sim_race_minute(s); /* nothing without a race */
    CHECK(u16(RB() + 2) == 79);
    /* the countdown runs on with two entries, the loop needs fewer than 2 */
    racers();
    race_on(55, 24);
    enter(0, 1, 0, 0), enter(1, 2, 0, 0);
    sim_race_minute(s);
    CHECK(u16(RB() + 2) == 25);
}

/* SIM-63 (docs/ui-garage.md, "The minute", step 2): at minute 30 the race draws one r31 (site 0x6D55E) and drops it
 * before the first round; minute 29 draws nothing. SIM-55: the round's sort leaves [0x45CF] (Sim.dup_byte) at 0. */
static int logged_sites(unsigned site, int *first_is) {
    FILE *f = tmpfile();
    if (!f) return -1;
    g_sim.rng_log = f;
    sim_race_minute(s);
    g_sim.rng_log = NULL;
    rewind(f);
    unsigned st, n, v, d, e;
    char kind;
    int count = 0, first = 1;
    *first_is = 0;
    while (fscanf(f, "%X %c%u %u %X %X", &st, &kind, &n, &v, &d, &e) == 6) {
        if (first) *first_is = st == site && kind == 'b' && n == 31;
        first = 0;
        count += st == site;
    }
    fclose(f);
    return count;
}
static void test_sim63_minute30_r31(void) {
    racers();
    race_on(55, 28);
    enter(0, 1, 0, 0), enter(1, 2, 0, 0);
    int first;
    CHECK(logged_sites(0x6D55E, &first) == 0 && u16(RB() + 2) == 29); /* 29: the countdown */
    g_sim.dup_byte = 0x55;
    CHECK(logged_sites(0x6D55E, &first) == 1 && first && u16(RB() + 2) == 30 && RB()[5] == 49);
    CHECK(g_sim.dup_byte == 0 && u16(E(0) + 2) > 0); /* the round ran and sorted */
    CHECK(logged_sites(0x6D55E, &first) == 0);
}

static void test_formulas(void) {
    CHECK(sim_race_grid(30, 0) == 160 && sim_race_grid(30, 1) == 150 && sim_race_grid(39, 3) == 13);
    CHECK(sim_race_grid(39, 20) == 0 && sim_race_grid(40, 0) == 0 && sim_race_grid(30, 17) == 0);
    CHECK(sim_race_grid(35, 16) == 0 && sim_race_grid(35, 15) == 5);
    CHECK(sim_race_speed(210, 175, 0) == 0 && sim_race_speed(210, 175, 19) == 5 && sim_race_speed(210, 175, 1) == 5);
    CHECK(sim_race_speed(210, 175, 75) == 1190 && sim_race_speed(210, 175, 100) == 1190);
    CHECK(sim_race_speed(210, 175, 74) == 1190 - 18 * 26 / 100); /* 1190 / 64 = 18 */
    CHECK(sim_race_speed(210, 175, 20) == 1190 - 18 * 80 / 100);
    CHECK(sim_race_speed(215, 200, 50) == 1260 - 19 * 50 / 100); /* 1260 / 64 = 19 (63 would give 20) */
    uint8_t st[32] = {0};
    st[2] = 30, st[5] = 10, st[15] = 7, st[12] = 2, st[13] = 3;
    put16(st + 16, 41);
    int base = 10 * 4 + 7 + 2 * 16 + 3 * 8 + 41 / 4 + 40;
    CHECK(sim_race_driver(st, 0) == 30 * 70 / 2 + base && sim_race_driver(st, 4) == 30 * 70 / 2 + base);
    CHECK(sim_race_driver(st, 5) == 30 * 62 / 2 + base && sim_race_driver(st, 9) == 30 * 46 / 2 + base);
    CHECK(sim_race_driver(st, 10) == 30 * 2 / 2 + base && sim_race_driver(st, 30) == 30 + base);
}

/* one round with one entry, against the spec's formula with the same draws (a twin generator) */
static void test_round(void) {
    for (int seed = 1; seed <= 40; seed++) {
        racers();
        race_on(55, 33);
        RB()[5] = 20;
        enter(0, 1, 0, 100);
        uint8_t *c = place(1, 0);
        c[1] = (uint8_t)(seed * 7 % 101);
        slot(1, 10)[0x0A] = (uint8_t)(seed % 12);
        uint8_t *st = staff(101);
        st[2] = (uint8_t)(seed * 3), st[5] = 9, st[15] = 4;
        sim_seed(&g_sim, (uint64_t)seed * 0x2545F4914F6CDD1Dull);
        Sim twin = g_sim;
        int cond = c[1], cc = cond;
        if (sim_bits(&twin, 15) == 3 && cc) cc--;
        if (sim_bits(&twin, 63) == 34 && cc < (int)sim_bits(&twin, 127)) {
            if (cc >= 70) cc -= (int)sim_bits(&twin, 31);
            else if (cc >= 50) cc -= (int)sim_bits(&twin, 63);
            else cc = 0;
            if (cc < 0) cc = 0;
        }
        int sp = sim_race_speed(160, 100, cond);
        int d = sim_race_driver(st, seed % 12) + (int)sim_bits(&twin, 127);
        sp += (int)sim_bits(&twin, 127);
        long v = ((long)sp * d) >> 14;
        v += sim_bits(&twin, 127) + sim_bits(&twin, 127) + sim_bits(&twin, 63);
        long step = v / 2 + sim_race_grid(33, 0) - (long)sim_bits(&twin, 15);
        sim_race_round(s);
        CHECK(c[1] == cc);
        CHECK(u16(E(0) + 2) == 100 + step);
    }
    /* a step below 1 is kept (docs/ui-garage.md, round step 6, read in the code 2026-10-21): a car of condition 0 after
     * minute 40, a driver of nothing; a seed whose draws make the raw step below 1, and the distance 100 falls by it */
    int floored = 0;
    for (uint64_t seed = 1; seed < 200000 && floored < 3; seed++) {
        Sim twin = g_sim;
        sim_seed(&twin, seed * 0x9E3779B97F4A7C15ull);
        sim_bits(&twin, 15); /* the wear (condition 0 stays 0) */
        if (sim_bits(&twin, 63) == 34) sim_bits(&twin, 127); /* the breakdown: below 50 it becomes 0, no more draws */
        int d = 40 + (int)sim_bits(&twin, 127), sp = (int)sim_bits(&twin, 127);
        long v = ((long)sp * d) >> 14;
        v += sim_bits(&twin, 127) + sim_bits(&twin, 127) + sim_bits(&twin, 63);
        long raw = v / 2 - (long)sim_bits(&twin, 15);
        if (raw >= 1) continue;
        racers();
        race_on(55, 50);
        enter(0, 1, 0, 100);
        place(1, 0)[1] = 0;
        memset(staff(101), 0, 32);
        sim_seed(&g_sim, seed * 0x9E3779B97F4A7C15ull);
        sim_race_round(s);
        CHECK(u16(E(0) + 2) == 100 + raw);
        floored++;
    }
    CHECK(floored == 3);
    /* step 6's floor: only a distance negative as a signed word becomes 1 (the spec's values) */
    CHECK(sim_race_distance(0, 0, 0, 3) == 1);       /* 0xFFFD is negative */
    CHECK(sim_race_distance(10, 0, 0, 3) == 7);      /* a step of -3 is kept (the old floor gave 11) */
    CHECK(sim_race_distance(0, 0, 0, 0) == 0);       /* 0 stays 0 (the old floor gave 1) */
    CHECK(sim_race_distance(32760, 0, 10, 0) == 1);  /* 32,770 is negative as a word */
    CHECK(sim_race_distance(32760, 14, 0, 0) == 32767);
    CHECK(sim_race_distance(100, 0x10006, 0, 0) == 103); /* v is a 16-bit word: 6 / 2 */
    CHECK(sim_race_distance(100, 7, 3, 2) == 104);  /* v / 2 = 3, + b 3, - r15 2 */
    /* the breakdown: r15 not 3, r63 = 34 and the condition below the r127 draw; 70 or more loses r31, 50-69 r63,
     * below 50 everything */
    static const int CONDS[5] = {90, 60, 30, 70, 50};
    for (int ci = 0; ci < 5; ci++) {
        int found = 0;
        for (uint64_t seed = 1; seed < 400000 && !found; seed++) {
            Sim twin = g_sim;
            sim_seed(&twin, seed * 0x9E3779B97F4A7C15ull);
            if (sim_bits(&twin, 15) == 3 || sim_bits(&twin, 63) != 34) continue;
            int lim = (int)sim_bits(&twin, 127);
            if (CONDS[ci] >= lim) continue;
            int band = CONDS[ci] >= 70 ? 0 : CONDS[ci] >= 50 ? 1 : 2;
            int loss = band == 0 ? (int)sim_bits(&twin, 31) : band == 1 ? (int)sim_bits(&twin, 63) : CONDS[ci];
            if (!loss) continue;
            racers();
            race_on(55, 50);
            enter(0, 1, 0, 100);
            place(1, 0)[1] = (uint8_t)CONDS[ci];
            sim_seed(&g_sim, seed * 0x9E3779B97F4A7C15ull);
            sim_race_round(s);
            int want = CONDS[ci] - loss < 0 ? 0 : CONDS[ci] - loss;
            CHECK(place(1, 0)[1] == want);
            found = 1;
        }
        CHECK(found);
    }
    /* no breakdown when the condition equals the r127 draw (strictly below) */
    {
        int found = 0;
        for (uint64_t seed = 1; seed < 400000 && !found; seed++) {
            Sim twin = g_sim;
            sim_seed(&twin, seed * 0x9E3779B97F4A7C15ull);
            if (sim_bits(&twin, 15) == 3 || sim_bits(&twin, 63) != 34) continue;
            int lim = (int)sim_bits(&twin, 127);
            if (lim < 20 || lim > 100) continue;
            racers();
            race_on(55, 50);
            enter(0, 1, 0, 100);
            place(1, 0)[1] = (uint8_t)lim;
            sim_seed(&g_sim, seed * 0x9E3779B97F4A7C15ull);
            sim_race_round(s);
            CHECK(place(1, 0)[1] == lim);
            found = 1;
        }
        CHECK(found);
    }
    /* the wear: r15 = 3 takes one point, condition 0 stays 0 */
    for (int cond = 0; cond <= 1; cond++) {
        int found = 0;
        for (uint64_t seed = 1; seed < 100000 && !found; seed++) {
            Sim twin = g_sim;
            sim_seed(&twin, seed * 0x9E3779B97F4A7C15ull);
            if (sim_bits(&twin, 15) != 3 || sim_bits(&twin, 63) == 34) continue;
            racers();
            race_on(55, 50);
            enter(0, 1, 0, 100);
            place(1, 0)[1] = (uint8_t)cond;
            sim_seed(&g_sim, seed * 0x9E3779B97F4A7C15ull);
            sim_race_round(s);
            CHECK(place(1, 0)[1] == 0);
            found = 1;
        }
        CHECK(found);
    }
    /* the sort: by distance, descending, whole entries; the grid bonus by the entry's place */
    racers();
    race_on(55, 70);
    enter(0, 1, 0, 10), enter(1, 2, 0, 5000), enter(2, 3, 0, 300);
    sim_race_round(s);
    CHECK(E(0)[0] == 2 && E(1)[0] == 3 && E(2)[0] == 1);
    /* an entry whose driver has no slot is skipped (the original loops for ever) */
    racers();
    race_on(55, 70);
    enter(0, 1, 0, 10);
    put16(slot(1, 10), 0);
    sim_race_round(s);
    CHECK(u16(E(0) + 2) == 10);
    /* the race loop reads a 17th entry, the entry routines 16 */
    racers();
    race_on(55, 70);
    for (int k = 0; k < 16; k++) enter(k, 1, 0, 10000);
    enter(16, 2, 0, 0);
    sim_race_round(s);
    int seventeenth = -1;
    for (int k = 0; k < 17; k++)
        if (E(k)[0] == 2) seventeenth = u16(E(k) + 2);
    CHECK(seventeenth > 0);
}

/* ---- the finish ---------------------------------------------------------------------------------------- */

static void ward(int h, int i, int beds, int occupied) {
    site(h)[6] = (uint8_t)(i + 1 > site(h)[6] ? i + 1 : site(h)[6]);
    uint8_t *k = sim_room_record(s, h, 6, i);
    k[0x0A] = (uint8_t)beds, k[0x0C] = (uint8_t)occupied;
    for (int b = 0; b < occupied; b++) put16(k + 0x14 + 4 * b, 500 + b);
}

static void test_victim(void) {
    fresh();
    site(2)[7] = 1;
    ward(2, 0, 10, 4);
    uint8_t *q = person(300);
    q[3] = 16;
    CHECK(sim_race_victim(s, 300, 2) == 2);
    /* the winner first: with hospital 9 also fit, the winner always gets the victim */
    for (int t = 0; t < 12; t++) {
        fresh();
        site(2)[7] = site(9)[7] = 1;
        ward(2, 0, 10, 0), ward(9, 0, 10, 0);
        sim_seed(&g_sim, (uint64_t)(t + 3) * 0x9E3779B97F4A7C15ull);
        CHECK(sim_race_victim(s, 300, 2) == 2);
    }
    fresh();
    site(2)[7] = 1;
    ward(2, 0, 10, 4);
    q = person(300); /* fresh() made a new world */
    q[3] = 16;
    CHECK(sim_race_victim(s, 300, 2) == 2);
    uint8_t *k = sim_room_record(s, 2, 6, 0);
    CHECK(k[0x0C] == 5 && u16(k + 0x14 + 16) == 300 && u16(k + 0x16 + 16) == 2);
    CHECK(q[3] == 2 && q[66] == 0 && q[48] == 50 && q[53] == 50);
    /* the winner without an OP, or with a full ward: another hospital with both */
    fresh();
    ward(1, 0, 6, 6);
    site(1)[7] = 1;
    site(9)[7] = 1;
    ward(9, 0, 4, 1);
    ward(9, 1, 4, 4);
    CHECK(sim_race_victim(s, 300, 1) == 9);
    CHECK(person(300)[3] == 9 && person(300)[66] == 0 && sim_room_record(s, 9, 6, 0)[0x0C] == 2);
    fresh();
    ward(4, 0, 6, 1); /* a ward but no OP */
    CHECK(sim_race_victim(s, 300, 4) == -1 && person(300)[3] == 0xFF && person(300)[44] == 100); /* home healthy */
}

static void test_finish(void) {
    racers();
    garage_for(4, 104, 0);
    for (int k = 0; k < 8; k++) {
        int h = 1 + k % 4;
        enter(k, h, 0, 1000 - k);
    }
    /* entries 0-3 are hospitals 1-4 with drivers 101-104; 4-7 the same hospitals again */
    race_on(321, 79);
    RB()[6] = 1;
    put16(staff(101) + 16, 5);
    put16(drv(0), 102), put16(drv(0) + 2, 9), put16(drv(0) + 4, 7); /* 102 already in the table */
    for (int h = 1; h <= 4; h++) G(h)[GR_ENTERED] = 0xFF;
    G(0)[GR_ENTERED] = 0xFF;
    world_set_tail(&g_world, T_RACES, 4);
    sim_race_minute(s);
    CHECK(u16(RB()) == 0);
    for (int k = 0; k < 8; k++) CHECK(E(k)[0] == 0xFF);
    /* drivers: 10 + 6 (entries 0 and 4), team points 10 / 6 / 4 / 3 then 2 / 1 / 0 / 0 */
    CHECK(u16(staff(101) + 16) == 5 + 10 + 6 && u16(staff(102) + 16) == 9 + 6 && u16(staff(103) + 16) == 8 + 5 &&
          u16(staff(104) + 16) == 7 + 5); /* table 0xAF18: 10, 9, 8, 7, 6, 6, 5, 5 */
    CHECK(team(1) == 10 + 2 && team(2) == 6 + 1 && team(3) == 4 && team(4) == 3);
    CHECK(u16(drv(0)) == 102 && u16(drv(0) + 2) == 2 && u16(drv(0) + 4) == 7 + 6 + 1);
    /* [0x4528] (docs/rng-sites.md 2.4): the garage record of the last placed entry's hospital, 4 */
    CHECK(s->rng_scratch == 0x16A270u + 158u * 4);
    CHECK(u16(drv(1)) == 101 && u16(drv(1) + 2) == 1 && u16(drv(1) + 4) == 12);
    CHECK(u16(drv(2)) == 103 && u16(drv(3)) == 104 && u16(drv(3) + 4) == 3);
    for (int h = 0; h <= 4; h++) CHECK(G(h)[GR_ENTERED] == 0);
    CHECK(u16(H(1) + 0x160) == 1 && u16(H(2) + 0x160) == 0); /* races won */
    CHECK(person(321)[3] == 0xFF); /* no ward anywhere: home healthy */
    int item = -1;
    for (int i = 0; i < 100; i++)
        if (sim_news_item(s, i)[0] && sim_news_item(s, i)[20] == 0x23) item = i;
    CHECK(item >= 0);
    if (item >= 0) {
        const uint8_t *it = sim_news_item(s, item);
        CHECK(it[8] == 1 && it[9] == 2 && u16(it + 10) == 101 && u16(it + 12) == 102 && it[22] == 6 &&
              u16(it + 16) == 321 && it[4] == 14 && it[5] == 2 && it[6] == 4);
    }
    CHECK(u16(H(1) + H_TITLES) == 0); /* not the season's end */
    /* a full driver table without the driver: not written */
    racers();
    for (int i = 0; i < 100; i++) put16(drv(i), 500 + i), put16(drv(i) + 4, 1);
    enter(0, 1, 0, 50), enter(1, 2, 0, 40);
    race_on(321, 79);
    sim_race_finish(s);
    int found = 0;
    for (int i = 0; i < 100; i++) found += u16(drv(i)) == 101;
    CHECK(!found && team(1) == 10);
    /* the season's end: race 25 */
    racers();
    put16(g_world.block[34] + 2 * 3, 50);
    put16(drv(0), 103), put16(drv(0) + 2, 7), put16(drv(0) + 4, 40);
    put16(drv(1), 101), put16(drv(1) + 2, 1), put16(drv(1) + 4, 33);
    enter(0, 1, 0, 50), enter(1, 2, 0, 40);
    race_on(321, 79);
    world_set_tail(&g_world, T_RACES, 25);
    sim_race_finish(s);
    /* teams: 3 has 50, 1 now 10: hospital 3's title; drivers: 101 now 43 beats 103's 40, entry's hospital 1 */
    CHECK(u16(H(3) + H_TITLES) == 1 && u16(H(1) + H_TITLES) == 1 && u16(H(7) + H_TITLES) == 0);
    racers();
    put16(drv(0), 103), put16(drv(0) + 2, 7), put16(drv(0) + 4, 60);
    enter(0, 1, 0, 50), enter(1, 2, 0, 40);
    race_on(321, 79);
    world_set_tail(&g_world, T_RACES, 25);
    sim_race_finish(s);
    CHECK(u16(H(1) + H_TITLES) == 1 && u16(H(7) + H_TITLES) == 1); /* team 1 (10 points), driver 103's hospital 7 */
    /* the spec's case (docs/rng-sites.md 2.4): the final order 7, 12, 15 leaves garage 15's record in [0x4528] */
    racers();
    s->rng_scratch = 0x1F21A6;
    enter(0, 7, 0, 30), enter(1, 12, 0, 20), enter(2, 15, 0, 10);
    race_on(321, 79);
    sim_race_finish(s);
    CHECK(s->rng_scratch == 0x16ABB2u);
    /* the race item's gate refuses with 96 items in the table: no race won counted (finish step 2); 95: counted */
    for (int n = 96; n >= 95; n--) {
        racers();
        for (int i = 0; i < n; i++) sim_news_item(s, i)[0] = 1;
        enter(0, 1, 0, 50), enter(1, 2, 0, 40);
        race_on(321, 79);
        sim_race_finish(s);
        CHECK(u16(H(1) + 0x160) == (n == 96 ? 0 : 1));
    }
}

static void test_season(void) {
    fresh();
    world_set_tail(&g_world, T_RACES, 24);
    T()[T_FINAL_TEAM] = 5;
    sim_season(s);
    CHECK(T()[T_FINAL_TEAM] == 5); /* nothing below 25 */
    world_set_tail(&g_world, T_RACES, 25);
    T()[T_FINAL_TEAM] = 3, T()[T_FINAL_DRIVER] = 0;
    put16(g_world.block[34] + 2 * 13, 162), put16(H(13) + 0x128, 4);
    put16(drv(0), 9), put16(drv(0) + 4, 33);
    sim_season(s);
    sim_season(s);
    CHECK(T()[T_FINAL_TEAM] == 1 && T()[T_FINAL_DRIVER] == 0);
    sim_season(s);
    CHECK(T()[T_FINAL_TEAM] == 1 && world_tail(&g_world, T_RACES) == 25); /* waits for the driver news */
    T()[T_FINAL_DRIVER] = 2;
    sim_season(s); /* driver reaches 1, but must already be 1 on entry to reset */
    CHECK(world_tail(&g_world, T_RACES) == 25 && T()[T_FINAL_TEAM] == 1 && T()[T_FINAL_DRIVER] == 1);
    CHECK(u16(H(13) + 0x128) == 4 && team(13) == 162 && u16(drv(0)) == 9 && u16(drv(0) + 4) == 33);
    sim_season(s);
    CHECK(world_tail(&g_world, T_RACES) == 0 && T()[T_FINAL_TEAM] == 0 && T()[T_FINAL_DRIVER] == 0);
    CHECK(u16(H(13) + 0x128) == 166 && team(13) == 0 && u16(drv(0)) == 0 && u16(drv(0) + 4) == 0);
    /* the countdown is in the news ageing 0x474BC of the schedule */
    world_set_tail(&g_world, T_RACES, 26);
    T()[T_FINAL_TEAM] = 7, T()[T_FINAL_DRIVER] = 9;
    CHECK(sim_call(s, 0x474BC));
    CHECK(T()[T_FINAL_TEAM] == 6 && T()[T_FINAL_DRIVER] == 8);
}

/* Compare every byte of the four affected blocks, including hospital 16 and the unused tail bytes. */
static int season_case(const World *initial, World *expected, int races, int team_news, int driver_news) {
    static const int blocks[] = {BLK_HOSPITALS, 34, 35, WORLD_TAIL};
    for (unsigned i = 0; i < sizeof blocks / sizeof *blocks; i++) {
        int b = blocks[i];
        memcpy(g_world.block[b], initial->block[b], initial->size[b]);
        memcpy(expected->block[b], initial->block[b], initial->size[b]);
    }
    world_set_tail(&g_world, T_RACES, races);
    T()[T_FINAL_TEAM] = (uint8_t)team_news;
    T()[T_FINAL_DRIVER] = (uint8_t)driver_news;
    world_set_tail(expected, T_RACES, races);
    uint8_t *tail = expected->block[WORLD_TAIL];
    int eligible = races >= 25 && races <= 0x7FFF;
    int reset = eligible && driver_news == 1 && (team_news == 1 || team_news == 2);
    tail[T_FINAL_TEAM] = (uint8_t)(team_news - (eligible && team_news > 1));
    tail[T_FINAL_DRIVER] = (uint8_t)(driver_news - (eligible && driver_news > 1));
    if (reset) {
        world_set_tail(expected, T_RACES, 0);
        tail[T_FINAL_TEAM] = tail[T_FINAL_DRIVER] = 0;
        for (int h = 0; h < 16; h++) {
            uint8_t *points = world_hospital(expected, h) + H_SEASONS_POINTS;
            unsigned sum = (unsigned)u16(points) + (unsigned)u16(initial->block[34] + 2 * h);
            put16(points, (int)(sum & 0xFFFF));
        }
        memset(expected->block[34], 0, expected->size[34]);
        memset(expected->block[35], 0, expected->size[35]);
    }
    sim_season(s);
    for (unsigned i = 0; i < sizeof blocks / sizeof *blocks; i++) {
        int b = blocks[i];
        if (memcmp(g_world.block[b], expected->block[b], expected->size[b])) {
            fprintf(stderr, "[FAIL] season races=%d team=%d driver=%d block=%d\n", races, team_news, driver_news, b);
            failures++;
            return 0;
        }
    }
    return 1;
}

static void test_season_domain(void) {
    fresh();
    World initial = {0}, expected = {0};
    if (world_alloc(&initial) || world_alloc(&expected)) {
        CHECK(0); /* fixtures stay on the heap, including on Windows */
        world_free(&initial);
        world_free(&expected);
        return;
    }
    /* Distinct nonzero data catches partial clears and writes outside the season fields. */
    for (int b = 0; b <= WORLD_TAIL; b++) {
        for (size_t i = 0; i < g_world.size[b]; i++) g_world.block[b][i] = (uint8_t)(1 + (i + 19 * b) % 255);
    }
    for (int h = 0; h < 17; h++) put16(H(h) + H_SEASONS_POINTS, 65530 - 101 * h);
    for (int h = 0; h < 16; h++) put16(g_world.block[34] + 2 * h, 10 + 4111 * h);
    CHECK(g_world.size[34] == 32 && g_world.size[35] == 600);
    for (int b = 0; b <= WORLD_TAIL; b++) memcpy(initial.block[b], g_world.block[b], g_world.size[b]);
    int ok = 1;
    for (int t = 0; t < 256 && ok; t++)
        for (int d = 0; d < 256 && ok; d++) ok = season_case(&initial, &expected, 25, t, d);
    static const int races[] = {0, 1, 24, 25, 26, 0x7FFF, 0x8000, 0xFFFF};
    static const int pairs[][2] = {{0, 0}, {1, 1}, {1, 2}, {2, 1}, {2, 2}, {255, 255}};
    for (unsigned r = 0; r < sizeof races / sizeof *races && ok; r++)
        for (unsigned p = 0; p < sizeof pairs / sizeof *pairs && ok; p++)
            ok = season_case(&initial, &expected, races[r], pairs[p][0], pairs[p][1]);
    for (int b = 0; b <= WORLD_TAIL; b++)
        if (b != BLK_HOSPITALS && b != 34 && b != 35 && b != WORLD_TAIL)
            CHECK(!memcmp(g_world.block[b], initial.block[b], initial.size[b]));
    world_free(&initial);
    world_free(&expected);
}

static void test_season_schedule(void) {
    /* Run the actual tick-43 list: news must age on below-threshold, pending and reset paths alike. */
    static const int cases[][3] = {{24, 1, 1}, {0x8000, 1, 1}, {25, 2, 0}, {25, 1, 2}, {25, 2, 1}};
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        fresh();
        int races = cases[i][0], t = cases[i][1], d = cases[i][2];
        world_set_tail(&g_world, T_RACES, races);
        T()[T_FINAL_TEAM] = (uint8_t)t, T()[T_FINAL_DRIVER] = (uint8_t)d;
        put16(g_world.block[34], 10);
        put16(H(0) + H_SEASONS_POINTS, 65530);
        uint8_t *item = sim_news_item(s, 99);
        item[0] = 2, item[20] = 5; /* ordinary, unpublished item */
        put16(item + 2, 149);
        s->tick = 42;
        sim_tick(s);
        CHECK(s->tick == 43);
        CHECK(u16(item + 2) == 150 && item[0] == 0);
        int eligible = races == 25, reset = eligible && d == 1;
        CHECK(u16(T() + T_RACES) == (reset ? 0 : races));
        CHECK(T()[T_FINAL_TEAM] == (reset ? 0 : t - (eligible && t > 1)));
        CHECK(T()[T_FINAL_DRIVER] == (reset ? 0 : d - (eligible && d > 1)));
        CHECK(u16(H(0) + H_SEASONS_POINTS) == (reset ? 4 : 65530));
        CHECK(team(0) == (reset ? 0 : 10));
    }
}

/* ---- the AI garage ------------------------------------------------------------------------------------- */

static void ai(int h) {
    site(h)[10] = 1, site(h)[7] = 1;
    put32(H(h) + H_CASH, 3000);
    world_set_tail(&g_world, TAIL_HOUR, 21);
}
static void offer(int i, int seller, int model, int price, int cond) {
    uint8_t *o = g_world.block[BLK_CARS] + 20 * i;
    o[0] = (uint8_t)seller, o[1] = (uint8_t)model, o[4] = (uint8_t)cond, o[5] = 3;
    put16(o + 2, price);
}

static void test_ai_purchase(void) {
    fresh();
    ai(5);
    /* cash 3000, no daily costs: budget 2600 -> level min(20, 6) = 6 */
    offer(3, DEALER, 5, 100, 40), offer(7, 9, 6, 2900, 50), offer(11, DEALER, 6, 1500, 37);
    car(5, 0, 1, 50), G(5)[GR_CARS] = 1;
    sim_ai_garage(s, 5);
    CHECK(G(5)[GR_CARS] == 2 && place(5, 1)[0] == 7 && place(5, 1)[1] == 37);
    CHECK(get32(H(5) + H_CASH) == 1500 && get32(H(5) + 0x120) == 1500 && get32(H(5) + 0x13A) == 1500);
    CHECK(get32(H(DEALER) + H_CASH) == 2500 && get32(H(DEALER) + 0x124) == 1500 && get32(H(DEALER) + 0x12E) == 1500);
    CHECK(g_world.block[BLK_CARS][20 * 11] == 0xFF && g_world.block[BLK_CARS][20 * 7] == 9);
    /* exactly the level: a level-6 budget does not buy model 5; L = cash - K - 200 bounds the price */
    fresh();
    ai(5);
    offer(3, DEALER, 5, 100, 40);
    sim_ai_garage(s, 5);
    CHECK(G(5)[GR_CARS] == 0);
    offer(4, DEALER, 6, 2800, 40); /* not below L = 2800 */
    sim_ai_garage(s, 5);
    CHECK(G(5)[GR_CARS] == 0);
    put16(g_world.block[BLK_CARS] + 20 * 4 + 2, 2799);
    sim_ai_garage(s, 5);
    CHECK(G(5)[GR_CARS] == 1 && get32(H(5) + H_CASH) == 201);
    /* budget 128 * 2 = 256 -> level 2; below 600 cash or before 20:00 nothing; the vehicle market ends it */
    fresh();
    ai(5);
    put32(H(5) + H_CASH, 656); /* budget 256: level 2 */
    offer(1, DEALER, 2, 300, 40);
    world_set_tail(&g_world, TAIL_HOUR, 19);
    sim_ai_garage(s, 5);
    CHECK(G(5)[GR_CARS] == 0);
    world_set_tail(&g_world, TAIL_HOUR, 20);
    s->screen = 130;
    sim_ai_garage(s, 5);
    CHECK(G(5)[GR_CARS] == 0);
    s->screen = 1;
    sim_ai_garage(s, 5);
    CHECK(G(5)[GR_CARS] == 1 && place(5, 0)[0] == 3);
    put32(H(5) + H_CASH, 599 + 300);
    G(5)[GR_CARS] = 0, memset(place(5, 0), 0, 4);
    put32(H(5) + H_CASH, 599);
    offer(2, DEALER, 0, 10, 40);
    sim_ai_garage(s, 5);
    CHECK(G(5)[GR_CARS] == 0);
    /* the gates: no OP, negative cash, hospital 0 */
    fresh();
    ai(5);
    site(5)[7] = 0;
    offer(1, DEALER, 6, 100, 40);
    sim_ai_garage(s, 5);
    CHECK(G(5)[GR_CARS] == 0);
    site(5)[7] = 1;
    put32(H(5) + H_CASH, -1);
    sim_ai_garage(s, 5);
    CHECK(G(5)[GR_CARS] == 0);
    ai(0);
    sim_ai_garage(s, 0);
    CHECK(G(0)[GR_CARS] == 0);
    /* daily costs count: one employee's wage 2,500 leaves no budget */
    fresh();
    ai(5);
    put16(slot(5, 50), 999), slot(5, 50)[6] = 250;
    put32(H(5) + H_CASH, 649);
    offer(1, DEALER, 0, 10, 40);
    sim_ai_garage(s, 5);
    CHECK(G(5)[GR_CARS] == 0); /* 649 - 250 - 400 < 0 */
    put32(H(5) + H_CASH, 650);
    sim_ai_garage(s, 5);
    CHECK(G(5)[GR_CARS] == 1 && place(5, 0)[0] == 1); /* budget 0: level 0 */
    /* exactly 600 cash buys (budget 200: level 1); a budget of 254 is level 1 (254 / 128), not 2 */
    fresh();
    ai(5);
    put32(H(5) + H_CASH, 600);
    offer(1, DEALER, 1, 100, 40);
    sim_ai_garage(s, 5);
    CHECK(G(5)[GR_CARS] == 1 && place(5, 0)[0] == 2);
    fresh();
    ai(5);
    put32(H(5) + H_CASH, 654);
    offer(1, DEALER, 2, 100, 40);
    sim_ai_garage(s, 5);
    CHECK(G(5)[GR_CARS] == 0);
    offer(2, DEALER, 1, 100, 40);
    sim_ai_garage(s, 5);
    CHECK(G(5)[GR_CARS] == 1 && place(5, 0)[0] == 2);
}

static void test_ai_staff(void) {
    fresh();
    ai(5);
    world_set_tail(&g_world, TAIL_HOUR, 10);
    G(5)[GR_CARS] = 2;
    /* three drivers and one off duty; scores by d = 24 - hours on duty */
    employ(5, 1, 31, 6), employ(5, 2, 32, 6), employ(5, 3, 33, 6), employ(5, 4, 34, 6), employ(5, 5, 35, 3);
    staff(31)[2] = 10, staff(32)[2] = 30, staff(33)[2] = 20;
    slot(5, 4)[3] = 0x13, staff(34)[4] = 0x13, staff(34)[2] = 90;
    slot(5, 2)[0x0A] = 12; /* 32: d = 12, halved */
    CHECK(sim_driver_score(s, slot(5, 1)) == 4 * 24 + 40);
    CHECK(sim_driver_score(s, slot(5, 2)) == (4 * 12 + 120) / 2);
    slot(5, 3)[0x0A] = 30; /* d at least 1 */
    CHECK(sim_driver_score(s, slot(5, 3)) == (4 + 80) / 2);
    slot(5, 3)[0x0A] = 10; /* 10 hours: halved */
    CHECK(sim_driver_score(s, slot(5, 3)) == (4 * 14 + 80) / 2);
    slot(5, 3)[0x0A] = 9;
    staff(33)[5] = 3, staff(33)[15] = 5, staff(33)[12] = 1, staff(33)[13] = 2;
    put16(staff(33) + 16, 9);
    CHECK(sim_driver_score(s, slot(5, 3)) == 4 * 15 + 80 + 12 + 5 + 2 + 16 + 16);
    staff(33)[5] = staff(33)[15] = staff(33)[12] = staff(33)[13] = 0;
    put16(staff(33) + 16, 0);
    slot(5, 3)[0x0A] = 0;
    put32(G(5) + GR_MECHANIC, 0x00200021);
    sim_ai_garage(s, 5);
    /* B: 33 scores 176, best; 31 (136) was best first and keeps state 10; C: the mechanic is the last idle one,
     * none is (31 and 33 hold state 10, 32 is idle: 32) */
    CHECK(u16(G(5) + GR_DRIVER) == 33 && slot(5, 3)[3] == 10 && staff(33)[4] == 10);
    CHECK(u16(G(5) + GR_MECHANIC) == 32 && slot(5, 2)[3] == 10);
    CHECK(slot(5, 4)[3] == 0x13); /* off duty stays */
    CHECK(slot(5, 5)[3] == 0); /* another group untouched */
    /* C's sending home: before 14:00 the others go off duty; 31 kept state 10 */
    CHECK(slot(5, 1)[3] == 0x13 && staff(31)[4] == 0x13);
    /* no driver at all: hours 5-18 the lookup; none ends the routine */
    fresh();
    ai(5);
    world_set_tail(&g_world, TAIL_HOUR, 15);
    race_on(9, 3);
    employ(5, 1, 31, 6);
    G(5)[GR_CARS] = 2; /* no purchase step (it ends the routine from 20:00) */
    staff(31)[21] = 9; /* mood below 10: not a candidate */
    sim_ai_garage(s, 5);
    CHECK(u16(G(5) + GR_DRIVER) == 0 && u16(G(5) + GR_MECHANIC) == 0);
    staff(31)[21] = 10;
    sim_ai_garage(s, 5);
    CHECK(u16(G(5) + GR_DRIVER) == 31 && slot(5, 1)[3] == 10);
    /* the mechanic hours 2-21 even outside the driver's hours (race running, so B does not run) */
    employ(5, 2, 32, 6);
    world_set_tail(&g_world, TAIL_HOUR, 21);
    sim_ai_garage(s, 5);
    CHECK(u16(G(5) + GR_MECHANIC) == 32);
    /* the evening: both released (race word 0), state 0x13 */
    fresh();
    ai(5);
    world_set_tail(&g_world, TAIL_HOUR, 20);
    employ(5, 1, 31, 6), employ(5, 2, 32, 6);
    slot(5, 1)[3] = slot(5, 2)[3] = 10;
    put16(G(5) + GR_DRIVER, 31), put16(G(5) + GR_MECHANIC, 32);
    G(5)[GR_CARS] = 2;
    sim_ai_garage(s, 5);
    CHECK(u16(G(5) + GR_DRIVER) == 0 && u16(G(5) + GR_MECHANIC) == 0 && slot(5, 1)[3] == 0x13 && slot(5, 2)[3] == 0x13);
    /* not while a race is announced */
    fresh();
    ai(5);
    world_set_tail(&g_world, TAIL_HOUR, 22);
    race_on(9, 40);
    employ(5, 1, 31, 6), slot(5, 1)[3] = 10, put16(G(5) + GR_DRIVER, 31);
    G(5)[GR_CARS] = 2;
    sim_ai_garage(s, 5);
    CHECK(u16(G(5) + GR_DRIVER) == 31);
}

static void test_ai_tie(void) { /* B: only a score strictly above the best replaces the driver */
    fresh();
    ai(5);
    world_set_tail(&g_world, TAIL_HOUR, 15);
    G(5)[GR_CARS] = 2;
    employ(5, 1, 31, 6), employ(5, 2, 32, 6);
    staff(31)[2] = staff(32)[2] = 20;
    sim_ai_garage(s, 5);
    CHECK(u16(G(5) + GR_DRIVER) == 31 && u16(G(5) + GR_MECHANIC) == 32);
}

/* the garage's crew in state 10: driver 31 in slot 1, with n = 2 also mechanic 32 in slot 2 */
static void crew(int h, int n) {
    employ(h, 1, 31, 6), slot(h, 1)[3] = 10, staff(31)[4] = 10, put16(G(h) + GR_DRIVER, 31);
    if (n == 2) employ(h, 2, 32, 6), slot(h, 2)[3] = 10, staff(32)[4] = 10, put16(G(h) + GR_MECHANIC, 32);
}

static void test_ai_entry_sale(void) {
    fresh();
    ai(5);
    world_set_tail(&g_world, TAIL_HOUR, 21);
    employ(5, 1, 31, 6), slot(5, 1)[3] = 10, put16(G(5) + GR_DRIVER, 31);
    G(5)[GR_CARS] = 2;
    car(5, 0, 2, 69), car(5, 4, 3, 70);
    race_on(9, 12);
    enter(0, 7, 1, 0);
    sim_ai_garage(s, 5);
    CHECK(E(1)[0] == 5 && E(1)[1] == 4 && u16(E(1) + 2) == 0);
    sim_ai_garage(s, 5); /* once per hospital */
    CHECK(E(2)[0] == 0xFF);
    /* not from minute 30 on; not with every car below 70 */
    E(1)[0] = 0xFF;
    put16(RB() + 2, 30);
    sim_ai_garage(s, 5);
    CHECK(E(1)[0] == 0xFF);
    put16(RB() + 2, 29);
    place(5, 4)[1] = 69;
    sim_ai_garage(s, 5);
    CHECK(E(1)[0] == 0xFF);
    /* no driver: no entry */
    place(5, 4)[1] = 90;
    put16(G(5) + GR_DRIVER, 0);
    world_set_tail(&g_world, TAIL_HOUR, 22);
    sim_ai_garage(s, 5);
    CHECK(E(1)[0] == 0xFF);
    /* the sale: hour 20, race word 0, two cars; a model below what X affords */
    fresh();
    ai(5);
    world_set_tail(&g_world, TAIL_HOUR, 20);
    G(5)[GR_CARS] = 2;
    car(5, 0, 4, 80), car(5, 1, 2, 60);
    put32(H(5) + H_CASH, 450 + 128 * 2); /* X = 256: L2 = 3, so models 0-1 (bytes 1-2) only */
    offer(0, DEALER, 0, 5, 5);
    crew(5, 1); /* a driver: without one the routine ends at C from 19:00 (SIM-26) */
    sim_ai_garage(s, 5);
    CHECK(G(5)[GR_CARS] == 2);
    put32(H(5) + H_CASH, 450 + 128 * 3 - 1); /* X = 383: L2 = 3 still */
    crew(5, 1);
    sim_ai_garage(s, 5);
    CHECK(G(5)[GR_CARS] == 2);
    put32(H(5) + H_CASH, 450 + 128 * 3); /* L2 = 4: model 2 (byte 3) sells */
    crew(5, 1);
    sim_ai_garage(s, 5);
    const uint8_t *o = g_world.block[BLK_CARS] + 20;
    CHECK(G(5)[GR_CARS] == 1 && place(5, 1)[0] == 0 && o[0] == 5 && o[1] == 2 && o[4] == 60 && o[5] == 0);
    CHECK(u16(o + 2) == 300 - 3 * 40 / 2 && get32(H(5) + H_CASH) == 450 + 128 * 3); /* no money moves */
    /* a full offer table: nothing */
    fresh();
    ai(5);
    world_set_tail(&g_world, TAIL_HOUR, 23);
    G(5)[GR_CARS] = 2, car(5, 0, 0, 80), car(5, 1, 0, 60);
    for (int i = 0; i < 20; i++) offer(i, DEALER, 3, 999, 50);
    crew(5, 2); /* driver and mechanic: from 22:00 a missing mechanic ends the routine at C */
    sim_ai_garage(s, 5);
    CHECK(G(5)[GR_CARS] == 2 && u16(G(5) + GR_DRIVER) == 0); /* E ran */
}

/* SIM-26 (docs/sim-drift9.md section 5, "What the engine must change" 3 (a)-(f)): step C ends the routine when the
 * driver field is 0 outside hours 5-18, or the mechanic field is 0 outside hours 2-21; no sweep, D or E then. */
static void test_ai_garage_early_ends(void) {
    /* (a) 01:00, no driver or mechanic, an idle group-6 employee: stays idle (the sweep does not run) */
    fresh();
    ai(5);
    G(5)[GR_CARS] = 2;
    world_set_tail(&g_world, TAIL_HOUR, 1);
    employ(5, 1, 31, 6);
    sim_ai_garage(s, 5);
    CHECK(slot(5, 1)[3] == 0 && staff(31)[4] == 0 && u16(G(5) + GR_DRIVER) == 0);
    /* (b) the same with a group-6 employee in state 10 that the garage does not name: stays in state 10 */
    slot(5, 1)[3] = 10, staff(31)[4] = 10;
    sim_ai_garage(s, 5);
    CHECK(slot(5, 1)[3] == 10 && staff(31)[4] == 10);
    /* (c) 23:00, driver set, mechanic 0: nothing changes (no release, no sale) */
    fresh();
    ai(5);
    world_set_tail(&g_world, TAIL_HOUR, 23);
    G(5)[GR_CARS] = 2, car(5, 0, 4, 80), car(5, 1, 2, 60);
    put32(H(5) + H_CASH, 3000);
    crew(5, 1);
    sim_ai_garage(s, 5);
    CHECK(u16(G(5) + GR_DRIVER) == 31 && slot(5, 1)[3] == 10 && G(5)[GR_CARS] == 2);
    /* (d) 21:00, driver set, mechanic 0, no idle group 6: the evening step releases the driver */
    world_set_tail(&g_world, TAIL_HOUR, 21);
    sim_ai_garage(s, 5);
    CHECK(u16(G(5) + GR_DRIVER) == 0 && slot(5, 1)[3] == 0x13);
    /* (f) 20:30 after the release, driver and mechanic 0, two cars and cash for a sale: no sale */
    G(5)[GR_CARS] = 2, car(5, 0, 4, 80), car(5, 1, 2, 60);
    world_set_tail(&g_world, TAIL_HOUR, 20);
    sim_ai_garage(s, 5);
    CHECK(G(5)[GR_CARS] == 2);
    crew(5, 1); /* control: with a driver the same call sells */
    sim_ai_garage(s, 5);
    CHECK(G(5)[GR_CARS] == 1);
    /* (e) 06:00, driver 0, an idle group-6 employee: hired as driver */
    fresh();
    ai(5);
    G(5)[GR_CARS] = 2;
    race_on(9, 40); /* B does not run, so C hires */
    world_set_tail(&g_world, TAIL_HOUR, 6);
    employ(5, 1, 31, 6);
    sim_ai_garage(s, 5);
    CHECK(u16(G(5) + GR_DRIVER) == 31 && slot(5, 1)[3] == 10);
    /* the hour edges: driver 0 at 04:00 and 19:00 ends; mechanic 0 at 01:00 and 22:00 ends */
    static const int H4[4] = {4, 19, 1, 22};
    for (int k = 0; k < 4; k++) {
        fresh();
        ai(5);
        G(5)[GR_CARS] = 2;
        world_set_tail(&g_world, TAIL_HOUR, H4[k]);
        if (k >= 2) crew(5, 1);
        employ(5, 3, 33, 6);
        sim_ai_garage(s, 5);
        CHECK(slot(5, 3)[3] == 0 && u16(G(5) + GR_MECHANIC) == 0); /* not hired, not swept */
        if (k >= 2) CHECK(u16(G(5) + GR_DRIVER) == 31); /* E did not release the driver */
    }
    /* inside: mechanic 0 at 02:00 hires (driver set) */
    fresh();
    ai(5);
    G(5)[GR_CARS] = 2;
    world_set_tail(&g_world, TAIL_HOUR, 2);
    crew(5, 1);
    employ(5, 3, 33, 6);
    sim_ai_garage(s, 5);
    CHECK(u16(G(5) + GR_MECHANIC) == 33);
}

/* ---- the garage minute and the fittings ------------------------------------------------------------------ */

static void test_garage_minute(void) {
    fresh();
    site(3)[10] = 1;
    car(3, 0, 0, 50), car(3, 2, 4, 60), car(3, 3, 6, 70);
    s->screen = 1;
    sim_garage_minute(s); /* one bubble pass: better models forward, empty places back, one step a minute */
    CHECK(place(3, 0)[0] == 1 && place(3, 1)[0] == 5 && place(3, 2)[0] == 7 && place(3, 3)[0] == 0);
    sim_garage_minute(s);
    CHECK(place(3, 0)[0] == 5 && place(3, 1)[0] == 7 && place(3, 2)[0] == 1 && place(3, 2)[1] == 50);
    s->screen = 5;
    sim_garage_minute(s);
    CHECK(place(3, 0)[0] == 5 && place(3, 1)[0] == 7);
    s->screen = 1;
    race_on(9, 3);
    sim_garage_minute(s);
    CHECK(place(3, 0)[0] == 5 && place(3, 1)[0] == 7);
    /* the repair: skill (8 b2 + b5 + 16 b12 + 16 b13 + 4 b14 + b15 + u16 16 + b21) / 128 + 5 */
    fresh();
    site(3)[10] = 1;
    car(3, 0, 1, 100), car(3, 1, 2, 40), car(3, 2, 3, 30);
    garage_for(3, 0, 60);
    uint8_t *m = staff(60);
    m[2] = 16, m[21] = 0;
    CHECK(sim_mechanic_skill(s, 60) == 6);
    m[21] = 100, m[5] = 28;
    put16(m + 16, 300);
    CHECK(sim_mechanic_skill(s, 60) == (128 + 28 + 300 + 100) / 128 + 5);
    int repaired = 0;
    for (int t = 0; t < 200; t++) {
        int v = next_bits(127);
        int before = place(3, 1)[1];
        sim_garage_minute(s);
        int ok = sim_mechanic_skill(s, 60) >= v;
        CHECK(place(3, 1)[1] == before + (ok && before < 100) || before == 100);
        repaired += ok;
    }
    CHECK(repaired > 0 && repaired < 200);
    CHECK(place(3, 0)[1] == 100 && place(3, 1)[1] >= 40);
    /* the experience: + 1 per repair */
    fresh();
    site(3)[10] = 1;
    car(3, 0, 1, 10);
    garage_for(3, 0, 60);
    put16(staff(60) + 16, 20000); /* skill (20,000 + 100) / 128 + 5 = 162, above every r127 */
    for (int t = 0; t < 5; t++) sim_garage_minute(s);
    CHECK(place(3, 0)[1] == 15 && u16(staff(60) + 16) == 20005);
    put16(staff(60) + 16, 30000), staff(60)[2] = 255, staff(60)[12] = staff(60)[13] = 255;
    CHECK(sim_mechanic_skill(s, 60) == (30000 + 2040 + 8160 + 100) / 128 + 5); /* above 32,767: unsigned */
    put16(staff(60) + 16, 20005), staff(60)[2] = staff(60)[12] = staff(60)[13] = 0;
    /* during a race: only the entered hospitals skip */
    race_on(9, 50);
    enter(0, 3, 0, 0);
    sim_garage_minute(s);
    CHECK(place(3, 0)[1] == 15);
    E(0)[0] = 4;
    sim_garage_minute(s);
    CHECK(place(3, 0)[1] == 16);
}

static void test_fittings(void) {
    fresh();
    site(3)[10] = 1;
    garage_for(3, 61, 60);
    staff(60)[21] = 0, staff(61)[21] = 100;
    race_on(9, 1); /* no leaving while the race word is set */
    int ups = 0;
    for (int t = 0; t < 400; t++) {
        int before = staff(60)[21];
        sim_garage_fittings(s);
        ups += staff(60)[21] - before;
    }
    CHECK(staff(61)[21] == 100 && ups > 28 && ups < 75); /* 1 in 8: 50 expected */
    put16(RB(), 0);
    /* below 4: leaves the garage (not while a race is announced) */
    staff(60)[21] = 2, staff(61)[21] = 3;
    race_on(9, 1);
    staff(60)[4] = staff(61)[4] = 10;
    seed_for(7, 0);
    sim_garage_fittings(s);
    CHECK(u16(G(3) + GR_MECHANIC) == 60 && u16(G(3) + GR_DRIVER) == 61);
    put16(RB(), 0);
    seed_for(7, 0);
    sim_garage_fittings(s);
    CHECK(u16(G(3) + GR_MECHANIC) == 0 && u16(G(3) + GR_DRIVER) == 0);
    CHECK(staff(60)[4] == 0 && staff(61)[4] == 0); /* 0x2301C: idle, whatever the group */
    /* the equipment: a tool box (capability 0x40) from the store */
    fresh();
    site(3)[10] = 1, site(3)[4] = 1;
    uint8_t *st = sim_room_record(s, 3, 4, 0);
    st[STORE_CAPACITY] = 50, st[STORE_EQUIPMENT_HELD] = 1;
    put16(st + STORE_EQUIPMENT, 129);
    st[STORE_EQUIPMENT_COND] = 77;
    g_image[0x144A + 129 * 20 + 10] = 0x40;
    sim_garage_fittings(s);
    CHECK(u16(G(3) + 0x30) == 129 && G(3)[0x8A] == 77 && u16(st + STORE_EQUIPMENT) == 0);
    put16(st + STORE_EQUIPMENT, 130), st[STORE_EQUIPMENT_HELD] = 1;
    g_image[0x144A + 130 * 20 + 11] = 0x40; /* a second tool box in the store */
    sim_garage_fittings(s);
    CHECK(u16(st + STORE_EQUIPMENT) == 130 && u16(G(3) + 0x32) == 0); /* it has one: nothing taken */
    /* a player hospital needs a usable store (a clerk who works independently) */
    fresh();
    site(0)[10] = 1, site(0)[4] = 1;
    H(0)[0x14] = 1;
    st = sim_room_record(s, 0, 4, 0);
    st[STORE_CAPACITY] = 50, st[STORE_EQUIPMENT_HELD] = 1;
    put16(st + STORE_EQUIPMENT, 129);
    g_image[0x144A + 129 * 20 + 10] = 0x40;
    sim_garage_fittings(s);
    CHECK(u16(G(0) + 0x30) == 0);
    put16(st + STORE_CLERK, 70), staff(70)[11] = 1;
    sim_garage_fittings(s);
    CHECK(u16(G(0) + 0x30) == 129);
}

/* ---- the schedule ------------------------------------------------------------------------------------- */

static void test_schedule(void) {
    /* the car offers do not expire while the player is on the vehicle market (130) */
    fresh();
    offer(4, DEALER, 3, 500, 50);
    g_world.block[BLK_CARS][20 * 4 + 5] = 7;
    s->screen = 130;
    CHECK(sim_call(s, 0x1AB70) && g_world.block[BLK_CARS][20 * 4] == DEALER && g_world.block[BLK_CARS][20 * 4 + 5] == 8);
    s->screen = 1;
    sim_call(s, 0x1AB70);
    CHECK(g_world.block[BLK_CARS][20 * 4] == 0xFF);
    fresh();
    CHECK(sim_call(s, 0x67BFB) && sim_call(s, 0x6D515) && sim_call(s, 0x6DA45) && sim_call(s, 0x57F15) &&
          sim_call(s, 0x542F8));
    /* the championship news only while no race is announced */
    fresh();
    put16(g_world.block[34] + 2 * 4, 30);
    race_on(9, 3);
    for (int t = 0; t < 50; t++) sim_call(s, 0x515CB);
    int n = 0;
    for (int i = 0; i < 100; i++) n += sim_news_item(s, i)[0] != 0;
    CHECK(n == 0);
    put16(RB(), 0);
    for (int t = 0; t < 50; t++) sim_call(s, 0x515CB);
    for (int i = 0; i < 100; i++) n += sim_news_item(s, i)[0] != 0;
    CHECK(n > 0);
}

/* ---- the race word elsewhere: the staff stub, the release from the roster, the estate agent ------------------ */

static void test_race_word_elsewhere(void) {
    fresh();
    garage_for(0, 0, 0);
    employ(0, 3, 44, 6);
    slot(0, 3)[3] = 10, staff(44)[4] = 10;
    put16(G(0) + GR_DRIVER, 44);
    int occupant;
    const RosterStub *driver = personnel_roster_stub(0x1CE07), *mechanic = personnel_roster_stub(0x1CE5B);
    race_on(9, 3);
    CHECK(personnel_roster_enter(s, driver, &occupant) == -2); /* 515/1669 at once */
    CHECK(personnel_roster_enter(s, mechanic, &occupant) != -2);
    CHECK(personnel_room_release(s, 3) == 1 && u16(G(0) + GR_DRIVER) == 44); /* refused during a race */
    put16(RB(), 0);
    CHECK(personnel_roster_enter(s, driver, &occupant) == 0);
    G(0)[GR_ENTERED] = 0xFF;
    CHECK(personnel_room_release(s, 3) == 1);
    G(0)[GR_ENTERED] = 0;
    CHECK(personnel_room_release(s, 3) == 0 && u16(G(0) + GR_DRIVER) == 0);
    /* the estate agent gives the garage back only while no race runs */
    fresh();
    site(0)[10] = 1;
    G(0)[0x1B] = 27;
    g_image[CATALOG + 27 * CATALOG_SIZE + CATALOG_CATEGORY] = 10;
    g_world.block[BLK_ROOM_SLOTS][0] = 27, g_world.block[BLK_ROOM_SLOTS][1] = 1;
    int type;
    CHECK(estate_removal_slot(s, 10, 1, &type) == 0 && type == 27);
    race_on(9, 3);
    CHECK(estate_removal_slot(s, 10, 1, &type) == -1);
}

/* ---- the desktop ---------------------------------------------------------------------------------------- */

static void test_patient_counts(void) {
    fresh();
    g_image[0x954 + 3 * 20 + 8] = 1; /* illness 3: teeth */
    g_image[0x954 + 4 * 20 + 8] = 2; /* illness 4: mind */
    g_image[0x954 + 5 * 20 + 8] = 7;
    person(9)[56] = 3, person(9)[79] = 1; /* a second toothache at home */
    person(1)[56] = 3;                 /* teeth, at home in bed */
    person(1)[79] = 1;
    person(2)[56] = 4, person(2)[3] = 2, person(2)[4] = 6; /* mind, an inpatient */
    person(3)[56] = 5;                 /* mind (byte 8 = 7), looking */
    person(4)[56] = 3, person(4)[57] = 2; /* a wish: other */
    person(5)[57] = 1;                 /* only a cosmetic wish: ill, other */
    person(6)[3] = 4, person(6)[4] = 3; /* in hospital, healthy, not an inpatient */
    person(7)[104] = 1;                /* dead */
    person(1008)[56] = 9;              /* other (byte 8 = 0), the last record counts */
    for (int i = 1; i < 1009; i++) person(i)[105] = 0;
    PatientCounts c;
    desktop_patient_counts(s, &c);
    CHECK(c.ill == 7 && c.teeth == 2 && c.mind == 2 && c.other == 3);
    CHECK(c.in_hospital == 2 && c.inpatients == 1 && c.at_home == 2 && c.looking == 4 && c.dead == 1);
    static const int STATES[] = {6, 7, 8, 9, 16, 17};
    for (int k = 0; k < 6; k++) {
        person(6)[4] = (uint8_t)STATES[k];
        desktop_patient_counts(s, &c);
        CHECK(c.inpatients == 2);
    }
    person(6)[4] = 10;
    desktop_patient_counts(s, &c);
    CHECK(c.inpatients == 1);
}

static void test_holdings(void) {
    fresh();
    uint8_t *slots = g_world.block[BLK_ROOM_SLOTS] + 4 * 400;
    g_image[CATALOG + 7 * CATALOG_SIZE + CATALOG_CATEGORY] = 5;
    g_image[CATALOG + 9 * CATALOG_SIZE + CATALOG_CATEGORY] = 18;
    slots[0] = 7, slots[1] = 1;
    slots[2] = 7, slots[3] = 0;
    slots[4] = 9, slots[5] = 2;
    slots[6] = 0x87, slots[7] = 1; /* type byte 0x80 or more: not counted */
    g_image[CATALOG + 0x87 * CATALOG_SIZE + CATALOG_CATEGORY] = 5;
    int total[19], rented[19], owned[19];
    desktop_holdings(s, 4, total, rented, owned);
    CHECK(total[5] == 2 && rented[5] == 1 && total[18] == 1 && rented[18] == 1 && total[2] == 0);
    CHECK(owned[5] == 1 && owned[18] == 0 && owned[2] == 0); /* the third column: flag 0 */
    put16(pb() + 0x0E, 15);
    desktop_holdings_step(s, 1);
    CHECK(u16(pb() + 0x0E) == 0);
    desktop_holdings_step(s, -1);
    CHECK(u16(pb() + 0x0E) == 15);
    desktop_holdings_step(s, -1);
    CHECK(u16(pb() + 0x0E) == 14);
}

/* two ranking cells measured against the original's render (docs/engine.md, the comparison of 2026-10-19):
 * Normale Behandlungen (0x507CC) column 2 = +0x28 - +0x52 - +0x54 - +0x12A - +0x12C - +0x4C + +0x50, below 0 as 0
 * (docs/desktop.md, row 13); the Patientenindex average (0x25697) is the template "00,00" with three trailing
 * spaces, which the centring counts (0x467E). */
static void test_ranking_cells(void) {
    fresh();
    static Biing b;
    memset(&b, 0, sizeof b);
    b.prog = g_prog;
    b.world = g_world; /* the same blocks */
    sim_init(&b.sim, &b.world, &b.prog, 3);
    char c[32];
    uint8_t *r = H(3);
    put16(r + 0x28, 100), put16(r + 0x52, 10), put16(r + 0x54, 5), put16(r + 0x12A, 1), put16(r + 0x12C, 2);
    put16(r + 0x4C, 3), put16(r + 0x50, 20);
    CHECK(ranking_cell(&b, 0x507CC, 3, 1, c, sizeof c) == 370 && strcmp(c, "99 ") == 0);
    put16(r + 0x50, 0), put16(r + 0x52, 200);
    CHECK(ranking_cell(&b, 0x507CC, 3, 1, c, sizeof c) == 370 && strcmp(c, "0 ") == 0); /* below 0: 0 */
    put32(r + 0x44, 50400); /* x 100 / 1,008 = 5,000 */
    CHECK(ranking_cell(&b, 0x25697, 3, 1, c, sizeof c) == 366 && strcmp(c, "50,00   ") == 0);
    put32(r + 0x44, 1008 * 7 + 509); /* 750.49 -> 750 */
    CHECK(ranking_cell(&b, 0x25697, 3, 1, c, sizeof c) == 366 && strcmp(c, "07,50   ") == 0);
    CHECK(ranking_cell(&b, 0x25697, 3, 3, c, sizeof c) == 468 && ranking_cell(&b, 0x507CC, 3, 3, c, sizeof c) == -1);
}

/* German CD (OTH-DE-8/9): ranking 19 shows the nurses' average experience +0x194 (the integer mean of staff u16 16
 * over the slots with an index and group 1; the count to +0x16A only when above 0), sorted descending; the disk and
 * English builds keep the bust average +0x4A */
static void test_nurse_experience_ranking(void) {
    fresh();
    static Biing b;
    memset(&b, 0, sizeof b);
    b.prog = g_prog;
    b.world = g_world;
    sim_init(&b.sim, &b.world, &b.prog, 3);
    for (int h = 0; h < 16; h++) memset(staff_slots(&b.sim, h), 0, (size_t)STAFF_SLOTS * STAFF_SLOT_SIZE);
    /* hospital 2: nurses 40 (16 points) and 41 (19 points), a doctor 42 (900 points), an empty nurse slot */
    uint8_t *sl = staff_slots(&b.sim, 2);
    put16(sl, 40), sl[2] = 1;
    put16(sl + STAFF_SLOT_SIZE, 41), sl[STAFF_SLOT_SIZE + 2] = 1;
    put16(sl + 2 * STAFF_SLOT_SIZE, 42), sl[2 * STAFF_SLOT_SIZE + 2] = 2;
    sl[3 * STAFF_SLOT_SIZE + 2] = 1;
    put16(staff(40) + 16, 16), put16(staff(41) + 16, 19), put16(staff(42) + 16, 900);
    staff(40)[5] = 90, staff(41)[5] = 100; /* the busts */
    /* hospital 5: one nurse with 60,000 points (u16) */
    put16(staff_slots(&b.sim, 5), 43), staff_slots(&b.sim, 5)[2] = 1;
    put16(staff(43) + 16, 60000);
    for (int h = 0; h < 16; h++) put32(world_hospital(&b.world, h) + 0x194, 7777), put16(world_hospital(&b.world, h) + 0x16A, 9);
    sim_nurse_experience(&b.sim);
    CHECK(get32(world_hospital(&b.world, 2) + 0x194) == 17 && u16(world_hospital(&b.world, 2) + 0x16A) == 2);
    CHECK(get32(world_hospital(&b.world, 5) + 0x194) == 60000 && u16(world_hospital(&b.world, 5) + 0x16A) == 1);
    CHECK(get32(world_hospital(&b.world, 7) + 0x194) == 0 && u16(world_hospital(&b.world, 7) + 0x16A) == 9);
    char c[32];
    for (int ed = 0; ed < 3; ed++) {
        b.prog.edition = (Edition)ed;
        put32(world_hospital(&b.world, 2) + 0x194, 17);
        put16(world_hospital(&b.world, 2) + 0x4A, 95);
        CHECK(ranking_cell(&b, 0x499F7, 2, 0, c, sizeof c) == 372);
        CHECK(strcmp(c, ed == EDITION_GERMAN_CD ? "17 " : "95 ") == 0);
        /* opening the window: German computes +0x194 (and not +0x4A), the others +0x4A */
        put16(world_hospital(&b.world, 2) + 0x4A, 1), put32(world_hospital(&b.world, 2) + 0x194, 1);
        static Image blank;
        static uint8_t map[256];
        b.screen.w = SCREEN_W, b.screen.h = SCREEN_H;
        b.ui.screen = &b.screen, b.ui.prog = &b.prog, b.prog.charmap = map;
        for (int i = 1; i < IXY_COUNT; i++) b.ui.cache[i] = &blank;
        ranking_routine(&b, 0x499F7);
        CHECK(get32(world_hospital(&b.world, 2) + 0x194) == (ed == EDITION_GERMAN_CD ? 17 : 1));
        CHECK(u16(world_hospital(&b.world, 2) + 0x4A) == (ed == EDITION_GERMAN_CD ? 1 : 95));
    }
    b.prog.edition = EDITION_DISK;
}

static void table_with(uint8_t t[HISCORE_SIZE], int32_t last) { /* scores 20,000 - 1,000 i, the last one `last` */
    for (int i = 0; i < 20; i++) {
        uint8_t *e = t + HISCORE_ENTRY * i;
        memset(e, '$', 20);
        e[0] = (uint8_t)('A' + i), e[1] = 'x';
        put32(e + 20, 20000 - 1000 * i);
        put16(e + 24, 30 + i), put16(e + 26, 1 + i % 16);
    }
    put32(t + HISCORE_ENTRY * 19 + 20, last);
}

static void test_hiscore(void) {
    static const int B[16] = {8000, 6000, 4000, 3000, 2000, 1500, 1100, 900, 700, 600, 500, 400, 300, 200, 100, 90};
    for (int r = 1; r <= 16; r++) {
        CHECK(hiscore_bonus(r, 30, 2, 1) == B[r - 1] && hiscore_bonus(r, 99, 1, 4) == B[r - 1]);
        CHECK(hiscore_bonus(r, 29, 5, 5) == B[r - 1] / 2 && hiscore_bonus(r, 25, 5, 5) == B[r - 1] / 2);
        CHECK(hiscore_bonus(r, 24, 5, 5) == B[r - 1] / 4 && hiscore_bonus(r, 20, 5, 5) == B[r - 1] / 4);
        CHECK(hiscore_bonus(r, 19, 5, 5) == B[r - 1] / 8 && hiscore_bonus(r, 15, 5, 5) == B[r - 1] / 8);
        CHECK(hiscore_bonus(r, 14, 5, 5) == B[r - 1] / 16 && hiscore_bonus(r, 10, 5, 5) == B[r - 1] / 16);
        CHECK(hiscore_bonus(r, 9, 5, 5) == B[r - 1] / 32 && hiscore_bonus(r, 0, 5, 5) == B[r - 1] / 32);
        CHECK(hiscore_bonus(r, 40, 1, 3) == 0 && hiscore_bonus(r, 40, 1, 1) == 0);
    }
    CHECK(hiscore_bonus(12, 19, 1, 20) == 50 && hiscore_bonus(5, 19, 1, 20) == 250); /* the spec's day-20 cases */
    uint8_t t[HISCORE_SIZE];
    table_with(t, 1850);
    CHECK(hiscore_insert(t, 1850, 19, 12) == -1); /* not strictly above */
    CHECK(hiscore_insert(t, 1435, 19, 12) == -1);
    CHECK(get32(t + HISCORE_ENTRY * 19 + 20) == 1850);
    int at = hiscore_insert(t, 7635, 19, 5);
    /* 20,000 ... 8,000 are entries 0-12: the new one goes after them */
    CHECK(at == 13);
    const uint8_t *e = t + HISCORE_ENTRY * at;
    CHECK(get32(e + 20) == 7635 && u16(e + 24) == 19 && u16(e + 26) == 5 && e[0] == '$' && e[19] == '$');
    CHECK(get32(t + HISCORE_ENTRY * 19 + 20) == 2000 && t[HISCORE_ENTRY * 14] == 'N'); /* the old last one dropped */
    for (int i = 0; i < 19; i++) CHECK(get32(t + HISCORE_ENTRY * i + 20) >= get32(t + HISCORE_ENTRY * (i + 1) + 20));
    /* step 6 takes the LAST row whose name starts with "$$": an older nameless entry above does not catch it */
    table_with(t, 100);
    t[HISCORE_ENTRY * 2] = '$', t[HISCORE_ENTRY * 2 + 1] = '$';
    CHECK(hiscore_insert(t, 9500, 3, 2) == 11);
    /* signed: a negative last score is beaten by 0 */
    table_with(t, -5);
    CHECK(hiscore_insert(t, 0, 3, 16) == 19);
    /* ties: the new entry stays after an equal score */
    table_with(t, 100);
    CHECK(hiscore_insert(t, 15000, 3, 2) == 6 && get32(t + HISCORE_ENTRY * 5 + 20) == 15000 && t[HISCORE_ENTRY * 5] == 'F');
    /* the oracle's day 20: score 7,385 at rank 5 after 19 days -> 7,635 */
    fresh();
    for (int h = 1; h < 16; h++) put32(H(h) + H_CASH, -1); /* every other hospital bankrupt: score 0 */
    put32(H(0) + H_CASH, 0);
    put16(H(0) + 0x28, 3700), put16(H(0) + 0x26, 0); /* 2 x 3,700 = 7,400 ... */
    world_set_tail(&g_world, TAIL_MONTH, 1), world_set_tail(&g_world, TAIL_DAY, 20), world_set_tail(&g_world, TAIL_HOUR, 0);
    put16(H(0) + 0x162, 19);
    for (int i = 1; i < 1009; i++) memset(person(i) + 20, 0, 16);
    table_with(t, 1850);
    int rank;
    int32_t score;
    at = hiscore_enter(s, t, &rank, &score);
    int32_t game = get32(H(0) + 0x44);
    CHECK(rank == 1 && score == game + 8000 / 8 && game == 7400 - 4 * 0 - 96 * 20 - 2880);
    CHECK(at >= 0 && get32(t + HISCORE_ENTRY * at + 20) == score && u16(t + HISCORE_ENTRY * at + 26) == 1);
    /* rank 5: four hospitals ahead */
    for (int h = 1; h <= 4; h++) put32(H(h) + H_CASH, 0), put16(H(h) + 0x28, 9000);
    table_with(t, 1850);
    at = hiscore_enter(s, t, &rank, &score);
    CHECK(rank == 5 && score == game + 2000 / 8);
}

static void test_quack(void) {
    uint8_t t[HISCORE_SIZE], out[QUACK_SIZE], back[HISCORE_SIZE];
    table_with(t, 1850);
    quack_encode(t, out);
    CHECK(out[0] == (uint8_t)(t[0] + 0x0B) && out[1] == (uint8_t)(t[1] + 0x0B + t[0]));
    unsigned sum = 0;
    for (int i = 0; i < HISCORE_SIZE; i++) sum += out[i];
    CHECK(u16(out + HISCORE_SIZE) == (int)((sum & 0xFFFF) >> 1));
    CHECK(quack_decode(out, QUACK_SIZE, back) == 0 && !memcmp(back, t, HISCORE_SIZE));
    out[17] += 2; /* the checksum drops the low bit, so a change of 1 can pass */
    CHECK(quack_decode(out, QUACK_SIZE, back) == -2);
    CHECK(quack_decode(out, QUACK_SIZE - 1, back) == -1);
    /* the file: missing -> the built-in table; written and read back; damaged -> -1 */
    fresh();
    for (int i = 0; i < HISCORE_SIZE; i++) g_image[HISCORE_DEFAULT + i] = (uint8_t)(i * 7);
    const char *path = "test_garage_quack.tmp"; /* in the working directory (the build tree under ctest) */
    remove(path);
    CHECK(quack_load(path, &g_prog, back) == 0 && back[1] == 7 && back[559] == (uint8_t)(559 * 7));
    CHECK(quack_save(path, t) == 0);
    memset(back, 0, sizeof back);
    CHECK(quack_load(path, &g_prog, back) == 1 && !memcmp(back, t, HISCORE_SIZE));
    FILE *f = fopen(path, "r+b");
    if (f) {
        fseek(f, 3, SEEK_SET);
        fputc(0x55, f);
        fclose(f);
    }
    CHECK(quack_load(path, &g_prog, back) == -1);
    remove(path);
}

/* docs/gui.md, "Keyboard": the DOS codes of the keys that type no text */
static int codes(int key, int ctrl, int alt, int shift, int a, int b) {
    int out[2] = {-1, -1}, n = dos_key_codes(key, ctrl, alt, shift, out);
    return b < 0 ? n == 1 && out[0] == a : n == 2 && out[0] == a && out[1] == b;
}
static void test_dos_keys(void) {
    CHECK(codes(DK_ENTER, 0, 0, 0, 13, -1) && codes('m', 1, 0, 0, 13, -1));
    CHECK(codes(DK_BACKSPACE, 0, 0, 0, 8, -1) && codes('h', 1, 0, 0, 8, -1));
    CHECK(codes(DK_ESCAPE, 0, 0, 0, 0x1B, -1) && codes('[', 1, 0, 0, 0x1B, -1));
    CHECK(codes(DK_TAB, 0, 0, 0, 9, -1) && codes('i', 1, 0, 0, 9, -1));
    CHECK(codes(DK_ENTER, 1, 0, 0, 0x0A, -1) && codes('j', 1, 0, 0, 0x0A, -1));
    CHECK(codes('a', 1, 0, 0, 1, -1) && codes('z', 1, 0, 0, 26, -1) && codes('u', 1, 0, 0, 0x15, -1));
    CHECK(codes('\\', 1, 0, 0, 0x1C, -1) && codes(']', 1, 0, 0, 0x1D, -1) && codes('6', 1, 0, 0, 0x1E, -1) &&
          codes('-', 1, 0, 0, 0x1F, -1));
    CHECK(codes(DK_BACKSPACE, 1, 0, 0, 0x7F, -1));
    CHECK(codes('2', 1, 0, 0, 0, 3));
    CHECK(codes(DK_F1, 0, 0, 0, 0, 0x3B) && codes(DK_F1 + 5, 0, 0, 0, 0, 0x40) && codes(DK_F1 + 9, 0, 0, 0, 0, 0x44));
    static const int nav[10] = {0x47, 0x48, 0x49, 0x4B, 0x4D, 0x4F, 0x50, 0x51, 0x52, 0x53};
    for (int k = 0; k < 10; k++) CHECK(codes(DK_HOME + k, 0, 0, 0, 0, nav[k]));
    CHECK(codes(DK_TAB, 0, 0, 1, 0, 0x0F));
    CHECK(codes('q', 0, 1, 0, 0, 0x10) && codes('p', 0, 1, 0, 0, 0x19) && codes('a', 0, 1, 0, 0, 0x1E) &&
          codes('l', 0, 1, 0, 0, 0x26) && codes('z', 0, 1, 0, 0, 0x2C) && codes('m', 0, 1, 0, 0, 0x32) &&
          codes('y', 0, 1, 0, 0, 0x15) && codes('j', 0, 1, 0, 0, 0x24));
    int out[2];
    CHECK(dos_key_codes('a', 0, 0, 0, out) == 0 && dos_key_codes('5', 1, 0, 0, out) == 0); /* text, or no code */
    /* Ctrl+Alt (AltGr on many layouts) types text: no code (engine choice) */
    CHECK(dos_key_codes('q', 1, 1, 0, out) == 0 && dos_key_codes('[', 1, 1, 0, out) == 0);
}

static int name_is(const uint8_t *n, const uint8_t *want, int len) { return memcmp(n, want, (size_t)len) == 0; }

static void test_name_input(void) {
    uint8_t n[HISCORE_NAME];
    int c = 5;
#define K(k) hiscore_name_key(n, &c, k)
    hiscore_name_start(n, &c);
    CHECK(n[0] == '|' && n[1] == '$' && n[19] == '$' && c == 0);
    CHECK(K('D') == 0 && n[0] == 'D' && n[1] == '|' && n[2] == '$' && c == 1);
    K('r');
    K('$'), K('@'), K(0x15), K(0), K(-1);
    CHECK(n[0] == 'D' && n[1] == 'r' && n[2] == '|' && c == 2);
    K(8);
    CHECK(n[1] == '|' && n[2] == 0 && c == 1); /* Backspace leaves 0 at the old cursor place */
    K(8), K(8);
    CHECK(n[0] == '|' && n[1] == 0 && n[2] == 0 && c == 0);
    for (int i = 0; i < 25; i++) K('a' + i % 26);
    CHECK(n[17] == 'r' && n[18] == '|' && n[19] == '$' && c == 18); /* at most 18 characters */
    CHECK(K(13) == 1 && n[18] == 0 && c == -1);
    CHECK(K('z') == 1 && n[18] == 0); /* ended */
    /* docs/gui.md, "Text input field": a typed '|' is a character (the count, not a search, finds the cursor) */
    hiscore_name_start(n, &c);
    K('a'), K('|'), K('b');
    CHECK(name_is(n, (const uint8_t[]){0x61, 0x7C, 0x62, 0x7C, 0x24}, 5) && c == 3);
    /* A, B, Backspace, Enter: 41 00 00 24 24 */
    hiscore_name_start(n, &c);
    K('A'), K('B'), K(8);
    CHECK(K(13) == 1 && name_is(n, (const uint8_t[]){0x41, 0x00, 0x00, 0x24, 0x24}, 5));
    /* the oracle's key list: A 1B B 09 01 05 0A 1F 7F 00 48 15 40 24 08 0D (control codes are characters, 0 ignored,
     * the scan code 0x48 an 'H', 15 / 40 / 24 ignored) */
    hiscore_name_start(n, &c);
    static const int keys[] = {'A', 0x1B, 'B', 0x09, 0x01, 0x05, 0x0A, 0x1F, 0x7F, 0x00, 0x48, 0x15, 0x40, 0x24};
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) K(keys[i]);
    CHECK(c == 10 && name_is(n, (const uint8_t[]){0x41, 0x1B, 0x42, 0x09, 0x01, 0x05, 0x0A, 0x1F, 0x7F, 0x48, 0x7C}, 11));
    K(8);
    CHECK(c == 9 && name_is(n, (const uint8_t[]){0x41, 0x1B, 0x42, 0x09, 0x01, 0x05, 0x0A, 0x1F, 0x7F, 0x7C, 0x00, 0x24}, 12));
    CHECK(K(13) == 1 && name_is(n, (const uint8_t[]){0x7F, 0x00, 0x00, 0x24}, 0) && n[8] == 0x7F && n[9] == 0 &&
          n[10] == 0 && n[11] == 0x24);
    /* codes from 0x80 up are characters too */
    hiscore_name_start(n, &c);
    K(0xE1), K(0x80);
    CHECK(c == 2 && n[0] == 0xE1 && n[1] == 0x80 && n[2] == '|');
#undef K
}

int main(void) {
    test_list();
    test_send();
    test_tables();
    test_activity();
    test_gap();
    test_start();
    test_sim51_stored_city();
    test_start_names();
    test_minute();
    test_sim63_minute30_r31();
    test_formulas();
    test_round();
    test_victim();
    test_finish();
    test_season();
    test_season_domain();
    test_season_schedule();
    test_ai_purchase();
    test_ai_staff();
    test_ai_tie();
    test_ai_entry_sale();
    test_ai_garage_early_ends();
    test_garage_minute();
    test_fittings();
    test_schedule();
    test_race_word_elsewhere();
    test_patient_counts();
    test_holdings();
    test_ranking_cells();
    test_nurse_experience_ranking();
    test_hiscore();
    test_quack();
    test_name_input();
    test_dos_keys();
    world_free(&g_world);
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] garage, race and desktop tests passed\n");
    return 0;
}
