/* Modules F (AI hospital management) and G (markets), from docs/sim-ai.md. Of the newspaper items (module H) only
 * the price item 0x4763B is written (sim_news.c); the copy-protection hook 0x1A3EA these routines call is left out. */
#include <string.h>

#include "sim_internal.h"

/* ---- tables (docs/sim-ai.md, "Tables") ---------------------------------------------------------- */

#define DESCRIPTIONS 0x4758 /* 14 bytes per category */
#define AI_ROOM_LIST 0x4746 /* 18 room types, one per category */
#define FURNITURE_CATALOG 0xEE0
#define FURNITURE_SIZE 18
#define EQUIPMENT_CATALOG 0x144A
#define EQUIPMENT_SIZE 20
#define ORGANS 0x470A
#define ORGAN_BASE 0x4712
#define CARS 0x3B49

/* Room-instance tables per category: block index, area per hospital, record size (0 = one record). */
static const struct {
    int8_t block;
    uint16_t area, record;
} ROOMS[19] = {
    [2] = {3, 174, 0},     [3] = {11, 1800, 180}, [4] = {12, 730, 0},    [5] = {15, 1800, 180},
    [6] = {16, 2400, 240}, [7] = {18, 1296, 216}, [8] = {19, 1296, 216}, [9] = {20, 1860, 186},
    [10] = {4, 158, 0},    [11] = {21, 1800, 180}, [12] = {9, 118, 0},   [13] = {7, 218, 0},
    [14] = {8, 118, 0},    [15] = {22, 1800, 180}, [16] = {17, 2400, 240}, [17] = {5, 178, 0},
    [18] = {6, 158, 0},
};


Description sim_description(Sim *s, int cat) {
    uint32_t at = DESCRIPTIONS + 14u * (uint32_t)cat;
    Description d = {
        .furniture = program_u16(s->prog, at),
        .furniture_slots = program_u16(s->prog, at + 2),
        .equipment = program_u16(s->prog, at + 4),
        .equipment_slots = program_u16(s->prog, at + 6),
        .cond_a = program_u16(s->prog, at + 8),
        .cond_b = program_u16(s->prog, at + 10),
        .type = program_u16(s->prog, at + 12),
    };
    return d;
}

static int records_of(int cat) { return ROOMS[cat].record ? ROOMS[cat].area / ROOMS[cat].record : 1; }
static int record_size(int cat) { return ROOMS[cat].record ? ROOMS[cat].record : ROOMS[cat].area; }
uint8_t *sim_room_record(Sim *s, int h, int cat, int i) {
    return s->world->block[ROOMS[cat].block] + h * ROOMS[cat].area + i * record_size(cat);
}
void sim_rating_room(Sim *s, int h, int cat, int i) {
    s->rating_ref = (SimRatingRef){(unsigned)ROOMS[cat].block + 1,
        (size_t)h * ROOMS[cat].area + (size_t)i * record_size(cat), SIM_RATING_WORLD};
}
/* Records in use: the room count, at most the table's capacity. */
int sim_rooms_in_use(Sim *s, int h, int cat) {
    int n = room_counts(s, h)[cat];
    return n < records_of(cat) ? n : records_of(cat);
}

const uint8_t *sim_furniture_item(Sim *s, int id) {
    return program_table(s->prog, FURNITURE_CATALOG) + (uint32_t)id * FURNITURE_SIZE;
}
const uint8_t *sim_equipment_item(Sim *s, int id) {
    return program_table(s->prog, EQUIPMENT_CATALOG) + (uint32_t)id * EQUIPMENT_SIZE;
}

/* ---- the picker and expected cash --------------------------------------------------------------- */

/* 0x62CFD: an AI hospital (not human-controlled, in business); 0 means "do nothing". Per try a random(16) and the r31
 * the original tests against h to call the copy-protection question scheduler 0x1A3EA: drawn, nothing called. Both in
 * context S (docs/rng-sites.md section 3.1); the random(16)'s shares come out of the generator with these inputs
 * (they replace the weights measured in docs/sim-drift5.md section 4). */
int sim_pick(Sim *s) {
    SimIn in = rng_scheduler(s);
    int h = 0;
    for (int tries = 0; tries < 999; tries++) {
        h = (int)sim_random_in(s, 16, 0x62D1F, in);
        sim_bits_in(s, 31, 0x62D40, in);
        const uint8_t *r = hospital(s, h);
        if (r[0x14] == 0 && r[0x15] != 0) break;
    }
    return h;
}

/* Waiting rooms (0x1B200): sum of free seats; *short_seats gets the sum of 10 - seats ([0x5B99]). */
static int waiting_free_seats(Sim *s, int h, int *short_seats) {
    int free_seats = 0, missing = 0;
    for (int i = 0; i < sim_rooms_in_use(s, h, 3); i++) {
        const uint8_t *rec = sim_room_record(s, h, 3, i);
        free_seats += rec[10] - rec[12];
        missing += 10 - rec[10];
    }
    if (short_seats) *short_seats = missing;
    return free_seats;
}

/* 0x1B015 (docs/sim-ai.md, steps 1-11). */
int32_t sim_expected_cash(Sim *s, int h) {
    World *w = s->world;
    const uint8_t *r = hospital(s, h);
    int32_t c = get32(r + H_CASH), k = (int32_t)sim_daily_costs(s, h);
    int hour = world_tail(w, TAIL_HOUR), played = world_tail(w, TAIL_HOURS);
    int32_t e = c - k / 8, b = k;
    if (k >= 500) {
        e -= (k - 450) / 4;
        b = (k - 450) / 4;
    }
    int32_t share = b / 24 * hour;
    for (int hh = hour < 2 ? 1 : hour; hh < 10; hh++) share /= 2; /* H = 9: once ... H = 1 and 0: 9 times */
    e -= share;
    const uint8_t *job = w->block[BLK_JOB_OPENINGS];
    for (int i = 0; i < JOB_OPENINGS; i++, job += JOB_OPENING_SIZE)
        if (job[0] == h) e -= job[2];
    if (waiting_free_seats(s, h, NULL) == 0) e += 20;
    if (room_counts(s, h)[2] && get16(sim_room_record(s, h, 2, 0) + 0x16)) e += 20;
    e -= 150 - 13 * (played < 5 ? played : 5);
    int rooms = 0;
    for (int k2 = 2; k2 <= 18; k2++) rooms += room_counts(s, h)[k2];
    if (rooms >= 3) e -= 16 * (rooms - 2);
    e += 25;
    if (hour < 9) e += 50;
    if (c >= 80) {
        e += c / 16;
        if (world_tail(w, TAIL_DAY) >= 2) {
            e += 40;
            if (hour >= 9 && c >= 100) {
                e += 8;
                if (c >= 130) e += 8;
            }
        }
    }
    return e - 40;
}

/* 0x1A941; the two-player part for hospital 1 is not implemented. */
void sim_expected_cash_all(Sim *s) {
    for (int h = 0; h < 16; h++) put32(hospital(s, h) + 0xE2, sim_expected_cash(s, h));
}

/* ---- creating rooms --------------------------------------------------------------------------- */

/* 0x2B5B3, the wear of one room record (docs/sim-ai.md, end of "Recompute", "Per slot, exactly"; SIM-44, SIM-64): with
 * the hour 7-17 and t&3 = 2 every slot of the list in order, empty ones included: an id negative as a signed word or
 * above 76 (equipment: 154) gets id 0 and condition 0, no draw; otherwise the condition c gets an r7 only when it is
 * not 0 (r7 = 0: c - 1) and is stored; c below 1 as a signed byte removes the slot (id 0), which ends the furniture
 * pass (an empty slot at condition 0 counts as such a removal) and goes on in the equipment list. The flag [0x5BD3] a
 * removed equipment item sets is not modelled. */
static void wear_list(Sim *s, uint8_t *ids, uint8_t *cond, int n, int max_id, int stop_on_removal, uint32_t site) {
    for (int i = 0; i < n; i++) {
        int id = (uint16_t)get16(ids + 2 * i);
        if (id > max_id) { /* a word negative as signed is above the bound too */
            put16(ids + 2 * i, 0);
            cond[i] = 0;
            continue;
        }
        uint8_t c = cond[i];
        if (c && sim_bits_site(s, 7, site) == 0) c--;
        cond[i] = c;
        if ((int8_t)c < 1) {
            put16(ids + 2 * i, 0);
            if (stop_on_removal) break;
        }
    }
}
static void wear(Sim *s, const Description *d, uint8_t *rec) {
    int hour = world_tail(s->world, TAIL_HOUR);
    if (hour < 7 || hour > 17 || sim_bits_site(s, 3, 0x2B5E3) != 2) return;
    wear_list(s, rec + d->furniture, rec + d->cond_a, d->furniture_slots, FURNITURE_IDS - 1, 1, 0x2B638);
    wear_list(s, rec + d->equipment, rec + d->cond_b, d->equipment_slots, EQUIPMENT_IDS - 1, 0, 0x2B699);
}

/* Recompute of one category for all hospitals (docs/sim-ai.md, "Recompute"); with_wear is the switch [0x5BC0],
 * which only 0x2B207 (tick 64) sets. */
void sim_recompute(Sim *s, int cat) { sim_recompute_wear(s, cat, 0); }
/* 0x1E523 (docs/sim-ai.md, the recompute): a waiting room with fewer seats than occupied places sends every waiting
 * patient away: per place with a person, its opinion of the hospital it sits in = 0, fields 3 and 4 = FF, the person
 * word = 0 (the wait word stays); then the u16 +0x0C = 0. Engine guards: an index past the person records is skipped,
 * and so is an opinion byte outside the record (the original writes into the next one). */
static void send_waiting_away(Sim *s, uint8_t *rec) {
    for (int i = 0; i < 10; i++) {
        uint8_t *place = rec + 0x14 + 4 * i;
        int index = (uint16_t)get16(place);
        if (!index) continue;
        if (index < 1009) {
            uint8_t *p = s->world->block[BLK_PATIENTS] + (size_t)index * 106;
            if (20 + p[3] < 106) p[20 + p[3]] = 0;
            p[3] = p[4] = 0xFF;
        }
        put16(place, 0);
    }
    put16(rec + 0x0C, 0);
}

/* SIM-32 (docs/sim-ai.md, "The pathology's base rating comes from a stale register"): the pathology recompute 0x1EBE9
 * calls the base-rating step before it loads its category 18, so the step takes the low byte al of a stale register
 * as the category: o = description al's word +0x0C (read past the 19 real descriptions as the image holds them), and
 * the base comes from the row of the pathology record's byte o (o = 0: byte 0, the old rating). Engine guard: a byte o
 * past the pathology block reads 0 (the original reads on into memory; never seen). */
static int stale_pathology_type(Sim *s, const uint8_t *rec, int al) {
    uint32_t o = program_word(s->prog, DESCRIPTIONS, 14u * (uint32_t)al + 0x0C);
    const uint8_t *block = s->world->block[ROOMS[18].block];
    size_t at = (size_t)(rec - block) + o;
    return at < s->world->size[ROOMS[18].block] ? block[at] : 0;
}
/* The base of catalog row t (+0x29, docs/sim-ai.md, the same section, step 4): rows past the real catalog are read as
 * the memory holds them; rows 88 and 89 fall in the daily-cost table [0x46BA] (s32 per hospital, 0x1AEC0), byte 1 of
 * hospital 0's and of hospital 15's cost, computed now as everywhere (sim_race.c). Only where the build keeps the
 * table at the disk distance from the catalog; every other row from the image (rows that hold other game state there,
 * 150, 172, 179, 202, 212, 221, are left as the image has them: Open in the doc). */
#define DAILY_COSTS 0x46BA
#define NEED_TABLE 0x472A /* 12 u16 of 0x62D6A / 0x5B25D, not saved (docs/sim-staff.md, "Need table") */
static int pathology_base(Sim *s, int t) {
    uint32_t a = catalog_at(t, CATALOG_BASE);
    uint32_t cat = program_at(s->prog, CATALOG), costs = program_at(s->prog, DAILY_COSTS);
    if (a >= DAILY_COSTS && a < DAILY_COSTS + 64 && cat != PROGRAM_NOWHERE && costs - cat == DAILY_COSTS - CATALOG) {
        uint32_t k = (uint32_t)sim_daily_costs(s, (int)(a - DAILY_COSTS) / 4);
        return (int)(k >> 8 * ((a - DAILY_COSTS) % 4) & 0xFF);
    }
    /* row 90 is the high byte of the need table's entry 4 (N4, SIM-65): 255 while N4 is negative, else 0 */
    uint32_t need = program_at(s->prog, NEED_TABLE);
    if (a == NEED_TABLE + 2 * 4 + 1 && cat != PROGRAM_NOWHERE && need != PROGRAM_NOWHERE && need - cat == NEED_TABLE - CATALOG)
        return s->need4 < 0 ? 255 : 0;
    return catalog_u8(s, t, CATALOG_BASE);
}
/* ... and what the step leaves for the next pathology: the last equipment slot's id w gives EAX = 20 w, and AL = item
 * w's +5 unless its +10 is 0x2B (an empty slot: item 0, +5 = 0). */
static int stale_pathology_al(Sim *s, const Description *d, const uint8_t *rec) {
    uint32_t w = d->equipment_slots ? (uint16_t)get16(rec + d->equipment + 2 * (d->equipment_slots - 1)) : 0;
    if (program_u8(s->prog, EQUIPMENT_CATALOG, EQUIPMENT_SIZE * w + 10) == 0x2B) return (int)(20u * w & 0xFF);
    return program_u8(s->prog, EQUIPMENT_CATALOG, EQUIPMENT_SIZE * w + 5);
}

void sim_recompute_wear(Sim *s, int cat, int with_wear) {
    if (cat < 2 || cat == 4 || cat > 18) return;
    Description d = sim_description(s, cat);
    /* SIM-32: the first pathology's al is the low byte of the site-table pointer [0x5A85] the loop set-up loaded (the
     * room-count rows: 0xB0 in the disk and German CD builds, 0x40 in the English one) */
    int al = (uint8_t)rng_row(s, 0);
    int single = !ROOMS[cat].record;
    if (single) sim_rating_room(s, 0, cat, 0);
    for (int h = 0; h < 16; h++) {
        if (single && !sim_rooms_in_use(s, h, cat)) sim_rating_advance(s, record_size(cat));
        for (int i = 0; i < sim_rooms_in_use(s, h, cat); i++, sim_rating_advance(s, record_size(cat))) {
            if (!single && !i) sim_rating_room(s, h, cat, 0);
            uint8_t *rec = sim_room_record(s, h, cat, i);
            int type = rec[d.type], base = catalog_u8(s, type, CATALOG_BASE);
            /* FIX-47 (docs/fixes.md): original takes the pathology's base from the stale category al (SIM-32); fixed
             * from the record's own room type, as every other category */
            if (cat == 18 && !fix_on(47)) base = pathology_base(s, stale_pathology_type(s, rec, al));
            int r0 = base, r1 = 0, kind5 = 0, kind8 = 0;
            for (int f = 0; f < d.furniture_slots; f++) {
                int id = (uint16_t)get16(rec + d.furniture + 2 * f);
                if (!id || id >= FURNITURE_IDS) continue;
                const uint8_t *it = sim_furniture_item(s, id);
                r0 += it[4], r1 += it[5];
                if (it[6] == 5) kind5 += it[10];
                if (it[6] == 8) kind8 += it[10];
            }
            int blood_capacity = 0;
            for (int e = 0; e < d.equipment_slots; e++) {
                int id = (uint16_t)get16(rec + d.equipment + 2 * e);
                if (!id || id >= EQUIPMENT_IDS) continue;
                const uint8_t *it = sim_equipment_item(s, id);
                if (it[10] == 0x3D) blood_capacity += 2000;
                if (it[10] == 0x2B) continue;
                r0 += it[4], r1 += it[5];
            }
            if (cat == 18) al = stale_pathology_al(s, &d, rec);
            rec[0] = (uint8_t)r0;
            rec[1] = (uint8_t)r1;
            switch (cat) {
            /* a byte: the reception's comfort counter +0x45 next to it has no writer but its countdown, the break and
             * the operator (docs/sim-drift7.md section 2.1), so the recompute cannot clear it */
            case 2: rec[0x44] = (uint8_t)kind5; break;
            case 3:
                rec[10] = (uint8_t)(kind5 > 10 ? 10 : kind5);
                if (rec[10] < rec[12]) send_waiting_away(s, rec);
                break;
            case 6:
            case 16: rec[10] = (uint8_t)(kind8 > base ? base : kind8); break;
            case 12: rec[4] = (uint8_t)base; break;
            case 10: rec[10] = (uint8_t)base; break;
            case 18: rec[8] = 10; break;
            case 17:
                put16(rec + 0x16, base);
                put32(rec + 0x10, blood_capacity);
                break;
            default: break;
            }
            /* wear: waiting rooms only with more than 2 free seats, bed wards with more than 1 free bed */
            int free = (uint8_t)rec[10] - (uint8_t)rec[12];
            if (with_wear && (cat == 3 ? free > 2 : cat == 6 || cat == 16 ? free > 1 : 1)) wear(s, &d, rec);
        }
    }
}

/* 0x1DE67 (with the newspaper item 0x46CC4) and the common setup 0x1E22E. */
static void create_room(Sim *s, int h, int cat, int type) {
    sim_rating_catalog(s, type);
    uint8_t *counts = room_counts(s, h);
    int index = counts[cat];
    counts[cat]++;
    sim_news_new_room(s, h, cat, index); /* 0x46CC4, the department item */
    if (cat < 2 || index >= records_of(cat)) return;
    uint8_t *rec = sim_room_record(s, h, cat, index);
    memset(rec, 0, (size_t)record_size(cat));
    Description d = sim_description(s, cat);
    rec[d.type] = (uint8_t)type;
    for (int i = 0; i < 4; i++) {
        put16(rec + d.furniture + 2 * i, catalog_u16(s, type, CATALOG_FURNITURE + 2 * i));
        put16(rec + d.equipment + 2 * i, catalog_u16(s, type, CATALOG_EQUIPMENT + 2 * i));
    }
    /* SIM-38 (docs/sim-ai.md, "Create the room"): the eight r31 list by list, D+8 bytes 0-3, then D+10 bytes 0-3 */
    for (int i = 0; i < 4; i++) rec[d.cond_a + i] = (uint8_t)(sim_bits(s, 31) + 60);
    for (int i = 0; i < 4; i++) rec[d.cond_b + i] = (uint8_t)(sim_bits(s, 31) + 60);
    if (cat == 4) {
        rec[7] = (uint8_t)catalog_u8(s, type, CATALOG_BASE);
        rec[8] = 2;
        rec[9] = 1;
        return;
    }
    if (cat == 18) rec[6] = 0, rec[8] = 4;
    sim_recompute(s, cat);
}

void sim_create_room(Sim *s, int h, int cat, int type) { create_room(s, h, cat, type); }

/* Build step (0x64B13 rent, 0x64BB4 buy). */
static void build(Sim *s, int h, int cat) {
    int type = -1;
    for (int tries = 0; tries < 400 && type < 0; tries++) {
        int t = program_u8(s->prog, AI_ROOM_LIST, sim_random(s, 18));
        if (catalog_u8(s, t, CATALOG_CATEGORY) == cat) type = t;
    }
    if (type < 0) return;
    uint8_t *r = hospital(s, h);
    int32_t cash = get32(r + H_CASH);
    int price = catalog_u16(s, type, CATALOG_PRICE);
    int buy = cash >= 60000 || cash > 32 * price;
    uint8_t *slot = room_slots(s, h);
    int i = 0;
    while (i < ROOM_SLOTS && !(slot[2 * i] == 0xFF && slot[2 * i + 1] == 0xFF)) i++;
    if (i == ROOM_SLOTS) return;
    int cost = buy ? price + price / 4 : catalog_rent(s, type);
    if (cost > cash) return;
    int free_plots = get16(r + H_PLOTS_OWNED) + get16(r + H_PLOTS_RENTED) - get16(r + H_PLOTS_BUILT);
    int needed = catalog_u8(s, type, CATALOG_PLOTS);
    if (free_plots < needed) {
        put16(r + 0xDE, needed - free_plots); /* order plots (overwritten, not added) */
        return;
    }
    add32(r + H_CASH, -cost);
    add32(r + (buy ? 0x14E : 0x96), cost);
    add32(r + (buy ? 0x152 : 0x9A), cost);
    slot[2 * i] = (uint8_t)type;
    slot[2 * i + 1] = (uint8_t)!buy;
    add16(r + H_PLOTS_BUILT, needed);
    create_room(s, h, cat, type);
}

/* ---- 0x6337D: AI building ---------------------------------------------------------------------- */

typedef struct {
    uint8_t cat; /* count tested */
    uint8_t below; /* build while count < below (1 = "= 0", 2 = "< 2") */
    uint8_t build;
    uint8_t equal; /* 1: build only when count == below (hospital 9's step 2, SIM-27) */
} Step;
/* Per-hospital plan, docs/sim-ai.md step 4: ensure order, then steps for X >= 180, 230, 270, 310. */
static const struct {
    uint8_t ensure[3];
    Step step[4];
} PLAN[16] = {
    [1] = {{5, 11, 15}, {{15, 2, 15, 0}, {5, 2, 5, 0}, {6, 1, 6, 0}, {16, 1, 16, 0}}},
    [2] = {{5, 15, 11}, {{15, 1, 15, 0}, {11, 1, 11, 0}, {6, 1, 6, 0}, {16, 1, 16, 0}}},
    [3] = {{15, 11, 5}, {{11, 1, 11, 0}, {5, 2, 5, 0}, {16, 1, 16, 0}, {6, 1, 6, 0}}},
    [4] = {{11, 5, 15}, {{15, 1, 15, 0}, {11, 2, 11, 0}, {6, 1, 6, 0}, {16, 1, 16, 0}}},
    [5] = {{11, 15, 5}, {{5, 1, 5, 0}, {15, 1, 15, 0}, {16, 1, 16, 0}, {6, 1, 6, 0}}},
    [6] = {{15, 5, 11}, {{11, 1, 11, 0}, {15, 2, 15, 0}, {6, 1, 6, 0}, {16, 1, 16, 0}}},
    [7] = {{15, 11, 5}, {{0, 0, 0, 0}, {5, 1, 5, 0}, {16, 1, 16, 0}, {6, 1, 6, 0}}},
    [8] = {{11, 15, 5}, {{15, 1, 15, 0}, {5, 2, 5, 0}, {6, 1, 6, 0}, {16, 1, 16, 0}}},
    /* SIM-27 (docs/sim-drift8.md section 22.3, `jne` at 0x63F57): step 2 builds a neurology only when c15 = 2 (a
     * third one), so a second neurology is never built here */
    [9] = {{5, 15, 11}, {{5, 1, 5, 0}, {15, 2, 15, 1}, {16, 1, 16, 0}, {6, 1, 6, 0}}},
    [10] = {{11, 5, 15}, {{5, 1, 5, 0}, {5, 2, 5, 0}, {16, 1, 16, 0}, {6, 1, 6, 0}}},
    [11] = {{15, 5, 11}, {{5, 2, 5, 0}, {15, 2, 15, 0}, {6, 1, 6, 0}, {16, 1, 16, 0}}},
    [12] = {{5, 11, 15}, {{15, 1, 15, 0}, {11, 2, 11, 0}, {6, 1, 6, 0}, {16, 1, 16, 0}}},
    [13] = {{5, 15, 11}, {{15, 1, 15, 0}, {5, 2, 5, 0}, {6, 1, 6, 0}, {16, 1, 16, 0}}},
    [14] = {{15, 11, 5}, {{11, 1, 11, 0}, {5, 2, 5, 0}, {16, 1, 16, 0}, {6, 1, 6, 0}}},
    [15] = {{11, 5, 15}, {{5, 1, 5, 0}, {15, 2, 15, 0}, {16, 1, 16, 0}, {6, 1, 6, 0}}},
};
static const int32_t STEP_THRESHOLD[4] = {180, 230, 270, 310};

/* Bed-ward load (0x1B262 / 0x1B2DC): free beds; *short_beds gets sum of (catalog base - beds) ([0x5B97]). */
static int ward_free_beds(Sim *s, int h, int cat, int *short_beds) {
    int free_beds = 0, missing = 0;
    Description d = sim_description(s, cat);
    for (int i = 0; i < sim_rooms_in_use(s, h, cat); i++) {
        const uint8_t *rec = sim_room_record(s, h, cat, i);
        free_beds += rec[10] - rec[12];
        missing += catalog_u8(s, rec[d.type], CATALOG_BASE) - rec[10];
    }
    *short_beds = missing;
    return free_beds;
}

/* The occupancy of a bed ward category: 0x1B356 sums byte +12 of the Krankenzimmer (6, the OP's test, docs/sim-ai.md),
 * 0x1B3A2 u16 +0x0C of the Neuro-Krankenzimmer (16, the further torture chamber's test; SIM-28, docs/sim-drift8.md
 * section 22.3; the high byte was 0 in every save measured). */
static int occupied_sum(Sim *s, int h, int ward) {
    int sum = 0;
    for (int i = 0; i < sim_rooms_in_use(s, h, ward); i++) {
        const uint8_t *rec = sim_room_record(s, h, ward, i);
        sum += ward == 16 ? (uint16_t)get16(rec + 0x0C) : rec[12];
    }
    return sum;
}

int sim_free_icu(Sim *s, int h) {
    for (int i = 0; i < sim_rooms_in_use(s, h, 9); i++) {
        uint8_t *rec = sim_room_record(s, h, 9, i);
        if (!get16(rec + 12)) {
            sim_rating_record(s, rec);
            return i;
        }
    }
    return -1;
}

/* The category to build for AI hospital h, 0 for none (docs/sim-ai.md, "Order"). */
static int choose_category(Sim *s, int h) {
    World *w = s->world;
    const uint8_t *r = hospital(s, h);
    const uint8_t *c = room_counts(s, h);
    int f = get16(r + H_PLOTS_OWNED) + get16(r + H_PLOTS_RENTED) - get16(r + H_PLOTS_BUILT);
    int32_t x = get32(r + 0xE2), cash = get32(r + H_CASH);
    int hour = world_tail(w, TAIL_HOUR);
    if (!c[1]) return 1;
    if (!c[2]) return 2;
    if (!c[4]) return 4;
    if (!c[3]) return 3;
    int depts = c[5] + c[11] + c[15];
    int extra_waiting = 0;
    if (f == 0 && depts >= 2) {
        if (hour >= 8 || cash < 250 || x < 100) return 0;
        extra_waiting = 1;
    } else if (f > 0) {
        extra_waiting = 1;
    }
    if (extra_waiting) {
        int short_seats;
        int free_seats = waiting_free_seats(s, h, &short_seats);
        if (depts >= 6 && c[3] <= 9 && free_seats < 3 && short_seats == 0) return 3;
    }
    /* 4: the hospital's own plan */
    for (int i = 0; i < 3; i++)
        if (!c[PLAN[h].ensure[i]]) return PLAN[h].ensure[i];
    for (int i = 0; i < 4; i++) {
        const Step *st = &PLAN[h].step[i];
        if (!st->build) continue;
        if (f == 0 && x < STEP_THRESHOLD[i]) return 0;
        if (st->equal ? c[st->cat] == st->below : c[st->cat] < st->below) return st->build;
    }
    /* 5: the common plan */
    int n = (30 * (world_tail(w, TAIL_MONTH) - 1) + world_tail(w, TAIL_DAY)) % 9 + 2;
    if (f == 0 && x < 340) return 0;
    if (!c[13]) return 13;
    int short_beds;
    if (c[16] < 10 && ward_free_beds(s, h, 16, &short_beds) < 2 * (c[16] + 1) && short_beds == 0) return 16;
    /* the Krankenzimmer test compares with 2 x (a byte of the AI scratch block + 1), measured 0 in the running game,
     * so: free beds below 2 (docs/sim-ai.md, corrected 2026-10-18) */
    if (c[6] < 10 && ward_free_beds(s, h, 6, &short_beds) < 2 && short_beds == 0) return 6;
    if (!c[17]) return 17;
    if (!c[7]) return 7;
    if (!c[9]) return 9;
    if (!c[18]) return 18;
    /* the department tests (signed compares; the waiting counts are 0x1B496 / 0x1B51F / 0x1B5A8) */
    if (c[5] < 10 && c[5] < n && sim_waiting_for(s, h, 5) >= c[5] + 6) return 5;
    if (c[11] < 10 && c[11] < n && sim_waiting_for(s, h, 11) >= c[11] + 7) return 11;
    if (c[15] < 10 && c[15] < n && sim_waiting_for(s, h, 15) >= c[15] + 6) return 15;
    if (c[7] < 6 && occupied_sum(s, h, 6) >= 8 * c[7] + 5) return 7;
    if (c[9] < 10 && sim_free_icu(s, h) < 0 && c[9] < c[6] + 1) return 9;
    if (!c[8]) return 8;
    if (!c[12]) return 12;
    if (!c[14]) return 14;
    if (c[8] < 6 && occupied_sum(s, h, 16) >= 8 * c[8] + 5) return 8;
    if (!c[10]) return 10;
    return 0;
}

int sim_ai_choice(Sim *s, int h) { return choose_category(s, h); }

static void ai_building(Sim *s) {
    int h = sim_pick(s);
    if (!h || cash_of(s, h) < 0) return;
    int cat = choose_category(s, h);
    if (cat) build(s, h, cat);
}

/* ---- other module F routines -------------------------------------------------------------------- */

static void pay_ordered_plots(Sim *s) { /* 0x1C8E2 */
    int h = sim_pick(s);
    if (!h) return;
    uint8_t *r = hospital(s, h);
    int ordered = get16(r + 0xDE);
    if (ordered <= 0) return;
    add32(r + H_CASH, -44 * ordered);
    add16(r + H_PLOTS_RENTED, ordered);
    put16(r + 0xDE, 0);
}

static void buy_rented_room(Sim *s) { /* 0x50E59 */
    World *w = s->world;
    if (world_tail(w, TAIL_HOUR) < 21) return;
    int h = sim_pick(s);
    if (!h || (world_tail(w, TAIL_DAY) + h) % 2) return;
    uint8_t *r = hospital(s, h);
    int32_t m = get32(r + H_CASH) - (int32_t)sim_daily_costs(s, h) - 80;
    if (m < 500) return;
    uint8_t *slot = room_slots(s, h);
    for (int i = 0; i < ROOM_SLOTS; i++, slot += 2) {
        if ((int8_t)slot[0] < 0 || !slot[1]) continue;
        int price = catalog_u16(s, slot[0], CATALOG_PRICE);
        if (price > m) continue;
        int cost = price + price / 4;
        add32(r + H_CASH, -cost);
        add32(r + 0x14E, cost);
        add32(r + 0x152, cost);
        slot[1] = 0;
        return;
    }
}

static void buy_plot(Sim *s) { /* 0x51B1B */
    World *w = s->world;
    if (world_tail(w, TAIL_HOUR) < 21) return;
    int h = sim_pick(s);
    if (!h || (world_tail(w, TAIL_DAY) + h) % 2) return;
    uint8_t *r = hospital(s, h);
    const uint8_t *c = room_counts(s, h);
    if (get16(r + H_PLOTS_RENTED) <= 0) return;
    if (get32(r + H_CASH) - sim_daily_costs(s, h) < 500) return;
    if (c[5] + c[11] + c[15] < 3 || !c[5] || !c[11] || !c[15]) return;
    add32(r + H_CASH, -375);
    add32(r + 0x14E, 375);
    add32(r + 0x152, 375);
    add16(r + H_PLOTS_RENTED, -1);
    add16(r + H_PLOTS_OWNED, 1);
}

/* 0x336CA */
static void ai_pricing(Sim *s) {
    static const uint8_t PRESET[6][8] = {{4, 9, 7, 5, 4, 4, 5, 5}, {4, 9, 6, 4, 3, 3, 4, 7}, {3, 9, 4, 3, 3, 3, 4, 9},
                                         {3, 8, 3, 3, 2, 2, 3, 9}, {2, 7, 2, 2, 2, 2, 2, 9}, {2, 6, 2, 1, 1, 1, 2, 9}};
    World *w = s->world;
    int h = sim_pick(s);
    uint8_t *r = hospital(s, h);
    if (!h || !r[0x15]) return;
    world_price_index(w, s->prog);
    int hour = world_tail(w, TAIL_HOUR), day = world_tail(w, TAIL_DAY), month = world_tail(w, TAIL_MONTH);
    if (hour < 4 || hour > 20) return;
    int32_t k = (int32_t)sim_daily_costs(s, h), cash = get32(r + H_CASH);
    int beds = 0;
    for (int i = 0; i < sim_rooms_in_use(s, h, 6); i++) beds += sim_room_record(s, h, 6, i)[12];
    for (int i = 0; i < sim_rooms_in_use(s, h, 16); i++) beds += sim_room_record(s, h, 16, i)[12];
    int32_t d = cash - k + 40 * beds;
    int p = -1; /* preset 0-5, -1 = no change */
    if ((day + h) % 3 == 0) {
        p = d >= 500 ? 2 : d >= 240 ? 3 : d >= 10 ? 4 : 5;
    } else if (hour < 6) {
        if ((day + h) % 2) return;
        p = d >= 0 ? 0 : d > -251 ? 1 : d >= -400 ? 2 : d > -701 ? 3 : d >= -1400 ? 4 : 5;
    } else if (hour <= 10 && day == 1 && month == 1) {
        p = d > 399 ? 0 : d > 149 ? 1 : d > 9 ? 2 : d > -101 ? 3 : d >= -240 ? 4 : 5;
    } else if (hour <= 7) {
        const uint8_t *c = room_counts(s, h);
        int sdep = c[5] + c[11] + c[15];
        if (sdep > 2) d += 40 * (sdep - 2);
        p = d > 99 ? 0 : d > 9 ? 1 : d > -151 ? 2 : d > -301 ? 3 : d >= -420 ? 4 : 5;
    } else if (hour <= 10) {
        p = d > 199 ? 0 : d > 49 ? 1 : d > -101 ? 2 : d > -201 ? 3 : d >= -350 ? 4 : 5;
    } else if (hour <= 13) {
        d = cash - k + 20 * beds;
        p = d > 399 ? 0 : d > 199 ? 1 : d > -21 ? 2 : d > -121 ? 3 : d > -201 ? 4 : 5;
    } else if (hour <= 15) {
        d = cash - k;
        p = d > 449 ? 0 : d > 249 ? 1 : d > 19 ? 2 : d > -51 ? 3 : d > -101 ? 4 : 5;
    } else if (hour <= 17) {
        d = cash - k;
        p = d > 499 ? 0 : d > 299 ? 1 : d > 69 ? 2 : d > -31 ? 3 : d > -61 ? 4 : 5;
    } else {
        d = cash - k;
        p = d > 499 ? 0 : d > 299 ? 1 : d > 69 ? 2 : d > 39 ? -1 : 5;
    }
    if (p < 0) return;
    static const int KEYS[7] = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x94};
    for (int i = 0; i < 7; i++) r[KEYS[i]] = PRESET[p][i];
    r[0x95] = PRESET[p][7];
    world_price_index(w, s->prog);
    if (get16(r + 0x10A) != get16(r + 0x10C)) {
        if (sim_bits(s, 15) == 1) r[0x109]++, sim_news_price(s, h); /* 0x4763B */
        put16(r + 0x10C, get16(r + 0x10A));
    }
}

/* 0x3B5FB: income booked as one cured, billed patient in the first department. Returns 1 if booked. */
static int book_income(Sim *s, int h, int x) {
    uint8_t *r = hospital(s, h);
    const uint8_t *c = room_counts(s, h);
    add32(r + H_CASH, x);
    if (!c[5] && !c[11] && !c[15]) return 0;
    add16(r + 0x26, 1);
    add16(r + 0x28, 1);
    if (c[5]) add16(r + 0x2C, 1);
    else if (c[11]) add16(r + 0x30, 1), add16(r + 0x54, 1);
    else add16(r + 0x4E, 1), add16(r + 0x52, 1);
    add32(r + 0x8C, x);
    add32(r + 0x90, x);
    return 1;
}

static void take_money(Sim *s, int h) {
    for (int tries = 0; tries < 71; tries++) {
        int o = (int)sim_random(s, 15) + 1;
        if (o == h) continue;
        if (cash_of(s, h) < 120) continue; /* the original tests the needy hospital's own cash */
        int x = (int)sim_bits(s, 3) + 4;
        uint8_t *rh = hospital(s, h), *ro = hospital(s, o);
        add32(ro + H_CASH, -x);
        add32(rh + H_CASH, x);
        if (sim_bits(s, 1) == 0) {
            add16(rh + 0xB6, 1), add32(rh + 0xD2, x), add32(rh + 0xD6, x);
            add32(ro + 0xC2, x), add32(ro + 0xC6, x);
        } else {
            add16(rh + 0xB0, 1), add32(rh + 0xCA, x), add32(rh + 0xCE, x);
            add32(ro + 0xBA, x), add32(ro + 0xBE, x);
        }
        return;
    }
}

static void ai_income(Sim *s) {
    World *w = s->world;
    int hour = world_tail(w, TAIL_HOUR), minute = world_tail(w, TAIL_MINUTE);
    int day = world_tail(w, TAIL_DAY), month = world_tail(w, TAIL_MONTH);
    if (hour < 4) return;
    if (day == 1 && month == 1 ? sim_bits(s, 3) != 0 : sim_bits(s, 7) != 0) return;
    for (int h = 1; h < 16; h++) {
        int32_t cash = cash_of(s, h);
        if (cash < 0) continue;
        int32_t gap = cash - (int32_t)sim_daily_costs(s, h);
        if (cash < 20) {
            take_money(s, h);
            return;
        }
        if (hour >= 20) {
            if (gap > -40 && gap < 0) {
                take_money(s, h);
                return;
            }
            continue;
        }
        if (month != 1 || day > 4) continue;
        int x = -1;
        if (hour == 17 && minute > 19 && gap < -60) x = (int)sim_bits(s, 15) + 34;
        else if (hour == 18 && minute > 9 && gap < -19) x = (int)sim_bits(s, 15) + 26;
        else if (hour == 19 && minute < 40 && gap < 12) x = (int)sim_bits(s, 15) + 34;
        if (x >= 0 && book_income(s, h, x)) return;
    }
}

/* 0x3B8B7 */
static void close_bankrupt(Sim *s) {
    World *w = s->world;
    if (get16(w->block[BLK_34])) return; /* [0x552A] is the first word of save block 34 */
    for (int h = 1; h < 16; h++) {
        uint8_t *r = hospital(s, h);
        if (get32(r + H_CASH) >= 0 || !r[0x15]) continue;
        uint8_t *p = w->block[BLK_PATIENTS];
        for (int i = 1; i < 1009; i++) {
            uint8_t *q = p + i * 106;
            if (q[3] != h) continue;
            q[3] = q[4] = 0xFF;
            q[41] = 0, q[42] = q[43] = 0, q[44] = 100, q[45] = 0;
            memset(q + 48, 0, 10);
            q[66] = 0;
            memset(q + 67, 100, 11);
            q[78] = 50, q[79] = 0, q[82] = 0xFF, q[83] = 0, q[93] = 0;
            put16(q + 94, 700);
            memset(q + 96, 0, 10);
        }
        uint8_t *slot = staff_slots(s, h);
        for (int i = 0; i < STAFF_SLOTS; i++, slot += STAFF_SLOT_SIZE) {
            int index = get16(slot);
            if (index <= 0) continue;
            uint8_t *st = w->block[BLK_STAFF] + index * 32;
            st[3] = st[4] = 0xFF;
            st[11] = st[20] = st[23] = st[26] = 0;
            st[27] = 0xFF;
            memset(slot, 0, STAFF_SLOT_SIZE);
        }
        memset(room_counts(s, h), 0, 20);
        memset(room_slots(s, h), 0xFF, ROOM_SLOTS * ROOM_SLOT_SIZE);
        for (int i = 1; i < 1009; i++) p[i * 106 + 20 + h] = 0;
        memset(r + 0x1A, 0, 6);
        memset(r + 0x10E, 0, 4);
        r[0x15] = 0;
        memset(r + 0x38, 0, 12);
    }
}

/* ---- module G: markets -------------------------------------------------------------------------- */

static uint8_t *market(Sim *s, int i) { return s->world->block[BLK_MARKET] + i * 10; }
uint8_t *sim_market(Sim *s, int i) { return market(s, i); }
static int free_market_slot(Sim *s) {
    for (int i = 0; i < 100; i++)
        if ((int8_t)market(s, i)[0] < 0) return i;
    return -1;
}

static void dealer_blood(Sim *s) { /* 0x2FB9B */
    for (int i = 0; i < 100; i++)
        if ((int8_t)market(s, i)[0] >= 0 && market(s, i)[1] == 0) return;
    int i = free_market_slot(s);
    if (i < 0) return;
    uint8_t *m = market(s, i);
    int q = (int)sim_bits(s, 7) * 50 + 400;
    memset(m, 0, 10);
    m[0] = DEALER;
    put16(m + 2, q);
    put16(m + 4, q / 32 + 8 + (int)sim_bits(s, 7));
}

static int organ_price(Sim *s, int id) {
    int base = program_u8(s->prog, ORGAN_BASE, 2u * (uint32_t)id); /* the byte at 0x4712 + 2 x id */
    return base + (int)sim_bits(s, 7) * (base / 16 + 1);
}
int sim_organ_price(Sim *s, int id) { return organ_price(s, id); }

static void dealer_organ(Sim *s) { /* 0x1BB7E */
    if (sim_bits(s, 7) != 4) return;
    int id = program_u8(s->prog, ORGANS, sim_bits(s, 7));
    int n = 0;
    for (int i = 0; i < 100; i++)
        if ((int8_t)market(s, i)[0] >= 0 && market(s, i)[1] == 1 && market(s, i)[2] == id) n++;
    if (n >= 5) return;
    int i = free_market_slot(s);
    if (i < 0) return;
    uint8_t *m = market(s, i);
    memset(m, 0, 10);
    m[0] = DEALER, m[1] = 1, m[2] = (uint8_t)id;
    put16(m + 4, organ_price(s, id));
    m[3] = (uint8_t)(sim_bits(s, 31) + 69);
}

/* 0x5A141: an AI hospital with a blood bank and an OP buys the organ its OP is waiting for (+0x86) */
static void ai_buys_organ(Sim *s) {
    int h = sim_pick(s);
    if (!h || !room_counts(s, h)[17] || !room_counts(s, h)[7]) return;
    uint8_t *bank = sim_room_record(s, h, 17, 0), *r = hospital(s, h);
    int held = get16(bank + 0x14), want = get16(r + 0x86);
    if (get16(bank + 0x16) <= held || !want) return;
    for (int i = 0; i < 100; i++) {
        uint8_t *m = market(s, i);
        if ((int8_t)m[0] < 0 || m[1] != 1 || m[2] != want || m[8] || m[0] == h || m[3] < 60) continue;
        int price = (uint16_t)get16(m + 4);
        uint8_t *seller = hospital(s, m[0]);
        add32(r + H_CASH, -price);
        add32(r + 0x72, price);
        add32(r + 0x6E, price);
        add16(r + 0x7E, 1);
        add32(seller + H_CASH, price);
        add32(seller + 0x7A, price);
        add32(seller + 0x76, price);
        if (held < 30) bank[0x58 + 2 * held] = m[2], bank[0x59 + 2 * held] = m[3]; /* engine choice: "the next
                                                                                      * slot" = slot number held */
        put16(bank + 0x14, held + 1);
        put16(r + 0x86, 0);
        m[0] = 0xFF;
        return;
    }
}

static uint8_t *ai_bank(Sim *s, int h) { return room_counts(s, h)[17] ? sim_room_record(s, h, 17, 0) : NULL; }

/* 0x3E3C7: an AI bank holding 2,000 cl or more offers 400-750 cl at once (taken from its stock); nothing is booked */
static void ai_sells_blood(Sim *s) {
    int h = sim_pick(s);
    uint8_t *bank = h ? ai_bank(s, h) : NULL;
    if (!bank || get32(bank + 0x0C) < 2000) return;
    int i = free_market_slot(s);
    if (i < 0) return;
    uint8_t *m = market(s, i);
    int q = (int)sim_bits(s, 7) * 50 + 400;
    memset(m, 0, 10);
    m[0] = (uint8_t)h;
    put16(m + 2, q);
    add32(bank + 0x0C, -q);
    put16(m + 4, q / 32 + 6 + (int)sim_bits(s, 7));
}

/* 0x59EB4: an AI hospital with a bank and an OP, stock below 1,000 and room for 800 or more, buys the blood offer
 * with the lowest price per 5,000 cl (ties: the later slot) that fits and that it can pay with 50 to spare. No
 * staff test. An offer of 0 cl is skipped (engine choice: the original divides by its quantity). */
static void ai_buys_blood(Sim *s) {
    int h = sim_pick(s);
    if (!h || !room_counts(s, h)[7]) return;
    uint8_t *bank = ai_bank(s, h), *r = hospital(s, h);
    if (!bank) return;
    int32_t stock = get32(bank + 0x0C), cap = get32(bank + 0x10), cash = get32(r + H_CASH);
    if (stock >= 1000 || !cap || cap - stock < 800) return;
    int best = -1;
    uint32_t best_value = 100000;
    for (int i = 0; i < 100; i++) {
        uint8_t *m = market(s, i);
        int q = (uint16_t)get16(m + 2), price = (uint16_t)get16(m + 4);
        if ((int8_t)m[0] < 0 || m[8] || m[1] != 0 || m[0] == h || !q) continue;
        if (q + stock >= cap || price + 50 >= cash) continue;
        uint32_t value = ((uint32_t)price << 14) / (uint32_t)q * 5000u >> 14;
        if (value <= best_value) best_value = value, best = i;
    }
    if (best < 0) return;
    uint8_t *m = market(s, best), *seller = hospital(s, m[0]);
    int q = (uint16_t)get16(m + 2), price = (uint16_t)get16(m + 4);
    add32(r + H_CASH, -price);
    add32(r + 0x6E, price);
    add32(r + 0x72, price);
    add32(r + 0x62, q);
    add32(bank + 0x0C, q);
    add32(seller + H_CASH, price);
    add32(seller + 0x76, price);
    add32(seller + 0x7A, price);
    add32(seller + 0x5E, q);
    m[0] = 0xFF;
}

/* 0x5A041: an AI bank holding 2 or more organs offers the first slot of the first organ id 1-11 it holds twice */
static void ai_sells_organ(Sim *s) {
    int h = sim_pick(s);
    uint8_t *bank = h ? ai_bank(s, h) : NULL;
    if (!bank || get16(bank + 0x14) < 2) return;
    int count[12] = {0}, id = 0;
    for (int i = 0; i < 30; i++)
        if (bank[0x58 + 2 * i] >= 1 && bank[0x58 + 2 * i] <= 11) count[bank[0x58 + 2 * i]]++;
    for (int k = 1; k <= 11 && !id; k++)
        if (count[k] >= 2) id = k;
    if (!id) return;
    int i = free_market_slot(s);
    if (i < 0) return;
    uint8_t *slot = bank + 0x58;
    while (slot[0] != id) slot += 2;
    uint8_t *m = market(s, i);
    memset(m, 0, 10);
    m[0] = (uint8_t)h, m[1] = 1, m[2] = (uint8_t)id, m[3] = slot[1];
    put16(slot, 0);
    add16(bank + 0x14, -1);
    put16(m + 4, organ_price(s, id));
    sim_bank_compact(s);
}

static void car_offers(Sim *s) { /* 0x1BAEF */
    uint8_t *cars = s->world->block[BLK_CARS];
    int used = 0, free_i = -1;
    for (int i = 0; i < 20; i++) {
        if ((int8_t)cars[i * 20] >= 0) used++;
        else if (free_i < 0) free_i = i;
    }
    if (used >= 15 || free_i < 0) return;
    uint8_t *c = cars + free_i * 20;
    int id = (int)sim_random(s, 7), cond = (int)sim_bits(s, 63) + 37;
    int list = (uint16_t)program_word(s->prog, CARS, (uint32_t)id * 20);
    c[0] = DEALER, c[1] = (uint8_t)id;
    put16(c + 2, list - (list / 100 * (100 - cond)) / 2);
    c[4] = (uint8_t)cond, c[5] = 0;
}

static void car_offers_age(Sim *s) { /* 0x1AB70; nothing expires while the player is on the vehicle market (130) */
    uint8_t *cars = s->world->block[BLK_CARS];
    for (int i = 0; i < 20; i++) {
        uint8_t *c = cars + i * 20;
        if ((int8_t)c[0] < 0) continue;
        c[5]++;
        if (s->screen != 130 && c[5] > (c[0] == DEALER ? 7 : 24)) c[0] = 0xFF;
    }
}

static void market_refill(Sim *s) { /* 0x2FA41 */
    uint8_t *eq = s->world->block[BLK_EQUIPMENT_MARKET];
    int slot = -1;
    for (int i = 0; i < 250 && slot < 0; i++)
        if ((int8_t)eq[i * 8] < 0) slot = i;
    if (slot >= 0) {
        int want = -1;
        for (int d = 1; d <= 66 && want < 0; d++) {
            int covered = 0;
            for (int i = 0; i < 300 && !covered; i++) {
                if ((int8_t)eq[i * 8] < 0) continue;
                const uint8_t *it = sim_equipment_item(s, (uint16_t)get16(eq + i * 8 + 2));
                for (int k = 10; k < 14; k++) covered |= it[k] == d;
            }
            if (!covered) want = d;
        }
        uint8_t *o = eq + slot * 8;
        int id = -1;
        for (int tries = 0; tries < 300 && id < 0; tries++) {
            int cand = (int)sim_random(s, 154) + 1;
            const uint8_t *it = sim_equipment_item(s, cand);
            if (want < 0 || it[10] == want || it[11] == want || it[12] == want || it[13] == want) id = cand;
        }
        o[0] = DEALER;
        if (id >= 0) {
            o[1] = (uint8_t)(get16(sim_equipment_item(s, id) + 8) + 2);
            put16(o + 2, id);
            o[4] = 100, o[5] = 0;
            put16(o + 6, 0);
        }
    }
    uint8_t *fu = s->world->block[BLK_FURNITURE_MARKET];
    for (int i = 0; i < 250; i++) {
        uint8_t *o = fu + i * 8;
        if ((int8_t)o[0] >= 0) continue;
        int id = (int)sim_random(s, 76) + 1;
        o[0] = DEALER;
        o[1] = (uint8_t)(get16(sim_furniture_item(s, id) + 8) + 2);
        put16(o + 2, id);
        o[4] = 100, o[5] = 0;
        put16(o + 6, 0);
        return;
    }
}

/* 0x3DAEB: offers age; the current player-0 office view also releases reservations (SIM-103). */
static void offers_age(Sim *s) {
    World *w = s->world;
    /* context M (docs/rng-sites.md 3.7, 2.4, 2.5): D / EBP the clock call's pair, [0x4528] = what its last writer
     * left, the waiting-room tick (tick 74): the record of the last waiting room it handled + 0x14 */
    SimIn in = rng_offers(s);
    for (int i = 0; i < 100; i++) {
        uint8_t *m = market(s, i);
        if (s->screen == 1 && m[8]) {
            m[8] = 0;
            if (get16(m + 6) < 0) {
                put16(m + 6, 0);
                m[0] = 0xFF;
            }
        }
        if ((int8_t)m[0] < 0) continue;
        if (get16(m + 6) >= 0) add16(m + 6, 1);
        if (m[1] == 0) {
            if (get16(m + 6) >= 800 && !m[8]) { /* SIM-81: expiry includes the new age 800 */
                add32(hospital(s, m[0]) + 0x66, (uint16_t)get16(m + 2));
                m[0] = 0xFF;
            }
        } else if (sim_bits_in(s, 3, 0x3DB3F, in) == 0) {
            /* the four draws in the original's order: r1, r3, r1, r1 */
            int drop = (int)sim_bits_in(s, 1, 0x3DB4A, in);
            drop += (int)sim_bits_in(s, 3, 0x3DB51, in);
            drop += (int)sim_bits_in(s, 1, 0x3DB58, in);
            drop += (int)sim_bits_in(s, 1, 0x3DB5F, in);
            drop /= 4;
            uint8_t q = (uint8_t)(m[3] - drop); /* SIM-106: wrap before the signed-byte threshold */
            if ((int8_t)q < 30) {
                if (m[8]) q = 1;
                else {
                    add16(hospital(s, m[0]) + 0x82, 1);
                    m[0] = 0xFF;
                }
            }
            m[3] = q;
        }
    }
    uint8_t *eq = w->block[BLK_EQUIPMENT_MARKET];
    for (int i = 0; i < 300; i++) {
        uint8_t *o = eq + i * 8;
        if ((int8_t)o[0] < 0) continue;
        add16(o + 6, 1);
        if (o[5]) {
            if (s->screen != 1) continue;
            o[5] = 0;
        }
        if (get16(o + 6) > 39 && o[0] != 0) {
            add16(hospital(s, o[0]) + 0xB2, 1);
            o[0] = 0xFF;
        }
    }
    uint8_t *fu = w->block[BLK_FURNITURE_MARKET];
    for (int i = 0; i < 300; i++) {
        uint8_t *o = fu + i * 8;
        if ((int8_t)o[0] < 0) continue;
        add16(o + 6, o[5] ? 1 : 2);
        if (!o[5] && get16(o + 6) > 39 && o[0] != 0) {
            add16(hospital(s, o[0]) + 0xB8, 1);
            o[0] = 0xFF;
        }
    }
}

/* ---- 0x4E1BD / 0x4E368: AI projects (docs/sim-ai.md; guess: advertising campaigns) -------------------- */

enum { PR_TYPE = 0xF4, PR_LEVEL = 0xF5, PR_DURATION = 0xF6, PR_COST = 0xF8, PR_LEFT = 0xFA, PR_GOOD = 0xFC,
       PR_NEUTRAL = 0xFE, PR_BAD = 0x100, PR_CLEAR_END = 0x102, PR_DAY = 0x102, PR_END = 0x106, PR_COOL = 0x108,
       PR_NET = 0x142, PR_TOTAL = 0x146 };
#define PROJECT_PEOPLE 0xA867 /* byte per hour: people reached per run, minus 1 */

static int project_cost(int type, int level, int duration) { /* 0x4D463: the product's low word, shifted right once */
    int add = type == 1 ? 3 : type == 3 ? 6 : 5; /* any type other than 1-3 counts as 2 (docs/ui-support.md) */
    return (int)(((unsigned)((level + add) * duration) & 0xFFFF) >> 1);
}
int sim_project_cost(int type, int level, int duration) { return project_cost(type, level, duration); }

static void project_clear(uint8_t *r) { memset(r + PR_TYPE, 0, PR_CLEAR_END - PR_TYPE); }

/* 0x4E1BD (tick 35): an AI hospital with 4+ departments ends a finished project or starts one. A = cash - K, the
 * day's fixed costs (0x1AEC0); X = the expected cash +0xE2. */
static void project_start(Sim *s) {
    int h = sim_pick(s);
    if (!h) return;
    const uint8_t *c = room_counts(s, h);
    if (c[5] + c[11] + c[15] < 4) return;
    uint8_t *r = hospital(s, h);
    if (r[PR_TYPE]) {
        if (get16(r + PR_LEFT) < 0) project_clear(r);
        return;
    }
    int hour = world_tail(s->world, TAIL_HOUR);
    if (hour < 10 || hour > 16 || r[PR_COOL]) return;
    int32_t cash = get32(r + H_CASH), a = cash - (int32_t)sim_daily_costs(s, h), x = get32(r + 0xE2);
    if (a < 50 || cash < 700 || x < 50) return;
    int type = (int)sim_random(s, 3) + 1, level = x / 64 > 11 ? 11 : (int)(x / 64);
    int duration = (int)sim_bits(s, 31) + 20;
    r[PR_TYPE] = (uint8_t)type;
    r[PR_LEVEL] = (uint8_t)level;
    put16(r + PR_LEFT, 0), put16(r + PR_GOOD, 0), put16(r + PR_NEUTRAL, 0), put16(r + PR_BAD, 0);
    int cost = project_cost(type, level, duration);
    while (2 * cost >= a || 2 * cost >= cash) cost = project_cost(type, level, --duration);
    if (duration <= 9) {
        project_clear(r);
        return;
    }
    put16(r + PR_DURATION, duration);
    put16(r + PR_COST, cost);
    add32(r + H_CASH, -cost);
    add32(r + PR_DAY, cost);
    add32(r + PR_TOTAL, cost);
    put16(r + PR_LEFT, duration);
}

/* -1 normal, -2 scandal; then the newspaper item 0x4E58A (module H, docs/news-generators.md section 8), for every
 * hospital, the player's included */
static void project_end(Sim *s, int h, uint8_t *r, int how) {
    put16(r + PR_LEFT, how);
    r[PR_COOL] = 30;
    put16(r + PR_END, -1);
    sim_news_campaign(s, h);
}

static void project_effect(uint8_t *r, uint8_t *o, int d, int counter) {
    int v = (int8_t)*o + d; /* engine choice: the opinion read as a signed byte, then kept within 0-100 */
    *o = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
    add16(r + counter, 1);
    add32(r + PR_NET, d);
}

/* 0x4E368 (tick 13, once per game minute): the running projects reach (byte 0xA867 + hour) + 1 random people. */
static void project_run(Sim *s) {
    int hour = world_tail(s->world, TAIL_HOUR);
    uint8_t *people = s->world->block[BLK_PATIENTS];
    for (int h = 0; h < 16; h++) {
        uint8_t *r = hospital(s, h);
        if (!r[PR_TYPE] || get16(r + PR_LEFT) <= 0) continue;
        add16(r + PR_LEFT, -1);
        if (!get16(r + PR_LEFT)) {
            project_end(s, h, r, -1);
            continue;
        }
        int n = program_u8(s->prog, PROJECT_PEOPLE, (uint32_t)hour) + 1, type = r[PR_TYPE];
        for (int k = 0; k < n; k++) {
            uint8_t *p = people + ((int)sim_random(s, 1008) + 1) * 106;
            /* no opinion penalty for +0x172 >= 4: that is copy protection (docs/mainloop.md, section 7) */
            if (hour < 5 || hour > 20 || p[104] || !p[1] || (int8_t)p[3] >= 0) continue;
            int roll = (int)sim_random(s, type == 1 ? 70 : type == 2 ? 90 : 120);
            if (roll < (p[0] == 1 ? p[5] / 2 : p[5])) continue;
            int q = 8 * r[PR_LEVEL] + 60 - r[PR_COOL], effect; /* 2 positive, 1 neutral-or-positive, -1 negative */
            uint8_t *o = p + 20 + h;
            if (sim_bits(s, 31) == 22) effect = -1;
            else if ((int)sim_bits(s, 127) < q) effect = (int)sim_bits(s, 127) < q ? 2 : 1;
            else if ((int)sim_bits(s, 127) < q) effect = 1;
            else if (sim_bits(s, 127) == 47) {
                project_end(s, h, r, -2); /* scandal */
                break;
            } else effect = -1;
            if (effect == 1 && sim_bits(s, 15) == 4) effect = 2;
            if (effect == 2) project_effect(r, o, 2, PR_GOOD);
            else if (effect == -1) project_effect(r, o, -1, PR_BAD);
            else add16(r + PR_NEUTRAL, 1);
        }
    }
}

/* ---- dispatch --------------------------------------------------------------------------------- */

int sim_ai_run(Sim *s, uint32_t routine) {
    switch (routine) {
    case 0x1A941: sim_expected_cash_all(s); return 1;
    case 0x6337D: ai_building(s); return 1;
    case 0x1C8E2: pay_ordered_plots(s); return 1;
    case 0x50E59: buy_rented_room(s); return 1;
    case 0x51B1B: buy_plot(s); return 1;
    case 0x336CA: ai_pricing(s); return 1;
    case 0x3B5FB: ai_income(s); return 1;
    case 0x3B8B7: close_bankrupt(s); return 1;
    case 0x2FB9B: dealer_blood(s); return 1;
    case 0x1BB7E: dealer_organ(s); return 1;
    case 0x5A141: ai_buys_organ(s); return 1;
    case 0x3E3C7: ai_sells_blood(s); return 1;
    case 0x59EB4: ai_buys_blood(s); return 1;
    case 0x5A041: ai_sells_organ(s); return 1;
    case 0x1BAEF: car_offers(s); return 1;
    case 0x1AB70: car_offers_age(s); return 1;
    case 0x2FA41: market_refill(s); return 1;
    case 0x3DAEB: offers_age(s); return 1;
    case 0x4E1BD: project_start(s); return 1;
    case 0x4E368: project_run(s); return 1;
    default: return 0;
    }
}
