/* The player's decisions in the core rooms (docs/ui-rooms.md, docs/ui-sitemap.md). Synthetic world and program
 * image (see AGENTS.md): every illness, treatment, equipment item and room description below is made up for the
 * test; the expected values follow the rules of the spec (e.g. the throw-out halves the opinion, 80 -> 40, as in
 * its oracle run). */
#include "fixes.h"
#include "sound.h"
#include "room_sound.h"
#include "purchase_ui.h"
#include <stdio.h>
#include <string.h>

#include "rooms.h"
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
static int32_t cash(void) { return world_cash(&g_world, 0); }
static uint8_t *pb(void) { return rooms_pb(s); }
static uint8_t *P(int i) { return rooms_person(s, i); }
static uint8_t *rec(int cat, int i) { return rooms_record(s, cat, i); }
static uint8_t *R(void) { return rec(2, 0); }
static uint8_t *counts(void) { return g_world.block[BLK_ROOM_COUNTS]; }
static int staff_work(int i) { return get16(rooms_staff(s, i) + 0x10); }
static int news_of(int type) {
    int n = 0;
    for (int i = 0; i < 100; i++) n += sim_news_item(s, i)[0] && sim_news_item(s, i)[20] == type;
    return n;
}

static void describe(int cat, int furniture, int equipment, int cond_a, int cond_b, int type) {
    const int d[7] = {furniture, 4, equipment, 4, cond_a, cond_b, type};
    for (int i = 0; i < 7; i++) put16(g_image + 0x4758 + 14 * cat + 2 * i, d[i]);
}

/* a person at home with an illness of the given id, health 70, mood 60, opinion 80 of the player */
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
    world_set_tail(&g_world, TAIL_HOUR, 10);
    world_hospital(&g_world, 15)[0x15] = 0xFF; /* one AI hospital in business for the picker */
    sim_init(&g_sim, &g_world, &g_prog, 11);
    for (int i = 0; i < 16; i++) g_ailments[i] = (uint8_t)(i & 3 ? 2 : 1); /* ids 0-15, ailment codes */
    sim_set_ailments(&g_sim, g_ailments, 64);
    for (int i = 0; i < 1041; i++) rooms_staff(s, i)[11] = 0;
    for (int i = 0; i <= 1008; i++) P(i)[3] = P(i)[4] = 0xFF; /* everyone at home */
    describe(2, 0x60, 0x68, 0x70, 0x74, 0x78);
    describe(5, 0x80, 0x88, 0x90, 0x94, 0x98);
    ITEM(1)[6] = 11, ITEM(1)[16] = 1; /* a form (kind 11) */
    ITEM(2)[6] = 3, ITEM(2)[16] = 1; /* a pen (kind 3) */
    ITEM(3)[10] = 4; /* an instrument with capability 4 */
    /* illness 7: physical, treated in the room by treatment 2 (best) or 3-5; illness 8: psychological;
     * illness 9: treated in a bed by treatment 6 */
    ILL(7)[11] = 2, ILL(7)[12] = 3, ILL(7)[13] = 4, ILL(7)[14] = 5, ILL(7)[18] = 2;
    ILL(8)[8] = 2, ILL(8)[11] = 2;
    ILL(9)[11] = 6, ILL(9)[12] = 6, ILL(9)[13] = 6, ILL(9)[14] = 6;
    TREAT(2)[6] = 1, TREAT(2)[7] = 3, TREAT(2)[21] = 4, TREAT(2)[16] = 1; /* byte 21: a capability */
    TREAT(6)[4] = 1, TREAT(6)[6] = 1, TREAT(6)[7] = 2, TREAT(6)[16] = 8, TREAT(6)[17] = 8;
    world_set_cash(&g_world, 0, 984);
    h0()[0x95] = 4; /* base fee */
}

/* reception with clerk 20, a waiting room of 10 seats (nurse 21), a treatment room with doctor 22 and nurse 23,
 * a Krankenzimmer with 4 beds and nurse 24 */
static void clinic(void) {
    fresh();
    counts()[1] = counts()[2] = counts()[3] = counts()[5] = counts()[6] = 1;
    put16(R() + 8, 20);
    put16(R() + 0x68, 1), put16(R() + 0x6A, 2), R()[0x74] = R()[0x75] = 50;
    uint8_t *w = rec(3, 0);
    put16(w + 8, 21), w[10] = 10;
    uint8_t *t = rec(5, 0);
    put16(t + 8, 23), put16(t + 10, 22);
    uint8_t *k = rec(6, 0);
    put16(k + 8, 24), k[10] = 4;
}

static void at_desk(int index) {
    put16(R() + 10, index);
    P(index)[3] = 0, P(index)[4] = 2;
}

/* ---- site map ------------------------------------------------------------------------------------------ */

static void test_site_click(void) {
    fresh();
    uint8_t flags[256];
    memset(flags, 0x55, sizeof flags);
    rooms_set_pb16(s, RB_ROOM, 3);
    const SiteBuilding *waiting = rooms_site_building(0x32D77), *ward = rooms_site_building(0x36D4C);
    CHECK(!rooms_site_click(s, flags, waiting)); /* unbuilt: no effect at all */
    CHECK(rooms_pb16(s, RB_ROOM) == 3 && flags[FB_WAITING] == 0x55);
    CHECK(!rooms_site_click(s, flags, ward)); /* "always": the selection is reset even unbuilt */
    CHECK(rooms_pb16(s, RB_ROOM) == 0 && flags[FB_WARD] == 0x55);
    counts()[3] = 1, pb()[0x44] = 0xFF, rooms_set_pb16(s, RB_ROOM, 3);
    CHECK(rooms_site_click(s, flags, waiting));
    CHECK(rooms_pb16(s, RB_ROOM) == 0 && flags[FB_WAITING] == 0 && pb()[0x44] == 0);
    counts()[3] = 2;
    CHECK(rooms_site_click(s, flags, waiting) && flags[FB_WAITING] == 0xFF);
    counts()[5] = 2, pb()[0x5C] = pb()[0xAC] = 0xFF;
    CHECK(rooms_site_click(s, flags, rooms_site_building(0x36D20)));
    CHECK(flags[FB_TREATMENT] == 0xFF && pb()[0x5C] == 0 && pb()[0xAC] == 0);
    CHECK(rooms_site_building(0x2C79B) == NULL); /* the office is not a building */
}

static void test_selector(void) {
    fresh();
    counts()[3] = 3;
    rooms_set_pb16(s, RB_ROOM, 1);
    rooms_selector_step(s, 3, 1);
    CHECK(rooms_pb16(s, RB_ROOM) == 2);
    rooms_selector_step(s, 3, 1);
    CHECK(rooms_pb16(s, RB_ROOM) == 0); /* past the last: the first */
    rooms_selector_step(s, 3, -1);
    CHECK(rooms_pb16(s, RB_ROOM) == 2); /* below the first: the last */
}

/* ---- reception ----------------------------------------------------------------------------------------- */

static void test_desk(void) {
    clinic();
    RoomsResult o = {0};
    put16(R() + 8, 0);
    CHECK(rooms_desk(s, &o) == 975);
    put16(R() + 8, 20), rooms_staff(s, 20)[11] = 1;
    CHECK(rooms_desk(s, &o) == 539 && o.staff == 20); /* nobody at the desk, independent clerk */
    ill(5, 0, 7), at_desk(5);
    CHECK(rooms_desk(s, &o) == 539);
    P(5)[104] = 1; /* a dead patient: the sheet opens, independent clerk or not */
    CHECK(rooms_desk(s, &o) == 0 && rooms_pb16(s, RB_PATIENT) == 5 && rooms_pb16(s, RB_PAGE) == 0);
    rooms_staff(s, 20)[11] = 0, put16(R() + 10, 0);
    CHECK(rooms_desk(s, &o) == 974);
    ill(6, 1, 7), at_desk(6), rooms_set_pb16(s, RB_PAGE, 2);
    CHECK(rooms_desk(s, &o) == 0 && rooms_pb16(s, RB_PATIENT) == 6 && rooms_pb16(s, RB_PAGE) == 0);
}

static void test_throw_out(void) {
    clinic();
    RoomsResult o = {0};
    ill(5, 0, 7), at_desk(5);
    ill(6, 1, 7), put16(R() + 14, 6), P(6)[3] = 0, P(6)[4] = 2;
    CHECK(rooms_admit(s, 0, &o) == 2266 && o.index == 5);
    CHECK(P(5)[3] == 0xFF && P(5)[4] == 0xFF && P(5)[20] == 40); /* opinion halved, 80 -> 40 */
    CHECK(get16(R() + 10) == 6 && get16(R() + 14) == 0); /* the queue moved up */
    CHECK(pb()[RB_DECISION] == 0 && news_of(0x1C) == 1);
}

static void test_admit_refusals(void) {
    clinic();
    RoomsResult o = {0};
    counts()[3] = 0;
    ill(5, 0, 7), at_desk(5);
    CHECK(rooms_admit(s, 1, &o) == 2265 && P(5)[3] == 0xFF && P(5)[20] == 40 && !get16(R() + 10));
    CHECK(pb()[RB_DECISION] == 0xFF);
    counts()[3] = 1, rec(3, 0)[12] = 10; /* full */
    ill(6, 1, 7), at_desk(6);
    CHECK(rooms_admit(s, 1, &o) == 2259 && P(6)[3] == 0xFF && news_of(0x1B) == 1 && !get16(R() + 10));
    rec(3, 0)[12] = 0;
    ill(7, 0, 7), at_desk(7);
    put16(R() + 0x68, 0); /* no form */
    CHECK(rooms_admit(s, 1, &o) == 2257 && P(7)[4] == 2 && get16(R() + 10) == 7);
    put16(R() + 0x68, 1), put16(R() + 0x6A, 0); /* no pen */
    CHECK(rooms_admit(s, 1, &o) == 2258 && P(7)[4] == 2 && get16(R() + 10) == 7 && rec(3, 0)[12] == 0);
}

static void test_admit_seated(void) {
    clinic();
    RoomsResult o = {0};
    ill(5, 1, 7), at_desk(5);
    int work = staff_work(20);
    CHECK(rooms_admit(s, 1, &o) == 2261 && o.room == 0);
    const uint8_t *w = rec(3, 0);
    int seat = -1;
    for (int i = 0; i < 10; i++)
        if (get16(w + 0x14 + 4 * i) == 5) seat = i;
    CHECK(seat >= 0 && w[12] == 1 && P(5)[4] == 3 && P(5)[66] == 0 && !get16(R() + 10));
    CHECK(staff_work(20) == work + 1);
    CHECK(R()[0x74] == 49 && R()[0x75] == 49); /* the form and the pen were used */
}

static void test_bed_assignment(void) {
    clinic();
    RoomsResult o = {0};
    counts()[6] = 0;
    CHECK(rooms_bed_open(s, &o) == 2371);
    counts()[6] = 1, rec(6, 0)[12] = 4;
    CHECK(rooms_bed_open(s, &o) == 2370);
    rec(6, 0)[12] = 1;
    put16(R() + 8, 0);
    CHECK(rooms_bed_open(s, &o) == 975);
    put16(R() + 8, 20), rooms_staff(s, 20)[11] = 1;
    CHECK(rooms_bed_open(s, &o) == 539);
    rooms_staff(s, 20)[11] = 0;
    CHECK(rooms_bed_open(s, &o) == 2367 && o.total == 4 && o.free_beds == 3);
    ill(5, 0, 9), ill(6, 1, 9);
    put16(R() + 0x46, 5), put16(R() + 0x4E, 6), P(5)[4] = P(6)[4] = 2, P(5)[3] = P(6)[3] = 0;
    CHECK(rooms_bed_open(s, &o) == 0 && rooms_pb16(s, RB_QUEUE) == 0x4E && rooms_pb16(s, RB_WARD) == 0);
    int work = staff_work(20), patients = get16(h0() + 0x2E);
    CHECK(rooms_bed_assign(s, &o) == 2362 && o.index == 6 && o.screen == 53 && o.staff == 20);
    const uint8_t *k = rec(6, 0);
    int bed = -1;
    for (int i = 0; i < 10; i++)
        if (get16(k + 0x14 + 4 * i) == 6) bed = i;
    CHECK(bed >= 0 && k[12] == 2 && P(6)[4] == 6 && P(6)[66] == 0 && !get16(R() + 0x4E) && get16(R() + 0x46) == 5);
    CHECK(staff_work(20) == work + 1 && get16(h0() + 0x2E) == patients + 1);
    pb()[RB_FROM_98] = 0xFF, rooms_set_pb16(s, RB_QUEUE, 0x46);
    CHECK(rooms_bed_assign(s, &o) == 2362 && o.screen == 60 && pb()[RB_FROM_98] == 0);
    rec(6, 0)[12] = 4;
    put16(R() + 0x46, 5), rooms_set_pb16(s, RB_QUEUE, 0x46);
    CHECK(rooms_bed_assign(s, &o) == -1 && get16(R() + 0x46) == 5); /* ward full: nothing happens */
}

static void test_overview(void) {
    clinic();
    CHECK(rooms_overview(s, 1) == 541);
    ill(9, 0, 7), P(9)[3] = 0, P(9)[4] = 3, P(9)[66] = 0;
    put16(rec(3, 0) + 0x14 + 4 * 6, 9);
    ill(4, 0, 7), P(4)[3] = 0, P(4)[4] = 2, P(4)[66] = 1; /* in the bed queue: skipped */
    CHECK(rooms_overview(s, 1) == 0 && rooms_pb16(s, RB_PATIENT) == 9 && rooms_pb16(s, RB_PLACE) == 6);
    CHECK(rooms_overview(s, 0) == 0 && rooms_pb16(s, RB_PATIENT) == 9); /* the only one: found again */
}

/* SIM-75 (docs/mainloop.md): every nurse show saves the old comfort in the shared newspaper byte.
 * A refusal must preserve both fields, even when a previous show is still running. */
static void test_show_scratch_byte(void) {
    static const int cats[] = {2, 3, 6, 7, 9, 11, 16};
    static const int offsets[] = {0x45, 0x0F, 0x0F, 0x17, 0x0F, 0x19, 0x0F};
    static const int refusals[] = {550, 551, 548, 547, 553, 552, 549};
    static const int old[] = {0, 1, 31, 255};
    for (int c = 0; c < 7; c++) {
        for (int v = 0; v < 4; v++) {
            fresh();
            /* Reception is always record 0; the other shows use the selected room. */
            rooms_set_pb16(s, RB_ROOM, 1);
            uint8_t *r = rec(cats[c], c ? 1 : 0);
            r[offsets[c]] = (uint8_t)old[v];
            s->dup_byte = 83;
            int result = c < 3 ? rooms_show(s, cats[c]) : depts_show(s, cats[c]);
            CHECK(result == refusals[c] && r[offsets[c]] == old[v] && s->dup_byte == 83);
            put16(r + 8, 21);
            result = c < 3 ? rooms_show(s, cats[c]) : depts_show(s, cats[c]);
            CHECK(result == 0 && s->dup_byte == old[v]);
            CHECK(r[offsets[c]] >= 22 && r[offsets[c]] <= 37);
            int running = r[offsets[c]];
            s->dup_byte = 83;
            result = c < 3 ? rooms_show(s, cats[c]) : depts_show(s, cats[c]);
            CHECK(result == 0 && s->dup_byte == running);
        }
    }
}

static void test_shows(void) {
    clinic();
    put16(R() + 8, 0);
    CHECK(rooms_show(s, 2) == 550);
    put16(rec(3, 0) + 8, 0);
    CHECK(rooms_show(s, 3) == 551);
    put16(rec(6, 0) + 8, 0);
    CHECK(rooms_show(s, 6) == 548);
    put16(R() + 8, 20);
    CHECK(rooms_show(s, 2) == 0 && R()[0x45] >= 22 && R()[0x45] <= 37);
}

/* ---- treatment room -------------------------------------------------------------------------------------- */

static void waiting_patient(int i, int sex, int id, int seat, int wait) {
    ill(i, sex, id);
    P(i)[3] = 0, P(i)[4] = 3;
    put16(rec(3, 0) + 0x14 + 4 * seat, i), put16(rec(3, 0) + 0x16 + 4 * seat, wait);
    rec(3, 0)[12]++;
}

static void test_call_next(void) {
    clinic();
    RoomsResult o = {0};
    uint8_t *t = rec(5, 0);
    put16(t + 10, 0);
    CHECK(rooms_call(s, &o) == 418);
    put16(t + 10, 22), put16(t + 8, 0);
    CHECK(rooms_call(s, &o) == 414);
    put16(t + 8, 23), rooms_staff(s, 23)[11] = 1;
    CHECK(rooms_call(s, &o) == 539 && o.staff == 23);
    rooms_staff(s, 23)[11] = 0;
    CHECK(rooms_call(s, &o) == 406); /* nobody waits */
    put16(t + 12, 99);
    CHECK(rooms_call(s, &o) == 410);
    put16(t + 12, 0);
    waiting_patient(5, 0, 8, 2, 150); /* psychological, waiting long */
    waiting_patient(6, 1, 7, 4, 10); /* physical, waiting briefly */
    int nurse = staff_work(23), doctor = staff_work(22);
    CHECK(rooms_call(s, &o) == 1340 && o.index == 6 && o.room == 0 && o.staff == 23);
    CHECK(get16(t + 12) == 6 && get16(t + 14) == 999 && !get16(t + 16) && !get16(t + 18));
    CHECK(P(6)[4] == 5 && P(6)[66] == 0 && rec(3, 0)[12] == 1 && !get16(rec(3, 0) + 0x14 + 16));
    CHECK(staff_work(23) == nurse + 1 && staff_work(22) == doctor); /* the nurse's counter */
    CHECK(!get32(pb() + RB_CALL));
    put16(t + 12, 0);
    CHECK(rooms_call(s, &o) == 1340 && o.index == 5); /* no type-0 case left: any */
}

/* the patient i in treatment room 0, diagnosed (illness 7, treatment t) */
static void diagnosed(int i, int id, int t) {
    ill(i, 0, id);
    P(i)[3] = 0, P(i)[4] = 5, P(i)[42] = (uint8_t)id, P(i)[43] = (uint8_t)t;
    put16(rec(5, 0) + 12, i);
    put16(rec(5, 0) + 14, 0);
}

static void test_player_diagnosis(void) {
    clinic();
    ill(5, 0, 7);
    P(5)[3] = 0, P(5)[4] = 5;
    uint8_t *t = rec(5, 0);
    put16(t + 12, 5), put16(t + 14, 1);
    rooms_staff(s, 22)[12] = rooms_staff(s, 22)[13] = 255; /* a doctor whose skill always finds the illness */
    sim_call(s, 0x6AD3C); /* the countdown reaches 0: the player's diagnosis and proposal */
    CHECK(get16(t + 14) == 0 && P(5)[42] == 7);
    CHECK(P(5)[43] >= 2 && P(5)[43] <= 5);
    /* an unskilled doctor guesses; a guess of another type is corrected to the real illness. Every place of ids 1-70
     * holds the ailment, so the sixteen candidates are found at once (docs/sim-rooms.md, "The wrong guess") */
    static uint8_t full[71 * 4];
    memset(full, 1, sizeof full);
    sim_set_ailments(s, full, sizeof full);
    rooms_staff(s, 22)[12] = rooms_staff(s, 22)[13] = 0;
    for (int id = 1; id <= 70; id++)
        if (id != 7) ILL(id)[8] = 2;
    for (int k = 0; k < 20; k++) {
        P(5)[42] = P(5)[43] = 0, put16(t + 14, 1);
        sim_call(s, 0x6AD3C);
        CHECK(P(5)[42] == 7);
    }
    for (int id = 1; id <= 70; id++) ILL(id)[8] = 0; /* now every guess is of the same type: some stay wrong */
    int wrong = 0;
    for (int k = 0; k < 20; k++) {
        P(5)[42] = 0, put16(t + 14, 1);
        sim_call(s, 0x6AD3C);
        wrong += P(5)[42] != 7;
    }
    CHECK(wrong > 0);
    /* the synthetic table of fresh(): the ailment at three places of 280, so the list never fills - always right */
    sim_set_ailments(s, g_ailments, sizeof g_ailments);
    for (int k = 0; k < 20; k++) {
        P(5)[42] = 0, put16(t + 14, 1);
        sim_call(s, 0x6AD3C);
        CHECK(P(5)[42] == 7);
    }
}

static void test_other_proposal(void) {
    clinic();
    RoomsResult o = {0};
    uint8_t *t = rec(5, 0);
    diagnosed(5, 7, 3);
    rooms_staff(s, 22)[11] = 1, put16(t + 0x13, 42);
    CHECK(rooms_other(s, &o) == 42 && o.staff == 42); /* the name of the staff index in T+0x13 (reproduced) */
    CHECK(rooms_accept(s, &o) == 42 && o.staff == 42);
    /* both CD builds name the doctor, T+0x0A (docs/editions.md 7.2, OTH-DE-2) */
    for (int ed = EDITION_GERMAN_CD; ed <= EDITION_ENGLISH_CD; ed++) {
        g_prog.edition = (Edition)ed;
        CHECK(rooms_other(s, &o) == 42 && o.staff == 22);
        CHECK(rooms_accept(s, &o) == 42 && o.staff == 22);
    }
    g_prog.edition = EDITION_DISK;
    rooms_staff(s, 22)[11] = 0, put16(t + 0x13, 0);
    put16(t + 16, 3);
    CHECK(rooms_other(s, &o) == 78);
    put16(t + 16, 0), put16(t + 14, 999);
    CHECK(rooms_other(s, &o) == 89);
    put16(t + 14, 5);
    CHECK(rooms_other(s, &o) == 87);
    put16(t + 14, 0);
    rooms_staff(s, 22)[12] = rooms_staff(s, 22)[13] = 255; /* a skilled doctor: the AI rank would give 0 */
    int seen = 0;
    for (int k = 0; k < 400; k++) {
        CHECK(rooms_other(s, &o) == 0);
        int x = P(5)[43];
        CHECK(x >= 2 && x <= 5);
        seen |= 1 << x;
    }
    CHECK(seen == (1 << 2 | 1 << 3 | 1 << 4 | 1 << 5)); /* the player's rank r3 reaches all four */
}

static void test_accept_in_room(void) {
    clinic();
    RoomsResult o = {0};
    uint8_t *t = rec(5, 0);
    diagnosed(5, 7, 2);
    put16(t + 14, 999);
    CHECK(rooms_accept(s, &o) == 89);
    put16(t + 14, 3);
    CHECK(rooms_accept(s, &o) == 87);
    put16(t + 14, 0);
    CHECK(rooms_accept(s, &o) == 0);
    CHECK(get16(t + 16) == 2 && get16(t + 18) == 999 && t[0x44] == 4 && t[0x45] == 0); /* capability 4 missing */
    CHECK(rooms_accept(s, &o) == 79); /* understood already */
    put16(t + 16, 0), put16(t + 0x88, 3); /* the instrument with capability 4 */
    CHECK(rooms_accept(s, &o) == 0 && t[0x44] == 0);
}

static void test_accept_psychological(void) {
    clinic();
    RoomsResult o = {0};
    diagnosed(5, 8, 0xFD);
    P(5)[66] = 7; /* field 66 must be set to the waiting room */
    CHECK(rooms_accept(s, &o) == 32 && o.room == 0);
    CHECK(P(5)[4] == 3 && P(5)[66] == 0 && P(5)[78] == 30 && P(5)[20] == 79 && !P(5)[42] && !P(5)[43]);
    CHECK(!get16(rec(5, 0) + 12) && rec(3, 0)[12] == 1);
    clinic();
    counts()[3] = 0;
    diagnosed(5, 8, 0xFD);
    CHECK(rooms_accept(s, &o) == 34 && P(5)[3] == 0xFF && P(5)[20] == 40);
    clinic();
    diagnosed(5, 8, 2); /* the diagnosed illness decides, not field 43 */
    CHECK(rooms_accept(s, &o) == 32 && P(5)[4] == 3);
}

static void test_accept_bed(void) {
    clinic();
    RoomsResult o = {0};
    uint8_t *t = rec(5, 0);
    diagnosed(5, 9, 6);
    for (int i = 0; i < 6; i++) put16(R() + 0x46 + 4 * i, 100 + i);
    CHECK(rooms_accept(s, &o) == 427 && get16(t + 12) == 5 && P(5)[4] == 5); /* nothing changes */
    memset(R() + 0x46, 0, 24);
    counts()[6] = 0;
    CHECK(rooms_accept(s, &o) == 2371 && !get16(t + 12) && P(5)[3] == 0xFF && P(5)[20] == 79);
    clinic();
    diagnosed(5, 9, 6);
    int doctor = staff_work(22), nurse = staff_work(23);
    CHECK(rooms_accept(s, &o) == 425 && o.screen == 98 && o.bed_button);
    CHECK(get16(R() + 0x46) == 5 && P(5)[4] == 2 && !get16(rec(5, 0) + 12));
    CHECK(staff_work(22) == doctor + 1 && staff_work(23) == nurse + 1);
    clinic();
    diagnosed(5, 9, 6), rec(6, 0)[12] = 4; /* no free bed: back to the treatment room */
    CHECK(rooms_accept(s, &o) == 425 && o.screen == 60 && !o.bed_button);
    clinic();
    diagnosed(5, 9, 6), put16(R() + 8, 0); /* free bed, no clerk: screen 98 without its button */
    CHECK(rooms_accept(s, &o) == 425 && o.screen == 98 && !o.bed_button);
    clinic();
    diagnosed(5, 9, 6), rooms_staff(s, 20)[11] = 1;
    CHECK(rooms_accept(s, &o) == 425 && o.screen == 60);
}

static void test_bill(void) {
    clinic();
    RoomsResult o = {0};
    uint8_t *t = rec(5, 0);
    diagnosed(5, 7, 2);
    put16(t + 14, 999);
    CHECK(rooms_hand_bill(s, &o) == 89);
    put16(t + 14, 2);
    CHECK(rooms_hand_bill(s, &o) == 87);
    put16(t + 14, 0), put16(t + 16, 2), put16(t + 18, 5);
    P(5)[79] = 0xFF; /* a home-treatment flag does not let a running treatment be billed */
    CHECK(rooms_hand_bill(s, &o) == 84 && cash() == 984); /* treatment running */
    P(5)[79] = 0;
    put16(t + 18, 0);
    CHECK(rooms_hand_bill(s, &o) == 84); /* field 79 not set yet */
    P(5)[57] = 5;
    CHECK(rooms_hand_bill(s, &o) == 86);
    P(5)[57] = 0, P(5)[79] = 0xFF;
    P(5)[50] = 4, h0()[0x20] = 2; /* 4 Krankenzimmer units at 2 per Lümmel: bill = 4 + 2 = 6 */
    put32(h0() + 0x16E, 999);
    CHECK(rooms_hand_bill(s, &o) == 14 && o.bill == 6 && o.index == 5);
    CHECK(cash() == 984 + 6 && get32(h0() + 0x8C) == 6 && get32(h0() + 0x90) == 6 && get16(h0() + 0x26) == 1);
    CHECK(get32(h0() + 0x16E) == 0 && get16(h0() + 0x172) == 1); /* owner's decision: raised as the original */
    CHECK(P(5)[3] == 0xFF && P(5)[4] == 0xFF && P(5)[81] == 0 && P(5)[105] == 3 && P(5)[79] == 0xFF);
    CHECK(!get16(t + 12) && !get16(t + 16) && !get16(t + 18));
    CHECK(o.reaction == 6 && P(5)[20] == 84); /* bill 6, ordinary: v = 2 at opinion 80 -> 2 four times */
    clinic();
    diagnosed(5, 7, 2);
    P(5)[79] = 0xFF;
    CHECK(rooms_accept(s, &o) == 14 && cash() == 984 + 4); /* a home treatment: OK goes to the bill */
}

static void test_abort(void) {
    clinic();
    RoomsResult o = {0};
    uint8_t *t = rec(5, 0);
    diagnosed(5, 7, 2);
    put16(t + 14, 999); /* allowed at any stage */
    CHECK(rooms_abort(s, &o) == 77 && o.index == 5);
    CHECK(!get16(t + 12) && P(5)[3] == 0xFF && P(5)[4] == 0xFF && P(5)[20] == 0 && !P(5)[43] && cash() == 984);
    diagnosed(6, 7, 12);
    P(6)[57] = 5, put16(t + 20, 200); /* a donor with blood in the room counter, no bank */
    CHECK(rooms_abort(s, &o) == 38 && get32(h0() + 0x66) == 200 && get16(h0() + 0x3E) == 1 && P(6)[20] == 1);
}

static void test_inventory(void) {
    clinic();
    uint8_t *t = rec(5, 0);
    CHECK(rooms_inventory(s, 5, 0x36D3C) == 0 && rooms_pb16(s, RB_INV_CAT) == 5 && rooms_pb16(s, RB_PAGE) == 0);
    CHECK(get32(pb() + 6) == 0x36D3C);
    put16(t + 12, 5);
    CHECK(rooms_inventory(s, 5, 0x36D3C) == 375); /* the doctor is busy with the instruments */
    t[0x44] = 4;
    CHECK(rooms_inventory(s, 5, 0x36D3C) == 0 && pb()[RB_INV_MISSING] == 0xFF && rooms_pb16(s, RB_PAGE) == 1);
}

/* ---- the rooms' sound requests and ambience gates (docs/sound.md, docs/ui-sitemap.md) ---------------------------- */

static Sound g_snd;
static uint32_t snd_random(void *ctx, uint32_t n) { return sim_random(ctx, n); }
static uint32_t snd_bits(void *ctx, uint32_t mask) { return sim_bits(ctx, mask); }

static void snd_fresh(void) {
    fresh();
    sound_free(&g_snd);
    sound_init(&g_snd, NULL, &g_prog, snd_random, s);
    sound_set_bits(&g_snd, snd_bits); /* as game.c wires it */
    s->sound = &g_snd;
    for (int k = 0; k < 19; k++) counts()[k] = 1;
}

static int req(int id, int period, int level) {
    return g_snd.bg_request == id && g_snd.bg_period == period && g_snd.bg_target == level;
}

static void test_room_sound_views(void) {
    snd_fresh();
    /* reception: 0xF1 / 10, then its own quiet night */
    room_sound_view(s, 2);
    CHECK(req(0xF1, 0x140, 10));
    world_set_tail(&g_world, TAIL_HOUR, 21);
    room_sound_view(s, 2);
    CHECK(req(0xAB, 0x140, 7));
    put16(R() + 0x4E, 5); /* someone in bed-queue place 3 */
    room_sound_view(s, 2);
    CHECK(req(0xF1, 0x140, 10));
    put16(R() + 0x4E, 0), put16(R() + 0x08, 7); /* the clerk */
    room_sound_view(s, 2);
    CHECK(req(0xF1, 0x140, 10));
    world_set_tail(&g_world, TAIL_HOUR, 2);
    put16(R() + 0x08, 0);
    room_sound_view(s, 2);
    CHECK(req(0xAB, 0x140, 7)); /* before 3 */
    world_set_tail(&g_world, TAIL_HOUR, 3);
    room_sound_view(s, 2);
    CHECK(req(0xF1, 0x140, 10));
    /* treatment room, the shown room pb+0x10 = 1 */
    counts()[5] = 2, rooms_set_pb16(s, RB_ROOM, 1);
    room_sound_view(s, 5);
    CHECK(req(0xD3, 0x140, 12));
    put16(rec(5, 1) + 0x0C, 9);
    room_sound_view(s, 5);
    CHECK(req(0xF1, 0x140, 12));
    put16(rec(5, 0) + 0x0C, 0), put16(rec(5, 1) + 0x0C, 0);
    world_set_tail(&g_world, TAIL_HOUR, 20);
    room_sound_view(s, 5);
    CHECK(req(0xAB, 0x140, 7)); /* quiet night: +8, +0xC, +0xA all 0 */
    put16(rec(5, 1) + 0x08, 3);
    room_sound_view(s, 5);
    CHECK(req(0xD3, 0x140, 12)); /* the word +8 counts here */
    put16(rec(5, 1) + 0x08, 0), put16(rec(5, 1) + 0x0A, 3);
    room_sound_view(s, 5);
    CHECK(req(0xD3, 0x140, 12));
    put16(rec(5, 1) + 0x0A, 0);
    world_set_tail(&g_world, TAIL_HOUR, 2);
    room_sound_view(s, 5);
    CHECK(req(0xAB, 0x140, 7)); /* 2 o'clock is still night */
    world_set_tail(&g_world, TAIL_HOUR, 3);
    room_sound_view(s, 5);
    CHECK(req(0xD3, 0x140, 12)); /* 3 o'clock is not */
    world_set_tail(&g_world, TAIL_HOUR, 20);
    rooms_set_pb16(s, RB_ROOM, 0);
    /* operating theatre: staff word +0x0C; the quiet night only without the surgeon (+6) */
    room_sound_view(s, 7);
    CHECK(req(0xAB, 0x140, 7));
    put16(rec(7, 0) + 6, 4);
    room_sound_view(s, 7);
    CHECK(req(0xD2, 0x140, 30));
    put16(rec(7, 0) + 0x0C, 4);
    room_sound_view(s, 7);
    CHECK(req(0x69, 0x140, 30));
    /* intensive care */
    world_set_tail(&g_world, TAIL_HOUR, 10);
    room_sound_view(s, 9);
    CHECK(req(0xAB, 0x140, 7)); /* no nurse, no patient */
    put16(rec(9, 0) + 0x0C, 12);
    room_sound_view(s, 9);
    CHECK(req(0x69, 0x140, 30)); /* no nurse, a patient */
    put16(rec(9, 0) + 8, 3);
    room_sound_view(s, 9);
    CHECK(req(0x58, 0x294, 63)); /* nurse and patient: the heartbeat */
    put16(rec(9, 0) + 0x0C, 0);
    room_sound_view(s, 9);
    CHECK(req(0xD3, 0x140, 12));
    /* dental, neurology, the fixed ones */
    room_sound_view(s, 11);
    CHECK(req(0xD3, 0x140, 12));
    put16(rec(11, 0) + 0x0C, 1);
    room_sound_view(s, 11);
    CHECK(req(0xDA, 0x140, 34));
    put16(rec(15, 0) + 0x0C, 1);
    room_sound_view(s, 15);
    CHECK(req(0xEC, 0x140, 30));
    room_sound_view(s, 6);
    CHECK(req(0xB2, 0x118, 50));
    room_sound_view(s, 8);
    CHECK(req(0x6A, 0x118, 49));
    room_sound_view(s, 16);
    CHECK(req(0xB0, 0x140, 35));
    room_sound_view(s, 17);
    CHECK(req(0x5A, 0x294, 63));
    room_sound_view(s, 0);
    CHECK(req(0xFC, 0x140, 8));
    /* the single rooms read record 0 whatever pb+0x10 says */
    rooms_set_pb16(s, RB_ROOM, 1);
    room_sound_view(s, 4);
    CHECK(req(0xAB, 0x140, 7));
    put16(rec(4, 0) + 4, 2);
    room_sound_view(s, 4);
    CHECK(req(0xF9, 0x140, 17));
    room_sound_view(s, 12);
    CHECK(req(0xAB, 0x140, 7));
    put16(rec(12, 0) + 2, 2);
    room_sound_view(s, 12);
    CHECK(req(0xEA, 0x140, 40));
    room_sound_view(s, 13);
    CHECK(req(0xAB, 0x140, 7));
    put16(rec(13, 0) + 6, 2);
    room_sound_view(s, 13);
    CHECK(req(0x68, 0xBE, 1));
    rec(13, 0)[5] = 1;
    room_sound_view(s, 13);
    CHECK(req(0x68, 0xBE, 20));
    room_sound_view(s, 14);
    CHECK(req(0xAB, 0x140, 7));
    rec(14, 0)[4] = 1; /* the dword +2 is set, the word +2 is not */
    room_sound_view(s, 14);
    CHECK(req(0xBE, 0x12C, 10));
    rec(14, 0)[2] = 1;
    room_sound_view(s, 14);
    CHECK(req(0xBE, 0x12C, 50));
    room_sound_view(s, 18);
    CHECK(req(0xA2, 0x140, 12));
    world_set_tail(&g_world, TAIL_HOUR, 23);
    put16(rec(18, 0) + 8, 5); /* 0x31B7F skips +8 */
    room_sound_view(s, 18);
    CHECK(req(0xAB, 0x140, 7));
    put16(rec(18, 0) + 0x0A, 5);
    room_sound_view(s, 18);
    CHECK(req(0xA2, 0x140, 60));
    room_sound_view(s, 10);
    CHECK(req(0xAB, 0x140, 7));
    rec(10, 0)[9] = 1; /* the dword +6 */
    room_sound_view(s, 10);
    CHECK(req(0xCE, 0x140, 36));
    rooms_set_pb16(s, RB_ROOM, 0);
}

static void test_waiting_sound(void) {
    snd_fresh();
    room_sound_view(s, 3);
    CHECK(req(0xF2, 0x140, 13)); /* nobody: at least 13 */
    /* the level reads the number waiting W+0x0C, not the seat words (0x32E43; docs/sound.md, SND-11) */
    for (int i = 0; i < 10; i++) put16(rec(3, 0) + 0x14 + 4 * i, 10 + i);
    room_sound_view(s, 3);
    CHECK(req(0xF2, 0x140, 13)); /* ten seats taken, W+0x0C = 0 */
    put16(rec(3, 0) + 0x0C, 1);
    room_sound_view(s, 3);
    CHECK(req(0xF2, 0x140, 13)); /* 13 for 0 and 1 */
    put16(rec(3, 0) + 0x0C, 3);
    room_sound_view(s, 3);
    CHECK(req(0xF2, 0x140, 21));
    put16(rec(3, 0) + 0x0C, 14);
    room_sound_view(s, 3);
    CHECK(req(0xF2, 0x140, 63)); /* 13 + 4 x 13 = 65: at most 63 */
    put16(rec(3, 0) + 0x0C, 13);
    room_sound_view(s, 3);
    CHECK(req(0xF2, 0x140, 61));
    put16(rec(3, 0) + 0x0C, 10);
    room_sound_view(s, 3);
    CHECK(req(0xF2, 0x140, 49)); /* 13 + 4 x 9 */
    put16(rec(3, 0) + 0x0C, 0);
    /* the frame: once 0xF2 plays, only the target moves (no new request, so no dip) */
    g_snd.bg_request = 0, g_snd.bg_playing = 0xF2, g_snd.bg_target = 13;
    put16(rec(3, 0) + 0x0C, 10);
    room_sound_waiting_level(s, 1);
    CHECK(!g_snd.bg_request && g_snd.bg_target == 49);
    room_sound_waiting_level(s, 0);
    CHECK(g_snd.bg_request == 0xF2);
    /* not while 0xAB is requested or playing */
    g_snd.bg_request = 0xAB, g_snd.bg_target = 7;
    room_sound_waiting_level(s, 0);
    CHECK(g_snd.bg_request == 0xAB && g_snd.bg_target == 7);
    g_snd.bg_request = 0, g_snd.bg_playing = 0xAB;
    room_sound_waiting_level(s, 1);
    CHECK(!g_snd.bg_request && g_snd.bg_target == 7);
    /* its quiet night: nobody waiting (+0x0C) and no nurse (+8) */
    put16(rec(3, 0) + 0x0C, 0);
    g_snd.bg_playing = 0;
    world_set_tail(&g_world, TAIL_HOUR, 22);
    room_sound_view(s, 3);
    CHECK(req(0xAB, 0x140, 7));
    put16(rec(3, 0) + 0x0C, 10), g_snd.bg_request = 0; /* a pending 0xAB would keep 0x32E43 off */
    room_sound_view(s, 3);
    CHECK(req(0xF2, 0x140, 49));
}

/* Picks over many frames with the ambience flag forced on and the queues emptied each time: the measured share of
 * frames that queued something, against the gate's probability x 1/64. */
static int picks(uint32_t frame, int n, int queue) {
    int got = 0;
    for (int i = 0; i < n; i++) {
        memset(g_snd.queue, 0, sizeof g_snd.queue);
        g_snd.ambience = 1;
        pb()[RB_TICK] = 1;
        room_sound_frame(s, frame);
        got += g_snd.queue[queue][0].id != 0 || g_snd.queue[queue][1].id != 0;
    }
    return got;
}

/* SND-17: empty wards consume no draw or sound work even with an occupied bed. A nonzero stored WORD
 * keeps the ranged-10 gate, show music and both ambience selections in their existing order. */
static void test_empty_ward_sound_gate(void) {
    static const int PATIENTS[] = {0, 1, 2, 10, 256};
    static const uint32_t FRAMES[] = {0x27588, 0x275E0};
    static Sound before;
    for (size_t ward = 0; ward < sizeof FRAMES / sizeof *FRAMES; ward++) {
        for (size_t count = 0; count < sizeof PATIENTS / sizeof *PATIENTS; count++) {
            for (int choice = 0; choice <= 9; choice += 9) {
                snd_fresh();
                uint8_t *r = rec(ward ? 16 : 6, 0);
                put16(r + 0x0C, (uint16_t)PATIENTS[count]);
                /* Deliberately inconsistent occupancy in both directions: this gate reads the stored count. */
                if (!PATIENTS[count]) put16(r + 0x14 + 4 * 9, 17);
                r[0x0F] = 1; /* a show: its first draw will return zero */
                uint32_t table = ward ? 0x4EFA : 0x4E9A;
                for (int i = 0; i < 4; i++) {
                    put16(g_image + table + 6 * i, (uint16_t)(70 + 10 * ward + (size_t)i));
                    put16(g_image + table + 6 * i + 2, (uint16_t)(200 + i));
                    put16(g_image + table + 6 * i + 4, (uint16_t)(20 + i));
                }
                g_snd.ambience = 1;
                g_snd.bg_request = 42, g_snd.bg_period = 333, g_snd.bg_target = 17;
                memcpy(&before, &g_snd, sizeof before);
                uint32_t values[] = {(uint32_t)choice, 0, 4, 0, 4, 1};
                SimScript script = {values, 6, 0, 0, tmpfile()};
                CHECK(script.trace != NULL);
                s->rng_script = &script;
                uint32_t state = s->rng_s;
                uint64_t auxiliary = s->rng;
                CHECK(room_sound_frame(s, FRAMES[ward]) == 1);
                s->rng_script = NULL;
                int accepted = choice < PATIENTS[count];
                size_t draws = !PATIENTS[count] ? 0 : accepted ? 6 : 1;
                CHECK(script.used == draws && script.extra == 0 && s->rng == auxiliary);
                for (size_t i = 0; i < draws; i++) state += values[i];
                CHECK(s->rng_s == state);
                if (!accepted) CHECK(memcmp(&g_snd, &before, sizeof before) == 0);
                else {
                    CHECK(g_snd.ambience == 0);
                    for (int queue = 0; queue < 2; queue++) {
                        for (int slot = 0; slot < 2; slot++) {
                            int i = 2 * queue + slot;
                            CHECK(g_snd.queue[queue][slot].id == 70 + 10 * ward + (size_t)i);
                            CHECK(g_snd.queue[queue][slot].period == 200 + i);
                            CHECK(g_snd.queue[queue][slot].volume == 20 + i);
                        }
                    }
                    CHECK(req(42, 333, 17));
                }
                if (script.trace) {
                    char trace[160] = {0}, expected[160] = {0};
                    rewind((FILE *)script.trace);
                    size_t bytes = fread(trace, 1, sizeof trace - 1, (FILE *)script.trace);
                    if (draws) snprintf(expected, sizeof expected, "0 r10 %d 1\n%s", choice,
                        accepted ? "0 b1 0 1\n0 b63 4 1\n0 b7 0 1\n0 b63 4 1\n0 b7 1 1\n" : "");
                    if (strcmp(trace, expected)) fprintf(stderr, "[FAIL] ward trace: %s; expected: %s\n", trace, expected);
                    CHECK(bytes == strlen(expected) && !strcmp(trace, expected));
                    fclose((FILE *)script.trace);
                }
            }
        }
    }
}

/* SND-19: real frame dispatch, scripted generator and both ambience queues. The existing sound draw callback
 * naturally observes that the continuation has not yet cleared PB0+56 while ambience is still running. */
static uint32_t administration_bits(void *ctx, uint32_t mask) {
    CHECK(rooms_pb(ctx)[0x56] == 0xA5);
    return sim_bits(ctx, mask);
}
static void administration_case(uint32_t gate, int tick, int ambience, int busy, int fill0, int fill1, int gates) {
    snd_fresh();
    static Biing b;
    b.sim = *s;
    g_snd.random_ctx = &b.sim;
    sound_set_bits(&g_snd, administration_bits);
    for (int p = 0; p < 2; p++) memset(g_world.block[BLK_PLAYER0 + p], p ? 0x59 : 0xA5, 500);
    pb()[RB_TICK] = (uint8_t)tick;
    uint8_t players[2][500];
    for (int p = 0; p < 2; p++) memcpy(players[p], g_world.block[BLK_PLAYER0 + p], 500);
    players[0][0x56] = 0;
    for (int row = 0; row < 16; row++) {
        put16(g_image + 0x4F5A + 6 * row, 80 + row);
        put16(g_image + 0x4F5A + 6 * row + 2, 200 + row);
        put16(g_image + 0x4F5A + 6 * row + 4, 20 + row);
    }
    g_snd.sticky = 11, g_snd.help_pending = busy & 1 ? 3 : 0, g_snd.race_pending = busy & 2 ? 5 : 0;
    g_snd.race_busy = busy & 4 ? 7 : 0, g_snd.help_busy = busy & 8 ? 9 : 0;
    g_snd.ambience = ambience, g_snd.ambience_count = 17, g_snd.pace = 19;
    int flags[] = {g_snd.sticky, g_snd.help_pending, g_snd.race_pending, g_snd.race_busy, g_snd.help_busy,
                   g_snd.ambience, g_snd.ambience_count, g_snd.pace};
    for (int q = 0; q < SOUND_QUEUES; q++) for (int i = 0; i < SOUND_QUEUE_LEN; i++) {
        g_snd.queue[q][i] = (SoundEntry){(uint16_t)(300 + 8 * q + i), (uint16_t)(600 + i), (uint16_t)(40 + q)};
        if (q < 2 && i >= (q ? fill1 : fill0) && i < 6) g_snd.queue[q][i].id = 0;
    }
    SoundEntry expected[SOUND_QUEUES][SOUND_QUEUE_LEN];
    memcpy(expected, g_snd.queue, sizeof expected);
    uint32_t raw[8] = {gate}; /* trailing zeros keep a regressed rejection loop bounded */
    size_t n = 1;
    char expected_trace[160];
    snprintf(expected_trace, sizeof expected_trace, "0 b31 %u 1\n", gate & 31);
    if (!(gate & 31) && ambience) for (int q = 0; q < 2; q++) {
        int pass = gates != q + 1;
        raw[n++] = pass ? 4 : 63;
        strcat(expected_trace, pass ? "0 b63 4 1\n" : "0 b63 63 1\n");
        if (!pass || (q == 0 && (busy & 7))) continue;
        if (q == 1) flags[5] = 0;
        int fill = q ? fill1 : fill0, pick = q ? 6 : 2;
        if (fill == 6) continue;
        raw[n++] = (uint32_t)pick;
        strcat(expected_trace, q ? "0 b7 6 1\n" : "0 b7 2 1\n");
        for (int i = 0; i < 2; i++) expected[q][fill + i] = (SoundEntry){(uint16_t)(80 + 2 * pick + i),
            (uint16_t)(200 + 2 * pick + i), (uint16_t)(20 + 2 * pick + i)};
    }
    SimScript script = {.v = raw, .n = sizeof raw / sizeof *raw, .trace = tmpfile()};
    CHECK(script.trace != NULL);
    b.sim.rng_script = &script;
    uint32_t state = b.sim.rng_s;
    uint64_t auxiliary = b.sim.rng;
    CHECK(purchase_routine(&b, 0x2535A) == 1);
    b.sim.rng_script = NULL;
    for (size_t i = 0; i < n; i++) state += raw[i];
    int actual[] = {g_snd.sticky, g_snd.help_pending, g_snd.race_pending, g_snd.race_busy, g_snd.help_busy,
                    g_snd.ambience, g_snd.ambience_count, g_snd.pace};
    int ok = script.used == n && script.extra == 0 && b.sim.rng_s == state && b.sim.rng == auxiliary &&
             !memcmp(expected, g_snd.queue, sizeof expected) && !memcmp(flags, actual, sizeof flags) &&
             !memcmp(players[0], pb(), 500) && !memcmp(players[1], g_world.block[BLK_PLAYER1], 500);
    if (!ok) fprintf(stderr, "  SND-19 raw %u tick %d ambience %d busy %d filled %d/%d gates %d\n",
                     gate, tick, ambience, busy, fill0, fill1, gates);
    CHECK(ok);
    if (script.trace) {
        char trace[160] = {0};
        rewind((FILE *)script.trace);
        size_t bytes = fread(trace, 1, sizeof trace - 1, (FILE *)script.trace);
        CHECK(bytes == strlen(expected_trace) && !strcmp(trace, expected_trace));
        fclose((FILE *)script.trace);
    }
}
static void test_administration_gate(void) {
    static const uint32_t RAW[] = {0, 31, 32, 0x10000, 0x10001, 0xFFFFFFFF};
    static const int BUSY[] = {0, 1, 2, 4, 8}, FILL[][2] = {{0, 0}, {5, 5}, {6, 6}, {6, 0}, {0, 6}};
    for (int raw = 0; raw < 6; raw++) for (int tick = 0; tick < 2; tick++) for (int ambience = 0; ambience < 2; ambience++)
        for (int busy = 0; busy < 5; busy++) for (int fill = 0; fill < 5; fill++) for (int gates = 0; gates < 3; gates++)
            administration_case(RAW[raw], tick, ambience, BUSY[busy], FILL[fill][0], FILL[fill][1], gates);
}

static void test_room_sound_frames(void) {
    snd_fresh();
    for (int i = 0; i < 0x6000; i += 2) put16(g_image + i, 0x33); /* every table entry an id */
    /* reception: anyone at the desk or in the bed queue, r15 = 0: 1/16 x 1/64, every frame (the room's tick gates only
     * its animations, docs/sound.md "Room frames") */
    CHECK(picks(0x1FEF4, 20000, 0) == 0);
    put16(R() + 0x0A, 3);
    int n = picks(0x1FEF4, 204800, 0);
    CHECK(n > 140 && n < 270); /* 200 expected */
    n = 0;
    for (int i = 0; i < 204800; i++) { /* without the tick: the same share */
        memset(g_snd.queue, 0, sizeof g_snd.queue);
        g_snd.ambience = 1, pb()[RB_TICK] = 0;
        room_sound_frame(s, 0x1FEF4);
        n += g_snd.queue[0][0].id != 0 || g_snd.queue[0][1].id != 0;
    }
    CHECK(n > 140 && n < 270);
    put16(R() + 0x0A, 0);
    put16(R() + 0x46 + 4 * 5, 7); /* the last bed-queue place alone */
    n = picks(0x1FEF4, 204800, 0);
    CHECK(n > 140 && n < 270);
    put16(R() + 0x46 + 4 * 5, 0), put16(R() + 0x0A, 3);
    /* the club close-up: every frame, 1/64 */
    n = picks(0x3717D, 64000, 0);
    CHECK(n > 850 && n < 1150);
    /* queue 2 clears the flag: without forcing it, one pick per 34 ticks at most */
    memset(g_snd.queue, 0, sizeof g_snd.queue);
    g_snd.ambience = 1;
    int frames = 0;
    while (g_snd.ambience && frames < 100000) room_sound_frame(s, 0x3717D), frames++;
    CHECK(!g_snd.ambience && g_snd.queue[1][0].id);
    for (int i = 0; i < 1000; i++) room_sound_frame(s, 0x3717D);
    CHECK(!g_snd.ambience);
    /* waiting room: random(10) < the number waiting */
    CHECK(picks(0x23E02, 20000, 0) == 0);
    put16(rec(3, 0) + 0x0C, 5);
    n = picks(0x23E02, 64000, 1); /* queue 2: the show music (no show running) does not add to it */
    CHECK(n > 380 && n < 620); /* 500 */
    /* personnel office and estate agent: t & 31 = 0 */
    n = picks(0x371BB, 204800, 0);
    CHECK(n > 70 && n < 140); /* 100 */
    /* kitchen: no cook, nothing; cooking t&3; not cooking two t&3 */
    CHECK(picks(0x3710B, 20000, 0) == 0);
    put16(rec(13, 0) + 6, 2), rec(13, 0)[5] = 1;
    n = picks(0x3710B, 64000, 0);
    CHECK(n > 200 && n < 310); /* 250 */
    rec(13, 0)[5] = 0;
    n = picks(0x3710B, 64000 * 4, 0);
    CHECK(n > 200 && n < 310); /* 250 */
    /* warehouse: a clerk, random(8) per queue (t AND 7) */
    CHECK(picks(0x4A2AB, 20000, 1) == 0);
    put16(rec(4, 0) + 4, 1);
    n = picks(0x4A2AB, 128000, 1);
    CHECK(n > 200 && n < 310); /* 128000 / 64 / 8 = 250; random(4) would give 500 */
    /* torture chamber: queue 1 from 0x519A, queue 2 from 0x51FA */
    for (int k = 0; k < 16; k++) put16(g_image + 0x519A + 6 * k, 0x44), put16(g_image + 0x51FA + 6 * k, 0x45);
    put16(rec(8, 0) + 0x0C, 9);
    for (int i = 0; i < 1000 && !(g_snd.queue[0][0].id && g_snd.queue[1][0].id); i++) {
        g_snd.ambience = 1;
        room_sound_frame(s, 0x37153);
    }
    CHECK(g_snd.queue[0][0].id == 0x44 && g_snd.queue[1][0].id == 0x45);
    /* intensive care: the heartbeat's level once a minute with a patient: 1 for a dead one, else 32-63 */
    put16(rec(9, 0) + 0x0C, 40);
    g_snd.bg_playing = 0x58, g_snd.bg_request = 0, g_snd.bg_target = 63;
    pb()[RB_MINUTE] = 0;
    room_sound_frame(s, 0x5B3D0);
    CHECK(g_snd.bg_target == 63);
    int lo = 99, hi = 0;
    for (int i = 0; i < 2000; i++) {
        pb()[RB_MINUTE] = 1;
        room_sound_frame(s, 0x5B3D0);
        lo = g_snd.bg_target < lo ? g_snd.bg_target : lo;
        hi = g_snd.bg_target > hi ? g_snd.bg_target : hi;
    }
    CHECK(lo == 32 && hi == 63);
    P(40)[0x68] = 1;
    room_sound_frame(s, 0x5B3D0);
    CHECK(g_snd.bg_target == 1);
    g_snd.bg_request = 0x58, g_snd.bg_target = 50;
    room_sound_frame(s, 0x5B3D0);
    CHECK(g_snd.bg_target == 50); /* a pending request: untouched */
    /* the race screen by phase */
    uint8_t *rb = g_world.block[BLK_34];
    put16(g_image + 0xA49A, 0x61), put16(g_image + 0xA4FA, 0x62), put16(g_image + 0xA55A, 0x63);
    for (int k = 1; k < 8; k++) put16(g_image + 0xA49A + 12 * k, 0x61), put16(g_image + 0xA4FA + 12 * k, 0x62),
                                 put16(g_image + 0xA55A + 12 * k, 0x63);
    put16(rb, 0);
    CHECK(picks(0x5AABB, 5000, 0) == 0); /* no race */
    put16(rb, 100), put16(rb + 2, 28);
    picks(0x5AABB, 5000, 0);
    CHECK(g_snd.queue[0][0].id == 0 || g_snd.queue[0][0].id == 0x61);
    int seen[3] = {0, 0, 0};
    const int minute[3] = {28, 29, 29}, result[3] = {0, 0, 4}, want[3] = {0x61, 0x62, 0x63};
    for (int c = 0; c < 3; c++) {
        put16(rb + 2, minute[c]), rb[6] = (uint8_t)result[c];
        for (int i = 0; i < 2000 && !seen[c]; i++) {
            memset(g_snd.queue, 0, sizeof g_snd.queue);
            g_snd.ambience = 1;
            room_sound_frame(s, 0x5AABB);
            if (g_snd.queue[0][0].id) seen[c] = g_snd.queue[0][0].id == want[c] ? 1 : -1;
        }
    }
    CHECK(seen[0] == 1 && seen[1] == 1 && seen[2] == 1);
    s->sound = NULL;
    sound_free(&g_snd);
}

/* SND-11 (docs/sound.md, "What the engine must change (SND-11)"): the heartbeat's two single draws and the pathology's
 * input pair */
static uint64_t mix_state;
static uint32_t mix32(void) { /* splitmix64: a random generator state per trial */
    uint64_t z = (mix_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return (uint32_t)((z ^ (z >> 31)) >> 32);
}

static void test_snd11_draws(void) {
    snd_fresh();
    for (int i = 0; i < 0x6000; i += 2) put16(g_image + i, 0x33);
    /* intensive care: a state and inputs (d, e) whose next two draws are 37 (AND 63) and 10 (AND 31) */
    uint32_t d = 0, e = 0, start = 0, after1 = 0, after3 = 0;
    for (uint32_t c = 1; c < 1000000 && !start; c++) {
        d = (c * 7919u) & 0xFFFFu, e = c * 40503u * 65537u;
        uint32_t x = c * 2654435761u, st = x;
        uint32_t t1 = sim_generator_step(&x, d, e);
        uint32_t x1 = x;
        uint32_t t2 = sim_generator_step(&x, d, e);
        if ((t1 & 63) == 37 && (t2 & 63) == 42) { /* AND 31: 10; random(32) would redraw 42 */
            start = st, after1 = x1;
            sim_generator_step(&x, d, e); /* the r1 gate of the show music that follows */
            after3 = x;
        }
    }
    CHECK(start != 0);
    SimIn old = sim_set_inputs(s, sim_in(d, e, 0));
    put16(rec(9, 0) + 0x0C, 40), P(40)[0x68] = 0, rec(9, 0)[0x0F] = 0;
    g_snd.bg_playing = 0x58, g_snd.bg_request = 0, g_snd.bg_target = 63, g_snd.ambience = 0;
    pb()[RB_MINUTE] = 1;
    s->rng_s = start;
    room_sound_frame(s, 0x5B3D0);
    CHECK(g_snd.bg_target == 53); /* 63 - 10: the first draw (37) is discarded */
    CHECK(s->rng_s == after3); /* two draws for the level, one for the r1 gate */
    P(40)[0x68] = 1; /* dead: target 1, no draw */
    s->rng_s = start;
    room_sound_frame(s, 0x5B3D0);
    CHECK(g_snd.bg_target == 1 && s->rng_s == after1);
    P(40)[0x68] = 0;
    sim_set_inputs(s, old);
    /* pathology: a pathologist (+0x0A), no case (+0x0C = 0); the gate's draw takes the pair 0x631B / 0x2051A7 */
    put16(rec(18, 0) + 0x0A, 5), put16(rec(18, 0) + 0x0C, 0);
    g_snd.ambience = 0; /* the picks make no draw: only the gate */
    s->rng_s = 0x13579BDF;
    uint32_t x = 0x13579BDF;
    sim_generator_step(&x, 0x631B, 0x2051A7);
    room_sound_frame(s, 0x370CD);
    CHECK(s->rng_s == x);
    CHECK(s->rng_in.on == 0); /* the frame restores the default inputs */
    /* with random states: queue 1 after a passed gate hits 3.0 % (the conditioned draw), not 1/64 */
    mix_state = 7;
    int q1 = 0, q2 = 0;
    const int N = 300000;
    for (int i = 0; i < N; i++) {
        memset(g_snd.queue, 0, sizeof g_snd.queue);
        g_snd.ambience = 1;
        s->rng_s = mix32();
        room_sound_frame(s, 0x370CD);
        q1 += g_snd.queue[0][0].id != 0;
        q2 += g_snd.queue[1][0].id != 0;
    }
    printf("[INFO] pathology without a case: %d / %d frames queue 1, %d queue 2 (uniform %d)\n", q1, N, q2, N / 16 / 64);
    CHECK(q1 > 470 && q1 < 660); /* 300,000 x 1/16 x 3.0 % = 562; uniform 293 */
    CHECK(q2 > 180 && q2 < 340); /* 1.4 %: 262 */
    /* with a case: no gate draw */
    put16(rec(18, 0) + 0x0C, 958);
    g_snd.ambience = 0;
    s->rng_s = 0x13579BDF;
    room_sound_frame(s, 0x370CD);
    CHECK(s->rng_s == 0x13579BDF);
    /* and the picks with the default inputs (fresh pairs), as a direct pick with the same generator states */
    int same = 0;
    for (int i = 0; i < 20; i++) {
        uint64_t aux = s->rng;
        uint32_t st = mix32();
        memset(g_snd.queue, 0, sizeof g_snd.queue);
        g_snd.ambience = 1, s->rng_s = st;
        room_sound_frame(s, 0x370CD);
        uint32_t via_frame = s->rng_s;
        memset(g_snd.queue, 0, sizeof g_snd.queue);
        g_snd.ambience = 1, s->rng_s = st, s->rng = aux;
        sound_ambience_pair(&g_snd, 0x543A);
        same += s->rng_s == via_frame;
    }
    CHECK(same == 20);
    s->rng_s = 0x13579BDF;
    /* no pathologist: nothing, no draw */
    put16(rec(18, 0) + 0x0A, 0);
    g_snd.ambience = 1;
    memset(g_snd.queue, 0, sizeof g_snd.queue);
    for (int i = 0; i < 20000; i++) room_sound_frame(s, 0x370CD);
    CHECK(s->rng_s == 0x13579BDF && !g_snd.queue[0][0].id);
    /* the ambience pick is one draw AND 63 (0x36F49): with the flag set and a fresh state it consumes exactly one draw
     * when it fails */
    s->rng_s = 0x2468ACE0;
    x = 0x2468ACE0;
    old = sim_set_inputs(s, sim_in(0x631B, 0x2051A7, 0));
    uint32_t t1 = sim_generator_step(&x, 0x631B, 0x2051A7);
    g_snd.ambience = 1;
    memset(g_snd.queue, 0, sizeof g_snd.queue);
    sound_ambience(&g_snd, 1, 0x543A);
    CHECK((t1 & 63) == 4 ? g_snd.queue[1][0].id != 0 : (s->rng_s == x && !g_snd.queue[1][0].id));
    sim_set_inputs(s, old);
    s->sound = NULL;
    sound_free(&g_snd);
}

/* the show sound: only when no show was running; without a patient the second entries are cleared */
static void test_show_sound(void) {
    snd_fresh();
    for (int k = 0; k < 8; k++)
        for (int i = 0; i < 6; i++) put16(g_image + 0x48AA + 48 * k + 6 * i, 0xDF);
    put16(R() + 8, 4); /* a clerk */
    CHECK(rooms_show(s, 2) == 0);
    CHECK(g_snd.queue[0][0].id == 0xDF && g_snd.queue[0][1].id == 0 && g_snd.queue[0][2].id == 0xDF);
    memset(g_snd.queue, 0, sizeof g_snd.queue);
    CHECK(rooms_show(s, 2) == 0); /* a show runs already: no sound */
    CHECK(g_snd.queue[0][0].id == 0);
    R()[0x45] = 0, put16(R() + 0x0A, 3); /* a patient at the desk */
    CHECK(rooms_show(s, 2) == 0);
    CHECK(g_snd.queue[0][1].id == 0xDF && g_snd.queue[1][1].id == 0xDF && g_snd.sticky);
    s->sound = NULL;
    sound_free(&g_snd);
}

static void test_reaction(void) {
    CHECK(rooms_reaction(3) == 8 && rooms_reaction(2) == 7 && rooms_reaction(1) == 6 && rooms_reaction(0) == 5);
    CHECK(rooms_reaction(-2) == 5 && rooms_reaction(-3) == 4 && rooms_reaction(-8) == 3 && rooms_reaction(-12) == 2);
    CHECK(rooms_reaction(-16) == 1 && rooms_reaction(-19) == 1 && rooms_reaction(-20) == 0);
}

/* ---- ward ---------------------------------------------------------------------------------------------- */

static void in_bed(int i, int id, int bed) {
    ill(i, 0, id);
    P(i)[3] = 0, P(i)[4] = 6, P(i)[43] = 6;
    put16(rec(6, 0) + 0x14 + 4 * bed, i);
    rec(6, 0)[12]++;
    rooms_bed_click(s, bed);
}

static void test_discharge(void) {
    clinic();
    RoomsResult o = {0};
    in_bed(5, 9, 8);
    CHECK(rooms_pb16(s, RB_PATIENT) == 5 && rooms_pb16(s, RB_PLACE) == 8);
    world_set_tail(&g_world, TAIL_HOUR, 21);
    CHECK(rooms_discharge(s, &o) == 2282);
    world_set_tail(&g_world, TAIL_HOUR, 3);
    CHECK(rooms_discharge(s, &o) == 2282);
    world_set_tail(&g_world, TAIL_HOUR, 12);
    P(5)[102] = 1;
    CHECK(rooms_discharge(s, &o) == 2279);
    P(5)[102] = 0, P(5)[44] = 4;
    CHECK(rooms_discharge(s, &o) == 2280);
    P(5)[44] = 50;
    P(5)[50] = 9, h0()[0x20] = 1; /* bill 4 + 9 = 13 */
    CHECK(rooms_discharge(s, &o) == 14 && o.bill == 13 && o.reaction == 8);
    CHECK(cash() == 984 + 13 && !get16(rec(6, 0) + 0x14 + 32) && rec(6, 0)[12] == 0);
    CHECK(P(5)[4] == 0xFF && get16(h0() + 0x2A) == 1 && P(5)[20] == 80 + 4 * 2 - 25); /* v 3 -> 2 from 75 on, four times; still ill: -25 */
}

static void test_step_and_icu(void) {
    clinic();
    RoomsResult o = {0};
    in_bed(5, 9, 2);
    rooms_step(s);
    CHECK(P(5)[93] == 0xFF && P(5)[45] == 2 && P(5)[105] == 10);
    rooms_step(s);
    CHECK(P(5)[93] == 0);
    CHECK(rooms_icu(s, &o) == 535); /* no intensive care */
    counts()[9] = 1;
    put16(rec(6, 0) + 8, 0);
    CHECK(rooms_icu(s, &o) == 536);
    put16(rec(6, 0) + 8, 24), P(5)[93] = 0xFF;
    CHECK(rooms_icu(s, &o) == 1340 && o.room == 0 && o.staff == 24);
    CHECK(P(5)[4] == 9 && P(5)[93] == 0 && get16(rec(9, 0) + 12) == 5 && get16(h0() + 0x56) == 1);
    CHECK(!get16(rec(6, 0) + 0x14 + 8) && rec(6, 0)[12] == 0);
}

static void test_corpse(void) {
    clinic();
    RoomsResult o = {0};
    in_bed(5, 9, 3);
    P(5)[104] = 1;
    CHECK(rooms_corpse(s, 6, &o) == 537); /* no pathology: reset at home */
    CHECK(P(5)[3] == 0xFF && !P(5)[104] && get16(h0() + 0x38) == 1 && get16(h0() + 0x3C) == 1);
    CHECK(!get16(rec(6, 0) + 0x14 + 12) && rec(6, 0)[12] == 0);
    counts()[18] = 1;
    uint8_t *path = rec(18, 0);
    put16(path + 8, 10), put16(path + 0x0A, 7);
    in_bed(6, 9, 1);
    P(6)[104] = 1;
    CHECK(rooms_corpse(s, 6, &o) == 1340 && o.staff == 10); /* +8 (the places), not the pathologist */
    CHECK(P(6)[4] == 18 && P(6)[66] == 6 && get16(path + 6) == 1 && get16(path + 0x58) == 6);
    put16(path + 6, 10);
    ill(7, 0, 7), at_desk(7), P(7)[104] = 1;
    ill(8, 0, 7), put16(R() + 14, 8);
    rooms_set_pb16(s, RB_PATIENT, 7);
    CHECK(rooms_corpse(s, 2, &o) == 538 && get16(R() + 10) == 8); /* full; the queue moved up */
}

static void test_clicks_and_sheet(void) {
    clinic();
    uint8_t flags[256] = {0};
    CHECK(rooms_seat(s, 3) == 0);
    waiting_patient(5, 0, 7, 3, 0);
    rooms_set_pb16(s, RB_PAGE, 2);
    CHECK(rooms_seat(s, 3) == 5 && rooms_pb16(s, RB_PATIENT) == 5 && rooms_pb16(s, RB_PLACE) == 3);
    CHECK(rooms_pb16(s, RB_PAGE) == 0);
    rooms_page_turn(s), rooms_page_turn(s);
    CHECK(rooms_pb16(s, RB_PAGE) == 2);
    rooms_page_turn(s);
    CHECK(rooms_pb16(s, RB_PAGE) == 0);
    rooms_sheet_flags(s, flags, 1);
    CHECK(flags[FB_ALIVE] == 0xFF && flags[FB_DEAD] == 0 && !pb()[RB_LOW_HEALTH] && !pb()[RB_DIED]);
    P(5)[44] = 20, P(5)[67] = 30; /* fell below 25 */
    rooms_sheet_flags(s, flags, 1);
    CHECK(pb()[RB_LOW_HEALTH] == 0xFF && !pb()[RB_DIED]);
    P(5)[104] = 1, P(5)[44] = 0, P(5)[67] = 20;
    rooms_sheet_flags(s, flags, 1);
    CHECK(flags[FB_ALIVE] == 0 && flags[FB_DEAD] == 0xFF && pb()[RB_DIED] == 0xFF);
    CHECK(rooms_couch(s) == 398);
    put16(rec(5, 0) + 12, 6);
    CHECK(rooms_couch(s) == 0 && rooms_pb16(s, RB_PATIENT) == 6);
    ill(7, 0, 9), put16(rec(6, 0) + 0x14 + 4, 7), P(7)[4] = 2; /* in a bed word but not in state 6 */
    CHECK(rooms_bed_click(s, 1) == 0);
    P(7)[4] = 6;
    CHECK(rooms_bed_click(s, 1) == 7 && rooms_pb16(s, RB_PLACE) == 1);
}

static void test_ward_lists(void) {
    clinic();
    counts()[6] = 3;
    rec(6, 0)[10] = 4, rec(6, 1)[10] = 4, rec(6, 2)[10] = 4;
    rec(6, 1)[12] = 4; /* full */
    rooms_set_pb16(s, RB_WARD, 0);
    rooms_bed_ward_step(s, 1);
    CHECK(rooms_pb16(s, RB_WARD) == 2); /* the full ward is skipped */
    rooms_bed_ward_step(s, 1);
    CHECK(rooms_pb16(s, RB_WARD) == 0);
    rooms_bed_ward_step(s, -1);
    CHECK(rooms_pb16(s, RB_WARD) == 2);
    counts()[6] = counts()[16] = 0;
    CHECK(rooms_occupancy_open(s) == 2371);
    counts()[16] = 2;
    CHECK(rooms_occupancy_open(s) == 0 && rooms_pb16(s, RB_QUEUE) == -1 && rooms_pb16(s, RB_WARD) == 0);
    counts()[6] = 2;
    CHECK(rooms_occupancy_open(s) == 0 && rooms_pb16(s, RB_QUEUE) == 0);
    rooms_occupancy_step(s, 1), rooms_occupancy_step(s, 1);
    CHECK(rooms_pb16(s, RB_QUEUE) == -1 && rooms_pb16(s, RB_WARD) == 0); /* the Krankenzimmer, then the neuro wards */
    rooms_occupancy_step(s, -1);
    CHECK(rooms_pb16(s, RB_QUEUE) == 0 && rooms_pb16(s, RB_WARD) == 1);
}

/* ---- the wrong guess of the diagnoses (docs/sim-rooms.md, "The wrong guess of the diagnoses") --------------------- */

/* the spec's rule on a twin generator: 600 tries of random(70) + 1 and r3; sixteen ids whose table byte is the ailment,
 * else the real illness; then at most S + 1 pairs of r15, stopping on equal ids; the guess is the last pair's first */
static int guess_ref(Sim *t, const uint8_t *tab, size_t len, int ailment, int real, int skill, int *pairs) {
    int list[16], found = 0;
    for (int k = 0; k < 600 && found < 16; k++) {
        int id = (int)sim_random(t, 70) + 1, col = (int)sim_bits(t, 3);
        if ((size_t)(4 * id + col) < len && tab[4 * id + col] == ailment) list[found++] = id;
    }
    *pairs = 0;
    if (found < 16) return real;
    int a = 0;
    for (int left = skill + 1; left > 0; left--) {
        a = (int)sim_bits(t, 15);
        int b = (int)sim_bits(t, 15);
        ++*pairs;
        if (list[a] == list[b]) break;
    }
    return list[a];
}

/* a room record whose doctor (+10 = staff 30) has skill S (0x2D62D): the doctor's byte 2 = S / 4 gives 4 x that, the
 * room's byte 1 = 4 x (S mod 4) the rest; the nurse (+8 = staff 31) is a different, unskilled employee */
static const uint8_t *skill_room(int skill) {
    static uint8_t room[64];
    memset(room, 0, sizeof room);
    memset(rooms_staff(s, 30), 0, 32), memset(rooms_staff(s, 31), 0, 32);
    put16(room + 8, 31), put16(room + 10, 30);
    rooms_staff(s, 30)[2] = (uint8_t)(skill / 4);
    room[1] = (uint8_t)(4 * (skill % 4));
    return room;
}

static int same_stream(Sim *a, Sim *b) { return sim_bits(a, 31) == sim_bits(b, 31) && sim_bits(a, 127) == sim_bits(b, 127); }

static void test_wrong_guess(void) {
    static uint8_t tab[71 * 4];
    uint8_t p[106] = {0};
    p[41] = 9, p[56] = 7; /* ailment code 9, real illness 7 */
    /* a rare ailment: one table place of 280 - the list never fills, the real illness after exactly 600 tries */
    fresh();
    memset(tab, 1, sizeof tab);
    tab[4 * 33 + 2] = 9;
    sim_set_ailments(s, tab, sizeof tab);
    for (int n = 0; n < 50; n++) {
        Sim twin = g_sim;
        for (int k = 0; k < 600; k++) sim_random(&twin, 70), sim_bits(&twin, 3);
        CHECK(sim_wrong_guess(s, p, skill_room(40)) == 7);
        CHECK(same_stream(&twin, s));
    }
    /* ids 36-70 hold the ailment at every place: the list fills, every guess is one of them, id 70 among them (the
     * ids are random(70) + 1); the ailment only at id 0's places is never found */
    memset(tab, 1, sizeof tab);
    memset(tab + 4 * 36, 9, 4 * 35);
    sim_set_ailments(s, tab, sizeof tab);
    int seen70 = 0, low = 0;
    for (int n = 0; n < 400; n++) {
        int g = sim_wrong_guess(s, p, skill_room(3));
        seen70 += g == 70, low += g < 36;
    }
    CHECK(seen70 > 0 && low == 0);
    memset(tab, 1, sizeof tab);
    memset(tab, 9, 4);
    sim_set_ailments(s, tab, sizeof tab);
    CHECK(sim_wrong_guess(s, p, skill_room(5)) == 7);
    /* the pair pick against the twin, over seeds and skills (0, 1, 7, 60): four ids at uneven place counts (22 of 280
     * places, about 47 matches in 600 tries), so lists of unequal ids and repeated ones */
    memset(tab, 1, sizeof tab);
    memset(tab + 4 * 11, 9, 4 * 3), memset(tab + 4 * 23, 9, 4 * 2), tab[4 * 40 + 1] = 9, memset(tab + 4 * 61, 9, 4);
    tab[4 * 62] = tab[4 * 62 + 3] = 9, tab[4 * 63 + 2] = 9;
    sim_set_ailments(s, tab, sizeof tab);
    int hist[71] = {0}, multi = 0, bound = 0, single = 0;
    for (int n = 0; n < 400; n++) {
        sim_seed(s, 900u + (uint64_t)n);
        int skill = n % 4 == 0 ? 0 : n % 4 == 1 ? 1 : n % 4 == 2 ? 7 : 60, pairs;
        Sim twin = g_sim;
        int want = guess_ref(&twin, tab, sizeof tab, 9, 7, skill, &pairs), got = sim_wrong_guess(s, p, skill_room(skill));
        CHECK(got == want && same_stream(&twin, s));
        CHECK(pairs >= 1 && pairs <= skill + 1);
        hist[got]++, multi += pairs > 1, bound += pairs == skill + 1 && skill > 0, single += !skill && pairs == 1;
    }
    CHECK(hist[11] > hist[40] && hist[7] == 0 && multi > 0 && bound > 0 && single == 100);
    /* seven places (about fifteen matches in 600 tries): a run that ends with exactly fifteen gives the real illness,
     * one with sixteen a guess */
    memset(tab, 1, sizeof tab);
    memset(tab + 4 * 12, 9, 4), memset(tab + 4 * 50, 9, 3);
    sim_set_ailments(s, tab, sizeof tab);
    int fifteen = 0, sixteen = 0;
    for (int n = 0; n < 3000 && (fifteen < 3 || sixteen < 3); n++) {
        sim_seed(s, 7000u + (uint64_t)n);
        Sim twin = g_sim;
        int found = 0;
        for (int k = 0; k < 600 && found < 16; k++) {
            int id = (int)sim_random(&twin, 70) + 1, col = (int)sim_bits(&twin, 3);
            found += tab[4 * id + col] == 9;
        }
        if (found < 15) continue;
        int got = sim_wrong_guess(s, p, skill_room(2));
        if (found == 15) {
            fifteen++;
            CHECK(got == 7);
        } else {
            sixteen++;
            CHECK(got == 12 || got == 50);
        }
    }
    CHECK(fifteen >= 3 && sixteen >= 3);
}

/* ---- the rename of the dead (docs/sim-rooms.md, "The rename of the dead, 0x3200C") ------------------------------- */

/* Synthetic stock texts: entry 4012 + n (n = 3-12) holds sixteen names of n bytes, name k all of the letter 'a' + k
 * (men 'a'-'h', women 'i'-'p'). Person i's name: a first word of 3 + i % 10 bytes "Q_qqq..." and " Doe". */
static uint8_t g_names[1009 * 24];
static uint8_t g_table[1009 * 4];
static DataDb g_people;
static uint8_t g_textblock[4096];
static TextDb g_texts;

static void names(void) {
    for (int i = 0; i < 1009; i++) {
        int n = 3 + i % 10;
        uint8_t *e = g_names + 24 * i;
        memset(e, 0, 24);
        memset(e, 'q', (size_t)n);
        e[0] = 'Q', e[1] = '_';
        memcpy(e + n, " Doe$", 5);
        put32(g_table + 4 * i, 24u * i);
    }
    memset(&g_people, 0, sizeof g_people);
    g_people.pools[0] = (TextPool){g_names, sizeof g_names, g_table, 1009};
    size_t at = 0;
    for (int i = 0; i < TEXT_COUNT; i++) {
        g_texts.offs[i] = (uint32_t)at;
        int n = i - 4012;
        if (n < 3 || n > 12) continue;
        for (int k = 0; k < 16; k++) memset(g_textblock + at + (size_t)(n * k), 'a' + k, (size_t)n);
        at += (size_t)(16 * n);
        g_textblock[at++] = '$';
    }
    g_texts.text = g_textblock;
    g_texts.text_size = (uint32_t)at;
    s->people = &g_people;
    s->texts = &g_texts;
}

static int name_is(int i, char letter) { /* the first word all `letter`, the rest unchanged */
    size_t len;
    const uint8_t *e = datadb_pool_entry(&g_people, DAT_PATIENT_NAMES, i, &len);
    int n = 3 + i % 10;
    if (len != (size_t)n + 4 || memcmp(e + n, " Doe", 4)) return 0;
    for (int k = 0; k < n; k++)
        if (e[k] != letter) return 0;
    return 1;
}

static int untouched(int i) { /* still "Q_qq... Doe" */
    size_t len;
    const uint8_t *e = datadb_pool_entry(&g_people, DAT_PATIENT_NAMES, i, &len);
    int n = 3 + i % 10;
    return len == (size_t)n + 4 && e[0] == 'Q' && e[1] == '_' && e[n - 1] == 'q' && !memcmp(e + n, " Doe", 4);
}

static void test_rename(void) {
    fresh();
    names();
    for (int i = 100; i < 120; i++) { /* every length 3-12, a man and a woman each */
        P(i)[0] = i >= 110;
        Sim twin = g_sim;
        sim_bits(&twin, 7); /* the r7 is drawn and does nothing */
        sim_rename_dead(s, i);
        CHECK(name_is(i, i >= 110 ? 'i' : 'a'));
        CHECK(same_stream(&twin, s));
        sim_rename_dead(s, i); /* a second death writes the same */
        CHECK(name_is(i, i >= 110 ? 'i' : 'a'));
    }
    CHECK(untouched(121));
    /* the death paths rename: the corpse found in a bed without a pathology (0x522D2) */
    clinic();
    names();
    RoomsResult o = {0};
    in_bed(5, 9, 3);
    P(5)[104] = 1;
    CHECK(rooms_corpse(s, 6, &o) == 537 && name_is(5, 'a'));
    /* the player's pathology: the case thrown away (0x52452) */
    counts()[18] = 1;
    put16(rec(18, 0) + 0x0C, 16), P(16)[0] = 1;
    sim_player_pathology_throw(s, 0);
    CHECK(name_is(16, 'i') && P(16)[3] == 0xFF);
    /* the AI pathology operator with a finished case (0x52457) */
    room_counts(s, 15)[18] = 1;
    uint8_t *path = sim_room_record(s, 15, 18, 0);
    put16(path + 0x0A, 40), put16(path + 0x0C, 27), put16(path + 0x0E, 999);
    world_set_tail(&g_world, TAIL_HOURS, 10);
    sim_call(s, 0x5A268);
    CHECK(name_is(27, 'a') && !get16(path + 0x0C));
    /* without the data nothing is renamed, the r7 is still drawn */
    s->people = NULL;
    Sim twin = g_sim;
    sim_bits(&twin, 7);
    sim_rename_dead(s, 38);
    CHECK(untouched(38) && same_stream(&twin, s));
}

/* SIM-20 (docs/sim-drift8.md, "What the engine must change", item 6; section 20.2): the dental chooser 0x349A9 draws an
 * r1 after r7 non-zero and r127 not below S in a hospital other than 0, and a non-zero r1 gives the best treatment;
 * the treatment room and the neurology have no r1, nor has hospital 0. Draws scripted: a generator state whose next
 * draws under fixed inputs (d, e) give the listed values (masks 7, 127, 1, 3, 63); the inputs vary with the state, as
 * one pair cannot give every sequence of low bits. S = 60 (doctor byte 2 = 15). Illness 10:
 * dental (type 1, byte 15 = 3), treatments 11-14; illness 12: physical (type 1, byte 15 = 0), treatments 21-24. */
static uint32_t sim20_state(const uint32_t *mask, const uint32_t *val, int n, uint32_t *after, uint32_t *d,
                            uint32_t *e) {
    for (uint32_t c = 1; c != 0; c++) {
        uint32_t x = c * 2654435761u, st = x;
        *d = (c * 7919u) & 0xFFFFu, *e = c * 40503u * 65537u;
        int i = 0;
        while (i < n && (sim_generator_step(&x, *d, *e) & mask[i]) == val[i]) i++;
        if (i == n) {
            *after = x;
            return st;
        }
    }
    return 0;
}

/* a fresh world with the doctor 22 (S = 60), nurse 23 and patient 5 diagnosed in room 0 of category cat of hospital h */
static void sim20_room(int h, int cat) {
    fresh();
    uint8_t *d = rooms_staff(s, 22);
    memset(d, 0, 32);
    d[2] = 15; /* skill (15 x 8) / 2 = 60 */
    ILL(10)[8] = 1, ILL(10)[15] = 3, ILL(10)[11] = 11, ILL(10)[12] = 12, ILL(10)[13] = 13, ILL(10)[14] = 14;
    ILL(12)[8] = 1, ILL(12)[15] = 0, ILL(12)[11] = 21, ILL(12)[12] = 22, ILL(12)[13] = 23, ILL(12)[14] = 24;
    int id = cat == 11 ? 10 : 12;
    ill(5, 1, id);
    P(5)[42] = (uint8_t)id, P(5)[43] = 0;
    uint8_t *t = sim_room_record(s, h, cat, 0);
    t[1] = 0;
    put16(t + 8, 23), put16(t + 10, 22), put16(t + 12, 5);
    sim_rating_record(s, t); /* this skill-60 fixture needs rating 0, not the initial null rating 110 (SIM-82) */
}

/* mode 0: sim_ai_choose (AI mode), 1: sim_player_propose_in (player mode) */
static void sim20_case(int h, int cat, int mode, const uint32_t *mask, const uint32_t *val, int n, int expect) {
    sim20_room(h, cat);
    uint32_t after = 0, de = 0, ee = 0, start = sim20_state(mask, val, n, &after, &de, &ee);
    CHECK(start != 0);
    SimIn old = sim_set_inputs(s, sim_in(de, ee, 0));
    s->rng_s = start;
    if (mode) sim_player_propose_in(s, h, cat, 0);
    else sim_ai_choose(s, h, cat, 0);
    sim_set_inputs(s, old);
    if (P(5)[43] != expect || s->rng_s != after)
        fprintf(stderr, "  SIM-20 case h %d cat %d mode %d: treatment %d (want %d), draws %s\n", h, cat, mode, P(5)[43],
                expect, s->rng_s == after ? "as scripted" : "differ");
    CHECK(P(5)[43] == expect);
    CHECK(s->rng_s == after); /* exactly the scripted draws, no more and no fewer */
}

static void test_sim20_dental_choice(void) {
    /* (a) r7 3, r127 70, r1 1: byte 11, three draws, no helper draw */
    const uint32_t ma[] = {7, 127, 1}, va[] = {3, 70, 1};
    sim20_case(4, 11, 0, ma, va, 3, 11);
    /* (b) r7 3, r127 70, r1 0, helper r3 2, r63 10, r127 30: offset 0, byte 11, six draws */
    const uint32_t mb[] = {7, 127, 1, 3, 63, 127}, vb[] = {3, 70, 0, 2, 10, 30};
    sim20_case(4, 11, 0, mb, vb, 6, 11);
    /* (c) r7 3, r127 70, r1 0, r3 1, r63 61, r1 1, r1 1, r1 1: offset 1, byte 12, eight draws */
    const uint32_t mc[] = {7, 127, 1, 3, 63, 1, 1, 1}, vc[] = {3, 70, 0, 1, 61, 1, 1, 1};
    sim20_case(4, 11, 0, mc, vc, 8, 12);
    /* (d) r7 0: no r127 and no r1, the helper next: r3 2, r63 61, r1 1, r1 1, r1 1: offset 1, byte 12, six draws */
    const uint32_t md[] = {7, 3, 63, 1, 1, 1}, vd[] = {0, 2, 61, 1, 1, 1};
    sim20_case(4, 11, 0, md, vd, 6, 12);
    /* (e) r7 3, r127 40: byte 11, two draws */
    const uint32_t me[] = {7, 127}, ve[] = {3, 40};
    sim20_case(4, 11, 0, me, ve, 2, 11);
    /* (f) the draws of (a) in hospital 0: no r1, the third draw (1) is the helper's r3; then r63 61, r1 1, r1 1, r1 1
     * give offset 1, byte 12, seven draws (the module-D AI branch); in player mode the r3 is the rank: byte 12 */
    const uint32_t mf[] = {7, 127, 3, 63, 1, 1, 1}, vf[] = {3, 70, 1, 61, 1, 1, 1};
    sim20_case(0, 11, 0, mf, vf, 7, 12);
    sim20_case(0, 11, 1, mf, vf, 3, 12);
    /* player mode in another hospital: the r1 is drawn there too (it follows [0xB054], not the mode) */
    sim20_case(4, 11, 1, ma, va, 3, 11);
    /* (g) the treatment room (5) and the neurology (15) with the draws of (a): no r1, the third draw is the helper's r3 */
    sim20_case(4, 5, 0, mf, vf, 7, 22);
    sim20_case(4, 15, 0, mf, vf, 7, 22);

    /* the AI operator's own path (room_operator -> the diagnosis -> the choice) passes the room's hospital: with free
     * draws the dental best-treatment share in hospital 4 is above hospital 0's by about 7/8 x 68/128 x 1/2 x (1 - the
     * helper's offset-0 share), about 0.12 at S = 60 */
    double share[2];
    for (int side = 0; side < 2; side++) {
        int h = side ? 0 : 4, best = 0, n = 0;
        for (int k = 1; k <= 3000; k++) {
            sim20_room(h, 11);
            uint8_t *t = sim_room_record(s, h, 11, 0);
            put16(t + 14, 0), put16(t + 16, 0), put16(t + 18, 0);
            s->rng_s = (uint32_t)k * 2654435761u;
            sim_rooms_after_operator(s, h, 11, 0);
            n += P(5)[42] == 10;
            best += P(5)[42] == 10 && P(5)[43] == 11;
        }
        share[side] = n ? (double)best / n : 0;
    }
    printf("[INFO] SIM-20 operator: dental best-treatment share hospital 4 %.3f, hospital 0 %.3f\n", share[0], share[1]);
    CHECK(share[0] - share[1] > 0.06);
}

/* FIX-3 (docs/fixes.md): the disk build's refusal names the u16 at T+0x13 (off) or the doctor at T+0x0A (on); the
 * German CD names the doctor in both modes */
static void test_fix3_refusal_name(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 2 : 0);
        clinic();
        RoomsResult o = {0};
        uint8_t *t = rec(5, 0);
        diagnosed(5, 7, 3);
        put16(t + 10, 5);
        rooms_staff(s, 5)[11] = 1, put16(t + 0x13, 42);
        CHECK(rooms_other(s, &o) == 42 && o.staff == (on ? 5 : 42));
        CHECK(rooms_accept(s, &o) == 42 && o.staff == (on ? 5 : 42));
        g_prog.edition = EDITION_GERMAN_CD;
        CHECK(rooms_other(s, &o) == 42 && o.staff == 5);
        g_prog.edition = EDITION_DISK;
    }
    fixes_set(0);
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "administration-gate")) {
        test_administration_gate();
        sound_free(&g_snd);
        world_free(&g_world);
        if (failures) return 1;
        puts("[OK] administration sound gate and continuation");
        return 0;
    }
    test_fix3_refusal_name();
    test_clicks_and_sheet();
    test_ward_lists();
    test_site_click();
    test_selector();
    test_desk();
    test_throw_out();
    test_admit_refusals();
    test_admit_seated();
    test_bed_assignment();
    test_overview();
    test_shows();
    test_show_scratch_byte();
    test_call_next();
    test_player_diagnosis();
    test_other_proposal();
    test_accept_in_room();
    test_accept_psychological();
    test_accept_bed();
    test_bill();
    test_abort();
    test_inventory();
    test_reaction();
    test_discharge();
    test_step_and_icu();
    test_corpse();
    test_wrong_guess();
    test_rename();
    test_room_sound_views();
    test_waiting_sound();
    test_room_sound_frames();
    test_empty_ward_sound_gate();
    test_administration_gate();
    test_snd11_draws();
    test_show_sound();
    test_sim20_dental_choice();
    world_free(&g_world);
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] all checks passed\n");
    return 0;
}
