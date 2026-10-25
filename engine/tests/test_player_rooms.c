/* Module D, the player's independent staff (docs/sim-player.md). Synthetic world and program image (AGENTS.md):
 * every illness, treatment, item and room below is made up for the test; the expected values and rates follow the
 * spec and its oracle measurements (reception moves 3 in 4, shows 1 in 8, the throw-out halves the opinion 80 -> 40,
 * the ward step goes to the first eligible bed). */
#include <stdio.h>
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
static uint8_t g_ailments[64];
static Sim g_sim;
static Sim *s = &g_sim;

#define ILL(id) (g_image + 0x954 + (id) * 20)
#define TREAT(id) (g_image + 0x270C + (id) * 38)
#define ITEM(id) (g_image + 0x144A + (id) * 20)

enum { NURSE = 1, DOCTOR = 2 }; /* job groups */

static uint8_t *h0(void) { return world_hospital(&g_world, 0); }
static uint8_t *pb(void) { return g_world.block[BLK_PLAYER0]; }
static uint8_t *P(int i) { return g_world.block[BLK_PATIENTS] + i * 106; }
static uint8_t *ST(int i) { return g_world.block[BLK_STAFF] + i * 32; }
static uint8_t *rec(int cat, int i) { return sim_room_record(s, 0, cat, i); }
static uint8_t *R(void) { return rec(2, 0); }
static uint8_t *counts(void) { return g_world.block[BLK_ROOM_COUNTS]; }
static void at(int hour, int minute) {
    world_set_tail(&g_world, TAIL_HOUR, hour);
    world_set_tail(&g_world, TAIL_MINUTE, minute);
}
static int u16(const uint8_t *p) { return (uint16_t)get16(p); }

static void describe(int cat, int furniture, int equipment, int cond_a, int cond_b, int type) {
    const int d[7] = {furniture, 4, equipment, 4, cond_a, cond_b, type};
    for (int i = 0; i < 7; i++) put16(g_image + 0x4758 + 14 * cat + 2 * i, d[i]);
}

/* employee `index` of hospital 0 in slot `slot`, put into a room field (category, room); independent or not */
static void hire(int index, int slot, int group, int cat, int room, uint8_t *field, int indep) {
    uint8_t *sl = staff_slots(s, 0) + slot * STAFF_SLOT_SIZE;
    memset(sl, 0, STAFF_SLOT_SIZE);
    put16(sl, index);
    sl[2] = (uint8_t)group, sl[3] = (uint8_t)cat, sl[4] = (uint8_t)room;
    sl[7] = 5, put16(sl + 10, 0x0303);
    uint8_t *st = ST(index);
    st[1] = (uint8_t)group, st[3] = 0, st[4] = (uint8_t)cat, st[26] = (uint8_t)room, st[11] = (uint8_t)(indep ? 1 : 0);
    st[21] = 80;
    if (field) put16(field, index);
}

/* a person with an illness, at home: health 70, opinion 80 of hospital 0 */
static void ill(int i, int sex, int id) {
    uint8_t *p = P(i);
    memset(p, 0, 106);
    p[0] = (uint8_t)sex, p[1] = 1, p[3] = p[4] = 0xFF;
    p[41] = 1, p[44] = 70, p[56] = (uint8_t)id, p[78] = 60, p[20] = 80;
    put16(p + 94, 700);
}

static void fresh(void) {
    world_free(&g_world);
    world_alloc(&g_world);
    memset(g_world.block[BLK_ROOM_SLOTS], 0xFF, g_world.size[BLK_ROOM_SLOTS]);
    memset(g_image, 0, sizeof g_image);
    g_prog.image.data = g_image;
    g_prog.image.size = sizeof g_image;
    world_set_tail(&g_world, TAIL_DAY, 1), world_set_tail(&g_world, TAIL_MONTH, 1);
    at(10, 0);
    sim_init(&g_sim, &g_world, &g_prog, 7);
    for (int i = 0; i < 16; i++) g_ailments[i] = 1;
    sim_set_ailments(&g_sim, g_ailments, 64);
    for (int i = 0; i <= 1008; i++) P(i)[3] = P(i)[4] = 0xFF;
    h0()[0x14] = 0xFF; /* human-controlled */
    s->screen = 97;
    describe(2, 0x60, 0x68, 0x70, 0x74, 0x78);
    describe(5, 0x80, 0x88, 0x90, 0x94, 0x98);
    ITEM(1)[6] = 11, ITEM(1)[16] = 0; /* a form (kind 11), wear r3 */
    ITEM(2)[6] = 3; /* a pen (kind 3) */
    /* illness 7: physical, treated in the room by treatment 2 whatever the rank; illness 8: dental (type 1);
     * illness 9: in a bed (treatment 6); illness 10: at home (treatment 3); illness 11: psychological (type 2) */
    for (int k = 11; k <= 14; k++) ILL(7)[k] = 2, ILL(9)[k] = 6, ILL(10)[k] = 3, ILL(8)[k] = 4, ILL(11)[k] = 5;
    ILL(8)[8] = 1, ILL(8)[15] = 3, ILL(11)[8] = 2;
    TREAT(2)[6] = 1, TREAT(2)[16] = 1;
    TREAT(4)[6] = 1, TREAT(4)[16] = 1;
    TREAT(5)[6] = 1, TREAT(5)[16] = 1;
    TREAT(3)[7] = 2;
    TREAT(6)[4] = 1, TREAT(6)[6] = 1, TREAT(6)[16] = 8, TREAT(6)[17] = 8;
    world_set_cash(&g_world, 0, 984);
    h0()[0x95] = 4;
}

/* reception (clerk 20 in slot 0, form and pen), waiting room with 10 seats (nurse 21), treatment room (nurse 23, doctor
 * 22), Krankenzimmer with 4 beds (nurse 24); `indep` for every employee */
static void clinic(int indep) {
    fresh();
    counts()[1] = counts()[2] = counts()[3] = counts()[5] = counts()[6] = 1;
    hire(20, 0, 3, 2, 0, R() + 8, indep);
    put16(R() + 0x68, 1), put16(R() + 0x6A, 2), R()[0x74] = R()[0x75] = 100;
    rec(3, 0)[10] = 10;
    hire(21, 1, NURSE, 3, 0, rec(3, 0) + 8, indep);
    hire(23, 2, NURSE, 5, 0, rec(5, 0) + 8, indep);
    hire(22, 3, DOCTOR, 5, 0, rec(5, 0) + 10, indep);
    rec(6, 0)[10] = 4;
    hire(24, 4, NURSE, 6, 0, rec(6, 0) + 8, indep);
}

static void at_desk(int index, int id) {
    ill(index, 0, id);
    put16(R() + 10, index);
    P(index)[3] = 0, P(index)[4] = 2;
}
static void seated(int index, int id, int seat) {
    ill(index, 0, id);
    P(index)[3] = 0, P(index)[4] = 3;
    put16(rec(3, 0) + 0x14 + 4 * seat, index);
    rec(3, 0)[12]++;
}

static int call(uint32_t routine) { return sim_call(s, routine); }

/* ---- shared parts -------------------------------------------------------------------------------------------- */

static void test_schedule(void) {
    static const uint32_t D[] = {0x49C65, 0x4A6C8, 0x552E8, 0x4A437, 0x54EF2, 0x55580, 0x55692, 0x557A4, 0x55B17, 0x55D72};
    fresh();
    for (int i = 0; i < 10; i++) CHECK(call(D[i]));
}

static void test_screens(void) {
    static const int YES[] = {1,  5,  6,  7,  8,   9,   10,  18,  19,  22,  25,  38,  39,  40, 42,
                              52, 65, 97, 112, 113, 114, 115, 118, 128, 135, 145, 146, 148, 153};
    int n = 0;
    for (int sc = 0; sc < 256; sc++) {
        int want = 0;
        for (int k = 0; k < 29; k++) want |= YES[k] == sc;
        CHECK(sim_off_duty_screen(sc) == want);
        n += sim_off_duty_screen(sc);
    }
    CHECK(n == 29);
    CHECK(!sim_off_duty_screen(29) && !sim_off_duty_screen(23) && !sim_off_duty_screen(30) && !sim_off_duty_screen(54));
}

/* the frame: r1 is drawn, and only r1, when the hospital is not human; the player-block words are restored */
static void test_frame(void) {
    clinic(1);
    at_desk(100, 7);
    h0()[0x14] = 0;
    for (int i = 0; i < 400; i++) {
        Sim twin = *s;
        sim_bits(&twin, 1);
        call(0x49C65);
        CHECK(twin.rng == s->rng && twin.rng_s == s->rng_s);
    }
    CHECK(u16(R() + 10) == 100 && P(100)[4] == 2);
    /* no reception: also only r1 */
    clinic(1);
    counts()[2] = 0;
    at_desk(100, 7);
    Sim twin = *s;
    sim_bits(&twin, 1);
    call(0x49C65);
    CHECK(twin.rng == s->rng && twin.rng_s == s->rng_s && P(100)[4] == 2);
    /* the words pb+0x0A .. +0x0D, +0x10, +0x14, +0x2C come back; the neurology's pb+0x12 does not */
    clinic(1);
    counts()[3] = 2;
    rec(3, 1)[10] = 10;
    for (int i = 0; i < 4; i++) pb()[0x0A + i] = (uint8_t)(0x31 + i);
    put16(pb() + 0x10, 7), put16(pb() + 0x14, 9), put16(pb() + 0x2C, 123), put16(pb() + 0x12, 4);
    at_desk(100, 7);
    for (int i = 0; i < 20; i++) call(0x49C65), call(0x4A6C8), call(0x552E8);
    CHECK(pb()[0x0A] == 0x31 && pb()[0x0D] == 0x34);
    CHECK(u16(pb() + 0x10) == 7 && u16(pb() + 0x14) == 9 && u16(pb() + 0x2C) == 123 && u16(pb() + 0x12) == 4);
    /* the neurology needs the WORD at +0x14: byte +0x14 clear and +0x15 set still runs it (and not the others) */
    fresh();
    counts()[15] = 1, counts()[3] = 1;
    rec(3, 0)[10] = 10;
    hire(30, 0, NURSE, 15, 0, rec(15, 0) + 8, 1);
    hire(31, 1, DOCTOR, 15, 0, rec(15, 0) + 10, 1);
    seated(100, 11, 0);
    h0()[0x14] = 0, h0()[0x15] = 1;
    call(0x54EF2);
    CHECK(u16(rec(15, 0) + 12) == 100 && P(100)[4] == 15);
    CHECK(u16(pb() + 0x12) == 15 && pb()[0x44] == 0xFF && pb()[0x5E] == 0xFF);
}

/* ---- reception ------------------------------------------------------------------------------------------------ */

static void test_reception_moves(void) {
    clinic(1);
    int moves = 0, shows = 0, calls = 400;
    for (int i = 0; i < calls; i++) {
        R()[0x74] = R()[0x75] = 100; /* independent admission trial: supplies stay usable (SIM-80) */
        memset(rec(3, 0) + 0x14, 0, 40), rec(3, 0)[12] = 0;
        R()[0x45] = 0;
        at_desk(100 + i, 7);
        pb()[0x44] = 0x55;
        call(0x49C65);
        if (P(100 + i)[4] == 3) {
            moves++;
            CHECK(pb()[0x44] == 0xFF && pb()[0x5B] == 0xFF);
        } else {
            CHECK(pb()[0x44] == 0x55 && P(100 + i)[4] == 2);
        }
        if (R()[0x45]) {
            shows++;
            CHECK(R()[0x45] >= 22 && R()[0x45] <= 37);
        }
    }
    CHECK(moves >= 270 && moves <= 330); /* 3 in 4 */
    CHECK(shows >= 30 && shows <= 75); /* 1 in 8 */
    CHECK(u16(ST(20) + 0x10) == moves); /* the clerk's work counter */
    CHECK(s->player_image == 0); /* screen 97 */
    /* image 519 only on screen 19; a running show is not restarted */
    s->screen = 19;
    int drawn = 0;
    for (int i = 0; i < 400; i++) {
        R()[0x45] = 0, s->player_image = 0;
        call(0x49C65);
        drawn += s->player_image == 519;
        CHECK((R()[0x45] != 0) == (s->player_image == 519));
    }
    CHECK(drawn >= 30 && drawn <= 75);
    R()[0x45] = 5;
    for (int i = 0; i < 400; i++) call(0x49C65);
    CHECK(R()[0x45] == 5);
    /* a dependent clerk: nothing at all */
    clinic(0);
    at_desk(100, 7);
    for (int i = 0; i < 400; i++) call(0x49C65);
    CHECK(P(100)[4] == 2 && R()[0x45] == 0 && pb()[0x5B] == 0);
    /* a dead patient at the desk stays */
    clinic(1);
    at_desk(100, 7);
    P(100)[104] = 1;
    for (int i = 0; i < 100; i++) call(0x49C65);
    CHECK(u16(R() + 10) == 100 && pb()[0x5B] == 0);
}

static void test_reception_refusals(void) {
    /* no form: the request byte 11, the patient stays; no pen: 3 */
    clinic(1);
    put16(R() + 0x68, 0);
    at_desk(100, 7);
    for (int i = 0; i < 40; i++) call(0x49C65);
    CHECK(R()[7] == 11 && P(100)[4] == 2 && pb()[0x44] == 0 && pb()[0x5B] == 0xFF);
    clinic(1);
    put16(R() + 0x6A, 0);
    at_desk(100, 7);
    for (int i = 0; i < 40; i++) call(0x49C65);
    CHECK(R()[7] == 3 && P(100)[4] == 2);
    /* a full waiting room: nothing, no request */
    clinic(1);
    rec(3, 0)[10] = 0;
    at_desk(100, 7);
    pb()[0x44] = 0x55;
    for (int i = 0; i < 40; i++) call(0x49C65);
    CHECK(pb()[0x44] == 0);
    CHECK(R()[7] == 0 && P(100)[4] == 2 && s->player_box == 0 && P(100)[20] == 80);
    /* no waiting room at all: thrown out at the first desk call, with the player's box */
    clinic(1);
    counts()[3] = 0;
    at_desk(100, 7);
    at_desk(101, 7);
    put16(R() + 10, 100), put16(R() + 14, 101);
    s->screen = 38;
    for (int i = 0; i < 40 && u16(R() + 10) == 100; i++) call(0x49C65);
    CHECK(P(100)[3] == 0xFF && P(100)[4] == 0xFF && P(100)[20] == 40);
    CHECK(s->player_box == 100 && u16(R() + 10) == 101 && pb()[0x44] == 0);
}

static void test_reception_transfer(void) {
    /* desk empty, places 2 and 4 of the bed queue taken: the patient of place 4 goes into the ward */
    for (int run = 0; run < 3; run++) {
        clinic(1);
        ill(200, 0, 9), ill(201, 0, 9);
        put16(R() + 0x46 + 8, 200), put16(R() + 0x46 + 16, 201);
        P(200)[3] = P(201)[3] = 0, P(200)[4] = P(201)[4] = 2;
        call(0x49C65);
        CHECK(P(201)[4] == 6 && u16(R() + 0x46 + 16) == 0 && u16(R() + 0x46 + 8) == 200);
        CHECK(pb()[0x62] == 0xFF && rec(6, 0)[12] == 1);
    }
    /* two wards: each chosen about half the time; ward 0 full: about half the calls move nobody, pb+0x62 always */
    int chosen[2] = {0, 0};
    for (int i = 0; i < 400; i++) {
        clinic(1);
        counts()[6] = 2;
        rec(6, 1)[10] = 4;
        sim_init(s, &g_world, &g_prog, (uint64_t)i + 100);
        s->screen = 97;
        ill(200, 0, 9);
        put16(R() + 0x46 + 20, 200), P(200)[4] = 2;
        call(0x49C65);
        if (P(200)[4] == 6) chosen[P(200)[66]]++;
    }
    CHECK(chosen[0] + chosen[1] == 400 && chosen[0] >= 160 && chosen[1] >= 160);
    int moved = 0, flagged = 0;
    for (int i = 0; i < 200; i++) {
        clinic(1);
        counts()[6] = 2;
        rec(6, 1)[10] = 4;
        rec(6, 0)[12] = 4; /* full */
        sim_init(s, &g_world, &g_prog, (uint64_t)i + 900);
        s->screen = 97;
        ill(200, 0, 9);
        put16(R() + 0x46, 200), P(200)[4] = 2;
        call(0x49C65);
        moved += P(200)[4] == 6;
        flagged += pb()[0x62] == 0xFF;
    }
    CHECK(moved >= 70 && moved <= 130 && flagged == 200);
    /* no free bed anywhere, or an empty queue: no flag */
    clinic(1);
    rec(6, 0)[12] = 4;
    put16(R() + 0x46, 200), P(200)[4] = 2;
    for (int i = 0; i < 40; i++) call(0x49C65);
    CHECK(pb()[0x62] == 0 && u16(R() + 0x46) == 200);
    clinic(1);
    for (int i = 0; i < 40; i++) call(0x49C65);
    CHECK(pb()[0x62] == 0);
    /* a patient at the desk: the transfer only on r3 = 0, about 1 call in 4 */
    int transfers = 0;
    for (int i = 0; i < 400; i++) {
        clinic(1);
        sim_init(s, &g_world, &g_prog, (uint64_t)i + 3000);
        s->screen = 97;
        rec(3, 0)[10] = 0; /* the desk patient finds no seat */
        at_desk(100, 7);
        ill(200, 0, 9);
        put16(R() + 0x46, 200), P(200)[4] = 2;
        call(0x49C65);
        transfers += pb()[0x62] == 0xFF;
    }
    CHECK(transfers >= 70 && transfers <= 130);
    /* SIM-47: 0x2336B runs in AI mode here: when its three rounds (600, 600, 1,200 random(10)) find no bed the place
     * stays as it was (the player's own transfer, rooms.c, takes the engine's first-free-bed guard instead). Every
     * step 0 (bed 0, taken), every bed priority entry 3: nobody moves; the place's second word (57) stays too. */
    clinic(1);
    memset(g_image + 0x4DCC, 3, 10);
    ill(99, 0, 9), ill(200, 0, 9);
    put16(rec(6, 0) + 0x14, 99), rec(6, 0)[12] = 1, P(99)[4] = 6;
    put16(R() + 0x46, 200), put16(R() + 0x48, 57), P(200)[4] = 2;
    static uint32_t zeros[5000];
    SimScript sc = {zeros, 5000, 0, 0, NULL};
    s->rng_script = &sc;
    call(0x49C65);
    s->rng_script = NULL;
    CHECK(sc.used >= 2400 && sc.extra == 0);
    CHECK(P(200)[4] == 2 && u16(R() + 0x46) == 200 && u16(R() + 0x48) == 57 && rec(6, 0)[12] == 1);
}

/* ---- waiting room and the evening release ---------------------------------------------------------------------- */

/* the draws in the original's order, predicted by a twin of the stream: r1, random(rooms), r7 (== 2 at the reception,
 * == 0 in the rooms), r15 */
static void test_draw_order(void) {
    clinic(1);
    counts()[3] = 3;
    rec(3, 1)[10] = rec(3, 2)[10] = 10;
    hire(25, 5, NURSE, 3, 1, rec(3, 1) + 8, 1);
    hire(26, 6, NURSE, 3, 2, rec(3, 2) + 8, 1);
    for (int i = 0; i < 300; i++) {
        R()[0x45] = 0;
        Sim twin = *s;
        sim_bits(&twin, 1);
        int want = sim_bits(&twin, 7) == 2 ? (int)sim_bits(&twin, 15) + 22 : 0;
        call(0x49C65);
        CHECK(R()[0x45] == want && twin.rng == s->rng && twin.rng_s == s->rng_s);
        for (int w = 0; w < 3; w++) rec(3, w)[15] = 0;
        twin = *s;
        sim_bits(&twin, 1);
        int room = (int)sim_random(&twin, 3);
        want = sim_bits(&twin, 7) == 0 ? (int)sim_bits(&twin, 15) + 22 : 0;
        call(0x4A6C8);
        CHECK(rec(3, room)[15] == want && twin.rng == s->rng && twin.rng_s == s->rng_s);
    }
}

static void test_waiting_show(void) {
    clinic(1);
    int shows = 0;
    for (int i = 0; i < 400; i++) {
        rec(3, 0)[15] = 0, pb()[0x44] = 0;
        call(0x4A6C8);
        if (rec(3, 0)[15]) {
            shows++;
            CHECK(pb()[0x44] == 0xFF && rec(3, 0)[15] >= 22 && rec(3, 0)[15] <= 37);
        } else {
            CHECK(pb()[0x44] == 0);
        }
    }
    CHECK(shows >= 30 && shows <= 75);
    rec(3, 0)[15] = 9;
    for (int i = 0; i < 100; i++) call(0x4A6C8);
    CHECK(rec(3, 0)[15] == 9);
    clinic(0);
    for (int i = 0; i < 400; i++) call(0x4A6C8);
    CHECK(rec(3, 0)[15] == 0 && pb()[0x44] == 0);
}

/* the waiting-room nurse released under the gates; 1 = released */
static int waiting_release(int hour, int minute, int screen, int clerk, int seated_patients) {
    clinic(1);
    if (!clerk) put16(R() + 8, 0);
    for (int i = 0; i < seated_patients; i++) seated(300 + i, 7, i);
    at(hour, minute);
    s->screen = screen;
    rec(3, 0)[15] = 30;
    call(0x4A6C8);
    return u16(rec(3, 0) + 8) == 0;
}

static void test_evening_release(void) {
    CHECK(!waiting_release(18, 44, 97, 0, 0));
    CHECK(waiting_release(18, 45, 97, 0, 0));
    CHECK(waiting_release(19, 0, 97, 0, 0));
    CHECK(waiting_release(19, 0, 1, 0, 0));
    CHECK(waiting_release(23, 59, 1, 0, 0));
    CHECK(!waiting_release(0, 0, 97, 0, 0)); /* after midnight: not until the next evening */
    CHECK(!waiting_release(5, 50, 97, 0, 0));
    CHECK(!waiting_release(17, 59, 97, 0, 0));
    CHECK(!waiting_release(19, 0, 29, 0, 0));
    CHECK(!waiting_release(19, 0, 54, 0, 0));
    CHECK(!waiting_release(19, 0, 97, 1, 0)); /* a staffed reception */
    CHECK(!waiting_release(19, 0, 97, 0, 1)); /* a patient on a seat */
    /* the bookkeeping of 0x551AB */
    CHECK(waiting_release(20, 0, 97, 0, 0));
    uint8_t *sl = staff_slots(s, 0) + 1 * STAFF_SLOT_SIZE;
    CHECK(rec(3, 0)[15] == 0 && sl[3] == 0x13 && sl[16] == 3 && sl[17] == 0 && sl[4] == 0);
    CHECK(sl[7] == 0 && u16(sl + 10) == 0 && ST(21)[4] == 0x13 && ST(21)[26] == 0);
    CHECK(u16(sl) == 21 && sl[2] == NURSE);
    /* the release does not reach a dependent nurse */
    clinic(0);
    put16(R() + 8, 0);
    at(20, 0);
    call(0x4A6C8);
    CHECK(u16(rec(3, 0) + 8) == 21);
    /* the room index is remembered: a nurse of waiting room 2 */
    clinic(1);
    put16(R() + 8, 0);
    counts()[3] = 3;
    rec(3, 1)[10] = rec(3, 2)[10] = 10;
    hire(25, 5, NURSE, 3, 2, rec(3, 2) + 8, 1);
    put16(rec(3, 0) + 8, 0);
    at(20, 0);
    for (int i = 0; i < 200 && u16(rec(3, 2) + 8); i++) call(0x4A6C8);
    sl = staff_slots(s, 0) + 5 * STAFF_SLOT_SIZE;
    CHECK(u16(rec(3, 2) + 8) == 0 && sl[16] == 3 && sl[17] == 2);
    CHECK(sl[4] == 0 && ST(25)[26] == 0 && ST(25)[4] == 0x13);
    /* and the off-duty return of 0x2FC77 brings her back into the same field */
    at(8, 0);
    ST(25)[21] = 100;
    int back = 0;
    for (int i = 0; i < 3000 && !back; i++) {
        sl[11] = 9;
        ST(25)[21] = 100;
        call(0x2FC77);
        back = u16(rec(3, 2) + 8) == 25;
    }
    CHECK(back && ST(25)[4] == 3 && ST(25)[26] == 2);
}

/* ---- treatment room, dental ward, neurology ------------------------------------------------------------------ */

static void test_treatment_fetch(void) {
    /* independent nurse, dependent doctor: the waiting type-0 patient comes in */
    clinic(1);
    ST(22)[11] = 0;
    seated(100, 7, 3);
    call(0x552E8);
    CHECK(u16(rec(5, 0) + 12) == 100 && P(100)[4] == 5 && u16(rec(5, 0) + 14) == 999);
    CHECK(pb()[0x44] == 0xFF && pb()[0x5C] == 0xFF && rec(3, 0)[12] == 0);
    /* a dependent nurse; a type-1 patient (AI pick: no fallback); no doctor: nothing */
    clinic(1);
    ST(23)[11] = 0;
    seated(100, 7, 3);
    for (int i = 0; i < 50; i++) call(0x552E8);
    CHECK(u16(rec(5, 0) + 12) == 0 && pb()[0x5C] == 0);
    clinic(1);
    seated(100, 8, 3);
    for (int i = 0; i < 50; i++) call(0x552E8);
    CHECK(u16(rec(5, 0) + 12) == 0 && pb()[0x44] == 0);
    CHECK(rec(5, 0)[0x19] == 0 && pb()[0x5D] == 0); /* the show of +0x19 is the dental ward's only */
    /* a wish patient whose illness is dental counts as waiting for the treatment room, but the AI pick (type 0
     * only, no fallback) does not take it */
    clinic(1);
    seated(100, 8, 3);
    P(100)[57] = 1;
    for (int i = 0; i < 50; i++) call(0x552E8);
    CHECK(u16(rec(5, 0) + 12) == 0 && pb()[0x44] == 0xFF && P(100)[4] == 3);
    clinic(1);
    put16(rec(5, 0) + 10, 0);
    seated(100, 7, 3);
    for (int i = 0; i < 50; i++) call(0x552E8);
    CHECK(u16(rec(5, 0) + 12) == 0);
}

/* a patient of illness `id` in treatment room 0, diagnosis done (+0x0E = 0) */
static void diagnosed(int index, int id) {
    ill(index, 0, id);
    P(index)[3] = 0, P(index)[4] = 5, P(index)[42] = (uint8_t)id;
    put16(rec(5, 0) + 12, index);
    put16(rec(5, 0) + 14, 0);
}

static void test_treatment_decision(void) {
    /* in the room */
    clinic(1);
    diagnosed(100, 7);
    call(0x552E8);
    CHECK(u16(rec(5, 0) + 16) == 2 && u16(rec(5, 0) + 18) == 999 && P(100)[43] == 2);
    CHECK(pb()[0x5C] == 0xFF && pb()[0x32] == 0xFF && pb()[0x5F] == 0);
    /* a finished treatment stays for the player's bill */
    put16(rec(5, 0) + 18, 0);
    pb()[0x32] = 0;
    for (int i = 0; i < 50; i++) call(0x552E8);
    CHECK(u16(rec(5, 0) + 12) == 100 && pb()[0x32] == 0 && h0()[0x26] == 0);
    /* the gates: a dependent doctor (the nurse does not decide), the diagnosis not started or running */
    clinic(1);
    ST(22)[11] = 0;
    diagnosed(100, 7);
    for (int i = 0; i < 50; i++) call(0x552E8);
    CHECK(u16(rec(5, 0) + 16) == 0 && pb()[0x32] == 0);
    ST(22)[11] = 1, ST(23)[11] = 0;
    call(0x552E8);
    CHECK(u16(rec(5, 0) + 16) == 2);
    clinic(1);
    diagnosed(100, 7);
    put16(rec(5, 0) + 14, 999);
    for (int i = 0; i < 50; i++) call(0x552E8);
    CHECK(u16(rec(5, 0) + 16) == 0);
    put16(rec(5, 0) + 14, 1);
    for (int i = 0; i < 50; i++) call(0x552E8);
    CHECK(u16(rec(5, 0) + 16) == 0);
    /* dead: nothing */
    clinic(1);
    diagnosed(100, 7);
    P(100)[104] = 1;
    call(0x552E8);
    CHECK(u16(rec(5, 0) + 16) == 0 && u16(rec(5, 0) + 12) == 100);
    /* a home treatment: billed, without the copy-protection counter */
    clinic(1);
    diagnosed(100, 10);
    int32_t cash = world_cash(&g_world, 0);
    call(0x552E8);
    CHECK(u16(rec(5, 0) + 12) == 0 && P(100)[4] == 0xFF && get16(h0() + 0x26) == 1);
    CHECK(world_cash(&g_world, 0) > cash && get32(h0() + 0x16E) == 0 && get16(h0() + 0x172) == 0);
    /* a bed treatment: into the reception's bed queue; with no Krankenzimmer home untreated */
    clinic(1);
    diagnosed(100, 9);
    call(0x552E8);
    CHECK(u16(R() + 0x46) == 100 && P(100)[4] == 2 && u16(rec(5, 0) + 12) == 0);
    clinic(1);
    counts()[6] = 0;
    diagnosed(100, 9);
    call(0x552E8);
    CHECK(u16(R() + 0x46) == 0 && P(100)[4] == 0xFF && get16(h0() + 0x26) == 0);
    /* the player viewing the patient on screen 61: pb+0x5F */
    clinic(1);
    diagnosed(100, 9);
    s->screen = 61, put16(pb() + 0x2C, 100);
    call(0x552E8);
    CHECK(pb()[0x5F] == 0xFF);
    clinic(1);
    diagnosed(100, 9);
    s->screen = 61, put16(pb() + 0x2C, 101);
    call(0x552E8);
    CHECK(pb()[0x5F] == 0);
    clinic(1);
    diagnosed(100, 9);
    s->screen = 23, put16(pb() + 0x2C, 100); /* the treatment room itself, not the sheet */
    call(0x552E8);
    CHECK(pb()[0x5F] == 0 && u16(R() + 0x46) == 100);
    /* the choice in AI mode: the rank helper by the doctor's skill (0 here: the best treatment 3 times in 4), not the
     * player's even r3 */
    int best = 0;
    for (int i = 0; i < 400; i++) {
        clinic(1);
        ILL(12)[11] = 2, ILL(12)[12] = 4, ILL(12)[13] = 5, ILL(12)[14] = 5;
        sim_init(s, &g_world, &g_prog, (uint64_t)i + 77);
        s->screen = 97;
        diagnosed(100, 12);
        sim_rating_record(s, rec(5, 0)); /* establish the rating-0 context this skill-0 rank test requires (SIM-82) */
        call(0x552E8);
        best += P(100)[43] == 2;
    }
    CHECK(best >= 280 && best <= 345);
    clinic(1);
    diagnosed(100, 7); /* treated in the room: not moved */
    s->screen = 61, put16(pb() + 0x2C, 100);
    call(0x552E8);
    CHECK(pb()[0x5F] == 0);
}

static void test_treatment_release(void) {
    clinic(1);
    put16(R() + 8, 0);
    at(19, 0);
    call(0x552E8);
    CHECK(u16(rec(5, 0) + 8) == 0 && u16(rec(5, 0) + 10) == 0);
    uint8_t *sl = staff_slots(s, 0) + 3 * STAFF_SLOT_SIZE;
    CHECK(sl[3] == 0x13 && sl[16] == 5 && ST(22)[4] == 0x13);
    /* a dependent nurse stays, the independent doctor goes */
    clinic(1);
    put16(R() + 8, 0), ST(23)[11] = 0;
    at(19, 0);
    call(0x552E8);
    CHECK(u16(rec(5, 0) + 8) == 23 && u16(rec(5, 0) + 10) == 0);
    /* a type-0 patient waiting: nobody; only a dental patient waiting: released */
    clinic(1);
    put16(R() + 8, 0);
    seated(100, 7, 0);
    ST(23)[11] = 0; /* no fetch either */
    at(19, 0);
    call(0x552E8);
    CHECK(u16(rec(5, 0) + 10) == 22);
    clinic(1);
    put16(R() + 8, 0);
    seated(100, 8, 0);
    at(19, 0);
    call(0x552E8);
    CHECK(u16(rec(5, 0) + 10) == 0);
    /* a patient in the room: nobody */
    clinic(1);
    put16(R() + 8, 0);
    diagnosed(100, 7);
    put16(rec(5, 0) + 14, 5);
    at(19, 0);
    call(0x552E8);
    CHECK(u16(rec(5, 0) + 8) == 23 && u16(rec(5, 0) + 10) == 22);
}

static void dental(int indep) {
    clinic(indep);
    counts()[11] = 1;
    hire(40, 6, NURSE, 11, 0, rec(11, 0) + 8, indep);
    hire(41, 7, DOCTOR, 11, 0, rec(11, 0) + 10, indep);
}

static void test_dental(void) {
    /* the show: no test of +0x19, so a running counter is rewritten 1 in 8 */
    dental(1);
    int rewritten = 0;
    for (int i = 0; i < 400; i++) {
        rec(11, 0)[0x19] = 5, pb()[0x5D] = 0;
        call(0x4A437);
        if (rec(11, 0)[0x19] != 5) {
            rewritten++;
            CHECK(pb()[0x5D] == 0xFF && rec(11, 0)[0x19] >= 22);
        }
    }
    CHECK(rewritten >= 30 && rewritten <= 75);
    dental(0);
    for (int i = 0; i < 400; i++) call(0x4A437);
    CHECK(rec(11, 0)[0x19] == 0);
    /* the fetch takes the dental patient, not a type-0 one */
    dental(1);
    seated(100, 7, 0), seated(101, 8, 1);
    call(0x4A437);
    CHECK(u16(rec(11, 0) + 12) == 101 && P(101)[4] == 11 && u16(rec(11, 0) + 14) == 999);
    CHECK(pb()[0x44] == 0xFF && pb()[0x5D] == 0xFF);
    dental(1);
    ST(40)[11] = 0;
    seated(101, 8, 1);
    for (int i = 0; i < 50; i++) call(0x4A437);
    CHECK(u16(rec(11, 0) + 12) == 0);
    /* the choice by an independent dentist */
    dental(1);
    ill(101, 0, 8);
    P(101)[3] = 0, P(101)[4] = 11, P(101)[42] = 8;
    put16(rec(11, 0) + 12, 101);
    pb()[0x3A] = 0;
    call(0x4A437);
    CHECK(u16(rec(11, 0) + 16) == 4 && u16(rec(11, 0) + 18) == 999 && pb()[0x3A] == 0xFF);
    dental(1);
    ST(41)[11] = 0;
    ill(101, 0, 8);
    P(101)[4] = 11, P(101)[42] = 8;
    put16(rec(11, 0) + 12, 101);
    for (int i = 0; i < 50; i++) call(0x4A437);
    CHECK(u16(rec(11, 0) + 16) == 0 && pb()[0x3A] == 0);
    /* a physical case diagnosed in the dental ward goes back to a waiting room; no "moved" flag here */
    dental(1);
    ill(101, 0, 7);
    P(101)[3] = 0, P(101)[4] = 11, P(101)[42] = 7;
    put16(rec(11, 0) + 12, 101);
    s->screen = 75, put16(pb() + 0x2C, 101);
    call(0x4A437);
    CHECK(u16(rec(11, 0) + 12) == 0 && P(101)[4] == 3 && pb()[0x61] == 0 && pb()[0x5F] == 0 && pb()[0x3A] == 0xFF);
    /* the release at 18:45 on screen 97 with the comfort counter, not on screen 21 */
    dental(1);
    put16(R() + 8, 0);
    at(18, 45);
    s->screen = 21;
    rec(11, 0)[0x19] = 30;
    call(0x4A437);
    CHECK(u16(rec(11, 0) + 8) == 40);
    s->screen = 97;
    for (int i = 0; i < 20 && u16(rec(11, 0) + 8); i++) call(0x4A437);
    CHECK(u16(rec(11, 0) + 8) == 0 && u16(rec(11, 0) + 10) == 0 && rec(11, 0)[0x19] == 0);
    CHECK(staff_slots(s, 0)[6 * STAFF_SLOT_SIZE + 16] == 11);
}

static void neurology(int indep) {
    clinic(indep);
    counts()[15] = 1;
    hire(30, 6, NURSE, 15, 0, rec(15, 0) + 8, indep);
    hire(31, 7, DOCTOR, 15, 0, rec(15, 0) + 10, indep);
}

static void test_neurology(void) {
    neurology(1);
    seated(100, 7, 0), seated(101, 11, 1);
    call(0x54EF2);
    CHECK(u16(rec(15, 0) + 12) == 101 && P(101)[4] == 15 && u16(pb() + 0x12) == 15);
    CHECK(pb()[0x44] == 0xFF && pb()[0x5E] == 0xFF && rec(15, 0)[0x19] == 0);
    neurology(1);
    ST(30)[11] = 0;
    seated(101, 11, 1);
    for (int i = 0; i < 50; i++) call(0x54EF2);
    CHECK(u16(rec(15, 0) + 12) == 0 && u16(pb() + 0x12) == 0);
    /* the choice: the dental flag and pb+0x41, not pb+0x5E; a neurology case in the room */
    neurology(1);
    ill(101, 0, 11);
    TREAT(5)[4] = 0;
    P(101)[3] = 0, P(101)[4] = 15, P(101)[42] = 11;
    put16(rec(15, 0) + 12, 101);
    call(0x54EF2);
    CHECK(pb()[0x5D] == 0xFF && pb()[0x41] == 0xFF && pb()[0x5E] == 0);
    CHECK(u16(rec(15, 0) + 16) == 5 && u16(rec(15, 0) + 18) == 999);
    /* a physical case diagnosed there goes back to a waiting room; the player viewing it on 75: pb+0x61 */
    neurology(1);
    ill(101, 0, 7);
    P(101)[3] = 0, P(101)[4] = 15, P(101)[42] = 7;
    put16(rec(15, 0) + 12, 101);
    s->screen = 75, put16(pb() + 0x2C, 101);
    call(0x54EF2);
    CHECK(u16(rec(15, 0) + 12) == 0 && P(101)[4] == 3 && pb()[0x61] == 0xFF && pb()[0x5F] == 0);
    neurology(1);
    ST(31)[11] = 0;
    ill(101, 0, 11);
    P(101)[4] = 15, P(101)[42] = 11;
    put16(rec(15, 0) + 12, 101);
    for (int i = 0; i < 50; i++) call(0x54EF2);
    CHECK(pb()[0x41] == 0 && u16(rec(15, 0) + 16) == 0);
    /* the release at 18:45, not with a psychological patient waiting */
    neurology(1);
    put16(R() + 8, 0);
    at(18, 45);
    ST(30)[11] = 0;
    seated(101, 11, 1);
    call(0x54EF2);
    CHECK(u16(rec(15, 0) + 10) == 31);
    neurology(1);
    put16(R() + 8, 0);
    at(18, 45);
    call(0x54EF2);
    CHECK(u16(rec(15, 0) + 8) == 0 && u16(rec(15, 0) + 10) == 0);
}

/* ---- wards and intensive care ------------------------------------------------------------------------------- */

static void ward_test(int cat, uint32_t routine) {
    int state = cat;
    for (int indep = 0; indep < 2; indep++) {
        fresh();
        counts()[cat] = 1;
        uint8_t *k = rec(cat, 0);
        k[10] = 10;
        hire(24, 0, NURSE, cat, 0, k + 8, indep);
        int ids[4] = {110, 111, 112, 113};
        for (int b = 0; b < 4; b++) {
            ill(ids[b], 0, 9);
            P(ids[b])[3] = 0, P(ids[b])[4] = (uint8_t)state, P(ids[b])[43] = 6;
            put16(k + 0x14 + 4 * (b + 2), ids[b]);
        }
        P(110)[93] = 0xFF; /* a step running */
        put16(P(111) + 56, 0); /* cured */
        call(routine);
        if (!indep) {
            call(routine);
            CHECK(!P(112)[93] && !P(113)[93]);
            continue;
        }
        CHECK(P(112)[93] == 0xFF && P(113)[93] == 0 && P(111)[93] == 0);
        call(routine);
        CHECK(P(113)[93] == 0xFF && P(111)[93] == 0);
        /* a dead patient or one in another state is skipped */
        P(112)[93] = P(113)[93] = 0;
        P(112)[104] = 1, P(113)[4] = 7;
        call(routine);
        CHECK(!P(112)[93] && !P(113)[93]);
        /* the show */
        int shows = 0;
        for (int i = 0; i < 400; i++) {
            k[15] = 0;
            call(routine);
            shows += k[15] != 0;
        }
        CHECK(shows >= 30 && shows <= 75);
        k[15] = 3;
        for (int i = 0; i < 200; i++) call(routine);
        CHECK(k[15] == 3);
        /* no discharge and no release at night */
        at(23, 0);
        put16(k + 0x14 + 8, 0), put16(k + 0x14 + 12, 0), put16(k + 0x14 + 16, 0), put16(k + 0x14 + 20, 0);
        for (int i = 0; i < 50; i++) call(routine);
        CHECK(u16(k + 8) == 24);
    }
}

static void test_wards(void) {
    ward_test(6, 0x55580);
    ward_test(16, 0x55692);
}

static void test_intensive_care(void) {
    for (int indep = 0; indep < 2; indep++) {
        fresh();
        counts()[9] = 1;
        uint8_t *c = rec(9, 0);
        hire(24, 0, NURSE, 9, 0, c + 8, indep);
        ill(110, 0, 9);
        P(110)[3] = 0, P(110)[4] = 9, P(110)[44] = 80;
        put16(c + 0x0C, 110);
        uint8_t before[186], person_before[106];
        int shows = 0;
        for (int i = 0; i < 400; i++) {
            c[15] = 0;
            memcpy(before, c, 186), memcpy(person_before, P(110), 106);
            call(0x557A4);
            shows += c[15] != 0;
            CHECK(memcmp(before, c, 15) == 0 && memcmp(before + 16, c + 16, 170) == 0);
            CHECK(memcmp(person_before, P(110), 106) == 0);
        }
        CHECK(indep ? shows >= 30 && shows <= 75 : shows == 0);
        c[15] = 3;
        for (int i = 0; i < 200; i++) call(0x557A4);
        CHECK(c[15] == 3);
        CHECK(u16(R() + 0x46) == 0);
    }
}

/* ---- OP and torture chamber ----------------------------------------------------------------------------------- */

/* an OP (or torture chamber) with independent staff, a Krankenzimmer (neuro ward) with a patient whose step is due */
static void theatre(int cat, int indep) {
    fresh();
    int ward_cat = cat == 7 ? 6 : 16;
    counts()[cat] = 1, counts()[ward_cat] = 1;
    uint8_t *t = rec(cat, 0), *k = rec(ward_cat, 0);
    k[10] = 4;
    hire(50, 0, NURSE, cat, 0, t + 8, indep);
    hire(51, 1, cat == 7 ? 5 : 10, cat, 0, t + 10, indep);
    if (cat == 7) hire(52, 2, DOCTOR, 7, 0, t + 6, indep);
    TREAT(20)[1] = 2, TREAT(20)[15] = cat == 7 ? 11 : 71; /* an operation, its step due at the start */
    ill(120, 0, 9);
    P(120)[3] = 0, P(120)[4] = (uint8_t)ward_cat, P(120)[43] = 20;
    put16(k + 0x14, 120);
    k[12] = 1;
}

static void test_op(void) {
    theatre(7, 1);
    call(0x55B17);
    uint8_t *t = rec(7, 0);
    CHECK(u16(t + 0x0C) == 120 && P(120)[4] == 7 && u16(t + 0x0E) == 999 && pb()[0x65] == 0xFF);
    /* the start by the independent anaesthetist (the empty capability list is met) */
    call(0x55B17);
    CHECK(u16(t + 0x0E) == 6 && P(120)[100] == 1 && get16(h0() + 0x32) == 1);
    put16(t + 0x0E, 4);
    for (int i = 0; i < 20; i++) call(0x55B17);
    CHECK(u16(t + 0x0E) == 4 && get16(h0() + 0x32) == 1); /* a running operation is not started again */
    put16(t + 0x0E, 6);
    /* the AI branch of the start: a missing organ is wished for in +0x86 */
    theatre(7, 1), t = rec(7, 0);
    TREAT(20)[3] = 9;
    call(0x55B17);
    call(0x55B17);
    CHECK(u16(t + 0x0C) == 120 && u16(t + 0x0E) == 999 && get16(h0() + 0x86) == 9 && get16(h0() + 0x32) == 0);
    TREAT(20)[3] = 0;
    theatre(7, 1), t = rec(7, 0);
    call(0x55B17);
    call(0x55B17);
    /* a finished operation stays on the table */
    put16(t + 0x0E, 999), P(120)[100] = 2;
    for (int i = 0; i < 50; i++) call(0x55B17);
    CHECK(u16(t + 0x0E) == 999 && get16(h0() + 0x32) == 1 && u16(t + 0x0C) == 120);
    /* the gates of the start: a dependent anaesthetist, no surgeon, a dead patient */
    theatre(7, 1), t = rec(7, 0);
    call(0x55B17);
    ST(51)[11] = 0;
    for (int i = 0; i < 50; i++) call(0x55B17);
    CHECK(u16(t + 0x0E) == 999);
    theatre(7, 1), t = rec(7, 0);
    call(0x55B17);
    put16(t + 6, 0);
    for (int i = 0; i < 50; i++) call(0x55B17);
    CHECK(u16(t + 0x0E) == 999);
    theatre(7, 1), t = rec(7, 0);
    call(0x55B17);
    P(120)[104] = 1;
    for (int i = 0; i < 50; i++) call(0x55B17);
    CHECK(u16(t + 0x0E) == 999);
    /* a dependent nurse fetches nobody (and the independent anaesthetist does not either) */
    theatre(7, 1), t = rec(7, 0);
    ST(50)[11] = 0;
    for (int i = 0; i < 50; i++) call(0x55B17);
    CHECK(u16(t + 0x0C) == 0 && pb()[0x65] == 0);
    /* screen 150: nothing at all, not even the show, and only r1 drawn */
    theatre(7, 1), t = rec(7, 0);
    s->screen = 150;
    for (int i = 0; i < 400; i++) {
        Sim twin = *s;
        sim_bits(&twin, 1);
        call(0x55B17);
        CHECK(twin.rng == s->rng && twin.rng_s == s->rng_s);
    }
    CHECK(u16(t + 0x0C) == 0 && t[0x17] == 0);
    /* the show 1 in 8 with an independent nurse (and nothing else in the room) */
    theatre(7, 1), t = rec(7, 0);
    put16(t + 6, 0);
    int shows = 0;
    for (int i = 0; i < 400; i++) t[0x17] = 0, call(0x55B17), shows += t[0x17] != 0;
    CHECK(shows >= 30 && shows <= 75 && u16(t + 0x0C) == 0);
    t[0x17] = 3;
    for (int i = 0; i < 200; i++) call(0x55B17);
    CHECK(t[0x17] == 3);
    theatre(7, 0), t = rec(7, 0);
    put16(t + 6, 0);
    for (int i = 0; i < 400; i++) call(0x55B17);
    CHECK(t[0x17] == 0);
    /* no nurse: nothing at all, neither the start by the anaesthetist nor the evening release */
    theatre(7, 1), t = rec(7, 0);
    call(0x55B17);
    put16(t + 8, 0);
    for (int i = 0; i < 50; i++) call(0x55B17);
    CHECK(u16(t + 0x0E) == 999 && get16(h0() + 0x32) == 0);
    theatre(7, 1), t = rec(7, 0);
    put16(rec(6, 0) + 0x14, 0);
    put16(t + 8, 0);
    at(21, 0);
    call(0x55B17);
    CHECK(u16(t + 6) == 52 && u16(t + 10) == 51);
    /* no Krankenzimmer, but a due patient in another hospital's ward: the engine takes nobody (guarded deviation) */
    theatre(7, 1), t = rec(7, 0);
    counts()[6] = 0;
    put16(rec(6, 0) + 0x14, 0);
    counts()[100 + 6] = 1;
    put16(sim_room_record(s, 1, 6, 0) + 0x14, 120);
    for (int i = 0; i < 50; i++) call(0x55B17);
    CHECK(u16(t + 0x0C) == 0 && P(120)[4] == 6 && pb()[0x65] == 0xFF);
}

static int op_release(int hour, int minute, int race, int cat) {
    theatre(cat, 1);
    put16(rec(cat == 7 ? 6 : 16, 0) + 0x14, 0);
    put16(g_world.block[BLK_34], race);
    at(hour, minute);
    rec(cat, 0)[0x17] = 9;
    call(cat == 7 ? 0x55B17 : 0x55D72);
    uint8_t *t = rec(cat, 0);
    return !u16(t + 8) && !u16(t + 10) && (cat == 8 || (!u16(t + 6) && t[0x17] == 0));
}

static void test_theatre_release(void) {
    CHECK(!op_release(19, 49, 0, 7));
    CHECK(op_release(19, 50, 0, 7));
    CHECK(op_release(20, 0, 0, 7));
    CHECK(op_release(20, 0, 0, 8));
    CHECK(!op_release(19, 50, 1, 7)); /* the race */
    CHECK(op_release(23, 0, 0, 7));
    CHECK(!op_release(2, 0, 0, 7));
    CHECK(!op_release(19, 49, 0, 8));
    CHECK(op_release(19, 50, 0, 8));
    CHECK(op_release(19, 50, 1, 8)); /* no race test in the torture chamber */
    CHECK(rec(8, 0)[0x17] == 9); /* no comfort counter goes with the torture nurse */
    /* the slots remember the room for the return */
    CHECK(op_release(21, 0, 0, 7));
    const uint8_t *sl = staff_slots(s, 0);
    CHECK(sl[16] == 7 && sl[STAFF_SLOT_SIZE + 16] == 7 && sl[2 * STAFF_SLOT_SIZE + 16] == 7);
    /* with a type-0 patient waiting nobody goes; a dependent anaesthetist stays */
    theatre(7, 1);
    put16(rec(6, 0) + 0x14, 0);
    counts()[3] = 1, rec(3, 0)[10] = 10;
    seated(130, 7, 0);
    at(21, 0);
    call(0x55B17);
    CHECK(u16(rec(7, 0) + 8) == 50);
    theatre(7, 1);
    put16(rec(6, 0) + 0x14, 0);
    ST(51)[11] = 0;
    at(21, 0);
    call(0x55B17);
    CHECK(u16(rec(7, 0) + 8) == 0 && u16(rec(7, 0) + 10) == 51);
    /* the fetch comes first: a due patient keeps the staff */
    theatre(7, 1);
    at(21, 0);
    call(0x55B17);
    CHECK(u16(rec(7, 0) + 0x0C) == 120 && u16(rec(7, 0) + 8) == 50);
}

static void test_torture(void) {
    theatre(8, 1);
    uint8_t *t = rec(8, 0);
    call(0x55D72);
    CHECK(u16(t + 0x0C) == 120 && P(120)[4] == 8 && pb()[0x65] == 0xFF);
    call(0x55D72);
    CHECK(u16(t + 0x0E) == 6 && get16(h0() + 0x114) == 1);
    /* no show; both staff needed; screen 150 does not stop it */
    theatre(8, 1), t = rec(8, 0);
    put16(t + 10, 0);
    for (int i = 0; i < 50; i++) call(0x55D72);
    CHECK(u16(t + 0x0C) == 0 && pb()[0x65] == 0);
    theatre(8, 1), t = rec(8, 0);
    s->screen = 150;
    call(0x55D72);
    call(0x55D72);
    CHECK(u16(t + 0x0E) == 6);
    theatre(8, 1), t = rec(8, 0);
    int shows = 0;
    put16(rec(16, 0) + 0x14, 0);
    for (int i = 0; i < 400; i++) t[0x17] = 0, call(0x55D72), shows += t[0x17] != 0;
    CHECK(shows == 0);
    /* the torture release waits for psychological patients only */
    theatre(8, 1), t = rec(8, 0);
    put16(rec(16, 0) + 0x14, 0);
    counts()[3] = 1, rec(3, 0)[10] = 10;
    seated(130, 7, 0);
    at(21, 0);
    call(0x55D72);
    CHECK(u16(t + 8) == 0 && u16(t + 10) == 0);
    theatre(8, 1), t = rec(8, 0);
    put16(rec(16, 0) + 0x14, 0);
    counts()[3] = 1, rec(3, 0)[10] = 10;
    seated(130, 11, 0);
    at(21, 0);
    call(0x55D72);
    CHECK(u16(t + 8) == 50 && u16(t + 10) == 51);
    /* a dependent torturer does not start */
    theatre(8, 1), t = rec(8, 0);
    ST(51)[11] = 0;
    for (int i = 0; i < 50; i++) call(0x55D72);
    CHECK(u16(t + 0x0C) == 120 && u16(t + 0x0E) == 999);
    /* no neuro ward: nobody fetched */
    theatre(8, 1), t = rec(8, 0);
    counts()[16] = 0;
    for (int i = 0; i < 50; i++) call(0x55D72);
    CHECK(u16(t + 0x0C) == 0);
}

int main(void) {
    test_schedule();
    test_screens();
    test_frame();
    test_reception_moves();
    test_reception_refusals();
    test_reception_transfer();
    test_draw_order();
    test_waiting_show();
    test_evening_release();
    test_treatment_fetch();
    test_treatment_decision();
    test_treatment_release();
    test_dental();
    test_neurology();
    test_wards();
    test_intensive_care();
    test_op();
    test_theatre_release();
    test_torture();
    world_free(&g_world);
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] player rooms (module D)\n");
    return 0;
}
