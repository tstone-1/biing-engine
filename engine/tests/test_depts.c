/* The player's decisions in the specialist departments (docs/ui-departments.md). Synthetic world and program image
 * (see AGENTS.md): every illness, treatment, equipment item, operation and room description below is made up for the
 * test; the expected values follow the rules of the spec (its oracle runs give the shape, e.g. a dental case that
 * waited 5 minutes is called before a psychological one that waited 120). */
#include <stdio.h>
#include <string.h>

#include "fixes.h"
#include "depts.h"
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
static uint8_t g_ailments[64];
static Sim g_sim;
static Sim *s = &g_sim;

#define ILL(id) (g_image + 0x954 + (id) * 20)
#define TREAT(id) (g_image + 0x270C + (id) * 38)
#define ITEM(id) (g_image + 0x144A + (id) * 20)

static uint8_t *h0(void) { return world_hospital(&g_world, 0); }
static uint8_t *pb(void) { return rooms_pb(s); }
static uint8_t *P(int i) { return rooms_person(s, i); }
static uint8_t *rec(int cat, int i) { return rooms_record(s, cat, i); }
static uint8_t *counts(void) { return g_world.block[BLK_ROOM_COUNTS]; }
static int staff_work(int i) { return get16(rooms_staff(s, i) + 0x10); }
static int u16(const uint8_t *p) { return (uint16_t)get16(p); }

static void describe(int cat, int furniture, int equipment, int cond_a, int cond_b, int type) {
    const int d[7] = {furniture, 4, equipment, 4, cond_a, cond_b, type};
    for (int i = 0; i < 7; i++) put16(g_image + 0x4758 + 14 * cat + 2 * i, d[i]);
}

/* a person of the player's hospital with an illness of the given id, health 70, mood 60, opinion 80 */
static void ill(int i, int sex, int id) {
    uint8_t *p = P(i);
    memset(p, 0, 106);
    p[0] = (uint8_t)sex, p[1] = 1, p[3] = 0, p[4] = 0xFF;
    p[41] = 1, p[44] = 70, p[56] = (uint8_t)id, p[78] = 60, p[20] = 80;
    put16(p + 94, 700);
}

static void seat(int index, int place, int wait) { /* waiting room 0 */
    uint8_t *w = rec(3, 0);
    put16(w + 0x14 + 4 * place, index), put16(w + 0x16 + 4 * place, wait);
    w[12]++;
    P(index)[4] = 3, P(index)[66] = 0;
}

static void in_bed(int cat, int ward, int place, int index) {
    uint8_t *k = rec(cat, ward);
    put16(k + 0x14 + 4 * place, index);
    k[12]++;
    P(index)[4] = (uint8_t)cat, P(index)[66] = (uint8_t)ward;
}

static void fresh(void) {
    world_free(&g_world);
    world_alloc(&g_world);
    memset(g_world.block[BLK_ROOM_SLOTS], 0xFF, g_world.size[BLK_ROOM_SLOTS]);
    memset(g_image, 0, sizeof g_image);
    g_prog.image.data = g_image;
    g_prog.image.size = sizeof g_image;
    world_set_tail(&g_world, TAIL_DAY, 1), world_set_tail(&g_world, TAIL_MONTH, 1);
    world_set_tail(&g_world, TAIL_HOUR, 10);
    world_hospital(&g_world, 15)[0x15] = 0xFF;
    sim_init(&g_sim, &g_world, &g_prog, 7);
    for (int i = 0; i < 16; i++) g_ailments[i] = 1;
    sim_set_ailments(&g_sim, g_ailments, 64);
    for (int i = 0; i < 1041; i++) rooms_staff(s, i)[11] = 0;
    for (int i = 0; i <= 1008; i++) P(i)[3] = P(i)[4] = 0xFF;
    h0()[0x14] = 2; /* human-controlled */
    describe(7, 0x9A, 0xA4, 0xAC, 0xB0, 0xB4);
    describe(8, 0x9A, 0xA4, 0xAC, 0xB0, 0xB4);
    describe(11, 0x60, 0x68, 0x70, 0x74, 0x78);
    describe(15, 0x60, 0x68, 0x70, 0x74, 0x78);
    /* illness 7: physical; 8: psychological, treated in the room by 2; 10: dental, treated in the room by 11 (best)
     * or 9; 12: psychological, treated in a bed by 13; 14: physical, operated (treatment 5, operation 1) */
    ILL(8)[8] = 2, ILL(8)[11] = 2, ILL(8)[12] = 2, ILL(8)[13] = 2, ILL(8)[14] = 2, ILL(8)[18] = 2;
    ILL(10)[8] = 1, ILL(10)[15] = 3, ILL(10)[11] = 11, ILL(10)[12] = 9, ILL(10)[13] = 9, ILL(10)[14] = 9;
    ILL(12)[8] = 2, ILL(12)[11] = 13, ILL(12)[12] = 13, ILL(12)[13] = 13, ILL(12)[14] = 13;
    ILL(14)[11] = 5;
    TREAT(2)[6] = 1, TREAT(2)[7] = 3, TREAT(2)[16] = 1;
    TREAT(9)[6] = 1, TREAT(11)[6] = 1, TREAT(11)[7] = 4, TREAT(11)[16] = 1;
    TREAT(13)[4] = 1, TREAT(13)[6] = 1, TREAT(13)[16] = 8;
    TREAT(5)[1] = 1, TREAT(5)[4] = 1, TREAT(5)[6] = 1, TREAT(5)[15 + 2] = 11, TREAT(5)[15 + 3] = 71;
    /* operation 1 needs capability 4 (the OP's 0x248C, the torture chamber's 0x26A8) */
    put16(g_image + 0x248C + 20, 4), put16(g_image + 0x26A8 + 20, 4);
    ITEM(3)[10] = 4, ITEM(3)[16] = 1; /* an instrument with capability 4 */
    world_set_cash(&g_world, 0, 984);
    h0()[0x95] = 4;
}

/* every department of the spec, one room each, with its staff (indices 20-41) */
static void clinic(void) {
    fresh();
    counts()[1] = counts()[2] = counts()[3] = counts()[6] = 1;
    counts()[7] = counts()[8] = counts()[9] = counts()[11] = counts()[15] = counts()[16] = counts()[18] = 1;
    put16(rec(2, 0) + 8, 20);
    rec(3, 0)[10] = 10, put16(rec(3, 0) + 8, 21);
    put16(rec(6, 0) + 8, 24), rec(6, 0)[10] = 4;
    put16(rec(11, 0) + 8, 30), put16(rec(11, 0) + 10, 31);
    put16(rec(15, 0) + 8, 32), put16(rec(15, 0) + 10, 33);
    put16(rec(16, 0) + 8, 34), rec(16, 0)[10] = 4;
    put16(rec(9, 0) + 8, 35);
    put16(rec(7, 0) + 8, 36), put16(rec(7, 0) + 6, 37), put16(rec(7, 0) + 10, 38);
    put16(rec(8, 0) + 8, 39), put16(rec(8, 0) + 10, 40);
    put16(rec(18, 0) + 10, 41), put16(rec(18, 0) + 8, 20);
}

/* ---- dental station and neurology ---------------------------------------------------------------------------- */

static void test_dental_call(void) {
    RoomsResult o;
    clinic();
    put16(rec(11, 0) + 10, 0);
    CHECK(depts_clinic_call(s, 11, &o) == 419);
    put16(rec(11, 0) + 10, 31), put16(rec(11, 0) + 8, 0);
    CHECK(depts_clinic_call(s, 11, &o) == 415);
    put16(rec(11, 0) + 8, 30), rooms_staff(s, 30)[11] = 1;
    CHECK(depts_clinic_call(s, 11, &o) == 539 && o.staff == 30);
    rooms_staff(s, 30)[11] = 0;
    CHECK(depts_clinic_call(s, 11, &o) == 409); /* nobody waiting: the torture chamber's text */
    put16(rec(11, 0) + 12, 99);
    CHECK(depts_clinic_call(s, 11, &o) == 411);
    put16(rec(11, 0) + 12, 0);
    ill(5, 0, 8), seat(5, 2, 120); /* psychological, waited 120 */
    ill(6, 1, 10), seat(6, 7, 5); /* dental, waited 5 */
    int work = staff_work(30);
    CHECK(depts_clinic_call(s, 11, &o) == 1340 && o.index == 6 && o.room == 0 && o.staff == 30);
    const uint8_t *r = rec(11, 0);
    CHECK(u16(r + 12) == 6 && u16(r + 14) == 999 && !u16(r + 16) && !u16(r + 18));
    CHECK(P(6)[4] == 11 && P(6)[66] == 0 && rec(3, 0)[12] == 1 && !u16(rec(3, 0) + 0x14 + 4 * 7));
    CHECK(staff_work(30) == work + 1 && !get32(pb() + RB_CALL));
    /* nobody of the room's type: anyone is called (player only) */
    put16(rec(11, 0) + 12, 0);
    CHECK(depts_clinic_call(s, 11, &o) == 1340 && o.index == 5 && P(5)[4] == 11);
}

static void test_neurology_call(void) {
    RoomsResult o;
    clinic();
    put16(rec(15, 0) + 10, 0);
    CHECK(depts_clinic_call(s, 15, &o) == 418);
    put16(rec(15, 0) + 10, 33), put16(rec(15, 0) + 8, 0);
    CHECK(depts_clinic_call(s, 15, &o) == 414);
    put16(rec(15, 0) + 8, 32);
    CHECK(depts_clinic_call(s, 15, &o) == 406);
    ill(5, 0, 10), seat(5, 2, 120); /* dental, waited 120 */
    ill(6, 1, 8), seat(6, 7, 5); /* psychological, waited 5 */
    put16(rec(15, 0) + 12, 5);
    CHECK(depts_clinic_call(s, 15, &o) == 410);
    put16(rec(15, 0) + 12, 0);
    CHECK(depts_clinic_call(s, 15, &o) == 1340 && o.index == 6 && P(6)[4] == 15);
}

/* the patient in the dental station after the diagnosis: diagnosed illness `diag`, treatment `t` proposed */
static void diagnosed(int cat, int index, int diag, int t) {
    uint8_t *r = rec(cat, 0);
    put16(r + 12, index), put16(r + 14, 0), put16(r + 16, 0), put16(r + 18, 0);
    P(index)[4] = (uint8_t)cat, P(index)[42] = (uint8_t)diag, P(index)[43] = (uint8_t)t;
}

static void test_dental_sheet_refusals(void) {
    RoomsResult o;
    clinic();
    ill(6, 1, 10), diagnosed(11, 6, 10, 11);
    uint8_t *r = rec(11, 0);
    rooms_staff(s, 31)[11] = 1, put16(r + 0x13, 7); /* the independent dentist: the name from R+0x13 */
    CHECK(depts_clinic_other(s, 11, &o) == 42 && o.staff == 7);
    CHECK(depts_clinic_accept(s, 11, &o) == 42);
    for (int ed = EDITION_GERMAN_CD; ed <= EDITION_ENGLISH_CD; ed++) { /* the CD builds name the dentist, R+0x0A */
        g_prog.edition = (Edition)ed;
        CHECK(depts_clinic_other(s, 11, &o) == 42 && o.staff == 31);
        CHECK(depts_clinic_accept(s, 11, &o) == 42 && o.staff == 31);
    }
    g_prog.edition = EDITION_DISK;
    rooms_staff(s, 31)[11] = 0;
    put16(r + 14, 999);
    CHECK(depts_clinic_other(s, 11, &o) == 89 && depts_clinic_accept(s, 11, &o) == 89);
    CHECK(depts_clinic_bill(s, 11, &o) == 89);
    put16(r + 14, 3);
    CHECK(depts_clinic_other(s, 11, &o) == 87 && depts_clinic_accept(s, 11, &o) == 87);
    CHECK(depts_clinic_bill(s, 11, &o) == 87);
    put16(r + 14, 0), put16(r + 16, 11);
    CHECK(depts_clinic_other(s, 11, &o) == 78 && depts_clinic_accept(s, 11, &o) == 80);
    put16(r + 18, 5);
    CHECK(depts_clinic_bill(s, 11, &o) == 84); /* treatment running */
    put16(r + 18, 0);
    CHECK(depts_clinic_bill(s, 11, &o) == 84); /* field 79 = 0 */
    P(6)[79] = 0xFF, put16(r + 16, 0);
    CHECK(depts_clinic_bill(s, 11, &o) == 84); /* the dental bill also wants R+0x10 */
    /* neurology: its own texts, and no R+0x10 test in the bill */
    ill(8, 0, 8), diagnosed(15, 8, 8, 2);
    put16(rec(15, 0) + 14, 999);
    CHECK(depts_clinic_other(s, 15, &o) == 396 && depts_clinic_bill(s, 15, &o) == 396);
    put16(rec(15, 0) + 14, 2);
    CHECK(depts_clinic_accept(s, 15, &o) == 88);
    put16(rec(15, 0) + 14, 0), P(8)[79] = 0xFF;
    CHECK(depts_clinic_bill(s, 15, &o) == 14);
}

static void test_dental_other_and_accept(void) {
    RoomsResult o;
    clinic();
    ill(6, 1, 10), diagnosed(11, 6, 10, 0);
    CHECK(depts_clinic_other(s, 11, &o) == 0);
    CHECK(P(6)[43] == 11 || P(6)[43] == 9); /* one of the illness's four */
    CHECK(P(6)[79] == 0); /* treated in the room (byte 6 non-zero) */
    P(6)[43] = 11;
    CHECK(depts_clinic_accept(s, 11, &o) == 0); /* in the room: R+0x10 = the treatment, R+0x12 = 999 */
    CHECK(u16(rec(11, 0) + 16) == 11 && u16(rec(11, 0) + 18) == 999);
    CHECK(rec(11, 0)[0x44] == 0); /* nothing missing */
    /* a wrong department: back to a waiting room, mood halved, opinion - 1 */
    ill(7, 0, 8), diagnosed(11, 7, 8, 0xFD);
    CHECK(depts_clinic_accept(s, 11, &o) == 32 && o.index == 7 && o.room == 0);
    CHECK(P(7)[4] == 3 && P(7)[78] == 30 && P(7)[20] == 79 && !u16(rec(11, 0) + 12));
}

static void test_dental_bill_and_abort(void) {
    RoomsResult o;
    clinic();
    ill(6, 1, 10), diagnosed(11, 6, 10, 11);
    put16(rec(11, 0) + 16, 11), P(6)[79] = 0xFF, P(6)[50] = 20; /* treated: 20 ward units at key 2 */
    h0()[0x20] = 2;
    int cash = world_cash(&g_world, 0);
    CHECK(depts_clinic_bill(s, 11, &o) == 14 && o.index == 6 && o.bill == 4 + 10);
    CHECK(world_cash(&g_world, 0) == cash + 14);
    CHECK(get16(h0() + 0x26) == 1 && get16(h0() + 0x30) == 1 && get32(h0() + 0x16E) == 1);
    CHECK(P(6)[3] == 0xFF && P(6)[4] == 0xFF && P(6)[105] == 4 && P(6)[79] == 0xFF && !u16(rec(11, 0) + 12));
    /* abort: allowed at any stage; the room's +0xE stays */
    ill(7, 0, 10), diagnosed(11, 7, 10, 11);
    put16(rec(11, 0) + 14, 999);
    CHECK(depts_clinic_abort(s, 11, &o) == 77 && o.index == 7);
    CHECK(!u16(rec(11, 0) + 12) && u16(rec(11, 0) + 14) == 999);
    CHECK(P(7)[3] == 0xFF && P(7)[4] == 0xFF && P(7)[20] == 0 && !P(7)[42] && !P(7)[43]);
    /* the neurology's bill counts +0x4E */
    ill(8, 0, 8), diagnosed(15, 8, 8, 2), P(8)[79] = 0xFF;
    CHECK(depts_clinic_bill(s, 15, &o) == 14 && get16(h0() + 0x4E) == 1 && get16(h0() + 0x30) == 1);
}

static void test_neurology_bed(void) {
    RoomsResult o;
    clinic();
    ill(6, 1, 12), diagnosed(15, 6, 12, 13);
    counts()[16] = 0;
    CHECK(depts_clinic_accept(s, 15, &o) == 1462 && u16(rec(15, 0) + 12) == 6);
    counts()[16] = 1, rec(16, 0)[12] = 4; /* full */
    CHECK(depts_clinic_accept(s, 15, &o) == -1 && u16(rec(15, 0) + 12) == 6); /* nothing happens */
    rec(16, 0)[12] = 0;
    rooms_set_pb16(s, DB_NEURO_WARD, 5);
    CHECK(depts_clinic_accept(s, 15, &o) == 81 && rooms_pb16(s, DB_NEURO_WARD) == 0);
    int work = staff_work(32);
    CHECK(depts_neuro_bed(s, &o) == 2362 && o.index == 6 && o.staff == 32 && o.room == 0);
    const uint8_t *k = rec(16, 0);
    int bed = -1;
    for (int i = 0; i < 10; i++)
        if (u16(k + 0x14 + 4 * i) == 6) bed = i;
    CHECK(bed >= 0 && k[12] == 1 && P(6)[4] == 16 && P(6)[66] == 0 && !u16(rec(15, 0) + 12));
    CHECK(get16(h0() + 0x4C) == 1 && get16(h0() + 0x4E) == 1 && staff_work(32) == work);
    /* a full ward: the counters all the same, nothing more */
    ill(7, 0, 12), diagnosed(15, 7, 12, 13);
    rec(16, 0)[12] = 4;
    CHECK(depts_neuro_bed(s, &o) == -1 && u16(rec(15, 0) + 12) == 7 && get16(h0() + 0x4C) == 2);
}

/* the dental tick in the player's hospital: the diagnosis ends with the player's diagnosis and the room's own
 * proposal, and the patient keeps the dental station's state */
static void test_dental_tick(void) {
    clinic();
    ill(6, 1, 10);
    uint8_t *r = rec(11, 0);
    put16(r + 12, 6), put16(r + 14, 1), P(6)[4] = 11;
    sim_call(s, 0x6B445);
    CHECK(u16(r + 14) == 0 && P(6)[42] == 10 && (P(6)[43] == 11 || P(6)[43] == 9));
    CHECK(P(6)[4] == 11 && P(6)[66] == 0);
}

/* ---- neuro ward ---------------------------------------------------------------------------------------------- */

static void test_neuro_ward(void) {
    RoomsResult o;
    clinic();
    ill(6, 1, 12), in_bed(16, 0, 3, 6), P(6)[43] = 13, P(6)[102] = 50; /* under narcosis: no test here */
    CHECK(depts_neuro_ward_bed(s, 3) == 6 && rooms_pb16(s, RB_PLACE) == 3);
    CHECK(depts_neuro_ward_bed(s, 2) == 0);
    world_set_tail(&g_world, TAIL_HOUR, 21);
    CHECK(depts_neuro_discharge(s, &o) == 2282);
    world_set_tail(&g_world, TAIL_HOUR, 10);
    P(6)[44] = 4;
    CHECK(depts_neuro_discharge(s, &o) == 2280 && P(6)[4] == 16);
    P(6)[44] = 70;
    depts_neuro_step(s);
    CHECK(P(6)[93] == 0xFF);
    depts_neuro_step(s);
    CHECK(P(6)[93] == 0);
    int cash = world_cash(&g_world, 0);
    CHECK(depts_neuro_discharge(s, &o) == 14 && o.index == 6 && o.bill == 4);
    CHECK(world_cash(&g_world, 0) == cash + 4 && get32(h0() + 0x16E) == 1);
    CHECK(!u16(rec(16, 0) + 0x14 + 12) && rec(16, 0)[12] == 0 && P(6)[4] == 0xFF);
    /* the bill's reaction first (bill 4 in a ward: +3, halved to 2 from opinion 75, four times: 88), then the case's
     * - 35 (the spec's oracle run: 80 -> 53) */
    CHECK(get16(h0() + 0x2A) == 1 && get16(h0() + 0x12C) == 1 && P(6)[20] == 53);
}

/* ---- intensive care --------------------------------------------------------------------------------------------- */

static void test_icu_back(void) {
    RoomsResult o;
    clinic();
    ill(6, 1, 7), put16(rec(9, 0) + 12, 6), P(6)[4] = 9, P(6)[44] = 40, put16(rec(9, 0) + 0x10, 7);
    CHECK(depts_icu_patient(s) == 0 && rooms_pb16(s, RB_PATIENT) == 6);
    for (int i = 0; i < 6; i++) put16(rec(2, 0) + 0x46 + 4 * i, 100 + i);
    CHECK(depts_icu_back(s, &o) == 423 && P(6)[4] == 9);
    put16(rec(2, 0) + 0x46 + 4, 0);
    P(6)[102] = 3;
    CHECK(depts_icu_back(s, &o) == 424 && P(6)[4] == 9);
    P(6)[102] = 0;
    CHECK(depts_icu_back(s, &o) == 421 && o.index == 6); /* at health 40: the player may */
    CHECK(u16(rec(2, 0) + 0x46 + 4) == 6 && P(6)[4] == 2 && P(6)[93] == 0xFF && !u16(rec(9, 0) + 12));
    CHECK(u16(rec(9, 0) + 0x10) == 7 && get16(h0() + 0x58) == 0); /* the hours stay; +0x58 only from 50 */
    ill(7, 0, 7), put16(rec(9, 0) + 12, 7), P(7)[4] = 9, P(7)[44] = 60;
    put16(rec(2, 0) + 0x46 + 8, 0);
    CHECK(depts_icu_back(s, &o) == 421 && get16(h0() + 0x58) == 1);
    put16(rec(9, 0) + 12, 0);
    CHECK(depts_icu_patient(s) == 399);
}

/* ---- OP and torture chamber -------------------------------------------------------------------------------------- */

/* P in Krankenzimmer bed `place` at the step of treatment 5 that wants the OP (code 11, field 45 = 2) */
static void to_be_operated(int index, int place) {
    ill(index, 0, 14), in_bed(6, 0, place, index);
    P(index)[43] = 5, P(index)[45] = 2;
}

static void test_op_fetch(void) {
    RoomsResult o;
    clinic();
    uint8_t *r = rec(7, 0);
    put16(r + 8, 0);
    CHECK(depts_theatre_fetch(s, 7, &o) == 416);
    put16(r + 8, 36), rooms_staff(s, 36)[11] = 1;
    CHECK(depts_theatre_fetch(s, 7, &o) == 539 && o.staff == 36);
    rooms_staff(s, 36)[11] = 0, put16(r + 12, 99);
    CHECK(depts_theatre_fetch(s, 7, &o) == 412);
    put16(r + 12, 0);
    CHECK(depts_theatre_fetch(s, 7, &o) == 407); /* nobody in the Krankenzimmer */
    ill(5, 1, 7), in_bed(6, 0, 0, 5); /* a bed patient without an operation */
    CHECK(depts_theatre_fetch(s, 7, &o) == 407 && !get32(pb() + RB_CALL));
    to_be_operated(6, 2);
    int work = staff_work(36);
    CHECK(depts_theatre_fetch(s, 7, &o) == 501 && o.index == 6 && o.room == 0 && o.staff == 36);
    CHECK(u16(r + 12) == 6 && u16(r + 14) == 999 && !get32(r + 0x10) && u16(r + 0x14) == 2 && !u16(r + 0x18));
    CHECK(P(6)[4] == 7 && P(6)[98] == 6 && P(6)[99] == 0 && P(6)[66] == 0 && staff_work(36) == work + 1);
    CHECK(u16(rec(6, 0) + 0x14 + 8) == 6); /* P keeps its bed */
    CHECK(depts_theatre_patient(s, 7) == 0 && rooms_pb16(s, RB_PATIENT) == 6);
}

static void test_op_start_stop(void) {
    RoomsResult o;
    clinic();
    to_be_operated(6, 2);
    depts_theatre_fetch(s, 7, &o);
    uint8_t *r = rec(7, 0);
    rooms_staff(s, 38)[11] = 1;
    CHECK(depts_theatre_toggle(s, 7, &o) == 3017 && u16(r + 14) == 999);
    rooms_staff(s, 38)[11] = 0;
    CHECK(depts_theatre_toggle(s, 7, &o) == 531 && o.item == 4 && r[4] == 4); /* the instrument is missing */
    CHECK(u16(r + 14) == 999 && get16(h0() + 0x32) == 0);
    put16(r + 0xA4, 3), r[0xB0] = 50; /* the instrument in the room */
    CHECK(depts_theatre_toggle(s, 7, &o) == 0);
    CHECK(u16(r + 14) == 6 && u16(r + 0x10) == 1 && P(6)[102] == 120 && P(6)[100] == 1 && u16(r + 0x14) == 2);
    /* each of the ten capabilities is used; the nine empty ones (0) find the instrument too, whose ids include 0
     * ("has" finds the first item with c among its four ids, docs/sim-rooms2.md): 50 - 10 */
    CHECK(get16(h0() + 0x32) == 1 && r[0xB0] == 40);
    /* stop: one more bleeding step, and the next minute starts it again from its first stage */
    CHECK(depts_theatre_toggle(s, 7, &o) == 0);
    CHECK(u16(r + 14) == 999 && u16(r + 0x12) == 0 && u16(r + 0x14) == 0 && P(6)[101] == 1 && u16(r + 0x10) == 1);
    sim_call(s, 0x58045);
    CHECK(u16(r + 14) == 6 && get16(h0() + 0x32) == 2 && P(6)[102] == 120);
    /* an operation that needs an organ, with none in a bank */
    TREAT(5)[3] = 2;
    P(6)[100] = 0, put16(r + 14, 999), put16(r + 0x12, 0), put16(r + 0x18, 0);
    CHECK(depts_theatre_toggle(s, 7, &o) == 530 && u16(r + 14) == 999 && !get16(h0() + 0x86));
}

static void test_op_back(void) {
    RoomsResult o;
    clinic();
    to_be_operated(6, 2);
    depts_theatre_fetch(s, 7, &o);
    uint8_t *r = rec(7, 0);
    CHECK(depts_theatre_back(s, 7, &o) == 2269 && o.index == 6 && o.total == 6 && o.room == 0 && o.staff == 36);
    CHECK(P(6)[4] == 6 && !u16(r + 12) && u16(r + 14) == 0); /* +0xE becomes 0, not 999 */
    depts_theatre_fetch(s, 7, &o);
    P(6)[100] = 1, P(6)[101] = 1, P(6)[102] = 120;
    CHECK(depts_theatre_back(s, 7, &o) == 2275 && P(6)[4] == 6 && P(6)[100] == 1 && P(6)[101] == 1);
    CHECK(depts_theatre_fetch(s, 7, &o) == 407); /* the recovery countdown keeps it in the bed */
    P(6)[102] = 0;
    CHECK(depts_theatre_fetch(s, 7, &o) == 501);
    P(6)[100] = 2, put16(r + 0x18, 0x5003);
    CHECK(depts_theatre_back(s, 7, &o) == 2277 && get16(h0() + 0x8A) == 1 && !u16(r + 0x18));
}

static void test_torture(void) {
    RoomsResult o;
    clinic();
    uint8_t *r = rec(8, 0);
    put16(r + 8, 0);
    CHECK(depts_theatre_fetch(s, 8, &o) == 417);
    put16(r + 8, 39);
    CHECK(depts_theatre_fetch(s, 8, &o) == 408);
    ill(6, 0, 14), in_bed(16, 0, 1, 6), P(6)[43] = 5, P(6)[45] = 3; /* step code 71: the torture chamber */
    CHECK(depts_theatre_fetch(s, 8, &o) == 501 && o.index == 6 && P(6)[4] == 8 && P(6)[98] == 16);
    CHECK(depts_theatre_fetch(s, 8, &o) == 0 && rooms_pb16(s, RB_PATIENT) == 6); /* with a patient: the sheet */
    put16(r + 0xA4, 3), r[0xB0] = 50;
    CHECK(depts_theatre_toggle(s, 8, &o) == 0 && u16(r + 14) == 6 && P(6)[102] == 120);
    CHECK(get16(h0() + 0x114) == 1);
    CHECK(depts_theatre_toggle(s, 8, &o) == 0 && u16(r + 14) == 999 && P(6)[101] == 1);
    sim_call(s, 0x597F9); /* the restart after a stop: field 102 = 40 */
    CHECK(u16(r + 14) == 6 && get16(h0() + 0x114) == 2 && P(6)[102] == 40);
    CHECK(depts_theatre_back(s, 8, &o) == 1336 && o.total == 16 && P(6)[4] == 16 && P(6)[93] == 0xFF);
    CHECK(u16(rec(16, 0) + 0x14 + 4) == 6 && !u16(r + 12));
    CHECK(depts_theatre_patient(s, 8) == 83);
    rooms_staff(s, 40)[11] = 1;
    CHECK(depts_theatre_toggle(s, 8, &o) == 3018);
}

/* ---- pathology ----------------------------------------------------------------------------------------------- */

static void test_pathology(void) {
    RoomsResult o;
    clinic();
    uint8_t *r = rec(18, 0);
    r[8] = 5;
    CHECK(depts_path_fetch(s, &o) == 400);
    ill(6, 1, 7), P(6)[104] = 1, P(6)[4] = 18;
    put16(r + 0x58, 6), put16(r + 6, 1);
    put16(r + 10, 0);
    CHECK(depts_path_fetch(s, &o) == 401);
    put16(r + 10, 41);
    CHECK(depts_path_fetch(s, &o) == 1490 && o.index == 6 && o.staff == 41);
    CHECK(u16(r + 12) == 6 && u16(r + 6) == 0 && u16(r + 14) == 5 && u16(r + 0x10) == 2 && !u16(r + 0x12));
    CHECK(depts_path_fetch(s, &o) == 402);
    CHECK(depts_path_open(s) == 0 && rooms_pb16(s, RB_PATIENT) == 6);
    CHECK(depts_path_throw(s, &o) == 1486 && o.staff == 41); /* not started: no verdict */
    CHECK(!u16(r + 12) && P(6)[3] == 0xFF && P(6)[44] == 100 && !get16(h0() + 0x3A) && !get16(h0() + 0x3C));
    CHECK(depts_path_open(s) == 403);
    put16(r + 12, 6), put16(r + 14, 999);
    CHECK(depts_path_throw(s, &o) == 1488);
    put16(r + 12, 6), put16(r + 14, 12), put16(r + 0x12, 3);
    CHECK(depts_path_throw(s, &o) == 1484);
}

/* 0x6F6B2 per minute: the disk build hangs (the engine skips the hospital) on a list entry that is not a dead person
 * of H in state 18, on such a case, and on a case with person +0x68 = 0; the English CD goes on with the case
 * (docs/editions.md 7.3) */
static void test_pathology_traps(void) {
    for (int ed = 0; ed < 3; ed++) {
        for (int trap = 0; trap < 4; trap++) {
            clinic();
            g_prog.edition = (Edition)ed;
            uint8_t *r = rec(18, 0);
            ill(6, 1, 7), P(6)[104] = 1, P(6)[4] = 18;
            ill(7, 1, 7), P(7)[104] = 1, P(7)[4] = 18;
            put16(r + 12, 6), put16(r + 14, 5), put16(r + 0x10, 0), put16(r + 0x58, 7), put16(r + 6, 1);
            if (trap == 1) P(7)[4] = 5; /* a listed person not in the pathology */
            if (trap == 2) P(6)[4] = 5; /* the case not in the pathology */
            if (trap == 3) P(6)[104] = 0; /* the case with +0x68 = 0 */
            sim_call(s, 0x6F6B2);
            int goes_on = trap == 0 || ed == EDITION_ENGLISH_CD;
            CHECK(u16(r + 14) == (goes_on ? 4 : 5));
            CHECK(u16(r + 0x10) == (goes_on || trap >= 2 ? 1 : 0)); /* the minutes count before the case's test */
        }
    }
    g_prog.edition = EDITION_DISK;
}

/* ---- shows and corpses -------------------------------------------------------------------------------------------- */

static void test_shows_and_corpse(void) {
    RoomsResult o;
    clinic();
    put16(rec(11, 0) + 8, 0), put16(rec(16, 0) + 8, 0), put16(rec(9, 0) + 8, 0), put16(rec(7, 0) + 8, 0);
    CHECK(depts_show(s, 11) == 552 && depts_show(s, 16) == 549 && depts_show(s, 9) == 553 && depts_show(s, 7) == 547);
    put16(rec(11, 0) + 8, 30), put16(rec(7, 0) + 8, 36);
    CHECK(depts_show(s, 11) == 0 && rec(11, 0)[0x19] >= 22 && rec(11, 0)[0x19] <= 37);
    CHECK(depts_show(s, 7) == 0 && rec(7, 0)[0x17] >= 22);
    /* a dead dental patient to the pathology, named by the dental nurse */
    ill(6, 1, 10), diagnosed(11, 6, 10, 11), P(6)[104] = 1;
    rooms_set_pb16(s, RB_PATIENT, 6);
    uint8_t *path = rec(18, 0);
    path[8] = 5;
    CHECK(depts_corpse(s, 11, &o) == 1340 && o.staff == 30);
    CHECK(P(6)[4] == 18 && u16(path + 0x58) == 6 && u16(path + 6) == 1 && !u16(rec(11, 0) + 12));
    /* a dead patient in the OP leaves its Krankenzimmer bed too */
    to_be_operated(7, 1), depts_theatre_fetch(s, 7, &o), P(7)[104] = 1;
    rooms_set_pb16(s, RB_PATIENT, 7);
    CHECK(depts_corpse(s, 7, &o) == 1340 && o.staff == 36 && !u16(rec(6, 0) + 0x14 + 4) && !u16(rec(7, 0) + 12));
    counts()[18] = 0;
    ill(8, 0, 7), put16(rec(9, 0) + 12, 8), P(8)[4] = 9, P(8)[104] = 1;
    rooms_set_pb16(s, RB_PATIENT, 8);
    CHECK(depts_corpse(s, 9, &o) == 537 && !u16(rec(9, 0) + 12));
}

/* FIX-3 (docs/fixes.md): the dental refusal names R+0x13 on the disk build (off) or the dentist at R+0x0A (on) */
static void test_fix3_dental_refusal_name(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 2 : 0);
        RoomsResult o;
        clinic();
        ill(6, 1, 10), diagnosed(11, 6, 10, 11);
        uint8_t *r = rec(11, 0);
        rooms_staff(s, 31)[11] = 1, put16(r + 0x13, 7);
        CHECK(depts_clinic_other(s, 11, &o) == 42 && o.staff == (on ? 31 : 7));
        CHECK(depts_clinic_accept(s, 11, &o) == 42 && o.staff == (on ? 31 : 7));
        g_prog.edition = EDITION_GERMAN_CD;
        CHECK(depts_clinic_other(s, 11, &o) == 42 && o.staff == 31);
        g_prog.edition = EDITION_DISK;
    }
    fixes_set(0);
}

int main(void) {
    test_fix3_dental_refusal_name();
    test_dental_call();
    test_neurology_call();
    test_dental_sheet_refusals();
    test_dental_other_and_accept();
    test_dental_bill_and_abort();
    test_neurology_bed();
    test_dental_tick();
    test_neuro_ward();
    test_icu_back();
    test_op_fetch();
    test_op_start_stop();
    test_op_back();
    test_torture();
    test_pathology();
    test_pathology_traps();
    test_shows_and_corpse();
    world_free(&g_world);
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] all checks passed\n");
    return 0;
}
