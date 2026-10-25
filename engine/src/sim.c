#include "sim_internal.h"

#include "advice.h"
#include "report.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HOURS_PLAYED_MAX 51
/* Tail block words (program offset - 0x694B). */
#define T_LEAD_A_VALUE 0x26
#define T_LEAD_B_VALUE 0x28
#define T_LEAD_A_HOSP 0x2A
#define T_LEAD_B_HOSP 0x2C

/* ---- random numbers --------------------------------------------------------------------------- */

/* docs/mainloop.md section 3, "One draw": t = S with d added to its low 16 bits (no carry into the high half), + e,
 * XOR 0x5566DF45, the 16-bit halves swapped, + S; S = S + t; the result is t. */
uint32_t sim_generator_step(uint32_t *state, uint32_t d, uint32_t e) {
    uint32_t s0 = *state;
    uint32_t t = (s0 & 0xFFFF0000u) | ((s0 + d) & 0xFFFFu);
    t += e;
    t ^= 0x5566DF45u;
    t = t << 16 | t >> 16;
    t += s0;
    *state = s0 + t;
    return t;
}

/* The reseed's closed form (docs/mainloop.md section 3): c = the carry of the second addition, S = S + DX + CX,
 * a = 4 DX + 2 c, b = 4 CX, a = a + b, b = b XOR a, S = S + b. */
void sim_generator_reseed(uint32_t *state, uint32_t dx, uint32_t cx) {
    uint32_t s1 = *state + dx, s2 = s1 + cx;
    uint32_t c = s2 < s1;
    uint32_t a = 4 * dx + 2 * c, b = 4 * cx;
    a += b;
    b ^= a;
    *state = s2 + b;
}

void sim_reset_generator(Sim *s, uint64_t seed) {
    s->rng_s = SIM_GENERATOR_START;
    s->rng_scratch = 0; /* [0x4528] as the RAM dump holds it (docs/rng-sites.md 2.4) */
    s->rating_ref = (SimRatingRef){0, 0, SIM_RATING_WORLD}; /* the unsaved initial room context at a new game or load */
    s->rng_time = (uint32_t)(seed * 100003u); /* the oracle's o.ticks = seed x 100003 */
}

void sim_reseed(Sim *s) {
    uint32_t t = ++s->rng_time;
    uint32_t hours = t / 360000u % 24, minutes = t / 6000u % 60, seconds = t / 100u % 60, hundredths = t % 100;
    sim_generator_reseed(&s->rng_s, seconds * 256 + hundredths, hours * 256 + minutes);
}

SimIn sim_set_inputs(Sim *s, SimIn in) {
    SimIn old = s->rng_in;
    s->rng_in = in;
    return old;
}

/* the auxiliary generator (splitmix64): a fresh input pair per generator step for the default draws */
static SimIn fresh_inputs(Sim *s) {
    uint64_t z = (s->rng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    SimIn in = {1, (uint32_t)(z >> 32) & 0xFFFFu, (uint32_t)z};
    return in;
}

void sim_seed(Sim *s, uint64_t seed) {
    s->rng = seed;
    s->rng_s = SIM_GENERATOR_START ^ fresh_inputs(s).e;
    s->rng = seed;
}

static uint32_t draw(Sim *s, SimIn in) {
    SimScript *sc = s->rng_script;
    if (sc) { /* the scripted results (tools/diff_routines.py): S = S + t as the generator's own step leaves it */
        if (sc->used < sc->n) {
            uint32_t t = sc->v[sc->used++];
            s->rng_s += t;
            return t;
        }
        if (s->rng_failure) s->rng_failure(sc, "rng script exhausted");
        if (!sc->extra++) fprintf(stderr, "[WARN] rng script ran out after %zu draws\n", sc->used);
    }
    if (!in.on) in = fresh_inputs(s);
    return sim_generator_step(&s->rng_s, in.d & 0xFFFFu, in.e);
}

/* one trace line per draw call while a script is set: "<site> <b mask | r n> <value> <steps>" */
static void trace_draw(Sim *s, uint32_t site, char kind, uint32_t n, uint32_t v, size_t steps) {
    if (s->rng_script && s->rng_script->trace &&
        fprintf((FILE *)s->rng_script->trace, "%X %c%u %u %zu\n", site, kind, n, v, steps) < 0 && s->rng_failure)
        s->rng_failure(s->rng_script, "cannot write rng trace");
}

static size_t script_steps(const Sim *s) { return s->rng_script ? s->rng_script->used + s->rng_script->extra : 0; }

int sim_script_read(const char *path, uint32_t **v, size_t *n) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t cap = 4096, k = 0;
    uint32_t *a = malloc(cap * sizeof *a);
    char line[128];
    int bad = !a;
    while (a && fgets(line, sizeof line, f)) {
        char *start = line, *end;
        /* Reject truncated lines, signs, overflow and trailing junk instead of silently changing the stream. */
        if (!strchr(line, '\n') && !feof(f)) { bad = 1; break; }
        while (isspace((unsigned char)*start)) start++;
        if (!*start || *start == '#') continue;
        errno = 0;
        unsigned long x = strtoul(start, &end, 16);
        if (!isxdigit((unsigned char)*start) || end == start || errno == ERANGE || x > UINT32_MAX) {
            bad = 1;
            break;
        }
        while (isspace((unsigned char)*end)) end++;
        if (*end && *end != '#') { bad = 1; break; }
        if (k == cap) {
            if (cap > SIZE_MAX / (2 * sizeof *a)) { bad = 1; break; }
            uint32_t *b = realloc(a, 2 * cap * sizeof *a);
            if (!b) { bad = 1; break; }
            a = b, cap *= 2;
        }
        a[k++] = (uint32_t)x;
    }
    bad |= ferror(f);
    bad |= fclose(f) != 0;
    if (bad) {
        free(a);
        return -1;
    }
    *v = a, *n = k;
    return 0;
}

/* "<site> <b mask | r n> <value> <d> <e>"; d = FFFFFFFF marks the default (fresh pairs) */
static void log_draw(Sim *s, uint32_t site, char kind, uint32_t n, uint32_t v, SimIn in) {
    if (s->rng_log && site)
        fprintf((FILE *)s->rng_log, "%X %c%u %u %X %X\n", site, kind, n, v, in.on ? in.d : 0xFFFFFFFFu, in.on ? in.e : 0u);
}

static uint32_t bits(Sim *s, uint32_t mask, uint32_t site, SimIn in) {
    size_t before = script_steps(s);
    uint32_t v = draw(s, in) & mask;
    log_draw(s, site, 'b', mask, v, in);
    trace_draw(s, site, 'b', mask, v, script_steps(s) - before);
    return v;
}

static uint32_t random_n(Sim *s, uint32_t n, uint32_t site, SimIn in) {
    n &= 0xFFFFu; /* n is AX */
    if (n <= 1) return 0; /* no draw */
    size_t before = script_steps(s);
    uint32_t mask = 1;
    while (mask < n) mask = mask << 1 | 1; /* 2^L - 1 for L = the bit length of n */
    mask = mask << 1 | 1;                  /* the original's table gives 2^(L+1) - 1: random(2) masks with 7 */
    uint32_t v;
    /* every redraw with the same pair (the default: a fresh pair per step). Under one constant pair the generator can
     * fall into a short cycle whose values are all n or more (measured 2026-10-20: the arrival's random(16) in a
     * 2-cycle drawing 24, 40, 24, ... hung a 22-day run; docs/engine.md, "the engine follows docs/sim-drift7.md").
     * Engine choice: a repeated state (Brent's cycle test) switches the rest of this call to fresh pairs. */
    uint32_t mark = s->rng_s, steps = 0, limit = 1;
    for (;;) {
        v = draw(s, in) & mask;
        if (v < n) break;
        if (in.on && s->rng_s == mark) in.on = 0;
        if (++steps == limit) mark = s->rng_s, limit <<= 1, steps = 0;
    }
    log_draw(s, site, 'r', n, v, in);
    trace_draw(s, site, 'r', n, v, script_steps(s) - before);
    return v;
}

uint32_t sim_bits(Sim *s, uint32_t mask) { return bits(s, mask, 0, s->rng_in); }
uint32_t sim_random(Sim *s, uint32_t n) { return random_n(s, n, 0, s->rng_in); }
uint32_t sim_bits_site(Sim *s, uint32_t mask, uint32_t site) { return bits(s, mask, site, s->rng_in); }
uint32_t sim_random_site(Sim *s, uint32_t n, uint32_t site) { return random_n(s, n, site, s->rng_in); }
uint32_t sim_bits_in(Sim *s, uint32_t mask, uint32_t site, SimIn in) { return bits(s, mask, site, in); }
uint32_t sim_random_in(Sim *s, uint32_t n, uint32_t site, SimIn in) { return random_n(s, n, site, in); }

/* The oracle's addresses of the save blocks (docs/rng-sites.md section 2.2, the RAM dump mem7.bin): block index (save
 * block - 1) and the address of the block's first record. */
static const struct {
    uint8_t block;
    uint32_t address;
} ADDRESSES[] = {
    {0, 0x1C4748},  /* person records, 106 B */
    {1, 0x1DE912},  /* staff records, 32 B */
    {3, 0x169790},  /* receptions, 174 B */
    {9, 0x16D650},  /* golf rooms, 118 B */
    {10, 0x16DDB0}, /* room-count rows, 100 B */
    {11, 0x16E3F0}, /* waiting rooms */
    {15, 0x188188}, /* treatment rooms */
    {16, 0x18F208}, /* Krankenzimmer */
    {17, 0x198808}, /* Neuro-Krankenzimmer */
    {18, 0x1A1E08}, /* OP */
    {20, 0x1AC008}, /* intensive care */
    {21, 0x1B3448}, /* dental wards */
    {22, 0x1BA4C8}, /* neurology */
    {26, 0x1F0266}, /* hospital records, 500 B */
};

uint32_t sim_address(const Sim *s, const void *record) {
    const uint8_t *p = record;
    for (size_t i = 0; i < sizeof ADDRESSES / sizeof *ADDRESSES; i++) {
        const uint8_t *b = s->world->block[ADDRESSES[i].block];
        if (b && p >= b && p < b + s->world->size[ADDRESSES[i].block])
            return ADDRESSES[i].address + s->rng_heap + (uint32_t)(p - b);
    }
    return 0;
}

void sim_rating_record(Sim *s, const uint8_t *record) {
    uintptr_t address = (uintptr_t)record;
    for (unsigned b = 0; b < WORLD_BLOCKS; b++) {
        uintptr_t base = (uintptr_t)s->world->block[b];
        if (base && address >= base && address - base < s->world->size[b]) {
            s->rating_ref = (SimRatingRef){b + 1, address - base, SIM_RATING_WORLD};
            return;
        }
    }
    s->rating_ref = (SimRatingRef){0, 0, SIM_RATING_WORLD}; /* engine guard: callers supply World records */
}

void sim_rating_catalog(Sim *s, int type) {
    s->rating_ref = (SimRatingRef){0, type < 0 ? SIZE_MAX : (size_t)type * CATALOG_SIZE, SIM_RATING_CATALOG};
}

void sim_rating_advance(Sim *s, size_t bytes) {
    if (bytes > SIZE_MAX - s->rating_ref.offset) s->rating_ref.offset = SIZE_MAX;
    else s->rating_ref.offset += bytes;
}

uint8_t sim_rating_byte(const Sim *s) {
    if (s->rating_ref.kind == SIM_RATING_CATALOG) {
        /* Only the measured 38-record catalog is addressable, through the mapped table API. */
        if (s->rating_ref.offset >= 38u * CATALOG_SIZE - 1) return 0;
        return (uint8_t)program_u8(s->prog, CATALOG, (uint32_t)s->rating_ref.offset + 1);
    }
    if (!s->rating_ref.block) return 110; /* all three builds' measured low-heap byte, never a null read */
    unsigned block = s->rating_ref.block;
    size_t offset = s->rating_ref.offset;
    if (offset == SIZE_MAX) return 0;
    offset++;
    /* Physical adjacency from docs/sim-rooms.md, not the numeric order outside these spans. */
    for (unsigned steps = 0; steps < WORLD_BLOCKS && block && block <= WORLD_BLOCKS; steps++) {
        size_t n = s->world->size[block - 1];
        if (offset < n) return s->world->block[block - 1] ? s->world->block[block - 1][offset] : 0;
        offset -= n;
        if (block >= 4 && block < 26) block++;
        else if (block == 26) block = 31;
        else if (block == 31) block = 1;
        else if (block == 1) block = 2;
        else if (block == 32) block = 27;
        else return 0; /* engine guard: never bridge an unmeasured gap */
    }
    return 0;
}

/* ---- routines (docs/sim.md) ------------------------------------------------------------------ */

/* 0x39730 */
static void daily_costs(Sim *s) {
    static const int day_fields[] = {0x8C, 0xA6, 0x120, 0x124, 0xBA, 0xC2, 0xCA, 0xD2,
                                     0x11C, 0x102, 0x72, 0x7A, 0x96, 0x14E, 0x156};
    for (int h = 0; h < 16; h++) {
        uint8_t *r = hospital(s, h);
        r[0x109] = 0;
        if (r[0x14D]) r[0x14D] = (uint8_t)(r[0x14D] - sim_bits(s, 1));
        for (size_t k = 0; k < sizeof day_fields / sizeof *day_fields; k++) put32(r + day_fields[k], 0);
        const uint8_t *room = room_slots(s, h);
        for (int i = 0; i < ROOM_SLOTS; i++, room += ROOM_SLOT_SIZE) {
            if ((int8_t)room[0] < 0 || !room[1]) continue;
            int rent = catalog_rent(s, room[0]);
            add32(r + H_CASH, -rent);
            add32(r + 0x96, rent);
            add32(r + 0x9A, rent);
        }
        int plots = PLOT_RENT * (uint16_t)get16(r + H_PLOTS_RENTED);
        add32(r + H_CASH, -plots);
        add32(r + 0x9E, plots); /* never reset: reproduces the original */
        add32(r + 0xA2, plots);
        const uint8_t *slot = staff_slots(s, h);
        for (int i = 0; i < STAFF_SLOTS; i++, slot += STAFF_SLOT_SIZE) {
            if (get16(slot) <= 0) continue;
            add32(r + H_CASH, -slot[6]);
            add32(r + 0xA6, slot[6]);
            add32(r + 0xAA, slot[6]);
        }
        if (get32(r + H_CASH) < 0) put32(r + H_CASH, -1000);
    }
}

/* 0x1AEC0 */
long sim_daily_costs(Sim *s, int h) {
    long sum = 0;
    const uint8_t *room = room_slots(s, h);
    for (int i = 0; i < ROOM_SLOTS; i++, room += ROOM_SLOT_SIZE)
        if ((int8_t)room[0] >= 0 && room[1]) sum += catalog_rent(s, room[0]);
    sum += PLOT_RENT * (uint16_t)get16(hospital(s, h) + H_PLOTS_RENTED);
    const uint8_t *slot = staff_slots(s, h);
    for (int i = 0; i < STAFF_SLOTS; i++, slot += STAFF_SLOT_SIZE)
        if (get16(slot) > 0) sum += slot[6];
    return sum;
}

#define PATIENTS 1009
#define PATIENT_SIZE 106
#define PATIENT_OPINION 20 /* bytes 20-35: opinion of hospital 0-15 */

/* 0x1A684 */
void sim_opinion_sum(Sim *s) {
    for (int h = 0; h < 16; h++) {
        put32(hospital(s, h) + 0x44, 0);
        put32(hospital(s, h) + 0x164, 0); /* one dword: +0x164 and +0x166 */
    }
    const uint8_t *p = s->world->block[BLK_PATIENTS] + PATIENT_SIZE;
    for (int i = 1; i < PATIENTS; i++, p += PATIENT_SIZE)
        for (int h = 0; h < 16; h++) {
            uint8_t *r = hospital(s, h);
            int o = p[PATIENT_OPINION + h];
            add32(r + 0x44, o);
            if (o < 30) put16(r + 0x166, get16(r + 0x166) + 1);
            if (o >= 70) put16(r + 0x164, get16(r + 0x164) + 1);
        }
    for (int h = 0; h < 16; h++)
        if (get32(hospital(s, h) + H_CASH) < 0) put32(hospital(s, h) + 0x44, 0);
}

/* 0x1A78C; u16 fields read unsigned */
void sim_score(Sim *s) {
    static const struct { int off, coef; } terms[] = {
        {0x28, 2}, {0xE6, 8}, {0xE8, 8}, {0x26, 1}, {0x2A, -3}, {0x34, 2}, {0x116, 2}, {0x58, 4}, {0x160, 2},
        {0x15E, 16}, {0x36, -4}, {0x118, -4}, {0x38, -3}, {0x3C, -8}, {0x3E, 2}, {0x48, 1}, {0x50, 1}, {0x54, 1},
    };
    World *w = s->world;
    for (int h = 0; h < 16; h++) {
        uint8_t *r = hospital(s, h);
        long v = (long)get32(r + 0x44) * 128 / 1008;
        int32_t cash = get32(r + H_CASH);
        if (cash < 0) v -= -(long)cash / 8;
        for (size_t k = 0; k < sizeof terms / sizeof *terms; k++) v += (long)terms[k].coef * (uint16_t)get16(r + terms[k].off);
        v += (uint16_t)get16(r + 0x128) / 4;
        v += (uint16_t)get16(w->block[34] + 2 * h) / 4; /* [0x5C71], save block 35 */
        v -= 4L * world_tail(w, TAIL_HOUR) + 96L * world_tail(w, TAIL_DAY) + 2880L * world_tail(w, TAIL_MONTH);
        put32(r + 0x44, (int32_t)(v < 0 ? 0 : v));
    }
}

/* 0x1A730 */
void sim_ranking(Sim *s, uint8_t order[16]) {
    for (int i = 0; i < 16; i++) order[i] = (uint8_t)i;
    for (int swapped = 1; swapped;) {
        swapped = 0;
        for (int i = 0; i < 15; i++)
            if (get32(hospital(s, order[i]) + 0x44) < get32(hospital(s, order[i + 1]) + 0x44)) {
                uint8_t t = order[i];
                order[i] = order[i + 1];
                order[i + 1] = t;
                swapped = 1;
            }
    }
}

static const uint8_t *staff_record(Sim *s, int index) { return s->world->block[BLK_STAFF] + index * 32; }

/* 0x629E7 (docs/desktop.md, "Values computed for the tables"); with no nurse or doctor the averages are 0
 * (the division by zero is not reproduced: open in the spec). */
void sim_staff_averages(Sim *s) {
    for (int h = 0; h < 16; h++) {
        uint8_t *r = hospital(s, h);
        long n = 0, bust = 0, age = 0, b6 = 0, b7 = 0, dn = 0, dsum = 0;
        int best = 0, best_index = 0;
        const uint8_t *slot = staff_slots(s, h);
        for (int i = 0; i < STAFF_SLOTS; i++, slot += STAFF_SLOT_SIZE) {
            int index = get16(slot);
            if (!index) continue;
            const uint8_t *st = staff_record(s, index);
            int group = slot[2];
            if (group == 1) {
                n++;
                bust += st[5], age += st[8], b6 += st[6], b7 += st[7];
                if (st[5] > best) best = st[5], best_index = index;
            } else if (group != 3 && group != 4 && group != 6 && group != 8) {
                dn++;
                dsum += (uint8_t)(st[6] * 8 + st[5]); /* the addition in the low byte only */
            }
        }
        put16(r + 0x4A, n ? (int)(bust / n) : 0);
        /* No nurses retain both cached words; zero bust retains the largest-bust index (SIM-105). */
        if (n) put16(r + 0x16A, (int)n);
        if (best) put16(r + 0x16C, best_index);
        r[0x113] = (uint8_t)(n ? age / n : 0);
        r[0x14A] = (uint8_t)(n ? b6 / n : 0);
        r[0x14B] = (uint8_t)(n ? b7 / n : 0);
        long d = dn ? dsum / dn : 0;
        if (d > 120 || (int8_t)d < 0) d = 120;
        r[0x14C] = (uint8_t)d;
    }
}

/* German CD, the ranking of desktop icon 19 (docs/editions.md 7.2, OTH-DE-8/9): +0x194 = the integer mean of the
 * nurses' experience points (staff u16 16) over the slots with an index and group 1; their count to +0x16A only when
 * there is one; no nurse: 0. */
void sim_nurse_experience(Sim *s) {
    for (int h = 0; h < 16; h++) {
        uint8_t *r = hospital(s, h);
        unsigned long n = 0, sum = 0;
        const uint8_t *slot = staff_slots(s, h);
        for (int i = 0; i < STAFF_SLOTS; i++, slot += STAFF_SLOT_SIZE) {
            int index = (uint16_t)get16(slot);
            if (!index || slot[2] != 1) continue;
            n++;
            sum += (uint16_t)get16(staff_record(s, index) + 16);
        }
        put32(r + 0x194, n ? (int32_t)(sum / n) : 0);
        if (n) put16(r + 0x16A, (int)n);
    }
}

/* 0x38D39. "Bytes 12 and 13 weighted twice for hospitals 1-15" is read as doubling their factors. */
void sim_staff_training(Sim *s) {
    for (int h = 0; h < 16; h++) {
        uint8_t *r = hospital(s, h);
        put32(r + 0x132, 0);
        put32(r + 0x136, 0);
        long n = 0, sum = 0;
        const uint8_t *slot = staff_slots(s, h);
        for (int i = 0; i < STAFF_SLOTS; i++, slot += STAFF_SLOT_SIZE) {
            int index = get16(slot);
            if (index <= 0) continue;
            const uint8_t *st = staff_record(s, index);
            n++;
            put16(r + 0x132, get16(r + 0x132) + st[19]);
            put16(r + 0x138, get16(r + 0x138) + st[12] + st[13]);
            int w = h ? 2 : 1;
            sum += st[2] * 4 + st[12] * 32 * w + st[13] * 16 * w + st[15] + (uint16_t)get16(st + 16) / 16;
        }
        put16(r + 0x134, (int)n);
        put16(r + 0x136, (int)(n ? sum / n + (h ? 8 : 0) : 0));
    }
}

/* 0x59E3B: try to start a story event (docs/office.md, "Starting a story event"). The done bytes of the 36
 * records live in save block 30, bytes 0-35. */
#define STORY_RECORDS 0x3F71
#define T_EVENT_NEW 0x10
#define T_EVENT 0x12
#define T_EVENT_COUNTDOWN 0x14
static void story_start(Sim *s) {
    uint8_t *done = s->world->block[BLK_30];
    const uint8_t *rec = program_table(s->prog, STORY_RECORDS);
    int n = (int)sim_random(s, 35) + 1;
    int pre = rec[n * 10 + 1];
    if (!done[n] && (pre == 0xFF || done[pre])) {
        done[n] = rec[n * 10 + 2] ? 0xFF : 0;
        world_set_tail(s->world, T_EVENT, n);
        world_set_tail(s->world, T_EVENT_NEW, 0xFF);
    }
    world_set_tail(s->world, T_EVENT_COUNTDOWN, 2);
}

/* 0x2C00D */
static void days_employed(Sim *s) {
    for (int h = 0; h < 16; h++) {
        uint8_t *slot = staff_slots(s, h);
        for (int i = 0; i < STAFF_SLOTS; i++, slot += STAFF_SLOT_SIZE)
            if (get16(slot)) put16(slot + 8, (uint16_t)get16(slot + 8) + 1);
    }
}

/* 0x2B6C3 */
static void job_ad_cost(Sim *s) {
    const uint8_t *job = s->world->block[BLK_JOB_OPENINGS];
    for (int i = 0; i < JOB_OPENINGS; i++, job += JOB_OPENING_SIZE) {
        if ((int8_t)job[0] < 0) continue;
        uint8_t *r = hospital(s, job[0]);
        add32(r + 0x16E, 1);
        if (get32(r + 0x16E) > 999) {
            put16(r + 0x172, get16(r + 0x172) + 1);
            put32(r + 0x16E, 0);
        }
        add32(r + H_CASH, -1);
        add32(r + 0xA6, 1);
        add32(r + 0xAA, 1);
    }
}

/* 0x3CDB3 */
static void leaders_and_debt(Sim *s) {
    World *w = s->world;
    for (int off = T_LEAD_A_VALUE; off <= T_LEAD_B_HOSP; off += 2) world_set_tail(w, off, 0);
    for (int h = 0; h < 16; h++) {
        uint8_t *r = hospital(s, h);
        if (get32(r + H_CASH) >= 0) {
            if (get16(r + 0x10E) > world_tail(w, T_LEAD_A_VALUE)) {
                world_set_tail(w, T_LEAD_A_VALUE, get16(r + 0x10E));
                world_set_tail(w, T_LEAD_A_HOSP, h);
            }
            if (get16(r + 0x110) > world_tail(w, T_LEAD_B_VALUE)) {
                world_set_tail(w, T_LEAD_B_VALUE, get16(r + 0x110));
                world_set_tail(w, T_LEAD_B_HOSP, h);
            }
        }
        if (r[0x108] && !r[0xF4]) r[0x108]--;
        if (get32(r + H_CASH) < 0) put32(r + H_CASH, -10000);
    }
}

/* ---- dispatch --------------------------------------------------------------------------------- */

static int run(Sim *s, uint32_t routine) {
    switch (routine) {
    case 0x39730: daily_costs(s); return 1;
    case 0x2C00D: days_employed(s); return 1;
    case 0x2B6C3: job_ad_cost(s); return 1;
    case 0x3CDB3: leaders_and_debt(s); return 1;
    case 0x1A4C5: sim_reseed(s); return 1; /* the reseed with the engine's clock rule */
    case 0x3DCCB: report_day_change(s); return 1; /* the daily report (docs/ui-reports.md, section 1) */
    case 0x27495: advice_scheduled(s); return 1; /* the work advice in the background (section 2) */
    default: return sim_ai_run(s, routine) || sim_patients_run(s, routine) || sim_staff_run(s, routine) || sim_rooms_run(s, routine) ||
                                    sim_news_run(s, routine) || sim_thugs_run(s, routine) || sim_race_run(s, routine) ||
                                    sim_player_run(s, routine) || sim_books_run(s, routine);
    }
}

static void call(Sim *s, uint32_t routine) {
    if (s->before_call) s->before_call(s->before_call_ctx, routine); /* test hook (sim.h), NULL by default */
    int i = 0;
    while (i < s->nroutines && s->routine[i] != routine) i++;
    if (i == s->nroutines && i < SIM_MAX_ROUTINES) s->routine[s->nroutines++] = routine;
    int done = run(s, routine);
    if (i < SIM_MAX_ROUTINES) {
        s->calls[i]++;
        s->implemented[i] = (uint8_t)done;
    }
}

int sim_call(Sim *s, uint32_t routine) {
    sim_clock_registers(s);
    call(s, routine);
    int i = 0;
    while (i < s->nroutines && s->routine[i] != routine) i++;
    return i < s->nroutines && s->implemented[i];
}

static void call_list(Sim *s, const uint32_t *list, int n) {
    for (int i = 0; i < n && list[i]; i++) call(s, list[i]);
}

void sim_set_ailments(Sim *s, const uint8_t *table, size_t len) {
    s->ailments = table;
    s->ailments_len = len;
}

void sim_init(Sim *s, World *w, const Program *p, uint64_t seed) {
    memset(s, 0, sizeof *s);
    s->world = w;
    s->prog = p;
    s->rng = seed; /* the auxiliary generator (splitmix64 takes any state) */
    s->rng_player = RNG_AI_PLAYER;
    s->redraw_count = SIM_REDRAW_START;
    s->anim_count = SIM_ANIM_START;
    if (p && p->build) { /* a CD build: its own scratch block address and heap layout (docs/rng-sites.md section 8) */
        uint32_t a = build_at(p->build, RNG_AI_PLAYER);
        if (a != PROGRAM_NOWHERE) s->rng_player = a;
        s->rng_heap = p->build->heap_shift;
    }
    sim_reset_generator(s, seed); /* clock_d / clock_ebp 0 / 0: no screen yet (sim_clock_registers) */
}

/* docs/sim-drift8.md section 14.2, measured in the game (disk build) at the clock call 0x19985 and in the skip loop
 * 0x491AA / 0x491BC: EDI is the player block 0x631B on every screen, EBP the screen's script (the screen-table entry)
 * except on the site map, whose frame leaves 0x552A; the skip loop passes EDI 0x2F850 / EBP 0xB124. Per build: the
 * build's offsets of those disk offsets (docs/editions.md), the script address read from the build's screen table. */
#define CLOCK_PLAYER 0x631Bu
#define CLOCK_SITE_MAP 0x552Au
#define CLOCK_SKIP_D 0x2F850u
#define CLOCK_SKIP_EBP 0xB124u
#define SCREEN_SITE_MAP 5
static uint32_t clock_at(const Sim *s, uint32_t disk) {
    uint32_t a = s->prog ? build_at(s->prog->build, disk) : disk;
    return a == PROGRAM_NOWHERE ? disk : a;
}

void sim_clock_registers(Sim *s) {
    if (s->fast_forward || s->skip_registers) {
        s->clock_d = clock_at(s, CLOCK_SKIP_D), s->clock_ebp = clock_at(s, CLOCK_SKIP_EBP);
    } else if (s->screen <= 0 || s->screen >= SCREEN_COUNT) {
        s->clock_d = 0, s->clock_ebp = 0; /* no screen: the oracle's start registers (engine choice) */
    } else {
        s->clock_d = clock_at(s, CLOCK_PLAYER);
        s->clock_ebp = s->screen == SCREEN_SITE_MAP ? clock_at(s, CLOCK_SITE_MAP)
                       : s->prog                    ? program_screen_script(s->prog, s->screen)
                                                    : 0;
    }
}

/* docs/mainloop.md, "Minute, hour, day, month". */
static const uint32_t MINUTE_LIST[] = {0x1C8A4, 0x6F6B2, 0x6FA5A, 0x597F9, 0x59DCA, 0x58BA3, 0x6EDA1, 0x57F15};
static const uint32_t HOUR_LIST[] = {0x2B718, 0x19A73, 0x672A1, 0x30AD9, 0x6ABB9, 0x5A3CD, 0x6CE07, 0x3DC86,
                                     0x3CDB3, 0x3091F, 0x4302C, 0x42714, 0x1AB70, 0x3B8B7, 0x2B6C3};
static const uint32_t DAY_LIST[] = {0x39730, 0x3DCCB, 0x2C00D};
#define LEN(a) ((int)(sizeof a / sizeof *a))

/* The day word advances first, then the day list runs, and only then do the month and year wrap (SIM-21,
 * docs/mainloop.md, "Minute, hour, day, month"; docs/sim-long.md section 3): the report baseline's score (0x3DCCB ->
 * 0x1A78C) takes the new day's time term, so a day with nothing else changed reports a loss of 96 points (oracle
 * 2026-10-19); on the last night of a month the list sees day 31 of the old month, which gives another score only at
 * the year change (96 x 31 + 2,880 x 12 instead of 96 + 2,880). */
static void wrap_date(World *w) {
    if (world_tail(w, TAIL_DAY) < 31) return;
    world_set_tail(w, TAIL_DAY, 1);
    int month = world_tail(w, TAIL_MONTH) + 1;
    if (month < 13) {
        world_set_tail(w, TAIL_MONTH, month);
        return;
    }
    world_set_tail(w, TAIL_MONTH, 1);
    world_set_tail(w, TAIL_YEAR, world_tail(w, TAIL_YEAR) + 1);
}

static void new_day(Sim *s) {
    World *w = s->world;
    world_set_tail(w, TAIL_DAY, world_tail(w, TAIL_DAY) + 1);
    if (fix_on(30)) {
        /* FIX-30 (docs/fixes.md): original runs the day list before the month/year wrap (it sees day 31 of the old
         * month, and 01.01. scores as "month 13"); fixed wraps first, so the list sees the new date */
        wrap_date(w);
        call_list(s, DAY_LIST, LEN(DAY_LIST));
        return;
    }
    call_list(s, DAY_LIST, LEN(DAY_LIST));
    wrap_date(w);
}

static void new_hour(Sim *s) {
    World *w = s->world;
    int hours = world_tail(w, TAIL_HOURS);
    if (hours < HOURS_PLAYED_MAX) world_set_tail(w, TAIL_HOURS, hours + 1);
    int hour = world_tail(w, TAIL_HOUR) + 1;
    world_set_tail(w, TAIL_HOUR, hour); /* midnight's hourly calls see 24:00 on the old date */
    if (hour >= 22) call(s, 0x5B25D); /* "only from hour 22", read with the new hour */
    call_list(s, HOUR_LIST, LEN(HOUR_LIST));
    if (hour == 24) {
        world_set_tail(w, TAIL_HOUR, 0);
        new_day(s);
    }
}

static void new_minute(Sim *s) {
    World *w = s->world;
    /* The word holds 1-60 through the minute list and the multiple-of-7 test, and is reset only after them
     * (docs/sim-drift8.md section 9): minute 60 is no multiple of 7, so 0x672A1 runs 8 times here and once from the
     * hour list, 9 times an hour. */
    int minute = world_tail(w, TAIL_MINUTE) + 1;
    world_set_tail(w, TAIL_MINUTE, minute);
    call(s, 0x1A4C5);
    /* story-event countdown: no event active, hours 5-19 */
    int hour = world_tail(w, TAIL_HOUR);
    if (!world_tail(w, T_EVENT) && hour >= 5 && hour <= 19) {
        int c = world_tail(w, T_EVENT_COUNTDOWN) - 1;
        world_set_tail(w, T_EVENT_COUNTDOWN, c);
        if (c <= 0) story_start(s);
    }
    call_list(s, MINUTE_LIST, LEN(MINUTE_LIST));
    if (minute % 7 == 0) call(s, 0x672A1);
    if (minute == 60) world_set_tail(w, TAIL_MINUTE, 0), new_hour(s);
}

/* 0x49184's loop: the clock routine runs until the minute word has changed and come back to its start value, so the
 * skip ends at the start of that minute one hour later (4,800 ticks minus those already in the minute); each() runs
 * after every tick (the sound driver's per-tick part). The fast-forward flag [0x5C25] is set meanwhile. */
int sim_skip_hour(Sim *s, void (*each)(void *), void *ctx) {
    int start = world_tail(s->world, TAIL_MINUTE), n = 0;
    s->fast_forward = 1;
    do {
        sim_tick(s), n++;
        if (each) each(ctx);
    } while (world_tail(s->world, TAIL_MINUTE) == start);
    while (world_tail(s->world, TAIL_MINUTE) != start) {
        sim_tick(s), n++;
        if (each) each(ctx);
    }
    s->fast_forward = 0;
    return n;
}

void sim_tick(Sim *s) {
    sim_clock_registers(s); /* what the clock call passes on this tick (context S) */
    /* [0x5AB1]: the players' "redraw the animations" flags pb+0x3B, first after 120 ticks, then every 50 */
    if (--s->redraw_count <= 0) {
        s->redraw_count = SIM_REDRAW_PERIOD;
        s->world->block[BLK_PLAYER0][0x3B] = 0xFF;
        s->world->block[BLK_PLAYER1][0x3B] = 0xFF;
    }
    /* [0x5AB3]: the players' animation tick pb+0x3C advances first after 120 ticks, then every second tick (the room
     * screens pace their animations and ambience by it and clear it, docs/mainloop.md and docs/ui-sheets.md,
     * section 3) */
    if (--s->anim_count <= 0) {
        s->anim_count = SIM_ANIM_PERIOD;
        s->world->block[BLK_PLAYER0][0x3C]++;
        s->world->block[BLK_PLAYER1][0x3C]++;
    }
    if (++s->tick >= SIM_TICKS_PER_MINUTE) {
        s->tick = 0;
        new_minute(s);
        return;
    }
    if (s->tick % 2 == 0) call(s, 0x2FC17);
    call_list(s, SIM_TICKS[s->tick], 8);
    /* on the inventory (screen 65) no draw at all; else 1 in 8, r7 = 3 (docs/sim-rooms2.md, "The store-screen test") */
    if (s->tick == 64 && s->screen != 0x41 && sim_bits_site(s, 7, 0x1C565) == 3) { /* site: a log label only */
        call(s, 0x2B207);
        call(s, 0x2BFB7);
        call(s, 0x2B210);
    }
}
