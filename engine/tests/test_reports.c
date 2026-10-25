/* The daily report, the work advice and the newspaper texts (docs/ui-reports.md). Synthetic world and program image
 * (see AGENTS.md): the room descriptions, catalog entries, treatments and illnesses are made up so that each rule can be
 * reached; the report cases repeat the spec's oracle plantings (sections 1, "Checked with the oracle"). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fixes.h"
#include "advice.h"
#include "newspaper.h"
#include "report.h"
#include "reports_ui.h"
#include "sim_internal.h"
#include "text.h"

static int failures;
#define CHECK(cond)                                                                                                     \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);                                          \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static World g_world;
static Program g_prog;
static uint8_t g_image[0x10000];
static Sim g_sim;
static Sim *s = &g_sim;
static Advice g_adv;

#define DESCRIPTIONS 0x4758
#define FURNITURE_CATALOG 0xEE0
#define EQUIPMENT_CATALOG 0x144A
#define TREATMENTS 0x270C
#define ILLNESSES 0x954
#define NAMES 0x3BCF
/* the made-up room layout: furniture list +0x60 (4 slots), equipment +0x68 (2 slots), conditions +0x6C / +0x70, the
 * room type +0x5E */
enum { FURN = 0x60, EQ = 0x68, TYPE = 0x5E };

static uint8_t *H(int h) { return world_hospital(&g_world, h); }
static uint8_t *pb(void) { return g_world.block[BLK_PLAYER0]; }
static uint8_t *site(void) { return g_world.block[BLK_ROOM_COUNTS]; }
static uint8_t *R(int cat, int i) { return sim_room_record(s, 0, cat, i); }
static uint8_t *P(int i) { return g_world.block[BLK_PATIENTS] + i * 106; }
static uint8_t *ST(int i) { return g_world.block[BLK_STAFF] + i * 32; }
static uint8_t *SL(int i) { return staff_slots(s, 0) + i * STAFF_SLOT_SIZE; }
static uint8_t *store_rec(void) { return R(4, 0); }
static int u16(const uint8_t *p) { return (uint16_t)get16(p); }

static void eq_item(int id, int kind, int cap) {
    uint8_t *it = g_image + EQUIPMENT_CATALOG + id * 20;
    it[6] = (uint8_t)kind, it[10] = (uint8_t)cap;
}
static void fu_item(int id, int kind, int cls) {
    uint8_t *it = g_image + FURNITURE_CATALOG + id * 18;
    it[6] = (uint8_t)kind, it[7] = (uint8_t)cls;
}
static void time_at(int month, int day, int hour) {
    world_set_tail(&g_world, TAIL_MONTH, month);
    world_set_tail(&g_world, TAIL_DAY, day);
    world_set_tail(&g_world, TAIL_HOUR, hour);
}

static void fresh(void) {
    world_free(&g_world);
    world_alloc(&g_world);
    memset(g_world.block[BLK_ROOM_SLOTS], 0xFF, g_world.size[BLK_ROOM_SLOTS]);
    memset(g_image, 0, sizeof g_image);
    g_prog.image.data = g_image;
    g_prog.image.size = sizeof g_image;
    sim_init(&g_sim, &g_world, &g_prog, 9);
    for (int cat = 0; cat < 19; cat++) {
        uint8_t *d = g_image + DESCRIPTIONS + 14 * cat;
        put16(d + 0, FURN), put16(d + 2, 4), put16(d + 4, EQ), put16(d + 6, 2);
        put16(d + 8, 0x6C), put16(d + 10, 0x70), put16(d + 12, TYPE);
    }
    memset(g_world.block[BLK_EQUIPMENT_MARKET], 0xFF, g_world.size[BLK_EQUIPMENT_MARKET]);
    memset(g_world.block[BLK_FURNITURE_MARKET], 0xFF, g_world.size[BLK_FURNITURE_MARKET]);
    memset(g_world.block[BLK_MARKET], 0, g_world.size[BLK_MARKET]);
    for (int i = 0; i < 100; i++) sim_market(s, i)[0] = 0xFF;
    memset(g_world.block[BLK_JOB_OPENINGS], 0xFF, g_world.size[BLK_JOB_OPENINGS]);
    for (int i = 1; i < 1041; i++) ST(i)[3] = 0xFF, ST(i)[27] = 0xFF; /* nobody employed, nobody applying */
    put32(H(0) + H_CASH, 984);
    time_at(3, 5, 12);
}

static void hire(int slot, int index, int group, int state, int mood) {
    put16(SL(slot), index);
    SL(slot)[2] = (uint8_t)group, SL(slot)[3] = (uint8_t)state;
    ST(index)[1] = (uint8_t)group, ST(index)[3] = 0, ST(index)[4] = (uint8_t)state, ST(index)[21] = (uint8_t)mood;
}
static void applicant_for(int index, int group, int opening_slot) {
    uint8_t *j = g_world.block[BLK_JOB_OPENINGS] + opening_slot * JOB_OPENING_SIZE;
    j[0] = 0, j[1] = (uint8_t)group;
    ST(index)[1] = (uint8_t)group, ST(index)[3] = 0xFF, ST(index)[27] = (uint8_t)opening_slot;
}
static void store_equipment(int id) {
    uint8_t *st = store_rec();
    put16(st + STORE_EQUIPMENT + 2 * st[STORE_EQUIPMENT_HELD], id);
    st[STORE_EQUIPMENT_HELD]++;
}
static void store_furniture(int id) {
    uint8_t *st = store_rec();
    put16(st + STORE_FURNITURE + 2 * st[STORE_FURNITURE_HELD], id);
    st[STORE_FURNITURE_HELD]++;
}
static void market(int furniture, int i, int id) {
    uint8_t *m = g_world.block[furniture ? BLK_FURNITURE_MARKET : BLK_EQUIPMENT_MARKET] + 8 * i;
    m[0] = 16, put16(m + 2, id), m[5] = 0;
}
static void clerk(int index, int independent) {
    site()[4] = 1;
    hire(150, index, 3, 4, 50);
    put16(store_rec() + STORE_CLERK, index);
    ST(index)[11] = (uint8_t)(independent ? 0xFF : 0);
}

static int walk_at(int entry, int line) {
    int a = advice_walk(s, entry, &g_adv);
    if (getenv("ADV_DEBUG")) fprintf(stderr, "  line %d: advice 0x%X text %d\n", line, (unsigned)a, g_adv.text.n ? g_adv.text.p[0].v : -1);
    return a;
}
#define walk(e) walk_at(e, __LINE__)
static int text0(void) { return g_adv.text.n ? g_adv.text.p[0].v : -1; }
static int has_text(int n) {
    for (int i = 0; i < g_adv.text.n; i++)
        if (g_adv.text.p[i].kind == PC_TEXT && g_adv.text.p[i].v == n) return 1;
    return 0;
}

/* ---- the daily report ---------------------------------------------------------------------------------- */

static void test_baseline(void) {
    fresh();
    time_at(0, 0, 0); /* the score's time terms out of the way */
    put32(H(0) + H_CASH, 100), put32(H(0) + 0x152, 50), put32(pb() + RP_WORTH_BASE, 120);
    put16(H(0) + 0x26, 10), put16(pb() + RP_DISCHARGED_BASE, 4);
    put16(H(0) + 0x28, 50), put16(pb() + RP_CURED_BASE, 52); /* a counter that fell: -2 */
    put16(H(0) + 0x2A, 3);
    put16(H(0) + 0x38, 5), put16(pb() + RP_DEATHS_BASE, 2);
    put16(H(3) + 0x28, 500); /* hospital 3 ranks first */
    put32(pb() + RP_RANK, 5), put32(pb() + RP_SCORE, 7);
    report_baseline(s, 0);
    CHECK(get32(pb() + RP_WORTH) == 30 && get32(pb() + RP_WORTH_BASE) == 150);
    CHECK(get16(pb() + RP_DISCHARGED) == 6 && get16(pb() + RP_DISCHARGED_BASE) == 10);
    CHECK(get16(pb() + RP_CURED) == -2 && get16(pb() + RP_CURED_BASE) == 50);
    CHECK(get16(pb() + RP_UNCURED) == 3 && get16(pb() + RP_UNCURED_BASE) == 3);
    CHECK(get16(pb() + RP_DEATHS) == 3 && get16(pb() + RP_DEATHS_BASE) == 5);
    int32_t score = get32(H(0) + 0x44);
    CHECK(score > 0 && get32(pb() + RP_SCORE) == score && get32(pb() + RP_SCORE_CHANGE) == score - 7);
    CHECK(get32(pb() + RP_RANK) == 2 && get32(pb() + RP_RANK_CHANGE) == 3);
    /* the day change: both players, the flags, the days played */
    fresh();
    report_day_change(s);
    CHECK(pb()[RP_ARMED] == 0xFF && pb()[RP_REDRAW] == 0xFF);
    CHECK(g_world.block[BLK_PLAYER1][RP_ARMED] == 0xFF && g_world.block[BLK_PLAYER1][RP_REDRAW] == 0xFF);
    CHECK(get16(H(0) + H_DAYS_PLAYED) == 1 && get16(H(1) + H_DAYS_PLAYED) == 1 && get16(H(2) + H_DAYS_PLAYED) == 0);
    put32(H(1) + H_CASH, 40);
    report_day_change(s);
    CHECK(get16(H(0) + H_DAYS_PLAYED) == 2 && get32(g_world.block[BLK_PLAYER1] + RP_WORTH) == 40);
    /* the first day: the whole net worth is the day's gain, a rank "drop" to place r */
    fresh();
    time_at(0, 0, 0);
    put16(H(3) + 0x28, 500);
    report_baseline(s, 0);
    CHECK(get32(pb() + RP_WORTH) == 984 && get32(pb() + RP_RANK_CHANGE) == -2);
}

/* SAV-1 (docs/mainloop.md section 1, "What the engine must change" 3): the new game's two baseline calls for player 0,
 * then the expected cash of all 16 hospitals */
static void test_new_game_baseline(void) {
    fresh();
    time_at(0, 0, 0);
    put32(H(0) + H_CASH, 1234), put32(H(0) + 0x152, 66);
    put16(H(3) + 0x28, 500); /* hospital 3 ranks first */
    for (int h = 0; h < 16; h++) put32(H(h) + 0xE2, 0x7777);
    static uint8_t b29[0x200];
    memcpy(b29, g_world.block[BLK_PLAYER1], g_world.size[BLK_PLAYER1]);
    report_new_game(s);
    CHECK(get32(pb() + RP_WORTH_BASE) == 1300 && get32(pb() + RP_WORTH) == 0);
    CHECK(get32(pb() + RP_RANK) == 2 && get32(pb() + RP_RANK_CHANGE) == 0); /* hospital 0 second after 3 */
    int32_t score = get32(H(0) + 0x44);
    CHECK(get32(pb() + RP_SCORE) == score && get32(pb() + RP_SCORE_CHANGE) == 0);
    CHECK(memcmp(b29, g_world.block[BLK_PLAYER1], g_world.size[BLK_PLAYER1]) == 0); /* block 29 untouched */
    for (int h = 0; h < 16; h++) CHECK(get32(H(h) + 0xE2) == sim_expected_cash(s, h));
    CHECK(get32(H(0) + 0xE2) != get32(H(5) + 0xE2)); /* the cash differs: hospital 0's own value, not one copied */
}

static void opinions(int n0, int v0, int n1, int v1, int rest) {
    for (int i = 1; i < 1009; i++) P(i)[20] = (uint8_t)(i <= n0 ? v0 : i <= n0 + n1 ? v1 : rest);
}
static void nurse(int bust, int age, int b6, int b7) {
    hire(0, 40, 1, 0, 50);
    ST(40)[5] = (uint8_t)bust, ST(40)[8] = (uint8_t)age, ST(40)[6] = (uint8_t)b6, ST(40)[7] = (uint8_t)b7;
}
static void plant(int32_t worth, int32_t rank_change, int32_t score_change, int d, int c, int u, int deaths) {
    put32(pb() + RP_WORTH, worth), put32(pb() + RP_RANK_CHANGE, rank_change), put32(pb() + RP_RANK, 5);
    put32(pb() + RP_SCORE_CHANGE, score_change), put32(pb() + RP_SCORE, 300);
    put16(pb() + RP_DISCHARGED, d), put16(pb() + RP_CURED, c), put16(pb() + RP_UNCURED, u), put16(pb() + RP_DEATHS, deaths);
}
static int ptext(const Pieces *t, int n) {
    for (int i = 0; i < t->n; i++)
        if (t->p[i].kind == PC_TEXT && t->p[i].v == n) return 1;
    return 0;
}

static void test_report(void) {
    Report r;
    /* the spec's three plantings */
    fresh();
    plant(30, 3, 5, 60, 40, 2, 3);
    nurse(125, 21, 70, 70);
    opinions(0, 0, 0, 0, 49);
    pb()[RP_ARMED] = 0xFF;
    report_build(s, &r);
    CHECK(pb()[RP_ARMED] == 0);
    CHECK(r.points[0] == 6 && r.points[1] == 1 && r.points[2] == 7 && r.points[3] == 1);
    CHECK(r.face[0] == 1291 && r.face[1] == 1297 && r.face[2] == 1293 && r.face[3] == 1304);
    CHECK(r.total == 15 && r.overall == 1300 && r.sound == 1);
    CHECK(ptext(&r.para[0], 1677) && ptext(&r.para[0], 1679) && ptext(&r.para[0], 1687));
    CHECK(ptext(&r.para[1], 1697) && ptext(&r.para[1], 1707) && ptext(&r.para[1], 1709) && ptext(&r.para[1], 1712));
    CHECK(ptext(&r.para[2], 1719) && ptext(&r.para[2], 1731) && r.para[2].p[1].v == 125);
    CHECK(get16(H(0) + 0x4A) == 125 && H(0)[0x113] == 21);
    fresh();
    plant(-40, -1, 1, 1, 1, 1, 1);
    nurse(85, 30, 85, 85);
    opinions(0, 0, 0, 0, 49);
    report_build(s, &r);
    CHECK(r.total == 0 && r.points[1] == -1 && r.overall == 1305 && r.sound == 2);
    CHECK(ptext(&r.para[0], 1674) && r.para[0].p[1].v == 40 && ptext(&r.para[0], 1685) && ptext(&r.para[0], 1690));
    CHECK(ptext(&r.para[1], 1695) && ptext(&r.para[1], 1700) && ptext(&r.para[1], 1705) && ptext(&r.para[1], 1715));
    CHECK(ptext(&r.para[2], 1723) && ptext(&r.para[2], 1726) && r.face[2] == 1303);
    fresh();
    plant(24, 1, -1, 45, 0, 5, 0);
    nurse(100, 23, 73, 73);
    opinions(0, 0, 0, 0, 49);
    report_build(s, &r);
    CHECK(r.total == 10 && r.points[0] == 3 && r.points[1] == 2 && r.points[2] == 4);
    CHECK(ptext(&r.para[0], 1676) && ptext(&r.para[0], 1682) && ptext(&r.para[0], 1693));
    CHECK(ptext(&r.para[1], 1701) && ptext(&r.para[1], 1703) && ptext(&r.para[1], 1714) && ptext(&r.para[2], 1721));
    CHECK(ptext(&r.para[2], 1730) && r.face[0] == 1296 && r.face[1] == 1297 && r.face[2] == 1293);
    /* the praiser quirk: the bonus counts the haters */
    fresh();
    plant(0, 0, 0, 0, 0, 0, 0);
    opinions(35, 0, 5, 100, 50);
    report_build(s, &r);
    CHECK(r.points[3] == 0 && ptext(&r.para[3], 1734) && ptext(&r.para[3], 1738));
    fresh();
    plant(0, 0, 0, 0, 0, 0, 0);
    opinions(1, 0, 65, 100, 50);
    report_build(s, &r);
    CHECK(r.points[3] == 3 && ptext(&r.para[3], 1737) && ptext(&r.para[3], 1738));
    fresh();
    plant(0, 0, 0, 0, 0, 0, 0);
    opinions(0, 0, 1, 100, 50);
    report_build(s, &r);
    CHECK(ptext(&r.para[3], 1736) && ptext(&r.para[3], 1741));
    fresh();
    plant(0, 0, 0, 0, 0, 0, 0);
    opinions(100, 0, 0, 0, 50); /* 100 haters: -3 for 10, 30, 60; A = 45 */
    report_build(s, &r);
    CHECK(r.points[3] == -3 && ptext(&r.para[3], 1740));
    opinions(0, 0, 0, 0, 39); /* below 40: -2 */
    report_build(s, &r);
    CHECK(r.points[3] == -2);
    opinions(0, 0, 0, 0, 55);
    report_build(s, &r);
    CHECK(r.points[3] == 4 && r.face[3] == 1294);
    /* paragraph 3 left out without nurses; rank 1 unchanged: +3; the paragraph-2 table */
    fresh();
    plant(0, 0, 0, 40, 1, 0, 2);
    put32(pb() + RP_RANK, 1);
    report_build(s, &r);
    CHECK(!r.shown[2] && r.points[2] == 0 && r.points[0] == 4 && r.face[0] == 1291);
    CHECK(r.points[1] == 1 + 2 + 1 - 3 && ptext(&r.para[1], 1711)); /* d 40, u 0, deaths 2 */
    fresh();
    plant(0, 0, 0, 39, 35, 1, 0);
    report_build(s, &r);
    CHECK(r.points[1] == 2); /* c >= 35, u 1 with d < 40, +1 */
    plant(0, 0, 0, 60, 2, 2, 0);
    report_build(s, &r);
    CHECK(r.points[1] == 3 && r.face[1] == 1292);
    plant(0, 0, 0, 59, 2, 2, 0);
    report_build(s, &r);
    CHECK(r.points[1] == 2);
    plant(0, 0, 0, 2, 2, 3, 0);
    report_build(s, &r);
    CHECK(r.points[1] == 1 && ptext(&r.para[1], 1709));
    plant(0, 0, 0, 0, 0, 0, 0);
    report_build(s, &r);
    CHECK(ptext(&r.para[1], 1696) && ptext(&r.para[1], 1706) && r.points[1] == 1);
    plant(-26, -2, -2, 0, 0, 0, 0);
    report_build(s, &r);
    CHECK(r.points[0] == 0 && ptext(&r.para[0], 1684) && ptext(&r.para[0], 1692));
    plant(-25, 2, 2, 0, 0, 0, 0);
    report_build(s, &r);
    CHECK(r.points[0] == 1 + 3 + 1 && ptext(&r.para[0], 1676));
    /* the nurses' bands */
    static const struct { int bust, age, fig, pts; } N[] = {
        {130, 30, 165, 3}, {120, 30, 160, 2}, {110, 30, 155, 2}, {90, 30, 150, 1}, {90, 23, 145, 3}, {90, 21, 144, 5}};
    for (size_t k = 0; k < sizeof N / sizeof *N; k++) {
        fresh();
        plant(0, 0, 0, 0, 0, 0, 0);
        nurse(N[k].bust, N[k].age, N[k].fig / 2, N[k].fig - N[k].fig / 2);
        report_build(s, &r);
        CHECK(r.points[2] == N[k].pts);
    }
    /* the heading: hospital name with the gender's article and the day count */
    fresh();
    g_image[NAMES + 29] = 1;
    put16(H(0) + H_DAYS_PLAYED, 13);
    report_build(s, &r);
    CHECK(r.heading.p[0].v == 1672 && r.heading.p[1].v == 2664 && r.heading.p[2].kind == PC_HOSPITAL && r.heading.p[3].v == 1673);
    CHECK(r.heading.p[4].kind == PC_NUM && r.heading.p[4].v == 13);
    /* the boundaries: a gain of exactly 25, one uncured with 40 discharged, a total of exactly 16 */
    fresh();
    plant(25, 0, 0, 40, 1, 1, 0);
    report_build(s, &r);
    CHECK(r.points[0] == 3 && ptext(&r.para[0], 1677) && r.points[1] == 3);
    fresh();
    plant(30, 3, 5, 60, 40, 0, 0);
    nurse(90, 23, 83, 82);
    opinions(0, 0, 0, 0, 55);
    report_build(s, &r);
    CHECK(r.total == 16 && r.overall == 1295 && r.sound == 0);
    /* the overall bands */
    fresh();
    plant(30, 3, 5, 60, 40, 0, 0);
    nurse(130, 21, 70, 70);
    opinions(0, 0, 0, 0, 55);
    report_build(s, &r);
    CHECK(r.total >= 16 && r.overall == 1295 && r.sound == 0);
}

/* ---- the advice: beginner rules and the end --------------------------------------------------------------- */

static void test_beginner(void) {
    fresh();
    CHECK(walk(ADVICE_CHIEF) == 0 && text0() == 2440 && g_adv.back == 0 && s->advice_pending);
    put16(H(0) + H_PLOTS_RENTED, 2);
    CHECK(walk(ADVICE_CHIEF) == 0 && text0() == 2439);
    put16(H(0) + H_PLOTS_OWNED, 2);
    CHECK(walk(ADVICE_CHIEF) == 1 && text0() == 2444);
    site()[1] = 1;
    CHECK(walk(ADVICE_CHIEF) == 2 && text0() == 2445);
    site()[2] = 1;
    put16(R(2, 0) + 8, 41); /* a receptionist, so A25 stays quiet */
    hire(1, 41, 1, 2, 50);
    CHECK(walk(ADVICE_CHIEF) == 3 && text0() == 2446);
    site()[3] = 1;
    CHECK(walk(ADVICE_CHIEF) == 0x39 && text0() == 2718);
    clerk(42, 1);
    CHECK(walk(ADVICE_CHIEF) == 4 && text0() == 2447 && pb()[AP_TOLD_TREATMENT] == 0xFF);
    put32(H(0) + H_CASH, 49);
    int a = walk(ADVICE_CHIEF);
    CHECK(a != 4 && a != 5); /* told once; dental needs cash 50 */
    put32(H(0) + H_CASH, 984);
    CHECK(walk(ADVICE_CHIEF) == 5 && text0() == 2448 && pb()[AP_TOLD_DENTAL] == 0xFF);
    time_at(3, 5, 8);
    CHECK(walk(ADVICE_CHIEF) != 0x13); /* hour 8 */
    time_at(3, 5, 7);
    CHECK(walk(ADVICE_CHIEF) == 0x13 && text0() == 2829 && pb()[AP_TOLD_NEUROLOGY] == 0xFF);
    /* expected cash below 0 with something built blocks the told-once rules */
    fresh();
    put16(H(0) + H_PLOTS_OWNED, 10);
    site()[1] = site()[2] = site()[3] = 1, site()[11] = 1;
    clerk(42, 1);
    put32(H(0) + H_CASH, 5), put32(H(0) + 0xE2, -1); /* cached E is below 0 */
    a = walk(ADVICE_CHIEF);
    CHECK(a != 4 && !pb()[AP_TOLD_TREATMENT]);
    /* the reception entry skips them and ends with 2441; the chief with a story event: 2833 */
    fresh();
    CHECK(walk(ADVICE_RECEPTION) == ADVICE_NONE && text0() == 2441 && get16(pb() + AP_ADVICE) == -1);
    CHECK(g_adv.back == 1 && !s->advice_pending);
    CHECK(walk(ADVICE_STRIP) == ADVICE_NONE && g_adv.back == 2);
    put16(H(0) + H_PLOTS_OWNED, 10);
    site()[1] = site()[2] = site()[3] = 1;
    hire(1, 41, 1, 2, 50);
    put16(R(2, 0) + 8, 41), put16(R(3, 0) + 8, 41);
    clerk(42, 1);
    for (int k = 0; k < 2; k++) put16(R(2, 0) + EQ + 2 * k, k + 1);
    eq_item(1, 3, 0), eq_item(2, 11, 0);
    R(2, 0)[0] = 20, R(3, 0)[0] = 20;
    pb()[AP_TOLD_TREATMENT] = pb()[AP_TOLD_DENTAL] = pb()[AP_TOLD_NEUROLOGY] = 0xFF;
    world_set_tail(&g_world, 0x12, 3);
    CHECK(walk(ADVICE_CHIEF) == ADVICE_NONE && text0() == 2833);
    CHECK(walk(ADVICE_RECEPTION) == ADVICE_NONE && text0() == 2441);
    /* the end restores pb +0 and +0x10 and keeps the expected cash in H +0xE2 */
    put16(pb() + AP_SAVED0, 77), put16(pb() + AP_ROOM, 3);
    put32(H(0) + 0xE2, 12345678);
    walk(ADVICE_RECEPTION);
    CHECK(get16(pb() + AP_SAVED0) == 77 && get16(pb() + AP_ROOM) == 3 && get32(H(0) + 0xE2) == 12345678);
}

/* a quiet player hospital for the chief doctor's rules: built through the warehouse, staffed, equipped */
static void quiet_chief(void) {
    fresh();
    put16(H(0) + H_PLOTS_OWNED, 10);
    site()[1] = site()[2] = site()[3] = 1;
    hire(1, 41, 1, 2, 50);
    put16(R(2, 0) + 8, 41);
    ST(41)[11] = 0xFF;
    clerk(42, 1);
    eq_item(1, 3, 0), eq_item(2, 11, 0);
    put16(R(2, 0) + EQ, 1), put16(R(2, 0) + EQ + 2, 2);
    R(2, 0)[0] = 20, R(3, 0)[0] = 20;
    put16(R(3, 0) + 8, 41); /* the waiting room's nurse (C39) */
    pb()[AP_TOLD_TREATMENT] = pb()[AP_TOLD_DENTAL] = pb()[AP_TOLD_NEUROLOGY] = 0xFF;
}

static void test_store_staff(void) {
    /* A1: the warehouse without a manager */
    fresh();
    put16(H(0) + H_PLOTS_OWNED, 10);
    site()[1] = site()[2] = site()[3] = site()[4] = 1;
    hire(1, 41, 1, 2, 50);
    put16(R(2, 0) + 8, 41);
    pb()[AP_TOLD_TREATMENT] = pb()[AP_TOLD_DENTAL] = pb()[AP_TOLD_NEUROLOGY] = 0xFF;
    CHECK(walk(ADVICE_CHIEF) == 0x3B && text0() == 2863 && has_text(2717));
    CHECK(walk(ADVICE_RECEPTION) == ADVICE_NONE); /* the ad branch only from the chief doctor */
    put32(H(0) + H_CASH, 19);
    CHECK(walk(ADVICE_CHIEF) != 0x3B); /* cash 20 needed */
    put32(H(0) + H_CASH, 984);
    hire(2, 43, 3, 0x13, 50); /* a manager off duty: no ad */
    CHECK(walk(ADVICE_CHIEF) != 0x3B);
    SL(2)[3] = 0, ST(43)[4] = 0; /* idle now */
    CHECK(walk(ADVICE_RECEPTION) == 0x3A && has_text(2854) && u16(pb() + AP_SAVED0) == 0); /* pb +0 restored */
    ST(43)[21] = 9; /* mood below 10: not idle */
    CHECK(walk(ADVICE_RECEPTION) == ADVICE_NONE);
    memset(SL(2), 0, STAFF_SLOT_SIZE);
    applicant_for(77, 3, 5);
    CHECK(walk(ADVICE_CHIEF) == 7 && has_text(2881));
    g_world.block[BLK_JOB_OPENINGS][5 * JOB_OPENING_SIZE] = 4; /* the opening is another hospital's */
    CHECK(walk(ADVICE_CHIEF) == 0x3B);
    g_world.block[BLK_JOB_OPENINGS][5 * JOB_OPENING_SIZE] = 0;
    /* the applicant advice ignores the group (any applicant of any opening of H counts) */
    ST(77)[1] = 5;
    CHECK(walk(ADVICE_CHIEF) == 7);
    ST(77)[27] = 0xFF;
    CHECK(walk(ADVICE_CHIEF) != 7 && walk(ADVICE_CHIEF) != 0x3B); /* opening, no applicant: next rule */
}

static void bank_world(void) {
    quiet_chief();
    site()[17] = 1;
    uint8_t *b = sim_bank(s, 0);
    put32(b + 0x10, 4000), put32(b + 0x0C, 1000);
    eq_item(3, 0, 0x3D), eq_item(4, 0, 0x33), eq_item(5, 0, 0x3C);
    put16(b + EQ, 3), put16(b + EQ + 2, 4);
}

static void test_bank_pathology(void) {
    /* A3: the cooling unit 0x33 missing: store has it, does not fetch */
    bank_world();
    uint8_t *b = sim_bank(s, 0);
    put16(b + EQ + 2, 0);
    ST(42)[11] = 0;
    store_equipment(4);
    CHECK(walk(ADVICE_RECEPTION) == 0x6F && text0() == 2842 && has_text(2780) && pb()[AP_CAPABILITY] == 0x33);
    ST(42)[11] = 0xFF; /* the store fetches by itself: no advice */
    CHECK(walk(ADVICE_RECEPTION) != 0x6F);
    s->screen = 0x41; /* ... except while the player is on the inventory */
    CHECK(walk(ADVICE_RECEPTION) == 0x6F);
    s->screen = 0;
    /* A2 through the free space: containers present, capacity 4,000, stock 3,600 */
    bank_world();
    b = sim_bank(s, 0);
    put32(b + 0x0C, 3600);
    ST(42)[11] = 0;
    store_equipment(3);
    CHECK(walk(ADVICE_RECEPTION) == 0x6F && text0() == 2844);
    put32(b + 0x0C, 3499);
    CHECK(walk(ADVICE_RECEPTION) != 0x6F || text0() != 2844);
    /* A4: not in the store, the market offers it: buy (H +0xDA); not offered: next rule; cash below 6: to B16 */
    bank_world();
    b = sim_bank(s, 0);
    put16(b + EQ, 5), put16(b + EQ + 2, 4); /* 0x3D missing now: A2 */
    eq_item(5, 0, 0x3C);
    market(0, 3, 3);
    CHECK(walk(ADVICE_RECEPTION) == 0x3C && text0() == 2844 && has_text(2716) && H(0)[0xDA] == 0x3D);
    H(0)[0xDA] = 0;
    put32(H(0) + H_CASH, 7);
    g_world.block[BLK_EQUIPMENT_MARKET][3 * 8] = 0xFF;
    CHECK(walk(ADVICE_RECEPTION) == ADVICE_NONE); /* not offered */
    market(0, 3, 3);
    g_world.block[BLK_EQUIPMENT_MARKET][3 * 8 + 5] = 1; /* reserved: not offered */
    CHECK(walk(ADVICE_RECEPTION) == ADVICE_NONE);
    put32(H(0) + H_CASH, 6);
    CHECK(walk(ADVICE_RECEPTION) == ADVICE_NONE); /* bank gate: cash 7 */
    /* A5-A11: the pathology */
    quiet_chief();
    site()[18] = 1;
    eq_item(6, 0, 9);
    market(0, 0, 6);
    CHECK(walk(ADVICE_RECEPTION) == 0x3C && text0() == 2839);
    put16(R(18, 0) + EQ, 6);
    eq_item(7, 0, 0x33);
    ST(42)[11] = 0;
    store_equipment(7);
    CHECK(walk(ADVICE_RECEPTION) == 0x70 && text0() == 2840);
    site()[4] = 0;
    CHECK(walk(ADVICE_RECEPTION) == ADVICE_NONE); /* the pathology gate needs the store room */
    /* the pathology's buy tail with cash 7 */
    quiet_chief();
    site()[18] = 1;
    eq_item(6, 0, 9);
    market(0, 0, 6);
    put32(H(0) + H_CASH, 7);
    CHECK(walk(ADVICE_RECEPTION) == 0x3C);
    /* the cash-below-6 skip: the golf room's buy tail jumps to B16, past A25 */
    quiet_chief();
    site()[12] = 1;
    fu_item(4, 0x12, 0);
    put16(R(12, 0) + FURN, 4);
    eq_item(8, 0, 0x36);
    market(0, 0, 8);
    put16(R(2, 0) + 8, 0); /* A25 would fire */
    hire(5, 45, 1, 0, 50);
    site()[5] = 1;
    hire(3, 44, 2, 0, 50); /* an idle doctor for B16 */
    put16(R(5, 0) + 8, 41);
    CHECK(walk(ADVICE_RECEPTION) == 0x3C && has_text(2691));
    put32(H(0) + H_CASH, 5);
    CHECK(walk(ADVICE_RECEPTION) == 0x0B);
    put32(H(0) + H_CASH, 984);
    g_world.block[BLK_EQUIPMENT_MARKET][0] = 0xFF; /* not offered: on to A25 */
    CHECK(walk(ADVICE_RECEPTION) == 6);
}

static void test_furniture_requests(void) {
    fu_item(1, 1, 0), fu_item(2, 5, 0), fu_item(3, 8, 0), fu_item(4, 0x12, 0);
    /* A12: the reception wants kind 1; the store has it */
    quiet_chief();
    fu_item(1, 1, 0), fu_item(2, 5, 0);
    ST(42)[11] = 0;
    R(2, 0)[4] = 1;
    store_furniture(1);
    CHECK(walk(ADVICE_RECEPTION) == 0x47 && text0() == 2702 && has_text(3837) && has_text(2688) && pb()[AP_KIND] == 1);
    /* the refresh rewrites the request to the first missing kind: kind 1 there, kind 5 missing */
    put16(R(2, 0) + FURN, 1);
    CHECK(walk(ADVICE_RECEPTION) == 0x48 && R(2, 0)[4] == 5 && has_text(3841) && has_text(2687) && H(0)[0xDC] == 5);
    put16(R(2, 0) + FURN + 2, 2);
    walk(ADVICE_RECEPTION);
    CHECK(R(2, 0)[4] == 0); /* nothing missing: cleared */
    /* A13: a treatment room; a patient there: skipped; cash below 6: on at A19 */
    quiet_chief();
    fu_item(1, 1, 0), fu_item(2, 5, 0);
    site()[5] = 1;
    put16(R(5, 0) + 8, 41), put16(R(5, 0) + 10, 41);
    R(5, 0)[4] = 5;
    CHECK(walk(ADVICE_RECEPTION) == 0x48 && text0() == 2711 && g_adv.text.p[1].v == 1);
    put16(R(5, 0) + 0x0C, 9);
    CHECK(walk(ADVICE_RECEPTION) != 0x48);
    put16(R(5, 0) + 0x0C, 0);
    put32(H(0) + H_CASH, 5);
    site()[12] = 1; /* A19 reached by the jump */
    fu_item(4, 0x12, 0);
    market(1, 0, 4);
    put16(R(12, 0) + EQ, 0);
    store_furniture(4);
    ST(42)[11] = 0;
    CHECK(walk(ADVICE_RECEPTION) == 0x6D && text0() == 2708 && has_text(2695));
    /* A16 (OP, request +0x7A), A17 (torture, text 2700), A18 (ICU kinds 8, 5) */
    quiet_chief();
    fu_item(3, 8, 0), fu_item(2, 5, 0);
    site()[7] = 1;
    R(7, 0)[0x7A] = 8;
    CHECK(walk(ADVICE_RECEPTION) == 0x48 && text0() == 2705);
    quiet_chief();
    fu_item(2, 5, 0);
    site()[8] = 1;
    R(8, 0)[0x7A] = 5;
    CHECK(walk(ADVICE_RECEPTION) == 0x48 && text0() == 2704 && has_text(2700));
    quiet_chief();
    fu_item(3, 8, 0), fu_item(2, 5, 0);
    site()[9] = 1;
    R(9, 0)[4] = 5;
    put16(R(9, 0) + FURN, 3);
    CHECK(walk(ADVICE_RECEPTION) == 0x48 && text0() == 2709 && R(9, 0)[4] == 5);
    /* A14 neurology, A15 dental (no patient test) */
    quiet_chief();
    fu_item(3, 8, 0), fu_item(2, 5, 0);
    site()[15] = 1;
    R(15, 0)[4] = 8;
    CHECK(walk(ADVICE_RECEPTION) == 0x48 && text0() == 2706);
    quiet_chief();
    fu_item(2, 5, 0);
    site()[11] = 1;
    R(11, 0)[4] = 5;
    put16(R(11, 0) + 0x0C, 9);
    CHECK(walk(ADVICE_RECEPTION) == 0x48 && text0() == 2710);
    /* without a store clerk no request rule */
    site()[4] = 0;
    CHECK(walk(ADVICE_RECEPTION) != 0x48);
}

static void test_golf_garage(void) {
    /* A19: the furniture market is asked first */
    quiet_chief();
    fu_item(4, 0x12, 0);
    site()[12] = 1;
    ST(42)[11] = 0;
    store_furniture(4);
    CHECK(walk(ADVICE_RECEPTION) != 0x6D); /* not offered: no advice although the store has it */
    market(1, 0, 4);
    CHECK(walk(ADVICE_RECEPTION) == 0x6D && has_text(2688));
    /* A20 / A21: golf clubs and balls */
    put16(R(12, 0) + FURN, 4);
    eq_item(8, 0, 0x36), eq_item(9, 0, 0x37);
    store_equipment(8);
    CHECK(walk(ADVICE_RECEPTION) == 0x6E && has_text(2691));
    put16(R(12, 0) + EQ, 8);
    store_equipment(9);
    CHECK(walk(ADVICE_RECEPTION) == 0x6E && has_text(2690));
    /* A22-A24: the garage; A24 says "Golfraum" */
    quiet_chief();
    fu_item(3, 8, 0);
    site()[10] = 1;
    ST(42)[11] = 0;
    market(1, 0, 3);
    store_furniture(3);
    CHECK(walk(ADVICE_RECEPTION) == 0x71 && text0() == 2707 && has_text(2694));
    put16(R(10, 0) + FURN, 3);
    eq_item(10, 0, 0x40), eq_item(11, 0, 0x41);
    store_equipment(10), store_equipment(11);
    CHECK(walk(ADVICE_RECEPTION) == 0x72 && text0() == 2707 && has_text(2693));
    put16(R(10, 0) + EQ, 10);
    CHECK(walk(ADVICE_RECEPTION) == 0x72 && text0() == 2708 && has_text(2692));
    /* the endless loop of the buy-furniture tail (cash below 6 in A22): the engine goes on */
    quiet_chief();
    fu_item(3, 8, 0);
    site()[10] = 1;
    market(1, 0, 3);
    put32(H(0) + H_CASH, 5);
    eq_item(10, 0, 0x40);
    store_equipment(10);
    ST(42)[11] = 0;
    CHECK(walk(ADVICE_RECEPTION) == 0x72); /* A23 after A22's tail */
}

static void test_reception_rules(void) {
    /* A25: no receptionist, hours 3-17 or 1 January */
    quiet_chief();
    put16(R(2, 0) + 8, 0);
    hire(5, 45, 1, 0, 50);
    CHECK(walk(ADVICE_RECEPTION) == 6 && text0() == 2449 && has_text(2889));
    time_at(3, 5, 18);
    CHECK(walk(ADVICE_RECEPTION) != 6);
    time_at(1, 1, 22);
    CHECK(walk(ADVICE_RECEPTION) == 6);
    time_at(3, 5, 2);
    CHECK(walk(ADVICE_RECEPTION) != 6);
    /* the nurse's no-opening branch gives 8 with 2879 (chief) */
    time_at(3, 5, 12);
    memset(SL(5), 0, STAFF_SLOT_SIZE);
    CHECK(walk(ADVICE_CHIEF) == 8 && has_text(2879));
    /* B4 / B5: the reception lacks a pen / a form */
    quiet_chief();
    put16(R(2, 0) + EQ, 0);
    ST(42)[11] = 0;
    CHECK(walk(ADVICE_RECEPTION) == 0x3C && text0() == 2778 && has_text(3839) && H(0)[0xDB] == 3 && pb()[AP_KIND] == 3);
    store_equipment(1);
    CHECK(walk(ADVICE_RECEPTION) == 0x38 && has_text(2779));
    ST(42)[11] = 0xFF;
    CHECK(walk(ADVICE_RECEPTION) != 0x38);
    quiet_chief();
    put16(R(2, 0) + EQ + 2, 0);
    ST(42)[11] = 0;
    store_equipment(2);
    CHECK(walk(ADVICE_RECEPTION) == 0x38 && has_text(3847));
    /* B4 tests no reception at all (an empty list lacks the pen) */
    quiet_chief();
    site()[2] = 0;
    put16(R(2, 0) + EQ, 0);
    ST(42)[11] = 0;
    store_equipment(1);
    CHECK(walk(ADVICE_RECEPTION) == 0x38);
    /* C9: a patient waits for a Krankenzimmer bed */
    quiet_chief();
    ST(41)[11] = 0;
    put16(R(2, 0) + 0x46, 300);
    site()[6] = 1;
    R(6, 0)[10] = 2;
    put16(R(6, 0) + 8, 41);
    CHECK(walk(ADVICE_RECEPTION) == 0x22 && text0() == 2815);
    R(6, 0)[12] = 2; /* full */
    CHECK(walk(ADVICE_RECEPTION) != 0x22);
    /* C37 / C38: the desk */
    quiet_chief();
    ST(41)[11] = 0;
    put16(R(2, 0) + 0x0A, 300);
    R(3, 0)[10] = 3;
    CHECK(walk(ADVICE_RECEPTION) == 9 && text0() == 2877);
    R(3, 0)[12] = 3;
    CHECK(walk(ADVICE_RECEPTION) != 9);
    P(300)[104] = 1; /* dead: the stale byte decides */
    int a = walk(ADVICE_RECEPTION);
    CHECK(a != 9);
}

static void test_requests(void) {
    /* A26: an OP's capability request; the kind half without the fetch test */
    quiet_chief();
    site()[7] = 1;
    put16(R(7, 0) + 6, 41), put16(R(7, 0) + 8, 41), put16(R(7, 0) + 10, 41);
    eq_item(12, 7, 0x22);
    R(7, 0)[4] = 0x22;
    ST(42)[11] = 0;
    store_equipment(12);
    CHECK(walk(ADVICE_RECEPTION) == 0x42 && text0() == 2847 && has_text(2712) && has_text(3858 + 0x22) && has_text(2779));
    R(7, 0)[4] = 0, R(7, 0)[5] = 7;
    ST(42)[11] = 0xFF; /* fetches, but the kind half asks no fetch test */
    CHECK(walk(ADVICE_RECEPTION) == 0x42 && has_text(3836 + 7) && pb()[AP_KIND] == 7);
    R(7, 0)[5] = 0;
    CHECK(walk(ADVICE_RECEPTION) != 0x42); /* the word 0: skipped */
    /* A27 torture chamber, B2/B3 ICU */
    quiet_chief();
    site()[8] = 1;
    R(8, 0)[5] = 7;
    eq_item(12, 7, 0);
    market(0, 0, 12);
    CHECK(walk(ADVICE_RECEPTION) == 0x3C && text0() == 2845); /* store lacks it: buy, no market test */
    quiet_chief();
    site()[9] = 1;
    R(9, 0)[6] = 0x22;
    eq_item(12, 7, 0x22);
    ST(42)[11] = 0;
    store_equipment(12);
    CHECK(walk(ADVICE_RECEPTION) == 0x41 && text0() == 2872);
    R(9, 0)[6] = 0, R(9, 0)[7] = 7;
    ST(42)[11] = 0xFF;
    CHECK(walk(ADVICE_RECEPTION) == 0x41);
    /* B6/B7 treatment room +0x44 / +0x45: the kind half with the fetch test */
    quiet_chief();
    site()[5] = 1;
    put16(R(5, 0) + 8, 41), put16(R(5, 0) + 10, 41);
    R(5, 0)[0x45] = 7;
    eq_item(12, 7, 0);
    ST(42)[11] = 0;
    store_equipment(12);
    CHECK(walk(ADVICE_RECEPTION) == 0x3D && text0() == 2870);
    ST(42)[11] = 0xFF;
    CHECK(walk(ADVICE_RECEPTION) != 0x3D);
    /* B8 dental, B10 neurology */
    quiet_chief();
    site()[11] = 1;
    R(11, 0)[0x44] = 0x22;
    eq_item(12, 7, 0x22);
    market(0, 0, 12);
    CHECK(walk(ADVICE_RECEPTION) == 0x3C && text0() == 2848);
    quiet_chief();
    site()[15] = 1;
    R(15, 0)[0x44] = 0x22;
    eq_item(12, 7, 0x22);
    ST(42)[11] = 0;
    store_equipment(12);
    CHECK(walk(ADVICE_RECEPTION) == 0x5A && text0() == 2869);
    /* B12: the Krankenzimmer's food, hours 4-21 */
    quiet_chief();
    site()[6] = 1;
    put16(R(6, 0) + 8, 41);
    R(6, 0)[6] = 43;
    eq_item(13, 0, 43);
    ST(42)[11] = 0;
    store_equipment(13);
    CHECK(walk(ADVICE_RECEPTION) == 0x3F && text0() == 2873 && has_text(2713) && has_text(2781));
    time_at(3, 5, 22);
    CHECK(walk(ADVICE_RECEPTION) != 0x3F);
    time_at(3, 5, 12);
    store_rec()[STORE_EQUIPMENT_HELD] = 0;
    CHECK(walk(ADVICE_RECEPTION) == 0x3C && has_text(2715)); /* no kitchen, cash >= 30: buy food */
    site()[13] = 1;
    put16(R(13, 0) + 6, 41); /* a kitchen (with a cook, so D12 stays quiet) */
    CHECK(walk(ADVICE_RECEPTION) != 0x3C);
    /* B12 other capability: no market test; B14 the neuro ward: market first */
    quiet_chief();
    site()[6] = 1;
    put16(R(6, 0) + 8, 41);
    R(6, 0)[6] = 0x22;
    CHECK(walk(ADVICE_RECEPTION) == 0x3C && has_text(3858 + 0x22));
    quiet_chief();
    site()[16] = 1;
    put16(R(16, 0) + 8, 41);
    R(16, 0)[6] = 0x22;
    eq_item(12, 7, 0x22);
    CHECK(walk(ADVICE_RECEPTION) != 0x3C); /* not offered */
    market(0, 0, 12);
    CHECK(walk(ADVICE_RECEPTION) == 0x3C && text0() == 2867);
    ST(42)[11] = 0;
    store_equipment(12);
    CHECK(walk(ADVICE_RECEPTION) == 0x40);
}

static void test_staff_rules(void) {
    /* B16: a treatment room without a doctor; an idle doctor */
    quiet_chief();
    site()[5] = 1;
    put16(R(5, 0) + 8, 41);
    hire(6, 46, 2, 0, 50);
    CHECK(walk(ADVICE_RECEPTION) == 0x0B && text0() == 2870 && has_text(2875) && has_text(2855));
    CHECK(u16(pb() + AP_INSTANCE) == 0);
    time_at(3, 5, 20);
    CHECK(walk(ADVICE_RECEPTION) != 0x0B);
    time_at(3, 5, 12);
    ST(46)[21] = 5; /* mood 5: not idle */
    CHECK(walk(ADVICE_RECEPTION) != 0x0B);
    CHECK(walk(ADVICE_CHIEF) == 0x0C && has_text(2853)); /* the ad */
    /* B17: only dentist applicants count */
    quiet_chief();
    site()[11] = 1;
    put16(R(11, 0) + 8, 41);
    applicant_for(80, 9, 0);
    ST(80)[1] = 2; /* a doctor applies to the dentist's opening */
    CHECK(walk(ADVICE_CHIEF) != 7);
    ST(80)[1] = 9;
    CHECK(walk(ADVICE_CHIEF) == 7 && has_text(2884));
    /* B18, B19-B21 */
    quiet_chief();
    site()[15] = 1;
    put16(R(15, 0) + 8, 41);
    hire(6, 46, 10, 0, 50);
    CHECK(walk(ADVICE_RECEPTION) == 0x14 && has_text(2876));
    quiet_chief();
    site()[5] = 1;
    put16(R(5, 0) + 10, 41);
    hire(6, 46, 1, 0, 50);
    CHECK(walk(ADVICE_RECEPTION) == 0x0A && has_text(2874));
    quiet_chief();
    site()[11] = 1;
    put16(R(11, 0) + 10, 41);
    hire(6, 46, 1, 0, 50);
    CHECK(walk(ADVICE_RECEPTION) == 0x0F);
    quiet_chief();
    site()[15] = 1;
    put16(R(15, 0) + 10, 41);
    hire(6, 46, 1, 0, 50);
    CHECK(walk(ADVICE_RECEPTION) == 0x16);
    /* B24: a staffed treatment room without a patient while somebody waits */
    quiet_chief();
    site()[5] = 1;
    put16(R(5, 0) + 8, 41), put16(R(5, 0) + 10, 41);
    ST(41)[11] = 0;
    put16(R(3, 0) + 0x14, 300); /* a waiting patient with a wish */
    P(300)[4] = 3, P(300)[57] = 1;
    CHECK(walk(ADVICE_RECEPTION) == 0x10 && has_text(2830));
    ST(41)[11] = 0xFF;
    CHECK(walk(ADVICE_RECEPTION) != 0x10);
    /* C10 / C11 ward nurses, C13-C15 the OP's staff, C23 / C24 the torture chamber's, C39, D2 */
    static const struct { uint8_t cat, field, group; int16_t advice, first; uint8_t hour_to; } MISS[] = {
        {6, 8, 1, 0x1F, 2873, 23},  {16, 8, 1, 0x21, 2867, 23}, {7, 10, 5, 0x27, 2847, 19}, {7, 6, 2, 0x26, 2847, 19},
        {7, 8, 1, 0x25, 2847, 18},  {8, 10, 10, 0x5F, 2845, 19}, {8, 8, 1, 0x60, 2845, 18}, {3, 8, 1, 0x2D, 2871, 18},
        {9, 8, 1, 0x2F, 2872, 23}};
    for (size_t k = 0; k < sizeof MISS / sizeof *MISS; k++) {
        quiet_chief();
        site()[MISS[k].cat] = 1;
        int theatre = MISS[k].cat == 7 || MISS[k].cat == 8, clinic = theatre || MISS[k].cat == 5;
        for (int f = theatre ? 6 : 8; f <= (clinic ? 10 : 8); f += 2)
            if (f != MISS[k].field) put16(R(MISS[k].cat, 0) + f, 41);
        if (MISS[k].cat == 3) memset(R(3, 0) + 8, 0, 2);
        hire(6, 46, MISS[k].group, 0, 50);
        CHECK(walk(ADVICE_RECEPTION) == MISS[k].advice && text0() == MISS[k].first);
        if (MISS[k].hour_to < 23) {
            time_at(3, 5, MISS[k].hour_to + 1);
            CHECK(walk(ADVICE_RECEPTION) != MISS[k].advice);
        }
    }
    /* D3 bank nurse, D4 golf (hour < 5), D5 club, D6 garage, D9 / D10 pathologists, D12 cook */
    quiet_chief();
    site()[17] = 1;
    put16(sim_bank(s, 0) + 10, 41);
    eq_item(3, 0, 0x3D), eq_item(4, 0, 0x33), eq_item(5, 0, 0x3C); /* bank complete */
    put32(sim_bank(s, 0) + 0x10, 4000);
    hire(6, 46, 1, 0, 50);
    put16(sim_bank(s, 0) + EQ, 3);
    ST(42)[11] = 0xFF;
    int a = walk(ADVICE_RECEPTION);
    CHECK(a == 0x69 || a == ADVICE_NONE || a == 0x3C);
    quiet_chief();
    site()[12] = 1;
    fu_item(4, 0x12, 0);
    put16(R(12, 0) + FURN, 4);
    eq_item(8, 0, 0x36), eq_item(9, 0, 0x37);
    put16(R(12, 0) + EQ, 8), put16(R(12, 0) + EQ + 2, 9);
    hire(6, 46, 1, 0, 50);
    time_at(3, 5, 4);
    CHECK(walk(ADVICE_RECEPTION) == 0x6A && text0() == 2860);
    time_at(3, 5, 5);
    CHECK(walk(ADVICE_RECEPTION) != 0x6A);
    quiet_chief();
    site()[14] = 1;
    hire(6, 46, 1, 0, 50);
    CHECK(walk(ADVICE_RECEPTION) == 0x6B && text0() == 2859);
    put16(R(14, 0) + 2, 41);
    CHECK(walk(ADVICE_RECEPTION) == 0x6C && text0() == 2858);
    time_at(3, 5, 16);
    CHECK(walk(ADVICE_RECEPTION) != 0x6C);
    quiet_chief();
    site()[10] = 1;
    fu_item(3, 8, 0);
    put16(R(10, 0) + FURN, 3);
    eq_item(10, 0, 0x40), eq_item(11, 0, 0x41);
    put16(R(10, 0) + EQ, 10), put16(R(10, 0) + EQ + 2, 11);
    hire(6, 46, 6, 0, 50);
    CHECK(walk(ADVICE_RECEPTION) == 0x65 && text0() == 2865 && has_text(2451));
    put16(R(10, 0) + 8, 41);
    CHECK(walk(ADVICE_RECEPTION) == 0x66 && text0() == 2866 && has_text(2450));
    put16(g_world.block[BLK_34], 1); /* a race: no garage rule */
    CHECK(walk(ADVICE_RECEPTION) != 0x66);
    quiet_chief();
    site()[18] = 1;
    eq_item(6, 0, 9);
    for (int k = 0; k < 2; k++) put16(R(18, 0) + EQ + 2 * k, 6);
    site()[4] = 0; /* no store: the pathology gate is closed */
    hire(6, 46, 7, 0, 50);
    CHECK(walk(ADVICE_RECEPTION) == 0x33 && text0() == 2861);
    quiet_chief();
    site()[13] = 1;
    site()[4] = 0;
    hire(6, 46, 4, 0, 50);
    CHECK(walk(ADVICE_RECEPTION) == 0x5D && text0() == 2864 && has_text(2788));
}

static void test_patients(void) {
    /* C1-C3: the treatment room's patient */
    quiet_chief();
    site()[5] = 1;
    put16(R(5, 0) + 8, 41), put16(R(5, 0) + 10, 41);
    put16(R(5, 0) + 0x0C, 300);
    ST(41)[11] = 0;
    put16(R(5, 0) + 0x10, 3);
    CHECK(walk(ADVICE_RECEPTION) == 0x19 && has_text(2818) && has_text(2819));
    put16(R(5, 0) + 0x12, 1); /* the treatment still running */
    CHECK(walk(ADVICE_RECEPTION) != 0x19);
    put16(R(5, 0) + 0x12, 0);
    put16(R(5, 0) + 0x10, 0);
    CHECK(walk(ADVICE_RECEPTION) == 0x18 && has_text(2824));
    ST(41)[11] = 0xFF;
    CHECK(walk(ADVICE_RECEPTION) != 0x18);
    P(300)[104] = 1;
    site()[18] = 1;
    put16(R(18, 0) + 8, 5); /* free pathology places */
    eq_item(6, 0, 9);
    int a = walk(ADVICE_RECEPTION);
    CHECK(a == 0x44 || a == 0x3C || a == 0x70);
    site()[4] = 0; /* no pathology gate */
    CHECK(walk(ADVICE_RECEPTION) == 0x44 && has_text(2804) && has_text(2805));
    put16(R(18, 0) + 6, 5); /* full */
    CHECK(walk(ADVICE_RECEPTION) != 0x44);
    /* C8: the neurology waits for a diagnosis only with byte 4 of the illness or free neuro beds */
    fresh();
    site()[15] = 1;
    put16(R(15, 0) + 8, 41), put16(R(15, 0) + 10, 41);
    put16(R(15, 0) + 0x0C, 300);
    P(300)[42] = 3;
    CHECK(walk(ADVICE_RECEPTION) != 0x1C);
    g_image[ILLNESSES + 3 * 20 + 4] = 1;
    CHECK(walk(ADVICE_RECEPTION) == 0x1C && text0() == 2869);
    g_image[ILLNESSES + 3 * 20 + 4] = 0;
    site()[16] = 1;
    R(16, 0)[10] = 1;
    put16(R(16, 0) + 8, 41);
    CHECK(walk(ADVICE_RECEPTION) == 0x1C);
    /* C30-C33: the Krankenzimmer's beds */
    fresh();
    site()[6] = 1;
    put16(R(6, 0) + 8, 41);
    put16(R(6, 0) + 0x14 + 4 * 3, 300);
    P(300)[4] = 6;
    put16(P(300) + 56, 2); /* an illness, field 93 = 0: start the treatment */
    CHECK(walk(ADVICE_RECEPTION) == 0x29 && has_text(2808) && u16(pb() + AP_PLACE) == 3);
    ST(41)[11] = 0xFF;
    CHECK(walk(ADVICE_RECEPTION) != 0x29);
    P(300)[93] = 1, P(300)[44] = 29;
    site()[9] = 1; /* a free intensive-care record */
    R(9, 0)[1] = 63;
    sim_rating_record(s, P(300));
    CHECK(walk(ADVICE_RECEPTION) == 0x29 && has_text(2806));
    CHECK(sim_rating_byte(s) == 63 && s->rating_ref.block == 21);
    P(300)[44] = 30;
    CHECK(walk(ADVICE_RECEPTION) != 0x29);
    put16(P(300) + 56, 0), P(300)[44] = 100;
    CHECK(walk(ADVICE_RECEPTION) == 0x29 && has_text(2811));
    time_at(3, 5, 21);
    CHECK(walk(ADVICE_RECEPTION) != 0x29);
    /* C29 / C30 dead patients: the stale byte [0x45C8], which a market test can leave set */
    fresh();
    site()[6] = 1;
    put16(R(6, 0) + 8, 41);
    put16(R(6, 0) + 0x14, 300);
    P(300)[4] = 6, P(300)[104] = 1;
    CHECK(walk(ADVICE_RECEPTION) != 0x29); /* [0x45C8] is 0 */
    site()[16] = 1; /* B14 runs a market test before (offered: [0x45C8] = the capability) */
    put16(R(16, 0) + 8, 41);
    R(16, 0)[6] = 0x22;
    eq_item(12, 7, 0x22);
    market(0, 0, 12);
    clerk(42, 1);
    eq_item(1, 3, 0), eq_item(2, 11, 0); /* B4 / B5 quiet: the reception list holds pen and form */
    put16(R(2, 0) + EQ, 1), put16(R(2, 0) + EQ + 2, 2);
    store_equipment(12);
    CHECK(walk(ADVICE_RECEPTION) == 0x29 && has_text(2804)); /* the store fetches: no B14; C30 fires on the stale byte */
    /* D8: the ICU patient back to a ward */
    fresh();
    site()[9] = 1, site()[2] = 1, site()[6] = 1;
    put16(R(9, 0) + 8, 41), put16(R(9, 0) + 0x0C, 300);
    put16(R(2, 0) + 8, 41);
    ST(41)[11] = 0xFF;
    P(300)[44] = 50;
    R(6, 0)[10] = 1;
    put16(R(6, 0) + 8, 41);
    CHECK(walk(ADVICE_RECEPTION) == 0x30 && has_text(2791));
    P(300)[102] = 1;
    CHECK(walk(ADVICE_RECEPTION) != 0x30);
    P(300)[102] = 0;
    for (int k = 0; k < 6; k++) put16(R(2, 0) + 0x46 + 4 * k, 500 + k); /* the queue full */
    CHECK(walk(ADVICE_RECEPTION) != 0x30);
}

/* The stale byte [0x45C8] = 1 the way the spec's oracle cases make it (docs/ui-reports.md, "Run against the engine
 * (2026-10-20, U-3)"): a Krankenzimmer without a nurse (C10) while the only nurse is not idle, so 0x273DE(1) leaves 1
 * and, from the reception, the walk goes on. */
static void stale_one(void) {
    site()[6] = 1;
    put16(R(6, 0) + 8, 0);
}

/* SCR-4 and SCR-5: "What the engine must change" 1-3 there */
static void test_advice_u3(void) {
    /* 1: C37 / C38, a dead desk patient */
    quiet_chief();
    ST(41)[11] = 0; /* the receptionist not independent */
    put16(R(2, 0) + 0x0A, 300);
    P(300)[104] = 1;
    R(3, 0)[10] = 3; /* free waiting seats */
    CHECK(walk(ADVICE_RECEPTION) == 9 && text0() == 2877); /* c8 = 0: on to C38 */
    stale_one();
    CHECK(walk(ADVICE_RECEPTION) == 9 && text0() == 2868); /* c8 = 1: C37 */
    quiet_chief();
    put16(R(2, 0) + 0x0A, 300);
    P(300)[104] = 1;
    R(3, 0)[10] = 3;
    CHECK(walk(ADVICE_RECEPTION) != 9); /* c8 = 0, receptionist independent: the next rule */
    /* 2: C29 tests the byte W+0x0F */
    quiet_chief();
    stale_one();
    put16(R(3, 0) + 0x14 + 4 * 2, 300);
    P(300)[4] = 3, P(300)[104] = 1;
    R(3, 0)[0x0F] = 0, R(3, 0)[0x10] = 1;
    CHECK(walk(ADVICE_RECEPTION) == 0x43 && text0() == 2871 && u16(pb() + AP_PLACE) == 2);
    R(3, 0)[0x0F] = 1, R(3, 0)[0x10] = 0;
    CHECK(walk(ADVICE_RECEPTION) != 0x43);
    /* 3: the loop counters. Waiting room count 1, nurse, ten places, no hit: +0xA6 = 1 (C39 is the last loop over
     * a non-zero count), +0xA4 = 10 */
    quiet_chief();
    put16(pb() + AP_INSTANCE, 7), put16(pb() + AP_PLACE, 7);
    CHECK(walk(ADVICE_RECEPTION) == ADVICE_NONE);
    CHECK(u16(pb() + AP_INSTANCE) == 1 && u16(pb() + AP_PLACE) == 10);
    /* Krankenzimmer count 2, both with a nurse, no hit: +0xA6 = 2 (no other room in the walk) */
    fresh();
    site()[6] = 2;
    put16(R(6, 0) + 8, 41), put16(R(6, 1) + 8, 41);
    CHECK(walk(ADVICE_RECEPTION) == ADVICE_NONE && u16(pb() + AP_INSTANCE) == 2);
    /* a count of 0 leaves both untouched */
    fresh();
    put16(pb() + AP_INSTANCE, 7), put16(pb() + AP_PLACE, 7);
    CHECK(walk(ADVICE_RECEPTION) == ADVICE_NONE && u16(pb() + AP_INSTANCE) == 7 && u16(pb() + AP_PLACE) == 7);
    /* a dead Krankenzimmer patient at place 3 with c8 = 0 and nothing else: +0xA4 = 10, not 3 */
    fresh();
    site()[6] = 1;
    put16(R(6, 0) + 8, 41);
    put16(R(6, 0) + 0x14 + 4 * 3, 300);
    P(300)[4] = 6, P(300)[104] = 1;
    CHECK(walk(ADVICE_RECEPTION) == ADVICE_NONE && u16(pb() + AP_PLACE) == 10);
    /* a room failing its room test leaves +0xA4 (the nurse missing) */
    put16(R(6, 0) + 8, 0);
    put16(pb() + AP_PLACE, 7);
    walk(ADVICE_RECEPTION);
    CHECK(u16(pb() + AP_PLACE) == 7);
}

static void test_theatres(void) {
    /* C17 / C18 / C19: the OP's patient */
    fresh();
    site()[7] = 1;
    for (int f = 6; f <= 10; f += 2) put16(R(7, 0) + f, 41);
    put16(R(7, 0) + 0x0C, 300);
    put16(R(7, 0) + 0x0E, 999), put16(R(7, 0) + 0x12, -1);
    CHECK(walk(ADVICE_RECEPTION) == 0x2B && has_text(2799));
    put16(R(7, 0) + 0x0E, 5);
    CHECK(walk(ADVICE_RECEPTION) == 0x2B && has_text(2797));
    ST(41)[11] = 0xFF;
    CHECK(walk(ADVICE_RECEPTION) != 0x2B);
    ST(41)[11] = 0;
    P(300)[43] = 4;
    g_image[TREATMENTS + 4 * 38 + 3] = 6; /* the operation needs organ 6 */
    CHECK(walk(ADVICE_RECEPTION) != 0x2B && walk(ADVICE_RECEPTION) != 0x37);
    uint8_t *m = sim_market(s, 4);
    m[0] = 16, m[1] = 1, m[2] = 6, m[8] = 0;
    g_image[0x4713 + 2 * 6] = 1;
    CHECK(walk(ADVICE_RECEPTION) == 0x37 && text0() == 2782 && has_text(1612) && has_text(3668));
    put32(H(0) + H_CASH, 19);
    CHECK(walk(ADVICE_RECEPTION) != 0x37);
    put32(H(0) + H_CASH, 984);
    site()[17] = 1; /* held in the bank: C18 */
    put16(sim_bank(s, 0) + 0x14, 1), sim_bank(s, 0)[0x58] = 6;
    CHECK(walk(ADVICE_RECEPTION) == 0x2B && has_text(2797));
    /* C20: no patient, a ward patient waits for the operation */
    fresh();
    site()[7] = 1, site()[6] = 1;
    for (int f = 6; f <= 10; f += 2) put16(R(7, 0) + f, 41);
    put16(R(6, 0) + 0x14, 300);
    P(300)[4] = 6, P(300)[43] = 4, P(300)[45] = 1;
    g_image[TREATMENTS + 4 * 38 + 1] = 1, g_image[TREATMENTS + 4 * 38 + 0x10] = 11;
    CHECK(walk(ADVICE_RECEPTION) == 0x2A && has_text(2802));
    ST(41)[11] = 0xFF;
    CHECK(walk(ADVICE_RECEPTION) != 0x2A);
    /* C21: the organ the scan found offered */
    ST(41)[11] = 0;
    g_image[TREATMENTS + 4 * 38 + 3] = 6;
    site()[17] = 1;
    uint8_t *mm = sim_market(s, 0);
    mm[0] = 16, mm[1] = 1, mm[2] = 6;
    CHECK(walk(ADVICE_RECEPTION) == 0x37);
    /* C22: the wanted organ H +0x86; held: cleared */
    fresh();
    site()[7] = 1, site()[17] = 1;
    H(0)[0x86] = 6;
    mm = sim_market(s, 0);
    mm[0] = 16, mm[1] = 1, mm[2] = 6;
    CHECK(walk(ADVICE_RECEPTION) == 0x37 && has_text(3668));
    put16(sim_bank(s, 0) + 0x14, 1), sim_bank(s, 0)[0x58] = 6;
    CHECK(walk(ADVICE_RECEPTION) != 0x37 && H(0)[0x86] == 0);
    /* C26 / C27 / C28 the torture chamber */
    fresh();
    site()[8] = 1;
    put16(R(8, 0) + 8, 41), put16(R(8, 0) + 10, 41), put16(R(8, 0) + 0x0C, 300);
    put16(R(8, 0) + 0x0E, 999), put16(R(8, 0) + 0x12, -1);
    CHECK(walk(ADVICE_RECEPTION) == 0x61 && has_text(2800));
    put16(R(8, 0) + 0x0E, 3);
    CHECK(walk(ADVICE_RECEPTION) == 0x61 && has_text(2794));
    put16(R(8, 0) + 0x0C, 0);
    site()[16] = 1;
    put16(R(16, 0) + 0x14, 301);
    P(301)[4] = 0x10, P(301)[43] = 4, P(301)[45] = 1;
    g_image[TREATMENTS + 4 * 38 + 0x10] = 0x47;
    CHECK(walk(ADVICE_RECEPTION) == 0x62 && has_text(2803));
    P(300)[104] = 1; /* C25: a dead patient; the stale byte [0x45C8] decides, not the free pathology places */
    put16(R(8, 0) + 0x0C, 300);
    CHECK(walk(ADVICE_RECEPTION) == 0x61 && has_text(2804)); /* C11's 0x273DE (no neuro-ward nurse) left it at 1 */
    put16(R(16, 0) + 8, 41);
    CHECK(walk(ADVICE_RECEPTION) != 0x61); /* nothing set it: no advice although the pathology is not asked */
}

static void test_expansion(void) {
    /* B22 / B23 / C5 / C12 / D1 / D7: the told-once expansion advice, chief only, hour < 8 */
    static const struct { uint8_t built[6], none, flag; int16_t advice, text; int32_t cash; } X[] = {
        {{5, 11, 15, 5, 0, 0}, 6, AP_TOLD_WARD, 0x1E, 2817, 2000},
        {{5, 11, 15, 6, 0, 0}, 17, AP_TOLD_BANK, 0x23, 2814, 2000},
        {{15, 0, 0, 0, 0, 0}, 16, AP_TOLD_NEURO_WARD, 0x20, 2816, 2000},
        {{5, 6, 0, 0, 0, 0}, 7, AP_TOLD_OP, 0x24, 2813, 2000},
        {{7, 0, 0, 0, 0, 0}, 9, AP_TOLD_ICU, 0x2E, 2793, 2000},
        {{6, 7, 9, 0, 0, 0}, 18, AP_TOLD_PATHOLOGY, 0x32, 2790, 2000},
    };
    for (size_t k = 0; k < sizeof X / sizeof *X; k++) {
        quiet_chief();
        time_at(3, 5, 7);
        for (int i = 0; i < 6 && X[k].built[i]; i++) site()[X[k].built[i]] = 1;
        if (k < 2) site()[5] = 2, site()[11] = 1, site()[15] = 1;
        put32(H(0) + H_CASH, X[k].cash);
        put32(H(0) + 0xE2, 1000); /* expansion reads the last scheduled expected-cash value */
        /* the rooms quiet: staffed, rated */
        static const uint8_t STAFFED[19][3] = {[3] = {8}, [5] = {8, 10}, [6] = {8}, [7] = {6, 8, 10}, [9] = {8},
                                               [11] = {8, 10}, [15] = {8, 10}, [16] = {8}, [17] = {8, 10}, [18] = {10}};
        for (int cat = 3; cat <= 18; cat++)
            for (int i = 0; i < 2; i++) {
                for (int f = 0; f < 3 && STAFFED[cat][f]; f++) put16(R(cat, i) + STAFFED[cat][f], 41);
                R(cat, i)[0] = 20;
            }
        put16(R(2, 0) + 8, 41);
        ST(41)[11] = 0xFF;
        static const uint8_t BLOCKS[6] = {6, 17, 16, 7, 9, 18}; /* the room whose existence silences the rule */
        site()[BLOCKS[k]]++;
        CHECK(walk(ADVICE_CHIEF) != X[k].advice);
        site()[BLOCKS[k]]--;
        if (k == 1 || k >= 4) {
            static const uint8_t NEEDS[6] = {0, 6, 0, 0, 7, 7}; /* a room the rule needs */
            site()[NEEDS[k]] = 0;
            CHECK(walk(ADVICE_CHIEF) != X[k].advice);
            site()[NEEDS[k]] = 1;
        }
        int a = walk(ADVICE_CHIEF);
        CHECK(a == X[k].advice && text0() == X[k].text && pb()[X[k].flag] == 0xFF);
        CHECK(walk(ADVICE_CHIEF) != X[k].advice); /* told once */
        pb()[X[k].flag] = 0;
        CHECK(walk(ADVICE_RECEPTION) != X[k].advice);
        time_at(3, 5, 8);
        CHECK(walk(ADVICE_CHIEF) != X[k].advice);
    }
}

static void test_d_rules(void) {
    /* D11: the pathologist idle with corpses stored; an autopsy done */
    fresh();
    site()[18] = 1;
    put16(R(18, 0) + 10, 41);
    CHECK(walk(ADVICE_RECEPTION) != 0x35); /* no corpse stored */
    put16(R(18, 0) + 6, 2);
    CHECK(walk(ADVICE_RECEPTION) == 0x35 && text0() == 2784);
    put16(R(18, 0) + 0x0C, 300), put16(R(18, 0) + 0x0E, 999);
    CHECK(walk(ADVICE_RECEPTION) == 0x36 && text0() == 2783);
    /* D13: the kitchen's furniture request and the cooking requests */
    quiet_chief();
    site()[13] = 1;
    fu_item(1, 1, 0), fu_item(2, 5, 0);
    put16(R(13, 0) + 6, 41);
    ST(42)[11] = 0;
    store_furniture(1);
    CHECK(walk(ADVICE_RECEPTION) == 0x5C && text0() == 2703 && R(13, 0)[4] == 1);
    put16(R(13, 0) + FURN, 1);
    put32(H(0) + H_CASH, 9);
    CHECK(walk(ADVICE_RECEPTION) != 0x48 && R(13, 0)[4] == 5); /* kind 5 missing, not in the store, cash 9 */
    put32(H(0) + H_CASH, 10);
    CHECK(walk(ADVICE_RECEPTION) == 0x48);
    put16(R(13, 0) + FURN + 2, 2);
    R(13, 0)[5] = 0xFF, R(13, 0)[2] = 0x2D;
    eq_item(14, 0, 0x2D);
    store_equipment(14);
    CHECK(walk(ADVICE_RECEPTION) == 0x5B && text0() == 2862 && has_text(2672));
    store_rec()[STORE_EQUIPMENT_HELD] = 0;
    put32(H(0) + H_CASH, 984);
    market(0, 0, 14);
    CHECK(walk(ADVICE_RECEPTION) == 0x3C && has_text(2714));
    R(13, 0)[5] = 0;
    CHECK(walk(ADVICE_RECEPTION) != 0x3C); /* not cooking */
    /* D14: seats; D15: beds; cash below 6 ends the walk */
    quiet_chief();
    put16(R(2, 0) + 0x16, 1);
    R(3, 0)[10] = 9;
    fu_item(2, 5, 0);
    store_furniture(2);
    CHECK(walk(ADVICE_CHIEF) == 0x4A && text0() == 2871 && has_text(2684) && has_text(2683));
    store_rec()[STORE_FURNITURE_HELD] = 0;
    CHECK(walk(ADVICE_CHIEF) == 0x48);
    put32(H(0) + H_CASH, 80);
    CHECK(walk(ADVICE_CHIEF) == 0x48);
    CHECK(walk(ADVICE_RECEPTION) != 0x48); /* chief only */
    R(3, 0)[10] = 10;
    CHECK(walk(ADVICE_CHIEF) != 0x48);
    quiet_chief();
    site()[6] = 1;
    put16(R(6, 0) + 8, 41);
    R(6, 0)[TYPE] = 11;
    g_image[CATALOG + 11 * CATALOG_SIZE + CATALOG_BASE] = 4;
    R(6, 0)[10] = 2;
    put32(H(0) + H_CASH, 5);
    CHECK(walk(ADVICE_CHIEF) == ADVICE_NONE && text0() == 2441); /* the walk ends: no D17 although the rating is 0 */
    put32(H(0) + H_CASH, 984);
    CHECK(walk(ADVICE_CHIEF) == 0x48 && has_text(2679));
    fu_item(3, 8, 0);
    store_furniture(3);
    CHECK(walk(ADVICE_CHIEF) == 0x4C && has_text(2678));
    CHECK(walk(ADVICE_RECEPTION) != 0x4C); /* beds below the occupancy only from the chief */
    R(6, 0)[12] = 3;
    CHECK(walk(ADVICE_RECEPTION) == 0x4C);
    R(6, 0)[10] = 4;
    CHECK(walk(ADVICE_CHIEF) == 0x4D && text0() == 2681 && has_text(2685));
    /* D17 and the rating rules: chief only; D18 tests the reception's free place */
    quiet_chief();
    R(2, 0)[0] = 19;
    CHECK(walk(ADVICE_CHIEF) == 0x49 && text0() == 2686);
    CHECK(walk(ADVICE_RECEPTION) == ADVICE_NONE);
    quiet_chief();
    site()[5] = 1;
    for (int f = 8; f <= 10; f += 2) put16(R(5, 0) + f, 41);
    for (int k = 0; k < 4; k++) put16(R(2, 0) + FURN + 2 * k, 1); /* the reception full */
    CHECK(walk(ADVICE_CHIEF) != 0x53); /* the treatment room has a free place, the reception none */
    put16(R(2, 0) + FURN, 0);
    for (int k = 0; k < 4; k++) put16(R(5, 0) + FURN + 2 * k, 1);
    CHECK(walk(ADVICE_CHIEF) == 0x53 && text0() == 2677);
    /* D22 waiting room, D24 ICU (no E test) */
    quiet_chief();
    R(3, 0)[0] = 0;
    CHECK(walk(ADVICE_CHIEF) == 0x4B && text0() == 2682);
    quiet_chief();
    site()[9] = 1;
    put16(R(9, 0) + 8, 41);
    put32(H(0) + H_CASH, 79);
    CHECK(walk(ADVICE_CHIEF) == 0x59 && text0() == 2673);
}

static void test_scheduled(void) {
    /* the background walk keeps no pb field, lights the strip, keeps its record writes */
    quiet_chief();
    put16(R(2, 0) + EQ, 0);
    ST(42)[11] = 0;
    pb()[AP_KIND] = 0x33;
    put16(pb() + AP_ADVICE, 0x1234);
    uint8_t before[200];
    memcpy(before, pb(), 200);
    advice_scheduled(s);
    CHECK(memcmp(before, pb(), 200) == 0 && s->help_strip && H(0)[0xDB] == 3);
    /* nothing to advise: the strip goes dark */
    put16(R(2, 0) + EQ, 1);
    advice_scheduled(s);
    CHECK(!s->help_strip);
    /* skipped while an advice is pending or the clock fast-forwards */
    put16(R(2, 0) + EQ, 0);
    s->advice_pending = 1;
    advice_scheduled(s);
    CHECK(!s->help_strip);
    s->advice_pending = 0, s->fast_forward = 1;
    advice_scheduled(s);
    CHECK(!s->help_strip);
    s->fast_forward = 0;
    CHECK(sim_call(s, 0x27495) && s->help_strip);
    /* the board's entries set [0x5C84] when an advice was found */
    CHECK(walk(ADVICE_RECEPTION) == 0x3C && s->advice_pending && get16(pb() + AP_ADVICE) == 0x3C);
}

/* ---- the newspaper ------------------------------------------------------------------------------------------ */

static int plural_is(const char *name, const char *want);
static uint8_t *item(int n) { return sim_news_item(s, n - 1); }
static Pieces g_t;
static int nt(int n, int head) { return news_text(s, n, head, &g_t); }
static int t_at(int i) { return i < g_t.n ? g_t.p[i].v : -1; }
static int t_kind(int i) { return i < g_t.n ? g_t.p[i].kind : -1; }
static int t_has(int n) {
    for (int i = 0; i < g_t.n; i++)
        if (g_t.p[i].kind == PC_TEXT && g_t.p[i].v == n) return 1;
    return 0;
}
static void news(int n, int type, int b21, int h) {
    memset(item(n), 0, 70);
    item(n)[0] = 2, item(n)[20] = (uint8_t)type, item(n)[21] = (uint8_t)b21, item(n)[8] = (uint8_t)h;
}

/* type 6, b21 = 1 (docs/ui-reports.md, "Type 6, b21 = 1 in detail"): the employee E = u16 10 lives in hospital 4, item
 * byte 8 names hospital 2 ("der" class, article 2664), and the text must use byte 8; no name in a headline; T386 after
 * the group; the pronouns {er} 2646 / {sein} 2648 / {seine} 2652 + E's sex (staff byte 0). Every piece is checked. */
static int seq_is(const int (*want)[2], int n) {
    if (g_t.n != n) {
        fprintf(stderr, "  pieces: %d, want %d\n", g_t.n, n);
        return 0;
    }
    for (int i = 0; i < n; i++)
        if (g_t.p[i].kind != want[i][0] || g_t.p[i].v != want[i][1]) {
            fprintf(stderr, "  piece %d: kind %d v %d, want kind %d v %d\n", i, g_t.p[i].kind, g_t.p[i].v, want[i][0], want[i][1]);
            return 0;
        }
    return 1;
}
static void test_thug_victim_texts(void) {
    enum { T = PC_TEXT, S = PC_STAFF, HN = PC_HOSPITAL };
    news(15, 6, 1, 2);
    put16(item(15) + 10, 60);
    ST(60)[1] = 1, ST(60)[3] = 4;
    for (int sex = 0; sex <= 1; sex++) {
        ST(60)[0] = (uint8_t)sex;
        int er = 2646 + sex, sein = 2648 + sex, seine = 2652 + sex;
        const int h0[][2] = {{T, 687}, {T, 1293}, {T, 688}};
        const int h1[][2] = {{T, 693}, {T, 1293}, {T, 386}, {T, 2664}, {HN, 2}, {T, 694}};
        const int h2[][2] = {{T, 700}, {T, 1293}, {T, 386}, {T, 2664}, {HN, 2}, {T, 701}};
        const int h3[][2] = {{T, 707}, {T, 1303}, {T, 386}, {T, 2664}, {HN, 2}};
        const int b0[][2] = {{T, 689}, {S, 60}, {T, 690}, {T, 1303}, {T, 386}, {T, 2664}, {HN, 2}, {T, 691}, {T, seine}, {T, 692}};
        const int b1[][2] = {{T, 695}, {S, 60}, {T, 696}, {T, 1303}, {T, 386}, {T, 2664}, {HN, 2}, {T, 697}, {T, sein},
                             {T, 698}, {S, 60}, {T, 699}};
        const int b2[][2] = {{T, 702}, {S, 60}, {T, 703}, {T, 1303}, {T, 386}, {T, 2664}, {HN, 2}, {T, 704}, {T, er}, {T, 705},
                             {T, seine}, {T, 706}};
        const int b3[][2] = {{T, 708}, {S, 60}, {T, 709}, {T, 1303}, {T, 386}, {T, 2664}, {HN, 2}, {T, 710}, {T, sein},
                             {T, 711}, {S, 60}, {T, 712}};
        item(15)[22] = 0;
        CHECK(nt(15, 1) && seq_is(h0, 3) && nt(15, 0) && seq_is(b0, 10));
        item(15)[22] = 1;
        CHECK(nt(15, 1) && seq_is(h1, 6) && nt(15, 0) && seq_is(b1, 12));
        item(15)[22] = 2;
        CHECK(nt(15, 1) && seq_is(h2, 6) && nt(15, 0) && seq_is(b2, 12));
        item(15)[22] = 3;
        CHECK(nt(15, 1) && seq_is(h3, 5) && nt(15, 0) && seq_is(b3, 12));
    }
    item(15)[22] = 4;
    CHECK(!nt(15, 1) && !nt(15, 0));
}

/* The templates the oracle's text builder 0x670C7 printed for the comparison's pages (docs/ui-reports.md, the table,
 * corrected 2026-10-19): 0x0B b21 1 b23 3 is H8 T720 (T719 never printed); 0x16's headline is chosen by b22 0 / 1,
 * any other b22 gives none; 0x17's chains with {Der}/{der} {prof} where the table had left them open. Person 400
 * is a woman (field 0 = 1): the pronoun texts are the base + 1. */
static void test_news_measured(void) {
    fresh();
    g_image[NAMES + 2 * 30 + 29] = 1; /* hospital 2: "der" class */
    enum { T = PC_TEXT, HN = PC_HOSPITAL, PE = PC_PERSON, PR = PC_PROFESSION, RW = PC_RAW };
    news(20, 0x0B, 1, 2);
    item(20)[23] = 3;
    const int thug[][2] = {{HN, 2}, {T, 720}};
    CHECK(nt(20, 1) && seq_is(thug, 2));
    news(21, 0x16, 4, 2);
    item(21)[22] = 0;
    const int p0[][2] = {{T, 1227}, {T, 2655}, {HN, 2}};
    CHECK(nt(21, 1) && seq_is(p0, 3));
    item(21)[22] = 1;
    const int p1[][2] = {{HN, 2}, {T, 1228}};
    CHECK(nt(21, 1) && seq_is(p1, 2));
    item(21)[22] = 32;
    CHECK(!nt(21, 1) && g_t.n == 0);
    CHECK(nt(21, 0) && t_at(0) == 1229); /* the body does not read b22 */
    item(21)[22] = 2;
    CHECK(!nt(21, 1));
    g_world.block[BLK_PATIENTS][400 * 106] = 1;
    news(22, 0x17, 2, 2);
    put16(item(22) + 16, 400);
    const int c2[][2] = {{T, 1269}, {PE, 400}, {T, 1270}, {T, 2645}, {T, 1271}, {T, 2655}, {HN, 2}, {T, 1272},
                         {T, 2639}, {T, 1273}, {T, 2645}, {RW, 0}, {PR, 400}, {T, 1274}};
    CHECK(nt(22, 0) && seq_is(c2, 14));
    item(22)[21] = 1;
    const int c1[][2] = {{T, 1261}, {PE, 400}, {T, 1262}, {T, 2645}, {T, 1263}, {PR, 400}, {T, 1264}, {T, 2664},
                         {HN, 2}, {T, 1265}, {T, 2647}, {T, 1266}, {T, 2647}, {T, 1267}, {T, 2649}, {T, 1268}};
    CHECK(nt(22, 0) && seq_is(c1, 16));
    item(22)[21] = 0, item(22)[23] = 2;
    const int c02[][2] = {{T, 1248}, {T, 2655}, {HN, 2}, {T, 1249}, {PE, 400}, {T, 1250}, {T, 2645}, {RW, 0},
                          {PR, 400}, {T, 1251}, {T, 2649}, {T, 1252}, {T, 2647}, {T, 1253}, {T, 2647}, {T, 1254}};
    CHECK(nt(22, 0) && seq_is(c02, 16));
    /* type 8, the last branch (b22 not 2, 3, 6, 7, 9, 16): text 1070 follows the second name with no space (code
     * 0x4113E-0x4114D, docs/ui-reports.md type 8, SIM-18), then {er} and 1071 */
    news(24, 8, 0, 2);
    put16(item(24) + 16, 400);
    const int d8[][2] = {{T, 1067}, {T, 2655}, {HN, 2}, {T, 1068}, {PC_ITEM_NAME, 23}, {T, 1069}, {PC_ITEM_NAME, 23},
                         {T, 1070}, {T, 2647}, {T, 1071}};
    CHECK(nt(24, 0) && seq_is(d8, 10));
}

/* The English CD's newspaper (docs/editions.md 7.3): rule C (four pronouns left out: 0x3F667 "sein/ihr" after 1267,
 * 0x3F6A0 "der/die" after 1270, and the "er/sie" at 0x3F807 and 0x411C8, read here as the ones after 1280 and 1070),
 * rule D (item 854-859 goes on with the hospital and 859), rule E (the name helpers clamp the index) and rule G (the
 * date). Person 400 is a woman. */
static void test_english_news(void) {
    fresh();
    g_prog.edition = EDITION_ENGLISH_CD;
    g_image[NAMES + 2 * 30 + 29] = 1; /* hospital 2: "der" class */
    g_image[NAMES + 1 * 30 + 29] = 2; /* hospital 1: "das" class */
    enum { T = PC_TEXT, HN = PC_HOSPITAL, PE = PC_PERSON, PR = PC_PROFESSION, RW = PC_RAW, IN = PC_ITEM_NAME };
    g_world.block[BLK_PATIENTS][400 * 106] = 1;
    news(22, 0x17, 2, 2);
    put16(item(22) + 16, 400);
    const int c2[][2] = {{T, 1269}, {PE, 400}, {T, 1270}, {T, 1271}, {T, 2655}, {HN, 2}, {T, 1272},
                         {T, 2639}, {T, 1273}, {T, 2645}, {RW, 0}, {PR, 400}, {T, 1274}};
    CHECK(nt(22, 0) && seq_is(c2, 13));
    item(22)[21] = 1;
    const int c1[][2] = {{T, 1261}, {PE, 400}, {T, 1262}, {T, 2645}, {T, 1263}, {PR, 400}, {T, 1264}, {T, 2664},
                         {HN, 2}, {T, 1265}, {T, 2647}, {T, 1266}, {T, 2647}, {T, 1267}, {T, 1268}};
    CHECK(nt(22, 0) && seq_is(c1, 15));
    /* 0x1B: no "er/sie" after 1280; the one after 1284 stays */
    news(23, 0x1B, 0, 2);
    put16(item(23) + 16, 400);
    const int w[][2] = {{T, 1278}, {T, 2655}, {HN, 2}, {T, 1279}, {PE, 400}, {T, 1280}, {T, 1281}, {T, 2645},
                        {T, 1282}, {PR, 400}, {T, 1283}, {PE, 400}, {T, 1284}, {T, 2647}, {RW, 0}, {T, 2670},
                        {HN, 2}, {T, 1285}};
    CHECK(nt(23, 0) && seq_is(w, 18));
    /* type 8, the last branch: no "er/sie" after 1070 */
    news(24, 8, 0, 2);
    put16(item(24) + 16, 400);
    const int d8[][2] = {{T, 1067}, {T, 2655}, {HN, 2}, {T, 1068}, {IN, 23}, {T, 1069}, {IN, 23}, {T, 1070},
                         {T, 1071}};
    CHECK(nt(24, 0) && seq_is(d8, 9));
    /* type 7, b22 1: after 858 the hospital name helper and 859 */
    news(25, 7, 0, 2);
    item(25)[22] = 1;
    put16(item(25) + 16, 400);
    const int d7[][2] = {{T, 854}, {IN, 24}, {T, 855}, {T, 2645}, {RW, 0}, {PR, 400}, {T, 856}, {T, 2664}, {HN, 2},
                         {T, 857}, {T, 2647}, {T, 858}, {T, 2664}, {HN, 2}, {T, 859}};
    CHECK(nt(25, 0) && seq_is(d7, 15));
    /* the name helpers clamp: a negative byte names hospital 1, 16 or more hospital 16 */
    news(26, 0x1D, 2, 0xFF);
    const int k1[][2] = {{T, 100}, {T, 2656}, {HN, 1}, {T, 101}};
    CHECK(nt(26, 1) && seq_is(k1, 4));
    item(26)[8] = 20;
    const int k16[][2] = {{T, 100}, {T, 2654}, {HN, 16}, {T, 101}};
    CHECK(nt(26, 1) && seq_is(k16, 4));
    item(26)[8] = 16;
    CHECK(nt(26, 1) && seq_is(k16, 4));
    item(26)[8] = 15;
    const int k15[][2] = {{T, 100}, {T, 2654}, {HN, 15}, {T, 101}};
    CHECK(nt(26, 1) && seq_is(k15, 4));
    /* the date: MM-DD-YYYY, a year later than the German date (the English original: 15.03.1996 -> 03-15-1997) */
    world_set_tail(&g_world, TAIL_YEAR, 1996);
    time_at(3, 15, 3);
    char date[11];
    news_date(s, date);
    CHECK(strcmp(date, "03-15-1997") == 0);
    world_set_tail(&g_world, TAIL_YEAR, 1995);
    time_at(1, 1, 3);
    news_date(s, date);
    CHECK(strcmp(date, "01-01-1996") == 0);
    /* the German builds: the pronouns stay, 854 ends after 858, no clamp, the German date */
    for (int ed = EDITION_DISK; ed <= EDITION_GERMAN_CD; ed++) {
        g_prog.edition = (Edition)ed;
        CHECK(nt(25, 0) && g_t.n == 12 && t_at(11) == 858);
        CHECK(nt(24, 0) && g_t.n == 10 && t_at(7) == 1070 && t_at(8) == 2647); /* no space before 1070 (SIM-18) */
        CHECK(nt(23, 0) && g_t.n == 19 && t_at(6) == 2647);
        item(26)[8] = 0xFF;
        const int kd[][2] = {{T, 100}, {T, 2654}, {T, 101}};
        CHECK(nt(26, 1) && seq_is(kd, 3));
        news_date(s, date);
        CHECK(strcmp(date, "01.01.1995") == 0);
    }
    g_prog.edition = EDITION_DISK;
}

/* SCR-9 (docs/ui-reports.md, "Run against the engine on every item type", "What the engine must change", and the
 * corrected rows of the type table): every piece of each corrected chain. Hospital 2 is of the "der" class (articles
 * base + 1); person 400 / 402 and employee 60 have the sex of the loop. */
static int nt_at(int *n, int head) { return news_text_at(s, n, head, &g_t); }
static void test_news_scr9(void) {
    fresh();
    g_image[NAMES + 2 * 30 + 29] = 1;
    enum { T = PC_TEXT, HN = PC_HOSPITAL, PE = PC_PERSON, PR = PC_PROFESSION, RW = PC_RAW, IN = PC_ITEM_NAME,
           NU = PC_NUM, S = PC_STAFF };
    /* (a) 0x27 stores b22 into the item number: item 1 (0x27, b21 0x24, b22 3) and item 3 (0x0B b21 1) */
    news(1, 0x27, 0x24, 2);
    item(1)[22] = 3;
    news(3, 0x0B, 1, 2);
    put16(item(3) + 10, 60), put16(item(3) + 12, 61);
    int n = 1;
    const int k24[][2] = {{T, 1851}, {NU, 3}, {T, 1852}, {T, 2655}, {HN, 2}};
    CHECK(nt_at(&n, 1) && seq_is(k24, 5) && n == 3);
    CHECK(nt_at(&n, 0) && t_at(0) == 721 && t_at(1) == 61 && t_at(3) == 60); /* the slot's body is item 3's */
    item(1)[22] = 0, n = 1;
    CHECK(nt_at(&n, 1) && n == 0 && !nt_at(&n, 0) && g_t.n == 0); /* b22 0: no body */
    item(1)[22] = 1, n = 1;
    const int k1[][2] = {{T, 1851}, {NU, 1}, {T, 1852}, {T, 2655}, {HN, 2}};
    CHECK(nt_at(&n, 1) && n == 1 && nt_at(&n, 0) && seq_is(k1, 5)); /* itself: the 0x27 text again */
    item(1)[21] = 0x21, item(1)[22] = 3, n = 1;
    CHECK(!nt_at(&n, 1) && n == 1); /* an offset the handler does not print stores nothing */
    item(1)[21] = 0x25, item(1)[22] = 3, n = 1;
    CHECK(nt_at(&n, 1) && n == 3);
    CHECK(news_text(s, 1, 0, &g_t) && t_at(1) == 1853); /* the plain call keeps the item */
    /* (b) 0x12 groups 3, 6, 7: {S u10} T386 [vom]H8 before T1154 / T1158 / T1159 */
    static const int GROUP[3][2] = {{3, 1154}, {6, 1158}, {7, 1159}};
    for (int g = 0; g < 3; g++) {
        news(4, 0x12, GROUP[g][0], 2);
        put16(item(4) + 10, 60);
        const int e[][2] = {{S, 60}, {T, 386}, {T, 2667}, {HN, 2}, {T, GROUP[g][1]}};
        CHECK(nt(4, 1) && seq_is(e, 5) && nt(4, 0) && seq_is(e, 5));
    }
    for (int sex = 0; sex <= 1; sex++) {
        P(400)[0] = P(402)[0] = (uint8_t)sex;
        int der = 2644 + sex, ihn = 2638 + sex, er = 2646 + sex;
        /* (c) type 5 b21 4: {der} " " {prof} after T660 */
        news(5, 5, 4, 2);
        put16(item(5) + 16, 400), put16(item(5) + 22, 77);
        const int b5[][2] = {{T, 657}, {T, 2670}, {HN, 2}, {T, 658}, {PE, 400}, {T, 659}, {NU, 77}, {T, 660}, {T, der},
                             {RW, 0}, {PR, 400}, {T, 661}, {T, 2670}, {HN, 2}, {T, 662}};
        CHECK(nt(5, 0) && seq_is(b5, 15));
        /* (d) type 7 b22 1: {der} " " {prof} after T855, not the name copy */
        news(6, 7, 2, 2);
        item(6)[22] = 1;
        put16(item(6) + 16, 400);
        const int b7[][2] = {{T, 854}, {IN, 5}, {T, 855}, {T, der}, {RW, 0}, {PR, 400}, {T, 856}, {T, 2664}, {HN, 2},
                             {T, 857}, {T, er}, {T, 858}};
        CHECK(nt(6, 0) && seq_is(b7, 12));
        /* (e) type 6 b21 0 bodies by b22, {P} the bare name */
        news(7, 6, 0, 2);
        put16(item(7) + 10, 402);
        const int s0[][2] = {{T, 739}, {PE, 402}, {T, 740}, {T, 2664}, {HN, 2}, {T, 741}, {T, der}, {T, 742}, {PR, 402},
                             {T, 743}};
        const int s1[][2] = {{T, 810}, {PE, 402}, {T, 811}, {T, 2664}, {HN, 2}, {T, 812}, {T, der}, {T, 813}, {PR, 402},
                             {T, 814}};
        const int s2[][2] = {{T, 816}, {PE, 402}, {T, 817}, {T, 2664}, {HN, 2}, {T, 818}, {T, ihn}, {T, 819}};
        const int s3[][2] = {{T, 821}, {PE, 402}, {T, 822}};
        const int s4[][2] = {{T, 826}, {T, 2664}, {HN, 2}, {T, 827}, {PE, 402}, {T, 828}};
        const int s5[][2] = {{T, 830}, {PE, 402}, {T, 831}, {T, er}, {T, 832}, {T, 2661}, {HN, 2}, {T, 833}};
        item(7)[22] = 0;
        CHECK(nt(7, 0) && seq_is(s0, 10));
        item(7)[22] = 1;
        CHECK(nt(7, 0) && seq_is(s1, 10));
        item(7)[22] = 2;
        CHECK(nt(7, 0) && seq_is(s2, 8));
        item(7)[22] = 3;
        CHECK(nt(7, 0) && seq_is(s3, 3));
        item(7)[22] = 4;
        CHECK(nt(7, 0) && seq_is(s4, 6));
        item(7)[22] = 5;
        CHECK(nt(7, 0) && seq_is(s5, 8));
        item(7)[22] = 6;
        CHECK(!nt(7, 0));
    }
    /* (e) 0x0E: T386 before [im]H8 in the b22 0 / b23 1 body; the instance printed + 1 (b22 2 prints 3) */
    news(8, 0x0E, 5, 2);
    item(8)[23] = 1;
    const int d1[][2] = {{T, 486}, {T, 472}, {T, 386}, {T, 2655}, {HN, 2}, {T, 487}};
    CHECK(nt(8, 0) && seq_is(d1, 6));
    item(8)[22] = 2;
    const int d21[][2] = {{T, 488}, {T, 2664}, {HN, 2}, {T, 489}, {T, 355}, {T, 460}, {NU, 3}, {T, 490}};
    CHECK(nt(8, 0) && seq_is(d21, 8));
    item(8)[23] = 0;
    const int d20[][2] = {{T, 464}, {T, 2664}, {HN, 2}, {T, 465}, {T, 355}, {T, 460}, {NU, 3}, {T, 466}, {T, 2664},
                          {HN, 2}, {T, 467}};
    CHECK(nt(8, 0) && seq_is(d20, 11));
    const int h20[][2] = {{HN, 2}, {T, 456}, {T, 355}, {T, 460}, {NU, 3}, {T, 458}};
    CHECK(nt(8, 1) && seq_is(h20, 6));
}

/* SCR-9 on the page (reports_ui.c): the body of a slot whose headline is a 0x27 item is item b22's, and the name copy
 * prints nothing when it is empty (no fallback to the person's name). Synthetic Biing: every text is "x", no images,
 * glyphs of width 0, the page picture a cached 1 x 1 image; the message buffer holds the last block drawn, slot 3's body. */
static void test_news_page_scr9(void) {
    static Biing b;
    static uint8_t charmap[256];
    static const char texts[] = "x$";
    static const char names[] = "Anna Probe$";
    static uint8_t table[4];
    memset(&b, 0, sizeof b);
    b.prog = g_prog;
    b.prog.charmap = charmap;
    world_alloc(&b.world);
    sim_init(&b.sim, &b.world, &b.prog, 5);
    b.texts.text = (const uint8_t *)texts, b.texts.text_size = 2;
    b.people.pools[0] = (TextPool){(const uint8_t *)names, sizeof names - 1, table, 1};
    b.sim.people = &b.people;
    b.screen.w = SCREEN_W, b.screen.h = SCREEN_H;
    b.ui.screen = &b.screen;
    b.ui.prog = &b.prog;
    b.ui.hover_rect = (Rect){0, 0, -1, -1};
    static uint8_t one = 1;
    static Image page = {1069, 1, 1, 0, 0, 0, &one, NULL};
    b.ui.cache[1069] = &page;
    for (int i = 1; i <= 4; i++) {
        uint8_t *it = sim_news_item(&b.sim, i - 1);
        it[0] = 2, it[20] = 0x27, it[21] = 0x25, it[22] = (uint8_t)(10 + i), it[8] = 0xFF; /* no hospital name */
    }
    uint8_t *issue = sim_news_issue(&b.sim);
    issue[0] = 1, issue[1] = 2, issue[2] = 3, issue[3] = 4;
    uint8_t *it2 = sim_news_item(&b.sim, 1); /* item 2: its b22 names item 3 */
    it2[22] = 3;
    issue[3] = 2; /* slot 3 = item 2: headline with 3, body = item 3's (b22 13) */
    CHECK(reports_routine(&b, 0x4704A) && b.ui.current == 118);
    CHECK(b.msg.len == 5 && memcmp(b.msg.buf, "x13 x", 5) == 0);
    it2[22] = 0; /* b22 0: no body */
    CHECK(reports_routine(&b, 0x4704A) && b.msg.len == 0);
    /* the name copy: empty prints nothing, a copy prints itself up to '$' or 0 */
    Pieces t;
    pieces_clear(&t);
    pc_item_name(&t, 5);
    uint8_t *it6 = sim_news_item(&b.sim, 5);
    put16(it6 + 16, 0); /* person 0: "Anna Probe" in the pool */
    it6[29] = '$';
    reports_render(&b, &t);
    CHECK(b.msg.len == 0);
    it6[29] = 0;
    reports_render(&b, &t);
    CHECK(b.msg.len == 0);
    memcpy(it6 + 29, "Erika Muster$Q", 14);
    reports_render(&b, &t);
    CHECK(b.msg.len == 12 && memcmp(b.msg.buf, "Erika Muster", 12) == 0);
    world_free(&b.world);
}

/* SCR-14 (docs/ui-reports.md section 3, "What the engine must change (SCR-14)", items 1-3): only frame x 88-547,
 * y 39-399 of the page reaches the screen, cut per pixel row; everything else keeps what the screen showed before.
 * The frame before is a pattern of colours 10-14, the glyphs are colour 7. */
enum { INK = 7 };
static uint8_t pattern(int x, int y) { return (uint8_t)(10 + (x + y) % 5); }
static void fill_pattern(Surface *sf) {
    sf->w = SCREEN_W, sf->h = SCREEN_H;
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++) sf->px[y * SCREEN_W + x] = pattern(x, y);
}
static void solid_font(Font *f, uint8_t *px, int width, int height) {
    memset(f, 0, sizeof *f);
    memset(px, INK, (size_t)width * height);
    f->height = height, f->width[0] = width, f->glyph[0] = px;
}
static uint8_t at(const Surface *sf, int x, int y) { return sf->px[y * sf->w + x]; }
static int column_kept(const Surface *sf, int x, int y0, int y1) {
    for (int y = y0; y <= y1; y++)
        if (at(sf, x, y) != pattern(x, y)) return 0;
    return 1;
}
static int column_ink(const Surface *sf, int x, int y0, int y1) {
    for (int y = y0; y <= y1; y++)
        if (at(sf, x, y) != INK) return 0;
    return 1;
}

static void test_paper_present_scr14(void) {
    static Surface shown, before;
    static uint8_t px6[3 * 9], px5[3 * 12], wide[460], left[10 * 12];
    Font f6, f5, fw, fl;
    solid_font(&f6, px6, 3, 9);
    solid_font(&f5, px5, 3, 12);
    solid_font(&fw, wide, 460, 1);
    solid_font(&fl, left, 10, 12);
    fill_pattern(&before);
    shown = before;
    /* item 1: a font-6 line at top 396 (rows 396-399 shown, 400-404 not), one at 405 (nothing), a font-5 line at 393 */
    draw_glyph(&shown, &f6, 0, 364, 396);
    draw_glyph(&shown, &f6, 0, 380, 405);
    draw_glyph(&shown, &f5, 0, 97, 393);
    /* item 2: a 460-pixel word at x 99 (slot 0's x) reaches x 558; the left edge from a glyph at x 80; the top edge */
    draw_glyph(&shown, &fw, 0, 99, 53);
    draw_glyph(&shown, &fl, 0, 80, 100);
    draw_glyph(&shown, &fl, 0, 200, 30);
    reports_paper_present(&shown, &before);
    CHECK(column_ink(&shown, 364, 396, 399) && column_kept(&shown, 364, 400, 404));
    CHECK(column_kept(&shown, 380, 405, 413));
    CHECK(column_ink(&shown, 97, 393, 399) && column_kept(&shown, 97, 400, 404));
    CHECK(at(&shown, 547, 53) == INK && at(&shown, 548, 53) == pattern(548, 53) && at(&shown, 99, 53) == INK);
    CHECK(at(&shown, 88, 100) == INK && at(&shown, 87, 100) == pattern(87, 100));
    CHECK(column_kept(&shown, 200, 30, 38) && column_ink(&shown, 200, 39, 41));
    /* item 3: rows 400-439 are exactly what they were */
    CHECK(memcmp(shown.px + 400 * SCREEN_W, before.px + 400 * SCREEN_W, 40 * SCREEN_W) == 0);
}

/* The rule on the page itself (reports_routine 0x4704A): the synthetic Biing of test_news_page_scr9 with fonts whose
 * slot-0 glyph is solid colour 7; font 6 (slot 3's body at 364, 263) is 142 rows tall, so its one line reaches frame
 * row 404, and font 3 (slot 0's headline at 99, 53) is 460 pixels wide. The frame before the page is the pattern. */
static void test_news_page_scr14(void) {
    static Biing b;
    static Surface before;
    static uint8_t charmap[256];
    static const char texts[] = "x$";
    static uint8_t px_small[1], px6[142], px3[460];
    memset(&b, 0, sizeof b);
    b.prog = g_prog;
    b.prog.charmap = charmap;
    world_alloc(&b.world);
    sim_init(&b.sim, &b.world, &b.prog, 5);
    b.texts.text = (const uint8_t *)texts, b.texts.text_size = 2;
    b.sim.people = &b.people;
    fill_pattern(&b.screen);
    before = b.screen;
    b.ui.screen = &b.screen;
    b.ui.prog = &b.prog;
    b.ui.hover_rect = (Rect){0, 0, -1, -1};
    static uint8_t one = 1;
    static Image page = {1069, 1, 1, 91, 39, 0, &one, NULL};
    b.ui.cache[1069] = &page;
    solid_font(&b.fonts[4], px_small, 1, 1);
    solid_font(&b.fonts[5], px_small, 1, 1);
    solid_font(&b.fonts[6], px6, 1, 142);
    solid_font(&b.fonts[3], px3, 460, 1);
    for (int i = 1; i <= 4; i++) {
        uint8_t *it = sim_news_item(&b.sim, i - 1);
        it[0] = 2, it[20] = 0x27, it[21] = 0x25, it[22] = (uint8_t)(10 + i), it[8] = 0xFF;
    }
    uint8_t *issue = sim_news_issue(&b.sim);
    issue[0] = 1, issue[1] = 2, issue[2] = 3, issue[3] = 4;
    sim_news_item(&b.sim, 3)[22] = 3; /* slot 3's body is item 3's ("x13 x", test_news_page_scr9) */
    CHECK(reports_routine(&b, 0x4704A) && b.ui.current == 118);
    CHECK(at(&b.screen, 91, 39) == 1); /* the page image itself */
    CHECK(column_ink(&b.screen, 364, 263, 399) && column_kept(&b.screen, 364, 400, 404)); /* slot 3's body, cut */
    CHECK(at(&b.screen, 547, 53) == INK && at(&b.screen, 548, 53) == pattern(548, 53)); /* slot 0's headline */
    CHECK(memcmp(b.screen.px + 400 * SCREEN_W, before.px + 400 * SCREEN_W, 40 * SCREEN_W) == 0);
    CHECK(memcmp(b.screen.px, before.px, 39 * SCREEN_W) == 0);
    world_free(&b.world);
}

/* The pieces composed as reports_render does for numbers, literals and the ")" (msg_over); every BITEXPZ entry shown
 * as <T n>, other pieces as <?>. */
static const char *composed(void) {
    static Message m;
    static char out[sizeof m.buf + 1];
    msg_clear(&m);
    for (int i = 0; i < g_t.n; i++) {
        const Piece *p = &g_t.p[i];
        char tag[16];
        if (p->kind == PC_NUM) msg_append_number(&m, p->v);
        else if (p->kind == PC_RAW) msg_append(&m, (const uint8_t *)p->s, strlen(p->s));
        else if (p->kind == PC_OVER) msg_over(&m, (uint8_t)p->s[0]);
        else {
            int n = p->kind == PC_TEXT ? snprintf(tag, sizeof tag, "<T%d>", (int)p->v) : snprintf(tag, sizeof tag, "<?>");
            msg_append(&m, (const uint8_t *)tag, (size_t)n);
        }
    }
    memcpy(out, m.buf, m.len);
    out[m.len] = 0;
    return out;
}

/* {bwh} (docs/ui-reports.md, "{bwh} in parentheses", SCR-3): the parenthesised form of types 0x10, 0x2A and 0x2B puts
 * the ")" over the third number's trailing space and the next text follows at once; the bare form of 0x0F and 0x14
 * keeps the space before T1078 / T1112. */
static void test_bwh(void) {
    fresh();
    static const int paren[3][2] = {{0x10, 155}, {0x2A, 2933}, {0x2B, 2941}};
    static const int bare[2][2] = {{0x0F, 1078}, {0x14, 1112}};
    static const int bytes[3][3] = {{7, 250, 9}, {145, 64, 102}, {0, 0, 0}};
    for (int k = 0; k < 3; k++) {
        uint8_t *st = g_world.block[BLK_STAFF] + 60 * 32;
        st[5] = (uint8_t)bytes[k][0], st[6] = (uint8_t)bytes[k][1], st[7] = (uint8_t)bytes[k][2];
        char want[64];
        for (int i = 0; i < 3; i++) {
            news(30, paren[i][0], 0, 2);
            put16(item(30) + 14, 60);
            snprintf(want, sizeof want, "<?> (%d %d %d)<T%d>", bytes[k][0], bytes[k][1], bytes[k][2], paren[i][1]);
            CHECK(nt(30, 0) && strstr(composed(), want));
        }
        for (int i = 0; i < 2; i++) {
            news(30, bare[i][0], 0, 2);
            put16(item(30) + 14, 60);
            snprintf(want, sizeof want, ">%d %d %d <T%d>", bytes[k][0], bytes[k][1], bytes[k][2], bare[i][1]);
            CHECK(nt(30, 0) && strstr(composed(), want));
        }
    }
    /* msg_over on an empty buffer appends */
    Message m;
    msg_clear(&m);
    msg_over(&m, ')');
    CHECK(m.len == 1 && m.buf[0] == ')');
}

static void test_newspaper(void) {
    fresh();
    g_image[NAMES + 2 * 30 + 29] = 1; /* hospital 2: "der" class */
    /* day 14's slot 0: type 0x1D b21 2 = T100 [im]H8 T101 */
    news(1, 0x1D, 2, 2);
    CHECK(nt(1, 1) && t_at(0) == 100 && t_at(1) == 2655 && t_kind(2) == PC_HOSPITAL && t_at(2) == 2 && t_at(3) == 101);
    /* slot 1: type 0x14 b21 1, b26 2: T1107 [des]H8 T1108 / T1109 n [des]H8 T1110 {F u22} ... */
    news(2, 0x14, 1, 2);
    item(2)[26] = 2;
    put16(item(2) + 14, 50), put16(item(2) + 22, 3), put16(item(2) + 10, 51), put16(item(2) + 24, 4);
    g_world.block[BLK_STAFF][50 * 32 + 5] = 95, g_world.block[BLK_STAFF][50 * 32 + 6] = 55, g_world.block[BLK_STAFF][50 * 32 + 7] = 90;
    CHECK(nt(2, 1) && t_at(0) == 1107 && t_at(1) == 2664 && t_at(3) == 1108);
    CHECK(nt(2, 0) && t_at(0) == 1109 && t_kind(1) == PC_NUM && t_at(1) == 2 && t_at(2) == 2664);
    int i95 = -1;
    for (int i = 0; i < g_t.n; i++)
        if (g_t.p[i].kind == PC_NUM && g_t.p[i].v == 95) i95 = i;
    CHECK(i95 > 0 && t_at(i95 + 1) == 55 && t_at(i95 + 2) == 90 && t_has(1112) && t_has(1115));
    /* slot 2: type 5 b21 1, bill 138: T680 H8 T681 / T683 {P} T684 138 T685 {P} T686 */
    news(3, 5, 1, 2);
    put16(item(3) + 22, 138), put16(item(3) + 16, 400);
    CHECK(nt(3, 1) && t_at(0) == 680 && t_kind(1) == PC_HOSPITAL && t_at(2) == 681);
    CHECK(nt(3, 0) && t_at(0) == 683 && t_kind(1) == PC_PERSON && t_at(1) == 400 && t_at(3) == 138 && t_at(6) == 686);
    item(3)[24] = 1;
    CHECK(nt(3, 1) && t_kind(0) == PC_HOSPITAL && t_at(1) == 682);
    /* slot 3: type 0x17 b21 0, b22 1, b23 3 */
    news(4, 0x17, 0, 2);
    item(4)[22] = 1, item(4)[23] = 3;
    CHECK(nt(4, 1) && t_kind(0) == PC_HOSPITAL && t_at(1) == 1233 && t_kind(2) == PC_PROFESSION && t_at(3) == 1234);
    CHECK(nt(4, 0) && t_at(0) == 1255 && t_has(1260));
    /* other plain templates */
    news(5, 0x23, 0, 2);
    put16(item(5) + 16, 401);
    CHECK(nt(5, 1) && t_kind(0) == PC_HOSPITAL && t_at(1) == 1059 && t_kind(2) == PC_PERSON);
    news(6, 4, 2, 2);
    item(6)[22] = 1;
    CHECK(nt(6, 1) && t_at(0) == 440 && t_at(1) == 2655);
    item(6)[22] = 0;
    CHECK(nt(6, 1) && t_at(0) == 438 && t_at(1) == 2664 && t_at(3) == 439);
    news(7, 0x30, 0, 2);
    CHECK(nt(7, 1) && t_kind(0) == PC_HOSPITAL && t_at(1) == 2986);
    /* type 0x15: the campaign's opinion change picks the verdict */
    news(8, 0x15, 0, 2);
    item(8)[22] = 1;
    put16(item(8) + 10, 2);
    CHECK(nt(8, 1) && t_at(1) == 1180 && t_at(2) == 1181 && t_at(3) == 1185);
    put16(item(8) + 10, -2);
    CHECK(nt(8, 1) && t_at(3) == 1184);
    put16(item(8) + 10, 1);
    CHECK(nt(8, 1) && t_at(3) == 1186);
    CHECK(nt(8, 0) && t_at(0) == 1187 && t_at(2) == 2404 && t_has(1195) && t_has(1197));
    item(8)[22] = 4;
    CHECK(!nt(8, 1) && g_t.n == 0); /* an unhandled selector: empty */
    /* type 3 b21 2 reads the hospital record */
    news(9, 3, 2, 2);
    put16(H(2) + 0x2A, 7), put16(H(2) + 0x38, 1);
    CHECK(nt(9, 0) && t_at(0) == 218 && t_at(1) == 7 && t_at(3) == 221);
    put16(H(2) + 0x38, 0);
    CHECK(nt(9, 0) && t_at(3) == 222);
    put16(H(2) + 0x38, 4);
    CHECK(nt(9, 0) && t_at(3) == 4 && t_at(4) == 220);
    /* unknown type, b21 out of range */
    news(10, 0x36, 0, 2);
    CHECK(!nt(10, 1));
    news(10, 2, 4, 2);
    CHECK(!nt(10, 1));
    news(10, 2, 3, 2);
    CHECK(nt(10, 1) && t_at(0) == 170 && nt(10, 0) && t_at(0) == 171);
    news(10, 0x26, 0x23, 2);
    CHECK(nt(10, 1) && t_at(0) == 1354 && t_at(2) == 1355);
    news(10, 0x26, 0x21, 2);
    CHECK(!nt(10, 1));
    /* {F} with the dative / accusative article and the plural flag by the class byte */
    news(11, 0x11, 0, 2);
    put16(item(11) + 22, 5), put16(item(11) + 24, 6);
    g_image[FURNITURE_CATALOG + 5 * 18 + 7] = 3; /* plural */
    g_image[EQUIPMENT_CATALOG + 6 * 20 + 7] = 1;
    CHECK(nt(11, 0) && t_has(1608) && t_has(1614));
    int noun = -1;
    for (int i = 0; i < g_t.n; i++)
        if (g_t.p[i].kind == PC_NOUN && g_t.p[i].v == 3705) noun = i;
    CHECK(noun > 0 && g_t.p[noun].flag == 0); /* the u22 after T1094 is the entry 0x4350C: no plural step (OTH-EN-6) */
    /* the death items: the name piece */
    news(12, 8, 1, 2);
    item(12)[22] = 9;
    CHECK(nt(12, 1) && t_at(0) == 861 && t_at(1) == 927);
    CHECK(nt(12, 0) && t_at(0) == 875 && t_at(3) == 876 && t_kind(4) == PC_ITEM_NAME);
    /* type 1 (three random parts), the thug items, the department, the best employee, the contest, a price key */
    news(13, 1, 2, 2);
    item(13)[22] = 3, item(13)[23] = 1;
    CHECK(nt(13, 1) && t_at(0) == 198 && t_at(1) == 207 && t_at(2) == 209);
    news(13, 1, 8, 2);
    CHECK(!nt(13, 1));
    news(14, 6, 0, 2);
    item(14)[22] = 4;
    put16(item(14) + 10, 402);
    P(402)[0] = 0; /* a man: "Stadtbekannte" + "r" */
    CHECK(nt(14, 1) && t_at(0) == 823 && t_at(1) == 824);
    P(402)[0] = 1;
    CHECK(nt(14, 1) && t_at(0) == 823 && t_at(1) != 824);
    item(14)[22] = 6;
    CHECK(!nt(14, 1));
    test_thug_victim_texts();
    news(16, 0x0B, 1, 2);
    item(16)[23] = 2;
    put16(item(16) + 10, 60), put16(item(16) + 12, 61);
    CHECK(nt(16, 1) && t_kind(0) == PC_HOSPITAL && t_at(1) == 717 && t_at(2) == 60 && t_at(3) == 718);
    CHECK(nt(16, 0) && t_at(0) == 721 && t_at(1) == 61 && t_at(3) == 60);
    item(16)[21] = 0;
    CHECK(nt(16, 0) && t_at(0) == 733 && t_at(1) == 61);
    news(17, 0x0E, 5, 2);
    item(17)[22] = 2;
    CHECK(nt(17, 1) && t_at(2) == 355 && t_at(3) == 460 && t_at(4) == 3 && t_at(5) == 458); /* the instance + 1 */
    item(17)[23] = 2;
    CHECK(!nt(17, 1));
    news(18, 0x12, 4, 2);
    CHECK(nt(18, 1) && t_at(0) == 1155 && t_has(1156));
    news(19, 0x24, 0, 2);
    put16(item(19) + 10, 30), put16(item(19) + 12, 30), put16(item(19) + 14, 20);
    CHECK(nt(19, 1) && t_at(0) == 1117 && nt(19, 0) && t_at(0) == 1125 && t_has(1130));
    put16(item(19) + 12, 29);
    CHECK(nt(19, 1) && t_at(1) == 1116 && nt(19, 0) && t_at(0) == 1118);
    int i5 = -1;
    for (int i = 0; i < g_t.n; i++)
        if (g_t.p[i].kind == PC_NUM && g_t.p[i].v == 5) i5 = i;
    CHECK(i5 > 0); /* 25 - day */
    news(20, 0x27, 0x25, 2);
    item(20)[22] = 7;
    CHECK(nt(20, 1) && t_kind(0) == PC_HOSPITAL && t_at(1) == 1853 && t_at(2) == 7 && t_at(3) == 1854);
    news(21, 0x28, 0, 2);
    put16(item(21) + 14, 2);
    CHECK(nt(21, 0) && t_has(1902) && !t_has(1901));
    put16(item(21) + 14, 1);
    CHECK(nt(21, 0) && t_has(1901));
    news(22, 0x16, 2, 2);
    CHECK(nt(22, 1) && t_at(0) == 1218 && t_at(3) == 1219);
    item(22)[22] = 1;
    CHECK(nt(22, 1) && t_kind(0) == PC_HOSPITAL && t_at(1) == 1220);
    news(23, 0x34, 1, 2);
    CHECK(nt(23, 1) && t_at(1) == 3009 && nt(23, 0) && t_at(0) == 3010);
    news(24, 0x35, 2, 2);
    CHECK(nt(24, 1) && t_at(0) == 3021 && nt(24, 0) && t_at(0) == 3022);
    item(24)[21] = 0;
    CHECK(nt(24, 1) && t_at(0) == 3019);
    /* the date and the issue test */
    world_set_tail(&g_world, TAIL_YEAR, 1995);
    time_at(1, 14, 3);
    char date[11];
    news_date(s, date);
    CHECK(strcmp(date, "14.01.1995") == 0);
    uint8_t *issue = sim_news_issue(s);
    issue[0] = issue[1] = issue[2] = 1;
    CHECK(!news_issue_ready(s));
    issue[3] = 1;
    CHECK(news_issue_ready(s));
    /* the plural "n" */
    CHECK(plural_is("Hamburger (1 Stueck)", "Hamburgern (1 Stueck)"));
    CHECK(plural_is("Kissen", "Kissen") && plural_is("Tisch", "Tischn") && plural_is("Glas (rund)", "Glas (rund)"));
    CHECK(plural_is("Teil)", "Teil)"));
}

/* ---- OTH-EN-6: the item-name helpers 0x43430-0x4350C (docs/editions.md 7.3.1) ---------------------------------- */

static int plural_is(const char *name, const char *want) {
    uint8_t out[64];
    size_t n = news_plural((const uint8_t *)name, strlen(name), out, sizeof out);
    if (n == strlen(want) && !memcmp(out, want, n)) return 1;
    fprintf(stderr, "  plural of \"%s\": \"%.*s\", want \"%s\"\n", name, (int)n, (const char *)out, want);
    return 0;
}
static int article_is(const char *name, const char *want) {
    Message m;
    msg_clear(&m);
    msg_english_article(&m, (const uint8_t *)name, strlen(name), (const uint8_t *)" ", 1);
    return m.len == strlen(want) && !memcmp(m.buf, want, m.len);
}
/* the pieces after text entry `after`: k pieces must be (kind, v[, flag]) */
static int after_is(int after, const int (*want)[3], int k) {
    for (int i = 0; i + k < g_t.n; i++) {
        if (g_t.p[i].kind != PC_TEXT || g_t.p[i].v != after) continue;
        for (int j = 0; j < k; j++) {
            const Piece *p = &g_t.p[i + 1 + j];
            if (p->kind != want[j][0] || p->v != want[j][1] || p->flag != want[j][2]) {
                fprintf(stderr, "  after T%d piece %d: kind %d v %d flag %d, want %d %d %d\n", after, j, p->kind, p->v,
                        p->flag, want[j][0], want[j][1], want[j][2]);
                return 0;
            }
        }
        return 1;
    }
    fprintf(stderr, "  T%d not found\n", after);
    return 0;
}

static void test_item_articles(void) {
    /* the test values of 7.3.1 on the two renderers: the English article (T386 = " ") and the plural step */
    CHECK(article_is("apple", "an ") && article_is("Umbrella", "an ") && article_is("yak", "an "));
    CHECK(article_is("table", "a ") && article_is("_ox", "a "));
    CHECK(plural_is("two chairs", "two chairs") && plural_is("Soup", "Soupn") && plural_is("Soup (hot)", "Soupn (hot)"));
    CHECK(plural_is("Beans (tin)", "Beans (tin)") && plural_is("Stew(hot)", "Sten (hot)") && plural_is("Pan)", "Pan)"));
    CHECK(plural_is("SoupS", "SoupSn") && plural_is("Ham_bur_ger (3 off)", "Ham_bur_gern (3 off)"));
    CHECK(plural_is("Bun_ (x)", "Bun_n (x)") && plural_is("Bun_", "Bun_n")); /* hyphenation marks count as bytes */

    /* through the text builder: furniture 1 class 0 ("apple"), 2 class 1, 3 class 2, 4 class 3; equipment 5 class 3, 6
     * class 2 */
    enum { T = PC_TEXT, N = PC_NOUN, A = PC_ARTICLE_EN };
    for (int ed = EDITION_DISK; ed <= EDITION_ENGLISH_CD; ed++) {
        fresh();
        g_prog.edition = (Edition)ed;
        int en = ed == EDITION_ENGLISH_CD;
        for (int id = 1; id <= 4; id++) g_image[FURNITURE_CATALOG + id * 18 + 7] = (uint8_t)(id - 1);
        g_image[EQUIPMENT_CATALOG + 5 * 20 + 7] = 3, g_image[EQUIPMENT_CATALOG + 6 * 20 + 7] = 2;
        /* 0x10: {F u24} after T152 and {F u22} after T153, dative with the step */
        news(30, 0x10, 0, 2);
        put16(item(30) + 24, 2), put16(item(30) + 22, 4);
        CHECK(nt(30, 0));
        const int f1de[][3] = {{T, 1608, 0}, {N, 3702, 0}}, f1en[][3] = {{A, 3702, 0}, {N, 3702, 0}};
        CHECK(after_is(152, en ? f1en : f1de, 2));
        const int f3de[][3] = {{T, 1610, 0}, {N, 3704, 1}}, f3en[][3] = {{N, 3704, 1}, {T, 153 + 1, 0}};
        CHECK(after_is(153, en ? f3en : f3de, 2));
        put16(item(30) + 24, 1), put16(item(30) + 22, 3);
        CHECK(nt(30, 0));
        const int a0de[][3] = {{T, 1607, 0}, {N, 3701, 0}}, a0en[][3] = {{A, 3701, 0}, {N, 3701, 0}};
        const int a2de[][3] = {{T, 1609, 0}, {N, 3703, 0}}, a2en[][3] = {{A, 3703, 0}, {N, 3703, 0}};
        CHECK(after_is(152, en ? a0en : a0de, 2) && after_is(153, en ? a2en : a2de, 2));
        /* 0x11: {E u24} after T1093 with the step, the 0x4350C site after T1094: accusative, no step */
        news(31, 0x11, 0, 2);
        put16(item(31) + 24, 5), put16(item(31) + 22, 4);
        CHECK(nt(31, 0));
        const int e5de[][3] = {{T, 1610, 0}, {N, 3505, 1}}, e5en[][3] = {{N, 3505, 1}, {T, 1094, 0}};
        CHECK(after_is(1093, en ? e5en : e5de, 2));
        const int b4de[][3] = {{T, 1614, 0}, {N, 3704, 0}}, b4en[][3] = {{N, 3704, 0}, {T, 1095, 0}};
        CHECK(after_is(1094, en ? b4en : b4de, 2));
        put16(item(31) + 22, 1);
        CHECK(nt(31, 0));
        const int b1de[][3] = {{T, 1611, 0}, {N, 3701, 0}}, b1en[][3] = {{A, 3701, 0}, {N, 3701, 0}};
        CHECK(after_is(1094, en ? b1en : b1de, 2));
        /* the other 0x4350C sites (0x13 after T1101, 0x18 after T1084, 0x2F after T2974) have no step either; the
         * accusative of 0x22 after T495 keeps it */
        news(32, 0x13, 0, 2);
        put16(item(32) + 22, 4);
        const int n4[][3] = {{N, 3704, 0}};
        CHECK(nt(32, 0) && after_is(1101, en ? n4 : b4de, en ? 1 : 2));
        news(32, 0x18, 0, 2);
        put16(item(32) + 22, 4);
        CHECK(nt(32, 0) && after_is(1084, en ? n4 : b4de, en ? 1 : 2));
        news(32, 0x2F, 0, 2);
        put16(item(32) + 22, 4);
        CHECK(nt(32, 0) && after_is(2974, en ? n4 : b4de, en ? 1 : 2));
        news(32, 0x22, 0, 2);
        put16(item(32) + 22, 4);
        const int s4de[][3] = {{T, 1614, 0}, {N, 3704, 1}}, s4en[][3] = {{N, 3704, 1}};
        CHECK(nt(32, 0) && after_is(495, en ? s4en : s4de, en ? 1 : 2));
        /* 0x1D b21 0 / 1: T96 / T98, the bare equipment name (no article, no step), T386, [im]H8, T97 / T99 */
        news(33, 0x1D, 0, 2);
        put16(item(33) + 14, 5);
        const int bed0[][3] = {{N, 3505, 0}, {T, 386, 0}, {T, 2654, 0}, {PC_HOSPITAL, 2, 0}, {T, 97, 0}};
        CHECK(nt(33, 0) && after_is(96, bed0, 5) && g_t.n == 6);
        put16(item(33) + 14, 6), item(33)[21] = 1;
        const int bed1[][3] = {{N, 3506, 0}, {T, 386, 0}, {T, 2654, 0}, {PC_HOSPITAL, 2, 0}, {T, 99, 0}};
        CHECK(nt(33, 0) && after_is(98, bed1, 5) && g_t.n == 6);
    }
    g_prog.edition = EDITION_DISK;
}

/* FIX-4 (docs/fixes.md): A24 off "Dein Golfraum" (2708), on "Deine Garage" (2707) */
static void test_fix4_garage_text(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 3 : 0);
        quiet_chief();
        fu_item(3, 8, 0);
        site()[10] = 1;
        ST(42)[11] = 0;
        market(1, 0, 3);
        store_furniture(3);
        put16(R(10, 0) + FURN, 3);
        eq_item(10, 0, 0x40), eq_item(11, 0, 0x41);
        store_equipment(10), store_equipment(11);
        put16(R(10, 0) + EQ, 10);
        CHECK(walk(ADVICE_RECEPTION) == 0x72 && text0() == (on ? 2707 : 2708) && has_text(2701) && has_text(2692));
    }
    fixes_set(0);
}

/* FIX-5 (docs/fixes.md): the corpse rules test the stale byte [0x45C8] (off) or the free pathology places (on) */
static void c29_plant(int stale, int free_places) {
    quiet_chief();
    if (stale) stale_one();
    if (free_places) {
        site()[18] = 1;
        put16(R(18, 0) + 8, free_places), put16(R(18, 0) + 6, 0);
    }
    put16(R(3, 0) + 0x14 + 4 * 2, 300);
    P(300)[4] = 3, P(300)[104] = 1;
    R(3, 0)[0x0F] = 0, R(3, 0)[0x10] = 1;
}
static void test_fix5_corpse_rules(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 4 : 0);
        c29_plant(1, 0); /* stale byte 1, no free pathology place: off C29, on not */
        CHECK((walk(ADVICE_RECEPTION) == 0x43) == !on);
        c29_plant(0, 2); /* stale byte 0, two free places: off not C29, on C29 */
        int a = walk(ADVICE_RECEPTION);
        CHECK((a == 0x43) == on);
        if (on) CHECK(text0() == 2871);
        /* a dead desk patient, stale byte 0, no free place: off C38 (text 2877), on neither C37 nor C38 */
        quiet_chief();
        ST(41)[11] = 0;
        put16(R(2, 0) + 0x0A, 300);
        P(300)[104] = 1;
        R(3, 0)[10] = 3;
        a = walk(ADVICE_RECEPTION);
        CHECK(on ? a != 9 : (a == 9 && text0() == 2877));
        /* the same with two free pathology places: off still C38 (stale byte 0), on C37 (text 2868) */
        site()[18] = 1;
        put16(R(18, 0) + 8, 2), put16(R(18, 0) + 6, 0);
        a = walk(ADVICE_RECEPTION);
        CHECK(a == 9 && text0() == (on ? 2868 : 2877));
    }
    fixes_set(0);
}

/* FIX-6 (docs/fixes.md): free places of a category without rooms: off the stale word, on 0 */
static void test_fix6_free_places(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 5 : 0);
        int b8 = 3;
        CHECK(advice_free_places(&b8, 0, 0) == !on && b8 == (on ? 0 : 3));
        b8 = 3;
        CHECK(advice_free_places(&b8, 1, 2) == 1 && b8 == 2);
        b8 = 3;
        CHECK(advice_free_places(&b8, 1, 0) == 0 && b8 == 0);
    }
    fixes_set(0);
}

/* FIX-7 (docs/fixes.md): D18 / D20 test the reception's / last dental record's free place (off) or their own (on) */
static void test_fix7_rating_place(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 6 : 0);
        quiet_chief();
        site()[5] = 1;
        for (int f = 8; f <= 10; f += 2) put16(R(5, 0) + f, 41);
        for (int k = 0; k < 4; k++) put16(R(2, 0) + FURN + 2 * k, 1); /* reception full, treatment room free */
        CHECK((walk(ADVICE_CHIEF) == 0x53) == on);
        put16(R(2, 0) + FURN, 0);
        for (int k = 0; k < 4; k++) put16(R(5, 0) + FURN + 2 * k, 1); /* treatment room full, reception free */
        CHECK((walk(ADVICE_CHIEF) == 0x53) == !on);
        /* D20, no dental station: off the reception's place, on the neurology's own */
        quiet_chief();
        site()[15] = 1;
        for (int f = 8; f <= 10; f += 2) put16(R(15, 0) + f, 41);
        for (int k = 0; k < 4; k++) put16(R(2, 0) + FURN + 2 * k, 1);
        CHECK((walk(ADVICE_CHIEF) == 0x55) == on);
        put16(R(2, 0) + FURN, 0);
        for (int k = 0; k < 4; k++) put16(R(15, 0) + FURN + 2 * k, 1);
        CHECK((walk(ADVICE_CHIEF) == 0x55) == !on);
    }
    fixes_set(0);
}

/* FIX-8 (docs/fixes.md): type 8's last branch, name "Xyzw": off T1070 right after the name, on a space between; an
 * empty name gets no space in either mode */
static void test_fix8_death_space(void) {
    enum { T = PC_TEXT, HN = PC_HOSPITAL, RW = PC_RAW, IN = PC_ITEM_NAME };
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 7 : 0);
        fresh();
        g_image[NAMES + 2 * 30 + 29] = 1;
        news(24, 8, 0, 2);
        put16(item(24) + 16, 400);
        g_world.block[BLK_PATIENTS][400 * 106] = 1;
        memcpy(item(24) + 29, "Xyzw$", 5);
        const int off[][2] = {{T, 1067}, {T, 2655}, {HN, 2}, {T, 1068}, {IN, 23}, {T, 1069}, {IN, 23}, {T, 1070},
                              {T, 2647}, {T, 1071}};
        const int fixed[][2] = {{T, 1067}, {T, 2655}, {HN, 2}, {T, 1068}, {IN, 23}, {T, 1069}, {IN, 23}, {RW, 0},
                                {T, 1070}, {T, 2647}, {T, 1071}};
        CHECK(nt(24, 0) && (on ? seq_is(fixed, 11) : seq_is(off, 10)));
        if (on) CHECK(t_kind(7) == PC_RAW && !strcmp(g_t.p[7].s, " "));
        item(24)[29] = '$'; /* an empty name: no space in both modes */
        CHECK(nt(24, 0) && seq_is(off, 10));
        memcpy(item(24) + 29, "Xyz $", 5); /* a name that ends in a space: none added */
        CHECK(nt(24, 0) && seq_is(off, 10));
    }
    fixes_set(0);
}

/* FIX-10 (docs/fixes.md): the plural step on English names: off flag 1 ("Soupn"), on 0; German keeps it */
static void test_fix10_english_plural(void) {
    enum { T = PC_TEXT, N = PC_NOUN };
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 9 : 0);
        for (int ed = EDITION_DISK; ed <= EDITION_ENGLISH_CD; ed++) {
            fresh();
            g_prog.edition = (Edition)ed;
            int en = ed == EDITION_ENGLISH_CD;
            for (int id = 1; id <= 4; id++) g_image[FURNITURE_CATALOG + id * 18 + 7] = (uint8_t)(id - 1);
            news(30, 0x10, 0, 2);
            put16(item(30) + 24, 2), put16(item(30) + 22, 4);
            CHECK(nt(30, 0));
            const int de[][3] = {{T, 1610, 0}, {N, 3704, 1}}, en_off[][3] = {{N, 3704, 1}, {T, 153 + 1, 0}},
                      en_on[][3] = {{N, 3704, 0}, {T, 153 + 1, 0}};
            CHECK(after_is(153, en ? (on ? en_on : en_off) : de, 2));
        }
    }
    g_prog.edition = EDITION_DISK;
    fixes_set(0);
}

static void test_rating_news_text(void) {
    for (int head = 0; head <= 1; head++)
        for (int param = 0; param < 2; param++) {
            fresh();
            int h = head ? 16 : 5;
            news(1, 3, param ? 255 : 0, h);
            world_hospital(&g_world, h)[1] = 67;
            world_hospital(&g_world, 2)[1] = 91;
            sim_rating_record(s, world_hospital(&g_world, 2));
            CHECK(nt(1, head) == !param);
            CHECK(sim_rating_byte(s) == 67 && s->rating_ref.block == 27 && s->rating_ref.offset == (size_t)h * 500);
            news(1, 1, 0, 2);
            nt(1, head);
            CHECK(sim_rating_byte(s) == 67); /* other handlers' hospital-name helpers are readers */
        }
}

/* SIM-102: advice consumes cached E, even when current costs/occupancy imply the opposite decision. Chief
 * beginner advice exposes the sign; all four independent entries must preserve every hospital record. */
static void test_advice_cached_cash(void) {
    static const int32_t CACHE[] = {-1, 0, 1, 12345678, INT32_MIN, 32768, -65536};
    for (int entry = ADVICE_SCHEDULED; entry <= ADVICE_CHIEF; entry++)
        for (size_t c = 0; c < sizeof CACHE / sizeof *CACHE; c++)
            for (int expensive = 0; expensive < 2; expensive++)
                for (int occupied = 0; occupied < 2; occupied++) {
                    quiet_chief();
                    site()[11] = 1;
                    pb()[AP_TOLD_TREATMENT] = 0;
                    ST(42)[11] = 0; /* manual store: missing reception item produces advice 56 */
                    put16(R(2, 0) + EQ, 0);
                    store_equipment(1);
                    put32(H(0) + H_CASH, 1000);
                    put16(H(0) + H_PLOTS_RENTED, expensive ? 1000 : 0);
                    put16(R(2, 0) + 0x16, occupied ? 261 : 0);
                    int32_t computed = sim_expected_cash(s, 0);
                    CHECK((computed < 0) == expensive); /* both opposite-sign controls are actually reached */
                    for (int h = 0; h < HOSPITAL_COUNT; h++) {
                        put32(H(h) + 0xE2, h ? (CACHE[c] < 0 ? 7 : -7) : CACHE[c]);
                        memset(H(h) + 0x1D0, 0x40 + h, 16);
                    }
                    uint8_t before[HOSPITAL_COUNT * HOSPITAL_SIZE];
                    CHECK(g_world.size[BLK_HOSPITALS] == sizeof before);
                    memcpy(before, g_world.block[BLK_HOSPITALS], sizeof before);
                    uint32_t raw = 0;
                    SimScript script = {.v = &raw, .n = 0};
                    s->rng_script = &script;
                    int beginner = entry == ADVICE_CHIEF && CACHE[c] >= 0;
                    int result = walk(entry);
                    s->rng_script = NULL;
                    CHECK(result == (beginner ? 4 : 56));
                    CHECK(text0() == (beginner ? 2447 : 2778));
                    CHECK(pb()[AP_TOLD_TREATMENT] == (beginner ? 255 : 0));
                    CHECK(g_adv.back == (entry == ADVICE_SCHEDULED ? -1 : entry == ADVICE_STRIP ? 2 : entry == ADVICE_RECEPTION ? 1 : 0));
                    CHECK(script.used == 0 && script.extra == 0);
                    CHECK(memcmp(g_world.block[BLK_HOSPITALS], before, sizeof before) == 0);
                }
    /* A pending scheduled request returns before clearing output or touching either player block/cache. */
    fresh();
    s->advice_pending = 1;
    put32(H(0) + 0xE2, INT32_MIN);
    uint8_t hospital_before[HOSPITAL_COUNT * HOSPITAL_SIZE], players_before[2][500];
    memcpy(hospital_before, g_world.block[BLK_HOSPITALS], sizeof hospital_before);
    for (int player = 0; player < 2; player++) {
        memset(g_world.block[BLK_PLAYER0 + player], 0x42 + player, 500);
        memcpy(players_before[player], g_world.block[BLK_PLAYER0 + player], 500);
    }
    memset(&g_adv, 0x55, sizeof g_adv);
    Advice output_before = g_adv;
    Sim sim_before = *s;
    CHECK(advice_walk(s, ADVICE_SCHEDULED, &g_adv) == ADVICE_NONE);
    CHECK(memcmp(&g_adv, &output_before, sizeof g_adv) == 0 && memcmp(s, &sim_before, sizeof *s) == 0);
    CHECK(memcmp(g_world.block[BLK_HOSPITALS], hospital_before, sizeof hospital_before) == 0);
    for (int player = 0; player < 2; player++) CHECK(memcmp(g_world.block[BLK_PLAYER0 + player], players_before[player], 500) == 0);
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "cached-cash")) {
        test_advice_cached_cash();
        test_beginner();
        test_expansion();
        test_scheduled();
        world_free(&g_world);
        if (failures) return 1;
        puts("[OK] cached cash and advice controls");
        return 0;
    }
    test_advice_cached_cash();
    test_rating_news_text();
    test_fix8_death_space();
    test_fix10_english_plural();
    test_fix4_garage_text();
    test_fix5_corpse_rules();
    test_fix6_free_places();
    test_fix7_rating_place();
    world_alloc(&g_world);
    test_baseline();
    test_new_game_baseline();
    test_report();
    test_beginner();
    test_store_staff();
    test_bank_pathology();
    test_furniture_requests();
    test_golf_garage();
    test_reception_rules();
    test_requests();
    test_staff_rules();
    test_patients();
    test_advice_u3();
    test_theatres();
    test_expansion();
    test_d_rules();
    test_scheduled();
    test_newspaper();
    test_bwh();
    test_news_measured();
    test_english_news();
    test_news_scr9();
    test_news_page_scr9();
    test_paper_present_scr14();
    test_news_page_scr14();
    test_item_articles();
    world_free(&g_world);
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] reports\n");
    return 0;
}
