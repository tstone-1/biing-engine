/* The scheduled newspaper generators of docs/news-generators.md (sim_news_gen.c). Synthetic world and program image:
 * the room descriptions and catalog entries are planted by the tests. */
#include <stdio.h>
#include <setjmp.h>
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

enum { FURN = 0x20, FURN_SLOTS = 20, EQ = 0x60, EQ_SLOTS = 30 };

static void fresh_hour(int hour) {
    world_free(&g_world);
    world_alloc(&g_world);
    memset(g_image, 0, sizeof g_image);
    g_prog.image.data = g_image;
    g_prog.image.size = sizeof g_image;
    world_set_tail(&g_world, TAIL_DAY, 1);
    world_set_tail(&g_world, TAIL_MONTH, 1);
    world_set_tail(&g_world, TAIL_HOUR, hour);
    for (int cat = 0; cat < 19; cat++) { /* one synthetic description for every category */
        uint8_t *d = g_image + 0x4758 + 14 * cat;
        put16(d, FURN), put16(d + 2, FURN_SLOTS), put16(d + 4, EQ), put16(d + 6, EQ_SLOTS);
    }
    sim_init(&g_sim, &g_world, &g_prog, 23);
}
static void fresh(void) { fresh_hour(10); }

static uint8_t *item(int i) { return sim_news_item(&g_sim, i); }
static uint8_t *hosp(int h) { return world_hospital(&g_world, h); }
static uint8_t *counts(int h) { return g_world.block[BLK_ROOM_COUNTS] + h * 100; }
static void furniture(int id, int q, int kind) { g_image[0xEE0 + 18 * id + 4] = (uint8_t)(int8_t)q, g_image[0xEE0 + 18 * id + 6] = (uint8_t)kind; }
static void equipment(int id, int q, int kind) { g_image[0x144A + 20 * id + 4] = (uint8_t)(int8_t)q, g_image[0x144A + 20 * id + 6] = (uint8_t)kind; }
static void plant_item(int i, int cls, int type) {
    uint8_t *it = item(i);
    memset(it, 0, 70);
    it[0] = (uint8_t)cls, it[20] = (uint8_t)type;
}
static void fill(int n) { /* n items in use in slots 99, 98, ... */
    for (int i = 0; i < n; i++) plant_item(99 - i, 2, 0x3F);
}
static int count_type(int type) {
    int n = 0;
    for (int i = 0; i < 100; i++) n += item(i)[0] && item(i)[20] == type;
    return n;
}
static uint8_t *find(int type) {
    for (int i = 0; i < 100; i++)
        if (item(i)[0] && item(i)[20] == type) return item(i);
    return NULL;
}

/* a room of category cat, instance i of hospital h, staffed by nurse (and doctor), ratings r0 / r1, lists given */
static uint8_t *room(int h, int cat, int i, int nurse, int doctor, int r0, int r1) {
    if (counts(h)[cat] < i + 1) counts(h)[cat] = (uint8_t)(i + 1);
    uint8_t *rec = sim_room_record(&g_sim, h, cat, i);
    put16(rec + 8, nurse), put16(rec + 0x0A, doctor);
    rec[0] = (uint8_t)r0, rec[1] = (uint8_t)r1;
    return rec;
}
static void list(uint8_t *rec, int equip, const int *ids, int n) {
    for (int k = 0; k < n; k++) put16(rec + (equip ? EQ : FURN) + 2 * k, ids[k]);
}
#define S_FILLED 51 /* the most fill() is asked for */
#define LIST(rec, eq, ...) list(rec, eq, (const int[]){__VA_ARGS__}, (int)(sizeof((int[]){__VA_ARGS__}) / sizeof(int)))

/* ---- short items -------------------------------------------------------------------------------------------- */

static void test_short_items(void) {
    static const struct { uint32_t r; int head, body, limit, pair; } S[5] = {
        {0x476E3, 1, 2, 30, 1}, {0x47766, 9, 0x0A, 30, 1}, {0x42D30, 0x0C, 0x0D, 30, 1}, {0x42CB6, 0x20, 0, 50, 0},
        {0x42CF3, 0x1E, 0, 50, 0}};
    for (int k = 0; k < 5; k++) {
        int heads = 0, n = 4000, maxp = 0, max22 = 0;
        fresh();
        for (int i = 0; i < n; i++) {
            memset(g_world.block[BLK_NEWS], 0, 7000);
            memset(item(0), 0xAB, 70), item(0)[0] = 0; /* a previous item's bytes in a free slot */
            Sim twin = g_sim;
            int head = !S[k].pair || sim_bits(&twin, 3) == 1;
            int p = !S[k].pair ? (int)sim_bits(&twin, 7) : head && S[k].head == 1 ? (int)sim_bits(&twin, 7) : (int)sim_bits(&twin, 3);
            CHECK(sim_call(&g_sim, S[k].r));
            const uint8_t *it = item(0);
            CHECK(sim_news_count(&g_sim) == 1);
            CHECK(it[0] == (head ? 1 : 2) && it[20] == (head ? S[k].head : S[k].body) && it[21] == p);
            CHECK(it[4] == 0 && it[5] == 0 && it[6] == 0 && it[8] == 0 && it[28] == (S[k].pair ? 1 : 0));
            for (int b = 1; b < 20; b++) CHECK(b == 8 || it[b] == 0); /* bytes 1-19 cleared */
            CHECK(it[30] == 0xAB && it[69] == 0xAB && it[27] == 0xAB); /* bytes 20-69 kept where not written */
            if (head && S[k].head == 1) {
                CHECK(it[22] <= 3 && it[23] <= 3);
                max22 = it[22] > max22 ? it[22] : max22;
            } else {
                CHECK(it[22] == 0xAB);
            }
            heads += head, maxp = p > maxp ? p : maxp;
        }
        if (S[k].pair) CHECK(heads > n / 4 - 150 && heads < n / 4 + 150);
        CHECK(maxp == (!S[k].pair || S[k].head == 1 ? 7 : 3));
        if (S[k].head == 1) CHECK(max22 == 3);
        /* the gate: with L items in use, one call in 50 at most; with L + 1, none */
        fresh();
        fill(S[k].limit);
        int made = 0;
        for (int i = 0; i < 5000; i++) {
            Sim twin = g_sim;
            int pass = sim_random_in(&twin, 50, 0, rng_scheduler(&twin)) == 2; /* the gate's inputs (context S) */
            sim_call(&g_sim, S[k].r);
            int now = sim_news_count(&g_sim) - S[k].limit;
            CHECK(now == pass);
            made += now;
            for (int j = 0; j < 100 - S_FILLED; j++) item(j)[0] = 0;
        }
        CHECK(made > 50 && made < 150);
        fresh();
        fill(S[k].limit + 1);
        for (int i = 0; i < 3000; i++) sim_call(&g_sim, S[k].r);
        CHECK(sim_news_count(&g_sim) == S[k].limit + 1);
    }
}

/* ---- city statistics ---------------------------------------------------------------------------------------- */

static void draw_k(int k) { /* move the stream until 0x477D9's random(7) gives k (fewer than 5 items: no gate draw) */
    for (;;) {
        Sim twin = g_sim;
        if ((int)sim_random(&twin, 7) == k) return;
        sim_bits(&g_sim, 1);
    }
}
static uint8_t *person(int i) { return g_world.block[BLK_PATIENTS] + i * 106; }

static void test_city_statistics(void) {
    /* case 0 / 1: the opinion sums are recomputed first (0x1A684), item or not */
    for (int k = 0; k < 2; k++)
        for (int billed = 0; billed < 2; billed++) {
            fresh();
            for (int h = 0; h < 16; h++) hosp(h)[0x15] = h != 9, put32(hosp(h) + 0x44, 777777);
            for (int i = 1; i < 1009; i++) person(i)[20 + 3] = 2, person(i)[20 + 5] = 2, person(i)[20 + 7] = 1;
            put32(hosp(9) + H_CASH, 0);
            person(4)[20 + 12] = 200; /* hospital 12 = the highest of the others besides 3 and 5 ... */
            for (int i = 1; i < 1009; i++) person(i)[20 + 9] = 9; /* 9 is not in business */
            int need = k == 0 ? 150 : 100;
            put16(hosp(2) + 0x26, need - 1 - 40 + billed), put16(hosp(9) + 0x26, 500), put16(hosp(4) + 0x26, 40);
            draw_k(k);
            sim_call(&g_sim, 0x477D9);
            CHECK(get32(hosp(3) + 0x44) == 2016 && get32(hosp(0) + 0x44) == 0 && get32(hosp(12) + 0x44) == 200);
            CHECK(get32(hosp(9) + 0x44) == 9072);
            CHECK(sim_news_count(&g_sim) == billed);
            if (billed) {
                const uint8_t *it = item(0);
                /* highest: 3 and 5 tie at 2016, the first wins; lowest: hospitals 0-15 in business at 0, the last (15) */
                CHECK(it[0] == 1 && it[20] == 3 && it[21] == k && it[4] == 1 && it[5] == 0 && it[6] == 0 && it[28] == 0);
                CHECK(it[8] == (k == 0 ? 3 : 15));
            }
        }
    /* case 0 with every sum 0: byte 8 keeps its preset 0 */
    fresh();
    for (int h = 1; h < 16; h++) hosp(h)[0x15] = 1; /* hospital 0 closed, yet named */
    put16(hosp(6) + 0x26, 150);
    draw_k(0);
    sim_call(&g_sim, 0x477D9);
    CHECK(sim_news_count(&g_sim) == 1 && item(0)[8] == 0);
    /* cases 2 / 3: +0x2A, most (ties first, >= 7) and fewest (ties last, >= 2) */
    for (int k = 2; k < 4; k++)
        for (int lim = 0; lim < 2; lim++) {
            fresh();
            for (int h = 0; h < 16; h++) hosp(h)[0x15] = h != 1, put16(hosp(h) + 0x2A, 20);
            put16(hosp(1) + 0x2A, k == 2 ? 99 : 0); /* not in business */
            if (k == 2) {
                for (int h = 0; h < 16; h++) put16(hosp(h) + 0x2A, 3);
                put16(hosp(4) + 0x2A, 6 + lim), put16(hosp(8) + 0x2A, 6 + lim);
            } else {
                put16(hosp(4) + 0x2A, 1 + lim), put16(hosp(8) + 0x2A, 1 + lim);
            }
            draw_k(k);
            sim_call(&g_sim, 0x477D9);
            CHECK(sim_news_count(&g_sim) == lim);
            if (lim) {
                const uint8_t *it = item(0);
                CHECK(it[0] == 2 && it[21] == k && it[4] == 2 && (int8_t)it[5] == (k == 2 ? -1 : 1) && it[6] == 2);
                CHECK(it[8] == (k == 2 ? 4 : 8));
            }
        }
    /* cases 2 / 3 with fixed starts (docs/news-generators.md section 2): nobody in business gives no case-2 item but a
     * case-3 item for hospital 0; 4 and 9 at 3 -> 9 (ties last); 4 alone at 1 -> none */
    for (int c = 0; c < 4; c++) {
        fresh();
        if (c >= 2) hosp(4)[0x15] = 1, put16(hosp(4) + 0x2A, c == 2 ? 3 : 1);
        if (c == 2) hosp(9)[0x15] = 1, put16(hosp(9) + 0x2A, 3);
        put16(hosp(0) + 0x2A, 1); /* not in business */
        draw_k(c == 1 ? 2 : 3);
        sim_call(&g_sim, 0x477D9);
        CHECK(sim_news_count(&g_sim) == (c == 0 || c == 2));
        if (c == 0) CHECK(item(0)[21] == 3 && item(0)[8] == 0 && item(0)[4] == 2 && item(0)[5] == 1 && item(0)[6] == 2);
        if (c == 2) CHECK(item(0)[8] == 9);
    }
    /* case 4: the most thug deliveries (ties first) when the city's sum is >= 20 */
    for (int lim = 0; lim < 2; lim++) {
        fresh();
        for (int h = 0; h < 16; h++) hosp(h)[0x15] = h != 2;
        put16(hosp(2) + 0xE6, 50), put16(hosp(5) + 0xE6, 8), put16(hosp(11) + 0xE6, 8), put16(hosp(13) + 0xE6, 3 + lim);
        draw_k(4);
        sim_call(&g_sim, 0x477D9);
        CHECK(sim_news_count(&g_sim) == lim);
        if (lim) CHECK(item(0)[0] == 1 && item(0)[8] == 5 && item(0)[4] == 3 && (int8_t)item(0)[5] == -2 && item(0)[6] == 4);
    }
    /* cases 5 / 6: the record holders, records 20 / 130, holder not tested for being in business */
    for (int k = 5; k < 7; k++)
        for (int lim = 0; lim < 2; lim++) {
            fresh();
            int rec = (k == 5 ? 19 : 129) + lim;
            world_set_tail(&g_world, k == 5 ? 0x26 : 0x28, rec);
            world_set_tail(&g_world, k == 5 ? 0x2A : 0x2C, k == 5 ? 7 : 11);
            draw_k(k);
            sim_call(&g_sim, 0x477D9);
            CHECK(sim_news_count(&g_sim) == lim);
            if (lim) {
                const uint8_t *it = item(0);
                CHECK(it[0] == 1 && it[21] == k && it[8] == (k == 5 ? 7 : 11) && it[4] == 5 && (int8_t)it[5] == -1);
                CHECK(it[6] == 5 && get16(it + 22) == rec);
            }
        }
    /* the gate 120/30 */
    fresh();
    world_set_tail(&g_world, 0x26, 999), world_set_tail(&g_world, 0x28, 999);
    fill(31);
    for (int i = 0; i < 3000; i++) sim_call(&g_sim, 0x477D9);
    CHECK(sim_news_count(&g_sim) == 31);
    fresh();
    fill(30);
    int pass = 0, made = 0;
    for (int i = 0; i < 3000; i++) {
        Sim twin = g_sim;
        int p = sim_random_in(&twin, 120, 0, rng_scheduler(&twin)) == 2;
        int k = p ? (int)sim_random(&twin, 7) : -1;
        world_set_tail(&g_world, 0x26, 999), world_set_tail(&g_world, 0x28, 999);
        sim_call(&g_sim, 0x477D9);
        int now = sim_news_count(&g_sim) - 30;
        CHECK(now == (k == 3 || k == 5 || k == 6)); /* nobody in business: case 3's start minimum 12,000 still names 0 */
        pass += p, made += now;
        for (int j = 0; j < 100 - S_FILLED; j++) item(j)[0] = 0;
    }
    CHECK(pass > 10 && made > 0 && made < pass);
}

/* ---- training ---------------------------------------------------------------------------------------------- */

/* hospital h gets n staff whose average +0x136 is 8 + 4 x b2 + b15 (h > 0, the other bytes 0) */
static void staff(int h, int n, int b2, int b15, int b12) {
    static int next = 1;
    if (h < 0) {
        next = 1;
        return;
    }
    for (int i = 0; i < n; i++) {
        uint8_t *sl = staff_slots(&g_sim, h) + i * STAFF_SLOT_SIZE;
        put16(sl, next);
        uint8_t *st = g_world.block[BLK_STAFF] + next * 32;
        st[2] = (uint8_t)b2, st[15] = (uint8_t)b15, st[12] = (uint8_t)b12;
        next++;
    }
}

static void test_training(void) {
    /* the hours 5-19, then the limits n >= 7 and the average >= 90 */
    static const struct { int hour, n, b15, item; } C[] = {{4, 7, 2, 0}, {5, 7, 2, 1}, {19, 7, 2, 1}, {20, 7, 2, 0},
                                                           {10, 6, 2, 0}, {10, 7, 1, 0}, {10, 7, 2, 1}};
    for (size_t c = 0; c < sizeof C / sizeof *C; c++) {
        fresh_hour(C[c].hour);
        staff(-1, 0, 0, 0, 0);
        for (int h = 1; h <= 4; h++) staff(h, C[c].n, 20, C[c].b15, 0); /* four equal: the pick is one of them */
        put16(hosp(9) + 0x136, 999); /* stale: the recompute overwrites it */
        Sim twin = g_sim;
        int pos = (int)sim_bits(&twin, 3);
        sim_call(&g_sim, 0x49924);
        int ran = C[c].hour >= 5 && C[c].hour <= 19;
        CHECK(get16(hosp(9) + 0x136) == (ran ? 0 : 999));
        CHECK(sim_news_count(&g_sim) == C[c].item);
        if (C[c].item) {
            const uint8_t *it = item(0);
            CHECK(it[0] == 2 && it[20] == 0x28 && it[8] == 1 + pos && it[4] == 11 && it[5] == 1 && it[6] == 3);
            CHECK(get16(it + 10) == 7 && get16(it + 12) == 90 && get16(it + 14) == 0 && it[21] <= 1 && it[22] <= 1);
        }
    }
    /* the order: +0x136 x 64 + +0x134 descending, stable; position r3 */
    int seen[16] = {0};
    for (int n = 0; n < 400; n++) {
        fresh();
        staff(-1, 0, 0, 0, 0);
        staff(2, 8, 30, 0, 0); /* 128, 8 staff */
        staff(5, 7, 30, 0, 0); /* 128, 7 */
        staff(7, 7, 30, 0, 0); /* 128, 7: after 5 */
        staff(9, 7, 30, 0, 1); /* 8 + (120 + 64) = 192 */
        staff(12, 9, 25, 0, 0); /* 108, 9 */
        for (int j = 0; j < n % 9; j++) sim_bits(&g_sim, 1);
        Sim twin = g_sim;
        int pos = (int)sim_bits(&twin, 3);
        sim_call(&g_sim, 0x49924);
        static const int ORDER[4] = {9, 2, 5, 7};
        CHECK(sim_news_count(&g_sim) == 1 && item(0)[8] == ORDER[pos]);
        if (pos == 0) CHECK(get16(item(0) + 12) == 192 && get16(item(0) + 14) == 7);
        seen[item(0)[8]]++;
    }
    CHECK(seen[9] && seen[2] && seen[5] && seen[7] && !seen[12]);
    /* the gate 500/50 */
    fresh();
    staff(-1, 0, 0, 0, 0);
    for (int h = 1; h <= 4; h++) staff(h, 7, 30, 0, 0);
    fill(51);
    for (int i = 0; i < 3000; i++) sim_call(&g_sim, 0x49924);
    CHECK(sim_news_count(&g_sim) == 51);
    CHECK(get16(hosp(1) + 0x134) == 0); /* the gate came before the recompute */
}

/* ---- room reviews ----------------------------------------------------------------------------------------- */

/* call the routine; the r1 rule's draw is made to give 1 (hospital 0 included) when `keep0` */
static int run(uint32_t r, int good, int keep0) {
    if (good)
        for (;;) {
            Sim twin = g_sim;
            if ((int)sim_bits(&twin, 1) == keep0) break;
            sim_bits(&g_sim, 3);
        }
    sim_store_city(&g_sim); /* the city counts as an onset just before stored them (SIM-51) */
    int before = sim_news_count(&g_sim);
    sim_call(&g_sim, r);
    return sim_news_count(&g_sim) - before;
}

static void test_review_basics(void) {
    /* reception praise: the score limit 30, best deco of kinds 2, 4, 6, 7 with q >= 2 */
    furniture(3, 4, 2), furniture(4, 9, 5), furniture(6, 6, 6), furniture(7, 1, 4);
    for (int score = 29; score <= 30; score++) {
        fresh();
        furniture(3, 4, 2), furniture(4, 9, 5), furniture(6, 6, 6), furniture(7, 1, 4);
        uint8_t *rec = room(5, 2, 0, 321, 0, 10, score - 20);
        LIST(rec, 0, 3, 4, 6, 7);
        memset(item(0) + 20, 0xCD, 50), item(0)[0] = 0;
        hosp(3)[1] = 79;
        sim_rating_record(&g_sim, hosp(3));
        CHECK(run(0x47B6A, 1, 1) == (score == 30));
        CHECK(sim_rating_byte(&g_sim) == (score == 30 ? 10 : 79));
        if (score == 30) {
            const uint8_t *it = item(0);
            CHECK(it[0] == 2 && it[20] == 0x0F && it[8] == 5 && it[4] == 2 && it[5] == 1 && it[6] == 3 && it[28] == 0);
            CHECK(get16(it + 14) == 321 && get16(it + 22) == 6 && it[21] <= 1);
            CHECK(get16(it + 10) == 0 && it[26] == 0xCD && it[24] == 0xCD); /* no doctor, no room number, no scan 2 */
        }
    }
    /* the hours 4-18 and the gate 250/40 */
    for (int hour = 3; hour <= 19; hour++) {
        fresh_hour(hour);
        furniture(3, 4, 2);
        LIST(room(5, 2, 0, 1, 0, 15, 0), 0, 3);
        hosp(3)[1] = 79;
        sim_rating_record(&g_sim, hosp(3));
        CHECK(run(0x47B6A, 1, 1) == (hour >= 4 && hour <= 18));
        CHECK(sim_rating_byte(&g_sim) == (hour >= 4 && hour <= 18 ? 0 : 79));
    }
    fresh();
    furniture(3, 4, 2);
    LIST(room(5, 2, 0, 1, 0, 15, 0), 0, 3);
    fill(41);
    for (int i = 0; i < 3000; i++) sim_call(&g_sim, 0x47B6A);
    CHECK(count_type(0x0F) == 0);
    fresh();
    furniture(3, 4, 2);
    LIST(room(5, 2, 0, 1, 0, 15, 0), 0, 3);
    fill(40);
    int made = 0, pass = 0;
    for (int i = 0; i < 4000; i++) {
        Sim twin = g_sim;
        int p = sim_random_in(&twin, 250, 0, rng_scheduler(&twin)) == 2;
        int now = run(0x47B6A, 0, 0); /* hospital 5: the r1 rule does not matter */
        CHECK(now == p);
        made += now, pass += p;
        for (int j = 0; j < 100 - S_FILLED; j++) item(j)[0] = 0;
    }
    CHECK(pass > 5 && made > 0);
    /* an item of the type anywhere in the table (printed or not, any hospital): no item */
    fresh();
    furniture(3, 4, 2);
    LIST(room(5, 2, 0, 1, 0, 15, 0), 0, 3);
    plant_item(30, 2, 0x0F), item(30)[8] = 11, item(30)[1] = 1;
    hosp(3)[1] = 79;
    sim_rating_record(&g_sim, hosp(3));
    CHECK(run(0x47B6A, 1, 1) == 0);
    CHECK(sim_rating_byte(&g_sim) == 79);
    /* the r1 rule: hospital 0 only in half of the praise attempts, always in criticism */
    fresh();
    furniture(3, 4, 2), furniture(5, 1, 0);
    LIST(room(0, 2, 0, 1, 0, 15, 0), 0, 3);
    CHECK(run(0x47B6A, 1, 0) == 0 && run(0x47B6A, 1, 1) == 1);
    LIST(room(0, 2, 0, 1, 0, 15, 0), 0, 5);
    CHECK(run(0x47D46, 1, 0) == 1 && find(0x18)[8] == 0); /* the next r1 is 0: criticism ignores it */
    /* unstaffed rooms are no candidates; two-staff rooms need the doctor */
    fresh();
    furniture(3, 4, 2);
    LIST(room(5, 2, 0, 0, 7, 40, 0), 0, 3);
    CHECK(run(0x47B6A, 1, 1) == 0);
    fresh();
    furniture(3, 4, 2), equipment(9, 2, 9);
    uint8_t *a = room(4, 5, 0, 1, 0, 40, 0), *b = room(4, 5, 1, 2, 0, 20, 0);
    LIST(a, 0, 3), LIST(a, 1, 9), LIST(b, 0, 3), LIST(b, 1, 9);
    CHECK(run(0x51838, 1, 1) == 0);
    put16(b + 0x0A, 66);
    CHECK(run(0x51838, 1, 1) == 1);
    const uint8_t *it = find(0x11);
    CHECK(it && it[8] == 4 && it[26] == 2 && get16(it + 14) == 2 && get16(it + 10) == 66 && get16(it + 22) == 3);
    CHECK(get16(it + 24) == 9 && it[4] == 2 && it[6] == 4);
}

static void test_review_picks(void) {
    /* best: the highest score, ties to the last room in hospital and instance order, over the whole city */
    fresh();
    furniture(3, 4, 2), furniture(4, 3, 2), furniture(8, 9, 5), furniture(13, 2, 5);
    LIST(room(1, 3, 0, 1, 0, 20, 5), 0, 3, 8), LIST(room(3, 3, 2, 2, 0, 20, 5), 0, 4, 13);
    LIST(room(3, 3, 1, 3, 0, 20, 4), 0, 3, 8), LIST(room(14, 3, 0, 4, 0, 10, 0), 0, 3, 8);
    CHECK(run(0x4968E, 1, 1) == 1);
    const uint8_t *it = find(0x10);
    CHECK(it[8] == 3 && it[26] == 3 && get16(it + 14) == 2 && get16(it + 22) == 4 && get16(it + 24) == 13);
    CHECK(it[4] == 2 && it[5] == 1 && it[6] == 4);
    /* the minimum 40 of the waiting room */
    fresh();
    furniture(3, 4, 2), furniture(8, 9, 5);
    LIST(room(1, 3, 0, 1, 0, 19, 1), 0, 3, 8);
    CHECK(run(0x4968E, 1, 1) == 0);
    sim_room_record(&g_sim, 1, 3, 0)[1] = 2;
    CHECK(run(0x4968E, 1, 1) == 1);
    /* worst: the lowest, ties to the last; no minimum */
    fresh();
    furniture(3, 1, 0);
    LIST(room(2, 3, 0, 1, 0, 50, 0), 0, 3, 4), LIST(room(6, 3, 0, 2, 0, 5, 0), 0, 3, 4), LIST(room(9, 3, 1, 3, 0, 5, 0), 0, 3, 4);
    furniture(3, 1, 5), furniture(4, 1, 5); /* scan 1: 4 (ties to the last); scan 2 takes 3 and stops at 4 */
    CHECK(run(0x56B68, 0, 0) == 1);
    it = find(0x19);
    CHECK(it[8] == 9 && it[26] == 2 && (int8_t)it[5] == -1 && it[4] == 2 && it[6] == 4);
    CHECK(get16(it + 22) == 4 && get16(it + 24) == 3);
    /* wards: score + 40 - 4 x beds; Krankenzimmer with 6 beds (16) no item, 5 beds (20) an item */
    for (int beds = 6; beds >= 5; beds--) {
        fresh();
        furniture(3, 4, 2), furniture(20, 1, 8);
        uint8_t *rec = room(7, 6, 1, 5, beds, 0, 0);
        LIST(rec, 0, 3, 20);
        CHECK(run(0x565AE, 1, 1) == (beds == 5));
        if (beds == 5) {
            it = find(0x2A);
            CHECK(it[26] == 2 && get16(it + 24) == 20 && get16(it + 10) == 0);
        }
    }
    /* city tests: OP reviews need three OPs in the city, torture reviews three torture chambers */
    for (int cat = 7; cat <= 8; cat++)
        for (int n = 2; n <= 3; n++) {
            fresh();
            int kind = cat == 7 ? 8 : 5;
            furniture(3, 2, kind), equipment(9, 0, cat == 7 ? 15 : 13);
            uint8_t *rec = room(4, cat, 0, 1, 2, 20, 0);
            LIST(rec, 0, 3), LIST(rec, 1, 9);
            counts(0)[cat] = (uint8_t)(n - 1); /* hospital 0 counts too; its records are unstaffed */
            CHECK(run(cat == 7 ? 0x57861 : 0x57B35, 1, 1) == (n == 3));
            CHECK(run(cat == 7 ? 0x57327 : 0x575C4, 0, 0) == (n == 3));
            if (n == 3) {
                it = find(cat == 7 ? 0x2F : 0x31);
                CHECK(it[4] == 3 && it[5] == 1 && get16(it + 22) == 3 && get16(it + 24) == 9);
                it = find(cat == 7 ? 0x30 : 0x32);
                CHECK(it[4] == 3 && (int8_t)it[5] == -1 && it[6] == 4);
            }
        }
    /* SIM-51: the city test reads what the last onset stored ([0x5BCD] / [0x5BCF], not saved): with three rooms in the
     * city and nothing stored since the program start the review stops; stored 3 and then a room closed, it writes */
    for (int cat = 7; cat <= 8; cat++)
        for (int stored = 0; stored <= 1; stored++) {
            fresh();
            int kind = cat == 7 ? 8 : 5;
            furniture(3, 2, kind), equipment(9, 0, cat == 7 ? 15 : 13);
            uint8_t *rec = room(4, cat, 0, 1, 2, 20, 0);
            LIST(rec, 0, 3), LIST(rec, 1, 9);
            counts(0)[cat] = 2;
            if (stored) sim_store_city(&g_sim), counts(0)[cat] = 0;
            for (;;) { /* a gate that passes and the r1 rule keeping hospital 0, as run() */
                Sim twin = g_sim;
                if ((int)sim_bits(&twin, 1) == 1) break;
                sim_bits(&g_sim, 3);
            }
            int before = sim_news_count(&g_sim);
            sim_call(&g_sim, cat == 7 ? 0x57861 : 0x57B35);
            CHECK(sim_news_count(&g_sim) - before == stored);
            CHECK(g_sim.city[cat == 7 ? CITY_OP : CITY_TORTURE] == (stored ? 3 : 0));
        }
    /* OP praise: kind 8 with q >= 2 only */
    fresh();
    for (int k = 0; k < 16; k++) counts(k)[7] = 1;
    furniture(3, 1, 8), equipment(9, 0, 15);
    uint8_t *rec = room(4, 7, 0, 1, 2, 20, 0);
    LIST(rec, 0, 3), LIST(rec, 1, 9);
    CHECK(run(0x57861, 1, 1) == 0);
    furniture(3, 2, 8);
    CHECK(run(0x57861, 1, 1) == 1);
}

static void test_review_quirks(void) {
    /* 1. the low-byte chain of 0x510A6: [8, 49] names 49, [1, 39] names 1 */
    static const struct { int a, b, want; } Q1[2] = {{8, 49, 49}, {1, 39, 1}};
    for (int k = 0; k < 2; k++) {
        fresh();
        furniture(8, 2, 0), furniture(49, 5, 0), furniture(1, 3, 0), furniture(39, 0, 0), equipment(5, 1, 0);
        uint8_t *rec = room(3, 5, 0, 1, 2, 9, 9);
        LIST(rec, 0, Q1[k].a, Q1[k].b), LIST(rec, 1, 5);
        CHECK(run(0x510A6, 0, 0) == 1 && get16(find(0x21) + 22) == Q1[k].want && get16(find(0x21) + 24) == 5);
    }
    /* 2. Krankenzimmer criticism rates its equipment list by the furniture catalog; then scan 2 stops at u22 */
    fresh();
    furniture(49, 5, 0), furniture(39, 0, 0), equipment(49, 1, 0), equipment(39, 5, 0);
    furniture(46, 3, 8), furniture(22, 1, 8);
    uint8_t *rec = room(2, 6, 0, 1, 1, 0, 0);
    LIST(rec, 1, 49, 39), LIST(rec, 0, 46, 39, 22);
    CHECK(run(0x56DF3, 0, 0) == 1 && get16(find(0x2C) + 22) == 39 && get16(find(0x2C) + 24) == 46);
    /* the neuro-ward twin uses the equipment catalog */
    fresh();
    furniture(49, 5, 0), furniture(39, 0, 0), equipment(49, 1, 0), equipment(39, 5, 0);
    furniture(46, 3, 8), furniture(22, 1, 8);
    rec = room(2, 16, 0, 1, 1, 0, 0);
    LIST(rec, 1, 49, 39), LIST(rec, 0, 46, 39, 22);
    CHECK(run(0x5708D, 0, 0) == 1 && get16(find(0x2D) + 22) == 49 && get16(find(0x2D) + 24) == 22);
    /* 3. waiting-room criticism [24, 53, 13]: u22 53, u24 24 (13 is never seen) */
    fresh();
    furniture(24, 2, 5), furniture(53, 0, 2), furniture(13, 1, 5);
    LIST(room(1, 3, 0, 1, 0, 0, 0), 0, 24, 53, 13);
    CHECK(run(0x56B68, 0, 0) == 1 && get16(find(0x19) + 22) == 53 && get16(find(0x19) + 24) == 24);
    /* 4. holes stop: dental criticism [49, hole, 39] names 49; waiting-room praise [hole, 43, 24] writes nothing */
    fresh();
    furniture(49, 5, 0), furniture(39, 0, 0), equipment(5, 1, 0);
    rec = room(1, 11, 0, 1, 2, 0, 0);
    LIST(rec, 0, 49, 0, 39), LIST(rec, 1, 5);
    CHECK(run(0x4C304, 0, 0) == 1 && get16(find(0x22) + 22) == 49);
    fresh();
    furniture(43, 5, 2), furniture(24, 2, 5);
    rec = room(1, 3, 0, 1, 0, 30, 0);
    LIST(rec, 0, 0, 43, 24);
    CHECK(run(0x4968E, 1, 1) == 0);
    LIST(rec, 0, 24, 0, 43); /* scan 2 would find the seat 24, but scan 1 stops at the hole before 43 */
    CHECK(run(0x4968E, 1, 1) == 0);
    LIST(rec, 0, 43, 24, 0);
    CHECK(run(0x4968E, 1, 1) == 1 && get16(find(0x10) + 22) == 43 && get16(find(0x10) + 24) == 24);
    /* all slots: treatment-room praise [hole, 43] / equipment [hole, 37] names 43 and 37 */
    fresh();
    furniture(43, 5, 2), equipment(37, 0, 10);
    rec = room(1, 5, 0, 1, 2, 15, 0);
    LIST(rec, 0, 0, 43), LIST(rec, 1, 0, 37);
    CHECK(run(0x51838, 1, 1) == 1 && get16(find(0x11) + 22) == 43 && get16(find(0x11) + 24) == 37);
    /* best ties to the first slot, worst to the last; reception criticism considers only q < 3 */
    fresh();
    furniture(43, 5, 2), furniture(5, 2, 0), furniture(8, 2, 0), furniture(44, 5, 4);
    rec = room(1, 2, 0, 1, 0, 30, 0);
    LIST(rec, 0, 43, 5, 8, 44);
    CHECK(run(0x47D46, 0, 0) == 1 && get16(find(0x18) + 22) == 8);
    CHECK(run(0x47B6A, 1, 1) == 1 && get16(find(0x0F) + 22) == 43);
    fresh();
    furniture(43, 3, 2);
    LIST(room(1, 2, 0, 1, 0, 30, 0), 0, 43);
    CHECK(run(0x47D46, 0, 0) == 0); /* q 3 is not below 3 */
    /* reception praise: a hole first hides everything; a deco item of q 1 is not taken */
    fresh();
    furniture(43, 5, 2), furniture(7, 1, 4);
    rec = room(1, 2, 0, 1, 0, 30, 0);
    LIST(rec, 0, 0, 43);
    CHECK(run(0x47B6A, 1, 1) == 0);
    LIST(rec, 0, 7, 0);
    CHECK(run(0x47B6A, 1, 1) == 0);
    furniture(7, 2, 4);
    CHECK(run(0x47B6A, 1, 1) == 1 && get16(find(0x0F) + 22) == 7);
    /* scan 2 starts at -1 for the best: an item of q 0 is taken; scan 1 starts at 0: q 0 is not */
    fresh();
    furniture(43, 5, 2), equipment(37, 0, 10);
    rec = room(1, 11, 0, 1, 2, 15, 0);
    LIST(rec, 0, 43), LIST(rec, 1, 37);
    CHECK(run(0x562CB, 1, 1) == 1 && get16(find(0x13) + 24) == 37);
    equipment(37, 0, 9); /* kind 9 is not one of the dental kinds 10, 12, 13, 14 */
    find(0x13)[0] = 0;
    CHECK(run(0x562CB, 1, 1) == 0);
}

/* ---- the empty slot (docs/news-generators.md section 9.4, SIM-15) ------------------------------------------------ */

static void mark(int i) { memset(item(i) + 1, 0x5A, 69), item(i)[0] = 0; } /* a free slot with the marker in 1-69 */
static int marked(const uint8_t *it, int from, int to) {
    for (int b = from; b <= to; b++)
        if (it[b] != 0x5A) return 0;
    return 1;
}

/* row 1: every hospital in business with +0x26 = +0x2A = +0xE6 = 0, the city records 0: each case k leaves bytes 1-19
 * = 0, b20 = 3, b21 = k, b28 = 0 and the marker elsewhere; byte 0 stays 0 */
static void test_empty_city_statistics(void) {
    for (int k = 0; k < 7; k++) {
        fresh();
        for (int h = 0; h < 16; h++) hosp(h)[0x15] = 1;
        mark(0);
        draw_k(k);
        sim_call(&g_sim, 0x477D9);
        const uint8_t *it = item(0);
        CHECK(sim_news_count(&g_sim) == 0 && it[0] == 0);
        int clear = 1;
        for (int b = 1; b < 20; b++) clear &= it[b] == 0;
        CHECK(clear && it[20] == 3 && it[21] == k && it[28] == 0);
        CHECK(marked(it, 22, 27) && marked(it, 29, 69));
    }
}

/* row 2: hospital 5 the only one with the category, record byte 0 = 50, +8 = 0x0123, +0x0A = 0x0456 (two staff), the OP
 * and torture-chamber city counts 3. (a) empty lists, (b) scan 1 satisfied by one fitting and nothing for scan 2, (c)
 * the room unstaffed. b21's r1 is drawn once the room is chosen, before the scans. */
static const struct { uint32_t r; int type, cat, two, good, w, b6; } EMPTY_REVIEWS[18] = {
    {0x47B6A, 0x0F, 2, 0, 1, 2, 3},  {0x47D46, 0x18, 2, 0, 0, 2, 4},  {0x4968E, 0x10, 3, 0, 1, 2, 4},
    {0x56B68, 0x19, 3, 0, 0, 2, 4},  {0x51838, 0x11, 5, 1, 1, 2, 4},  {0x510A6, 0x21, 5, 1, 0, 2, 4},
    {0x562CB, 0x13, 11, 1, 1, 2, 4}, {0x4C304, 0x22, 11, 1, 0, 2, 4}, {0x55FEE, 0x14, 15, 1, 1, 3, 4},
    {0x51338, 0x29, 15, 1, 0, 2, 4}, {0x565AE, 0x2A, 6, 0, 1, 2, 4},  {0x56DF3, 0x2C, 6, 0, 0, 2, 4},
    {0x5688B, 0x2B, 16, 0, 1, 2, 4}, {0x5708D, 0x2D, 16, 0, 0, 2, 4}, {0x57861, 0x2F, 7, 1, 1, 3, 4},
    {0x57327, 0x30, 7, 1, 0, 3, 4},  {0x57B35, 0x31, 8, 1, 1, 3, 4},  {0x575C4, 0x32, 8, 1, 0, 3, 4}};
static void test_empty_reviews(void) {
    for (int k = 0; k < 18; k++)
        for (int c = 0; c < 3; c++) {
            const int cat = EMPTY_REVIEWS[k].cat, two = EMPTY_REVIEWS[k].two, good = EMPTY_REVIEWS[k].good;
            if (c == 1 && cat == 2) continue; /* the reception reviews have one scan */
            fresh();
            furniture(5, 2, 2), furniture(23, 2, 8), furniture(19, 2, 5), equipment(5, 2, 2);
            uint8_t *rec = room(5, cat, 0, c == 2 ? 0 : 0x0123, c == 2 || !two ? 0 : 0x0456, 50, 0);
            if (cat == 7 || cat == 8) counts(0)[cat] = 2; /* the city count 3; hospital 0's records are unstaffed */
            sim_store_city(&g_sim); /* as the last onset stored it (SIM-51) */
            int e1 = EMPTY_REVIEWS[k].r == 0x56DF3 || EMPTY_REVIEWS[k].r == 0x5708D; /* scan 1 walks the equipment */
            int id1 = cat == 7 ? 23 : cat == 8 ? 19 : 5;
            if (c == 1) LIST(rec, e1, id1);
            mark(0);
            Sim twin = g_sim;
            int b21 = (int)sim_bits(&twin, 1);
            if (good) b21 = (int)sim_bits(&twin, 1); /* the r1 rule for hospital 0 draws first */
            Sim twin0 = g_sim;
            if (good) sim_bits(&twin0, 1); /* (c): only the rule's draw */
            hosp(3)[1] = 79;
            sim_rating_record(&g_sim, hosp(3));
            sim_call(&g_sim, EMPTY_REVIEWS[k].r);
            SimRatingRef retained = g_sim.rating_ref;
            sim_rating_record(&g_sim, c == 2 ? hosp(3) : rec);
            CHECK(retained.block == g_sim.rating_ref.block && retained.offset == g_sim.rating_ref.offset);
            g_sim.rating_ref = retained;
            const uint8_t *it = item(0);
            CHECK(sim_news_count(&g_sim) == 0 && it[0] == 0);
            if (c == 2) {
                CHECK(marked(it, 1, 69));
                CHECK(sim_bits(&twin0, 31) == sim_bits(&g_sim, 31)); /* no r1 for b21 without a room */
                continue;
            }
            int clear = 1;
            for (int b = 1; b < 20; b++)
                clear &= b == 4 || b == 5 || b == 6 || b == 8 || b == 10 || b == 11 || b == 14 || b == 15 || it[b] == 0;
            CHECK(clear && it[4] == EMPTY_REVIEWS[k].w && (int8_t)it[5] == (good ? 1 : -1) && it[6] == EMPTY_REVIEWS[k].b6);
            CHECK(it[8] == 5 && get16(it + 14) == 0x0123 && get16(it + 10) == (two ? 0x0456 : 0));
            CHECK(it[20] == EMPTY_REVIEWS[k].type && it[21] == b21 && it[28] == 0);
            CHECK(cat == 2 ? it[26] == 0x5A : it[26] == 1);
            CHECK(sim_bits(&twin, 31) == sim_bits(&g_sim, 31)); /* b21's r1 was drawn although no item resulted */
            if (c == 0) {
                CHECK((uint16_t)get16(it + 22) == 0xFFFF && marked(it, 24, 25));
            } else {
                CHECK(get16(it + 22) == id1 && (uint16_t)get16(it + 24) == 0xFFFF);
            }
            CHECK(it[27] == 0x5A && marked(it, 29, 69));
        }
    /* (b) of the doc: the waiting-room praise with one Dekoration of q 2 and no seat: u22 = that id, u24 = FFFF */
    fresh();
    furniture(5, 2, 2);
    LIST(room(5, 3, 0, 0x0123, 0, 50, 0), 0, 5);
    mark(0);
    CHECK(run(0x4968E, 1, 1) == 0 && get16(item(0) + 22) == 5 && (uint16_t)get16(item(0) + 24) == 0xFFFF);
    /* and with the seat the same call writes the item (byte 0 = 2) over those bytes */
    furniture(24, 2, 5);
    LIST(room(5, 3, 0, 0x0123, 0, 50, 0), 0, 5, 24);
    CHECK(run(0x4968E, 1, 1) == 1 && item(0)[0] == 2 && get16(item(0) + 24) == 24 && g_sim.news_written[0x10] == 1);
}

/* SIM-104/105: full-record fixtures shared by the English news and direct all-edition average tests.
 * Means rank hospitals 0/3/12/7; 0 and 3 tie, and rank 2 has only three nurses, so cannot publish. */
static const int FIGURE_H[] = {0, 3, 12, 7}, FIGURE_N[] = {4, 4, 3, 4}, FIGURE_B[] = {110, 110, 100, 90};
static void figure_setup(World *expected, int hour, int mode, int count, int index, int news) {
    fresh_hour(hour);
    for (int h = 0; h < HOSPITAL_COUNT; h++) {
        memset(hosp(h), 0x5A, HOSPITAL_SIZE);
        hosp(h)[0x15] = 0; /* even closed hospitals remain candidates */
        put16(hosp(h) + 0x16A, count), put16(hosp(h) + 0x16C, index);
    }
    for (int group = 0; group < (mode == 0 ? 4 : mode == 2 ? 1 : 0); group++)
        for (int i = 0; i < FIGURE_N[group]; i++) {
            int who = 10 + 10 * group + i;
            uint8_t *sl = staff_slots(&g_sim, FIGURE_H[group]) + i * STAFF_SLOT_SIZE;
            put16(sl, who), sl[2] = 1;
            uint8_t *st = g_world.block[BLK_STAFF] + who * STAFF_SIZE;
            st[5] = (uint8_t)(mode == 2 ? 0 : FIGURE_B[group]);
            st[6] = 3, st[7] = 5, st[8] = 40;
        }
    put16(staff_slots(&g_sim, 15), 90), staff_slots(&g_sim, 15)[2] = 2;
    g_world.block[BLK_STAFF][90 * STAFF_SIZE + 5] = 1;
    g_world.block[BLK_STAFF][90 * STAFF_SIZE + 6] = 10; /* unselected doctor mean 81 */
    memset(g_world.block[BLK_NEWS], 0xA7, 7000);
    for (int i = 0; i < 100; i++) item(i)[0] = 0;
    fill(news);
    if (news) item(99)[20] = 0x2E; /* a duplicate does not suppress the new item */
    world_alloc(expected);
    for (int b = 0; b <= WORLD_BLOCKS; b++) memcpy(expected->block[b], g_world.block[b], g_world.size[b]);
}
static void figure_average_result(World *expected, int mode) {
    for (int h = 0; h < 16; h++) {
        uint8_t *r = world_hospital(expected, h);
        put16(r + 0x4A, 0);
        r[0x113] = r[0x14A] = r[0x14B] = r[0x14C] = 0;
    }
    for (int group = 0; group < (mode == 0 ? 4 : mode == 2 ? 1 : 0); group++) {
        uint8_t *r = world_hospital(expected, FIGURE_H[group]);
        put16(r + 0x4A, mode == 2 ? 0 : FIGURE_B[group]);
        put16(r + 0x16A, FIGURE_N[group]);
        if (mode == 0) put16(r + 0x16C, 10 + 10 * group); /* first equal-bust nurse wins */
        r[0x113] = 40, r[0x14A] = 3, r[0x14B] = 5;
    }
    world_hospital(expected, 15)[0x14C] = 81;
}
static void figure_check_world(const World *expected) {
    for (int b = 0; b <= WORLD_BLOCKS; b++) CHECK(!memcmp(expected->block[b], g_world.block[b], g_world.size[b]));
}
static void test_staff_average_cache(void) {
    static const int CACHE[] = {0, 555, 65535};
    for (int edition = EDITION_DISK; edition <= EDITION_ENGLISH_CD; edition++)
        for (int mode = 0; mode < 3; mode++) for (int cached = 0; cached < 3; cached++) {
            g_prog.edition = (Edition)edition;
            World expected;
            figure_setup(&expected, 10, mode, CACHE[cached], CACHE[cached], 0);
            figure_average_result(&expected, mode);
            uint32_t raw = 0;
            SimScript script = {.v = &raw, .n = 1};
            g_sim.rng_script = &script;
            sim_staff_averages(&g_sim);
            CHECK(script.used == 0 && script.extra == 0);
            figure_check_world(&expected);
            world_free(&expected);
            g_sim.rng_script = NULL;
        }
    g_prog.edition = EDITION_DISK;
}

static void figure_case(int edition, int hour, int news, int gate, int rank, int payload, int mode, int count, int index) {
    g_prog.edition = (Edition)edition;
    World expected;
    figure_setup(&expected, hour, mode, count, index, news);
    int before = failures;
    int enabled = edition == EDITION_ENGLISH_CD && hour >= 3 && hour <= 19;
    int averages = enabled && (news < 5 || (gate && news <= 50));
    int h = mode == 0 ? FIGURE_H[rank] : rank;
    if (averages) figure_average_result(&expected, mode);
    uint8_t *r = world_hospital(&expected, h);
    int publish = averages && get16(r + 0x16A) >= 4 && get16(r + 0x16C) > 0;
    uint32_t raw[12] = {0};
    size_t n = 0;
    char expected_trace[200] = {0}, line[80];
    if (enabled && news >= 5) {
        if (gate == 2) raw[n++] = 0xFFFFFFFF;
        raw[n++] = gate ? 0x10002 : 0;
        snprintf(expected_trace, sizeof expected_trace, "42E10 r500 %d %d\n", gate ? 2 : 0, gate == 2 ? 2 : 1);
    }
    if (averages) {
        raw[n++] = rank == 3 ? 0xFFFFFFFF : 0x10000u + (uint32_t)rank;
        snprintf(line, sizeof line, "628D3 b3 %d 1\n", rank);
        strcat(expected_trace, line);
    }
    if (publish) {
        raw[n++] = payload ? 0xFFFFFFFF : 0x10000;
        raw[n++] = payload ? 0x10000 : 0x10001;
        snprintf(line, sizeof line, "62915 b1 %d 1\n6291D b1 %d 1\n", payload, !payload);
        strcat(expected_trace, line);
        uint8_t *it = expected.block[BLK_NEWS]; /* fill() leaves slot 0 as the first free slot */
        it[0] = 2;
        memset(it + 1, 0, 19);
        it[4] = 11, it[5] = 1, it[6] = 3, it[8] = (uint8_t)h, it[20] = 0x2E;
        it[21] = (uint8_t)payload, it[22] = (uint8_t)!payload, it[28] = 0;
        put16(it + 10, get16(r + 0x4A)), put16(it + 12, get16(r + 0x16A)), put16(it + 14, get16(r + 0x16C));
    }
    SimScript script = {.v = raw, .n = sizeof raw / sizeof *raw, .trace = tmpfile()};
    CHECK(script.trace != NULL);
    g_sim.rng_script = &script;
    uint32_t state = g_sim.rng_s;
    uint64_t auxiliary = g_sim.rng;
    CHECK(sim_call(&g_sim, 0x62888));
    g_sim.rng_script = NULL;
    for (size_t i = 0; i < n; i++) state += raw[i];
    CHECK(script.used == n && script.extra == 0 && g_sim.rng_s == state && g_sim.rng == auxiliary);
    CHECK(g_sim.news_written[0x2E] == (uint32_t)publish);
    figure_check_world(&expected);
    if (script.trace) {
        char trace[200] = {0};
        rewind((FILE *)script.trace);
        size_t bytes = fread(trace, 1, sizeof trace - 1, (FILE *)script.trace);
        CHECK(bytes == strlen(expected_trace) && !strcmp(trace, expected_trace));
        fclose((FILE *)script.trace);
    }
    if (before != failures) fprintf(stderr, "  SIM-104 edition %d hour %d news %d gate %d rank %d payload %d mode %d cache %d/%d\n",
                                   edition, hour, news, gate, rank, payload, mode, count, index);
    world_free(&expected);
}
static void test_nurses_figure(void) {
    CHECK(build_routine(&PROGRAM_BUILDS[EDITION_ENGLISH_CD], 0x6250E) == 0x62888);
    static const int NEWS[] = {0, 4, 5, 50, 51, 99, 100};
    static const int HOUR[] = {2, 3, 19, 20, 0x103, 0x113, 0x7FFF, 0x8000, 0xFFFF};
    static const int COUNT[] = {0, 3, 4, 255, 256, 32767, 32768, 65535};
    static const int INDEX[] = {0, 1, 255, 256, 32767, 32768, 65535};
    for (int edition = EDITION_DISK; edition <= EDITION_ENGLISH_CD; edition++) {
        for (int news = 0; news < 7; news++) for (int gate = 0; gate < 3; gate++)
            for (int rank = 0; rank < 4; rank++) for (int payload = 0; payload < 2; payload++)
                figure_case(edition, 10, NEWS[news], gate, rank, payload, 0, 42, 555);
        for (int hour = 0; hour < 9; hour++) figure_case(edition, HOUR[hour], 5, 1, 0, 1, 0, 42, 555);
        for (int count = 0; count < 8; count++) for (int index = 0; index < 7; index++)
            figure_case(edition, 10, 0, 1, 0, 1, 1, COUNT[count], INDEX[index]);
        for (int index = 0; index < 7; index++) figure_case(edition, 10, 0, 1, 0, 1, 2, 42, INDEX[index]);
    }
    g_prog.edition = EDITION_DISK;
}

static jmp_buf figure_boundary;
static World *figure_boundary_expected;
static void figure_before_rank(const SimScript *script, const char *message) {
    (void)message;
    CHECK(script->used == script->n);
    figure_check_world(figure_boundary_expected); /* averages complete; no header or payload write yet */
    longjmp(figure_boundary, 1);
}
static void test_nurses_figure_order(void) {
    for (int news = 0; news <= 5; news += 5) {
        g_prog.edition = EDITION_ENGLISH_CD;
        World expected;
        figure_setup(&expected, 10, 0, 42, 555, news);
        figure_average_result(&expected, 0);
        figure_boundary_expected = &expected;
        uint32_t raw = 2;
        SimScript script = {.v = &raw, .n = news ? 1 : 0};
        g_sim.rng_script = &script;
        g_sim.rng_failure = figure_before_rank;
        if (!setjmp(figure_boundary)) { sim_call(&g_sim, 0x62888); CHECK(0); }
        g_sim.rng_script = NULL, g_sim.rng_failure = NULL;
        world_free(&expected);
    }
    g_prog.edition = EDITION_DISK;
}

static void test_noop(void) {
    fresh();
    staff(-1, 0, 0, 0, 0);
    for (int h = 1; h <= 4; h++) staff(h, 7, 30, 0, 0); /* training would write an item and +0x134 */
    World before;
    world_alloc(&before);
    for (int b = 0; b <= WORLD_BLOCKS; b++) memcpy(before.block[b], g_world.block[b], g_world.size[b]);
    CHECK(sim_call(&g_sim, 0x62888));
    for (int b = 0; b <= WORLD_BLOCKS; b++) CHECK(memcmp(before.block[b], g_world.block[b], g_world.size[b]) == 0);
    world_free(&before);
}

int main(int argc, char **argv) {
    if (argc == 2 && (!strcmp(argv[1], "nurse-news") || !strcmp(argv[1], "nurse-news-order"))) {
        if (!strcmp(argv[1], "nurse-news")) test_nurses_figure();
        test_nurses_figure_order();
        world_free(&g_world);
        if (failures) return 1;
        puts("[OK] SIM-104 English nurse news and edition controls");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "staff-average-cache")) {
        test_staff_average_cache();
        world_free(&g_world);
        if (failures) return 1;
        puts("[OK] SIM-105 all-edition staff average cache retention");
        return 0;
    }
    test_short_items();
    test_city_statistics();
    test_training();
    test_review_basics();
    test_review_picks();
    test_review_quirks();
    test_empty_city_statistics();
    test_empty_reviews();
    test_noop();
    test_staff_average_cache();
    test_nurses_figure();
    test_nurses_figure_order();
    world_free(&g_world);
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] all checks passed\n");
    return 0;
}
