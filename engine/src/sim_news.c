/* Module H, the newspaper simulation, from docs/mainloop.md, module H: the item table (save block 31, 100 items of
 * 70 bytes), the issue (tail block +0x16..+0x19), the gates, the selection, the ageing, the opinion effect of the
 * printed items and the generators of "When the generators fire" with a non-zero opinion change. Drawing the
 * paper is not part of it. The copy-protection hooks (item type 0x35, 0x4A35A, 0x45FF4, the cool-down 0x2EE24)
 * are left out (docs/mainloop.md, section 7). Engine choices are marked. */
#include <string.h>

#include "garage.h"
#include "sim_internal.h"

#define NEWS_ITEMS 100
#define NEWS_SIZE 70
#define PERSONS 1009
#define PERSON_SIZE 106
#define STAFF_SIZE 32
#define STAFF_LAST 1040
enum { N_CLASS = 0, N_ISSUE = 1, N_AGE = 2, N_WEIGHT = 4, N_CHANGE = 5, N_CHANCE = 6, N_POS = 7, N_HOSP = 8,
       N_TYPE = 20, N_PARAM = 21, N_TAG = 28 };
enum { T_ISSUE = 0x16, T_RECORD_ROOM = 0x26, T_RECORD_WARD = 0x28, T_RECORD_ROOM_H = 0x2A, T_RECORD_WARD_H = 0x2C,
       T_CONTEST_DAY = 0x2E, T_CONTEST_DONE_A = 0x30, T_CONTEST_DONE_B = 0x31 };

uint8_t *sim_news_item(Sim *s, int i) { return s->world->block[BLK_NEWS] + i * NEWS_SIZE; }
uint8_t *sim_news_issue(Sim *s) { return s->world->block[WORLD_TAIL] + T_ISSUE; }
static uint8_t *person(Sim *s, int i) { return s->world->block[BLK_PATIENTS] + i * PERSON_SIZE; }
static uint8_t *tail(Sim *s) { return s->world->block[WORLD_TAIL]; }
static int r1(Sim *s) { return (int)sim_bits(s, 1); }
static int r3(Sim *s) { return (int)sim_bits(s, 3); }

/* ---- the table ------------------------------------------------------------------------------------ */

/* [0x5BEB]: the original counts an item when it is written and recounts in 0x474BC; engine choice: counted live */
int sim_news_count(Sim *s) {
    int n = 0;
    for (int i = 0; i < NEWS_ITEMS; i++) n += sim_news_item(s, i)[N_CLASS] != 0;
    return n;
}

static int free_slot(Sim *s) { /* 0x42E38 */
    for (int i = 0; i < NEWS_ITEMS; i++)
        if (!sim_news_item(s, i)[N_CLASS]) return i;
    return -1;
}

static int gate(Sim *s, int limit) { /* 0x42DD5, AX = limit */
    int n = sim_news_count(s);
    return n < 5 || n <= limit ? free_slot(s) : -1;
}

static int gate_draw_inputs(Sim *s, int chance, int limit, SimIn in) { /* 0x42E01, AX = chance, BX = limit */
    int n = sim_news_count(s);
    if (n < 5) return free_slot(s);
    if (sim_random_in(s, (uint32_t)chance, 0x42E10, in) != 2 || n > limit) return -1;
    return free_slot(s);
}

int sim_news_gate_draw(Sim *s, int chance, int limit) {
    return gate_draw_inputs(s, chance, limit, rng_scheduler(s));
}

static int exists(Sim *s, int type, int h, int param) { /* an item of the type about h (param -1: any) */
    for (int i = 0; i < NEWS_ITEMS; i++) {
        const uint8_t *it = sim_news_item(s, i);
        if (it[N_CLASS] && it[N_TYPE] == type && it[N_HOSP] == h && (param < 0 || it[N_PARAM] == param)) return 1;
    }
    return 0;
}

/* 0x42DAE alone: bytes 1-19 cleared, byte 0 untouched. A generator that has its slot and then finds no subject leaves
 * what it wrote so far in the free slot (docs/news-generators.md, section 9, SIM-15). */
uint8_t *sim_news_clear(Sim *s, int slot) {
    uint8_t *it = sim_news_item(s, slot);
    memset(it + 1, 0, 19);
    return it;
}

/* byte 0 = the class of an item whose bytes are written; counts its type (b20) in news_written */
void sim_news_commit(Sim *s, int slot, int cls) {
    uint8_t *it = sim_news_item(s, slot);
    it[N_CLASS] = (uint8_t)cls;
    s->news_written[it[N_TYPE] & 63]++;
}

/* 0x42DAE (bytes 1-19 cleared; bytes 20-69 keep what the slot's previous item left, docs/news-generators.md,
 * "Common rules"), byte 0 = the class, and the item's type and opinion fields */
uint8_t *sim_news_new_item(Sim *s, int slot, int cls, int type, int weight, int change, int chance, int h) {
    uint8_t *it = sim_news_clear(s, slot);
    it[N_TYPE] = (uint8_t)type;
    it[N_TAG] = 0;
    it[N_WEIGHT] = (uint8_t)weight;
    it[N_CHANGE] = (uint8_t)(int8_t)change;
    it[N_CHANCE] = (uint8_t)chance;
    it[N_HOSP] = (uint8_t)h;
    sim_news_commit(s, slot, cls);
    return it;
}

/* ---- the issue -------------------------------------------------------------------------------------- */

/* 0x46EA4: the best unprinted item of the class for the issue */
static int select_item(Sim *s, int cls) {
    const uint8_t *issue = sim_news_issue(s);
    int best = 0, pick = -1;
    for (int i = 0; i < NEWS_ITEMS; i++) {
        const uint8_t *it = sim_news_item(s, i);
        if (it[N_CLASS] != cls || it[N_ISSUE]) continue;
        int score = it[N_WEIGHT] * 16 + (uint16_t)get16(it + N_AGE) + it[N_CHANCE] * 4;
        if (score <= best) continue;
        int dup = 0;
        if (!(it[N_TYPE] == 0x0E && issue[0]))
            for (int j = 0; j < 4 && !dup; j++)
                dup = issue[j] && sim_news_item(s, issue[j] - 1)[N_TYPE] == it[N_TYPE];
        if (dup) continue;
        best = score;
        pick = i;
    }
    return pick;
}

static void fill_issue(Sim *s) { /* 0x46E0C; the cool-down 0x2EE24 of slot 2 is copy protection */
    uint8_t *issue = sim_news_issue(s);
    for (int j = 0; j < 4; j++) {
        if (issue[j]) continue;
        int i = select_item(s, j ? 2 : 1);
        if (i < 0) continue;
        uint8_t *it = sim_news_item(s, i);
        it[N_ISSUE] = 1;
        it[N_POS] = (uint8_t)j;
        put16(it + N_AGE, 0);
        issue[j] = (uint8_t)(i + 1);
    }
}

static int issue_full(Sim *s) {
    const uint8_t *issue = sim_news_issue(s);
    return issue[0] && issue[1] && issue[2] && issue[3];
}

/* 0x474BC: the season countdown and reset (sim_season, docs/ui-garage.md "Season reset"), then the ageing */
static void age_items(Sim *s) {
    sim_season(s);
    static const uint8_t KEEP[] = {7, 8, 0x23, 0x24, 0x25, 0x33, 0x35};
    for (int i = 0; i < NEWS_ITEMS; i++) {
        uint8_t *it = sim_news_item(s, i);
        if (!it[N_CLASS]) continue;
        if (!it[N_ISSUE]) {
            add16(it + N_AGE, 1);
            if ((uint16_t)get16(it + N_AGE) < 150 || memchr(KEEP, it[N_TYPE], sizeof KEEP)) continue;
            it[N_CLASS] = 0;
        } else if (issue_full(s)) {
            add16(it + N_AGE, 1);
            if ((uint16_t)get16(it + N_AGE) < 30) continue;
            if (it[N_POS] < 4) sim_news_issue(s)[it[N_POS]] = 0;
            it[N_CLASS] = 0;
        }
    }
}

/* 0x42C1D: each printed item changes one random person's opinion of its hospital */
static void opinion_effect(Sim *s) {
    if (!issue_full(s)) return;
    const uint8_t *issue = sim_news_issue(s);
    for (int j = 0; j < 4; j++) {
        const uint8_t *it = sim_news_item(s, (issue[j] - 1) % NEWS_ITEMS);
        int d = (int8_t)it[N_CHANGE];
        if (!d || (int)sim_bits(s, 7) >= it[N_CHANCE] + 1) continue;
        uint8_t *p = person(s, (int)sim_random(s, 1008) + 1);
        if (it[N_HOSP] >= 16) continue; /* engine guard: no item names one */
        int o = p[20 + it[N_HOSP]] + d;
        p[20 + it[N_HOSP]] = (uint8_t)(o < 0 ? 0 : o > 100 ? 100 : o);
    }
}

/* ---- generators called from other modules ---------------------------------------------------------- */

/* 0x42982 (treatment room, dental, neurology) / 0x42AEA (Krankenzimmer, neuro ward): the records, then an item */
void sim_news_bill(Sim *s, int h, int index, int bill, int ward) {
    uint8_t *r = hospital(s, h);
    int field = ward ? 0x110 : 0x10E, rec = ward ? T_RECORD_WARD : T_RECORD_ROOM;
    int flag = 0;
    if (bill >= get16(r + field)) {
        put16(r + field, bill);
        if (bill > world_tail(s->world, rec)) {
            world_set_tail(s->world, rec, bill);
            world_set_tail(s->world, ward ? T_RECORD_WARD_H : T_RECORD_ROOM_H, h);
            flag = ward ? 4 : 2;
        } else if (bill == world_tail(s->world, rec)) {
            flag = ward ? 5 : 3;
        }
    }
    if (bill < (ward ? 120 : 30)) return;
    int slot = gate(s, ward ? 80 : 30);
    if (slot < 0) return;
    const int *steps = ward ? (const int[]){150, 190, 230} : (const int[]){35, 40, 45};
    int d = bill >= steps[2] ? -7 : bill >= steps[1] ? -4 : bill >= steps[0] ? -2 : -1;
    int w = 5, b6 = 4;
    if (flag == 2 || flag == 4) w = 14, d = -2, b6 = 5;
    if (flag == 3 || flag == 5) w = 12, d = -2;
    const uint8_t *p = person(s, index);
    uint8_t *it = sim_news_new_item(s, slot, 2, 5, w, d, b6, p[81]);
    it[N_PARAM] = (uint8_t)(flag ? flag : ward);
    put16(it + 16, index);
    put16(it + 22, bill);
    it[24] = (uint8_t)r1(s), it[25] = (uint8_t)r1(s);
    it[26] = p[56], it[27] = p[57];
}

/* 0x493AD (0: left a waiting room), 0x493D0 (1: the reception queue), 0x493F3 (2: the Krankenzimmer queue).
 * The duplicate test 0x4948E compares b21 with the scratch byte [0x45CF] (Sim.dup_byte, whatever its last writer left,
 * docs/mainloop.md module H item 2, "Who leaves the value"; SIM-55), not with the new item's b21. */
void sim_news_gave_up(Sim *s, int h, int index, int variant) {
    int slot = gate(s, 50);
    /* FIX-54 (docs/fixes.md): original compares with [0x45CF]; fixed with the new item's own b21 */
    if (slot < 0 || exists(s, 0x17, h, fix_on(54) ? variant : s->dup_byte)) return;
    uint8_t *it = sim_news_new_item(s, slot, 2, 0x17, 5, -1, 4, h);
    it[N_PARAM] = (uint8_t)variant;
    put16(it + 16, index);
    it[22] = (uint8_t)r3(s), it[23] = (uint8_t)r3(s);
}

/* 0x42916 (type 0x1C, thrown out at the reception) and 0x428BD (type 0x1B, no seat found), docs/news-generators.md
 * section 9.3 item 4 (SIM-30): gate 95, class 2, 0x1B w 9, d -1, b6 4; 0x1C w 13, d -2, b6 6; b21 = r1, b28 = 0.
 * Byte 8 and u16 16 come from the records [0x4518] / [0x451C] point at, which the doc does not settle (probably the
 * reception's patient, guess): engine reading h and the patient at the desk. */
void sim_news_refusal(Sim *s, int h, int index, int type) {
    int slot = gate(s, 95);
    if (slot < 0) return;
    int out = type == 0x1C;
    uint8_t *it = sim_news_new_item(s, slot, 2, type, out ? 13 : 9, out ? -2 : -1, out ? 6 : 4, h);
    put16(it + 16, index);
    it[N_PARAM] = (uint8_t)r1(s);
}

/* 0x494CB: a failed home treatment; `type` is the illness type the caller books the failure under */
void sim_news_home_failure(Sim *s, int index, int type) {
    int slot = gate(s, 70);
    if (slot < 0) return;
    const uint8_t *p = person(s, index);
    int b = !get16(p + 56) ? 0 : type == 0 ? 1 : type == 1 ? 2 : 3;
    if (p[100]) b = b == 0 ? 5 : b == 3 ? 6 : 4;
    uint8_t *it = sim_news_new_item(s, slot, 2, 4, 6, -1, 3, p[81]);
    it[N_PARAM] = (uint8_t)b;
    put16(it + 16, index);
    it[22] = (uint8_t)r1(s), it[23] = (uint8_t)r3(s), it[24] = (uint8_t)r1(s);
    it[25] = p[56], it[26] = p[57], it[27] = p[42];
}

/* 0x4CA9B: the hospital with the most deaths, when this death is its own. The next free slot is cleared first, item or
 * not (docs/news-generators.md section 9.4 row 5). */
static void most_deaths(Sim *s, int h) {
    int slot = free_slot(s);
    if (slot < 0) return;
    sim_news_clear(s, slot);
    int best = -1, most = 10;
    for (int k = 0; k < 16; k++) {
        int c = (uint16_t)get16(hospital(s, k) + 0x38);
        if (c > most) most = c, best = k;
    }
    int month = world_tail(s->world, TAIL_MONTH), day = world_tail(s->world, TAIL_DAY);
    if (best != h || most < (30 * (month - 1) + day) / 2 + 2) return;
    uint8_t *it = sim_news_new_item(s, slot, 2, 0x33, 3, -3, 4, h);
    it[21] = (uint8_t)r1(s), it[22] = (uint8_t)r1(s), it[23] = (uint8_t)r1(s);
    put16(it + 24, most);
}

/* 0x4C8D6: the person's name copied to the item's bytes 29 on up to the '$' (docs/mainloop.md, module H, deaths).
 * The copy is taken before the rename of the dead (0x3200C), so the page prints the name the person died with.
 * Engine reading: the '$' is copied too, so stale bytes of the slot's previous item end there; engine guard: a name
 * longer than the 40 bytes up to the item's end is cut. Without the name pool (NULL) the copy stays empty. */
static void name_copy(Sim *s, uint8_t *it, int index) {
    size_t n = 0;
    const uint8_t *name = s->people ? datadb_pool_entry(s->people, DAT_PATIENT_NAMES, index, &n) : NULL;
    if (n > NEWS_SIZE - 30) n = NEWS_SIZE - 30;
    if (n) memcpy(it + 29, name, n);
    it[29 + n] = '$';
}

/* 0x4C904: a death booked against hospital h (before the person is reset) */
void sim_news_death(Sim *s, int h, int index) {
    int slot = free_slot(s);
    if (slot < 0) return;
    const uint8_t *p = person(s, index);
    uint8_t *it;
    if ((int8_t)p[3] < 0) {
        it = sim_news_new_item(s, slot, 2, 7, 13, -3, 4, h);
        it[N_PARAM] = (uint8_t)r3(s);
        it[22] = (uint8_t)r1(s);
    } else {
        it = sim_news_new_item(s, slot, 2, 8, 13, -2, 4, h);
        it[N_PARAM] = (uint8_t)r3(s);
        it[22] = room_counts(s, h)[18] && p[4] == 0x12 ? p[66] : p[4];
        it[23] = (uint8_t)r3(s);
    }
    put16(it + 16, index);
    name_copy(s, it, index);
    most_deaths(s, h);
}

/* The walk of 0x4C904 when 0x70321 hands it intensive-care record k of hospital h (docs/sim-rooms2.md, "The death item
 * of 0x70321 without a pathology place", item 2): the record lies D = -100,160 + 1,860 h + 186 k bytes from the person
 * table (always even), the walk ends at c = (D / 2) x 53^-1 mod 2^31, and the name copy reads its pool offset at
 * offset a = 140,266 + 4 c from the person table, linear 0x1CD948 + a (all mod 2^32). */
IcuDeathWalk sim_news_icu_walk(int h, int k) {
    uint32_t d = (uint32_t)(-100160 + 1860 * h + 186 * k), inv = 53;
    for (int i = 0; i < 5; i++) inv *= 2u - 53u * inv; /* 53^-1 mod 2^32 (Newton), hence also mod 2^31 */
    IcuDeathWalk w;
    w.c = ((d >> 1) * inv) & 0x7FFFFFFFu;
    w.offset = 140266u + 4u * w.c;
    w.linear = 0x1CD948u + w.offset;
    return w;
}

/* The name copy of that item (item 4): v = the pool offset the original reads, -1 (`FFFFFFFF`) above its RAM (157
 * records) and the live staff dword for the 3 records whose dword lies below the RAM's end, all three in the staff
 * table (no record of the 160 reaches other memory; test_news checks every record). -1 copies a 0 byte and the pool's
 * first entry, an offset inside the pool the entry there, anything else is the departure where the original crashes
 * (DEP-2): an empty name. Each copy runs up to and including the first '$'; engine guard as in the normal copy: cut at
 * the item's end. Engine guard: the staff read stays inside the staff block. */
static void icu_name_copy(Sim *s, uint8_t *it, IcuDeathWalk w) {
    enum { STAFF_AT = 0x1A1CA, STAFF_BYTES = 1041 * 32 };
    const TextPool *pool = s->people ? &s->people->pools[DAT_PATIENT_NAMES - DAT_PATIENT_NAMES] : NULL;
    uint32_t v = 0xFFFFFFFFu;
    if (w.linear < 0x1000000u && w.offset - STAFF_AT <= STAFF_BYTES - 4u)
        v = (uint32_t)get32(s->world->block[BLK_STAFF] + (w.offset - STAFF_AT));
    int at = 29;
    const uint8_t *src = NULL;
    size_t left = 0;
    if (v == 0xFFFFFFFFu) {
        it[at++] = 0;
        if (pool) src = pool->text, left = pool->text_size;
    } else if (pool && v < pool->text_size) {
        src = pool->text + v, left = pool->text_size - v;
    }
    for (size_t i = 0; i < left && at < NEWS_SIZE - 1; i++)
        if ((it[at++] = src[i]) == '$') return;
    it[at] = '$';
}

/* 0x4C904 as 0x70321 calls it in an AI hospital without a pathology place: with intensive-care record `rec` (k of
 * hospital h) in place of the person (docs/sim-rooms2.md, "What the engine must change (SIM-18)", items 3 and 4). */
void sim_news_icu_death(Sim *s, int h, int k, const uint8_t *rec) {
    int slot = free_slot(s);
    if (slot < 0) return;
    IcuDeathWalk w = sim_news_icu_walk(h, k);
    uint8_t *it;
    if ((int8_t)rec[3] < 0) {
        it = sim_news_new_item(s, slot, 2, 7, 13, -3, 4, h);
        it[N_PARAM] = (uint8_t)r3(s);
        it[22] = (uint8_t)r1(s);
    } else {
        it = sim_news_new_item(s, slot, 2, 8, 13, -2, 4, h);
        it[N_PARAM] = (uint8_t)r3(s);
        it[22] = room_counts(s, h)[18] && rec[4] == 0x12 ? rec[0x42] : rec[4];
        it[23] = (uint8_t)r3(s);
    }
    put16(it + 16, (int)(w.c & 0xFFFF));
    icu_name_copy(s, it, w);
    most_deaths(s, h);
}

/* 0x4CE7A: +0x112 reached 40 in a bed ward (docs/news-generators.md section 9.3 item 1, SIM-30). It clears nothing:
 * bytes 1-3, 7, 9-15 and 18-19 keep the slot's old header (issue byte, age, issue place), so in the slot of a deleted
 * printed item it is never selected and the next ageing clears the issue place its byte 7 names. u16 16 = CX after the
 * duplicate test 0x49636, 0 unless a 0x1A item about the hospital stands behind another one; engine: the duplicate
 * test, which practically never skips, is left out, so CX is 0. */
void sim_news_long_stay(Sim *s, int h) {
    int slot = gate(s, 90);
    if (slot < 0) return;
    uint8_t *it = sim_news_item(s, slot);
    it[N_TYPE] = 0x1A, it[N_WEIGHT] = 13, it[N_CHANGE] = (uint8_t)(int8_t)-2, it[N_CHANCE] = 6, it[N_HOSP] = (uint8_t)h;
    put16(it + 16, 0);
    it[21] = (uint8_t)r1(s), it[22] = (uint8_t)r1(s);
    it[N_TAG] = 0;
    sim_news_commit(s, slot, 2);
}

/* 0x4CDAC: a meal of kind `kind` (its byte 4) was eaten by patient `index` in a ward of hospital h. b21 is the kind
 * (3 for 3 or more) and u16 14 is [0x5B71], the id of the meal just eaten (docs/mainloop.md, module H item 7; parity
 * SIM-43). */
void sim_news_comfort(Sim *s, int h, int index, int kind, int meal) {
    /* SIM-111: both the nested gate and payload use the local meal-catalog context. */
    SimIn old = sim_set_inputs(s, sim_in(s->rng_in.on ? s->rng_in.d : s->clock_d,
                                       program_at(s->prog, 0x144A) + 20u * (uint32_t)meal, s->rng_scratch));
    int slot = gate_draw_inputs(s, 50, 90, s->rng_in);
    if (slot < 0) goto done;
    for (int i = 0; i < NEWS_ITEMS; i++) { /* 0x49668: only the first item in use is compared */
        const uint8_t *first = sim_news_item(s, i);
        if (!first[N_CLASS]) continue;
        if (first[N_TYPE] == 0x1D) goto done;
        break;
    }
    static const struct { uint8_t w, b6; int8_t d; } K[4] = {{11, 5, -1}, {6, 2, -1}, {6, 2, 0}, {9, 4, 1}};
    int k = kind > 3 ? 3 : kind;
    uint8_t *it = sim_news_new_item(s, slot, 1, 0x1D, K[k].w, K[k].d, K[k].b6, h);
    it[N_PARAM] = (uint8_t)k;
    put16(it + 14, meal);
    put16(it + 16, index);
    it[22] = (uint8_t)r3(s);
done:
    sim_set_inputs(s, old);
}

/* 0x4763B: called by the AI pricing after +0x109 was counted and before +0x10C takes the new index. The gate (no draw)
 * comes first, then the clear, byte 8 and b20; the b21 = 1 case outside hours 4-18 leaves that in the free slot
 * (docs/news-generators.md section 9.4 row 6). */
void sim_news_price(Sim *s, int h) {
    const uint8_t *r = hospital(s, h);
    int hour = world_tail(s->world, TAIL_HOUR);
    int slot = gate(s, 90);
    if (slot < 0) return;
    uint8_t *it = sim_news_clear(s, slot);
    it[N_HOSP] = (uint8_t)h, it[N_TYPE] = 0x16;
    if (r[0x109] < 5 && (uint16_t)get16(r + 0x10A) >= (uint16_t)get16(r + 0x10C) && (hour < 4 || hour > 18)) return;
    if (r[0x109] >= 5) it = sim_news_new_item(s, slot, 2, 0x16, 11, -2, 6, h), it[N_PARAM] = 0;
    else if ((uint16_t)get16(r + 0x10A) < (uint16_t)get16(r + 0x10C)) it = sim_news_new_item(s, slot, 2, 0x16, 10, -2, 4, h), it[N_PARAM] = 2;
    else it = sim_news_new_item(s, slot, 2, 0x16, 8, 1, 3, h), it[N_PARAM] = 1;
    it[22] = (uint8_t)r1(s);
}

/* 0x495A0 (docs/sim-staff2.md, "0x495A0"): employee `index` of hospital h finished the course with catalog id
 * `course`. Gate 0x42DD5 with limit 90 (no draw); then r1, and for the player's hospital a 0 ends it (AI hospitals
 * draw too, the value does not matter). Class 1, type 0x1F, w 8, d +1, b6 3. The copy-protection hook 0x4A35A after
 * it is left out (docs/mainloop.md, module H). */
void sim_news_course(Sim *s, int h, int index, int course) {
    int slot = gate(s, 90);
    if (slot < 0) return;
    if (!r1(s) && h == 0) return;
    uint8_t *it = sim_news_new_item(s, slot, 1, 0x1F, 8, 1, 3, h);
    s->dup_byte = (uint8_t)course; /* the subject's +4 goes through [0x45CF] (SIM-55; engine reading: with the item) */
    it[N_PARAM] = (uint8_t)course;
    it[22] = (uint8_t)r1(s);
    put16(it + 16, index);
}

/* 0x4E58A (docs/news-generators.md, section 8): hospital h's advertising campaign ended in this minute, +0xFA = -1
 * (normal end) or -2 (scandal), +0x106 / +0x108 already set. Gate 85, no draw; class 2, w 11, d 0, b6 0, byte 8 = h;
 * u10 = 2 x (positive AND 255) - negative (the low byte of one count, the whole word of the other, 16 bits, as the
 * original: 512 x k too small from 256 positive reactions on); b21 = the end, b22 / b23 = type and slogan, b24-b26 the
 * three counts' low bytes, b28 = 0. The campaign fields stay. */
void sim_news_campaign(Sim *s, int h) {
    int slot = gate(s, 85);
    if (slot < 0) return;
    const uint8_t *r = hospital(s, h);
    uint8_t *it = sim_news_new_item(s, slot, 2, 0x15, 11, 0, 0, h);
    put16(it + 10, (int)(uint16_t)(2 * r[0xFC] - (uint16_t)get16(r + 0x100)));
    it[N_PARAM] = (uint8_t)(get16(r + 0xFA) != -1);
    it[22] = r[0xF4], it[23] = r[0xF5];
    it[24] = r[0xFC], it[25] = r[0xFE], it[26] = r[0x100];
}

/* 0x4CB5E: an AI hospital is closed by the reputation rule 0x19A73 */
void sim_news_closure(Sim *s, int h) {
    int slot = free_slot(s);
    if (slot < 0) return;
    uint8_t *it = sim_news_new_item(s, slot, 2, 0x34, 11, -3, 4, h);
    it[21] = (uint8_t)r1(s), it[22] = (uint8_t)r1(s);
}

/* The thug outcomes of 0x4A822 (docs/mainloop.md, module H item 5). Type 0x0B: 0x4C244 b21 0, w 9, d -1, b6 4, gate
 * 80; 0x4C1BF b21 1, w 6, d -1, b6 2, gate 70; both b28 = 1, byte 9 = the victim employee's hospital (b21 1 only;
 * engine: 0 for b21 0), u16 10 / 12 = victim and thug, byte 22 = the mission's byte (slot +0x0F), byte 23 = r3.
 * Type 6 (both writers): gate 30, w 3, d 0, b6 0, b28 = 1, b22 = the mission byte, u16 10 = the victim, nothing in byte
 * 9 or u16 12; 0x4C165 (b21 1, the beaten employee): byte 8 = the victim's hospital (its staff byte 3 now); 0x4C2AE
 * (b21 0, the delivered person): byte 8 = h, the loop hospital (the thug's). */
void sim_news_thug(Sim *s, int type, int b21, int h, int victim_h, int victim, int thug, int param) {
    int lost = type == 0x0B;
    int slot = gate(s, !lost ? 30 : b21 == 1 ? 70 : 80);
    if (slot < 0) return;
    uint8_t *it;
    if (!lost) it = sim_news_new_item(s, slot, 2, 6, 3, 0, 0, b21 == 1 ? victim_h : h);
    else if (b21 == 0) it = sim_news_new_item(s, slot, 2, 0x0B, 9, -1, 4, h);
    else it = sim_news_new_item(s, slot, 2, 0x0B, 6, -1, 2, h);
    it[N_PARAM] = (uint8_t)b21;
    put16(it + 10, victim);
    it[22] = (uint8_t)param;
    it[N_TAG] = 1;
    if (lost) {
        if (b21 == 1) it[9] = (uint8_t)victim_h;
        put16(it + 12, thug);
        it[23] = (uint8_t)r3(s);
    }
}

/* 0x46CC4, called by the room creation 0x1DE67 (AI and player) after the count c[cat] + 1: the department item
 * (docs/mainloop.md, module H routine table, and docs/sim-ai.md, build step 5): skipped for categories 0-4, 12 and
 * 14; gate 40; class 2, w 4, d +1, b6 3, byte 8 = the current hospital [0xB054] (the creator, h), b21 = the category,
 * b22 = BL, the instance (engine reading: the new room's record index, the count before + 1, so 0 for the first
 * room, which the builder prints as a new department; docs/ui-reports.md, type 0x0E), b23 / b24 = r1 each, b28 = 0. */
void sim_news_new_room(Sim *s, int h, int cat, int instance) {
    if (cat <= 4 || cat == 12 || cat == 14) return;
    int slot = gate(s, 40);
    if (slot < 0) return;
    uint8_t *it = sim_news_new_item(s, slot, 2, 0x0E, 4, 1, 3, h);
    it[N_PARAM] = (uint8_t)cat;
    it[22] = (uint8_t)instance;
    it[23] = (uint8_t)r1(s), it[24] = (uint8_t)r1(s);
}

/* ---- scheduled generators ---------------------------------------------------------------------------- */

/* 0x625D3 (lowest index below the average) and 0x62728 (highest above it, +0x109 below 4). Engine choice: both
 * consider the hospitals in business (+0x15), "share the index" among those. */
static void price_compare(Sim *s, int highest) {
    int hour = world_tail(s->world, TAIL_HOUR);
    if (hour < 5 || hour > 19) return;
    world_price_index(s->world, s->prog);
    unsigned sum = 0;
    for (int h = 0; h < 16; h++) sum += (uint16_t)get16(hospital(s, h) + 0x10A);
    int avg = (int)((sum & 0xFFFF) / 16);
    int slot = highest ? sim_news_gate_draw(s, 150, 50) : sim_news_gate_draw(s, 190, 60);
    if (slot < 0) return;
    int pick = -1, best = 0;
    for (int h = 0; h < 16; h++) {
        const uint8_t *r = hospital(s, h);
        int v = (uint16_t)get16(r + 0x10A);
        if (!r[0x15]) continue;
        if (highest ? (r[0x109] < 4 && v > avg && (pick < 0 || v > best)) : (v < avg && (pick < 0 || v <= best)))
            pick = h, best = v;
    }
    if (pick < 0 || exists(s, 0x16, pick, -1)) return;
    for (int h = 0; h < 16; h++)
        if (h != pick && hospital(s, h)[0x15] && (uint16_t)get16(hospital(s, h) + 0x10A) == best) return;
    uint8_t *it = highest ? sim_news_new_item(s, slot, 2, 0x16, 8, 2, 4, pick) : sim_news_new_item(s, slot, 2, 0x16, 9, -2, 5, pick);
    it[N_PARAM] = (uint8_t)(highest ? 3 : 4);
    it[22] = (uint8_t)r1(s); /* after b28 (docs/news-generators.md section 9.3 item 2, SIM-30): the headline */
}

/* 0x515CB / 0x5176C: the championship leaders, only while the race word [0x552A] is 0; the gate is drawn first and
 * the word tested after it (docs/sim-drift6.md section 4) */
static int race_word(Sim *s) { return get16(s->world->block[BLK_34]) != 0; }
static void contest_hospitals(Sim *s) {
    int slot = sim_news_gate_draw(s, 300, 95);
    if (race_word(s) || slot < 0) return;
    const uint8_t *pts = s->world->block[34]; /* [0x5C71], save block 35 */
    uint8_t order[16];
    for (int i = 0; i < 16; i++) order[i] = (uint8_t)i;
    for (int swapped = 1; swapped;) {
        swapped = 0;
        for (int i = 0; i < 15; i++)
            if ((uint16_t)get16(pts + 2 * order[i]) < (uint16_t)get16(pts + 2 * order[i + 1])) {
                uint8_t t = order[i];
                order[i] = order[i + 1], order[i + 1] = t, swapped = 1;
            }
    }
    int a = (uint16_t)get16(pts + 2 * order[0]), b = (uint16_t)get16(pts + 2 * order[1]);
    if (a < 21) return;
    int day = world_tail(s->world, T_CONTEST_DAY);
    uint8_t *it;
    if (day < 25) {
        it = sim_news_new_item(s, slot, 2, 0x24, 15, 1, 2, order[0]);
        it[N_PARAM] = 0;
    } else {
        if (tail(s)[T_CONTEST_DONE_A]) { /* the season's last item was written: part of one stays (section 9.4 row 3) */
            it = sim_news_clear(s, slot);
            it[N_TYPE] = 0x24, it[N_PARAM] = 0, it[N_CHANGE] = 1, it[N_CHANCE] = 2;
            return;
        }
        it = sim_news_new_item(s, slot, 2, 0x24, 15, 2, 4, order[0]);
        it[N_PARAM] = 1;
        tail(s)[T_CONTEST_DONE_A] = 100;
    }
    it[9] = order[1];
    put16(it + 10, a), put16(it + 12, b), put16(it + 14, day);
}

static void contest_entries(Sim *s) {
    int slot = sim_news_gate_draw(s, 300, 95);
    if (race_word(s) || slot < 0) return;
    uint8_t *e = s->world->block[35]; /* [0x5C75], save block 36: 100 entries of 6 bytes, sorted in place */
    for (int swapped = 1; swapped;) {
        swapped = 0;
        for (int i = 0; i < 99; i++)
            if ((uint16_t)get16(e + 6 * i + 4) < (uint16_t)get16(e + 6 * i + 10)) {
                uint8_t t[6];
                memcpy(t, e + 6 * i, 6), memcpy(e + 6 * i, e + 6 * i + 6, 6), memcpy(e + 6 * i + 6, t, 6);
                swapped = 1;
            }
    }
    if ((uint16_t)get16(e + 4) < 21) return;
    uint8_t *it;
    int day = world_tail(s->world, T_CONTEST_DAY);
    if (day < 25) {
        it = sim_news_new_item(s, slot, 2, 0x25, 15, 1, 3, get16(e + 2));
        it[N_PARAM] = 0;
    } else {
        if (tail(s)[T_CONTEST_DONE_B]) { /* as the team leaders' (section 9.4 row 3): w 15, b6 stays 0 */
            it = sim_news_clear(s, slot);
            it[N_TYPE] = 0x25, it[N_PARAM] = 0, it[N_CHANGE] = 1, it[N_WEIGHT] = 15;
            return;
        }
        it = sim_news_new_item(s, slot, 2, 0x25, 15, 2, 3, get16(e + 2)); /* b6 3 in both branches (section 9.3) */
        it[N_PARAM] = 1;
        tail(s)[T_CONTEST_DONE_B] = 100;
    }
    it[9] = (uint8_t)get16(e + 8);
    put16(it + 10, get16(e + 4)), put16(it + 12, get16(e + 10));
    put16(it + 14, day); /* [0x6979], the contest day, in both branches (section 9.3 item 3, SIM-30) */
    put16(it + 16, get16(e)), put16(it + 18, get16(e + 6));
}

/* 0x55F4A: the race result (type 0x23, class 2, w 14, d +2, b6 4, gate 95). The winner's races won +0x160 + 1 is
 * counted whether or not the gate lets the item through (engine reading). */
void sim_news_race(Sim *s, int winner, int second, int driver1, int driver2, int model, int p) {
    int slot = gate(s, 95);
    if (slot < 0) return; /* a refused item counts no race won (docs/ui-garage.md, finish step 2) */
    if (winner >= 0 && winner < 16) add16(hospital(s, winner) + 0x160, 1);
    uint8_t *it = sim_news_new_item(s, slot, 2, 0x23, 14, 2, 4, winner);
    it[N_PARAM] = 0; /* b21 = 0 right after the type (0x55F77, SIM-66) */
    it[9] = (uint8_t)second;
    put16(it + 10, driver1), put16(it + 12, driver2);
    it[22] = (uint8_t)model;
    put16(it + 16, p);
}

/* 0x4C597: a hospital's price key, as a headline */
static void price_key(Sim *s) {
    int slot = sim_news_gate_draw(s, 50, 50);
    if (slot < 0) return;
    sim_news_clear(s, slot); /* right after the gate, item or not (section 9.4 row 4) */
    int h = (int)sim_random(s, 16);
    uint8_t *r = hospital(s, h);
    add32(r + 0x16E, 1);
    if (get32(r + 0x16E) >= 1000) put32(r + 0x16E, 0), add16(r + 0x172, 1);
    if (!r[0x15]) return;
    /* s = r3 first, then k = r7 (one step, no redraw); the item when s + 2 < v, signed bytes (SIM-52) */
    int sv = (int)sim_bits_site(s, 3, 0x4C603);
    int o = (uint16_t)program_word(s->prog, 0x5516, 2 * sim_bits_site(s, 7, 0x4C60E));
    int v = (int8_t)r[o];
    if (sv + 2 >= v) return;
    if (o == 0x20 && !room_counts(s, h)[6] && !room_counts(s, h)[16]) return;
    static const struct { uint8_t w, b6, d; } V[5] = {{7, 3, 1}, {9, 5, 1}, {12, 4, 2}, {14, 6, 2}, {16, 5, 3}};
    int k = v >= 7 ? 4 : v - 3;
    uint8_t *it = sim_news_new_item(s, slot, 1, 0x27, V[k].w, V[k].d, V[k].b6, h);
    it[N_PARAM] = (uint8_t)o;
    it[22] = (uint8_t)v;
}

/* 0x46D40 (T18): the dearest price key, type 0x26 (docs/mainloop.md, module H item 11a), the counterpart of 0x4C597.
 * The original first counts +0x16E / +0x172 (0x2B47B) of the hospital record [0x45F0] still points at: that is the
 * counter the copy protection watches (docs/mainloop.md, section 7), so it is left out here entirely. */
static void dearest_price_key(Sim *s) {
    int slot = sim_news_gate_draw(s, 50, 50);
    if (slot < 0) return;
    sim_news_clear(s, slot); /* right after the gate, item or not (section 9.4 row 4) */
    int h = (int)sim_random(s, 16);
    const uint8_t *r = hospital(s, h);
    if (!r[0x15]) return;
    int o = (uint16_t)program_word(s->prog, 0x5506, 2 * sim_bits(s, 7));
    if (r[o] != 1) return; /* exactly 1: one unit per Luemmel, the dearest setting */
    const uint8_t *c = room_counts(s, h);
    if (o == 0x20 && !(uint8_t)(c[6] + c[16])) return; /* a Krankenzimmer or neuro ward */
    if (o == 0x21 && !c[9]) return; /* never yielded by the table */
    if (o == 0x22 && !(uint8_t)(c[7] + c[8])) return; /* an OP or torture chamber */
    uint8_t *it = sim_news_new_item(s, slot, 1, 0x26, 8, -1, 3, h);
    it[N_PARAM] = (uint8_t)o;
}

/* 0x47EE3: the best employee of a random job group, as a headline */
static void best_employee(Sim *s) {
    int slot = sim_news_gate_draw(s, 124, 50);
    if (slot < 0) return;
    int g = (int)sim_random(s, 10) + 1, pick = 0;
    long best = 0;
    for (int i = 1; i <= STAFF_LAST; i++) {
        const uint8_t *st = s->world->block[BLK_STAFF] + i * STAFF_SIZE;
        if ((int8_t)st[3] < 0 || st[1] != g) continue;
        long v = 16L * (st[2] + st[12] + st[13]) + (uint16_t)get16(st + 16) + 4L * st[14] +
                 (st[0] == 0 ? st[15] : st[5] - st[6] + st[7]);
        if (!pick || v >= best) best = v, pick = i;
    }
    if (!pick) return;
    uint8_t *it = sim_news_new_item(s, slot, 1, 0x12, 2, 1, 1, s->world->block[BLK_STAFF][pick * STAFF_SIZE + 3]);
    it[N_PARAM] = (uint8_t)g;
    put16(it + 10, pick);
}

int sim_news_run(Sim *s, uint32_t routine) {
    switch (routine) {
    case 0x42C1D: opinion_effect(s); return 1;
    case 0x46E0C: fill_issue(s); return 1;
    case 0x474BC: age_items(s); return 1;
    case 0x625D3: price_compare(s, 0); return 1;
    case 0x62728: price_compare(s, 1); return 1;
    case 0x515CB: contest_hospitals(s); return 1;
    case 0x5176C: contest_entries(s); return 1;
    case 0x4C597: price_key(s); return 1;
    case 0x46D40: dearest_price_key(s); return 1;
    case 0x47EE3: best_employee(s); return 1;
    default: return sim_news_gen_run(s, routine);
    }
}
