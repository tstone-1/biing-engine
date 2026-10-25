/* Module B, patients, from docs/sim-patients.md. The cool-down 0x2EE24 and the copy-protection
 * hooks are left out; the reset 0x30DE6 is module C's, the newspaper items (0x494CB, 0x4C904, 0x4CB5E) module H's. */
#include <string.h>

#include "sim_internal.h"

#define PATIENTS 1009
#define PATIENT_SIZE 106
#define ILLNESSES 0x954
#define ILLNESS_SIZE 20
#define TREATMENTS 0x270C
#define TREATMENT_SIZE 38
#define WISHES 0x2066
#define VARIANTS 0x607F

/* patient fields (docs/formats.md, Block A, and docs/sim-patients.md) */
enum {
    P_SEX = 0, P_PROFESSION = 1, P_HOSPITAL = 3, P_STATE = 4, P_AGE = 8, P_BUST = 5, P_HISTORY = 11, P_OPINION = 20,
    P_FIELD40 = 40, P_AILMENT = 41, P_TREATMENT = 43, P_HEALTH = 44, P_UNITS = 48, P_ILLNESS = 56, P_WISH = 57,
    P_FIELD66 = 66, P_HEALTH_HISTORY = 67, P_MOOD = 78, P_IN_TREATMENT = 79, P_VARIANT = 80, P_TREATED_BY = 81,
    P_RANK = 82, P_VARIANT_HISTORY = 84, P_BLOOD = 94, P_FIELD100 = 100, P_FIELD101 = 101, P_FIELD102 = 102,
    P_ROUNDS = 103, P_DEAD = 104, P_COUNTDOWN = 105,
};

static uint8_t *person(Sim *s, int i) { return s->world->block[BLK_PATIENTS] + i * PATIENT_SIZE; }
static int random_person(Sim *s) { return (int)sim_random(s, 1008) + 1; }
static const uint8_t *illness(Sim *s, int id) { return program_table(s->prog, ILLNESSES) + id * ILLNESS_SIZE; }
static const uint8_t *treatment(Sim *s, int id) { return program_table(s->prog, TREATMENTS) + id * TREATMENT_SIZE; }
static const uint8_t *wish(Sim *s, int id) { return program_table(s->prog, WISHES) + id * 4; }
static int image_byte(Sim *s, uint32_t table, uint32_t index) { return program_u8(s->prog, table, index); }
static uint8_t *player_block(Sim *s, int h) { return s->world->block[h ? BLK_PLAYER1 : BLK_PLAYER0]; }

static void bump_activity(Sim *s, int h) { /* +0x16E, rolling over into +0x172 at 1,000 */
    uint8_t *r = hospital(s, h);
    add32(r + 0x16E, 1);
    if (get32(r + 0x16E) >= 1000) {
        put32(r + 0x16E, 0);
        add16(r + 0x172, 1);
    }
}

/* 0x2D6B4 */
static int set_health(uint8_t *p, int v) {
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    memmove(p + P_HEALTH_HISTORY + 1, p + P_HEALTH_HISTORY, 10);
    p[P_HEALTH_HISTORY] = p[P_HEALTH];
    p[P_HEALTH] = (uint8_t)v;
    return v;
}

static void refill_health(Sim *s, uint8_t *p) { /* 11 times health = 70 + r15 */
    for (int i = 0; i < 11; i++) set_health(p, 70 + (int)sim_bits(s, 15));
}

/* ---- 0x6953D illness onset and 0x69BC7 ---------------------------------------------------------- */

static int free_beds(Sim *s, int cat, int *count) { /* 0x2B852 / 0x2B8DC over hospitals 0-15 */
    int beds = 0, n = 0;
    uint8_t *base = s->world->block[cat == 6 ? 16 : 17];
    for (int h = 0; h < 16; h++) {
        int c = room_counts(s, h)[cat];
        n += c;
        for (int i = 0; i < c && i < 10; i++) {
            const uint8_t *rec = base + h * 2400 + i * 240;
            beds += get16(rec + 10) - get16(rec + 12);
        }
    }
    *count = n;
    return beds;
}

static int accept(int is_wish, int x, int y, int a, int b) {
    if (is_wish ? x == 0 : x != 0) return !a;
    if (!a) return 0;
    return y == 0 ? !b : b;
}

/* 0x69BC7 */
static void new_illness(Sim *s, uint8_t *p, int id) {
    const uint8_t *r = illness(s, id);
    p[P_TREATED_BY] = 0xFF;
    p[P_VARIANT] = 0;
    size_t at = (size_t)id * 4 + sim_bits(s, 3);
    p[P_AILMENT] = at < s->ailments_len ? s->ailments[at] : 0;
    if (r[17]) put16(p + P_BLOOD, get16(p + P_BLOOD) - (int)(sim_bits(s, 7) + 1) * r[17]);
    if (r[16] == 1) p[P_VARIANT] = (uint8_t)(sim_random(s, 5) + 1);
    else if (r[16] == 3) p[P_VARIANT] = (uint8_t)image_byte(s, VARIANTS, sim_bits(s, 7));
    else if (r[16] == 2) p[P_VARIANT] = (uint8_t)(sim_random(s, 4) + 1);
    int d = r[0] + (int)sim_bits(s, 15);
    p[P_HEALTH] = (uint8_t)(p[P_HEALTH] - d);
    p[P_MOOD] = (uint8_t)(45 - d < 0 ? 0 : 45 - d);
    int v = p[P_HEALTH];
    for (int i = 0; i < 11; i++) {
        v -= (int)sim_bits(s, 7); /* - r7 + r15 in this order (docs/sim-patients.md) */
        v += (int)sim_bits(s, 15);
        v = v < 30 ? 30 : v > 100 ? 100 : v;
        p[P_HEALTH_HISTORY + i] = (uint8_t)v;
    }
}

/* The onset's draws after the person pick (docs/sim-drift8.md section 18.3, docs/rng-sites.md 3.5): D = the low word of
 * the picked person's record, E = EBP + [0x4528], with EBP = the room-count row past hospital 15 (the address of
 * block 12's base, RNG_WAITING_ROOMS) until the first illness id has been drawn in this call, then the record of the
 * last drawn illness (the build's offset of the illness table + 20 id). Measured inputs, not uniform: with them type 2
 * is drawn about 5 % more often than 31/91. The new illness's own draws (0x69BC7) keep the default (not measured).
 * Log sites: the type draw under its return address 0x697ED (docs/rng-sites.md 6.1), the r3 and the illness r127
 * under the addresses docs/sim-drift8.md 18.3 names (0x698E9, 0x69913). */
typedef struct {
    uint32_t d, ebp;
} OnsetIn;
static SimIn onset_in(const Sim *s, const OnsetIn *o) { return sim_in(o->d, o->ebp, s->rng_scratch); }
static uint32_t onset_illness_record(const Sim *s, int id) {
    uint32_t a = s->prog ? build_at(s->prog->build, ILLNESSES) : ILLNESSES;
    return (a == PROGRAM_NOWHERE ? ILLNESSES : a) + (uint32_t)id * ILLNESS_SIZE;
}

static void onset(Sim *s) {
    World *w = s->world;
    int hour = world_tail(w, TAIL_HOUR), minute = world_tail(w, TAIL_MINUTE);
    int day = world_tail(w, TAIL_DAY), month = world_tail(w, TAIL_MONTH);
    if (hour >= 18 || !(hour >= 3 || (hour == 2 && minute >= 45))) return;
    if (world_tail(w, TAIL_HOURS) < 1 && world_tail(w, TAIL_HOURS + 2) == 0) return;
    int t;
    if (month == 1 && day >= 20) {
        t = 33;
    } else if (month == 1) {
        t = day + 12 + (hour == 9 ? 3 : 0) + (day >= 5 ? 3 : 0) + (day != 1 && hour == 14 ? 5 : 0) + (hour == 17 ? 3 : 0);
    } else {
        t = 4 * month + 30;
    }
    t += month == 1 ? (day >= 6) * 2 + (day >= 9) * 2 + (day >= 14) * 2 : 6;
    /* r127 below T, then the person: context S (docs/rng-sites.md 3.5; the share below T comes out of the generator,
     * replacing the share measured in docs/sim-drift4.md section 4) */
    if ((int)sim_bits_in(s, 127, 0x695A8, rng_scheduler(s)) >= t) return;
    uint8_t *p = person(s, (int)sim_random_in(s, 1008, 0x6968E, rng_scheduler(s)) + 1);
    if (p[P_DEAD] || !p[P_PROFESSION] || p[P_IN_TREATMENT] || p[P_AILMENT] || (int8_t)p[P_HOSPITAL] >= 0 ||
        p[P_WISH] || p[P_COUNTDOWN])
        return;
    int s5 = 0, s11 = 0, s15 = 0, s7 = 0, s8 = 0;
    for (int h = 0; h < 16; h++) {
        const uint8_t *c = room_counts(s, h);
        s5 += c[5], s11 += c[11], s15 += c[15], s7 += c[7], s8 += c[8];
    }
    sim_store_city(s); /* the six sums into [0x5BC3]-[0x5BD1], also when the onset stops here (SIM-51) */
    if (s5 + s11 + s15 == 0) return;
    int pos = 0;
    if (s5) memset(s->onset_types + pos, 0, 30), pos += 30;
    if (s11) memset(s->onset_types + pos, 1, 30), pos += 30;
    if (s15) memset(s->onset_types + pos, 2, 31), pos += 31;
    int n = s15 ? 91 : 90; /* fixed length: entries this call did not write keep older values */
    OnsetIn in = {sim_address(s, p), rng_waiting_rooms(s)};
    int type = s->onset_types[sim_random_in(s, (uint32_t)n, 0x697ED, onset_in(s, &in))];
    int a = 0, b = 0;
    if (type != 1) {
        int count, beds = free_beds(s, type == 0 ? 6 : 16, &count);
        int base = type == 0 ? s5 : s15, other = type == 0 ? s7 : s8;
        if (beds > 0) {
            int x = base + base / 4;
            a = (int)sim_random_in(s, (uint32_t)(x + count), 0, onset_in(s, &in)) >= x;
            /* SIM-45: the b draw only when A is set (docs/sim-patients.md, onset step 5) */
            if (a) b = (int)sim_random_in(s, (uint32_t)(count + other), 0, onset_in(s, &in)) >= count;
        }
    }
    /* the sexes run separate copies of the loop with their own sites (call addresses of the r3 and the illness r127,
     * return addresses of the wish r15; docs/sim-patients.md step 6, SIM-45) */
    int woman = p[P_SEX] != 0;
    uint32_t site_r3 = woman ? 0x69A1F : 0x698E9, site_ill = woman ? 0x69A49 : 0x69913;
    uint32_t site_wish = woman ? 0x69AF0 : 0x699A6;
    /* the original loops without a limit; the engine gives up after many draws */
    for (int guard = 0; guard < 100000; guard++) {
        if (sim_bits_in(s, 3, site_r3, onset_in(s, &in)) != 1) {
            uint32_t table = p[P_SEX] == 0 ? (p[P_AGE] < 30 ? 0x42E1 : p[P_AGE] < 55 ? 0x4361 : 0x43E1)
                                           : (p[P_AGE] < 25 ? 0x41E1 : 0x4261);
            for (; guard < 100000; guard++) {
                int id = image_byte(s, table + sim_bits_in(s, 127, site_ill, onset_in(s, &in)), 0);
                in.ebp = onset_illness_record(s, id); /* the following draws: the drawn illness's record */
                const uint8_t *r = illness(s, id);
                if (r[8] != type || !accept(0, r[4], r[1], a, b)) continue;
                p[P_ILLNESS] = (uint8_t)id;
                p[P_HEALTH] = (uint8_t)(p[P_HEALTH] - 1);
                new_illness(s, p, id);
                return;
            }
            return;
        }
        int wish_id, tries = 0;
        /* the redraw of wish 5 under one constant pair can fall into a short cycle of the generator that never
         * leaves wish 5 (as random_n's redraws, sim.c); engine choice: after 4,096 draws the rest takes fresh pairs */
#define WISH_IN (tries++ < 4096 ? onset_in(s, &in) : SIM_DEFAULT_IN)
        if (p[P_SEX] == 0) {
            uint32_t table = p[P_AGE] < 30 ? 0x4465 : 0x4475;
            do wish_id = image_byte(s, table + sim_bits_in(s, 15, site_wish, WISH_IN), 0);
            while (wish_id == 5 && s7 < 4);
        } else if (p[P_AGE] < 40) {
            wish_id = p[P_BUST] < 120 ? 2 : 3;
        } else {
            do wish_id = image_byte(s, 0x4495, sim_bits_in(s, 15, site_wish, WISH_IN));
            while (wish_id == 5 && s7 < 4);
#undef WISH_IN
        }
        if (type != 0) continue;
        if (!accept(1, wish(s, wish_id)[3], wish(s, wish_id)[1], a, b)) continue;
        p[P_WISH] = (uint8_t)wish_id;
        return;
    }
}

/* ---- 0x69CB6 patient arrival -------------------------------------------------------------------- */

static int arrival_candidate(const uint8_t *p) {
    return !p[P_DEAD] && p[P_PROFESSION] && !p[P_IN_TREATMENT] && (int8_t)p[P_HOSPITAL] < 0 &&
           (p[P_WISH] || (p[P_AILMENT] && p[P_HEALTH] != 100));
}

static const uint8_t *first_room(Sim *s, int h, int cat) {
    static const struct { int8_t block; uint16_t area; } R[19] = {
        [2] = {3, 174}, [5] = {15, 1800}, [6] = {16, 2400}, [7] = {18, 1296}, [8] = {19, 1296},
        [11] = {21, 1800}, [15] = {22, 1800}, [16] = {17, 2400},
    };
    return s->world->block[R[cat].block] + h * R[cat].area;
}

static int staffed(Sim *s, int h, int cat, int offsets) { /* room count and the listed u16 fields non-zero */
    if (!room_counts(s, h)[cat]) return 0;
    const uint8_t *r = first_room(s, h, cat);
    if ((offsets & 1) && !get16(r + 6)) return 0;
    if ((offsets & 2) && !get16(r + 8)) return 0;
    if ((offsets & 4) && !get16(r + 10)) return 0;
    return 1;
}

enum { CH_OP, CH_KZ, CH_TR, CH_N1, CH_N3, CH_WAIT, CH_DENTAL };

static int chain(Sim *s, int h, int c) {
    for (;;) switch (c) {
        case CH_OP: if (!staffed(s, h, 7, 7)) return 0; c = CH_KZ; break;
        case CH_KZ: if (!staffed(s, h, 6, 2)) return 0; c = CH_TR; break;
        case CH_TR: if (!staffed(s, h, 5, 6)) return 0; c = CH_WAIT; break;
        case CH_DENTAL: if (!staffed(s, h, 11, 6)) return 0; c = CH_WAIT; break;
        case CH_N1: if (!staffed(s, h, 16, 2)) return 0; c = CH_N3; break;
        case CH_N3: if (!staffed(s, h, 15, 6)) return 0; c = CH_WAIT; break;
        default: return room_counts(s, h)[3] != 0;
        }
}

static int department_ok(Sim *s, int h, const uint8_t *p) {
    if (!get16(first_room(s, h, 2) + 8)) return 0; /* reception staffed */
    if (p[P_WISH]) {
        const uint8_t *wd = wish(s, p[P_WISH]);
        return chain(s, h, wd[1] ? CH_OP : wd[3] ? CH_KZ : CH_TR);
    }
    if (!p[P_ILLNESS]) return 0; /* the original jumps into unrelated code here */
    const uint8_t *r = illness(s, p[P_ILLNESS]);
    switch (r[8]) {
    case 0: return chain(s, h, r[1] ? CH_OP : r[4] == 0 ? CH_KZ : CH_TR);
    case 1: return chain(s, h, CH_DENTAL);
    case 2:
        if (r[4]) return chain(s, h, CH_N3);
        if (r[1] && !staffed(s, h, 8, 6)) return 0;
        return chain(s, h, CH_N1);
    default: return 0;
    }
}

static void arrival(Sim *s) {
    int index = 0;
    uint8_t *p = NULL;
    /* the person tries (docs/rng-sites.md 3.4, 2.5): EBP the clock call's at every try, [0x4528] as its last writer
     * left it (2.4); D at the first try what the clock call passes (context S, docs/sim-drift8.md sections 13.3 and
     * 14), then the low word of the previous try's person record */
    uint32_t d = s->clock_d;
    for (int tries = 0; tries < 43 && !p; tries++) {
        int i = (int)sim_random_in(s, 1008, 0x69CE2, sim_in(d, s->clock_ebp, s->rng_scratch)) + 1;
        d = sim_address(s, person(s, i));
        if (arrival_candidate(person(s, i))) index = i, p = person(s, i);
    }
    if (!p) return;
    int hour = world_tail(s->world, TAIL_HOUR), health = p[P_HEALTH];
    if (health >= 70 && (hour < 4 || hour > 18)) return;
    if (health >= 85 && hour < 5) return;
    if (health >= 95 && hour < 7) return;
    p[P_FIELD100] = 0;
    int m = -1;
    for (int h = 0; h < 16; h++)
        if (room_counts(s, h)[2] && p[P_OPINION + h] > m) m = p[P_OPINION + h];
    if (m < 0) return;
    if (m >= 16) m -= 13;
    int h;
    /* step 4, the hospital: D = the low word of hospital record 16, EBP = block 12's base (docs/rng-sites.md 3.4),
     * [0x4528] as its last writer left it (2.4) */
    SimIn in = sim_in(rng_hospital16(s), rng_waiting_rooms(s), s->rng_scratch);
    /* engine choice: as in random(n) (sim.c), a generator state that repeats at the loop's head means a cycle under the
     * constant pair that may never leave; the rest of the loop then draws with fresh pairs */
    uint32_t mark = s->rng_s, steps = 0, limit = 1;
    for (;;) {
        if (in.on && steps && s->rng_s == mark) in.on = 0;
        if (++steps >= limit) mark = s->rng_s, limit <<= 1, steps = 0;
        h = (int)sim_random_in(s, 16, 0x69DFF, in);
        if (!room_counts(s, h)[2]) continue;
        bump_activity(s, h);
        if (sim_bits_in(s, 31, 0x69E40, in) == 5) break;
        if (sim_bits_in(s, 7, 0x69E4B, in) != 4 && p[P_OPINION + h] < m) continue;
        if (p[P_OPINION + h] < 20) continue;
        break;
    }
    uint8_t *queue = (uint8_t *)first_room(s, h, 2) + 10;
    int place = -1;
    for (int i = 0; i < 4 && place < 0; i++)
        if (!get16(queue + 4 * i)) place = i;
    if (place >= 0 && department_ok(s, h, p)) {
        p[P_HOSPITAL] = (uint8_t)h;
        p[P_STATE] = 2;
        p[P_FIELD66] = 0;
        put16(queue + 4 * place, index);
        put16(queue + 4 * place + 2, 0);
        memset(p + P_UNITS, 0, 8);
        if (h <= 1) player_block(s, h)[0x2A] = 0xFF;
        return;
    }
    /* not admitted: byte 0x14 + h of hospital record 16 drops when it is 70 or more (as the original) */
    uint8_t *odd = hospital(s, 16) + 0x14 + h;
    if ((int8_t)*odd >= 70) (*odd)--;
}

/* ---- 0x672A1 home treatment, 0x37BBE, 0x37D19 --------------------------------------------------- */

static void case_history(uint8_t *p) { /* 0x37D19 */
    if (!get16(p + P_ILLNESS) || p[P_WISH] == 4) return;
    memmove(p + P_HISTORY + 1, p + P_HISTORY, 8);
    p[P_HISTORY] = p[P_ILLNESS] ? p[P_ILLNESS] : 13;
    memmove(p + P_VARIANT_HISTORY + 1, p + P_VARIANT_HISTORY, 8);
    p[P_VARIANT_HISTORY] = p[P_ILLNESS] ? p[P_VARIANT] : p[P_WISH];
}

static void cure_reset(Sim *s, uint8_t *p) {
    case_history(p);
    p[41] = p[42] = p[43] = p[45] = 0;
    put16(p + 48, 0);
    memset(p + 50, 0, 8);
    p[P_COUNTDOWN] = p[P_IN_TREATMENT] = p[P_VARIANT] = p[P_TREATED_BY] = 0;
    p[P_RANK] = 0xFF;
    p[83] = p[93] = 0;
    memset(p + 96, 0, 8);
    int follow = s->last_treatment ? treatment(s, s->last_treatment)[34] : 0;
    if (follow) {
        p[P_WISH] = (uint8_t)follow;
        p[P_VARIANT] = p[P_VARIANT_HISTORY];
    }
}

/* 0x37BBE: one step with health change d; returns 0 when the person died. */
static int treatment_step(Sim *s, int index, int d) {
    uint8_t *p = person(s, index);
    int v = (int8_t)(uint8_t)(p[P_HEALTH] + d);
    if (v >= 1) {
        set_health(p, v);
        return 1;
    }
    if (p[P_ILLNESS] && illness(s, p[P_ILLNESS])[9] == 0) {
        refill_health(s, p);
        return 1;
    }
    p[P_HEALTH] = 0;
    p[P_DEAD] = 1;
    int8_t by = (int8_t)p[P_TREATED_BY];
    if (by >= 0 && by < 17) {
        /* The original zeroes the dying patient's opinion here when hospital record 1's +0x177 is set or +0x174 >= 3:
         * those bytes are hospital 0's manual-query state, so that is the copy protection's penalty and stays out
         * (docs/sim-bookkeeping.md, docs/sim-drift2.md; removed 2026-10-19). */
        uint8_t *r = hospital(s, by);
        int type = p[P_ILLNESS] ? illness(s, p[P_ILLNESS])[8] : 0;
        add16(r + 0x2A, 1);
        if (type == 1) add16(r + 0xEC, 1);
        if (type == 2) add16(r + 0xEE, 1);
        add16(r + 0x38, 1);
        if (by < 16) sim_news_death(s, by, index); /* 0x4C904 */
        if (!p[P_TREATMENT] || p[P_RANK] != 4) add16(r + 0x3C, 1);
        else add16(r + 0x3A, 1);
        if (p[P_FIELD100]) {
            if (type == 0) add16(r + 0x36, 1), add16(r + 0x12A, 1);
            else add16(r + 0x118, 1), add16(r + 0x12C, 1);
        }
        if (p[P_FIELD101] && p[P_RANK] == 4) add16(r + 0x3A, -1), add16(r + 0x3C, 1);
    }
    sim_rename_dead(s, index); /* 0x3200C, called at 0x37CF8 */
    sim_reset_healthy(p); /* 0x30DE6: the person lives on at home */
    return 0;
}

static void home_treatment(Sim *s) {
    for (int i = 1; i < PATIENTS; i++) {
        uint8_t *p = person(s, i);
        if (p[P_DEAD] || !p[P_PROFESSION] || !p[P_IN_TREATMENT] || (int8_t)p[P_HOSPITAL] >= 0) continue;
        int8_t by = (int8_t)p[P_TREATED_BY];
        /* [0x4528] (docs/rng-sites.md 2.4): 0, then the treating hospital's record when its byte is not negative */
        s->rng_scratch = by >= 0 ? RNG_HOSPITAL0 + s->rng_heap + 500u * (uint32_t)by : 0;
        uint8_t *hr = by >= 0 && by < 17 ? hospital(s, by) : NULL;
        if (p[P_ILLNESS]) s->last_illness = p[P_ILLNESS];
        if (p[P_TREATMENT]) s->last_treatment = p[P_TREATMENT];
        if (by >= 0) sim_bits(s, 127); /* the copy-protection hook's draw */
        if (p[P_WISH] == 5) {
            int blood = get16(p + P_BLOOD) + (int)sim_bits(s, 3);
            if (blood >= 700) {
                put16(p + P_BLOOD, 700);
                p[P_IN_TREATMENT] = 0;
                cure_reset(s, p);
                continue;
            }
            put16(p + P_BLOOD, blood);
        }
        if ((int8_t)p[P_RANK] < 0) {
            p[P_RANK] = 0;
            if (p[P_ILLNESS]) {
                const uint8_t *r = illness(s, p[P_ILLNESS]);
                for (int k = 0; k < 4; k++)
                    if (p[P_TREATMENT] == r[11 + k]) p[P_RANK] = (uint8_t)(4 - k);
            }
        }
        if (p[P_COUNTDOWN]) p[P_COUNTDOWN]--;
        if (!p[P_COUNTDOWN]) {
            p[P_ROUNDS]++;
            int type = s->last_illness ? illness(s, s->last_illness)[8] : 0;
            if (p[P_ROUNDS] >= 32 && p[P_HEALTH] != 100) { /* failure */
                if (hr) {
                    add16(hr + 0x2A, 1);
                    if (by < 16) sim_news_home_failure(s, i, type); /* 0x494CB */
                    /* SIM-39 (docs/sim-patients.md, "0x672A1", step 4): only with an illness of the person's own
                     * (field 56); type 1 -> +0xEC, any other non-zero type -> +0xEE */
                    if (p[P_ILLNESS] && type == 1) add16(hr + 0xEC, 1);
                    else if (p[P_ILLNESS] && type) add16(hr + 0xEE, 1);
                    if (p[P_FIELD100]) {
                        if (type == 0) add16(hr + 0x36, 1), add16(hr + 0x12A, 1);
                        else add16(hr + 0x118, 1), add16(hr + 0x12C, 1);
                    }
                    int o = p[P_OPINION + by] - 35;
                    p[P_OPINION + by] = (uint8_t)(o < 0 ? 0 : o);
                }
                put16(p + 42, 0);
                p[45] = 0;
                memset(p + 48, 0, 8);
                p[P_COUNTDOWN] = p[P_FIELD66] = p[P_IN_TREATMENT] = p[P_TREATED_BY] = 0;
                p[P_RANK] = 0xFF;
                p[93] = 0;
                memset(p + 96, 0, 8);
                refill_health(s, p);
                continue;
            }
            if (p[P_HEALTH] >= 100) continue;
            p[P_COUNTDOWN] = s->last_treatment ? treatment(s, s->last_treatment)[7] : 0;
            static const int STEP[5] = {-4, -2, -1, 1, 4};
            if (p[P_RANK] <= 4) treatment_step(s, i, STEP[p[P_RANK]]);
            if (!p[P_IN_TREATMENT]) continue;
        }
        if (p[P_HEALTH] < 100) continue; /* cure check */
        int type = s->last_illness ? illness(s, s->last_illness)[8] : 0;
        if (hr) {
            add16(hr + 0x28, 1);
            /* SIM-39 (step 5, 0x67541): the department counters only when field 56 is non-zero; a wish patient or a
             * blood donor keeps an earlier person's illness record loaded but counts no cure there */
            if (p[P_ILLNESS] && type == 2) add16(hr + 0x52, 1);
            if (p[P_ILLNESS] && type == 1) add16(hr + 0x54, 1);
        }
        p[P_MOOD] = (uint8_t)((p[P_MOOD] + 100) / 2);
        if (by >= 0 && by < 16) {
            int o = p[P_OPINION + by] + 11;
            p[P_OPINION + by] = (uint8_t)(o > 100 ? 100 : o);
        }
        cure_reset(s, p);
    }
}

/* ---- the smaller routines ----------------------------------------------------------------------- */

static void health_clamps(Sim *s) { /* 0x67ABD */
    for (int i = 1; i < PATIENTS; i++) {
        uint8_t *p = person(s, i);
        if ((int8_t)p[P_MOOD] > (int8_t)p[P_HEALTH]) p[P_MOOD] = p[P_HEALTH];
        if (p[P_ILLNESS] && (int8_t)illness(s, p[P_ILLNESS])[19] > (int8_t)p[P_HEALTH])
            p[P_HEALTH] = (uint8_t)(illness(s, p[P_ILLNESS])[19] + sim_bits(s, 3));
        int8_t h = (int8_t)p[P_HOSPITAL];
        if (h == 16 || h < 0) continue;
        if (p[P_DEAD]) p[P_OPINION + h] = 0;
        if (!get16(p + P_ILLNESS)) p[P_HEALTH] = 100;
        if (p[P_STATE] == 18) continue;
        if (p[P_DEAD]) {
            if (p[P_DEAD] != 100 && sim_bits(s, 7) == 3) p[P_DEAD] = (uint8_t)(p[P_DEAD] + (sim_bits(s, 3) + 1) / 4);
            continue;
        }
        if ((int8_t)p[P_STATE] >= 6 && p[P_STATE] != 11 && p[P_STATE] != 15) {
            /* SIM-84: the signed cap precedes the draw, including values above 100. */
            if ((int8_t)p[P_FIELD40] >= 100) p[P_FIELD40] = 100;
            else if (sim_bits(s, 15) == 4) p[P_FIELD40]++;
        } else {
            p[P_FIELD40] = 0;
        }
        if ((int8_t)p[P_FIELD102] >= 10) p[P_FIELD102] = (uint8_t)(p[P_FIELD102] - sim_bits(s, 1));
    }
}

static void hospital_countdown(Sim *s) { /* 0x30AD9, without its copy-protection part */
    for (int i = 1; i < PATIENTS; i++) {
        uint8_t *p = person(s, i);
        if (p[P_DEAD] || !p[P_PROFESSION] || p[P_HOSPITAL] > 15 || p[P_IN_TREATMENT] || p[P_STATE] == 6 ||
            p[P_STATE] == 16 || !p[P_COUNTDOWN])
            continue;
        p[P_COUNTDOWN]--;
    }
}

static void reputation_rule(Sim *s) { /* 0x19A73 */
    sim_opinion_sum(s);
    for (int h = 1; h < 16; h++) {
        uint8_t *r = hospital(s, h);
        uint32_t a = (uint32_t)get32(r + 0x44) / 1008;
        if (a >= 26 && a <= 29) {
            sim_news_closure(s, h); /* 0x4CB5E */
            put32(r + H_CASH, -20000);
            for (int i = 1; i < PATIENTS; i++) person(s, i)[P_OPINION + h] = 0;
        }
        if (get32(r + H_CASH) >= 0 && a < 43)
            for (int i = 1; i < PATIENTS; i++) {
                uint8_t *o = person(s, i) + P_OPINION + h;
                *o = (uint8_t)(*o >= 2 ? *o - 2 : 0);
            }
    }
}

static void reputation_catch_up(Sim *s) { /* 0x30B6B */
    int hour = world_tail(s->world, TAIL_HOUR);
    if (hour < 5 || hour > 20 || sim_bits(s, 31)) return;
    int p0 = get32(hospital(s, 0) + 0x44) / 1008 - (int)sim_bits(s, 1); /* one statement per draw */
    p0 -= (int)sim_bits(s, 1);
    for (int h = 1; h < 16; h++) {
        int a = get32(hospital(s, h) + 0x44) / 1008;
        if (a > p0 || a < 48) continue;
        uint8_t *o = person(s, random_person(s)) + P_OPINION + h;
        if (*o < 80) *o += 2;
        o = person(s, random_person(s)) + P_OPINION + h;
        if (*o < 80) *o += 1;
    }
}

static void units_clamp(Sim *s) { /* 0x50F3A */
    for (int i = 1; i < PATIENTS; i++)
        for (int k = 48; k <= 55; k++)
            if (person(s, i)[k] >= 100) person(s, i)[k] = 99;
}

static int city_has(Sim *s, const int *cats) {
    for (; *cats; cats++) {
        int any = 0;
        for (int h = 0; h < 16 && !any; h++) any = room_counts(s, h)[*cats] != 0;
        if (!any) return 0;
    }
    return 1;
}

static void untreatable(Sim *s) { /* 0x4CBAF */
    for (int tries = 0; tries < 50; tries++) {
        uint8_t *p = person(s, random_person(s));
        if (!arrival_candidate(p)) continue;
        static const int W5[] = {17, 5, 3, 0}, OPC[] = {7, 6, 5, 3, 0}, KZC[] = {6, 5, 3, 0}, TRC[] = {5, 3, 0},
                         DEN[] = {11, 3, 0}, N1T[] = {8, 16, 15, 3, 0}, N1[] = {16, 15, 3, 0}, N3[] = {15, 3, 0};
        const int *need;
        if (p[P_WISH]) {
            const uint8_t *wd = wish(s, p[P_WISH]);
            need = p[P_WISH] == 5 ? W5 : wd[1] ? OPC : wd[3] ? KZC : TRC;
        } else if (p[P_ILLNESS]) {
            const uint8_t *r = illness(s, p[P_ILLNESS]);
            need = r[8] == 0 ? (r[1] ? OPC : r[4] == 0 ? KZC : TRC) : r[8] == 1 ? DEN : (r[1] ? N1T : r[4] == 0 ? N1 : N3);
        } else {
            return;
        }
        if (city_has(s, need)) return;
        p[P_HOSPITAL] = p[P_STATE] = 0xFF;
        p[41] = 0;
        put16(p + 42, 0);
        p[P_HEALTH] = 100;
        p[45] = 0;
        memset(p + 48, 0, 8);
        put16(p + 56, 0);
        p[66] = 0;
        memset(p + 67, 100, 11);
        p[78] = 50, p[79] = 0, p[82] = 0xFF, p[83] = 0, p[93] = 0;
        put16(p + 94, 700);
        memset(p + 96, 0, 8);
        put16(p + 104, 0);
        return;
    }
}

static void opinion_drift(Sim *s) { /* 0x1AA17; opinions are signed bytes without an upper clamp here */
    World *w = s->world;
    int hour = world_tail(w, TAIL_HOUR), day = world_tail(w, TAIL_DAY), month = world_tail(w, TAIL_MONTH);
    if (hour < 6 || hour > 16 || (month == 1 && day < 2) || !room_counts(s, 0)[2]) return;
    uint8_t *p = person(s, random_person(s));
    if ((int8_t)p[P_HOSPITAL] >= 0) return;
    if ((int8_t)p[P_OPINION] >= 78) {
        int d = 1 + (month == 1 ? (day >= 6 ? 2 : 0) + (day >= 15 ? 1 : 0) : 3);
        p[P_OPINION] = (uint8_t)(p[P_OPINION] - d);
    }
    int h = (int)sim_random(s, 15) + 1;
    if (!hospital(s, h)[0x15]) return;
    uint8_t *o = p + P_OPINION + h;
    /* SIM-22 (docs/sim-patients.md step 4): 60 or more ends the routine; no r1, no Q */
    if ((int8_t)*o >= 60) return;
    *o = (uint8_t)(*o + 2 + sim_bits(s, 1)); /* one statement per draw */
    *o = (uint8_t)(*o + sim_bits(s, 1));
    if ((int8_t)*o >= 30) return;
    *o += 2;
    if (month == 1 && day < 6) return;
    *o += 2;
    if (month == 1 && day < 11) return;
    uint8_t *q = person(s, random_person(s));
    if (q[P_HOSPITAL] == 0) return;
    o = q + P_OPINION + h;
    if ((int8_t)*o >= 60) return;
    *o += 2;
    if (month == 1) {
        if (day < 12) return;
        *o += 2;
        if (day < 15 || (int8_t)*o < 20) return;
        *o += 4;
    } else {
        if ((int8_t)*o < 20) return;
        *o += 5;
    }
}

int sim_patients_run(Sim *s, uint32_t routine) {
    switch (routine) {
    case 0x6953D: onset(s); return 1;
    case 0x69CB6: arrival(s); return 1;
    case 0x672A1: home_treatment(s); return 1;
    case 0x67ABD: health_clamps(s); return 1;
    case 0x30AD9: hospital_countdown(s); return 1;
    case 0x19A73: reputation_rule(s); return 1;
    case 0x30B6B: reputation_catch_up(s); return 1;
    case 0x50F3A: units_clamp(s); return 1;
    case 0x4CBAF: untreatable(s); return 1;
    case 0x1AA17: opinion_drift(s); return 1;
    default: return 0;
    }
}
