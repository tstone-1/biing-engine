/* The scheduled bookkeeping of docs/sim-bookkeeping.md (engine/src/sim_books.c, the leave helper 0x2301C and the
 * waiting-room operator's comfort counter). Synthetic world and program image (see AGENTS.md). */
#include <stdio.h>
#include <string.h>

#include "fixes.h"
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

/* furniture catalog 0xEE0, 18 bytes: +4/+5 ratings, +6 kind, +16 wear; equipment catalog 0x144A, 20 bytes: +6 kind,
 * +10 the first capability */
static uint8_t *furn(int id) { return g_image + 0xEE0 + 18 * id; }
static uint8_t *equip(int id) { return g_image + 0x144A + 20 * id; }
enum { FURN = 0x40, FCOND = 0x50, EQ = 0x58, ECOND = 0x68, SLOTS = 8 };

static void fresh(void) {
    world_free(&g_world);
    world_alloc(&g_world);
    memset(g_world.block[BLK_ROOM_SLOTS], 0xFF, g_world.size[BLK_ROOM_SLOTS]);
    memset(g_world.block[BLK_JOB_OPENINGS], 0xFF, g_world.size[BLK_JOB_OPENINGS]);
    for (int i = 0; i < 1041; i++) g_world.block[BLK_STAFF][i * 32 + 3] = 0xFF; /* nobody employed */
    memset(g_image, 0, sizeof g_image);
    g_prog.image.data = g_image;
    g_prog.image.size = sizeof g_image;
    /* one layout for every category: 8 furniture slots at 0x40 (conditions 0x50), 8 equipment slots at 0x58 (0x68) */
    for (int cat = 2; cat <= 18; cat++) {
        const uint16_t D[7] = {FURN, SLOTS, EQ, SLOTS, FCOND, ECOND, 0x74};
        for (int i = 0; i < 7; i++) put16(g_image + 0x4758 + 14 * cat + 2 * i, D[i]);
    }
    furn(1)[6] = 1;                    /* table, wear 0 */
    furn(2)[6] = 5;                    /* chair, wear 0 */
    furn(8)[6] = 5, furn(8)[16] = 3;   /* a chair with wear 3 */
    furn(3)[6] = 8;                    /* bed */
    furn(4)[6] = 2, furn(5)[6] = 6;    /* decoration, books */
    furn(6)[6] = 0x12;                 /* carpet */
    furn(7)[4] = 60, furn(7)[5] = 60;  /* a picture rated 120 (kind 0 is never checked) */
    world_set_tail(&g_world, TAIL_DAY, 5), world_set_tail(&g_world, TAIL_MONTH, 2), world_set_tail(&g_world, TAIL_YEAR, 1995);
    world_set_tail(&g_world, TAIL_HOUR, 12), world_set_tail(&g_world, TAIL_HOURS, 51);
    sim_init(&g_sim, &g_world, &g_prog, 11);
}

static uint8_t *room(int h, int cat, int i) {
    uint8_t *c = g_world.block[BLK_ROOM_COUNTS] + h * 100 + cat;
    if (*c < i + 1) *c = (uint8_t)(i + 1);
    return sim_room_record(&g_sim, h, cat, i);
}
static uint8_t *staff(int i) { return g_world.block[BLK_STAFF] + i * 32; }
static uint8_t *slot(int h, int i) { return staff_slots(&g_sim, h) + i * STAFF_SLOT_SIZE; }
static void hire(int h, int k, int index, int group, int state, int mood) {
    uint8_t *sl = slot(h, k), *st = staff(index);
    put16(sl, index), sl[2] = (uint8_t)group, sl[3] = (uint8_t)state;
    st[1] = (uint8_t)group, st[3] = (uint8_t)h, st[4] = (uint8_t)state, st[21] = (uint8_t)mood;
}
static void put_furn(uint8_t *rec, int k, int id, int cond) { put16(rec + FURN + 2 * k, id), rec[FCOND + k] = (uint8_t)cond; }
static int furn_id(const uint8_t *rec, int k) { return (uint16_t)get16(rec + FURN + 2 * k); }
static void ai(int h) { world_hospital(&g_world, h)[0x15] = 0xFF; } /* in business, not human: the picker's choice */
static int run(uint32_t routine) { return sim_call(&g_sim, routine); }

/* 0x2C0E7: the first item of the kind is used; wear = catalog byte 16, else 1 when r127 = 47 (one draw per call, the
 * stream predicted by a twin generator); at 0 or less (signed) the item goes and the lists close up (0x2BFB7); a
 * missing kind draws nothing. */
static void test_use_furniture(void) {
    fresh();
    uint8_t *rec = room(3, 5, 0);
    put_furn(rec, 0, 3, 90), put_furn(rec, 1, 1, 120), put_furn(rec, 2, 1, 50), put_furn(rec, 3, 8, 10), put_furn(rec, 4, 3, 77);
    Sim twin = g_sim;
    int hits = 0;
    for (int i = 0; i < 12800; i++) {
        CHECK(sim_use_furniture(&g_sim, rec, 5, 1) == 1);
        hits += sim_bits(&twin, 127) == 47;
    }
    CHECK(rec[FCOND + 1] == 120 - hits && rec[FCOND + 2] == 50 && rec[FCOND] == 90);
    CHECK(hits >= 70 && hits <= 130); /* 1 in 128 */
    uint64_t rng = g_sim.rng;
    uint32_t rng_s = g_sim.rng_s;
    CHECK(sim_use_furniture(&g_sim, rec, 5, 6) == 0 && g_sim.rng == rng && g_sim.rng_s == rng_s); /* missing: no draw */
    for (int i = 0; i < 3; i++) CHECK(sim_use_furniture(&g_sim, rec, 5, 5) == 1);
    CHECK(rec[FCOND + 3] == 1 && furn_id(rec, 3) == 8 && g_sim.rng == rng && g_sim.rng_s == rng_s); /* 10 - 9, byte 16 > 0: no draw */
    CHECK(sim_use_furniture(&g_sim, rec, 5, 5) == 1);
    CHECK(furn_id(rec, 3) == 3 && rec[FCOND + 3] == 77 && furn_id(rec, 4) == 0); /* gone, the list closed up */
    CHECK(sim_use_furniture(&g_sim, rec, 5, 5) == 0);
    put_furn(rec, 5, 8, 3); /* 3 - 3 = 0: broken */
    CHECK(sim_use_furniture(&g_sim, rec, 5, 5) == 1 && furn_id(rec, 4) == 0 && furn_id(rec, 5) == 0);
    put_furn(rec, 5, 8, 200); /* 200 - 3 = 197, negative as a signed byte: broken at once */
    CHECK(sim_use_furniture(&g_sim, rec, 5, 5) == 1 && furn_id(rec, 4) == 0 && furn_id(rec, 5) == 0);
}

/* Treatment room 0x32F6C: the wanted-byte rule (W follows a check only when 0 or the kind), the mood rules and the
 * leave paths through 0x2301C (idle, slot +0x0A / +0x0B kept), and "idle only". */
static uint8_t *treatment(int with_table, int rated) {
    fresh();
    ai(4);
    uint8_t *rec = room(4, 5, 0);
    put_furn(rec, 0, 2, 100);
    if (with_table) put_furn(rec, 1, 1, 100);
    rec[0] = rated ? 120 : 0;
    hire(4, 0, 40, 1, 5, 50), hire(4, 1, 41, 2, 5, 50);
    put16(rec + 8, 40), put16(rec + 0x0A, 41);
    slot(4, 0)[7] = 5, slot(4, 0)[10] = 6, slot(4, 0)[11] = 7;
    return rec;
}

static void test_wanted_rule(void) {
    uint8_t *rec = treatment(0, 1);
    run(0x32F6C);
    CHECK(rec[4] == 1); /* no table: W = 1 */
    rec[4] = 6;
    run(0x32F6C);
    CHECK(rec[4] == 6); /* a rating wish is kept */
    rec = treatment(1, 1);
    rec[4] = 5;
    run(0x32F6C);
    CHECK(rec[4] == 0); /* the chair check clears its own kind */
    rec[4] = 1;
    run(0x32F6C);
    CHECK(rec[4] == 0);
}

static void test_room_mood(void) {
    /* supplied, rated 120: the nurse (first field) + 1 with r15 = 7 (1 in 16), never above 100 */
    uint8_t *rec = treatment(1, 1);
    int rises = 0;
    for (int i = 0; i < 1600; i++) {
        staff(40)[21] = 50;
        run(0x32F6C);
        rises += staff(40)[21] == 51;
        CHECK(staff(40)[21] == 50 || staff(40)[21] == 51);
    }
    CHECK(rises >= 70 && rises <= 135);
    staff(40)[21] = 100;
    int moved = 0;
    for (int i = 0; i < 2000; i++) run(0x32F6C), moved += staff(40)[21] != 100;
    CHECK(moved == 0); /* at 100 the fall needs rating < a new r63, which 120 never is */
    /* idle only: a patient in the room stops the staff draws */
    put16(rec + 0x0C, 9), staff(40)[21] = 50, staff(41)[21] = 50;
    for (int i = 0; i < 500; i++) run(0x32F6C);
    CHECK(staff(40)[21] == 50 && staff(41)[21] == 50);
    /* rated 0: falls (a new r63 above 0), and below 4 the nurse leaves, why 2, idle */
    rec = treatment(1, 0);
    staff(40)[21] = 6;
    for (int i = 0; i < 20000 && get16(rec + 8); i++) run(0x32F6C);
    CHECK(get16(rec + 8) == 0 && rec[5] == 2 && staff(40)[21] == 3);
    CHECK(staff(40)[4] == 0 && staff(40)[26] == 0 && slot(4, 0)[3] == 0 && slot(4, 0)[7] == 0);
    CHECK(slot(4, 0)[10] == 6 && slot(4, 0)[11] == 7); /* not the evening release: no off duty, +0x0A kept */
    /* unsupplied (no table): - (r1 + r1) / 2, below 20 out with why 1; from 0 no wrap */
    /* (SIM-29: only the nurse waits for r15 = 7; the doctor's field +0x0A is updated on every call) */
    rec = treatment(0, 1);
    int drops = 0, doctor_drops = 0;
    for (int i = 0; i < 6400; i++) {
        staff(40)[21] = 60, staff(41)[21] = 60;
        run(0x32F6C);
        drops += staff(40)[21] == 59;
        doctor_drops += staff(41)[21] == 59;
    }
    CHECK(rec[4] == 1 && drops >= 70 && drops <= 135);      /* 1/16 x 1/4 x 6400 = 100 */
    CHECK(doctor_drops >= 1480 && doctor_drops <= 1720);   /* 1/4 x 6400 = 1,600 */
    rec = treatment(0, 1);
    put16(rec + 0x0A, 0), staff(40)[21] = 22;
    for (int i = 0; i < 20000 && get16(rec + 8); i++) run(0x32F6C);
    CHECK(get16(rec + 8) == 0 && rec[5] == 1 && staff(40)[21] == 19 && staff(40)[4] == 0);
    int wrapped = 0;
    for (int trial = 0; trial < 200; trial++) {
        rec = treatment(0, 1);
        sim_seed(&g_sim, 500u + (uint64_t)trial);
        staff(40)[21] = 0;
        put16(rec + 0x0A, 0);
        for (int i = 0; i < 500 && get16(rec + 8); i++) run(0x32F6C);
        wrapped += get16(rec + 8) != 0 || staff(40)[21] != 0 || rec[5] != 1;
    }
    CHECK(wrapped == 0); /* from 0 the fall is skipped: no wrap to 255 */
    /* rated 0: a rise needs r63 = 0 (rating >= r63), about 1 in 1,024 calls */
    rec = treatment(1, 0);
    put16(rec + 0x0A, 0);
    int rare = 0;
    for (int i = 0; i < 20000; i++) {
        staff(40)[21] = 50;
        run(0x32F6C);
        rare += staff(40)[21] == 51;
    }
    CHECK(rare >= 6 && rare <= 40);
}

/* Furniture wears only in the "use" rooms: about 10 points per 1,280 calls for the table and the chair of a
 * reception (chair wear 3 per call); intensive care and the kitchen only test ("has"). */
static void test_wear_by_room(void) {
    fresh();
    uint8_t *r = room(6, 2, 0), *icu = room(6, 9, 0), *k = room(6, 13, 0);
    put_furn(r, 0, 1, 100), put_furn(r, 1, 2, 100), put_furn(r, 2, 1, 100);
    put_furn(icu, 0, 3, 100), put_furn(icu, 1, 2, 100);
    put_furn(k, 0, 1, 100), put_furn(k, 1, 2, 100);
    for (int i = 0; i < 1280; i++) run(0x35A3E), run(0x53845), run(0x53ACD);
    CHECK(r[FCOND] >= 82 && r[FCOND] <= 97 && r[FCOND + 1] >= 82 && r[FCOND + 1] <= 97 && r[FCOND + 2] == 100);
    CHECK(icu[FCOND] == 100 && icu[FCOND + 1] == 100 && k[FCOND] == 100 && k[FCOND + 1] == 100);
    CHECK(r[4] == 0 && icu[4] == 0 && k[4] == 0);
    put16(icu + FURN, 0);
    put16(k + FURN, 0);
    run(0x53845), run(0x53ACD);
    CHECK(icu[4] == 8 && k[4] == 1);
}

/* Each "use" room wears the first item of its first kind (wear-3 items: 3 a call); the "has" rooms never wear. */
static void test_use_rooms(void) {
    static const struct { uint32_t r; int cat, kind, use; } R[] = {
        {0x35A3E, 2, 5, 1}, {0x32F6C, 5, 5, 1}, {0x333FB, 11, 5, 1}, {0x37583, 15, 5, 1}, {0x548EB, 8, 5, 1},
        {0x53845, 9, 5, 0}, {0x53ACD, 13, 5, 0}};
    for (size_t k = 0; k < sizeof R / sizeof *R; k++) {
        fresh();
        uint8_t *rec = room(5, R[k].cat, 0);
        put_furn(rec, 0, 1, 100), put_furn(rec, 1, 3, 100), put_furn(rec, 2, 8, 100);
        for (int i = 0; i < 5; i++) run(R[k].r);
        CHECK(rec[FCOND + 2] == (R[k].use ? 85 : 100));
    }
    fresh(); /* the OP wears its bed */
    furn(9)[6] = 8, furn(9)[16] = 2;
    uint8_t *op = room(5, 7, 0);
    put_furn(op, 0, 9, 100);
    for (int i = 0; i < 5; i++) run(0x5456F);
    CHECK(op[FCOND] == 90);
}

/* The kitchen: staff +6, no why byte; the OP: W at +0x7A, why +0x7B, staff +6 / +8 / +0x0A, idle only. */
static void test_kitchen_and_op(void) {
    fresh();
    uint8_t *k = room(7, 13, 0);
    put_furn(k, 0, 1, 100), put_furn(k, 1, 2, 100);
    hire(7, 0, 50, 4, 13, 3);
    put16(k + 6, 50), k[5] = 0x55, k[8] = 0x66; /* no why byte: +5 and +8 stay */
    for (int i = 0; i < 20000 && get16(k + 6); i++) run(0x53ACD);
    CHECK(get16(k + 6) == 0 && k[5] == 0x55 && k[8] == 0x66 && staff(50)[4] == 0);
    uint8_t *op = room(7, 7, 0);
    put_furn(op, 0, 3, 100);
    op[0] = 120;
    hire(7, 1, 51, 2, 7, 3), hire(7, 2, 52, 1, 7, 3), hire(7, 3, 53, 5, 7, 3);
    put16(op + 6, 51), put16(op + 8, 52), put16(op + 0x0A, 53);
    op[4] = 0x44; /* +4 is not the OP's wanted byte */
    put16(op + FURN, 0);
    run(0x5456F);
    CHECK(op[0x7A] == 8 && op[4] == 0x44);
    op[0] = 0;
    for (int i = 0; i < 20000 && (get16(op + 6) || get16(op + 8) || get16(op + 0x0A)); i++) run(0x5456F);
    CHECK(!get16(op + 6) && !get16(op + 8) && !get16(op + 0x0A) && op[0x7B] == 1);
    uint8_t *t = room(7, 8, 0);
    put_furn(t, 0, 8, 100);
    hire(7, 4, 54, 1, 8, 50);
    put16(t + 8, 54);
    for (int i = 0; i < 10; i++) run(0x548EB);
    CHECK(t[FCOND] == 70 && t[0x7A] == 0); /* the chair wears (3 a call) */
}

/* SIM-29 (docs/sim-drift8.md, "What the engine must change", item 9; section 22.4): in the room-mood routines only the
 * first staff field (+8, the kitchen's +6) waits for r15 = 7; the further fields (+0x0A, the OP's +6) are updated on
 * every call. Draws scripted: a generator state whose next draws under fixed inputs (d, e) give the listed values (the
 * inputs vary with the state, as one pair cannot give every sequence of low bits); the state after them must be
 * reached exactly, so the number of draws is checked too. */
static uint32_t sim29_state(const uint32_t *mask, const uint32_t *val, int n, uint32_t *after, uint32_t *d, uint32_t *e) {
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

/* runs the routine once with the scripted draws; 1 when exactly those draws were made */
static int sim29_run(uint32_t routine, const uint32_t *mask, const uint32_t *val, int n) {
    uint32_t after = 0, d = 0, e = 0, start = sim29_state(mask, val, n, &after, &d, &e);
    CHECK(start != 0);
    SimIn old = sim_set_inputs(&g_sim, sim_in(d, e, 0));
    g_sim.rng_s = start;
    run(routine);
    sim_set_inputs(&g_sim, old);
    return g_sim.rng_s == after;
}

/* an AI room of category cat in hospital 5 with a bed (kind 8) and a chair (kind 5) that wear without a draw, rating
 * 63, no patient */
static uint8_t *sim29_room(int cat) {
    fresh();
    ai(5);
    furn(9)[6] = 8, furn(9)[16] = 2;   /* a bed with wear 2 */
    furn(10)[6] = 1, furn(10)[16] = 2; /* a table with wear 2 */
    uint8_t *rec = room(5, cat, 0);
    put_furn(rec, 0, 9, 100), put_furn(rec, 1, 8, 100), put_furn(rec, 2, 10, 100);
    rec[0] = 63, rec[1] = 0;
    return rec;
}

static void test_sim29_first_field_gate(void) {
    static const uint32_t M15_63[] = {15, 63}, M15_63_63[] = {15, 63, 63}, M63[] = {63}, M15_1_1[] = {15, 1, 1};
    static const uint32_t M15[] = {15};
    /* (a) r15 3, r63 10: nurse 50, doctor 51, two draws */
    uint8_t *rec = sim29_room(15);
    hire(5, 0, 40, 1, 15, 50), hire(5, 1, 41, 10, 15, 50);
    put16(rec + 8, 40), put16(rec + 0x0A, 41);
    static const uint32_t Va[] = {3, 10};
    CHECK(sim29_run(0x37583, M15_63, Va, 2));
    CHECK(staff(40)[21] == 50 && staff(41)[21] == 51);
    /* (b) r15 7, r63 10, r63 10: nurse 51, doctor 51, three draws */
    staff(40)[21] = 50, staff(41)[21] = 50;
    static const uint32_t Vb[] = {7, 10, 10};
    CHECK(sim29_run(0x37583, M15_63_63, Vb, 3));
    CHECK(staff(40)[21] == 51 && staff(41)[21] == 51);
    /* (c) nurse field 0: no r15; r63 10: doctor 51, one draw */
    put16(rec + 8, 0), staff(41)[21] = 50;
    static const uint32_t Vc[] = {10};
    CHECK(sim29_run(0x37583, M63, Vc, 1));
    CHECK(staff(41)[21] == 51);
    /* (d) the doctor's mood 3, r15 3, r63 30 above the rating (10 here: r63 is at most 63, so the brief's "rating 63,
     * r63 70" cannot be drawn); below 4 no second r63: the doctor leaves, two draws */
    put16(rec + 8, 40), staff(40)[21] = 50, staff(41)[21] = 3;
    rec[0] = 10;
    static const uint32_t Vd[] = {3, 30};
    CHECK(sim29_run(0x37583, M15_63, Vd, 2));
    CHECK(rec[5] == 2 && get16(rec + 0x0A) == 0 && get16(rec + 8) == 40 && staff(40)[21] == 50);
    /* (e) a wanted kind (6, kept by the checks): r15 3, r1 1, r1 1: doctor 49; from 20: 19 and leaves, why 1 */
    rec = sim29_room(15);
    hire(5, 0, 40, 1, 15, 50), hire(5, 1, 41, 10, 15, 50);
    put16(rec + 8, 40), put16(rec + 0x0A, 41);
    rec[4] = 6;
    static const uint32_t Ve[] = {3, 1, 1};
    CHECK(sim29_run(0x37583, M15_1_1, Ve, 3));
    CHECK(staff(41)[21] == 49 && staff(40)[21] == 50 && get16(rec + 0x0A) == 41);
    staff(41)[21] = 20;
    CHECK(sim29_run(0x37583, M15_1_1, Ve, 3));
    CHECK(staff(41)[21] == 19 && get16(rec + 0x0A) == 0 && rec[5] == 1);
    /* (f) the OP with +8, +0x0A and +6 set, r15 3: +8 unchanged, +0x0A and +6 both updated */
    rec = sim29_room(7);
    hire(5, 0, 51, 2, 7, 50), hire(5, 1, 52, 1, 7, 50), hire(5, 2, 53, 5, 7, 50);
    put16(rec + 8, 52), put16(rec + 0x0A, 53), put16(rec + 6, 51);
    static const uint32_t Vf[] = {3, 10, 10};
    CHECK(sim29_run(0x5456F, M15_63_63, Vf, 3));
    CHECK(staff(52)[21] == 50 && staff(53)[21] == 51 && staff(51)[21] == 51);
    /* the treatment room, the dental ward and the torture chamber likewise: r15 3, r63 10 updates the doctor only */
    static const struct { uint32_t r; int cat; } TWO[] = {{0x32F6C, 5}, {0x333FB, 11}, {0x548EB, 8}};
    for (unsigned k = 0; k < 3; k++) {
        rec = sim29_room(TWO[k].cat);
        hire(5, 0, 40, 1, TWO[k].cat, 50), hire(5, 1, 41, 10, TWO[k].cat, 50);
        put16(rec + 8, 40), put16(rec + 0x0A, 41);
        CHECK(sim29_run(TWO[k].r, M15_63, Va, 2));
        CHECK(staff(40)[21] == 50 && staff(41)[21] == 51);
    }
    /* (g) reception, waiting room, intensive care and kitchen with their one field set, r15 3: nothing changes, one
     * draw each */
    static const struct { uint32_t r; int cat, field; } ONE[] = {{0x35A3E, 2, 8}, {0x33276, 3, 8}, {0x53845, 9, 8}, {0x53ACD, 13, 6}};
    static const uint32_t Vg[] = {3};
    for (unsigned k = 0; k < 4; k++) {
        rec = sim29_room(ONE[k].cat);
        if (ONE[k].cat == 3) room(5, 2, 0); /* the waiting room reads the reception's queue */
        hire(5, 0, 40, 1, ONE[k].cat, 50);
        put16(rec + ONE[k].field, 40);
        CHECK(sim29_run(ONE[k].r, M15, Vg, 1));
        CHECK(staff(40)[21] == 50 && get16(rec + ONE[k].field) == 40);
    }
}

/* The waiting room 0x33276: chairs wanted when someone has been second in the reception queue for a minute and the
 * room has fewer than 10 seats; its supplied test reads the room's +0x15, so a mood rises above 100 (reproduced); an
 * AI room's unsupplied clerk leaves below 20 with +0x0F = 0, a human room always takes the supplied branch. */
static void test_waiting_room(void) {
    fresh();
    uint8_t *rec = room(8, 3, 0), *rcp = room(8, 2, 0);
    rec[10] = 9;
    run(0x33276);
    CHECK(rec[6] == 0);
    put16(rcp + 0x10, 3);
    run(0x33276);
    CHECK(rec[6] == 5);
    rec[6] = 0, rec[10] = 10;
    run(0x33276);
    CHECK(rec[6] == 0);
    /* overflow: rated 120, W = 0, mood from 100 */
    rec[0] = 120, rec[0x15] = 3;
    hire(8, 0, 60, 1, 3, 100);
    put16(rec + 8, 60);
    for (int i = 0; i < 640; i++) run(0x33276);
    CHECK(staff(60)[21] > 120 && staff(60)[21] <= 160);
    /* unsupplied AI room: out below 20, comfort ended */
    rec[6] = 5, rec[0x0F] = 30, staff(60)[21] = 21;
    for (int i = 0; i < 20000 && get16(rec + 8); i++) run(0x33276);
    CHECK(get16(rec + 8) == 0 && rec[7] == 1 && rec[0x0F] == 0 && staff(60)[21] < 20);
    /* human: W set, but the mood still rises */
    world_hospital(&g_world, 8)[0x14] = 1;
    put16(rec + 8, 60), staff(60)[21] = 50, rec[6] = 5, rec[7] = 0;
    for (int i = 0; i < 1000; i++) run(0x33276);
    CHECK(get16(rec + 8) == 60 && staff(60)[21] > 50 && rec[7] == 0);
}

/* Player-only fetch: a human hospital with a usable store (clerk byte 11, not on screen 0x41) takes the wanted kind
 * from the store; W stays set until the next check. AI hospitals do not fetch here. The reception's player-only
 * equipment step installs the pen (kind 3) and the form (kind 11). */
static void test_player_fetch(void) {
    for (int c = 0; c < 4; c++) {
        fresh();
        int h = c == 3 ? 5 : 0;
        if (c != 3) world_hospital(&g_world, h)[0x14] = 1;
        uint8_t *st = room(h, 4, 0), *rec = room(h, 5, 0);
        st[7] = 40, st[8] = 1;
        put16(st + STORE_FURNITURE, 1), st[STORE_FURNITURE_COND] = 66;
        hire(h, 0, 70, 3, 4, 50);
        put16(st + STORE_CLERK, 70);
        staff(70)[11] = c == 1 ? 0 : 1;
        g_sim.screen = c == 2 ? 0x41 : 0;
        put_furn(rec, 0, 2, 100);
        run(0x32F6C);
        int fetched = furn_id(rec, 1) == 1 && rec[FCOND + 1] == 66 && get16(st + STORE_FURNITURE) == 0;
        CHECK(rec[4] == 1);
        CHECK(fetched == (c == 0));
        if (c == 0) {
            run(0x32F6C);
            CHECK(rec[4] == 0);
        }
    }
    fresh(); /* the waiting room has no fetch: a human room keeps its chair wish and the store its chair */
    world_hospital(&g_world, 0)[0x14] = 1;
    uint8_t *wst = room(0, 4, 0), *wr = room(0, 3, 0);
    wst[7] = 40, wst[8] = 1, put16(wst + STORE_FURNITURE, 2);
    hire(0, 0, 70, 3, 4, 50), put16(wst + STORE_CLERK, 70), staff(70)[11] = 1;
    wr[6] = 5;
    run(0x33276);
    CHECK(get16(wst + STORE_FURNITURE) == 2 && furn_id(wr, 0) == 0 && wr[6] == 5);
    for (int human = 0; human <= 1; human++) {
        fresh();
        if (human) world_hospital(&g_world, 0)[0x14] = 1;
        int h = human ? 0 : 5;
        uint8_t *st = room(h, 4, 0), *r = room(h, 2, 0);
        st[7] = 40, st[9] = 2;
        equip(8)[6] = 3, equip(9)[6] = 11;
        put16(st + STORE_EQUIPMENT, 9), put16(st + STORE_EQUIPMENT + 2, 8);
        hire(h, 0, 70, 3, 4, 50);
        put16(st + STORE_CLERK, 70), staff(70)[11] = 1;
        put_furn(r, 0, 1, 100), put_furn(r, 1, 2, 100);
        run(0x35A3E);
        CHECK(human ? get16(r + EQ) == 8 && get16(r + EQ + 2) == 9 && st[9] == 0 : get16(r + EQ) == 0 && st[9] == 2);
    }
    fresh();
    world_hospital(&g_world, 0)[0x14] = 1;
    uint8_t *st = room(0, 4, 0), *r = room(0, 2, 0);
    st[7] = 40, st[9] = 2;
    equip(8)[6] = 3, equip(9)[6] = 11;
    put16(st + STORE_EQUIPMENT, 9), put16(st + STORE_EQUIPMENT + 2, 8);
    hire(0, 0, 70, 3, 4, 50);
    put16(st + STORE_CLERK, 70), staff(70)[11] = 1;
    put_furn(r, 0, 1, 100), put_furn(r, 1, 2, 100);
    run(0x35A3E);
    CHECK(get16(r + EQ) == 8 && get16(r + EQ + 2) == 9 && st[9] == 0);
    for (int i = 0; i < SLOTS; i++) put16(r + EQ + 2 * i, 20); /* a full list ends the step: nothing taken */
    put16(st + STORE_EQUIPMENT, 8), st[9] = 1;
    run(0x35A3E);
    CHECK(get16(st + STORE_EQUIPMENT) == 8 && st[9] == 1);
}

/* Section 2: pathology (r3 = 1 with a case on the table), golf attendant (r7 = 3; leaves below 4 only without a
 * golfer), club staff (r7 = 3, +2 then +4); at 100 nothing; the fittings of pathology and golf room. */
static void test_rise_only(void) {
    fresh();
    uint8_t *p = room(9, 18, 0), *g = room(9, 12, 0), *c = room(9, 14, 0);
    hire(9, 0, 80, 7, 18, 50), hire(9, 1, 81, 1, 0x0C, 50), hire(9, 2, 82, 1, 0x0E, 50), hire(9, 3, 83, 1, 0x0E, 50);
    put16(p + 0x0A, 80), put16(g + 2, 81), put16(c + 2, 82), put16(c + 4, 83);
    for (int i = 0; i < 400; i++) run(0x53F31);
    CHECK(staff(80)[21] == 50); /* no case */
    put16(p + 0x0C, 5);
    int up[4] = {0};
    for (int i = 0; i < 800; i++) {
        staff(80)[21] = staff(81)[21] = staff(82)[21] = staff(83)[21] = 50;
        run(0x53F31), run(0x54BCE), run(0x54DF5);
        for (int k = 0; k < 4; k++) up[k] += staff(80 + k)[21] == 51;
    }
    CHECK(up[0] >= 160 && up[0] <= 240); /* 1/4 */
    for (int k = 1; k < 4; k++) CHECK(up[k] >= 70 && up[k] <= 135); /* 1/8 */
    staff(80)[21] = staff(81)[21] = 100;
    uint64_t rng = g_sim.rng;
    uint32_t rng_s = g_sim.rng_s;
    put16(c + 2, 0), put16(c + 4, 0);
    run(0x53F31), run(0x54BCE);
    CHECK(g_sim.rng == rng && g_sim.rng_s == rng_s && staff(80)[21] == 100); /* at 100: no draw */
    staff(80)[21] = staff(81)[21] = 2, g[5] = 1; /* 2: a rise still leaves it below 4 */
    run(0x53F31), run(0x54BCE);
    CHECK(get16(p + 0x0A) == 0 && staff(80)[4] == 0 && get16(g + 2) == 81);
    g[5] = 0, staff(81)[21] = 2;
    run(0x54BCE);
    CHECK(get16(g + 2) == 0 && staff(81)[4] == 0 && slot(9, 1)[3] == 0);
    int stayed = 0; /* mood 3: out unless the r7 = 3 draw lifted it to 4 */
    for (int trial = 0; trial < 64; trial++) {
        put16(c + 4, 83), staff(83)[21] = 3, put16(c + 2, 0);
        run(0x54DF5);
        stayed += get16(c + 4) && staff(83)[21] == 3;
    }
    CHECK(stayed == 0);
    put16(c + 2, 82), put16(c + 4, 83), staff(82)[21] = 50, staff(83)[21] = 2;
    run(0x54DF5);
    CHECK(get16(c + 2) == 82 && get16(c + 4) == 0 && staff(83)[4] == 0);
    /* fittings: an AI hospital with a store; capabilities 0x36, 0x37 and the carpet */
    fresh();
    ai(9);
    uint8_t *st = room(9, 4, 0);
    g = room(9, 12, 0), p = room(9, 18, 0);
    equip(10)[10] = 0x36, equip(11)[10] = 0x37, equip(12)[10] = 9, equip(13)[10] = 0x33;
    st[7] = 40, st[8] = 1, st[9] = 4;
    put16(st + STORE_EQUIPMENT, 10), put16(st + STORE_EQUIPMENT + 2, 11), put16(st + STORE_EQUIPMENT + 4, 13);
    put16(st + STORE_EQUIPMENT + 6, 12);
    put16(st + STORE_FURNITURE, 6);
    run(0x54BCE);
    CHECK(get16(g + EQ) == 10 && get16(g + EQ + 2) == 11 && furn_id(g, 0) == 6);
    run(0x53F31);
    CHECK(get16(p + EQ) == 12 && get16(p + EQ + 2) == 13 && world_hospital(&g_world, 9)[0xDA] == 0x3C); /* 9 first */
    st[8] = 1, put16(st + STORE_FURNITURE, 6); /* a carpet in the room: none taken */
    run(0x54BCE);
    CHECK(get16(st + STORE_FURNITURE) == 6 && furn_id(g, 1) == 0);
    /* a human hospital whose store is not usable (clerk byte 11 clear) gets no fittings */
    fresh();
    world_hospital(&g_world, 0)[0x14] = 1;
    st = room(0, 4, 0), p = room(0, 18, 0);
    equip(12)[10] = 9;
    st[7] = 40, st[9] = 1, put16(st + STORE_EQUIPMENT, 12);
    hire(0, 0, 70, 3, 4, 50), put16(st + STORE_CLERK, 70);
    run(0x53F31);
    CHECK(get16(p + EQ) == 0 && st[9] == 1);
    staff(70)[11] = 1;
    run(0x53F31);
    CHECK(get16(p + EQ) == 12 && st[9] == 0);
}

/* Section 3: the comfort counters fall to 0 in an AI hospital and to 1 in a human one; the dental ward's W = 1 goes. */
static void test_countdowns(void) {
    static const struct { uint32_t r; int cat, off; } C[4] = {{0x4A23A, 3, 0x0F}, {0x4C008, 6, 0x0F}, {0x4C079, 16, 0x0F},
                                                              {0x4C0EA, 11, 0x19}};
    for (int k = 0; k < 4; k++) {
        fresh();
        world_hospital(&g_world, 0)[0x14] = 1;
        uint8_t *a = room(2, C[k].cat, 1), *b = room(0, C[k].cat, 0), *z = room(2, C[k].cat, 0);
        a[C[k].off] = 5, b[C[k].off] = 5, z[C[k].off] = 0, a[4] = 1, b[4] = 5;
        run(C[k].r);
        CHECK(a[C[k].off] == 4 && z[C[k].off] == 0);
        for (int i = 0; i < 9; i++) run(C[k].r);
        CHECK(a[C[k].off] == 0 && b[C[k].off] == 1);
        CHECK(a[4] == (C[k].cat == 11 ? 0 : 1) && b[4] == 5);
    }
}

/* Section 4: the documented examples (docs/sim-bookkeeping.md, the oracle's planted places). */
static void place(uint8_t *rec, int i, int person, int sex, int counter) {
    put16(rec + 0x14 + 4 * i, person), put16(rec + 0x16 + 4 * i, counter);
    g_world.block[BLK_PATIENTS][person * 106] = (uint8_t)sex;
}
static void test_resorts(void) {
    static const uint8_t WAITING[10] = {0, 0, 0, 0, 1, 1, 2, 0, 1, 0}, WARDS[10] = {2, 2, 2, 2, 1, 2, 2, 0, 1, 0};
    for (int h = 0; h <= 2; h++)
        for (int screen = 0; screen <= 1; screen++) {
            fresh();
            memcpy(g_image + 0x486F, WAITING, 10), memcpy(g_image + 0x4DCC, WARDS, 10);
            g_sim.screen = screen ? 0x3B : 0;
            uint8_t *w = room(h, 3, 0);
            static const int SEX[10] = {1, 1, 1, 0, 0, 0, -1, 1, 1, 1};
            for (int i = 0; i < 10; i++)
                if (SEX[i] >= 0) place(w, i, 100 + i, SEX[i], 1000 + i);
            run(0x30C55);
            static const int WANT[10] = {104, 105, 102, 103, 100, 101, 0, 107, 108, 109};
            int ok = 1, same = 1;
            for (int i = 0; i < 10; i++) {
                int p = get16(w + 0x14 + 4 * i);
                ok &= p == WANT[i] && (!p || get16(w + 0x16 + 4 * i) == 1000 + p - 100);
                same &= p == (SEX[i] >= 0 ? 100 + i : 0);
            }
            CHECK(h < 2 && !screen ? ok : same);
        }
    for (int cat = 6; cat <= 16; cat += 10) {
        fresh();
        memcpy(g_image + 0x4DCC, WARDS, 10);
        uint8_t *w = room(cat == 6 ? 0 : 1, cat, 0);
        for (int i = 0; i < 10; i++) place(w, i, 200 + i, i >= 6, i);
        run(cat == 6 ? 0x3789E : 0x37A2E);
        for (int i = 0; i < 10; i++) CHECK(get16(w + 0x14 + 4 * i) == (i == 4 ? 207 : i == 7 ? 204 : 200 + i));
        for (int i = 0; i < 10; i++) place(w, i, 200 + i, i >= 6, i);
        g_sim.screen = cat == 6 ? 0x26 : 0x2B;
        run(cat == 6 ? 0x3789E : 0x37A2E);
        CHECK(get16(w + 0x14 + 16) == 204 && get16(w + 0x14 + 28) == 207); /* the player looks: nothing moves */
    }
}

/* Section 5, 0x307BF, one employed record against a twin generator that draws in the documented order. */
static int predict(Sim *t, int state, int w, int d, int mood, int hour) {
    if (state == 0x17) {
        if (hour < 10) return 100;
        return sim_bits(t, 7) == 0 && mood >= 4 ? mood - 1 : mood;
    }
    if (state >= 0x13 && state <= 0x19) return mood;
    if ((int8_t)mood < 0) mood -= 256;
    int up = 0, down = 0;
    if (state == 0) {
        if (w < d) down = sim_bits(t, 15) == 1;
        else if (w == d) up = sim_bits(t, 31) == 1;
        else up = sim_bits(t, 3) == 1;
    } else {
        int first = sim_bits(t, 3) == 0;
        if (first) mood = mood - 1 < 1 ? 1 : mood - 1;
        if (w < d) down = 1;
        else if (w > d) up = w - d >= (int)sim_bits(t, 7);
    }
    if (down) mood = mood - 1 < 1 ? 1 : mood - 1;
    if (up) mood = (int8_t)(mood + 1) >= 100 ? 100 : (uint8_t)(mood + 1); /* signed: 127 + 1 stays 128 */
    return (uint8_t)mood;
}
static void test_staff_mood(void) {
    static const int STATES[] = {0, 5, 0x0C, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19};
    static const int WD[3][2] = {{8, 10}, {10, 10}, {14, 10}};
    static const int MOODS[] = {0, 1, 2, 50, 99, 101, 127};
    for (size_t s = 0; s < sizeof STATES / sizeof *STATES; s++)
        for (int k = 0; k < 3; k++)
            for (size_t m = 0; m < sizeof MOODS / sizeof *MOODS; m++)
                for (int hour = 9; hour <= 12; hour += hour == 10 ? 2 : 1) {
                    fresh();
                    world_set_tail(&g_world, TAIL_HOUR, hour);
                    uint8_t *st = staff(300);
                    st[3] = 6, st[4] = (uint8_t)STATES[s], st[19] = (uint8_t)WD[k][0], st[18] = (uint8_t)WD[k][1];
                    staff(301)[4] = 5, staff(301)[21] = 50; /* not employed: untouched */
                    int mood = MOODS[m];
                    Sim twin = g_sim;
                    for (int i = 0; i < 200; i++) {
                        st[21] = (uint8_t)mood;
                        run(0x307BF);
                        mood = predict(&twin, STATES[s], WD[k][0], WD[k][1], mood, hour);
                        if (st[21] != mood) break;
                    }
                    CHECK(st[21] == mood && g_sim.rng == twin.rng && g_sim.rng_s == twin.rng_s && staff(301)[21] == 50);
                }
    /* a negative wage byte is signed: -1 < 10 */
    fresh();
    staff(300)[3] = 6, staff(300)[4] = 5, staff(300)[19] = 0xFF, staff(300)[18] = 10, staff(300)[21] = 50;
    run(0x307BF);
    CHECK(staff(300)[21] <= 49);
}

/* Section 6: the thug dispatch 0x48215 */
static void thug_world(int hour) {
    fresh();
    ai(10);
    world_set_cash(&g_world, 10, 100);
    world_set_tail(&g_world, TAIL_HOUR, hour);
    room(10, 5, 0);
    uint8_t *r = room(10, 2, 0);
    put16(r + 8, 90);
    hire(10, 0, 91, 1, 2, 90);  /* not a thug */
    hire(10, 1, 92, 8, 0x14, 90); /* on a mission: skipped */
    hire(10, 2, 93, 8, 0x16, 50); /* below 70: off duty */
    hire(10, 3, 94, 8, 0, 70);    /* sent (70 is enough) */
    hire(10, 4, 95, 8, 0, 80);    /* not reached */
    slot(10, 2)[7] = 4, put16(slot(10, 2) + 0x0A, 0x0909), slot(10, 2)[4] = 3, staff(93)[26] = 3;
}
static void test_thug_dispatch(void) {
    thug_world(2);
    run(0x48215);
    CHECK(slot(10, 2)[3] == 0x13 && staff(93)[4] == 0x13 && slot(10, 2)[7] == 0 && get16(slot(10, 2) + 0x0A) == 0 &&
          slot(10, 2)[4] == 0 && staff(93)[26] == 0);
    CHECK(slot(10, 3)[3] == 0x15 && staff(94)[4] == 0x15 && slot(10, 4)[3] == 0 && slot(10, 1)[3] == 0x14);
    thug_world(2); /* an off-duty thug of mood 90 first: skipped, not sent */
    hire(10, 0, 96, 8, 0x13, 90);
    run(0x48215);
    CHECK(slot(10, 0)[3] == 0x13 && slot(10, 3)[3] == 0x15);
    thug_world(12);
    run(0x48215);
    CHECK(slot(10, 3)[3] == 0x14 && slot(10, 4)[3] == 0);
    for (int c = 0; c < 5; c++) {
        thug_world(12);
        uint8_t *r = sim_room_record(&g_sim, 10, 2, 0);
        if (c == 0) put16(r + 8, 0);
        if (c == 1) put16(r + 0x0A, 7);
        if (c == 2) world_set_cash(&g_world, 10, -1);
        if (c == 3) g_world.block[BLK_ROOM_COUNTS][10 * 100 + 5] = 0;
        if (c == 4) { /* waiting patients >= rooms + 1 */
            uint8_t *w = room(10, 3, 0);
            for (int i = 0; i < 2; i++) put16(w + 0x14 + 4 * i, 400 + i), g_world.block[BLK_PATIENTS][(400 + i) * 106 + 57] = 1;
        }
        run(0x48215);
        CHECK(slot(10, 3)[3] == 0 && slot(10, 2)[3] == 0x16);
    }
    thug_world(12); /* one waiting patient with one room: still below n + 1 */
    uint8_t *w = room(10, 3, 0);
    put16(w + 0x14, 400), g_world.block[BLK_PATIENTS][400 * 106 + 57] = 1;
    run(0x48215);
    CHECK(slot(10, 3)[3] == 0x14);
    thug_world(19); /* outside hours 4-18 the reception is not asked */
    put16(sim_room_record(&g_sim, 10, 2, 0) + 8, 0);
    run(0x48215);
    CHECK(slot(10, 3)[3] == 0x15);
}

/* Golf and club operators */
static void test_golf_club_operators(void) {
    for (int c = 0; c < 6; c++) {
        fresh();
        ai(11);
        world_set_cash(&g_world, 11, c == 2 ? -1 : 10);
        static const int HOUR[6] = {12, 1, 12, 1, 21, 22};
        world_set_tail(&g_world, TAIL_HOUR, HOUR[c]);
        if (c == 3) world_set_tail(&g_world, TAIL_HOURS, 4);
        uint8_t *g = room(11, 12, 0);
        hire(11, 0, 100, 1, 0, 50);
        run(0x51BD4);
        int got = get16(g + 2) == 100 && slot(11, 0)[3] == 0x0C && staff(100)[4] == 0x0C;
        CHECK(got == (c == 0 || c == 3 || c == 4));
        if (c == 4) {
            run(0x51BD4);
            CHECK(get16(g + 2) == 100); /* hour 21: kept */
        }
        if (c == 5) {
            put16(g + 2, 100), slot(11, 0)[3] = staff(100)[4] = 0x0C;
            run(0x51BD4);
            CHECK(get16(g + 2) == 0 && slot(11, 0)[3] == 0x13); /* the evening release 0x22EDD: off duty */
        }
    }
    for (int c = 0; c < 5; c++) {
        fresh();
        ai(12);
        world_set_cash(&g_world, 12, c == 3 ? -1 : 10);
        static const int HOUR[5] = {10, 15, 16, 12, 20};
        world_set_tail(&g_world, TAIL_HOUR, HOUR[c]);
        uint8_t *cl = room(12, 14, 0);
        cl[0] = 120; /* above any threshold: no fittings */
        hire(12, 0, 110, 1, 0, 50), hire(12, 1, 111, 1, 0, 50);
        if (c == 4) {
            put16(cl + 2, 110), put16(cl + 4, 111), slot(12, 0)[3] = slot(12, 1)[3] = 0x0E;
            run(0x533CD);
            CHECK(!get16(cl + 2) && !get16(cl + 4) && slot(12, 0)[3] == 0x13 && slot(12, 1)[3] == 0x13);
            put16(cl + 2, 110), put16(cl + 4, 0), slot(12, 0)[3] = 0x0E, slot(12, 1)[3] = 0;
            run(0x533CD);
            CHECK(get16(cl + 2) == 110 && !get16(cl + 4)); /* only +2: not released, nobody asked at 20 */
            continue;
        }
        run(0x533CD);
        int first = get16(cl + 2) && !get16(cl + 4);
        run(0x533CD);
        int both = get16(cl + 2) && get16(cl + 4) && slot(12, 0)[3] == 0x0E && slot(12, 1)[3] == 0x0E;
        CHECK(first == (c < 2) && both == (c < 2));
        if (c < 2) {
            world_set_cash(&g_world, 12, -1);
            run(0x533CD);
            CHECK(!get16(cl + 2) && !get16(cl + 4)); /* negative cash releases */
        }
    }
    /* club fittings: rating below the threshold, a store: kind 2 or 6 by r1 */
    int kinds[2] = {0};
    for (int t = 0; t < 40; t++) {
        fresh();
        ai(12);
        world_set_cash(&g_world, 12, 10);
        uint8_t *st = room(12, 4, 0), *cl = room(12, 14, 0);
        st[7] = 40, st[8] = 2;
        put16(st + STORE_FURNITURE, 4), put16(st + STORE_FURNITURE + 2, 5);
        sim_seed(&g_sim, 1000u + (uint64_t)t);
        run(0x533CD);
        int id = furn_id(cl, 0);
        CHECK(id == 4 || id == 5);
        if (id == 4 || id == 5) kinds[id - 4]++;
    }
    CHECK(kinds[0] > 5 && kinds[1] > 5);
    /* r1 = 0 with no decoration in the store also looks for kind 6 in the same call (h+0xDC = 2); r1 = 1 looks for kind 6
     * only (h+0xDC = 6 when the store has none) */
    for (int only = 4; only <= 5; only++) {
        int asked = 0, quiet = 0;
        for (int t = 0; t < 40; t++) {
            fresh();
            ai(12);
            world_set_cash(&g_world, 12, 10);
            uint8_t *st = room(12, 4, 0), *cl = room(12, 14, 0), *r = world_hospital(&g_world, 12);
            st[7] = 40, st[8] = 1;
            put16(st + STORE_FURNITURE, only);
            sim_seed(&g_sim, 2000u + (uint64_t)t);
            run(0x533CD);
            int id = furn_id(cl, 0);
            if (only == 5) { /* books only: always installed, the request shows which draw it was */
                CHECK(id == 5 && (r[0xDC] == 0 || r[0xDC] == 2));
                asked += r[0xDC] == 2, quiet += r[0xDC] == 0;
            } else { /* decoration only: r1 = 0 installs it, r1 = 1 asks for kind 6 */
                CHECK((id == 4 && r[0xDC] == 0) || (id == 0 && r[0xDC] == 6));
                asked += r[0xDC] == 6, quiet += id == 4;
            }
        }
        CHECK(asked > 5 && quiet > 5);
    }
    fresh(); /* rated 120: above the threshold, nothing is fetched */
    ai(12);
    world_set_cash(&g_world, 12, 10);
    uint8_t *st = room(12, 4, 0), *cl = room(12, 14, 0);
    st[7] = 40, st[8] = 1, put16(st + STORE_FURNITURE, 4);
    put_furn(cl, 0, 7, 100);
    cl[0] = 120;
    run(0x533CD);
    CHECK(furn_id(cl, 1) == 0 && get16(st + STORE_FURNITURE) == 4);
}

/* 0x2301C against 0x22EDD: the store clerk 0x22F65 now leaves idle */
static void test_clerk_leaves_idle(void) {
    fresh();
    uint8_t *st = room(13, 4, 0);
    hire(13, 0, 120, 3, 4, 2);
    put16(st + STORE_CLERK, 120), slot(13, 0)[10] = 4;
    for (int i = 0; i < 100 && get16(st + STORE_CLERK); i++) run(0x22F65);
    CHECK(get16(st + STORE_CLERK) == 0 && slot(13, 0)[3] == 0 && staff(120)[4] == 0 && slot(13, 0)[10] == 4);
}

/* The waiting-room operator sets the comfort counter (r15 + 22) with r7 = 0 when 0 and the room is staffed. */
static void test_waiting_comfort_set(void) {
    for (int staffed = 0; staffed <= 1; staffed++) {
        fresh();
        g_sim.rng_scratch = 0x1F21A6; /* [0x4528] as the staff pass leaves it (docs/rng-sites.md 2.4) */
        ai(14);
        world_set_tail(&g_world, TAIL_HOUR, 22);
        uint8_t *w = room(14, 3, 0);
        hire(14, 0, 130, 1, 3, 50);
        put16(room(14, 2, 0) + 8, 131); /* a staffed reception: no evening release of the empty room at 22:00 */
        /* the r7's rate is the generator's with the operator's inputs (docs/rng-sites.md 3.2: the value 0 comes up
         * at 0.29 of uniform in the original, and in this one-hospital world far less), so the rule is checked against
         * the logged draws: one r7 (0x2CB06) per staffed call, none unstaffed, and +0x0F = the helper's r15 (0x2B4AE)
         * + 22 exactly after every r7 = 0, otherwise unset */
        FILE *log = tmpfile();
        CHECK(log != NULL);
        if (!log) return;
        int sets = 0, bad = 0, draws = 0, zeros = 0;
        for (int i = 0; i < 800; i++) {
            w[0x0F] = 0;
            put16(w + 8, staffed ? 130 : 0);
            rewind(log);
            g_sim.rng_log = log;
            sim_staff_run(&g_sim, 0x2C993);
            g_sim.rng_log = NULL;
            fflush(log);
            long end = ftell(log);
            rewind(log);
            unsigned site, n, v, in_d, in_e;
            char kind;
            int want = 0;
            while (ftell(log) < end && fscanf(log, "%X %c%u %u %X %X", &site, &kind, &n, &v, &in_d, &in_e) == 6) {
                if (site == 0x2CB06) draws++, zeros += v == 0;
                if (site == 0x2B4AE) want = (int)v + 22;
            }
            sets += w[0x0F] != 0;
            bad += w[0x0F] != want;
        }
        fclose(log);
        if (bad || draws != (staffed ? 800 : 0) || sets != zeros)
            fprintf(stderr, "  waiting comfort staffed %d: %d sets, %d draws, %d zeros, %d bad\n", staffed, sets, draws, zeros, bad);
        CHECK(bad == 0 && draws == (staffed ? 800 : 0) && sets == zeros && (staffed ? sets > 0 : sets == 0));
    }
}

/* Section 7, and through the clock: every minute sets +0x2E in both player blocks */
static void test_flags(void) {
    fresh();
    int n56 = 0;
    for (int i = 0; i < 800; i++) {
        g_world.block[BLK_PLAYER0][0x2E] = g_world.block[BLK_PLAYER1][0x2E] = 0;
        g_world.block[BLK_PLAYER0][0x56] = g_world.block[BLK_PLAYER1][0x56] = 0;
        run(0x1C8A4);
        CHECK(g_world.block[BLK_PLAYER0][0x2E] == 0xFF && g_world.block[BLK_PLAYER1][0x2E] == 0xFF);
        n56 += g_world.block[BLK_PLAYER0][0x56] == 0xFF;
        n56 += g_world.block[BLK_PLAYER1][0x56] == 0xFF;
    }
    CHECK(n56 >= 330 && n56 <= 470); /* 1/4 of 1,600 */
    run(0x3DC86);
    CHECK(g_world.block[BLK_PLAYER0][0x33] == 0xFF && g_world.block[BLK_PLAYER1][0x33] == 0xFF);
    g_world.block[BLK_PLAYER0][0x2E] = 0;
    for (int i = 0; i < SIM_TICKS_PER_MINUTE; i++) sim_tick(&g_sim);
    CHECK(g_world.block[BLK_PLAYER0][0x2E] == 0xFF);
    /* every routine of this file is implemented */
    static const uint32_t ALL[] = {0x48215, 0x307BF, 0x35A3E, 0x32F6C, 0x33276, 0x333FB, 0x37583, 0x53845, 0x53ACD,
                                   0x5456F, 0x548EB, 0x4A23A, 0x4C008, 0x4C079, 0x4C0EA, 0x533CD, 0x54DF5, 0x51BD4,
                                   0x54BCE, 0x53F31, 0x30C55, 0x3789E, 0x37A2E, 0x1C8A4, 0x3DC86};
    for (size_t i = 0; i < sizeof ALL / sizeof *ALL; i++) CHECK(run(ALL[i]));
    CHECK(!run(0x4CA3B)); /* copy protection: never */
}

/* FIX-28 (docs/fixes.md): a clerk at mood 100 in an AI waiting room rated 120 with W = 0, 640 calls: off above 120
 * (the doc's oracle: 140), on 100 */
static void test_fix28_waiting_room_mood(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 27 : 0);
        fresh();
        uint8_t *rec = room(8, 3, 0);
        rec[10] = 10, rec[0] = 120, rec[0x15] = 3;
        hire(8, 0, 60, 1, 3, 100);
        put16(rec + 8, 60);
        for (int i = 0; i < 640; i++) run(0x33276);
        CHECK(on ? staff(60)[21] == 100 : (staff(60)[21] > 120 && staff(60)[21] <= 160));
    }
    fixes_set(0);
}

int main(void) {
    test_fix28_waiting_room_mood();
    test_use_furniture();
    test_wanted_rule();
    test_room_mood();
    test_wear_by_room();
    test_use_rooms();
    test_kitchen_and_op();
    test_sim29_first_field_gate();
    test_waiting_room();
    test_player_fetch();
    test_rise_only();
    test_countdowns();
    test_resorts();
    test_staff_mood();
    test_thug_dispatch();
    test_golf_club_operators();
    test_clerk_leaves_idle();
    test_waiting_comfort_set();
    test_flags();
    world_free(&g_world);
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] bookkeeping\n");
    return 0;
}
