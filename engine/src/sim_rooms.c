/* Module C, patients in rooms, from docs/sim-rooms.md and docs/sim-rooms2.md: reception queue -> waiting room ->
 * treatment room -> discharge with bill, the Krankenzimmer queue and ward, intensive care, the OP, the torture
 * chamber, death, the pathology and the blood and organ bank. Engine choices where the spec is open are marked
 * "open" or "engine choice". The reception's forms and pens, the diagnosis start and the treatment rooms, intensive
 * care, the OP, the torture chamber and the bank check their equipment and take it from the store (its market
 * purchase 0x2F3FF is in sim_staff.c). Saved notification flags are implemented where measured; other notification
 * call sites remain incomplete. The newspaper items are written through sim_news.c (module H). */
#include <string.h>

#include "sim_internal.h"

#define PATIENT_SIZE 106
#define ILLNESSES 0x954
#define TREATMENTS 0x270C
#define WISHES 0x2066
#define WAIT_PRIORITY 0x486F /* 10 bytes: 0 men, 1 women, 2 anyone */
#define BED_PRIORITY 0x4DCC
#define STEP_DURATION 0x44A5
#define SCREEN_INVENTORY 0x41 /* the player's inventory, screen 65 */

enum { P_SEX = 0, P_PRIVATE = 2, P_HOSPITAL = 3, P_STATE = 4, P_OPINION = 20, P_AILMENT = 41, P_DIAGNOSIS = 42,
       P_TREATMENT = 43, P_HEALTH = 44, P_STEPS = 45, P_UNITS = 48, P_ILLNESS = 56, P_WISH = 57, P_ROOM = 66,
       P_MOOD = 78, P_IN_TREATMENT = 79, P_TREATED_BY = 81, P_SKILL = 83, P_IN_STEP = 93, P_BLOOD = 94,
       P_FIELD100 = 100, P_ROUNDS = 103, P_DEAD = 104, P_COUNTDOWN = 105 };
enum { P_SAVED_STATE = 98, P_SAVED_ROOM = 99, P_BLEEDING = 101, P_RECOVERY = 102 };
enum { ST_RECEPTION = 2, ST_WAITING = 3, ST_TREATMENT = 5, ST_WARD = 6, ST_OP = 7, ST_TORTURE = 8, ST_ICU = 9,
       ST_NEURO_WARD = 16, ST_PATHOLOGY = 18 };

static uint8_t *person(Sim *s, int i) { return s->world->block[BLK_PATIENTS] + i * PATIENT_SIZE; }
static const uint8_t *illness(Sim *s, int id) { return program_table(s->prog, ILLNESSES) + id * 20; }
static const uint8_t *treatment(Sim *s, int id) { return program_table(s->prog, TREATMENTS) + id * 38; }
static int image_byte(Sim *s, uint32_t table, uint32_t index) { return program_u8(s->prog, table, index); }
static uint8_t *staff_record(Sim *s, int i) { return s->world->block[BLK_STAFF] + i * 32; }
/* Saved-block effects of 0x2EA97 (docs/sim-rooms2.md, SIM-88). AI hospitals select unsaved scratch instead. */
static void notify_watched_patient(Sim *s, int h, int index, int flag) {
    if (h < 0 || h > 1) return;
    uint8_t *pb = s->world->block[h == 0 ? BLK_PLAYER0 : BLK_PLAYER1];
    if (get16(pb + 0x2C) == index) pb[flag] = 0xFF;
}
static void work_done(Sim *s, int staff_index) { /* the staff record's work counter +0x10 */
    if (staff_index) add16(staff_record(s, staff_index) + 0x10, 1);
}
static void opinion_add(uint8_t *p, int h, int d) {
    int o = p[P_OPINION + h] + d;
    p[P_OPINION + h] = (uint8_t)(o < 0 ? 0 : o > 100 ? 100 : o);
}

/* ---- resets and death ------------------------------------------------------------------------------ */

void sim_reset_healthy(uint8_t *p) { /* 0x30DE6 */
    p[P_HOSPITAL] = p[P_STATE] = 0xFF;
    p[P_AILMENT] = 0;
    put16(p + P_DIAGNOSIS, 0);
    p[P_HEALTH] = 100;
    p[P_STEPS] = 0;
    memset(p + P_UNITS, 0, 8);
    put16(p + P_ILLNESS, 0);
    p[P_ROOM] = 0;
    memset(p + 67, 100, 11);
    p[P_MOOD] = 50, p[P_IN_TREATMENT] = 0, p[82] = 0xFF, p[P_SKILL] = 0, p[P_IN_STEP] = 0;
    put16(p + P_BLOOD, 700);
    memset(p + 96, 0, 8);
    put16(p + P_DEAD, 0);
}

/* 0x3200C (docs/sim-rooms.md, "The rename of the dead"), just before the reset at home after a death or a pathology
 * case: the first word of the person's name (n bytes before the first space, '_' counted) is overwritten with the
 * first n-byte stock name of text 4012 + n, the men's (variant 0) or, for a woman (field 0 non-zero), the women's
 * (variant 8, offset 8 n). The original's r7 is multiplied by a cleared register and changes nothing; it is drawn all
 * the same, so the generator sequence stays the original's. The name pool is the session's copy of BIDATP, never
 * saved. Engine guards (never met in the shipped data, whose first words are 3-12 bytes): no space, n outside 3-12 or
 * a stock text too short leave the name. */
void sim_rename_dead(Sim *s, int index) {
    sim_bits(s, 7);
    if (!s->people || !s->texts) return;
    size_t len, stock_len;
    uint8_t *name = (uint8_t *)datadb_pool_entry(s->people, DAT_PATIENT_NAMES, index, &len); /* the session's copy */
    size_t n = 0;
    while (n < len && name[n] != ' ') n++;
    if (n == len || n < 3 || n > 12) return;
    int variant = person(s, index)[0] ? 8 : 0;
    const uint8_t *stock = textdb_entry(s->texts, 4012 + (int)n, &stock_len);
    if (stock_len < n * (size_t)(variant + 1)) return;
    memcpy(name, stock + n * (size_t)variant, n);
}

/* The pathology record of hospital h when it has one with room (u16 +6 listed != u16 +8 places), else NULL. */
static uint8_t *pathology_place(Sim *s, int h) {
    if (!room_counts(s, h)[18]) return NULL;
    uint8_t *rec = sim_room_record(s, h, 18, 0);
    return get16(rec + 6) != get16(rec + 8) ? rec : NULL;
}

/* SIM-79 (docs/sim-rooms.md, "Death and its paths"): 0x31299 indexes a pathology-sized record with the low
 * byte of the room-count row's address. Blocks 7-15 were adjacent in the original heap; resolve bytes through
 * their separate allocations here. A malformed world must never turn an emulated offset into a host overrun. */
static uint8_t *death_heap_byte(Sim *s, size_t offset) {
    for (int b = 6; b <= 14; b++) {
        if (offset < s->world->size[b]) return s->world->block[b] ? s->world->block[b] + offset : NULL;
        offset -= s->world->size[b];
    }
    return NULL;
}
static int death_heap_word(Sim *s, size_t offset) {
    const uint8_t *lo = death_heap_byte(s, offset), *hi = death_heap_byte(s, offset + 1);
    return lo && hi ? *lo | *hi << 8 : 0;
}
static void death_heap_put(Sim *s, size_t offset, int value) {
    uint8_t *lo = death_heap_byte(s, offset), *hi = death_heap_byte(s, offset + 1);
    if (lo && hi) *lo = (uint8_t)value, *hi = (uint8_t)(value >> 8);
}
static int death_pathology_place(Sim *s, int h) {
    if (!room_counts(s, h)[18]) return -1;
    int row = (uint8_t)((program_english(s->prog) ? 0x40 : 0xB0) + 100 * h);
    int offset = 158 * row;
    return death_heap_word(s, offset + 6) != death_heap_word(s, offset + 8) ? offset : -1;
}

/* 0x31299 (a death in a per-tick routine) and 0x522D2 plus the list entry (a death found by a room operator,
 * docs/sim-rooms2.md "Dead patients"): both count the death at +0x38 and then either list the person in the
 * pathology (field 66 = the state it died in, state 18) or, without a place there, reset it at home (0x30DE6;
 * after the rename 0x3200C) and count +0x3C. They differ in the newspaper item 0x4C904:
 * 0x522D2 always writes one and uses the actual hospital's pathology; 0x31299 uses the alias above and only writes
 * an item when that has no place. `icu` is the intensive-care record (k
 * of hospital h) that 0x70321 hands 0x31299 as the person: its item is the one of docs/sim-rooms2.md, "The death item
 * of 0x70321 without a pathology place" (SIM-18), the rest is the normal death of the person. */
static void died_as(Sim *s, int h, int index, int common, const uint8_t *icu, int k) {
    uint8_t *r = hospital(s, h), *p = person(s, index), *path = common ? pathology_place(s, h) : NULL;
    int alias = common ? -1 : death_pathology_place(s, h);
    int has_place = path != NULL || alias >= 0;
    add16(r + 0x38, 1);
    /* FIX-11 (docs/fixes.md): original writes the garbled item of the intensive-care record; fixed the person's own
     * normal death item */
    if (icu && !has_place && !fix_on(11)) sim_news_icu_death(s, h, k, icu);
    else if (common || !has_place) sim_news_death(s, h, index);
    p[P_ROOM] = p[P_STATE];
    if (!has_place) {
        sim_rename_dead(s, index); /* 0x312EF in 0x31299, 0x5231C in 0x522D2 */
        sim_reset_healthy(p);
        add16(r + 0x3C, 1);
        return;
    }
    p[P_STATE] = ST_PATHOLOGY;
    if (common) {
        if (get16(path + 6) < 20) put16(path + 0x58 + 2 * get16(path + 6), index); /* 20 list words */
        add16(path + 6, 1);
    } else {
        uint16_t displacement = (uint16_t)(0x58 + 2 * death_heap_word(s, alias + 6));
        death_heap_put(s, alias + displacement, index);
        death_heap_put(s, alias + 6, death_heap_word(s, alias + 6) + 1);
    }
}
static void died(Sim *s, int h, int index) { died_as(s, h, index, 0, NULL, 0); } /* 0x31299, the per-tick routines */
static void died_found(Sim *s, int h, int index) { died_as(s, h, index, 1, NULL, 0); } /* 0x522D2, the room operators */

/* ---- the invoice and what it does to the opinion -------------------------------------------------- */

/* 0x2E148 (docs/economy.md, "Prices and the invoice") */
int sim_invoice(Sim *s, int h, const uint8_t *p) {
    static const struct { uint8_t field, level; } UNITS[7] = {{50, 0x20}, {51, 0x21}, {52, 0x22}, {53, 0x23},
                                                               {54, 0x24}, {55, 0x25}, {48, 0x94}};
    const uint8_t *r = hospital(s, h);
    int bill = r[0x95];
    for (int k = 0; k < 7; k++) {
        int u = p[UNITS[k].field], level = r[UNITS[k].level];
        if (u && p[P_PRIVATE]) u = u + u / 2 + 1;
        if (level) bill += u / level;
    }
    return bill;
}

/* the opinion step v of 0x320FE (bed wards) and 0x32075 (treatment rooms) before it is applied; the player's
 * invoice screen computes it first with [0x5C2B] set (docs/ui-rooms.md, "Invoice message") */
int sim_bill_reaction(const uint8_t *p, int bill, int ward) {
    int priv = p[P_PRIVATE];
    if (ward && !priv) return bill < 30 ? 3 : bill < 40 ? 2 : bill < 50 ? 1 : -(bill / 16);
    if (ward) return bill < 40 ? 3 : bill < 60 ? 2 : bill < 90 ? 1 : -((bill - 90) / 32);
    if (!priv) return bill < 4 ? 3 : bill < 6 ? 2 : bill < 8 ? 1 : -((bill - 8) / 2);
    return bill < 5 ? 3 : bill < 8 ? 2 : bill < 11 ? 1 : -((bill - 11) / 2);
}

/* 0x320FE (bed wards) and 0x32075 (treatment rooms), docs/sim-rooms2.md "Resolved open points" with the correction of
 * docs/sim-drift2.md section 4: 8-bit adds with no clamp between the steps and one clamp (signed byte, 0-100) at the
 * end. v > 0: the thresholds read the opinion before the bill. v < 0: they read the RUNNING opinion (signed byte): the
 * 75 test after the two v steps, the 90 test after the (v - 1) step. */
static void opinion_after_bill(uint8_t *p, int h, int bill, int ward) {
    int v = sim_bill_reaction(p, bill, ward);
    /* SIM-56: a computed 0 takes the v >= 0 branch (from opinion 75 the halving makes it + 1). FIX-49 (docs/fixes.md):
     * original v = 0 raises the opinion from 75 on; fixed v = 0 changes nothing */
    if (!v && fix_on(49)) return;
    uint8_t o = p[P_OPINION + h];
    if (v >= 0) {
        int before = o;
        if (before >= 75) v = v / 2 + 1;
        if (before >= 90) v = v / 2 + 1;
        for (int k = 0; k < (before >= 90 ? 3 : 4); k++) o = (uint8_t)(o + v);
    } else {
        o = (uint8_t)(o + v), o = (uint8_t)(o + v);
        if ((int8_t)o >= 75) {
            o = (uint8_t)(o + v - 1);
            if ((int8_t)o >= 90) o = (uint8_t)(o + v - 2), o = (uint8_t)(o + v - 2);
        }
    }
    int8_t f = (int8_t)o;
    p[P_OPINION + h] = (uint8_t)(f < 0 ? 0 : f > 100 ? 100 : f);
}
void sim_opinion_after_bill(uint8_t *p, int h, int bill, int ward) { opinion_after_bill(p, h, bill, ward); }

static void pay(Sim *s, int h, int bill) {
    uint8_t *r = hospital(s, h);
    add32(r + H_CASH, bill);
    add32(r + 0x8C, bill);
    add32(r + 0x90, bill);
}

/* ---- reception -> waiting room (0x2B989) ----------------------------------------------------------- */

static uint8_t *reception(Sim *s, int h) { return sim_room_record(s, h, 2, 0); }

static void queue_up(uint8_t *queue, int places) { /* the queue moves up one place */
    memmove(queue, queue + 4, (size_t)(places - 1) * 4);
    memset(queue + (places - 1) * 4, 0, 4);
}

/* the seat search of 0x2B989: up to 199 tries; 1 with the waiting room and the place when a seat was found */
static int seat_search(Sim *s, int h, const uint8_t *p, int *room, int *seat) {
    int rooms = sim_rooms_in_use(s, h, 3);
    for (int t = 1; rooms && t <= 199; t++) {
        int w = (int)sim_random(s, (uint32_t)rooms);
        uint8_t *wr = sim_room_record(s, h, 3, w);
        if (wr[12] >= wr[10]) continue;
        for (int draws = 0; draws < 46; draws++) { /* at most 46 random(10) a try (SIM-47, docs/sim-rooms.md) */
            int i = (int)sim_random(s, 10), prio = image_byte(s, WAIT_PRIORITY, (uint32_t)i);
            if (get16(wr + 0x14 + 4 * i) || !(prio == p[P_SEX] || (t >= 30 && prio == 2) || t >= 80)) continue;
            *room = w, *seat = i;
            return 1;
        }
    }
    return 0;
}

/* the move of 0x2B989: P (queue place 1) into the seat, the queue moves up, the clerk's work counter */
static void seat_take(Sim *s, int h, int w, int i) {
    uint8_t *rec = reception(s, h), *queue = rec + 10, *wr = sim_room_record(s, h, 3, w);
    int index = get16(queue);
    uint8_t *p = person(s, index);
    put16(wr + 0x14 + 4 * i, index);
    put16(wr + 0x16 + 4 * i, 0);
    wr[12]++;
    p[P_STATE] = ST_WAITING;
    p[P_ROOM] = (uint8_t)w;
    queue_up(queue, 4);
    work_done(s, get16(rec + 8));
}

static int equipment_id(Sim *s, uint8_t *rec, int cat, int cap, int use, int by_kind);
static int use_kind(Sim *s, uint8_t *rec, int cat, int kind);

/* 0x2B989 in AI mode. After the seat search (its draws come first) the supply checks of docs/sim-drift2.md section 2:
 * forms (equipment kind 11), then pens (kind 3) in the reception's own list (0x2BD17, check only); the first one missing
 * goes to R +7 and P stays at queue place 1. With both there one of each is used (0x2BD44), then the move. */
static void to_waiting_room(Sim *s, int h) {
    static const int SUPPLIES[2] = {11, 3};
    uint8_t *rec = reception(s, h), *queue = rec + 10;
    int index = get16(queue);
    uint8_t *p = person(s, index);
    if (p[P_DEAD]) {
        queue_up(queue, 4);
        died(s, h, index);
        return;
    }
    int w, i;
    if (!seat_search(s, h, p, &w, &i)) return;
    for (int k = 0; k < 2; k++)
        if (!equipment_id(s, rec, 2, SUPPLIES[k], 0, 1)) {
            rec[7] = (uint8_t)SUPPLIES[k];
            return;
        }
    for (int k = 0; k < 2; k++) use_kind(s, rec, 2, SUPPLIES[k]);
    seat_take(s, h, w, i);
}

/* The reception operator's request step (0x23060 at 0x23260, docs/sim-drift2.md section 2), staffed branch, before the
 * admission: with R +7 set, room is made in the reception's equipment list (0x2A499); without a store room the routine
 * ends; otherwise an item of kind R +7 is taken from the store and installed (0x2A7C4 with take, 0x2B07E; none in the
 * store: h+0xDB = the kind when it is 0, and the routine ends); installed: R +7 = 0. Returns 1 to go on. */
static int reception_request(Sim *s, int h) {
    uint8_t *rec = reception(s, h);
    if (!rec[7]) return 1;
    sim_equipment_room(s, h, 2, rec);
    if (!room_counts(s, h)[4] || sim_store_supply(s, h, 2, rec, rec[7], 1) <= 0) return 0;
    rec[7] = 0;
    return 1;
}

/* ---- the Krankenzimmer queue and ward ---------------------------------------------------------- */

static int ward_free_bed(Sim *s, int h, int *ward) { /* 0x2B4B3: free beds, the last ward with one */
    int free_beds = 0;
    for (int k = 0; k < sim_rooms_in_use(s, h, 6); k++) {
        uint8_t *rec = sim_room_record(s, h, 6, k);
        if (rec[12] < rec[10]) free_beds += rec[10] - rec[12], *ward = k;
    }
    return free_beds;
}

/* 0x2336B's bed choice and move: three rounds on random(10) over all ten places, whatever the bed count +10
 * (docs/ui-rooms.md, "Put into a bed": the oracle put a patient into place 8 of a 4-bed ward); first a bed whose
 * priority entry (0x4DCC) is P's sex, then one whose entry is 2, then any free one. The rounds make at most 600, 600
 * and 1,200 draws (SIM-47, docs/sim-rooms.md "0x2336B"); where the player's branch then loops for ever, the engine
 * takes the first free place (cap, engine choice). Returns the bed or -1. */
static int bed_choose(Sim *s, int h, int ward, int index, int player) {
    static const int ROUND_DRAWS[3] = {600, 600, 1200};
    uint8_t *p = person(s, index), *k = sim_room_record(s, h, 6, ward);
    int b = -1;
    for (int round = 0; round < 3 && b < 0; round++)
        for (int draws = 0; draws < ROUND_DRAWS[round] && b < 0; draws++) {
            int c = (int)sim_random(s, 10), prio = image_byte(s, BED_PRIORITY, (uint32_t)c);
            if (get16(k + 0x14 + 4 * c) || (round == 0 && prio != p[P_SEX]) || (round == 1 && prio != 2)) continue;
            b = c;
        }
    for (int c = 0; player && b < 0 && c < 10; c++)
        if (!get16(k + 0x14 + 4 * c)) b = c;
    if (b < 0) return -1;
    put16(k + 0x14 + 4 * b, index);
    put16(k + 0x16 + 4 * b, 0);
    k[12]++;
    if (!p[P_IN_STEP]) add16(hospital(s, h) + 0x2E, 1);
    work_done(s, get16(reception(s, h) + 8));
    p[P_STATE] = ST_WARD;
    p[P_ROOM] = (uint8_t)ward;
    return b;
}

/* 0x2336B: the queued patient at R + place into ward K. A full K (+12 >= +10): nothing. Only the place's patient word
 * is cleared (its second word keeps its value): in player mode before the rounds, in AI mode after the move; AI mode
 * leaves the place untouched when the rounds find no bed (SIM-47, docs/sim-rooms.md "0x2336B"). */
static int bed_transfer(Sim *s, int h, int ward, int place, int ai) {
    uint8_t *k = sim_room_record(s, h, 6, ward), *at = reception(s, h) + place;
    if (k[12] >= k[10]) return -1;
    int index = get16(at);
    if (!ai) put16(at, 0);
    int b = bed_choose(s, h, ward, index, !ai);
    if (b < 0 && !ai) put16(at, (uint16_t)index); /* engine guard: no free place although +12 < +10 */
    if (b >= 0 && ai) put16(at, 0);
    return b;
}

/* 0x23060 step 7: with free beds in H's Krankenzimmer (0x2B4B3), the last taken place of the six and K = random(room
 * count 6), which may be a full ward (SIM-47) */
static void queue_to_bed(Sim *s, int h) {
    int ward = 0;
    if (!room_counts(s, h)[6] || !ward_free_bed(s, h, &ward)) return;
    uint8_t *queue = reception(s, h) + 0x46;
    int place = -1;
    for (int i = 0; i < 6; i++)
        if (get16(queue + 4 * i)) place = i;
    if (place < 0) return;
    ward = (int)sim_random(s, (uint32_t)sim_rooms_in_use(s, h, 6));
    bed_transfer(s, h, ward, 0x46 + 4 * place, 1);
}

/* ---- the bed wards: Krankenzimmer (6) and Neuro-Krankenzimmer (16) ------------------------------- */

/* docs/sim-rooms.md "Bed wards" and docs/sim-rooms2.md (0x6A0BA, 0x6C3D5, 0x2A01F): one code path, these
 * differences */
typedef struct {
    uint8_t cat, state, request; /* category, the patient state in a bed, the request byte */
    uint8_t heal_countdown, heal_code; /* step codes 8/17 and 73/72 */
    uint8_t unit_limit, unit_r31; /* unit draws below this health with r31 = this value */
    uint8_t bonus[3]; /* field 50 thresholds for the healing bonus */
    int8_t cure_opinion; /* opinion of an uncured discharge */
} WardKind;
static const WardKind WARDS[2] = {
    {6, 6, 6, 8, 17, 70, 9, {30, 45, 60}, -25},
    {16, 16, 6, 73, 72, 90, 18, {35, 50, 75}, -35}, /* request byte +6 as in the Krankenzimmer (docs/sim-rooms2.md) */
};

static void set_health(uint8_t *p, int v);
static void worsen(Sim *s, int h, uint8_t *p, int flag);
static void worsen_in_treatment(Sim *s, int h, uint8_t *p, int flag);
static void nurse_care(Sim *s, int h, uint8_t *p, const uint8_t *room, int comfort, int health_flag);
static void care_drift(Sim *s, const uint8_t *rec, int index, uint32_t bonus_context);
static void nurse_bonus(Sim *s, uint8_t *p, int nurse, int v);
static int icu_admit(Sim *s, int h, uint8_t *ward, int index);

static int duration(Sim *s, const uint8_t *t, int code) { /* the byte at 0x44A5 + code, times treatment byte 37 */
    return image_byte(s, STEP_DURATION, (uint32_t)code) * t[37];
}

/* "has" (0x2C20A) and "use" (0x2C237 / 0x311AF) of a capability among the room's equipment; returns the item
 * or 0. Using subtracts its catalog byte 16 (or 1 with r3 = 0) modulo 256; only stored bytes 1-127 retain it.
 * Otherwise its id is cleared, without separately clearing the condition (SIM-80), and the
 * list tidy-up 0x2BFB7 runs over the lists of all 16 hospitals, the stores' meal pass included. No rating is
 * recomputed: it keeps the worn-out item until the next recompute (docs/sim-rooms2.md, "Shared helpers", parity
 * SIM-42). The kind uses 0x2BD44 / 0x31164 (use_kind) are the same. */
static int equipment_search(Sim *s, uint8_t *rec, int cat, int cap, int use, int by_kind) {
    s->equipment_use = use; /* [0x5B6F], which the patient sheet's instrument search reads (docs/ui-sheets.md) */
    Description d = sim_description(s, cat);
    for (int i = 0; i < d.equipment_slots; i++) {
        int id = (uint16_t)get16(rec + d.equipment + 2 * i);
        if (!id) continue;
        const uint8_t *it = sim_equipment_item(s, id);
        if (by_kind ? it[6] != cap : (it[10] != cap && it[11] != cap && it[12] != cap && it[13] != cap)) continue;
        if (use) {
            int wear = it[16] ? it[16] : (sim_bits(s, 3) == 0);
            uint8_t c = (uint8_t)(rec[d.cond_b + i] - wear);
            rec[d.cond_b + i] = c;
            if (c == 0 || c >= 128) {
                put16(rec + d.equipment + 2 * i, 0);
                sim_tidy_lists(s);
            }
        }
        return id;
    }
    return 0;
}
static int equipment_id(Sim *s, uint8_t *rec, int cat, int cap, int use, int by_kind) {
    return equipment_search(s, rec, cat, cap, use, by_kind);
}
/* The kind searches 0x2BD44 (the reception's check and use) and 0x31164 (the diagnosis start's check and use,
 * docs/sim-drift2.md sections 2 and 3): the first item of the kind wears as above; a worn-out item leaves its list and
 * the lists are tidied (0x2BFB7). Returns 0 when the kind was found (and used), else the kind. */
static int use_kind(Sim *s, uint8_t *rec, int cat, int kind) {
    return equipment_search(s, rec, cat, kind, 1, 1) ? 0 : kind;
}
static const uint8_t *equipment(Sim *s, uint8_t *rec, int cat, int cap, int use) {
    int id = equipment_id(s, rec, cat, cap, use, 0);
    return id ? sim_equipment_item(s, id) : NULL;
}

/* The treatment rooms' equipment check 0x2C570 (use = 0) and check-and-use 0x2C567 (use = 1), docs/sim-drift.md
 * section 3: the treatment's bytes 21-25 are capabilities (equipment catalog bytes 10-13), checked first; the first
 * one the room lacks ends the check with miss[0] = it, miss[1] = 0. All present: bytes 11-15 are equipment kinds
 * (catalog byte 6); the first missing gives miss[0] = 0, miss[1] = it. Returns the missing id, 0 when all are there.
 * With use, every item found loses condition (equipment_id's wear). */
static int equipment_check(Sim *s, int cat, uint8_t *tr, int treat, int use, uint8_t miss[2]) {
    const uint8_t *t = treatment(s, treat);
    miss[0] = miss[1] = 0;
    for (int i = 21; i <= 25; i++)
        if (t[i] && !equipment_id(s, tr, cat, t[i], use, 0)) return miss[0] = t[i];
    for (int i = 11; i <= 15; i++)
        if (t[i] && !equipment_id(s, tr, cat, t[i], use, 1)) return miss[1] = t[i];
    return 0;
}

/* The decision's step 5 (0x2D11A at 0x2D3F4, 0x34C5C at 0x34CDB, 0x35E59 at 0x35EE3), both modes: with a gap +0x44 =
 * the missing capability and +0x45 = the missing kind; without one the items are used (0x2C567) and u16 +0x44 = 0. */
static void decide_equipment(Sim *s, int cat, uint8_t *tr, int treat) {
    uint8_t miss[2];
    if (equipment_check(s, cat, tr, treat, 0, miss)) {
        tr[0x44] = miss[0], tr[0x45] = miss[1];
    } else {
        equipment_check(s, cat, tr, treat, 1, miss);
        put16(tr + 0x44, 0);
    }
}

/* the cure check: the person stays in the bed. 0x37D19 first: fields 11-19 and, parallel to them, 84-92 shift up;
 * 84 becomes field 80 (an illness) or the wish id (docs/sim-patients.md, "0x37D19 - case history") */
static void cure_reset_in_bed(uint8_t *p) {
    if (get16(p + P_ILLNESS) && p[P_WISH] != 4) {
        memmove(p + 12, p + 11, 8);
        p[11] = p[P_ILLNESS] ? p[P_ILLNESS] : 13;
        memmove(p + 85, p + 84, 8);
        p[84] = p[P_ILLNESS] ? p[80] : p[P_WISH];
    }
    p[P_AILMENT] = p[P_DIAGNOSIS] = p[P_TREATMENT] = p[P_STEPS] = 0;
    put16(p + P_ILLNESS, 0);
    p[P_COUNTDOWN] = p[P_IN_TREATMENT] = p[80] = p[P_TREATED_BY] = 0;
    p[82] = 0xFF, p[P_SKILL] = 0, p[P_IN_STEP] = 0;
}

static void ward_death_check(Sim *s, int h, uint8_t *ward, int b, int index) { /* 0x30766 */
    uint8_t *p = person(s, index);
    if (p[P_HEALTH] >= 1 || hospital(s, h)[0x14]) return;
    p[P_DEAD] = 1;
    memset(ward + 0x14 + 4 * b, 0, 4);
    ward[12]--;
    died(s, h, index);
}

/* SIM-93/94/96 (docs/sim-rooms2.md): cleanup may clear the room's patient word and reset p. Health uses that word,
 * including zero; the sibling watched-death selector saves only hospital 0. Keep the write order. */
static void post_cleanup_health_notification(Sim *s, int h, const uint8_t *patient_word, uint8_t *p, int flag) {
    int index = get16(patient_word);
    notify_watched_patient(s, h, index, flag);
    if (!p[P_HEALTH]) {
        p[P_DEAD] = 1;
        if (h == 0) notify_watched_patient(s, h, index, 0x46);
    }
}

/* 0x29FC8 / 0x4F16C: the next treatment step in bed */
static void next_bed_step(Sim *s, uint8_t *p) {
    p[P_IN_STEP] = 0xFF;
    const uint8_t *t = treatment(s, p[P_TREATMENT]);
    int n = 0;
    while (n < 5 && t[16 + n]) n++;
    p[P_STEPS] = (uint8_t)n;
    p[P_COUNTDOWN] = 10;
}

/* 0x29DE0 / 0x4EF4A: discharge from a bed */
static void ward_discharge(Sim *s, int h, const WardKind *wk, uint8_t *ward, int bed_index) {
    uint8_t *bed = ward + 0x14 + 4 * bed_index;
    int index = get16(bed);
    uint8_t *p = person(s, index), *r = hospital(s, h);
    sim_rating_record(s, p); /* the common discharge keeps the patient's record in [0x4514] */
    memset(bed, 0, 4);
    ward[12]--;
    p[P_HOSPITAL] = 0xFF;
    p[P_TREATED_BY] = (uint8_t)h;
    int bill = sim_invoice(s, h, p);
    pay(s, h, bill);
    if (wk->cat == 16) {
        add32(r + 0x16E, 1);
        if (get32(r + 0x16E) >= 1000) put32(r + 0x16E, 0), add16(r + 0x172, 1);
    }
    sim_news_bill(s, h, index, bill, 1); /* 0x42AEA */
    opinion_after_bill(p, h, bill, 1);
    if (wk->cat == 6) { /* the units: Krankenzimmer only, the neuro ward keeps them (SIM-46, docs/sim-rooms.md step 3) */
        put16(p + P_UNITS, 0);
        memset(p + 50, 0, 4);
        put16(p + 54, 0);
    }
    if (get16(p + P_ILLNESS)) {
        add16(r + 0x2A, 1);
        add16(r + (wk->cat == 6 ? 0x12A : 0x12C), 1);
        opinion_add(p, h, wk->cure_opinion);
    }
    p[P_STATE] = 0xFF;
    p[P_COUNTDOWN] = treatment(s, p[P_TREATMENT])[7]; /* no zero test: field 43 = 0 reads record 0 (SIM-57) */
    p[P_IN_TREATMENT] = 0xFF;
    p[P_ROUNDS] = 0;
    if (p[P_IN_STEP]) p[P_IN_STEP] = 0, p[P_TREATMENT] = 1;
    add16(r + 0x26, 1);
    if (!get16(p + P_ILLNESS)) {
        if (wk->cat == 6) add16(r + 0x48, 1);
        else add16(r + 0x50, 1), add16(r + 0x52, 1);
        add16(r + 0x28, 1);
        if (p[P_FIELD100] > 1) add16(r + (wk->cat == 6 ? 0x34 : 0x116), 1);
        opinion_add(p, h, h == 0 ? 10 : 14);
        p[P_IN_TREATMENT] = 0;
        p[P_COUNTDOWN] = 0;
    }
    int nurse = get16(ward + 8);
    work_done(s, nurse);
    if (p[P_HEALTH] == 100) work_done(s, nurse);
}

static void equipment_needs(Sim *s, int h, int cat, uint8_t *rec, int cap, int clerk_gate);
static int self_fetching(Sim *s, int h);

/* The bed wards' supplies step (docs/sim-rooms.md, 0x2995B; the neuro twin at 0x2A22C-0x2A305): with the u16 at +6
 * non-zero, the missing capability +6 and kind +7, each when the ward lacks it, are taken from the store and
 * installed, and then that request byte is cleared; a request the ward already has is left as it is, and one the
 * store cannot serve stays set (sim_store_supply has asked the store's purchase through h+0xDA / h+0xDB). Unlike the
 * intensive-care operator, which keeps its request bytes. Engine choice: a request byte of 0 is skipped. */
static void ward_supplies(Sim *s, int h, int cat, uint8_t *ward) {
    if (!get16(ward + 6)) return;
    if (ward[6] && !equipment_id(s, ward, cat, ward[6], 0, 0) && sim_store_supply(s, h, cat, ward, ward[6], 0) > 0)
        ward[6] = 0;
    if (ward[7] && !equipment_id(s, ward, cat, ward[7], 0, 1) && sim_store_supply(s, h, cat, ward, ward[7], 1) > 0)
        ward[7] = 0;
}

/* 0x30965 / 0x536D1 (docs/sim-rooms.md, "ward beds wanted and nurse mood"), once a minute, for hospitals 0-15 and every
 * ward of the category: a ward below the catalog's +0x29 beds of its type wants beds (+4 = 8) whenever +4 is 0; then,
 * with a nurse and r15 = 7, the nurse's mood (staff byte 21): while the ward wants anything it falls by (r1 + r1) / 2
 * and the nurse leaves below 20 (+5 = 1); otherwise it rises with the rating (byte 0 + byte 1) >= r63, else falls when
 * the rating is below a new r63, and below 4 the nurse leaves (+5 = 2), through 0x2301C (sim_leave_room). */
static void nurse_leaves(Sim *s, int h, uint8_t *ward, int why) {
    int who = get16(ward + 8);
    ward[5] = (uint8_t)why;
    put16(ward + 8, 0);
    sim_leave_room(s, h, who);
}
static void ward_wants(Sim *s, const WardKind *wk) {
    Description d = sim_description(s, wk->cat);
    /* the Neuro-Krankenzimmer's draws (0x536D1, docs/rng-sites.md 3.12): EBP = the row of h, [0x4528] as its last
     * writer left it (2.4; hospital record 16 at tick 61); D = the incoming caller until an r15 hits 7, then that nurse's record (SIM-109). The Krankenzimmer's
     * twin 0x30965 is even: default inputs. */
    int neuro = wk->cat == 16;
    uint32_t edi = s->rng_in.on ? s->rng_in.d : s->clock_d;
    for (int h = 0; h < 16; h++)
        for (int i = 0; i < sim_rooms_in_use(s, h, wk->cat); i++) {
            uint8_t *ward = sim_room_record(s, h, wk->cat, i);
            int capacity = catalog_u8(s, ward[d.type], CATALOG_BASE);
            if (!ward[4] && (uint16_t)(capacity - get16(ward + 10)) != 0) ward[4] = 8;
            int nurse = get16(ward + 8);
            if (!nurse) continue;
            SimIn old = sim_set_inputs(s, neuro ? sim_in(edi, rng_row(s, h), s->rng_scratch) : SIM_DEFAULT_IN);
            if (sim_bits_site(s, 15, neuro ? 0x53762 : 0) != 7) {
                sim_set_inputs(s, old);
                continue;
            }
            if (neuro) {
                edi = sim_address(s, staff_record(s, nurse));
                sim_set_inputs(s, sim_in(edi, rng_row(s, h), s->rng_scratch));
            }
            uint8_t *mood = staff_record(s, nurse) + 21;
            int rating = ward[0] + ward[1];
            if (ward[4]) {
                if (*mood) {
                    int a = (int)sim_bits_site(s, 1, neuro ? 0x53788 : 0); /* r1 then r1, in order */
                    *mood = (uint8_t)(*mood - (a + (int)sim_bits_site(s, 1, neuro ? 0x53790 : 0)) / 2);
                }
                if ((int8_t)*mood < 20) nurse_leaves(s, h, ward, 1);
            } else if (*mood < 100 && rating >= (int)sim_bits_site(s, 63, neuro ? 0x537CD : 0)) {
                (*mood)++;
            } else if ((int8_t)*mood >= 4) {
                if (rating < (int)sim_bits_site(s, 63, neuro ? 0x537EA : 0)) (*mood)--;
            } else {
                nurse_leaves(s, h, ward, 2);
            }
            sim_set_inputs(s, old);
        }
}

/* 0x2995B / 0x2A01F: the ward operators (staffing and patients) */
static void ward_operator(Sim *s, const WardKind *wk) {
    World *w = s->world;
    int h = sim_pick(s);
    if (!h || !room_counts(s, h)[wk->cat]) return;
    /* context O (docs/rng-sites.md 3.2): the pick with D = the scratch block (disk 0x6187, section 8), then D = the ward's record */
    int six = wk->cat == 6;
    int k = (int)sim_random_in(s, (uint32_t)sim_rooms_in_use(s, h, wk->cat), six ? 0x299A0 : 0x2A064,
                               rng_operator(s, h, NULL));
    uint8_t *ward = sim_room_record(s, h, wk->cat, k);
    SimIn in = rng_operator(s, h, ward);
    int hour = world_tail(w, TAIL_HOUR), played = world_tail(w, TAIL_HOURS);
    int reception_staffed = room_counts(s, h)[2] && get16(reception(s, h) + 8);
    uint8_t *store = room_counts(s, h)[4] ? sim_room_record(s, h, 4, 0) : NULL;
    int clerk = store && get16(store + 4); /* the u16 +4 of 0x1FBD8's record */
    /* in order as docs/sim-rooms.md (0x2995B, read again 2026-10-18): the fittings step (wanted furniture +4), in the
     * Krankenzimmer only with a present store clerk */
    if (wk->cat == 16 || clerk) {
        SimIn old = sim_set_inputs(s, in); /* its rare r1 (0x29A3D / 0x2A0EB) with the operator's pair */
        sim_fittings(s, h, wk->cat, ward, ward + 4);
        sim_set_inputs(s, old);
    }
    /* patients waiting for a treatment room (0x1B496), for the neuro ward for the neurology (0x1B5A8): they call the
     * nurse and hold off her release (docs/sim-drift7.md section 4.2) */
    int waiting = sim_waiting_for(s, h, six ? 5 : 15) != 0;
    if (!get16(ward + 8)) { /* no nurse: the nurse request, and the call ends there */
        int slot, who;
        if ((played < 5 || ward[12] || waiting || (hour >= 4 && hour <= 19 && reception_staffed)) &&
            (who = sim_assignable(s, h, 1, &slot)))
            sim_assign(s, h, who, slot, wk->cat, k, ward + 8);
        return;
    }
    if (sim_bits_in(s, 7, six ? 0x29AF0 : 0x2A19E, in) == 5 && !ward[0x0F]) ward[0x0F] = rng_comfort(s, in);
    if (hour >= 21 && !reception_staffed && !ward[12] && !waiting) {
        int who = get16(ward + 8);
        put16(ward + 8, 0);
        ward[0x0F] = 0;
        sim_release(s, h, who);
        return;
    }
    /* the supplies step (ward +6/+7): a store room, in the neuro ward also the clerk (0x2A01F) */
    if (store && (wk->cat == 6 || clerk)) ward_supplies(s, h, wk->cat, ward);
    int b = -1;
    for (int tries = 0; tries < 50 && b < 0; tries++) {
        int c = (int)sim_random_in(s, 10, six ? 0x29C6E : 0x2A331, in); /* the bed pick, the operator's pair */
        if (get16(ward + 0x14 + 4 * c)) b = c;
    }
    if (b < 0) return;
    int index = get16(ward + 0x14 + 4 * b);
    uint8_t *p = person(s, index);
    if (p[P_DEAD]) { /* 0x5A484 / 0x5A562 */
        if (p[P_STATE] == wk->state) {
            died_found(s, h, index);
            /* 0x52C90 clears the bed numbered by scratch +2, which no AI routine writes: bed 0, whoever lies
             * there, and only its patient word (reproduce; the dead person's own bed is cleared later by the
             * Krankenzimmer tick once the person has left H) */
            if (fix_on(25)) {
                /* FIX-25 (docs/fixes.md): original clears bed 0 whoever lies there; fixed clears the dead person's own
                 * bed (the first place holding P; none: nothing cleared), W +12 - 1 in both */
                for (int q = 0; q < 10; q++)
                    if (get16(ward + 0x14 + 4 * q) == index) {
                        put16(ward + 0x14 + 4 * q, 0);
                        break;
                    }
            } else {
                put16(ward + 0x14, 0);
            }
            ward[12]--;
        }
        return;
    }
    if (p[P_STATE] != wk->state) return;
    /* step 3, the Krankenzimmer only: 0x4F395, to intensive care */
    if (wk->cat == 6 && (int8_t)p[P_HEALTH] < 30 && (!treatment(s, p[P_TREATMENT])[1] || p[P_FIELD100])) {
        icu_admit(s, h, ward, index);
        return;
    }
    int hours = (int16_t)get16(ward + 0x14 + 4 * b + 2);
    int32_t cash = cash_of(s, h), gap = cash - (int32_t)sim_daily_costs(s, h);
    /* the hour tests of the diagnosis step (80 / 90 hours) can never fire after the 66-hour rule */
    int discharge = (gap < 40 && ((hour == 19 && hours >= 35) || (hour >= 20 && hours >= 10))) || hours >= 66 ||
                    cash < 0;
    if (!discharge) {
        if (p[P_IN_STEP]) return;
        if (get16(p + P_ILLNESS)) {
            next_bed_step(s, p); /* at any hour */
            return;
        }
        discharge = 1; /* cured: no case and no step running */
    }
    if (hour >= 4 && hour < 21) ward_discharge(s, h, wk, ward, b);
}

/* healing by a treatment step in bed (codes 17 / 72); returns the health change b */
static int healing(Sim *s, const WardKind *wk, const uint8_t *p) {
    int b;
    if (p[P_ILLNESS]) {
        const uint8_t *r = illness(s, p[P_ILLNESS]);
        b = p[P_TREATMENT] == r[11] ? 2 : 0;
        for (int i = 0; i < 3; i++) b += (int8_t)p[50] >= wk->bonus[i]; /* signed byte compares (SIM-34) */
        b += p[P_TREATMENT] == r[12];
    } else {
        b = 2; /* a wish */
    }
    if (wk->cat == 6) {
        int blood = get16(p + P_BLOOD);
        b -= (blood < 680) + (blood < 660) + (blood < 640) + 2 * (blood < 610);
    } else if ((int8_t)p[50] >= 20) {
        b += 4;
    }
    /* + r1 - r1 - r1 in this order (one statement per draw: C leaves the order inside an expression open) */
    b += (int)sim_bits(s, 1);
    b -= (int)sim_bits(s, 1);
    b -= (int)sim_bits(s, 1);
    return b;
}

/* The meal rounds of one bed patient in the ward tick (field 40 is hunger, capability 0x2B food;
 * docs/sim-rooms2.md, "Meals"): a round runs when (+6 = 0 and hunger >= 10) or (+6 = 0x2B and hunger >= 80), and
 * again while +6 = 0 and hunger >= 10. The use (0x311AF) writes +6: 0 for a meal found, 0x2B for none, so a round
 * without a meal ends the loop, and every meal used wears down by its catalog byte 16 (else 1 with r3 = 0). The
 * compares are signed bytes; mood and opinion of H move as 8-bit values and are clamped to 0-100 once, at the end
 * of the round. */
static int8_t clamp_signed_percent(uint8_t v) { return (int8_t)v < 0 ? 0 : (int8_t)v > 100 ? 100 : (int8_t)v; }
static void meal_rounds(Sim *s, int h, const WardKind *wk, uint8_t *ward, int index, uint8_t *p) {
    uint8_t *req = ward + wk->request;
    if (!((*req == 0 && (int8_t)p[40] >= 10) || (*req == 0x2B && (int8_t)p[40] >= 80))) return;
    do {
        uint8_t *op = p + P_OPINION + h;
        int meal = equipment_id(s, ward, wk->cat, 0x2B, 1, 0); /* 0x311AF: [0x5B71] = the meal used, or 0 */
        if (meal) {
            const uint8_t *it = sim_equipment_item(s, meal);
            *req = 0;
            p[48] = (uint8_t)(p[48] + it[8] / 4 + 1);
            p[53]++;
            int8_t hunger = (int8_t)(uint8_t)(p[40] - 8 * it[5]);
            p[40] = (uint8_t)(hunger < 0 ? 0 : hunger);
            if (it[4] >= 3) {
                p[P_MOOD] = (uint8_t)(p[P_MOOD] + 5);
                if ((int8_t)p[P_MOOD] >= 50) (*op)++;
            }
            sim_news_comfort(s, h, index, it[4], meal); /* 0x4CDAC, before the draws; u16 14 = the meal (SIM-43) */
            if ((int)sim_bits(s, 15) < it[4]) p[P_MOOD] = (uint8_t)(p[P_MOOD] + 5);
            if ((int)sim_bits(s, 15) >= it[4]) p[P_MOOD] = (uint8_t)(p[P_MOOD] - 5), (*op)--;
        } else {
            *req = 0x2B;
            if (sim_bits(s, 15) == 5) {
                (*op)--;
                p[P_MOOD] = (uint8_t)(p[P_MOOD] - 5);
                if ((int8_t)p[P_HEALTH] >= 10 && get16(p + P_ILLNESS))
                    set_health(p, p[P_HEALTH] - (int)(sim_bits(s, 3) + 1));
            }
        }
        p[P_MOOD] = (uint8_t)clamp_signed_percent(p[P_MOOD]);
        *op = (uint8_t)clamp_signed_percent(*op);
    } while (*req == 0 && (int8_t)p[40] >= 10);
}

/* 0x6A0BA / 0x6C3D5: the wards per tick */
static void ward_tick(Sim *s, const WardKind *wk) {
    for (int h = 0; h < 16; h++) {
        uint32_t bonus_context = rng_row(s, h); /* SIM-108: processing loads carry into the next room's care pass */
        for (int k = 0; k < sim_rooms_in_use(s, h, wk->cat); k++, sim_rating_advance(s, 240)) {
            if (!k) sim_rating_room(s, h, wk->cat, 0);
            uint8_t *ward = sim_room_record(s, h, wk->cat, k);
            sim_staff_break(s, h, wk->cat, k, ward + 8, ward + 0x0F); /* 0x2E666 when staffed, before the care drift */
            int nurse = get16(ward + 8);
            if (ward[wk->request] == 0x2B && !ward[12]) ward[wk->request] = 0;
            rng_scratch_room(s, ward); /* [0x4528] before the care drift (docs/rng-sites.md 2.4) */
            for (int b = 0; b < 10; b++) /* 0x2ED92 */
                if (get16(ward + 0x14 + 4 * b)) care_drift(s, ward, get16(ward + 0x14 + 4 * b), bonus_context);
            SimIn outer = s->rng_in;
            for (int b = 0; b < 10; b++) {
                uint8_t *bed = ward + 0x14 + 4 * b;
                int index = get16(bed);
                if (!index) continue;
                uint8_t *p = person(s, index);
                bonus_context = sim_address(s, p);
                /* every draw of the bed loop in context B (docs/rng-sites.md 3.10, 3.11) */
                sim_set_inputs(s, rng_bed(s, ward, sim_address(s, p)));
                if (wk->cat == 6) {
                    if (p[P_HOSPITAL] != h) {
                        memset(bed, 0, 4);
                        ward[12]--;
                        sim_reset_healthy(p);
                        add16(hospital(s, h) + 0x168, 1);
                        continue;
                    }
                    if (p[P_STATE] != wk->state) continue;
                } else if (p[P_STATE] == 8 || p[P_DEAD]) {
                    continue;
                }
                if (ward[10] < ward[12]) { /* overfull */
                    if (p[P_OPINION + h]) p[P_OPINION + h]--;
                    p[P_MOOD] = 0;
                }
                uint8_t *r = hospital(s, h);
                if (p[40] >= 70) {
                    /* +0x112 is a byte: +0x113 is the nurses' average age (0x629E7); the saves show it set */
                    r[0x112]++;
                    if (r[0x112] == 40) sim_news_long_stay(s, h), r[0x112] = 0;
                } else if (r[0x112]) {
                    r[0x112]--;
                }
                uint8_t *req = ward + wk->request;
                meal_rounds(s, h, wk, ward, index, p);
                if (p[P_HEALTH] < 96 && (!nurse || p[P_HEALTH] >= 50) && sim_bits_site(s, 31, 0x30741) == 12) { /* 0x30721 */
                    int v = p[P_HEALTH] - (int)(sim_bits(s, 3) + 1); /* one r3 (docs/sim-drift6.md section 3) */
                    set_health(p, v < 3 ? 3 : v); /* through set health: the history shifts (SIM-34) */
                }
                worsen_in_treatment(s, h, p, wk->cat == 6 ? 0x34 : 0x37);
                /* every bed, staffed or not: without a nurse it reads staff record 0 (SIM-62) */
                if (nurse || !fix_on(53)) nurse_care(s, h, p, ward, ward[15], wk->cat == 6 ? 0x34 : 0x37); /* FIX-53 (docs/fixes.md) */
                if (wk->cat == 6 && get16(p + P_BLOOD) < 700 && p[P_FIELD100] >= 2 && sim_bits(s, 31) == 4)
                    put16(p + P_BLOOD, get16(p + P_BLOOD) + (int)sim_bits(s, 1));
                /* recovery: still non-zero, the Krankenzimmer skips only its r7 = 3 worsening and the bed goes on with
                 * the step part; the neuro ward skips nothing (docs/sim-drift6.md section 3) */
                int recovering = 0;
                if (p[102]) {
                    p[102] = (uint8_t)(p[102] - sim_bits(s, 1));
                    if (!p[102]) {
                        notify_watched_patient(s, h, index, wk->cat == 6 ? 0x35 : 0x38);
                        notify_watched_patient(s, h, index, wk->cat == 6 ? 0x34 : 0x37);
                        p[P_STEPS]--;
                    }
                    else recovering = 1;
                }
                if (wk->cat == 6 && !recovering && p[P_FIELD100] != 2 && p[96] < 2 && sim_bits(s, 7) == 3) {
                    worsen(s, h, p, 0x34);
                    if (p[P_DEAD] && !hospital(s, h)[0x14]) {
                        memset(bed, 0, 4);
                        ward[12]--;
                        died(s, h, index);
                        continue;
                    }
                }
                if (!p[P_IN_STEP] || !p[P_TREATMENT] || p[P_TREATMENT] >= 0xFD) goto round_check;
                const uint8_t *t = treatment(s, p[P_TREATMENT]);
                if (!nurse) goto done;
                int neuro = wk->cat == 16;
                if (p[P_HEALTH] < wk->unit_limit && sim_bits_site(s, 31, neuro ? 0x6C7B4 : 0x6A576) == wk->unit_r31) {
                    if (sim_bits_site(s, 127, neuro ? 0x6C7BF : 0x6A581) == 12) p[55]++;
                    if (sim_bits_site(s, 127, neuro ? 0x6C7CD : 0) == 5) p[48]++;
                    if (sim_bits_site(s, 127, neuro ? 0x6C7DB : 0) == 13) p[53]++;
                    if (sim_bits_site(s, 127, neuro ? 0x6C7E9 : 0) == 5) p[50]++;
                }
                if (!t[36]) { /* a single healing treatment */
                    if (sim_bits(s, 7) != 2) goto done;
                    int c = t[15 + p[P_STEPS]];
                    if (c == wk->heal_countdown) {
                        if (p[P_COUNTDOWN] && --p[P_COUNTDOWN]) goto done;
                        if (p[P_HEALTH] >= 100) goto cure;
                    }
                    if (p[P_HEALTH] >= 100) goto done;
                    if (c == wk->heal_code && p[P_FIELD100] >= 2) {
                        set_health(p, p[P_HEALTH] + healing(s, wk, p));
                        ward_death_check(s, h, ward, b, index);
                        post_cleanup_health_notification(s, h, bed, p, neuro ? 0x37 : 0x34);
                        if (p[P_HEALTH] >= 100) p[P_STEPS]--, p[P_COUNTDOWN] = 4;
                    }
                    goto done;
                }
                /* a stepped treatment */
                if (p[P_COUNTDOWN]) p[P_COUNTDOWN]--;
                if (!*req)
                    for (int c = 21; c <= 25; c++)
                        if (t[c] && !equipment(s, ward, wk->cat, t[c], 0)) {
                            *req = t[c];
                            break;
                        }
                if (p[P_COUNTDOWN]) goto round_check;
                p[48]++, p[53]++, p[55]++;
                notify_watched_patient(s, h, index, neuro ? 0x38 : 0x35);
                if (p[P_STEPS] && --p[P_STEPS]) {
                    int cap = t[20 + p[P_STEPS]];
                    if (cap) { /* 0x311AF: +6 = 0 when the item was found and used, cap when it is missing */
                        *req = equipment(s, ward, wk->cat, cap, 1) ? 0 : (uint8_t)cap;
                        if (*req) { /* missing: the step waits and is tried again */
                            p[P_STEPS]++;
                            p[P_COUNTDOWN] = 5;
                            goto round_check;
                        }
                    }
                    p[P_COUNTDOWN] = (uint8_t)duration(s, t, t[15 + p[P_STEPS]]);
                    p[55]++;
                } else {
                    p[96]++;
                    int n = 0;
                    while (n < 5 && t[16 + n]) n++;
                    p[P_STEPS] = (uint8_t)n;
                    p[P_COUNTDOWN] = (uint8_t)duration(s, t, t[15 + p[P_STEPS]]);
                    p[55]++;
                }
            round_check:
                /* docs/sim-drift6.md sections 2 and 3: the neuro ward's setback reaches every patient with field 96
                 * below 3 (no test of wish, illness or treatment); the cure check runs only after an applied setback,
                 * a heal that left health above 0, or a treatment that is neither I byte 11 nor 12 */
                if (wk->cat == 16 && p[96] < 3) {
                    if (sim_bits_site(s, 63, 0x6CBAE) != 51) goto done;
                    int x = (int)sim_bits(s, 7) + 2;
                    if (x >= p[P_HEALTH]) goto done;
                    set_health(p, p[P_HEALTH] - x);
                } else {
                    if (p[96] < 2 || p[P_WISH] || !p[P_ILLNESS]) goto done;
                    const uint8_t *ir = illness(s, p[P_ILLNESS]);
                    if (p[P_TREATMENT] == ir[11]) {
                        if (sim_bits(s, 31) != 0) goto done;
                        int v = p[P_HEALTH] + ir[7]; /* + r1 + r1 - r1, drawn in this order */
                        v += (int)sim_bits(s, 1);
                        v += (int)sim_bits(s, 1);
                        v -= (int)sim_bits(s, 1);
                        set_health(p, v);
                    } else if (p[P_TREATMENT] == ir[12]) {
                        int f = (int8_t)p[50]; /* signed byte compares (SIM-34) */
                        int chance = f < 35 ? sim_bits(s, 63) == 0 : f < 50 ? sim_bits(s, 31) == 0 : f < 75 ? sim_bits(s, 15) == 0 : sim_bits(s, 7) == 0;
                        if (!chance) goto done;
                        /* + r1 - r1 - r1, then one r1 for each of field 50 >= 35, >= 50, >= 75, each drawn only
                         * when its test holds, and a failed test skips the rest (SIM-34) */
                        int v = p[P_HEALTH] + ir[7] / 2 + 1;
                        v += (int)sim_bits(s, 1);
                        v -= (int)sim_bits(s, 1);
                        v -= (int)sim_bits(s, 1);
                        if (f >= 35) {
                            v += (int)sim_bits(s, 1);
                            if (f >= 50) {
                                v += (int)sim_bits(s, 1);
                                if (f >= 75) v += (int)sim_bits(s, 1);
                            }
                        }
                        set_health(p, v);
                    } else {
                        goto cure; /* other treatment (field 43 = 0 included) */
                    }
                }
                ward_death_check(s, h, ward, b, index); /* 0x30766 after a heal or an applied setback */
                post_cleanup_health_notification(s, h, bed, p, wk->cat == 6 ? 0x34 : 0x37);
                if (!p[P_HEALTH]) {
                    goto done;
                }
            cure:
                if (p[P_HEALTH] >= 100 && get16(p + P_ILLNESS)) cure_reset_in_bed(p);
            done:
                if (p[P_MOOD] > 100) p[P_MOOD] = 100;
            }
            sim_set_inputs(s, outer);
        }
    }
}

/* 0x6ABB9 / 0x6CE07: the wards, hourly */
static void wards_hourly(Sim *s, const WardKind *wk) {
    for (int h = 0; h < 16; h++)
        for (int k = 0; k < sim_rooms_in_use(s, h, wk->cat); k++, sim_rating_advance(s, 240)) {
            if (!k) sim_rating_room(s, h, wk->cat, 0);
            uint8_t *ward = sim_room_record(s, h, wk->cat, k);
            int count = 0;
            for (int b = 0; b < 10; b++) {
                uint8_t *bed = ward + 0x14 + 4 * b;
                int index = get16(bed);
                if (!index) continue;
                count++;
                uint8_t *p = person(s, index);
                if (p[P_DEAD] || (wk->cat == 6 && p[P_STATE] != wk->state)) continue;
                if (!get16(bed + 2)) p[50]++;
                add16(bed + 2, 1);
                if (p[P_IN_STEP] && get16(p + P_ILLNESS) && get16(ward + 8)) {
                    p[50]++;
                    if (p[P_TREATMENT] && p[P_TREATMENT] < 0xFD && treatment(s, p[P_TREATMENT])[36]) work_done(s, get16(ward + 8));
                }
                int hours = get16(bed + 2);
                if (hours % 6 == 4 && p[P_MOOD]) { /* every 6 hours: random(102 - health) / 4 + r1 */
                    int x = (int)sim_random(s, (uint32_t)(102 - p[P_HEALTH])) / 4; /* the random drawn first */
                    x += (int)sim_bits(s, 1);
                    p[P_MOOD] = (uint8_t)(p[P_MOOD] - x < 0 ? 0 : p[P_MOOD] - x);
                }
                if (p[P_MOOD] > 100) p[P_MOOD] = 100;
            }
            ward[12] = (uint8_t)count;
        }
}

/* Neurology -> Neuro-Krankenzimmer in AI mode (0x235FA, docs/sim-drift.md section 5): ward K is the last neuro ward
 * with a free bed (0x2B52C, the caller's sim_free_beds); K full: nothing. The bed is the first free one of places
 * 0-9 whose entry in the sex table 0x4DCC is P's sex, else the first free one whose entry is 2, else the first free
 * one, in order (no random tries; all ten places whatever the bed count); none: nothing. Found: the bed = (P, 0),
 * K +12 + 1, the neurology record's +12 = 0 (P leaves the room), the hospital's +0x4C and +0x4E + 1, P state 16,
 * field 66 = K. No message. Returns 1 when P was put into a bed. */
static int to_neuro_bed(Sim *s, int h, uint8_t *tr, int index, int ward) {
    uint8_t *k = sim_room_record(s, h, 16, ward), *p = person(s, index);
    if (k[12] >= k[10]) return 0;
    int b = -1;
    for (int round = 0; round < 3 && b < 0; round++)
        for (int c = 0; c < 10 && b < 0; c++) {
            int prio = image_byte(s, BED_PRIORITY, (uint32_t)c);
            if (get16(k + 0x14 + 4 * c) || (round == 0 && prio != p[P_SEX]) || (round == 1 && prio != 2)) continue;
            b = c;
        }
    if (b < 0) return 0;
    put16(k + 0x14 + 4 * b, index);
    put16(k + 0x16 + 4 * b, 0);
    k[12]++;
    put16(tr + 12, 0);
    uint8_t *r = hospital(s, h);
    add16(r + 0x4C, 1);
    add16(r + 0x4E, 1);
    p[P_STATE] = ST_NEURO_WARD;
    p[P_ROOM] = (uint8_t)ward;
    return 1;
}

/* ---- waiting rooms (0x6BFC8) and the shared drifts -------------------------------------------------- */

/* 0x2D6B4: the value is taken as a signed byte, so 101-127 become 100 and an 8-bit sum or difference that
 * wrapped to 128-255 becomes 0 (docs/sim-rooms2.md: "health + b" above 127 kills; reproduce) */
static void set_health(uint8_t *p, int v) {
    memmove(p + 68, p + 67, 10);
    p[67] = p[P_HEALTH];
    int8_t b = (int8_t)(uint8_t)v;
    p[P_HEALTH] = (uint8_t)(b < 0 ? 0 : b > 100 ? 100 : b);
}

static int index_of(Sim *s, const uint8_t *p) { return (int)((p - s->world->block[BLK_PATIENTS]) / PATIENT_SIZE); }

/* 0x2EA17: the person leaves the first Krankenzimmer bed of hospital h that holds it */
static void leave_ward_bed(Sim *s, int h, int index) {
    for (int k = 0; k < sim_rooms_in_use(s, h, 6); k++) {
        uint8_t *ward = sim_room_record(s, h, 6, k);
        for (int b = 0; b < 10; b++)
            if (get16(ward + 0x14 + 4 * b) == index) {
                memset(ward + 0x14 + 4 * b, 0, 4);
                ward[12]--;
                return;
            }
    }
}
static void died_in_op(Sim *s, int h, uint8_t *p) { /* 0x2EA28: a person who died in the OP gives up its bed */
    if (p[P_STATE] == ST_OP) leave_ward_bed(s, h, index_of(s, p));
}

/* 0x2E8BB: worsening during a treatment */
static void worsen_in_treatment(Sim *s, int h, uint8_t *p, int flag) {
    /* the caller's inputs (context B in the ward ticks, docs/rng-sites.md 3.10); the four r7 in the original's order */
    if (sim_bits_site(s, 15, 0x2E8CB) != 4 || p[P_HEALTH] < 25 || p[P_HEALTH] > 79) return;
    notify_watched_patient(s, h, index_of(s, p), flag); /* before the loss draws, even for zero loss (SIM-89) */
    int d = (int)sim_bits_site(s, 7, 0x2E906);
    d += (int)sim_bits_site(s, 7, 0x2E90D);
    d -= (int)sim_bits_site(s, 7, 0x2E914);
    d -= (int)sim_bits_site(s, 7, 0x2E91B);
    if (get16(p + P_BLOOD) < 700) d += (int)sim_bits_site(s, 3, 0x2E92A);
    int v = p[P_HEALTH] - d;
    if (v < 20 && (!p[P_ILLNESS] || illness(s, p[P_ILLNESS])[9] == 0)) v += (int)sim_bits(s, 3);
    set_health(p, v);
    if (!p[P_HEALTH]) p[P_DEAD] = 1, died_in_op(s, h, p);
}
void sim_worsen_in_treatment(Sim *s, int h, uint8_t *p, int flag) { worsen_in_treatment(s, h, p, flag); }

static void worsen(Sim *s, int h, uint8_t *p, int flag) { /* 0x2E975 */
    if (sim_bits(s, 31) != 11 || p[P_HEALTH] >= 100 || !p[P_ILLNESS]) return;
    notify_watched_patient(s, h, index_of(s, p), flag); /* SIM-90: before the loss draws */
    const uint8_t *r = illness(s, p[P_ILLNESS]);
    int d = (int)sim_random(s, (uint32_t)(r[0] / 2 + 1));
    d = (d > 10 ? 10 : d) + 1 + (int)sim_bits(s, 3);
    int v = (uint8_t)(p[P_HEALTH] - d);
    if (v < 20 && r[9] == 0) v += (int)sim_bits(s, 3);
    set_health(p, v);
    if (!p[P_HEALTH]) {
        p[P_DEAD] = 1;
        /* SIM-78: the general worsening's death flag is saved for hospitals 0 and 1, watched or not
         * (docs/sim-rooms2.md, "Death notification retained in the save"). AI hospitals select unsaved scratch. */
        if (h == 0 || h == 1) s->world->block[h ? BLK_PLAYER1 : BLK_PLAYER0][0x46] = 0xFF;
        died_in_op(s, h, p);
    }
}
void sim_worsen(Sim *s, int h, uint8_t *p, int flag) { worsen(s, h, p, flag); }

static void drift(Sim *s, uint8_t *p, int h, const uint8_t *room) { /* 0x2E7EB: room value = record byte 0 */
    if (sim_bits(s, 3) != 2 || p[P_IN_TREATMENT]) return;
    int m = p[P_MOOD], o = p[P_OPINION + h], value = room[0];
    if (value < (int)sim_bits(s, 15)) m--;
    if (value >= (int)sim_bits(s, 63) + 15) m++;
    if (m < 20 && value < (int)sim_bits(s, 15)) o--;
    if (m >= 80 && value >= (int)sim_bits(s, 63) + 15) o++;
    p[P_MOOD] = (uint8_t)(m < 0 ? 0 : m > 100 ? 100 : m);
    p[P_OPINION + h] = (uint8_t)(o < 0 ? 0 : o > 100 ? 100 : o);
}

/* SIM-99: only a would-be departure consults the player's pending call or patient view. The live player's
 * saved screen can be stale; player 1 has no runtime view. A waiting-room view refreshes before the news. */
static int waiting_departure_protected(Sim *s, int h, int index) {
    if (h > 1 || !hospital(s, h)[0x14]) return 0;
    uint8_t *pb = s->world->block[h ? BLK_PLAYER1 : BLK_PLAYER0];
    if (get32(pb + 0x12)) return 1;
    int screen = h ? (uint16_t)get16(pb + 4) : s->screen;
    if (screen == 29) pb[0x44] = 0xFF;
    return screen == 59 && get16(pb + 0x2C) == index;
}

static void waiting_rooms(Sim *s) {
    World *w = s->world;
    int hour = world_tail(w, TAIL_HOUR);
    for (int h = 0; h < 16; h++) {
        uint32_t bonus_context = rng_row(s, h); /* SIM-108: only the later processing pass changes this */
        /* SIM-31: the r15 index first, then the r127 value (in one C expression the order is open; clang drew the
         * value first) */
        uint32_t at = 0x194 + sim_bits(s, 15);
        hospital(s, h)[at] = (uint8_t)(sim_bits(s, 127) + 102);
        for (int k = 0; k < sim_rooms_in_use(s, h, 3); k++, sim_rating_advance(s, 180)) {
            if (!k) sim_rating_room(s, h, 3, 0);
            uint8_t *wr = sim_room_record(s, h, 3, k);
            int count = 0;
            sim_staff_break(s, h, 3, k, wr + 8, wr + 0x0F); /* 0x2E666 when staffed (docs/sim-drift3.md section 4) */
            /* the work credit (docs/sim-drift4.md section 6): +0x0C as the previous run left it, before the recount */
            int left = get16(wr + 0x0C);
            if (get16(wr + 8) && left >= 5 && sim_bits(s, 15) == 5) {
                work_done(s, get16(wr + 8));
                if (left == 10 && sim_bits(s, 1)) work_done(s, get16(wr + 8));
            }
            rng_scratch_room(s, wr); /* [0x4528] before the care drift (docs/rng-sites.md 2.4) */
            for (int i = 0; i < 10; i++) /* 0x2ED92 */
                if (get16(wr + 0x14 + 4 * i)) care_drift(s, wr, get16(wr + 0x14 + 4 * i), bonus_context);
            SimIn outer = s->rng_in;
            for (int i = 0; i < 10; i++) {
                uint8_t *place = wr + 0x14 + 4 * i;
                int index = get16(place);
                if (!index) continue;
                count++;
                uint8_t *p = person(s, index);
                bonus_context = sim_address(s, p);
                sim_set_inputs(s, rng_bed(s, wr, sim_address(s, p))); /* context B (docs/rng-sites.md 2.3, 3.13) */
                drift(s, p, h, wr);
                int wait = get16(place + 2) + 1;
                put16(place + 2, wait);
                if (p[P_DEAD]) continue;
                int m = p[P_MOOD];
                int patient = m > 94 || (wait < 160 && m > 84) || (wait < 140 && m > 69) || (wait < 120 && m > 49) ||
                              (wait < 100 && m > 29) || wait < 80;
                int stay = (hour < 20 || (wait < 60 && (hour < 21 || wait < 15))) && wait < 180 && patient;
                if (!stay && !waiting_departure_protected(s, h, index)) {
                    sim_news_gave_up(s, h, index, 0); /* 0x493AD */
                    memset(place, 0, 4);
                    wr[12]--; /* the record's +12; the count below keeps the leaver (SIM-41) */
                    p[P_HOSPITAL] = p[P_STATE] = 0xFF;
                    p[P_MOOD] = 40;
                    opinion_add(p, h, -12);
                    if (h == 0) notify_watched_patient(s, h, index, 0x49);
                    continue;
                }
                if (wait >= 90 && wait <= 99 && m) {
                    int x = (int)sim_random(s, (uint32_t)(102 - p[P_HEALTH])) / 8; /* the random first, then r1 */
                    x += (int)sim_bits(s, 1);
                    if (!sim_bits(s, 1)) x /= 2;
                    p[P_MOOD] = (uint8_t)(m - x < 0 ? 0 : m - x);
                    if (h == 0) notify_watched_patient(s, h, index, 0x49);
                }
                if (wr[15] && sim_bits(s, 3) == 0) { /* for a man a second r1 replaces the first (SIM-40) */
                    int b = (int)sim_bits(s, 1);
                    if (p[P_SEX] == 0) b = (int)sim_bits(s, 1);
                    p[P_MOOD] = (uint8_t)(p[P_MOOD] + b);
                    if (h == 0) notify_watched_patient(s, h, index, 0x49);
                }
                if (p[P_HEALTH] < 90 && wait > 19 && sim_bits(s, 3) == 3) {
                    worsen(s, h, p, 0x30);
                    if (p[P_DEAD] && h) {
                        memset(place, 0, 4);
                        wr[12]--; /* as for a leaver: the count below keeps the dead (SIM-41) */
                        died(s, h, index);
                    }
                }
                if (p[P_MOOD] > 100) p[P_MOOD] = 100;
            }
            sim_set_inputs(s, outer);
            wr[12] = (uint8_t)count; /* every patient counted, leavers and the dead included (SIM-41) */
        }
    }
}

/* ---- treatment room, dental ward, neurology ------------------------------------------------------ */

/* The three rooms share the record layout and the cycle (docs/sim-rooms.md, docs/sim-rooms2.md); the
 * differences are the case they take, the unit draws and the step-duration lookup. */
typedef struct {
    uint8_t cat;
    uint8_t want_type; /* the illness type the room takes: 0 physical, 1 dental, 2 psychological */
    uint8_t diag[4][3]; /* diagnosing: {mask, value, field} for four draws */
    uint8_t treat[4][2]; /* treating: an r31 draw per test, {value, field} */
    uint8_t step_bug; /* neurology reads T[field 45] instead of T[15 + field 45] */
    uint8_t bring_state; /* the state the bring routine sets: 5, dental 11 (0x34BD6), neurology 15 (0x359E1) */
} RoomKind;
static const RoomKind KINDS[3] = {
    {5, 0, {{15, 6, 54}, {15, 1, 55}, {7, 6, 48}, {7, 1, 53}}, {{1, 55}, {2, 54}, {3, 48}, {4, 53}}, 0, ST_TREATMENT},
    {11, 1, {{7, 3, 54}, {7, 3, 48}, {7, 3, 53}, {7, 4, 55}}, {{2, 54}, {5, 55}, {11, 53}, {6, 48}}, 0, 11},
    {15, 2, {{7, 4, 54}, {7, 5, 55}, {7, 4, 48}, {7, 2, 53}}, {{11, 54}, {5, 55}, {13, 48}, {7, 53}}, 1, 15},
};
static const RoomKind *kind_of(int cat) { return cat == 5 ? &KINDS[0] : cat == 11 ? &KINDS[1] : &KINDS[2]; }
static int patient_type(Sim *s, const uint8_t *p) { return p[P_ILLNESS] ? illness(s, p[P_ILLNESS])[8] : 0; } /* 0x2E3C1 */
static uint32_t *flow_of(Sim *s, const RoomKind *k) { return s->flow[k - KINDS]; }

static int needs_room(Sim *s, const RoomKind *k, const uint8_t *p) {
    if (k->cat == 5) return p[P_WISH] || (p[P_ILLNESS] && illness(s, p[P_ILLNESS])[8] == 0); /* 0x1B496 */
    return !p[P_WISH] && p[P_ILLNESS] && illness(s, p[P_ILLNESS])[8] == k->want_type; /* 0x1B51F / 0x1B5A8 */
}

static int waiting_for(Sim *s, int h, const RoomKind *k) {
    int n = 0;
    for (int r = 0; r < sim_rooms_in_use(s, h, 3); r++) {
        const uint8_t *wr = sim_room_record(s, h, 3, r);
        for (int i = 0; i < 10; i++) {
            int index = get16(wr + 0x14 + 4 * i);
            if (index && needs_room(s, k, person(s, index))) n++;
        }
    }
    return n;
}
int sim_waiting_for(Sim *s, int h, int cat) { return waiting_for(s, h, kind_of(cat)); } /* 0x1B496 / 0x1B51F / 0x1B5A8 */

/* 0x2E3F2 (docs/sim-rooms.md, "0x2E252"): the four passes over H's waiting rooms for a patient of the wanted type
 * [0x45CF] (the type byte 8 of the real illness, field 56; 0 without an illness, so a wish patient is type 0); a
 * negative `want` matches every patient. The place or NULL; *from = its waiting room. */
static uint8_t *pick_waiting(Sim *s, int h, int want, int *from) {
    static const int MIN_WAIT[4] = {100, 70, 40, 0};
    for (int pass = 0; pass < 4; pass++)
        for (int r = 0; r < sim_rooms_in_use(s, h, 3); r++) {
            uint8_t *wr = sim_room_record(s, h, 3, r);
            for (int i = 0; i < 10; i++) {
                uint8_t *place = wr + 0x14 + 4 * i;
                int index = get16(place);
                if (!index) continue;
                const uint8_t *p = person(s, index);
                if (want >= 0 && patient_type(s, p) != want) continue;
                if (!(get16(place + 2) >= MIN_WAIT[pass] || (pass == 0 && (int8_t)p[P_HEALTH] < 60))) continue;
                *from = r;
                s->dup_byte = 0; /* [0x45CF] = 0: a patient found (0x2E603, SIM-55) */
                return place;
            }
        }
    s->dup_byte = 0xFF; /* none (0x2E5F7 / 0x2E60C) */
    return NULL;
}

/* 0x2E252 / 0x34A8F / the neurology's bring (and their player branches 0x2E3F2 / 0x34ABB / 0x358B5): the pick wants
 * the room's type (treatment room 0, dental 1, neurology 2); in AI mode a miss takes nobody, in player mode the passes
 * run again over every patient. The dental bring then tests the found patient in every mode (0x34AF0, docs/sim-rooms.md,
 * "0x2E252"): in hospital 0 with a nurse (+8; record 0 when the room has none) whose staff byte 11 is 0 anyone is
 * taken, otherwise only a patient of type 1. The dentist (+10) is not read. Only the player's call can bring a
 * non-dental patient this far (the AI pick has no fallback). The move: the place cleared, waiting room +12 - 1, the room
 * +12 = P, +14 = 999, +16 = +18 = 0, the nurse's (+8) work counter + 1 (docs/ui-rooms.md, oracle), P's state and
 * field 66. Returns the patient or 0; *from = its waiting room. */
static int bring_patient(Sim *s, int h, const RoomKind *k, uint8_t *tr, int room, int player, int *from) {
    int r = 0;
    s->dup_byte = (uint8_t)k->want_type; /* the wanted type into [0x45CF] before the pick (SIM-55) */
    uint8_t *place = pick_waiting(s, h, k->want_type, &r);
    if (!place && player) place = pick_waiting(s, h, -1, &r);
    if (!place) return 0;
    int index = get16(place);
    uint8_t *p = person(s, index);
    if (k->cat == 11 && (h || staff_record(s, get16(tr + 8))[11]) && patient_type(s, p) != 1) return 0;
    memset(place, 0, 4);
    sim_room_record(s, h, 3, r)[12]--;
    put16(tr + 12, index);
    put16(tr + 14, 999);
    put16(tr + 16, 0);
    put16(tr + 18, 0);
    work_done(s, get16(tr + 8));
    p[P_STATE] = k->bring_state;
    p[P_ROOM] = (uint8_t)room;
    if (from) *from = r;
    return index;
}

static int doctor_skill_rating(Sim *s, int rating, int doctor) { /* 0x2D62D */
    const uint8_t *d = staff_record(s, doctor);
    int b15 = d[15] - 60;
    unsigned v = (unsigned)(d[2] * 8 + d[12] * 16 + d[13] * 16 + (b15 > 0 ? b15 : 0) * 2) + (uint16_t)get16(d + 16) / 32 +
                 rating / 2;
    return (int)((v & 0xFFFF) / 2);
}
static int doctor_skill(Sim *s, const uint8_t *room, int doctor) {
    return doctor_skill_rating(s, room[1], doctor);
}

static int rank_offset(Sim *s, int skill) { /* 0x31109 in AI mode */
    sim_bits(s, 3);
    if ((int)sim_bits(s, 63) < skill) return (int)sim_bits(s, 127) < skill ? 0 : 1;
    if (!sim_bits(s, 1) || !sim_bits(s, 1)) return 0;
    if (sim_bits(s, 1)) return 1;
    return (int)sim_bits(s, 3);
}

/* 0x3104F and the twins: the treatment, read from the diagnosed illness (field 42). FE = "not for this room", FD /
 * FF = back to a waiting room. */
static void choose_treatment(Sim *s, int h, const RoomKind *k, uint8_t *p, const uint8_t *room, int player);
/* 0x3103D (docs/sim-rooms.md, "the doctor's work-counter bonuses"), at the end of every diagnosis: a diagnosis that is the
 * real illness (field 42 non-zero and equal to the low byte of field 56) gives the room's doctor (+10) 3 on +0x10. The
 * dental and neurology diagnoses call the same helper with the room's +10 too (the dentist, the neurologist). */
static void diagnosis_bonus(Sim *s, const uint8_t *p, const uint8_t *room) {
    if (p[P_DIAGNOSIS] && p[P_DIAGNOSIS] == p[P_ILLNESS] && get16(room + 10)) add16(staff_record(s, get16(room + 10)) + 0x10, 3);
}
/* 0x31277, in the treatment-finished step 0x3122B and its twins: the right diagnosis treated with the illness's best
 * treatment (its byte 11 = field 43) gives the room's doctor (+10) 1 more on +0x10. */
static void treatment_bonus(Sim *s, const uint8_t *p, const uint8_t *room) {
    if (p[P_DIAGNOSIS] && p[P_DIAGNOSIS] == p[P_ILLNESS] && illness(s, p[P_DIAGNOSIS])[11] == p[P_TREATMENT] &&
        get16(room + 10))
        add16(staff_record(s, get16(room + 10)) + 0x10, 1);
}
/* The wrong guess of 0x30ED0 / 0x34E36 (docs/sim-rooms.md, "The wrong guess of the diagnoses"): 600 tries of an
 * illness id random(70) + 1 and a column r3; an id whose ailment-table byte there is P's ailment joins a list of
 * sixteen (duplicates allowed). A list still short after the 600 tries gives the real illness (field 56). Otherwise S
 * + 1 pairs at most: two positions r15 and r15, stop on equal ids, else S - 1 and stop at 0; the guess is the first
 * position of the last pair. So an ailment held at few table places is diagnosed right. S is the skill (0x2D62D) of
 * the room's doctor (+10), as for the r127 test before. */
int sim_wrong_guess(Sim *s, const uint8_t *p, const uint8_t *room) {
    uint8_t list[16];
    int found = 0;
    for (int tries = 600; tries > 0 && found < 16; tries--) {
        int id = (int)sim_random(s, 70) + 1, column = (int)sim_bits(s, 3);
        if ((size_t)(id * 4 + column) < s->ailments_len && s->ailments[id * 4 + column] == p[P_AILMENT])
            list[found++] = (uint8_t)id;
    }
    if (found < 16) return p[P_ILLNESS];
    int a;
    for (int left = doctor_skill(s, room, get16(room + 10)) + 1;;) {
        a = (int)sim_bits(s, 15);
        int b = (int)sim_bits(s, 15);
        if (list[a] == list[b] || --left == 0) break;
    }
    return list[a];
}

/* 0x34E36, the dental diagnosis (docs/sim-rooms.md, "The dental diagnosis 0x34E36 can stay wrong"): with r127 < S the
 * real illness; otherwise an AI hospital draws r1 and a non-zero r1 gives the real illness too; else the wrong guess,
 * replaced by the real illness only when its type (byte 8) differs - for every hospital, with no byte-4 test. */
static void diagnose_dental(Sim *s, uint8_t *p, const uint8_t *room, int ai) {
    int real = p[P_ILLNESS], guess = real;
    if (!p[P_AILMENT]) guess = 0;
    else if ((int)sim_bits(s, 127) >= doctor_skill(s, room, get16(room + 10)) && !(ai && sim_bits(s, 1))) {
        guess = sim_wrong_guess(s, p, room);
        if (!real || illness(s, guess)[8] != illness(s, real)[8]) guess = real; /* !real: engine guard */
    }
    p[P_DIAGNOSIS] = (uint8_t)guess;
    diagnosis_bonus(s, p, room);
}

/* The tick's diagnosis at the end of the countdown, in every hospital (docs/sim-rooms.md, "The tick's diagnosis and
 * choice, in every hospital (SIM-36, SIM-37)", step 2): 0x30ED0 (treatment room), 0x34E36 (dental ward, above) and
 * 0x373D2 (neurology). No ailment: field 42 = 0 and no draw. Otherwise r127 < S gives the real illness, else the
 * wrong guess, which is drawn in AI hospitals too; then the correction: an AI treatment room takes the real illness
 * when field 56 is non-zero (else the guess stays), an AI neurology always (also 0); the player's hospital takes it
 * when the guess's type (byte 8) or byte-4 class differs (the neurology as the treatment room). Then 0x3103D. */
static void diagnose(Sim *s, int h, const RoomKind *k, uint8_t *p, const uint8_t *room) {
    if (k->cat == 11) {
        diagnose_dental(s, p, room, h != 0);
        return;
    }
    if (!p[P_AILMENT]) {
        p[P_DIAGNOSIS] = 0;
        return;
    }
    int skill = doctor_skill(s, room, get16(room + 10)), real = p[P_ILLNESS], guess = real;
    if ((int)sim_bits(s, 127) >= skill) {
        guess = sim_wrong_guess(s, p, room);
        if (h) {
            if (k->cat == 15 || get16(p + P_ILLNESS)) guess = real;
        } else if (!real || illness(s, guess)[8] != illness(s, real)[8] || illness(s, guess)[4] != illness(s, real)[4]) {
            guess = real; /* !real: engine guard */
        }
    }
    p[P_DIAGNOSIS] = (uint8_t)guess;
    diagnosis_bonus(s, p, room);
}

/* 0x3104F: the treatment for the diagnosis (field 42); the rank helper 0x31109 gives r3 in player mode. h is the
 * hospital whose room it is ([0xB054]). The dental chooser 0x349A9 has one more draw (docs/sim-drift8.md section
 * 20.2, SIM-20): after r7 non-zero and r127 not below S, in a hospital other than 0 an r1, and a non-zero r1 gives the
 * best treatment too; the treatment room 0x3104F and the neurology 0x35D82 have none. */
static void choose_treatment(Sim *s, int h, const RoomKind *k, uint8_t *p, const uint8_t *room, int player) {
    /* The doctor is this room's, but the rating belongs to the last [0x4514] writer (SIM-82/SIM-83). */
    int skill = doctor_skill_rating(s, sim_rating_byte(s), get16(room + 10));
    /* field 42 = 0 reads illness record 0, which is all zero: the treatment room draws as for any illness and takes
     * its byte 11, 0; the dental ward and the neurology give FE without a draw (docs/sim-rooms.md, "The tick's
     * diagnosis and choice", step 3) */
    const uint8_t *r = illness(s, p[P_DIAGNOSIS]);
    int t = 0;
    if (k->cat == 11) {
        if (p[P_WISH] || r[15] != 3) t = 0xFE;
        else if (r[8] == 2) t = 0xFD;
    } else if (k->cat == 15) {
        if (p[P_WISH] || r[8] == 0) t = 0xFE;
        else if (r[15] == 3) t = 0xFF;
    } else if (p[P_WISH]) {
        t = program_u8(s->prog, WISHES, p[P_WISH] * 4u);
    } else if (r[15] == 3) {
        t = 0xFF;
    } else if (r[8] == 2) {
        t = 0xFD;
    }
    if (!t && !p[P_WISH]) {
        int best = sim_bits(s, 7) && ((int)sim_bits(s, 127) < skill || (k->cat == 11 && h != 0 && sim_bits(s, 1)));
        t = best ? r[11] : r[11 + (player ? (int)sim_bits(s, 3) : rank_offset(s, skill))];
    }
    p[P_TREATMENT] = (uint8_t)t;
    p[P_STEPS] = t >= 0xFD ? (uint8_t)t : 0;
    p[P_IN_TREATMENT] = 0;
    if (t && t < 0xFD && !treatment(s, t)[6]) {
        p[P_IN_TREATMENT] = 0xFF;
        p[P_ROUNDS] = 0;
    }
}

static void free_room(uint8_t *tr) {
    put16(tr + 12, 0);
    put16(tr + 16, 0);
    put16(tr + 18, 0);
}

static void send_home_untreated(Sim *s, uint8_t *p, int h) {
    opinion_add(p, h, -1);
    p[P_HOSPITAL] = p[P_STATE] = 0xFF;
    put16(p + P_DIAGNOSIS, 0);
    p[P_STEPS] = 0;
    memset(p + P_UNITS, 0, 8);
    p[P_COUNTDOWN] = p[P_ROOM] = p[P_IN_TREATMENT] = p[P_TREATED_BY] = 0;
    p[82] = 0xFF, p[P_IN_STEP] = 0;
    memset(p + 96, 0, 8);
    for (int i = 0; i < 11; i++) set_health(p, 70 + (int)sim_bits(s, 15));
}

/* 0x2D11A step 4: back to a waiting room (450 tries), else home without a bill. The room or -1 (gone home). */
static int back_to_waiting(Sim *s, int h, uint8_t *tr, int index) {
    uint8_t *p = person(s, index);
    free_room(tr);
    for (int t = 1; t <= 450; t++) {
        int rooms = sim_rooms_in_use(s, h, 3);
        if (!rooms) break;
        int w = (int)sim_random(s, (uint32_t)rooms);
        uint8_t *wr = sim_room_record(s, h, 3, w);
        int i = (int)sim_random(s, 10), prio = image_byte(s, WAIT_PRIORITY, (uint32_t)i);
        if (wr[12] >= wr[10] || get16(wr + 0x14 + 4 * i) || !(prio == p[P_SEX] || (t >= 250 && prio == 2))) continue;
        put16(wr + 0x14 + 4 * i, index);
        put16(wr + 0x16 + 4 * i, 0);
        wr[12]++;
        p[P_STATE] = ST_WAITING;
        p[P_ROOM] = (uint8_t)w; /* field 66 = the room (docs/sim-rooms.md, step 4) */
        opinion_add(p, h, -1);
        p[P_DIAGNOSIS] = p[P_TREATMENT] = p[P_STEPS] = p[P_COUNTDOWN] = p[P_IN_TREATMENT] = 0;
        p[P_MOOD] /= 2;
        return w;
    }
    p[P_HOSPITAL] = p[P_STATE] = 0xFF;
    p[P_OPINION + h] /= 2;
    p[P_DIAGNOSIS] = p[P_TREATMENT] = p[P_STEPS] = p[P_COUNTDOWN] = p[P_IN_TREATMENT] = 0;
    return -1;
}

/* 0x2D11A step 1, a blood donor done (no bill): 0 = the blood went into the bank, 1 = no bank, 2 = a full bank */
static int donor_done(Sim *s, int h, uint8_t *tr) {
    int index = get16(tr + 12), treat;
    uint8_t *p = person(s, index), *r = hospital(s, h);
    treat = p[P_TREATMENT];
    int taken = (uint16_t)get16(tr + 20), result;
    add16(r + 0x3E, 1);
    free_room(tr);
    add32(r + 0x40, taken);
    put16(tr + 20, 0);
    work_done(s, get16(tr + 10));
    work_done(s, get16(tr + 8));
    p[P_HOSPITAL] = 0xFF, p[P_TREATED_BY] = (uint8_t)h;
    uint8_t *bank = room_counts(s, h)[17] ? sim_room_record(s, h, 17, 0) : NULL;
    if (bank && get32(bank + 0x0C) != get32(bank + 0x10)) { /* into the bank, no staff needed */
        int32_t stock = get32(bank + 0x0C) + taken, over = stock - get32(bank + 0x10);
        if (over > 0) stock -= over, add32(r + 0x66, over); /* the overflow is lost */
        put32(bank + 0x0C, stock);
        p[P_COUNTDOWN] = 1;
        result = 0;
    } else { /* no bank, or a full one */
        add32(r + 0x66, taken);
        if (h == 0) p[P_OPINION] = 1, p[P_MOOD] = 1;
        p[P_COUNTDOWN] = treat && treat < 0xFD ? treatment(s, treat)[7] : 0;
        result = bank ? 2 : 1;
    }
    p[P_STATE] = 0xFF, p[P_IN_TREATMENT] = 0xFF, p[P_ROUNDS] = 0;
    return result;
}

/* 0x2D11A and its twins 0x34C5C / 0x35E59 for an AI hospital */
static void after_diagnosis(Sim *s, int h, const RoomKind *k, uint8_t *tr) {
    int index = get16(tr + 12);
    uint8_t *p = person(s, index), *r = hospital(s, h);
    if (get16(tr + 18) && get16(tr + 18) != 999) return; /* treatment still running */
    int treat = p[P_TREATMENT];
    if (k->cat == 5 && p[P_WISH] == 5 && get16(tr + 16) && (int16_t)get16(p + P_BLOOD) < 690) { /* donor: no bill */
        donor_done(s, h, tr);
        return;
    }
    if (get16(tr + 16) || p[P_IN_TREATMENT]) { /* discharge with bill, treatment continues at home */
        sim_rating_record(s, p);
        add16(r + 0x26, 1);
        if (k->cat == 11) add16(r + 0x30, 1); /* the dental and neuro twins enter one step earlier */
        if (k->cat == 15) add16(r + 0x4E, 1);
        free_room(tr);
        p[P_HOSPITAL] = 0xFF;
        p[P_TREATED_BY] = (uint8_t)h;
        int bill = sim_invoice(s, h, p);
        pay(s, h, bill);
        opinion_after_bill(p, h, bill, 0);
        sim_news_bill(s, h, index, bill, 0); /* 0x42982 */
        p[P_STATE] = 0xFF;
        p[P_COUNTDOWN] = treat && treat < 0xFD ? treatment(s, treat)[7] : 0;
        p[P_IN_TREATMENT] = 0xFF, p[P_ROUNDS] = 0;
        return;
    }
    if (!get16(tr + 18)) sim_rating_record(s, tr); /* the decision refreshes only after the chooser */
    /* step 4: the treatment room tests the diagnosed illness (field 42): byte 15 = 3 or type 2; never reached in an AI
     * hospital while the take and the diagnosis follow the spec, kept as the code has it. The twins' choices give
     * FD / FE / FF instead; no treatment at all (no ailment, no wish) takes the same way (engine choice). */
    const uint8_t *dr = p[P_DIAGNOSIS] ? illness(s, p[P_DIAGNOSIS]) : NULL;
    if ((k->cat == 5 && dr && (dr[15] == 3 || dr[8] == 2)) || !treat || treat >= 0xFD) {
        flow_of(s, k)[5]++;
        back_to_waiting(s, h, tr, index);
        return;
    }
    if (!treatment(s, treat)[4]) { /* treatment in the room: the equipment check, then pending */
        decide_equipment(s, k->cat, tr, treat);
        put16(tr + 16, treat);
        put16(tr + 18, 999);
        return;
    }
    if (k->cat == 15) { /* to the last neuro ward with a free bed (0x2B52C, 0x235FA), else home untreated */
        int ward = 0;
        if (room_counts(s, h)[16] && sim_free_beds(s, h, 16, &ward, NULL)) {
            to_neuro_bed(s, h, tr, index, ward); /* no bed found: nothing, P stays */
            return;
        }
        free_room(tr);
        send_home_untreated(s, p, h);
        return;
    }
    free_room(tr);
    uint8_t *queue = reception(s, h) + 0x46; /* treatment in a bed: into the Krankenzimmer queue */
    int place = -1;
    for (int i = 0; i < 6 && place < 0; i++)
        if (!get16(queue + 4 * i)) place = i;
    if (room_counts(s, h)[6] && place >= 0) {
        put16(queue + 4 * place, index);
        put16(queue + 4 * place + 2, 0);
        p[P_STATE] = ST_RECEPTION;
        work_done(s, get16(tr + 8));
        work_done(s, get16(tr + 10));
        return;
    }
    send_home_untreated(s, p, h);
}

/* the operators' patient part (0x2CB75, 0x34535, 0x35430), with both staff present (the caller's branch, sim_staff.c;
 * the dental comfort r7 is drawn there). Returns 1 when the room was free and nobody waited: the caller's release
 * check (docs/sim-drift7.md section 4.1); 0 after a patient's step or a bring. */
static int room_operator(Sim *s, int h, int cat, int room) {
    const RoomKind *k = kind_of(cat);
    uint8_t *tr = sim_room_record(s, h, cat, room);
    if (!get16(tr + 8) || !get16(tr + 10)) return 0;
    int index = get16(tr + 12);
    if (index) {
        uint8_t *p = person(s, index);
        if (p[P_DEAD]) {
            free_room(tr);
            died_found(s, h, index);
        } else if (!get16(tr + 14) && (!get16(tr + 16) || !get16(tr + 18))) {
            /* only the chooser, in AI mode: the tick has diagnosed (docs/sim-rooms.md, "The tick's diagnosis and
             * choice", step 4; SIM-37) */
            if (!get16(tr + 16)) choose_treatment(s, h, k, p, tr, 0);
            after_diagnosis(s, h, k, tr);
        }
        return 0;
    }
    if (!waiting_for(s, h, k)) return 1;
    int got = bring_patient(s, h, k, tr, room, 0, NULL);
    if (got && h) {
        int type = patient_type(s, person(s, got));
        flow_of(s, k)[type < 2 ? type : 2]++;
    }
    return 0;
}

/* 0x34D22: nurse care (wards, dental ward, intensive care) for a patient in a room of hospital h. A raised mood or
 * opinion sets the player block's +0x49 (block 28 for h = 0, 29 for h = 1; the sheet's mood redraw, docs/sim-rooms2.md,
 * "The player block's mood flag +0x49") */
static void nurse_care(Sim *s, int h, uint8_t *p, const uint8_t *room, int comfort, int health_flag) {
    int hour = world_tail(s->world, TAIL_HOUR);
    if (!comfort || !p[102] || hour < 5 || hour > 20) return;
    /* the nurse's bust - 60 as an 8-bit difference (BL), so a bust below 60 and staff record 0 of a ward without a nurse
     * give 196-255, which every draw passes (SIM-62) */
    int b = (uint8_t)(staff_record(s, get16(room + 8))[5] - 60);
    /* FIX-53 (docs/fixes.md): original wraps the difference; fixed a signed difference (and no care without a nurse) */
    if (fix_on(53)) b = staff_record(s, get16(room + 8))[5] - 60;
    if ((int)sim_bits_site(s, 127, 0x34D93) >= b || (int)sim_bits(s, 127) >= b) return;
    int c = 1 + ((int)sim_bits(s, 127) < b); /* one statement per draw */
    c += (int)sim_bits(s, 127) < b;
    if (p[P_HEALTH] < 90) {
        if (p[50] >= 20) c += 4;
        set_health(p, p[P_HEALTH] + c);
        notify_watched_patient(s, h, index_of(s, p), health_flag); /* SIM-90: only after the health update */
    }
    uint8_t *flag = h == 0 || h == 1 ? s->world->block[h ? BLK_PLAYER1 : BLK_PLAYER0] + 0x49 : NULL;
    if (p[P_MOOD] < 100) { /* a failed mood draw ends the care: no opinion draw (0x34DF8-0x34E33, SIM-54) */
        if ((int)sim_bits_site(s, 63, 0x34E03) >= b) return;
        p[P_MOOD]++;
        if (flag) *flag = 0xFF;
    }
    if (p[P_MOOD] >= 80 && (int)sim_bits_site(s, 63, 0x34E1A) < b && (int8_t)p[P_HOSPITAL] >= 0 &&
        (int8_t)p[P_OPINION + p[P_HOSPITAL]] < 100) {
        opinion_add(p, p[P_HOSPITAL], 1);
        if (flag) *flag = 0xFF;
    }
}
void sim_nurse_care(Sim *s, int h, uint8_t *p, const uint8_t *room, int comfort, int health_flag) {
    nurse_care(s, h, p, room, comfort, health_flag);
}

/* 0x30E7C, the staff credit of a diagnosing tick (docs/sim-drift2.md section 7): r15 = 3 credits the nurse (+8), a new
 * r15 = 7 the doctor, dentist or neurologist (+0x0A), a third r15 = 7 the patient's field 54. */
static void staff_credit(Sim *s, const uint8_t *tr, uint8_t *p) {
    if (sim_bits(s, 15) == 3) work_done(s, get16(tr + 8));
    if (sim_bits(s, 15) == 7) work_done(s, get16(tr + 10));
    if (sim_bits(s, 15) == 7) p[54]++;
}

/* 0x6AD3C (treatment rooms), 0x6B445 (dental ward), 0x6BAE4 (neurology): every hospital */
static void room_tick(Sim *s, int cat) {
    const RoomKind *k = kind_of(cat);
    int progress_flag = cat == 5 ? 0x32 : cat == 11 ? 0x3A : 0x41;
    for (int h = 0; h < 16; h++) {
        if (cat == 5) { /* the r7 index first, then the r127 x 4 value (docs/sim-rooms.md; SIM-31 audit) */
            uint32_t at = 0x1A6 + 2 * sim_bits(s, 7);
            int v = (int)sim_bits(s, 127) * 4;
            put16(hospital(s, h) + at, v);
        }
        for (int room = 0; room < sim_rooms_in_use(s, h, cat); room++, sim_rating_advance(s, 180)) {
            if (!room) sim_rating_room(s, h, cat, 0);
            uint8_t *tr = sim_room_record(s, h, cat, room);
            int index = get16(tr + 12);
            if (!index) { /* 0x2E666 for the nurse (+8; the dental ward clears its comfort +0x19 with her), then the doctor */
                sim_staff_break(s, h, cat, room, tr + 8, cat == 11 ? tr + 0x19 : NULL);
                sim_staff_break(s, h, cat, room, tr + 10, NULL);
                continue;
            }
            if (!get16(tr + 8) || !get16(tr + 10)) continue;
            uint8_t *p = person(s, index);
            care_drift(s, tr, index, 0); /* 0x2EB8D and 0x2E7EB right after the staff test (docs/sim-drift2.md section 7) */
            drift(s, p, h, tr);
            if (cat != 15) {
                if (p[P_HOSPITAL] != h) {
                    put16(tr + 12, 0);
                    if (cat == 5) add16(hospital(s, h) + 0x168, 1);
                    continue;
                }
                p[P_STATE] = k->bring_state; /* 5, or 11 in the dental station (its state, docs/ui-departments.md) */
                p[P_ROOM] = (uint8_t)room;
            }
            if (p[P_DEAD]) continue;
            if (cat == 5) { /* SIM-110: this caller retains its patient/row pair through all worsening draws */
                SimIn old = sim_set_inputs(s, sim_in(sim_address(s, p), rng_row(s, h), s->rng_scratch));
                worsen_in_treatment(s, h, p, 0x31);
                sim_set_inputs(s, old);
            }
            else if (cat == 11) nurse_care(s, h, p, tr, tr[0x19], 0x39);
            int diag = get16(tr + 14), cnt = get16(tr + 18);
            if (diag == 999) {
                /* the diagnosis start (docs/sim-drift2.md section 3): an r1 draw while +0x45 is 0 (the neurology always,
                 * with no check); after a miss no draw, and a human hospital with a self-fetching clerk fetches the
                 * missing kind (engine choice: only when the room lacks it; the original's 32-bit test can pass with
                 * the kind present). Then the check and use 0x31164 of equipment kind 9 (treatment room) or 12 (dental
                 * ward): the missing kind into +0x45 and the diagnosis waits. */
                if (cat == 15 || !tr[0x45]) {
                    if (sim_bits(s, 1)) continue;
                } else if (self_fetching(s, h) && !equipment_id(s, tr, cat, tr[0x45], 0, 1)) {
                    sim_store_supply(s, h, cat, tr, tr[0x45], 1);
                }
                if (cat != 15 && (tr[0x45] = (uint8_t)use_kind(s, tr, cat, cat == 5 ? 9 : 12)) != 0) continue;
                p[54]++;
                notify_watched_patient(s, h, index, progress_flag);
                put16(tr + 14, p[P_ILLNESS] && !p[P_WISH] ? illness(s, p[P_ILLNESS])[18] : 3);
            } else if (diag) {
                staff_credit(s, tr, p); /* 0x30E7C; the dental ward's comes after its +0x0E - 1, which draws nothing */
                for (int d = 0; d < 4; d++)
                    if ((int)sim_bits(s, k->diag[d][0]) == k->diag[d][1]) p[k->diag[d][2]]++;
                put16(tr + 14, diag - 1);
                /* at 0: the diagnosis and the room's chooser with [0x5ABD] clear (rank = r3), in every hospital; an AI
                 * operator later chooses again in AI mode (SIM-36, SIM-37) */
                if (diag == 1) {
                    notify_watched_patient(s, h, index, progress_flag);
                    diagnose(s, h, k, p, tr);
                    if (h) {
                        flow_of(s, k)[3]++;
                        if (p[P_DIAGNOSIS] && p[P_DIAGNOSIS] == p[P_ILLNESS]) flow_of(s, k)[4]++;
                    }
                    choose_treatment(s, h, k, p, tr, 1);
                }
            } else if (!get16(tr + 16)) {
                worsen(s, h, p, cat == 5 ? 0x31 : cat == 11 ? 0x39 : 0x40);
                if (p[P_DEAD] && h) {
                    free_room(tr);
                    died(s, h, index);
                }
            } else if (cnt == 999) {
                /* treatment pending (0x6B1FA, 0x6B89A, 0x6BDF6; docs/sim-drift.md section 3): without an equipment gap
                 * r1 = 0 starts it; with one the check runs again, and once the gap is closed the items are used
                 * and the start waits for a later tick's draw. Only the player's hospital with a self-fetching
                 * clerk orders here; the AI's order is the operator's supplies step. */
                if (!get16(tr + 0x44)) {
                    if (!sim_bits(s, 1)) {
                        if (cat == 11) p[54]++, p[55]++; /* the dental start 0x6BA2B books two units (docs/sim-drift3.md section 2) */
                        notify_watched_patient(s, h, index, progress_flag);
                        const uint8_t *t = treatment(s, get16(tr + 16));
                        int n = 0;
                        while (n < 5 && t[16 + n]) n++;
                        p[P_STEPS] = (uint8_t)n;
                        put16(tr + 18, 10);
                    }
                } else {
                    uint8_t miss[2];
                    int treat = p[P_TREATMENT];
                    equipment_check(s, cat, tr, treat, 0, miss);
                    tr[0x44] = miss[0], tr[0x45] = miss[1];
                    if (!miss[0] && !miss[1]) equipment_check(s, cat, tr, treat, 1, miss);
                    else if (self_fetching(s, h)) equipment_needs(s, h, cat, tr, 0x44, 0);
                }
            } else if (cnt) {
                /* a blood donor at step code 10, once per tick: r7 + 6 is always drawn; below 220 it is added to +20
                 * and taken from the u16 at +0x5E (the condition of equipment slot 4), not from the donor's blood -
                 * so +20 carries over to the next donor and only donors who arrive below 690 count (docs/sim-rooms.md) */
                if (cat == 5 && p[P_WISH] == 5 && treatment(s, get16(tr + 16))[15 + p[P_STEPS]] == 10) {
                    int x = (int)sim_bits(s, 7) + 6;
                    if ((int16_t)get16(tr + 20) < 220) {
                        add16(tr + 20, x);
                        put16(tr + 0x5E, (uint16_t)(get16(tr + 0x5E) - x));
                        notify_watched_patient(s, h, index, progress_flag);
                    }
                }
                for (int d = 0; d < 4; d++) /* a new r31 draw before each of the four tests (docs/sim-drift3.md section 2) */
                    if ((int)sim_bits(s, 31) == k->treat[d][0]) p[k->treat[d][1]]++;
                cnt--;
                put16(tr + 18, cnt);
                if (cnt == 2 && p[P_STEPS]) {
                    p[P_STEPS]--;
                    const uint8_t *t = treatment(s, get16(tr + 16));
                    int code = k->step_bug ? t[p[P_STEPS]] : t[15 + p[P_STEPS]];
                    int d = image_byte(s, STEP_DURATION, (uint32_t)code);
                    cnt = d < 3 ? 3 : d;
                    p[54]++, p[55]++;
                    put16(tr + 18, cnt);
                    notify_watched_patient(s, h, index, progress_flag);
                }
                if (!cnt) { /* 0x3122B, and the dental twin 0x34F80 byte for byte; the neurology's 0x37539 books no fields
                             * 48 and 53 (docs/sim-drift3.md section 2) */
                    notify_watched_patient(s, h, index, progress_flag);
                    if (cat != 15) p[48]++, p[53]++;
                    p[P_SKILL] = (uint8_t)doctor_skill(s, tr, get16(tr + 10));
                    treatment_bonus(s, p, tr);
                    p[P_IN_TREATMENT] = 0xFF;
                    p[P_ROUNDS] = 0;
                }
            }
        }
    }
}

/* ---- care drift (0x2EB8D for a room record, 0x2ED92 for every place of a ward or waiting room) ------------ */

/* docs/sim-rooms2.md "Care drift": a drop by the room's ratings V, then a nurse bonus by the nurse's looks */
/* A nonzero bonus_context selects a multi-place loop's retained address (SIM-108, docs/rng-sites.md 3.9).
 * Drop draws use the current patient. Zero selects the unchanged single-room caller inputs. */
static void care_drift(Sim *s, const uint8_t *rec, int index, uint32_t bonus_context) {
    uint8_t *p = person(s, index);
    SimIn old = s->rng_in;
    if (bonus_context) sim_set_inputs(s, rng_bed(s, rec, sim_address(s, p)));
    int v = rec[0] + 2 * rec[1];
    if (sim_bits_site(s, 3, 0x2ECA7) == 0 && (int)sim_bits_site(s, 63, 0x2ECB6) >= v && (int8_t)p[P_HEALTH] >= 35 &&
        (int8_t)p[P_HEALTH] <= 94) {
        enum { NONE, MILD, STRONG } drop;
        int f50 = p[50];
        const uint8_t *ill = p[P_ILLNESS] ? illness(s, p[P_ILLNESS]) : NULL;
        if (!ill) drop = !p[52] ? NONE : p[P_RECOVERY] ? MILD : f50 >= 34 ? NONE : f50 >= 17 ? MILD : STRONG;
        else if (!ill[1]) drop = !f50 ? (ill[17] ? STRONG : MILD) : f50 >= 50 ? NONE : f50 >= 35 ? MILD : STRONG;
        else if (p[52]) drop = p[P_RECOVERY] ? MILD : f50 >= 40 ? NONE : f50 >= 30 ? MILD : STRONG;
        else drop = !ill[17] ? MILD : (int)sim_bits(s, 63) < (int8_t)ill[17] ? STRONG : MILD;
        if (drop != NONE) {
            int d = drop == STRONG ? (int)sim_bits(s, 3) + 2 : (int)sim_bits(s, 1);
            if (drop != STRONG) d += (int)sim_bits(s, 1); /* r1 then r1, in order */
            set_health(p, (uint8_t)(p[P_HEALTH] - d));
        }
    }
    if (bonus_context) sim_set_inputs(s, rng_bed(s, rec, bonus_context));
    int nurse = get16(rec + 8);
    /* logged under the bed loops' site 0x2EB06 or the single records' own 0x2EBBC (docs/sim-drift6.md section 1) */
    int bonus = nurse && sim_bits_site(s, 15, bonus_context ? 0x2EB06 : 0x2EBBC) == 5 && !p[P_DEAD] && (int8_t)p[P_HEALTH] < 94;
    if (bonus) nurse_bonus(s, p, nurse, v);
    sim_set_inputs(s, old);
}

static void nurse_bonus(Sim *s, uint8_t *p, int nurse, int v) {
    const uint8_t *n = staff_record(s, nurse);
    int c = n[5];
    if (n[6] < 54) c += 4 * (54 - n[6]);
    if (n[7] >= 97) c += 4 * (n[7] - 96);
    if (n[8] < 22) c += 8 * (23 - n[8]);
    c = (int16_t)(c - 100 + (uint16_t)v / 4);
    if (c < 0 || (int)sim_bits(s, 63) >= c) return;
    uint8_t h = (uint8_t)(p[P_HEALTH] + sim_bits(s, 3) + 2);
    if ((int8_t)h < 90) {
        set_health(p, h);
        work_done(s, nurse);
    }
    if ((int8_t)p[P_STATE] >= 5 && sim_bits(s, 3) == 1) p[55]++;
    if (p[P_MOOD] < 100) p[P_MOOD]++; /* not under the state test or the r3 draw (docs/sim-drift2.md section 7) */
}

/* ---- shared by intensive care, the OP and the torture chamber ------------------------------------------ */

static uint8_t *store_of(Sim *s, int h) { return room_counts(s, h)[4] ? sim_room_record(s, h, 4, 0) : NULL; }

/* 0x2C06B, the store-usable test (docs/sim-rooms2.md, "The store-screen test"): false while the player is on the
 * inventory, screen 65 (0x41; player 2's screen word is always the office); otherwise a store room whose clerk works
 * independently (staff byte 11). Only called for a human-controlled hospital (+0x14 set). */
static int store_usable(Sim *s, int h) {
    uint8_t *st = store_of(s, h);
    return s->screen != SCREEN_INVENTORY && st && get16(st + 4) && staff_record(s, get16(st + 4))[11];
}

static int self_fetching(Sim *s, int h) { return hospital(s, h)[0x14] && store_usable(s, h); }

/* The player-only equipment request (docs/sim-rooms2.md "Shared helpers"): a human-controlled hospital whose store is
 * usable (0x2C06B). */
static void player_request(Sim *s, int h, int cat, uint8_t *rec, uint8_t *req) {
    if (!*req || !self_fetching(s, h)) return;
    if (equipment(s, rec, cat, *req, 0) || sim_store_supply(s, h, cat, rec, *req, 0) > 0) *req = 0;
}

/* The AI operators' equipment step: the missing capability (byte cap) and kind (cap + 1), each when the room lacks
 * it, from the store. Gated by a present clerk and a non-zero request word; the request bytes stay set.
 * Engine choice: a request byte of 0 is skipped (the original would look for "capability 0"). */
static void equipment_needs(Sim *s, int h, int cat, uint8_t *rec, int cap, int clerk_gate) {
    uint8_t *st = store_of(s, h);
    if (!st || (clerk_gate && !get16(st + 4)) || !get16(rec + cap)) return;
    if (rec[cap] && !equipment_id(s, rec, cat, rec[cap], 0, 0)) sim_store_supply(s, h, cat, rec, rec[cap], 0);
    if (rec[cap + 1] && !equipment_id(s, rec, cat, rec[cap + 1], 0, 1)) sim_store_supply(s, h, cat, rec, rec[cap + 1], 1);
}

/* The supplies step of the treatment-room, dental and neurology operators (0x2CB75 at 0x2CD74, 0x34535 at 0x34754,
 * 0x35430 at 0x35636; docs/sim-drift.md section 3): with both staff present, a store room and u16 +0x44 non-zero, the
 * missing capability +0x44 and kind +0x45 are taken from the store and installed, or requested through h+0xDA /
 * h+0xDB. The bytes stay; the tick's next check clears them once the item is in. */
void sim_rooms_supplies(Sim *s, int h, int cat, int room) {
    uint8_t *tr = sim_room_record(s, h, cat, room);
    if (!get16(tr + 8) || !get16(tr + 10)) return;
    equipment_needs(s, h, cat, tr, 0x44, 0);
}

static int reception_staffed(Sim *s, int h) { return room_counts(s, h)[2] && get16(reception(s, h) + 8); }

/* Asks for one staff member of the group for a room field (docs/sim-staff.md); the caller ends its call. */
static void ask_staff(Sim *s, int h, int group, int cat, int room, uint8_t *field) {
    int slot, who = sim_assignable(s, h, group, &slot);
    if (who) sim_assign(s, h, who, slot, cat, room, field);
}

/* The blood bank (category 17, docs/sim-ai.md): +8 staff, s32 +0x0C stock, u16 +0x14 organs, slots at +0x58 */
static uint8_t *bank_ready(Sim *s, int h) {
    if (!room_counts(s, h)[17]) return NULL;
    uint8_t *bank = sim_room_record(s, h, 17, 0);
    return get16(bank + 8) && get32(bank + 0x0C) ? bank : NULL;
}
/* x cl from the bank into P, at most the stock (and 20 when capped); units 48 and 53, and 55 too in intensive care
 * (the OP's three transfusions book 48 and 53 only: docs/sim-rooms2.md, "0x58045", steps 7 and 8; SIM-35) */
static void transfuse(Sim *s, int h, uint8_t *bank, uint8_t *p, int x, int cap20, int icu) {
    int32_t stock = get32(bank + 0x0C);
    if (x >= stock) x = (int)stock;
    if (cap20 && (int16_t)x >= 20) x = 20;
    put32(bank + 0x0C, stock - x);
    add32(hospital(s, h) + 0x5A, x);
    put16(p + P_BLOOD, get16(p + P_BLOOD) + x);
    p[48]++, p[53]++;
    if (icu) p[55]++;
    work_done(s, get16(bank + 8));
}

/* ---- intensive care (category 9, 186-byte records) ------------------------------------------------------ */

enum { ICU_WANT = 4, ICU_REQUEST = 6, ICU_STAFF = 8, ICU_PATIENT = 0x0C, ICU_TREAT = 0x0E, ICU_COMFORT = 0x0F,
       ICU_HOURS = 0x10 };

/* 0x4F395: a Krankenzimmer patient below 30 health into the first free intensive-care record */
static int icu_admit(Sim *s, int h, uint8_t *ward, int index) { /* the record, or -1 (none free), -2 (no nurse) */
    int r = sim_free_icu(s, h);
    if (r < 0) return -1;
    if (!get16(ward + 8)) return -2; /* the nurse carries the patient */
    ward[12]--;
    for (int b = 0; b < 10; b++) /* not found: the original loops for ever (cannot happen from 0x2995B) */
        if (get16(ward + 0x14 + 4 * b) == index) {
            memset(ward + 0x14 + 4 * b, 0, 4);
            break;
        }
    uint8_t *rec = sim_room_record(s, h, 9, r), *p = person(s, index);
    put16(rec + ICU_PATIENT, index);
    add16(hospital(s, h) + 0x56, 1);
    p[P_STATE] = ST_ICU, p[P_ROOM] = (uint8_t)r, p[P_IN_STEP] = 0;
    put16(rec + ICU_HOURS, 0);
    rec[ICU_TREAT] = 0xFF;
    return r;
}

/* "use" a capability; the request byte +6 takes the result when it is 0 or that capability */
static int icu_use(Sim *s, uint8_t *rec, int cap) {
    int found = equipment_id(s, rec, 9, cap, 1, 0) != 0;
    if (!rec[ICU_REQUEST] || rec[ICU_REQUEST] == cap) rec[ICU_REQUEST] = found ? 0 : (uint8_t)cap;
    return found;
}

/* 0x70321: a death by the treatment or round step in an AI hospital. The original hands 0x31299 the room record
 * as the person record. Without a pathology place that only changes the newspaper item, which the engine writes as
 * the original does (SIM-18, docs/sim-rooms2.md); with one the original lists the person in the pathology without
 * state 18 and later hangs 0x6F6B2, and the engine choice is the person record (no hang, docs/engine-care.md). */
static void icu_death_check(Sim *s, int h, int k, uint8_t *rec, int index) {
    if ((int8_t)person(s, index)[P_HEALTH] >= 1 || hospital(s, h)[0x14]) return;
    person(s, index)[P_DEAD] = 1;
    put16(rec + ICU_PATIENT, 0);
    put16(rec + ICU_HOURS, 0);
    died_as(s, h, index, 0, rec, k);
}

/* 0x6FA5A: intensive care per minute */
static void icu_minute(Sim *s) {
    for (int h = 0; h < 16; h++)
        for (int k = 0; k < sim_rooms_in_use(s, h, 9); k++, sim_rating_advance(s, 186)) {
            if (!k) sim_rating_room(s, h, 9, 0);
            uint8_t *rec = sim_room_record(s, h, 9, k);
            sim_staff_break(s, h, 9, k, rec + ICU_STAFF, rec + ICU_COMFORT); /* 0x2E666 when staffed, before the patient test */
            int index = get16(rec + ICU_PATIENT), nurse = get16(rec + ICU_STAFF);
            if (!index) continue;
            care_drift(s, rec, index, 0);
            uint8_t *p = person(s, index);
            if (p[P_DEAD]) continue;
            p[P_IN_STEP] = 0xFF;
            player_request(s, h, 9, rec, rec + ICU_REQUEST);
            worsen(s, h, p, 0x42);
            worsen_in_treatment(s, h, p, 0x42);
            if (nurse) {
                if (rec[ICU_COMFORT] && sim_bits(s, 15) == 3) work_done(s, nurse);
                if ((int8_t)p[P_HEALTH] < 20 && sim_bits(s, 31) == 17) work_done(s, nurse);
                nurse_care(s, h, p, rec, rec[ICU_COMFORT], 0x42);
                int found = icu_use(s, rec, 16);
                if (found && (int8_t)p[P_HEALTH] < 50 && sim_bits(s, 31) == 13) p[53]++;
                if ((int8_t)p[P_HEALTH] < 50) {
                    if (sim_bits(s, 127) == 12) p[55]++;
                    if (sim_bits(s, 31) == 13 && !icu_use(s, rec, 5)) {
                        /* missing: the next two draws are skipped */
                    } else {
                        if (sim_bits(s, 127) == 5) p[48]++;
                        if (sim_bits(s, 15) == 3) p[51]++;
                    }
                }
                int blood = get16(p + P_BLOOD);
                if (blood < 700) {
                    int bank = 1;
                    if (p[P_FIELD100] >= 2 && sim_bits(s, 15) == 4) {
                        blood += (int)sim_bits(s, 1);
                        put16(p + P_BLOOD, blood);
                        if (blood >= 700) put16(p + P_BLOOD, 700), bank = 0;
                    }
                    uint8_t *b = bank ? bank_ready(s, h) : NULL;
                    if (b && icu_use(s, rec, 17)) transfuse(s, h, b, p, 700 - get16(p + P_BLOOD), 1, 1);
                }
            }
            if (p[P_RECOVERY]) { /* no skip, unlike the ward */
                p[P_RECOVERY] = (uint8_t)(p[P_RECOVERY] - sim_bits(s, 1));
                if (!p[P_RECOVERY]) {
                    notify_watched_patient(s, h, get16(rec + ICU_PATIENT), 0x43);
                    notify_watched_patient(s, h, get16(rec + ICU_PATIENT), 0x42);
                    p[P_STEPS]--;
                }
            }
            const uint8_t *t = treatment(s, p[P_TREATMENT]);
            if (!rec[ICU_TREAT]) goto round_check;
            if (!t[36]) { /* a single healing treatment */
                if (sim_bits(s, 7) != 2 || !nurse) goto done;
                int c = t[15 + p[P_STEPS]];
                if (c == 8) { /* code 8 only counts down here */
                    if (p[P_COUNTDOWN]) p[P_COUNTDOWN]--;
                    goto done;
                }
                if ((int8_t)p[P_HEALTH] >= 50 || c != 17 || p[P_FIELD100] < 2) goto done;
                int b;
                if (p[P_ILLNESS]) {
                    const uint8_t *ill = illness(s, p[P_ILLNESS]);
                    b = (p[P_TREATMENT] == ill[11] ? 2 : 0) + (p[P_TREATMENT] == ill[12]);
                } else if (p[P_WISH]) {
                    b = 2;
                } else {
                    goto done;
                }
                if (rec[ICU_REQUEST] == 16 || rec[ICU_REQUEST] == 5) b = 0;
                int blood = get16(p + P_BLOOD);
                b -= (blood < 680) + (blood < 660) + (blood < 640) + 2 * (blood < 610);
                b += (int)sim_bits(s, 1); /* + r1 - r1 - r1 in this order, then 8-bit */
                b -= (int)sim_bits(s, 1);
                b -= (int)sim_bits(s, 1);
                b = (int8_t)b;
                if (icu_use(s, rec, 10)) b++;
                if ((int8_t)p[P_HEALTH] < 20) {
                    if (icu_use(s, rec, 8)) b++;
                    if (icu_use(s, rec, 7)) b++;
                }
                set_health(p, (uint8_t)(p[P_HEALTH] + b));
                icu_death_check(s, h, k, rec, index);
                post_cleanup_health_notification(s, h, rec + ICU_PATIENT, p, 0x42);
                if (p[P_HEALTH] >= 100) p[P_STEPS]--, p[P_COUNTDOWN] = 4;
                goto done;
            }
            /* a stepped treatment: the countdown wraps at 0 (8-bit, reproduce) */
            if (--p[P_COUNTDOWN]) goto round_check;
            if (!nurse) {
                p[P_COUNTDOWN] = 2;
                goto round_check;
            }
            notify_watched_patient(s, h, get16(rec + ICU_PATIENT), 0x43);
            if (p[P_STEPS] && --p[P_STEPS]) {
                p[P_COUNTDOWN] = (uint8_t)duration(s, t, t[15 + p[P_STEPS]]);
                p[55]++;
            } else {
                p[96]++;
                int n = 0;
                while (n < 5 && t[16 + n]) n++;
                p[P_STEPS] = (uint8_t)n;
                p[P_COUNTDOWN] = (uint8_t)duration(s, t, t[15 + p[P_STEPS]]);
            }
        round_check:
            if (p[96] >= 2 && !p[P_WISH] && p[P_ILLNESS]) {
                const uint8_t *ill = illness(s, p[P_ILLNESS]);
                int b = 0, hit = 0, zero = rec[ICU_REQUEST] == 16 || rec[ICU_REQUEST] == 5;
                if (p[P_TREATMENT] == ill[11]) {
                    if ((hit = sim_bits(s, 31) == 0)) {
                        b = ill[7] + (int)sim_bits(s, 1); /* one statement per draw */
                        b += (int)sim_bits(s, 1);
                        if (zero) b = 0;
                        b -= (int)sim_bits(s, 1);
                    }
                } else if (p[P_TREATMENT] == ill[12]) {
                    if ((hit = sim_bits(s, 63) == 0)) {
                        b = ill[7] / 2 + 1 + (int)sim_bits(s, 1);
                        if (zero) b = 0;
                        b -= (int)sim_bits(s, 1); /* one statement per draw */
                        b -= (int)sim_bits(s, 1);
                    }
                }
                if (hit) {
                    set_health(p, (uint8_t)(p[P_HEALTH] + b));
                    icu_death_check(s, h, k, rec, index);
                    post_cleanup_health_notification(s, h, rec + ICU_PATIENT, p, 0x42);
                }
            }
        done:
            if (p[P_MOOD] > 100) p[P_MOOD] = 100;
        }
}

/* 0x5A3CD: intensive care hourly */
static void icu_hourly(Sim *s) {
    for (int h = 0; h < 16; h++)
        for (int k = 0; k < sim_rooms_in_use(s, h, 9); k++) {
            uint8_t *rec = sim_room_record(s, h, 9, k);
            int index = get16(rec + ICU_PATIENT);
            if (!index) continue;
            uint8_t *p = person(s, index);
            if (p[P_DEAD] || p[P_STATE] != ST_ICU) continue;
            add16(rec + ICU_HOURS, 1);
            if (get16(rec + ICU_STAFF)) {
                if (get16(p + P_HEALTH) < 50) p[51]++; /* fields 44 and 45 as one signed word (reproduce) */
                p[55]++;
            }
            if (p[P_MOOD] > 100) p[P_MOOD] = 100;
        }
}

/* 0x6F346: the intensive-care operator */
static void icu_operator(Sim *s) {
    World *w = s->world;
    int h = sim_pick(s);
    if (!h || !room_counts(s, h)[9]) return;
    int r = (int)sim_random_in(s, (uint32_t)sim_rooms_in_use(s, h, 9), 0x6F392, rng_operator(s, h, NULL));
    uint8_t *rec = sim_room_record(s, h, 9, r);
    SimIn in = rng_operator(s, h, rec); /* context O (docs/rng-sites.md 3.2) */
    int hour = world_tail(w, TAIL_HOUR), played = world_tail(w, TAIL_HOURS), index = get16(rec + ICU_PATIENT);
    SimIn old = sim_set_inputs(s, in); /* the fittings' rare r1 (0x6F419) with the operator's pair */
    sim_fittings(s, h, 9, rec, rec + ICU_WANT); /* engine choice: before the staffing, which can end the call */
    sim_set_inputs(s, old);
    if (!get16(rec + ICU_STAFF)) {
        if (played < 5 || index || (hour >= 4 && hour <= 19 && reception_staffed(s, h)))
            ask_staff(s, h, 1, 9, r, rec + ICU_STAFF);
        return;
    }
    if (sim_bits_in(s, 7, 0x6F4C6, in) == 5 && !rec[ICU_COMFORT]) rec[ICU_COMFORT] = rng_comfort(s, in);
    if (hour >= 21 && !reception_staffed(s, h) && !index) {
        int who = get16(rec + ICU_STAFF);
        put16(rec + ICU_STAFF, 0);
        rec[ICU_COMFORT] = 0;
        sim_release(s, h, who);
        return;
    }
    equipment_needs(s, h, 9, rec, ICU_REQUEST, 1);
    if (!index) return;
    uint8_t *p = person(s, index);
    if (p[P_DEAD]) { /* 0x52332: 0x52404 clears +0x0C and +0x10 */
        died_found(s, h, index);
        put16(rec + ICU_PATIENT, 0);
        put16(rec + ICU_HOURS, 0);
        return;
    }
    if (p[P_STATE] != ST_ICU) return;
    rec[ICU_TREAT] = 0xFF;
    if ((int8_t)p[P_HEALTH] < 50) return;
    uint8_t *queue = reception(s, h) + 0x46; /* back to the Krankenzimmer queue; no free place: P stays */
    for (int i = 0; i < 6; i++)
        if (!get16(queue + 4 * i)) {
            put16(queue + 4 * i, index);
            put16(queue + 4 * i + 2, 0);
            put16(rec + ICU_PATIENT, 0);
            p[P_STATE] = ST_RECEPTION;
            add16(hospital(s, h) + 0x58, 1);
            return;
        }
}

/* ---- the OP (category 7) and the torture chamber (category 8), 216-byte records -------------------------- */

enum { TH_TABLE_BYTE = 3, TH_REQUEST = 4, TH_DOCTOR = 6, TH_NURSE = 8, TH_THIRD = 0x0A, TH_PATIENT = 0x0C,
       TH_STAGE = 0x0E, TH_OP = 0x10, TH_INDEX = 0x12, TH_COUNTER = 0x14, TH_COMFORT = 0x17, TH_ORGAN = 0x18,
       TH_QUALITY = 0x19, TH_TABLE = 0x76, TH_WANT = 0x7A };
#define ORGAN_BASE 0x4712
#define BLOOD_LOSS 0x2470

/* docs/sim-rooms2.md "The OP" and "Torture chamber": one code path, these differences */
typedef struct {
    uint8_t cat, state;
    uint16_t sequences, capabilities, durations; /* program image: 11 stage bytes, 10 u16 capabilities per op */
    uint16_t started; /* hospital counter of starts: +0x32 operations, +0x114 tortures */
    uint8_t player_recovery; /* field 102 at a start by the player */
    uint8_t ward_cat, step_code; /* where the patient comes from, and the treatment step code that sends it */
    uint8_t staff[3], groups[3]; /* staff fields in the order the operator fills them, 0 = none */
    uint8_t op; /* the OP's extras: cash eviction, team score, blood, organs, bleeding, comfort, auction test */
} TheatreKind;
static const TheatreKind THEATRES[2] = {
    {7, ST_OP, 0x208E, 0x248C, 0x69EB, 0x32, 120, 6, 11, {TH_NURSE, TH_THIRD, TH_DOCTOR}, {1, 5, 2}, 1},
    {8, ST_TORTURE, 0x242E, 0x26A8, 0x6A57, 0x114, 40, 16, 71, {TH_NURSE, TH_THIRD, 0}, {1, 10, 0}, 0},
};

static int stage_byte(Sim *s, const TheatreKind *tk, int op, int i) {
    if (i < 0 || i > 10) return 1; /* engine choice: an index outside the 11 bytes ends the operation */
    return image_byte(s, tk->sequences + 11u * (uint32_t)op + (uint32_t)i, 0);
}
static int op_capability(Sim *s, const TheatreKind *tk, int op, int i) {
    return (uint16_t)program_u16(s->prog, tk->capabilities + 20u * (uint32_t)op + 2u * (uint32_t)i);
}

/* the start's equipment: the first missing capability goes to +4 and stops; with all present each is used
 * (capability 30, the operating table, sets +3 and +0x76). Returns 1 when the operation started. */
static int theatre_equip(Sim *s, const TheatreKind *tk, uint8_t *rec, int op) {
    for (int i = 0; i < 10; i++) {
        int cap = op_capability(s, tk, op, i);
        rec[TH_REQUEST] = equipment_id(s, rec, tk->cat, cap, 0, 0) ? 0 : (uint8_t)cap;
        if (rec[TH_REQUEST]) return 0;
    }
    for (int i = 0; i < 10; i++) {
        int cap = op_capability(s, tk, op, i), id = equipment_id(s, rec, tk->cat, cap, 1, 0);
        if (cap == 30 && id) rec[TH_TABLE_BYTE] = sim_equipment_item(s, id)[5], put16(rec + TH_TABLE, id);
    }
    return 1;
}

static void theatre_begin(Sim *s, const TheatreKind *tk, int h, uint8_t *rec, uint8_t *p, int recovery) {
    add16(hospital(s, h) + tk->started, 1);
    put16(rec + TH_STAGE, 6);
    p[P_RECOVERY] = (uint8_t)recovery;
    p[P_FIELD100] = 1;
    put16(rec + TH_INDEX, 0);
    put16(rec + TH_COUNTER, 2);
}

static void theatre_clear(uint8_t *rec) { /* +0x0C .. +0x15 and +0x18/+0x19 */
    memset(rec + TH_PATIENT, 0, 10);
    put16(rec + TH_ORGAN, 0);
}

/* the OP's death check 0x58B59 (as 0x30766): below 1 in an AI hospital */
static int op_death_check(Sim *s, int h, uint8_t *rec, uint8_t *p) {
    if ((int8_t)p[P_HEALTH] >= 1 || hospital(s, h)[0x14]) return 0;
    p[P_DEAD] = 1;
    died_in_op(s, h, p);
    theatre_clear(rec);
    died(s, h, index_of(s, p));
    return 1;
}

static int organ_draws(Sim *s, const uint8_t *rec, const int *limits, int n, int last_mask) {
    if (!get16(rec + TH_ORGAN)) return 0;
    int d = 0, q = (int8_t)rec[TH_QUALITY];
    for (int i = 0; i < n && q < limits[i]; i++) d += (int)sim_bits(s, i == n - 1 && last_mask ? (uint32_t)last_mask : 3);
    return d;
}

/* OP wish effects (the routines of the table 0xAEA4 by operation id, docs/sim-rooms2.md); ids without an entry,
 * and ids above 26 (not read in the spec; engine choice), have none */
static void wish_effect(Sim *s, uint8_t *p, const uint8_t *rec, int op) {
    switch (op) {
    case 2: p[5] = (uint8_t)(p[5] + sim_bits(s, 1) * rec[TH_TABLE_BYTE] + rec[TH_TABLE_BYTE] + 1); return;
    case 3: p[5] = (uint8_t)(p[5] - (sim_bits(s, 1) + 2)); return;
    case 6: case 15: case 18: case 21: case 23: p[52] += 3, p[53] += 3, p[P_UNITS] += 3; /* then the effect of 13 */
    /* fall through */
    case 13: case 14: case 26:
        if ((int8_t)p[P_HEALTH] >= 20) p[P_HEALTH] = (uint8_t)(p[P_HEALTH] - sim_bits(s, 7)); /* written directly */
        if ((int16_t)get16(p + P_BLOOD) >= 400) put16(p + P_BLOOD, get16(p + P_BLOOD) - ((int)sim_bits(s, 31) + 8));
        return;
    default: return;
    }
}

/* 0x58045 (OP, tick 76) and 0x597F9 (torture chamber, every minute) */
static void theatre_tick(Sim *s, const TheatreKind *tk) {
    static const int OP_ORGAN[5] = {70, 50, 30, 20, 10}, RUN_ORGAN[4] = {70, 50, 30, 15};
    static const int RUN_BLOOD[7] = {680, 660, 640, 615, 580, 550, 520};
    for (int h = 0; h < 16; h++)
        for (int k = 0; k < sim_rooms_in_use(s, h, tk->cat); k++, sim_rating_advance(s, 216)) {
            if (!k) sim_rating_room(s, h, tk->cat, 0);
            uint8_t *rec = sim_room_record(s, h, tk->cat, k), *r = hospital(s, h);
            int index = get16(rec + TH_PATIENT);
            if (!index) { /* 0x2E666 for +8 (the OP clears its comfort +0x17 with it), +0x0A and, in the OP, +6 */
                sim_staff_break(s, h, tk->cat, k, rec + TH_NURSE, tk->op ? rec + TH_COMFORT : NULL);
                sim_staff_break(s, h, tk->cat, k, rec + TH_THIRD, NULL);
                if (tk->op) sim_staff_break(s, h, tk->cat, k, rec + TH_DOCTOR, NULL);
                continue;
            }
            uint8_t *p = person(s, index);
            care_drift(s, rec, index, 0);
            if (tk->op && cash_of(s, h) < 0) { /* back to the ward; the rest of the record stays */
                p[P_STATE] = ST_WARD, p[P_ROOM] = p[P_SAVED_ROOM], p[P_FIELD100] = 2, p[P_BLEEDING] = 1;
                put16(rec + TH_PATIENT, 0);
                continue;
            }
            add16(rec + TH_COUNTER, 1);
            if (p[P_DEAD]) {
                if (tk->op && !r[0x14]) {
                    died_in_op(s, h, p);
                    theatre_clear(rec);
                    died(s, h, index);
                }
                continue;
            }
            int op = get16(rec + TH_OP), stage = get16(rec + TH_STAGE);
            if (stage == 999) {
                if (op && r[0x14] && get16(rec + TH_INDEX) >= 0) { /* the player's start */
                    player_request(s, h, tk->cat, rec, rec + TH_REQUEST);
                    if (theatre_equip(s, tk, rec, op)) theatre_begin(s, tk, h, rec, p, tk->player_recovery);
                    continue;
                }
                if (sim_bits(s, 3) == 3) { /* waiting */
                    worsen(s, h, p, tk->op ? 0x3E : 0x9F);
                    worsen_in_treatment(s, h, p, tk->op ? 0x3E : 0x9F);
                    if (p[P_DEAD] && !r[0x14]) {
                        if (tk->op) died_in_op(s, h, p);
                        theatre_clear(rec);
                        died(s, h, index);
                    }
                }
                goto running;
            }
            if (stage) {
                put16(rec + TH_STAGE, --stage);
                if (tk->op) { /* 0x58AEF */
                    if (sim_bits(s, 31) == 3) work_done(s, get16(rec + TH_NURSE));
                    if (sim_bits(s, 15) == 7) work_done(s, get16(rec + TH_THIRD));
                    if (sim_bits(s, 31) == 2) work_done(s, get16(rec + TH_DOCTOR));
                    if (sim_bits(s, 15) == 7) p[54]++;
                    if (sim_bits(s, 127) == 12) p[54]++;
                    if (sim_bits(s, 127) == 5) p[55]++;
                    if (sim_bits(s, 127) == 13) p[48]++;
                    if (sim_bits(s, 127) == 7) p[53]++;
                } else { /* 0x59D7E */
                    if (sim_bits(s, 31) == 3) work_done(s, get16(rec + TH_NURSE));
                    if (sim_bits(s, 15) == 7) work_done(s, get16(rec + TH_THIRD));
                    if (sim_bits(s, 15) == 7) p[54]++;
                    if (sim_bits(s, 127) == 12) p[54]++;
                    if (sim_bits(s, 127) == 5) p[55]++;
                    if (sim_bits(s, 63) == 13) p[48]++;
                    if (sim_bits(s, 63) == 7) p[53]++;
                }
                if (sim_bits(s, 127) == 4) p[52]++;
                if (stage) goto running;
            }
            int i = get16(rec + TH_INDEX);
            if (stage_byte(s, tk, op, i) == 1) { /* finished */
                put16(rec + TH_STAGE, 999);
                put16(rec + TH_INDEX, -1);
                put16(rec + TH_COUNTER, 2);
                p[P_BLEEDING] = 0;
                work_done(s, get16(rec + TH_NURSE));
                for (int n = 0; n < 3; n++) work_done(s, get16(rec + TH_THIRD));
                if (tk->op) work_done(s, get16(rec + TH_DOCTOR));
                p[P_FIELD100] = 2;
                p[53] += 2, p[48] += 2;
                if (tk->op && get16(rec + TH_ORGAN)) {
                    /* the whole word (id + 256 x quality) indexes the price bytes: reproduce, read the image there */
                    p[48] = (uint8_t)(p[48] + image_byte(s, ORGAN_BASE, 2u * (uint16_t)get16(rec + TH_ORGAN)) / 2 + 1);
                    p[53]++;
                }
                notify_watched_patient(s, h, get16(rec + TH_PATIENT), tk->op ? 0x3E : 0x9F);
                notify_watched_patient(s, h, get16(rec + TH_PATIENT), tk->op ? 0x3F : 0x9F);
                if (!tk->op) continue;
                goto running;
            }
            if (tk->op) { /* the stage step: the team score is the success rule */
                const uint8_t *a = staff_record(s, get16(rec + TH_THIRD)), *d = staff_record(s, get16(rec + TH_DOCTOR)),
                              *n = staff_record(s, get16(rec + TH_NURSE));
                unsigned sc = (uint16_t)(get16(a + 16) + a[15] + 16 * a[12] + 8 * a[13] + 4 * a[2] + a[7] - a[5]) +
                              (uint16_t)(get16(d + 16) + d[15] + 8 * d[12] + 4 * d[13] + 2 * d[2] + d[7] - d[5]) +
                              (uint16_t)(get16(n + 16) + 8 * n[12] + 4 * n[13] + 2 * n[2]) + 4u * rec[1];
                int score = (int)((sc & 0xFFFF) >> 6);
                if ((int)sim_bits(s, 127) > score) { /* written directly: no history, no death check */
                    p[P_HEALTH] = (uint8_t)(p[P_HEALTH] - (sim_bits(s, 3) + 2));
                    if ((int8_t)p[P_HEALTH] < 4) p[P_HEALTH] = 1;
                }
                if ((int)sim_bits(s, 127) <= score) {
                    p[P_HEALTH] = (uint8_t)(p[P_HEALTH] + sim_bits(s, 3) + 1);
                    if ((int8_t)p[P_HEALTH] >= 80) p[P_HEALTH] = (uint8_t)(75 + sim_bits(s, 3));
                }
                if (p[P_WISH]) { /* a cosmetic operation: its own effect (0xAEA4), then the blood loss */
                    wish_effect(s, p, rec, op);
                    /* SIM-48: c = the op table read at index 8 b, from b = 4 past its 28 bytes (docs/sim-rooms2.md,
                     * "0x58045" step 6; reproduce: the intended value is not evident, docs/fixes.md "Not bugs") */
                    int b = image_byte(s, BLOOD_LOSS, (uint32_t)op);
                    int c = image_byte(s, BLOOD_LOSS, (uint32_t)(8 * b));
                    int blood = (int16_t)get16(p + P_BLOOD) - ((int)sim_random(s, (uint32_t)(8 * b)) + 2 * c);
                    put16(p + P_BLOOD, blood < 100 ? 100 : blood);
                    int dd = (int)sim_bits(s, 3); /* d = r3 first, then the organ's draws */
                    dd += organ_draws(s, rec, OP_ORGAN, 5, 7);
                    set_health(p, (uint8_t)(p[P_HEALTH] - dd));
                    op_death_check(s, h, rec, p);
                    post_cleanup_health_notification(s, h, rec + TH_PATIENT, p, 0x3E);
                    if (!p[P_HEALTH]) continue;
                }
            }
            /* Disposal may clear the room: reload its stage, but retain this patient's pointer and op sequence. */
            i = get16(rec + TH_INDEX) + 1;
            put16(rec + TH_INDEX, i);
            put16(rec + TH_STAGE, image_byte(s, tk->durations + (uint32_t)stage_byte(s, tk, op, i), 0));
            p[52]++, p[54] += 2, p[55]++;
            notify_watched_patient(s, h, get16(rec + TH_PATIENT), tk->op ? 0x3F : 0xA0);
            continue; /* the record ends after a stage step */
        running:
            if (!tk->op) continue;
            uint8_t *bank = bank_ready(s, h);
            if (p[P_FIELD100] == 1) {
                if (bank && get16(p + P_BLOOD) < 700) transfuse(s, h, bank, p, 700 - get16(p + P_BLOOD), 1, 0);
                if (sim_bits(s, 7) == 3) {
                    if (get16(p + P_BLOOD) >= 200) {
                        int l = (int)sim_bits(s, 3) + 4;
                        add16(p + P_BLOOD, -l);
                        if ((bank = bank_ready(s, h))) transfuse(s, h, bank, p, l, 0, 0);
                    }
                    int d = (int)sim_bits(s, 1) + 1, blood = get16(p + P_BLOOD);
                    for (int t = 0; t < 7; t++)
                        if (blood < RUN_BLOOD[t]) d += (int)sim_bits(s, 1);
                    d += organ_draws(s, rec, RUN_ORGAN, 4, 0);
                    set_health(p, (uint8_t)(p[P_HEALTH] - d));
                    op_death_check(s, h, rec, p);
                    post_cleanup_health_notification(s, h, rec + TH_PATIENT, p, 0x3E);
                    if (!p[P_HEALTH]) continue;
                }
            }
            if (p[P_BLEEDING]) { /* set only by the cash eviction, so not reached in practice */
                if (get16(p + P_BLOOD) >= 200) {
                    int l = ((int)sim_bits(s, 7) + 5) * p[P_BLEEDING];
                    add16(p + P_BLOOD, -l);
                    if ((bank = bank_ready(s, h))) transfuse(s, h, bank, p, l, 1, 0);
                }
                int d = (int)sim_bits(s, 3) + 1, blood = get16(p + P_BLOOD);
                if (blood < 680) d *= 2;
                d += (blood < 660) + (blood < 640) + (blood < 615);
                set_health(p, (uint8_t)(p[P_HEALTH] - d));
                op_death_check(s, h, rec, p);
                post_cleanup_health_notification(s, h, rec + TH_PATIENT, p, 0x3E);
            }
        }
}

/* 0x4FFA0: an organ of id k from the bank's first n slots (not compacted: reproduce); the slot as a word */
static int organ_from_bank(Sim *s, int h, int k) {
    if (!room_counts(s, h)[17]) return 0;
    uint8_t *bank = sim_room_record(s, h, 17, 0);
    int n = get16(bank + 0x14);
    for (int i = 0; i < n && i < 30; i++) {
        uint8_t *slot = bank + 0x58 + 2 * i;
        if (slot[0] != k) continue;
        int w = (uint16_t)get16(slot);
        put16(slot, 0);
        add16(bank + 0x14, -1);
        sim_bank_compact(s);
        return w;
    }
    return 0;
}

/* 0x4FDDA (OP) and 0x52749 (torture): the start, shared by the AI operator and the player's button
 * (docs/ui-departments.md, "Start / stop"). Returns THEATRE_STARTED, THEATRE_NO_ORGAN, or the first capability the
 * room lacks (also in +4). The wanted-organ byte +0x86 is written in AI mode only. */
static int theatre_start(Sim *s, const TheatreKind *tk, int h, uint8_t *rec, uint8_t *p, int ai) {
    const uint8_t *t = treatment(s, p[P_TREATMENT]);
    uint8_t *r = hospital(s, h);
    int op = t[1];
    if (tk->op) {
        if (t[3] && !get16(rec + TH_ORGAN)) {
            int w = organ_from_bank(s, h, t[3]);
            if (!w) {
                put16(rec + TH_ORGAN, 0);
                if (ai && !get16(r + 0x86)) put16(r + 0x86, t[3]); /* the organ the AI wants to buy */
                return THEATRE_NO_ORGAN;
            }
            if (w == (uint16_t)get16(r + 0x86)) put16(r + 0x86, 0); /* a whole-word compare (reproduce) */
            put16(rec + TH_ORGAN, w);
            add16(r + 0x84, 1);
        }
        if (op == 1 && p[P_WISH]) op = image_byte(s, WISHES, (uint32_t)p[P_WISH] * 4 + 1);
        if (op == 5) {
            const uint8_t *ill = illness(s, p[P_DIAGNOSIS]);
            if (ill[15] == 1) op = p[80] == 1 ? 6 : p[80] == 2 ? 7 : ill[1] == 1 ? 6 : ill[1] == 2 ? 7 : 5;
            else if (ill[15] == 2) op = p[80] == 2 || ill[1] == 2 ? 8 : 5;
        }
    }
    put16(rec + TH_OP, op); /* engine choice for the torture: the spec does not say that 0x52749 stores it */
    if (!theatre_equip(s, tk, rec, op)) return rec[TH_REQUEST];
    theatre_begin(s, tk, h, rec, p, 120);
    return THEATRE_STARTED;
}

/* The torture bring's work counter (SIM-49, docs/sim-rooms2.md, torture operator, "Bring"): the staff index is the
 * word W(n) at program offsets 8 + n / 9 + n, n = H's number of Neuro-Krankenzimmer; the bytes there are the
 * extender's heap words during play, measured in the RAM dumps (disk and German CD alike; the English CD differs at
 * offsets 8 and 12). W(n) below the staff table's 1,041 records raises that record's +0x10, record 0 included; a
 * larger W (and n above 14, not measured) writes memory no save holds: skipped (DEP-12, FIX-46). Measured table, by n:
 * the in-table values, -1 = outside the table. */
static void torture_wrong_work(Sim *s, int h) {
    static const int16_t W[2][15] = {
        {-1, -1, 254, 0, -1, -1, 254, -1, -1, 129, 0, -1, -1, 130, 0},  /* disk 1.4, German CD */
        {-1, -1, 254, -1, -1, -1, 254, -1, -1, 129, 0, -1, -1, 130, 0}, /* English CD: n = 3 0x9000, 4 0x6E90 */
    };
    int n = room_counts(s, h)[16], w = n < 15 ? W[program_english(s->prog)][n] : -1;
    if (w >= 0 && w < STAFF_COUNT) add16(staff_record(s, w) + 0x10, 1);
}

/* 0x4E6D1 / 0x4E978, the bring of the operator and of the player's fetch: over H's wards of tk->ward_cat in order and
 * their beds in order, the first P whose treatment has an operation (byte 1), whose step is due here, in the ward's
 * state, without a recovery countdown and alive. Returns P (0 = none); *ward = the ward it came from. */
static int theatre_bring(Sim *s, const TheatreKind *tk, int h, uint8_t *rec, int r, int *ward) {
    for (int k = 0; k < sim_rooms_in_use(s, h, tk->ward_cat); k++) {
        uint8_t *wd = sim_room_record(s, h, tk->ward_cat, k);
        for (int b = 0; b < 10; b++) {
            int i = get16(wd + 0x14 + 4 * b);
            if (!i) continue;
            uint8_t *p = person(s, i);
            const uint8_t *t = treatment(s, p[P_TREATMENT]);
            if (!t[1] || t[15 + p[P_STEPS]] != tk->step_code || p[P_STATE] != tk->ward_cat || p[P_RECOVERY] ||
                p[P_DEAD])
                continue;
            /* FIX-27 (docs/fixes.md): original (torture) credits staff W(n), the word at program offset 8 + n (n = H's
             * Neuro-Krankenzimmer, SIM-49); fixed credits the torture record's +8, as the OP's bring */
            if (tk->op || fix_on(27)) work_done(s, get16(rec + TH_NURSE));
            else torture_wrong_work(s, h);
            put16(rec + TH_PATIENT, i);
            put16(rec + TH_STAGE, 999);
            put32(rec + TH_OP, 0);
            put16(rec + TH_COUNTER, 2);
            put16(rec + TH_ORGAN, 0);
            p[P_SAVED_STATE] = p[P_STATE], p[P_SAVED_ROOM] = p[P_ROOM];
            p[P_STATE] = tk->state, p[P_ROOM] = (uint8_t)r; /* the person keeps its bed */
            if (ward) *ward = k;
            return i;
        }
    }
    return 0;
}

/* 0x6E8BE (OP) and 0x6E4C1 (torture): the operators */
static void theatre_operator(Sim *s, const TheatreKind *tk) {
    World *w = s->world;
    int h = sim_pick(s);
    if (!h || !room_counts(s, h)[tk->cat]) return;
    /* the OP (0x6E8BE) in context O (docs/rng-sites.md 3.2); the torture chamber's operator is not listed: defaults */
    int r = (int)sim_random_in(s, (uint32_t)sim_rooms_in_use(s, h, tk->cat), tk->op ? 0x6E904 : 0,
                               tk->op ? rng_operator(s, h, NULL) : SIM_DEFAULT_IN);
    uint8_t *rec = sim_room_record(s, h, tk->cat, r);
    SimIn in = tk->op ? rng_operator(s, h, rec) : SIM_DEFAULT_IN;
    int hour = world_tail(w, TAIL_HOUR), played = world_tail(w, TAIL_HOURS);
    SimIn old = sim_set_inputs(s, in); /* the fittings' rare r1 (0x6E98B) with the operator's pair */
    sim_fittings(s, h, tk->cat, rec, rec + TH_WANT); /* engine choice: before the staffing, which can end the call */
    sim_set_inputs(s, old);
    for (int f = 0; f < 3 && tk->staff[f]; f++) {
        if (get16(rec + tk->staff[f])) continue;
        if (played < 5 || (hour >= 4 && hour <= 19)) {
            if (played < 5 || reception_staffed(s, h)) {
                uint8_t *st = s->world->block[BLK_STAFF], keep = 0;
                int who = 0, slot;
                if (tk->staff[f] == TH_DOCTOR && (who = sim_assignable(s, h, tk->groups[f], &slot))) keep = st[who * 32 + 26];
                if (tk->staff[f] != TH_DOCTOR) ask_staff(s, h, tk->groups[f], tk->cat, r, rec + tk->staff[f]);
                else if (who && fix_on(26)) {
                    /* FIX-26 (docs/fixes.md): original writes the room index into the OP record's +0x1A and keeps
                     * staff byte 26; fixed writes it to staff byte 26, as every other assignment */
                    sim_assign(s, h, who, slot, tk->cat, r, rec + TH_DOCTOR);
                    st[who * 32 + 26] = (uint8_t)r;
                } else if (who) { /* the room index goes to the room record's +0x1A, not to staff byte 26 (reproduce) */
                    sim_assign(s, h, who, slot, tk->cat, r, rec + TH_DOCTOR);
                    st[who * 32 + 26] = keep;
                    rec[0x1A] = (uint8_t)r;
                }
            }
            return;
        }
        goto release;
    }
    if (tk->op && sim_bits_in(s, 7, 0x6EB5F, in) == 3 && !rec[TH_COMFORT]) rec[TH_COMFORT] = rng_comfort(s, in);
    equipment_needs(s, h, tk->cat, rec, TH_REQUEST, 1);
    int index = get16(rec + TH_PATIENT);
    if (!index) { /* 0x4E6D1 / 0x4E978: bring the first patient whose treatment step is due here */
        if (theatre_bring(s, tk, h, rec, r, NULL)) return;
        goto release;
    }
    uint8_t *p = person(s, index);
    if (p[P_DEAD]) { /* 0x5A645 / 0x528A6: the OP's dead also leave their Krankenzimmer bed, the tortured do not */
        died_found(s, h, index);
        if (tk->op) leave_ward_bed(s, h, index);
        put32(rec + TH_PATIENT, 0);
        put32(rec + TH_OP, 0);
        put16(rec + TH_COUNTER, 0);
        put16(rec + TH_ORGAN, 0);
        return;
    }
    if (get16(rec + TH_STAGE) == 999 && p[P_FIELD100] != 2) {
        theatre_start(s, tk, h, rec, p, 1);
    } else if ((int8_t)p[P_FIELD100] >= 2) { /* 0x4FBC4 / 0x52546: back to the bed it kept */
        p[P_STATE] = p[P_SAVED_STATE], p[P_ROOM] = p[P_SAVED_ROOM];
        if (tk->op && get16(rec + TH_ORGAN)) add16(hospital(s, h) + 0x8A, 1);
        memset(rec + TH_PATIENT, 0, 14);
    }
    return;
release:
    if (get16(rec + TH_PATIENT) || (tk->op && get16(w->block[BLK_34])) || hour < 20 || reception_staffed(s, h)) return;
    for (int f = 0; f < 3 && tk->staff[f]; f++) {
        int who = get16(rec + tk->staff[f]);
        if (!who) continue;
        put16(rec + tk->staff[f], 0);
        sim_release(s, h, who);
    }
}

/* 0x58BA3 (OP +0x17) and 0x59DCA (intensive care +0x0F): the comfort counters run down once per minute, to 1 in a
 * human-controlled hospital and to 0 otherwise */
static void comfort_countdown(Sim *s, int cat, int off) {
    for (int h = 0; h < 16; h++)
        for (int k = 0; k < sim_rooms_in_use(s, h, cat); k++) {
            uint8_t *c = sim_room_record(s, h, cat, k) + off;
            if (*c > (hospital(s, h)[0x14] ? 1 : 0)) (*c)--;
        }
}

/* ---- the blood and organ bank (category 17, one 178-byte record; docs/sim-rooms2.md, "Blood and organ bank") -- */

enum { BK_NURSE = 8, BK_KEEPER = 0x0A, BK_STOCK = 0x0C, BK_CAPACITY = 0x10, BK_HELD = 0x14, BK_PLACES = 0x16,
       BK_SLOTS = 0x58, BK_SLOT_COUNT = 30 };
#define HARVEST_ROWS 0xA749 /* 16 entry numbers per table choice (pathology +0x14) */

static uint8_t *bank_of(Sim *s, int h) { return room_counts(s, h)[17] ? sim_room_record(s, h, 17, 0) : NULL; }
uint8_t *sim_bank(Sim *s, int h) { return bank_of(s, h); }

/* 0x4BF9B: in every bank holding organs, an empty slot among positions 0-28 pulls the later slots down by one
 * (slot 29 = 0) and the next position is checked, so a gap of two empty slots leaves one behind (reproduced) */
void sim_bank_compact(Sim *s) {
    for (int h = 0; h < 16; h++) {
        uint8_t *b = bank_of(s, h);
        if (!b || !get16(b + BK_HELD)) continue;
        for (int i = 0; i < BK_SLOT_COUNT - 1; i++) {
            uint8_t *slot = b + BK_SLOTS + 2 * i;
            if (get16(slot)) continue;
            memmove(slot, slot + 2, (size_t)(2 * (BK_SLOT_COUNT - 1 - i)));
            put16(b + BK_SLOTS + 2 * (BK_SLOT_COUNT - 1), 0);
            if (fix_on(22)) {
                /* FIX-22 (docs/fixes.md): original checks the next position after a shift (one of two adjacent
                 * gaps stays); fixed checks the same position again until every later slot is empty */
                int later = 0;
                for (int j = i; j < BK_SLOT_COUNT; j++) later |= get16(b + BK_SLOTS + 2 * j) != 0;
                if (!later) break;
                i--;
            }
        }
    }
}

/* 0x6F9F2: organ k of the dead person P into slot "organs held" of H's bank while places - held is not 0 (the
 * original's test: not "positive"); quality 101 - field 104, 8-bit. Otherwise the organ is lost. A slot number
 * outside the 30 slots (held above the places) loses it too: engine choice, the original writes past the slots. */
int sim_organ_quality(int dead) {
    /* FIX-23 (docs/fixes.md): original q = 101 - field 104 as a byte (wraps above 101: 105 -> 252); fixed 0 there */
    if (fix_on(23) && dead > 101) return 0;
    return (uint8_t)(101 - dead);
}

static void organ_into_bank(Sim *s, int h, const uint8_t *p, int k) {
    uint8_t *b = bank_of(s, h);
    if (!b) return;
    int held = get16(b + BK_HELD);
    if ((int16_t)(get16(b + BK_PLACES) - held) == 0 || held < 0 || held >= BK_SLOT_COUNT) return;
    b[BK_SLOTS + 2 * held] = (uint8_t)k;
    b[BK_SLOTS + 2 * held + 1] = (uint8_t)sim_organ_quality(p[P_DEAD]);
    put16(b + BK_HELD, held + 1);
}

/* The entries of 0xE60F that take an organ (docs/sim-rooms2.md, "Which organs the pathology takes"): the organ,
 * how often, and the illnesses (field 56, 0-terminated) that keep it; entry 13 also not with diagnosis 5 */
static const struct {
    uint8_t organ, times, not_with[6];
} HARVEST[23] = {
    [3] = {8, 1, {24, 42}},   [4] = {1, 1, {23, 42}},     [5] = {6, 1, {42, 23}},           [6] = {11, 2, {42}},
    [8] = {4, 1, {42, 49}},   [9] = {9, 1, {42, 69, 70}}, [13] = {5, 1, {1, 5, 47, 60, 15}},
};

/* the pathology's organ harvest at countdown 2 of stage +0x12 with the table row of +0x14 */
static void harvest(Sim *s, int h, const uint8_t *rec, const uint8_t *p) {
    int row = get16(rec + 0x14), stage = get16(rec + 0x12);
    if (row < 0 || row > 1 || stage < 0 || stage > 15) return; /* engine choice: only the two rows the spec gives */
    int e = image_byte(s, HARVEST_ROWS, 16u * (uint32_t)row + (uint32_t)stage);
    if (e >= 23 || !HARVEST[e].organ) return;
    for (int i = 0; i < 6 && HARVEST[e].not_with[i]; i++)
        if (p[P_ILLNESS] == HARVEST[e].not_with[i]) return;
    if (e == 13 && p[P_DIAGNOSIS] == 5) return;
    for (int n = 0; n < HARVEST[e].times; n++) organ_into_bank(s, h, p, HARVEST[e].organ);
}

/* 0x535DC: the bank operator asks for a nurse (group 1) into +8, once that is filled for group 7 into +0x0A, at any
 * hour and with the pick's cash not negative; it never releases them and has no fittings step */
static void bank_operator(Sim *s) {
    int h = sim_pick(s);
    uint8_t *b = h ? bank_of(s, h) : NULL;
    if (!b || cash_of(s, h) < 0) return;
    sim_rating_record(s, b); /* before either staff search, regardless of store/furniture requests */
    if (!get16(b + BK_NURSE)) ask_staff(s, h, 1, 17, 0, b + BK_NURSE);
    else if (!get16(b + BK_KEEPER)) ask_staff(s, h, 7, 17, 0, b + BK_KEEPER);
}

/* 0x53C79: the bank's fittings (blood containers 0x3D, cooling 0x33, capability 0x3C), the capacity top-up and the
 * staff mood, every hospital with a bank. A full equipment list makes an AI hospital sell an item first (0x2A499,
 * in sim_store_supply); for hospital 0 it ends the equipment step. A human-controlled hospital needs a usable store
 * (0x2C06B, with the screen-65 test); an AI hospital only a store room. */
static void bank_minute(Sim *s) {
    static const int CAPS[3] = {0x3D, 0x33, 0x3C};
    for (int h = 0; h < 16; h++) {
        uint8_t *b = bank_of(s, h), *st = store_of(s, h);
        if (!b) continue;
        int usable = st && (!hospital(s, h)[0x14] || store_usable(s, h));
        for (int i = 0; usable && i <= 3; i++) {
            if (i == 3) { /* capacity: below 5,000, or less than 500 free */
                int32_t cap = get32(b + BK_CAPACITY);
                if (cap >= 5000 && cap - get32(b + BK_STOCK) >= 500) break;
            } else if (equipment_id(s, b, 17, CAPS[i], 0, 0)) {
                continue;
            }
            if (sim_store_supply(s, h, 17, b, i == 3 ? 0x3D : CAPS[i], 0) < 0) break;
        }
        for (int f = BK_KEEPER; f >= BK_NURSE; f -= 2) { /* the mood only rises here */
            int who = get16(b + f);
            if (!who) continue;
            uint8_t *m = staff_record(s, who) + 21;
            if (*m == 100) continue;
            if (sim_bits(s, 3) == 1) (*m)++;
            if ((int8_t)*m < 4) {
                put16(b + f, 0);
                sim_leave_room(s, h, who); /* 0x2301C */
            }
        }
    }
}

/* 0x57E03: organ ageing. S = the skill of the group-7 person when a cooling unit (0x33) is there; without one a
 * 1-in-4 draw ages the organs at once. Otherwise r15 = 0 and r127 >= S age them; each organ then loses a point
 * when its own r127 >= S. Quality stops at 0 and the organ stays. The cooling test reads the bank record of the
 * "current hospital", which is 0 in the clock, so every bank asks whether hospital 0's (the player's) record lists
 * a cooling unit, built or not (docs/sim-rooms2.md, "0x57E03 - organ ageing", step 1; SIM-33). */
static void organ_ageing(Sim *s) {
    for (int h = 0; h < 16; h++) {
        uint8_t *b = bank_of(s, h);
        if (!b) continue;
        int skill = 0, keeper = get16(b + BK_KEEPER), now;
        /* FIX-48 (docs/fixes.md): original tests hospital 0's bank record; fixed tests the bank's own */
        uint8_t *cooled = fix_on(48) ? b : sim_room_record(s, 0, 17, 0);
        if (equipment_id(s, cooled, 17, 0x33, 0, 0)) {
            if (keeper) {
                sim_rating_record(s, b);
                skill = doctor_skill(s, b, keeper);
            }
            now = 0;
        } else {
            now = sim_bits(s, 3) == 0;
        }
        if (!now) {
            if (sim_bits(s, 15)) continue;
            if ((int)sim_bits(s, 127) < skill) {
                if (skill && sim_bits(s, 7) == 0) work_done(s, keeper);
                continue;
            }
        }
        for (int i = 0; i < BK_SLOT_COUNT; i++) {
            uint8_t *slot = b + BK_SLOTS + 2 * i;
            if (!slot[0]) continue;
            if ((int)sim_bits(s, 127) >= skill && slot[1]) slot[1]--;
        }
    }
}

/* ---- the pathology (category 18, one 158-byte record) ---------------------------------------------------- */

enum { PA_LISTED = 6, PA_PLACES = 8, PA_STAFF = 0x0A, PA_CASE = 0x0C, PA_COUNT = 0x0E, PA_MINUTES = 0x10,
       PA_STAGE = 0x12, PA_TABLE = 0x14, PA_LIST = 0x58 };

static int listed_ok(Sim *s, int h, int index) {
    const uint8_t *p = person(s, index);
    return p[P_STATE] == ST_PATHOLOGY && p[P_HOSPITAL] == h;
}

/* the verdict in H's counters (docs/sim-rooms2.md, 0x6F6B2 step 6) */
static void verdict(Sim *s, uint8_t *r, const uint8_t *p) {
    if (p[P_WISH]) {
        add16(r + 0x3C, 1), add16(r + 0x26, 1), add16(r + 0x2A, 1), add16(r + 0x36, 1);
        return;
    }
    if (!p[P_DIAGNOSIS]) {
        add16(r + 0x3A, 1);
        return;
    }
    if (!p[P_ILLNESS]) return; /* the original loops for ever here; engine choice: no verdict */
    if (p[P_DIAGNOSIS] == p[P_ILLNESS] && !p[P_TREATMENT]) {
        add16(r + 0x3A, 1);
        return;
    }
    /* with field 42 not equal to 56 the original tests a stale illness record; engine choice: field 56's */
    const uint8_t *ill = illness(s, p[P_ILLNESS]);
    int right = p[P_DIAGNOSIS] == p[P_ILLNESS] && p[P_TREATMENT] == ill[11] && (int16_t)get16(p + P_BLOOD) >= 680 &&
                !(p[P_ROOM] == ST_WARD && !p[P_IN_STEP]);
    add16(r + (right ? 0x3A : 0x3C), 1);
    add16(r + 0x2A, 1);
    if (ill[8] == 1) add16(r + 0xEC, 1);
    else if (ill[8] >= 2) add16(r + 0xEE, 1);
    if (p[P_FIELD100]) {
        if (ill[8]) add16(r + 0x118, 1), add16(r + 0x12C, 1);
        else add16(r + 0x36, 1), add16(r + 0x12A, 1);
    }
    if (p[P_ROOM] == ST_WARD || p[P_ROOM] == ST_OP || p[P_ROOM] == ST_ICU) add16(r + 0x26, 1);
}

/* 0x6F6B2: the pathology per minute. Where the original enters an endless loop (a list word or the case not a
 * dead person of H in state 18) the engine skips the hospital for this minute (engine choice). The English CD has no
 * such traps and goes on with the case (docs/editions.md 7.3). */
static void pathology_minute(Sim *s) {
    int traps = !program_english(s->prog);
    for (int h = 0; h < 16; h++) {
        if (!room_counts(s, h)[18]) continue;
        uint8_t *rec = sim_room_record(s, h, 18, 0);
        int ok = 1;
        for (int i = 0; i < 20; i++)
            if (get16(rec + PA_LIST + 2 * i) && !listed_ok(s, h, get16(rec + PA_LIST + 2 * i))) ok = 0;
        int index = get16(rec + PA_CASE);
        if ((!ok && traps) || !get16(rec + PA_STAFF) || !index) continue;
        add16(rec + PA_MINUTES, 1);
        if (get16(rec + PA_COUNT) == 999) continue;
        uint8_t *p = person(s, index);
        if (traps && (!listed_ok(s, h, index) || !p[P_DEAD])) continue;
        int c = get16(rec + PA_COUNT);
        if (c) {
            put16(rec + PA_COUNT, --c);
            if (c == 2) harvest(s, h, rec, p); /* the stage's entry of 0xE60F */
            if (c) continue;
        }
        work_done(s, get16(rec + PA_STAFF));
        add16(staff_record(s, get16(rec + PA_STAFF)) + 0x10, (int)sim_bits(s, 1));
        add16(rec + PA_STAGE, 1);
        if (get16(rec + PA_STAGE) < 16) {
            put16(rec + PA_COUNT, 12);
            notify_watched_patient(s, h, index, 0x45);
            continue;
        }
        put16(rec + PA_COUNT, 999);
        verdict(s, hospital(s, h), p);
        notify_watched_patient(s, h, index, 0x45);
    }
}

/* 0x50FF2: the next listed case onto the table, for the operator and the player's fetch (docs/ui-departments.md) */
static int pathology_take(Sim *s, uint8_t *rec) {
    int index = get16(rec + PA_LIST);
    put16(rec + PA_CASE, index);
    memmove(rec + PA_LIST, rec + PA_LIST + 2, 38);
    put16(rec + PA_LIST + 38, 0); /* the original keeps word 19, listing a full list's last entry twice, which
                                   * then fails the list check and hangs 0x6F6B2: engine choice, cleared */
    add16(rec + PA_LISTED, -1);
    put16(rec + PA_COUNT, 5);
    put16(rec + PA_MINUTES, 2);
    put16(rec + PA_STAGE, 0);
    put16(rec + PA_TABLE, (int)sim_bits(s, 1));
    return index;
}

/* 0x5A268: the pathology operator */
static void pathology_operator(Sim *s) {
    World *w = s->world;
    int h = sim_pick(s);
    if (!h || !room_counts(s, h)[18]) return;
    uint8_t *rec = sim_room_record(s, h, 18, 0);
    int hour = world_tail(w, TAIL_HOUR), played = world_tail(w, TAIL_HOURS);
    if (!get16(rec + PA_STAFF)) {
        if (played < 5 || get16(rec + PA_CASE) || (hour >= 5 && hour <= 19 && reception_staffed(s, h)))
            ask_staff(s, h, 7, 18, 0, rec + PA_STAFF);
        return;
    }
    if (hour >= 20 && !reception_staffed(s, h) && !get16(rec + PA_CASE) && !get16(rec + PA_LISTED)) {
        int who = get16(rec + PA_STAFF);
        put16(rec + PA_STAFF, 0);
        sim_release(s, h, who);
        return;
    }
    int index = get16(rec + PA_CASE);
    if (index && get16(rec + PA_COUNT) == 999) { /* 0x52457: the examined person leaves the hospital */
        put16(rec + PA_CASE, 0);
        sim_rename_dead(s, index); /* 0x52471 */
        sim_reset_healthy(person(s, index));
        index = 0;
    }
    if (!index && get16(rec + PA_LISTED)) pathology_take(s, rec);
}

/* ---- 0x67611: the reception minute (docs/mainloop.md, module H) ------------------------------------- */

/* SIM-100: the ward overview protects both queues in every hospital. Player 0's runtime view can differ from
 * its saved screen word; player 1 has only the saved word. The reception-head view uses a different selector. */
static int reception_overview(Sim *s) {
    return s->screen == 109 || get16(s->world->block[BLK_PLAYER1] + 4) == 109;
}

static void reception_minute(Sim *s) {
    sim_rating_room(s, 0, 2, 0);
    for (int h = 0; h < 16; h++, sim_rating_advance(s, 174)) {
        uint8_t *r = hospital(s, h);
        /* step 1 (SIM-31): the r15 index first, then the r127 value (in one C expression the order is open; clang
         * drew the value first) */
        uint32_t at = 0x17A + sim_bits(s, 15);
        r[at] = (uint8_t)(sim_bits(s, 127) + 90);
        if (!room_counts(s, h)[2]) continue;
        uint8_t *rec = reception(s, h);
        if (get16(rec + 8)) sim_staff_break(s, h, 2, 0, rec + 8, rec + 0x45);
        uint8_t *kz = rec + 0x46;
        for (int i = 0; i < 6; i++) { /* the Krankenzimmer queue */
            uint8_t *place = kz + 4 * i;
            int index = get16(place);
            if (!index) continue;
            add16(place + 2, 1);
            if (get16(place + 2) < 60 || reception_overview(s)) continue;
            sim_news_gave_up(s, h, index, 2); /* 0x493F3 */
            memset(place, 0, 4);
            uint8_t *p = person(s, index);
            p[P_OPINION + h] = (uint8_t)(p[P_OPINION + h] >= 12 ? p[P_OPINION + h] - 12 : 0);
            p[P_HOSPITAL] = p[P_STATE] = 0xFF;
            put16(p + P_DIAGNOSIS, 0);
            p[P_STEPS] = 0;
            memset(p + P_UNITS, 0, 8);
            p[P_COUNTDOWN] = p[P_ROOM] = p[P_IN_TREATMENT] = p[P_TREATED_BY] = 0;
            p[82] = 0xFF, p[P_IN_STEP] = 0;
            memset(p + 96, 0, 8);
            for (int k = 0; k < 11; k++) set_health(p, 70 + (int)sim_bits(s, 15));
        }
        uint8_t *queue = rec + 10;
        int first = get16(queue);
        if (!(r[0x14] && (h ? get16(s->world->block[BLK_PLAYER1] + 4) : s->screen) == 57) && first &&
            get16(queue + 2) >= 200 && !reception_overview(s) &&
            !person(s, first)[P_DEAD]) { /* leaves the queue */
            uint8_t *p = person(s, first);
            sim_news_gave_up(s, h, first, 1); /* 0x493D0 */
            p[P_HOSPITAL] = p[P_STATE] = 0xFF;
            queue_up(queue, 4);
            p[P_MOOD] = 40;
            p[P_OPINION + h] = (uint8_t)(p[P_OPINION + h] >= 12 ? p[P_OPINION + h] - 12 : 0);
        }
        int q = rec[0], staffed = get16(rec + 8) != 0, timer = rec[0x45];
        const uint8_t *st = staff_record(s, get16(rec + 8));
        for (int i = 0; i < 4; i++) {
            uint8_t *place = queue + 4 * i;
            int index = get16(place);
            if (!index) continue;
            uint8_t *p = person(s, index);
            add16(place + 2, 1);
            int wait = (uint16_t)get16(place + 2);
            drift(s, p, h, rec);
            if (p[P_DEAD]) continue;
            int8_t m = (int8_t)p[P_MOOD];
            if (wait >= 90 && wait <= 99 && m) {
                int x = (int)sim_random(s, (uint32_t)(102 - p[P_HEALTH])) / 8; /* the random first, then r1 */
                x += (int)sim_bits(s, 1);
                if (!sim_bits(s, 1)) x /= 4;
                x += 1;
                m = (int8_t)(m - x < 0 ? 0 : m - x);
                p[P_MOOD] = (uint8_t)m;
                if (h == 0) notify_watched_patient(s, h, index, 0x49);
            }
            int o = p[P_OPINION + h];
            if (o && m < 30 && !timer) {
                uint8_t c = (uint8_t)(30 + q + (staffed ? st[5] - 40 : 0));
                if ((int)sim_bits(s, 127) >= c) {
                    o--;
                    p[P_OPINION + h] = (uint8_t)o;
                    if (h == 0) notify_watched_patient(s, h, index, 0x49);
                }
            }
            if (o < 100 && m >= 51) {
                uint8_t c = (uint8_t)(10 + q + (staffed ? st[5] - 50 + (st[5] < 100 ? 20 : 0) : 0));
                if ((int)sim_bits(s, 127) < c && !sim_bits(s, 3)) {
                    if (sim_bits(s, 3)) m++;
                    else o++;
                    p[P_MOOD] = (uint8_t)m;
                    p[P_OPINION + h] = (uint8_t)o;
                    if (h == 0) notify_watched_patient(s, h, index, 0x49);
                }
            }
            p[P_OPINION + h] = (uint8_t)o;
            if (timer && staffed) {
                uint8_t c = (uint8_t)(10 + st[5] - 80);
                if (c >= sim_bits(s, 31) && !sim_bits(s, 31)) {
                    m++;
                    p[P_MOOD] = (uint8_t)m;
                    if (h == 0) notify_watched_patient(s, h, index, 0x49);
                }
            }
            p[P_MOOD] = (uint8_t)m;
            if (p[P_HEALTH] < 90 && wait >= 40 && sim_bits(s, 3) == 3) worsen(s, h, p, 0x2F);
            if ((int8_t)p[P_MOOD] > 100) p[P_MOOD] = 100;
            if (timer && sim_bits(s, 3)) {
                /* the r1 for everyone, then for a man an r3 that replaces it (docs/mainloop.md step 6; SIM-40) */
                int b = (int)sim_bits(s, 1);
                if (p[P_SEX] == 0) b = (int)sim_bits(s, 3);
                if (sim_bits(s, 15) == 5) add16(staff_record(s, get16(rec + 8)) + 16, 1);
                p[P_MOOD] = (uint8_t)(p[P_MOOD] + b);
                if (h == 0) notify_watched_patient(s, h, index, 0x49);
            }
        }
        if (rec[0x45] > (r[0x14] ? 1 : 0)) rec[0x45]--;
    }
}

/* ---- the kitchen (category 13, one 218-byte record; docs/sim-rooms2.md, "The bed wards' request byte +6 and the
 * kitchen's meals") -------------------------------------------------------------------------------------------- */

enum { KI_CAP = 2, KI_KIND = 3, KI_WANT = 4, KI_COOKING = 5, KI_COOK = 6, KI_RECIPE = 8, KI_MEAL = 0x0A, KI_LEFT = 0x0C,
       KI_LEVEL = 0x0E };
#define RECIPES 0xAD90 /* 8 bytes, entries 0-32: level, minutes, u16 meal, utensils ended by FF */
#define UTENSIL_CAPS 0xAE9A /* the capability each utensil number needs (0 = none) */
#define RECIPE_COUNT 33

static uint8_t *kitchen_of(Sim *s, int h) { return sim_room_record(s, h, 13, 0); }
static int has_cap(Sim *s, uint8_t *rec, int cat, int cap) { return equipment_id(s, rec, cat, cap, 0, 0) != 0; }
static int has_kind(Sim *s, uint8_t *rec, int cat, int kind) { return equipment_id(s, rec, cat, kind, 0, 1) != 0; }

/* 0x4DF60: the cook's level, (group x 64 + bust + skill + work counter / 2) / 128 in 16 bits, at most 8 */
static int cook_level(Sim *s, int index) {
    const uint8_t *st = staff_record(s, index);
    unsigned v = (unsigned)(st[1] * 64 + st[5] + st[15] + (uint16_t)get16(st + 16) / 2) & 0xFFFF;
    return v / 128 > 8 ? 8 : (int)(v / 128);
}
int sim_cook_level(Sim *s, int index) { return cook_level(s, index); }
static int store_free(const uint8_t *st) { return (int8_t)(uint8_t)(st[7] - st[8] - st[9]); }
static int bed_patients(Sim *s, int h) { /* 0x4E696 + 0x4EB5B: +12 over the Krankenzimmer and Neuro-Krankenzimmer */
    int n = 0;
    for (int cat = 6; cat <= 16; cat += 10)
        for (int k = 0; k < sim_rooms_in_use(s, h, cat); k++) n += sim_room_record(s, h, cat, k)[12];
    return n;
}
static void kitchen_stop(uint8_t *k) { memset(k + KI_MEAL, 0, 4); } /* K +0x0A .. +0x0D */

/* 0x6E161: the kitchen operator (AI) */
static void kitchen_operator(Sim *s) {
    World *w = s->world;
    int h = sim_pick(s);
    if (!h || !room_counts(s, h)[13]) return;
    uint8_t *k = kitchen_of(s, h), *st = store_of(s, h);
    sim_rating_record(s, k); /* the kitchen operator sets its room before fittings/staff work */
    int hour = world_tail(w, TAIL_HOUR), played = world_tail(w, TAIL_HOURS);
    /* 1. fittings, only for a set wish (engine choice: the shared rule, which asks h+0xDC when the store lacks it) */
    if (st && k[KI_WANT]) sim_fittings(s, h, 13, k, k + KI_WANT);
    /* 2. no cook: ask for group 4; the call ends either way */
    if (!get16(k + KI_COOK)) {
        if ((played < 5 || (hour >= 4 && hour <= 19)) && cash_of(s, h) >= 0) ask_staff(s, h, 4, 13, 0, k + KI_COOK);
        return;
    }
    /* 3. supplies: the missing kind, then the missing capability (engine choice: a request byte of 0 is skipped) */
    if (k[KI_KIND]) {
        if (has_kind(s, k, 13, k[KI_KIND])) k[KI_KIND] = 0;
        else if (!st) return;
        else if (sim_store_supply(s, h, 13, k, k[KI_KIND], 1) > 0) k[KI_KIND] = 0;
    }
    if (k[KI_CAP]) {
        if (has_cap(s, k, 13, k[KI_CAP])) k[KI_CAP] = 0;
        else if (sim_store_supply(s, h, 13, k, k[KI_CAP], 0) > 0) k[KI_CAP] = 0;
    }
    /* 4. from hour 20 the cook goes */
    if (hour >= 20) {
        int who = get16(k + KI_COOK);
        put16(k + KI_COOK, 0);
        kitchen_stop(k);
        sim_release(s, h, who);
        return;
    }
    const uint8_t *sr = sim_room_record(s, h, 4, 0); /* the store's record is read whether or not one is built */
    int open = hour >= 4 && hour <= 19 && store_free(sr) >= 5 && bed_patients(s, h) > 0;
    if (!k[KI_COOKING]) { /* 5. idle: start, with a recipe of the cook's level */
        if (!open) return;
        k[KI_COOKING] = 0xFF;
        kitchen_stop(k);
        k[KI_RECIPE] = 0xFF;
        int level = cook_level(s, get16(k + KI_COOK));
        put16(k + KI_LEVEL, level > 3 ? level : 3);
        for (int draw = 0; draw < 120; draw++) {
            k[KI_RECIPE] = (uint8_t)(sim_random(s, 32) + 1);
            if (image_byte(s, RECIPES, 8u * k[KI_RECIPE]) == get16(k + KI_LEVEL)) break;
        }
    } else if (!open) { /* 6. cooking: stop */
        k[KI_COOKING] = 0;
        kitchen_stop(k);
    }
}

/* The player-only request of a kind or a wanted furniture kind (step 8 of 0x6EDA1), as player_request */
static void player_kind_request(Sim *s, int h, int cat, uint8_t *rec, uint8_t *req) {
    uint8_t *st = store_of(s, h);
    if (!hospital(s, h)[0x14] || !*req || !st || !get16(st + 4) || !staff_record(s, get16(st + 4))[11]) return;
    if (has_kind(s, rec, cat, *req) || sim_store_supply(s, h, cat, rec, *req, 1) > 0) *req = 0;
}
static void player_furniture_request(Sim *s, int h, int cat, uint8_t *rec, uint8_t *want) {
    uint8_t *st = store_of(s, h);
    if (!hospital(s, h)[0x14] || !*want || !st || !get16(st + 4) || !staff_record(s, get16(st + 4))[11]) return;
    Description d = sim_description(s, cat);
    for (int f = 0; f < d.furniture_slots; f++) { /* 0x2C0BA: the kitchen has it */
        int id = (uint16_t)get16(rec + d.furniture + 2 * f);
        if (id && sim_furniture_item(s, id)[6] == *want) {
            *want = 0;
            return;
        }
    }
    sim_fittings(s, h, cat, rec, want); /* 0x2A59A / 0x2A718 / 0x2B003; engine choice: the shared rule */
}

/* 0x6EDA1: the kitchens per minute: cooking, and the cooked meal into the store room */
static void kitchen_minute(Sim *s) {
    for (int h = 0; h < 16; h++) {
        if (!room_counts(s, h)[13]) continue;
        uint8_t *k = kitchen_of(s, h);
        /* 1. the cook */
        int cook = get16(k + KI_COOK);
        if (!cook) continue;
        if (cook < 0 || staff_record(s, cook)[1] != 4) put16(k + KI_COOK, 0);
        cook = get16(k + KI_COOK);
        /* 2. idle */
        if (!k[KI_COOKING]) continue;
        /* 3. the time runs down; at 0 the meal goes into the store */
        if (get16(k + KI_LEFT)) {
            put16(k + KI_LEFT, (uint16_t)get16(k + KI_LEFT) - 1);
            if (get16(k + KI_LEFT)) continue;
            uint8_t *st = store_of(s, h);
            if (!get16(k + KI_MEAL) || !st || !get16(st + 4) || store_free(st) <= 0) continue;
            st[9]++;
            int slot = 0;
            while (slot < 120 && get16(st + 0xFA + 2 * slot)) slot++;
            if (slot == 120) {
                st[9]--;
                k[KI_COOKING] = 0;
                put16(k + KI_MEAL, 0);
                continue;
            }
            put16(st + 0xFA + 2 * slot, get16(k + KI_MEAL));
            st[0x262 + slot] = 100;
            work_done(s, cook);
        }
        /* 4. the player's cook below the recipe's level makes the plain meal */
        if (h == 0 && cook_level(s, cook) < get16(k + KI_LEVEL)) put16(k + KI_LEVEL, 0), put16(k + KI_MEAL, 79), k[KI_RECIPE] = 0;
        /* 5. what the kitchen lacks: capabilities 9, 0x24 (list 0xE60A), kind 0x11 (list 0xE60D) */
        k[KI_CAP] = !has_cap(s, k, 13, 9) ? 9 : !has_cap(s, k, 13, 0x24) ? 0x24 : 0;
        k[KI_KIND] = !has_kind(s, k, 13, 0x11) ? 0x11 : 0;
        if (k[KI_RECIPE] >= RECIPE_COUNT) continue; /* engine choice: no such recipe */
        uint32_t r = RECIPES + 8u * k[KI_RECIPE];
        /* 6. the recipe's utensils; a missing kind or wanted furniture stops the cooking as well */
        int ready = !k[KI_KIND] && !k[KI_WANT];
        if (ready) {
            k[KI_CAP] = 0; /* step 5's capability is dropped here */
            for (int u = 4; u < 8 && image_byte(s, r + (uint32_t)u, 0) != 0xFF; u++) {
                int cap = image_byte(s, UTENSIL_CAPS, (uint32_t)image_byte(s, r + (uint32_t)u, 0));
                if (cap && !has_cap(s, k, 13, cap)) {
                    k[KI_CAP] = (uint8_t)cap;
                    ready = 0;
                    break;
                }
            }
        }
        if (ready) { /* 7. every utensil is used, and the next meal is on */
            for (int u = 4; u < 8 && image_byte(s, r + (uint32_t)u, 0) != 0xFF; u++) {
                int cap = image_byte(s, UTENSIL_CAPS, (uint32_t)image_byte(s, r + (uint32_t)u, 0));
                /* engine choice: the original loops for ever on a utensil worn out by an earlier use of this
                 * round (one item carrying two of the recipe's capabilities); the engine stops using */
                if (cap && !equipment_id(s, k, 13, cap, 1, 0)) break;
            }
            put16(k + KI_LEFT, image_byte(s, r + 1, 0));
            int meal = (uint16_t)program_u16(s->prog, r + 2);
            put16(k + KI_MEAL, h != 0 && meal == 79 ? 101 : meal);
            continue;
        }
        /* 8. the player's requests */
        player_request(s, h, 13, k, k + KI_CAP);
        player_kind_request(s, h, 13, k, k + KI_KIND);
        player_furniture_request(s, h, 13, k, k + KI_WANT);
    }
}

/* ---- the player's branches of the helpers (docs/ui-rooms.md; with [0x5ABD] clear) ------------------------------ */

int sim_player_seat(Sim *s, int h, int *room) { /* 0x2B989 with scratch +0x5B set */
    uint8_t *rec = reception(s, h);
    const uint8_t *p = person(s, get16(rec + 10));
    int w, i;
    if (!room_counts(s, h)[3]) return SEAT_NO_ROOMS;
    if (!seat_search(s, h, p, &w, &i)) return SEAT_FULL;
    /* the form (kind 11) and the pen (kind 3): "has" (0x2BD17), then "use" (0x2BD44), which cannot fail then */
    if (!equipment_id(s, rec, 2, 11, 0, 1)) return SEAT_NO_FORM;
    if (!equipment_id(s, rec, 2, 3, 0, 1)) return SEAT_NO_PEN;
    use_kind(s, rec, 2, 11); /* a worn-out item leaves through the tidy-up 0x2BFB7 (docs/sim-drift2.md section 2) */
    use_kind(s, rec, 2, 3);
    seat_take(s, h, w, i);
    *room = w;
    return SEAT_DONE;
}

void sim_player_leave(Sim *s, int h) { /* 0x2BE91: queue place 1 leaves the clinic */
    uint8_t *queue = reception(s, h) + 10, *p = person(s, get16(queue));
    p[P_HOSPITAL] = p[P_STATE] = 0xFF;
    p[P_OPINION + h] /= 2;
    queue_up(queue, 4);
}

int sim_free_beds(Sim *s, int h, int cat, int *ward, int *total) { /* 0x2B4B3 (6) / 0x2B52C (16) */
    int free_beds = 0, beds = 0;
    for (int k = 0; k < sim_rooms_in_use(s, h, cat); k++) {
        const uint8_t *rec = sim_room_record(s, h, cat, k);
        beds += rec[10];
        if (rec[12] < rec[10]) free_beds += rec[10] - rec[12], *ward = k;
    }
    if (total) *total = beds;
    return free_beds;
}

int sim_player_bed(Sim *s, int h, int ward, int place, int ai) { return bed_transfer(s, h, ward, place, ai); }

int sim_player_call(Sim *s, int h, int room, int *from) { /* 0x2E3F2 + the move */
    uint8_t *tr = sim_room_record(s, h, 5, room);
    return bring_patient(s, h, &KINDS[0], tr, room, 1, from); /* type 0 first, then anyone */
}

void sim_player_propose(Sim *s, int h, int room) { sim_player_propose_in(s, h, 5, room); } /* 0x3104F */
/* 0x3104F, 0x349A9 (dental), 0x35D82 (neurology) in player mode */
void sim_player_propose_in(Sim *s, int h, int cat, int room) {
    uint8_t *tr = sim_room_record(s, h, cat, room);
    choose_treatment(s, h, kind_of(cat), person(s, get16(tr + 12)), tr, 1);
}

int sim_player_after_diagnosis(Sim *s, int h, int room, int *n) { /* 0x2D11A in player mode, without the bill */
    uint8_t *tr = sim_room_record(s, h, 5, room);
    int index = get16(tr + 12);
    uint8_t *p = person(s, index);
    int treat = p[P_TREATMENT], cnt = get16(tr + 18);
    if (p[P_WISH] == 5 && get16(tr + 16) && cnt != 999 && (int16_t)get16(p + P_BLOOD) < 690) {
        *n = donor_done(s, h, tr);
        return AFTER_DONOR;
    }
    if (cnt && cnt != 999) return AFTER_RUNNING;
    if (get16(tr + 16) || p[P_IN_TREATMENT]) return AFTER_BILL;
    if (!cnt) sim_rating_record(s, tr);
    const uint8_t *r = p[P_DIAGNOSIS] ? illness(s, p[P_DIAGNOSIS]) : NULL;
    /* the code tests the diagnosed illness's record (byte 8 = 2 or byte 15 = 3); engine choice: no treatment at all
     * (no illness found) takes the same way, as the AI branch does */
    if ((r && (r[8] == 2 || r[15] == 3)) || !treat || treat >= 0xFD) {
        *n = back_to_waiting(s, h, tr, index);
        return *n >= 0 ? AFTER_WAITING : AFTER_LEFT;
    }
    if (!treatment(s, treat)[4]) {
        decide_equipment(s, 5, tr, treat);
        put16(tr + 16, treat);
        put16(tr + 18, 999);
        return AFTER_IN_ROOM;
    }
    uint8_t *queue = reception(s, h) + 0x46;
    int place = -1;
    for (int i = 0; i < 6 && place < 0; i++)
        if (!get16(queue + 4 * i)) place = i;
    if (place < 0) return AFTER_QUEUE_FULL; /* player: nothing changes */
    free_room(tr);
    if (!room_counts(s, h)[6]) {
        send_home_untreated(s, p, h);
        return AFTER_NO_WARD;
    }
    put16(queue + 4 * place, index);
    put16(queue + 4 * place + 2, 0);
    p[P_STATE] = ST_RECEPTION;
    work_done(s, get16(tr + 10));
    work_done(s, get16(tr + 8));
    *n = place;
    return AFTER_QUEUED;
}

static void count_bill(Sim *s, int h) { /* 0x2B47B: +0x16E + 1, at 1,000 back to 0 and +0x172 + 1 */
    uint8_t *r = hospital(s, h);
    add32(r + 0x16E, 1);
    if (get32(r + 0x16E) >= 1000) put32(r + 0x16E, 0), add16(r + 0x172, 1);
}

int sim_player_bill(Sim *s, int h, int room, int *v) { return sim_player_bill_in(s, h, 5, room, v); } /* 0x2D957 */
/* 0x2D957 (treatment room), 0x2D976 (dental: +0x30 too), 0x2D93D (neurology: +0x4E too) in player mode */
int sim_player_bill_in(Sim *s, int h, int cat, int room, int *v) {
    uint8_t *tr = sim_room_record(s, h, cat, room), *r = hospital(s, h);
    int index = get16(tr + 12);
    uint8_t *p = person(s, index);
    sim_rating_record(s, p); /* player-message entry to the common home-treatment exit */
    int treat = p[P_TREATMENT];
    add16(r + 0x26, 1); /* 0x2EE24 (copy-protection cool-down) is left out by design */
    if (cat == 11) add16(r + 0x30, 1);
    if (cat == 15) add16(r + 0x4E, 1);
    free_room(tr);
    p[P_HOSPITAL] = 0xFF;
    p[P_TREATED_BY] = (uint8_t)h;
    int bill = sim_invoice(s, h, p);
    *v = sim_bill_reaction(p, bill, 0);
    pay(s, h, bill);
    sim_news_bill(s, h, index, bill, 0); /* 0x42982 */
    opinion_after_bill(p, h, bill, 0); /* 0x32075 */
    p[P_STATE] = 0xFF;
    p[P_COUNTDOWN] = treat && treat < 0xFD ? treatment(s, treat)[7] : 0;
    p[P_IN_TREATMENT] = 0xFF, p[P_ROUNDS] = 0;
    count_bill(s, h); /* owner's decision 2026-10-18: raised as in the original */
    return bill;
}

int sim_player_abort(Sim *s, int h, int room, int *n) { return sim_player_abort_in(s, h, 5, room, n); } /* 0x241EC */
/* 0x241EC (treatment room: 1 = the donor-done step ran), 0x25BAF / 0x25DC5 (dental, neurology: no donor step; the
 * room's +0xE .. +0x12 stay) */
int sim_player_abort_in(Sim *s, int h, int cat, int room, int *n) {
    uint8_t *tr = sim_room_record(s, h, cat, room);
    uint8_t *p = person(s, get16(tr + 12));
    if (cat == 5 && get16(tr + 20) && p[P_WISH] == 5) {
        *n = donor_done(s, h, tr);
        return 1;
    }
    put16(tr + 12, 0);
    p[P_HOSPITAL] = p[P_STATE] = 0xFF;
    p[P_OPINION + h] = 0;
    put16(p + P_DIAGNOSIS, 0);
    p[P_STEPS] = 0;
    memset(p + P_UNITS, 0, 8);
    p[P_IN_TREATMENT] = 0;
    return 0;
}

int sim_player_discharge(Sim *s, int h, int ward, int bed, int *v) { return sim_player_discharge_in(s, h, 6, ward, bed, v); }
/* 0x29DE0 (Krankenzimmer) / 0x4EF4A (neuro ward) in player mode: the bill */
int sim_player_discharge_in(Sim *s, int h, int cat, int ward, int bed, int *v) {
    uint8_t *k = sim_room_record(s, h, cat, ward);
    const uint8_t *p = person(s, get16(k + 0x14 + 4 * bed));
    sim_rating_record(s, p); /* the player's invoice screen precedes the common discharge */
    int bill = sim_invoice(s, h, p);
    *v = sim_bill_reaction(p, bill, 1); /* the invoice screen first, with [0x5C2B] set */
    ward_discharge(s, h, &WARDS[cat == 16], k, bed);
    return bill;
}

void sim_player_step(Sim *s, int index) { /* 0x4F1B9: start or interrupt the bed step */
    uint8_t *p = person(s, index);
    if (p[P_IN_STEP]) p[P_IN_STEP] = 0;
    else next_bed_step(s, p);
}

int sim_player_icu(Sim *s, int h, int ward, int index) { /* 0x4F395 */
    return icu_admit(s, h, sim_room_record(s, h, 6, ward), index);
}

/* The corpse button: 0x522D2 and the pathology list, then the room's clean-up (docs/ui-rooms.md, "The corpse
 * button"). cat/room/place name where P lies: 2 the desk, 3 seat `place` of waiting room `room`, 5 treatment room
 * `room`, 6 bed `place` of Krankenzimmer `room`. Returns 0 (pathology), 1 (no pathology), 2 (pathology full);
 * *staff = the staff index the message names: the pathology record's u16 +8 (its place count; reproduce). */
int sim_player_corpse(Sim *s, int h, int index, int cat, int room, int place, int *staff) {
    int has = room_counts(s, h)[18];
    uint8_t *path = pathology_place(s, h);
    *staff = has ? get16(sim_room_record(s, h, 18, 0) + 8) : 0;
    died_found(s, h, index);
    if (cat == 2) {
        queue_up(reception(s, h) + 10, 4); /* 0x52D53 */
    } else if (cat == 3) {
        uint8_t *wr = sim_room_record(s, h, 3, room); /* 0x53396 */
        memset(wr + 0x14 + 4 * place, 0, 4);
        wr[12]--;
    } else if (cat == 5) {
        memset(sim_room_record(s, h, 5, room) + 12, 0, 10); /* 0x52CB0 */
    } else if (cat == 6 || cat == 16) {
        uint8_t *k = sim_room_record(s, h, cat, room); /* 0x52C90: the bed pb+2, K+0xC - 1 */
        put16(k + 0x14 + 4 * place, 0);
        k[12]--;
    } else if (cat == 11 || cat == 15) {
        memset(sim_room_record(s, h, cat, room) + 12, 0, 10); /* 0x52D1F / 0x52D39: R+0xC .. +0x15 */
    } else if (cat == 9) {
        uint8_t *rec = sim_room_record(s, h, 9, room); /* 0x52404 */
        put16(rec + ICU_PATIENT, 0);
        put16(rec + ICU_HOURS, 0);
    } else if (cat == 7 || cat == 8) { /* 0x52CCA: the OP's dead also leave their Krankenzimmer bed; 0x52978 */
        if (cat == 7) leave_ward_bed(s, h, index);
        theatre_clear(sim_room_record(s, h, cat, room));
    }
    return path ? 0 : has ? 2 : 1;
}

/* ---- the player's branches in the specialist departments (docs/ui-departments.md) ----------------------------- */

int sim_player_call_in(Sim *s, int h, int cat, int room, int *from) { /* 0x2E3F2 / 0x34ABB / 0x358B5 + the move */
    uint8_t *tr = sim_room_record(s, h, cat, room);
    return bring_patient(s, h, kind_of(cat), tr, room, 1, from); /* the room's own type first, then anyone */
}

/* 0x34C5C (dental) / 0x35E59 (neurology) in player mode, without the bill. AFTER_QUEUED: a neurology bed case with a
 * free neuro bed, *n = the last neuro ward with one (pb+0xC); AFTER_NOTHING: nothing happens. */
int sim_player_after_clinic(Sim *s, int h, int cat, int room, int *n) {
    uint8_t *tr = sim_room_record(s, h, cat, room);
    int index = get16(tr + 12);
    uint8_t *p = person(s, index);
    if (get16(tr + 18)) return AFTER_RUNNING;
    if (get16(tr + 16) || p[P_IN_TREATMENT]) return AFTER_BILL;
    const uint8_t *r = illness(s, p[P_DIAGNOSIS]); /* the diagnosed illness's record, whatever field 42 holds */
    if (cat == 11 ? r[15] != 3 : r[8] != 2) {
        *n = back_to_waiting(s, h, tr, index);
        return *n >= 0 ? AFTER_WAITING : AFTER_LEFT;
    }
    int treat = p[P_TREATMENT];
    if (!treatment(s, treat)[4]) {
        decide_equipment(s, cat, tr, treat);
        put16(tr + 16, treat);
        put16(tr + 18, 999);
        return AFTER_IN_ROOM;
    }
    /* a dental bed treatment: the original loops for ever (0x34D1F); no dental illness has one (cap: nothing) */
    if (cat == 11) return AFTER_NOTHING;
    if (!room_counts(s, h)[16]) return AFTER_NO_WARD;
    int ward = 0;
    if (!sim_free_beds(s, h, 16, &ward, NULL)) return AFTER_NOTHING; /* no free bed: no message (oracle) */
    *n = ward;
    return AFTER_QUEUED;
}

/* 0x235ED after its counters: the neurology patient (neurology record `room`) into a free bed of neuro ward `ward`.
 * Returns the bed, or -1 when the ward is full (nothing more happens). */
int sim_player_neuro_bed(Sim *s, int h, int room, int ward) {
    uint8_t *r = hospital(s, h), *k = sim_room_record(s, h, 16, ward), *nr = sim_room_record(s, h, 15, room);
    add16(r + 0x4C, 1); /* first, unconditionally (oracle) */
    add16(r + 0x4E, 1);
    if (ward < 0 || k[12] >= k[10]) return -1; /* ward < 0: the original loops for ever (cap) */
    int index = get16(nr + 12);
    uint8_t *p = person(s, index);
    put16(nr + 12, 0); /* P leaves the neurology; +0x10 / +0x12 stay */
    int b = -1;
    for (int round = 0; round < 2 && b < 0; round++) /* random(10) over all ten places, whatever the bed count */
        for (int t = 0; t < 100 && b < 0; t++) {
            int c = (int)sim_random(s, 10), prio = image_byte(s, BED_PRIORITY, (uint32_t)c);
            if (!get16(k + 0x14 + 4 * c) && prio == (round ? 2 : p[P_SEX])) b = c;
        }
    for (int c = 0; b < 0 && c < 10; c++)
        if (!get16(k + 0x14 + 4 * c)) b = c;
    if (b < 0) { /* no free place at all: the player's branch loops for ever (cap: P stays in the neurology) */
        put16(nr + 12, index);
        return -1;
    }
    put16(k + 0x14 + 4 * b, index);
    put16(k + 0x16 + 4 * b, 0);
    k[12]++;
    p[P_STATE] = ST_NEURO_WARD;
    p[P_ROOM] = (uint8_t)ward;
    return b;
}

/* 0x520DC: from intensive care back to the reception's bed queue, at any health. The queue place, or -1 (all six
 * taken), -2 (under narcosis). */
int sim_player_icu_back(Sim *s, int h, int room) {
    uint8_t *rec = sim_room_record(s, h, 9, room), *queue = reception(s, h) + 0x46;
    int index = get16(rec + ICU_PATIENT), place = -1;
    uint8_t *p = person(s, index);
    for (int i = 0; i < 6 && place < 0; i++)
        if (!get16(queue + 4 * i)) place = i;
    if (place < 0) return -1;
    if (p[P_RECOVERY]) return -2;
    put16(queue + 4 * place, index);
    put16(queue + 4 * place + 2, 0);
    put16(rec + ICU_PATIENT, 0); /* the hours +0x10 stay */
    if ((int8_t)p[P_HEALTH] >= 50) add16(hospital(s, h) + 0x58, 1);
    p[P_STATE] = ST_RECEPTION;
    p[P_IN_STEP] = 0xFF;
    return place;
}

/* 0x4E6CC / 0x4E978 for the player: the patient brought, or 0; *ward = the ward it came from */
int sim_player_theatre_fetch(Sim *s, int h, int cat, int room, int *ward) {
    const TheatreKind *tk = &THEATRES[cat == 8];
    return theatre_bring(s, tk, h, sim_room_record(s, h, cat, room), room, ward);
}

/* 0x4FD90 / 0x526FF after the staff test: a running operation is stopped (field 101 + 1, the next tick restarts it
 * from its first stage), else started. Returns THEATRE_STOPPED, THEATRE_STARTED, THEATRE_NO_ORGAN or the missing
 * capability. */
int sim_player_theatre_toggle(Sim *s, int h, int cat, int room) {
    const TheatreKind *tk = &THEATRES[cat == 8];
    uint8_t *rec = sim_room_record(s, h, cat, room), *p = person(s, get16(rec + TH_PATIENT));
    if (get16(rec + TH_STAGE) != 999) {
        p[P_BLEEDING]++;
        put16(rec + TH_STAGE, 999);
        put16(rec + TH_INDEX, 0);
        put16(rec + TH_COUNTER, 0);
        return THEATRE_STOPPED;
    }
    return theatre_start(s, tk, h, rec, p, 0);
}

/* 0x4FBBF / 0x52541: the patient back to the bed it kept, whatever the operation's state. Returns field 100 (0 never
 * started, 1 running or stopped, 2 or more finished) as it was. */
int sim_player_theatre_back(Sim *s, int h, int cat, int room) {
    uint8_t *rec = sim_room_record(s, h, cat, room), *p = person(s, get16(rec + TH_PATIENT));
    int done = p[P_FIELD100];
    p[P_STATE] = p[P_SAVED_STATE], p[P_ROOM] = p[P_SAVED_ROOM];
    if (cat == 8) p[P_IN_STEP] = 0xFF;
    if (cat == 7 && done >= 2 && get16(rec + TH_ORGAN)) add16(hospital(s, h) + 0x8A, 1);
    memset(rec + TH_PATIENT, 0, 12); /* the dwords +0xC, +0x10, +0x14 (so +0xE becomes 0) and u16 +0x18 */
    put16(rec + TH_ORGAN, 0);
    return done; /* fields 100, 101 and 102 are not reset */
}

int sim_player_pathology_take(Sim *s, int h) { return pathology_take(s, sim_room_record(s, h, 18, 0)); } /* 0x50FF2 */

/* 0x52452: the case thrown away (renamed, 0x3200C, and reset at home; docs/ui-departments.md). Returns
 * 0 (examination finished), 1 (not started), 2 (in between). No verdict, no further organs. */
int sim_player_pathology_throw(Sim *s, int h) {
    uint8_t *rec = sim_room_record(s, h, 18, 0);
    int index = get16(rec + PA_CASE);
    int state = get16(rec + PA_COUNT) == 999 ? 0 : !get16(rec + PA_STAGE) ? 1 : 2;
    put16(rec + PA_CASE, 0); /* +0xE keeps its value until the next case */
    sim_rename_dead(s, index);
    sim_reset_healthy(person(s, index));
    return state;
}

/* ---- the AI branches for module D (sim_player.c, docs/sim-player.md): the player's own staff working alone ------- */

int sim_ai_bring(Sim *s, int h, int cat, int room) { /* 0x2E252 / 0x34A8F / 0x358DB in AI mode */
    return bring_patient(s, h, kind_of(cat), sim_room_record(s, h, cat, room), room, 0, NULL);
}
void sim_ai_choose(Sim *s, int h, int cat, int room) { /* 0x3104F / 0x349A9 / 0x35D82 in AI mode */
    uint8_t *tr = sim_room_record(s, h, cat, room);
    choose_treatment(s, h, kind_of(cat), person(s, get16(tr + 12)), tr, 0);
}
void sim_ai_act(Sim *s, int h, int cat, int room) { /* 0x2D11A / 0x34C5C / 0x35E59 in AI mode */
    after_diagnosis(s, h, kind_of(cat), sim_room_record(s, h, cat, room));
}
int sim_ai_theatre_start(Sim *s, int h, int cat, int room) { /* 0x4FDDA / 0x52749 in AI mode */
    uint8_t *rec = sim_room_record(s, h, cat, room);
    return theatre_start(s, &THEATRES[cat == 8], h, rec, person(s, get16(rec + TH_PATIENT)), 1);
}

/* ---- dispatch ------------------------------------------------------------------------------------- */

int sim_rooms_after_operator(Sim *s, int h, int cat, int room) {
    if (cat == 2) {
        if (cash_of(s, h) < 0 || !get16(reception(s, h) + 8)) return 0;
        if (!reception_request(s, h)) return 0; /* no admission and no bed move this run */
        if (get16(reception(s, h) + 10)) to_waiting_room(s, h);
        queue_to_bed(s, h);
    } else if (cat == 5 || cat == 11 || cat == 15) {
        return room_operator(s, h, cat, room);
    }
    return 0;
}

int sim_rooms_run(Sim *s, uint32_t routine) {
    switch (routine) {
    case 0x6BFC8: waiting_rooms(s); return 1;
    case 0x67611: reception_minute(s); return 1;
    case 0x6AD3C: room_tick(s, 5); return 1;
    case 0x6B445: room_tick(s, 11); return 1;
    case 0x6BAE4: room_tick(s, 15); return 1;
    case 0x6ABB9: wards_hourly(s, &WARDS[0]); return 1;
    case 0x6CE07: wards_hourly(s, &WARDS[1]); return 1;
    case 0x6A0BA: ward_tick(s, &WARDS[0]); return 1;
    case 0x6C3D5: ward_tick(s, &WARDS[1]); return 1;
    case 0x2995B: ward_operator(s, &WARDS[0]); return 1;
    case 0x2A01F: ward_operator(s, &WARDS[1]); return 1;
    case 0x30965: ward_wants(s, &WARDS[0]); return 1;
    case 0x536D1: ward_wants(s, &WARDS[1]); return 1;
    case 0x6F346: icu_operator(s); return 1;
    case 0x6FA5A: icu_minute(s); return 1;
    case 0x5A3CD: icu_hourly(s); return 1;
    case 0x59DCA: comfort_countdown(s, 9, ICU_COMFORT); return 1;
    case 0x58045: theatre_tick(s, &THEATRES[0]); return 1;
    case 0x597F9: theatre_tick(s, &THEATRES[1]); return 1;
    case 0x6E8BE: theatre_operator(s, &THEATRES[0]); return 1;
    case 0x6E4C1: theatre_operator(s, &THEATRES[1]); return 1;
    case 0x58BA3: comfort_countdown(s, 7, TH_COMFORT); return 1;
    case 0x6F6B2: pathology_minute(s); return 1;
    case 0x5A268: pathology_operator(s); return 1;
    case 0x535DC: bank_operator(s); return 1;
    case 0x53C79: bank_minute(s); return 1;
    case 0x6E161: kitchen_operator(s); return 1;
    case 0x6EDA1: kitchen_minute(s); return 1;
    case 0x57E03: organ_ageing(s); return 1;
    default: return 0;
    }
}

/* "has" a capability among a room's equipment (0x2C20A), and a furniture kind among its furniture (0x2C0BA), for the
 * garage's fittings (sim_race.c) */
int sim_has_equipment(Sim *s, uint8_t *rec, int cat, int cap) { return equipment_id(s, rec, cat, cap, 0, 0) != 0; }
int sim_has_equipment_kind(Sim *s, uint8_t *rec, int cat, int kind) { return equipment_id(s, rec, cat, kind, 0, 1) != 0; }
int sim_has_furniture_kind(Sim *s, const uint8_t *rec, int cat, int kind) {
    Description d = sim_description(s, cat);
    for (int f = 0; f < d.furniture_slots; f++) {
        int id = (uint16_t)get16(rec + d.furniture + 2 * f);
        if (id && sim_furniture_item(s, id)[6] == kind) return 1;
    }
    return 0;
}
