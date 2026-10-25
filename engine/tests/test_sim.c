/* Simulation routines of docs/sim.md and the clock. Synthetic world and program image (see AGENTS.md). */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fixes.h"
#include "report.h"
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
    memset(g_world.block[BLK_ROOM_SLOTS], 0xFF, g_world.size[BLK_ROOM_SLOTS]);
    memset(g_world.block[BLK_JOB_OPENINGS], 0xFF, g_world.size[BLK_JOB_OPENINGS]);
    memset(g_image, 0, sizeof g_image);
    put16(g_image + 0x31F2 + 2 * 60 + 30, 40); /* room type 2 rents for 40 a day */
    g_prog.image.data = g_image;
    g_prog.image.size = sizeof g_image;
    world_set_tail(&g_world, TAIL_DAY, 1);
    world_set_tail(&g_world, TAIL_MONTH, 1);
    world_set_tail(&g_world, TAIL_YEAR, 1995);
    /* One AI hospital in business, without money: the AI picker always finds it, and with no cash the AI
     * routines change nothing the module A tests look at. Without one the picker falls back to a random
     * hospital after 999 tries and the AI acts on the test's hospitals (docs/sim-ai.md, the picker). */
    world_hospital(&g_world, 15)[0x15] = 0xFF;
    sim_init(&g_sim, &g_world, &g_prog, 7);
}

/* Every field of every byte-sized category lookup, including the stale category values used by pathology.
 * Distinct high-bit words catch swapped fields, truncation and signed conversion (docs/sim-ai.md, Tables). */
static void test_description_fields(void) {
    fresh();
    for (int cat = 0; cat < 256; cat++)
        for (int field = 0; field < 7; field++)
            put16(g_image + 0x4758 + 14 * cat + 2 * field, 0x8000 + cat * 7 + field);
    for (int cat = 0; cat < 256; cat++) {
        Description d = sim_description(&g_sim, cat);
        int base = 0x8000 + cat * 7;
        CHECK(d.furniture == base);
        CHECK(d.furniture_slots == base + 1);
        CHECK(d.equipment == base + 2);
        CHECK(d.equipment_slots == base + 3);
        CHECK(d.cond_a == base + 4);
        CHECK(d.cond_b == base + 5);
        CHECK(d.type == base + 6);
    }
}

/* Runs the clock until the next day change, which runs the day list (0x39730 first). */
static void run_to_next_day(void) {
    int day = world_tail(&g_world, TAIL_DAY);
    while (world_tail(&g_world, TAIL_DAY) == day) sim_tick(&g_sim);
}

static void test_daily_costs(void) {
    fresh();
    uint8_t *h3 = world_hospital(&g_world, 3);
    world_set_cash(&g_world, 3, 1000);
    uint8_t *room = g_world.block[BLK_ROOM_SLOTS] + 3 * 400;
    room[0] = 2, room[1] = 1; /* rented */
    room[2] = 2, room[3] = 0; /* owned: no rent */
    put16(h3 + 0x1C, 2); /* two rented plots */
    uint8_t *slot = g_world.block[14] + 3 * 4000;
    put16(slot, 5), slot[6] = 7; /* employee with wage 7 */
    put16(slot + 20, -1), slot[26] = 9; /* negative index: not employed */
    put32(h3 + 0x8C, 123); /* a day counter that is reset */
    run_to_next_day();
    CHECK(get32(h3 + 0x16) == 1000 - 40 - 44 - 7);
    CHECK(get32(h3 + 0x96) == 40 && get32(h3 + 0x9A) == 40);
    CHECK(get32(h3 + 0x9E) == 44 && get32(h3 + 0xA2) == 44);
    CHECK(get32(h3 + 0xA6) == 7 && get32(h3 + 0xAA) == 7);
    CHECK(get32(h3 + 0x8C) == 0);
    CHECK(slot[8] == 1); /* 0x2C00D: days employed */
    run_to_next_day();
    CHECK(get32(h3 + 0x96) == 40 && get32(h3 + 0x9A) == 80); /* day counter reset, total grows */
    CHECK(get32(h3 + 0x9E) == 88); /* plot day counter is never reset in the original */
    world_set_cash(&g_world, 3, 50);
    run_to_next_day();
    CHECK(get32(h3 + 0x16) == -1000); /* 50 - 91 < 0 */
}

static void test_job_ad_cost(void) {
    fresh();
    uint8_t *job = g_world.block[BLK_JOB_OPENINGS];
    job[0] = 2; /* one opening of hospital 2 */
    uint8_t *h2 = world_hospital(&g_world, 2);
    world_set_cash(&g_world, 2, 100);
    put32(h2 + 0x16E, 999);
    /* a full newspaper table: the price-key item 0x4C597 (which also counts +0x16E) gets no slot */
    for (int i = 0; i < 100; i++) sim_news_item(&g_sim, i)[0] = 2;
    for (int i = 0; i < 60 * SIM_TICKS_PER_MINUTE; i++) sim_tick(&g_sim); /* one hour */
    CHECK(get32(h2 + 0x16) == 99);
    CHECK(get32(h2 + 0xA6) == 1 && get32(h2 + 0xAA) == 1);
    CHECK(get32(h2 + 0x16E) == 0 && h2[0x172] == 1);
}

static void test_leaders_and_debt(void) {
    fresh();
    put16(world_hospital(&g_world, 1) + 0x10E, 5);
    put16(world_hospital(&g_world, 4) + 0x10E, 5); /* tie: the first wins */
    put16(world_hospital(&g_world, 6) + 0x10E, 9);
    world_set_cash(&g_world, 6, -5); /* in debt: not a leader, floored at -10,000 */
    world_hospital(&g_world, 7)[0x108] = 3;
    world_hospital(&g_world, 8)[0x108] = 3, world_hospital(&g_world, 8)[0xF4] = 1;
    for (int i = 0; i < 60 * SIM_TICKS_PER_MINUTE; i++) sim_tick(&g_sim);
    CHECK(world_tail(&g_world, 0x26) == 5 && world_tail(&g_world, 0x2A) == 1);
    CHECK(world_cash(&g_world, 6) == -10000);
    CHECK(world_hospital(&g_world, 7)[0x108] == 2 && world_hospital(&g_world, 8)[0x108] == 3);
}

static uint32_t calls_of(uint32_t routine) {
    for (int i = 0; i < g_sim.nroutines; i++)
        if (g_sim.routine[i] == routine) return g_sim.calls[i];
    return 0;
}

/* SIM-17 (docs/sim-drift8.md section 9): the home treatment 0x672A1 runs at minutes 7-56 and once from the hour list,
 * 9 times an hour; the engine also ran it at minute 60 (60 mod 60 = 0), 10 times. */
static void test_home_treatment_calls(void) {
    fresh();
    world_set_tail(&g_world, TAIL_HOUR, 10);
    world_set_tail(&g_world, TAIL_MINUTE, 0);
    uint32_t before = calls_of(0x672A1);
    for (int i = 0; i < 60 * SIM_TICKS_PER_MINUTE; i++) sim_tick(&g_sim);
    CHECK(calls_of(0x672A1) - before == 9);
    CHECK(world_tail(&g_world, TAIL_MINUTE) == 0 && world_tail(&g_world, TAIL_HOUR) == 11);
}

static void test_clock(void) {
    fresh();
    world_set_tail(&g_world, TAIL_DAY, 30);
    world_set_tail(&g_world, TAIL_MONTH, 12);
    world_set_tail(&g_world, TAIL_HOUR, 23);
    world_set_tail(&g_world, TAIL_MINUTE, 59);
    for (int i = 0; i < SIM_TICKS_PER_MINUTE - 1; i++) sim_tick(&g_sim);
    CHECK(world_tail(&g_world, TAIL_MINUTE) == 59); /* 79 ticks: still the same minute */
    sim_tick(&g_sim);
    CHECK(world_tail(&g_world, TAIL_MINUTE) == 0 && world_tail(&g_world, TAIL_HOUR) == 0);
    CHECK(world_tail(&g_world, TAIL_DAY) == 1 && world_tail(&g_world, TAIL_MONTH) == 1);
    CHECK(world_tail(&g_world, TAIL_YEAR) == 1996);
    for (long i = 0; i < 60L * 60 * SIM_TICKS_PER_MINUTE; i++) sim_tick(&g_sim);
    CHECK(world_tail(&g_world, TAIL_HOURS) == 51); /* capped */
}

typedef struct {
    struct { uint32_t routine; int minute, hour, day, month, year; } entry[32];
    unsigned count;
    SimScript course;
} ClockBoundary;

static void observe_clock_boundary(void *ctx, uint32_t routine) {
    ClockBoundary *seen = ctx;
    /* Script only the course offer; unrelated hourly draws keep their ordinary generator. */
    g_sim.rng_script = routine == 0x42714 ? &seen->course : NULL;
    unsigned i = seen->count++;
    if (i >= sizeof seen->entry / sizeof *seen->entry) return;
    seen->entry[i].routine = routine;
    seen->entry[i].minute = world_tail(&g_world, TAIL_MINUTE);
    seen->entry[i].hour = world_tail(&g_world, TAIL_HOUR);
    seen->entry[i].day = world_tail(&g_world, TAIL_DAY);
    seen->entry[i].month = world_tail(&g_world, TAIL_MONTH);
    seen->entry[i].year = world_tail(&g_world, TAIL_YEAR);
}

/* SIM-87: every hourly routine, including the late-hour call, sees 24:00 before the date advances.
 * A course offered there starts in 5 hours with draw 3; wrapping early to 00:00 would make that 4. */
static void test_midnight_call_order(void) {
    static const uint32_t minute[] = {0x1A4C5, 0x1C8A4, 0x6F6B2, 0x6FA5A, 0x597F9, 0x59DCA, 0x58BA3, 0x6EDA1, 0x57F15};
    static const uint32_t hourly[] = {0x5B25D, 0x2B718, 0x19A73, 0x672A1, 0x30AD9, 0x6ABB9, 0x5A3CD, 0x6CE07,
                                      0x3DC86, 0x3CDB3, 0x3091F, 0x4302C, 0x42714, 0x1AB70, 0x3B8B7, 0x2B6C3};
    static const uint32_t daily[] = {0x39730, 0x3DCCB, 0x2C00D};
    static const struct { int hour, day, month, final_day, final_month, final_year; } cases[] = {
        {22, 30, 12, 30, 12, 1995}, /* ordinary 23:00, including the late-hour call */
        {23, 18, 3, 19, 3, 1995},
        {23, 30, 3, 1, 4, 1995},
        {23, 30, 12, 1, 1, 1996}
    };
    static const uint32_t draws[] = {1, 0, 0, 3}; /* offer, general course, id 0, starting-time adjustment */
    for (unsigned c = 0; c < sizeof cases / sizeof *cases; c++) {
        fresh();
        world_set_tail(&g_world, TAIL_HOUR, cases[c].hour);
        world_set_tail(&g_world, TAIL_MINUTE, 59);
        world_set_tail(&g_world, TAIL_DAY, cases[c].day);
        world_set_tail(&g_world, TAIL_MONTH, cases[c].month);
        for (int i = 0; i < 20; i++) g_world.block[BLK_COURSES][10 * i] = 0xFF;
        g_image[0x40D9 + 2] = 17; /* synthetic course duration */
        ClockBoundary seen = {0};
        seen.course = (SimScript){draws, sizeof draws / sizeof *draws, 0, 0, NULL};
        g_sim.before_call = observe_clock_boundary;
        g_sim.before_call_ctx = &seen;
        g_sim.tick = SIM_TICKS_PER_MINUTE - 1;
        sim_tick(&g_sim);
        g_sim.before_call = NULL;
        g_sim.before_call_ctx = NULL;
        g_sim.rng_script = NULL;
        int midnight = cases[c].hour == 23;
        unsigned expected_count = (unsigned)(25 + (midnight ? 3 : 0));
        CHECK(seen.count == expected_count);
        for (unsigned i = 0; i < seen.count && i < expected_count; i++) {
            int phase = i < 9 ? 0 : i < 25 ? 1 : 2;
            CHECK(seen.entry[i].routine == (phase == 0 ? minute[i] : phase == 1 ? hourly[i - 9] : daily[i - 25]));
            CHECK(seen.entry[i].minute == (phase == 0 ? 60 : 0));
            CHECK(seen.entry[i].hour == (phase == 0 ? cases[c].hour : phase == 1 ? cases[c].hour + 1 : 0));
            CHECK(seen.entry[i].day == cases[c].day + (phase == 2));
            CHECK(seen.entry[i].month == cases[c].month && seen.entry[i].year == 1995);
        }
        CHECK(seen.course.used == 4 && seen.course.extra == 0);
        const uint8_t expected_course[] = {0, (uint8_t)(midnight ? 5 : 6), 17, 17, 0};
        CHECK(!memcmp(g_world.block[BLK_COURSES], expected_course, sizeof expected_course));
        CHECK(world_tail(&g_world, TAIL_MINUTE) == 0 && world_tail(&g_world, TAIL_HOUR) == (midnight ? 0 : 23));
        CHECK(world_tail(&g_world, TAIL_DAY) == cases[c].final_day);
        CHECK(world_tail(&g_world, TAIL_MONTH) == cases[c].final_month);
        CHECK(world_tail(&g_world, TAIL_YEAR) == cases[c].final_year);
    }
}

/* The clock routine's animation counters (docs/mainloop.md, "Rest of the clock routine"; docs/ui-sheets.md, section 3;
 * SCR-8): from the program start pb+0x3C advances first at game tick 120, then at 122, 124, ...; the redraw flags
 * pb+0x3B are set first at tick 120, then at 170, 220, ... Both players' blocks alike. */
static void test_animation_counters(void) {
    fresh();
    uint8_t *pb = g_world.block[BLK_PLAYER0], *pb1 = g_world.block[BLK_PLAYER1];
    int anim[8], na = 0, redraw[4], nr = 0;
    for (int t = 1; t <= 230; t++) {
        uint8_t a0 = pb[0x3C];
        pb[0x3B] = pb1[0x3B] = 0;
        sim_tick(&g_sim);
        if (pb[0x3C] != a0 && na < 8) anim[na++] = t;
        CHECK(pb[0x3C] == pb1[0x3C]);
        if (pb[0x3B] == 0xFF && nr < 4) redraw[nr++] = t;
        CHECK(pb[0x3B] == pb1[0x3B]);
    }
    CHECK(na == 8 && anim[0] == 120 && anim[1] == 122 && anim[2] == 124 && anim[7] == 134);
    CHECK(pb[0x3C] == (230 - 120) / 2 + 1); /* 120, 122, ..., 230 */
    CHECK(nr == 3 && redraw[0] == 120 && redraw[1] == 170 && redraw[2] == 220);
}

/* The day change advances the date before the day list runs (docs/mainloop.md, "Minute, hour, day, month"), so the
 * report baseline's score (0x3DCCB -> 0x1A78C) takes the new day's time term: with nothing else changed the report
 * says "Verlust von 96" (the original, oracle 2026-10-19: run1_long_6 to 00:00 gave 2456 -> 2360). The engine ran the
 * list first and reported "Nach wie vor". Across a month end the day and month terms still differ by 96. */
static void test_day_change_score(void) {
    static const int D[2][2] = {{11, 1}, {30, 1}}; /* day, month before midnight */
    for (int k = 0; k < 2; k++) {
        fresh();
        uint8_t *h0 = world_hospital(&g_world, 0), *pb0 = g_world.block[BLK_PLAYER0];
        put16(h0 + 0x15E, 1000); /* 16 x 1000: the score stays far above its floor */
        world_set_tail(&g_world, TAIL_DAY, D[k][0]);
        world_set_tail(&g_world, TAIL_MONTH, D[k][1]);
        world_set_tail(&g_world, TAIL_HOUR, 23);
        world_set_tail(&g_world, TAIL_MINUTE, 59);
        long before = 16000L - 96L * D[k][0] - 2880L * D[k][1]; /* the snapshot of the previous day change */
        put32(pb0 + RP_SCORE, (int32_t)before);
        for (int i = 0; i < SIM_TICKS_PER_MINUTE; i++) sim_tick(&g_sim);
        CHECK(world_tail(&g_world, TAIL_HOUR) == 0 && pb0[RP_ARMED] == 0xFF);
        int day = world_tail(&g_world, TAIL_DAY), month = world_tail(&g_world, TAIL_MONTH);
        CHECK(day == (k ? 1 : 12) && month == (k ? 2 : 1));
        CHECK(get32(pb0 + RP_SCORE) == 16000L - 96L * day - 2880L * month);
        CHECK(get32(pb0 + RP_SCORE_CHANGE) == -96);
    }
}

static void staff_world(void);

/* SIM-21 (docs/sim-long.md, "What the engine must change" item 1): at hour 24 the day word advances, the day list runs,
 * and only then do the month and year wrap. Every other score term is fixed so that the score before the time term is
 * 40,000; the report baseline's score (RP_SCORE) shows the date the day list saw. */
static void test_day_list_before_wrap(void) {
    static const struct {
        int day, month, seen_day, seen_month, day_after, month_after, year_after;
    } C[3] = {{30, 12, 31, 12, 1, 1, 1996}, {30, 3, 31, 3, 1, 4, 1995}, {29, 3, 30, 3, 30, 3, 1995}};
    static const long SCORE[3] = {2464, 28384, 28480};
    for (int k = 0; k < 3; k++) {
        fresh();
        uint8_t *h0 = world_hospital(&g_world, 0), *pb0 = g_world.block[BLK_PLAYER0];
        put16(h0 + 0x15E, 2500); /* 16 x 2,500 = 40,000 */
        world_set_tail(&g_world, TAIL_DAY, C[k].day);
        world_set_tail(&g_world, TAIL_MONTH, C[k].month);
        world_set_tail(&g_world, TAIL_HOUR, 23);
        world_set_tail(&g_world, TAIL_MINUTE, 59);
        for (int i = 0; i < SIM_TICKS_PER_MINUTE; i++) sim_tick(&g_sim);
        CHECK(pb0[RP_ARMED] == 0xFF);
        CHECK(get32(pb0 + RP_SCORE) == 40000L - 96L * C[k].seen_day - 2880L * C[k].seen_month);
        CHECK(get32(pb0 + RP_SCORE) == SCORE[k]);
        CHECK(world_tail(&g_world, TAIL_DAY) == C[k].day_after && world_tail(&g_world, TAIL_MONTH) == C[k].month_after);
        CHECK(world_tail(&g_world, TAIL_YEAR) == C[k].year_after);
    }
}

/* SIM-21, docs/sim-long.md section 7: a 31.12. -> 01.01. change where the December term floors the score. The 01.01.
 * report shows score 0 and change +0 (computed with 31.12.), the gain shows on 02.01. The old order gave 01.01. the
 * January score at once (here 27,024, change +27,024). */
static void test_year_change_report(void) {
    fresh();
    uint8_t *h0 = world_hospital(&g_world, 0), *pb0 = g_world.block[BLK_PLAYER0];
    put16(h0 + 0x15E, 1875); /* 16 x 1,875 = 30,000, below December's 37,536 */
    world_set_tail(&g_world, TAIL_DAY, 30);
    world_set_tail(&g_world, TAIL_MONTH, 12);
    world_set_tail(&g_world, TAIL_HOUR, 23);
    world_set_tail(&g_world, TAIL_MINUTE, 0);
    put32(pb0 + RP_SCORE, 0); /* 30.12.'s own report: floored as well */
    run_to_next_day();
    CHECK(world_tail(&g_world, TAIL_DAY) == 1 && world_tail(&g_world, TAIL_MONTH) == 1);
    CHECK(world_tail(&g_world, TAIL_YEAR) == 1996);
    CHECK(get32(pb0 + RP_SCORE) == 0 && get32(pb0 + RP_SCORE_CHANGE) == 0);
    run_to_next_day();
    CHECK(world_tail(&g_world, TAIL_DAY) == 2);
    CHECK(get32(pb0 + RP_SCORE) == 30000 - 96 * 2 - 2880);
    CHECK(get32(pb0 + RP_SCORE_CHANGE) == 30000 - 96 * 2 - 2880);
}

/* SIM-22 (docs/sim-long.md item 2, docs/sim-patients.md 0x1AA17 step 4): every person at home with opinion 25 of every
 * hospital; the person P the routine will draw (predicted with a twin) gets its opinion of the drawn h. Month 3, 10:00,
 * the player has a reception. */
static void test_opinion_drift_stops_at_60(void) {
    static const int PV[3] = {65, 20, 59};
    for (int k = 0; k < 3; k++) {
        fresh();
        world_set_tail(&g_world, TAIL_MONTH, 3), world_set_tail(&g_world, TAIL_DAY, 10);
        world_set_tail(&g_world, TAIL_HOUR, 10);
        g_world.block[BLK_ROOM_COUNTS][2] = 1; /* hospital 0's reception */
        for (int h = 1; h <= 15; h++) world_hospital(&g_world, h)[0x15] = 0xFF;
        uint8_t *pat = g_world.block[BLK_PATIENTS];
        for (int i = 0; i < 1009; i++) pat[i * 106 + 3] = 0xFF, memset(pat + i * 106 + 20, 25, 16), pat[i * 106 + 20] = 50;
        Sim twin = g_sim;
        int p = (int)sim_random(&twin, 1008) + 1, h = (int)sim_random(&twin, 15) + 1;
        pat[p * 106 + 20 + h] = (uint8_t)PV[k];
        int r1 = 0, q = 0;
        if (PV[k] < 60) {
            r1 = (int)sim_bits(&twin, 1) + (int)sim_bits(&twin, 1);
            if (PV[k] + 2 + r1 < 30) q = (int)sim_random(&twin, 1008) + 1;
        }
        uint8_t before[1009 * 106];
        memcpy(before, pat, sizeof before);
        sim_patients_run(&g_sim, 0x1AA17);
        CHECK(sim_bits(&g_sim, 0xFFFF) == sim_bits(&twin, 0xFFFF)); /* the same draws as the twin, no more */
        int changed = 0;
        for (int i = 0; i < (int)sizeof before; i++) changed += pat[i] != before[i];
        if (k == 0) CHECK(changed == 0 && pat[p * 106 + 20 + h] == 65); /* was Q 25 -> 32 */
        if (k == 1) {
            CHECK(q && q != p);
            CHECK(pat[p * 106 + 20 + h] == 20 + 2 + r1 + 4);
            CHECK(pat[q * 106 + 20 + h] == 32);
            CHECK(changed == 2);
        }
        if (k == 2) CHECK(changed == 1 && pat[p * 106 + 20 + h] == 61 + r1);
    }
}

/* SIM-23 (docs/sim-long.md item 3, docs/sim-staff.md "The opening"): the group-1 opening's minimum bust below 100
 * becomes 80. One woman in the pool with bust B; AI hospital 15 asks for group 1 (+0xE0); February. The twin tells
 * which path (t&7) each call takes. */
static void test_nurse_ad_minimum_bust(void) {
    static const int B[3] = {110, 90, 125};
    for (int k = 0; k < 3; k++) {
        staff_world();
        world_set_tail(&g_world, TAIL_MONTH, 2), world_set_tail(&g_world, TAIL_DAY, 3);
        uint8_t *w = g_world.block[BLK_STAFF] + 9 * 32;
        w[0] = 1, w[1] = 1, w[5] = (uint8_t)B[k], w[18] = 12;
        int seen[2] = {0};
        for (int n = 0; n < 200; n++) {
            memset(g_world.block[BLK_JOB_OPENINGS], 0xFF, g_world.size[BLK_JOB_OPENINGS]);
            world_hospital(&g_world, 15)[0xE0] = 1;
            Sim twin = g_sim;
            CHECK(sim_pick(&twin) == 15);
            sim_bits(&twin, 3);
            int zero = B[k] < 120 && sim_bits(&twin, 7) == 0;
            sim_staff_run(&g_sim, 0x62D6A);
            const uint8_t *o = g_world.block[BLK_JOB_OPENINGS];
            CHECK(o[0] == 15 && o[1] == 1);
            if (B[k] >= 120) {
                CHECK(o[3] == 105 && o[5] == 1);
            } else {
                CHECK(o[3] == 80 && o[5] == 2); /* was B - 20 on the t&7 = 0 path */
                if (zero) CHECK(o[2] == 13); /* D + 1 */
            }
            seen[zero]++;
        }
        if (B[k] < 120) CHECK(seen[0] > 0 && seen[1] > 0);
    }
}

/* SIM-24 (docs/sim-long.md item 4, docs/sim-staff2.md "Mood, working rules"): with wage = demand both compares take
 * the hours on duty H as a signed byte. Results: 2 rise, 0 none, 1 loss. */
static void test_mood_working_signed_hours(void) {
    fresh();
    StaffId employee = {1};
    StaffSlotId place = {0, 0};
    uint8_t *st = staff_record_view(&g_world, employee).data, *sl = staff_slot_view(&g_world, place).data;
    put16(sl, employee.value);
    st[19] = 10, st[18] = 10, st[14] = 0; /* W = D, stamina 0: the stamina draw always gives a loss */
    for (int n = 0; n < 64; n++) {
        sl[10] = 200; /* -56: a >= H at once, one draw */
        Sim twin = g_sim;
        sim_bits(&twin, 7);
        CHECK(sim_mood_working(&g_sim, employee, place) == 2);
        CHECK(sim_bits(&g_sim, 0xFFFF) == sim_bits(&twin, 0xFFFF));
        sl[10] = 128;
        CHECK(sim_mood_working(&g_sim, employee, place) == 2);
        sl[10] = 127; /* a <= 7 and b <= 31 never reach it: the stamina draw decides */
        twin = g_sim;
        sim_bits(&twin, 7), sim_bits(&twin, 15), sim_bits(&twin, 63);
        CHECK(sim_mood_working(&g_sim, employee, place) == 1);
        CHECK(sim_bits(&g_sim, 0xFFFF) == sim_bits(&twin, 0xFFFF));
    }
    st[19] = 11; /* W > D, H = 200: loss at once (unsigned, H >= 24) */
    sl[10] = 200;
    Sim twin = g_sim;
    CHECK(sim_mood_working(&g_sim, employee, place) == 1);
    CHECK(sim_bits(&g_sim, 0xFFFF) == sim_bits(&twin, 0xFFFF));
}

static void test_random_range(void) {
    fresh();
    int seen[5] = {0};
    for (int i = 0; i < 1000; i++) {
        uint32_t v = sim_random(&g_sim, 5);
        CHECK(v < 5);
        if (v < 5) seen[v]++;
    }
    for (int k = 0; k < 5; k++) CHECK(seen[k] > 120); /* roughly uniform */
    CHECK(sim_random(&g_sim, 1) == 0 && sim_random(&g_sim, 0) == 0);
}

static void test_opinion_score_ranking(void) {
    fresh();
    uint8_t *pat = g_world.block[BLK_PATIENTS];
    for (int i = 0; i < 1009; i++) memset(pat + i * 106 + 20, 50, 16);
    pat[0 * 106 + 20 + 2] = 255; /* record 0 is not counted */
    pat[1 * 106 + 20 + 2] = 80; /* >= 70 */
    pat[2 * 106 + 20 + 2] = 10; /* < 30 */
    uint8_t *h2 = world_hospital(&g_world, 2);
    put16(h2 + 0x166, 99); /* cleared with the dword at +0x164 */
    world_set_cash(&g_world, 5, -1);
    sim_opinion_sum(&g_sim);
    CHECK(get32(h2 + 0x44) == 1008 * 50 + 30 - 40);
    CHECK(h2[0x164] == 1 && h2[0x166] == 1);
    CHECK(get32(world_hospital(&g_world, 5) + 0x44) == 0); /* negative cash */
    CHECK(get32(world_hospital(&g_world, 3) + 0x44) == 1008 * 50);

    world_set_tail(&g_world, TAIL_HOUR, 2);
    world_set_tail(&g_world, TAIL_DAY, 1);
    world_set_tail(&g_world, TAIL_MONTH, 1);
    put16(world_hospital(&g_world, 3) + 0x28, 10); /* cured: +2 each */
    sim_score(&g_sim);
    /* 50400 x 128 / 1008 = 6400; + 20 cured; - 4 x 2 - 96 - 2880 */
    CHECK(get32(world_hospital(&g_world, 3) + 0x44) == 6400 + 20 - 8 - 96 - 2880);
    CHECK(get32(world_hospital(&g_world, 5) + 0x44) == 0); /* 0 - 1/8 - time, floored at 0 */

    uint8_t order[16];
    sim_ranking(&g_sim, order);
    CHECK(order[0] == 3); /* the cured ones lead */
    CHECK(order[1] == 0 && order[2] == 1); /* ties keep the hospital order */
}

/* AI building (docs/sim-ai.md): type 0 = category 1, rent 50, price 300, 3 plots. Hospital 15 is the AI. */
static void ai_world(int32_t cash) {
    fresh();
    for (int i = 0; i < 18; i++) g_image[0x4746 + i] = 0; /* the AI room list: only type 0 */
    put16(g_image + 0x31F2 + 0x1E, 50);
    put16(g_image + 0x31F2 + 0x20, 300);
    g_image[0x31F2 + 0x26] = 3;
    g_image[0x31F2 + 0x2A] = 1;
    world_set_cash(&g_world, 15, cash);
}

static void test_ai_orders_plots_then_rents(void) {
    ai_world(900);
    uint8_t *r = world_hospital(&g_world, 15);
    sim_ai_run(&g_sim, 0x6337D);
    CHECK(get16(r + 0xDE) == 3 && world_cash(&g_world, 15) == 900); /* no plots: order 3, pay nothing */
    sim_ai_run(&g_sim, 0x1C8E2);
    CHECK(get16(r + 0x1C) == 3 && get16(r + 0xDE) == 0 && world_cash(&g_world, 15) == 900 - 132);
    sim_ai_run(&g_sim, 0x6337D);
    const uint8_t *slot = g_world.block[BLK_ROOM_SLOTS] + 15 * 400;
    CHECK(slot[0] == 0 && slot[1] == 1); /* rented, because 900 - 132 < 32 x 300 */
    CHECK(world_cash(&g_world, 15) == 900 - 132 - 50);
    CHECK(get32(r + 0x96) == 50 && get32(r + 0x9A) == 50 && get16(r + 0x1E) == 3);
    CHECK(g_world.block[BLK_ROOM_COUNTS][15 * 100 + 1] == 1);
}

static void test_ai_buys_when_rich(void) {
    ai_world(20000);
    uint8_t *r = world_hospital(&g_world, 15);
    put16(r + 0x1A, 3); /* three owned plots */
    sim_ai_run(&g_sim, 0x6337D);
    const uint8_t *slot = g_world.block[BLK_ROOM_SLOTS] + 15 * 400;
    CHECK(slot[0] == 0 && slot[1] == 0); /* bought: cash > 32 x price */
    CHECK(world_cash(&g_world, 15) == 20000 - 375 && get32(r + 0x14E) == 375 && get32(r + 0x152) == 375);
}

/* Module B (docs/sim-patients.md). A person 5 with an illness that needs a treatment room. */
static uint8_t *patient(int i) { return g_world.block[BLK_PATIENTS] + i * 106; }

static void patient_world(void) {
    fresh();
    world_set_tail(&g_world, TAIL_HOUR, 10);
    for (int i = 1; i < 1009; i++) {
        uint8_t *p = patient(i);
        p[1] = 1, p[3] = 0xFF, p[44] = 100;
        memset(p + 20, 50, 16);
    }
    /* illness 7: type 0, byte 4 non-zero (treatment room chain), treatments 11-14 = 30, 31, 32, 33 */
    uint8_t *ill = g_image + 0x954 + 7 * 20;
    ill[4] = 1, ill[8] = 0, ill[11] = 30, ill[12] = 31, ill[13] = 32, ill[14] = 33;
    g_image[0x270C + 30 * 38 + 7] = 2; /* treatment 30 takes 2 days a step */
    uint8_t *p = patient(5);
    p[56] = 7, p[41] = 3, p[44] = 60;
    for (int i = 1; i < 1009; i++)
        if (i != 5) patient(i)[1] = 0; /* nobody else is a candidate */
}

/* SIM-79, docs/sim-rooms.md: measured record indices, deliberately tabulated independently of the engine's
 * address formula. A fixture's pathology capacity belongs at this alias for per-tick deaths only. */
static size_t death_alias_offset(int h) {
    static const int ROWS[2][16] = {
        {176, 20, 120, 220, 64, 164, 8, 108, 208, 52, 152, 252, 96, 196, 40, 140},
        {64, 164, 8, 108, 208, 52, 152, 252, 96, 196, 40, 140, 240, 84, 184, 28}};
    return 158u * ROWS[g_prog.edition == EDITION_ENGLISH_CD][h];
}
static uint8_t *death_alias_record(int h) {
    size_t offset = death_alias_offset(h);
    for (int b = 6; b <= 14; b++) {
        if (offset < g_world.size[b]) return g_world.block[b] + offset;
        offset -= g_world.size[b];
    }
    CHECK(0); /* every measured alias lies inside blocks 7-15 */
    return NULL;
}

static void staff_rooms(int h, int staffed) {
    uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + h * 100;
    c[2] = c[3] = c[5] = 1;
    put16(g_world.block[3] + h * 174 + 8, staffed ? 156 : 0); /* reception staff index */
    put16(g_world.block[15] + h * 1800 + 8, 7); /* treatment room +8, +10 */
    put16(g_world.block[15] + h * 1800 + 10, 8);
}

static void test_arrival_admits_when_staffed(void) {
    patient_world();
    for (int h = 0; h < 16; h++) staff_rooms(h, 1);
    for (int i = 0; i < 2000 && (int8_t)patient(5)[3] < 0; i++) sim_patients_run(&g_sim, 0x69CB6);
    int h = patient(5)[3];
    CHECK(h < 16 && patient(5)[4] == 2);
    CHECK(get16(g_world.block[3] + h * 174 + 10) == 5); /* first queue place */
    CHECK(get32(world_hospital(&g_world, h) + 0x16E) >= 1); /* activity counted */
}

static void test_arrival_refused_without_reception_staff(void) {
    patient_world();
    for (int h = 0; h < 16; h++) staff_rooms(h, 0);
    for (int i = 0; i < 2000; i++) sim_patients_run(&g_sim, 0x69CB6);
    CHECK((int8_t)patient(5)[3] < 0);
}

static void test_home_treatment_cures(void) {
    patient_world();
    uint8_t *p = patient(5);
    p[1] = 1, p[79] = 1, p[43] = 30, p[81] = 4, p[82] = 0xFF, p[44] = 97, p[105] = 1;
    sim_patients_run(&g_sim, 0x672A1); /* rank 4 (first treatment): +4 -> 100 */
    CHECK(p[82] == 4 || p[56] == 0);
    sim_patients_run(&g_sim, 0x672A1); /* cure check */
    CHECK(p[56] == 0 && p[79] == 0 && p[11] == 7); /* cured, history keeps illness 7 */
    CHECK(p[20 + 4] == 61); /* opinion of the treating hospital + 11 */
    CHECK(get16(world_hospital(&g_world, 4) + 0x28) == 1); /* cured counter */
}

static void test_health_history_shift(void) {
    patient_world();
    uint8_t *p = patient(5);
    p[1] = 1, p[79] = 1, p[43] = 30, p[81] = 4, p[82] = 3, p[44] = 50, p[105] = 1;
    p[67] = 33, p[76] = 44, p[77] = 55;
    sim_patients_run(&g_sim, 0x672A1); /* rank 3: +1 */
    CHECK(p[44] == 51 && p[67] == 50 && p[105] == 2);
    CHECK(p[68] == 33 && p[77] == 44); /* shifted up, the oldest dropped */
}

/* Module E (docs/sim-staff.md): one applicant (staff record 7, a man, group 3, demand 9) and one opening
 * of AI hospital 15 for group 3. */
static void staff_world(void) {
    fresh();
    for (int i = 0; i < 1041; i++) g_world.block[BLK_STAFF][i * 32 + 3] = 0xFF, g_world.block[BLK_STAFF][i * 32 + 4] = 0xFF,
                                   g_world.block[BLK_STAFF][i * 32 + 27] = 0xFF;
    uint8_t *st = g_world.block[BLK_STAFF] + 7 * 32;
    st[0] = 0, st[1] = 3, st[2] = 5, st[15] = 50, st[18] = 9, st[21] = 50, st[22] = 0xFF;
    uint8_t *o = g_world.block[BLK_JOB_OPENINGS] + 4 * 7;
    o[0] = 15, o[1] = 3, o[2] = 9, o[3] = 0, o[4] = 20, o[5] = 2, o[6] = 0;
    world_set_cash(&g_world, 15, 500);
    g_sim.openings = 1;
}

static void test_pairing_and_hiring(void) {
    staff_world();
    uint8_t *st = g_world.block[BLK_STAFF] + 7 * 32;
    for (int i = 0; i < 20000 && st[27] == 0xFF; i++) sim_staff_run(&g_sim, 0x1F1F3);
    CHECK(st[27] == 4); /* reserved the opening at index 4 */
    sim_staff_run(&g_sim, 0x1F63D);
    const uint8_t *slot = g_world.block[BLK_STAFF_SLOTS] + 15 * 4000;
    CHECK(get16(slot) == 7 && slot[2] == 3 && slot[6] == 11); /* wage = demand + 2 */
    CHECK(st[3] == 15 && st[4] == 0 && st[19] == 11 && st[27] == 0xFF);
    CHECK(world_cash(&g_world, 15) == 500 - 11); /* the first wage at once */
    CHECK(get32(world_hospital(&g_world, 15) + 0xA6) == 11);
    CHECK((int8_t)g_world.block[BLK_JOB_OPENINGS][4 * 7] < 0); /* opening freed */
}

static void test_pairing_respects_requirements(void) {
    staff_world();
    uint8_t *st = g_world.block[BLK_STAFF] + 7 * 32;
    st[18] = 10; /* demands more than the opening's 9 */
    for (int i = 0; i < 20000; i++) sim_staff_run(&g_sim, 0x1F1F3);
    CHECK(st[27] == 0xFF);
    st[18] = 9, st[22] = 15; /* dismissed by that hospital before */
    for (int i = 0; i < 20000; i++) sim_staff_run(&g_sim, 0x1F1F3);
    CHECK(st[27] == 0xFF);
}

static void test_staffing_need_opens_store_clerk(void) {
    staff_world();
    memset(g_world.block[BLK_JOB_OPENINGS], 0xFF, g_world.size[BLK_JOB_OPENINGS]);
    g_world.block[BLK_ROOM_COUNTS][15 * 100 + 4] = 1; /* a store room, nothing else */
    world_set_tail(&g_world, TAIL_DAY, 1);
    sim_staff_run(&g_sim, 0x62D6A);
    const uint8_t *o = g_world.block[BLK_JOB_OPENINGS];
    CHECK(o[0] == 15 && o[1] == 3 && o[2] == 9 && o[4] == 20 && o[5] == 0 && o[6] == 0);
    sim_staff_run(&g_sim, 0x62D6A); /* the need is covered by the opening now */
    CHECK((int8_t)o[7] < 0);
}

/* SIM-25 (docs/sim-drift9.md section 4, "What the engine must change" 2 (a)-(g)): with a clerk, hours 19-21 keep it
 * while the RECEPTION's +8 (the receptionist) is set or someone waits for a treatment room, dental ward or neurology;
 * the store's own +8 is not read. AI hospital 15 with a store holding 5 furniture items, a reception, one waiting
 * room; clerk 20 in slot 0, an idle group-3 employee 21 in slot 1. */
static uint8_t *clerk_slot(int i) { return staff_slots(&g_sim, 15) + i * STAFF_SLOT_SIZE; }
static void clerk_world(int hour, int receptionist) {
    fresh();
    uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + 15 * 100;
    c[2] = c[3] = c[4] = 1;
    uint8_t *st = sim_room_record(&g_sim, 15, 4, 0);
    st[STORE_FURNITURE_HELD] = 5;
    put16(sim_room_record(&g_sim, 15, 2, 0) + 8, receptionist ? 156 : 0);
    for (int k = 0; k < 2; k++) {
        int index = 20 + k;
        uint8_t *sl = clerk_slot(k), *p = g_world.block[BLK_STAFF] + index * 32;
        put16(sl, index), sl[2] = 3, sl[3] = k ? 0 : 4;
        p[1] = 3, p[3] = 15, p[4] = k ? 0 : 4, p[21] = 100;
    }
    put16(st + STORE_CLERK, 20);
    world_set_tail(&g_world, TAIL_HOUR, hour);
    world_set_tail(&g_world, TAIL_HOURS, 100);
}
static int clerk_of(void) { return get16(sim_room_record(&g_sim, 15, 4, 0) + STORE_CLERK); }

static void test_store_clerk_evening(void) {
    /* (a) 20:00, reception unstaffed, nobody waiting: released (off duty, store +4 = 0) */
    clerk_world(20, 0);
    sim_staff_run(&g_sim, 0x1F9B9);
    CHECK(clerk_of() == 0 && clerk_slot(0)[3] == 0x13 && g_world.block[BLK_STAFF][20 * 32 + 4] == 0x13);
    /* (f) 19:00 after (a), no clerk: the idle group-3 employee is hired, and the next call releases it too */
    world_set_tail(&g_world, TAIL_HOUR, 19);
    sim_staff_run(&g_sim, 0x1F9B9);
    CHECK(clerk_of() == 21 && clerk_slot(1)[3] == 4);
    sim_staff_run(&g_sim, 0x1F9B9);
    CHECK(clerk_of() == 0 && clerk_slot(1)[3] == 0x13);
    /* (b) 20:00 with the reception staffed: stays */
    clerk_world(20, 1);
    sim_staff_run(&g_sim, 0x1F9B9);
    CHECK(clerk_of() == 20 && clerk_slot(0)[3] == 4);
    /* (c) 20:00, reception unstaffed, one patient waiting for a dental ward (illness 9, type 1): stays; likewise
     * for a treatment room (type 0) and the neurology (type 2) */
    static const int CAT[3] = {11, 5, 15}, TYPE[3] = {1, 0, 2};
    for (int k = 0; k < 3; k++) {
        clerk_world(20, 0);
        g_image[0x954 + 9 * 20 + 8] = (uint8_t)TYPE[k];
        uint8_t *p = g_world.block[BLK_PATIENTS] + 5 * 106;
        p[56] = 9, p[57] = 0;
        put16(sim_room_record(&g_sim, 15, 3, 0) + 0x14, 5);
        CHECK(sim_waiting_for(&g_sim, 15, CAT[k]) == 1);
        CHECK(sim_waiting_for(&g_sim, 15, 5) + sim_waiting_for(&g_sim, 15, 11) + sim_waiting_for(&g_sim, 15, 15) == 1);
        sim_staff_run(&g_sim, 0x1F9B9);
        CHECK(clerk_of() == 20);
    }
    /* (d) 22:00 with the reception staffed: released */
    clerk_world(22, 1);
    sim_staff_run(&g_sim, 0x1F9B9);
    CHECK(clerk_of() == 0 && clerk_slot(0)[3] == 0x13);
    /* 21:00 is still inside: staffed reception keeps it, unstaffed releases it */
    clerk_world(21, 1);
    sim_staff_run(&g_sim, 0x1F9B9);
    CHECK(clerk_of() == 20);
    clerk_world(21, 0);
    sim_staff_run(&g_sim, 0x1F9B9);
    CHECK(clerk_of() == 0);
    /* (e) 18:00, everything empty: stays */
    clerk_world(18, 0);
    sim_room_record(&g_sim, 15, 4, 0)[STORE_FURNITURE_HELD] = 0;
    sim_staff_run(&g_sim, 0x1F9B9);
    CHECK(clerk_of() == 20);
    /* (g) the store empty at 20:00 with the reception staffed: stays (the store's stock is not the test) */
    clerk_world(20, 1);
    sim_room_record(&g_sim, 15, 4, 0)[STORE_FURNITURE_HELD] = 0;
    sim_staff_run(&g_sim, 0x1F9B9);
    CHECK(clerk_of() == 20);
}

static void test_invoice(void) {
    fresh();
    uint8_t *r = world_hospital(&g_world, 3);
    for (int k = 0x20; k <= 0x25; k++) r[k] = (uint8_t)(k - 0x1E); /* levels 2, 3, 4, 5, 6, 7 */
    r[0x94] = 3, r[0x95] = 9; /* material 3, base fee 9 */
    uint8_t p[106] = {0};
    p[50] = 12, p[51] = 30, p[54] = 12, p[48] = 7, p[49] = 50; /* byte 49 is not billed */
    CHECK(sim_invoice(&g_sim, 3, p) == 9 + 12 / 2 + 30 / 3 + 12 / 6 + 7 / 3);
    p[2] = 1; /* private: u + u/2 + 1 for each used type */
    CHECK(sim_invoice(&g_sim, 3, p) == 9 + 19 / 2 + 46 / 3 + 19 / 6 + 11 / 3);
}

/* The discharge with bill counts every treatment at +0x26, and the dental and neuro rooms also at +0x30 and
 * +0x4E (they enter the discharge at 0x2D976 / 0x2D93D, docs/sim-rooms.md); the treatment room counts neither. */
static void test_discharge_counts_per_department(void) {
    static const int CATS[3] = {5, 11, 15};
    for (int k = 0; k < 3; k++) {
        patient_world();
        int h = 3, cat = CATS[k];
        g_world.block[BLK_ROOM_COUNTS][h * 100 + cat] = 1;
        uint8_t *tr = sim_room_record(&g_sim, h, cat, 0);
        put16(tr + 8, 7), put16(tr + 10, 8), put16(tr + 12, 5); /* staffed, patient 5 inside */
        put16(tr + 14, 0), put16(tr + 16, 30), put16(tr + 18, 0); /* diagnosed, treatment 30 finished */
        patient(5)[3] = (uint8_t)h;
        sim_rooms_after_operator(&g_sim, h, cat, 0);
        const uint8_t *r = world_hospital(&g_world, h);
        CHECK(get16(r + 0x26) == 1 && get16(tr + 12) == 0); /* counted and the room freed */
        CHECK(get16(r + 0x30) == (cat == 11));
        CHECK(get16(r + 0x4E) == (cat == 15));
    }
}

/* Price index (docs/mainloop.md): the original drops the OP term when +0x172 >= 4, a copy-protection penalty
 * that only builds up while a manual question stays unanswered; the engine has no query and keeps the term. */
static void test_price_index_activity(void) {
    static const int16_t V[10] = {0, 0, 15, 23, 29, 34, 38, 41, 43, 44};
    for (int activity = 3; activity <= 4; activity++) {
        fresh();
        for (int i = 0; i < 10; i++) put16(g_image + 0x465E + 2 * i, (uint16_t)V[i]);
        uint8_t *r = world_hospital(&g_world, 3), *c = g_world.block[BLK_ROOM_COUNTS] + 3 * 100;
        for (int k = 0x20; k <= 0x25; k++) r[k] = 2;
        r[0x22] = 4, r[0x94] = 2, r[0x95] = 9; /* OP level 4: weight 29 */
        c[6] = c[7] = c[9] = 1;
        put16(r + 0x172, (uint16_t)activity);
        world_price_index(&g_world, &g_prog);
        CHECK(get16(r + 0x10A) == 6 * 15 + 29 - 18);
    }
}

/* 0x2995B (docs/sim-rooms.md): a cured patient (no case, no step running) leaves the bed at once at hours 4-20,
 * whatever the hours in bed; outside those hours the patient stays. One with a case gets the next step. */
static void test_ward_discharges_cured(void) {
    for (int k = 0; k < 3; k++) {
        patient_world();
        int h = 15;
        if (k == 1) world_set_tail(&g_world, TAIL_HOUR, 22);
        world_set_cash(&g_world, h, 1000);
        g_world.block[BLK_ROOM_COUNTS][h * 100 + 6] = 1;
        uint8_t *ward = sim_room_record(&g_sim, h, 6, 0), *p = patient(5);
        put16(ward + 0x14, 5), put16(ward + 0x16, 5), ward[10] = 10, ward[12] = 1; /* 5 hours in bed */
        put16(ward + 8, 7); /* a nurse: without one the operator ends after the nurse request */
        p[3] = (uint8_t)h, p[4] = 6, p[93] = 0, p[44] = 100;
        if (k < 2) put16(p + 56, 0); /* cured */
        for (int i = 0; i < 20 && p[4] == 6 && !p[93]; i++) sim_rooms_run(&g_sim, 0x2995B);
        const uint8_t *r = world_hospital(&g_world, h);
        if (k == 0) CHECK(p[4] == 0xFF && get16(r + 0x48) == 1 && get16(r + 0x26) == 1 && ward[12] == 0);
        if (k == 1) CHECK(p[4] == 6 && get16(r + 0x26) == 0);
        if (k == 2) CHECK(p[4] == 6 && p[93] == 0xFF && get16(r + 0x26) == 0);
    }
}

/* 0x4F395 (docs/sim-rooms2.md): the Krankenzimmer operator moves a patient below 30 health into the first free
 * intensive-care record; with no free record, or at 30, the patient stays in the bed. */
static void test_ward_to_intensive_care(void) {
    for (int k = 0; k < 3; k++) {
        patient_world();
        int h = 15;
        world_set_cash(&g_world, h, 1000);
        uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + h * 100, *p = patient(5);
        c[6] = 1, c[9] = 2;
        uint8_t *ward = sim_room_record(&g_sim, h, 6, 0), *icu0 = sim_room_record(&g_sim, h, 9, 0),
                *icu1 = sim_room_record(&g_sim, h, 9, 1);
        put16(ward + 8, 7), ward[10] = 10, ward[12] = 1;
        put16(ward + 0x14 + 4 * 3, 5), put16(ward + 0x16 + 4 * 3, 4); /* bed 3, 4 hours */
        p[3] = (uint8_t)h, p[4] = 6, p[44] = k == 2 ? 30 : 29, p[93] = 0xFF, p[43] = 30; /* treatment byte 1 = 0 */
        put16(icu0 + 0x0C, 9); /* the first record is taken */
        if (k == 1) put16(icu1 + 0x0C, 10);
        for (int i = 0; i < 20 && p[4] == 6; i++) sim_rooms_run(&g_sim, 0x2995B);
        const uint8_t *r = world_hospital(&g_world, h);
        if (k == 0) {
            CHECK(p[4] == 9 && p[66] == 1 && p[93] == 0 && get16(icu1 + 0x0C) == 5 && icu1[0x0E] == 0xFF);
            CHECK(get16(ward + 0x14 + 4 * 3) == 0 && ward[12] == 0 && get16(r + 0x56) == 1);
        } else {
            CHECK(p[4] == 6 && get16(ward + 0x14 + 4 * 3) == 5 && get16(r + 0x56) == 0);
        }
    }
}

/* 0x6F346: an intensive-care patient at health 50 or more goes back to the reception's Krankenzimmer queue;
 * at 49 the patient stays. */
static void test_intensive_care_release(void) {
    for (int health = 49; health <= 50; health++) {
        patient_world();
        int h = 15;
        uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + h * 100, *p = patient(5);
        c[2] = c[9] = 1;
        uint8_t *icu = sim_room_record(&g_sim, h, 9, 0), *queue = g_world.block[3] + h * 174 + 0x46;
        put16(icu + 8, 7), put16(icu + 0x0C, 5);
        put16(queue, 11); /* the first queue place is taken */
        p[3] = (uint8_t)h, p[4] = 9, p[44] = (uint8_t)health, p[43] = 30, p[45] = 2;
        sim_rooms_run(&g_sim, 0x6F346);
        const uint8_t *r = world_hospital(&g_world, h);
        if (health == 50) {
            CHECK(p[4] == 2 && get16(queue + 4) == 5 && get16(icu + 0x0C) == 0 && get16(r + 0x58) == 1);
            CHECK(p[43] == 30 && p[45] == 2); /* the bed treatment goes on */
        } else {
            CHECK(p[4] == 9 && get16(queue + 4) == 0 && get16(icu + 0x0C) == 5 && get16(r + 0x58) == 0);
        }
    }
}

/* 0x6E8BE and 0x58045: the OP operator brings a Krankenzimmer patient whose next step is code 11, starts the
 * operation when the room has every capability of the operation (+0x32 counted), the per-tick routine runs the
 * stages to the end, and the operator sends the patient back to the bed it kept. Without the capability the
 * operation does not start. */
static void test_operation(void) {
    for (int equipped = 0; equipped <= 1; equipped++) {
        patient_world();
        int h = 15;
        uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + h * 100, *p = patient(5);
        c[6] = c[7] = 1;
        static const uint16_t DESC7[7] = {126, 30, 28, 30, 186, 88, 27};
        for (int i = 0; i < 7; i++) put16(g_image + 0x4758 + 14 * 7 + 2 * i, DESC7[i]);
        put16(g_image + 0x248C + 20 * 2, 6), put16(g_image + 0x248C + 20 * 2 + 2, 30); /* op 2 needs 6, 30, 0 x 8 */
        static const uint8_t SEQ[3] = {0, 2, 1};
        memcpy(g_image + 0x208E + 11 * 2, SEQ, 3);
        g_image[0x69EB + 1] = 2, g_image[0x69EB + 2] = 3; /* stage durations */
        uint8_t *item = g_image + 0x144A + 20 * 1; /* equipment item 1: capabilities 6, 30, 0, 0; byte 5 = 7 */
        item[10] = 6, item[11] = 30, item[5] = 7;
        uint8_t *t = g_image + 0x270C + 30 * 38;
        t[1] = 2, t[16] = 11; /* operation 2; step 1 sends to the OP */
        uint8_t *ward = sim_room_record(&g_sim, h, 6, 0), *op = sim_room_record(&g_sim, h, 7, 0);
        put16(ward + 8, 7), ward[10] = 10, ward[12] = 1, put16(ward + 0x14, 5);
        put16(op + 6, 7), put16(op + 8, 8), put16(op + 10, 9);
        if (equipped) put16(op + 28, 1), op[88] = 100;
        p[3] = (uint8_t)h, p[4] = 6, p[44] = 100, p[43] = 30, p[45] = 1, p[93] = 0xFF;
        sim_rooms_run(&g_sim, 0x6E8BE); /* bring */
        CHECK(get16(op + 0x0C) == 5 && p[4] == 7 && p[98] == 6 && get16(op + 0x0E) == 999);
        sim_rooms_run(&g_sim, 0x6E8BE); /* start */
        const uint8_t *r = world_hospital(&g_world, h);
        if (!equipped) {
            CHECK(get16(r + 0x32) == 0 && get16(op + 0x0E) == 999 && op[4] == 6 && get16(op + 0x10) == 2);
            continue;
        }
        CHECK(get16(r + 0x32) == 1 && get16(op + 0x0E) == 6 && p[100] == 1 && p[102] == 120);
        CHECK(op[3] == 7 && get16(op + 0x76) == 1); /* the operating table */
        int ticks = 0;
        while (ticks < 50 && p[100] != 2) sim_rooms_run(&g_sim, 0x58045), ticks++;
        CHECK(ticks == 11 && get16(op + 0x0E) == 999 && (int16_t)get16(op + 0x12) == -1 && p[44] > 0);
        sim_rooms_run(&g_sim, 0x6E8BE); /* finish */
        CHECK(p[4] == 6 && get16(op + 0x0C) == 0 && get16(ward + 0x14) == 5 && get16(r + 0x32) == 1);
    }
}

/* 0x2F3FF through 0x2F610 (docs/sim-staff.md, the refund rule): the store buys the requested equipment; when the
 * price would make the buyer's cash negative the item comes free, nothing is booked and the seller gets nothing */
static void test_equipment_purchase_refund(void) {
    for (int poor = 0; poor <= 1; poor++) {
        fresh();
        int h = 15, seller = 3;
        uint8_t *mk = g_world.block[BLK_EQUIPMENT_MARKET];
        for (int i = 0; i < 300; i++) mk[i * 8] = 0xFF;
        mk[0] = (uint8_t)seller, mk[1] = 50, put16(mk + 2, 7), mk[4] = 80, mk[5] = 0;
        g_image[0x144A + 7 * 20 + 10] = 0x20; /* item 7 has capability 0x20 */
        g_world.block[BLK_ROOM_COUNTS][h * 100 + 4] = 1;
        uint8_t *st = sim_room_record(&g_sim, h, 4, 0), *r = world_hospital(&g_world, h);
        st[7] = 20;
        r[0xDA] = 0x20;
        world_set_cash(&g_world, h, poor ? 30 : 1000);
        world_set_cash(&g_world, seller, 0);
        sim_staff_run(&g_sim, 0x2F3FF);
        const uint8_t *sr = world_hospital(&g_world, seller);
        CHECK(get16(st + 0xFA) == 7 && st[9] == 1 && mk[0] == 0xFF && r[0xDA] == 0); /* bought either way */
        if (poor) CHECK(get32(r + 0x16) == 30 && get16(r + 0xAE) == 0 && get32(sr + 0x16) == 0 && get16(sr + 0xB0) == 0);
        else CHECK(get32(r + 0x16) == 950 && get16(r + 0xAE) == 1 && get32(r + 0xBE) == 50 && get32(sr + 0x16) == 50);
    }
}

/* 0x2FC77 (docs/sim-staff2.md): an off-duty employee becomes idle again only after 8 hours off duty, with mood
 * 95 or more, by a 1-in-32 draw per minute; before the eighth hour never. */
static void test_off_duty_return(void) {
    for (int hours_off = 7; hours_off <= 8; hours_off++) {
        fresh();
        int h = 3, index = 40;
        uint8_t *st = g_world.block[BLK_STAFF] + index * 32, *sl = staff_slots(&g_sim, h);
        st[1] = 1, st[3] = (uint8_t)h, st[4] = 0x13, st[18] = st[19] = 10, st[21] = 100; /* nurse, wage = demand */
        put16(sl, index), sl[2] = 1, sl[3] = 0x13, sl[11] = (uint8_t)hours_off;
        int minutes = 0;
        while (minutes < 2000 && st[4] == 0x13) sim_staff_run(&g_sim, 0x2FC77), minutes++;
        if (hours_off == 7) CHECK(st[4] == 0x13 && sl[3] == 0x13);
        else CHECK(st[4] == 0 && sl[3] == 0 && sl[11] == 0 && sl[18] == 0xFF && minutes < 2000);
    }
}

/* 0x2FC77 step 5 (docs/sim-staff2.md): a finished course (entry +3 = 0) sends the employee back to idle and writes
 * the newspaper item 0x495A0 (type 0x1F, class 1) about the hospital, the employee and the course id; a course still
 * running writes nothing. AI hospital 15, so the item does not hang on the player's 50 % draw. */
static void test_course_end_item(void) {
    for (int left = 0; left <= 1; left++) {
        fresh();
        int h = 15, index = 41;
        memset(g_world.block[BLK_COURSES], 0xFF, 200);
        uint8_t *c = g_world.block[BLK_COURSES] + 3 * 10;
        c[0] = 1, c[1] = 0, c[2] = 4, c[3] = (uint8_t)left, c[4] = 6;
        uint8_t *st = g_world.block[BLK_STAFF] + index * 32, *sl = staff_slots(&g_sim, h);
        st[1] = 1, st[3] = (uint8_t)h, st[4] = 0x19, st[26] = 3, st[18] = st[19] = 10, st[21] = 50;
        put16(sl, index), sl[2] = 1, sl[3] = 0x19, sl[4] = 3;
        sim_staff_run(&g_sim, 0x2FC77);
        int found = -1;
        for (int i = 0; i < 100; i++)
            if (sim_news_item(&g_sim, i)[0] && sim_news_item(&g_sim, i)[20] == 0x1F) found = i;
        if (left) {
            CHECK(found < 0 && st[4] == 0x19);
            continue;
        }
        CHECK(st[4] == 0 && sl[3] == 0 && found >= 0);
        if (found < 0) continue;
        const uint8_t *it = sim_news_item(&g_sim, found);
        CHECK(it[0] == 1 && it[8] == h && it[21] == 6 && get16(it + 16) == index && sim_news_count(&g_sim) == 1);
    }
}

/* The blood and organ bank (docs/sim-rooms2.md, "Blood and organ bank"): AI hospital 15 with a bank whose
 * description is the one the spec gives (furniture +0x1C x 10, equipment +0x30 x 20, conditions +0x94 / +0x9E,
 * type +0x1B); equipment items 1-3 have the capabilities 0x3D (container), 0x33 (cooling), 0x3C. */
static uint8_t *bank_world(int h) {
    fresh();
    static const uint16_t DESC17[7] = {0x1C, 10, 0x30, 20, 0x94, 0x9E, 0x1B};
    for (int i = 0; i < 7; i++) put16(g_image + 0x4758 + 14 * 17 + 2 * i, DESC17[i]);
    g_image[0x144A + 20 * 1 + 10] = 0x3D, g_image[0x144A + 20 * 2 + 10] = 0x33, g_image[0x144A + 20 * 3 + 10] = 0x3C;
    g_world.block[BLK_ROOM_COUNTS][h * 100 + 17] = 1;
    return sim_room_record(&g_sim, h, 17, 0);
}

/* 0x535DC: a nurse (group 1) into +8 first, a group-7 person into +0x0A on the next call, also at night; with
 * negative cash nothing */
static void test_bank_operator(void) {
    for (int broke = 0; broke <= 1; broke++) {
        uint8_t *b = bank_world(15);
        world_set_tail(&g_world, TAIL_HOUR, 23);
        world_set_cash(&g_world, 15, broke ? -1 : 0);
        uint8_t *sl = staff_slots(&g_sim, 15);
        put16(sl, 40), sl[2] = 1; /* idle nurse */
        put16(sl + 20, 41), sl[22] = 7; /* idle group 7 */
        g_world.block[BLK_STAFF][40 * 32 + 21] = g_world.block[BLK_STAFF][41 * 32 + 21] = 50;
        sim_rooms_run(&g_sim, 0x535DC);
        if (broke) {
            CHECK(get16(b + 8) == 0 && get16(b + 10) == 0);
            continue;
        }
        CHECK(get16(b + 8) == 40 && get16(b + 10) == 0 && sl[3] == 17 && g_world.block[BLK_STAFF][40 * 32 + 4] == 17);
        sim_rooms_run(&g_sim, 0x535DC);
        CHECK(get16(b + 8) == 40 && get16(b + 10) == 41 && sl[23] == 17);
    }
}

/* 0x53C79: with a store room holding containers, the bank adds one per call while its capacity is below 5,000,
 * then only while less than 500 cl are free */
static void test_bank_capacity(void) {
    uint8_t *b = bank_world(15);
    g_world.block[BLK_ROOM_COUNTS][15 * 100 + 4] = 1;
    uint8_t *st = sim_room_record(&g_sim, 15, 4, 0);
    for (int k = 0; k < 4; k++) put16(st + 0xFA + 2 * k, 1), st[0x262 + k] = 90;
    st[9] = 4;
    put16(b + 0x30, 1), put16(b + 0x32, 2), put16(b + 0x34, 3); /* container, cooling, 0x3C */
    put32(b + 0x10, 2000);
    sim_rooms_run(&g_sim, 0x53C79);
    CHECK(get32(b + 0x10) == 4000 && get16(b + 0x36) == 1 && st[9] == 3);
    sim_rooms_run(&g_sim, 0x53C79);
    CHECK(get32(b + 0x10) == 6000 && st[9] == 2);
    sim_rooms_run(&g_sim, 0x53C79);
    CHECK(get32(b + 0x10) == 6000 && st[9] == 2); /* 5,000 or more and 6,000 free */
    put32(b + 0x0C, 5501);
    put16(st + 0xFA, 1), st[9] = 3; /* the store finds items only among its first +9 slots */
    sim_rooms_run(&g_sim, 0x53C79);
    CHECK(get32(b + 0x10) == 8000 && st[9] == 2); /* 499 free */
}

/* The store-screen test (docs/sim-rooms2.md): the player's bank (hospital 0, +0x14 set) takes a container from its
 * store only with an independent clerk (byte 11) and the player not on the inventory, screen 65; screens 0 and 1
 * are not 65. The player's intensive care clears its request byte 0x2B (an item of it is there) the same way. */
static void test_store_screen(void) {
    static const int SCREENS[4] = {0, 1, 0x41, 1};
    for (int c = 0; c < 4; c++) { /* c = 3: clerk byte 11 clear */
        uint8_t *b = bank_world(0);
        world_hospital(&g_world, 0)[0x14] = 1;
        g_world.block[BLK_ROOM_COUNTS][4] = 1;
        uint8_t *st = sim_room_record(&g_sim, 0, 4, 0);
        put16(st + 0xFA, 1), st[0x262] = 90, st[9] = 1;
        put16(st + 4, 70), g_world.block[BLK_STAFF][70 * 32 + 11] = c == 3 ? 0 : 1;
        put16(b + 0x32, 2), put16(b + 0x34, 3); /* cooling, 0x3C; the container is missing */
        put32(b + 0x10, 0);
        g_sim.screen = SCREENS[c];
        sim_rooms_run(&g_sim, 0x53C79);
        int took = get32(b + 0x10) == 2000 && st[9] == 0;
        CHECK(took == (c < 2));
        /* intensive care of hospital 0 with patient 5 and request 0x2B, its item 1 (0x2B) installed */
        g_world.block[BLK_ROOM_COUNTS][9] = 1;
        static const uint16_t D[7] = {0x40, 5, 0x60, 10, 0x90, 0xA0, 0xC0};
        for (int i = 0; i < 7; i++) put16(g_image + 0x4758 + 14 * 9 + 2 * i, D[i]);
        g_image[0x144A + 20 * 1 + 10] = 0x2B;
        uint8_t *icu = sim_room_record(&g_sim, 0, 9, 0), *p = g_world.block[BLK_PATIENTS] + 5 * 106;
        put16(icu + 0x0C, 5), icu[6] = 0x2B, put16(icu + 0x60, 1), icu[0xA0] = 100;
        p[1] = 1, p[3] = 0, p[4] = 9, p[44] = 100;
        sim_rooms_run(&g_sim, 0x6FA5A);
        CHECK(icu[6] == (c < 2 ? 0 : 0x2B));
        g_sim.screen = 0;
    }
}

/* SIM-18 (docs/sim-rooms2.md, "The death item of 0x70321 without a pathology place"): a death by intensive care's
 * round step in AI hospital 8, record 0. Health 100 is stable (no worsening, care drift or bed worsening), and illness
 * 7's b = 100 takes it below 1 when the round hits (r31 = 0). Without a pathology place the newspaper item is the one
 * of the record (u16 16 = 432, not the patient, and the name copy of the 157 records: 0, the pool's first entry);
 * the rest is the normal death of the patient (+0x38, +0x3C, reset at home). With a place at the SIM-79 alias,
 * no item, the patient listed there. */
static void test_icu_death_item(void) {
    static const char text[] = "Ab$Cd Ef$";
    static uint8_t table[8];
    put32(table, 0), put32(table + 4, 3);
    static DataDb db;
    memset(&db, 0, sizeof db);
    db.pools[0] = (TextPool){(const uint8_t *)text, sizeof text - 1, table, 2};
    for (int place = 0; place <= 1; place++) {
        patient_world();
        g_sim.people = &db;
        int h = 8;
        uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + h * 100, *p = patient(5);
        c[9] = 1;
        uint8_t *icu = sim_room_record(&g_sim, h, 9, 0), *path = NULL;
        put16(icu + 0x0C, 5), icu[0] = 100, icu[0x0E] = 0;
        if (place) {
            c[18] = 1;
            path = death_alias_record(h);
            put16(path + 8, 5); /* five places, none listed */
        }
        g_image[0x954 + 7 * 20 + 7] = 100;
        p[3] = (uint8_t)h, p[4] = 9, p[44] = 100, p[43] = 30, p[96] = 2, p[57] = 0;
        int minutes = 0;
        while (get16(icu + 0x0C) && minutes < 5000) sim_rooms_run(&g_sim, 0x6FA5A), minutes++;
        CHECK(get16(icu + 0x0C) == 0 && minutes < 5000);
        const uint8_t *r = world_hospital(&g_world, h);
        CHECK(get16(r + 0x38) == 1);
        if (!place) {
            CHECK(sim_news_count(&g_sim) == 1);
            const uint8_t *it = sim_news_item(&g_sim, 0);
            CHECK(it[0] == 2 && it[20] == 8 && it[8] == h && get16(it + 16) == 432);
            CHECK(memcmp(it + 29, "\0Ab$", 4) == 0);
            CHECK(get16(r + 0x3C) == 1 && p[4] == 0xFF && p[3] == 0xFF && p[44] == 100);
        } else {
            CHECK(sim_news_count(&g_sim) == 0);
            CHECK(p[4] == 18 && p[66] == 9 && get16(path + 6) == 1 && get16(path + 0x58) == 5 && get16(r + 0x3C) == 0);
        }
        g_sim.people = NULL;
    }
}

/* The nurse care's mood flag (docs/sim-rooms2.md, "The player block's mood flag +0x49"): in intensive care with comfort,
 * nurse bust 255 (every draw passes), a raised mood sets +0x49 of block 28 (hospital 0) or 29 (hospital 1); mood and
 * opinion 100 raise nothing and set nothing; hospital 3 sets neither block. */
static void test_nurse_mood_flag(void) {
    static const struct { int h, mood, opinion, want28, want29; } C[5] = {
        {1, 50, 50, 0, 0xFF}, {0, 50, 50, 0xFF, 0}, {1, 100, 100, 0, 0}, {1, 100, 50, 0, 0xFF}, {3, 50, 50, 0, 0}};
    for (int c = 0; c < 5; c++) {
        patient_world();
        int h = C[c].h;
        g_world.block[BLK_ROOM_COUNTS][h * 100 + 9] = 1;
        uint8_t *icu = sim_room_record(&g_sim, h, 9, 0), *p = patient(5), *nurse = g_world.block[BLK_STAFF] + 7 * 32;
        put16(icu + 8, 7), put16(icu + 0x0C, 5), icu[0x0F] = 20;
        nurse[5] = 255;
        p[3] = (uint8_t)h, p[4] = 9, p[44] = 95, p[102] = 1, p[0x4E] = (uint8_t)C[c].mood, p[20 + h] = (uint8_t)C[c].opinion;
        sim_rooms_run(&g_sim, 0x6FA5A);
        CHECK(g_world.block[BLK_PLAYER0][0x49] == C[c].want28 && g_world.block[BLK_PLAYER1][0x49] == C[c].want29);
        CHECK(p[0x4E] == (C[c].mood < 100 ? C[c].mood + 1 : 100));
        CHECK(p[20 + h] == (C[c].mood >= 80 && C[c].opinion < 100 ? C[c].opinion + 1 : C[c].opinion));
    }
}

/* 0x59EB4: a hospital with a bank buys the blood offer with the lowest price per 5,000 cl, only with an OP */
static void test_ai_buys_blood(void) {
    for (int op = 0; op <= 1; op++) {
        uint8_t *b = bank_world(15);
        g_world.block[BLK_ROOM_COUNTS][15 * 100 + 7] = (uint8_t)op;
        put32(b + 0x10, 6000), put32(b + 0x0C, 900);
        world_set_cash(&g_world, 15, 1000);
        uint8_t *m = g_world.block[BLK_MARKET];
        for (int i = 0; i < 100; i++) m[i * 10] = 0xFF;
        m[0] = 16, m[1] = 0, put16(m + 2, 500), put16(m + 4, 20); /* 200 per 5,000 */
        m[10] = 16, m[11] = 0, put16(m + 12, 400), put16(m + 14, 10); /* 125 per 5,000 */
        sim_ai_run(&g_sim, 0x59EB4);
        const uint8_t *r = world_hospital(&g_world, 15);
        if (!op) {
            CHECK(get32(b + 0x0C) == 900 && world_cash(&g_world, 15) == 1000 && m[0] == 16 && m[10] == 16);
            continue;
        }
        CHECK(get32(b + 0x0C) == 1300 && world_cash(&g_world, 15) == 990 && get32(r + 0x62) == 400);
        CHECK(get32(r + 0x6E) == 10 && m[10] == 0xFF && m[0] == 16 && world_cash(&g_world, 16) == 10);
        CHECK(get32(world_hospital(&g_world, 16) + 0x5E) == 400);
    }
}

/* 0x57E03: one organ in hospital 15's bank, its quality restored after each call; without a cooling unit it ages in
 * 19 of 64 calls, with one and no group-7 staff in 1 of 16. The cooling test reads hospital 0's bank record, built or
 * not (SIM-33): a unit in bank 15 alone counts only with FIX-48, a unit in hospital 0's record (no bank there) only
 * without it. */
static void test_organ_ageing(void) {
    for (int fix = 0; fix <= 1; fix++)
        for (int where = 0; where < 3; where++) { /* no unit, a unit in bank 15, a unit in hospital 0's record */
            fixes_set(fix ? 1ull << 47 : 0);
            uint8_t *b = bank_world(15);
            if (where == 1) put16(b + 0x30, 2);
            if (where == 2) put16(sim_room_record(&g_sim, 0, 17, 0) + 0x30, 2);
            put16(b + 0x14, 1);
            b[0x58] = 6;
            int aged = 0, n = 40000;
            for (int i = 0; i < n; i++) {
                b[0x59] = 80;
                sim_rooms_run(&g_sim, 0x57E03);
                aged += b[0x59] == 79;
            }
            int cooled = where == (fix ? 1 : 2);
            double rate = (double)aged / n, want = cooled ? 1.0 / 16 : 19.0 / 64;
            CHECK(rate > want - 0.01 && rate < want + 0.01);
        }
    fixes_set(0);
}

/* 0x53C79's staff mood: below 4 the nurse (group 1) leaves the bank through 0x2301C - idle, not off duty as after
 * the evening release 0x22EDD - with slot +0x0A kept */
static void test_bank_staff_leaves(void) {
    uint8_t *b = bank_world(15);
    uint8_t *sl = staff_slots(&g_sim, 15), *nurse = g_world.block[BLK_STAFF] + 40 * 32;
    put16(sl, 40), sl[2] = 1, sl[3] = 17, sl[7] = 2, sl[10] = 5, nurse[1] = 1, nurse[4] = 17, nurse[21] = 2;
    put16(b + 8, 40);
    sim_rooms_run(&g_sim, 0x53C79);
    CHECK(get16(b + 8) == 0 && nurse[4] == 0 && sl[3] == 0 && sl[7] == 0 && sl[10] == 5);
}

/* 0x6AD3C, the donation step (docs/sim-rooms.md, "Treating"): r7 + 6 goes to the room's +20 and is taken from the
 * room's +0x5E, never from the donor's blood; with +20 at 220 or more nothing changes */
static void test_donation_step(void) {
    for (int full = 0; full <= 1; full++) {
        patient_world();
        int h = 3;
        g_world.block[BLK_ROOM_COUNTS][h * 100 + 5] = 1;
        uint8_t *t = g_image + 0x270C + 30 * 38;
        t[15] = 0, t[16] = 10, t[17] = 10;
        uint8_t *tr = sim_room_record(&g_sim, h, 5, 0), *p = patient(5);
        put16(tr + 8, 7), put16(tr + 10, 8), put16(tr + 12, 5);
        put16(tr + 14, 0), put16(tr + 16, 30), put16(tr + 18, 10), put16(tr + 20, full ? 220 : 0), put16(tr + 0x5E, 500);
        p[3] = (uint8_t)h, p[4] = 5, p[43] = 30, p[45] = 1, p[57] = 5, put16(p + 94, 700);
        sim_rooms_run(&g_sim, 0x6AD3C);
        int taken = get16(tr + 20) - (full ? 220 : 0);
        CHECK(get16(p + 94) == 700);
        if (full) CHECK(taken == 0 && get16(tr + 0x5E) == 500);
        else CHECK(taken >= 6 && taken <= 13 && get16(tr + 0x5E) == 500 - taken);
    }
}

/* 0x2D11A step 1: a donor's blood goes into the bank up to its capacity, the overflow is counted at +0x66; with a
 * full bank all of it is, and the donor's home countdown is then the treatment's byte 7 instead of 1 */
static void test_donor_into_bank(void) {
    for (int full = 0; full <= 1; full++) {
        patient_world();
        int h = 3;
        uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + h * 100;
        c[5] = c[17] = 1;
        uint8_t *b = sim_room_record(&g_sim, h, 17, 0), *tr = sim_room_record(&g_sim, h, 5, 0), *p = patient(5);
        put32(b + 0x10, 6000), put32(b + 0x0C, full ? 6000 : 5900);
        put16(tr + 8, 7), put16(tr + 10, 8), put16(tr + 12, 5);
        put16(tr + 14, 0), put16(tr + 16, 30), put16(tr + 18, 0), put16(tr + 20, 150);
        p[3] = (uint8_t)h, p[43] = 30, p[57] = 5, put16(p + 94, 550);
        sim_rooms_after_operator(&g_sim, h, 5, 0);
        const uint8_t *r = world_hospital(&g_world, h);
        CHECK(get16(r + 0x3E) == 1 && get32(r + 0x40) == 150 && get16(tr + 12) == 0 && get16(tr + 20) == 0);
        CHECK(get32(b + 0x0C) == 6000 && get32(r + 0x66) == (full ? 150 : 50) && p[105] == (full ? 2 : 1));
    }
}

/* 0x4BF9B through 0x4FFA0's use: the gap is closed, but two adjacent empty slots leave one behind (reproduced) */
static void test_bank_compaction(void) {
    uint8_t *b = bank_world(15);
    static const uint8_t IDS[6] = {1, 0, 0, 4, 5, 0};
    for (int i = 0; i < 6; i++) b[0x58 + 2 * i] = IDS[i], b[0x59 + 2 * i] = IDS[i] ? (uint8_t)(50 + i) : 0;
    put16(b + 0x14, 3);
    sim_bank_compact(&g_sim);
    static const uint8_t WANT[6] = {1, 0, 4, 5, 0, 0};
    for (int i = 0; i < 6; i++) CHECK(b[0x58 + 2 * i] == WANT[i]);
    CHECK(b[0x5C] == 4 && b[0x5D] == 53);
}

/* ---- making room, AI projects, course booking (docs/sim-staff.md, docs/sim-ai.md) ---------------------------- */

/* A store room for hospital h of bank_world, with nothing in it. */
static uint8_t *store_world(int h) {
    g_world.block[BLK_ROOM_COUNTS][h * 100 + 4] = 1;
    return sim_room_record(&g_sim, h, 4, 0);
}

/* 0x2A499 through sim_store_supply: a full equipment list of an AI hospital sells one random item onto the
 * equipment market (price byte = (u16 of the FOLLOWING offer's bytes 0-1 + 2) / 2, condition kept), then the
 * store's container is installed in the freed slot. Hospital 0's full list sells nothing, takes nothing from the
 * store and sets no request. */
static void test_equipment_sale(void) {
    for (int h = 0; h <= 15; h += 15) {
        uint8_t *b = bank_world(h), *st = store_world(h);
        put16(st + 0xFA, 1), st[0x262] = 90, st[9] = 1; /* a container (item 1) in the store */
        for (int i = 0; i < 20; i++) put16(b + 0x30 + 2 * i, 3), b[0x9E + i] = (uint8_t)(40 + i); /* full: item 3 */
        uint8_t *m = g_world.block[BLK_EQUIPMENT_MARKET];
        memset(m, 0xFF, 300 * 8);
        m[0] = 16;               /* offer 0 in use: the first free one is offer 1 */
        m[16] = 16, m[17] = 7;   /* offer 2's bytes 0-1 = 0x0710: price byte (0x0710 + 2) / 2 = 0x389 -> 0x89 */
        int got = sim_store_supply(&g_sim, h, 17, b, 0x3D, 0);
        if (!h) {
            CHECK(got == -1 && st[9] == 1 && get16(st + 0xFA) == 1 && m[8] == 0xFF && world_hospital(&g_world, 0)[0xDA] == 0);
            continue;
        }
        int k = -1, left = 0;
        for (int i = 0; i < 20; i++) {
            if (get16(b + 0x30 + 2 * i) == 1) k = i;
            left += get16(b + 0x30 + 2 * i) == 3;
        }
        CHECK(got == 1 && st[9] == 0 && k >= 0 && left == 19 && b[0x9E + k] == 90);
        CHECK(m[8] == 15 && m[9] == 0x89 && get16(m + 10) == 3 && k >= 0 && m[12] == 40 + k && m[13] == 0 && get16(m + 14) == 0);
    }
}

/* 0x2A59A through the fittings rule: with the bank's 10 furniture slots full, an AI hospital sells one item that is
 * not of kind 1, 5 or 8 and whose ratings sum below the try's limit (price byte (class + 2) / 2), and the wanted
 * kind then comes from the store into the freed slot. Only kind-5 items: the 60th failed try sells the slot of its
 * draw all the same (SIM-53), so the wish is met. An item rated 4 + 4 is sold only in the last ten tries (limit 60): in
 * 1 - 0.9^10 = 65 % of calls; in the other 35 % a kind-5 item goes. */
static void test_furniture_sale(void) {
    uint8_t *b = bank_world(15), *st = store_world(15); /* bank_world clears the image: the catalog comes after it */
    uint8_t *F = g_image + 0xEE0;
    F[18 * 1 + 6] = 5;                                               /* id 1: kind 5, never sold */
    F[18 * 2 + 4] = 4, F[18 * 2 + 5] = 4;                            /* id 2: kind 0, ratings 8 */
    F[18 * 3 + 4] = 1, F[18 * 3 + 5] = 1, put16(F + 18 * 3 + 8, 10); /* id 3: kind 0, ratings 2, class 10 */
    F[18 * 4 + 6] = 7;                                               /* id 4: kind 7, in the store */
    int sold2 = 0, n = 2000;
    for (int round = 0; round < n + 2; round++) {
        int odd = round == 0 ? 3 : round == 1 ? 0 : 2; /* round 0: id 3, round 1: none, then id 2 */
        for (int i = 0; i < 10; i++) put16(b + 0x1C + 2 * i, 1), b[0x94 + i] = 50;
        if (odd) put16(b + 0x1C + 2 * 6, odd), b[0x94 + 6] = 33;
        memset(st, 0, 730);
        put16(st + 10, 4), st[0x1EA] = 77, st[8] = 1;
        uint8_t *m = g_world.block[BLK_FURNITURE_MARKET];
        memset(m, 0xFF, 300 * 8);
        world_hospital(&g_world, 15)[0xDC] = 0;
        uint8_t want = 7;
        sim_fittings(&g_sim, 15, 17, b, &want);
        if (round == 0) {
            CHECK(want == 0 && get16(b + 0x1C + 12) == 4 && b[0x94 + 6] == 77 && st[8] == 0);
            CHECK(m[0] == 15 && m[1] == 6 && get16(m + 2) == 3 && m[4] == 33 && m[5] == 0);
        } else if (round == 1) {
            CHECK(want == 0 && st[8] == 0 && m[0] == 15 && get16(m + 2) == 1 && world_hospital(&g_world, 15)[0xDC] == 0);
        } else {
            CHECK(want == 0 && m[0] == 15);
            sold2 += get16(m + 2) == 2;
        }
    }
    double rate = (double)sold2 / n;
    CHECK(rate > 0.62 && rate < 0.68);
    /* hospital 0 never sells: the same list with id 3 keeps its item and its wish */
    g_world.block[BLK_ROOM_COUNTS][17] = 1;
    uint8_t *b0 = sim_room_record(&g_sim, 0, 17, 0), *st0 = store_world(0), *m = g_world.block[BLK_FURNITURE_MARKET];
    for (int i = 0; i < 10; i++) put16(b0 + 0x1C + 2 * i, i == 6 ? 3 : 1);
    memset(st0, 0, 730);
    put16(st0 + 10, 4), st0[8] = 1;
    memset(m, 0xFF, 300 * 8);
    uint8_t want = 7;
    sim_fittings(&g_sim, 0, 17, b0, &want);
    CHECK(want == 7 && get16(b0 + 0x1C + 12) == 3 && m[0] == 0xFF && st0[8] == 1);
}

/* 0x4E1BD: an AI hospital with 4 departments, cash 2,000 and expected cash 640 starts a project at hour 12: level
 * 10, duration 20-51, cost ((10 + 3, 5 or 6) x duration) / 2 paid at once; at hour 9 it starts none; an ended one
 * (+0xFA negative) is cleared. 30 draw sequences, so that all three types occur. */
static void test_project_start(void) {
    static const int ADD[4] = {0, 3, 5, 6};
    int seen[4] = {0};
    for (int trial = 0; trial < 31; trial++) {
        int hour = trial ? 12 : 9;
        fresh();
        /* another draw sequence per trial, a seed each: 30 streams shifted by one draw missed one of the three types
         * after the picker's weighted draw (docs/sim-drift5.md) */
        sim_seed(&g_sim, 3000u + (uint64_t)trial);
        uint8_t *r = world_hospital(&g_world, 15);
        g_world.block[BLK_ROOM_COUNTS][15 * 100 + 5] = 2, g_world.block[BLK_ROOM_COUNTS][15 * 100 + 15] = 2;
        world_set_tail(&g_world, TAIL_HOUR, hour);
        world_set_cash(&g_world, 15, 2000);
        put32(r + 0xE2, 640);
        sim_ai_run(&g_sim, 0x4E1BD);
        if (hour == 9) {
            CHECK(r[0xF4] == 0 && world_cash(&g_world, 15) == 2000);
            continue;
        }
        int type = r[0xF4], dur = get16(r + 0xF6), cost = get16(r + 0xF8);
        seen[type & 3] = 1;
        CHECK(type >= 1 && type <= 3 && r[0xF5] == 10 && dur >= 20 && dur <= 51 && get16(r + 0xFA) == dur);
        CHECK(type >= 1 && type <= 3 && cost == (10 + ADD[type]) * dur / 2 && cost == sim_project_cost(type, 10, dur));
        CHECK(world_cash(&g_world, 15) == 2000 - cost && get32(r + 0x102) == cost && get32(r + 0x146) == cost);
        put16(r + 0xFA, -1);
        sim_ai_run(&g_sim, 0x4E1BD);
        CHECK(r[0xF4] == 0 && get16(r + 0xF6) == 0 && get16(r + 0xFA) == 0 && get32(r + 0x146) == cost);
    }
    CHECK(seen[1] && seen[2] && seen[3]);
}

/* 0x4E368: a level-11 project (Q = 148 > every t&127) reaches 100 people per run at hour 12, all positive except
 * the t&31 = 22 negatives; it runs 4 more times, the fifth run ends it (+0xFA = -1, cool-down 30, +0x106 = -1). At
 * hour 4 the runs count down but reach nobody. With +0x172 at 10 the opinions of hospital 0 stay (the penalty
 * that zeroes them is copy protection, docs/mainloop.md section 7). */
static void test_project_run(void) {
    for (int hour = 4; hour <= 12; hour += 8) {
        fresh();
        uint8_t *r = world_hospital(&g_world, 15), *people = g_world.block[BLK_PATIENTS];
        for (int i = 1; i < 1009; i++) {
            uint8_t *p = people + i * 106;
            p[0] = 0, p[1] = 1, p[3] = 0xFF, p[5] = 0, p[104] = 0, p[20 + 15] = 50, p[20] = 50;
        }
        put16(r + 0x172, 10); /* the copy-protection counter: it must not touch the opinions of hospital 0 */
        g_image[0xA867 + hour] = 99;
        world_set_tail(&g_world, TAIL_HOUR, hour);
        r[0xF4] = 1, r[0xF5] = 11, put16(r + 0xFA, 5);
        for (int run = 0; run < 5; run++) sim_ai_run(&g_sim, 0x4E368);
        int good = get16(r + 0xFC), neutral = get16(r + 0xFE), bad = get16(r + 0x100);
        long sum = 0;
        for (int i = 1; i < 1009; i++) sum += people[i * 106 + 20 + 15];
        CHECK(get16(r + 0xFA) == -1 && r[0x108] == 30 && get16(r + 0x106) == -1 && r[0xF4] == 1);
        long sum0 = 0;
        for (int i = 1; i < 1009; i++) sum0 += people[i * 106 + 20];
        CHECK(sum0 == 1008 * 50);
        if (hour == 4) {
            CHECK(good + neutral + bad == 0 && sum == 1008 * 50);
            continue;
        }
        CHECK(good + bad == 400 && neutral == 0 && bad > 0 && bad < 30 && get32(r + 0x142) == 2 * good - bad);
        CHECK(sum == 1008 * 50 + 2 * good - bad);
    }
}

/* 0x3DF91: the course goes to the first idle employee it fits, not to the first idle employee: slot 0 (group 1) is
 * skipped for a group-8 course, slot 1 (group 8) is booked and the price paid. */
static void test_course_booking(void) {
    fresh();
    world_set_tail(&g_world, TAIL_DAY, 10);
    world_set_tail(&g_world, TAIL_HOUR, 10);
    world_set_cash(&g_world, 15, 2000);
    memset(g_world.block[BLK_COURSES], 0xFF, 200);
    uint8_t *c = g_world.block[BLK_COURSES];
    c[0] = 8, c[1] = 5, c[2] = c[3] = 4, c[4] = 3;
    g_image[0x40D9 + 3 * 22 + 9] = 30;
    uint8_t *sl = staff_slots(&g_sim, 15);
    put16(sl, 40), sl[2] = 1;
    put16(sl + 20, 41), sl[22] = 8;
    const uint8_t *r = world_hospital(&g_world, 15);
    for (int i = 0; i < 5000 && !r[0x14D]; i++) sim_staff_run(&g_sim, 0x3DF91);
    CHECK(r[0x14D] == 1 && sl[23] == 0x19 && sl[24] == 0 && sl[3] == 0 && g_world.block[BLK_STAFF][41 * 32 + 4] == 0x19);
    CHECK(world_cash(&g_world, 15) == 1970 && get32(r + 0x11C) == 30 && get32(r + 0x13E) == 30);
}

/* ---- the bed wards' request byte +6, meals and the kitchen (docs/sim-rooms2.md, "The bed wards' request byte +6 and
 * the kitchen's meals"; docs/sim-rooms.md, 0x2995B) ------------------------------------------------------------ */

/* Synthetic room descriptions: wards (6, 16) and kitchen (13) with furniture at +0x40, equipment at +0x60 (10
 * slots), conditions at +0x90 / +0xA0, room type at +0xC0. Equipment items: 1 a meal (capability 0x2B), 2 an item
 * of capability 0x20, 3 an item of kind 5, 4 a utensil (0x2C), 5 the kitchen's basics (9, 0x24, kind 0x11). */
static void room_descriptions(void) {
    static const uint16_t D[7] = {0x40, 5, 0x60, 10, 0x90, 0xA0, 0xC0};
    static const int CATS[3] = {6, 13, 16};
    for (int c = 0; c < 3; c++)
        for (int i = 0; i < 7; i++) put16(g_image + 0x4758 + 14 * CATS[c] + 2 * i, D[i]);
    uint8_t *E = g_image + 0x144A;
    E[20 * 1 + 10] = 0x2B, E[20 * 1 + 16] = 1;
    E[20 * 2 + 10] = 0x20, E[20 * 2 + 16] = 1;
    E[20 * 3 + 6] = 5, E[20 * 3 + 16] = 1;
    E[20 * 4 + 10] = 0x2C, E[20 * 4 + 16] = 1;
    E[20 * 5 + 10] = 9, E[20 * 5 + 11] = 0x24, E[20 * 5 + 6] = 0x11, E[20 * 5 + 16] = 1;
}
static void install(uint8_t *rec, int slot, int id, int cond) { put16(rec + 0x60 + 2 * slot, id), rec[0xA0 + slot] = (uint8_t)cond; }

/* A ward of category cat for hospital 15 with a nurse and patient 5 in bed 0 (hunger 0, health 95). */
static uint8_t *ward_world(int cat) {
    patient_world();
    room_descriptions();
    world_set_cash(&g_world, 15, 1000);
    g_world.block[BLK_ROOM_COUNTS][15 * 100 + cat] = 1;
    uint8_t *ward = sim_room_record(&g_sim, 15, cat, 0), *p = patient(5);
    put16(ward + 8, 7), ward[10] = 10, ward[12] = 1, put16(ward + 0x14, 5);
    p[3] = 15, p[4] = (uint8_t)cat, p[44] = 95, p[40] = 0, p[100] = 2;
    return ward;
}

/* 0x6A0BA / 0x6C3D5, stepped treatment: the use of k = T[20 + field 45] writes +6 whatever it held - 0 when the item
 * was found (the step goes on: field 105 = duration), k when it is missing (field 45 back up, field 105 = 5). In the
 * neuro ward the byte is +6 as well, and +0x10 is left alone. */
static void test_ward_step_use(void) {
    for (int cat = 6; cat <= 16; cat += 10)
        for (int found = 0; found <= 1; found++) {
            uint8_t *ward = ward_world(cat), *p = patient(5);
            uint8_t *t = g_image + 0x270C + 30 * 38;
            t[36] = 1, t[37] = 2, t[16] = 3, t[17] = 3, t[21] = 0x20; /* two steps of code 3; step 2 needs 0x20 */
            g_image[0x44A5 + 3] = 4;
            p[43] = 30, p[93] = 0xFF, p[45] = 2, p[105] = 0;
            ward[6] = 0x33, ward[0x10] = 0x44; /* +6 set: the check before the use does not run */
            if (found) install(ward, 0, 2, 100);
            sim_rooms_run(&g_sim, cat == 6 ? 0x6A0BA : 0x6C3D5);
            if (found) CHECK(ward[6] == 0 && p[45] == 1 && p[105] == 8 && ward[0xA0] == 99);
            else CHECK(ward[6] == 0x20 && p[45] == 2 && p[105] == 5);
            CHECK(ward[0x10] == 0x44);
        }
}

/* SIM-88: run actual clock tick 73, which schedules both ward routines. Only one ward has a patient.
 * Entire saved player blocks must match, including old flags and the unselected player's bytes. */
static void test_ward_step_notification(void) {
    static const int HOSPITALS[] = {0, 1, 2, 15};
    static const uint8_t OLD[] = {0, 55, 255};
    enum { EXPIRED, EXPIRES_NOW, RUNNING, SINGLE, NO_NURSE, MISSING_CAP, CASES };
    uint32_t raw[64];
    for (size_t i = 0; i < sizeof raw / sizeof *raw; i++) raw[i] = 14;
    for (int cat = 6; cat <= 16; cat += 10)
        for (size_t hc = 0; hc < sizeof HOSPITALS / sizeof *HOSPITALS; hc++)
            for (int watched = 0; watched < 2; watched++)
                for (size_t old = 0; old < sizeof OLD / sizeof *OLD; old++)
                    for (int mode = 0; mode < CASES; mode++)
                        for (int human = 0; human < 2; human++)
                            for (int screen = 0; screen < 2; screen++) {
                                patient_world();
                                room_descriptions();
                                int h = HOSPITALS[hc], flag = cat == 6 ? 0x35 : 0x38;
                                /* The other scheduled routine's picker selects hospital 14, which has no rooms. */
                                world_hospital(&g_world, 14)[0x15] = 0xFF;
                                world_hospital(&g_world, h)[0x14] = human ? 0xFF : 0;
                                g_world.block[BLK_ROOM_COUNTS][h * 100 + cat] = 1;
                                uint8_t *ward = sim_room_record(&g_sim, h, cat, 0), *p = patient(261);
                                memcpy(p, patient(5), 106);
                                put16(ward + 8, mode == NO_NURSE ? 0 : 7);
                                ward[10] = 10, ward[12] = 1, put16(ward + 0x14, 261);
                                ward[6] = 0x33; /* skip the preliminary capability scan; test the step's use */
                                p[3] = (uint8_t)h, p[4] = (uint8_t)cat, p[44] = 100, p[100] = 2;
                                p[43] = 30, p[93] = 0xFF, p[45] = 2, p[57] = 1;
                                put16(p + 94, 700); /* no unrelated blood-recovery draw */
                                p[105] = mode == RUNNING ? 2 : mode == EXPIRES_NOW ? 1 : 0;
                                uint8_t *t = g_image + 0x270C + 30 * 38;
                                t[36] = mode == SINGLE ? 0 : 1;
                                t[37] = 2, t[16] = 3, t[17] = 3, t[21] = 0x20;
                                g_image[0x44A5 + 3] = 4;
                                if (mode != MISSING_CAP) install(ward, 0, 2, 100);
                                uint8_t *pb[2] = {g_world.block[BLK_PLAYER0], g_world.block[BLK_PLAYER1]};
                                uint8_t *expected[2];
                                for (int player = 0; player < 2; player++) {
                                    size_t n = g_world.size[BLK_PLAYER0 + player];
                                    memset(pb[player], 0x62 + player, n);
                                    int matching = h <= 1 && player != h ? !watched : watched;
                                    put16(pb[player] + 0x2C, matching ? 261 : 517); /* same low byte: test u16 matching */
                                    pb[player][flag] = OLD[old];
                                    expected[player] = malloc(n);
                                    CHECK(expected[player] != NULL);
                                    if (!expected[player]) exit(1);
                                    memcpy(expected[player], pb[player], n);
                                }
                                int advances = mode == EXPIRED || mode == EXPIRES_NOW || mode == MISSING_CAP;
                                if (advances && watched && h <= 1) expected[h][flag] = 0xFF;
                                SimScript draws = {.v = raw, .n = sizeof raw / sizeof *raw};
                                g_sim.rng_script = &draws;
                                g_sim.screen = screen ? 40 : 0; /* an unrelated screen, or no screen */
                                g_sim.tick = 72;
                                sim_tick(&g_sim);
                                g_sim.rng_script = NULL;
                                CHECK(g_sim.tick == 73 && calls_of(0x6A0BA) == 1 && calls_of(0x6C3D5) == 1);
                                /* Picker: 2 draws; care: 1 + nurse bonus; worsening: 1; single or neuro tail: 1. */
                                size_t expected_draws = mode == NO_NURSE ? 4 :
                                    mode == SINGLE || cat == 16 ? 6 : 5;
                                CHECK(draws.used == expected_draws && draws.extra == 0);
                                for (int player = 0; player < 2; player++) {
                                    CHECK(memcmp(pb[player], expected[player], g_world.size[BLK_PLAYER0 + player]) == 0);
                                    free(expected[player]);
                                }
                                CHECK(p[48] == advances && p[53] == advances);
                                CHECK(p[55] == (advances ? mode == MISSING_CAP ? 1 : 2 : 0));
                                CHECK(p[45] == (advances && mode != MISSING_CAP ? 1 : 2));
                                CHECK(p[105] == (mode == RUNNING ? 1 : advances ? mode == MISSING_CAP ? 5 : 8 : 0));
                                if (mode == MISSING_CAP) CHECK(ward[6] == 0x20);
                            }
}

/* The meal loop (field 40 hunger, capability 0x2B): from hunger 80 it runs while meals are found and hunger is 10 or
 * more. 50 uses of two meals that do not fill (byte 5 = 0) all run and the round without a meal sets +6 = 0x2B; a
 * filling meal (byte 5 = 10, -80) ends it at hunger below 10 with +6 = 0 and the meal still there. */
static void test_ward_meals(void) {
    for (int filling = 0; filling <= 1; filling++) {
        uint8_t *ward = ward_world(6), *p = patient(5);
        g_image[0x144A + 20 * 1 + 5] = filling ? 10 : 0;
        install(ward, 0, 1, 25), install(ward, 1, 1, 25);
        p[40] = 90, p[53] = 0;
        sim_rooms_run(&g_sim, 0x6A0BA);
        if (!filling) CHECK(p[53] == 50 && get16(ward + 0x60) == 0 && get16(ward + 0x62) == 0 && ward[6] == 0x2B && p[40] == 90);
        else CHECK(p[53] == 2 && p[40] == 0 && ward[6] == 0 && ward[0xA0] == 23 && get16(ward + 0x62) == 1);
    }
}

/* 0x2995B / 0x2A01F, supplies step: a missing capability (+6) or kind (+7) taken from the store is installed and
 * clears the byte; one the ward has stays set; one the store lacks stays set and asks the store's purchase (h+0xDA).
 * The Krankenzimmer needs a store room only, the neuro ward also the clerk. */
static void test_ward_supplies(void) {
    for (int k = 0; k < 6; k++) {
        int cat = k < 4 ? 6 : 16;
        uint8_t *ward = ward_world(cat), *st = store_world(15), *r = world_hospital(&g_world, 15);
        put16(ward + 0x14, 0), ward[12] = 0; /* no patient: the call ends in the bed draw */
        st[7] = 20;
        if (k == 0 || k == 4 || k == 5) put16(st + 0xFA, 1), st[0x262] = 80, st[9] = 1, ward[6] = 0x2B; /* a meal */
        if (k == 1) put16(st + 0xFA, 3), st[0x262] = 80, st[9] = 1, ward[7] = 5; /* an item of kind 5 */
        if (k == 2) install(ward, 0, 2, 50), ward[6] = 0x20; /* the ward has it */
        if (k == 3) ward[6] = 0x20; /* nobody has it */
        if (k == 5) put16(st + 4, 40); /* the neuro ward's clerk */
        sim_rooms_run(&g_sim, cat == 6 ? 0x2995B : 0x2A01F);
        if (k == 0 || k == 5) CHECK(ward[6] == 0 && get16(ward + 0x60) == 1 && ward[0xA0] == 80 && st[9] == 0);
        if (k == 1) CHECK(ward[7] == 0 && get16(ward + 0x60) == 3 && st[9] == 0);
        if (k == 2) CHECK(ward[6] == 0x20 && r[0xDA] == 0);
        if (k == 3) CHECK(ward[6] == 0x20 && r[0xDA] == 0x20);
        if (k == 4) CHECK(ward[6] == 0x2B && get16(ward + 0x60) == 0 && st[9] == 1);
    }
}

/* 0x2995B: without a nurse the call ends after the nurse request (a cured patient is not discharged); with one the
 * comfort counter +0x0F is set to r15 + 22 with r7 = 5 when it is 0, and the release at hour 21 clears it. */
static void test_ward_operator_nurse(void) {
    uint8_t *ward = ward_world(6), *p = patient(5);
    put16(ward + 8, 0), put16(p + 56, 0), p[93] = 0; /* cured, no nurse, nobody to assign */
    for (int i = 0; i < 50; i++) sim_rooms_run(&g_sim, 0x2995B);
    CHECK(p[4] == 6 && get16(ward + 0x14) == 5 && get16(world_hospital(&g_world, 15) + 0x26) == 0);
    ward = ward_world(6), p = patient(5);
    p[93] = 0xFF; /* a step running: the patient stays */
    int calls = 0;
    while (calls < 200 && !ward[0x0F]) sim_rooms_run(&g_sim, 0x2995B), calls++;
    CHECK(ward[0x0F] >= 22 && ward[0x0F] <= 37 && calls > 1 && calls < 200);
    put16(ward + 0x14, 0), ward[12] = 0;
    world_set_tail(&g_world, TAIL_HOUR, 21);
    sim_rooms_run(&g_sim, 0x2995B);
    CHECK(get16(ward + 8) == 0 && ward[0x0F] == 0);
}

/* A kitchen for hospital 15 with a bed patient, a store with a clerk and room, cook 40 of group 4; recipe 17: level
 * 5, 3 minutes, meal 79, utensil 1 (capability 0x2C). Recipes 0-16 and 18-32 have level 0. */
static uint8_t *kitchen_world(int bust) {
    uint8_t *ward = ward_world(6);
    (void)ward;
    g_world.block[BLK_ROOM_COUNTS][15 * 100 + 13] = 1;
    uint8_t *k = sim_room_record(&g_sim, 15, 13, 0), *st = store_world(15);
    st[7] = 20, put16(st + 4, 41);
    uint8_t *rc = g_image + 0xAD90 + 8 * 17;
    rc[0] = 5, rc[1] = 3, put16(rc + 2, 79), rc[4] = 1, rc[5] = 0xFF;
    for (int i = 0; i < 33; i++)
        if (i != 17) g_image[0xAD90 + 8 * i + 4] = 0xFF;
    g_image[0xAE9A + 1] = 0x2C;
    uint8_t *cook = g_world.block[BLK_STAFF] + 40 * 32, *sl = staff_slots(&g_sim, 15);
    cook[1] = 4, cook[5] = (uint8_t)bust, cook[15] = (uint8_t)bust, cook[21] = 50; /* level (256 + 2 bust) / 128 */
    put16(sl, 40), sl[2] = 4;
    return k;
}

/* 0x6E161: no cook -> group 4 is asked for and the call ends; a cook at hours 4-19 with store room and a bed patient
 * starts cooking a recipe of the cook's level (at least 3); with no bed patient it stops; from hour 20 the cook goes. */
static void test_kitchen_operator(void) {
    for (int bust = 0; bust <= 200; bust += 200) {
        uint8_t *k = kitchen_world(bust), *cook = g_world.block[BLK_STAFF] + 40 * 32;
        sim_rooms_run(&g_sim, 0x6E161);
        CHECK(get16(k + 6) == 40 && cook[4] == 13 && k[5] == 0);
        sim_rooms_run(&g_sim, 0x6E161);
        if (bust) CHECK(k[5] == 0xFF && get16(k + 0x0E) == 5 && k[8] == 17); /* (256 + 400) / 128 = 5 */
        else CHECK(k[5] == 0xFF && get16(k + 0x0E) == 3 && k[8] >= 1 && k[8] <= 32);
        sim_room_record(&g_sim, 15, 6, 0)[12] = 0; /* no bed patient */
        sim_rooms_run(&g_sim, 0x6E161);
        CHECK(k[5] == 0);
        world_set_tail(&g_world, TAIL_HOUR, 20);
        sim_rooms_run(&g_sim, 0x6E161);
        CHECK(get16(k + 6) == 0 && cook[4] != 13);
    }
}

/* 0x6EDA1: a cooking AI kitchen with all its utensils starts recipe 17 as meal 101 (not 79), uses the utensil, and
 * after 3 minutes puts the meal into the store's first free slot with condition 100 (store +9 + 1, the cook's work
 * counter + 1) and starts the next. A missing utensil (K +2 = 0x2C) or kind (K +3 = 0x11) cooks nothing. */
static void test_kitchen_minute(void) {
    for (int k3 = 0; k3 < 3; k3++) {
        uint8_t *k = kitchen_world(200), *st = sim_room_record(&g_sim, 15, 4, 0), *cook = g_world.block[BLK_STAFF] + 40 * 32;
        put16(k + 6, 40), k[5] = 0xFF, k[8] = 17;
        if (k3 != 1) install(k, 0, 4, 50);
        if (k3 != 2) install(k, 1, 5, 50);
        put16(st + 0xFA, 1), st[9] = 1; /* slot 0 taken */
        sim_rooms_run(&g_sim, 0x6EDA1);
        if (k3 == 1) CHECK(k[2] == 0x2C && get16(k + 0x0C) == 0 && get16(k + 0x0A) == 0);
        if (k3 == 2) CHECK(k[3] == 0x11 && get16(k + 0x0C) == 0 && get16(k + 0x0A) == 0);
        if (k3) {
            for (int m = 0; m < 5; m++) sim_rooms_run(&g_sim, 0x6EDA1);
            CHECK(st[9] == 1 && get16(st + 0xFC) == 0);
            continue;
        }
        CHECK(get16(k + 0x0A) == 101 && get16(k + 0x0C) == 3 && k[0xA0] == 49 && k[2] == 0 && k[3] == 0);
        sim_rooms_run(&g_sim, 0x6EDA1), sim_rooms_run(&g_sim, 0x6EDA1);
        CHECK(st[9] == 1 && get16(k + 0x0C) == 1);
        sim_rooms_run(&g_sim, 0x6EDA1);
        CHECK(st[9] == 2 && get16(st + 0xFC) == 101 && st[0x263] == 100 && get16(cook + 0x10) == 1);
        CHECK(get16(k + 0x0C) == 3 && k[0xA0] == 48);
    }
}

/* The meal loop's start and repeat (docs/sim-rooms2.md, "Meals"): a round runs when (+6 = 0 and hunger >= 10) or
 * (+6 = 0x2B and hunger >= 80), and again while +6 = 0 and hunger >= 10; any other +6 means no round. Meal 1 lowers
 * hunger by 8 and has uses enough for every case. */
static void test_meal_start(void) {
    static const int REQ[3] = {0, 0x2B, 5}, HUNGER[5] = {9, 10, 30, 79, 80};
    for (int r = 0; r < 3; r++)
        for (int k = 0; k < 5; k++) {
            uint8_t *ward = ward_world(6), *p = patient(5);
            g_image[0x144A + 20 * 1 + 5] = 1, g_image[0x144A + 20 * 1 + 16] = 3; /* wear 3 per use */
            install(ward, 0, 1, 100); /* enough for every round, within the signed condition range */
            ward[6] = (uint8_t)REQ[r], p[40] = (uint8_t)HUNGER[k], p[53] = 0;
            sim_rooms_run(&g_sim, 0x6A0BA);
            int h0 = HUNGER[k], eats = (REQ[r] == 0 && h0 >= 10) || (REQ[r] == 0x2B && h0 >= 80);
            int meals = eats ? (h0 - 2) / 8 : 0; /* down to the first value below 10 */
            CHECK(p[53] == meals && p[40] == (eats ? h0 - 8 * meals : h0));
            CHECK(ward[6] == (eats ? 0 : REQ[r]) && ward[0xA0] == 100 - 3 * meals);
        }
}

/* One meal round's mood and opinion (byte 4 = 8: the +5 for byte 4 >= 3, then two separate r15 draws, the first
 * + 5, the second - 5 and opinion - 1): from mood 40 / opinion 30 the four outcomes 50/30, 45/29 (both draws),
 * 45/30 (neither) and 40/29 all occur, and 45/29 only with two independent draws. From mood and opinion 100 both
 * stay 100 in every case, because they are clamped once at the end of the round (clamping after each step would
 * leave 95 / 99 when only the second draw fires); with byte 4 = 0 from mood 2 / opinion 0 the round ends at 0 / 0
 * (- 5 and - 1 clamped from below). */
static void test_meal_effects(void) {
    int seen[4] = {0, 0, 0, 0}, unclamped = 0;
    for (int top = 0; top <= 2; top++)
        for (int trial = 0; trial < 64; trial++) {
            uint8_t *ward = ward_world(6), *p = patient(5);
            g_image[0x144A + 20 * 1 + 4] = top == 2 ? 0 : 8, g_image[0x144A + 20 * 1 + 5] = 1;
            install(ward, 0, 1, 1); /* one use */
            static const uint8_t MOOD[3] = {40, 100, 2}, OP[3] = {30, 100, 0};
            ward[6] = 0, p[40] = 12, p[78] = MOOD[top], p[20 + 15] = OP[top];
            for (int i = 0; i < trial; i++) sim_bits(&g_sim, 1);
            sim_rooms_run(&g_sim, 0x6A0BA);
            CHECK(p[53] == 1);
            if (top) unclamped += p[78] != MOOD[top] / 100 * 100 || p[20 + 15] != OP[top];
            else if (p[78] == 50 && p[20 + 15] == 30) seen[0]++;
            else if (p[78] == 45 && p[20 + 15] == 29) seen[1]++;
            else if (p[78] == 45 && p[20 + 15] == 30) seen[2]++;
            else if (p[78] == 40 && p[20 + 15] == 29) seen[3]++;
            else CHECK(0 && "mood and opinion after one meal");
        }
    CHECK(seen[0] && seen[1] && seen[2] && seen[3] && !unclamped);
}

/* SIM-42 (docs/sim-rooms2.md, "Shared helpers", "A worn-out item runs the whole list tidy-up, never a recompute"):
 * in both wards a meal used up in hospital 15's meal round leaves the ward, and the list tidy-up 0x2BFB7 runs over
 * every hospital: hospital 3's store (meal 1 in slot 0, the non-meal 2 in slot 1) comes out as 2, 1 (the meal pass),
 * and hospital 2's Krankenzimmer with a gap in front of item 3 is packed. A worn-out item that counts (capability 0x20
 * in the stepped treatment) leaves the ward the same way, and the ward's rating bytes 0 / 1 and beds +10 keep their
 * values: no recompute. */
static void test_sim42_worn_item_tidies(void) {
    for (int cat = 6; cat <= 16; cat += 10)
        for (int step = 0; step <= 1; step++) {
            uint8_t *ward = ward_world(cat), *p = patient(5), *st = store_world(3);
            g_world.block[BLK_ROOM_COUNTS][2 * 100 + 6] = 1;
            uint8_t *other = sim_room_record(&g_sim, 2, 6, 0);
            install(other, 0, 0, 0), install(other, 1, 3, 33);
            put16(st + 0xFA, 1), st[0x262] = 40, put16(st + 0xFC, 2), st[0x263] = 50, st[9] = 2;
            ward[0] = 0x77, ward[1] = 0x66, ward[10] = 9;
            if (step) {
                uint8_t *t = g_image + 0x270C + 30 * 38;
                t[36] = 1, t[37] = 2, t[16] = 3, t[17] = 3, t[21] = 0x20; /* step 2 needs capability 0x20 */
                g_image[0x44A5 + 3] = 4;
                p[43] = 30, p[93] = 0xFF, p[45] = 2, p[105] = 0;
                ward[6] = 0x33; /* no meal round, no check before the use */
                install(ward, 0, 2, 1);
            } else {
                g_image[0x144A + 20 * 1 + 5] = 2; /* hunger - 16 */
                install(ward, 0, 1, 1);           /* one use left */
                ward[6] = 0, p[40] = 12, p[53] = 0;
            }
            sim_rooms_run(&g_sim, cat == 6 ? 0x6A0BA : 0x6C3D5);
            if (step) CHECK(p[45] == 1 && ward[6] == 0);
            else CHECK(p[53] == 1 && p[40] == 0 && ward[6] == 0);
            CHECK(get16(ward + 0x60) == 0 && ward[0xA0] == 0);
            CHECK(get16(st + 0xFA) == 2 && st[0x262] == 50 && get16(st + 0xFC) == 1 && st[0x263] == 40);
            CHECK(get16(other + 0x60) == 3 && other[0xA0] == 33 && get16(other + 0x62) == 0);
            CHECK(ward[0] == 0x77 && ward[1] == 0x66 && ward[10] == 9);
        }
}

/* SIM-43 (docs/mainloop.md, module H item 7): the meal-round newspaper item 0x1D written by both wards carries the id
 * of the meal just eaten in u16 14 - here meal 103, which this use wears out - with b21 = its byte 4 (1), byte 8 the
 * hospital and u16 16 the patient. */
static void test_sim43_comfort_meal_id(void) {
    for (int cat = 6; cat <= 16; cat += 10) {
        uint8_t *ward = ward_world(cat), *p = patient(5);
        uint8_t *m = g_image + 0x144A + 20 * 103;
        m[10] = 0x2B, m[16] = 1, m[5] = 2, m[4] = 1;
        install(ward, 0, 103, 1);
        ward[6] = 0, p[40] = 12;
        sim_rooms_run(&g_sim, cat == 6 ? 0x6A0BA : 0x6C3D5);
        int found = -1, n = 0;
        for (int i = 0; i < 100; i++)
            if (sim_news_item(&g_sim, i)[0] && sim_news_item(&g_sim, i)[20] == 0x1D) found = i, n++;
        CHECK(n == 1 && get16(ward + 0x60) == 0);
        if (found < 0) continue;
        const uint8_t *it = sim_news_item(&g_sim, found);
        CHECK(get16(it + 14) == 103 && it[21] == 1 && it[8] == 15 && get16(it + 16) == 5);
    }
}

/* 0x2A873, the store tidy-up: one gap per list (furniture and equipment, for every hospital with a store room), then
 * meals (capability 0x2B, item 1) move behind the other equipment up to the first empty slot. */
static void test_store_tidy(void) {
    fresh();
    room_descriptions();
    uint8_t *a = store_world(3), *b = store_world(15);
    for (int k = 0; k < 2; k++) {
        uint8_t *st = k ? b : a;
        static const uint16_t E[6] = {0, 0, 1, 2, 1, 3};
        for (int i = 0; i < 6; i++) put16(st + 0xFA + 2 * i, E[i]), st[0x262 + i] = (uint8_t)(10 + i);
        put16(st + 0x0A, 0), put16(st + 0x0C, 9), st[0x1EB] = 77, put16(st + 0x0A + 2 * 119, 9), st[0x1EA + 119] = 5;
        put16(st + 0xFA + 2 * 119, 2), st[0x262 + 119] = 6;
    }
    sim_store_tidy(&g_sim);
    for (int k = 0; k < 2; k++) {
        uint8_t *st = k ? b : a;
        /* furniture: the gap at 0 closed, the last slot emptied */
        CHECK(get16(st + 0x0A) == 9 && st[0x1EA] == 77 && get16(st + 0x0A + 2 * 118) == 9 && get16(st + 0x0A + 2 * 119) == 0);
        /* equipment: one gap of two closed, so slot 0 is still empty and the meal pass does nothing */
        CHECK(get16(st + 0xFA) == 0 && get16(st + 0xFA + 2) == 1 && st[0x263] == 12 && get16(st + 0xFA + 2 * 118) == 2 &&
              get16(st + 0xFA + 2 * 119) == 0);
    }
    sim_store_tidy(&g_sim);
    /* the second gap closed: 1 2 1 3 -> the meal pass gives 2 1 1 3 -> 2 3 1 1 (conditions follow) */
    static const uint16_t WANT[6] = {2, 3, 1, 1, 0, 0};
    static const uint8_t COND[4] = {13, 15, 14, 12};
    for (int i = 0; i < 6; i++) CHECK(get16(a + 0xFA + 2 * i) == WANT[i]);
    for (int i = 0; i < 4; i++) CHECK(a[0x262 + i] == COND[i]);
    CHECK(get16(a + 0xFA + 2 * 117) == 2 && get16(a + 0xFA + 2 * 118) == 0); /* the old last item, moved twice */
}

/* A take from the store (0x2D003) runs the tidy-up, and the install (0x2B07E) runs it twice more: with two gaps in
 * front of the taken item the list is packed again afterwards and its meal is behind the other item. */
static void test_store_take_tidies(void) {
    uint8_t *ward = ward_world(6), *st = store_world(15);
    st[7] = 20, st[9] = 3;
    static const uint16_t E[5] = {0, 0, 2, 1, 3};
    for (int i = 0; i < 5; i++) put16(st + 0xFA + 2 * i, E[i]), st[0x262 + i] = (uint8_t)(50 + i);
    CHECK(sim_store_supply(&g_sim, 15, 6, ward, 0x20, 0) == 1);
    CHECK(get16(ward + 0x60) == 2 && ward[0xA0] == 52 && st[9] == 2);
    CHECK(get16(st + 0xFA) == 3 && st[0x262] == 54 && get16(st + 0xFA + 2) == 1 && st[0x263] == 53 && get16(st + 0xFA + 4) == 0);
}

/* The install 0x2B07E recounts every category (0x2B210), not only the room's: a kitchen of another hospital too. */
static void test_store_take_recounts_all(void) {
    uint8_t *ward = ward_world(6), *st = store_world(15);
    st[7] = 20, st[9] = 1, put16(st + 0xFA, 2), st[0x262] = 50;
    g_image[0xEE0 + 18 * 2 + 4] = 4;
    g_world.block[BLK_ROOM_COUNTS][3 * 100 + 13] = 1;
    uint8_t *kitchen = sim_room_record(&g_sim, 3, 13, 0);
    put16(kitchen + 0x40, 2);
    CHECK(sim_store_supply(&g_sim, 15, 6, ward, 0x20, 0) == 1 && kitchen[0] == 4);
}

/* 0x2F4C9: a meal (first capability 0x2B) is bought when its catalog +4 and +5 are both non-zero, otherwise the
 * offer is skipped and the request stays. */
static void test_meal_purchase(void) {
    for (int k = 0; k < 3; k++) {
        fresh();
        int h = 15;
        uint8_t *mk = g_world.block[BLK_EQUIPMENT_MARKET];
        for (int i = 0; i < 300; i++) mk[i * 8] = 0xFF;
        mk[0] = 3, mk[1] = 5, put16(mk + 2, 9), mk[4] = 90, mk[5] = 0;
        uint8_t *it = g_image + 0x144A + 9 * 20;
        it[10] = 0x2B, it[4] = k == 1 ? 0 : 2, it[5] = k == 2 ? 0 : 1;
        uint8_t *st = store_world(h), *r = world_hospital(&g_world, h);
        st[7] = 20, r[0xDA] = 0x2B;
        world_set_cash(&g_world, h, 1000);
        sim_staff_run(&g_sim, 0x2F3FF);
        if (k == 0) CHECK(get16(st + 0xFA) == 9 && st[9] == 1 && r[0xDA] == 0 && mk[0] == 0xFF);
        else CHECK(get16(st + 0xFA) == 0 && st[9] == 0 && r[0xDA] == 0x2B && mk[0] == 3);
    }
}

/* 0x6337D, common plan, the department tests after c18 = 0: build 5 when c5 < 10, c5 < N and the waiting patients
 * with a wish or a type-0 illness are at least c5 + 6; 11 likewise with no-wish type-1 cases >= c11 + 7; 15 with
 * no-wish type-2 cases >= c15 + 6. N = (30 x (month - 1) + day) mod 9 + 2 = 3 on 1 January. Every other step of the
 * plan is satisfied, so a failed test ends in "nothing" (0). */
static void test_ai_department_tests(void) {
    /* {c5, c11, c15, n, type, n2, type2, expected}; type 3 = a wish with a type-1 illness; the second group sits in a
     * second waiting room */
    static const int CASES[][8] = {
        {1, 1, 2, 7, 0, 0, 0, 5},  {1, 1, 2, 6, 0, 0, 0, 0},  {1, 1, 2, 7, 3, 0, 0, 5}, {3, 1, 2, 9, 0, 0, 0, 0},
        {2, 1, 2, 8, 0, 0, 0, 5},  {1, 1, 2, 8, 1, 0, 0, 11}, {1, 1, 2, 7, 1, 0, 0, 0}, {1, 1, 2, 9, 3, 0, 0, 5},
        {1, 3, 2, 10, 1, 0, 0, 0}, {1, 1, 2, 8, 2, 0, 0, 15}, {1, 1, 2, 7, 2, 0, 0, 0}, {1, 1, 3, 10, 2, 0, 0, 0},
        {1, 1, 2, 7, 0, 8, 1, 5},  {1, 1, 2, 8, 1, 8, 2, 11}, {1, 1, 2, 8, 2, 7, 0, 5},
    };
    for (unsigned n = 0; n < sizeof CASES / sizeof CASES[0]; n++) {
        const int *cs = CASES[n];
        patient_world();
        room_descriptions();
        int h = 15;
        uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + h * 100, *r = world_hospital(&g_world, h);
        for (int k = 1; k <= 18; k++) c[k] = 1;
        c[3] = 2, c[5] = (uint8_t)cs[0], c[11] = (uint8_t)cs[1], c[15] = (uint8_t)cs[2];
        g_image[0x31F2 + 0x29] = 10; /* catalog base of room type 0: 10 beds */
        world_set_tail(&g_world, TAIL_HOUR, 7);
        world_set_cash(&g_world, h, 1000);
        put32(r + 0xE2, 1000);
        uint8_t *kz = sim_room_record(&g_sim, h, 6, 0), *nz = sim_room_record(&g_sim, h, 16, 0);
        kz[10] = 10, kz[12] = 0, nz[10] = 10, nz[12] = 0;
        for (int t = 0; t < 3; t++) g_image[0x954 + (20 + t) * 20 + 8] = (uint8_t)t; /* illnesses 20-22: types 0-2 */
        for (int g = 0; g < 2; g++) {
            uint8_t *wr = sim_room_record(&g_sim, h, 3, g);
            int count = cs[3 + 2 * g], type = cs[4 + 2 * g];
            for (int i = 0; i < count; i++) {
                uint8_t *p = patient(100 + 10 * g + i);
                put16(wr + 0x14 + 4 * i, 100 + 10 * g + i);
                p[3] = (uint8_t)h, p[4] = 3, p[56] = (uint8_t)(type == 3 ? 21 : 20 + type), p[57] = type == 3;
            }
            wr[10] = 10, wr[12] = (uint8_t)count;
        }
        int got = sim_ai_choice(&g_sim, h);
        if (got != cs[7]) fprintf(stderr, "  department case %u: %d, want %d\n", n, got, cs[7]);
        CHECK(got == cs[7]);
    }
}

/* The bring routines of the treatment room (0x2E252), the dental ward (0x34BD6) and the neurology (0x359E1): the
 * waiting patient goes into the free room with state 5, 11 and 15 and field 66 = the room's index. */
static void test_bring_states(void) {
    static const int CAT[3] = {5, 11, 15}, STATE[3] = {5, 11, 15};
    for (int k = 0; k < 3; k++) {
        patient_world();
        int h = 15;
        uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + h * 100;
        c[3] = 1, c[CAT[k]] = 2;
        g_image[0x954 + (20 + k) * 20 + 8] = (uint8_t)k; /* illness 20 + k of type k */
        uint8_t *tr = sim_room_record(&g_sim, h, CAT[k], 1), *wr = sim_room_record(&g_sim, h, 3, 0), *p = patient(5);
        put16(tr + 8, 7), put16(tr + 10, 8);
        put16(wr + 0x14, 5), wr[12] = 1;
        p[3] = (uint8_t)h, p[4] = 3, p[56] = (uint8_t)(20 + k), p[57] = 0, p[66] = 0x77;
        sim_rooms_after_operator(&g_sim, h, CAT[k], 1);
        CHECK(get16(tr + 12) == 5 && p[4] == STATE[k] && p[66] == 1 && wr[12] == 0);
    }
}

/* 0x6337D, common plan: a Krankenzimmer is built only when its free beds are below 2 (not 2 x (c6 + 1)); with 3 free
 * beds the plan goes on to the blood bank (17). */
static void test_ai_krankenzimmer_test(void) {
    for (int free_beds = 1; free_beds <= 3; free_beds += 2) {
        fresh();
        room_descriptions();
        int h = 15;
        uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + h * 100, *r = world_hospital(&g_world, h);
        c[1] = c[2] = c[3] = c[4] = c[5] = c[11] = c[6] = c[16] = c[13] = 1, c[15] = 2;
        g_image[0x31F2 + 0x29] = 10; /* catalog base of room type 0: 10 beds */
        world_set_tail(&g_world, TAIL_HOUR, 7);
        world_set_cash(&g_world, h, 1000);
        put32(r + 0xE2, 1000);
        uint8_t *kz = sim_room_record(&g_sim, h, 6, 0), *nz = sim_room_record(&g_sim, h, 16, 0);
        kz[10] = 10, kz[12] = (uint8_t)(10 - free_beds), nz[10] = 10, nz[12] = 0;
        CHECK(sim_ai_choice(&g_sim, h) == (free_beds < 2 ? 6 : 17));
    }
}

/* SIM-27 (docs/sim-drift8.md, "What the engine must change", item 7; section 22.3): hospital 9's plan, step 2, builds
 * a neurology only when c15 = 2 (a third one), else goes on to step 3; the threshold X >= 230 with F = 0 is tested
 * first. Hospital 9: c1 = c2 = c3 = c4 = c5 = c11 = 1, c16 = c6 = 0, hour 6, cash 1,000. */
static int sim27_choice(int h, int f, int32_t x, int c15) {
    fresh();
    room_descriptions();
    uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + h * 100, *r = world_hospital(&g_world, h);
    c[1] = c[2] = c[3] = c[4] = c[5] = c[11] = 1, c[15] = (uint8_t)c15;
    put16(r + H_PLOTS_OWNED, f), put16(r + H_PLOTS_RENTED, 0), put16(r + H_PLOTS_BUILT, 0);
    world_set_tail(&g_world, TAIL_HOUR, 6);
    world_set_cash(&g_world, h, 1000);
    put32(r + 0xE2, x);
    return sim_ai_choice(&g_sim, h);
}

static void test_sim27_hospital9_plan(void) {
    static const struct { int h, f, x, c15, want; } C[] = {
        {9, 3, 1000, 1, 16}, /* (a) a second neurology is not built: step 3's c16 = 0 */
        {9, 3, 1000, 2, 15}, /* (b) the third one is */
        {9, 3, 1000, 3, 16}, /* (c) */
        {9, 3, 1000, 0, 15}, /* (d) the ensure step, unchanged */
        {9, 0, 250, 1, 0},   /* (e) step 3's X >= 270 stops */
        {9, 0, 200, 2, 0},   /* (f) step 2's threshold stops first */
        {6, 3, 1000, 1, 15}, /* (g) hospital 6's step 2 is "c15 < 2" */
    };
    for (unsigned k = 0; k < sizeof C / sizeof C[0]; k++) {
        int got = sim27_choice(C[k].h, C[k].f, C[k].x, C[k].c15);
        if (got != C[k].want) fprintf(stderr, "  SIM-27 case %c: %d, want %d\n", 'a' + k, got, C[k].want);
        CHECK(got == C[k].want);
    }
}

/* SIM-28 (item 8; section 22.3): the further torture chamber's test "c8 < 6 and occupancy >= 8 x c8 + 5" sums u16
 * +0x0C over the Neuro-Krankenzimmer (0x1B3A2); the OP's test before it keeps the Krankenzimmer (0x1B356). Hospital 15
 * reaches the common plan's end: every earlier test passes over, c7 = 6, c8 = 1, c12 = c14 = 1, c10 = 0; the wards have
 * enough free beds (or more beds than the catalog's 10) that neither is built. */
static int sim28_choice(int c7, int c8, int kz_occupied, int n16, const int *neuro_occupied) {
    fresh();
    room_descriptions();
    int h = 15;
    uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + h * 100, *r = world_hospital(&g_world, h);
    for (int k = 1; k <= 18; k++) c[k] = 1;
    c[15] = 2, c[7] = (uint8_t)c7, c[8] = (uint8_t)c8, c[10] = 0, c[16] = (uint8_t)n16;
    g_image[0x31F2 + 0x29] = 10; /* catalog base of room type 0: 10 beds */
    world_set_tail(&g_world, TAIL_HOUR, 7);
    world_set_cash(&g_world, h, 1000);
    put32(r + 0xE2, 1000);
    uint8_t *kz = sim_room_record(&g_sim, h, 6, 0);
    kz[10] = 40, put16(kz + 0x0C, kz_occupied);
    for (int i = 0; i < n16; i++) {
        uint8_t *nz = sim_room_record(&g_sim, h, 16, i);
        nz[10] = 40, put16(nz + 0x0C, neuro_occupied[i]);
    }
    return sim_ai_choice(&g_sim, h);
}

static void test_sim28_torture_occupancy(void) {
    static const int ZERO[2] = {0, 0}, B[2] = {7, 6}, C12[2] = {6, 6}, D60[2] = {30, 30}, HIGH[2] = {0x100, 0};
    struct { int c7, c8, kz, n16; const int *nz; int want; } C[] = {
        {6, 1, 20, 1, ZERO, 10}, /* (a) the Krankenzimmer do not count */
        {6, 1, 0, 2, B, 8},      /* (b) neuro wards 7 + 6 = 13 >= 13 */
        {6, 1, 0, 2, C12, 10},   /* (c) 12 */
        {6, 6, 0, 2, D60, 10},   /* (d) c8 = 6 */
        {1, 1, 13, 1, ZERO, 7},  /* (e) the OP's test, unchanged */
        {6, 1, 0, 1, HIGH, 8},   /* the u16: 256 occupied in the high byte */
    };
    for (unsigned k = 0; k < sizeof C / sizeof C[0]; k++) {
        int got = sim28_choice(C[k].c7, C[k].c8, C[k].kz, C[k].n16, C[k].nz);
        if (got != C[k].want) fprintf(stderr, "  SIM-28 case %c: %d, want %d\n", 'a' + k, got, C[k].want);
        CHECK(got == C[k].want);
    }
}

/* ---- ward beds and nurse mood, the doctors' bonuses, tick 64 (docs/engine.md, 2026-10-18 night) ------------------ */

/* Wards of category cat in hospital h, type 1 (catalog +0x29 = 10 beds), with the beds given and no nurse. */
static uint8_t *beds_ward(int h, int cat, int i, int beds) {
    g_world.block[BLK_ROOM_COUNTS][h * 100 + cat] = (uint8_t)(i + 1);
    uint8_t *w = sim_room_record(&g_sim, h, cat, i);
    w[0xC0] = 1, put16(w + 10, beds);
    return w;
}

/* 0x30965 / 0x536D1, step 1: for hospitals 0-15 and every ward, +4 = 8 when +4 is 0 and catalog +0x29 - beds is not 0
 * (also above the capacity: the 16-bit difference is not 0); a ward at capacity and one that wants something else keep
 * their byte. */
static void test_ward_wants_beds(void) {
    for (int cat = 6; cat <= 16; cat += 10)
        for (int h = 0; h <= 9; h += 9) {
            patient_world();
            room_descriptions();
            g_image[0x31F2 + 60 + 0x29] = 10;
            uint8_t *w0 = beds_ward(h, cat, 0, 4), *w1 = beds_ward(h, cat, 1, 10), *w2 = beds_ward(h, cat, 2, 4),
                    *w3 = beds_ward(h, cat, 3, 11);
            w2[4] = 2;
            sim_rooms_run(&g_sim, cat == 6 ? 0x30965 : 0x536D1);
            CHECK(w0[4] == 8 && w1[4] == 0 && w2[4] == 2 && w3[4] == 8);
        }
    /* through the schedule: one game minute (ticks 59 and 61) */
    patient_world();
    room_descriptions();
    g_image[0x31F2 + 60 + 0x29] = 10;
    uint8_t *k = beds_ward(0, 6, 0, 4), *n = beds_ward(0, 16, 0, 4);
    for (int i = 0; i < SIM_TICKS_PER_MINUTE; i++) sim_tick(&g_sim);
    CHECK(k[4] == 8 && n[4] == 8);
}

/* Step 2, the nurse's mood (staff byte 21), only with r15 = 7: a ward that wants something lowers it by (r1 + r1) / 2
 * and the nurse leaves below 20 (+5 = 1, idle through 0x2301C); from 0 it does not wrap. A ward that wants nothing raises it while
 * the rating >= r63 (up to 100), otherwise lowers it, and the nurse leaves below 4 (+5 = 2). */
static void test_ward_nurse_mood(void) {
    for (int cat = 6; cat <= 16; cat += 10)
        for (int k = 0; k < 6; k++) {
            patient_world();
            room_descriptions();
            g_image[0x31F2 + 60 + 0x29] = 10;
            uint8_t *w = beds_ward(9, cat, 0, k < 2 || k == 5 ? 4 : 10), *nurse = g_world.block[BLK_STAFF] + 40 * 32;
            uint8_t *sl = staff_slots(&g_sim, 9);
            put16(sl, 40), sl[2] = 1, sl[3] = (uint8_t)cat, nurse[1] = 1, nurse[4] = (uint8_t)cat;
            put16(w + 8, 40);
            sl[10] = 7, sl[11] = 9, sl[7] = 3; /* 0x2301C keeps slot +0x0A / +0x0B and clears +7 */
            static const uint8_t MOOD[6] = {20, 0, 99, 5, 4, 50};
            nurse[21] = MOOD[k];
            w[0] = k == 3 ? 0 : 200; /* rating 0 or 200 */
            int calls = 0, rises = 0;
            uint32_t routine = cat == 6 ? 0x30965 : 0x536D1;
            if (k == 4) { /* the r15 = 7 gate: about 1 call in 16 changes the mood */
                for (; calls < 320; calls++) {
                    nurse[21] = 4;
                    sim_rooms_run(&g_sim, routine);
                    rises += nurse[21] == 5;
                }
                CHECK(rises >= 8 && rises <= 36 && get16(w + 8) == 40);
                continue;
            }
            if (k == 5) { /* wanting: - 1 with r15 = 7 and both r1 set, 1 call in 64 */
                int drops = 0;
                FILE *log = cat == 16 ? tmpfile() : NULL;
                g_sim.rng_log = log;
                for (; calls < 3200; calls++) {
                    nurse[21] = 50;
                    sim_rooms_run(&g_sim, routine);
                    drops += nurse[21] == 49;
                }
                g_sim.rng_log = NULL;
                if (cat == 6) { /* default inputs: the uniform rate, 50 expected */
                    if (drops < 30 || drops > 72) fprintf(stderr, "  ward mood cat %d drops %d\n", cat, drops);
                    CHECK(drops >= 30 && drops <= 72 && get16(w + 8) == 40);
                    continue;
                }
                /* the Neuro-Krankenzimmer draws with the inputs of docs/rng-sites.md 3.12, whose rates are the
                 * generator's: the rule against the logged draws, exactly (a drop for every r15 = 7 followed by r1 = 1
                 * and r1 = 1, nothing else), the r15 once per call, the two r1 after every 7 and only then */
                CHECK(log != NULL);
                if (!log) continue;
                rewind(log);
                unsigned site, n, v, in_d, in_e;
                char kind;
                int r15 = 0, hits = 0, want = 0, ok = 1, pending = 0, ones = 0;
                while (fscanf(log, "%X %c%u %u %X %X", &site, &kind, &n, &v, &in_d, &in_e) == 6) {
                    if (site == 0x53762) {
                        ok &= pending == 0;
                        r15++, hits += v == 7, pending = v == 7 ? 2 : 0, ones = 0;
                    } else {
                        ok &= (site == 0x53788 || site == 0x53790) && pending > 0;
                        ones += v == 1;
                        if (--pending == 0 && ones == 2) want++;
                    }
                }
                fclose(log);
                if (drops != want || r15 != 3200) fprintf(stderr, "  ward mood cat 16: %d drops, %d wanted, %d hits\n", drops, want, hits);
                CHECK(ok && r15 == 3200 && drops == want && hits >= 100 && hits <= 300 && get16(w + 8) == 40);
                continue;
            }
            if (k == 1) { /* from 0, 200 times: the mood never wraps (the fall (r1 + r1) / 2 is skipped at 0) */
                int wrapped = 0;
                for (int trial = 0; trial < 200; trial++) {
                    put16(w + 8, 40), nurse[21] = 0, w[5] = 0;
                    for (calls = 0; calls < 2000 && get16(w + 8); calls++) sim_rooms_run(&g_sim, routine);
                    wrapped += nurse[21] != 0 || w[5] != 1;
                }
                CHECK(wrapped == 0);
                continue;
            }
            while (calls < 20000 && get16(w + 8)) {
                sim_rooms_run(&g_sim, routine);
                calls++;
                CHECK(nurse[21] <= 100);
                if (k == 2 && calls == 2000) break;
            }
            /* the nurse (group 1) leaves through 0x2301C: idle, not off duty as after the evening release 0x22EDD */
            if (k == 0) CHECK(get16(w + 8) == 0 && w[5] == 1 && nurse[21] == 19 && nurse[4] == 0 && sl[3] == 0 &&
                              sl[7] == 0 && sl[10] == 7 && sl[11] == 9);
            if (k == 2) CHECK(get16(w + 8) == 40 && nurse[21] == 100 && w[5] == 0);
            if (k == 3) CHECK(get16(w + 8) == 0 && w[5] == 2 && nurse[21] == 3);
        }
}

/* The beds fill: a ward with no beds wants 8, the operator installs a two-bed item from the store, the install
 * recounts at once (0x2B003: 0x2A873, 0x2B210, 0x2A873), and the next minute asks again, until 10 beds; then +4
 * stays 0. The Krankenzimmer's fittings need the store clerk, the neuro ward's do not. */
static void test_ward_beds_fill(void) {
    for (int cat = 6; cat <= 16; cat += 10) {
        uint8_t *ward = ward_world(cat), *st = store_world(15);
        g_image[0x31F2 + 60 + 0x29] = 10;
        g_image[0xEE0 + 18 * 7 + 6] = 8, g_image[0xEE0 + 18 * 7 + 10] = 2; /* furniture 7: kind 8, 2 beds */
        ward[0xC0] = 1, ward[10] = 0, put16(ward + 0x14, 0), ward[12] = 0;
        st[7] = 20, put16(st + 4, 41);
        for (int i = 0; i < 5; i++) put16(st + 10 + 2 * (2 * i + 1), 7), st[0x1EA + 2 * i + 1] = (uint8_t)(70 + i); /* gaps between */
        st[8] = 5;
        g_image[0xEE0 + 18 * 2 + 4] = 4;
        g_world.block[BLK_ROOM_COUNTS][3 * 100 + 13] = 1;
        uint8_t *kitchen = sim_room_record(&g_sim, 3, 13, 0);
        put16(kitchen + 0x40, 2); /* rating 4, not yet counted */
        int rounds = 0;
        while (rounds < 400 && get16(ward + 10) < 10) {
            sim_rooms_run(&g_sim, cat == 6 ? 0x30965 : 0x536D1);
            sim_rooms_run(&g_sim, cat == 6 ? 0x2995B : 0x2A01F);
            rounds++;
            if (st[8] == 4 && get16(ward + 10) == 2) /* first install: taken from slot 1, then two tidy-ups */
                CHECK(get16(st + 10 + 2) == 7 && get16(st + 10 + 4) == 0 && get16(st + 10 + 6) == 7 && kitchen[0] == 4);
        }
        CHECK(get16(ward + 10) == 10 && st[8] == 0 && rounds < 400);
        CHECK(get16(st + 10) == 0); /* the store list was tidied after the takes */
        CHECK(kitchen[0] == 4); /* the install recounted every category, the kitchen of another hospital too */
        sim_rooms_run(&g_sim, cat == 6 ? 0x30965 : 0x536D1);
        CHECK(ward[4] == 0);
    }
}

/* A treatment room (5), dental ward (11) or neurology (15) of hospital h with nurse 7, doctor 8 and patient 5. */
static uint8_t *doctor_room(int h, int cat) {
    patient_world();
    g_world.block[BLK_ROOM_COUNTS][h * 100 + cat] = 1;
    uint8_t *tr = sim_room_record(&g_sim, h, cat, 0), *p = patient(5);
    put16(tr + 8, 7), put16(tr + 10, 8), put16(tr + 12, 5);
    p[3] = (uint8_t)h, p[4] = (uint8_t)cat;
    uint8_t *ill8 = g_image + 0x954 + 8 * 20; /* illness 8: as 7, best treatment 31 */
    ill8[4] = 1, ill8[11] = 31;
    return tr;
}
static int doctor_counter(void) { return get16(g_world.block[BLK_STAFF] + 8 * 32 + 0x10); }

/* 0x3103D: every diagnosis that is the real illness gives the room's doctor 3 on +0x10, in all three rooms; no
 * ailment (diagnosis 0) gives nothing. The player's diagnosis: a skilled doctor is right (+3), an unskilled one
 * guesses one of the illnesses 8-40 for illness 7 (no bonus): their rows hold ailment 3 at all 132 places, so the
 * sixteen candidates of the wrong guess are found (docs/sim-rooms.md, "The wrong guess of the diagnoses"). */
static void test_diagnosis_bonus(void) {
    static const int CAT[3] = {5, 11, 15};
    for (int c = 0; c < 3; c++)
        for (int kase = 0; kase < 3; kase++) { /* no ailment, ailment 3 with illness 7, ailment 3 without an illness */
            uint8_t *tr = doctor_room(15, CAT[c]), *p = patient(5);
            int ailment = kase == 1;
            p[41] = (uint8_t)(kase ? 3 : 0);
            if (kase == 2) p[56] = 0;
            put16(tr + 14, 1), put16(tr + 16, 0), put16(tr + 18, 0); /* the tick's countdown ends: it diagnoses */
            sim_rooms_run(&g_sim, CAT[c] == 5 ? 0x6AD3C : CAT[c] == 11 ? 0x6B445 : 0x6BAE4);
            CHECK(p[42] == (ailment ? 7 : 0));
            int credit = doctor_counter() - (ailment ? 3 : 0); /* the tick's staff credit 0x30E7C gives 0 or 1 */
            CHECK(credit == 0 || credit == 1);
            int before = doctor_counter(); /* the operator then only chooses: no second +3 (SIM-37) */
            sim_rooms_after_operator(&g_sim, 15, CAT[c], 0);
            CHECK(doctor_counter() - before <= 1); /* at most a finish's or bring's own +1, never the +3 */
            CHECK(doctor_counter() - before != 3);
        }
    static uint8_t ailments[71 * 4];
    for (int skilled = 0; skilled <= 1; skilled++) {
        uint8_t *tr = doctor_room(0, 5), *p = patient(5), *doc = g_world.block[BLK_STAFF] + 8 * 32;
        memset(ailments, 0, sizeof ailments);
        memset(ailments + 8 * 4, 3, 33 * 4); /* the rows of illnesses 8-40 hold ailment 3 */
        for (int id = 9; id <= 40; id++) memcpy(g_image + 0x954 + id * 20, g_image + 0x954 + 8 * 20, 20); /* as 8 */
        sim_set_ailments(&g_sim, ailments, sizeof ailments);
        doc[2] = skilled ? 255 : 0;
        put16(tr + 14, 1), put16(tr + 16, 0), put16(tr + 18, 0);
        sim_rooms_run(&g_sim, 0x6AD3C);
        CHECK(skilled ? p[42] == 7 : p[42] >= 8 && p[42] <= 40);
        int credit = doctor_counter() - (skilled ? 3 : 0); /* the tick's staff credit 0x30E7C gives 0 or 1 */
        CHECK(credit == 0 || credit == 1);
        sim_set_ailments(&g_sim, NULL, 0);
    }
}

/* 0x31277: a treatment that finishes with the right diagnosis and the illness's best treatment (byte 11 = field 43)
 * gives the doctor 1; another treatment, a wrong diagnosis treated with its own best treatment, and no illness at all
 * (diagnosis 0, treatment 0 = illness 0's byte 11) give nothing. All three rooms. */
static void test_treatment_bonus(void) {
    static const int CAT[3] = {5, 11, 15};
    static const uint8_t CASE[4][3] = {{7, 7, 30}, {7, 7, 31}, {8, 7, 31}, {0, 0, 0}}; /* field 42, 56, 43 */
    for (int c = 0; c < 3; c++)
        for (int k = 0; k < 4; k++) {
            uint8_t *tr = doctor_room(3, CAT[c]), *p = patient(5);
            p[42] = CASE[k][0], p[56] = CASE[k][1], p[43] = CASE[k][2], p[45] = 0;
            put16(tr + 14, 0), put16(tr + 16, 30), put16(tr + 18, 1);
            sim_rooms_run(&g_sim, CAT[c] == 5 ? 0x6AD3C : CAT[c] == 11 ? 0x6B445 : 0x6BAE4);
            CHECK(get16(tr + 18) == 0 && p[79] == 0xFF); /* finished */
            CHECK(doctor_counter() == (k == 0 ? 1 : 0));
        }
}

/* Waiting room 0 of hospital h with patients 20 and 21 in places 0 and 1 (waits 200 and 0); illnesses 20, 21, 22 of
 * types 0, 1, 2. ill / wish give each patient's field 56 / 57 (-1: the place stays empty). */
static uint8_t *take_world(int h, int cat, const int ill[2], const int wish[2]) {
    patient_world();
    uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + h * 100;
    c[3] = 1, c[cat] = 1;
    for (int t = 0; t < 3; t++) g_image[0x954 + (20 + t) * 20 + 8] = (uint8_t)t;
    uint8_t *tr = sim_room_record(&g_sim, h, cat, 0), *wr = sim_room_record(&g_sim, h, 3, 0);
    put16(tr + 8, 7), put16(tr + 10, 8);
    wr[10] = 10, wr[12] = 0;
    for (int i = 0; i < 2; i++) {
        if (ill[i] < 0) continue;
        uint8_t *p = patient(20 + i);
        put16(wr + 0x14 + 4 * i, 20 + i), put16(wr + 0x16 + 4 * i, i ? 0 : 200), wr[12]++;
        p[3] = (uint8_t)h, p[4] = 3, p[56] = (uint8_t)ill[i], p[57] = (uint8_t)wish[i];
    }
    return tr;
}

/* 0x2E252 / 0x34A8F / the neurology's bring in AI mode: only the room's own type (byte 8 of field 56's illness, 0
 * without one), the four passes limited to it, and no fallback. A wish patient with a dental illness is counted by
 * 0x1B496 but is type 1: the treatment room passes over it to a later type-0 case, and takes nobody when it waits
 * alone; a wish without an illness is type 0. */
static void test_take_types(void) {
    static const struct { int cat, ill[2], wish[2], taken; } CASES[] = {
        {5, {21, 20}, {1, 0}, 21}, {5, {21, -1}, {1, 0}, 0}, {5, {0, -1}, {1, 0}, 20}, {5, {22, 20}, {0, 0}, 21},
        {11, {20, 21}, {0, 0}, 21}, {15, {21, 22}, {0, 0}, 21}, {15, {20, 22}, {0, 0}, 21},
    };
    for (unsigned n = 0; n < sizeof CASES / sizeof CASES[0]; n++) {
        uint8_t *tr = take_world(15, CASES[n].cat, CASES[n].ill, CASES[n].wish);
        sim_rooms_after_operator(&g_sim, 15, CASES[n].cat, 0);
        if (get16(tr + 12) != CASES[n].taken) fprintf(stderr, "  take case %u: %d, want %d\n", n, get16(tr + 12), CASES[n].taken);
        CHECK(get16(tr + 12) == CASES[n].taken);
    }
}

/* The player's take (0x2E3F2 in player mode): the room's own type first, then anyone; the dental ward (0x34AF0) refuses
 * a patient not of type 1 when its NURSE's (+8, staff 7) byte 11 is set, whatever the dentist's (+10, staff 8) byte 11,
 * and in any hospital but 0 (nothing taken, the patient keeps the seat). */
static void test_player_take(void) {
    static const struct { int h, cat, ill[2], nurse11, dentist11, taken; } CASES[] = {
        {0, 5, {21, 20}, 0, 0, 21}, {0, 5, {21, -1}, 0, 0, 20}, {0, 11, {20, 21}, 0, 0, 21}, {0, 11, {20, -1}, 0, 0, 20},
        {0, 11, {20, -1}, 1, 0, 0}, {0, 11, {20, -1}, 0, 1, 20}, {0, 11, {20, -1}, 1, 1, 0}, {0, 11, {21, -1}, 1, 1, 20},
        {15, 11, {20, -1}, 0, 0, 0}, {15, 11, {21, -1}, 0, 0, 20}, {0, 15, {20, -1}, 0, 0, 20}, {0, 15, {21, 22}, 0, 0, 21},
    };
    static const int NO_WISH[2] = {0, 0};
    for (unsigned n = 0; n < sizeof CASES / sizeof CASES[0]; n++) {
        int h = CASES[n].h;
        uint8_t *tr = take_world(h, CASES[n].cat, CASES[n].ill, NO_WISH);
        g_world.block[BLK_STAFF][7 * 32 + 11] = (uint8_t)CASES[n].nurse11;
        g_world.block[BLK_STAFF][8 * 32 + 11] = (uint8_t)CASES[n].dentist11;
        int from = -1, got = CASES[n].cat == 5 ? sim_player_call(&g_sim, h, 0, &from)
                                               : sim_player_call_in(&g_sim, h, CASES[n].cat, 0, &from);
        if (got != CASES[n].taken) fprintf(stderr, "  player take case %u: %d, want %d\n", n, got, CASES[n].taken);
        CHECK(got == CASES[n].taken && get16(tr + 12) == CASES[n].taken);
        if (!got) CHECK(get16(sim_room_record(&g_sim, h, 3, 0) + 0x14) == 20 && patient(20)[4] == 3);
    }
    g_world.block[BLK_STAFF][7 * 32 + 11] = g_world.block[BLK_STAFF][8 * 32 + 11] = 0;
}

/* 0x2D11A step 4 in the AI's treatment room tests the diagnosed illness (field 42): byte 15 = 3 or type 2 sends the
 * patient back to a waiting room even with a wish's treatment; a type-0 illness with the same wish is treated in the
 * room, and so is a type-2 illness without an ailment (diagnosis 0: the test reads 42, not 56). Treatment 30 (the
 * wish's) is done in the room (bytes 4 and 6: 0 and 1). */
static void test_step4_return(void) {
    static const struct { int b15, type, ailment, back; } CASES[] = {{0, 0, 3, 0}, {3, 0, 3, 1}, {0, 2, 3, 1}, {0, 2, 0, 0}};
    for (int n = 0; n < 4; n++) {
        uint8_t *tr = doctor_room(15, 5), *p = patient(5), *ill = g_image + 0x954 + 23 * 20;
        g_world.block[BLK_ROOM_COUNTS][15 * 100 + 3] = 1;
        sim_room_record(&g_sim, 15, 3, 0)[10] = 10;
        g_image[0x2066 + 4] = 30; /* wish 1: treatment 30 */
        g_image[0x270C + 30 * 38 + 6] = 1;
        ill[15] = (uint8_t)CASES[n].b15, ill[8] = (uint8_t)CASES[n].type;
        p[41] = (uint8_t)CASES[n].ailment, p[56] = 23, p[57] = 1;
        p[42] = (uint8_t)(CASES[n].ailment ? 23 : 0); /* the tick's diagnosis (SIM-36) */
        put16(tr + 14, 0), put16(tr + 16, 0), put16(tr + 18, 0);
        sim_rooms_after_operator(&g_sim, 15, 5, 0);
        if (CASES[n].back) CHECK(get16(tr + 12) == 0 && p[4] == 3 && p[42] == 0);
        else CHECK(p[42] == (CASES[n].ailment ? 23 : 0) && get16(tr + 12) == 5 && get16(tr + 16) == 30 && get16(tr + 18) == 999);
    }
}

/* 0x34E36: the dental diagnosis with a doctor of skill 0 (r127 is never below S). The ailment rows of illnesses 9-30
 * except 22 hold ailment 3 at every place (84 of 280, so the wrong guess's sixteen candidates are found), so a wrong
 * guess is one of them, all set up as illness 9, for the real illness 22 (both dental, type 1; illness 9 has another
 * byte 4). The AI
 * gets a second chance by r1 (about half right), the player none (all wrong, kept: no byte-4 test); a guess of
 * another type (illness 9 of type 0) is corrected for both. The +3 goes only to a right diagnosis. */
static void test_dental_diagnosis(void) {
    static uint8_t ailments[31 * 4];
    memset(ailments, 0, sizeof ailments);
    memset(ailments + 9 * 4, 3, 22 * 4);
    memset(ailments + 22 * 4, 0, 4);
    for (int kase = 0; kase < 4; kase++) { /* 0 / 1: AI same / other type, 2 / 3: player same / other type */
        int ai = kase < 2, h = ai ? 15 : 0, other = kase & 1;
        uint8_t *tr = doctor_room(h, 11), *p = patient(5), *ill9 = g_image + 0x954 + 9 * 20, *ill22 = g_image + 0x954 + 22 * 20;
        sim_set_ailments(&g_sim, ailments, sizeof ailments);
        ill9[8] = other ? 0 : 1, ill9[4] = 1, ill9[15] = 3, ill22[8] = 1, ill22[15] = 3;
        memset(ill9 + 11, 30, 4), memset(ill22 + 11, 30, 4); /* every choice is treatment 30, in the room */
        for (int id = 10; id <= 30; id++)
            if (id != 22) memcpy(g_image + 0x954 + id * 20, ill9, 20);
        g_image[0x270C + 30 * 38 + 6] = 1;
        int wrong = 0, bonus = 0;
        for (int t = 0; t < 200; t++) {
            p[3] = (uint8_t)h, p[4] = 11, p[41] = 3, p[42] = 0, p[56] = 22, p[57] = 0, p[44] = 100;
            put16(tr + 12, 5), put16(tr + 14, 1), put16(tr + 16, 0), put16(tr + 18, 0);
            put16(g_world.block[BLK_STAFF] + 8 * 32 + 0x10, 0);
            sim_rooms_run(&g_sim, 0x6B445); /* the tick diagnoses in every hospital (SIM-36) */
            CHECK(p[42] >= 9 && p[42] <= 30);
            wrong += p[42] != 22;
            int want = p[42] == 22 ? 3 : 0; /* the tick also runs the staff credit 0x30E7C (+1 or not) */
            bonus += doctor_counter() == want || doctor_counter() == want + 1;
        }
        if (kase == 0) fprintf(stderr, "  AI dental: %d of 200 wrong\n", wrong);
        CHECK(bonus == 200);
        if (other) CHECK(wrong == 0);
        else if (ai) CHECK(wrong >= 60 && wrong <= 140);
        else CHECK(wrong == 200);
        sim_set_ailments(&g_sim, NULL, 0);
    }
}

/* Room descriptions for the kitchen (13, as room_descriptions) and a waiting room (3); furniture 2 (ratings 4/1),
 * 3 (rating 5), 8 (kind 5, 5 seats), 9 (kind 8, 5 beds); equipment 5, 6. */
static void wear_world(void) {
    patient_world();
    room_descriptions();
    static const uint16_t D[7] = {0x40, 5, 0x60, 10, 0x90, 0xA0, 0xC0};
    for (int i = 0; i < 7; i++) put16(g_image + 0x4758 + 14 * 3 + 2 * i, D[i]);
    uint8_t *F = g_image + 0xEE0;
    F[18 * 2 + 4] = 4, F[18 * 2 + 5] = 1, F[18 * 3 + 4] = 5;
    F[18 * 8 + 6] = 5, F[18 * 8 + 10] = 5, F[18 * 9 + 6] = 8, F[18 * 9 + 10] = 5;
    g_image[0x31F2 + 60 + 0x29] = 10;
    world_set_tail(&g_world, TAIL_HOUR, 12);
}

/* 0x2B5B3 inside the recompute with wear: with t&3 = 2 (1 in 4) ids above 76 / 154 are cleared, the others lose 1 with
 * t&7 = 0 (1 in 32 calls) and go at a condition below 1; 76 and 154 are kept; at hours 6 and 18 and without the
 * switch nothing happens. */
static void test_room_wear(void) {
    for (int mode = 0; mode < 4; mode++) { /* 0: hour 12 with wear, 1: hour 6, 2: hour 18, 3: no wear */
        wear_world();
        world_set_tail(&g_world, TAIL_HOUR, mode == 1 ? 6 : mode == 2 ? 18 : 12);
        g_world.block[BLK_ROOM_COUNTS][15 * 100 + 13] = 1;
        uint8_t *r = sim_room_record(&g_sim, 15, 13, 0);
        int cleared = 0, dropped = 0, worn = 0, gone = 0, kept = 1, n = 4000;
        for (int i = 0; i < n; i++) {
            put16(r + 0x40, 80), put16(r + 0x42, 2), r[0x91] = 100, put16(r + 0x44, 3), r[0x92] = 1, put16(r + 0x46, 76),
                r[0x93] = 100;
            put16(r + 0x60, 160), put16(r + 0x62, 6), r[0xA1] = 0, put16(r + 0x64, 154), r[0xA2] = 100;
            sim_recompute_wear(&g_sim, 13, mode != 3);
            cleared += get16(r + 0x40) == 0 && get16(r + 0x60) == 0;
            dropped += r[0x91] == 99;
            worn += get16(r + 0x44) == 0;
            gone += get16(r + 0x62) == 0;
            kept &= get16(r + 0x46) == 76 && get16(r + 0x64) == 154 && get16(r + 0x42) == 2;
        }
        CHECK(kept);
        if (mode) {
            CHECK(cleared == 0 && dropped == 0 && worn == 0 && gone == 0);
            continue;
        }
        CHECK(cleared > n / 4 - 120 && cleared < n / 4 + 120 && gone == cleared);
        CHECK(dropped > n / 32 - 50 && dropped < n / 32 + 50 && worn > n / 32 - 50 && worn < n / 32 + 50);
    }
}

/* The wear gates: a waiting room only with more than 2 free seats, a bed ward with more than 1 free bed; the kitchen
 * always. */
static void test_room_wear_gates(void) {
    for (int k = 0; k < 4; k++) {
        wear_world();
        int cat = k < 2 ? 3 : 6;
        g_world.block[BLK_ROOM_COUNTS][15 * 100 + cat] = 1;
        uint8_t *r = sim_room_record(&g_sim, 15, cat, 0);
        r[0xC0] = 1;
        int cleared = 0;
        for (int i = 0; i < 400; i++) {
            int item = cat == 3 ? 8 : 9;
            put16(r + 0x40, item), put16(r + 0x42, item), r[0x90] = r[0x91] = 100, put16(r + 0x44, 80);
            r[12] = (uint8_t)(cat == 3 ? (k ? 7 : 8) : (k == 3 ? 8 : 9)); /* free 3 / 2 seats, 2 / 1 beds */
            sim_recompute_wear(&g_sim, cat, 1);
            cleared += get16(r + 0x44) == 0;
        }
        CHECK(r[10] == 10);
        if (k == 1 || k == 3) CHECK(cleared > 60 && cleared < 140);
        else CHECK(cleared == 0);
    }
}

/* Tick 64, 1 in 8: 0x2B207 (recompute with wear), 0x2BFB7 (first gap of every list), 0x2B210 (recompute without wear).
 * In a kitchen of hospital 3 a worn-out item (condition 0, rating 5) goes in some minute, and the ratings are recounted
 * in the same tick; the gap in front of item 2 closes. */
static void test_tick64(void) {
    wear_world();
    world_set_tail(&g_world, TAIL_HOUR, 9);
    g_world.block[BLK_ROOM_COUNTS][3 * 100 + 13] = 1;
    g_image[0x31F2 + 0x29] = 1; /* type 0 base rating 1 */
    uint8_t *r = sim_room_record(&g_sim, 3, 13, 0);
    put16(r + 0x40, 3), r[0x90] = 0, put16(r + 0x44, 2), r[0x92] = 50; /* slot 1 empty */
    sim_recompute(&g_sim, 13);
    CHECK(r[0] == 1 + 5 + 4 && r[1] == 1);
    int ticks = 0, stale = 0, closed = 0;
    /* until the gap has closed and item 2 has moved up into slot 0 (when the item goes in the very tick whose gap pass
     * already moved item 2 to slot 1, the next pass moves it on; the minute depends on the random stream) */
    while (ticks < 400 * SIM_TICKS_PER_MINUTE && (get16(r + 0x40) != 2 || !closed)) {
        sim_tick(&g_sim);
        ticks++;
        if (!closed && get16(r + 0x42) == 2) closed = ticks;
        if (get16(r + 0x40) != 3 && r[0] != 1 + 4) stale = 1;
    }
    CHECK(closed && !stale && r[0] == 1 + 4 && get16(r + 0x40) == 2 && get16(r + 0x42) == 0);
}

/* The store-screen test at tick 64 (docs/sim-rooms2.md, "The store-screen test"): one r7 draw a minute, the three calls
 * when it is 3 (each tidy-up moves an equipment item behind nine gaps one slot up); on the inventory, screen 65, no draw
 * at all and nothing moves. Night hour, so no wear. */
static void test_tick64_screen(void) {
    for (int inventory = 0; inventory <= 1; inventory++) {
        wear_world();
        world_set_tail(&g_world, TAIL_HOUR, 23);
        g_world.block[BLK_ROOM_COUNTS][3 * 100 + 13] = 1;
        uint8_t *r = sim_room_record(&g_sim, 3, 13, 0);
        put16(r + 0x60 + 2 * 9, 1), r[0xA0 + 9] = 100;
        g_sim.screen = inventory ? 0x41 : 1;
        FILE *log = tmpfile();
        CHECK(log != NULL);
        if (!log) return;
        g_sim.rng_log = log;
        for (int t = 0; t < 30 * SIM_TICKS_PER_MINUTE; t++) sim_tick(&g_sim);
        g_sim.rng_log = NULL;
        g_sim.screen = 0;
        rewind(log);
        char line[128];
        int draws = 0, threes = 0, slot = -1;
        while (fgets(line, sizeof line, log)) {
            unsigned site, v;
            if (sscanf(line, "%X b7 %u", &site, &v) == 2 && site == 0x1C565) draws++, threes += v == 3;
        }
        fclose(log);
        for (int i = 0; i < 10; i++)
            if (get16(r + 0x60 + 2 * i) == 1) slot = i;
        if (inventory) {
            CHECK(draws == 0 && slot == 9);
            continue;
        }
        CHECK(draws == 30 && threes > 0 && threes < 9 && slot == 9 - threes);
    }
}

/* ---- the treatment rooms' equipment check (docs/sim-drift.md, sections 3 and 5) ------------------------------- */

static const int ROOM_CATS[3] = {5, 11, 15};
static const uint32_t ROOM_TICK[3] = {0x6AD3C, 0x6B445, 0x6BAE4}, ROOM_OPERATOR[3] = {0x2CB75, 0x34535, 0x35430};

/* doctor_room with the treatment-room layout (furniture +0x1C, equipment +0x78, 4 slots each, conditions +0x46 /
 * +0x5A), a skilled doctor, and illness 7 fitted to the room (type 0 / dental: type 1 with byte 15 = 3 / type 2).
 * Treatments 30-33 are done in the room and need capability 0x20 (byte 21) and kind 5 (byte 11); item 2 has the
 * capability, item 3 the kind, both with wear 1 (room_descriptions). */
static uint8_t *equipped_room(int h, int cat) {
    uint8_t *tr = doctor_room(h, cat);
    room_descriptions();
    static const uint16_t D[7] = {0x1C, 4, 0x78, 4, 0x46, 0x5A, 0xA0};
    for (int i = 0; i < 7; i++) put16(g_image + 0x4758 + 14 * cat + 2 * i, D[i]);
    uint8_t *ill = g_image + 0x954 + 7 * 20, *doc = g_world.block[BLK_STAFF] + 8 * 32;
    ill[8] = (uint8_t)(cat == 5 ? 0 : cat == 11 ? 1 : 2), ill[15] = (uint8_t)(cat == 11 ? 3 : 0);
    doc[2] = doc[12] = doc[13] = 255;
    for (int t = 30; t <= 33; t++) {
        uint8_t *T = g_image + 0x270C + t * 38;
        T[4] = 0, T[6] = 1, T[16] = 1, T[21] = 0x20, T[11] = 5;
    }
    return tr;
}
static void room_item(uint8_t *tr, int slot, int id, int cond) { put16(tr + 0x78 + 2 * slot, id), tr[0x5A + slot] = (uint8_t)cond; }

/* SIM-81, docs/sim-ai.md "The blood-expiry boundary is inclusive": the new age 800 expires, including the
 * player's and dealer's offers. Reserved offers age but stay; negative ages and inactive sellers do not age;
 * 32767 wraps negative and stays. The unsigned quantity adds modulo 2^32 to loss, never back to bank stock. */
static void test_sim81_blood_expiry(void) {
    static const int SELLERS[5] = {0, 15, 16, 128, 255};
    static const int AGES[8] = {-32768, -1, 0, 798, 799, 800, 801, 32767};
    for (int edition = EDITION_DISK; edition <= EDITION_ENGLISH_CD; edition++)
        for (int seller = 0; seller < 5; seller++)
            for (int age = 0; age < 8; age++)
                for (int reserved = 0; reserved <= 1; reserved++) {
                    fresh();
                    g_prog.edition = (Edition)edition;
                    g_sim.screen = 65;
                    memset(g_world.block[BLK_MARKET], 0xFF, 100 * 10);
                    memset(g_world.block[BLK_FURNITURE_MARKET], 0xFF, 300 * 8);
                    memset(g_world.block[BLK_EQUIPMENT_MARKET], 0xFF, 300 * 8);
                    uint8_t *m = g_world.block[BLK_MARKET], expected_offer[10];
                    memset(m, 0, 10);
                    m[0] = (uint8_t)SELLERS[seller], m[8] = (uint8_t)reserved, m[9] = 0x5A;
                    put16(m + 2, 65535), put16(m + 4, 123), put16(m + 6, AGES[age]);
                    for (int h = 0; h < HOSPITAL_COUNT; h++) put32(world_hospital(&g_world, h) + 0x66, 0xFFFFFFF0u);
                    for (int h = 0; h < 16; h++) put32(sim_room_record(&g_sim, h, 17, 0) + 0x0C, 1234);
                    uint8_t expected_hospitals[HOSPITAL_COUNT * HOSPITAL_SIZE];
                    memcpy(expected_hospitals, g_world.block[BLK_HOSPITALS], sizeof expected_hospitals);
                    memcpy(expected_offer, m, sizeof expected_offer);
                    int active = SELLERS[seller] < 128, start = AGES[age];
                    int expires = active && !reserved && start >= 799 && start < 32767;
                    if (active && start >= 0) put16(expected_offer + 6, start == 32767 ? -32768 : start + 1);
                    if (expires) {
                        expected_offer[0] = 255;
                        put32(expected_hospitals + SELLERS[seller] * HOSPITAL_SIZE + 0x66, 65519);
                    }
                    uint32_t draw = 0;
                    SimScript sc = {&draw, 1, 0, 0, NULL};
                    g_sim.rng_script = &sc;
                    sim_call(&g_sim, 0x3DAEB);
                    g_sim.rng_script = NULL;
                    int ok = !memcmp(m, expected_offer, sizeof expected_offer) &&
                             !memcmp(g_world.block[BLK_HOSPITALS], expected_hospitals, sizeof expected_hospitals) &&
                             sc.used == 0 && sc.extra == 0;
                    for (int h = 0; h < 16; h++) ok &= get32(sim_room_record(&g_sim, h, 17, 0) + 0x0C) == 1234;
                    if (!ok) fprintf(stderr, "  SIM-81 edition %d seller %d age %d reserved %d\n",
                                     edition, SELLERS[seller], start, reserved);
                    CHECK(ok);
                    g_prog.edition = EDITION_DISK;
                }
}

/* SIM-103: runtime player-0 office view, independent saved screens and human flags. Exercise the last slots,
 * all bytes of the affected tables/hospitals and both player blocks; the organ continuation keeps its draws. */
static void office_offers_case(int seller, int age, int reserved, int kind, int screen, int human) {
    fresh();
    g_sim.screen = screen;
    for (int h = 0; h < HOSPITAL_COUNT; h++) world_hospital(&g_world, h)[0x14] = (uint8_t)human;
    for (int p = 0; p < 2; p++) {
        memset(g_world.block[BLK_PLAYER0 + p], p ? 0x59 : 0xA6, 500);
        put16(g_world.block[BLK_PLAYER0 + p] + 4, p ? (human ? 257 : 1) : (screen == 1 ? 0 : 1));
    }
    memset(g_world.block[BLK_MARKET], 0xFF, 1000);
    memset(g_world.block[BLK_EQUIPMENT_MARKET], 0xFF, 2400);
    memset(g_world.block[BLK_FURNITURE_MARKET], 0xFF, 2400);
    uint8_t *m = g_world.block[BLK_MARKET] + 99 * 10;
    uint8_t *eq = g_world.block[BLK_EQUIPMENT_MARKET] + 299 * 8;
    uint8_t *fu = g_world.block[BLK_FURNITURE_MARKET] + 299 * 8;
    memset(m, 0x5A, 10), memset(eq, 0x6B, 8), memset(fu, 0x7C, 8);
    m[0] = eq[0] = fu[0] = (uint8_t)seller;
    m[1] = (uint8_t)(kind != 0), m[8] = eq[5] = fu[5] = (uint8_t)reserved;
    put16(m + 2, kind == 3 ? 0x1D01 : kind ? 0x1E01 : 17); /* organ quality 29/30, or blood quantity 17 */
    put16(m + 6, age), put16(eq + 6, age), put16(fu + 6, age);
    uint8_t markets[1000], equipment[2400], furniture[2400], hospitals[HOSPITAL_COUNT * HOSPITAL_SIZE], players[2][500];
    memcpy(markets, g_world.block[BLK_MARKET], sizeof markets);
    memcpy(equipment, g_world.block[BLK_EQUIPMENT_MARKET], sizeof equipment);
    memcpy(furniture, g_world.block[BLK_FURNITURE_MARKET], sizeof furniture);
    memcpy(hospitals, g_world.block[BLK_HOSPITALS], sizeof hospitals);
    for (int p = 0; p < 2; p++) memcpy(players[p], g_world.block[BLK_PLAYER0 + p], 500);
    uint8_t *em = markets + 990, *ee = equipment + 2392, *ef = furniture + 2392;
    /* Every other market slot is inactive but reserved with negative age: office cleanup precedes the skip. */
    if (screen == 1) for (int i = 0; i < 99; i++) markets[10 * i + 8] = 0, put16(markets + 10 * i + 6, 0);
    int cleaned = screen == 1 && reserved, active = seller < 128;
    int removed_draft = cleaned && age < 0;
    if (cleaned) em[8] = 0;
    if (removed_draft) em[0] = 255, put16(em + 6, 0);
    if (active && !removed_draft) {
        if (age >= 0) put16(em + 6, age + 1);
        if (!kind && age >= 799 && age != 32767 && (!reserved || cleaned)) {
            em[0] = 255;
            put32(hospitals + seller * HOSPITAL_SIZE + 0x66, 17);
        }
        if (kind == 1 || kind == 3) { /* quality reaches/stays 29 after one-point/zero loss */
            em[3] = reserved && !cleaned ? 1 : 29;
            if (!reserved || cleaned) em[0] = 255, put16(hospitals + seller * HOSPITAL_SIZE + 0x82, 1);
        }
    }
    if (active) {
        int eq_age = (int16_t)(age + 1), fu_age = (int16_t)(age + (reserved ? 1 : 2));
        put16(ee + 6, eq_age), put16(ef + 6, fu_age);
        if (cleaned) ee[5] = 0;
        if ((!reserved || cleaned) && eq_age > 39 && seller != 0)
            ee[0] = 255, put16(hospitals + seller * HOSPITAL_SIZE + 0xB2, 1);
        if (!reserved && fu_age > 39 && seller != 0)
            ef[0] = 255, put16(hospitals + seller * HOSPITAL_SIZE + 0xB8, 1);
    }
    static const uint32_t DROP[] = {0, 1, 3, 1, 1}, ZERO[] = {0, 0, 0, 0, 0}, SKIP[] = {3};
    size_t draws = kind && active && !removed_draft ? (kind == 2 ? 1 : 5) : 0;
    SimScript script = {.v = kind == 1 ? DROP : kind == 3 ? ZERO : SKIP, .n = draws};
    g_sim.rng_script = &script;
    CHECK(sim_call(&g_sim, 0x3DAEB));
    g_sim.rng_script = NULL;
    int ok = !memcmp(markets, g_world.block[BLK_MARKET], sizeof markets) &&
             !memcmp(equipment, g_world.block[BLK_EQUIPMENT_MARKET], sizeof equipment) &&
             !memcmp(furniture, g_world.block[BLK_FURNITURE_MARKET], sizeof furniture) &&
             !memcmp(hospitals, g_world.block[BLK_HOSPITALS], sizeof hospitals) &&
             !memcmp(players[0], g_world.block[BLK_PLAYER0], 500) && !memcmp(players[1], g_world.block[BLK_PLAYER1], 500) &&
             script.used == draws && script.extra == 0;
    if (!ok) fprintf(stderr, "  SIM-103 seller %d age %d reserved %d kind %d screen %d human %d\n",
                     seller, age, reserved, kind, screen, human);
    CHECK(ok);
}
static void test_office_offers(void) {
    static const int SELLER[] = {0, 15, 16, 128, 255}, AGE[] = {-32768, -1, 0, 38, 39, 798, 799, 32767};
    static const int RESERVED[] = {0, 1, 255}, SCREEN[] = {0, 1, 257, 65535};
    for (int s = 0; s < 5; s++) for (int a = 0; a < 8; a++) for (int r = 0; r < 3; r++)
        for (int k = 0; k < 4; k++) for (int view = 0; view < 4; view++) for (int human = 0; human < 2; human++)
            office_offers_case(SELLER[s], AGE[a], RESERVED[r], k, SCREEN[view], human ? 255 : 0);
}

/* SIM-106: quality is stored modulo 256 before the signed-byte expiry test. Compare complete saved blocks,
 * including the freed offer's retained quality, counters, other markets and the two disagreeing saved views. */
static void organ_offer_quality_case(int edition, int quality, int draw, int reserved, int screen,
                                     int seller, int age, int counter) {
    fresh();
    g_prog.edition = (Edition)edition;
    g_sim.screen = screen;
    put16(g_world.block[BLK_PLAYER0] + 4, screen == 1 ? 0 : 1);
    put16(g_world.block[BLK_PLAYER1] + 4, 1);
    memset(g_world.block[BLK_MARKET], 0, 1000);
    for (int i = 0; i < 100; i++) g_world.block[BLK_MARKET][10 * i] = 255;
    memset(g_world.block[BLK_EQUIPMENT_MARKET], 0xFF, 2400);
    memset(g_world.block[BLK_FURNITURE_MARKET], 0xFF, 2400);
    uint8_t *m = g_world.block[BLK_MARKET] + 99 * 10;
    memset(m, 0x5A, 10);
    m[0] = (uint8_t)seller, m[1] = 1, m[3] = (uint8_t)quality, m[8] = (uint8_t)reserved;
    put16(m + 6, age);
    for (int h = 0; h < HOSPITAL_COUNT; h++) put16(world_hospital(&g_world, h) + 0x82, counter);
    World expected;
    world_alloc(&expected);
    for (int b = 0; b <= WORLD_BLOCKS; b++) memcpy(expected.block[b], g_world.block[b], g_world.size[b]);
    uint8_t *em = expected.block[BLK_MARKET] + 990;
    int released = screen == 1 && reserved;
    if (released) em[8] = 0;
    if (released && age < 0) em[0] = 255, put16(em + 6, 0);
    int active = seller < 128 && !(released && age < 0);
    if (active && age >= 0) put16(em + 6, age == 32767 ? -32768 : age + 1);
    if (active && draw != 2) {
        /* Independent expected unsigned ranges: 30..127 survive; 0..29 and 128..255 expire. */
        int value = (quality + 256 - (draw == 1)) % 256;
        if (value < 30 || value >= 128) {
            if (reserved && !released) value = 1;
            else {
                em[0] = 255;
                put16(world_hospital(&expected, seller) + 0x82, counter == 65535 ? 0 : counter + 1);
            }
        }
        em[3] = (uint8_t)value;
    }
    static const uint32_t RAW[3][5] = {
        {0x10000, 0x10000, 0x10003, 0x10000, 0x10000}, /* sum 3: zero loss */
        {0x10000, 0x10001, 0x10003, 0x10001, 0xFFFFFFFF}, /* sum 6: one loss */
        {0xFFFFFFFF, 0, 0, 0, 0}, /* rejected gate: no quality test, even for zero */
    };
    size_t draws = active ? (draw == 2 ? 1 : 5) : 0;
    SimScript script = {.v = RAW[draw], .n = 5};
    g_sim.rng_script = &script;
    uint32_t state = g_sim.rng_s;
    uint64_t auxiliary = g_sim.rng;
    CHECK(sim_call(&g_sim, 0x3DAEB));
    g_sim.rng_script = NULL;
    for (size_t i = 0; i < draws; i++) state += RAW[draw][i];
    int ok = script.used == draws && script.extra == 0 && g_sim.rng_s == state && g_sim.rng == auxiliary;
    for (int b = 0; b <= WORLD_BLOCKS; b++) ok &= !memcmp(expected.block[b], g_world.block[b], g_world.size[b]);
    if (!ok) fprintf(stderr, "  SIM-106 edition %d quality %d draw %d reserved %d screen %d seller %d age %d counter %d\n",
                     edition, quality, draw, reserved, screen, seller, age, counter);
    CHECK(ok);
    world_free(&expected);
    g_prog.edition = EDITION_DISK;
}
static void test_organ_offer_quality(void) {
    static const int SCREEN[] = {0, 1, 257}, RESERVED[] = {0, 1, 255};
    static const int SELLER[] = {0, 15, 16, 128, 255}, AGE[] = {-32768, -1, 0, 799, 32767};
    for (int edition = EDITION_DISK; edition <= EDITION_ENGLISH_CD; edition++) {
        /* Every quality byte, both loss amounts and the nonzero gate, with and without reservation. */
        for (int quality = 0; quality < 256; quality++) for (int draw = 0; draw < 3; draw++)
            for (int reserved = 0; reserved < 2; reserved++)
                organ_offer_quality_case(edition, quality, draw, reserved, 0, 15, 0, 65535);
        for (int view = 0; view < 3; view++) for (int reserved = 0; reserved < 3; reserved++)
            for (int seller = 0; seller < 5; seller++) for (int age = 0; age < 5; age++)
                organ_offer_quality_case(edition, 0, 1, RESERVED[reserved], SCREEN[view], SELLER[seller], AGE[age], 37);
    }
}

static jmp_buf office_offer_boundary;
static void office_before_organ_draw(const SimScript *script, const char *message) {
    (void)message;
    uint8_t *m = g_world.block[BLK_MARKET];
    CHECK(script->used == 0);
    CHECK(m[0] == 255 && get16(m + 6) == 0 && m[8] == 0); /* prior draft already discarded */
    CHECK(m[10] == 15 && get16(m + 16) == 1 && m[18] == 0); /* this organ unreserved and aged first */
    CHECK(g_world.block[BLK_EQUIPMENT_MARKET][5] == 255); /* equipment follows the organ pass */
    longjmp(office_offer_boundary, 1);
}
static void test_office_offers_order(void) {
    fresh();
    g_sim.screen = 1;
    memset(g_world.block[BLK_MARKET], 0xFF, 1000);
    uint8_t *m = g_world.block[BLK_MARKET];
    m[0] = 0, m[8] = 1;
    m[10] = 15, m[11] = 1, m[18] = 1, put16(m + 16, 0);
    g_world.block[BLK_EQUIPMENT_MARKET][5] = 255;
    SimScript script = {0};
    g_sim.rng_script = &script;
    g_sim.rng_failure = office_before_organ_draw;
    if (!setjmp(office_offer_boundary)) { sim_call(&g_sim, 0x3DAEB); CHECK(0); }
    g_sim.rng_script = NULL;
    g_sim.rng_failure = NULL;
}

/* SIM-80, docs/sim-rooms2.md "Equipment wear is a stored byte before the signed test": every condition byte,
 * wear 1/4/128/255 and both outcomes of catalog-zero wear, capability/kind, first/final slot, every edition.
 * The real player treatment decision checks then uses the item. A second match behind slot 0 must remain unused.
 * Check all list ids and conditions: first-slot removal shifts them, last-slot removal keeps the worn byte.
 * In particular 0 - 255 retains condition 1, while 255 - 0 removes and retains condition 255 in the last slot. */
static void test_sim80_equipment_wear(void) {
    static const int WEAR[6] = {1, 4, 128, 255, 0, 0};
    for (int edition = EDITION_DISK; edition <= EDITION_ENGLISH_CD; edition++)
        for (int kind = 0; kind <= 1; kind++)
            for (int last = 0; last <= 1; last++) {
                uint8_t *tr = equipped_room(15, 11), *p = patient(5), *t = g_image + 0x270C + 30 * 38;
                g_prog.edition = (Edition)edition;
                p[42] = 7, p[43] = 30;
                t[21] = kind ? 0 : 0x20, t[11] = kind ? 5 : 0;
                int id = kind ? 3 : 2, slot = last ? 3 : 0;
                uint8_t *item = g_image + 0x144A + 20 * id;
                for (int w = 0; w < 6; w++)
                    for (int condition = 0; condition < 256; condition++) {
                        item[16] = (uint8_t)WEAR[w];
                        put16(tr + 16, 0), put16(tr + 18, 0);
                        tr[0] = 77, tr[1] = 66; /* no rating recompute on removal */
                        int ids[4], cond[4];
                        for (int i = 0; i < 4; i++) {
                            ids[i] = !last && i == 1 ? id : 4 + i;
                            cond[i] = 90 + i;
                        }
                        ids[slot] = id, cond[slot] = condition;
                        for (int i = 0; i < 4; i++) room_item(tr, i, ids[i], cond[i]);
                        uint32_t draw = w == 5 ? 1 : 0;
                        SimScript sc = {&draw, 1, 0, 0, NULL};
                        g_sim.rng_script = &sc;
                        int n = -1, result = sim_player_after_clinic(&g_sim, 15, 11, 0, &n);
                        g_sim.rng_script = NULL;
                        /* Independent integer-range expectation, including negative differences wrapping positive. */
                        int difference = condition - (WEAR[w] ? WEAR[w] : w == 4);
                        int retained = (difference >= 1 && difference <= 127) || difference <= -129;
                        cond[slot] = difference < 0 ? difference + 256 : difference;
                        if (!retained) {
                            if (last) ids[3] = 0;
                            else {
                                for (int i = 0; i < 3; i++) ids[i] = ids[i + 1], cond[i] = cond[i + 1];
                                ids[3] = cond[3] = 0;
                            }
                        }
                        int ok = result == AFTER_IN_ROOM && get16(tr + 16) == 30 && get16(tr + 18) == 999 &&
                                 get16(tr + 0x44) == 0 && sc.used == (w >= 4) && sc.extra == 0 &&
                                 tr[0] == 77 && tr[1] == 66;
                        for (int i = 0; i < 4; i++)
                            ok &= get16(tr + 0x78 + 2 * i) == ids[i] && tr[0x5A + i] == cond[i];
                        if (!ok) fprintf(stderr, "  SIM-80 edition %d kind %d last %d wear case %d condition %d\n",
                                         edition, kind, last, w, condition);
                        CHECK(ok);
                    }
                g_prog.edition = EDITION_DISK;
            }
}

/* Presence checks and misses neither test condition nor draw wear. A later unmet requirement prevents ALL use,
 * even when the first item was found with a zero/high condition. Compare the complete equipment list unchanged. */
static void test_sim80_equipment_no_use(void) {
    static const int CONDITIONS[4] = {0, 127, 128, 255};
    for (int kind = 0; kind <= 1; kind++)
        for (int c = 0; c < 4; c++)
            for (int found = 0; found <= 1; found++) {
                uint8_t *tr = equipped_room(15, 11), *p = patient(5), *t = g_image + 0x270C + 30 * 38;
                int id = kind ? 3 : 2, requested = kind ? 5 : 0x20;
                p[42] = 7, p[43] = 30;
                t[21] = kind ? 0 : 0x20, t[11] = kind ? 5 : 0;
                t[kind ? 12 : 22] = 0x7E; /* a later missing item keeps the decision in check-only mode */
                g_image[0x144A + 20 * id + 16] = 0;
                room_item(tr, 0, found ? id : 7, CONDITIONS[c]);
                room_item(tr, 1, 0, 55), room_item(tr, 2, 7, 66); /* tidy would change this gap */
                uint8_t before_ids[8], before_cond[4];
                memcpy(before_ids, tr + 0x78, sizeof before_ids), memcpy(before_cond, tr + 0x5A, sizeof before_cond);
                uint32_t draw = 0;
                SimScript sc = {&draw, 1, 0, 0, NULL};
                g_sim.rng_script = &sc;
                int has = kind ? sim_has_equipment_kind(&g_sim, tr, 11, requested) :
                                 sim_has_equipment(&g_sim, tr, 11, requested);
                int n = -1, result = sim_player_after_clinic(&g_sim, 15, 11, 0, &n);
                g_sim.rng_script = NULL;
                CHECK(has == found && result == AFTER_IN_ROOM);
                CHECK(tr[0x44 + kind] == (found ? 0x7E : requested) && tr[0x45 - kind] == 0);
                CHECK(!memcmp(before_ids, tr + 0x78, sizeof before_ids) && !memcmp(before_cond, tr + 0x5A, sizeof before_cond));
                CHECK(sc.used == 0 && sc.extra == 0);
            }
}

/* The decision (0x2D11A step 5 and its twins, AI mode): capabilities (bytes 21-25) first; a missing one goes to +0x44
 * with +0x45 = 0; with all there, a missing kind (bytes 11-15) goes to +0x45 with +0x44 = 0; nothing missing: both
 * items are used once (condition 50 -> 49) and u16 +0x44 = 0. The treatment is pending (+18 = 999) in every case. */
static void test_equipment_decision(void) {
    for (int c = 0; c < 3; c++)
        for (int k = 0; k < 4; k++) { /* nothing, the capability item only, both, the kind item only */
            uint8_t *tr = equipped_room(15, ROOM_CATS[c]), *p = patient(5);
            p[41] = 3, p[42] = 7;
            if (k == 1 || k == 2) room_item(tr, 0, 2, 50);
            if (k == 2 || k == 3) room_item(tr, 1, 3, 50);
            put16(tr + 0x44, 0x0707); /* stale bytes: the decision writes both */
            put16(tr + 14, 0), put16(tr + 16, 0), put16(tr + 18, 0);
            sim_rooms_after_operator(&g_sim, 15, ROOM_CATS[c], 0);
            CHECK(get16(tr + 12) == 5 && get16(tr + 16) >= 30 && get16(tr + 16) <= 33 && get16(tr + 18) == 999);
            static const uint8_t WANT[4][2] = {{0x20, 0}, {0, 5}, {0, 0}, {0x20, 0}};
            if (tr[0x44] != WANT[k][0] || tr[0x45] != WANT[k][1])
                fprintf(stderr, "  decision cat %d case %d: %02X %02X\n", ROOM_CATS[c], k, tr[0x44], tr[0x45]);
            CHECK(tr[0x44] == WANT[k][0] && tr[0x45] == WANT[k][1]);
            if (k == 2) CHECK(tr[0x5A] == 49 && tr[0x5B] == 49);  /* used */
            if (k == 1) CHECK(tr[0x5A] == 50);                     /* a gap: nothing used */
            if (k == 3) CHECK(tr[0x5B] == 50);
        }
}

/* The pending tick (+18 = 999): with a gap the check runs again; still missing: nothing starts, an AI hospital orders
 * nothing (no install although the store holds it); closed: the bytes are cleared, the items are used, and the start
 * waits for a later tick. Without a gap r1 decides the start in all three rooms (both outcomes over 40 seeds). */
static void test_equipment_pending(void) {
    for (int c = 0; c < 3; c++) {
        for (int k = 0; k < 2; k++) {
            uint8_t *tr = equipped_room(15, ROOM_CATS[c]), *p = patient(5), *st = store_world(15);
            p[43] = 30, put16(tr + 14, 0), put16(tr + 16, 30), put16(tr + 18, 999);
            put16(st + 4, 40), g_world.block[BLK_STAFF][40 * 32 + 11] = 1; /* a self-fetching clerk */
            put16(st + 0xFA, 2), st[0x262] = 80, st[9] = 1, st[7] = 20;
            tr[0x44] = 0x20, tr[0x45] = 0;
            if (k == 1) room_item(tr, 0, 2, 50), room_item(tr, 1, 3, 50);
            sim_rooms_run(&g_sim, ROOM_TICK[c]);
            CHECK(get16(tr + 18) == 999);
            if (k == 0) CHECK(tr[0x44] == 0x20 && get16(tr + 0x78) == 0 && st[9] == 1); /* AI: the tick orders nothing */
            if (k == 1) CHECK(get16(tr + 0x44) == 0 && tr[0x5A] == 49 && tr[0x5B] == 49);
        }
        int started = 0, waited = 0;
        for (int t = 0; t < 40; t++) {
            uint8_t *tr = equipped_room(15, ROOM_CATS[c]), *p = patient(5);
            p[43] = 30, put16(tr + 14, 0), put16(tr + 16, 30), put16(tr + 18, 999), put16(tr + 0x44, 0);
            sim_seed(&g_sim, 500u + (uint64_t)t);
            sim_rooms_run(&g_sim, ROOM_TICK[c]);
            started += get16(tr + 18) == 10 && p[45] == 1;
            waited += get16(tr + 18) == 999;
        }
        CHECK(started > 5 && waited > 5 && started + waited == 40);
    }
    /* the player's hospital (+0x14 set) with a self-fetching clerk orders in the tick: the item is installed */
    for (int self = 0; self <= 1; self++) {
        uint8_t *tr = equipped_room(0, 5), *p = patient(5), *st = store_world(0);
        world_hospital(&g_world, 0)[0x14] = 1;
        p[43] = 30, put16(tr + 14, 0), put16(tr + 16, 30), put16(tr + 18, 999);
        put16(st + 4, 40), g_world.block[BLK_STAFF][40 * 32 + 11] = (uint8_t)self;
        put16(st + 0xFA, 2), st[0x262] = 80, st[9] = 1, st[7] = 20;
        tr[0x44] = 0x20, tr[0x45] = 0;
        sim_rooms_run(&g_sim, 0x6AD3C);
        CHECK((get16(tr + 0x78) == 2) == self && st[9] == (self ? 0 : 1));
        CHECK(tr[0x44] == 0x20 && get16(tr + 18) == 999); /* cleared only by the next check */
        if (self) {
            sim_rooms_run(&g_sim, 0x6AD3C);
            CHECK(tr[0x44] == 0 && tr[0x45] == 5); /* the capability is there now; the kind is missing */
        }
    }
}

/* The operators' supplies step: both staff present, a store, u16 +0x44 non-zero: the capability +0x44 and the kind
 * +0x45 are installed from the store, the bytes stay; an empty store sets h+0xDA / h+0xDB; one staff member missing,
 * or no gap, does nothing. */
static void test_equipment_supplies(void) {
    for (int c = 0; c < 3; c++)
        for (int k = 0; k < 4; k++) { /* store has both, store empty, no doctor, no gap */
            uint8_t *tr = equipped_room(15, ROOM_CATS[c]), *st = store_world(15), *r = world_hospital(&g_world, 15);
            world_set_cash(&g_world, 15, 1000);
            put16(tr + 12, 0), tr[0] = 120; /* no patient, rated high: no fittings */
            st[7] = 20;
            if (k != 1) put16(st + 0xFA, 2), put16(st + 0xFC, 3), st[0x262] = 80, st[0x263] = 70, st[9] = 2;
            if (k == 2) put16(tr + 10, 0), world_set_tail(&g_world, TAIL_HOURS, 51), world_set_tail(&g_world, TAIL_HOUR, 2);
            if (k != 3) tr[0x44] = 0x20, tr[0x45] = 5;
            sim_staff_run(&g_sim, ROOM_OPERATOR[c]);
            int both = get16(tr + 0x78) == 2 && tr[0x5A] == 80 && get16(tr + 0x7A) == 3 && tr[0x5B] == 70;
            CHECK(both == (k == 0));
            if (k == 0) CHECK(tr[0x44] == 0x20 && tr[0x45] == 5 && st[9] == 0);
            CHECK((r[0xDA] == 0x20 && r[0xDB] == 5) == (k == 1));
            if (k >= 2) CHECK(get16(tr + 0x78) == 0 && st[9] == 2);
        }
}

/* 0x235FA: the AI's neurology puts a bed case into the LAST neuro ward with a free bed, the first free place whose
 * sex entry (0x4DCC) is P's, else entry 2, else any (all ten places), and counts +0x4C and +0x4E; P leaves the room.
 * No free bed anywhere: home untreated, nothing counted. */
static void test_ai_neuro_bed(void) {
    static const uint8_t SEX[10] = {2, 2, 2, 2, 1, 2, 2, 0, 1, 0};
    for (int k = 0; k < 4; k++) { /* a man: place 7; a woman: 4; places 4 and 8 taken (woman): 0; no free bed */
        uint8_t *tr = equipped_room(15, 15), *p = patient(5), *r = world_hospital(&g_world, 15);
        memcpy(g_image + 0x4DCC, SEX, 10);
        for (int t = 30; t <= 33; t++) g_image[0x270C + t * 38 + 4] = 1; /* treated in a bed */
        g_world.block[BLK_ROOM_COUNTS][15 * 100 + 16] = 2;
        uint8_t *w0 = sim_room_record(&g_sim, 15, 16, 0), *w1 = sim_room_record(&g_sim, 15, 16, 1);
        w0[10] = 4, w0[12] = 0, w1[10] = 6, w1[12] = 0;
        p[0] = (uint8_t)(k == 0 ? 0 : 1), p[41] = 3;
        if (k == 2) put16(w1 + 0x14 + 4 * 4, 100), put16(w1 + 0x14 + 4 * 8, 101), w1[12] = 2;
        if (k == 3) w0[12] = 4, w1[12] = 6;
        p[42] = p[56]; /* the tick's diagnosis (SIM-36) */
        put16(tr + 14, 0), put16(tr + 16, 0), put16(tr + 18, 0);
        sim_rooms_after_operator(&g_sim, 15, 15, 0);
        static const int PLACE[3] = {7, 4, 0};
        if (k < 3) {
            CHECK(get16(w1 + 0x14 + 4 * PLACE[k]) == 5 && get16(w1 + 0x16 + 4 * PLACE[k]) == 0);
            CHECK(w1[12] == (k == 2 ? 3 : 1) && w0[12] == 0 && !get16(tr + 12));
            CHECK(get16(r + 0x4C) == 1 && get16(r + 0x4E) == 1 && p[4] == 16 && p[66] == 1);
        } else {
            CHECK(!get16(tr + 12) && p[3] == 0xFF && get16(r + 0x4C) == 0 && get16(r + 0x4E) == 0);
        }
    }
}

/* ---- docs/sim-drift2.md: the bill rule, the reception's forms and pens, the diagnosis start, the staff credit ------ */

/* 0x32075 (rooms) and 0x320FE (wards), the oracle's table of docs/sim-drift2.md section 4: an ordinary patient with
 * opinion O of hospital 12 after a bill B. The negative branch tests the running opinion (O 100, B 40: 100 - 16 - 16 =
 * 68, below 75, stop) and clamps once at the end; 8-bit steps wrap (O 10, B 208: v = -100, 10 - 100 - 100 = 66). */
static void test_bill_opinion(void) {
    static const int O[6] = {50, 80, 89, 90, 95, 100}, B[6] = {3, 7, 10, 20, 30, 40};
    static const int WANT[6][6] = {{62, 54, 48, 38, 28, 18}, {88, 84, 76, 68, 58, 48}, {97, 93, 85, 70, 67, 57},
                                   {96, 93, 86, 71, 68, 58}, {100, 98, 85, 76, 73, 63}, {100, 100, 90, 81, 66, 68}};
    patient_world();
    uint8_t *p = patient(500);
    p[2] = 0;
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 6; j++) {
            p[20 + 12] = (uint8_t)O[i];
            sim_opinion_after_bill(p, 12, B[j], 0);
            if (p[20 + 12] != WANT[i][j]) fprintf(stderr, "  bill O %d B %d: %d, want %d\n", O[i], B[j], p[32], WANT[i][j]);
            CHECK(p[20 + 12] == WANT[i][j]);
        }
    static const int WB[3] = {60, 100, 200}, WW[3] = {85, 76, 71};
    for (int j = 0; j < 3; j++) {
        p[32] = 95;
        sim_opinion_after_bill(p, 12, WB[j], 1);
        CHECK(p[32] == WW[j]);
    }
    p[32] = 10;
    sim_opinion_after_bill(p, 12, 208, 0);
    CHECK(p[32] == 66);
    p[32] = 50;
    sim_opinion_after_bill(p, 12, 108, 0); /* 50 - 50 - 50 = -50 (signed): below 75, no more steps, clamped to 0 */
    CHECK(p[32] == 0);
    p[32] = 80;
    sim_opinion_after_bill(p, 12, 208, 0); /* 80 - 100 - 100 = -120 as a signed byte: fails the 75 test, 0 */
    CHECK(p[32] == 0);
    p[32] = 10;
    sim_opinion_after_bill(p, 12, 30, 0); /* 10 - 11 - 11: negative, clamped to 0 at the end */
    CHECK(p[32] == 0 && p[20 + 4] == 50); /* only the billing hospital's opinion */
}

/* A reception (hospital 15, staffed, 20 equipment slots at +134, conditions at +114) with patient 5 at queue place 1,
 * one waiting room with 10 free seats; item 1 is a form (kind 11, wear 2), item 2 a pen (kind 3, wear 1). */
static uint8_t *reception_world(void) {
    patient_world();
    world_set_cash(&g_world, 15, 1000);
    static const uint16_t D[7] = {0, 0, 134, 20, 0, 114, 0};
    for (int i = 0; i < 7; i++) put16(g_image + 0x4758 + 14 * 2 + 2 * i, D[i]);
    uint8_t *E = g_image + 0x144A;
    E[20 * 1 + 6] = 11, E[20 * 1 + 16] = 2, E[20 * 2 + 6] = 3, E[20 * 2 + 16] = 1;
    uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + 15 * 100;
    c[2] = c[3] = 1;
    uint8_t *R = sim_room_record(&g_sim, 15, 2, 0), *w = sim_room_record(&g_sim, 15, 3, 0), *p = patient(5);
    put16(R + 8, 156), put16(R + 10, 5);
    w[10] = 10;
    /* seats from furniture (as wear_world), so that a recount after a store install keeps them */
    static const uint16_t W[7] = {0x40, 5, 0x60, 10, 0x90, 0xA0, 0xC0};
    for (int i = 0; i < 7; i++) put16(g_image + 0x4758 + 14 * 3 + 2 * i, W[i]);
    g_image[0xEE0 + 18 * 8 + 6] = 5, g_image[0xEE0 + 18 * 8 + 10] = 5, g_image[0x31F2 + 60 + 0x29] = 10;
    put16(w + 0x40, 8), w[0x90] = 50;
    p[0] = 0, p[3] = 15, p[4] = 2;
    return R;
}
static void reception_item(uint8_t *R, int slot, int id, int cond) { put16(R + 134 + 2 * slot, id), R[114 + slot] = (uint8_t)cond; }
static int seated(void) { return patient(5)[4] == 3 && sim_room_record(&g_sim, 15, 3, 0)[12] == 1; }

/* 0x2B989 (AI): no form -> R +7 = 11 (also when the pen is missing too), no pen -> R +7 = 3, P stays at queue place 1
 * and nothing is used; no free seat -> no check at all (the seat search comes first); both there: one of each used (the
 * form 10 -> 8, the pen at 1 wears out, leaves slot 0 and the tidy-up 0x2BFB7 moves the form up), then the move and the
 * receptionist's +0x10. */
static void test_reception_supplies(void) {
    for (int k = 0; k < 6; k++) { /* pen only, form only, both but no free seat, both, neither, neither and no seat */
        uint8_t *R = reception_world();
        if (k == 1 || k == 2 || k == 3) reception_item(R, 1, 1, 10);
        if (k == 0 || k == 2 || k == 3) reception_item(R, 0, 2, 1);
        if (k == 2 || k == 5) sim_room_record(&g_sim, 15, 3, 0)[12] = 10;
        sim_rooms_after_operator(&g_sim, 15, 2, 0);
        int staff = get16(g_world.block[BLK_STAFF] + 156 * 32 + 0x10);
        if (k >= 4) {
            CHECK(R[7] == (k == 4 ? 11 : 0) && get16(R + 10) == 5 && patient(5)[4] == 2 && staff == 0);
        } else if (k < 3) {
            CHECK(R[7] == (k == 0 ? 11 : k == 1 ? 3 : 0) && get16(R + 10) == 5 && patient(5)[4] == 2 && staff == 0);
            CHECK(R[114] == (k == 1 ? 0 : 1) && R[115] == (k == 0 ? 0 : 10)); /* nothing used */
        } else {
            CHECK(R[7] == 0 && seated() && get16(R + 10) == 0 && staff == 1);
            CHECK(get16(R + 134) == 1 && R[114] == 8 && get16(R + 136) == 0 && R[115] == 0);
        }
    }
}

/* 0x23060's request step: with R +7 set and no store room the operator ends (no admission although forms and pens are
 * there); a store without the item sets h+0xDB and ends; a store with it installs the item (the clerk's +0x10 + 1),
 * clears R +7 and goes on to the admission. SIM-77 (docs/sim-rooms2.md, "Store-room helpers"): successful equipment
 * installs credit even clerk index 0, with u16 wrap; failed supplies credit nobody. */
static void test_reception_request(void) {
    static const int CLERKS[] = {0, 1, 1040};
    static const int WORK[] = {0, 65535};
    for (int c = 0; c < 3; c++)
        for (int w = 0; w < 2; w++)
            for (int k = 0; k < 3; k++) { /* no store, a store without a form, a store with one */
                uint8_t *R = reception_world(), *r = world_hospital(&g_world, 15);
                reception_item(R, 0, 2, 5);
                R[7] = 11;
                if (k == 0) reception_item(R, 1, 1, 5); /* already in the room: still ends without a store */
                uint8_t *st = sim_room_record(&g_sim, 15, 4, 0);
                st[7] = 20, put16(st + 4, CLERKS[c]);
                if (k > 0) store_world(15);
                if (k == 2) put16(st + 0xFA, 1), st[0x262] = 30, st[9] = 1;
                for (int i = 0; i < 3; i++) put16(g_world.block[BLK_STAFF] + CLERKS[i] * 32 + 0x10, WORK[w]);
                sim_rooms_after_operator(&g_sim, 15, 2, 0);
                if (k < 2) {
                    CHECK(R[7] == 11 && !seated() && get16(R + 10) == 5);
                    CHECK(r[0xDB] == (k == 1 ? 11 : 0));
                } else {
                    CHECK(R[7] == 0 && seated() && r[0xDB] == 0 && st[9] == 0);
                    CHECK(get16(R + 136) == 1 && R[115] == 28); /* installed, then used once (wear 2) */
                }
                for (int i = 0; i < 3; i++) {
                    int expected = (WORK[w] + (k == 2 && i == c)) & 0xFFFF;
                    CHECK((uint16_t)get16(g_world.block[BLK_STAFF] + CLERKS[i] * 32 + 0x10) == expected);
                }
            }
}

/* The diagnosis start (docs/sim-drift2.md section 3), +14 = 999. Item 6 is of kind 9 (wear 0: 1 with r3 = 0), item 7
 * of kind 12 (wear 2). */
static uint8_t *start_room(int h, int cat) {
    uint8_t *tr = equipped_room(h, cat);
    g_image[0x144A + 20 * 6 + 6] = 9, g_image[0x144A + 20 * 7 + 6] = 12, g_image[0x144A + 20 * 7 + 16] = 2;
    put16(tr + 14, 999), put16(tr + 16, 0), put16(tr + 18, 0);
    patient(5)[44] = 100;
    return tr;
}
static void test_diagnosis_start(void) {
    for (int c = 0; c < 3; c++) {
        int cat = ROOM_CATS[c], kind = cat == 5 ? 9 : 12, item = cat == 5 ? 6 : 7;
        /* +0x45 = 0: r1 decides; with the item the start uses it (field 54 + 1, +14 = the duration), without it the
         * neurology starts anyway, the other two record the kind and wait */
        for (int have = 0; have <= 1; have++) {
            int started = 0, waited = 0, missed = 0, worn = 0;
            for (int t = 0; t < 60; t++) {
                uint8_t *tr = start_room(15, cat), *p = patient(5);
                if (have) room_item(tr, 2, item, 50);
                sim_seed(&g_sim, 900u + (uint64_t)t);
                sim_rooms_run(&g_sim, ROOM_TICK[c]);
                int go = get16(tr + 14) != 999;
                started += go && p[54] == 1;
                waited += !go && tr[0x45] == 0 && p[54] == 0;
                missed += !go && tr[0x45] == kind && p[54] == 0;
                if (go && have && cat != 15) worn += tr[0x5C] == (cat == 5 ? 49 : 48);
                if (go && have && cat != 15) CHECK(tr[0x5C] == 50 - (cat == 5 ? 0 : 2) || tr[0x5C] == 49);
            }
            if (cat == 15 || have) CHECK(started > 10 && waited > 10 && started + waited == 60 && !missed);
            else CHECK(!started && waited > 10 && missed > 10 && waited + missed == 60);
            if (have && cat == 11) CHECK(worn == started);
            if (have && cat == 5) CHECK(worn > 0 && worn < started); /* wear 0: 1 with r3 = 0 */
        }
        if (cat == 15) {
            uint8_t *tr = start_room(15, cat); /* no check at all: +0x45 is neither read nor written */
            tr[0x45] = 33;
            int t = 0;
            for (; t < 40 && get16(tr + 14) == 999; t++) sim_rooms_run(&g_sim, ROOM_TICK[c]);
            CHECK(get16(tr + 14) != 999 && tr[0x45] == 33);
            continue;
        }
        /* +0x45 set: no draw - the start comes in the same tick when the item is there (+0x45 cleared) */
        for (int t = 0; t < 20; t++) {
            uint8_t *tr = start_room(15, cat);
            room_item(tr, 2, item, 50), tr[0x45] = (uint8_t)kind;
            sim_seed(&g_sim, 300u + (uint64_t)t);
            sim_rooms_run(&g_sim, ROOM_TICK[c]);
            CHECK(get16(tr + 14) != 999 && tr[0x45] == 0 && patient(5)[54] == 1);
        }
        /* a worn-out item leaves the list and the tidy-up closes the gap (item 2 behind it moves up) */
        {
            uint8_t *tr = start_room(15, cat);
            room_item(tr, 1, item, 1), room_item(tr, 2, 2, 40), tr[0x45] = (uint8_t)kind;
            g_image[0x144A + 20 * item + 16] = 1;
            sim_rooms_run(&g_sim, ROOM_TICK[c]);
            CHECK(get16(tr + 14) != 999 && get16(tr + 0x7A) == 2 && tr[0x5B] == 40 && get16(tr + 0x7C) == 0);
        }
        /* missing with +0x45 set: an AI hospital waits and fetches nothing in the tick; the player's hospital with a
         * self-fetching clerk fetches the item from the store and starts; without that clerk byte it waits */
        for (int k = 0; k < 3; k++) {
            int h = k == 0 ? 15 : 0;
            uint8_t *tr = start_room(h, cat), *st = store_world(h);
            if (h == 0) world_hospital(&g_world, 0)[0x14] = 1;
            put16(st + 4, 40), g_world.block[BLK_STAFF][40 * 32 + 11] = (uint8_t)(k != 2);
            put16(st + 0xFA, item), st[0x262] = 60, st[9] = 1, st[7] = 20;
            tr[0x45] = (uint8_t)kind;
            sim_rooms_run(&g_sim, ROOM_TICK[c]);
            int fetched = k == 1;
            CHECK((get16(tr + 14) != 999) == fetched && tr[0x45] == (fetched ? 0 : kind) && st[9] == (fetched ? 0 : 1));
            if (fetched) CHECK(get16(tr + 0x78) == item);
        }
    }
}

/* 0x30E7C in every diagnosing tick (+14 counting) of the three rooms: r15 = 3 credits the nurse (staff 7), a second
 * r15 = 7 the doctor (staff 8), a third r15 = 7 field 54. 1,600 ticks: about 100 each; field 54 also gets the room's own
 * r15 = 6 (treatment room) or r7 = 3 / r7 = 4 (dental, neurology) draw, so 200 or 300 in all. The patient is healthy
 * (no nurse bonus) and the countdown never reaches 0 (no diagnosis bonus). */
static void test_staff_credit(void) {
    for (int c = 0; c < 3; c++) {
        uint8_t *tr = doctor_room(15, ROOM_CATS[c]), *p = patient(5), *staff = g_world.block[BLK_STAFF];
        p[44] = 100, p[79] = 0xFF;
        sim_seed(&g_sim, 77);
        int f54 = 0;
        for (int t = 0; t < 1600; t++) {
            put16(tr + 14, 5), put16(tr + 16, 0), put16(tr + 18, 0);
            p[54] = 0;
            sim_rooms_run(&g_sim, ROOM_TICK[c]);
            f54 += p[54];
        }
        int nurse = get16(staff + 7 * 32 + 0x10), doctor = get16(staff + 8 * 32 + 0x10), own = c == 0 ? 100 : 200;
        fprintf(stderr, "  staff credit cat %d: nurse %d doctor %d field 54 %d\n", ROOM_CATS[c], nurse, doctor, f54);
        CHECK(nurse > 60 && nurse < 140 && doctor > 60 && doctor < 140);
        CHECK(f54 > own + 55 && f54 < own + 150);
    }
}

/* Care drift and room drift in the three rooms (docs/sim-drift2.md section 7): only with nurse, doctor and patient
 * present. A strong-drop case (no illness, OP units, few ward units) at health 60 in a room rated 0, and a content
 * patient (mood 90, not in treatment) in a room whose byte 0 is 200: staffed, health falls and the opinion of the
 * hospital rises (0x2E7EB, now also in the dental ward and the neurology); with the doctor or the nurse missing nothing
 * changes. The nurse's looks give no bonus (C below 0). */
static void test_drift_needs_staff(void) {
    for (int c = 0; c < 3; c++)
        for (int k = 0; k < 3; k++) { /* staffed, no doctor, no nurse */
            uint8_t *tr = doctor_room(15, ROOM_CATS[c]), *p = patient(5), *n = g_world.block[BLK_STAFF] + 7 * 32;
            n[5] = 0, n[6] = 54, n[7] = 50, n[8] = 30;
            p[56] = 0, p[52] = 5, p[50] = 0, p[102] = 0, p[44] = 60, p[78] = 90, p[79] = 0, p[20 + 15] = 50;
            put16(tr + 14, 0), put16(tr + 16, 0), put16(tr + 18, 0);
            if (k == 1) put16(tr + 10, 0);
            if (k == 2) put16(tr + 8, 0);
            for (int t = 0; t < 400; t++) {
                tr[0] = 0, tr[1] = 0;
                sim_rooms_run(&g_sim, ROOM_TICK[c]);
                tr[0] = 200; /* the room drift reads byte 0; the care drift ran with V = 0 */
                if (k == 0 && p[44] < 60) break;
            }
            int health = p[44];
            for (int t = 0; t < 400; t++) sim_rooms_run(&g_sim, ROOM_TICK[c]);
            if (k == 0) CHECK(health < 60 && p[20 + 15] > 50);
            else CHECK(health == 60 && p[44] == 60 && p[78] == 90 && p[20 + 15] == 50);
        }
}

/* The nurse bonus's mood step (0x2EB8D step 2) follows the r63 test alone: a patient in state 2 (below 5) in a staffed
 * neurology gets mood + 1 from the bonus but no field 55; in state 16 also field 55 now and then (r3 = 1). */
static void test_nurse_bonus_mood(void) {
    for (int k = 0; k < 2; k++) {
        uint8_t *tr = doctor_room(15, 15), *p = patient(5);
        p[4] = (uint8_t)(k ? 16 : 2), p[44] = 92, p[78] = 0, p[79] = 0xFF, p[55] = 0, p[56] = 0;
        put16(tr + 14, 0), put16(tr + 16, 0), put16(tr + 18, 0);
        for (int t = 0; t < 320; t++) sim_rooms_run(&g_sim, 0x6BAE4);
        CHECK(p[78] > 5 && p[78] < 40);
        CHECK(k ? p[55] > 0 : p[55] == 0);
    }
}

/* a synthetic name pool where every person is "Abcd Doe" and stock text 4016 (n = 4) is sixteen names "mmmm".."ffff"
 * (men first): the rename of the dead (0x3200C) turns a man into "mmmm Doe", a woman into the ninth name */
static uint8_t g_names[1009 * 12], g_name_table[1009 * 4], g_stock[80];
static DataDb g_people;
static TextDb g_stock_texts;
static void name_pool(void) {
    for (int i = 0; i < 1009; i++) memcpy(g_names + 12 * i, "Abcd Doe$\0\0\0", 12), put32(g_name_table + 4 * i, 12u * i);
    memset(&g_people, 0, sizeof g_people);
    g_people.pools[0] = (TextPool){g_names, sizeof g_names, g_name_table, 1009};
    for (int k = 0; k < 16; k++) memset(g_stock + 4 * k, 'm' - k, 4);
    g_stock[64] = '$';
    for (int i = 0; i < TEXT_COUNT; i++) g_stock_texts.offs[i] = i <= 4016 ? 0 : 65;
    g_stock_texts.text = g_stock, g_stock_texts.text_size = 65;
    g_sim.people = &g_people, g_sim.texts = &g_stock_texts;
}

/* Independent flat reference for the heap span of SIM-79. Comparing the complete span checks both the target
 * word and every byte that must stay unchanged, including the real hospital's pathology. */
enum { DEATH_HEAP_SIZE = 117272 };
static void death_heap_copy(uint8_t *flat, int to_world) {
    size_t at = 0;
    for (int b = 6; b <= 14; b++) {
        CHECK(at + g_world.size[b] <= DEATH_HEAP_SIZE);
        if (to_world) memcpy(g_world.block[b], flat + at, g_world.size[b]);
        else memcpy(flat + at, g_world.block[b], g_world.size[b]);
        at += g_world.size[b];
    }
    CHECK(at == DEATH_HEAP_SIZE);
}

/* Per-tick death through the reception's actual patient path, for every hospital and edition. Cases include no
 * pathology, empty/full/overfull aliases, index 20 and beyond, the largest list displacement (into a later block),
 * displacement and count wrap, and writes that overwrite +6/+8 themselves before the count is incremented. */
static void test_sim79_death_alias(void) {
    static const struct { int has, listed, places; } CASES[] = {
        {0, 0, 5}, {1, 0, 0}, {1, 7, 7}, {1, 0, 5}, {1, 6, 5}, {1, 20, 21}, {1, 25, 26},
        {1, 32723, 0}, {1, 32767, 0}, {1, 65535, 0}, {1, 32727, 0}, {1, 32728, 0}};
    uint8_t expected[DEATH_HEAP_SIZE], actual[DEATH_HEAP_SIZE];
    for (int edition = EDITION_DISK; edition <= EDITION_ENGLISH_CD; edition++)
        for (int h = 0; h < 16; h++)
            for (size_t c = 0; c < sizeof CASES / sizeof *CASES; c++) {
                patient_world();
                g_prog.edition = (Edition)edition;
                name_pool();
                uint8_t *p = patient(5), *rec = sim_room_record(&g_sim, h, 2, 0);
                p[3] = (uint8_t)h, p[4] = 2, p[66] = 73, p[104] = 1;
                uint8_t want_patient[106];
                memcpy(want_patient, p, sizeof want_patient);
                put16(rec + 8, 7), put16(rec + 10, 5);
                g_world.block[BLK_ROOM_COUNTS][100 * h + 18] = (uint8_t)CASES[c].has;
                int listed = CASES[c].listed, places = CASES[c].places;
                int keep = CASES[c].has && listed != places;
                /* The real pathology deliberately has the opposite capacity verdict. */
                uint8_t *path = sim_room_record(&g_sim, h, 18, 0);
                put16(path + 6, 0), put16(path + 8, keep ? 0 : 9);
                size_t alias = death_alias_offset(h);
                death_heap_copy(expected, 0);
                put16(expected + alias + 6, listed), put16(expected + alias + 8, places);
                death_heap_copy(expected, 1);
                if (keep) {
                    size_t displacement = (0x58u + 2u * listed) % 65536u;
                    put16(expected + alias + displacement, 5);
                    put16(expected + alias + 6, (get16(expected + alias + 6) + 1) % 65536);
                    want_patient[4] = 18, want_patient[66] = 2;
                }
                sim_rooms_after_operator(&g_sim, h, 2, 0);
                CHECK(get16(world_hospital(&g_world, h) + 0x38) == 1);
                CHECK(get16(world_hospital(&g_world, h) + 0x3C) == !keep);
                CHECK(get16(rec + 10) == 0);
                CHECK(sim_news_count(&g_sim) == !keep);
                death_heap_copy(actual, 0);
                CHECK(memcmp(expected, actual, sizeof expected) == 0);
                if (keep) CHECK(memcmp(want_patient, p, sizeof want_patient) == 0);
                else CHECK(p[3] == 0xFF && p[4] == 0xFF && p[44] == 100 && p[104] == 0);
                CHECK(memcmp(g_names + 60, keep ? "Abcd Doe" : "mmmm Doe", 8) == 0);
            }
    g_prog.edition = EDITION_DISK;
}

/* Common death (the corpse action) makes the opposite choice from SIM-79: the real hospital's pathology
 * decides, and a newspaper item is always made. Cover all hospital/edition pairs with conflicting alias capacity. */
static void test_sim79_common_death_control(void) {
    uint8_t expected[DEATH_HEAP_SIZE], actual[DEATH_HEAP_SIZE];
    for (int edition = EDITION_DISK; edition <= EDITION_ENGLISH_CD; edition++)
        for (int h = 0; h < 16; h++)
            for (int keep = 0; keep < 2; keep++) {
                patient_world();
                g_prog.edition = (Edition)edition;
                patient(5)[3] = (uint8_t)h, patient(5)[4] = 2, patient(5)[104] = 1;
                g_world.block[BLK_ROOM_COUNTS][100 * h + 18] = 1;
                uint8_t *path = sim_room_record(&g_sim, h, 18, 0);
                put16(path + 8, keep ? 9 : 0);
                uint8_t *alias = death_alias_record(h);
                put16(alias + 6, 0), put16(alias + 8, keep ? 0 : 9);
                death_heap_copy(expected, 0);
                if (keep) put16(expected + 158 * h + 6, 1), put16(expected + 158 * h + 0x58, 5);
                int staff = -1;
                CHECK(sim_player_corpse(&g_sim, h, 5, 2, 0, 0, &staff) == (keep ? 0 : 2));
                CHECK(staff == (keep ? 9 : 0));
                CHECK(patient(5)[4] == (keep ? 18 : 0xFF));
                CHECK(get16(world_hospital(&g_world, h) + 0x38) == 1);
                CHECK(get16(world_hospital(&g_world, h) + 0x3C) == !keep);
                CHECK(sim_news_count(&g_sim) == 1);
                death_heap_copy(actual, 0);
                CHECK(memcmp(expected, actual, sizeof expected) == 0);
            }
    g_prog.edition = EDITION_DISK;
}

/* A death in the home treatment (0x37BBE) counts for the treating hospital and leaves every opinion alone, whatever
 * hospital record 1's +0x174 / +0x177 hold: those are hospital 0's manual-query state, and the zeroing the original
 * does with them is the copy protection's penalty (removed 2026-10-19; this test fails if it comes back). */
static void test_death_keeps_opinion(void) {
    for (int k = 0; k < 2; k++) {
        patient_world();
        uint8_t *p = patient(5), *h1 = world_hospital(&g_world, 1);
        g_image[0x954 + 7 * 20 + 9] = 1; /* not survivable: health 3 - 4 kills */
        p[1] = 1, p[79] = 1, p[43] = 30, p[81] = 4, p[82] = 0, p[44] = 3, p[105] = 1;
        p[20] = 70, p[20 + 4] = 60;
        if (k) h1[0x177] = 1, h1[0x174] = 3;
        name_pool();
        p[0] = (uint8_t)k; /* a man, then a woman */
        sim_patients_run(&g_sim, 0x672A1);
        CHECK(get16(world_hospital(&g_world, 4) + 0x38) == 1 && p[56] == 0 && p[44] == 100); /* died, reset at home */
        CHECK(!memcmp(g_names + 60, k ? "eeee Doe" : "mmmm Doe", 8) && !memcmp(g_names + 48, "Abcd Doe", 8)); /* renamed */
        CHECK(p[20] == 70 && p[20 + 4] == 60);
        CHECK(h1[0x177] == (k ? 1 : 0) && h1[0x174] == (k ? 3 : 0));
    }
}

/* ---- docs/sim-drift3.md: the operators' release and night call, the room breaks, the unit counts --------------- */

/* staff slot k of hospital h holds employee `index` of job group `group` in state `state`, mood 50 */
static uint8_t *put_slot(int h, int k, int index, int group, int state) {
    uint8_t *sl = staff_slots(&g_sim, h) + k * STAFF_SLOT_SIZE, *st = g_world.block[BLK_STAFF] + index * 32;
    put16(sl, index), sl[2] = (uint8_t)group, sl[3] = (uint8_t)state;
    st[1] = (uint8_t)group, st[3] = (uint8_t)h, st[4] = (uint8_t)state, st[21] = 50;
    return sl;
}

/* patient 5 waits in waiting room 0 of hospital h (equipped_room fits illness 7 to the room's kind) */
static void waiting_patient(int h) {
    g_world.block[BLK_ROOM_COUNTS][h * 100 + 3] = 1;
    uint8_t *wr = sim_room_record(&g_sim, h, 3, 0);
    wr[10] = 10, wr[12] = 1, put16(wr + 0x14, 5), put16(wr + 0x16, 0);
    patient(5)[3] = (uint8_t)h, patient(5)[4] = 3;
}

/* Section 3 and docs/sim-drift7.md section 4.1: the treatment-room, dental and neurology operators' release check frees
 * each set staff field (nurse +8 first, the dental ward clears +0x19 with her; the neurology does not) when nobody of
 * the room's kind waits, the hour is 19 or later and the reception is unstaffed. It is reached from the free room with
 * nobody waiting and after every staffing branch. Cases: 0 the release; 1 hour 18; 2 a patient in the room; 3 a patient
 * waiting; 4 a staffed reception; 5 no nurse (nobody to assign): the lone doctor goes; 6 no doctor: the lone nurse goes;
 * 7 no nurse at 18:00 with an unstaffed reception: the call ends, the doctor stays; 8 no nurse at 19:00 with a patient
 * waiting: the nurse is asked for, nobody is released; 9 no doctor at 02:00: the release check, too early. */
static void test_operator_release(void) {
    static const int GROUP10[3] = {2, 9, 10};
    for (int c = 0; c < 3; c++)
        for (int k = 0; k < 10; k++) {
            int cat = ROOM_CATS[c];
            uint8_t *tr = equipped_room(15, cat);
            tr[0] = 120, tr[0x19] = 30;
            put16(tr + 12, 0);
            if (k == 2) put16(tr + 12, 5), put16(tr + 14, 3), patient(5)[4] = (uint8_t)cat; /* diagnosing */
            if (k == 3 || k == 8) waiting_patient(15);
            if (k == 4) g_world.block[BLK_ROOM_COUNTS][15 * 100 + 2] = 1, put16(sim_room_record(&g_sim, 15, 2, 0) + 8, 99);
            int no_nurse = k == 5 || k == 7 || k == 8, no_doctor = k == 6 || k == 9;
            if (no_nurse) put16(tr + 8, 0);
            if (no_doctor) put16(tr + 10, 0);
            uint8_t *nurse = put_slot(15, 0, 7, 1, no_nurse ? 0x13 : cat);
            uint8_t *doc = put_slot(15, 1, 8, GROUP10[c], no_doctor ? 0x13 : cat);
            world_set_tail(&g_world, TAIL_HOURS, 51);
            world_set_tail(&g_world, TAIL_HOUR, k == 1 || k == 7 ? 18 : k == 9 ? 2 : 19);
            sim_staff_run(&g_sim, ROOM_OPERATOR[c]);
            int kept30 = tr[0x19] == 30;
            if (k == 0) {
                CHECK(get16(tr + 8) == 0 && get16(tr + 10) == 0 && nurse[3] == 0x13 && doc[3] != cat);
                CHECK(tr[0x19] == (cat == 11 ? 0 : 30));
            } else if (k == 5) {
                CHECK(get16(tr + 8) == 0 && get16(tr + 10) == 0 && doc[3] != cat && kept30);
            } else if (k == 6) {
                CHECK(get16(tr + 8) == 0 && get16(tr + 10) == 0 && nurse[3] == 0x13 && tr[0x19] == (cat == 11 ? 0 : 30));
            } else {
                CHECK(get16(tr + 8) == (no_nurse ? 0 : 7) && get16(tr + 10) == (no_doctor ? 0 : 8) && kept30);
                if (!no_doctor) CHECK(doc[3] == cat);
                if (!no_nurse) CHECK(nurse[3] == cat);
            }
        }
}

/* docs/sim-drift7.md section 4.1: a call that finds a staff field empty ends after the assignment, without the patient
 * part: at 10:00 with a staffed reception and a patient waiting, the first call fills the empty field (nurse or
 * doctor), the second brings the patient. */
static void test_operator_staffing_ends(void) {
    static const int GROUP10[3] = {2, 9, 10};
    for (int c = 0; c < 3; c++)
        for (int field = 8; field <= 10; field += 2) {
            int cat = ROOM_CATS[c];
            uint8_t *tr = equipped_room(15, cat);
            tr[0] = 120;
            put16(tr + 12, 0), put16(tr + field, 0);
            room_item(tr, 0, 2, 100), room_item(tr, 1, 3, 100);
            waiting_patient(15);
            g_world.block[BLK_ROOM_COUNTS][15 * 100 + 2] = 1, put16(sim_room_record(&g_sim, 15, 2, 0) + 8, 99);
            put_slot(15, 0, 7, 1, field == 8 ? 0 : cat), put_slot(15, 1, 8, GROUP10[c], field == 10 ? 0 : cat);
            world_set_tail(&g_world, TAIL_HOURS, 51);
            world_set_tail(&g_world, TAIL_HOUR, 10);
            sim_staff_run(&g_sim, ROOM_OPERATOR[c]);
            CHECK(get16(tr + field) == (field == 8 ? 7 : 8) && get16(tr + 12) == 0 && patient(5)[4] == 3);
            sim_staff_run(&g_sim, ROOM_OPERATOR[c]);
            CHECK(get16(tr + 12) == 5);
        }
}

/* docs/sim-drift7.md section 2.2: the reception operator 0x23060 against a twin generator that draws in the documented
 * order: the picker, the fittings' rating r1 (rating 0, below the day's threshold, with a store room), the r3 of the
 * +0x16E counter, the r7 and, on r7 = 0 with +0x45 at 0, the helper's r15 (+0x45 = r15 + 22). Every second call starts
 * with +0x45 = 5, which no set replaces. Nobody at the desk, so the patient part draws nothing. */
static void test_reception_comfort(void) {
    uint8_t *R = reception_world(), *r = world_hospital(&g_world, 15);
    put16(R + 10, 0);
    g_world.block[BLK_ROOM_COUNTS][15 * 100 + 4] = 1;
    world_set_tail(&g_world, TAIL_HOURS, 51);
    int sets = 0, ok = 1;
    for (int i = 0; i < 400 && ok; i++) {
        int busy = i & 1;
        R[4] = 0, R[0x45] = (uint8_t)(busy ? 5 : 0);
        int count = get16(r + 0x16E);
        Sim twin = g_sim;
        int h = sim_pick(&twin);
        SimIn in = rng_operator(&twin, h, R);
        SimIn old = sim_set_inputs(&twin, in);
        int want = sim_bits(&twin, 1) == 0 ? 6 : 2;
        sim_set_inputs(&twin, old);
        int r3 = (int)sim_bits_in(&twin, 3, 0x23128, in), r7 = (int)sim_bits_in(&twin, 7, 0x231ED, in);
        int c45 = busy ? 5 : r7 == 0 ? (int)sim_bits_in(&twin, 15, 0x2B4AE, in) + 22 : 0;
        sim_staff_run(&g_sim, 0x23060);
        ok = h == 15 && R[4] == want && get16(r + 0x16E) == count + (r3 == 0) && R[0x45] == c45 && g_sim.rng == twin.rng &&
             g_sim.rng_s == twin.rng_s;
        sets += !busy && r7 == 0;
    }
    CHECK(ok && sets > 10);
}

/* The fittings come first in the department operators (docs/sim-staff.md, "Where the fittings step sits"): a staffed
 * dental ward of hospital 15 with a store, rating 0 below the threshold and the wanted byte 0: a twin drawing the
 * picker, the room pick (one room: no draw), then the fittings' r1 and then the dental comfort r7 predicts the wanted
 * byte (r1 = 0: 6, else 2) and whether the comfort is set (r7 = 4), call after call. */
static void test_dental_fittings_first(void) {
    uint8_t *tr = doctor_room(15, 11);
    g_world.block[BLK_ROOM_COUNTS][15 * 100 + 4] = 1;
    int ok = 1, sixes = 0, comforts = 0;
    for (int i = 0; i < 200 && ok; i++) {
        tr[4] = 0, tr[0x19] = 0, tr[0] = tr[1] = 0;
        Sim twin = g_sim;
        int h = sim_pick(&twin);
        sim_random_in(&twin, 1, 0x34575, rng_operator(&twin, h, NULL));
        SimIn in = rng_operator(&twin, h, tr);
        SimIn old = sim_set_inputs(&twin, in);
        int want = sim_bits(&twin, 1) == 0 ? 6 : 2;
        sim_set_inputs(&twin, old);
        int comfort = sim_bits_in(&twin, 7, 0x3472B, in) == 4;
        sim_staff_run(&g_sim, 0x34535);
        ok = h == 15 && tr[4] == want && (tr[0x19] != 0) == comfort && get16(tr + 8) == 7 && get16(tr + 10) == 8;
        sixes += want == 6, comforts += comfort;
    }
    CHECK(ok && sixes > 50 && sixes < 150 && comforts > 5);
}

/* The same for the waiting-room operator 0x2C993: staffed, a store, rating 0, wanted byte +6 at 0: the r1 comes before
 * the comfort r7 (r7 = 0 sets +0x0F). */
static void test_waiting_fittings_first(void) {
    reception_world();
    uint8_t *w = sim_room_record(&g_sim, 15, 3, 0);
    g_world.block[BLK_ROOM_COUNTS][15 * 100 + 4] = 1;
    put16(w + 8, 9);
    int ok = 1, sixes = 0, comforts = 0;
    for (int i = 0; i < 200 && ok; i++) {
        w[6] = 0, w[0x0F] = 0, w[0] = w[1] = 0;
        Sim twin = g_sim;
        int h = sim_pick(&twin);
        sim_random_in(&twin, 1, 0x2C9D8, rng_operator(&twin, h, NULL));
        SimIn in = rng_operator(&twin, h, w);
        SimIn old = sim_set_inputs(&twin, in);
        int want = sim_bits(&twin, 1) == 0 ? 6 : 2;
        sim_set_inputs(&twin, old);
        int comfort = sim_bits_in(&twin, 7, 0x2CB06, in) == 0;
        sim_staff_run(&g_sim, 0x2C993);
        ok = h == 15 && w[6] == want && (w[0x0F] != 0) == comfort && get16(w + 8) == 9;
        sixes += want == 6, comforts += comfort;
    }
    CHECK(ok && sixes > 50 && sixes < 150 && comforts > 5);
}

static void logged_call(uint32_t routine);
static int site_inputs(unsigned site, unsigned d, unsigned e);

/* Step 2: with the cash below 0 the call draws nothing after the picker and runs no fittings; a staffed reception's
 * receptionist is released and +0x45 cleared, an unstaffed one gets nobody (+0x45 untouched). */
static void test_reception_cash(void) {
    for (int staffed = 0; staffed <= 1; staffed++) {
        uint8_t *R = reception_world();
        put16(R + 10, 0);
        g_world.block[BLK_ROOM_COUNTS][15 * 100 + 4] = 1;
        world_set_cash(&g_world, 15, -1);
        world_set_tail(&g_world, TAIL_HOURS, 51);
        uint8_t *sl = put_slot(15, 0, 156, 1, staffed ? 2 : 0);
        if (!staffed) put16(R + 8, 0);
        R[0x45] = 9;
        Sim twin = g_sim;
        sim_pick(&twin);
        logged_call(0x23060);
        CHECK(R[4] == 0 && site_inputs(0x23128, 0, 0) == 0 && site_inputs(0x231ED, 0, 0) == 0);
        CHECK(g_sim.rng == twin.rng && g_sim.rng_s == twin.rng_s);
        CHECK(get16(R + 8) == 0 && R[0x45] == (staffed ? 0 : 9) && sl[3] == (staffed ? 0x13 : 0));
    }
}

/* Step 7: from 19:00 the receptionist goes when queue place 1 and the Krankenzimmer list are empty, +0x45 is cleared
 * and the call ends (a pending +7 with its item in the store is not served). Cases: 0 the release; 1 a patient at
 * place 1; 2 a patient in the Krankenzimmer list; 3 hour 18. */
static void test_reception_evening(void) {
    for (int k = 0; k < 4; k++) {
        uint8_t *R = reception_world();
        if (k != 1) put16(R + 10, 0), patient(5)[3] = 0xFF;
        if (k == 2) put16(R + 0x46, 6);
        reception_item(R, 0, 2, 5);
        R[7] = 11, R[0x45] = 9;
        uint8_t *st = store_world(15);
        st[7] = 20, put16(st + 4, 40);
        put16(st + 0xFA, 1), st[0x262] = 30, st[9] = 1;
        uint8_t *sl = put_slot(15, 0, 156, 1, 2);
        world_set_tail(&g_world, TAIL_HOURS, 51);
        world_set_tail(&g_world, TAIL_HOUR, k == 3 ? 18 : 19);
        sim_staff_run(&g_sim, 0x23060);
        if (k == 0) CHECK(get16(R + 8) == 0 && R[0x45] == 0 && sl[3] == 0x13 && R[7] == 11);
        else CHECK(get16(R + 8) == 156 && R[0x45] == 9 && sl[3] == 2 && R[7] == 0);
    }
}

/* Step 5: an unstaffed reception asks for its receptionist and the call ends: the patient at place 1 waits for the
 * next call. */
static void test_reception_staffing_ends(void) {
    uint8_t *R = reception_world();
    reception_item(R, 0, 2, 5), reception_item(R, 1, 1, 10);
    put16(R + 8, 0);
    put_slot(15, 0, 156, 1, 0);
    world_set_tail(&g_world, TAIL_HOURS, 51);
    sim_staff_run(&g_sim, 0x23060);
    CHECK(get16(R + 8) == 156 && get16(R + 10) == 5 && !seated());
    sim_staff_run(&g_sim, 0x23060);
    CHECK(seated());
}

/* docs/sim-drift7.md section 4.2: the ward operators call a nurse into an empty ward at 22:00 (unstaffed reception) only
 * when a patient waits for a treatment room (the neuro ward: for the neurology), and from 21:00 keep her while one
 * waits. */
static void test_ward_waiting_test(void) {
    for (int c = 0; c < 2; c++)
        for (int waits = 0; waits <= 1; waits++)
            for (int has_nurse = 0; has_nurse <= 1; has_nurse++) {
                int cat = c ? 16 : 6;
                equipped_room(15, c ? 15 : 5); /* illness 7 fitted to the treatment room or the neurology */
                g_world.block[BLK_ROOM_COUNTS][15 * 100 + (c ? 15 : 5)] = 0;
                room_descriptions();
                world_set_cash(&g_world, 15, 1000);
                g_world.block[BLK_ROOM_COUNTS][15 * 100 + cat] = 1;
                uint8_t *ward = sim_room_record(&g_sim, 15, cat, 0);
                ward[10] = 10, ward[12] = 0;
                put16(ward + 8, has_nurse ? 7 : 0);
                put_slot(15, 0, 7, 1, has_nurse ? cat : 0);
                if (waits) waiting_patient(15);
                world_set_tail(&g_world, TAIL_HOURS, 51);
                world_set_tail(&g_world, TAIL_HOUR, 22);
                sim_rooms_run(&g_sim, c ? 0x2A01F : 0x2995B);
                CHECK((get16(ward + 8) == 7) == waits);
            }
}

/* Section 3, the night call: at 02:00 with 51 hours played an empty +8 is filled only when a patient of the room's kind
 * waits (for the waiting room: its own count +0x0C). */
static void test_operator_night_call(void) {
    for (int c = 0; c < 4; c++)
        for (int waits = 0; waits <= 1; waits++) {
            int cat = c < 3 ? ROOM_CATS[c] : 3;
            uint8_t *tr = c < 3 ? equipped_room(15, cat) : (patient_world(), NULL);
            if (c == 3) {
                g_world.block[BLK_ROOM_COUNTS][15 * 100 + 3] = 1;
                tr = sim_room_record(&g_sim, 15, 3, 0);
                tr[10] = 10, tr[12] = (uint8_t)waits, tr[0x0F] = 30;
            } else {
                tr[0] = 120;
                put16(tr + 12, 0), put16(tr + 8, 0);
                if (waits) waiting_patient(15);
            }
            put_slot(15, 0, 7, 1, 0); /* an idle nurse */
            world_set_tail(&g_world, TAIL_HOURS, 51);
            world_set_tail(&g_world, TAIL_HOUR, 2);
            sim_staff_run(&g_sim, c < 3 ? ROOM_OPERATOR[c] : 0x2C993);
            CHECK((get16(tr + 8) == 7) == waits);
        }
}

/* Section 3: the waiting-room operator releases its employee from hour 19 when the room is empty (+0x0C) and the
 * reception unstaffed, and clears +0x0F. Cases: the release; a patient; a staffed reception; hour 18. */
static void test_waiting_room_release(void) {
    for (int k = 0; k < 4; k++) {
        patient_world();
        g_world.block[BLK_ROOM_COUNTS][15 * 100 + 3] = 1;
        uint8_t *wr = sim_room_record(&g_sim, 15, 3, 0);
        wr[10] = 10, wr[12] = (uint8_t)(k == 1), wr[0x0F] = 30, put16(wr + 8, 7);
        put_slot(15, 0, 7, 1, 3);
        if (k == 2) g_world.block[BLK_ROOM_COUNTS][15 * 100 + 2] = 1, put16(sim_room_record(&g_sim, 15, 2, 0) + 8, 99);
        world_set_tail(&g_world, TAIL_HOURS, 51);
        world_set_tail(&g_world, TAIL_HOUR, k == 3 ? 18 : 20);
        sim_staff_run(&g_sim, 0x2C993);
        if (k == 0) CHECK(get16(wr + 8) == 0 && wr[0x0F] == 0);
        else CHECK(get16(wr + 8) == 7 && wr[0x0F] == 30);
    }
}

/* Section 4: the room ticks call the staff break 0x2E666 (category, the room's instance, staff field, extra field):
 * an employee with slot +18 set and 10 hours on duty goes to rest (state 0x18) remembering the room (slot +16 / +17),
 * the field and only the call's extra field are cleared. 9 hours: no break. In the five treatment-type rooms only
 * without a patient. No club room, so the rest is at home. */
static void test_room_breaks(void) {
    static const struct { uint32_t tick; int cat, field, extra, patient_gate; } B[] = {
        {0x6BFC8, 3, 8, 0x0F, 0}, {0x6A0BA, 6, 8, 0x0F, 0}, {0x6C3D5, 16, 8, 0x0F, 0}, {0x6FA5A, 9, 8, 0x0F, 0},
        {0x6AD3C, 5, 8, 0, 1}, {0x6AD3C, 5, 10, 0, 1}, {0x6B445, 11, 8, 0x19, 1}, {0x6B445, 11, 10, 0, 1},
        {0x6BAE4, 15, 8, 0, 1}, {0x6BAE4, 15, 10, 0, 1}, {0x58045, 7, 8, 0x17, 1}, {0x58045, 7, 10, 0, 1},
        {0x58045, 7, 6, 0, 1}, {0x597F9, 8, 8, 0, 1}, {0x597F9, 8, 10, 0, 1},
    };
    static const int EXTRAS[3] = {0x0F, 0x17, 0x19};
    for (size_t b = 0; b < sizeof B / sizeof B[0]; b++)
        for (int k = 0; k < 3; k++) { /* the break; 9 hours on duty; a patient in the record */
            if (k == 2 && !B[b].patient_gate) continue;
            patient_world();
            g_world.block[BLK_ROOM_COUNTS][15 * 100 + B[b].cat] = 2;
            uint8_t *rec = sim_room_record(&g_sim, 15, B[b].cat, 1);
            int places = B[b].cat == 3 || B[b].cat == 6 || B[b].cat == 16; /* +0x14 on: places and beds */
            for (int e = 0; e < (places ? 1 : 3); e++) rec[EXTRAS[e]] = 30;
            put16(rec + B[b].field, 40);
            if (k == 2) put16(rec + 12, 5);
            uint8_t *sl = put_slot(15, 0, 40, 1, B[b].cat);
            sl[4] = 1, sl[10] = (uint8_t)(k == 1 ? 9 : 10), sl[18] = 1;
            g_world.block[BLK_STAFF][40 * 32 + 14] = 10; /* stamina 10: 60 - 20 = 40 minutes of rest */
            sim_rooms_run(&g_sim, B[b].tick);
            int rested = k == 0;
            if ((get16(rec + B[b].field) == 0) != rested)
                fprintf(stderr, "  break tick %X field %X case %d\n", (unsigned)B[b].tick, B[b].field, k);
            CHECK((get16(rec + B[b].field) == 0) == rested && (sl[3] == 0x18) == rested);
            if (rested) {
                CHECK(sl[16] == B[b].cat && sl[17] == 1 && sl[19] == 40 && sl[18] == 0);
                for (int e = 0; e < (places ? 1 : 3); e++) {
                    if (rec[EXTRAS[e]] != (EXTRAS[e] == B[b].extra ? 0 : 30))
                        fprintf(stderr, "  break tick %X: +%X = %d\n", (unsigned)B[b].tick, EXTRAS[e], rec[EXTRAS[e]]);
                    CHECK(rec[EXTRAS[e]] == (EXTRAS[e] == B[b].extra ? 0 : 30));
                }
            } else {
                CHECK(sl[18] == 1 && sl[3] == B[b].cat);
            }
        }
}

/* Section 2: the treatment end books fields 48 and 53 in the treatment room and the dental ward (0x3122B, 0x34F80), not
 * in the neurology (0x37539); the dental pending start books 54 and 55 (0x6BA2B), the other two rooms nothing. Health
 * 100 keeps the care drift and the worsening out. */
static void test_unit_counts(void) {
    for (int c = 0; c < 3; c++) {
        int cat = ROOM_CATS[c], ends48 = 0, starts = 0, start_units = 0;
        for (int t = 0; t < 40; t++) {
            uint8_t *tr = equipped_room(15, cat), *p = patient(5);
            p[44] = 100, p[43] = 30, p[45] = 0;
            put16(tr + 14, 0), put16(tr + 16, 30), put16(tr + 18, 1);
            sim_seed(&g_sim, 900u + (uint64_t)t);
            sim_rooms_run(&g_sim, ROOM_TICK[c]);
            ends48 += p[48] >= 1 && p[53] >= 1;
            tr = equipped_room(15, cat), p = patient(5);
            p[44] = 100, p[43] = 30;
            put16(tr + 14, 0), put16(tr + 16, 30), put16(tr + 18, 999), put16(tr + 0x44, 0);
            sim_seed(&g_sim, 700u + (uint64_t)t);
            sim_rooms_run(&g_sim, ROOM_TICK[c]);
            if (get16(tr + 18) == 10) starts++, start_units += p[54] == 1 && p[55] == 1;
            else CHECK(p[54] == 0 && p[55] == 0);
        }
        CHECK(ends48 == (cat == 15 ? 0 : 40) || (cat == 15 && ends48 <= 2));
        CHECK(starts > 5 && start_units == (cat == 11 ? starts : 0));
    }
}

/* Section 2: the treating phase draws r31 once per field test, so two fields can move in one tick (with one shared draw
 * the four distinct values never could). 4,000 ticks per room: about 23 expected. */
static void test_treating_draws(void) {
    for (int c = 0; c < 3; c++) {
        uint8_t *tr = equipped_room(15, ROOM_CATS[c]), *p = patient(5);
        p[44] = 100, p[43] = 30, p[45] = 0;
        put16(tr + 14, 0), put16(tr + 16, 30);
        int multi = 0, any = 0;
        for (int t = 0; t < 4000; t++) {
            put16(tr + 18, 200);
            p[48] = p[53] = p[54] = p[55] = 0;
            sim_rooms_run(&g_sim, ROOM_TICK[c]);
            int moved = (p[48] != 0) + (p[53] != 0) + (p[54] != 0) + (p[55] != 0);
            multi += moved >= 2, any += moved >= 1;
        }
        CHECK(multi >= 5 && any > 300 && any < 650);
    }
}

/* docs/mainloop.md section 3: one draw of the original's generator, against values worked out by hand from the spec's
 * three steps (synthetic states and inputs: zeros, the start value with context S, a low-half wrap of D with an E
 * that wraps the sum, all ones, an operator pair). */
static void test_generator_step(void) {
    static const struct { uint32_t s, d, e, t, s2; } V[] = {
        {0x00000000, 0x0000, 0x00000000, 0xDF455566, 0xDF455566},
        {0xE7C2D08C, 0x0000, 0x001F21A6, 0x153A8313, 0xFCFD539F},
        {0x1234FFF0, 0x0020, 0xFFFFFFF0, 0xF17A4742, 0x03AF4732},
        {0xFFFFFFFF, 0xFFFF, 0xFFFFFFFF, 0x20B8AA98, 0x20B8AA97},
        {0xE7C2D08C, 0x6187, 0x00360082, 0xD593832A, 0xBD5653B6},
    };
    for (size_t i = 0; i < sizeof V / sizeof *V; i++) {
        uint32_t state = V[i].s;
        uint32_t t = sim_generator_step(&state, V[i].d, V[i].e);
        if (t != V[i].t || state != V[i].s2) fprintf(stderr, "  step %d: t %08X S %08X\n", (int)i, t, state);
        CHECK(t == V[i].t && state == V[i].s2);
    }
    /* through the engine's draw: the state moves by exactly one step, the result is t AND the mask */
    fresh();
    CHECK(g_sim.rng_s == SIM_GENERATOR_START); /* sim_init: the start state */
    CHECK(sim_bits_in(&g_sim, 31, 0, sim_in(0, 0, 0x1F21A6)) == (0x153A8313u & 31) && g_sim.rng_s == 0xFCFD539F);
    /* a context set by the routine is used by sim_bits; only the default draws advance the auxiliary generator */
    g_sim.rng_s = 0xE7C2D08C;
    uint64_t aux = g_sim.rng;
    SimIn old = sim_set_inputs(&g_sim, sim_in(0x6187, 0x16DDB0 + 300, 0x1F21A6));
    CHECK(sim_bits(&g_sim, 0xFFFFFFFF) == 0xD593832A && g_sim.rng == aux && !old.on);
    sim_set_inputs(&g_sim, old);
    sim_bits(&g_sim, 1);
    CHECK(g_sim.rng != aux);
}

/* random(n) (docs/mainloop.md section 3): n 0 and 1 give 0 without a draw; else the mask is 2^(L+1) - 1 (random(2)
 * with 7, random(16) with 63) and every redraw uses the same pair. (a) the spec worked out by hand: random(2) from the
 * start state with context S redraws once (value 1), random(16) three times (value 3), random(1,008) and random(3)
 * take the first draw; (b) 2,000 states per n against a loop with the spec's mask table. */
/* The scripted draws of tools/diff_routines.py (BIING_RNG_SCRIPT): every generator step returns the next scripted t,
 * whatever the inputs; S advances by t; random(n) masks and redraws the scripted values as its own; a script that runs
 * out goes back to the generator and counts the steps; the trace names each draw call and its steps. */
static void test_rng_script(void) {
    fresh();
    static const uint32_t V[] = {0x1234567Bu, 0xABCDEF0Cu, 0x00000007u, 0x80000002u, 0xFFFFFFF1u};
    SimScript sc = {V, sizeof V / sizeof *V, 0, 0, tmpfile()};
    uint32_t s0 = g_sim.rng_s;
    uint64_t aux = g_sim.rng;
    g_sim.rng_script = &sc;
    CHECK(sim_bits(&g_sim, 15) == 0xB);                                   /* V[0] & 15 */
    CHECK(g_sim.rng_s == s0 + V[0] && sc.used == 1);
    CHECK(sim_random(&g_sim, 10) == 7 && sc.used == 3);                   /* V[1] & 15 = 12 >= 10: redraw, V[2] */
    CHECK(sim_random_in(&g_sim, 3, 0x62D1F, sim_in(0x21A6, 0, 0x1F21A6)) == 2); /* V[3] & 7, the inputs unused */
    CHECK(sim_random(&g_sim, 1) == 0 && sc.used == 4);                    /* random(1): no draw */
    CHECK(g_sim.rng == aux);                                              /* no fresh input pairs drawn */
    CHECK(sim_bits_site(&g_sim, 1, 0x2B4AE) == 1 && sc.used == 5 && sc.extra == 0);
    uint32_t ref = g_sim.rng_s, want = sim_generator_step(&ref, 0x4748, 0x1F21A6);
    CHECK(sim_bits_in(&g_sim, 0xFFFFFFFFu, 0, sim_in(0x4748, 0, 0x1F21A6)) == want); /* ran out: the generator's own */
    CHECK(sc.extra == 1 && g_sim.rng_s == ref);
    char text[256] = {0};
    rewind((FILE *)sc.trace);
    size_t got = fread(text, 1, sizeof text - 1, (FILE *)sc.trace);
    fclose((FILE *)sc.trace);
    char want_trace[160];
    snprintf(want_trace, sizeof want_trace, "0 b15 11 1\n0 r10 7 2\n62D1F r3 2 1\n2B4AE b1 1 1\n0 b4294967295 %u 1\n",
             want);
    CHECK(got > 0 && !strcmp(text, want_trace));
    /* without a script: the generator's own results, as before the hook */
    g_sim.rng_script = NULL;
    ref = g_sim.rng_s;
    want = sim_generator_step(&ref, 0x21A6, 0x1F21A6) & 63;
    CHECK(sim_bits_in(&g_sim, 63, 0, sim_in(0x21A6, 0, 0x1F21A6)) == want && g_sim.rng_s == ref);
    /* the script file: one hex value per line, "#" lines and blank lines skipped */
    FILE *f = fopen("test_rng_script.txt", "w");
    CHECK(f != NULL);
    if (f) {
        fputs("# scripted\n0000000a\n\nFFFFFFFF\n12345678\n", f);
        fclose(f);
        uint32_t *v = NULL;
        size_t n = 0;
        CHECK(sim_script_read("test_rng_script.txt", &v, &n) == 0);
        CHECK(n == 3 && v && v[0] == 10 && v[1] == 0xFFFFFFFFu && v[2] == 0x12345678u);
        free(v);
        remove("test_rng_script.txt");
    }
    CHECK(sim_script_read("no_such_script.txt", &(uint32_t *){NULL}, &(size_t){0}) != 0);
}

/* SIM-31 (docs/parity.md): step 1 of the reception minute 0x67611 (hospital byte +0x17A + r15 = r127 + 90,
 * docs/mainloop.md) and of the waiting rooms 0x6BFC8 (+0x194 + r15 = r127 + 102, docs/sim-rooms.md) draw the r15
 * index first, then the r127 value. With no reception and no waiting room each hospital draws only these two. The
 * script gives hospital h the pair (index h, value 0x80 + 7 h + 5, so r127 = 7 h + 5): drawn the other way round the
 * index would be (7 h + 5) AND 15, never h, so every hospital's byte would land elsewhere. */
static void test_index_draw_first(void) {
    static const struct {
        uint32_t routine, table, add;
    } SITES[] = {{0x67611, 0x17A, 90}, {0x6BFC8, 0x194, 102}};
    for (size_t k = 0; k < sizeof SITES / sizeof *SITES; k++) {
        fresh();
        uint32_t v[32];
        for (uint32_t h = 0; h < 16; h++) v[2 * h] = h, v[2 * h + 1] = 0x80u + 7u * h + 5u;
        SimScript sc = {v, 32, 0, 0, NULL};
        g_sim.rng_script = &sc;
        CHECK(sim_call(&g_sim, SITES[k].routine));
        g_sim.rng_script = NULL;
        CHECK(sc.used == 32 && sc.extra == 0);
        int ok = 1;
        for (uint32_t h = 0; h < 16; h++)
            for (uint32_t i = 0; i < 16; i++) {
                uint8_t want = i == h ? (uint8_t)(7u * h + 5u + SITES[k].add) : 0;
                if (world_hospital(&g_world, (int)h)[SITES[k].table + i] != want) ok = 0;
            }
        CHECK(ok);
    }
}

static void test_random_mask(void) {
    fresh();
    uint32_t before = g_sim.rng_s;
    uint64_t aux = g_sim.rng;
    CHECK(sim_random(&g_sim, 0) == 0 && sim_random(&g_sim, 1) == 0 && g_sim.rng_s == before && g_sim.rng == aux);
    static const struct { uint32_t s, n, d, e, v, s2; } V[] = {
        {0xE7C2D08C, 2, 0, 0x1F21A6, 1, 0xA3FB4FB8},
        {0xE7C2D08C, 16, 0, 0x1F21A6, 3, 0x59FCC52F},
        {0x89ABCDEF, 1008, 0x4748, 0x1F21A6, 667, 0xFCF0788A},
        {0x13579BDF, 3, 0x1111, 0x2222, 0, 0x37067DEF},
    };
    for (size_t i = 0; i < sizeof V / sizeof *V; i++) {
        g_sim.rng_s = V[i].s;
        uint32_t v = sim_random_in(&g_sim, V[i].n, 0, sim_in(V[i].d, 0, V[i].e));
        if (v != V[i].v || g_sim.rng_s != V[i].s2) fprintf(stderr, "  random %d: %u S %08X\n", (int)i, v, g_sim.rng_s);
        CHECK(v == V[i].v && g_sim.rng_s == V[i].s2);
    }
    static const uint32_t N[] = {2, 3, 4, 7, 8, 15, 16, 17, 31, 32, 102, 154, 1008, 1024};
    static const uint32_t MASK[] = {7, 7, 15, 15, 31, 31, 63, 63, 63, 127, 255, 511, 2047, 4095};
    for (size_t k = 0; k < sizeof N / sizeof *N; k++)
        for (uint32_t i = 0; i < 2000; i++) {
            uint32_t st = 0x9E3779B9u * (i + 1) + (uint32_t)k, d = i * 7919u, e = i * 104729u;
            uint32_t want, ref = st;
            do want = sim_generator_step(&ref, d & 0xFFFF, e) & MASK[k];
            while (want >= N[k]);
            g_sim.rng_s = st;
            uint32_t got = sim_random_in(&g_sim, N[k], 0, sim_in(d, 0, e));
            if (got != want || g_sim.rng_s != ref) {
                fprintf(stderr, "  random(%u) state %08X: %u, want %u\n", N[k], st, got, want);
                CHECK(0);
                break;
            }
        }
    /* a trapped redraw (engine choice, sim.c random_n): under the pair (0x21A6, 0x360596) the state 0x380D7933 is a
     * 2-cycle drawing 24 and 40, so random(16) never accepts with that pair; the guard leaves it with fresh pairs */
    uint32_t ref = 0x380D7933u, a = sim_generator_step(&ref, 0x21A6, 0x360596) & 63;
    uint32_t b = sim_generator_step(&ref, 0x21A6, 0x360596) & 63;
    CHECK(a == 24 && b == 40 && ref == 0x380D7933u);
    g_sim.rng_s = 0x380D7933u;
    SimIn trap = {1, 0x21A6, 0x360596};
    CHECK(sim_random_in(&g_sim, 16, 0, trap) < 16);
}

/* The reseed (docs/mainloop.md section 3): the closed form against hand-worked values (the second addition's carry
 * counts, the first one's not), and the engine's clock rule: seed 3 gives T = 300009, the first reseed reads
 * T = 300010 = 0:50:00.10, DX = 10, CX = 50. */
static void test_reseed(void) {
    static const struct { uint32_t s, dx, cx, s2; } V[] = {
        {0xFFFFFFF0, 0x0005, 0x0020, 0x0000002B}, /* carry out of the second addition */
        {0xE7C2D08C, 0x000A, 0x0032, 0xE7C2D100},
        {0xFFFFFF00, 0x3B63, 0x173B, 0x00016832}, /* carry out of the first addition only: c = 0 */
        {0x80000000, 0xFFFF, 0xFFFF, 0x80060002},
        {0x00000000, 0x0000, 0x0000, 0x00000000},
    };
    for (size_t i = 0; i < sizeof V / sizeof *V; i++) {
        uint32_t state = V[i].s;
        sim_generator_reseed(&state, V[i].dx, V[i].cx);
        if (state != V[i].s2) fprintf(stderr, "  reseed %d: %08X\n", (int)i, state);
        CHECK(state == V[i].s2);
    }
    fresh();
    sim_reset_generator(&g_sim, 3);
    CHECK(g_sim.rng_s == SIM_GENERATOR_START && g_sim.rng_time == 300009);
    sim_reseed(&g_sim);
    CHECK(g_sim.rng_time == 300010 && g_sim.rng_s == 0xE7C2D100);
    /* through the clock: the minute step reseeds once per game minute (T + 1 per minute) */
    sim_reset_generator(&g_sim, 3);
    for (int t = 0; t < 3 * SIM_TICKS_PER_MINUTE; t++) sim_tick(&g_sim);
    CHECK(g_sim.rng_time == 300012);
}

/* The default inputs (docs/rng-sites.md section 4, its model: every call from a fresh uniform state): random(16) with
 * a fresh pair per step is uniform (1,600,000 calls, every value within 1.5 %), the same draw with the constant pair
 * (0, 0x1F21A6) is not (the spec's table: 0.279 off at its worst value), which is why every unlisted draw takes the
 * default. */
static void test_default_inputs(void) {
    fresh();
    const int n = 1600000;
    int fresh_count[16] = {0}, fixed_count[16] = {0};
    uint64_t x = 12345;
    for (int i = 0; i < 2 * n; i++) {
        x = x * 6364136223846793005ull + 1442695040888963407ull; /* the fresh uniform state */
        g_sim.rng_s = (uint32_t)(x >> 32);
        if (i < n) fresh_count[sim_random(&g_sim, 16)]++;
        else fixed_count[sim_random_in(&g_sim, 16, 0, sim_in(0, 0, 0x1F21A6))]++;
    }
    double worst_fresh = 0, worst_fixed = 0;
    for (int h = 0; h < 16; h++) {
        double a = fresh_count[h] / (n / 16.0) - 1, b = fixed_count[h] / (n / 16.0) - 1;
        if (a < 0) a = -a;
        if (b < 0) b = -b;
        if (a > worst_fresh) worst_fresh = a;
        if (b > worst_fixed) worst_fixed = b;
    }
    if (worst_fresh > 0.015 || worst_fixed < 0.25 || worst_fixed > 0.31) fprintf(stderr, "  default %.4f, fixed %.4f\n", worst_fresh, worst_fixed);
    CHECK(worst_fresh < 0.015 && worst_fixed > 0.25 && worst_fixed < 0.31);
}

/* The AI picker (docs/rng-sites.md 3.1): per try one random(16) and one r31, both with D 0, EBP 0, [0x4528] (here
 * hospital record 16, as its loop writers leave it), logged under 0x62D1F and 0x62D40 in that order; only an AI hospital in business is accepted
 * (hospitals 3, 7, 13, 15 open, 5 human-controlled, the rest closed: 400,000 picks give only the four). The draws
 * themselves are replayed with the spec's generator from the log's state: each try's pair of draws matches. */
static void test_picker_draws(void) {
    fresh();
    g_sim.rng_scratch = 0x1F21A6; /* [0x4528] as the staff pass and the thugs leave it (docs/rng-sites.md 2.4) */
    world_hospital(&g_world, 15)[0x15] = 0;
    static const int OPEN[4] = {3, 7, 13, 15};
    for (int k = 0; k < 4; k++) world_hospital(&g_world, OPEN[k])[0x15] = 0xFF;
    world_hospital(&g_world, 5)[0x15] = 0xFF, world_hospital(&g_world, 5)[0x14] = 1; /* human-controlled */
    sim_seed(&g_sim, 6160);
    const int m = 400000;
    int got[16] = {0};
    for (int i = 0; i < m; i++) got[sim_pick(&g_sim)]++;
    int others = 0, open = 0;
    for (int h = 0; h < 16; h++) others += (h != 3 && h != 7 && h != 13 && h != 15) ? got[h] : 0;
    for (int k = 0; k < 4; k++) open += got[OPEN[k]] > m / 8;
    CHECK(others == 0 && open == 4);
    /* the draws of one pick, replayed */
    FILE *log = tmpfile();
    CHECK(log != NULL);
    if (!log) return;
    g_sim.rng_log = log;
    uint32_t ref = g_sim.rng_s;
    int h = sim_pick(&g_sim);
    g_sim.rng_log = NULL;
    rewind(log);
    unsigned site, n, v, in_d, in_e;
    char kind;
    int tries = 0, ok = 1, last = -1, lines = 0;
    while (fscanf(log, "%X %c%u %u %X %X", &site, &kind, &n, &v, &in_d, &in_e) == 6) {
        uint32_t want;
        ok &= site == (lines++ % 2 ? 0x62D40u : 0x62D1Fu); /* random(16) first, then the r31, per try */
        if (site == 0x62D1F) {
            ok &= kind == 'r' && n == 16;
            do want = sim_generator_step(&ref, 0, 0x1F21A6) & 63;
            while (want >= 16);
            last = (int)v;
            tries++;
        } else {
            ok &= site == 0x62D40 && kind == 'b' && n == 31;
            want = sim_generator_step(&ref, 0, 0x1F21A6) & 31;
        }
        ok &= v == want;
    }
    fclose(log);
    CHECK(ok && tries >= 1 && last == h && ref == g_sim.rng_s);
}

/* The onset's threshold through the routine 0x6953D (docs/rng-sites.md 3.5): on day 14 at 10:00 T = 35; every person is
 * a candidate, only dental wards exist, and the illness tables name illness 9 (dental, byte 4 set), so every pass gives
 * one person illness 9. 200,000 calls, logged: one r127 at 0x695A8 per call, the person's random(1,008) at 0x6968E
 * right after every value below 35 and only then, and exactly one onset per such value. The share below 35 comes
 * from the generator now (it replaced the measured 0.95); here only the onset's own draws run, so it is only held to
 * a broad band. */
static void test_onset_rate(void) {
    fresh();
    world_set_tail(&g_world, TAIL_DAY, 14);
    world_set_tail(&g_world, TAIL_HOUR, 10);
    world_set_tail(&g_world, TAIL_HOURS, 51);
    g_world.block[BLK_ROOM_COUNTS][1 * 100 + 11] = 1;
    memset(g_image + 0x41E1, 9, 0x4461 - 0x41E1);
    uint8_t *ill = g_image + 0x954 + 9 * 20;
    ill[8] = 1, ill[4] = 1;
    for (int i = 1; i < 1009; i++) {
        uint8_t *p = patient(i);
        memset(p, 0, 106);
        p[1] = 1, p[3] = 0xFF, p[44] = 100;
    }
    memset(g_sim.onset_types, 1, sizeof g_sim.onset_types); /* the kept list's unwritten tail: dental too */
    sim_seed(&g_sim, 99);
    FILE *log = tmpfile();
    CHECK(log != NULL);
    if (!log) return;
    g_sim.rng_log = log;
    int onsets = 0;
    for (int c = 0; c < 200000; c++) {
        sim_patients_run(&g_sim, 0x6953D);
        for (int i = 1; i < 1009; i++) {
            uint8_t *p = patient(i);
            if (p[56] == 9) onsets++, p[56] = 0, p[41] = 0, p[44] = 100;
        }
    }
    g_sim.rng_log = NULL;
    rewind(log);
    unsigned site, n, v, in_d, in_e;
    char kind;
    int draws = 0, below = 0, persons = 0, ok = 1, prev_below = 0;
    while (fscanf(log, "%X %c%u %u %X %X", &site, &kind, &n, &v, &in_d, &in_e) == 6) {
        if (site == 0x695A8) {
            ok &= kind == 'b' && n == 127 && !prev_below; /* a pass is always followed by the person draw */
            draws++;
            prev_below = v < 35;
            below += prev_below;
        } else if (site == 0x6968E) {
            ok &= kind == 'r' && n == 1008 && prev_below;
            persons++;
            prev_below = 0;
        }
    }
    fclose(log);
    double ratio = below / (draws * 35 / 128.0);
    if (onsets != below || draws != 200000 || ratio < 0.85 || ratio > 1.15)
        fprintf(stderr, "  onsets %d, r127 below 35 %d of %d (%.4f of 35/128), persons %d\n", onsets, below, draws, ratio,
                persons);
    CHECK(ok && draws == 200000 && persons == below && onsets == below && ratio > 0.85 && ratio < 1.15);
}

/* docs/sim-drift4.md section 6: the waiting room's work credit. With an employee in +8 and +0x0C (as the previous run
 * left it) at 5 or more, r15 = 5 adds 1 to the employee's +0x10; at exactly 10 a non-zero r1 adds 1 more. The places
 * are empty, so the run recounts +0x0C to 0 afterwards: the credit must read it before. Cases (4,000 runs each):
 * +0x0C 4, 5, 9, 10; 10 without an employee; 10 with the employee taken by the break first. */
static void test_waiting_room_credit(void) {
    static const struct { int left, staffed, rest; } C[] = {{4, 1, 0}, {5, 1, 0}, {9, 1, 0}, {10, 1, 0}, {10, 0, 0}, {10, 1, 1}};
    for (size_t c = 0; c < sizeof C / sizeof C[0]; c++) {
        int ones = 0, twos = 0, other = 0;
        for (int t = 0; t < 4000; t++) {
            patient_world();
            g_world.block[BLK_ROOM_COUNTS][15 * 100 + 3] = 1;
            uint8_t *wr = sim_room_record(&g_sim, 15, 3, 0);
            wr[10] = 10, put16(wr + 12, (uint16_t)C[c].left), put16(wr + 8, C[c].staffed ? 40 : 0);
            uint8_t *sl = put_slot(15, 0, 40, 1, 3);
            if (C[c].rest) sl[10] = 10, sl[18] = 1;
            uint8_t *st = g_world.block[BLK_STAFF] + 40 * 32;
            put16(st + 0x10, 0);
            sim_seed(&g_sim, 5000u + (uint64_t)t);
            sim_rooms_run(&g_sim, 0x6BFC8);
            int d = get16(st + 0x10);
            ones += d == 1, twos += d == 2, other += d > 2;
            CHECK(get16(wr + 12) == 0);
        }
        int credited = C[c].left >= 5 && C[c].staffed && !C[c].rest;
        if (!credited) CHECK(ones == 0 && twos == 0 && other == 0);
        else if (C[c].left == 10) CHECK(ones > 80 && ones < 170 && twos > 80 && twos < 170 && other == 0); /* 125 each */
        else CHECK(ones > 190 && ones < 310 && twos == 0 && other == 0); /* 4,000 / 16 = 250 */
        if (!credited ? ones || twos : (C[c].left == 10 ? ones < 80 || twos < 80 : twos || ones < 190))
            fprintf(stderr, "  credit case %d: %d / %d / %d\n", (int)c, ones, twos, other);
    }
}

/* ---- the input pairs per site (docs/rng-sites.md sections 2 and 3) ------------------------------------------------ */

/* one logged draw: site, value, inputs (d = FFFFFFFF: the default) */
typedef struct {
    unsigned site, n, v, d, e;
    char kind;
} Logged;
static Logged g_logged[200000];
static int g_nlogged;

/* runs the routine with the draw log on and reads the log back */
static void logged_call(uint32_t routine) {
    FILE *log = tmpfile();
    CHECK(log != NULL);
    if (!log) return;
    g_sim.rng_log = log;
    sim_call(&g_sim, routine);
    g_sim.rng_log = NULL;
    rewind(log);
    g_nlogged = 0;
    Logged x;
    while (g_nlogged < 200000 && fscanf(log, "%X %c%u %u %X %X", &x.site, &x.kind, &x.n, &x.v, &x.d, &x.e) == 6)
        g_logged[g_nlogged++] = x;
    fclose(log);
}

/* every logged draw of the site has inputs (d, e); returns how many there were, -1 at the first mismatch */
static int site_inputs(unsigned site, unsigned d, unsigned e) {
    int k = 0;
    for (int i = 0; i < g_nlogged; i++) {
        if (g_logged[i].site != site) continue;
        if (g_logged[i].d != (d & 0xFFFF) || g_logged[i].e != e) {
            fprintf(stderr, "  site %X: inputs %X %X, want %X %X\n", site, g_logged[i].d, g_logged[i].e, d & 0xFFFF, e);
            return -1;
        }
        k++;
    }
    return k;
}
static int first_value(unsigned site) {
    for (int i = 0; i < g_nlogged; i++)
        if (g_logged[i].site == site) return (int)g_logged[i].v;
    return -1;
}

/* The addresses of docs/rng-sites.md section 2.2, written out here from the table (not through sim_address). */
#define ROW(h) (0x16DDB0u + 100u * (h))
#define HOSP(h) (0x1F0266u + 500u * (h))
#define H16 0x1F21A6u

/* SIM-107: use the real generator, not scripted raw results. Early golf refusals keep the room-count row;
 * late refusals keep the golf record. A second employee must start with its own room-count inputs. */
static unsigned golf_context_draw(Logged *expected, int *count, uint32_t *state, unsigned site,
                                  unsigned mask, unsigned d, unsigned e) {
    unsigned value = sim_generator_step(state, d & 0xFFFF, e) & mask;
    expected[(*count)++] = (Logged){site, mask, value, d & 0xFFFF, e, 'b'};
    return value;
}
static void golf_context_mood(Logged *expected, int *count, uint32_t *state, unsigned d, unsigned e, uint8_t *st) {
    unsigned first = golf_context_draw(expected, count, state, 0x300B0, 15, d, e);
    if (first > 14) st[21]++;
    else if (golf_context_draw(expected, count, state, 0x300BA, 15, d, e) <= 14)
        st[21] -= st[1] == 4 ? 3 : 1;
    if (st[21] > 100) st[21] = 100;
}
static void test_golf_join_context(void) {
    enum { NO_ROOM, WORKING, WOMAN, GROUP3, GROUP4, GROUP6, GROUP8, SCREEN54, NO_ATTENDANT, TIRED,
           FULL, JOIN, SCREEN310, STILL_GOLFING, LEAVE_GOLF, BELOW_TIRED, NEGATIVE_FATIGUE, CASES };
    static const int HOSPITAL[] = {0, 9, 15};
    for (int edition = EDITION_DISK; edition <= EDITION_ENGLISH_CD; edition++)
        for (int hi = 0; hi < 3; hi++) for (int branch = 0; branch < CASES; branch++) {
            int second_site = 0;
            for (unsigned seed = 0; seed < 4; seed++) {
                fresh();
                g_prog.edition = (Edition)edition;
                g_sim.rng_heap = PROGRAM_BUILDS[edition].heap_shift;
                int h = HOSPITAL[hi];
                unsigned heap = g_sim.rng_heap, d = 0x1DE912u + 32u * 77 + heap;
                unsigned row = ROW(h) + HOSP(h) + 2 * heap;
                unsigned golf = 0x16D650u + 118u * (unsigned)h + HOSP(h) + 2 * heap;
                uint8_t *sl = put_slot(h, 0, 77, 2, 0), *st = g_world.block[BLK_STAFF] + 77 * 32;
                uint8_t *next_sl = put_slot(h, 1, 78, 2, 2), *next = st + 32;
                sl[10] = next_sl[10] = 15;
                st[18] = next[18] = 2, st[19] = next[19] = 9;
                st[5] = 80, st[7] = 20;
                uint8_t *g = sim_room_record(&g_sim, h, 12, 0);
                g_world.block[BLK_ROOM_COUNTS][h * 100 + 12] = 1;
                put16(g + 2, 90), g[4] = 1;
                if (branch == NO_ROOM || branch == LEAVE_GOLF) g_world.block[BLK_ROOM_COUNTS][h * 100 + 12] = 0;
                if (branch == WORKING) st[4] = sl[3] = 2;
                if (branch == WOMAN) st[0] = 1;
                if (branch >= GROUP3 && branch <= GROUP8) {
                    static const uint8_t GROUP[] = {3, 4, 6, 8};
                    st[1] = sl[2] = GROUP[branch - GROUP3];
                }
                g_sim.screen = branch == SCREEN54 ? 54 : branch == SCREEN310 ? 310 : 0;
                put16(g_world.block[BLK_PLAYER0] + 4, branch == SCREEN54 ? 0 : 54);
                put16(g_world.block[BLK_PLAYER1] + 4, 54);
                if (branch == NO_ATTENDANT || branch == SCREEN310 || branch == STILL_GOLFING || branch == LEAVE_GOLF)
                    put16(g + 2, 0);
                if (branch == TIRED) st[7] = 95, st[5] = 60;
                if (branch == FULL || branch == BELOW_TIRED || branch == NEGATIVE_FATIGUE) g[4] = 0;
                if (branch == BELOW_TIRED) st[7] = 89, st[5] = 0;
                if (branch == NEGATIVE_FATIGUE) st[7] = 95, st[5] = 110;
                if (branch == STILL_GOLFING || branch == LEAVE_GOLF) {
                    st[4] = sl[3] = 12, st[7] = branch == STILL_GOLFING ? 90 : 100;
                    put16(g + 6, 77), g[5] = 1;
                }
                World expected_world;
                world_alloc(&expected_world);
                for (int b = 0; b <= WORLD_BLOCKS; b++)
                    memcpy(expected_world.block[b], g_world.block[b], g_world.size[b]);
                uint8_t *es = expected_world.block[BLK_STAFF] + 77 * 32;
                uint8_t *el = expected_world.block[BLK_STAFF_SLOTS] + h * 4000;
                uint8_t *eg = expected_world.block[9] + h * 118; /* golf rooms */
                uint32_t state = 0x12345678u + seed * 0x9E3779B9u;
                g_sim.rng_s = state;
                uint64_t auxiliary = g_sim.rng;
                SimIn outer = {(int)(seed & 1), 0x4567, 0x89ABCDEF};
                sim_set_inputs(&g_sim, outer);
                Logged expected[5];
                int count = 0;
                if (branch == STILL_GOLFING && !golf_context_draw(expected, &count, &state, 0x2FD0D, 15, d, golf)) es[7]++;
                if (branch == LEAVE_GOLF) {
                    es[4] = el[3] = 0, es[26] = el[4] = 0, es[21] = 100;
                    put16(eg + 6, 0), eg[5] = 0;
                }
                if (branch == JOIN) {
                    put16(eg + 6, 77), eg[5] = 1;
                    es[4] = el[3] = 12, es[26] = el[4] = 0;
                } else {
                    int late = branch == NO_ATTENDANT || branch == TIRED || branch == FULL || branch == SCREEN310 ||
                               branch == BELOW_TIRED || branch == NEGATIVE_FATIGUE;
                    golf_context_mood(expected, &count, &state, d, late ? golf : row, es);
                }
                golf_context_mood(expected, &count, &state, d + 32, row, es + 32);
                logged_call(0x2FC77);
                int ok = g_nlogged == count && g_sim.rng_s == state && g_sim.rng == auxiliary &&
                         g_sim.rng_in.on == outer.on && g_sim.rng_in.d == outer.d && g_sim.rng_in.e == outer.e &&
                         g_sim.rng_scratch == H16 + heap && g_sim.rng_script == NULL;
                for (int i = 0; i < count && i < g_nlogged; i++) {
                    const Logged *a = &expected[i], *b = &g_logged[i];
                    ok &= a->site == b->site && a->kind == b->kind && a->n == b->n && a->v == b->v && a->d == b->d && a->e == b->e;
                    second_site += a->site == 0x300BA && a->d == (d & 0xFFFF);
                }
                for (int b = 0; b <= WORLD_BLOCKS; b++) ok &= !memcmp(expected_world.block[b], g_world.block[b], g_world.size[b]);
                if (!ok) fprintf(stderr, "  SIM-107 edition %d hospital %d branch %d seed %u\n", edition, h, branch, seed);
                CHECK(ok);
                world_free(&expected_world);
            }
            CHECK(branch == JOIN || second_site > 0);
        }
    g_prog.edition = EDITION_DISK;
}

/* The operators (context O, 3.2): the room pick with D = 0x6187 and EBP = the row of h, the comfort draw and its helper
 * with D = the picked room's record; the picker before them in context S (3.1). Hospital 15, two rooms of each kind,
 * every room staffed; 400 calls of each operator. */
static void test_operator_inputs(void) {
    static const struct {
        uint32_t routine, pick, comfort;
        int cat, block, area, size;
        uint32_t base;
    } O[] = {
        {0x2CB75, 0x2CBB5, 0, 5, 15, 1800, 180, 0x188188},          {0x34535, 0x34575, 0x3472B, 11, 21, 1800, 180, 0x1B3448},
        {0x35430, 0x35470, 0, 15, 22, 1800, 180, 0x1BA4C8},         {0x2C993, 0x2C9D8, 0x2CB06, 3, 11, 1800, 180, 0x16E3F0},
        {0x2A01F, 0x2A064, 0x2A19E, 16, 17, 2400, 240, 0x198808},    {0x2995B, 0x299A0, 0x29AF0, 6, 16, 2400, 240, 0x18F208},
        {0x6F346, 0x6F392, 0x6F4C6, 9, 20, 1860, 186, 0x1AC008},     {0x6E8BE, 0x6E904, 0x6EB5F, 7, 18, 1296, 216, 0x1A1E08},
    };
    const int h = 15;
    for (size_t o = 0; o < sizeof O / sizeof *O; o++) {
        fresh();
        g_sim.rng_scratch = H16; /* as the staff pass leaves it (2.4) */
        world_set_tail(&g_world, TAIL_HOUR, 10);
        world_set_tail(&g_world, TAIL_HOURS, 51);
        world_set_cash(&g_world, h, 100000);
        uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + h * 100;
        c[2] = 1, c[O[o].cat] = 2;
        put16(g_world.block[3] + h * 174 + 8, 156); /* a staffed reception */
        for (int k = 0; k < 2; k++) {
            uint8_t *rec = g_world.block[O[o].block] + h * O[o].area + k * O[o].size;
            for (int f = 6; f <= 10; f += 2) put16(rec + f, 300 + 4 * k + f / 2);
            if (O[o].cat == 6 || O[o].cat == 16) rec[10] = 10; /* beds */
        }
        int picks = 0, comforts = 0, helpers = 0, pickers = 0, ok = 1;
        for (int i = 0; i < 400; i++) {
            for (int k = 0; k < 2; k++) { /* comfort counters back to 0, so the helper runs on a hit */
                uint8_t *rec = g_world.block[O[o].block] + h * O[o].area + k * O[o].size;
                rec[0x0F] = rec[0x17] = rec[0x19] = 0;
            }
            logged_call(O[o].routine);
            int a = site_inputs(0x62D1F, 0, H16), b = site_inputs(0x62D40, 0, H16);
            int p = site_inputs(O[o].pick, 0x6187, ROW(h) + H16);
            int k = first_value(O[o].pick);
            uint32_t room = O[o].base + (uint32_t)(h * O[o].area + (k < 0 ? 0 : k) * O[o].size);
            int q = O[o].comfort ? site_inputs(O[o].comfort, room, ROW(h) + H16) : 0;
            int r = site_inputs(0x2B4AE, room, ROW(h) + H16);
            ok &= a > 0 && b == a && p == 1 && q >= 0 && r >= 0 && (r == 0 || q == 1);
            pickers += a, picks += p, comforts += q, helpers += r;
        }
        if (!ok || picks != 400 || (O[o].comfort && (!comforts || !helpers)))
            fprintf(stderr, "  operator %X: picks %d, comfort draws %d, helpers %d, picker tries %d\n", O[o].routine, picks,
                    comforts, helpers, pickers);
        CHECK(ok && picks == 400 && pickers >= 400);
        if (O[o].comfort) CHECK(comforts > 0 && helpers > 0);
        if (O[o].cat == 11) { /* the dental comfort draw only with both staff there (docs/sim-drift7.md section 4.1) */
            for (int f = 8; f <= 10; f += 2) {
                int without = 0;
                for (int k = 0; k < 2; k++) {
                    uint8_t *rec = g_world.block[O[o].block] + h * O[o].area + k * O[o].size;
                    put16(rec + 8, 300 + 4 * k + 4), put16(rec + 10, 300 + 4 * k + 5), put16(rec + f, 0);
                }
                for (int i = 0; i < 100; i++) {
                    logged_call(O[o].routine);
                    without += site_inputs(O[o].comfort, 0, 0) != 0;
                }
                CHECK(without == 0);
            }
        }
    }
}

/* The scheduled routines in context S and the others of section 3: the onset (3.5), the newspaper gate (3.6), the
 * arrival (3.4: D 0 at the first person try, then the previous try's person record; the hospital step with D = the
 * low word of hospital record 16 and EBP = block 12's base), the offers' ageing (3.7: [0x4528] = the last waiting room
 * + 0x14), the staff pass (3.8: D = the staff record, EBP = the row of h, [0x4528] = hospital record h). */
static void test_scheduled_inputs(void) {
    /* onset: day 14, 10:00 (T 35), every person a candidate; [0x4528] as the staff pass leaves it (2.4) */
    fresh();
    g_sim.rng_scratch = H16;
    world_set_tail(&g_world, TAIL_DAY, 14);
    world_set_tail(&g_world, TAIL_HOUR, 10);
    world_set_tail(&g_world, TAIL_HOURS, 51);
    int n127 = 0, n1008 = 0;
    for (int i = 0; i < 400; i++) {
        logged_call(0x6953D);
        int a = site_inputs(0x695A8, 0, H16), b = site_inputs(0x6968E, 0, H16);
        CHECK(a == 1 && b >= 0);
        n127 += a, n1008 += b;
    }
    CHECK(n127 == 400 && n1008 > 0);
    /* the newspaper gate with 5 items in use */
    fresh();
    g_sim.rng_scratch = H16;
    for (int i = 0; i < 5; i++) sim_news_item(&g_sim, i)[0] = 1;
    FILE *log = tmpfile();
    g_sim.rng_log = log;
    for (int i = 0; i < 50; i++) sim_news_gate_draw(&g_sim, 250, 90);
    g_sim.rng_log = NULL;
    rewind(log);
    g_nlogged = 0;
    Logged x;
    while (log && fscanf(log, "%X %c%u %u %X %X", &x.site, &x.kind, &x.n, &x.v, &x.d, &x.e) == 6) g_logged[g_nlogged++] = x;
    if (log) fclose(log);
    CHECK(site_inputs(0x42E10, 0, H16) == 50);
    /* arrival: the odd persons are candidates, every hospital opinion 50, hospital 15 has a staffed reception;
     * [0x4528] as the home treatment of a person treated by hospital 7 left it (2.4) */
    patient_world();
    g_sim.rng_scratch = HOSP(7);
    for (int i = 1; i < 1009; i++) {
        uint8_t *p = patient(i);
        p[1] = (uint8_t)(i & 1), p[41] = 3, p[44] = 60;
    }
    g_world.block[BLK_ROOM_COUNTS][15 * 100 + 2] = 1;
    put16(g_world.block[3] + 15 * 174 + 8, 156);
    int chained = 0, hospital_steps = 0;
    for (int i = 0; i < 300; i++) {
        logged_call(0x69CB6);
        unsigned d = 0;
        for (int k = 0; k < g_nlogged; k++) {
            const Logged *l = &g_logged[k];
            if (l->site == 0x69CE2) {
                CHECK(l->d == d && l->e == HOSP(7));
                chained += d != 0;
                d = (0x1C4748u + 106u * (l->v + 1)) & 0xFFFF;
            } else if (l->site == 0x69DFF || l->site == 0x69E40 || l->site == 0x69E4B) {
                CHECK(l->d == 0x21A6 && l->e == 0x16E3F0u + HOSP(7));
                hospital_steps++;
            }
        }
        for (int k = 1; k < 1009; k++) patient(k)[3] = 0xFF, patient(k)[4] = 0;
        memset(g_world.block[3] + 15 * 174 + 10, 0, 16); /* the queue empty again */
    }
    CHECK(chained > 0 && hospital_steps > 0);
    /* the offers' ageing: hospital 15's second waiting room is the last the waiting-room tick handled (it ran before) */
    fresh();
    g_world.block[BLK_ROOM_COUNTS][15 * 100 + 3] = 2;
    logged_call(0x6BFC8);
    uint8_t *m = sim_market(&g_sim, 0);
    m[0] = 3, m[1] = 1, m[3] = 200;
    int gates = 0, drops = 0;
    for (int i = 0; i < 100; i++) {
        m[0] = 3, m[3] = 200;
        logged_call(0x3DAEB);
        int a = site_inputs(0x3DB3F, 0, 0x16E3F0u + 1800u * 15 + 180u + 0x14);
        CHECK(a == 1);
        gates += a;
        CHECK(site_inputs(0x3DB51, 0, 0x16E3F0u + 1800u * 15 + 180u + 0x14) >= 0);
        /* on a gate 0 the drop's four draws in the original's order: r1, r3, r1, r1 */
        static const unsigned ORDER[5] = {0x3DB3F, 0x3DB4A, 0x3DB51, 0x3DB58, 0x3DB5F};
        if (g_nlogged == 5) {
            drops++;
            for (int k = 0; k < 5; k++) CHECK(g_logged[k].site == ORDER[k]);
        } else {
            CHECK(g_nlogged == 1 && g_logged[0].v != 0);
        }
    }
    CHECK(gates == 100 && drops > 5);
    /* the staff pass: employee 77 of hospital 9 works with a wage above the demand (the two r15 of 3.8) */
    fresh();
    uint8_t *sl = put_slot(9, 0, 77, 1, 5), *st = g_world.block[BLK_STAFF] + 77 * 32;
    st[18] = 10, st[19] = 11, sl[10] = 5;
    int r15 = 0;
    for (int i = 0; i < 200; i++) {
        st[21] = 50;
        logged_call(0x2FC77);
        int a = site_inputs(0x300B0, 0x1DE912u + 32u * 77, ROW(9) + HOSP(9));
        CHECK(a == 1 && site_inputs(0x300BA, 0x1DE912u + 32u * 77, ROW(9) + HOSP(9)) >= 0);
        r15 += a;
    }
    CHECK(r15 == 200);
}

/* SIM-19 (docs/sim-drift8.md "What the engine must change" item 4): the onset's type draw with the person's inputs,
 * the three synthetic checks of the doc (n = 91, E = 0x16E3F0 + 0x1F21A6 = 0x360596, D = the low word of person i's
 * record 0x1C4748 + 106 i): state -> value, state after. */
static void test_onset_type_vectors(void) {
    static const struct { uint32_t s; int person; uint32_t d, v, s2; } V[] = {
        {0x12345678, 100, 0x70B0, 90, 0x5F171F51},
        {0xE7C2D08C, 1, 0x47B2, 42, 0x921753B6},
        {0x0BADF00D, 1008, 0xE8A8, 61, 0x87120A09},
    };
    fresh();
    for (size_t i = 0; i < sizeof V / sizeof *V; i++) {
        uint32_t d = (uint32_t)sim_address(&g_sim, patient(V[i].person));
        CHECK((d & 0xFFFF) == V[i].d && 0x16E3F0u + H16 == 0x360596u);
        g_sim.rng_s = V[i].s;
        uint32_t v = sim_random_in(&g_sim, 91, 0, sim_in(d, 0x16E3F0, H16));
        if (v != V[i].v || g_sim.rng_s != V[i].s2) fprintf(stderr, "  onset type %d: %u S %08X\n", (int)i, v, g_sim.rng_s);
        CHECK(v == V[i].v && g_sim.rng_s == V[i].s2);
    }
}

/* SIM-19 through the routine 0x6953D: after the person pick (0x6968E, context S) the type draw (0x697ED), the r3
 * (0x698E9) and the illness r127s (0x69913) take D = the picked person's record and E = EBP + [0x4528], with EBP =
 * 0x16E3F0 up to and including the first r127 of the call and the last drawn illness's record (0x954 + 20 id) at every
 * r127 after it. Only dental wards; the illness tables alternate illness 9 (dental, accepted) and 8 (type 0, rejected),
 * so calls with a second r127 occur. Disk layout, then the English CD's heap shift 0x90 (person records and 0x16E3F0
 * move, the illness table in the image does not). */
static void test_onset_person_inputs(void) {
    for (int layout = 0; layout < 2; layout++) {
        const uint32_t S = layout ? 0x90 : 0;
        fresh();
        g_sim.rng_heap = S;
        g_sim.rng_scratch = H16 + S; /* as the staff pass leaves it (2.4) */
        world_set_tail(&g_world, TAIL_DAY, 14);
        world_set_tail(&g_world, TAIL_HOUR, 10);
        world_set_tail(&g_world, TAIL_HOURS, 51);
        g_world.block[BLK_ROOM_COUNTS][1 * 100 + 11] = 1;
        for (uint32_t a = 0x41E1; a < 0x4461; a++) g_image[a] = (a & 1) ? 9 : 8;
        g_image[0x954 + 9 * 20 + 8] = 1, g_image[0x954 + 9 * 20 + 4] = 1;
        g_image[0x954 + 8 * 20 + 8] = 0;
        memset(g_sim.onset_types, 1, sizeof g_sim.onset_types);
        int types = 0, r3s = 0, first = 0, later = 0, ok = 1;
        for (int c = 0; c < 3000; c++) {
            for (int i = 1; i < 1009; i++) {
                uint8_t *p = patient(i);
                memset(p, 0, 106);
                p[1] = 1, p[3] = 0xFF, p[44] = 100;
            }
            logged_call(0x6953D);
            unsigned d = 0, ebp = 0;
            int picked = 0;
            for (int k = 0; k < g_nlogged; k++) {
                const Logged *l = &g_logged[k];
                if (l->site == 0x6968E) {
                    d = (0x1C4748u + S + 106u * (l->v + 1)) & 0xFFFF, ebp = 0x16E3F0u + S, picked = 1;
                } else if (l->site == 0x697ED || l->site == 0x698E9 || l->site == 0x69913) {
                    ok &= picked && l->d == d && l->e == ebp + H16 + S;
                    if (l->site == 0x697ED) types++;
                    else if (l->site == 0x698E9) r3s++;
                    else {
                        if (ebp == 0x16E3F0u + S) first++;
                        else later++;
                        /* person sex 0, age 0: the table at 0x42E1 */
                        ebp = 0x954u + 20u * (unsigned)g_image[0x42E1 + l->v];
                    }
                }
            }
        }
        if (!ok || !types || !r3s || !first || !later)
            fprintf(stderr, "  onset inputs (layout %d): ok %d, types %d, r3 %d, r127 first %d later %d\n", layout, ok,
                    types, r3s, first, later);
        CHECK(ok && types > 100 && r3s >= types && first > 0 && later > 0);
    }
}

/* OTH-DE-14 (docs/rng-sites.md section 8, "What the engine must change" 1-2): the CD builds' input constants. sim_init
 * takes the scratch block from the build's segment map (its offset of disk 0x6187) and the heap shift from the build
 * table; the sites then use them: the context-O pick, the records, [0x4528]'s hospital 16, the arrival's block-12 base,
 * the offers' last waiting room. Synthetic builds that move 0x6187 by 0x52 (English) / 0x51 (German). */
static void test_cd_build_inputs(void) {
    static const ProgramSegment EN_SEG[] = {{0, 0x6000, 0}, {0x6000, 0x10000, 0x6052}};
    static const ProgramSegment DE_SEG[] = {{0, 0x6000, 0}, {0x6000, 0x10000, 0x6051}};
    static const ProgramBuild EN = {"GOG English CD", 0, 0, 0, "T", "D", 0, EN_SEG, 2, NULL, 0, NULL, 0, 0x90};
    static const ProgramBuild DE = {"GOG German CD", 0, 0, 0, "T", "D", 0, DE_SEG, 2, NULL, 0, NULL, 0, 0};
    static Sim t;
    fresh();
    Program p = g_prog;
    p.build = &EN, p.edition = EDITION_ENGLISH_CD;
    sim_init(&t, &g_world, &p, 7);
    rng_scratch_hospital16(&t); /* [0x4528] as a hospital loop leaves it (2.4) */
    SimIn in = rng_operator(&t, 3, NULL); /* hospital 3's first operator draw */
    CHECK(in.d == 0x61D9 && in.e == 0x16DE40u + 300 + 0x1F2236u);
    const uint8_t *kz = g_world.block[16] + 2 * 2400 + 240; /* hospital 2's Krankenzimmer 1 */
    CHECK((sim_address(&t, kz) & 0xFFFF) == 0x0648 && rng_operator(&t, 2, kz).d == 0x0648);
    CHECK(rng_scheduler(&t).e == 0x1F2236u && rng_waiting_rooms(&t) == 0x16E480u);
    p.build = &DE, p.edition = EDITION_GERMAN_CD;
    sim_init(&t, &g_world, &p, 7);
    rng_scratch_hospital16(&t);
    in = rng_operator(&t, 3, NULL);
    CHECK(in.d == 0x61D8 && in.e == 0x16DDB0u + 300 + 0x1F21A6u);
    CHECK((sim_address(&t, kz) & 0xFFFF) == ((0x18F208u + 4800 + 240) & 0xFFFF));
    rng_scratch_hospital16(&g_sim);
    in = rng_operator(&g_sim, 3, NULL); /* the disk (no build) */
    CHECK(in.d == 0x6187 && in.e == 0x16DDB0u + 300 + 0x1F21A6u);

    /* the sites on the English layout (the two constants set as sim_init sets them from the English build) */
    const uint32_t S = 0x90, EH16 = H16 + S;
#define EN_LAYOUT() (g_sim.rng_player = 0x61D9, g_sim.rng_heap = S, g_sim.rng_scratch = EH16)
    fresh();
    EN_LAYOUT();
    for (int i = 0; i < 5; i++) sim_news_item(&g_sim, i)[0] = 1;
    FILE *log = tmpfile();
    g_sim.rng_log = log;
    for (int i = 0; i < 20; i++) sim_news_gate_draw(&g_sim, 250, 90);
    g_sim.rng_log = NULL;
    rewind(log);
    g_nlogged = 0;
    Logged x;
    while (log && fscanf(log, "%X %c%u %u %X %X", &x.site, &x.kind, &x.n, &x.v, &x.d, &x.e) == 6) g_logged[g_nlogged++] = x;
    if (log) fclose(log);
    CHECK(site_inputs(0x42E10, 0, EH16) == 20); /* context S */
    /* the arrival: person tries chained through the shifted person records, the hospital step */
    patient_world();
    EN_LAYOUT();
    for (int i = 1; i < 1009; i++) {
        uint8_t *q = patient(i);
        q[1] = (uint8_t)(i & 1), q[41] = 3, q[44] = 60;
    }
    g_world.block[BLK_ROOM_COUNTS][15 * 100 + 2] = 1;
    put16(g_world.block[3] + 15 * 174 + 8, 156);
    int chained = 0, steps = 0;
    for (int i = 0; i < 100; i++) {
        logged_call(0x69CB6);
        unsigned d = 0;
        for (int k = 0; k < g_nlogged; k++) {
            const Logged *l = &g_logged[k];
            if (l->site == 0x69CE2) {
                CHECK(l->d == d && l->e == EH16);
                chained += d != 0;
                d = (0x1C4748u + S + 106u * (l->v + 1)) & 0xFFFF;
            } else if (l->site == 0x69DFF || l->site == 0x69E40 || l->site == 0x69E4B) {
                CHECK(l->d == (EH16 & 0xFFFF) && l->e == 0x16E3F0u + S + EH16);
                steps++;
            }
        }
        for (int k = 1; k < 1009; k++) patient(k)[3] = 0xFF, patient(k)[4] = 0;
        memset(g_world.block[3] + 15 * 174 + 10, 0, 16);
    }
    CHECK(chained > 0 && steps > 0);
    /* the offers' ageing: [0x4528] = hospital 15's second waiting room + 0x14, shifted */
    fresh();
    EN_LAYOUT();
    g_world.block[BLK_ROOM_COUNTS][15 * 100 + 3] = 2;
    logged_call(0x6BFC8);
    uint8_t *m = sim_market(&g_sim, 0);
    m[0] = 3, m[1] = 1, m[3] = 200;
    logged_call(0x3DAEB);
    CHECK(site_inputs(0x3DB3F, 0, 0x16E3F0u + S + 1800u * 15 + 180u + 0x14) == 1);
    /* no waiting room anywhere: the tick leaves [0x4528] as the staff pass left it, hospital record 16, shifted */
    g_world.block[BLK_ROOM_COUNTS][15 * 100 + 3] = 0;
    rng_scratch_hospital16(&g_sim);
    logged_call(0x6BFC8);
    m[0] = 3, m[3] = 200;
    logged_call(0x3DAEB);
    CHECK(site_inputs(0x3DB3F, 0, EH16) == 1);
    /* an operator (the Krankenzimmer 0x2995B): the room pick with the build's scratch block, EBP the shifted row */
    fresh();
    EN_LAYOUT();
    const int h = 15;
    world_set_tail(&g_world, TAIL_HOUR, 10);
    world_set_tail(&g_world, TAIL_HOURS, 51);
    world_set_cash(&g_world, h, 100000);
    uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + h * 100;
    c[2] = 1, c[6] = 2;
    put16(g_world.block[3] + h * 174 + 8, 156);
    for (int k = 0; k < 2; k++) {
        uint8_t *rec = g_world.block[16] + h * 2400 + k * 240;
        for (int f = 6; f <= 10; f += 2) put16(rec + f, 300 + 4 * k + f / 2);
        rec[10] = 10;
    }
    int picks = 0;
    for (int i = 0; i < 50; i++) {
        logged_call(0x2995B);
        CHECK(site_inputs(0x62D1F, 0, EH16) > 0); /* the picker, context S */
        picks += site_inputs(0x299A0, 0x61D9, ROW(h) + S + EH16);
    }
    CHECK(picks == 50);
#undef EN_LAYOUT
}

/* reads a draw log written since the file was opened into g_logged */
static void read_log(FILE *log) {
    g_nlogged = 0;
    if (!log) return;
    rewind(log);
    Logged x;
    while (g_nlogged < 200000 && fscanf(log, "%X %c%u %u %X %X", &x.site, &x.kind, &x.n, &x.v, &x.d, &x.e) == 6)
        g_logged[g_nlogged++] = x;
    fclose(log);
}

/* docs/sim-drift8.md section 14 ("What the engine must change" 2): context S takes the registers the game's clock call
 * passes. D = the player block 0x631B on every screen; EBP = the screen's script address (the screen-table entry:
 * office 1 0xE9B7, reception 19 0x1097F, desktop 97 0x15111; any other screen its entry), the site map (5) 0x552A
 * whatever its entry; while "skip one hour" runs, and in biing-save sim, D = lo(0x2F850) = 0xF850, EBP 0xB124. Per build:
 * the build's offsets, the build's screen table. Synthetic screen table, laid out like the disk build's. */
static void test_clock_registers(void) {
    static const struct {
        int screen;
        uint32_t entry, ebp;
    } CASES[] = {{1, 0xE9B7, 0xE9B7}, {19, 0x1097F, 0x1097F}, {97, 0x15111, 0x15111}, {5, 0x2222, 0x552A},
                 {42, 0x13579, 0x13579}};
    fresh();
    for (int i = 0; i < 5; i++) put32(g_image + 0xE72F + 4 * CASES[i].screen, (int32_t)CASES[i].entry);
    g_sim.rng_scratch = H16;
    sim_clock_registers(&g_sim); /* screen 0: none shown, the oracle's start registers */
    CHECK(g_sim.clock_d == 0 && g_sim.clock_ebp == 0);
    for (int i = 0; i < 5; i++) {
        g_sim.screen = CASES[i].screen;
        sim_clock_registers(&g_sim);
        SimIn in = rng_scheduler(&g_sim);
        CHECK(g_sim.clock_d == 0x631B && g_sim.clock_ebp == CASES[i].ebp);
        CHECK(in.d == 0x631B && in.e == CASES[i].ebp + H16);
        g_sim.skip_registers = 1; /* biing-save sim: the skip pair on every screen */
        sim_clock_registers(&g_sim);
        CHECK(rng_scheduler(&g_sim).d == 0xF850 && rng_scheduler(&g_sim).e == 0xB124u + H16);
        g_sim.skip_registers = 0;
        g_sim.fast_forward = 1; /* the skip loop itself */
        sim_clock_registers(&g_sim);
        CHECK(g_sim.clock_d == 0x2F850 && g_sim.clock_ebp == 0xB124);
        g_sim.fast_forward = 0;
    }
    /* the sites: the onset (desktop), the picker in an operator (reception), the newspaper gate (office); the offers'
     * ageing (context M) and the arrival's person tries take the clock's pair too (docs/rng-sites.md 2.5) */
    world_set_tail(&g_world, TAIL_DAY, 14);
    world_set_tail(&g_world, TAIL_HOUR, 10);
    world_set_tail(&g_world, TAIL_HOURS, 51);
    g_sim.screen = 97;
    logged_call(0x6953D);
    CHECK(site_inputs(0x695A8, 0x631B, 0x15111u + H16) == 1);
    g_sim.screen = 19;
    g_sim.rng_scratch = H16;
    logged_call(0x2CB75);
    CHECK(site_inputs(0x62D1F, 0x631B, 0x1097Fu + H16) > 0 && site_inputs(0x62D40, 0x631B, 0x1097Fu + H16) > 0);
    g_sim.screen = 1;
    sim_clock_registers(&g_sim);
    g_sim.rng_scratch = H16;
    for (int i = 0; i < 5; i++) sim_news_item(&g_sim, i)[0] = 1;
    FILE *log = tmpfile();
    g_sim.rng_log = log;
    for (int i = 0; i < 20; i++) sim_news_gate_draw(&g_sim, 250, 90);
    g_sim.rng_log = NULL;
    read_log(log);
    CHECK(site_inputs(0x42E10, 0x631B, 0xE9B7u + H16) == 20);
    g_world.block[BLK_ROOM_COUNTS][15 * 100 + 3] = 1;
    logged_call(0x6BFC8);
    uint8_t *m = sim_market(&g_sim, 0);
    m[0] = 3, m[1] = 1, m[3] = 200;
    /* context M (2.5): office D 0x631B / EBP 0xE9B7; the skip pair: D 0xF850, E = 0xB124 + V; no screen: 0 / 0 */
    const uint32_t VW = 0x16E3F0u + 1800u * 15 + 0x14;
    int market_drops = 0;
    for (int pass = 0; pass < 3; pass++) {
        g_sim.screen = pass == 2 ? 0 : 1;
        g_sim.skip_registers = pass == 1;
        const unsigned md = pass == 0 ? 0x631B : pass == 1 ? 0xF850 : 0;
        const uint32_t me = (pass == 0 ? 0xE9B7u : pass == 1 ? 0xB124u : 0u) + VW;
        for (int i = 0; i < 40; i++) {
            m[0] = 3, m[1] = 1, m[3] = 200;
            logged_call(0x3DAEB);
            CHECK(site_inputs(0x3DB3F, md, me) == 1);
            if (g_nlogged == 5) {
                market_drops++;
                CHECK(site_inputs(0x3DB4A, md, me) == 1 && site_inputs(0x3DB51, md, me) == 1);
                CHECK(site_inputs(0x3DB58, md, me) == 1 && site_inputs(0x3DB5F, md, me) == 1);
            }
        }
    }
    CHECK(market_drops > 3);
    g_sim.skip_registers = 0;
    /* the arrival's person tries (2.5): D the clock's at the first try, then lo(the previous pick's record); EBP the
     * clock's at every try (office 0xE9B7, skip 0xB124, no screen 0) */
    patient_world();
    put32(g_image + 0xE72F + 4, 0xE9B7);
    g_sim.rng_scratch = HOSP(7);
    for (int i = 1; i < 1009; i++) patient(i)[1] = (uint8_t)(i & 1), patient(i)[41] = 3, patient(i)[44] = 60;
    g_world.block[BLK_ROOM_COUNTS][15 * 100 + 2] = 1;
    put16(g_world.block[3] + 15 * 174 + 8, 156);
    for (int pass = 0; pass < 3; pass++) {
        g_sim.screen = pass == 2 ? 0 : 1;
        g_sim.skip_registers = pass == 1;
        const unsigned d0 = pass == 0 ? 0x631B : pass == 1 ? 0xF850 : 0;
        const uint32_t ebp = pass == 0 ? 0xE9B7u : pass == 1 ? 0xB124u : 0u;
        int first = 0, later = 0;
        for (int i = 0; i < 20; i++) {
            logged_call(0x69CB6);
            unsigned d = d0;
            int tries = 0;
            for (int k = 0; k < g_nlogged; k++)
                if (g_logged[k].site == 0x69CE2) {
                    CHECK(g_logged[k].d == d && g_logged[k].e == ebp + HOSP(7));
                    if (tries++) later++;
                    else first++;
                    d = (0x1C4748u + 106u * (g_logged[k].v + 1)) & 0xFFFF;
                }
            for (int k = 1; k < 1009; k++) patient(k)[3] = 0xFF, patient(k)[4] = 0;
            memset(g_world.block[3] + 15 * 174 + 10, 0, 16);
        }
        CHECK(first == 20 && later > 0);
    }
    g_sim.skip_registers = 0;

    /* the skip loop from the desktop: every context-S draw of the hour with the skip pair, the desktop's pair after */
    fresh();
    put32(g_image + 0xE72F + 4 * 97, 0x15111);
    g_sim.screen = 97;
    world_set_tail(&g_world, TAIL_DAY, 14);
    world_set_tail(&g_world, TAIL_HOUR, 10);
    world_set_tail(&g_world, TAIL_HOURS, 51);
    log = tmpfile();
    g_sim.rng_log = log;
    sim_skip_hour(&g_sim, NULL, NULL);
    g_sim.rng_log = NULL;
    read_log(log);
    int skip_s = 0, other = 0;
    for (int k = 0; k < g_nlogged; k++) {
        unsigned st = g_logged[k].site;
        if (st != 0x62D1F && st != 0x62D40 && st != 0x695A8 && st != 0x42E10) continue;
        if (g_logged[k].d == 0xF850 && g_logged[k].e >= 0xB124u && (g_logged[k].e == 0xB124u || g_logged[k].e - 0xB124u == H16 ||
                                                                     g_logged[k].e - 0xB124u == HOSP(15)))
            skip_s++;
        else
            other++, fprintf(stderr, "  skip: site %X inputs %X %X\n", st, g_logged[k].d, g_logged[k].e);
    }
    CHECK(skip_s > 0 && other == 0);
    sim_tick(&g_sim);
    CHECK(g_sim.fast_forward == 0 && g_sim.clock_d == 0x631B && g_sim.clock_ebp == 0x15111);

    /* per build: a synthetic build that moves everything from 0x5000 on by 0x52 (the English CD's shift at 0x631B and
     * 0x552A); its screen table at 0xE781 holds the build's own script addresses */
    static const ProgramSegment SEG[] = {{0, 0x5000, 0}, {0x5000, 0x40000, 0x5052}};
    static const ProgramBuild EN = {"GOG English CD", 0, 0, 0, "T", "D", 0, SEG, 2, NULL, 0, NULL, 0, 0x90};
    static Sim t;
    fresh();
    Program p = g_prog;
    p.build = &EN, p.edition = EDITION_ENGLISH_CD;
    put32(g_image + 0xE781 + 4 * 97, 0x15163);
    sim_init(&t, &g_world, &p, 7);
    t.screen = 97;
    sim_clock_registers(&t);
    CHECK(t.clock_d == 0x636D && t.clock_ebp == 0x15163);
    t.screen = 5;
    sim_clock_registers(&t);
    CHECK(t.clock_d == 0x636D && t.clock_ebp == 0x557C);
    t.fast_forward = 1;
    sim_clock_registers(&t);
    CHECK((t.clock_d & 0xFFFF) == 0xF8A2 && t.clock_ebp == 0xB176 && rng_scheduler(&t).d == 0xF8A2);
}

/* SIM-89, docs/sim-rooms2.md: the saved-block notification on treatment worsening. */
static uint8_t sim89_expected[2][500];
static jmp_buf sim89_draw_boundary;

static void sim89_players(int h, int flag, int watched, uint8_t old, int notify) {
    for (int player = 0; player < 2; player++) {
        uint8_t *pb = g_world.block[BLK_PLAYER0 + player];
        CHECK(g_world.size[BLK_PLAYER0 + player] == sizeof sim89_expected[player]);
        memset(pb, 0x62 + player, sizeof sim89_expected[player]);
        int matching = h <= 1 && player != h ? !watched : watched;
        put16(pb + 0x2C, matching ? 261 : 517);
        pb[flag] = old;
        memcpy(sim89_expected[player], pb, sizeof sim89_expected[player]);
    }
    if (notify && watched && h <= 1) sim89_expected[h][flag] = 0xFF;
}

static void sim89_check_players(void) {
    for (int player = 0; player < 2; player++)
        CHECK(memcmp(g_world.block[BLK_PLAYER0 + player], sim89_expected[player], sizeof sim89_expected[player]) == 0);
}

/* The existing exhaustion hook stops exactly before the first loss draw. This makes the notification's ordering
 * observable without a new production hook or a draw callback that supplies behavior absent from the game. */
static void sim89_first_loss(const SimScript *script, const char *message) {
    (void)message;
    CHECK(script->used == script->n);
    sim89_check_players();
    longjmp(sim89_draw_boundary, 1);
}

static void test_treatment_worsening_notification(void) {
    static const int FLAGS[] = {0x31, 0x34, 0x37, 0x3E, 0x42, 0x9F};
    static const int HOSPITALS[] = {0, 1, 2, 15}, HEALTH[] = {24, 25, 50, 79, 80, 128};
    static const uint8_t OLD[] = {0, 55, 255};
    for (int f = 0; f < 6; f++)
        for (int hc = 0; hc < 4; hc++)
            for (int watched = 0; watched < 2; watched++)
                for (int health = 0; health < 6; health++)
                    for (int gate = 0; gate < 2; gate++)
                        for (int old = 0; old < 3; old++) {
                            fresh();
                            int h = HOSPITALS[hc], active = gate && HEALTH[health] >= 25 && HEALTH[health] <= 79;
                            uint8_t *p = patient(261);
                            p[3] = 7; /* current hospital decides, not this deliberately different patient field */
                            p[44] = (uint8_t)HEALTH[health];
                            put16(p + 94, 700);
                            sim89_players(h, FLAGS[f], watched, OLD[old], active);
                            uint32_t raw[] = {gate ? 4 : 3, 0, 0, 0, 0};
                            SimScript script = {.v = raw, .n = sizeof raw / sizeof *raw};
                            g_sim.rng_script = &script;
                            sim_worsen_in_treatment(&g_sim, h, p, FLAGS[f]);
                            g_sim.rng_script = NULL;
                            CHECK(script.used == (size_t)(active ? 5 : 1) && script.extra == 0);
                            CHECK(p[44] == HEALTH[health]); /* notification also on a zero net health change */
                            sim89_check_players();
                        }
}

/* Complete calls through each real scheduled routine, plus a stop at the first loss draw to check ordering.
 * Script prefixes select only the worsening path; no staff, meal, recovery or equipment side effects are needed. */
static void test_treatment_notification_callers(void) {
    static const struct { int cat, flag; uint32_t routine; size_t draws; } CASES[] = {
        {5, 0x31, 0x6AD3C, 41}, {6, 0x34, 0x6A0BA, 7}, {16, 0x37, 0x6C3D5, 8},
        {7, 0x3E, 0x58045, 8}, {9, 0x42, 0x6FA5A, 7}, {8, 0x9F, 0x597F9, 8}};
    for (int c = 0; c < 6; c++)
        for (int h = 0; h < 2; h++)
            for (int stop_at_loss = 0; stop_at_loss < 2; stop_at_loss++) {
                fresh();
                int cat = CASES[c].cat;
                g_world.block[BLK_ROOM_COUNTS][100 * h + cat] = 1;
                uint8_t *rec = sim_room_record(&g_sim, h, cat, 0), *p = patient(261);
                p[3] = (uint8_t)h, p[4] = (uint8_t)cat, p[44] = 50, p[100] = 2;
                put16(p + 94, 700);
                if (cat == 6 || cat == 16) {
                    rec[10] = 10, rec[12] = 1, put16(rec + 0x14, 261);
                } else {
                    put16(rec + 12, 261);
                    if (cat == 5) put16(rec + 8, 7), put16(rec + 10, 8);
                    if (cat == 7 || cat == 8) put16(rec + 14, 999);
                }
                sim89_players(h, CASES[c].flag, 1, 55, 1);
                uint32_t raw[64];
                for (size_t i = 0; i < sizeof raw / sizeof *raw; i++) raw[i] = 1;
                size_t prefix = cat == 5 ? (size_t)(6 + 2 * h) : cat == 7 || cat == 8 ? 4 : 3;
                if (cat == 7 || cat == 8) raw[1] = 3; /* waiting-stage gate */
                raw[prefix - 1] = 4;
                for (size_t i = 0; i < 4; i++) raw[prefix + i] = 0;
                SimScript script = {.v = raw, .n = stop_at_loss ? prefix : sizeof raw / sizeof *raw};
                g_sim.rng_script = &script;
                if (stop_at_loss) {
                    g_sim.rng_failure = sim89_first_loss;
                    if (!setjmp(sim89_draw_boundary)) {
                        sim_call(&g_sim, CASES[c].routine);
                        CHECK(0); /* the first loss draw must reach the exhaustion hook */
                    }
                    g_sim.rng_failure = NULL;
                } else {
                    CHECK(sim_call(&g_sim, CASES[c].routine));
                    CHECK(script.used == CASES[c].draws && script.extra == 0);
                    CHECK(p[44] == 50);
                    sim89_check_players();
                }
                g_sim.rng_script = NULL;
            }
}

/* SIM-90: whole saved blocks, including independent death/mood flags. Direct helper matrices isolate the
 * eligibility boundaries; scheduled callers below prove each supplied offset rather than testing only a helper. */
static void test_general_worsening_notifications(void) {
    static const int FLAGS[] = {0x2F, 0x30, 0x31, 0x34, 0x39, 0x3E, 0x40, 0x42, 0x9F};
    static const int HOSPITALS[] = {0, 1, 2, 15}, HEALTH[] = {1, 50, 99, 100};
    static const uint8_t OLD[] = {0, 55, 255};
    for (int f = 0; f < 9; f++)
        for (int hc = 0; hc < 4; hc++)
            for (int watched = 0; watched < 2; watched++)
                for (int health = 0; health < 4; health++)
                    for (int gate = 0; gate < 2; gate++)
                        for (int ill = 0; ill < 2; ill++)
                            for (int old = 0; old < 3; old++) {
                                fresh();
                                int h = HOSPITALS[hc], active = gate && ill && HEALTH[health] < 100;
                                uint8_t *p = patient(261);
                                p[3] = 7, p[4] = 5, p[44] = (uint8_t)HEALTH[health], p[56] = ill ? 7 : 0;
                                g_image[0x954 + 7 * 20 + 9] = 1; /* loss one, no low-health bonus */
                                sim89_players(h, FLAGS[f], watched, OLD[old], active);
                                if (active && HEALTH[health] == 1 && h <= 1) sim89_expected[h][0x46] = 0xFF;
                                uint32_t raw[] = {gate ? 11 : 10, 0};
                                SimScript script = {.v = raw, .n = sizeof raw / sizeof *raw};
                                g_sim.rng_script = &script;
                                sim_worsen(&g_sim, h, p, FLAGS[f]);
                                g_sim.rng_script = NULL;
                                CHECK(script.used == (size_t)(active ? 2 : 1) && script.extra == 0);
                                CHECK(p[44] == HEALTH[health] - active && p[104] == (active && HEALTH[health] == 1));
                                sim89_check_players();
                            }
}

static void test_nurse_health_notifications(void) {
    static const int FLAGS[] = {0x34, 0x39, 0x37, 0x42}, HOSPITALS[] = {0, 1, 2, 15};
    static const int HEALTH[] = {0, 50, 89, 90, 100};
    static const uint8_t OLD[] = {0, 55, 255};
    enum { PASS, NO_COMFORT, NO_RECOVERY, EARLY, LATE, FIRST_MISS, SECOND_MISS, FIRST_HOUR, LAST_HOUR, CONTROLS };
    for (int f = 0; f < 4; f++)
        for (int hc = 0; hc < 4; hc++)
            for (int watched = 0; watched < 2; watched++)
                for (int health = 0; health < 5; health++)
                    for (int control = 0; control < CONTROLS; control++)
                        for (int old = 0; old < 3; old++) {
                            fresh();
                            int h = HOSPITALS[hc], passed = control == PASS || control >= FIRST_HOUR;
                            uint8_t *p = patient(261), room[32] = {0};
                            p[3] = 7, p[44] = (uint8_t)HEALTH[health], p[102] = control != NO_RECOVERY;
                            p[78] = 100;
                            memset(p + 20, 100, 16); /* mood/opinion cannot add +0x49 */
                            put16(room + 8, 7), g_world.block[BLK_STAFF][7 * 32 + 5] = 70;
                            world_set_tail(&g_world, TAIL_HOUR, control == EARLY ? 4 : control == LATE ? 21 :
                                           control == FIRST_HOUR ? 5 : control == LAST_HOUR ? 20 : 10);
                            sim89_players(h, FLAGS[f], watched, OLD[old], passed && HEALTH[health] < 90);
                            uint32_t raw[5] = {0};
                            if (control == FIRST_MISS) raw[0] = 10;
                            if (control == SECOND_MISS) raw[1] = 10;
                            SimScript script = {.v = raw, .n = sizeof raw / sizeof *raw};
                            g_sim.rng_script = &script;
                            sim_nurse_care(&g_sim, h, p, room, control != NO_COMFORT, FLAGS[f]);
                            g_sim.rng_script = NULL;
                            size_t expected_draws = passed ? 5 : control == FIRST_MISS ? 1 : control == SECOND_MISS ? 2 : 0;
                            CHECK(script.used == expected_draws && script.extra == 0);
                            CHECK(p[44] == HEALTH[health] + (passed && HEALTH[health] < 90 ? 3 : 0));
                            sim89_check_players();
                        }
    /* A raised mood still notifies an unwatched player independently of the health flag. */
    for (int control = 0; control < 4; control++) {
        fresh();
        int h = control / 2, health = 89 + control % 2;
        uint8_t *p = patient(261), room[32] = {0};
        p[3] = (uint8_t)h, p[44] = (uint8_t)health, p[102] = 1, p[78] = 50;
        put16(room + 8, 7), g_world.block[BLK_STAFF][7 * 32 + 5] = 70;
        world_set_tail(&g_world, TAIL_HOUR, 10);
        sim89_players(h, 0x42, 0, 55, 1);
        sim89_expected[h][0x49] = 0xFF;
        uint32_t raw[5] = {0};
        SimScript script = {.v = raw, .n = sizeof raw / sizeof *raw};
        g_sim.rng_script = &script;
        sim_nurse_care(&g_sim, h, p, room, 1, 0x42);
        g_sim.rng_script = NULL;
        CHECK(script.used == 5 && script.extra == 0 && p[44] == (health < 90 ? 92 : 90) && p[78] == 51);
        sim89_check_players();
    }
}

static uint8_t *notification_room(int cat, int h, int care) {
    fresh();
    world_set_tail(&g_world, TAIL_HOUR, 10);
    g_world.block[BLK_ROOM_COUNTS][h * 100 + cat] = 1;
    uint8_t *rec = sim_room_record(&g_sim, h, cat, 0), *p = patient(261);
    p[3] = (uint8_t)h, p[4] = (uint8_t)cat, p[44] = 50, p[56] = 7, p[78] = 100;
    p[100] = care ? 2 : 0;
    memset(p + 20, 100, 16);
    put16(p + 94, 700);
    g_image[0x954 + 7 * 20 + 9] = 1;
    if (cat == 2) put16(rec + 10, 261), put16(rec + 12, 40);
    else if (cat == 3 || cat == 6 || cat == 16) {
        rec[10] = 10, rec[12] = 1, put16(rec + 0x14, 261);
        if (cat == 3) put16(rec + 0x16, 20);
    } else put16(rec + 12, 261);
    if (cat == 5 || cat == 11 || cat == 15) put16(rec + 8, 7), put16(rec + 10, 8);
    if (cat == 7 || cat == 8) put16(rec + 14, 999);
    if (care) {
        put16(rec + 8, 7), rec[cat == 11 ? 0x19 : 0x0F] = 20;
        g_world.block[BLK_STAFF][7 * 32 + 5] = 255;
        p[102] = 2;
    }
    return p;
}

static void test_sim90_notification_callers(void) {
    static const struct { int cat, flag, care; uint32_t routine; size_t draws; } CASES[] = {
        {2, 0x2F, 0, 0x67611, 36}, {3, 0x30, 0, 0x6BFC8, 37}, {5, 0x31, 0, 0x6AD3C, 38},
        {6, 0x34, 0, 0x6A0BA, 6}, {11, 0x39, 0, 0x6B445, 5}, {7, 0x3E, 0, 0x58045, 5},
        {15, 0x40, 0, 0x6BAE4, 5}, {9, 0x42, 0, 0x6FA5A, 4}, {8, 0x9F, 0, 0x597F9, 5},
        {6, 0x34, 1, 0x6A0BA, 10}, {11, 0x39, 1, 0x6B445, 9},
        {16, 0x37, 1, 0x6C3D5, 11}, {9, 0x42, 1, 0x6FA5A, 11}};
    static const int HOSPITALS[] = {0, 1, 2, 15};
    static const uint8_t OLD[] = {0, 55, 255};
    for (size_t c = 0; c < sizeof CASES / sizeof *CASES; c++)
        for (int hc = 0; hc < 4; hc++)
            for (int watched = 0; watched < 2; watched++)
                for (int old = 0; old < 3; old++) {
                    int h = HOSPITALS[hc], care = CASES[c].care;
                    uint8_t *p = notification_room(CASES[c].cat, h, care);
                    sim89_players(h, CASES[c].flag, watched, OLD[old], 1);
                    uint32_t raw[64];
                    for (size_t i = 0; i < sizeof raw / sizeof *raw; i++) raw[i] = care ? 127 : 11;
                    SimScript script = {.v = raw, .n = sizeof raw / sizeof *raw};
                    g_sim.rng_script = &script;
                    CHECK(sim_call(&g_sim, CASES[c].routine));
                    g_sim.rng_script = NULL;
                    CHECK(script.used == CASES[c].draws && script.extra == 0);
                    CHECK(p[44] == (care ? 53 : 46));
                    sim89_check_players();
                }
}

/* SIM-92: use the real ward ticks with no treatment, round or care active. Recovery needs no nurse. */
static void test_ward_recovery_notifications(void) {
    static const int HOSPITALS[] = {0, 1, 2, 15};
    static const uint8_t OLD[] = {0, 55, 255};
    for (int neuro = 0; neuro < 2; neuro++)
        for (int hc = 0; hc < 4; hc++)
            for (int watched = 0; watched < 2; watched++)
                for (int recovery = 0; recovery < 3; recovery++)
                    for (int draw = 0; draw < 2; draw++)
                        for (int nurse = 0; nurse < 2; nurse++)
                            for (int old = 0; old < 3; old++)
                                for (int zero_step = 0; zero_step < 2; zero_step++) {
                                    int h = HOSPITALS[hc], cat = neuro ? 16 : 6;
                                    int progress = neuro ? 0x38 : 0x35, health = neuro ? 0x37 : 0x34;
                                    int complete = recovery == 1 && draw == 1, step = zero_step ? 0 : 5;
                                    uint8_t *p = notification_room(cat, h, 0);
                                    uint8_t *ward = sim_room_record(&g_sim, h, cat, 0);
                                    p[44] = 95, p[45] = (uint8_t)step, p[56] = 0, p[100] = 2, p[102] = (uint8_t)recovery;
                                    put16(ward + 8, nurse ? 7 : 0);
                                    sim89_players(h, progress, watched, OLD[old], complete);
                                    for (int player = 0; player < 2; player++) {
                                        g_world.block[BLK_PLAYER0 + player][health] = OLD[old];
                                        sim89_expected[player][health] = OLD[old];
                                    }
                                    if (complete && watched && h <= 1) sim89_expected[h][health] = 0xFF;
                                    uint32_t raw[8];
                                    for (size_t i = 0; i < sizeof raw / sizeof *raw; i++) raw[i] = (uint32_t)draw;
                                    SimScript script = {.v = raw, .n = sizeof raw / sizeof *raw};
                                    g_sim.rng_script = &script;
                                    CHECK(sim_call(&g_sim, neuro ? 0x6C3D5 : 0x6A0BA));
                                    g_sim.rng_script = NULL;
                                    CHECK(script.used == (size_t)(3 + !draw + nurse + (recovery != 0) + neuro));
                                    CHECK(script.extra == 0);
                                    CHECK(p[102] == recovery - (recovery ? draw : 0));
                                    CHECK(p[45] == (uint8_t)(step - complete) && p[44] == 95);
                                    sim89_check_players();
                                }
}

static int sim90_boundary_health;
static void sim90_observe_boundary(const SimScript *script, const char *message) {
    CHECK(patient(261)[44] == sim90_boundary_health);
    sim89_first_loss(script, message); /* checks both blocks and exits through the existing longjmp boundary */
}

static void test_sim90_notification_order(void) {
    for (int h = 0; h < 2; h++)
        for (int care = 0; care < 2; care++) {
            fresh();
            world_set_tail(&g_world, TAIL_HOUR, 10);
            uint8_t *p = patient(261), room[32] = {0};
            p[3] = (uint8_t)h, p[44] = 50, p[56] = 7, p[102] = 1, p[78] = 100;
            g_image[0x954 + 7 * 20] = 2; /* range two forces a raw draw at the first loss boundary */
            memset(p + 20, 100, 16);
            put16(room + 8, 7), g_world.block[BLK_STAFF][7 * 32 + 5] = 70;
            sim89_players(h, 0x34, 1, 55, 1);
            uint32_t raw[] = {care ? 0 : 11, 0, 0, 0};
            SimScript script = {.v = raw, .n = care ? 4 : 1};
            g_sim.rng_script = &script;
            g_sim.rng_failure = sim90_observe_boundary;
            sim90_boundary_health = care ? 53 : 50;
            if (!setjmp(sim89_draw_boundary)) {
                if (care) sim_nurse_care(&g_sim, h, p, room, 1, 0x34);
                else sim_worsen(&g_sim, h, p, 0x34);
                CHECK(0); /* loss draw / first post-health opinion draw must reach the observation boundary */
            }
            g_sim.rng_failure = NULL;
            g_sim.rng_script = NULL;
        }
}

/* The bed loops (context B, 3.9-3.11): in the Krankenzimmer tick every draw of a bed's patient has D = the ward's record
 * and E = the person record + the ward's record + 0x14; the care drift's nurse bonus E = the row of h + record + 0x14.
 * The neuro ward's wants (3.12): D = 0 until an r15 comes up 7, then the nurse's staff record; E = the row + hospital
 * record 16. */
static void test_bed_inputs(void) {
    uint8_t *ward = ward_world(6);
    patient(5)[44] = 60;
    const uint32_t w = 0x18F208u + 2400u * 15, person = 0x1C4748u + 106u * 5;
    int decline = 0, worsen = 0, drop = 0, bonus = 0;
    for (int i = 0; i < 300; i++) {
        patient(5)[44] = 60, patient(5)[104] = 0, put16(ward + 0x14, 5), ward[12] = 1;
        logged_call(0x6A0BA);
        int a = site_inputs(0x30741, w, person + w + 0x14), b = site_inputs(0x2E8CB, w, person + w + 0x14);
        int c = site_inputs(0x2ECA7, w, person + w + 0x14), d = site_inputs(0x2EB06, w, ROW(15) + w + 0x14);
        CHECK(a >= 0 && b >= 0 && c >= 0 && d >= 0);
        decline += a > 0, worsen += b > 0, drop += c > 0, bonus += d > 0;
    }
    CHECK(decline > 0 && worsen > 0 && drop > 0 && bonus > 0);
    /* the worsening's arithmetic over its logged draws (3.10): health - (r7 + r7 - r7 - r7), the second r7 added */
    int checked = 0;
    for (int i = 0; i < 4000; i++) {
        uint8_t *p = patient(5);
        p[44] = 60, p[104] = 0, put16(p + 94, 800); /* blood (field 94) 800: no r3 */
        sim_seed(&g_sim, 7000u + (uint64_t)i);
        FILE *log = tmpfile();
        if (!log) break;
        g_sim.rng_log = log;
        SimIn old = sim_set_inputs(&g_sim, sim_in(0x1234, 0x5678, 0));
        sim_worsen_in_treatment(&g_sim, 15, p, 0x34);
        sim_set_inputs(&g_sim, old);
        g_sim.rng_log = NULL;
        rewind(log);
        unsigned v[5] = {0}, site, n, x, in_d, in_e;
        char kind;
        int k = 0;
        while (k < 5 && fscanf(log, "%X %c%u %u %X %X", &site, &kind, &n, &x, &in_d, &in_e) == 6) v[k++] = x;
        fclose(log);
        if (k == 5) {
            CHECK(p[44] == 60 - ((int)v[1] + (int)v[2] - (int)v[3] - (int)v[4]));
            checked++;
        } else {
            CHECK(k == 1 && v[0] != 4 && p[44] == 60);
        }
    }
    CHECK(checked > 100);
    /* the neuro ward's wants: two wards of hospital 4 with nurses 40 and 41; within one call D starts at 0 and after an
     * r15 = 7 is the low word of that ward's nurse's record, for the draws that follow it and the next ward's r15; the
     * second pass on the English CD's heap layout (OTH-DE-14: every heap address + 0x90) */
    for (unsigned S = 0; S <= 0x90; S += 0x90) {
    fresh();
    g_sim.rng_heap = S;
    g_sim.rng_scratch = HOSP(7) + S; /* [0x4528]: what its last writer left (2.4), here hospital record 7 */
    room_descriptions();
    for (int k = 0; k < 2; k++) {
        uint8_t *nw = beds_ward(4, 16, k, 4);
        put16(nw + 8, 40 + k);
        put_slot(4, k, 40 + k, 1, 16);
    }
    int zero = 0, switched = 0, followers = 0;
    for (int i = 0; i < 600; i++) {
        g_world.block[BLK_STAFF][40 * 32 + 21] = 50, g_world.block[BLK_STAFF][41 * 32 + 21] = 50;
        put16(sim_room_record(&g_sim, 4, 16, 0) + 8, 40), put16(sim_room_record(&g_sim, 4, 16, 1) + 8, 41);
        logged_call(0x536D1);
        unsigned cur = 0;
        int nth = 0;
        for (int k = 0; k < g_nlogged; k++) {
            const Logged *l = &g_logged[k];
            CHECK(l->d == cur && l->e == ROW(4) + HOSP(7) + 2 * S);
            if (l->site == 0x53762) {
                zero += cur == 0, switched += cur != 0;
                if (l->v == 7) cur = (0x1DE912u + S + 32u * (40u + (unsigned)nth)) & 0xFFFF;
                nth++;
            } else {
                followers++;
            }
        }
    }
    CHECK(zero > 0 && switched > 0 && followers > 0);
    }
}

/* ---- docs/sim-drift6.md: the ward ticks' setback, recovery, cure check and decline, the championship gate, the
 * single records' nurse bonus site ------------------------------------------------------------------------------ */

static int logged_count(unsigned site, int value) { /* value -1: every draw of the site */
    int k = 0;
    for (int i = 0; i < g_nlogged; i++) k += g_logged[i].site == site && (value < 0 || (int)g_logged[i].v == value);
    return k;
}

/* Section 2: in the Neuro-Krankenzimmer every bed patient reaching the round check with field 96 below 3 draws the r63
 * (0x6CBAE), whatever the wish, illness or treatment; on 51 health - (r7 + 2). Field 96 3 or more, and the
 * Krankenzimmer at any field 96, draw nothing there. No nurse and health 98: nothing else moves the health. The setback
 * applies only when r7 + 2 is below the health: from health 4 (3 after a decline) it never kills. */
static void test_neuro_setback(void) {
    for (int k = 0; k < 7; k++) {
        int cat = k == 5 ? 6 : 16, calls = k == 6 ? 3200 : 1600, draws = 0, hits = 0, setbacks = 0, deep = 0;
        uint8_t *ward = ward_world(cat), *p = patient(5);
        put16(ward + 8, 0);
        for (int i = 0; i < calls; i++) {
            p[44] = k == 6 ? 4 : 98, p[93] = 0, p[104] = 0, p[40] = 0, p[57] = 0, put16(p + 56, 7), p[43] = 0;
            p[96] = (uint8_t)(k == 3 ? 3 : k == 4 ? 5 : k >= 5 ? 0 : k);
            if (k == 1) p[57] = 4;                  /* a wish */
            if (k == 2) put16(p + 56, 0), p[43] = 30; /* no illness, a treatment */
            put16(ward + 0x14, 5), ward[12] = 1;
            logged_call(cat == 6 ? 0x6A0BA : 0x6C3D5);
            int d = logged_count(0x6CBAE, -1), h = logged_count(0x6CBAE, 51);
            draws += d, hits += h;
            if (k == 6) {
                CHECK(!p[104] && p[44] >= 1 && p[44] <= 4);
                continue;
            }
            if (p[44] < 98) {
                setbacks++;
                CHECK(h == 1 && p[44] >= 89 && p[44] <= 96);
                deep += p[44] <= 92; /* r7 + 2 of 6 or more */
            }
        }
        if (k == 6) CHECK(draws == calls && hits >= 20);
        else if (k < 3) CHECK(draws == calls && hits == setbacks && hits >= 8 && hits <= 45 && deep > 0);
        else CHECK(draws == 0 && setbacks == 0);
    }
}

/* Section 3, item 2: the cure check (health 100 and a case: the case is cleared) only after a heal or for "other
 * treatment"; not at field 96 below 2, not with a wish, not after the neuro ward's setback draw that missed. A heal by I
 * byte 11 needs r31 = 0, by I byte 12 (field 50 = 0) r63 = 0. Health 100, no step running, no nurse; illness 7's byte
 * 7 = 5 keeps a healed patient at 100. Case 7: in a human hospital a heal that wraps health to 0 (byte 7 = 120) sets
 * field 104 and ends without the cure check; case 8, the same in an AI hospital: the death check (0x30766) also clears
 * the bed. */
static void test_ward_cure_paths(void) {
    for (int k = 0; k < 9; k++) {
        int cat = k == 5 || k == 6 ? 16 : 6, trials = k >= 4 ? 640 : 40, cured = 0, dead = 0;
        for (int i = 0; i < trials; i++) {
            uint8_t *ward = i == 0 ? ward_world(cat) : sim_room_record(&g_sim, 15, cat, 0), *p = patient(5);
            g_image[0x954 + 7 * 20 + 7] = (uint8_t)(k >= 7 ? 120 : 5);
            world_hospital(&g_world, 15)[0x14] = (uint8_t)(k == 7);
            put16(ward + 8, 0), put16(ward + 0x14, 5), ward[12] = 1;
            p[44] = 100, p[93] = 0, p[104] = 0, p[40] = 0, p[50] = 0, put16(p + 56, 7), p[3] = 15, p[4] = (uint8_t)cat;
            static const uint8_t F96[9] = {0, 2, 2, 2, 2, 3, 0, 2, 2}, T[9] = {33, 33, 33, 0, 30, 31, 33, 30, 30};
            p[96] = F96[k], p[43] = T[k];
            if (k == 1) p[57] = 4;
            int deaths = get16(world_hospital(&g_world, 15) + 0x38);
            sim_rooms_run(&g_sim, cat == 6 ? 0x6A0BA : 0x6C3D5);
            if (k == 8) { /* died (0x31299): counted at +0x38, the person reset at home */
                int died = get16(world_hospital(&g_world, 15) + 0x38) != deaths;
                dead += died;
                if (died) CHECK(get16(ward + 0x14) == 0 && ward[12] == 0);
                else CHECK(get16(ward + 0x14) == 5 && p[44] == 100 && get16(p + 56) == 7);
                continue;
            }
            cured += get16(p + 56) == 0, dead += p[104] != 0;
            if (k == 7) CHECK(p[104] ? p[44] == 0 : p[44] == 100);
        }
        if (k >= 7) CHECK(cured == 0 && dead >= 3 && dead <= 50);
        else if (k < 2 || k == 6) CHECK(cured == 0);
        else if (k < 4) CHECK(cured == trials);
        else CHECK(cured >= 3 && cured <= 30); /* 1 in 32 / 1 in 64 */
    }
}

/* Section 3, item 1: recovery (field 102) still non-zero lets the bed go on with the step part (the stepped
 * treatment's countdown runs) in both wards; in the Krankenzimmer it skips the r7 = 3 worsening only. */
static void test_ward_recovery(void) {
    for (int cat = 6; cat <= 16; cat += 10) {
        ward_world(cat);
        uint8_t *p = patient(5);
        uint8_t *t = g_image + 0x270C + 30 * 38;
        t[36] = 1, t[16] = 3;
        p[43] = 30, p[93] = 0xFF, p[45] = 2, p[105] = 3, p[102] = 50;
        sim_rooms_run(&g_sim, cat == 6 ? 0x6A0BA : 0x6C3D5);
        CHECK(p[105] == 2 && p[102] >= 49 && p[102] <= 50 && p[45] == 2);
        /* reaching 0: field 45 - 1 */
        int reached = 0;
        for (int i = 0; i < 40; i++) {
            p[102] = 1, p[45] = 2, p[105] = 3;
            sim_rooms_run(&g_sim, cat == 6 ? 0x6A0BA : 0x6C3D5);
            CHECK(p[45] == (p[102] ? 2 : 1));
            reached += !p[102];
        }
        CHECK(reached >= 5 && reached <= 35);
    }
    for (int recovering = 0; recovering <= 1; recovering++) {
        uint8_t *ward = ward_world(6), *p = patient(5);
        put16(ward + 8, 0);
        int drops = 0;
        for (int i = 0; i < 5120; i++) {
            p[44] = 99, p[100] = 0, p[96] = 0, p[93] = 0, p[104] = 0, p[40] = 0, p[102] = (uint8_t)(recovering * 50);
            put16(ward + 0x14, 5), ward[12] = 1;
            sim_rooms_run(&g_sim, 0x6A0BA);
            drops += p[44] < 99;
        }
        if (recovering) CHECK(drops == 0);
        else CHECK(drops >= 5 && drops <= 45); /* r7 = 3 and r31 = 11: 1 in 256 */
    }
}

/* Section 3, item 3: the decline (0x30741, r31 = 12) draws one r3: health - (r3 + 1), at least 3, so from health 4 a hit
 * always gives 3. */
static void test_decline_one_r3(void) {
    uint8_t *ward = ward_world(6), *p = patient(5);
    put16(ward + 8, 0);
    int hits = 0;
    for (int i = 0; i < 1280; i++) {
        p[44] = 4, p[96] = 0, p[93] = 0, p[104] = 0, p[40] = 0;
        put16(ward + 0x14, 5), ward[12] = 1;
        logged_call(0x6A0BA);
        int hit = logged_count(0x30741, 12);
        hits += hit;
        CHECK(p[44] == (hit ? 3 : 4));
    }
    CHECK(hits >= 15 && hits <= 70);
}

/* Section 4: the championship generators draw the newspaper gate (0x42E10) before they test the race word, so they draw
 * during a race too and write nothing. */
static void test_contest_gate_first(void) {
    for (int race = 0; race <= 1; race++)
        for (int r = 0; r < 2; r++) {
            fresh();
            for (int i = 0; i < 10; i++) sim_news_item(&g_sim, i)[0] = 2; /* 5 or more items: the gate draws */
            put16(g_world.block[BLK_34], race);
            int draws = 0;
            for (int i = 0; i < 100; i++) {
                logged_call(r ? 0x5176C : 0x515CB);
                draws += logged_count(0x42E10, -1);
            }
            int n = 0;
            for (int i = 0; i < 100; i++) n += sim_news_item(&g_sim, i)[0] != 0;
            CHECK(draws == 100 && n == 10);
        }
}

/* Section 1: the single records' care drift (0x2EB8D, here the treatment room) logs its nurse bonus r15 under 0x2EBBC,
 * once per staffed call, never under the bed loops' 0x2EB06. */
static void test_single_nurse_bonus_site(void) {
    patient_world();
    staff_rooms(15, 1);
    uint8_t *tr = g_world.block[15] + 15 * 1800, *p = patient(5);
    int own = 0, beds = 0;
    for (int i = 0; i < 50; i++) {
        put16(tr + 12, 5), p[3] = 15, p[104] = 0;
        logged_call(0x6AD3C);
        own += logged_count(0x2EBBC, -1), beds += logged_count(0x2EB06, -1);
    }
    CHECK(own == 50 && beds == 0);
}

/* SIM-109/110/111: predict actual generator state and named input pairs; no scripted raw results. */
static void context_snapshot(World *expected) {
    world_alloc(expected);
    for (int b = 0; b <= WORLD_BLOCKS; b++) memcpy(expected->block[b], g_world.block[b], g_world.size[b]);
}
static unsigned context_expected_bits(Sim *ref, Logged *draws, int *count, unsigned site, unsigned mask) {
    SimIn in = ref->rng_in;
    unsigned value = sim_bits(ref, mask);
    if (site) draws[(*count)++] = (Logged){site, mask, value, in.on ? in.d & 0xFFFF : 0xFFFFFFFFu, in.on ? in.e : 0, 'b'};
    return value;
}
static unsigned context_expected_gate(Sim *ref, Logged *draws, int *count, SimIn in) {
    unsigned value = sim_random_in(ref, 50, 0, in);
    draws[(*count)++] = (Logged){0x42E10, 50, value, in.d & 0xFFFF, in.e, 'r'};
    return value;
}
static void context_read_log(FILE *log) {
    g_sim.rng_log = NULL;
    rewind(log);
    g_nlogged = 0;
    Logged line;
    while (g_nlogged < 200000 && fscanf(log, "%X %c%u %u %X %X", &line.site, &line.kind, &line.n,
                                      &line.v, &line.d, &line.e) == 6) g_logged[g_nlogged++] = line;
    fclose(log);
}
static void context_check(const World *expected, const Sim *ref, const Logged *draws, int count, SimIn outer, int rule) {
    int ok = count == g_nlogged && ref->rng_s == g_sim.rng_s && ref->rng == g_sim.rng && g_sim.rng_script == NULL &&
             outer.on == g_sim.rng_in.on && outer.d == g_sim.rng_in.d && outer.e == g_sim.rng_in.e &&
             ref->rng_scratch == g_sim.rng_scratch;
    for (int i = 0; i < count && i < g_nlogged; i++) {
        const Logged *a = &draws[i], *b = &g_logged[i];
        ok &= a->site == b->site && a->kind == b->kind && a->n == b->n && a->v == b->v && a->d == b->d && a->e == b->e;
    }
    for (int b = 0; b <= WORLD_BLOCKS; b++) ok &= !memcmp(expected->block[b], g_world.block[b], g_world.size[b]);
    if (!ok) fprintf(stderr, "  SIM-%d generator context or complete saved state\n", rule);
    CHECK(ok);
}
static void test_neuro_incoming_context(void) {
    int hits = 0, misses = 0, carried = 0;
    for (int edition = EDITION_DISK; edition <= EDITION_ENGLISH_CD; edition++)
        for (int mode = 0; mode < 3; mode++) for (unsigned seed = 0; seed < 32; seed++) {
            g_prog.build = &PROGRAM_BUILDS[edition];
            fresh();
            g_prog.edition = (Edition)edition;
            g_sim.rng_heap = PROGRAM_BUILDS[edition].heap_shift;
            g_sim.screen = 5, g_sim.skip_registers = mode != 0;
            g_sim.clock_d = build_at(&PROGRAM_BUILDS[edition], mode ? 0x2F850 : 0x631B);
            g_sim.rng_scratch = HOSP(7) + g_sim.rng_heap;
            SimIn outer = {mode == 2, 0xABCD, 0x76543210};
            sim_set_inputs(&g_sim, outer);
            for (int n = 0; n < 4; n++) {
                int h = n < 2 ? 0 : 9;
                g_world.block[BLK_ROOM_COUNTS][100 * h + 16] = 2;
                uint8_t *ward = sim_room_record(&g_sim, h, 16, n % 2);
                ward[4] = 1, put16(ward + 8, 40 + n);
                g_world.block[BLK_STAFF][32 * (40 + n) + 21] = 50;
            }
            World expected;
            context_snapshot(&expected);
            g_sim.rng_s = 0x13579BDFu + seed * 0x9E3779B9u;
            Sim ref = g_sim;
            for (int pass = 0; pass < 2; pass++) {
                Logged draws[12];
                int count = 0;
                unsigned d = outer.on ? outer.d : g_sim.clock_d;
                for (int n = 0; n < 4; n++) {
                    int h = n < 2 ? 0 : 9;
                    ref.rng_in = sim_in(d, ROW(h) + g_sim.rng_heap, ref.rng_scratch);
                    if (context_expected_bits(&ref, draws, &count, 0x53762, 15) != 7) { misses++; continue; }
                    hits++;
                    carried += n < 3;
                    d = 0x1DE912u + 32u * (unsigned)(40 + n) + g_sim.rng_heap;
                    ref.rng_in = sim_in(d, ROW(h) + g_sim.rng_heap, ref.rng_scratch);
                    unsigned a = context_expected_bits(&ref, draws, &count, 0x53788, 1);
                    unsigned b = context_expected_bits(&ref, draws, &count, 0x53790, 1);
                    expected.block[BLK_STAFF][32 * (40 + n) + 21] -= (uint8_t)((a + b) / 2);
                }
                logged_call(0x536D1);
                context_check(&expected, &ref, draws, count, outer, 109);
            }
            world_free(&expected);
        }
    CHECK(hits > 0 && misses > 0 && carried > 0);
    g_prog.build = NULL;
    g_prog.edition = EDITION_DISK;
}
static void test_treatment_worsen_context(void) {
    int applied = 0, missed = 0;
    for (int edition = EDITION_DISK; edition <= EDITION_ENGLISH_CD; edition++)
        for (int active = 0; active < 2; active++) for (unsigned seed = 0; seed < 32; seed++) {
            fresh();
            g_prog.edition = (Edition)edition;
            g_sim.rng_heap = PROGRAM_BUILDS[edition].heap_shift;
            int h = seed % 2 ? 0 : 9, index = 261 + h;
            g_world.block[BLK_ROOM_COUNTS][100 * h + 5] = 1;
            uint8_t *r = sim_room_record(&g_sim, h, 5, 0), *p = patient(index);
            put16(r + 8, 7), put16(r + 10, 8), put16(r + 12, index), put16(r + 14, 999), r[0x45] = 9;
            uint8_t *nurse = g_world.block[BLK_STAFF] + 7 * 32;
            nurse[6] = 54, nurse[7] = 96, nurse[8] = 22; /* negative nurse bonus, no extra draws */
            p[3] = (uint8_t)h, p[4] = 5, p[44] = 50, p[79] = 1;
            put16(p + 94, 800);
            World expected;
            context_snapshot(&expected);
            uint8_t *ep = expected.block[BLK_PATIENTS] + index * PATIENT_SIZE;
            SimIn outer = {active, 0xABCD, 0x76543210};
            sim_set_inputs(&g_sim, outer);
            g_sim.rng_scratch = (seed % 2 ? ROW(3) : HOSP(15)) + g_sim.rng_heap;
            g_sim.rng_s = 0x13579BDFu + seed * 0x9E3779B9u;
            Sim ref = g_sim;
            Logged draws[10];
            int count = 0;
            for (int hospital_index = 0; hospital_index < 16; hospital_index++) {
                ref.rng_in = outer;
                unsigned at = 0x1A6 + 2 * context_expected_bits(&ref, draws, &count, 0, 7);
                put16(world_hospital(&expected, hospital_index) + at, 4 * (int)context_expected_bits(&ref, draws, &count, 0, 127));
                if (hospital_index != h) continue;
                if (!context_expected_bits(&ref, draws, &count, 0x2ECA7, 3))
                    context_expected_bits(&ref, draws, &count, 0x2ECB6, 63);
                context_expected_bits(&ref, draws, &count, 0x2EBBC, 15);
                context_expected_bits(&ref, draws, &count, 0, 3); /* drift, suppressed by +79 */
                ref.rng_in = sim_in(0x1C4748u + 106u * (unsigned)index + g_sim.rng_heap,
                                    ROW(h) + g_sim.rng_heap, ref.rng_scratch);
                if (context_expected_bits(&ref, draws, &count, 0x2E8CB, 15) == 4) {
                    int loss = (int)context_expected_bits(&ref, draws, &count, 0x2E906, 7);
                    loss += (int)context_expected_bits(&ref, draws, &count, 0x2E90D, 7);
                    loss -= (int)context_expected_bits(&ref, draws, &count, 0x2E914, 7);
                    loss -= (int)context_expected_bits(&ref, draws, &count, 0x2E91B, 7);
                    ep[44] = (uint8_t)(50 - loss), ep[67] = 50;
                    applied++;
                } else missed++;
            }
            logged_call(0x6AD3C);
            context_check(&expected, &ref, draws, count, outer, 110);
            /* Calling the shared helper directly still uses its caller; SIM-110 is scoped to category 5. */
            p[44] = ep[44] = 100;
            ref.rng_in = outer, count = 0;
            context_expected_bits(&ref, draws, &count, 0x2E8CB, 15);
            FILE *log = tmpfile();
            CHECK(log != NULL);
            if (log) {
                g_sim.rng_log = log;
                sim_worsen_in_treatment(&g_sim, h, p, 0x31);
                context_read_log(log);
                context_check(&expected, &ref, draws, count, outer, 110);
            }
            world_free(&expected);
        }
    CHECK(applied > 0 && missed > 0);
    g_prog.edition = EDITION_DISK;
}
static void test_comfort_gate_context(void) {
    static const int ITEMS[] = {0, 4, 5, 91, 100}, MEAL[] = {79, 102, 143};
    int accepted = 0, rejected = 0, duplicates = 0;
    for (int edition = EDITION_DISK; edition <= EDITION_ENGLISH_CD; edition++)
        for (int meal = 0; meal < 3; meal++) for (int fill = 0; fill < 5; fill++)
            for (int duplicate = 0; duplicate < 2; duplicate++) for (unsigned seed = 0; seed < 16; seed++) {
                g_prog.build = &PROGRAM_BUILDS[edition];
                g_prog.edition = (Edition)edition;
                fresh();
                int items = ITEMS[fill];
                memset(g_world.block[BLK_NEWS], 0xA7, 7000);
                for (int i = 0; i < 100; i++) {
                    uint8_t *it = sim_news_item(&g_sim, i);
                    it[0] = (uint8_t)(i < items ? 2 : 0), it[20] = (uint8_t)(duplicate ? 0x1D : 1);
                }
                World expected;
                context_snapshot(&expected);
                SimIn outer = {(int)(seed & 1), 0xF208, 0x76543210};
                sim_set_inputs(&g_sim, outer);
                g_sim.clock_d = 0x1122, g_sim.clock_ebp = 0x3344;
                g_sim.rng_scratch = ROW(7) + g_sim.rng_heap;
                g_sim.rng_s = 0x13579BDFu + seed * 0x9E3779B9u;
                Sim ref = g_sim;
                Logged draws[2];
                int count = 0;
                ref.rng_in = sim_in(outer.on ? outer.d : ref.clock_d,
                    build_at(&PROGRAM_BUILDS[edition], 0x144A) + 20u * (unsigned)MEAL[meal], ref.rng_scratch);
                int gate = items < 5 || (context_expected_gate(&ref, draws, &count, ref.rng_in) == 2 && items <= 90);
                int publish = gate && !(duplicate && items);
                if (publish) {
                    uint8_t *it = expected.block[BLK_NEWS] + 70 * items;
                    memset(it + 1, 0, 19);
                    it[0] = 1, it[4] = 9, it[5] = 1, it[6] = 4, it[8] = 9, it[20] = 0x1D, it[21] = 3, it[28] = 0;
                    put16(it + 14, MEAL[meal]), put16(it + 16, 261);
                    it[22] = (uint8_t)context_expected_bits(&ref, draws, &count, 0, 3);
                    accepted++;
                } else if (gate) duplicates++;
                else rejected++;
                FILE *log = tmpfile();
                CHECK(log != NULL);
                if (!log) { world_free(&expected); continue; }
                g_sim.rng_log = log;
                sim_news_comfort(&g_sim, 9, 261, 3, MEAL[meal]);
                /* The next ordinary gate still uses the scheduler, independently of the helper's context. */
                if (items + publish >= 5) context_expected_gate(&ref, draws, &count, rng_scheduler(&ref));
                sim_news_gate_draw(&g_sim, 50, 90);
                context_read_log(log);
                context_check(&expected, &ref, draws, count, outer, 111);
                CHECK(g_sim.news_written[0x1D] == (uint32_t)publish);
                world_free(&expected);
            }
    CHECK(accepted > 0 && rejected > 0 && duplicates > 0);
    g_prog.build = NULL;
    g_prog.edition = EDITION_DISK;
}

/* SIM-108: real generator inputs, not a raw script. Rooms 0 and 2 each have two care places, room 1 is empty.
 * Independent expected inputs distinguish the care pass from the later patient-processing loads. */
static void care_context_predict(World *w, int cat, unsigned heap, SimIn outer, uint32_t *state,
                                 Logged *draws, int *count) {
    int block = cat == 3 ? 11 : cat == 6 ? 16 : 17, size = cat == 3 ? 180 : 240;
    unsigned base = cat == 3 ? 0x16E3F0u : cat == 6 ? 0x18F208u : 0x198808u;
    for (int h = 0; h < 16; h++) {
        if (cat == 3) {
            unsigned at = 0x194 + (sim_generator_step(state, outer.d, outer.e) & 15);
            world_hospital(w, h)[at] = (uint8_t)((sim_generator_step(state, outer.d, outer.e) & 127) + 102);
        }
        unsigned retained = ROW(h) + heap;
        for (int room = 0; room < w->block[BLK_ROOM_COUNTS][100 * h + cat]; room++) {
            uint8_t *r = w->block[block] + (10 * h + room) * size;
            unsigned address = base + (unsigned)(10 * h + room) * (unsigned)size + heap;
            for (int slot = 0; slot < 10; slot++) {
                int index = get16(r + 0x14 + 4 * slot);
                if (!index) continue;
                unsigned patient_address = 0x1C4748u + 106u * (unsigned)index + heap;
                if (!golf_context_draw(draws, count, state, 0x2ECA7, 3, address, patient_address + address + 0x14))
                    golf_context_draw(draws, count, state, 0x2ECB6, 63, address, patient_address + address + 0x14);
                golf_context_draw(draws, count, state, 0x2EB06, 15, address, retained + address + 0x14);
            }
            int present = 0;
            for (int slot = 0; slot < 10; slot++) {
                uint8_t *place = r + 0x14 + 4 * slot;
                int index = get16(place);
                if (!index) continue;
                present++;
                retained = 0x1C4748u + 106u * (unsigned)index + heap;
                uint8_t *p = w->block[BLK_PATIENTS] + index * PATIENT_SIZE;
                if (cat == 3) {
                    sim_generator_step(state, address & 0xFFFF, retained + address + 0x14); /* drift gate; +79 prevents changes */
                    put16(place + 2, get16(place + 2) + 1);
                } else if (cat == 6 && p[3] != h) {
                    memset(place, 0, 4), r[12]--;
                    sim_reset_healthy(p); /* existing mismatch cleanup, independent of the input-context correction */
                    put16(world_hospital(w, h) + 0x168, get16(world_hospital(w, h) + 0x168) + 1);
                }
            }
            if (cat == 3) r[12] = (uint8_t)present;
        }
    }
}
static void care_context_check(const World *expected, const Logged *draws, int count, uint32_t state,
                               uint64_t auxiliary, SimIn outer, int edition, int cat, int variant, unsigned seed, int pass) {
    int ok = g_nlogged == count && g_sim.rng_s == state && g_sim.rng == auxiliary && g_sim.rng_script == NULL &&
             g_sim.rng_in.on == outer.on && g_sim.rng_in.d == outer.d && g_sim.rng_in.e == outer.e;
    for (int i = 0; i < count && i < g_nlogged; i++) {
        const Logged *a = &draws[i], *b = &g_logged[i];
        ok &= a->site == b->site && a->kind == b->kind && a->n == b->n && a->v == b->v && a->d == b->d && a->e == b->e;
    }
    for (int b = 0; b <= WORLD_BLOCKS; b++) ok &= !memcmp(expected->block[b], g_world.block[b], g_world.size[b]);
    if (!ok) fprintf(stderr, "  SIM-108 edition %d category %d variant %d seed %u pass %d\n", edition, cat, variant, seed, pass);
    CHECK(ok);
}
static void test_care_context_carry(void) {
    static const int CAT[] = {3, 6, 16}, HOSPITAL[] = {0, 9};
    static const uint32_t ROUTINE[] = {0x6BFC8, 0x6A0BA, 0x6C3D5};
    for (int edition = EDITION_DISK; edition <= EDITION_ENGLISH_CD; edition++)
        for (int kind = 0; kind < 3; kind++) for (int variant = 0; variant < 3; variant++) for (unsigned seed = 0; seed < 4; seed++) {
            fresh();
            g_prog.edition = (Edition)edition;
            g_sim.rng_heap = PROGRAM_BUILDS[edition].heap_shift;
            world_set_tail(&g_world, TAIL_HOUR, 10);
            int cat = CAT[kind];
            for (int hi = 0; hi < 2; hi++) {
                int h = HOSPITAL[hi];
                g_world.block[BLK_ROOM_COUNTS][100 * h + cat] = 3;
                for (int room = 0; room < 3; room++) {
                    uint8_t *r = sim_room_record(&g_sim, h, cat, room);
                    r[0] = r[1] = 255; /* the care drop's rating test cannot change health */
                    put16(r + 8, 7), r[10] = 10, r[12] = room == 1 ? 0 : 2;
                    if (room == 1) continue;
                    for (int seat = 0; seat < 2; seat++) {
                        int index = 11 + hi * 40 + room * 8 + seat * 8;
                        uint8_t *p = patient(index);
                        put16(r + 0x14 + seat * 36, index);
                        p[3] = (uint8_t)h, p[4] = 8, p[44] = 100, p[78] = 50, p[79] = 1;
                        if (!room && seat && variant == 1) p[104] = 1, p[4] = cat == 16 ? 16 : 8;
                        if (!room && seat && variant == 2) p[3] = (uint8_t)(h + 1);
                    }
                }
            }
            /* Single-room care after both invocations must still use the outer inputs, including its own site. */
            g_world.block[BLK_ROOM_COUNTS][9] = 1;
            uint8_t *icu = sim_room_record(&g_sim, 0, 9, 0);
            icu[0] = icu[1] = 255, put16(icu + 8, 7), put16(icu + 12, 99);
            patient(99)[44] = 100, patient(99)[104] = 1;
            World expected;
            world_alloc(&expected);
            for (int b = 0; b <= WORLD_BLOCKS; b++) memcpy(expected.block[b], g_world.block[b], g_world.size[b]);
            uint32_t state = 0x13579BDFu + seed * 0x9E3779B9u;
            g_sim.rng_s = state;
            uint64_t auxiliary = g_sim.rng;
            SimIn outer = {1, 0x4321u + seed, 0x76543210u + seed};
            sim_set_inputs(&g_sim, outer);
            for (int pass = 0; pass < 2; pass++) {
                Logged draws[64];
                int count = 0;
                care_context_predict(&expected, cat, g_sim.rng_heap, outer, &state, draws, &count);
                logged_call(ROUTINE[kind]);
                care_context_check(&expected, draws, count, state, auxiliary, outer, edition, cat, variant, seed, pass);
            }
            Logged draws[3];
            int count = 0;
            if (!golf_context_draw(draws, &count, &state, 0x2ECA7, 3, outer.d, outer.e))
                golf_context_draw(draws, &count, &state, 0x2ECB6, 63, outer.d, outer.e);
            golf_context_draw(draws, &count, &state, 0x2EBBC, 15, outer.d, outer.e);
            logged_call(0x6FA5A);
            care_context_check(&expected, draws, count, state, auxiliary, outer, edition, cat, variant, seed, 2);
            world_free(&expected);
        }
    g_prog.edition = EDITION_DISK;
}

/* SIM-9 (docs/rng-sites.md 2.4, "What the engine must change" 4): [0x4528] is one value its writers set, and the
 * context-S draws read it. (a) the home treatment, (b) the waiting-room tick, (c) the Neuro-Krankenzimmer tick, (d) the
 * staff pass and the thugs, (f) the picker after (a), (g) a load; (e), the race finish, is in test_garage.c. */
static void test_scratch_writers(void) {
    /* (a) persons 5 (treating hospital 3) and 9 (7) pass the home treatment's filter, nobody else (not in treatment) */
    patient_world();
    for (int i = 1; i < 1009; i++) patient(i)[79] = 0;
    patient(5)[79] = patient(9)[79] = 1, patient(5)[81] = 3, patient(9)[81] = 7;
    patient(5)[44] = patient(9)[44] = 50, patient(9)[1] = 1; /* alive, a profession, at home (+3 negative) */
    g_sim.rng_scratch = 0x1F21A6;
    sim_call(&g_sim, 0x672A1);
    CHECK(g_sim.rng_scratch == 0x1F1012u);
    /* (f) the picker's first random(16) right after it: E = that record, D 0 */
    logged_call(0x336CA);
    CHECK(g_nlogged > 0 && g_logged[0].site == 0x62D1F && g_logged[0].d == 0 && g_logged[0].e == 0x1F1012u);
    /* context O reads it too: hospital 3's room pick E = its row + the value */
    g_sim.rng_scratch = 0x1F1012;
    CHECK(rng_operator(&g_sim, 3, NULL).e == 0x16DDB0u + 300 + 0x1F1012u);
    patient(9)[79] = 1, patient(9)[81] = 0; /* treating byte 0: hospital record 0 */
    sim_call(&g_sim, 0x672A1);
    CHECK(g_sim.rng_scratch == 0x1F0266u);
    patient(9)[79] = 1, patient(9)[81] = 0xFF; /* a negative treating byte: 0 */
    sim_call(&g_sim, 0x672A1);
    CHECK(g_sim.rng_scratch == 0);
    for (int i = 1; i < 1009; i++) patient(i)[79] = 0; /* nobody passes: unchanged */
    g_sim.rng_scratch = 0x1F21A6;
    sim_call(&g_sim, 0x672A1);
    CHECK(g_sim.rng_scratch == 0x1F21A6u);
    /* (b) hospital 4 holds three waiting rooms, no higher hospital one */
    fresh();
    g_world.block[BLK_ROOM_COUNTS][4 * 100 + 3] = 3;
    sim_call(&g_sim, 0x6BFC8);
    CHECK(g_sim.rng_scratch == 0x17018Cu);
    /* (c) Neuro-Krankenzimmer: hospitals 2 (two wards) and 11 (one) */
    fresh();
    room_descriptions();
    beds_ward(2, 16, 0, 4), beds_ward(2, 16, 1, 4), beds_ward(11, 16, 0, 4);
    g_world.block[BLK_ROOM_COUNTS][2 * 100 + 16] = 2, g_world.block[BLK_ROOM_COUNTS][11 * 100 + 16] = 1;
    g_sim.rng_scratch = 0x12345;
    sim_call(&g_sim, 0x6C3D5);
    CHECK(g_sim.rng_scratch == 0x19EF3Cu);
    g_world.block[BLK_ROOM_COUNTS][2 * 100 + 16] = g_world.block[BLK_ROOM_COUNTS][11 * 100 + 16] = 0;
    g_sim.rng_scratch = 0x12345; /* no ward anywhere: unchanged */
    sim_call(&g_sim, 0x6C3D5);
    CHECK(g_sim.rng_scratch == 0x12345u);
    /* (d) the staff pass and the thugs leave hospital record 16 from any value, also with nobody employed */
    g_sim.rng_scratch = 0x12345;
    sim_call(&g_sim, 0x2FC77);
    CHECK(g_sim.rng_scratch == 0x1F21A6u);
    g_sim.rng_scratch = 0x12345;
    sim_call(&g_sim, 0x4A822);
    CHECK(g_sim.rng_scratch == 0x1F21A6u);
    /* (g) a load sets it to 0, as the RAM dump holds it */
    sim_reset_generator(&g_sim, 0);
    CHECK(g_sim.rng_scratch == 0);
}

/* SIM-15 (docs/sim-ai.md, the recompute, 0x1E523): a waiting room of hospital 6 whose chairs give 2 seats while 3 are
 * taken sends its patients away (opinion of 6 = 0, fields 3 and 4 FF, the person words 0, the wait words kept, the u16
 * +0x0C = 0); with 3 seats nothing changes. */
static void test_waiting_capacity_cut(void) {
    for (int seats = 2; seats <= 3; seats++) {
        fresh();
        static const uint16_t D[7] = {0x40, 2, 0x60, 0, 0x70, 0x71, 0x72}; /* a description for category 3 */
        for (int i = 0; i < 7; i++) put16(g_image + 0x4758 + 14 * 3 + 2 * i, D[i]);
        g_image[0xEE0 + 18 * 3 + 6] = 5, g_image[0xEE0 + 18 * 3 + 10] = (uint8_t)seats; /* chairs */
        g_world.block[BLK_ROOM_COUNTS][6 * 100 + 3] = 1;
        uint8_t *wr = sim_room_record(&g_sim, 6, 3, 0);
        put16(wr + 0x40, 3);
        static const int PLACE[3] = {0, 3, 9};
        for (int k = 0; k < 3; k++) {
            put16(wr + 0x14 + 4 * PLACE[k], 11 + k), put16(wr + 0x16 + 4 * PLACE[k], 5);
            uint8_t *p = patient(11 + k);
            p[3] = 6, p[4] = 7, p[20 + 6] = 60;
        }
        wr[12] = 3, wr[13] = 1;
        sim_recompute(&g_sim, 3);
        CHECK(wr[10] == seats);
        int away = seats == 2, ok = 1;
        for (int k = 0; k < 3; k++) {
            const uint8_t *p = patient(11 + k);
            ok &= p[20 + 6] == (away ? 0 : 60) && p[3] == (away ? 0xFF : 6) && p[4] == (away ? 0xFF : 7);
            ok &= get16(wr + 0x14 + 4 * PLACE[k]) == (away ? 0 : 11 + k) && get16(wr + 0x16 + 4 * PLACE[k]) == 5;
        }
        CHECK(ok && get16(wr + 0x0C) == (away ? 0 : 0x0103));
    }
}

/* FIX-11 (docs/fixes.md): an intensive-care death in AI hospital 8, record 0, no pathology place: off the record's
 * garbled item (u16 16 = 432, name 0 + "Ab$"), on the patient's own death item (u16 16 = 5, type 8). The rest of the
 * death is the patient's in both modes. */
static void test_fix11_icu_death_item(void) {
    static const char text[] = "Ab$Cd Ef$";
    static uint8_t table[8];
    put32(table, 0), put32(table + 4, 3);
    static DataDb db;
    memset(&db, 0, sizeof db);
    db.pools[0] = (TextPool){(const uint8_t *)text, sizeof text - 1, table, 2};
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 10 : 0);
        patient_world();
        g_sim.people = &db;
        int h = 8;
        uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + h * 100, *p = patient(5);
        c[9] = 1;
        uint8_t *icu = sim_room_record(&g_sim, h, 9, 0);
        put16(icu + 0x0C, 5), icu[0] = 100, icu[0x0E] = 0;
        g_image[0x954 + 7 * 20 + 7] = 100;
        p[3] = (uint8_t)h, p[4] = 9, p[44] = 100, p[43] = 30, p[96] = 2, p[57] = 0;
        int minutes = 0;
        while (get16(icu + 0x0C) && minutes < 5000) sim_rooms_run(&g_sim, 0x6FA5A), minutes++;
        CHECK(get16(icu + 0x0C) == 0 && minutes < 5000);
        const uint8_t *r = world_hospital(&g_world, h);
        CHECK(sim_news_count(&g_sim) == 1);
        const uint8_t *it = sim_news_item(&g_sim, 0);
        CHECK(it[0] == 2 && it[20] == 8 && it[8] == h && get16(it + 16) == (on ? 5 : 432));
        if (!on) CHECK(memcmp(it + 29, "\0Ab$", 4) == 0);
        else CHECK(it[29] != 0);
        CHECK(get16(r + 0x38) == 1 && get16(r + 0x3C) == 1 && p[4] == 0xFF && p[3] == 0xFF);
        g_sim.people = NULL;
    }
    fixes_set(0);
}

/* FIX-22 (docs/fixes.md): ids A, 0, 0, B, C, 0 with 3 held: off A, 0, B, C (C beyond the first 3), on A, B, C */
static void test_fix22_bank_compaction(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 21 : 0);
        uint8_t *b = bank_world(15);
        static const uint8_t IDS[6] = {1, 0, 0, 4, 5, 0};
        for (int i = 0; i < 6; i++) b[0x58 + 2 * i] = IDS[i], b[0x59 + 2 * i] = IDS[i] ? (uint8_t)(50 + i) : 0;
        put16(b + 0x14, 3);
        sim_bank_compact(&g_sim);
        static const uint8_t OFF[6] = {1, 0, 4, 5, 0, 0}, ON[6] = {1, 4, 5, 0, 0, 0};
        for (int i = 0; i < 6; i++) CHECK(b[0x58 + 2 * i] == (on ? ON : OFF)[i]);
        CHECK(on ? (b[0x5A] == 4 && b[0x5B] == 53 && b[0x5C] == 5 && b[0x5D] == 54) : (b[0x5C] == 4 && b[0x5D] == 53));
        for (int i = 6; i < 30; i++) CHECK(get16(b + 0x58 + 2 * i) == 0);
    }
    fixes_set(0);
}

/* FIX-23 (docs/fixes.md): field 104 = 1: q 100 both; 101: 0 both; 105: off 252, on 0; 200: off 157, on 0 */
static void test_fix23_organ_quality(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 22 : 0);
        CHECK(sim_organ_quality(1) == 100 && sim_organ_quality(101) == 0);
        CHECK(sim_organ_quality(105) == (on ? 0 : 252) && sim_organ_quality(200) == (on ? 0 : 157));
    }
    fixes_set(0);
}

/* FIX-25 (docs/fixes.md): an AI Krankenzimmer, the dead P (5) in bed 3, Q (6) in bed 0: off bed 0 cleared and bed 3
 * keeps P, on bed 3 cleared and bed 0 keeps Q; W +12 - 1 in both */
static void test_fix25_ward_cleanup(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 24 : 0);
        uint8_t *ward = ward_world(6), *p = patient(5), *q = patient(6);
        put16(ward + 0x14, 6), put16(ward + 0x14 + 4 * 3, 5), ward[12] = 2;
        q[3] = 15, q[4] = 6, q[44] = 95, q[93] = 0xFF, q[100] = 2;
        p[104] = 1;
        const uint8_t *r = world_hospital(&g_world, 15);
        int calls = 0;
        while (calls < 500 && get16(r + 0x38) == 0) sim_rooms_run(&g_sim, 0x2995B), calls++;
        CHECK(get16(r + 0x38) == 1 && calls < 500);
        CHECK(ward[12] == 1);
        CHECK(get16(ward + 0x14) == (on ? 6 : 0) && get16(ward + 0x14 + 4 * 3) == (on ? 0 : 5));
    }
    fixes_set(0);
}

/* FIX-26 (docs/fixes.md): AI OP room 2 takes anaesthetist staff 77 (staff byte 26 was 0x55, OP +0x1A was 0): off OP
 * +0x1A = 2 and byte 26 kept, on OP +0x1A = 0 and byte 26 = 2 */
static void test_fix26_op_doctor_room(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 25 : 0);
        patient_world();
        int h = 15;
        uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + h * 100;
        c[7] = 3;
        for (int r = 0; r < 3; r++) {
            uint8_t *op = sim_room_record(&g_sim, h, 7, r);
            put16(op + 6, r == 2 ? 0 : 7), put16(op + 8, 8), put16(op + 10, 9);
        }
        uint8_t *st = g_world.block[BLK_STAFF] + 77 * 32, *sl = staff_slots(&g_sim, h);
        put16(sl, 77), sl[2] = 2, sl[3] = 0;
        st[1] = 2, st[3] = (uint8_t)h, st[21] = 80, st[26] = 0x55;
        uint8_t *op2 = sim_room_record(&g_sim, h, 7, 2);
        int calls = 0;
        while (calls < 500 && get16(op2 + 6) != 77) sim_rooms_run(&g_sim, 0x6E8BE), calls++;
        CHECK(get16(op2 + 6) == 77 && calls < 500);
        CHECK(op2[0x1A] == (on ? 0 : 2) && st[26] == (on ? 2 : 0x55));
    }
    fixes_set(0);
}

/* FIX-27 (docs/fixes.md): the torture chamber's bring; record +8 = staff 7 at work counter 100, a due neuro-ward
 * patient: off no counter changes, on staff 7 at 101 */
static void test_fix27_torture_work(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 26 : 0);
        patient_world();
        int h = 15;
        uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + h * 100, *p = patient(5);
        c[8] = 1, c[16] = 1;
        uint8_t *tc = sim_room_record(&g_sim, h, 8, 0), *ward = sim_room_record(&g_sim, h, 16, 0);
        put16(tc + 8, 7), put16(tc + 10, 9);
        put16(ward + 8, 3), ward[10] = 10, ward[12] = 1, put16(ward + 0x14, 5);
        uint8_t *t = g_image + 0x270C + 30 * 38;
        t[1] = 1, t[16] = 71;
        p[3] = (uint8_t)h, p[4] = 16, p[44] = 100, p[43] = 30, p[45] = 1, p[102] = 0;
        uint8_t *s7 = g_world.block[BLK_STAFF] + 7 * 32, *s9 = g_world.block[BLK_STAFF] + 9 * 32;
        put16(s7 + 0x10, 100), put16(s9 + 0x10, 50);
        sim_rooms_run(&g_sim, 0x6E4C1);
        CHECK(get16(tc + 0x0C) == 5 && p[4] == 8); /* brought */
        CHECK(get16(s7 + 0x10) == (on ? 101 : 100) && get16(s9 + 0x10) == 50);
    }
    fixes_set(0);
}

/* FIX-29 (docs/fixes.md): W = D. H = 200, a = 3: off + 1 (one draw); on no + 1, then the b and stamina draws (stamina
 * 0: a loss). H = 128: the same. H = 127: the stamina draw in both. */
static void test_fix29_signed_hours(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 28 : 0);
        fresh();
        StaffId employee = {1};
        StaffSlotId place = {0, 0};
        uint8_t *st = staff_record_view(&g_world, employee).data, *sl = staff_slot_view(&g_world, place).data;
        put16(sl, employee.value);
        st[19] = 10, st[18] = 10, st[14] = 0;
        for (int n = 0; n < 32; n++) {
            for (int k = 0; k < 2; k++) {
                sl[10] = k ? 128 : 200;
                Sim twin = g_sim;
                sim_bits(&twin, 7);
                if (on) sim_bits(&twin, 15), sim_bits(&twin, 63);
                CHECK(sim_mood_working(&g_sim, employee, place) == (on ? 1 : 2));
                CHECK(sim_bits(&g_sim, 0xFFFF) == sim_bits(&twin, 0xFFFF));
            }
            sl[10] = 127;
            CHECK(sim_mood_working(&g_sim, employee, place) == 1);
        }
    }
    fixes_set(0);
}

/* FIX-30 (docs/fixes.md): score 40,000 before the time term. From 30.12.1995 23:59 one minute: off the day list sees
 * 31.12. (2,464), on 01.01.1996 (37,024); from 30.03.: 28,384 in both; from 29.03.: both see 30.03. (28,480). The
 * date after the change is the same in both. */
static void test_fix30_day_list_date(void) {
    static const struct { int day, month; long off, on; } C[3] = {
        {30, 12, 2464, 37024}, {30, 3, 28384, 28384}, {29, 3, 28480, 28480}};
    static const int AFTER[3][3] = {{1, 1, 1996}, {1, 4, 1995}, {30, 3, 1995}};
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 29 : 0);
        for (int k = 0; k < 3; k++) {
            fresh();
            uint8_t *h0 = world_hospital(&g_world, 0), *pb0 = g_world.block[BLK_PLAYER0];
            put16(h0 + 0x15E, 2500);
            world_set_tail(&g_world, TAIL_DAY, C[k].day);
            world_set_tail(&g_world, TAIL_MONTH, C[k].month);
            world_set_tail(&g_world, TAIL_HOUR, 23);
            world_set_tail(&g_world, TAIL_MINUTE, 59);
            for (int i = 0; i < SIM_TICKS_PER_MINUTE; i++) sim_tick(&g_sim);
            CHECK(pb0[RP_ARMED] == 0xFF && get32(pb0 + RP_SCORE) == (on ? C[k].on : C[k].off));
            CHECK(world_tail(&g_world, TAIL_DAY) == AFTER[k][0] && world_tail(&g_world, TAIL_MONTH) == AFTER[k][1]);
            CHECK(world_tail(&g_world, TAIL_YEAR) == AFTER[k][2]);
        }
    }
    fixes_set(0);
}

/* ---- SIM-33 to SIM-41 (docs/parity.md): scripted draws (the SimScript hook of tools/diff_routines.py) ------------ */

/* Runs a routine (or with rooms_operator the operator of room 0 of category cat in hospital h) with every generator
 * step returning t, after the first values first[0..nfirst-1]; the steps used. */
static uint32_t g_const_script[20000];
static size_t script_call(uint32_t routine, uint32_t t, const uint32_t *first, size_t nfirst, int rooms_operator, int h,
                          int cat) {
    for (size_t i = 0; i < sizeof g_const_script / sizeof *g_const_script; i++)
        g_const_script[i] = i < nfirst ? first[i] : t;
    SimScript sc = {g_const_script, sizeof g_const_script / sizeof *g_const_script, 0, 0, NULL};
    g_sim.rng_script = &sc;
    if (rooms_operator) sim_rooms_after_operator(&g_sim, h, cat, 0);
    else sim_rooms_run(&g_sim, routine);
    g_sim.rng_script = NULL;
    CHECK(sc.extra == 0);
    return sc.used;
}
static size_t const_call(uint32_t routine, uint32_t t, int rooms_operator, int h, int cat) {
    return script_call(routine, t, NULL, 0, rooms_operator, h, cat);
}

/* SIM-33 and FIX-48 (docs/sim-rooms2.md, "0x57E03 - organ ageing", step 1): every bank's cooling test reads hospital 0's
 * bank record. Script 1, 1: a bank counted as cooled (no keeper, S = 0) draws r15 = 1 and stops (one step); one counted
 * as not cooled draws r3 = 1, then r15 = 1 (two steps). Hospital 15's own unit counts only with FIX-48, a unit in
 * hospital 0's record (no bank built there) only without it. */
static void test_sim33_cooling_of_hospital0(void) {
    for (int fix = 0; fix <= 1; fix++)
        for (int where = 0; where < 3; where++) { /* none, bank 15's own record, hospital 0's record */
            fixes_set(fix ? 1ull << 47 : 0);
            uint8_t *b = bank_world(15);
            if (where == 1) put16(b + 0x30, 2);
            if (where == 2) put16(sim_room_record(&g_sim, 0, 17, 0) + 0x30, 2);
            put16(b + 0x14, 1), b[0x58] = 6, b[0x59] = 80;
            int cooled = where == (fix ? 1 : 2);
            CHECK(const_call(0x57E03, 1, 0, 0, 0) == (cooled ? 1u : 2u));
            CHECK(b[0x59] == 80);
        }
    fixes_set(0);
}

/* SIM-34 (docs/sim-rooms2.md, "0x30721"): the decline writes the health through set health, so the old value moves
 * into the history 67-77 (a hit from health 4 gives 3, the history then starts 4, 50). */
static void test_sim34_decline_history(void) {
    uint8_t *ward = ward_world(6), *p = patient(5);
    put16(ward + 8, 0);
    int hits = 0;
    for (int i = 0; i < 1280; i++) {
        p[44] = 4, p[96] = 0, p[93] = 0, p[104] = 0, p[40] = 0;
        memset(p + 67, 50, 11);
        put16(ward + 0x14, 5), ward[12] = 1;
        sim_rooms_run(&g_sim, 0x6A0BA);
        int hit = p[44] == 3;
        hits += hit;
        if (hit) CHECK(p[67] == 4 && p[68] == 50 && p[77] == 50);
        else CHECK(p[67] == 50);
    }
    CHECK(hits >= 15 && hits <= 70);
}

/* SIM-34 (docs/sim-rooms2.md, round check): the heal by I byte 12 draws the threshold r1s only for the thresholds that
 * hold, and field 50 is a signed byte. With every draw 0 (the chance draw holds at any field 50) a Krankenzimmer bed
 * draws one step more at 35, two at 50, three at 75 than at 0; 200 (-56) draws as 0. */
static void test_sim34_threshold_draws(void) {
    static const int F[5] = {0, 40, 60, 80, 200}, MORE[5] = {0, 1, 2, 3, 0};
    size_t base = 0;
    for (int k = 0; k < 5; k++) {
        uint8_t *ward = ward_world(6), *p = patient(5);
        (void)ward;
        p[93] = 0, p[96] = 2, p[43] = 31, p[50] = (uint8_t)F[k]; /* illness 7's byte 12 is 31 */
        size_t used = const_call(0x6A0BA, 0, 0, 0, 0);
        if (!k) base = used;
        if (used != base + (size_t)MORE[k]) fprintf(stderr, "  SIM-34 field 50 = %d: %zu steps, base %zu\n", F[k], used, base);
        CHECK(used == base + (size_t)MORE[k]);
        CHECK(p[44] == 96); /* 95 + (byte 7 / 2 + 1), every r1 0 */
    }
}

/* SIM-34: the heal of a single healing step (code 72 in the neuro ward) compares field 50 as a signed byte: with every
 * draw 2 (r7 = 2 heals, every r1 is 0) the best treatment gives + 2, + 1 per threshold 35 / 50 / 75 held and + 4 at 20
 * or more; field 50 = 200 (-56) gives + 2 as 0 does, 80 gives + 9. */
static void test_sim34_signed_heal(void) {
    static const int F[3] = {0, 80, 200}, GAIN[3] = {2, 9, 2};
    for (int k = 0; k < 3; k++) {
        uint8_t *ward = ward_world(16), *p = patient(5), *t = g_image + 0x270C + 30 * 38;
        (void)ward;
        t[36] = 0, t[15] = 72;
        p[93] = 0xFF, p[43] = 30, p[45] = 0, p[44] = 50, p[100] = 2, p[50] = (uint8_t)F[k];
        const_call(0x6C3D5, 2, 0, 0, 0);
        if (p[44] != 50 + GAIN[k]) fprintf(stderr, "  SIM-34 heal, field 50 = %d: health %d\n", F[k], p[44]);
        CHECK(p[44] == 50 + GAIN[k]);
    }
}

/* SIM-35 (docs/sim-rooms2.md, "0x58045", step 7): a transfusion during the operation books fields 48 and 53, not 55.
 * Every draw 0: the stage runs on (+0x0E 5 -> 4) without a unit draw hitting, the bank (staffed, stock 1,000) refills
 * 20 cl of the missing 100 and no r7 = 3 follows. */
static void test_sim35_op_transfusion(void) {
    patient_world();
    int h = 15;
    uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + h * 100, *p = patient(5);
    c[7] = c[17] = 1;
    static const uint16_t DESC7[7] = {126, 30, 28, 30, 186, 88, 27};
    for (int i = 0; i < 7; i++) put16(g_image + 0x4758 + 14 * 7 + 2 * i, DESC7[i]);
    uint8_t *op = sim_room_record(&g_sim, h, 7, 0), *bank = sim_room_record(&g_sim, h, 17, 0);
    put16(op + 6, 7), put16(op + 8, 8), put16(op + 10, 9);
    put16(op + 0x0C, 5), put16(op + 0x0E, 5), put16(op + 0x10, 2), put16(op + 0x12, 0);
    put16(bank + 8, 40), put32(bank + 0x0C, 1000);
    p[3] = (uint8_t)h, p[4] = 7, p[44] = 100, p[100] = 1, p[101] = 0, put16(p + 94, 600);
    p[48] = p[53] = p[55] = 0;
    const_call(0x58045, 0, 0, 0, 0);
    CHECK(get16(op + 0x0E) == 4 && get16(p + 94) == 620 && get32(bank + 0x0C) == 980);
    CHECK(p[48] == 1 && p[53] == 1 && p[55] == 0);
}

/* SIM-35: intensive care's transfusion books fields 48, 53 and 55 (unchanged). Every draw 0, health 100, blood 600, the
 * capability-17 item installed, the bank staffed: 20 cl. */
static void test_sim35_icu_transfusion(void) {
    uint8_t *bank = bank_world(15), *p = patient(5);
    world_set_cash(&g_world, 15, 1000);
    g_world.block[BLK_ROOM_COUNTS][15 * 100 + 9] = 1;
    static const uint16_t D[7] = {0x40, 5, 0x60, 10, 0x90, 0xA0, 0xC0};
    for (int i = 0; i < 7; i++) put16(g_image + 0x4758 + 14 * 9 + 2 * i, D[i]);
    g_image[0x144A + 20 * 4 + 10] = 17, g_image[0x144A + 20 * 4 + 16] = 1; /* item 4: capability 17 */
    uint8_t *icu = sim_room_record(&g_sim, 15, 9, 0);
    put16(icu + 8, 7), put16(icu + 0x0C, 5), put16(icu + 0x60, 4), icu[0xA0] = 100;
    put16(bank + 8, 40), put32(bank + 0x0C, 1000);
    p[1] = 1, p[3] = 15, p[4] = 9, p[44] = 100, p[100] = 0, put16(p + 94, 600);
    p[48] = p[53] = p[55] = 0;
    const_call(0x6FA5A, 0, 0, 0, 0);
    CHECK(get16(p + 94) == 620 && get32(bank + 0x0C) == 980);
    CHECK(p[48] == 1 && p[53] == 1 && p[55] == 1);
}

/* SIM-36 (docs/sim-rooms.md, "The tick's diagnosis and choice, in every hospital"): in an AI hospital the tick's
 * countdown end diagnoses (r127 = 0 below a skilled doctor's S: the real illness 7, the doctor's + 3) and chooses with
 * the rank r3 = 0: the treatment room takes illness 7's byte 11 (30), the dental ward and the neurology FE (illness 7
 * is type 0 with byte 15 = 0). */
static void test_sim36_tick_diagnoses_ai(void) {
    static const int CAT[3] = {5, 11, 15};
    for (int c = 0; c < 3; c++) {
        uint8_t *tr = doctor_room(15, CAT[c]), *p = patient(5), *doc = g_world.block[BLK_STAFF] + 8 * 32;
        doc[2] = doc[12] = doc[13] = 255;
        put16(tr + 14, 1), put16(tr + 16, 0), put16(tr + 18, 0);
        p[42] = 0, p[43] = 0x55, p[79] = 0x55;
        const_call(CAT[c] == 5 ? 0x6AD3C : CAT[c] == 11 ? 0x6B445 : 0x6BAE4, 0, 0, 0, 0);
        CHECK(get16(tr + 14) == 0 && p[42] == 7);
        CHECK(p[43] == (CAT[c] == 5 ? 30 : 0xFE) && p[79] == (CAT[c] == 5 ? 0xFF : 0)); /* 30 is done at home */
        int credit = doctor_counter() - 3;
        CHECK(credit == 0 || credit == 1);
    }
    /* the tick's chooser runs with [0x5ABD] clear in every hospital: the rank is one r3 (the AI mode's helper would
     * draw r3, r63, r127), so hospital 15 draws as many steps as hospital 0 */
    size_t used[2];
    for (int ai = 0; ai <= 1; ai++) {
        uint8_t *tr = doctor_room(ai ? 15 : 0, 5), *p = patient(5), *doc = g_world.block[BLK_STAFF] + 8 * 32;
        doc[2] = doc[12] = doc[13] = 255;
        put16(tr + 14, 1), put16(tr + 16, 0), put16(tr + 18, 0);
        used[ai] = const_call(0x6AD3C, 0, 0, 0, 0);
        CHECK(p[42] == 7 && p[43] == 30);
    }
    CHECK(used[0] == used[1]);
}

/* SIM-36, step 2: the AI tick's wrong guess is drawn and then corrected - the treatment room to the real illness when
 * field 56 is non-zero (else the guess stays), the neurology always (also to 0). A doctor of skill 0 (r127 never
 * below S) and illnesses 8-40 holding ailment 3 at every place (the guess is one of them), as test_diagnosis_bonus. */
static void test_sim36_ai_guess_correction(void) {
    static uint8_t ailments[71 * 4];
    memset(ailments, 0, sizeof ailments);
    memset(ailments + 8 * 4, 3, 33 * 4);
    for (int neuro = 0; neuro <= 1; neuro++)
        for (int ill = 0; ill <= 1; ill++) {
            uint8_t *tr = doctor_room(15, neuro ? 15 : 5), *p = patient(5);
            for (int id = 9; id <= 40; id++) memcpy(g_image + 0x954 + id * 20, g_image + 0x954 + 8 * 20, 20);
            sim_set_ailments(&g_sim, ailments, sizeof ailments);
            put16(p + 56, (uint16_t)(ill ? 7 : 0));
            put16(tr + 14, 1), put16(tr + 16, 0), put16(tr + 18, 0);
            sim_rooms_run(&g_sim, neuro ? 0x6BAE4 : 0x6AD3C);
            if (ill) CHECK(p[42] == 7);
            else if (neuro) CHECK(p[42] == 0);
            else CHECK(p[42] >= 8 && p[42] <= 40);
            sim_set_ailments(&g_sim, NULL, 0);
        }
}

/* SIM-37: the AI operator of a diagnosed patient (+0x0E = 0, +0x10 = 0) only chooses, in AI mode: field 42 stays the
 * tick's (here 8, illness 8: best treatment 31) and the doctor gets no second + 3. Every draw 0: the rank helper's r3,
 * r63 and r127 give rank 0, so the treatment room takes 31; the twins give FE and send the patient back. */
static void test_sim37_operator_only_chooses(void) {
    static const int CAT[3] = {5, 11, 15};
    for (int c = 0; c < 3; c++) {
        uint8_t *tr = doctor_room(15, CAT[c]), *p = patient(5), *doc = g_world.block[BLK_STAFF] + 8 * 32;
        doc[2] = doc[12] = doc[13] = 255;
        put16(tr + 14, 0), put16(tr + 16, 0), put16(tr + 18, 0);
        p[42] = 8, p[43] = 0x55;
        const_call(0, 0, 1, 15, CAT[c]);
        if (CAT[c] == 5) CHECK(p[42] == 8 && p[43] == 31);
        CHECK(doctor_counter() == 0);
    }
    /* AI mode: r7 = 0, then the rank helper's r3 = 1, r63 = 0 and r127 = 0 below S give rank 0 (31); the player mode's
     * rank would be the r3, 1 (illness 8's byte 12, 32) */
    uint8_t *tr = doctor_room(15, 5), *p = patient(5), *doc = g_world.block[BLK_STAFF] + 8 * 32;
    doc[2] = doc[12] = doc[13] = 255;
    g_image[0x954 + 8 * 20 + 12] = 32;
    put16(tr + 14, 0), put16(tr + 16, 0), put16(tr + 18, 0);
    p[42] = 8, p[43] = 0x55;
    static const uint32_t FIRST[2] = {0, 1};
    script_call(0, 0, FIRST, 2, 1, 15, 5);
    CHECK(p[43] == 31);
}

/* SIM-36, step 3: a patient without an ailment gets diagnosis 0, and the chooser reads illness record 0 (all zero in
 * the game; here its byte 11 is 33 to see the read): the treatment room draws as for an illness (every draw 2: r7 = 2
 * and r127 = 2 below S give the best treatment, 33); the dental ward and the neurology give FE. */
static void test_sim36_chooser_record0(void) {
    static const int CAT[3] = {5, 11, 15};
    for (int c = 0; c < 3; c++) {
        uint8_t *tr = doctor_room(0, CAT[c]), *p = patient(5), *doc = g_world.block[BLK_STAFF] + 8 * 32;
        doc[2] = doc[12] = doc[13] = 255;
        g_image[0x954 + 11] = 33;
        put16(tr + 14, 1), put16(tr + 16, 0), put16(tr + 18, 0);
        p[41] = 0, p[42] = 0x55, p[43] = 0x55;
        const_call(CAT[c] == 5 ? 0x6AD3C : CAT[c] == 11 ? 0x6B445 : 0x6BAE4, 2, 0, 0, 0);
        CHECK(p[42] == 0 && p[43] == (CAT[c] == 5 ? 33 : 0xFE));
        g_image[0x954 + 11] = 0;
    }
}

/* The bed wards' cure check runs 0x37D19 (docs/sim-patients.md, "0x37D19 - case history"): fields 11-19 shift up and
 * take the illness, and fields 84-92 shift with them and take field 80 (found with SIM-34's re-run). Health 100,
 * "other treatment" (field 43 = 33), field 96 = 2, no step running. */
static void test_ward_cure_history(void) {
    for (int cat = 6; cat <= 16; cat += 10) {
        uint8_t *ward = ward_world(cat), *p = patient(5);
        put16(ward + 8, 0);
        p[44] = 100, p[93] = 0, p[96] = cat == 6 ? 2 : 3, p[43] = 33, p[80] = 9;
        for (int i = 0; i < 9; i++) p[11 + i] = (uint8_t)(20 + i), p[84 + i] = (uint8_t)(40 + i);
        sim_rooms_run(&g_sim, cat == 6 ? 0x6A0BA : 0x6C3D5);
        CHECK(get16(p + 56) == 0 && p[11] == 7 && p[12] == 20 && p[19] == 27);
        CHECK(p[84] == 9 && p[85] == 40 && p[92] == 47);
    }
}

/* SIM-40 (docs/sim-rooms.md, "0x6BFC8", staying): with the comfort flag set and r3 = 0 every patient draws an r1, a
 * man (field 0 = 0) a second r1 that replaces it: one step more for a man. Every draw 0. */
static void test_sim40_waiting_room_men(void) {
    size_t used[2];
    for (int man = 0; man <= 1; man++) {
        patient_world();
        g_world.block[BLK_ROOM_COUNTS][15 * 100 + 3] = 1;
        uint8_t *wr = sim_room_record(&g_sim, 15, 3, 0), *p = patient(5);
        wr[10] = 10, wr[15] = 1, put16(wr + 0x14, 5), put16(wr + 0x16, 0);
        p[0] = (uint8_t)(man ? 0 : 1), p[3] = 15, p[4] = 3, p[44] = 95, p[78] = 60;
        used[man] = const_call(0x6BFC8, 0, 0, 0, 0);
        CHECK(get16(wr + 0x14) == 5);
    }
    CHECK(used[1] == used[0] + 1);
}

/* SIM-40 (docs/mainloop.md, "0x67611", step 6): with R +0x45 set and r3 non-zero every patient of the reception queue
 * draws b = r1, and a man an r3 after it that replaces it: one step more for a man. Every draw 1. */
static void test_sim40_reception_men(void) {
    size_t used[2];
    for (int man = 0; man <= 1; man++) {
        uint8_t *R = reception_world(), *p = patient(5);
        R[0x45] = 5;
        p[0] = (uint8_t)(man ? 0 : 1), p[78] = 60;
        used[man] = const_call(0x67611, 1, 0, 0, 0);
    }
    CHECK(used[1] == used[0] + 1);
}

/* SIM-41 (docs/sim-rooms.md, "0x6BFC8"): the record's +12 ends as the number of patients counted, a leaver (wait 180)
 * and a patient who died there included. Patient 5 leaves (every draw 0) or dies (every draw 11: r3 = 3, the worsening's
 * r31 = 11, a drop of 4 from health 4 in an AI hospital); patient 6 stays. */
static void test_sim41_waiting_count(void) {
    for (int dies = 0; dies <= 1; dies++) {
        patient_world();
        g_world.block[BLK_ROOM_COUNTS][15 * 100 + 3] = 1;
        uint8_t *wr = sim_room_record(&g_sim, 15, 3, 0), *p = patient(5), *q = patient(6);
        wr[10] = 10, wr[12] = 2;
        put16(wr + 0x14, 5), put16(wr + 0x16, dies ? 20 : 179);
        put16(wr + 0x18, 6), put16(wr + 0x1A, 0);
        p[0] = 1, p[3] = 15, p[4] = 3, p[44] = dies ? 4 : 95, p[78] = 99;
        g_image[0x954 + 7 * 20 + 9] = 1; /* no r3 back below 20 */
        q[0] = 1, q[1] = 1, q[3] = 15, q[4] = 3, q[44] = 95, q[78] = 99;
        int deaths = get16(world_hospital(&g_world, 15) + 0x38);
        const_call(0x6BFC8, dies ? 11 : 0, 0, 0, 0);
        CHECK(get16(wr + 0x14) == 0 && get16(wr + 0x18) == 6);
        CHECK(dies ? get16(world_hospital(&g_world, 15) + 0x38) == deaths + 1 : p[3] == 0xFF); /* 0x31299 counts it */
        CHECK(wr[12] == 2);
    }
}

/* SIM-78 (docs/sim-rooms2.md, "Death notification retained in the save"): general worsening through the scheduled
 * waiting-room routine writes only the hospital's saved player death flag, irrespective of the watched patient
 * (player u16 +0x2C). Every draw 11 gives a health loss of 4. A pathology place preserves the dead patient's record
 * in hospitals 1-15, so the tests can also check that the normal death handling still runs. */
static void test_sim78_worsening_death_flag(void) {
    static const int HOSPITALS[] = {0, 1, 2, 15};
    for (int c = 0; c < 4; c++)
        for (int dies = 0; dies <= 1; dies++)
            for (int watched = 0; watched <= 1; watched++)
                for (int already_set = 0; already_set <= 1; already_set++) {
                    patient_world();
                    int h = HOSPITALS[c];
                    uint8_t *counts = g_world.block[BLK_ROOM_COUNTS] + h * 100;
                    counts[3] = counts[18] = 1;
                    uint8_t *wr = sim_room_record(&g_sim, h, 3, 0), *p = patient(5);
                    uint8_t *path = death_alias_record(h);
                    wr[10] = 10, wr[12] = 1;
                    put16(wr + 0x14, 5), put16(wr + 0x16, 20);
                    put16(path + 8, 5); /* five free places at the per-tick death alias (SIM-79) */
                    p[0] = 1, p[3] = (uint8_t)h, p[4] = 3, p[44] = dies ? 4 : 50, p[78] = 99;
                    g_image[0x954 + 7 * 20 + 9] = 1; /* no survivable-illness health bonus */
                    uint8_t *pb0 = g_world.block[BLK_PLAYER0], *pb1 = g_world.block[BLK_PLAYER1];
                    put16(pb0 + 0x2C, watched ? 5 : 6), put16(pb1 + 0x2C, watched ? 5 : 6);
                    uint8_t before0 = already_set ? 0xFF : 0x37, before1 = already_set ? 0xFF : 0x62;
                    pb0[0x46] = before0, pb1[0x46] = before1;
                    const_call(0x6BFC8, 11, 0, 0, 0);
                    CHECK(p[44] == (dies ? 0 : 46) && p[104] == dies);
                    CHECK(pb0[0x46] == (dies && h == 0 ? 0xFF : before0));
                    CHECK(pb1[0x46] == (dies && h == 1 ? 0xFF : before1));
                    CHECK(get16(world_hospital(&g_world, h) + 0x38) == (dies && h != 0));
                    CHECK(get16(wr + 0x14) == (dies && h != 0 ? 0 : 5));
                    CHECK(get16(path + 6) == (dies && h != 0));
                    if (dies && h != 0) CHECK(p[4] == 18 && get16(path + 0x58) == 5);
                }
}

/* SIM-32 and FIX-47 (docs/sim-ai.md, "The pathology's base rating comes from a stale register"; docs/fixes.md): the
 * pathology recompute takes its base from the stale category al. Pathologies in hospitals 2, 5, 9, 12 (description 18:
 * equipment 20 slots from 0x30, type at 0x1B, real type 36 with base 30), in this order:
 * - 2: al = the room-count rows' low byte 0xB0, description 0xB0's +0x0C = 0x8F, byte 0x8F = 99 -> base 1; its last
 *   equipment slot holds item 6 (+10 = 0, +5 = 3): al = 3;
 * - 5: description 3's +0x0C = 0x20, byte 0x20 = 16 -> base 5; last slot item 7 (+10 = 0x2B, +5 = 0): al = 20 x 7 =
 *   0x8C;
 * - 9: description 0x8C's +0x0C = 0x22, byte 0x22 = 13 -> base 2; last slot empty: al = 0;
 * - 12: description 0's +0x0C = 0 -> byte 0, the old rating 16 -> base 5.
 * The old ratings of 5 and 9 are 99 (base 1), so an al of 0 there (item 7's +5, or the first slot read instead of the
 * last, which is empty) shows.
 * The English build's heap shift 0x90 makes the first al 0x40 (description 0x40's +0x0C = 0x21, byte 0x21 = 16 -> 5).
 * With FIX-47 every base is the real type's, 30. The items add nothing to byte 0 (+4 = 0). */
static void test_pathology_stale_base(void) {
    static const uint16_t D[7] = {0x10, 2, 0x30, 20, 0x80, 0x8A, 0x1B};
    for (int mode = 0; mode < 3; mode++) {
        fresh();
        fixes_set(mode == 1 ? 1ull << 46 : 0);
        if (mode == 2) g_sim.rng_heap = 0x90;
        for (int i = 0; i < 7; i++) put16(g_image + 0x4758 + 14 * 18 + 2 * i, D[i]);
        put16(g_image + 0x4758 + 14 * 0xB0 + 0x0C, 0x8F);
        put16(g_image + 0x4758 + 14 * 3 + 0x0C, 0x20);
        put16(g_image + 0x4758 + 14 * 0x40 + 0x0C, 0x21);
        put16(g_image + 0x4758 + 14 * 0x8C + 0x0C, 0x22);
        static const int BASE[][2] = {{99, 1}, {13, 2}, {16, 5}, {36, 30}};
        for (int k = 0; k < 4; k++) g_image[0x31F2 + 60 * BASE[k][0] + 0x29] = (uint8_t)BASE[k][1];
        g_image[0x144A + 20 * 6 + 5] = 3;
        g_image[0x144A + 20 * 7 + 10] = 0x2B;
        static const int H[4] = {2, 5, 9, 12};
        for (int k = 0; k < 4; k++) {
            g_world.block[BLK_ROOM_COUNTS][H[k] * 100 + 18] = 1;
            uint8_t *rec = sim_room_record(&g_sim, H[k], 18, 0);
            rec[0x1B] = 36, rec[0x8F] = 99, rec[0x20] = 16, rec[0x21] = 16, rec[0x22] = 13;
            rec[0] = k == 1 || k == 2 ? 99 : 16;
        }
        put16(sim_room_record(&g_sim, 2, 18, 0) + 0x30 + 2 * 19, 6);
        put16(sim_room_record(&g_sim, 5, 18, 0) + 0x30 + 2 * 19, 7);
        sim_recompute(&g_sim, 18);
        static const int WANT[3][4] = {{1, 5, 2, 5}, {30, 30, 30, 30}, {5, 5, 2, 5}};
        for (int k = 0; k < 4; k++) {
            const uint8_t *rec = sim_room_record(&g_sim, H[k], 18, 0);
            if (rec[0] != WANT[mode][k]) fprintf(stderr, "  pathology mode %d h %d: %d\n", mode, H[k], rec[0]);
            CHECK(rec[0] == WANT[mode][k] && rec[8] == 10);
        }
        CHECK(sim_room_record(&g_sim, 2, 18, 0)[1] == 3); /* item 6's +5 in byte 1 */
        g_sim.rng_heap = 0;
    }
    fixes_set(0);
}

/* SIM-32, catalog rows past the catalog: row 89's +0x29 is byte 1 of hospital 15's daily cost ([0x46BA] + 60, 0x1AEC0).
 * One pathology (hospital 3, the first: al 0xB0 -> byte 0x8F = 89); hospital 15 rents 100 plots: cost 100 x 22 = 0x898,
 * so the base is 8 (the image byte there is 0). */
static void test_pathology_base_daily_cost(void) {
    fresh();
    static const uint16_t D[7] = {0x10, 2, 0x30, 20, 0x80, 0x8A, 0x1B};
    for (int i = 0; i < 7; i++) put16(g_image + 0x4758 + 14 * 18 + 2 * i, D[i]);
    put16(g_image + 0x4758 + 14 * 0xB0 + 0x0C, 0x8F);
    g_world.block[BLK_ROOM_COUNTS][3 * 100 + 18] = 1;
    sim_room_record(&g_sim, 3, 18, 0)[0x8F] = 89;
    put16(world_hospital(&g_world, 15) + 0x1C, 100);
    CHECK(sim_daily_costs(&g_sim, 15) == 0x898);
    sim_recompute(&g_sim, 18);
    CHECK(sim_room_record(&g_sim, 3, 18, 0)[0] == 8);
}

/* SIM-51 (docs/sim-patients.md, 0x6953D step 3): the onset stores the six city sums (categories 5, 11, 15, 7, 8, 10) in
 * Sim.city when a picked person passes the step-2 tests, also when it then stops at S5 + S11 + S15 = 0; a person who
 * fails step 2 leaves them. The city: 3 OPs, 2 torture chambers, 2 garages, no treatment room, dental or neurology. */
static size_t traced_call(uint32_t routine, uint32_t t, const size_t *at, const uint32_t *v, int nat); /* below */
static void test_sim51_onset_stores_city(void) {
    for (int passes = 0; passes <= 1; passes++) {
        patient_world();
        world_set_tail(&g_world, TAIL_HOURS, 51);
        patient(1)[1] = (uint8_t)passes; /* every draw 0: person 1, a profession only when it passes */
        uint8_t *c = g_world.block[BLK_ROOM_COUNTS];
        c[4 * 100 + 7] = 2, c[9 * 100 + 7] = 1, c[4 * 100 + 8] = 2, c[2 * 100 + 10] = 1, c[11 * 100 + 10] = 1;
        traced_call(0x6953D, 0, NULL, NULL, 0);
        const uint16_t *k = g_sim.city;
        CHECK(k[CITY_5] == 0 && k[CITY_11] == 0 && k[CITY_15] == 0);
        CHECK(k[CITY_OP] == (passes ? 3 : 0) && k[CITY_TORTURE] == (passes ? 2 : 0) && k[CITY_GARAGE] == (passes ? 2 : 0));
    }
}

/* SIM-65 (docs/sim-ai.md, "Row 90 reads the staffing need of job group 4"): row 90's +0x29 is the high byte of N4, the
 * need table's kitchen entry, not the image's byte (planted 0x33): one pathology (hospital 3, al 0xB0 -> byte 0x8F =
 * 90); N4 -1 gives base 255, 0 and 1 give 0. N4 itself: 0x62D6A for hospital 15 with a kitchen and two kitchen staff
 * leaves 1 - 2 = -1; the dismissals leave hospital 15's group-4 count (3). */
static void test_sim65_row90_need4(void) {
    static const int N4[3] = {-1, 0, 1};
    for (int k = 0; k < 3; k++) {
        fresh();
        static const uint16_t D[7] = {0x10, 2, 0x30, 20, 0x80, 0x8A, 0x1B};
        for (int i = 0; i < 7; i++) put16(g_image + 0x4758 + 14 * 18 + 2 * i, D[i]);
        put16(g_image + 0x4758 + 14 * 0xB0 + 0x0C, 0x8F);
        g_image[0x4733] = 0x33;
        g_world.block[BLK_ROOM_COUNTS][3 * 100 + 18] = 1;
        sim_room_record(&g_sim, 3, 18, 0)[0x8F] = 90;
        g_sim.need4 = (int16_t)N4[k];
        sim_recompute(&g_sim, 18);
        CHECK(sim_room_record(&g_sim, 3, 18, 0)[0] == (N4[k] < 0 ? 255 : 0));
    }
    fresh();
    g_world.block[BLK_ROOM_COUNTS][15 * 100 + 13] = 1;
    world_set_cash(&g_world, 15, 1000);
    put_slot(15, 0, 40, 4, 0), put_slot(15, 1, 41, 4, 0);
    sim_staff_run(&g_sim, 0x62D6A);
    CHECK(g_sim.need4 == -1);
    put_slot(15, 2, 42, 4, 0);
    sim_staff_run(&g_sim, 0x5B25D);
    CHECK(g_sim.need4 == 3);
}

/* SIM-44 (docs/sim-ai.md, the wear at the end of "Recompute"): a furniture item removed for wear ends the furniture
 * list's pass, the equipment list goes on. Kitchen of hospital 15 (room_descriptions: furniture from 0x40, conditions
 * 0x90; equipment from 0x60, conditions 0xA0): furniture 2, 3, 2 and equipment 5, 6, all at condition 1. Script: the
 * gate t&3 = 2, then 0 for every draw: furniture 0 goes and the pass ends (3 and the second 2 keep condition 1, no
 * draw), equipment 5 and 6 both go: 4 draws. */
static void test_wear_removal_ends_furniture(void) {
    wear_world();
    g_world.block[BLK_ROOM_COUNTS][15 * 100 + 13] = 1;
    uint8_t *r = sim_room_record(&g_sim, 15, 13, 0);
    put16(r + 0x40, 2), put16(r + 0x42, 3), put16(r + 0x44, 2), r[0x90] = r[0x91] = r[0x92] = 1;
    put16(r + 0x60, 5), put16(r + 0x62, 6), r[0xA0] = r[0xA1] = 1;
    static const uint32_t V[8] = {2, 0, 0, 0, 0, 0, 0, 0};
    SimScript sc = {V, 8, 0, 0, NULL};
    g_sim.rng_script = &sc;
    sim_recompute_wear(&g_sim, 13, 1);
    g_sim.rng_script = NULL;
    CHECK(sc.used == 4 && sc.extra == 0);
    CHECK(get16(r + 0x40) == 0 && get16(r + 0x42) == 3 && get16(r + 0x44) == 2 && r[0x91] == 1 && r[0x92] == 1);
    CHECK(get16(r + 0x60) == 0 && get16(r + 0x62) == 0);
}

/* SIM-38 (docs/sim-ai.md, "Create the room"): the eight r31 of a new room's condition lists go list by list, D+8
 * bytes 0-3, then D+10 bytes 0-3. Category 14 (no newspaper item, no draw but these) with a synthetic description
 * (conditions at 0x30 and 0x40); script value i gives condition 60 + i. Alternating would put 60 62 64 66 / 61 63 65 67. */
static void test_create_room_condition_order(void) {
    fresh();
    static const uint16_t D[7] = {0x10, 4, 0x20, 4, 0x30, 0x40, 0x50};
    for (int i = 0; i < 7; i++) put16(g_image + 0x4758 + 14 * 14 + 2 * i, D[i]);
    static const uint32_t V[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    SimScript sc = {V, 8, 0, 0, NULL};
    g_sim.rng_script = &sc;
    sim_create_room(&g_sim, 6, 14, 3);
    g_sim.rng_script = NULL;
    CHECK(sc.used == 8 && sc.extra == 0);
    const uint8_t *rec = sim_room_record(&g_sim, 6, 14, 0);
    static const uint8_t A[4] = {60, 61, 62, 63}, B[4] = {64, 65, 66, 67};
    CHECK(memcmp(rec + 0x30, A, 4) == 0 && memcmp(rec + 0x40, B, 4) == 0 && rec[0x50] == 3);
}

/* SIM-39 (docs/sim-patients.md, "0x672A1", steps 4 and 5): the department counters of the cure (+0x52 type 2, +0x54
 * type 1) and of the failure (+0xEC type 1, +0xEE any other non-zero type) move only when the person's own field 56
 * is non-zero; the illness record kept from an earlier person (g_sim.last_illness) does not count. Illnesses 8, 9, 10
 * have types 2, 1, 3. Person 5 at home, treated by hospital 4. */
static void test_home_treatment_type_gate(void) {
    static const struct {
        int own, kept, fail;
        uint32_t counter; /* 0: none moves */
    } C[] = {
        {0, 8, 0, 0}, {8, 8, 0, 0x52}, {0, 9, 0, 0}, {9, 9, 0, 0x54},
        {0, 9, 1, 0}, {9, 9, 1, 0xEC}, {0, 10, 1, 0}, {10, 10, 1, 0xEE}, {0, 8, 1, 0},
    };
    for (size_t k = 0; k < sizeof C / sizeof *C; k++) {
        patient_world();
        g_image[0x954 + 8 * 20 + 8] = 2, g_image[0x954 + 9 * 20 + 8] = 1, g_image[0x954 + 10 * 20 + 8] = 3;
        uint8_t *p = patient(5);
        p[56] = (uint8_t)C[k].own, p[57] = 0, p[79] = 1, p[81] = 4, p[82] = 0, p[100] = 0;
        if (C[k].fail) p[44] = 50, p[105] = 1, p[103] = 31; /* the countdown ends in the 32nd round: failure */
        else p[44] = 100, p[105] = 5; /* the countdown runs on: the cure check */
        g_sim.last_illness = C[k].kept;
        sim_patients_run(&g_sim, 0x672A1);
        const uint8_t *r = world_hospital(&g_world, 4);
        static const uint32_t AT[4] = {0x52, 0x54, 0xEC, 0xEE};
        int ok = get16(r + (C[k].fail ? 0x2A : 0x28)) == 1;
        for (int a = 0; a < 4; a++) ok &= get16(r + AT[a]) == (AT[a] == C[k].counter);
        if (!ok) fprintf(stderr, "  home treatment case %d\n", (int)k);
        CHECK(ok);
    }
}

/* ---- SIM-45 to SIM-49, SIM-56, SIM-57 (docs/parity.md): found by tools/diff_clock.py ---------------------------- */

/* A routine (sim_call) with the script first[0..nfirst-1], then t; the draws as the trace names them. */
typedef struct {
    unsigned site, n, v;
    char kind;
} TDraw;
static TDraw g_td[8192];
static int g_ntd;
static size_t onset_traced_call(uint32_t routine, const uint32_t *first, size_t nfirst, uint32_t t) {
    for (size_t i = 0; i < sizeof g_const_script / sizeof *g_const_script; i++)
        g_const_script[i] = i < nfirst ? first[i] : t;
    FILE *tr = tmpfile();
    CHECK(tr != NULL);
    SimScript sc = {g_const_script, sizeof g_const_script / sizeof *g_const_script, 0, 0, tr};
    g_sim.rng_script = &sc;
    sim_call(&g_sim, routine);
    g_sim.rng_script = NULL;
    CHECK(sc.extra == 0);
    g_ntd = 0;
    if (tr) {
        rewind(tr);
        TDraw d;
        size_t steps;
        while (g_ntd < 8192 && fscanf(tr, "%X %c%u %u %zu", &d.site, &d.kind, &d.n, &d.v, &steps) == 5) g_td[g_ntd++] = d;
        fclose(tr);
    }
    return sc.used;
}
static int td_is(int k, unsigned site, char kind, unsigned n) {
    int ok = k < g_ntd && g_td[k].site == site && g_td[k].kind == kind && g_td[k].n == n;
    if (!ok && k < g_ntd)
        fprintf(stderr, "  draw %d: %X %c%u, want %X %c%u\n", k, g_td[k].site, g_td[k].kind, g_td[k].n, site, kind, n);
    return ok;
}

/* SIM-45 (docs/sim-patients.md, "0x6953D" steps 5 and 6): with a type-0 onset and a free Krankenzimmer bed the b draw
 * random(K + S7) is made only when a = random(X + K) is not below X; the sexes draw at their own sites. Hospital 1: four
 * treatment rooms (X = 5), one Krankenzimmer with free beds (K = 1), two OPs (S7 = 2). Script: r127 0 (below T), person
 * 4 + 1, type entry 0, then a = 2 (below X) or 5 (A set, then b = 0, B clear), then the r3 (0: an illness, 1: a wish).
 * Illness 9 is type 0; with A clear its byte 4 = 1 is accepted, with A set (B clear) byte 4 = 0 and byte 1 = 0; wish 2
 * (byte 3 = 0) is accepted with A clear. Men 20, women 40 (the women's illness table from 25, the wish table 0x4495). */
static void test_sim45_onset_b_draw(void) {
    static const struct {
        int a_set, woman, wish;
        unsigned r3, ill, wsite;
    } C[] = {
        {0, 0, 0, 0x698E9, 0x69913, 0}, {1, 0, 0, 0x698E9, 0x69913, 0}, {0, 1, 0, 0x69A1F, 0x69A49, 0},
        {1, 1, 0, 0x69A1F, 0x69A49, 0}, {0, 0, 1, 0x698E9, 0, 0x699A6}, {0, 1, 1, 0x69A1F, 0, 0x69AF0},
    };
    for (size_t k = 0; k < sizeof C / sizeof *C; k++) {
        fresh();
        world_set_tail(&g_world, TAIL_DAY, 14);
        world_set_tail(&g_world, TAIL_HOUR, 10);
        world_set_tail(&g_world, TAIL_HOURS, 51);
        uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + 1 * 100;
        c[5] = 4, c[6] = 1, c[7] = 2;
        sim_room_record(&g_sim, 1, 6, 0)[10] = 10;
        memset(g_image + 0x41E1, 9, 0x4461 - 0x41E1);
        memset(g_image + 0x4465, 2, 0x44A5 - 0x4465);
        uint8_t *ill = g_image + 0x954 + 9 * 20;
        ill[8] = 0, ill[4] = (uint8_t)!C[k].a_set, ill[1] = 0;
        for (int i = 1; i < 1009; i++) {
            uint8_t *q = patient(i);
            memset(q, 0, 106);
            q[1] = 1, q[3] = 0xFF, q[44] = 100;
        }
        uint8_t *p = patient(5);
        p[0] = (uint8_t)C[k].woman, p[8] = (uint8_t)(C[k].woman ? 40 : 20);
        uint32_t first[8] = {0, 4, 0, C[k].a_set ? 5u : 2u};
        size_t n = 4;
        if (C[k].a_set) first[n++] = 0;
        first[n++] = C[k].wish ? 1u : 0u;
        onset_traced_call(0x6953D, first, n, 0);
        int ok = td_is(0, 0x695A8, 'b', 127) && td_is(1, 0x6968E, 'r', 1008) && td_is(2, 0x697ED, 'r', 90) &&
                 td_is(3, 0, 'r', 6);
        int at = 4;
        if (C[k].a_set) ok &= td_is(at++, 0, 'r', 3);
        ok &= td_is(at++, C[k].r3, 'b', 3);
        if (C[k].wish) ok &= td_is(at, C[k].wsite, 'b', 15) && p[57] == 2;
        else ok &= td_is(at, C[k].ill, 'b', 127) && p[56] == 9;
        if (!ok) fprintf(stderr, "  SIM-45 case %d\n", (int)k);
        CHECK(ok);
    }
}

/* SIM-46 and SIM-57 (docs/sim-rooms.md, "Discharge from a bed", steps 3 and 5): the Krankenzimmer's discharge clears the
 * units (48, 50-53, 54-55), the neuro ward's keeps them; an uncured patient's field 105 is the treatment's byte 7 of
 * field 43, record 0 included (byte 7 = 1 there). */
static void test_sim46_sim57_discharge(void) {
    static const uint8_t U[8] = {25, 0, 6, 20, 4, 37, 9, 3}; /* fields 48-55 */
    static const uint8_t Z[8] = {0};
    for (int neuro = 0; neuro <= 1; neuro++)
        for (int t = 0; t <= 1; t++) {
            patient_world();
            int h = 15, cat = neuro ? 16 : 6;
            g_world.block[BLK_ROOM_COUNTS][h * 100 + cat] = 1;
            g_image[0x270C + 7] = 1;           /* treatment record 0's byte 7 */
            g_image[0x270C + 30 * 38 + 7] = 2; /* treatment 30 */
            uint8_t *ward = sim_room_record(&g_sim, h, cat, 0), *p = patient(5);
            put16(ward + 0x14, 5), ward[10] = 10, ward[12] = 1;
            p[3] = (uint8_t)h, p[4] = (uint8_t)cat, p[93] = 0, p[43] = (uint8_t)(t ? 30 : 0), p[105] = 7;
            memcpy(p + 48, U, 8);
            int v;
            sim_player_discharge_in(&g_sim, h, cat, 0, 0, &v);
            CHECK(!memcmp(p + 48, neuro ? U : Z, 8));
            CHECK(get16(p + 56) == 7 && p[105] == (t ? 2 : 1)); /* not cured: the countdown goes home */
        }
}

/* SIM-47 (docs/sim-rooms.md, "0x2B989"): the seat search makes at most 46 random(10) a try. One waiting room (random(1)
 * draws nothing), every place's priority entry 3 (never P's sex, never 2): tries 1-79 find nothing, try 80 takes the
 * first draw; every step 0. */
static void test_sim47_seat_bound(void) {
    reception_world();
    memset(g_image + 0x486F, 3, 10);
    for (size_t i = 0; i < sizeof g_const_script / sizeof *g_const_script; i++) g_const_script[i] = 0;
    SimScript sc = {g_const_script, sizeof g_const_script / sizeof *g_const_script, 0, 0, NULL};
    g_sim.rng_script = &sc;
    int room = -1;
    int r = sim_player_seat(&g_sim, 15, &room);
    g_sim.rng_script = NULL;
    if (sc.used != 79u * 46 + 1) fprintf(stderr, "  SIM-47 seat search: %zu steps\n", sc.used);
    CHECK(sc.used == 79u * 46 + 1 && r == SEAT_NO_FORM);
}

/* SIM-47 (docs/sim-rooms.md, "0x2336B"): the bed rounds make at most 600, 600 and 1,200 random(10); only the place's
 * patient word is cleared (AI after the move, the player before the rounds). Every priority entry 3, every step 0 (bed
 * 0): free, the third round's first draw takes it; taken, the three rounds fail: AI leaves the place as it was, the
 * player's guard takes the first free bed (1). The place's second word (57) stays. */
static void test_sim47_bed_rounds(void) {
    for (int ai = 0; ai <= 1; ai++)
        for (int taken = 0; taken <= 1; taken++) {
            uint8_t *R = reception_world();
            g_world.block[BLK_ROOM_COUNTS][15 * 100 + 6] = 1;
            memset(g_image + 0x4DCC, 3, 10);
            uint8_t *k = sim_room_record(&g_sim, 15, 6, 0);
            k[10] = 10;
            if (taken) put16(k + 0x14, 9), k[12] = 1;
            put16(R + 0x4A, 5), put16(R + 0x4C, 57);
            for (size_t i = 0; i < sizeof g_const_script / sizeof *g_const_script; i++) g_const_script[i] = 0;
            SimScript sc = {g_const_script, sizeof g_const_script / sizeof *g_const_script, 0, 0, NULL};
            g_sim.rng_script = &sc;
            int b = sim_player_bed(&g_sim, 15, 0, 0x4A, ai);
            g_sim.rng_script = NULL;
            if (sc.used != (taken ? 2400u : 1201u)) fprintf(stderr, "  SIM-47 bed rounds: %zu steps\n", sc.used);
            CHECK(sc.used == (taken ? 2400u : 1201u));
            CHECK(b == (taken ? (ai ? -1 : 1) : 0));
            CHECK(get16(R + 0x4A) == (taken && ai ? 5 : 0) && get16(R + 0x4C) == 57);
        }
}

/* SIM-47 (docs/sim-rooms.md, "0x23060" step 7): the AI reception moves the LAST taken place of the six into ward K =
 * random(room count 6), which may be full (then nothing happens). Three Krankenzimmer: 0 with free beds, 1 and 2 full;
 * places 1 (patient 5) and 3 (patient 6, second word 57). First value 2: ward 2, nothing moves; 0: patient 6 into ward
 * 0's bed 0 (priority entries 0 = men), place 3's patient word cleared. */
static void test_sim47_last_place(void) {
    for (int k = 0; k < 2; k++) {
        uint8_t *R = reception_world(), *q = patient(6);
        put16(R + 10, 0); /* nobody at the desk */
        g_world.block[BLK_ROOM_COUNTS][15 * 100 + 6] = 3;
        uint8_t *w0 = sim_room_record(&g_sim, 15, 6, 0), *w1 = sim_room_record(&g_sim, 15, 6, 1),
                *w2 = sim_room_record(&g_sim, 15, 6, 2);
        w0[10] = 10, w1[10] = 10, w1[12] = 10, w2[10] = 4, w2[12] = 4;
        put16(R + 0x4A, 5), put16(R + 0x52, 6), put16(R + 0x54, 57);
        q[0] = 0, q[1] = 1, q[3] = 15, q[4] = 2;
        uint32_t first[1] = {k ? 0u : 2u};
        script_call(0, 0, first, 1, 1, 15, 2);
        if (k) CHECK(get16(w0 + 0x14) == 6 && w0[12] == 1 && q[4] == 6 && get16(R + 0x52) == 0);
        else CHECK(w0[12] == 0 && get16(R + 0x52) == 6 && patient(5)[4] != 6);
        CHECK(get16(R + 0x4A) == 5 && get16(R + 0x54) == 57);
    }
}

/* SIM-48 (docs/sim-rooms2.md, "0x58045" step 6): the cosmetic operation's blood loss is random(8 b) + 2 c, c = the op
 * table at index 8 b. Op 2, b = 2, the byte at 0x2470 + 16 = 4: every draw 0, blood 600 -> 592 (2 b + 8 b would give
 * 580, the table at index b 596). */
static void test_sim48_op_blood_loss(void) {
    patient_world();
    int h = 15;
    uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + h * 100, *p = patient(5);
    c[7] = 1;
    g_image[0x2470 + 2] = 2, g_image[0x2470 + 16] = 4;
    uint8_t *op = sim_room_record(&g_sim, h, 7, 0);
    put16(op + 6, 7), put16(op + 8, 8), put16(op + 10, 9);
    put16(op + 0x0C, 5), put16(op + 0x0E, 0), put16(op + 0x10, 2), put16(op + 0x12, 0);
    p[3] = (uint8_t)h, p[4] = 7, p[44] = 100, p[100] = 1, p[57] = 1, put16(p + 94, 600);
    const_call(0x58045, 0, 0, 0, 0);
    if (get16(p + 94) != 592) fprintf(stderr, "  SIM-48 blood %d\n", get16(p + 94));
    CHECK(get16(p + 94) == 592 && get16(op + 0x12) == 1);
}

/* SIM-49 (docs/sim-rooms2.md, torture operator, "Bring"): the bring raises staff W(n)'s +0x10, n = H's number of
 * Neuro-Krankenzimmer, from the measured table: disk and German CD n = 2 and 6 staff 254, 3, 10 and 14 staff 0, 9 staff
 * 129, 13 staff 130, the other n (and n = 15, not measured) nobody; the English CD the same except n = 3 (0x9000, nobody).
 * FIX-27 credits the torture record's +8 (staff 7) instead, for every n. Staff 0, 7, 129, 130 and 254 are watched. */
static void test_sim49_torture_wrong_work(void) {
    static const int WATCH[5] = {0, 7, 129, 130, 254};
    static const int DISK[16] = {-1, -1, 254, 0, -1, -1, 254, -1, -1, 129, 0, -1, -1, 130, 0, -1};
    for (int fix = 0; fix <= 1; fix++)
        for (int english = 0; english <= 1; english++)
            for (int n = 1; n <= 15; n++) {
                fixes_set(fix ? 1u << 26 : 0);
                patient_world();
                g_prog.edition = english ? EDITION_ENGLISH_CD : EDITION_DISK;
                int h = 15;
                uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + h * 100, *p = patient(5);
                c[8] = 1, c[16] = (uint8_t)n;
                uint8_t *tc = sim_room_record(&g_sim, h, 8, 0), *ward = sim_room_record(&g_sim, h, 16, 0);
                put16(tc + 8, 7), put16(tc + 10, 9);
                put16(ward + 8, 3), ward[10] = 10, ward[12] = 1, put16(ward + 0x14, 5);
                uint8_t *t = g_image + 0x270C + 30 * 38;
                t[1] = 1, t[16] = 71;
                p[3] = (uint8_t)h, p[4] = 16, p[44] = 100, p[43] = 30, p[45] = 1, p[102] = 0;
                uint8_t *st = g_world.block[BLK_STAFF];
                for (int i = 0; i < 5; i++) put16(st + WATCH[i] * 32 + 0x10, (uint16_t)(10 * i + 10));
                sim_rooms_run(&g_sim, 0x6E4C1);
                g_prog.edition = EDITION_DISK;
                int want = fix ? 7 : english && n == 3 ? -1 : DISK[n], ok = get16(tc + 0x0C) == 5;
                for (int i = 0; i < 5; i++) ok &= get16(st + WATCH[i] * 32 + 0x10) == 10 * i + 10 + (WATCH[i] == want);
                if (!ok) fprintf(stderr, "  SIM-49 fix %d english %d n %d\n", fix, english, n);
                CHECK(ok);
            }
    fixes_set(0);
}

/* SIM-56 and FIX-49 (docs/sim-rooms2.md, "Resolved open points", 0x320FE / 0x32075): a computed v = 0 takes the v >= 0
 * branch: from opinion 75 + 1 four times, from 90 three times, clamped; below 75 no change. An ordinary room bill of 8
 * and a private ward bill of 100 give v = 0. With FIX-49 a 0 changes nothing. */
static void test_sim56_bill_zero(void) {
    static const int O[4] = {50, 75, 90, 98}, WANT[4] = {50, 79, 93, 100};
    patient_world();
    uint8_t *p = patient(500);
    for (int fix = 0; fix <= 1; fix++) {
        fixes_set(fix ? 1ull << 48 : 0);
        for (int i = 0; i < 4; i++)
            for (int ward = 0; ward <= 1; ward++) {
                p[2] = (uint8_t)ward;
                p[32] = (uint8_t)O[i];
                CHECK(sim_bill_reaction(p, ward ? 100 : 8, ward) == 0);
                sim_opinion_after_bill(p, 12, ward ? 100 : 8, ward);
                CHECK(p[32] == (fix ? O[i] : WANT[i]));
            }
    }
    fixes_set(0);
}

/* ---- SIM-53, SIM-54, SIM-55, SIM-61, SIM-62, SIM-64 (docs/parity.md): rules from the clock-level comparison ----- */

/* A script of t at every step but the steps at[i], which return v[i]; the draws traced (site, kind, n, value, steps) */
typedef struct {
    unsigned site, n, v;
    char kind;
    size_t steps;
} Traced;
static Traced g_traced[8192];
static int g_ntraced;
static SimScript g_tsc;
static FILE *g_tfile;
static void traced_begin(uint32_t t, const size_t *at, const uint32_t *v, int nat) {
    for (size_t i = 0; i < sizeof g_const_script / sizeof *g_const_script; i++) g_const_script[i] = t;
    for (int i = 0; i < nat; i++) g_const_script[at[i]] = v[i];
    g_tfile = tmpfile();
    CHECK(g_tfile != NULL);
    g_tsc = (SimScript){g_const_script, sizeof g_const_script / sizeof *g_const_script, 0, 0, g_tfile};
    g_sim.rng_script = &g_tsc;
}
static size_t traced_end(void) {
    g_sim.rng_script = NULL;
    CHECK(g_tsc.extra == 0);
    g_ntraced = 0;
    if (g_tfile) {
        rewind(g_tfile);
        Traced x;
        while (g_ntraced < 8192 && fscanf(g_tfile, "%X %c%u %u %zu", &x.site, &x.kind, &x.n, &x.v, &x.steps) == 5)
            g_traced[g_ntraced++] = x;
        fclose(g_tfile);
        g_tfile = NULL;
    }
    return g_tsc.used;
}
static size_t traced_call(uint32_t routine, uint32_t t, const size_t *at, const uint32_t *v, int nat) {
    traced_begin(t, at, v, nat);
    sim_call(&g_sim, routine);
    return traced_end();
}
static long traced_step(unsigned site) { /* the script step of the first draw at the site, or -1 */
    size_t at = 0;
    for (int i = 0; i < g_ntraced; i++) {
        if (g_traced[i].site == site) return (long)at;
        at += g_traced[i].steps;
    }
    return -1;
}
static int traced_count(unsigned site) {
    int n = 0;
    for (int i = 0; i < g_ntraced; i++) n += g_traced[i].site == site;
    return n;
}

/* intensive care of hospital 3 with nurse 7 (bust given) and patient 5 (health 95, mood given, opinion of 3 = 50) */
static uint8_t *icu_world(int bust, int mood) {
    patient_world();
    g_world.block[BLK_ROOM_COUNTS][3 * 100 + 9] = 1;
    uint8_t *icu = sim_room_record(&g_sim, 3, 9, 0), *p = patient(5);
    put16(icu + 8, 7), put16(icu + 0x0C, 5), icu[0x0F] = 20;
    g_world.block[BLK_STAFF][7 * 32 + 5] = (uint8_t)bust;
    p[3] = 3, p[4] = 9, p[44] = 95, p[102] = 1, p[0x4E] = (uint8_t)mood, p[20 + 3] = 50;
    return p;
}

/* SIM-54 (docs/sim-rooms2.md, 0x34D22, "The mood draw decides"): with mood below 100 a failed mood draw (r63 >= B) ends
 * the nurse care: no opinion draw. Bust 70 (B = 10), mood 90: every draw 0 raises mood and opinion (both r63 drawn);
 * 20 at the mood draw's step leaves both and draws no second r63. */
static void test_sim54_mood_draw_ends_care(void) {
    uint8_t *p = icu_world(70, 90);
    traced_call(0x6FA5A, 0, NULL, NULL, 0);
    long k = traced_step(0x34E03);
    CHECK(k >= 0 && traced_count(0x34E03) == 1 && traced_count(0x34E1A) == 1 && p[0x4E] == 91 && p[23] == 51);
    if (k < 0) return;
    p = icu_world(70, 90);
    size_t at = (size_t)k;
    uint32_t v = 20;
    traced_call(0x6FA5A, 0, &at, &v, 1);
    CHECK(traced_count(0x34E03) == 1 && traced_count(0x34E1A) == 0 && p[0x4E] == 90 && p[23] == 50);
}

/* SIM-62 and FIX-53 (docs/sim-rooms2.md, 0x34D22, "B is an 8-bit difference"): (a) a Krankenzimmer bed without a nurse
 * gets the nurse care from staff record 0 (B = 196): every draw 0, the first r127 (0x34D93) and the mood r63 are drawn;
 * FIX-53: no care. (b) a nurse with bust 50: B = 246 and every draw of 100 passes (mood 50 -> 51); FIX-53: B = -10, the
 * first r127 fails, the mood stays. */
static void test_sim62_care_without_nurse(void) {
    for (int fix = 0; fix <= 1; fix++) {
        fixes_set(fix ? 1ull << 52 : 0);
        uint8_t *ward = ward_world(6), *p = patient(5);
        put16(ward + 8, 0), ward[15] = 20;
        p[102] = 1;
        traced_call(0x6A0BA, 0, NULL, NULL, 0);
        CHECK(traced_count(0x34D93) == !fix && traced_count(0x34E03) == !fix);
        p = icu_world(50, 50);
        traced_call(0x6FA5A, 100, NULL, NULL, 0);
        CHECK(traced_count(0x34D93) == 1 && traced_count(0x34E03) == !fix && p[0x4E] == (fix ? 50 : 51));
    }
    fixes_set(0);
}

/* SIM-61 (docs/sim-staff.md, "The waiting-room operator ends the call after an assignment"): the call that staffs the
 * empty +8 ends there, without the comfort r7 (0x2CB06); the next call, staffed, draws it. */
static void test_sim61_assignment_ends_call(void) {
    patient_world();
    g_world.block[BLK_ROOM_COUNTS][15 * 100 + 3] = 1;
    uint8_t *wr = sim_room_record(&g_sim, 15, 3, 0);
    wr[10] = 10, wr[12] = 1, wr[0x0F] = 0;
    put_slot(15, 0, 7, 1, 0); /* an idle nurse */
    world_set_tail(&g_world, TAIL_HOURS, 51);
    world_set_tail(&g_world, TAIL_HOUR, 10);
    for (int call = 0; call < 2; call++) {
        logged_call(0x2C993);
        int r7 = 0;
        for (int i = 0; i < g_nlogged; i++) r7 += g_logged[i].site == 0x2CB06;
        CHECK(get16(wr + 8) == 7 && r7 == call);
    }
}

/* SIM-53 (docs/sim-staff.md, "Three more rules of the sales"): neither sale recomputes the room, so the record's rating
 * bytes 0 and 1 keep their values (sentinels 0x77, 0x66) after an item leaves a full list. */
static void test_sim53_sales_keep_rating(void) {
    uint8_t *b = bank_world(15);
    b[0] = 0x77, b[1] = 0x66;
    for (int i = 0; i < 10; i++) put16(b + 0x1C + 2 * i, 3), b[0x94 + i] = 50; /* furniture item 3: kind 0, ratings 0 */
    for (int i = 0; i < 20; i++) put16(b + 0x30 + 2 * i, 3), b[0x9E + i] = 50;
    memset(g_world.block[BLK_FURNITURE_MARKET], 0xFF, 300 * 8);
    memset(g_world.block[BLK_EQUIPMENT_MARKET], 0xFF, 300 * 8);
    CHECK(sim_furniture_room(&g_sim, 15, 17, b) == 1 && g_world.block[BLK_FURNITURE_MARKET][0] == 15);
    CHECK(sim_equipment_room(&g_sim, 15, 17, b) == 1 && g_world.block[BLK_EQUIPMENT_MARKET][0] == 15);
    int furniture = 0, equipment = 0;
    for (int i = 0; i < 10; i++) furniture += get16(b + 0x1C + 2 * i) == 3;
    for (int i = 0; i < 20; i++) equipment += get16(b + 0x30 + 2 * i) == 3;
    CHECK(furniture == 9 && equipment == 19 && b[0] == 0x77 && b[1] == 0x66);
}

/* SIM-64 (docs/sim-ai.md, the wear, "Per slot, exactly"), a kitchen of hospital 15 (5 furniture slots, 10 equipment),
 * every draw 2 (the gate t&3 = 2 passes, no r7 = 0): (a) furniture slot 0 at condition 0 goes without a draw and ends
 * the furniture pass (slot 1 kept, undrawn); (b) an empty furniture slot with condition 5 is drawn for like an item,
 * the hole at condition 0 behind slot 1 ends the pass; (c) equipment: condition 0 removed without a draw, the pass goes
 * on (slot 1 drawn), ids 200 and 0xFFFF cleared with their condition, no draw; the empty slots draw nothing. */
static void test_sim64_wear_per_slot(void) {
    for (int c = 0; c < 3; c++) {
        wear_world();
        g_world.block[BLK_ROOM_COUNTS][15 * 100 + 13] = 1;
        uint8_t *r = sim_room_record(&g_sim, 15, 13, 0);
        if (c == 0) put16(r + 0x40, 2), r[0x90] = 0, put16(r + 0x42, 3), r[0x91] = 50;
        if (c == 1) r[0x90] = 5, put16(r + 0x42, 3), r[0x91] = 50;
        if (c == 2) {
            put16(r + 0x40, 3), r[0x90] = 50;
            put16(r + 0x60, 6), r[0xA0] = 0, put16(r + 0x62, 6), r[0xA1] = 9;
            put16(r + 0x64, 200), r[0xA2] = 7, put16(r + 0x66, 0xFFFF), r[0xA3] = 8;
        }
        traced_begin(2, NULL, NULL, 0);
        sim_recompute_wear(&g_sim, 13, 1);
        traced_end();
        int gate = traced_count(0x2B5E3), fd = traced_count(0x2B638), ed = traced_count(0x2B699);
        if (fd != (c == 1 ? 2 : c == 2 ? 1 : 0) || ed != (c == 2 ? 1 : 0))
            fprintf(stderr, "  SIM-64 case %d: gate %d, furniture %d, equipment %d draws\n", c, gate, fd, ed);
        CHECK(gate == 1 && fd == (c == 1 ? 2 : c == 2 ? 1 : 0) && ed == (c == 2 ? 1 : 0));
        if (c == 0) CHECK(get16(r + 0x40) == 0 && get16(r + 0x42) == 3 && r[0x91] == 50);
        if (c == 1) CHECK(r[0x90] == 5 && get16(r + 0x42) == 3);
        if (c == 2) {
            CHECK(get16(r + 0x60) == 0 && get16(r + 0x62) == 6 && r[0xA1] == 9);
            CHECK(get16(r + 0x64) == 0 && r[0xA2] == 0 && get16(r + 0x66) == 0 && r[0xA3] == 0);
        }
    }
}

/* SIM-55 (docs/mainloop.md, module H item 2, "Who leaves the value"): the writers of [0x45CF] (Sim.dup_byte) the clock
 * reaches: the waiting-room pick ends with 0 (a patient found) or FF (none); the store's equipment purchase and the
 * furniture purchase end with 1 (bought) or 0 (nothing on the market). */
static void test_sim55_dup_byte_writers(void) {
    static const int FOUND[2] = {0, -1}, NONE[2] = {-1, -1}, WISH[2] = {0, 0};
    take_world(15, 5, FOUND, WISH);
    g_sim.dup_byte = 0x55;
    CHECK(sim_ai_bring(&g_sim, 15, 5, 0) == 20 && g_sim.dup_byte == 0);
    take_world(15, 5, NONE, WISH);
    g_sim.dup_byte = 0x55;
    CHECK(sim_ai_bring(&g_sim, 15, 5, 0) == 0 && g_sim.dup_byte == 0xFF);
    static const int DENTAL[2] = {21, -1}; /* the dental bring wants type 1: the found 0 replaces the wanted 1 */
    take_world(15, 11, DENTAL, WISH);
    g_sim.dup_byte = 0x55;
    CHECK(sim_ai_bring(&g_sim, 15, 11, 0) == 20 && g_sim.dup_byte == 0);
    for (int offered = 0; offered <= 1; offered++) {
        fresh();
        int h = 15;
        uint8_t *mk = g_world.block[BLK_EQUIPMENT_MARKET], *fm = g_world.block[BLK_FURNITURE_MARKET];
        memset(mk, 0xFF, 300 * 8), memset(fm, 0xFF, 300 * 8);
        if (offered) {
            mk[0] = 3, mk[1] = 50, put16(mk + 2, 7), mk[4] = 80, mk[5] = 0;
            fm[0] = 3, fm[1] = 5, put16(fm + 2, 4), fm[4] = 80, fm[5] = 0;
        }
        g_image[0x144A + 7 * 20 + 10] = 0x20; /* equipment item 7 has capability 0x20 */
        g_image[0xEE0 + 18 * 4 + 6] = 7;      /* furniture item 4 is of kind 7 */
        g_world.block[BLK_ROOM_COUNTS][h * 100 + 4] = 1;
        uint8_t *st = sim_room_record(&g_sim, h, 4, 0), *r = world_hospital(&g_world, h);
        st[7] = 20;
        world_set_cash(&g_world, h, 1000);
        r[0xDA] = 0x20;
        g_sim.dup_byte = 0x55;
        sim_staff_run(&g_sim, 0x2F3FF);
        CHECK(g_sim.dup_byte == offered && r[0xDA] == (offered ? 0 : 0x20));
        r[0xDC] = 7;
        g_sim.dup_byte = 0x55;
        sim_staff_run(&g_sim, 0x390BA);
        CHECK(g_sim.dup_byte == offered && r[0xDC] == (offered ? 0 : 7));
    }
}

/* SIM-82/SIM-83: a live, unsaved record reference with measured physical adjacency. */
static void test_rating_reference(void) {
    fresh();
    CHECK(g_sim.rating_ref.block == 0 && sim_rating_byte(&g_sim) == 110);
    uint8_t *room = sim_room_record(&g_sim, 3, 11, 0);
    room[1] = 13;
    sim_rating_record(&g_sim, room);
    CHECK(g_sim.rating_ref.block == 22 && g_sim.rating_ref.offset == 5400);
    CHECK(sim_rating_byte(&g_sim) == 13);
    room[1] = 94;
    CHECK(sim_rating_byte(&g_sim) == 94); /* reference, not a cached value */
    sim_room_record(&g_sim, 5, 15, 0)[1] = 7;
    CHECK(sim_rating_byte(&g_sim) == 94); /* a lookup is not a writer */
    static const unsigned NEXT[][2] = {{4, 5}, {23, 24}, {26, 31}, {31, 1}, {1, 2}, {32, 27}};
    for (size_t i = 0; i < sizeof NEXT / sizeof *NEXT; i++) {
        unsigned a = NEXT[i][0], b = NEXT[i][1];
        g_world.block[b - 1][1] = (uint8_t)(60 + i);
        g_sim.rating_ref = (SimRatingRef){a, g_world.size[a - 1], SIM_RATING_WORLD};
        CHECK(sim_rating_byte(&g_sim) == 60 + i);
    }
    g_sim.rating_ref = (SimRatingRef){2, g_world.size[1], SIM_RATING_WORLD};
    g_world.block[31][1] = 87; /* there is a gap here, unlike the preceding spans */
    CHECK(sim_rating_byte(&g_sim) == 0);
    g_sim.rating_ref = (SimRatingRef){36, SIZE_MAX, SIM_RATING_WORLD};
    CHECK(sim_rating_byte(&g_sim) == 0);
    sim_reset_generator(&g_sim, 9);
    CHECK(g_sim.rating_ref.block == 0 && g_sim.rating_ref.offset == 0 && sim_rating_byte(&g_sim) == 110);
    sim_rating_room(&g_sim, 2, 5, 0);
    sim_rating_record(&g_sim, patient(5)); /* nested writer before a loop's increment */
    sim_rating_advance(&g_sim, 180);
    CHECK(g_sim.rating_ref.block == 1 && g_sim.rating_ref.offset == 710);
}

/* Chooser -> decision order: rating 100 yields skill 25, current rating 0 yields skill 0.
 * The same r7=1,r127=20 chooses best only from the stale room; the extra rank draws are observable too. */
static void test_rating_chooser(void) {
    static const int CAT[] = {5, 11, 15};
    static const uint32_t DRAW[] = {1, 20, 0, 0, 0, 0};
    for (unsigned c = 0; c < sizeof CAT / sizeof *CAT; c++) {
        uint8_t *tr = doctor_room(15, CAT[c]), *p = patient(5);
        uint8_t *other = sim_room_record(&g_sim, 2, 11, 0);
        uint8_t *ill = g_image + 0x954 + 7 * 20;
        ill[8] = CAT[c] == 15 ? 2 : CAT[c] == 11 ? 1 : 0;
        ill[15] = CAT[c] == 11 ? 3 : 0;
        p[42] = 7;
        other[1] = 100;
        sim_rating_record(&g_sim, other);
        SimScript sc = {DRAW, 6, 0, 0, NULL};
        g_sim.rng_script = &sc;
        sim_ai_choose(&g_sim, 15, CAT[c], 0);
        g_sim.rng_script = NULL;
        CHECK(p[43] == 30 && sc.used == 2 && sc.extra == 0);
        CHECK(g_sim.rating_ref.block == 22 && g_sim.rating_ref.offset == 3600);
        /* The decision refreshes after the chooser, but only on its fresh-decision branch. */
        p[79] = 0; /* treatment 30 has no steps in this fixture */
        sim_ai_act(&g_sim, 15, CAT[c], 0);
        CHECK(sim_rating_byte(&g_sim) == 0);
        CHECK(g_sim.rating_ref.block == (unsigned)(CAT[c] == 5 ? 16 : CAT[c] == 11 ? 22 : 23));
        put16(tr + 16, 0), put16(tr + 18, 0);
        sc.used = sc.extra = 0;
        g_sim.rng_script = &sc;
        sim_ai_choose(&g_sim, 15, CAT[c], 0);
        g_sim.rng_script = NULL;
        CHECK(sc.used == (CAT[c] == 11 ? 6u : 5u) && sc.extra == 0);
        /* A live change to the stale room is seen without reselecting it. */
        sim_rating_record(&g_sim, other);
        other[1] = 0;
        sc.used = sc.extra = 0;
        g_sim.rng_script = &sc;
        sim_player_propose_in(&g_sim, 15, CAT[c], 0);
        g_sim.rng_script = NULL;
        CHECK(sc.used == (CAT[c] == 11 ? 4u : 3u) && sc.extra == 0);
    }
}

static void test_rating_staff_and_fittings(void) {
    fresh();
    room_descriptions();
    uint8_t *prior = patient(5), *rec = sim_room_record(&g_sim, 15, 5, 0);
    prior[1] = 123;
    rec[0] = 200, rec[1] = 41;
    sim_rating_record(&g_sim, prior);
    uint8_t *c = room_counts(&g_sim, 15);
    uint8_t want = 0;
    sim_fittings(&g_sim, 15, 5, rec, &want); /* no store */
    CHECK(sim_rating_byte(&g_sim) == 123);
    c[4] = 1;
    want = 6;
    sim_fittings(&g_sim, 15, 5, rec, &want); /* pending request, store lacks it */
    CHECK(sim_rating_byte(&g_sim) == 123);
    want = 0;
    sim_fittings(&g_sim, 15, 5, rec, &want);
    CHECK(sim_rating_byte(&g_sim) == 41);
    uint8_t *sl = staff_slots(&g_sim, 15);
    put16(sl, 7), sl[2] = 2;
    put16(sl + 40, 9), sl[42] = 2;
    g_world.block[BLK_STAFF][7 * 32 + 21] = 10;
    g_world.block[BLK_STAFF][9 * 32 + 21] = 10;
    int slot = -1;
    CHECK(sim_idle_candidate(&g_sim, 15, 2, &slot) == 9 && slot == 2);
    CHECK(g_sim.rating_ref.block == 15 && g_sim.rating_ref.offset == 60040);
    sl[41] = 77; /* this is the selected slot's byte 1, not a staff-person attribute */
    CHECK(sim_rating_byte(&g_sim) == 77);
    CHECK(sim_idle_candidate(&g_sim, 15, 3, &slot) == 0);
    CHECK(sim_rating_byte(&g_sim) == 77);
    /* Recompute changes a selected room's bytes while preserving its identity. */
    c[5] = 1;
    sim_rating_record(&g_sim, rec);
    SimRatingRef before = g_sim.rating_ref;
    sim_recompute_all(&g_sim);
    CHECK(g_sim.rating_ref.block == before.block && g_sim.rating_ref.offset == before.offset);
    CHECK(sim_rating_byte(&g_sim) == rec[1] && rec[1] == 0);
    rec[1] = 55;
    sim_recompute_all_wear(&g_sim);
    CHECK(g_sim.rating_ref.block == before.block && g_sim.rating_ref.offset == before.offset);
    CHECK(sim_rating_byte(&g_sim) == 0);
}

/* The loop's final reference is one past its last processed room, including continue paths and full blocks. */
static void test_rating_loops(void) {
    static const struct { uint32_t routine; int cat, block, stride, area, capacity; } CASES[] = {
        {0x6AD3C, 5, 16, 180, 1800, 10}, {0x6B445, 11, 22, 180, 1800, 10},
        {0x6BAE4, 15, 23, 180, 1800, 10}, {0x6A0BA, 6, 17, 240, 2400, 10},
        {0x6C3D5, 16, 18, 240, 2400, 10}, {0x6ABB9, 6, 17, 240, 2400, 10},
        {0x6CE07, 16, 18, 240, 2400, 10}, {0x6BFC8, 3, 12, 180, 1800, 10},
        {0x58045, 7, 19, 216, 1296, 6}, {0x597F9, 8, 20, 216, 1296, 6},
        {0x6FA5A, 9, 21, 186, 1860, 10}};
    for (size_t i = 0; i < sizeof CASES / sizeof *CASES; i++) {
        fresh();
        patient(5)[1] = 89;
        sim_rating_record(&g_sim, patient(5));
        sim_rooms_run(&g_sim, CASES[i].routine);
        CHECK(sim_rating_byte(&g_sim) == 89); /* no rooms: no writer */
        room_counts(&g_sim, 2)[CASES[i].cat] = 2;
        room_counts(&g_sim, 9)[CASES[i].cat] = 3;
        /* empty rooms take the early continue in the treatment, ICU and OP/torture loops */
        sim_rooms_run(&g_sim, CASES[i].routine);
        CHECK(g_sim.rating_ref.block == (unsigned)CASES[i].block);
        CHECK(g_sim.rating_ref.offset == (size_t)(9 * CASES[i].area + 3 * CASES[i].stride));
        room_counts(&g_sim, 15)[CASES[i].cat] = (uint8_t)CASES[i].capacity;
        g_world.block[CASES[i].block][1] = 67; /* following physical block's byte 1 */
        sim_rooms_run(&g_sim, CASES[i].routine);
        CHECK(g_sim.rating_ref.offset == g_world.size[CASES[i].block - 1]);
        CHECK(sim_rating_byte(&g_sim) == 67);
    }
    fresh();
    g_world.block[4][1] = 76;
    sim_rooms_run(&g_sim, 0x67611); /* no reception anywhere still leaves record 16 */
    CHECK(g_sim.rating_ref.block == 4 && g_sim.rating_ref.offset == 16 * 174);
    CHECK(sim_rating_byte(&g_sim) == 76);
    /* Occupied but unstaffed treatment rooms take a different early continue. */
    for (int c = 0; c < 3; c++) {
        uint8_t *tr = doctor_room(15, ROOM_CATS[c]);
        put16(tr + 8, 0);
        sim_rooms_run(&g_sim, ROOM_TICK[c]);
        CHECK(g_sim.rating_ref.offset == 15 * 1800 + 180);
    }
}

static void test_rating_fittings_install_and_clerk(void) {
    fresh();
    room_descriptions();
    uint8_t *c = room_counts(&g_sim, 15), *ward = sim_room_record(&g_sim, 15, 6, 0);
    uint8_t *st = sim_room_record(&g_sim, 15, 4, 0);
    c[4] = c[6] = 1;
    g_image[0xEE0 + 18 + 6] = 6;
    put16(st + STORE_FURNITURE, 1), st[STORE_FURNITURE_HELD] = 1;
    st[STORE_FURNITURE_COND] = 100;
    patient(5)[1] = 81;
    sim_rating_record(&g_sim, patient(5));
    ward[4] = 6;
    sim_fittings(&g_sim, 15, 6, ward, ward + 4);
    CHECK(!ward[4] && get16(ward + 0x40) == 1); /* fulfilled request is still not a context write */
    CHECK(sim_rating_byte(&g_sim) == 81);
    for (int clerk = 0; clerk <= 1; clerk++) {
        ward[0] = 200, ward[1] = 37;
        sim_rating_record(&g_sim, patient(5));
        put16(st + 4, clerk ? 8 : 0);
        const_call(0x2995B, 15, 0, 0, 0); /* no eligible nurse, no later writer */
        CHECK(sim_rating_byte(&g_sim) == (clerk ? 37 : 81));
    }
}

static void test_rating_decision_and_discharge(void) {
    for (int c = 0; c < 3; c++)
        for (int branch = 0; branch < 4; branch++) {
            uint8_t *tr = doctor_room(15, ROOM_CATS[c]), *p = patient(5);
            uint8_t *prior = patient(6);
            p[42] = 7, p[43] = 30, p[1] = 77;
            prior[1] = 83;
            sim_rating_record(&g_sim, prior);
            put16(tr + 18, branch == 0 ? 1 : branch == 1 ? 999 : 0);
            put16(tr + 16, branch == 2 ? 30 : 0);
            p[79] = branch == 3 ? 1 : 0;
            sim_ai_act(&g_sim, 15, ROOM_CATS[c], 0);
            CHECK(sim_rating_byte(&g_sim) == (branch < 2 ? 83 : 77));
        }
    for (int cat = 6; cat <= 16; cat += 10) {
        uint8_t *ward = ward_world(cat), *p = patient(5);
        p[1] = 73;
        int bill;
        sim_player_discharge_in(&g_sim, 15, cat, 0, 0, &bill);
        CHECK(get16(ward + 0x14) == 0 && sim_rating_byte(&g_sim) == 73);
        CHECK(g_sim.rating_ref.block == 1 && g_sim.rating_ref.offset == 530);
        ward = ward_world(cat);
        p = patient(5);
        p[1] = 71, p[56] = 0, p[93] = 0; /* cured, no step: operator discharges without the player-message helper */
        static const uint32_t PICK[] = {15, 15};
        script_call(cat == 6 ? 0x2995B : 0x2A01F, 0, PICK, 2, 0, 0, 0);
        CHECK(get16(ward + 0x14) == 0 && sim_rating_byte(&g_sim) == 71);
    }
    for (int c = 0; c < 3; c++) {
        doctor_room(0, ROOM_CATS[c]);
        patient(5)[1] = 69;
        int bill;
        sim_player_bill_in(&g_sim, 0, ROOM_CATS[c], 0, &bill);
        CHECK(sim_rating_byte(&g_sim) == 69 && g_sim.rating_ref.offset == 530);
    }
}

static void rating_operator_call(uint32_t routine) {
    for (size_t i = 0; i < sizeof g_const_script / sizeof *g_const_script; i++) g_const_script[i] = 15;
    SimScript sc = {g_const_script, sizeof g_const_script / sizeof *g_const_script, 0, 0, NULL};
    g_sim.rng_script = &sc;
    CHECK(sim_call(&g_sim, routine));
    g_sim.rng_script = NULL;
    CHECK(sc.extra == 0);
}

static void test_rating_operator_writers(void) {
    static const struct { uint32_t routine; int cat; } CASES[] = {
        {0x23060, 2}, {0x2C993, 3}, {0x2CB75, 5}, {0x34535, 11}, {0x35430, 15},
        {0x2995B, 6}, {0x2A01F, 16}, {0x6E8BE, 7}, {0x6E4C1, 8}, {0x6F346, 9},
        {0x533CD, 14}, {0x6E161, 13}, {0x535DC, 17}, {0x6DA45, 10}};
    for (size_t i = 0; i < sizeof CASES / sizeof *CASES; i++) {
        fresh();
        patient(5)[1] = 91;
        sim_rating_record(&g_sim, patient(5));
        rating_operator_call(CASES[i].routine); /* missing room is before the writer */
        CHECK(sim_rating_byte(&g_sim) == 91);
        uint8_t *c = room_counts(&g_sim, 15), *rec = sim_room_record(&g_sim, 15, CASES[i].cat, 0);
        c[CASES[i].cat] = 1;
        if (i < 10) c[4] = 1; /* common fittings need a store; special operators do not */
        if (CASES[i].cat == 2) c[3] = 1;
        if (CASES[i].cat == 6) put16(sim_room_record(&g_sim, 15, 4, 0) + 4, 8);
        if (CASES[i].cat == 10) c[7] = 1;
        rec[0] = 200, rec[1] = 43;
        rating_operator_call(CASES[i].routine);
        CHECK(sim_rating_byte(&g_sim) == 43); /* no idle staff: the search cannot overwrite it */
    }
}

static void test_rating_recomputes(void) {
    static const struct { int cat, block, stride, area, cap; } CASES[] = {
        {2,4,174,174,1}, {3,12,180,1800,10}, {5,16,180,1800,10}, {6,17,240,2400,10},
        {7,19,216,1296,6}, {8,20,216,1296,6}, {9,21,186,1860,10}, {10,5,158,158,1},
        {11,22,180,1800,10}, {12,10,118,118,1}, {13,8,218,218,1}, {14,9,118,118,1},
        {15,23,180,1800,10}, {16,18,240,2400,10}, {17,6,178,178,1}, {18,7,158,158,1}};
    for (size_t i = 0; i < sizeof CASES / sizeof *CASES; i++) {
        fresh();
        room_descriptions();
        sim_rating_record(&g_sim, patient(5));
        sim_recompute(&g_sim, CASES[i].cat);
        CHECK(g_sim.rating_ref.block == (unsigned)(CASES[i].cap == 1 ? CASES[i].block : 1));
        CHECK(g_sim.rating_ref.offset == (size_t)(CASES[i].cap == 1 ? 16 * CASES[i].stride : 530));
        room_counts(&g_sim, 2)[CASES[i].cat] = 1;
        room_counts(&g_sim, 9)[CASES[i].cat] = 1;
        sim_recompute(&g_sim, CASES[i].cat);
        CHECK(g_sim.rating_ref.block == (unsigned)CASES[i].block);
        CHECK(g_sim.rating_ref.offset == (size_t)(CASES[i].cap == 1 ? 16 * CASES[i].stride : 9 * CASES[i].area + CASES[i].stride));
        room_counts(&g_sim, 15)[CASES[i].cat] = (uint8_t)CASES[i].cap;
        sim_recompute_wear(&g_sim, CASES[i].cat, 1);
        CHECK(g_sim.rating_ref.offset == g_world.size[CASES[i].block - 1]);
    }
    /* Aggregate wrappers preserve a program-table identity too, while their children select World records. */
    sim_rating_catalog(&g_sim, 3);
    sim_recompute_all(&g_sim);
    CHECK(g_sim.rating_ref.kind == SIM_RATING_CATALOG && g_sim.rating_ref.offset == 180);
    sim_recompute_all_wear(&g_sim);
    CHECK(g_sim.rating_ref.kind == SIM_RATING_CATALOG && g_sim.rating_ref.offset == 180);
}

static void test_rating_icu_and_bank(void) {
    static const int EMPTY_WARD[] = {0};
    CHECK(sim28_choice(6, 1, 0, 1, EMPTY_WARD) == 10);
    CHECK(g_sim.rating_ref.block == 21 && g_sim.rating_ref.offset == 15 * 1860);
    put16(sim_room_record(&g_sim, 15, 9, 0) + 12, 0x100);
    CHECK(sim_ai_choice(&g_sim, 15) == 9); /* an occupied high byte also requires another ICU */
    fresh();
    uint8_t *prior = patient(5);
    prior[1] = 93;
    sim_rating_record(&g_sim, prior);
    CHECK(sim_free_icu(&g_sim, 3) == -1 && sim_rating_byte(&g_sim) == 93);
    room_counts(&g_sim, 3)[9] = 3;
    for (int i = 0; i < 3; i++) put16(sim_room_record(&g_sim, 3, 9, i) + 12, 0x100);
    CHECK(sim_free_icu(&g_sim, 3) == -1 && sim_rating_byte(&g_sim) == 93); /* full word, not low byte */
    for (int i = 2; i >= 0; i--) {
        uint8_t *r = sim_room_record(&g_sim, 3, 9, i);
        put16(r + 12, 0), r[1] = (uint8_t)(70 + i);
        CHECK(sim_player_icu(&g_sim, 3, 0, 5) == -2); /* no ward nurse, but the search already wrote */
        CHECK(sim_rating_byte(&g_sim) == 70 + i);
        CHECK(g_sim.rating_ref.offset == (size_t)(3 * 1860 + i * 186));
    }
    for (int cooled = 0; cooled <= 1; cooled++)
        for (int keeper = 0; keeper <= 1; keeper++) {
            uint8_t *b = bank_world(15);
            put16(b + 10, keeper ? 7 : 0), b[1] = 61;
            if (cooled) put16(sim_room_record(&g_sim, 0, 17, 0) + 0x30, 2);
            patient(5)[1] = 93;
            sim_rating_record(&g_sim, patient(5));
            sim_rooms_run(&g_sim, 0x57E03);
            CHECK(sim_rating_byte(&g_sim) == (cooled && keeper ? 61 : 93));
        }
}

static void test_rating_catalog(void) {
    fresh();
    static const ProgramPoint POINT[] = {{CATALOG, 0x7000}};
    static const ProgramSegment SEGMENT[] = {{0, 0x10000, 0}};
    static const ProgramBuild BUILD = {.segments = SEGMENT, .segment_count = 1, .points = POINT, .point_count = 1};
    g_prog.build = &BUILD;
    g_image[0x7000 + 37 * 60 + 1] = 81;
    g_image[CATALOG + 37 * 60 + 1] = 22;
    sim_rating_catalog(&g_sim, 37);
    CHECK(sim_rating_byte(&g_sim) == 81);
    g_image[0x7000 + 37 * 60 + 1] = 92;
    CHECK(sim_rating_byte(&g_sim) == 92);
    sim_rating_advance(&g_sim, 60);
    CHECK(sim_rating_byte(&g_sim) == 0); /* the catalog bound, not unrelated following tables */
    sim_rating_catalog(&g_sim, -1);
    CHECK(sim_rating_byte(&g_sim) == 0);
    sim_rating_catalog(&g_sim, 0);
    sim_reset_generator(&g_sim, 1);
    CHECK(g_sim.rating_ref.kind == SIM_RATING_WORLD && sim_rating_byte(&g_sim) == 110);
    g_prog.build = NULL;
}

static void test_rating_ai_construction(void) {
    for (int branch = 0; branch < 4; branch++) {
        ai_world(branch == 2 ? 49 : 900);
        uint8_t *r = world_hospital(&g_world, 15);
        put16(r + H_PLOTS_OWNED, branch == 1 ? 0 : 3);
        if (!branch) memset(room_slots(&g_sim, 15), 0, 400);
        patient(5)[1] = 89;
        sim_rating_record(&g_sim, patient(5));
        g_image[CATALOG + 1] = 66;
        rating_operator_call(0x6337D);
        CHECK(room_counts(&g_sim, 15)[1] == (branch == 3));
        CHECK(sim_rating_byte(&g_sim) == (branch == 3 ? 66 : 89));
    }
}

/* SIM-84: batches of synthetic records exercise the entire byte domains. Untouched records and
 * bytes, including health history, must also remain identical. High-bit inputs are intentional. */
static uint8_t g_clamp_expected[1009 * 106];

static void clamp_fixture(void) {
    fresh();
    memset(patient(0), 0xA5, sizeof g_clamp_expected);
    for (int i = 0; i < 1009; i++) {
        uint8_t *p = patient(i);
        p[3] = 0xFF, p[44] = 100, p[78] = 50, p[56] = p[57] = 0;
    }
    memcpy(g_clamp_expected, patient(0), sizeof g_clamp_expected);
}

static uint8_t *clamp_patient(int i) {
    uint8_t *p = patient(i);
    p[3] = 0, p[4] = 6, p[40] = 0, p[44] = 100, p[56] = 1, p[57] = 0;
    p[78] = 50, p[102] = 0, p[104] = 0;
    return p;
}

static uint8_t *clamp_expect(int i) {
    uint8_t *e = g_clamp_expected + i * 106;
    memcpy(e, patient(i), 106);
    return e;
}

static void clamp_check(uint32_t raw, size_t draws) {
    uint32_t values[1024];
    for (size_t i = 0; i < 1024; i++) values[i] = raw;
    SimScript script = {values, 1024, 0, 0, NULL};
    g_sim.rng_script = &script;
    CHECK(sim_patients_run(&g_sim, 0x67ABD));
    g_sim.rng_script = NULL;
    CHECK(script.used == draws && script.extra == 0);
    if (memcmp(patient(0), g_clamp_expected, sizeof g_clamp_expected)) {
        for (size_t i = 0; i < sizeof g_clamp_expected; i++)
            if (patient(0)[i] != g_clamp_expected[i]) {
                fprintf(stderr, "  health clamps patient %zu field %zu: %u, expected %u (raw %u)\n",
                        i / 106, i % 106, patient(0)[i], g_clamp_expected[i], raw);
                break;
            }
        CHECK(!memcmp(patient(0), g_clamp_expected, sizeof g_clamp_expected));
    }
}

static void test_sim84_hunger(void) {
    static const int STATES[] = {5, 6, 11, 15, 16, 17, 18, 128, 255};
    static const uint32_t RAW[] = {3, 4, 15};
    for (size_t r = 0; r < 3; r++)
        for (size_t st = 0; st < sizeof STATES / sizeof *STATES; st++) {
            clamp_fixture();
            int state = STATES[st];
            size_t draws = 0;
            for (int v = 0; v < 256; v++) {
                uint8_t *p = clamp_patient(v + 1);
                p[4] = (uint8_t)state, p[40] = (uint8_t)v;
                uint8_t *e = clamp_expect(v + 1);
                if (state == 18) continue;
                if (state == 6 || state == 16 || state == 17) {
                    if (v >= 100 && v <= 127) e[40] = 100;
                    else {
                        draws++;
                        if (RAW[r] == 4) e[40] = (uint8_t)((v + 1) % 256);
                    }
                } else e[40] = 0;
            }
            clamp_check(RAW[r], draws);
        }
}

static int signed_byte_value(int v) { return v < 128 ? v : v - 256; }

static void test_sim84_health_and_mood(void) {
    static const int BOUNDARY[] = {0, 100, 127, 128, 255};
    static const uint32_t RAW[] = {3, 4, 15};
    for (size_t r = 0; r < 3; r++)
        for (size_t m = 0; m < 5; m++)
            for (size_t low = 0; low < 5; low++) {
                clamp_fixture();
                g_image[0x954 + 20 + 19] = (uint8_t)BOUNDARY[low];
                size_t draws = 0;
                for (int v = 0; v < 256; v++) {
                    uint8_t *p = clamp_patient(v + 1);
                    p[3] = 0xFF, p[44] = (uint8_t)v, p[78] = (uint8_t)BOUNDARY[m];
                    uint8_t *e = clamp_expect(v + 1);
                    if (signed_byte_value(BOUNDARY[m]) > signed_byte_value(v)) e[78] = (uint8_t)v;
                    if (signed_byte_value(BOUNDARY[low]) > signed_byte_value(v)) {
                        e[44] = (uint8_t)((BOUNDARY[low] + (RAW[r] % 4)) % 256);
                        draws++;
                    }
                }
                clamp_check(RAW[r], draws);
            }
}

static void test_sim84_states_narcosis_death(void) {
    static const uint32_t RAW[] = {3, 4, 15};
    for (size_t r = 0; r < 3; r++)
        for (int domain = 0; domain < 3; domain++) {
            clamp_fixture();
            size_t draws = 0;
            for (int v = 0; v < 256; v++) {
                uint8_t *p = clamp_patient(v + 1);
                p[4] = domain == 0 ? (uint8_t)v : domain == 1 ? 5 : 6;
                p[40] = domain == 1 ? 0 : 99;
                p[102] = domain == 1 ? (uint8_t)v : 10;
                p[104] = domain == 2 ? (uint8_t)v : 0;
                uint8_t *e = clamp_expect(v + 1);
                if (domain == 0) {
                    if (v == 18) continue;
                    if (v >= 6 && v <= 127 && v != 11 && v != 15) {
                        draws++;
                        if (RAW[r] == 4) e[40] = 100;
                    } else e[40] = 0;
                    e[102] = (uint8_t)(10 - RAW[r] % 2);
                    draws++;
                } else if (domain == 1) {
                    if (v >= 10 && v <= 127) {
                        e[102] = (uint8_t)(v - RAW[r] % 2);
                        draws++;
                    }
                } else if (v) {
                    e[20] = 0;
                    if (v == 100) continue;
                    draws++;
                    if (RAW[r] == 3) {
                        e[104] = (uint8_t)((v + 1) % 256);
                        draws++;
                    }
                } else {
                    if (RAW[r] == 4) e[40] = 100;
                    e[102] = (uint8_t)(10 - RAW[r] % 2);
                    draws += 2;
                }
            }
            clamp_check(RAW[r], draws);
        }
}

static void test_sim84_exits_and_alignment(void) {
    clamp_fixture();
    static const int HOSP[] = {16, 128, 255, 0, 0, 0};
    for (int i = 0; i < 6; i++) {
        uint8_t *p = clamp_patient(i + 1);
        p[3] = (uint8_t)HOSP[i], p[4] = 18, p[40] = 80, p[44] = 20;
        p[56] = 0, p[57] = i == 5 ? 1 : 0, p[104] = i == 4 ? 100 : 0;
        uint8_t *e = clamp_expect(i + 1);
        e[78] = 20; /* mood runs before every exit, and before the no-illness health reset */
        if (i >= 3 && i != 5) e[44] = 100;
        if (i == 4) e[20] = 0;
    }
    clamp_check(3, 0);
    /* The cap must leave the next patient's narcosis draw at script position zero. */
    clamp_fixture();
    uint8_t *p = clamp_patient(1), *q = clamp_patient(2);
    p[4] = 16, p[40] = 100;
    q[4] = 5, q[102] = 10;
    clamp_expect(1);
    clamp_expect(2)[102] = 9;
    const uint32_t values[] = {1, 0};
    SimScript script = {values, 2, 0, 0, NULL};
    g_sim.rng_script = &script;
    CHECK(sim_call(&g_sim, 0x67ABD));
    g_sim.rng_script = NULL;
    CHECK(script.used == 1 && script.extra == 0);
    CHECK(q[102] == 9 && p[40] == 100);
    CHECK(!memcmp(patient(0), g_clamp_expected, sizeof g_clamp_expected));
}

/* SIM-93: actual ward ticks, with separate controls for a prior occupant, an empty post-cleanup bed,
 * and an unrelated watched index sharing the occupant's low byte. Adjacent notification/dead writes
 * have no public observation boundary; their ordering is specified in sim-rooms2.md, not proved here. */
static void test_ward_health_notifications(void) {
    enum { SINGLE, PRIMARY, SECONDARY, SETBACK };
    static const struct {
        int kind, health, treatment, amount, gate, draw[3], after, notify;
    } C[] = {
        {SINGLE, 50, 30, 0, 2, {0, 0, 0}, 52, 1},
        {SINGLE, 99, 30, 0, 2, {0, 0, 0}, 100, 1}, /* completes the step, without the round cure */
        {SINGLE, 50, 32, 0, 2, {0, 0, 0}, 50, 1}, /* zero-net healing */
        {SINGLE, 1, 32, 0, 2, {0, 1, 1}, 0, 1},
        {SINGLE, 100, 30, 0, 2, {0, 0, 0}, 100, 0},
        {SINGLE, 50, 30, 0, 3, {0, 0, 0}, 50, 0}, /* failed healing gate */
        {PRIMARY, 50, 30, 2, 0, {0, 0, 0}, 52, 1},
        {PRIMARY, 50, 30, 0, 0, {0, 0, 0}, 50, 1},
        {PRIMARY, 100, 30, 2, 0, {0, 0, 0}, 100, 1}, /* capped, still notifies */
        {PRIMARY, 1, 30, 0, 0, {0, 0, 1}, 0, 1},
        {PRIMARY, 50, 30, 2, 1, {0, 0, 0}, 50, 0},
        {SECONDARY, 50, 31, 2, 0, {0, 0, 0}, 52, 1},
        {SECONDARY, 50, 31, 0, 0, {0, 1, 0}, 50, 1},
        {SECONDARY, 100, 31, 2, 0, {0, 0, 0}, 100, 1},
        {SECONDARY, 1, 31, 0, 0, {0, 1, 1}, 0, 1},
        {SECONDARY, 50, 31, 2, 1, {0, 0, 0}, 50, 0},
        {SETBACK, 50, 30, 0, 51, {0, 0, 0}, 48, 1},
        {SETBACK, 50, 30, 0, 50, {0, 0, 0}, 50, 0},
        {SETBACK, 2, 30, 0, 51, {0, 0, 0}, 2, 0}, /* loss equals health */
        {SETBACK, 1, 30, 0, 51, {0, 0, 0}, 1, 0}
    };
    static const int HOSPITALS[] = {0, 1, 2, 15}, WATCH[] = {0, 261, 517};
    static const uint8_t OLD[] = {0, 55, 255};
    for (size_t c = 0; c < sizeof C / sizeof *C; c++)
        for (int neuro = 0; neuro < 2; neuro++) {
            if (C[c].kind == SETBACK && !neuro) continue;
            for (int hi = 0; hi < 4; hi++)
                for (int human = 0; human < 2; human++)
                    for (int nurse = 0; nurse < 2; nurse++)
                        for (int watch = 0; watch < 3; watch++)
                            for (int old = 0; old < 3; old++) {
                                fresh();
                                room_descriptions();
                                int h = HOSPITALS[hi], cat = neuro ? 16 : 6, flag = neuro ? 0x37 : 0x34;
                                world_hospital(&g_world, h)[0x14] = (uint8_t)human;
                                g_world.block[BLK_ROOM_COUNTS][h * 100 + cat] = 1;
                                uint8_t *ward = sim_room_record(&g_sim, h, cat, 0), *p = patient(261);
                                put16(ward + 8, nurse ? 7 : 0), put16(ward + 0x14, 261);
                                ward[10] = 10, ward[12] = 1;
                                put16(g_world.block[BLK_STAFF_SLOTS] + h * 4000, 7);
                                p[3] = (uint8_t)h, p[4] = (uint8_t)cat, p[44] = (uint8_t)C[c].health;
                                p[43] = (uint8_t)C[c].treatment, p[45] = 2, p[56] = 7, p[100] = 2;
                                p[93] = C[c].kind == SINGLE ? 0xFF : 0;
                                p[96] = C[c].kind == SETBACK ? 0 : 3;
                                put16(p + 94, 700);
                                uint8_t *ill = g_image + 0x954 + 7 * 20;
                                ill[7] = (uint8_t)C[c].amount, ill[11] = 30, ill[12] = 31;
                                g_image[0x270C + C[c].treatment * 38 + 17] = neuro ? 72 : 17;
                                /* No pathology space or newspaper slot: AI cleanup resets P and draws only the rename. */
                                uint8_t *alias = death_alias_record(h);
                                put16(alias + 6, 0), put16(alias + 8, 0);
                                for (int i = 0; i < 100; i++) sim_news_item(&g_sim, i)[0] = 2;
                                int applies = C[c].notify && (C[c].kind != SINGLE || nurse);
                                int fatal = applies && C[c].after == 0, cleaned = fatal && !human;
                                int selected = cleaned ? 0 : 261;
                                uint8_t expected[2][500];
                                for (int player = 0; player < 2; player++) {
                                    uint8_t *pb = g_world.block[BLK_PLAYER0 + player];
                                    CHECK(g_world.size[BLK_PLAYER0 + player] == sizeof expected[player]);
                                    memset(pb, 0x62 + player, sizeof expected[player]);
                                    put16(pb + 0x2C, WATCH[watch]);
                                    pb[flag] = pb[0x46] = OLD[old];
                                    memcpy(expected[player], pb, sizeof expected[player]);
                                }
                                if (applies && h <= 1 && WATCH[watch] == selected) expected[h][flag] = 0xFF;
                                if (fatal && human && h == 0 && WATCH[watch] == 261) expected[0][0x46] = 0xFF;
                                uint32_t raw[16];
                                size_t n = 0;
                                raw[n++] = 2; /* no care drift */
                                if (nurse) raw[n++] = 2; /* no nurse bonus */
                                if (C[c].health < 96 && (!nurse || C[c].health >= 50)) raw[n++] = 2; /* no decline */
                                raw[n++] = 2; /* no treatment worsening */
                                if (C[c].kind == SINGLE) {
                                    if (nurse) {
                                        if (C[c].health < (neuro ? 90 : 70)) raw[n++] = 2; /* no unit draws */
                                        raw[n++] = (uint32_t)C[c].gate;
                                    }
                                } else raw[n++] = (uint32_t)C[c].gate;
                                if (applies && C[c].kind != SETBACK)
                                    for (int j = 0; j < 3; j++) raw[n++] = (uint32_t)C[c].draw[j];
                                if (C[c].kind == SETBACK && C[c].gate == 51) raw[n++] = 0;
                                if (cleaned) raw[n++] = 0; /* death rename */
                                SimScript script = {.v = raw, .n = n};
                                g_sim.rng_script = &script;
                                CHECK(sim_call(&g_sim, neuro ? 0x6C3D5 : 0x6A0BA));
                                g_sim.rng_script = NULL;
                                if (script.used != n || script.extra) fprintf(stderr, "  SIM-93 case %zu neuro %d h %d human %d nurse %d: draws %zu/%zu extra %zu\n", c, neuro, h, human, nurse, script.used, n, script.extra);
                                CHECK(script.used == n && script.extra == 0);
                                CHECK(p[44] == (cleaned ? 100 : applies ? C[c].after : C[c].health));
                                CHECK(p[104] == (fatal && human));
                                CHECK(get16(ward + 0x14) == (cleaned ? 0 : 261) && ward[12] == (cleaned ? 0 : 1));
                                int cured = applies && !cleaned && C[c].after == 100 && C[c].kind != SINGLE;
                                int step_done = applies && C[c].kind == SINGLE && (cleaned || C[c].after == 100);
                                CHECK(p[56] == (cleaned || cured ? 0 : 7));
                                CHECK(p[45] == (cleaned ? step_done ? 255 : 0 : cured ? 0 : step_done ? 1 : 2));
                                CHECK(p[105] == (step_done ? 4 : 0));
                                if (applies && !cleaned) CHECK(p[67] == C[c].health);
                                for (int player = 0; player < 2; player++)
                                    CHECK(memcmp(g_world.block[BLK_PLAYER0 + player], expected[player], sizeof expected[player]) == 0);
                            }
        }
}

static uint8_t *icu_notification_world(int h, int human, int nurse, int health) {
    fresh();
    world_hospital(&g_world, h)[0x14] = (uint8_t)human;
    g_world.block[BLK_ROOM_COUNTS][h * 100 + 9] = 1;
    uint8_t *rec = sim_room_record(&g_sim, h, 9, 0), *p = patient(261);
    put16(rec + 8, nurse ? 7 : 0), put16(rec + 12, 261), put16(rec + 16, 12);
    rec[6] = 3; /* a different missing capability: failed uses must not force the healing amount to zero */
    put16(g_world.block[BLK_STAFF_SLOTS] + h * 4000, 7);
    p[3] = (uint8_t)h, p[4] = 9, p[44] = (uint8_t)health, p[45] = 2, p[56] = 7, p[100] = 2;
    put16(p + 94, 700);
    uint8_t *ill = g_image + 0x954 + 7 * 20;
    ill[11] = 30, ill[12] = 31;
    for (int i = 0; i < 100; i++) sim_news_item(&g_sim, i)[0] = 2;
    return rec;
}

static void icu_notification_players(int watch0, int watch1, int old) {
    for (int player = 0; player < 2; player++) {
        uint8_t *pb = g_world.block[BLK_PLAYER0 + player];
        CHECK(g_world.size[BLK_PLAYER0 + player] == sizeof sim89_expected[player]);
        memset(pb, 0x62 + player, sizeof sim89_expected[player]);
        put16(pb + 0x2C, player ? watch1 : watch0);
        pb[0x42] = pb[0x43] = pb[0x46] = (uint8_t)old;
        memcpy(sim89_expected[player], pb, sizeof sim89_expected[player]);
    }
}

/* Fixed failed care/worsening/unit draws isolate the notification branch without supplying its behavior. */
static size_t icu_notification_prefix(uint32_t *raw, int nurse, int health) {
    size_t n = 0;
    raw[n++] = 2; /* no care drift */
    if (nurse) raw[n++] = 2; /* no nurse bonus */
    raw[n++] = 2, raw[n++] = 2; /* neither worsening gate */
    if (nurse && health < 20) raw[n++] = 2; /* no staff credit */
    if (nurse && health < 50)
        for (int i = 0; i < 4; i++) raw[n++] = 2; /* no unit credit or capability-5 branch */
    return n;
}

static void icu_notification_tick(const uint32_t *raw, size_t n) {
    SimScript script = {.v = raw, .n = n};
    g_sim.rng_script = &script;
    CHECK(sim_call(&g_sim, 0x6FA5A));
    g_sim.rng_script = NULL;
    CHECK(script.used == n && script.extra == 0);
    sim89_check_players();
}

/* SIM-94: all three healing sites, including zero-net, capped, suppressed and fatal results. The saved
 * blocks watch independently. Available pathology space keeps the existing FIX-35 engine behavior. */
static void test_icu_health_notifications(void) {
    enum { SINGLE, PRIMARY, SECONDARY };
    static const struct { int kind, health, treat, amount, gate, draw[3], after, notify; } C[] = {
        {SINGLE, 40, 30, 0, 2, {0, 0, 0}, 42, 1},
        {SINGLE, 40, 32, 0, 2, {0, 0, 0}, 40, 1},
        {SINGLE, 1, 32, 0, 2, {0, 1, 1}, 0, 1},
        {SINGLE, 50, 30, 0, 2, {0, 0, 0}, 50, 0},
        {SINGLE, 40, 30, 0, 3, {0, 0, 0}, 40, 0},
        {PRIMARY, 40, 30, 2, 0, {0, 0, 0}, 42, 1},
        {PRIMARY, 40, 30, 0, 0, {0, 0, 0}, 40, 1},
        {PRIMARY, 100, 30, 2, 0, {0, 0, 0}, 100, 1},
        {PRIMARY, 1, 30, 0, 0, {0, 0, 1}, 0, 1},
        {PRIMARY, 40, 30, 2, 1, {0, 0, 0}, 40, 0},
        {SECONDARY, 40, 31, 2, 0, {0, 0, 0}, 42, 1},
        {SECONDARY, 40, 31, 0, 0, {0, 1, 0}, 40, 1},
        {SECONDARY, 100, 31, 2, 0, {0, 0, 0}, 100, 1},
        {SECONDARY, 1, 31, 0, 0, {0, 1, 1}, 0, 1},
        {SECONDARY, 40, 31, 2, 1, {0, 0, 0}, 40, 0}
    };
    static const int H[] = {0, 1, 2, 15}, WATCH[] = {0, 261, 517}, OLD[] = {0, 55, 255};
    for (size_t c = 0; c < sizeof C / sizeof *C; c++)
        for (int hi = 0; hi < 4; hi++)
            for (int human = 0; human < 2; human++)
                for (int nurse = 0; nurse < 2; nurse++)
                    for (int space = 0; space < (C[c].after == 0 ? 3 : 1); space++)
                        for (int w0 = 0; w0 < 3; w0++)
                            for (int w1 = 0; w1 < 3; w1++)
                                for (int old = 0; old < 3; old++) {
                                    int h = H[hi];
                                    uint8_t *rec = icu_notification_world(h, human, nurse, C[c].health), *p = patient(261);
                                    p[43] = (uint8_t)C[c].treat, p[96] = 2;
                                    rec[14] = C[c].kind == SINGLE;
                                    g_image[0x954 + 7 * 20 + 7] = (uint8_t)C[c].amount;
                                    g_image[0x270C + C[c].treat * 38 + 17] = 17;
                                    uint8_t *alias = death_alias_record(h);
                                    g_world.block[BLK_ROOM_COUNTS][h * 100 + 18] = space != 0;
                                    put16(alias + 6, space == 2), put16(alias + 8, space != 0);
                                    icu_notification_players(WATCH[w0], WATCH[w1], OLD[old]);
                                    int applied = C[c].notify && (C[c].kind != SINGLE || nurse);
                                    int fatal = applied && C[c].after == 0, cleaned = fatal && !human;
                                    int disposed = cleaned && space != 1, dead = fatal && !disposed;
                                    int selected = cleaned ? 0 : 261, watch = h == 0 ? WATCH[w0] : WATCH[w1];
                                    if (applied && h <= 1 && watch == selected) sim89_expected[h][0x42] = 0xFF;
                                    if (dead && h == 0 && WATCH[w0] == selected) sim89_expected[0][0x46] = 0xFF;
                                    uint32_t raw[20];
                                    size_t n = icu_notification_prefix(raw, nurse, C[c].health);
                                    raw[n++] = (uint32_t)C[c].gate; /* single consumes this even without a nurse */
                                    if (applied) for (int j = 0; j < 3; j++) raw[n++] = (uint32_t)C[c].draw[j];
                                    if (disposed) raw[n++] = 0; /* death rename; the newspaper is full */
                                    icu_notification_tick(raw, n);
                                    CHECK(p[44] == (disposed ? 100 : applied ? C[c].after : C[c].health));
                                    CHECK(p[104] == dead);
                                    CHECK(get16(rec + 12) == (cleaned ? 0 : 261) && get16(rec + 16) == (cleaned ? 0 : 12));
                                    CHECK(p[4] == (disposed ? 0xFF : cleaned ? 18 : 9)); /* existing FIX-35 */
                                    CHECK(get16(alias + 6) == (space == 2 || (cleaned && space == 1)));
                                    if (cleaned && space == 1) CHECK(get16(alias + 0x58) == 261 && p[66] == 9);
                                    CHECK(p[56] == (disposed ? 0 : 7)); /* ICU never does the ward's cure reset */
                                    CHECK(p[45] == (disposed ? C[c].kind == SINGLE ? 255 : 0 : 2));
                                    CHECK(p[105] == (disposed && C[c].kind == SINGLE ? 4 : 0));
                                }
}

/* Recovery continues into the treatment/round path; step 0/1/2 and countdown 0/1/2 retain their byte rules. */
static void test_icu_progress_notifications(void) {
    enum { RECOVERY, STEPPED, CODE8, ROUND };
    static const struct { int kind, recovery, draw, countdown, step; } C[] = {
        {RECOVERY, 0, 0, 0, 2}, {RECOVERY, 2, 0, 0, 2}, {RECOVERY, 2, 1, 0, 2},
        {RECOVERY, 1, 1, 0, 2}, {RECOVERY, 1, 1, 0, 0},
        {STEPPED, 0, 0, 0, 2}, {STEPPED, 0, 0, 2, 2},
        {STEPPED, 0, 0, 1, 0}, {STEPPED, 0, 0, 1, 1}, {STEPPED, 0, 0, 1, 2},
        {STEPPED, 1, 1, 1, 2}, {STEPPED, 2, 1, 1, 2}, {CODE8, 0, 0, 2, 2},
        {ROUND, 1, 1, 0, 2}, {ROUND, 2, 1, 0, 2}
    };
    static const int H[] = {0, 1, 2, 15}, WATCH[] = {0, 261, 517}, OLD[] = {0, 55, 255};
    for (size_t c = 0; c < sizeof C / sizeof *C; c++)
        for (int hi = 0; hi < 4; hi++)
            for (int nurse = 0; nurse < 2; nurse++)
                for (int w0 = 0; w0 < 3; w0++)
                    for (int w1 = 0; w1 < 3; w1++)
                        for (int old = 0; old < 3; old++) {
                            int h = H[hi];
                            uint8_t *rec = icu_notification_world(h, 1, nurse, 95), *p = patient(261);
                            p[43] = 30, p[45] = (uint8_t)C[c].step, p[102] = (uint8_t)C[c].recovery;
                            p[105] = (uint8_t)C[c].countdown, p[96] = C[c].kind == ROUND ? 2 : 0;
                            rec[14] = C[c].kind == STEPPED || C[c].kind == CODE8;
                            uint8_t *t = g_image + 0x270C + 30 * 38;
                            t[36] = C[c].kind == STEPPED, t[37] = 2;
                            t[15] = t[16] = t[17] = 8;
                            g_image[0x44A5 + 8] = 3;
                            g_image[0x954 + 7 * 20 + 7] = 2;
                            icu_notification_players(WATCH[w0], WATCH[w1], OLD[old]);
                            int recovered = C[c].recovery == 1 && C[c].draw == 1;
                            int advanced = C[c].kind == STEPPED && C[c].countdown == 1 && nurse;
                            int watched = h <= 1 && (h == 0 ? WATCH[w0] : WATCH[w1]) == 261;
                            if (watched && (recovered || advanced)) sim89_expected[h][0x43] = 0xFF;
                            if (watched && (recovered || C[c].kind == ROUND)) sim89_expected[h][0x42] = 0xFF;
                            uint32_t raw[16];
                            size_t n = icu_notification_prefix(raw, nurse, 95);
                            if (C[c].recovery) raw[n++] = (uint32_t)C[c].draw;
                            if (C[c].kind == CODE8) raw[n++] = 2;
                            if (C[c].kind == ROUND) for (int j = 0; j < 4; j++) raw[n++] = 0;
                            icu_notification_tick(raw, n);
                            int step = (uint8_t)(C[c].step - recovered), rounds = C[c].kind == ROUND ? 2 : 0;
                            if (advanced) { if (step <= 1) step = 2, rounds++; else step--; }
                            int countdown = C[c].countdown;
                            if (C[c].kind == STEPPED) countdown = advanced ? 6 : countdown == 1 ? 2 : (uint8_t)(countdown - 1);
                            if (C[c].kind == CODE8 && nurse) countdown--;
                            CHECK(p[102] == C[c].recovery - (C[c].recovery ? C[c].draw : 0));
                            CHECK(p[45] == step && p[105] == countdown && p[96] == rounds);
                            CHECK(p[44] == (C[c].kind == ROUND ? 97 : 95) && p[104] == 0);
                        }
}

/* SIM-95, docs/sim-rooms.md: full ticks through the five progress phases in each room and donation in category 5.
 * Watchers are independent, and every byte of both saved player blocks is checked. */
enum { RP_DIAG_START, RP_DIAG_GATE, RP_DIAG_MISSING, RP_DIAG_DONE, RP_DIAG_RUNNING,
       RP_START, RP_START_GATE, RP_START_MISSING, RP_STEP, RP_STEP_EMPTY, RP_FINISH,
       RP_RUNNING, RP_IDLE, RP_DONOR, RP_DONOR_FULL, RP_COUNT };
static const struct { int diag, count, steps, after_diag, after_count, after_steps, notify; } RP[RP_COUNT] = {
    {999, 0, 4, 4, 0, 4, 1}, {999, 0, 4, 999, 0, 4, 0}, {999, 0, 4, 999, 0, 4, 0},
    {1, 0, 4, 0, 0, 0, 1}, {2, 0, 4, 1, 0, 4, 0},
    {0, 999, 4, 0, 10, 2, 1}, {0, 999, 4, 0, 999, 4, 0}, {0, 999, 4, 0, 999, 4, 0},
    {0, 3, 2, 0, 8, 1, 1}, {0, 3, 0, 0, 2, 0, 0}, {0, 1, 4, 0, 0, 4, 1},
    {0, 4, 4, 0, 3, 4, 0}, {0, 0, 4, 0, 0, 4, 0},
    {0, 5, 2, 0, 4, 2, 1}, {0, 5, 2, 0, 4, 2, 0}
};
static uint8_t *room_progress_world(int h, int c, int human, int missing, int phase) {
    int cat = ROOM_CATS[c];
    uint8_t *tr = start_room(h, cat), *p = patient(261);
    memcpy(p, patient(5), 106);
    world_hospital(&g_world, h)[0x14] = (uint8_t)human;
    put16(tr + 12, 261);
    if (missing) put16(tr + (missing == 1 ? 8 : 10), 0);
    p[42] = 9, p[43] = 30, p[44] = 80, p[45] = (uint8_t)RP[phase].steps;
    p[48] = 17, p[53] = 19, p[54] = 11, p[55] = 13, p[79] = 33, p[103] = 7;
    put16(p + 94, 700);
    g_image[0x954 + 7 * 20 + 18] = 4;
    uint8_t *t = g_image + 0x270C + 30 * 38;
    t[16] = 1, t[17] = 2, t[18] = 0;
    g_image[0x44A5 + 1] = 8; /* neurology deliberately reads t[1] = 0, hence its minimum duration 3 */
    put16(tr + 14, RP[phase].diag), put16(tr + 16, 30), put16(tr + 18, RP[phase].count);
    g_image[0x144A + 20 * 6 + 16] = g_image[0x144A + 20 * 7 + 16] = 1;
    if (phase != RP_DIAG_MISSING) room_item(tr, 2, cat == 5 ? 6 : 7, 50);
    if (phase == RP_START_MISSING) tr[0x44] = 0x20;
    if (phase == RP_DONOR || phase == RP_DONOR_FULL) {
        p[57] = 5, t[17] = 10;
        put16(tr + 20, phase == RP_DONOR_FULL ? 220 : 0);
    }
    return tr;
}

static void room_progress_players(int h, int c, int watch0, int watch1, int old, int notify) {
    static const int FLAGS[3] = {0x32, 0x3A, 0x41};
    for (int player = 0; player < 2; player++) {
        uint8_t *pb = g_world.block[BLK_PLAYER0 + player];
        CHECK(g_world.size[BLK_PLAYER0 + player] == sizeof sim89_expected[player]);
        memset(pb, 0x62 + player, sizeof sim89_expected[player]);
        put16(pb + 0x2C, player ? watch1 : watch0);
        for (int f = 0; f < 3; f++) pb[FLAGS[f]] = (uint8_t)old;
        memcpy(sim89_expected[player], pb, sizeof sim89_expected[player]);
    }
    if (notify && h <= 1 && (h ? watch1 : watch0) == 261) sim89_expected[h][FLAGS[c]] = 255;
}

/* The fixed draws suppress unrelated health/mood/unit changes. Category 5 also draws twice per hospital. */
static size_t room_progress_draws(uint32_t *raw, int h, int c, int missing, int phase, int boundary) {
    size_t n = 0;
    if (!c) for (int i = 0; i < 2 * (h + 1); i++) raw[n++] = 0;
    if (!missing) {
        raw[n++] = 2, raw[n++] = 2, raw[n++] = 3; /* care drop, nurse bonus, mood drift */
        if (!c) raw[n++] = 2; /* treatment worsening */
        if (RP[phase].diag == 999 || phase == RP_START || phase == RP_START_GATE)
            raw[n++] = phase == RP_DIAG_GATE || phase == RP_START_GATE;
        if (phase == RP_DIAG_DONE || phase == RP_DIAG_RUNNING) {
            for (int i = 0; i < 7; i++) raw[n++] = 0; /* staff credit, diagnosis units */
            if (boundary) return n; /* diagnosis helper's first draw */
            if (phase == RP_DIAG_DONE) for (int i = 0; i < 3; i++) raw[n++] = 0; /* diagnosis, chooser, rank */
        }
        if (phase >= RP_STEP && phase != RP_IDLE) {
            if (phase == RP_DONOR || phase == RP_DONOR_FULL) {
                raw[n++] = 0;
                if (boundary) return n; /* first ordinary treatment-unit draw */
            }
            for (int i = 0; i < 4; i++) raw[n++] = 0;
        }
    }
    if (!c) for (int i = 0; i < 2 * (15 - h); i++) raw[n++] = 0;
    return n;
}

static void room_progress_case(int h, int c, int human, int missing, int phase, int w0, int w1, int old) {
    uint8_t *tr = room_progress_world(h, c, human, missing, phase), *p = patient(261);
    room_progress_players(h, c, w0, w1, old, !missing && RP[phase].notify);
    uint32_t raw[64];
    SimScript script = {.v = raw, .n = room_progress_draws(raw, h, c, missing, phase, 0)};
    g_sim.rng_script = &script;
    int before = failures;
    CHECK(sim_call(&g_sim, ROOM_TICK[c]));
    g_sim.rng_script = NULL;
    CHECK(script.used == script.n && script.extra == 0);
    sim89_check_players();
    int diag = missing ? RP[phase].diag : RP[phase].after_diag;
    int count = missing ? RP[phase].count : c == 2 && phase == RP_STEP ? 3 : RP[phase].after_count;
    int steps = missing ? RP[phase].steps : RP[phase].after_steps;
    CHECK(get16(tr + 14) == diag && get16(tr + 18) == count && p[45] == steps);
    int start = !missing && phase == RP_DIAG_START, step = !missing && phase == RP_STEP;
    int dental_start = !missing && phase == RP_START && c == 1;
    CHECK(p[54] == 11 + start + step + dental_start && p[55] == 13 + step + dental_start);
    int finish = !missing && phase == RP_FINISH;
    CHECK(p[48] == 17 + (finish && c != 2) && p[53] == 19 + (finish && c != 2));
    int diagnosed = !missing && phase == RP_DIAG_DONE;
    CHECK(p[42] == (diagnosed ? 7 : 9));
    CHECK(p[79] == (finish ? 255 : diagnosed ? 0 : 33) && p[103] == (finish ? 0 : 7));
    int donor = !missing && phase == RP_DONOR;
    CHECK(get16(tr + 20) == (phase == RP_DONOR_FULL ? 220 : donor ? 6 : 0));
    CHECK((uint16_t)get16(tr + 0x5E) == (donor ? 65530 : 0) && get16(p + 94) == 700 && p[44] == 80);
    if (before != failures) fprintf(stderr, "  SIM-95 cat %d hospital %d human %d missing %d phase %d watches %d/%d old %d\n",
                                   ROOM_CATS[c], h, human, missing, phase, w0, w1, old);
}

static void test_room_progress_notifications(void) {
    static const int H[] = {0, 1, 2, 15}, WATCH[] = {0, 261, 517}, OLD[] = {0, 55, 255};
    for (int c = 0; c < 3; c++)
        for (int phase = 0; phase < RP_COUNT; phase++) {
            if ((c && phase >= RP_DONOR) || (c == 2 && phase == RP_DIAG_MISSING)) continue;
            for (int hi = 0; hi < 4; hi++)
                for (int human = 0; human < 2; human++) {
                    for (int w0 = 0; w0 < 3; w0++)
                        for (int w1 = 0; w1 < 3; w1++)
                            for (int old = 0; old < 3; old++)
                                room_progress_case(H[hi], c, human, 0, phase, WATCH[w0], WATCH[w1], OLD[old]);
                    if (RP[phase].notify) for (int missing = 1; missing <= 2; missing++)
                        room_progress_case(H[hi], c, human, missing, phase, 261, 261, 0);
                }
        }
}

static uint8_t *room_progress_boundary_record;
static int room_progress_boundary_phase, room_progress_boundary_seen;
static void room_progress_boundary(const SimScript *script, const char *message) {
    (void)message;
    CHECK(script->used == script->n);
    room_progress_boundary_seen = 1;
    sim89_check_players();
    uint8_t *tr = room_progress_boundary_record, *p = patient(261);
    CHECK(p[79] == 33 && p[103] == 7 && p[45] == RP[room_progress_boundary_phase].steps);
    if (room_progress_boundary_phase == RP_DIAG_DONE) {
        CHECK(get16(tr + 14) == 0 && p[42] == 9);
    } else {
        CHECK(get16(tr + 20) == 6 && (uint16_t)get16(tr + 0x5E) == 65530 && get16(tr + 18) == 5);
        CHECK(get16(p + 94) == 700 && p[54] == 11 && p[55] == 13);
    }
    longjmp(sim89_draw_boundary, 1);
}

/* These two boundaries are naturally observable. Adjacent writes in the other phases have no draw boundary;
 * their exact order is specified in the source, but final-state assertions cannot distinguish a swap. */
static void test_room_progress_order(void) {
    for (int c = 0; c < 3; c++)
        for (int h = 0; h < 2; h++)
            for (int donor = 0; donor < (c ? 1 : 2); donor++) {
                int phase = donor ? RP_DONOR : RP_DIAG_DONE;
                room_progress_boundary_record = room_progress_world(h, c, 1, 0, phase);
                room_progress_boundary_phase = phase, room_progress_boundary_seen = 0;
                room_progress_players(h, c, 261, 261, 55, 1);
                uint32_t raw[64];
                SimScript script = {.v = raw, .n = room_progress_draws(raw, h, c, 0, phase, 1)};
                g_sim.rng_script = &script;
                g_sim.rng_failure = room_progress_boundary;
                if (!setjmp(sim89_draw_boundary)) {
                    sim_call(&g_sim, ROOM_TICK[c]);
                    CHECK(0); /* the scripted boundary must be reached */
                }
                g_sim.rng_script = NULL;
                g_sim.rng_failure = NULL;
                CHECK(room_progress_boundary_seen);
            }
}

/* SIM-96, docs/sim-rooms2.md: actual theatre ticks, including stored-state bleeding controls. These do not
 * assert normal-play reachability of bleeding: cash eviction already removes the patient from the OP. */
enum { TN_FINISH, TN_STAGE, TN_RUNNING, TN_BLEED };
static const struct {
    int cat, phase, health, after, countdown, index, wish, gate, gain, field100, bleeding, dead;
} TN[] = {
    {7, TN_FINISH, 50, 50, 1, 2, 0, 0, 0, 1, 1, 0},
    {7, TN_STAGE, 50, 51, 1, 0, 0, 0, 1, 1, 0, 0}, /* non-wish success: progress only */
    {7, TN_STAGE, 50, 50, 0, 2, 0, 0, 0, 1, 0, 0},
    {7, TN_STAGE, 50, 50, 1, 0, 1, 0, 0, 1, 0, 0}, /* zero-net wish health still notifies */
    {7, TN_STAGE, 50, 48, 0, 2, 1, 0, 0, 1, 0, 0},
    {7, TN_STAGE, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0},
    {7, TN_STAGE, 1, 0, 0, 2, 1, 0, 0, 1, 0, 0}, /* reload stage 0 after cleanup, retain op 1 */
    {7, TN_RUNNING, 50, 49, 2, 2, 0, 3, 0, 1, 0, 0},
    {7, TN_RUNNING, 1, 0, 2, 2, 0, 3, 0, 1, 0, 0},
    {7, TN_RUNNING, 50, 50, 2, 2, 0, 0, 0, 1, 0, 0}, /* failed r7 gate */
    {7, TN_RUNNING, 50, 50, 2, 2, 0, 3, 0, 2, 0, 0}, /* field 100 blocks even the gate draw */
    {7, TN_BLEED, 50, 49, 2, 2, 0, 0, 0, 2, 1, 0},
    {7, TN_BLEED, 1, 0, 2, 2, 0, 0, 0, 2, 1, 0},
    {7, TN_BLEED, 50, 50, 2, 2, 0, 0, 0, 2, 0, 0},
    {8, TN_FINISH, 50, 50, 1, 2, 0, 0, 0, 1, 1, 0},
    {8, TN_STAGE, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0},
    {8, TN_STAGE, 50, 50, 1, 0, 0, 0, 0, 1, 0, 0},
    {8, TN_STAGE, 100, 100, 1, 0, 0, 0, 0, 1, 0, 0},
    {8, TN_STAGE, 50, 50, 0, 2, 0, 0, 0, 1, 0, 0},
    {8, TN_RUNNING, 50, 50, 2, 2, 0, 0, 0, 1, 0, 0},
    {8, TN_FINISH, 50, 50, 1, 2, 0, 0, 0, 1, 1, 1},
    {8, TN_STAGE, 50, 50, 1, 0, 0, 0, 0, 1, 0, 1}
};

static uint8_t *theatre_notification_world(size_t c, int h, int human, int space) {
    fresh();
    int cat = TN[c].cat;
    world_hospital(&g_world, h)[0x14] = (uint8_t)human;
    g_world.block[BLK_ROOM_COUNTS][h * 100 + cat] = 1;
    uint8_t *rec = sim_room_record(&g_sim, h, cat, 0), *p = patient(261);
    /* World supplies the real six-record table, 216 bytes each, 1296 bytes per hospital. */
    CHECK(sim_room_record(&g_sim, h, cat, 1) - rec == 216);
    put16(rec + 6, 7), put16(rec + 8, 8), put16(rec + 10, 9), put16(rec + 12, 261);
    put16(rec + 14, TN[c].countdown), put16(rec + 16, 1), put16(rec + 18, TN[c].index), put16(rec + 20, 9);
    p[3] = (uint8_t)h, p[4] = (uint8_t)cat, p[44] = (uint8_t)TN[c].health;
    p[48] = 11, p[52] = 13, p[53] = 17, p[54] = 19, p[55] = 23;
    p[56] = 7, p[57] = (uint8_t)TN[c].wish, p[100] = (uint8_t)TN[c].field100;
    p[101] = (uint8_t)TN[c].bleeding, p[104] = (uint8_t)TN[c].dead;
    put16(p + 94, 700);
    int sequence = cat == 7 ? 0x208E : 0x242E, duration = cat == 7 ? 0x69EB : 0x6A57;
    g_image[sequence + 1] = 4; /* cleared op 0 would give the wrong duration 9 */
    g_image[sequence + 11 + 1] = 2, g_image[sequence + 11 + 3] = 3;
    if (TN[c].phase == TN_FINISH) g_image[sequence + 11 + 2] = 1;
    g_image[duration + 2] = 5, g_image[duration + 3] = 7, g_image[duration + 4] = 9;
    g_image[0x2470 + 1] = 1; /* wish blood draw random(8), no extra fixed loss at table byte 8 */
    g_world.block[BLK_ROOM_COUNTS][h * 100 + 18] = space != 0;
    uint8_t *alias = death_alias_record(h);
    put16(alias + 6, space == 2), put16(alias + 8, space != 0); /* absent / available / full */
    for (int i = 0; i < 100; i++) sim_news_item(&g_sim, i)[0] = 2;
    return rec;
}

static void theatre_notification_players(int watch0, int watch1, int old) {
    static const int FLAGS[] = {0x3E, 0x3F, 0x46, 0x9F, 0xA0};
    for (int player = 0; player < 2; player++) {
        uint8_t *pb = g_world.block[BLK_PLAYER0 + player];
        CHECK(g_world.size[BLK_PLAYER0 + player] == sizeof sim89_expected[player]);
        memset(pb, 0x62 + player, sizeof sim89_expected[player]);
        put16(pb + 0x2C, player ? watch1 : watch0);
        for (size_t f = 0; f < sizeof FLAGS / sizeof *FLAGS; f++) pb[FLAGS[f]] = (uint8_t)old;
        memcpy(sim89_expected[player], pb, sizeof sim89_expected[player]);
    }
}

static size_t theatre_notification_draws(uint32_t *raw, size_t c, int disposed) {
    size_t n = 0;
    raw[n++] = 2, raw[n++] = 2; /* failed care drop and nurse bonus */
    if (TN[c].dead) return n;
    if (TN[c].countdown) for (int d = 0; d < (TN[c].cat == 7 ? 9 : 8); d++) raw[n++] = 0;
    if (TN[c].cat == 7 && TN[c].phase == TN_STAGE) {
        raw[n++] = 0, raw[n++] = TN[c].gain ? 0 : 1; /* no failure; success only for the gain control */
        if (TN[c].gain) raw[n++] = 0;
        if (TN[c].wish) {
            raw[n++] = 0; /* blood */
            raw[n++] = (uint32_t)(TN[c].health + TN[c].gain - TN[c].after);
        }
    }
    if (TN[c].cat == 7 && TN[c].phase == TN_RUNNING && TN[c].field100 == 1) {
        raw[n++] = (uint32_t)TN[c].gate;
        if (TN[c].gate == 3) raw[n++] = 0, raw[n++] = 0; /* blood loss 4, health loss 1 */
    }
    if (TN[c].phase == TN_BLEED && TN[c].bleeding) raw[n++] = 0, raw[n++] = 0;
    if (disposed) raw[n++] = 0; /* death rename; newspaper has no free slot */
    return n;
}

static void test_theatre_notifications(void) {
    static const int H[] = {0, 1, 2, 15}, WATCH[] = {0, 261, 517}, OLD[] = {0, 55, 255};
    for (size_t c = 0; c < sizeof TN / sizeof *TN; c++)
        for (int hi = 0; hi < 4; hi++)
            for (int human = 0; human < 2; human++)
                for (int space = 0; space < (TN[c].after == 0 ? 3 : 1); space++)
                    for (int w0 = 0; w0 < 3; w0++)
                        for (int w1 = 0; w1 < 3; w1++)
                            for (int old = 0; old < 3; old++) {
                                int h = H[hi], fatal = TN[c].after == 0, cleaned = fatal && !human;
                                int disposed = cleaned && space != 1, dead = TN[c].dead || (fatal && !disposed);
                                int finish = !TN[c].dead && TN[c].phase == TN_FINISH;
                                int advance = !dead && TN[c].phase == TN_STAGE, selected = cleaned ? 0 : 261;
                                int health = !TN[c].dead && (finish || (TN[c].cat == 7 &&
                                    ((TN[c].phase == TN_STAGE && TN[c].wish) ||
                                     (TN[c].phase == TN_RUNNING && TN[c].field100 == 1 && TN[c].gate == 3) ||
                                     (TN[c].phase == TN_BLEED && TN[c].bleeding))));
                                uint8_t *rec = theatre_notification_world(c, h, human, space), *p = patient(261);
                                theatre_notification_players(WATCH[w0], WATCH[w1], OLD[old]);
                                if (h <= 1 && (h ? WATCH[w1] : WATCH[w0]) == selected) {
                                    if (health) sim89_expected[h][TN[c].cat == 7 ? 0x3E : 0x9F] = 255;
                                    if (advance || (finish && TN[c].cat == 7))
                                        sim89_expected[h][TN[c].cat == 7 ? 0x3F : 0xA0] = 255;
                                }
                                if (fatal && dead && h == 0 && WATCH[w0] == selected) sim89_expected[0][0x46] = 255;
                                uint32_t raw[32];
                                SimScript script = {.v = raw, .n = theatre_notification_draws(raw, c, disposed)};
                                g_sim.rng_script = &script;
                                int before = failures;
                                CHECK(sim_call(&g_sim, TN[c].cat == 7 ? 0x58045 : 0x597F9));
                                g_sim.rng_script = NULL;
                                CHECK(script.used == script.n && script.extra == 0);
                                sim89_check_players();
                                CHECK(p[44] == (disposed ? 100 : TN[c].after) && p[104] == dead);
                                CHECK(p[4] == (disposed ? 255 : cleaned ? 18 : TN[c].cat));
                                CHECK(get16(rec + 12) == selected && get16(rec + 16) == !cleaned);
                                int next_index = cleaned ? advance : finish ? -1 : TN[c].index + advance;
                                int count = cleaned ? advance ? 5 : 0 : finish ? 999 :
                                            advance ? TN[c].index == 2 ? 7 : 5 :
                                            TN[c].countdown - (TN[c].countdown > 0 && !TN[c].dead);
                                CHECK(get16(rec + 18) == next_index && get16(rec + 14) == count);
                                CHECK(get16(rec + 20) == (cleaned ? 0 : finish ? 2 : 10));
                                CHECK(p[48] == (disposed ? 0 : 11 + 2 * finish) && p[53] == (disposed ? 0 : 17 + 2 * finish));
                                CHECK(p[52] == (disposed ? 0 : 13) + advance && p[54] == (disposed ? 0 : 19) + 2 * advance &&
                                      p[55] == (disposed ? 0 : 23) + advance);
                                CHECK(p[100] == (disposed ? 0 : finish ? 2 : TN[c].field100));
                                CHECK(p[101] == (disposed || finish ? 0 : TN[c].bleeding));
                                int blood = disposed ? 700 : TN[c].phase == TN_RUNNING && health ? 696 :
                                            TN[c].phase == TN_BLEED && health ? 695 : 700;
                                CHECK(get16(p + 94) == blood);
                                CHECK(get16(world_hospital(&g_world, h) + 0x38) == cleaned &&
                                      get16(world_hospital(&g_world, h) + 0x3C) == disposed);
                                uint8_t *alias = death_alias_record(h), zero[106] = {0};
                                CHECK(get16(alias + 6) == (space == 2 || (cleaned && space == 1)));
                                if (cleaned && space == 1) CHECK(get16(alias + 0x58) == 261 && p[66] == 7);
                                CHECK(memcmp(patient(0), zero, sizeof zero) == 0);
                                if (before != failures) fprintf(stderr, "  SIM-96 case %zu h %d human %d space %d watches %d/%d old %d\n",
                                                               c, h, human, space, WATCH[w0], WATCH[w1], OLD[old]);
                            }
}

/* A naturally following bleeding draw observes the ongoing-health notification before bleeding starts.
 * Adjacent writes (especially torture's duplicate 9F) have no such boundary; no observer is added for them. */
static uint8_t *theatre_boundary_record;
static int theatre_boundary_seen;
static void theatre_notification_boundary(const SimScript *script, const char *message) {
    (void)message;
    CHECK(script->used == script->n);
    theatre_boundary_seen = 1;
    sim89_check_players();
    CHECK(patient(261)[44] == 49 && patient(261)[104] == 0 && get16(patient(261) + 94) == 696);
    CHECK(get16(theatre_boundary_record + 14) == 1 && get16(theatre_boundary_record + 18) == 2);
    longjmp(sim89_draw_boundary, 1);
}
static void test_theatre_notification_order(void) {
    for (int h = 0; h < 2; h++) {
        theatre_boundary_record = theatre_notification_world(7, h, 1, 0);
        patient(261)[101] = 1;
        theatre_notification_players(261, 261, 55);
        sim89_expected[h][0x3E] = 255;
        uint32_t raw[32];
        SimScript script = {.v = raw, .n = theatre_notification_draws(raw, 7, 0)};
        theatre_boundary_seen = 0;
        g_sim.rng_script = &script;
        g_sim.rng_failure = theatre_notification_boundary;
        if (!setjmp(sim89_draw_boundary)) {
            sim_call(&g_sim, 0x58045);
            CHECK(0);
        }
        g_sim.rng_script = NULL;
        g_sim.rng_failure = NULL;
        CHECK(theatre_boundary_seen);
    }
}

/* SIM-97, docs/engine-care.md: valid dead cases with no diagnosis, an empty list and no harvest entry.
 * Whole selected room, staff, patient and hospital records and both saved player blocks are compared. */
static uint8_t *pathology_notification_world(int h, int human, int stage, int count, int missing) {
    fresh();
    world_hospital(&g_world, h)[0x14] = (uint8_t)human;
    g_world.block[BLK_ROOM_COUNTS][h * 100 + 18] = 1;
    uint8_t *rec = sim_room_record(&g_sim, h, 18, 0), *p = patient(261);
    put16(rec + 10, missing & 1 ? 0 : 7), put16(rec + 12, missing & 2 ? 0 : 261);
    put16(rec + 14, count), put16(rec + 16, 7), put16(rec + 18, stage);
    put16(g_world.block[BLK_STAFF] + 7 * 32 + 16, 100);
    p[3] = (uint8_t)h, p[4] = 18, p[104] = 1;
    return rec;
}
static void pathology_notification_players(int h, int watch0, int watch1, int old, int notify) {
    for (int player = 0; player < 2; player++) {
        uint8_t *pb = g_world.block[BLK_PLAYER0 + player];
        CHECK(g_world.size[BLK_PLAYER0 + player] == sizeof sim89_expected[player]);
        memset(pb, 0x62 + player, sizeof sim89_expected[player]);
        put16(pb + 0x2C, player ? watch1 : watch0);
        pb[0x45] = (uint8_t)old;
        memcpy(sim89_expected[player], pb, sizeof sim89_expected[player]);
    }
    if (notify && h <= 1 && (h ? watch1 : watch0) == 261) sim89_expected[h][0x45] = 255;
}
static void pathology_notification_case(int h, int human, int stage, int count, int draw, int missing,
                                       int watch0, int watch1, int old) {
    uint8_t *rec = pathology_notification_world(h, human, stage, count, missing);
    uint8_t *staff = g_world.block[BLK_STAFF] + 7 * 32, *p = patient(261), *hospital = world_hospital(&g_world, h);
    uint8_t expected_room[158], expected_staff[32], expected_patient[106], expected_hospital[HOSPITAL_SIZE];
    memcpy(expected_room, rec, sizeof expected_room), memcpy(expected_staff, staff, sizeof expected_staff);
    memcpy(expected_patient, p, sizeof expected_patient), memcpy(expected_hospital, hospital, sizeof expected_hospital);
    int advance = !missing && count <= 1;
    if (!missing) {
        put16(expected_room + 16, 8);
        if (advance) {
            put16(expected_room + 18, stage + 1), put16(expected_room + 14, stage == 15 ? 999 : 12);
            put16(expected_staff + 16, 101 + draw);
            if (stage == 15) put16(expected_hospital + 0x3A, 1);
        } else if (count != 999) put16(expected_room + 14, count - 1);
    }
    pathology_notification_players(h, watch0, watch1, old, advance);
    uint32_t raw = (uint32_t)draw;
    SimScript script = {.v = &raw, .n = (size_t)advance};
    g_sim.rng_script = &script;
    int before = failures;
    CHECK(sim_call(&g_sim, 0x6F6B2));
    g_sim.rng_script = NULL;
    CHECK(script.used == script.n && script.extra == 0);
    sim89_check_players();
    CHECK(memcmp(rec, expected_room, sizeof expected_room) == 0);
    CHECK(memcmp(staff, expected_staff, sizeof expected_staff) == 0);
    CHECK(memcmp(p, expected_patient, sizeof expected_patient) == 0);
    CHECK(memcmp(hospital, expected_hospital, sizeof expected_hospital) == 0);
    if (before != failures) fprintf(stderr, "  SIM-97 h %d human %d stage %d count %d draw %d missing %d watches %d/%d old %d\n",
                                   h, human, stage, count, draw, missing, watch0, watch1, old);
}
static void test_pathology_notifications(void) {
    static const int H[] = {0, 1, 2, 15}, COUNT[] = {0, 1, 2, 3, 999}, WATCH[] = {0, 261, 517}, OLD[] = {0, 55, 255};
    for (int stage = 0; stage <= 15; stage += 15)
        for (int hi = 0; hi < 4; hi++)
            for (int human = 0; human < 2; human++) {
                for (int count = 0; count < 5; count++)
                    for (int draw = 0; draw < 2; draw++)
                        for (int w0 = 0; w0 < 3; w0++)
                            for (int w1 = 0; w1 < 3; w1++)
                                for (int old = 0; old < 3; old++)
                                    pathology_notification_case(H[hi], human, stage, COUNT[count], draw, 0,
                                                                WATCH[w0], WATCH[w1], OLD[old]);
                for (int missing = 1; missing <= 3; missing++) {
                    int watch = missing & 2 ? 0 : 261;
                    pathology_notification_case(H[hi], human, stage, 1, 1, missing, watch, watch, 0);
                }
            }
}

/* Stop before the existing work draw: the unconditional work credit already happened, but the flag must still
 * be unchanged. Adjacent countdown/verdict/flag writes after this draw have no observable intermediate boundary. */
static uint8_t *pathology_boundary_record;
static int pathology_boundary_stage, pathology_boundary_seen;
static void pathology_notification_boundary(const SimScript *script, const char *message) {
    (void)message;
    CHECK(script->used == 0 && script->n == 0);
    pathology_boundary_seen = 1;
    sim89_check_players();
    CHECK(get16(pathology_boundary_record + 14) == 0 && get16(pathology_boundary_record + 16) == 8 &&
          get16(pathology_boundary_record + 18) == pathology_boundary_stage);
    CHECK(get16(g_world.block[BLK_STAFF] + 7 * 32 + 16) == 101);
    longjmp(sim89_draw_boundary, 1);
}
static void test_pathology_notification_order(void) {
    for (int h = 0; h < 2; h++)
        for (int stage = 0; stage <= 15; stage += 15) {
            pathology_boundary_record = pathology_notification_world(h, 1, stage, 1, 0);
            pathology_boundary_stage = stage, pathology_boundary_seen = 0;
            pathology_notification_players(h, 261, 261, 55, 0);
            uint32_t raw = 0;
            SimScript script = {.v = &raw, .n = 0};
            g_sim.rng_script = &script;
            g_sim.rng_failure = pathology_notification_boundary;
            if (!setjmp(sim89_draw_boundary)) {
                sim_call(&g_sim, 0x6F6B2);
                CHECK(0);
            }
            g_sim.rng_script = NULL;
            g_sim.rng_failure = NULL;
            CHECK(pathology_boundary_seen);
        }
}

/* SIM-98, docs/sim-patients.md: field-specific full ticks through reception (2) and waiting rooms (3).
 * No countdown/view flags are planted. News is full in the leaving controls, so DEP-1 is not exercised. */
static const struct {
    int cat, wait, mood, opinion, staffed, timer, sex, after_mood, after_opinion, notify;
    size_t draws;
    uint32_t raw[5];
} MN[] = {
    {2, 89, 50, 50, 0, 0, 0, 49, 50, 1, 3, {0, 0, 1}}, /* patience */
    {2, 88, 50, 50, 0, 0, 0, 50, 50, 0, 0, {0}},
    {2, 89, 0, 0, 0, 0, 0, 0, 0, 0, 0, {0}},
    {2, 0, 20, 50, 0, 0, 0, 20, 49, 1, 1, {127}}, /* low opinion */
    {2, 0, 20, 50, 0, 0, 0, 20, 50, 0, 1, {0}},
    {2, 0, 30, 50, 0, 0, 0, 30, 50, 0, 0, {0}},
    {2, 0, 20, 0, 0, 0, 0, 20, 0, 0, 0, {0}},
    {2, 0, 51, 50, 0, 0, 0, 51, 51, 1, 3, {0, 0, 0}}, /* good-mood opinion branch */
    {2, 0, 51, 50, 0, 0, 0, 52, 50, 1, 3, {0, 0, 1}}, /* good-mood mood branch */
    {2, 0, 51, 50, 0, 0, 0, 51, 50, 0, 1, {127}},
    {2, 0, 51, 50, 0, 0, 0, 51, 50, 0, 2, {0, 1}},
    {2, 0, 50, 50, 0, 0, 0, 50, 50, 0, 0, {0}},
    {2, 0, 40, 50, 1, 5, 0, 41, 50, 1, 3, {0, 0, 0}}, /* staffed comfort, tail gate fails */
    {2, 0, 40, 50, 1, 5, 0, 40, 50, 0, 2, {31, 0}},
    {2, 0, 40, 50, 1, 5, 0, 40, 50, 0, 3, {0, 1, 0}},
    {2, 0, 40, 50, 0, 5, 0, 40, 50, 0, 1, {0}},
    {2, 0, 40, 50, 0, 5, 0, 42, 50, 1, 4, {1, 1, 2, 0}}, /* man's replacement r3 */
    {2, 0, 40, 50, 0, 5, 1, 41, 50, 1, 3, {1, 1, 0}},
    {2, 0, 40, 50, 0, 5, 0, 40, 50, 1, 4, {1, 1, 0, 0}}, /* zero addition still notifies */
    {2, 0, 40, 50, 0, 5, 0, 40, 50, 0, 1, {0}},
    {2, 0, 40, 50, 0, 0, 0, 40, 50, 0, 0, {0}},
    {3, 89, 50, 50, 0, 0, 0, 49, 50, 1, 3, {0, 1, 1}}, /* waiting patience */
    {3, 89, 50, 50, 0, 0, 0, 50, 50, 1, 3, {0, 1, 0}}, /* halved to zero */
    {3, 88, 50, 50, 0, 0, 0, 50, 50, 0, 0, {0}},
    {3, 0, 40, 50, 0, 5, 0, 41, 50, 1, 3, {0, 0, 1}}, /* man's replacement r1 */
    {3, 0, 40, 50, 0, 5, 1, 41, 50, 1, 2, {0, 1}},
    {3, 0, 40, 50, 0, 5, 0, 40, 50, 1, 3, {0, 1, 0}},
    {3, 0, 40, 50, 0, 5, 0, 40, 50, 0, 1, {1}},
    {3, 0, 40, 50, 0, 0, 0, 40, 50, 0, 0, {0}},
    {3, 0, 100, 50, 0, 5, 0, 100, 50, 1, 3, {0, 0, 1}}, /* existing cap */
    {3, 179, 50, 50, 0, 0, 0, 40, 38, 1, 0, {0}}, /* retains patient index after place clears */
    {3, 200, 50, 50, 0, 0, 0, 40, 38, 1, 0, {0}}
};
static uint8_t *mood_notification_world(size_t c, int h, int human) {
    fresh();
    world_set_tail(&g_world, TAIL_HOUR, 10);
    int cat = MN[c].cat;
    g_world.block[BLK_ROOM_COUNTS][h * 100 + cat] = 1;
    world_hospital(&g_world, h)[0x14] = (uint8_t)human;
    uint8_t *rec = sim_room_record(&g_sim, h, cat, 0), *p = patient(261);
    put16(rec + 8, MN[c].staffed ? 7 : 0);
    rec[cat == 2 ? 0x45 : 15] = (uint8_t)MN[c].timer;
    int place = cat == 2 ? 10 : 20;
    put16(rec + place, 261), put16(rec + place + 2, MN[c].wait);
    if (cat == 3) rec[10] = 10, rec[12] = 1;
    p[0] = (uint8_t)MN[c].sex, p[3] = (uint8_t)h, p[4] = (uint8_t)cat, p[44] = 100;
    p[78] = (uint8_t)MN[c].mood, p[20 + h] = (uint8_t)MN[c].opinion;
    g_world.block[BLK_STAFF][7 * 32 + 5] = 100; /* staffed comfort threshold 30 */
    uint8_t *slot = g_world.block[BLK_STAFF_SLOTS] + 4000 * h;
    memset(slot, 0, STAFF_SLOT_SIZE), put16(slot, 7); /* no break due */
    for (int i = 0; i < 100; i++) sim_news_item(&g_sim, i)[0] = 2;
    return rec;
}
static void mood_notification_players(int h, int watch0, int watch1, int old, int notify) {
    for (int player = 0; player < 2; player++) {
        uint8_t *pb = g_world.block[BLK_PLAYER0 + player];
        CHECK(g_world.size[BLK_PLAYER0 + player] == sizeof sim89_expected[player]);
        memset(pb, 0, sizeof sim89_expected[player]);
        put16(pb + 0x2C, player ? watch1 : watch0);
        pb[0x49] = (uint8_t)old;
        put32(pb + 0xBA, 55);
        memcpy(sim89_expected[player], pb, sizeof sim89_expected[player]);
    }
    if (notify && h == 0 && watch0 == 261) sim89_expected[0][0x49] = 255;
}
static size_t mood_notification_prefix(uint32_t *raw, size_t c, int h) {
    size_t n = 0;
    for (int i = 0; i < 2 * (h + 1); i++) raw[n++] = 0; /* hospital daily values */
    if (MN[c].cat == 3) raw[n++] = 2; /* failed care drop, no nurse */
    raw[n++] = 3; /* failed drift gate */
    return n;
}
static void test_mood_notifications(void) {
    static const int H[] = {0, 1, 2, 15}, WATCH[] = {0, 261, 517}, OLD[] = {0, 55, 255};
    for (size_t c = 0; c < sizeof MN / sizeof *MN; c++)
        for (int hi = 0; hi < 4; hi++)
            for (int human = 0; human < 2; human++)
                for (int w0 = 0; w0 < 3; w0++)
                    for (int w1 = 0; w1 < 3; w1++)
                        for (int old = 0; old < 3; old++) {
                            int h = H[hi], cat = MN[c].cat, leave = cat == 3 && MN[c].wait >= 179;
                            uint8_t *rec = mood_notification_world(c, h, human), *p = patient(261);
                            uint8_t expected_room[180], expected_patient[106];
                            size_t room_size = cat == 2 ? 174 : 180;
                            memcpy(expected_room, rec, room_size), memcpy(expected_patient, p, sizeof expected_patient);
                            expected_patient[78] = (uint8_t)MN[c].after_mood;
                            expected_patient[20 + h] = (uint8_t)MN[c].after_opinion;
                            if (leave) memset(expected_room + 20, 0, 4), expected_patient[3] = expected_patient[4] = 255;
                            else put16(expected_room + (cat == 2 ? 12 : 22), MN[c].wait + 1);
                            if (cat == 2 && MN[c].timer) expected_room[0x45]--;
                            mood_notification_players(h, WATCH[w0], WATCH[w1], OLD[old], MN[c].notify);
                            uint32_t raw[64];
                            size_t n = mood_notification_prefix(raw, c, h);
                            for (size_t d = 0; d < MN[c].draws; d++) raw[n++] = MN[c].raw[d];
                            for (int i = 0; i < 2 * (15 - h); i++) raw[n++] = 0;
                            SimScript script = {.v = raw, .n = n};
                            g_sim.rng_script = &script;
                            int before = failures;
                            CHECK(sim_call(&g_sim, cat == 2 ? 0x67611 : 0x6BFC8));
                            g_sim.rng_script = NULL;
                            CHECK(script.used == n && script.extra == 0);
                            sim89_check_players();
                            CHECK(memcmp(rec, expected_room, room_size) == 0);
                            CHECK(memcmp(p, expected_patient, sizeof expected_patient) == 0);
                            CHECK(get16(g_world.block[BLK_STAFF] + 7 * 32 + 16) == 0);
                            if (before != failures) fprintf(stderr, "  SIM-98 case %zu h %d human %d watches %d/%d old %d\n",
                                                           c, h, human, WATCH[w0], WATCH[w1], OLD[old]);
                        }
}

/* Existing following draws expose stored state and flags: patience before low opinion, good mood before staffed
 * comfort, staffed comfort before tail comfort, and tail comfort before the next patient and timer decrement.
 * The leave case has a second occupied place, exposing the decrement before the final occupied recount. */
static uint8_t *mood_boundary_record;
static int mood_boundary_kind, mood_boundary_h, mood_boundary_seen;
static void mood_notification_boundary(const SimScript *script, const char *message) {
    (void)message;
    static const int MOOD[] = {19, 52, 51, 41, 42, 49, 41, 40, 20};
    static const int OPINION[] = {50, 50, 51, 50, 50, 50, 50, 38, 49};
    int kind = mood_boundary_kind, h = mood_boundary_h, waiting = kind >= 5 && kind <= 7;
    uint8_t *rec = mood_boundary_record, *p = patient(261);
    CHECK(script->used == script->n);
    mood_boundary_seen = 1;
    sim89_check_players();
    CHECK(p[78] == MOOD[kind] && p[20 + h] == OPINION[kind]);
    CHECK(p[3] == (kind == 7 ? 255 : h) && p[4] == (kind == 7 ? 255 : waiting ? 3 : 2));
    CHECK(get16(rec + (waiting ? 20 : 10)) == (kind == 7 ? 0 : 261));
    CHECK(get16(rec + (waiting ? 22 : 12)) == (kind == 7 ? 0 : kind == 0 || kind == 5 ? 90 : 1));
    CHECK(rec[waiting ? 15 : 0x45] == (kind >= 1 && kind <= 6 ? 5 : 0));
    if (kind == 7) CHECK(rec[12] == 1 && get16(rec + 24) == 262); /* final recount has not run */
    longjmp(sim89_draw_boundary, 1);
}
static void test_mood_notification_order(void) {
    static const size_t CASE[] = {0, 8, 7, 12, 16, 21, 24, 30, 3};
    static const size_t DRAWS[] = {3, 3, 3, 2, 4, 3, 3, 0, 1};
    for (int kind = 0; kind < 9; kind++)
        for (int h = 0; h < 2; h++)
            for (int human = 0; human < 2; human++) {
                size_t c = CASE[kind];
                uint8_t *rec = mood_notification_world(c, h, human);
                if (kind == 0) patient(261)[78] = 20;
                if (kind == 1 || kind == 2) put16(rec + 8, 7), rec[0x45] = 5;
                if (kind == 5) rec[15] = 5;
                if (kind == 4 || kind == 7) {
                    int place = kind == 4 ? 14 : 24;
                    put16(rec + place, 262), put16(rec + place + 2, 0);
                    uint8_t *q = patient(262);
                    q[3] = (uint8_t)h, q[4] = (uint8_t)MN[c].cat, q[44] = 100, q[78] = 50;
                    if (kind == 7) rec[12] = 2;
                }
                mood_notification_players(h, 261, 261, 55, 1);
                uint32_t raw[64];
                size_t n = mood_notification_prefix(raw, c, h);
                if (kind == 7) raw[n - 1] = 2, raw[n++] = 3; /* second patient's care before first patient's drift */
                for (size_t d = 0; d < DRAWS[kind]; d++) raw[n++] = MN[c].raw[d];
                SimScript script = {.v = raw, .n = n};
                mood_boundary_record = rec, mood_boundary_kind = kind, mood_boundary_h = h, mood_boundary_seen = 0;
                g_sim.rng_script = &script;
                g_sim.rng_failure = mood_notification_boundary;
                if (!setjmp(sim89_draw_boundary)) {
                    sim_call(&g_sim, MN[c].cat == 2 ? 0x67611 : 0x6BFC8);
                    CHECK(0);
                }
                g_sim.rng_script = NULL;
                g_sim.rng_failure = NULL;
                CHECK(mood_boundary_seen);
            }
}

/* SIM-99: the exception is reached only after an ordinary departure decision. The two saved blocks retain
 * distinct bytes throughout; hospital 0's saved screen deliberately disagrees with the runtime screen. */
typedef struct {
    int screen, watch, protects, refresh;
    uint32_t request;
} WaitingView;
static uint8_t *waiting_departure_world(int h, int human, WaitingView view, int other, int old, int kind, int room) {
    uint8_t *rec = mood_notification_world(30, h, human), *p = patient(261);
    if (room) {
        uint8_t *next = sim_room_record(&g_sim, h, 3, room);
        memcpy(next, rec, 180), memset(rec, 0, 180);
        rec = next;
        g_world.block[BLK_ROOM_COUNTS][h * 100 + 3] = (uint8_t)(room + 1);
    }
    p[66] = (uint8_t)room;
    if (kind == 1) p[104] = 1; /* dead: wait advances, but no exception or staying effects */
    if (kind == 2) put16(rec + 22, 0); /* ordinary stay must not refresh screen 29 */
    if (kind >= 3) rec[15] = 5;
    if (kind == 3 || kind == 6) p[44] = 89, p[56] = 1;
    g_image[0x954 + 20] = 2; /* nontrivial loss range: raw zero still loses one health */
    if (kind == 4) put16(rec + 22, 89), p[78] = 20; /* early patience-ladder failure */
    if (kind == 5) p[78] = 100; /* comfort still runs before the cap */
    for (int player = 0; player < 2; player++) {
        uint8_t *pb = g_world.block[BLK_PLAYER0 + player];
        memset(pb, 0x62 + player, sizeof sim89_expected[player]);
        int selected = player == h;
        int screen = selected ? view.screen : other ? 59 : 0;
        if (!player) g_sim.screen = screen;
        put16(pb + 4, player ? screen : screen == 59 ? 0 : 59);
        put32(pb + 0x12, selected ? view.request : (uint32_t)other);
        put16(pb + 0x2C, selected ? view.watch : other ? 261 : 517);
        put16(pb + 0x10, 7), put16(pb + 2, 9); /* neither shown room nor seat matches */
        pb[0x44] = pb[0x49] = pb[0x30] = (uint8_t)old;
        memcpy(sim89_expected[player], pb, sizeof sim89_expected[player]);
    }
    return rec;
}
static size_t waiting_departure_prefix(uint32_t *raw, int h) {
    size_t n = 0;
    for (int i = 0; i < 2 * (h + 1); i++) raw[n++] = 0;
    raw[n++] = 2; /* no care drop */
    raw[n++] = 3; /* no mood drift */
    return n;
}
static void waiting_departure_case(int h, int human, WaitingView view, int other, int old, int kind, int room) {
    uint8_t *rec = waiting_departure_world(h, human, view, other, old, kind, room), *p = patient(261);
    uint8_t expected_room[180], expected_patient[106];
    memcpy(expected_room, rec, sizeof expected_room), memcpy(expected_patient, p, sizeof expected_patient);
    int eligible = h < 2 && human, leave = kind != 1 && kind != 2 && !(eligible && view.protects);
    int comfort = kind >= 3 && !leave, watch = h < 2 && view.watch == 261;
    put16(expected_room + 22, get16(rec + 22) + 1);
    if (leave) {
        memset(expected_room + 20, 0, 4);
        expected_patient[3] = expected_patient[4] = 255;
        expected_patient[78] = 40, expected_patient[20 + h] = 38;
    } else if (comfort && kind != 4 && kind != 5) expected_patient[78]++;
    if (kind == 6 && comfort) expected_patient[44] = 88, expected_patient[67] = 89;
    if (eligible && kind != 1 && kind != 2 && view.refresh) sim89_expected[h][0x44] = 255;
    if (h == 0 && watch && (leave || comfort)) sim89_expected[0][0x49] = 255;
    if (watch && kind == 6 && comfort) sim89_expected[h][0x30] = 255;
    uint32_t raw[64];
    size_t n = waiting_departure_prefix(raw, h);
    if (comfort) {
        if (kind == 4) raw[n++] = 0, raw[n++] = 1, raw[n++] = 1; /* patience -1 */
        raw[n++] = 0, raw[n++] = 0, raw[n++] = 1; /* comfort +1, man's replacement draw */
        if (kind == 3) raw[n++] = 0; /* worsening gate fails */
        if (kind == 6) raw[n++] = 3, raw[n++] = 11, raw[n++] = 0, raw[n++] = 0;
    }
    for (int i = 0; i < 2 * (15 - h); i++) raw[n++] = 0;
    SimScript script = {.v = raw, .n = n};
    g_sim.rng_script = &script;
    int before = failures;
    CHECK(sim_call(&g_sim, 0x6BFC8));
    g_sim.rng_script = NULL;
    CHECK(script.used == n && script.extra == 0);
    sim89_check_players();
    CHECK(memcmp(rec, expected_room, sizeof expected_room) == 0);
    CHECK(memcmp(p, expected_patient, sizeof expected_patient) == 0);
    if (before != failures) fprintf(stderr, "  SIM-99 h %d human %d screen %d watch %d request %u other %d old %d kind %d room %d\n",
                                   h, human, view.screen, view.watch, view.request, other, old, kind, room);
}
static void test_waiting_departure(void) {
    static const WaitingView VIEW[] = {
        {0, 261, 0, 0, 0}, {29, 261, 0, 1, 0}, {59, 261, 1, 0, 0}, {59, 517, 0, 0, 0},
        {0x13B, 261, 0, 0, 0}, {0x11D, 261, 0, 0, 0}, {59, 0, 0, 0, 0},
        {0, 517, 1, 0, 1}, {0, 517, 1, 0, 0x100}, {0, 517, 1, 0, 0x10000},
        {0, 517, 1, 0, 0x1000000}, {0, 517, 1, 0, 0x80000000u}, {29, 261, 1, 0, 1}
    };
    static const int HUMAN[] = {0, 1, 2, 255}, OLD[] = {0, 55, 255};
    for (int h = 0; h < 16; h++)
        for (int hu = 0; hu < 4; hu++)
            for (size_t v = 0; v < sizeof VIEW / sizeof *VIEW; v++)
                for (int other = 0; other < 2; other++)
                    waiting_departure_case(h, HUMAN[hu], VIEW[v], other, OLD[(h + hu) % 3], 0, h % 2);
    for (int h = 0; h < 2; h++) {
        for (int screen = 0; screen < 256; screen++)
            waiting_departure_case(h, 1, (WaitingView){screen, 261, screen == 59, screen == 29, 0}, 1, 55, 0, 0);
        for (int kind = 1; kind <= 6; kind++)
            for (size_t v = 0; v < sizeof VIEW / sizeof *VIEW; v++)
                for (int old = 0; old < 3; old++)
                    waiting_departure_case(h, 1, VIEW[v], 1, OLD[old], kind, 1);
    }
}

/* The first existing news draw observes +44 while the place and patient are still intact. No added observer
 * or random draw is needed. This does not distinguish a swap with the adjacent news-header writes. */
static uint8_t *waiting_boundary_record;
static int waiting_boundary_h, waiting_boundary_seen;
static void waiting_departure_boundary(const SimScript *script, const char *message) {
    (void)message;
    CHECK(script->used == script->n);
    waiting_boundary_seen = 1;
    sim89_check_players();
    CHECK(get16(waiting_boundary_record + 20) == 261 && get16(waiting_boundary_record + 22) == 180);
    CHECK(patient(261)[3] == waiting_boundary_h && patient(261)[4] == 3 && patient(261)[78] == 50);
    CHECK(sim_news_item(&g_sim, 0)[0] == 2 && sim_news_item(&g_sim, 0)[20] == 0x17);
    longjmp(sim89_draw_boundary, 1);
}
static void test_waiting_departure_order(void) {
    for (int h = 0; h < 2; h++) {
        waiting_boundary_record = waiting_departure_world(h, 1, (WaitingView){29, 261, 0, 1, 0}, 1, 55, 0, 0);
        for (int i = 0; i < 100; i++) sim_news_item(&g_sim, i)[0] = 0;
        sim89_expected[h][0x44] = 255;
        uint32_t raw[64];
        SimScript script = {.v = raw, .n = waiting_departure_prefix(raw, h)};
        waiting_boundary_h = h, waiting_boundary_seen = 0;
        g_sim.rng_script = &script;
        g_sim.rng_failure = waiting_departure_boundary;
        if (!setjmp(sim89_draw_boundary)) {
            sim_call(&g_sim, 0x6BFC8);
            CHECK(0);
        }
        g_sim.rng_script = NULL;
        g_sim.rng_failure = NULL;
        CHECK(waiting_boundary_seen);
    }
}

/* SIM-100: two separated ward slots and two reception places expose accidental skips of the remaining room.
 * Screen expectations are independent of the production predicates; 0x16D/0x139 are full-word nonmatches. */
typedef struct {
    int screen0, screen1, overview, head_players;
} ReceptionView;
static const ReceptionView RECEPTION_VIEW[] = {
    {0, 0, 0, 0}, {109, 0, 1, 0}, {0, 109, 1, 0}, {109, 109, 1, 0},
    {57, 0, 0, 1}, {0, 57, 0, 2}, {57, 57, 0, 3}, {54, 54, 0, 0},
    {0x16D, 0, 0, 0}, {0, 0x16D, 0, 0}, {0x139, 0, 0, 0}, {0, 0x139, 0, 0}
};
static uint8_t *reception_departure_world(int h, int human, ReceptionView view, int ward_wait, int head_wait,
                                        int head_kind, int fields, int old) {
    fresh();
    g_world.block[BLK_ROOM_COUNTS][h * 100 + 2] = 1;
    world_hospital(&g_world, h)[0x14] = (uint8_t)human;
    world_hospital(&g_world, h)[0x15] = 0x7E; /* a nonzero adjacent byte is not the human flag */
    uint8_t *rec = sim_room_record(&g_sim, h, 2, 0);
    rec[0x45] = 5; /* unstaffed tail comfort: +1 to each living retained reception patient */
    put16(rec + 0x46, 261), put16(rec + 0x48, ward_wait);
    put16(rec + 0x5A, 263), put16(rec + 0x5C, ward_wait);
    put16(rec + 10, head_kind == 2 ? 0 : 262), put16(rec + 12, head_wait);
    put16(rec + 14, 264), put16(rec + 16, 10);
    for (int index = 261; index <= 264; index++) {
        uint8_t *p = patient(index);
        memset(p, 0x22, 106); /* distinguish every reset from fields that must remain */
        p[0] = 0, p[3] = (uint8_t)h, p[4] = index & 1 ? 6 : 2;
        p[20 + h] = 50, p[44] = 100, p[78] = 50, p[104] = index == 262 && head_kind == 1;
    }
    g_sim.screen = view.screen0;
    for (int player = 0; player < 2; player++) {
        uint8_t *pb = g_world.block[BLK_PLAYER0 + player];
        memset(pb, 0x62 + player, sizeof sim89_expected[player]);
        put16(pb + 4, player ? view.screen1 : view.screen0 == 0 ? 109 : 0);
        put32(pb + 0x12, fields ? UINT32_MAX : 0);
        put16(pb + 0x2C, (player ^ fields) ? 264 : 262);
        put16(pb + 2, fields ? 5 : 0), put16(pb + 0x10, fields ? 7 : 0);
        pb[0x49] = (uint8_t)old;
        memcpy(sim89_expected[player], pb, sizeof sim89_expected[player]);
    }
    for (int i = 0; i < 100; i++) sim_news_item(&g_sim, i)[0] = 2;
    return rec;
}
static size_t reception_departure_prefix(uint32_t *raw, int h) {
    size_t n = 0;
    for (int i = 0; i < 2 * (h + 1); i++) raw[n++] = 0;
    return n;
}
static void reception_departure_case(int h, int human, ReceptionView view, int ward_wait, int head_wait,
                                    int head_kind, int fields, int old) {
    uint8_t *rec = reception_departure_world(h, human, view, ward_wait, head_wait, head_kind, fields, old);
    uint8_t expected_room[174], expected_patient[4][106], expected_hospital[HOSPITAL_SIZE];
    memcpy(expected_room, rec, sizeof expected_room);
    memcpy(expected_hospital, world_hospital(&g_world, h), sizeof expected_hospital);
    expected_hospital[0x17A] = 90;
    for (int i = 0; i < 4; i++) memcpy(expected_patient[i], patient(261 + i), 106);
    int ward_after = (ward_wait + 1) & 65535;
    int ward_leaves = ward_after >= 60 && ward_after < 32768 && !view.overview;
    int head_leaves = head_kind == 0 && head_wait >= 200 && head_wait < 32768 && !view.overview &&
                      !(human && (view.head_players & (h ? 2 : 1)));
    uint32_t raw[96];
    size_t n = reception_departure_prefix(raw, h);
    for (int i = 0; i < 2; i++) {
        int at = 0x46 + i * 20;
        put16(expected_room + at + 2, ward_after);
        if (ward_leaves) {
            memset(expected_room + at, 0, 4);
            uint8_t *p = expected_patient[i * 2];
            p[3] = p[4] = p[82] = 255, p[20 + h] = 38;
            p[42] = p[43] = p[45] = p[66] = p[79] = p[81] = p[93] = p[105] = 0;
            memset(p + 48, 0, 8), memset(p + 96, 0, 8);
            p[44] = 70, memset(p + 67, 70, 10), p[77] = 100;
            for (int draw = 0; draw < 11; draw++) raw[n++] = 0;
        }
    }
    if (head_leaves) {
        memset(expected_room + 10, 0, 16);
        put16(expected_room + 10, 264), put16(expected_room + 12, 11);
        expected_patient[1][3] = expected_patient[1][4] = 255;
        expected_patient[1][78] = 40, expected_patient[1][20 + h] = 38;
    } else {
        if (head_kind != 2) put16(expected_room + 12, head_wait + 1);
        put16(expected_room + 16, 11);
    }
    if (!head_leaves && head_kind != 2) {
        raw[n++] = 3; /* drift gate, including a dead head */
        if (head_kind == 0) {
            raw[n++] = 1, raw[n++] = 0, raw[n++] = 1, raw[n++] = 0;
            expected_patient[1][78] = 51;
            if (h == 0 && !fields) sim89_expected[0][0x49] = 255;
        }
    }
    raw[n++] = 3, raw[n++] = 1, raw[n++] = 0, raw[n++] = 1, raw[n++] = 0; /* witness drift and comfort */
    expected_patient[3][78] = 51;
    if (h == 0 && fields) sim89_expected[0][0x49] = 255;
    expected_room[0x45] = 4;
    for (int i = 0; i < 2 * (15 - h); i++) raw[n++] = 0;
    SimScript script = {.v = raw, .n = n};
    g_sim.rng_script = &script;
    int before = failures;
    CHECK(sim_call(&g_sim, 0x67611));
    g_sim.rng_script = NULL;
    CHECK(script.used == n && script.extra == 0);
    sim89_check_players();
    CHECK(memcmp(rec, expected_room, sizeof expected_room) == 0);
    for (int i = 0; i < 4; i++) CHECK(memcmp(patient(261 + i), expected_patient[i], 106) == 0);
    CHECK(memcmp(world_hospital(&g_world, h), expected_hospital, sizeof expected_hospital) == 0);
    if (before != failures) fprintf(stderr, "  SIM-100 h %d human %d screens %d/%d waits %d/%d head %d fields %d old %d\n",
                                   h, human, view.screen0, view.screen1, ward_wait, head_wait, head_kind, fields, old);
}
static void test_reception_departure(void) {
    static const int H[] = {0, 1, 2, 15}, HUMAN[] = {0, 1, 2, 255}, OLD[] = {0, 55, 255};
    static const int WARD[] = {58, 59, 60, 32766, 32767, 32768, 65534, 65535};
    static const int HEAD[] = {199, 200, 201, 32767, 32768, 65535};
    for (int hi = 0; hi < 4; hi++)
        for (int hu = 0; hu < 4; hu++)
            for (size_t v = 0; v < sizeof RECEPTION_VIEW / sizeof *RECEPTION_VIEW; v++)
                for (int fields = 0; fields < 2; fields++) {
                    for (int w = 0; w < 8; w++)
                        reception_departure_case(H[hi], HUMAN[hu], RECEPTION_VIEW[v], WARD[w], 200, 0, fields, OLD[w % 3]);
                    for (int w = 0; w < 6; w++)
                        for (int kind = 0; kind < 3; kind++)
                            reception_departure_case(H[hi], HUMAN[hu], RECEPTION_VIEW[v], 59, HEAD[w], kind, fields, OLD[w % 3]);
                }
}

/* Observe guards at existing draws: after both protected ward slots, before departure's first news draw, and
 * after protected head comfort at the witness's drift. Pure screen-read ordering is not observable here. */
static uint8_t *reception_boundary_record;
static int reception_boundary_h, reception_boundary_kind, reception_boundary_seen;
static void reception_departure_boundary(const SimScript *script, const char *message) {
    (void)message;
    CHECK(script->used == script->n);
    reception_boundary_seen = 1;
    sim89_check_players();
    CHECK(get16(reception_boundary_record + 0x46) == 261 && get16(reception_boundary_record + 0x5A) == 263);
    CHECK(get16(reception_boundary_record + 0x48) == (reception_boundary_kind == 0 ? 60 : 59));
    CHECK(get16(reception_boundary_record + 0x5C) == (reception_boundary_kind == 0 ? 60 : 59));
    CHECK(get16(reception_boundary_record + 10) == 262);
    CHECK(get16(reception_boundary_record + 12) == (reception_boundary_kind == 1 ? 200 : 201));
    CHECK(patient(262)[3] == reception_boundary_h && patient(262)[4] == 2);
    CHECK(patient(262)[78] == (reception_boundary_kind == 2 ? 51 : 50));
    CHECK(patient(264)[78] == 50 && reception_boundary_record[0x45] == 5);
    if (reception_boundary_kind == 1) CHECK(sim_news_item(&g_sim, 0)[20] == 0x17);
    if (reception_boundary_kind == 2) CHECK(get16(reception_boundary_record + 16) == 11);
    longjmp(sim89_draw_boundary, 1);
}
static void test_reception_departure_order(void) {
    for (int h = 0; h < 3; h++)
        for (int kind = 0; kind < 3; kind++) {
            ReceptionView view = kind == 0 ? (ReceptionView){109, 0, 1, 0}
                                : kind == 2 ? (ReceptionView){57, 57, 0, 3} : (ReceptionView){0, 0, 0, 0};
            reception_boundary_record = reception_departure_world(h, 1, view, kind == 0 ? 59 : 58, 200, 0, 0, 55);
            for (int i = 0; i < 100; i++) sim_news_item(&g_sim, i)[0] = 0;
            uint32_t raw[64];
            size_t n = reception_departure_prefix(raw, h);
            if (kind == 2) {
                raw[n++] = 3, raw[n++] = 1, raw[n++] = 0, raw[n++] = 1, raw[n++] = 0;
                if (!h) sim89_expected[0][0x49] = 255;
            }
            SimScript script = {.v = raw, .n = n};
            reception_boundary_h = h, reception_boundary_kind = kind, reception_boundary_seen = 0;
            g_sim.rng_script = &script;
            g_sim.rng_failure = reception_departure_boundary;
            if (!setjmp(sim89_draw_boundary)) {
                sim_call(&g_sim, 0x67611);
                CHECK(0);
            }
            g_sim.rng_script = NULL;
            g_sim.rng_failure = NULL;
            CHECK(reception_boundary_seen);
        }
}

/* SIM-101: run the complete reception tick with a real employee slot, a waiting patient and optional staffed
 * club. Pending state must survive the staff-selection view, while patient care and the timer still advance. */
static void staff_break_view_case(int h, int human, int screen, int hours, int pending, int club_kind, int missing) {
    fresh();
    g_world.block[BLK_ROOM_COUNTS][h * 100 + 2] = 1;
    world_hospital(&g_world, h)[0x14] = (uint8_t)human;
    uint8_t *rec = sim_room_record(&g_sim, h, 2, 0), *p = patient(261);
    put16(rec + 8, missing == 2 ? 0 : 7), put16(rec + 10, 261), put16(rec + 12, 10);
    rec[0x45] = 5;
    p[3] = (uint8_t)h, p[4] = 2, p[44] = 100, p[78] = 50, p[20 + h] = 50;
    uint8_t *employee = g_world.block[BLK_STAFF] + 7 * 32;
    memset(employee, 0x22, 32);
    employee[1] = 1, employee[3] = (uint8_t)h, employee[4] = 2, employee[5] = 100;
    employee[14] = 10, employee[26] = 3;
    uint8_t *sl = staff_slots(&g_sim, h) + 17 * STAFF_SLOT_SIZE;
    memset(sl, 0x33, STAFF_SLOT_SIZE);
    put16(sl, missing == 1 ? 8 : 7);
    sl[3] = 2, sl[4] = 3, sl[10] = (uint8_t)hours, sl[18] = (uint8_t)pending;
    if (club_kind) {
        g_world.block[BLK_ROOM_COUNTS][h * 100 + 14] = 1;
        uint8_t *club = sim_room_record(&g_sim, h, 14, 0);
        put16(club + 2, 9), put16(club + 4, 10), club[0] = 10;
    }
    put16(g_world.block[BLK_STAFF] + 9 * 32 + 16, 100);
    put16(g_world.block[BLK_STAFF] + 10 * 32 + 16, 200);
    g_sim.screen = screen;
    for (int player = 0; player < 2; player++) {
        uint8_t *pb = g_world.block[BLK_PLAYER0 + player];
        memset(pb, 0x62 + player, sizeof sim89_expected[player]);
        put16(pb + 4, screen == 54 ? 0 : 54); /* both saved screens disagree with live protection */
        put16(pb + 0x2C, player || human == 255 ? 517 : 261);
        put32(pb + 0x12, human == 255 ? UINT32_MAX : 0);
        put16(pb + 2, 7), put16(pb + 0x10, 9);
        pb[0x49] = 55;
        memcpy(sim89_expected[player], pb, sizeof sim89_expected[player]);
    }
    uint8_t expected_staff[3][32], expected_slot[STAFF_SLOT_SIZE], expected_room[174], expected_patient[106];
    memcpy(expected_staff[0], employee, 32);
    memcpy(expected_staff[1], g_world.block[BLK_STAFF] + 9 * 32, 32);
    memcpy(expected_staff[2], g_world.block[BLK_STAFF] + 10 * 32, 32);
    memcpy(expected_slot, sl, sizeof expected_slot), memcpy(expected_room, rec, sizeof expected_room);
    memcpy(expected_patient, p, sizeof expected_patient);
    int eligible = !missing && pending && hours >= 10 && hours < 128 && screen != 54;
    int rest = eligible && club_kind != 1, staffed = missing != 2 && !rest;
    if (eligible) expected_slot[18] = 0;
    if (eligible && club_kind == 1) put16(expected_staff[1] + 16, 101), put16(expected_staff[2] + 16, 201);
    if (rest) {
        expected_staff[0][4] = expected_slot[3] = 24;
        expected_staff[0][26] = expected_slot[4] = 0;
        expected_slot[16] = 2, expected_slot[17] = 0, expected_slot[19] = 40;
        put16(expected_room + 8, 0);
    }
    put16(expected_room + 12, 11);
    expected_room[0x45] = rest ? 0 : 4;
    expected_patient[78] = staffed ? 51 : 50;
    if (!h && human != 255 && staffed) sim89_expected[0][0x49] = 255;
    uint32_t raw[64];
    size_t n = reception_departure_prefix(raw, h);
    if (eligible && club_kind) raw[n++] = club_kind == 1 ? 0 : 63;
    raw[n++] = 3; /* drift gate */
    if (staffed) raw[n++] = 0, raw[n++] = 0; /* staffed comfort +1 */
    if (!rest) raw[n++] = 0; /* tail comfort gate fails, including no employee */
    for (int i = 0; i < 2 * (15 - h); i++) raw[n++] = 0;
    SimScript script = {.v = raw, .n = n};
    g_sim.rng_script = &script;
    int before = failures;
    CHECK(sim_call(&g_sim, 0x67611));
    g_sim.rng_script = NULL;
    CHECK(script.used == n && script.extra == 0);
    CHECK(memcmp(employee, expected_staff[0], 32) == 0);
    CHECK(memcmp(g_world.block[BLK_STAFF] + 9 * 32, expected_staff[1], 32) == 0);
    CHECK(memcmp(g_world.block[BLK_STAFF] + 10 * 32, expected_staff[2], 32) == 0);
    CHECK(memcmp(sl, expected_slot, sizeof expected_slot) == 0);
    CHECK(memcmp(rec, expected_room, sizeof expected_room) == 0);
    CHECK(memcmp(p, expected_patient, sizeof expected_patient) == 0);
    sim89_check_players();
    if (before != failures) fprintf(stderr, "  SIM-101 h %d human %d screen %d hours %d pending %d club %d missing %d\n",
                                   h, human, screen, hours, pending, club_kind, missing);
}
static void test_staff_break_view(void) {
    static const int H[] = {0, 1, 2, 15}, HUMAN[] = {0, 1, 2, 255}, SCREEN[] = {0, 54, 310, 65535};
    static const int HOURS[] = {0, 9, 10, 127, 128, 255}, PENDING[] = {0, 1, 255};
    for (int hi = 0; hi < 4; hi++)
        for (int hu = 0; hu < 4; hu++)
            for (int screen = 0; screen < 4; screen++)
                for (int hours = 0; hours < 6; hours++)
                    for (int pending = 0; pending < 3; pending++)
                        for (int club = 0; club < 3; club++)
                            staff_break_view_case(H[hi], HUMAN[hu], SCREEN[screen], HOURS[hours], PENDING[pending], club, 0);
    for (int hi = 0; hi < 4; hi++)
        for (int screen = 0; screen < 2; screen++)
            for (int missing = 1; missing <= 2; missing++)
                staff_break_view_case(H[hi], 1, SCREEN[screen], 10, 255, 1, missing);
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "residual-contexts")) {
        test_neuro_incoming_context();
        test_treatment_worsen_context();
        test_comfort_gate_context();
        world_free(&g_world);
        if (failures) return 1;
        puts("[OK] SIM-109/110/111 real generator contexts and restoration");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "care-context-carry")) {
        test_care_context_carry();
        world_free(&g_world);
        if (failures) return 1;
        puts("[OK] SIM-108 retained care context, current-patient drops and single-room controls");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "golf-join-context")) {
        test_golf_join_context();
        world_free(&g_world);
        if (failures) return 1;
        puts("[OK] SIM-107 golf join context, real generator and caller restoration");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "organ-offer-quality")) {
        test_organ_offer_quality();
        test_office_offers();
        test_office_offers_order();
        test_sim81_blood_expiry();
        world_free(&g_world);
        if (failures) return 1;
        puts("[OK] SIM-106 organ quality, office cleanup and blood expiry controls");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "office-offers-order")) {
        test_office_offers_order();
        world_free(&g_world);
        if (failures) return 1;
        puts("[OK] office offer cleanup before organ draw");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "office-offers")) {
        test_office_offers();
        test_office_offers_order();
        test_sim81_blood_expiry();
        world_free(&g_world);
        if (failures) return 1;
        puts("[OK] office offer cleanup and expiry controls");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "staff-break-view")) {
        test_staff_break_view();
        test_room_breaks();
        world_free(&g_world);
        if (failures) return 1;
        puts("[OK] staff-break view and room-break controls");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "reception-departure")) {
        test_reception_departure();
        test_reception_departure_order();
        test_mood_notifications();
        test_mood_notification_order();
        test_sim40_reception_men();
        world_free(&g_world);
        if (failures) return 1;
        puts("[OK] reception departure and mood controls");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "waiting-departure")) {
        test_waiting_departure();
        test_waiting_departure_order();
        test_mood_notifications();
        test_mood_notification_order();
        test_sim40_waiting_room_men();
        test_sim41_waiting_count();
        world_free(&g_world);
        if (failures) return 1;
        puts("[OK] waiting departure and mood controls");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "mood-notifications")) {
        test_mood_notifications();
        test_mood_notification_order();
        test_sim40_waiting_room_men();
        test_sim40_reception_men();
        test_sim41_waiting_count();
        world_free(&g_world);
        if (failures) return 1;
        puts("[OK] mood notifications and waiting controls");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "pathology-notifications")) {
        test_pathology_notifications();
        test_pathology_notification_order();
        world_free(&g_world);
        if (failures) return 1;
        puts("[OK] pathology notifications");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "theatre-notifications")) {
        test_theatre_notifications();
        test_theatre_notification_order();
        test_operation();
        test_sim48_op_blood_loss();
        test_sim79_death_alias();
        test_sim79_common_death_control();
        world_free(&g_world);
        if (failures) return 1;
        puts("[OK] theatre notifications and lifecycle/death controls");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "room-progress-notifications")) {
        test_room_progress_notifications();
        test_room_progress_order();
        world_free(&g_world);
        if (failures) return 1;
        puts("[OK] room progress notifications");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "icu-notifications")) {
        test_icu_health_notifications();
        test_icu_progress_notifications();
        test_icu_death_item();
        test_fix11_icu_death_item();
        world_free(&g_world);
        if (failures) return 1;
        puts("[OK] ICU notifications and death controls");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "ward-notifications")) {
        test_ward_health_notifications();
        world_free(&g_world);
        if (failures) return 1;
        puts("[OK] ward health notifications");
        return 0;
    }
    test_staff_break_view();
    test_reception_departure();
    test_reception_departure_order();
    test_waiting_departure();
    test_waiting_departure_order();
    test_mood_notifications();
    test_mood_notification_order();
    test_pathology_notifications();
    test_pathology_notification_order();
    test_theatre_notifications();
    test_theatre_notification_order();
    test_room_progress_notifications();
    test_room_progress_order();
    test_icu_health_notifications();
    test_icu_progress_notifications();
    test_ward_health_notifications();
    test_description_fields();
    test_neuro_incoming_context();
    test_treatment_worsen_context();
    test_comfort_gate_context();
    test_care_context_carry();
    test_golf_join_context();
    test_sim84_hunger();
    test_sim84_health_and_mood();
    test_sim84_states_narcosis_death();
    test_sim84_exits_and_alignment();
    test_rating_ai_construction();
    test_rating_recomputes();
    test_rating_icu_and_bank();
    test_rating_catalog();
    test_rating_reference();
    test_rating_chooser();
    test_rating_staff_and_fittings();
    test_rating_loops();
    test_rating_fittings_install_and_clerk();
    test_rating_decision_and_discharge();
    test_rating_operator_writers();
    test_sim79_death_alias();
    test_sim79_common_death_control();
    test_sim45_onset_b_draw();
    test_sim46_sim57_discharge();
    test_sim47_seat_bound();
    test_sim47_bed_rounds();
    test_sim47_last_place();
    test_sim48_op_blood_loss();
    test_sim49_torture_wrong_work();
    test_sim56_bill_zero();
    test_sim54_mood_draw_ends_care();
    test_sim62_care_without_nurse();
    test_sim61_assignment_ends_call();
    test_sim53_sales_keep_rating();
    test_sim64_wear_per_slot();
    test_sim55_dup_byte_writers();
    test_sim65_row90_need4();
    test_sim51_onset_stores_city();
    test_sim33_cooling_of_hospital0();
    test_sim34_decline_history();
    test_sim34_threshold_draws();
    test_sim34_signed_heal();
    test_sim35_op_transfusion();
    test_sim35_icu_transfusion();
    test_sim36_tick_diagnoses_ai();
    test_sim37_operator_only_chooses();
    test_sim36_ai_guess_correction();
    test_sim36_chooser_record0();
    test_sim40_waiting_room_men();
    test_sim40_reception_men();
    test_sim41_waiting_count();
    test_sim78_worsening_death_flag();
    test_ward_cure_history();
    test_fix25_ward_cleanup();
    test_fix26_op_doctor_room();
    test_fix27_torture_work();
    test_fix29_signed_hours();
    test_fix30_day_list_date();
    test_pathology_stale_base();
    test_pathology_base_daily_cost();
    test_wear_removal_ends_furniture();
    test_create_room_condition_order();
    test_home_treatment_type_gate();
    test_fix22_bank_compaction();
    test_fix23_organ_quality();
    test_fix11_icu_death_item();
    test_generator_step();
    test_random_mask();
    test_rng_script();
    test_index_draw_first();
    test_reseed();
    test_home_treatment_calls();
    test_animation_counters();
    test_default_inputs();
    test_picker_draws();
    test_scratch_writers();
    test_waiting_capacity_cut();
    test_operator_inputs();
    test_scheduled_inputs();
    test_onset_type_vectors();
    test_onset_person_inputs();
    test_cd_build_inputs();
    test_clock_registers();
    test_bed_inputs();
    test_treatment_worsening_notification();
    test_treatment_notification_callers();
    test_general_worsening_notifications();
    test_nurse_health_notifications();
    test_sim90_notification_callers();
    test_ward_recovery_notifications();
    test_sim90_notification_order();
    test_neuro_setback();
    test_ward_cure_paths();
    test_ward_recovery();
    test_ward_step_notification();
    test_decline_one_r3();
    test_contest_gate_first();
    test_single_nurse_bonus_site();
    test_onset_rate();
    test_waiting_room_credit();
    test_operator_release();
    test_operator_staffing_ends();
    test_reception_comfort();
    test_reception_cash();
    test_reception_evening();
    test_reception_staffing_ends();
    test_ward_waiting_test();
    test_operator_night_call();
    test_waiting_room_release();
    test_room_breaks();
    test_unit_counts();
    test_treating_draws();
    test_bill_opinion();
    test_reception_supplies();
    test_reception_request();
    test_diagnosis_start();
    test_staff_credit();
    test_drift_needs_staff();
    test_nurse_bonus_mood();
    test_death_keeps_opinion();
    test_invoice();
    test_pairing_and_hiring();
    test_pairing_respects_requirements();
    test_staffing_need_opens_store_clerk();
    test_store_clerk_evening();
    test_arrival_admits_when_staffed();
    test_arrival_refused_without_reception_staff();
    test_home_treatment_cures();
    test_discharge_counts_per_department();
    test_off_duty_return();
    test_course_end_item();
    test_equipment_purchase_refund();
    test_price_index_activity();
    test_ward_discharges_cured();
    test_ward_to_intensive_care();
    test_ward_step_use();
    test_ward_meals();
    test_meal_start();
    test_meal_effects();
    test_sim42_worn_item_tidies();
    test_sim43_comfort_meal_id();
    test_store_tidy();
    test_ward_wants_beds();
    test_ward_nurse_mood();
    test_ward_beds_fill();
    test_diagnosis_bonus();
    test_treatment_bonus();
    test_take_types();
    test_player_take();
    test_step4_return();
    test_dental_diagnosis();
    test_room_wear();
    test_room_wear_gates();
    test_tick64();
    test_tick64_screen();
    test_store_screen();
    test_nurse_mood_flag();
    test_dental_fittings_first();
    test_waiting_fittings_first();
    test_store_take_tidies();
    test_store_take_recounts_all();
    test_meal_purchase();
    test_ai_department_tests();
    test_bring_states();
    test_ward_supplies();
    test_ward_operator_nurse();
    test_kitchen_operator();
    test_kitchen_minute();
    test_ai_krankenzimmer_test();
    test_sim27_hospital9_plan();
    test_sim28_torture_occupancy();
    test_intensive_care_release();
    test_icu_death_item();
    test_operation();
    test_bank_operator();
    test_bank_capacity();
    test_ai_buys_blood();
    test_organ_ageing();
    test_bank_staff_leaves();
    test_donor_into_bank();
    test_donation_step();
    test_bank_compaction();
    test_equipment_sale();
    test_furniture_sale();
    test_project_start();
    test_project_run();
    test_course_booking();
    test_health_history_shift();
    test_ai_orders_plots_then_rents();
    test_ai_buys_when_rich();
    test_opinion_score_ranking();
    test_daily_costs();
    test_job_ad_cost();
    test_leaders_and_debt();
    test_clock();
    test_midnight_call_order();
    test_day_change_score();
    test_day_list_before_wrap();
    test_year_change_report();
    test_opinion_drift_stops_at_60();
    test_nurse_ad_minimum_bust();
    test_mood_working_signed_hours();
    test_random_range();
    test_sim81_blood_expiry();
    test_office_offers();
    test_office_offers_order();
    test_organ_offer_quality();
    test_sim80_equipment_wear();
    test_sim80_equipment_no_use();
    test_equipment_decision();
    test_equipment_pending();
    test_equipment_supplies();
    test_ai_neuro_bed();
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] all checks passed\n");
    return 0;
}
