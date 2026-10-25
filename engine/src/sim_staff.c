/* Module E, staff and hiring, and the AI store room, from docs/sim-staff.md (and 0x390BA from
 * docs/sim-ai.md). The patient side of the room operators belongs to module C and is not here. */
#include <string.h>

#include "sim_internal.h"

#define STAFF_RECORDS 1041
#define STAFF_RECORD_SIZE 32
#define COURSES 0x40D9
#define COURSE_SIZE 22

/* staff record bytes (docs/formats.md, block B; docs/sim-staff.md) */
enum { S_SEX = 0, S_GROUP = 1, S_RATING = 2, S_HOSPITAL = 3, S_STATE = 4, S_BUST = 5, S_SKILL = 15, S_DEMAND = 18,
       S_WAGE = 19, S_MOOD = 21, S_DISMISSED_BY = 22, S_ROOM = 26, S_RESERVED = 27 };
/* staff slot bytes */
enum { SL_INDEX = 0, SL_GROUP = 2, SL_STATE = 3, SL_ROOM = 4, SL_WAGE = 6, SL_HOURS = 7, SL_ON_DUTY = 10,
       SL_OFF_DUTY = 11, SL_REST = 18 };
enum { OFF_DUTY = 0x13, RESTING = 0x18, TRAINING = 0x19 };

static uint8_t *staff(Sim *s, int i) { return s->world->block[BLK_STAFF] + i * STAFF_RECORD_SIZE; }
/* A slot's staff index, 0 when the slot is empty. The original skips index 0 only; an index outside the staff block
 * (a test's -1 for "not employed") would read outside it, so the engine treats it as empty (engine guard, not a rule
 * of the original). */
static int slot_staff(const uint8_t *sl) {
    int index = get16(sl + SL_INDEX);
    return index > 0 && index < STAFF_COUNT ? index : 0;
}
static uint8_t *opening(Sim *s, int j) { return s->world->block[BLK_JOB_OPENINGS] + j * JOB_OPENING_SIZE; }
static uint8_t *slot(Sim *s, int h, int i) { return staff_slots(s, h) + i * STAFF_SLOT_SIZE; }
static int in_pool(const uint8_t *st) { return (int8_t)st[S_HOSPITAL] < 0; }

/* ---- openings, pairing, hiring -------------------------------------------------------------------- */

/* The running maximum bust B of the women in the pool and the demand D of the records that raised it. */
static void best_bust(Sim *s, int *b, int *d) {
    *b = 0, *d = 0;
    for (int i = 1; i < STAFF_RECORDS; i++) {
        const uint8_t *st = staff(s, i);
        if (!in_pool(st) || st[S_SEX] != 1 || st[S_BUST] <= *b) continue;
        *b = st[S_BUST];
        if (st[S_DEMAND] > *d) *d = st[S_DEMAND];
    }
}

/* 0x62D6A */
static void staffing_need(Sim *s) {
    World *w = s->world;
    int h = sim_pick(s);
    if (!h || cash_of(s, h) < 0) return;
    uint8_t *r = hospital(s, h);
    const uint8_t *c = room_counts(s, h);
    int day = world_tail(w, TAIL_DAY), hour = world_tail(w, TAIL_HOUR), month = world_tail(w, TAIL_MONTH);
    int32_t cash = get32(r + H_CASH);
    int need[11] = {0};
    need[1] = (c[2] ? 1 : 0) + c[3] + c[5] + c[6] + c[7] + c[8] + c[9] + c[11] + (c[12] ? 1 : 0) + (c[14] ? 2 : 0) +
              c[15] + c[16] + (c[17] ? 1 : 0);
    need[2] = c[5] + c[7];
    need[3] = c[4] ? 1 : 0;
    need[4] = c[13] ? 1 : 0;
    need[5] = c[7];
    need[6] = (c[10] ? 2 : 0) + (day > 3 && hour < 8 && cash >= 600 ? 1 : 0);
    need[7] = (c[17] ? 1 : 0) + c[18];
    if (c[3] && day != 1 && hour < 8) {
        if (cash >= 600) need[8]++;
        if (day > 3 && cash >= 800) need[8]++;
        if (day > 7 && cash >= 1000) need[8]++;
    }
    need[9] = c[11];
    need[10] = c[8] + c[15];
    for (int i = 0; i < STAFF_SLOTS; i++) {
        const uint8_t *sl = slot(s, h, i);
        if (get16(sl + SL_INDEX) && sl[SL_GROUP] <= 10) need[sl[SL_GROUP]]--;
    }
    for (int j = 0; j < JOB_OPENINGS; j++) {
        const uint8_t *o = opening(s, j);
        if (o[0] == h && o[1] <= 10) need[o[1]]--;
    }
    s->need4 = (int16_t)need[4]; /* entry 4 stays in the table, read as catalog row 90 (SIM-65) */
    int group = 0;
    if (r[0xE0]) {
        group = r[0xE0];
        r[0xE0] = 0;
    } else {
        for (int g = 1; g <= 10 && !group; g++)
            if (need[g] > 0) group = g;
    }
    if (!group) return;
    int j = 0;
    while (j < JOB_OPENINGS && (int8_t)opening(s, j)[0] >= 0) j++;
    if (j == JOB_OPENINGS) return;
    uint8_t *o = opening(s, j);
    o[0] = (uint8_t)h, o[1] = (uint8_t)group;
    int b, d;
    best_bust(s, &b, &d);
    o[2] = (uint8_t)(sim_bits(s, 3) + 11);
    o[3] = 0;
    if (group == 1) {
        o[2] = (uint8_t)(d + 1);
        o[3] = (uint8_t)(b - 20);
        if (b >= 120) {
            o[2] = (uint8_t)(sim_bits(s, 3) + 11);
        } else if (sim_bits(s, 7)) {
            o[3] = 80;
            o[2] = (uint8_t)(sim_bits(s, 3) + 7);
        }
        if (o[3] < 100) o[3] = 80; /* SIM-23: below 100 becomes 80 before the month ladder (docs/sim-staff.md) */
    }
    o[4] = 75, o[5] = 5;
    int jan = month == 1;
    switch (group) {
    case 1: {
        int m = o[3];
        if (m >= 130) o[5] = 1;
        else if (m >= 120) o[5] = 2;
        else if (m >= 110) o[5] = month >= 2 || day >= 10 ? 1 : day >= 5 ? 2 : 3;
        else if (m >= 100) o[5] = month >= 2 ? 1 : day >= 10 ? 2 : day >= 5 ? 3 : 4;
        else o[5] = month >= 2 ? 2 : day >= 10 ? 3 : day >= 5 ? 4 : 5;
        break;
    }
    case 2: o[2] = 12; break;
    case 3: o[2] = 9, o[4] = 20, o[5] = jan && day >= 4 && day <= 9 ? 2 : 0; break;
    case 4: o[2] = 13, o[4] = 50, o[5] = jan && day >= 4 && day <= 9 ? 4 : 2; break;
    case 5: o[2] = 13; break;
    case 6: o[2] = 14, o[4] = jan && day >= 4 ? 40 : 20, o[5] = jan && day >= 4 ? 4 : 2; break;
    case 7: o[2] = 11, o[4] = 60, o[5] = 4; break;
    case 8: o[2] = 12, o[4] = jan && day <= 9 ? 30 : 20, o[5] = jan && day <= 9 ? 2 : 1; break;
    default: o[2] = 12; break; /* 9 and 10 */
    }
    o[6] = 0;
}

static int qualifies(const uint8_t *st, const uint8_t *o, int compare_wage_signed) {
    if (st[S_GROUP] != o[1]) return 0;
    if (compare_wage_signed ? (int8_t)o[2] < (int8_t)st[S_DEMAND] : st[S_DEMAND] > o[2]) return 0;
    if (st[S_SEX] == 0 ? st[S_SKILL] < o[4] : st[S_BUST] < o[3]) return 0;
    return st[S_RATING] >= o[5] && st[S_DISMISSED_BY] != o[0];
}

static void release_reservations(Sim *s, int j) {
    for (int i = 0; i < STAFF_RECORDS; i++) {
        uint8_t *st = staff(s, i);
        if (st[S_GROUP] && in_pool(st) && st[S_RESERVED] == j) st[S_RESERVED] = 0xFF;
    }
}

/* 0x1F2A8 */
static void opening_upkeep(Sim *s) {
    s->openings = 0;
    for (int j = 0; j < JOB_OPENINGS; j++) {
        uint8_t *o = opening(s, j);
        if ((int8_t)o[0] < 0) continue;
        if (o[0] == 0 && hospital(s, 0)[0x14]) {
            int any = 0;
            for (int i = 1; i < STAFF_RECORDS && !any; i++) any = in_pool(staff(s, i)) && qualifies(staff(s, i), o, 0);
            if (!any) {
                o[0] = 0xFF;
                continue;
            }
        }
        s->openings++;
        if (sim_bits(s, 7) == 3) o[6]++;
        if (o[6] > 59) {
            o[6] = 60;
            if (o[0] == 0 && !s->on_applications) { /* the player's opening expires, unless screen 51 is open */
                release_reservations(s, j);
                o[0] = 0xFF;
            }
        }
    }
}

/* 0x1F1F3 */
static void pairing(Sim *s) {
    if (!s->openings) return;
    for (int tries = 0; tries < 40; tries++) {
        uint8_t *st = staff(s, (int)sim_random(s, 1040) + 1);
        if ((int8_t)st[S_HOSPITAL] >= 0 || (int8_t)st[S_STATE] >= 0 || (int8_t)st[S_RESERVED] >= 0) continue;
        int j = (int)sim_random(s, 200);
        const uint8_t *o = opening(s, j);
        if ((int8_t)o[0] < 0 || !qualifies(st, o, 1)) continue;
        st[S_RESERVED] = (uint8_t)j;
        return;
    }
}

/* 0x1F63D */
static void ai_hiring(Sim *s) {
    if (!s->openings) return;
    int h = sim_pick(s);
    if (!h) return;
    uint8_t *r = hospital(s, h);
    for (int j = 0; j < JOB_OPENINGS; j++) {
        uint8_t *o = opening(s, j);
        if (o[0] != h) continue;
        int who = 0;
        for (int i = 1; i < STAFF_RECORDS && !who; i++)
            if (staff(s, i)[S_RESERVED] == j) who = i;
        if (who) {
            int k = 0;
            while (k < STAFF_SLOTS && get16(slot(s, h, k) + SL_INDEX)) k++;
            uint8_t *st = staff(s, who);
            if (k == STAFF_SLOTS || st[S_GROUP] != o[1]) return;
            uint8_t *sl = slot(s, h, k);
            int wage = st[S_DEMAND] + 2;
            put16(sl + SL_INDEX, who);
            sl[SL_GROUP] = o[1];
            sl[SL_STATE] = sl[SL_ROOM] = sl[5] = 0;
            sl[SL_WAGE] = (uint8_t)wage;
            st[S_WAGE] = (uint8_t)wage;
            add32(r + H_CASH, -wage);
            add32(r + 0xA6, wage);
            add32(r + 0xAA, wage);
            st[S_HOSPITAL] = (uint8_t)h;
            st[S_STATE] = 0;
            st[S_RESERVED] = 0xFF;
            release_reservations(s, j);
            o[0] = 0xFF;
            return;
        }
        if (o[2] > 14) o[2] = 15;
        if (o[6] > 14) {
            if (o[3] == 0) {
                if (o[4] < 20) o[2]++;
                else {
                    o[4] = (uint8_t)(o[4] - 10);
                    if (o[5] > 1) o[5]--;
                }
            } else {
                int b, d;
                best_bust(s, &b, &d);
                b -= world_tail(s->world, TAIL_HOURS);
                if (b < 125) {
                    int x = (int)sim_bits(s, 3);
                    o[2] = (uint8_t)(8 + x);
                    o[3] = (uint8_t)(80 + 4 * x);
                    if (o[5] > 1) o[5]--;
                } else {
                    b -= 42;
                    o[3] = (uint8_t)b;
                    o[2] = b >= 134 ? 15 : b >= 128 ? 14 : b >= 124 ? 13 : b >= 114 ? 12 : b >= 108 ? 11
                         : b >= 100 ? 9 : b >= 96 ? 8 : b >= 93 ? 7 : 6;
                }
            }
            o[6] = 0;
        }
    }
}

/* ---- assigning and releasing ---------------------------------------------------------------------- */

static const int GROUP_CAP[11] = {0, 0, 0, 2, 2, 0, 4, 3, 3, 0, 0}; /* 0 = no cap */

/* 0x22D5F / 0x22D73: the last idle employee of the group with mood >= 10, or 0; with none, possibly
 * request an opening of that group through h+0xE0. Returns the staff index and the slot number. */
int sim_idle_candidate(Sim *s, int h, int group, int *slot_out) {
    int found = 0;
    for (int i = 0; i < STAFF_SLOTS; i++) {
        const uint8_t *sl = slot(s, h, i);
        int index = slot_staff(sl);
        if (!index || sl[SL_STATE] || sl[SL_GROUP] != group || staff(s, index)[S_MOOD] < 10) continue;
        found = index;
        *slot_out = i;
        sim_rating_record(s, sl); /* [0x4514]: the last qualifying slot, not the staff person */
    }
    return found;
}

int sim_assignable(Sim *s, int h, int group, int *slot_out) {
    int found = sim_idle_candidate(s, h, group, slot_out);
    if (found) return found;
    int count = 0;
    for (int i = 0; i < STAFF_SLOTS; i++) {
        const uint8_t *sl = slot(s, h, i);
        if (!get16(sl + SL_INDEX) || sl[SL_GROUP] != group) continue;
        if (sl[SL_STATE] == RESTING) return 0;
        count++;
    }
    if (GROUP_CAP[group] && count >= GROUP_CAP[group]) return 0;
    uint8_t *r = hospital(s, h);
    if (r[0xE0]) return 0;
    for (int j = 0; j < JOB_OPENINGS; j++)
        if (opening(s, j)[0] == h && opening(s, j)[1] == group) return 0;
    r[0xE0] = (uint8_t)group;
    return 0;
}

void sim_assign(Sim *s, int h, int index, int sl_index, int cat, int room, uint8_t *field) {
    uint8_t *sl = slot(s, h, sl_index);
    put16(field, index);
    sl[SL_STATE] = (uint8_t)cat;
    sl[SL_ROOM] = (uint8_t)room;
    staff(s, index)[S_STATE] = (uint8_t)cat;
    staff(s, index)[S_ROOM] = (uint8_t)room;
}

/* 0x22EDD */
void sim_release(Sim *s, int h, int index) {
    uint8_t *st = staff(s, index);
    st[S_STATE] = 0;
    st[S_ROOM] = 0;
    for (int i = 0; i < STAFF_SLOTS; i++) {
        uint8_t *sl = slot(s, h, i);
        if (get16(sl + SL_INDEX) != index) continue;
        int g = sl[SL_GROUP];
        int off = g == 1 || g == 3 || g == 4 || g == 6 || g == 8 || st[7] - st[5] >= 19;
        sl[SL_STATE] = off ? OFF_DUTY : 0;
        st[S_STATE] = sl[SL_STATE];
        sl[SL_HOURS] = sl[SL_ON_DUTY] = sl[SL_OFF_DUTY] = sl[SL_ROOM] = 0;
        return;
    }
}

/* 0x2301C (docs/sim-bookkeeping.md, "Shared helpers"): the employee leaves the room and becomes idle, whatever the
 * job group; unlike the evening release 0x22EDD there is no off-duty branch, and slot +0x0A / +0x0B are kept. The
 * caller has already cleared the room's staff field. */
void sim_leave_room(Sim *s, int h, int index) {
    uint8_t *st = staff(s, index);
    st[S_STATE] = 0;
    st[S_ROOM] = 0;
    for (int i = 0; i < STAFF_SLOTS; i++) {
        uint8_t *sl = slot(s, h, i);
        if (get16(sl + SL_INDEX) != index) continue;
        sl[SL_STATE] = sl[SL_ROOM] = sl[SL_HOURS] = 0;
        return;
    }
}

/* ---- the store room -------------------------------------------------------------------------------- */

static uint8_t *store(Sim *s, int h) { return sim_room_record(s, h, 4, 0); }

static int store_has_room(const uint8_t *st) { return st[STORE_CAPACITY] - st[STORE_FURNITURE_HELD] != st[STORE_EQUIPMENT_HELD]; }

/* 0x2AF09: close the first gap (id 0) of one list of n slots; the ids and conditions behind it move one place down
 * and the last slot becomes 0. Only one gap per call; a gap already in the last slot is untouched (SIM-76). */
void sim_close_gap(uint8_t *ids, uint8_t *cond, int n) {
    int k = 0;
    while (k < n && get16(ids + 2 * k)) k++;
    if (k >= n - 1) return;
    memmove(ids + 2 * k, ids + 2 * (k + 1), (size_t)(2 * (n - 1 - k)));
    memmove(cond + k, cond + k + 1, (size_t)(n - 1 - k));
    put16(ids + 2 * (n - 1), 0);
    cond[n - 1] = 0;
}
static void close_first_gap(uint8_t *ids, uint8_t *cond) { sim_close_gap(ids, cond, STORE_SLOTS); }

/* 0x2A873, the store tidy-up (docs/ui-purchasing.md): for every hospital with a store room, the first gap of the
 * furniture list and of the equipment list is closed; then, in the equipment list from slot 0 up to the first
 * empty slot, a meal (first capability 0x2B) swaps places (id and condition) with the next item behind it whose
 * first capability is not 0x2B, so meals end up behind the other equipment. Engine choice: the search for that
 * next item also stops at the first empty slot. The meal pass covers the first 119 slots (a meal in slot 119 has nothing
 * behind it, so 119 and 120 act the same); the English CD's covers 118 (docs/editions.md 7.3). */
void sim_store_tidy(Sim *s) {
    int meal_slots = program_english(s->prog) ? 118 : 119;
    for (int h = 0; h < 16; h++) {
        if (!room_counts(s, h)[4]) continue;
        uint8_t *st = store(s, h);
        close_first_gap(st + STORE_FURNITURE, st + STORE_FURNITURE_COND);
        close_first_gap(st + STORE_EQUIPMENT, st + STORE_EQUIPMENT_COND);
        for (int i = 0; i < meal_slots && get16(st + STORE_EQUIPMENT + 2 * i); i++) {
            if (sim_equipment_item(s, (uint16_t)get16(st + STORE_EQUIPMENT + 2 * i))[10] != 0x2B) continue;
            for (int j = i + 1; j < STORE_SLOTS && get16(st + STORE_EQUIPMENT + 2 * j); j++) {
                if (sim_equipment_item(s, (uint16_t)get16(st + STORE_EQUIPMENT + 2 * j))[10] == 0x2B) continue;
                uint16_t id = (uint16_t)get16(st + STORE_EQUIPMENT + 2 * i);
                uint8_t c = st[STORE_EQUIPMENT_COND + i];
                put16(st + STORE_EQUIPMENT + 2 * i, get16(st + STORE_EQUIPMENT + 2 * j));
                st[STORE_EQUIPMENT_COND + i] = st[STORE_EQUIPMENT_COND + j];
                put16(st + STORE_EQUIPMENT + 2 * j, id);
                st[STORE_EQUIPMENT_COND + j] = c;
                break;
            }
        }
    }
}

/* 0x2BFB7, the list tidy-up (docs/mainloop.md, tick 64 row): category by category (2-18), for hospitals 0-15 and every
 * room of the category, the first gap of the furniture list and of the equipment list is closed; category 4 is the
 * store tidy-up 0x2A873. */
void sim_tidy_lists(Sim *s) {
    for (int cat = 2; cat <= 18; cat++) {
        if (cat == 4) {
            sim_store_tidy(s);
            continue;
        }
        Description d = sim_description(s, cat);
        for (int h = 0; h < 16; h++)
            for (int i = 0; i < sim_rooms_in_use(s, h, cat); i++) {
                uint8_t *rec = sim_room_record(s, h, cat, i);
                sim_close_gap(rec + d.furniture, rec + d.cond_a, d.furniture_slots);
                sim_close_gap(rec + d.equipment, rec + d.cond_b, d.equipment_slots);
            }
    }
}

/* 0x2B210: the per-category recomputes in the order 2, 3, 5 ... 18, without wear (docs/mainloop.md, tick 64 row) */
void sim_recompute_all(Sim *s) {
    SimRatingRef saved = s->rating_ref;
    for (int cat = 2; cat <= 18; cat++) sim_recompute(s, cat); /* sim_recompute skips category 4 */
    s->rating_ref = saved;
}

/* 0x2B207: the same recomputes with the wear switch [0x5BC0] on, so every room record also gets its wear 0x2B5B3 */
void sim_recompute_all_wear(Sim *s) {
    SimRatingRef saved = s->rating_ref;
    for (int cat = 2; cat <= 18; cat++) sim_recompute_wear(s, cat, 1);
    s->rating_ref = saved;
}

/* The store write of 0x2F610 / 0x391D2: the first slot with id 0 of the list. The search has no failure path: with
 * all 120 slots taken it writes one past the list (docs/ui-purchasing.md, reproduced): equipment id over +0x1EA and
 * the condition over byte 0 of the next hospital's store record, furniture id over the first equipment slot and the
 * condition over the first equipment condition. Engine guard: a write that would leave the block is skipped. */
static void store_put(Sim *s, uint8_t *st, int ids, int conds, int id, int condition) {
    int k = 0;
    while (k < STORE_SLOTS && get16(st + ids + 2 * k)) k++;
    uint8_t *base = s->world->block[BLK_STORES], *end = base + s->world->size[BLK_STORES];
    if (st + ids + 2 * k + 2 <= end) put16(st + ids + 2 * k, id);
    if (st + conds + k < end) st[conds + k] = (uint8_t)condition;
}

int sim_buy_offer(Sim *s, int buyer, int i, int equipment, int player) {
    uint8_t *o = s->world->block[equipment ? BLK_EQUIPMENT_MARKET : BLK_FURNITURE_MARKET] + i * 8;
    uint8_t *st = store(s, buyer), *r = hospital(s, buyer);
    int price = o[1];
    if (player && price > get32(r + H_CASH)) { /* 518 / 1621; cash equal to the price is enough */
        o[5] = 0;
        return BUY_NO_CASH;
    }
    st[equipment ? STORE_EQUIPMENT_HELD : STORE_FURNITURE_HELD]++;
    store_put(s, st, equipment ? STORE_EQUIPMENT : STORE_FURNITURE, equipment ? STORE_EQUIPMENT_COND : STORE_FURNITURE_COND,
              (uint16_t)get16(o + 2), o[4]);
    int result = BUY_DONE;
    add32(r + H_CASH, -price);
    if (get32(r + H_CASH) < 0) { /* refund rule: the item is free, nothing is booked, nobody is paid */
        add32(r + H_CASH, price);
        result = BUY_FREE;
    } else {
        uint8_t *seller = hospital(s, o[0]);
        add32(seller + H_CASH, price);
        if (o[0] == buyer) {
            result = BUY_OWN; /* the buyer's own offer: no counters */
        } else if (equipment) {
            add16(r + 0xAE, 1), add32(r + 0xBA, price), add32(r + 0xBE, price);
            add16(seller + 0xB0, 1), add32(seller + 0xCA, price), add32(seller + 0xCE, price);
        } else {
            add16(r + 0xB4, 1), add32(r + 0xC2, price), add32(r + 0xC6, price);
            add16(seller + 0xB6, 1), add32(seller + 0xD2, price), add32(seller + 0xD6, price);
        }
    }
    o[0] = 0xFF;
    if (player && equipment && result != BUY_OWN) sim_store_tidy(s); /* 0x2F610 only, the player only */
    return result;
}

/* 0x390BA (docs/sim-ai.md): buy a furniture offer of the wanted kind h+0xDC through 0x391D2 */
static void furniture_purchase(Sim *s) {
    int h = sim_pick(s);
    if (!h || !room_counts(s, h)[4]) return;
    uint8_t *r = hospital(s, h), *st = store(s, h);
    if (!store_has_room(st) || !r[0xDC]) return;
    /* [0x45CF] = the wanted kind (0x39114), then the market search 0x3913A ends with 1 (bought) or 0 (SIM-55) */
    s->dup_byte = r[0xDC];
    uint8_t *market = s->world->block[BLK_FURNITURE_MARKET];
    for (int i = 0; i < MARKET_OFFERS; i++) {
        uint8_t *o = market + i * 8;
        if ((int8_t)o[0] < 0 || o[5] || sim_furniture_item(s, (uint16_t)get16(o + 2))[6] != r[0xDC]) continue;
        sim_buy_offer(s, h, i, 0, 0);
        r[0xDC] = 0;
        s->dup_byte = 1;
        return;
    }
    s->dup_byte = 0;
}

/* 0x2F4C9 / 0x2F578 with the purchase 0x2F610: the first unreserved equipment offer with capability id (or of
 * kind id), first without the dealer's offers and then with them, into the store (sim_buy_offer).
 * 0x2F4C9 skips an item whose first capability is 0x2B (a meal) unless its catalog bytes +4 and +5 are both
 * non-zero (docs/sim-staff.md, read at 0x2F508-0x2F518); 0x2F578 (by kind) has no such test. */
static int buy_equipment(Sim *s, int h, int id, int by_kind) {
    /* [0x45CF] = the wanted id (0x2F461 / 0x2F4A3), and the purchase ends with 1 (bought) or 0 (SIM-55) */
    s->dup_byte = (uint8_t)id;
    uint8_t *market = s->world->block[BLK_EQUIPMENT_MARKET];
    for (int round = 0; round < 2; round++)
        for (int i = 0; i < MARKET_OFFERS; i++) {
            uint8_t *o = market + i * 8;
            if ((int8_t)o[0] < 0 || o[5] || (round == 0 && o[0] == DEALER)) continue;
            const uint8_t *it = sim_equipment_item(s, (uint16_t)get16(o + 2));
            if (by_kind ? it[6] != id : (it[10] != id && it[11] != id && it[12] != id && it[13] != id)) continue;
            if (!by_kind && it[10] == 0x2B && !(it[4] && it[5])) continue; /* a meal needs catalog +4 and +5 */
            sim_buy_offer(s, h, i, 1, 0);
            s->dup_byte = 1;
            return 1;
        }
    s->dup_byte = 0;
    return 0;
}

/* 0x2F3FF: the store room buys the equipment its rooms asked for (h+0xDA capability, h+0xDB kind) */
static void equipment_purchase(Sim *s) {
    int h = sim_pick(s);
    if (!h || !room_counts(s, h)[4]) return;
    uint8_t *r = hospital(s, h), *st = store(s, h);
    if (store_has_room(st) && r[0xDA] && buy_equipment(s, h, r[0xDA], 0)) r[0xDA] = 0;
    if (store_has_room(st) && r[0xDB] && buy_equipment(s, h, r[0xDB], 1)) r[0xDB] = 0;
}

/* 0x1F9B9: the clerk */
static void store_clerk(Sim *s) {
    World *w = s->world;
    int h = sim_pick(s);
    if (!h || !room_counts(s, h)[4]) return;
    uint8_t *st = store(s, h);
    int hour = world_tail(w, TAIL_HOUR), played = world_tail(w, TAIL_HOURS);
    int clerk = get16(st + STORE_CLERK);
    if (!clerk) {
        if (played < 5 || (hour >= 4 && hour <= 19)) {
            int k, who = sim_assignable(s, h, 3, &k);
            if (who) sim_assign(s, h, who, k, 4, 0, st + STORE_CLERK);
        }
    } else if (hour >= 19 &&
               !(hour <= 21 && (get16(sim_room_record(s, h, 2, 0) + 8) || sim_waiting_for(s, h, 5) ||
                                sim_waiting_for(s, h, 11) || sim_waiting_for(s, h, 15)))) {
        /* hours 19-21 keep the clerk while the reception (0x1FB7B) has its receptionist (+8) or someone waits for a
         * treatment room, dental ward or neurology; the store's own +8 is not read (docs/sim-drift9.md section 4) */
        put16(st + STORE_CLERK, 0);
        sim_release(s, h, clerk);
    }
}

/* 0x22F65 */
static void clerk_mood(Sim *s) {
    for (int h = 0; h < 16; h++) {
        if (!room_counts(s, h)[4]) continue;
        uint8_t *st = store(s, h);
        int clerk = get16(st + STORE_CLERK);
        if (!clerk) continue;
        uint8_t *c = staff(s, clerk);
        if (c[S_MOOD] != 100 && sim_bits(s, 7) == 3) c[S_MOOD]++;
        if (c[S_MOOD] < 4) {
            put16(st + STORE_CLERK, 0);
            sim_leave_room(s, h, clerk); /* 0x2301C (docs/sim-bookkeeping.md) */
        }
    }
}

/* ---- room operators: staffing and fittings -------------------------------------------------------- */

/* 0x2B0FB: the rating a room should reach */
static int rating_threshold(Sim *s, int h) {
    World *w = s->world;
    int32_t c = cash_of(s, h), x = get32(hospital(s, h) + 0xE2);
    int month = world_tail(w, TAIL_MONTH), d = world_tail(w, TAIL_DAY), hour = world_tail(w, TAIL_HOUR);
    if (d == 1) return hour >= 8 && hour <= 11 && c >= 200 ? 15 : 10;
    if (x < -250 || c < 250) return 10;
    if (month == 1 && c < 1000 && d < 6) return 25;
    if (month == 1 && d < 10) return c < 700 ? 25 : c < 1100 ? 35 : c < 1400 ? 45 : 55;
    return c < 700 ? 25 : c < 1200 ? 45 : c < 1500 ? 55 : c < 1900 ? 65 : c < 2400 ? 75 : 85;
}

/* ---- making room: the sales 0x2A59A (furniture) and 0x2A499 (equipment), docs/sim-staff.md ---------------- */

static int first_free_offer(const uint8_t *market) {
    for (int i = 0; i < 300; i++)
        if ((int8_t)market[i * 8] < 0) return i;
    return -1; /* engine choice: the first free offer among all 300 (the spec names no range) */
}

int sim_offer_price(int value) {
    int price = (value + 2) / 2;
    /* FIX-16 (docs/fixes.md): original keeps (value + 2) / 2 as a byte (600 -> 45); fixed caps it at 255 */
    if (fix_on(16)) return price > 255 ? 255 : price;
    return (uint8_t)price;
}

static void put_offer(uint8_t *o, int seller, int price, int id, int condition) {
    o[0] = (uint8_t)seller;
    o[1] = (uint8_t)price;
    put16(o + 2, id);
    o[4] = (uint8_t)condition;
    o[5] = 0;
    put16(o + 6, 0);
}

/* 0x2A59A: 1 when the room's furniture list has a free slot, if need be after an AI hospital sold one item onto
 * the furniture market: up to 60 tries of a random slot whose item is not of kind 1, 5 or 8 and whose two
 * ratings (catalog +4, +5) sum below the limit 3, 4, 5, 6, 7 for tries 1-10, 11-20 ... 41-50 and 60 for tries
 * 51-60 (engine choice: the spec's "3 for the first 10 tries ... 7, then 60" read as bands of ten). Price byte =
 * (price class + 2) / 2, condition kept. Hospital 0 never sells. Engine choice: with no free offer the item
 * leaves the room all the same, as in 0x2A499. */
static int furniture_sale(Sim *s, int h, int cat, uint8_t *rec) {
    Description d = sim_description(s, cat);
    for (int f = 0; f < d.furniture_slots; f++)
        if (!get16(rec + d.furniture + 2 * f)) return 1;
    if (h == 0 || d.furniture_slots <= 0) return 0;
    /* SIM-53 (docs/sim-staff.md, "Three more rules of the sales"): the 60th failed try sells the slot of its draw all
     * the same, so a full AI list always loses an item; and the sale does not recompute the room (bytes 0, 1 stay). */
    int k = 0, id = 0;
    const uint8_t *it = NULL;
    for (int tries = 0; tries < 60; tries++) {
        int limit = tries < 50 ? 3 + tries / 10 : 60;
        k = (int)sim_random(s, (uint32_t)d.furniture_slots);
        id = (uint16_t)get16(rec + d.furniture + 2 * k);
        it = sim_furniture_item(s, id);
        if (!(it[6] == 1 || it[6] == 5 || it[6] == 8 || it[4] + it[5] >= limit)) break;
    }
    uint8_t *market = s->world->block[BLK_FURNITURE_MARKET];
    int o = first_free_offer(market);
    if (o >= 0) put_offer(market + o * 8, h, sim_offer_price((uint16_t)get16(it + 8)), id, rec[d.cond_a + k]);
    put16(rec + d.furniture + 2 * k, 0);
    return 1;
}

/* 0x2A499: as 0x2A59A for equipment, with any slot (random(slots), no kind, rating or try limit). The price byte
 * is (u16 + 2) / 2 of the FOLLOWING market offer's bytes 0-1 - a fault of the original, reproduced (engine
 * choice: behind the last offer that u16 is 0). With no free offer the item is removed all the same and lost.
 * Returns 1 when the list has a free slot, 0 only for a full list of hospital 0. */
static int equipment_sale(Sim *s, int h, int cat, uint8_t *rec) {
    Description d = sim_description(s, cat);
    for (int i = 0; i < d.equipment_slots; i++)
        if (!get16(rec + d.equipment + 2 * i)) return 1;
    if (h == 0 || d.equipment_slots <= 0) return 0;
    int k = (int)sim_random(s, (uint32_t)d.equipment_slots);
    uint8_t *market = s->world->block[BLK_EQUIPMENT_MARKET];
    int o = first_free_offer(market);
    if (o >= 0) {
        int id = (uint16_t)get16(rec + d.equipment + 2 * k);
        int next = o + 1 < 300 ? (uint16_t)get16(market + (o + 1) * 8) : 0;
        /* FIX-17 (docs/fixes.md): original takes the price from the following offer's bytes 0-1; fixed from the
         * item's catalog price class, as the furniture sale */
        int value = fix_on(17) ? (uint16_t)get16(sim_equipment_item(s, id) + 8) : next;
        put_offer(market + o * 8, h, sim_offer_price(value), id, rec[d.cond_b + k]);
    }
    put16(rec + d.equipment + 2 * k, 0); /* no recompute after the sale (SIM-53) */
    return 1;
}

int sim_equipment_room(Sim *s, int h, int cat, uint8_t *rec) { return equipment_sale(s, h, cat, rec); }

/* Fittings rule for one room record of category cat with its wanted-furniture byte at want. */
static void fittings(Sim *s, int h, int cat, uint8_t *rec, uint8_t *want) {
    if (!room_counts(s, h)[4]) return;
    if (*want) {
        Description d = sim_description(s, cat);
        if (!furniture_sale(s, h, cat, rec)) return; /* 0x2A59A: no free slot and nothing sold */
        int free_slot = -1;
        for (int f = 0; f < d.furniture_slots && free_slot < 0; f++)
            if (!get16(rec + d.furniture + 2 * f)) free_slot = f;
        uint8_t *st = store(s, h);
        for (int k = 0; k < STORE_SLOTS; k++) {
            int id = (uint16_t)get16(st + STORE_FURNITURE + 2 * k);
            if (!id || sim_furniture_item(s, id)[6] != *want) continue;
            put16(rec + d.furniture + 2 * free_slot, id); /* 0x2B003 */
            rec[d.cond_a + free_slot] = st[STORE_FURNITURE_COND + k];
            put16(st + STORE_FURNITURE + 2 * k, 0);
            st[STORE_FURNITURE_HELD]--;
            *want = 0;
            sim_store_tidy(s); /* 0x2B003: 0x2A873, 0x2B210, 0x2A873 (every category recounted, beds included) */
            sim_recompute_all(s);
            sim_store_tidy(s);
            return;
        }
        uint8_t *r = hospital(s, h);
        if (!r[0xDC]) r[0xDC] = *want;
        return;
    }
    sim_rating_record(s, rec); /* only the store-present, no-request comparison writes [0x4514] */
    if (rec[0] + rec[1] < rating_threshold(s, h)) *want = sim_bits(s, 1) == 0 ? 6 : 2;
}
void sim_fittings(Sim *s, int h, int cat, uint8_t *rec, uint8_t *want) { fittings(s, h, cat, rec, want); }
int sim_rating_threshold(Sim *s, int h) { return rating_threshold(s, h); }
int sim_furniture_room(Sim *s, int h, int cat, uint8_t *rec) { return furniture_sale(s, h, cat, rec); }

/* An equipment need of room rec (docs/sim-rooms2.md, "Store-room helpers"): 0x2D003 (by capability) or 0x2A7C4
 * (by equipment kind, catalog byte +6) takes the first match among the store's first +9 equipment slots, and
 * 0x2B07E installs it in the room's first free equipment slot with its condition. Without a match h+0xDA
 * (capability) or h+0xDB (kind) is set, when 0, for the store-room purchase. First 0x2A499 makes room in a full
 * list (an AI hospital sells an item). Returns 1 when installed, 0 when not, -1 when the list is full and nothing
 * was sold (hospital 0): then neither the store nor the request is touched. */
int sim_store_supply(Sim *s, int h, int cat, uint8_t *rec, int id, int by_kind) {
    if (!room_counts(s, h)[4]) return 0;
    if (!equipment_sale(s, h, cat, rec)) return -1;
    Description d = sim_description(s, cat);
    int free_slot = -1;
    for (int i = 0; i < d.equipment_slots && free_slot < 0; i++)
        if (!get16(rec + d.equipment + 2 * i)) free_slot = i;
    uint8_t *st = store(s, h), *r = hospital(s, h);
    for (int k = 0; k < st[STORE_EQUIPMENT_HELD] && k < STORE_SLOTS; k++) { /* the first +9 slots only (reproduce) */
        int item = (uint16_t)get16(st + STORE_EQUIPMENT + 2 * k);
        if (!item) continue;
        const uint8_t *it = sim_equipment_item(s, item);
        if (by_kind ? it[6] != id : (it[10] != id && it[11] != id && it[12] != id && it[13] != id)) continue;
        put16(rec + d.equipment + 2 * free_slot, item);
        rec[d.cond_b + free_slot] = st[STORE_EQUIPMENT_COND + k];
        put16(st + STORE_EQUIPMENT + 2 * k, 0);
        st[STORE_EQUIPMENT_HELD]--;
        sim_store_tidy(s); /* 0x2A873, called by the take at 0x2D0AE */
        int clerk = get16(st + STORE_CLERK);
        add16(staff(s, clerk) + 0x10, 1); /* SIM-77: the installer credits record 0 too (docs/sim-rooms2.md). */
        sim_store_tidy(s); /* 0x2B07E's recompute: 0x2A873, 0x2B210, 0x2A873 */
        sim_recompute_all(s);
        sim_store_tidy(s);
        return 1;
    }
    uint8_t *want = r + (by_kind ? 0xDB : 0xDA);
    if (!*want) *want = (uint8_t)id;
    return 0;
}


/* The five operators with known staff fields (docs/sim-staff.md table). */
static const struct {
    uint32_t routine;
    uint8_t cat, group8, group10, want;
} OPERATORS[] = {
    {0x23060, 2, 1, 0, 4}, {0x2C993, 3, 1, 0, 6}, {0x2CB75, 5, 1, 2, 4}, {0x34535, 11, 1, 9, 4}, {0x35430, 15, 1, 10, 4},
};

/* 0x22EDD for each staff field of the operator's room, +8 first */
static void release_staff(Sim *s, int h, int which, uint8_t *rec) {
    for (int f = 8; f <= (OPERATORS[which].group10 ? 10 : 8); f += 2) {
        int who = get16(rec + f);
        if (!who) continue;
        put16(rec + f, 0);
        sim_release(s, h, who);
    }
}

/* 0x23060, the reception operator, in the order of docs/sim-drift7.md section 2.2 (after the pick). */
static void reception_operator(Sim *s, int h, int which, uint8_t *rec, SimIn in, int hour, int played) {
    /* step 2: cash below 0 releases the receptionist (+0x45 cleared with it) before any draw, and ends */
    if (cash_of(s, h) < 0) {
        if (get16(rec + 8)) {
            release_staff(s, h, which, rec);
            rec[0x45] = 0;
        }
        return;
    }
    /* step 3: the fittings before every draw of the routine (their rare r1 with the operator's pair) */
    SimIn old = sim_set_inputs(s, in);
    fittings(s, h, 2, rec, rec + OPERATORS[which].want);
    sim_set_inputs(s, old);
    /* step 4: with r3 = 0 the +0x16E counter */
    if (sim_bits_in(s, 3, 0x23128, in) == 0) {
        uint8_t *r = hospital(s, h);
        add16(r + 0x16E, 1);
        if ((uint16_t)get16(r + 0x16E) >= 1000) add16(r + 0x172, 1), put16(r + 0x16E, 0);
    }
    int waiting = 0; /* the Krankenzimmer list at +0x46, six places */
    for (int k = 0; k < 6; k++) waiting |= get16(rec + 0x46 + 4 * k) != 0;
    /* step 5, unstaffed: the receptionist is asked for, and the call ends */
    if (!get16(rec + 8)) {
        int k, who;
        if ((played < 5 || waiting || (hour >= 4 && hour <= 18)) && (who = sim_assignable(s, h, OPERATORS[which].group8, &k)))
            sim_assign(s, h, who, k, 2, 0, rec + 8);
        return;
    }
    /* step 6, staffed: the r7 (docs/rng-sites.md 3.2); on 0 the comfort set when +0x45 is 0, r15 + 22 by the helper
     * 0x2B4A9 with the same pair. The copy-protection counter 0x2B47B before the test is not modelled. */
    if (sim_bits_in(s, 7, 0x231ED, in) == 0 && !rec[0x45]) rec[0x45] = rng_comfort(s, in);
    /* step 7, the evening release: from hour 19 with queue place 1 and the Krankenzimmer list empty; it ends the call */
    if (hour >= 19 && !get16(rec + 0x0A) && !waiting) {
        release_staff(s, h, which, rec);
        rec[0x45] = 0;
        return;
    }
    sim_rooms_after_operator(s, h, 2, 0); /* step 8: the supply request, queue place 1, the Krankenzimmer list */
}

/* The fittings step of the waiting-room and department operators, with the operator's pair for its rare r1 */
static void operator_fittings(Sim *s, int h, int cat, int which, uint8_t *rec, SimIn in) {
    SimIn old = sim_set_inputs(s, in);
    fittings(s, h, cat, rec, rec + OPERATORS[which].want);
    sim_set_inputs(s, old);
}

/* 0x2C993, the waiting-room operator (docs/sim-staff.md, docs/sim-drift3.md section 3) */
static void waiting_room_operator(Sim *s, int h, int which, uint8_t *rec, SimIn in, int hour, int played) {
    operator_fittings(s, h, 3, which, rec, in); /* first, right after the room pick (docs/sim-staff.md) */
    int waiting = rec[0x0C];
    int day_shift = hour >= 4 && hour <= 18;
    int reception_staffed = room_counts(s, h)[2] && get16(sim_room_record(s, h, 2, 0) + 8);
    if (!get16(rec + 8)) {
        int k, who;
        if ((played < 5 || waiting || (day_shift && reception_staffed)) &&
            (who = sim_assignable(s, h, OPERATORS[which].group8, &k)))
            sim_assign(s, h, who, k, 3, 0, rec + 8);
        return; /* an empty +8 ends the call, an assignment too (0x2CA69-0x2CAF9, SIM-61) */
    }
    /* the comfort counter (0x2CAFB, docs/sim-bookkeeping.md): with r7 = 0, when 0 and the room staffed (the staffed gate
     * measured in the oracle, 2026-10-19: 35 sets in 300 calls staffed, 0 unstaffed). The cash bonus that follows it in
     * the original while +0x172 >= 4 is copy protection and left out. */
    if (!rec[0x0F] && sim_bits_in(s, 7, 0x2CB06, in) == 0) rec[0x0F] = rng_comfort(s, in);
    /* the evening release: from hour 19, the room empty and the reception unstaffed (+0x0F cleared too) */
    if (hour >= 19 && !waiting && !reception_staffed) {
        release_staff(s, h, which, rec);
        rec[0x0F] = 0;
    }
}

/* 0x2CB75 / 0x34535 / 0x35430, the treatment-room, dental and neurology operators (docs/sim-drift7.md section 4.1): a
 * call that finds a staff field empty asks for that field and goes to the release check without the patient part;
 * with both staff the dental comfort r7, the supplies step and the patient part, and the release check when the room is
 * free and nobody waits. The release check frees each set field (+8 first; dental clears +0x19 with it) when nobody
 * waits for the room's kind, the hour is 19 or later and the reception is unstaffed. */
static void room_staff_operator(Sim *s, int h, int which, int room, uint8_t *rec, SimIn in, int hour, int played) {
    int cat = OPERATORS[which].cat;
    operator_fittings(s, h, cat, which, rec, in); /* first, right after the room pick (docs/sim-staff.md) */
    int waiting = sim_waiting_for(s, h, cat); /* 0x1B496 / 0x1B51F / 0x1B5A8 */
    int day_shift = hour >= 4 && hour <= 18;
    int reception_staffed = room_counts(s, h)[2] && get16(sim_room_record(s, h, 2, 0) + 8);
    int check, k, who;
    if (!get16(rec + 8)) {
        /* outside hours 4-18 the call goes to the release check unasked; inside them with an unstaffed reception it ends
         * (the release check, which needs hour 19 or later, does nothing there, so check = 1 is the same) */
        if ((played < 5 || waiting || (day_shift && reception_staffed)) &&
            (who = sim_assignable(s, h, OPERATORS[which].group8, &k)))
            sim_assign(s, h, who, k, cat, room, rec + 8);
        check = 1;
    } else if (!get16(rec + 10)) {
        if ((played < 5 || waiting || day_shift) && (who = sim_assignable(s, h, OPERATORS[which].group10, &k)))
            sim_assign(s, h, who, k, cat, room, rec + 10);
        check = 1;
    } else {
        /* the dental comfort: r7 = 4 with +0x19 at 0, the operator's pair (docs/rng-sites.md 3.2: drawn, then tested),
         * only with both staff (0x34726) */
        if (cat == 11 && sim_bits_in(s, 7, 0x3472B, in) == 4 && !rec[0x19]) rec[0x19] = rng_comfort(s, in);
        sim_rooms_supplies(s, h, cat, room); /* 0x2CD74 / 0x34754 / 0x35636 */
        check = sim_rooms_after_operator(s, h, cat, room);
    }
    if (check && !waiting && hour >= 19 && !reception_staffed) {
        int nurse = get16(rec + 8), doctor = get16(rec + 10);
        if (nurse) {
            put16(rec + 8, 0);
            sim_release(s, h, nurse);
            if (cat == 11) rec[0x19] = 0;
        }
        if (doctor) {
            put16(rec + 10, 0);
            sim_release(s, h, doctor);
        }
    }
}

static void operator(Sim *s, int which) {
    World *w = s->world;
    int cat = OPERATORS[which].cat;
    int h = sim_pick(s);
    if (!h) return;
    const uint8_t *c = room_counts(s, h);
    if (!c[cat] || (cat == 2 && !c[3])) return;
    /* context O (docs/rng-sites.md 3.2): the room pick with D = the scratch block (disk 0x6187, section 8), then D = the picked room's record */
    static const uint32_t PICK_SITE[16] = {[3] = 0x2C9D8, [5] = 0x2CBB5, [11] = 0x34575, [15] = 0x35470};
    int room = cat == 2 ? 0 : (int)sim_random_in(s, (uint32_t)sim_rooms_in_use(s, h, cat), PICK_SITE[cat],
                                                 rng_operator(s, h, NULL));
    uint8_t *rec = sim_room_record(s, h, cat, room);
    SimIn in = rng_operator(s, h, rec);
    int hour = world_tail(w, TAIL_HOUR), played = world_tail(w, TAIL_HOURS);
    if (cat == 2) reception_operator(s, h, which, rec, in, hour, played);
    else if (cat == 3) waiting_room_operator(s, h, which, rec, in, hour, played);
    else room_staff_operator(s, h, which, room, rec, in, hour, played);
}

/* ---- upkeep ----------------------------------------------------------------------------------------- */

/* 0x5B25D */
static void dismissals(Sim *s) {
    static const int LIMIT[11] = {0, 0, 0, 2, 2, 0, 4, 3, 3, 0, 0};
    for (int h = 1; h < 16; h++) {
        int count[11] = {0};
        for (int i = 0; i < STAFF_SLOTS; i++) {
            const uint8_t *sl = slot(s, h, i);
            if (get16(sl + SL_INDEX) && sl[SL_GROUP] <= 10) count[sl[SL_GROUP]]++;
        }
        s->need4 = (int16_t)count[4]; /* the table is the counter here: hospital 15's count is left (SIM-65) */
        for (int g = 3; g <= 8; g++) {
            if (!LIMIT[g] || count[g] <= LIMIT[g]) continue;
            for (int i = 0; i < STAFF_SLOTS; i++) { /* 0x5B355: one off-duty employee of the group */
                uint8_t *sl = slot(s, h, i);
                int index = slot_staff(sl);
                if (!index || sl[SL_GROUP] != g || sl[SL_STATE] != OFF_DUTY || staff(s, index)[S_STATE] != OFF_DUTY) continue;
                uint8_t *st = staff(s, index);
                st[S_HOSPITAL] = st[S_STATE] = 0xFF;
                st[S_ROOM] = st[20] = 0;
                st[S_DISMISSED_BY] = (uint8_t)h;
                st[S_RESERVED] = 0xFF;
                st[11] = st[23] = 0;
                put16(sl + SL_INDEX, 0);
                break;
            }
        }
    }
}

/* 0x2B718 */
static void hourly_upkeep(Sim *s) {
    for (int h = 0; h < 16; h++)
        for (int i = 0; i < STAFF_SLOTS; i++) {
            uint8_t *sl = slot(s, h, i);
            int index = slot_staff(sl);
            if (!index) continue;
            uint8_t *st = staff(s, index);
            sl[SL_HOURS]++;
            if ((uint16_t)get16(st + 16) > 30000) put16(st + 16, 30000);
            int g = st[S_GROUP];
            if (st[S_HOSPITAL] && (g == 6 || g == 8)) {
                if (sim_bits(s, 31) == 1 && st[S_SKILL] < 110) st[S_SKILL]++;
                add16(st + 16, 1);
                if (sim_bits(s, 15) == 1 && st[S_BUST] < 99) st[S_BUST]++;
            }
            int state = sl[SL_STATE];
            if (state == TRAINING) {
                sl[SL_REST] = 0;
                continue;
            }
            if (g != 1 && g != 3 && g != 4 && g != 6 && g != 8 && st[S_BUST] > 50) st[S_BUST] = 50;
            if (state == 0x16) {
                if (sl[SL_ON_DUTY] < sl[SL_HOURS]) {
                    sl[SL_STATE] = st[S_STATE] = OFF_DUTY;
                    sl[SL_HOURS] = sl[SL_ON_DUTY] = 0;
                    put16(sl + 16, 0);
                    sl[SL_ROOM] = 0;
                    st[S_ROOM] = 0;
                }
            } else if (state == OFF_DUTY) {
                sl[SL_OFF_DUTY]++;
                sl[SL_REST] = 0xFF;
            } else if (state != 0x17) {
                sl[SL_ON_DUTY]++;
            }
        }
}

/* 0x3091F (docs/formats.md, block B): doctors' byte 7 drops by 1 per hour, floor 0; records 0-1,040 */
static void doctors_byte7(Sim *s) {
    for (int i = 0; i < STAFF_RECORDS; i++) {
        uint8_t *st = staff(s, i);
        int g = st[S_GROUP];
        if ((int8_t)st[S_HOSPITAL] < 0 || g == 1 || g == 3 || g == 4 || g == 6 || g == 8) continue;
        st[7]--;
        if ((int8_t)st[7] < 0) st[7] = 0; /* also a value of 128 or more: signed negative */
    }
}

/* ---- training courses --------------------------------------------------------------------------- */

static uint8_t *course(Sim *s, int i) { return s->world->block[BLK_COURSES] + i * 10; }

static void new_course(Sim *s) { /* 0x42714 and 0x4282F */
    static const uint8_t PAIRS[10][2] = {{3, 1}, {1, 2}, {8, 3}, {10, 4}, {9, 5}, {2, 6}, {6, 7}, {4, 8}, {5, 9}, {7, 10}};
    if (sim_bits(s, 3) != 1) return;
    int i = 0;
    while (i < 20 && (int8_t)course(s, i)[0] >= 0) i++;
    if (i == 20) return;
    uint8_t *c = course(s, i);
    if (sim_bits(s, 7) == 0) {
        c[0] = 0;
        c[4] = sim_bits(s, 1) ? 11 : 0;
    } else {
        int r = (int)sim_random(s, 10);
        c[0] = PAIRS[r][0];
        c[4] = PAIRS[r][1];
    }
    int duration = program_u8(s->prog, COURSES, c[4] * COURSE_SIZE + 2u);
    c[2] = c[3] = (uint8_t)duration;
    int hour = world_tail(s->world, TAIL_HOUR);
    int start = (hour < 9 ? 9 : 34) - hour;
    if (start > 3) start -= (int)sim_bits(s, 3);
    start -= 2;
    c[1] = (uint8_t)(start < 2 ? 2 : start);
    for (int k = 0; k < 20; k++)
        if (k != i && (int8_t)course(s, k)[0] >= 0 && course(s, k)[4] == c[4]) {
            c[0] = 0xFF;
            return;
        }
}

static void courses_age(Sim *s) { /* 0x4302C */
    for (int i = 0; i < 20; i++) {
        uint8_t *c = course(s, i);
        if ((int8_t)c[0] < 0) continue;
        if (c[1]) c[1]--;
        else if ((int8_t)--c[3] < 0) c[0] = 0xFF;
    }
}

static void book_course(Sim *s) { /* 0x3DF91 */
    World *w = s->world;
    int h = sim_pick(s);
    int day = world_tail(w, TAIL_DAY), hour = world_tail(w, TAIL_HOUR);
    if (!h || day == 1 || hour == 3 || hour == 4 || hour == 6 || hour == 7 || sim_bits(s, 63) != 5) return;
    uint8_t *r = hospital(s, h);
    int limit = day <= 3 ? 0 : day <= 6 ? 1 : 2;
    if (r[0x14D] > limit) return;
    int32_t cash = get32(r + H_CASH), m = cash - (int32_t)sim_daily_costs(s, h);
    if (cash < 900 || (hour >= 12 && hour <= 14 && m <= -31) || (hour >= 15 && m <= 169)) return;
    /* The first idle slot for which a course fits, not only the first idle slot: measured with the oracle on
     * orig_day11 (hospital 13 skipped its idle slots 0, 3 and 6 of groups 1 and 2 and sent slot 17, group 8, on the
     * one open course for group 8). Engine choice: slots outside, courses inside. */
    for (int k = 0; k < STAFF_SLOTS; k++) {
        uint8_t *sl = slot(s, h, k);
        if (!get16(sl + SL_INDEX) || sl[SL_STATE] != 0) continue;
        for (int i = 0; i < 20; i++) {
            uint8_t *c = course(s, i);
            if ((int8_t)c[0] < 0 || !c[1] || (int8_t)c[3] <= 0 || (c[0] && c[0] != sl[SL_GROUP])) continue;
            int price = program_u8(s->prog, COURSES, c[4] * COURSE_SIZE + 9u);
            if (price < cash) {
                add32(r + H_CASH, -price);
                add32(r + 0x11C, price);
                add32(r + 0x13E, price);
                uint8_t *st = staff(s, get16(sl + SL_INDEX));
                sl[SL_STATE] = st[S_STATE] = TRAINING;
                sl[SL_ROOM] = st[S_ROOM] = (uint8_t)i;
                sl[SL_HOURS] = sl[SL_ON_DUTY] = sl[SL_OFF_DUTY] = 0;
                r[0x14D]++;
            }
            return;
        }
    }
}

/* ---- dispatch --------------------------------------------------------------------------------- */

/* ---- the per-minute pass (docs/sim-staff2.md) ------------------------------------------------------- */

enum { S_BYTE7 = 7, S_COURSE_SKILL = 12, S_GENERAL_SKILL = 13, S_STAMINA = 14, S_MARK = 23, SL_RETURN_CAT = 16,
       SL_RETURN_ROOM = 17, SL_REST_LEFT = 19, GOLF = 0x0C, AWAY = 0x16, EXHAUSTED = 0x17 };

/* the employee leaves its room and duty: off duty or idle, with the fields every transition clears */
static void set_state(uint8_t *st, uint8_t *sl, int state, int clear_hours) {
    st[S_MARK] = 0;
    if (clear_hours) sl[SL_HOURS] = sl[SL_ON_DUTY] = sl[SL_OFF_DUTY] = 0;
    st[S_STATE] = sl[SL_STATE] = (uint8_t)state;
    st[S_ROOM] = 0;
    sl[SL_ROOM] = 0;
}

/* the room remembered in slot +16/+17 */
static void room_return(Sim *s, int h, int index, uint8_t *st, uint8_t *sl) {
    int q = sl[SL_RETURN_CAT], i = sl[SL_RETURN_ROOM], woman = st[S_SEX] != 0, off;
    switch (q) {
    case 2: case 3: case 6: case 9: case 16: off = 8; break;
    case 4: off = 4; break;
    case 5: case 11: case 15: off = woman ? 8 : 10; break;
    case 7: off = woman ? 8 : st[S_GROUP] == 2 ? 6 : 10; break;
    case 8: off = woman ? 8 : st[S_GROUP] == 10 ? 10 : 6; break;
    case 13: off = 6; break;
    case 18: off = 10; break;
    default: return; /* no case: slot +16/+17 stay */
    }
    uint8_t *field = sim_room_record(s, h, q, i) + off;
    if (!get16(field)) {
        put16(field, index);
        st[S_STATE] = sl[SL_STATE] = (uint8_t)q;
        st[S_ROOM] = sl[SL_ROOM] = (uint8_t)i;
    } else {
        sl[SL_HOURS] = 0;
    }
    sl[SL_RETURN_CAT] = sl[SL_RETURN_ROOM] = 0;
}

/* 0x305E7: an idle employee with slot +18 set and 10 or more hours: a break in the club room, or rest at home */
static int club_break(Sim *s, int h) { /* step 2 of 0x305E7: 1 when the club takes the break */
    if (!room_counts(s, h)[14]) return 0;
    uint8_t *club = sim_room_record(s, h, 14, 0);
    int a = get16(club + 2), b = get16(club + 4);
    if (!a || !b) return 0;
    uint8_t score = (uint8_t)(club[0] + club[1] + club[24]);
    for (int k = 0; k < 2; k++) {
        const uint8_t *m = staff(s, k ? b : a);
        int bust = (int8_t)(uint8_t)(m[S_BUST] - 80);
        score = (uint8_t)(score + (bust < 0 ? 0 : bust) / 16 + m[S_COURSE_SKILL] + m[S_GENERAL_SKILL] + m[S_RATING]);
    }
    int v = (int8_t)score < 0 || score >= 70 ? 70 : score;
    if (v < (int)sim_bits(s, 63)) return 0;
    add16(staff(s, b) + 16, 1);
    add16(staff(s, a) + 16, 1);
    return 1;
}

static void leisure_or_rest(Sim *s, int h, uint8_t *st, uint8_t *sl) {
    if (!sl[SL_REST] || sl[SL_ON_DUTY] < 10) return;
    if (club_break(s, h)) {
        sl[SL_REST] = 0;
        return;
    }
    sl[SL_REST] = 0;
    st[S_STATE] = sl[SL_STATE] = RESTING;
    st[S_ROOM] = sl[SL_ROOM] = 0;
    int left = (int8_t)(uint8_t)(50 - 2 * st[S_STAMINA]);
    sl[SL_REST_LEFT] = (uint8_t)(left < 5 ? 5 : left);
    sl[SL_RETURN_CAT] = sl[SL_RETURN_ROOM] = 0;
}

/* 0x2E666: the break of an employee working in a room (staff field `field` of room cat/instance; `extra` is the
 * room's field the break clears). No slot holds the employee: the original loops for ever, the engine returns.
 * SIM-101: the live player's staff-selection view holds every hospital's pending working break. */
void sim_staff_break(Sim *s, int h, int cat, int instance, uint8_t *field, uint8_t *extra) {
    int index = get16(field), i = 0;
    while (i < STAFF_SLOTS && get16(slot(s, h, i)) != index) i++;
    if (!index || i == STAFF_SLOTS) return;
    uint8_t *sl = slot(s, h, i), *st = staff(s, index);
    if (!sl[SL_REST] || (int8_t)sl[SL_ON_DUTY] < 10) return;
    if (s->screen == 54) return;
    sl[SL_REST] = 0;
    if (club_break(s, h)) return;
    st[S_STATE] = sl[SL_STATE] = RESTING;
    st[S_ROOM] = 0;
    sl[SL_ROOM] = 0;
    put16(field, 0);
    int left = (int8_t)(uint8_t)(60 - 2 * st[S_STAMINA]);
    sl[SL_REST_LEFT] = (uint8_t)(left < 9 ? 9 : left);
    sl[SL_RETURN_CAT] = (uint8_t)cat;
    sl[SL_RETURN_ROOM] = (uint8_t)instance;
    if (extra) *extra = 0; /* NULL: the call has no extra field (DL = 0) */
}

/* the golf room: a man leaves when byte 7 reaches 100 (step 2) */
static void golf_leave(Sim *s, int h, int index, uint8_t *st, uint8_t *sl) {
    uint8_t *g = sim_room_record(s, h, 12, 0);
    if (st[S_BYTE7] != 100) {
        /* the golf room's draws with EBP = its record (context P, docs/rng-sites.md 3.8) */
        SimIn old = s->rng_in;
        if (old.on) sim_set_inputs(s, sim_in(old.d, sim_address(s, g), sim_address(s, hospital(s, h))));
        int d = (int)sim_bits_site(s, 15, 0x2FD0D);
        if (get16(g + 2)) d = (int)sim_bits_site(s, 1, 0x2FD19);
        sim_set_inputs(s, old);
        if (d || ++st[S_BYTE7] != 100) return;
    }
    int k = 0;
    while (k < 9 && get16(g + 6 + 2 * k) != index) k++;
    if (k == 9) return; /* the original hangs here */
    put16(g + 6 + 2 * k, 0);
    g[5]--;
    st[S_MOOD] = 100;
    if (h && world_tail(s->world, TAIL_HOUR) >= 19) set_state(st, sl, OFF_DUTY, 1);
    else set_state(st, sl, 0, 0);
    if (get16(g + 2)) add16(staff(s, get16(g + 2)) + 16, 1);
}

/* step 4: an idle man of a doctors' group joins the golf room; returns 1 when he joined */
static int golf_join(Sim *s, int h, int index, uint8_t *st, uint8_t *sl) {
    int grp = st[S_GROUP];
    if (!room_counts(s, h)[12] || st[S_STATE] || st[S_SEX] || grp == 3 || grp == 4 || grp == 6 || grp == 8) return 0;
    if (s->screen == 54) return 0;
    uint8_t *g = sim_room_record(s, h, 12, 0);
    /* SIM-107: late refusals retain the selected golf record for this employee's mood draws. */
    sim_set_inputs(s, sim_in(sim_address(s, st), sim_address(s, g), sim_address(s, hospital(s, h))));
    int tired = (int8_t)(uint8_t)(st[S_BYTE7] - st[S_BUST]);
    if (!get16(g + 2) || ((int8_t)st[S_BYTE7] >= 90 && tired >= 20)) return 0;
    if ((int8_t)(uint8_t)(g[4] - g[5]) <= 0) return 0;
    int k = 0;
    while (k < 9 && get16(g + 6 + 2 * k)) k++;
    if (k == 9) return 0; /* the original hangs here */
    put16(g + 6 + 2 * k, index);
    g[5]++;
    st[S_STATE] = sl[SL_STATE] = GOLF;
    st[S_ROOM] = sl[SL_ROOM] = 0;
    return 1;
}

/* step 5: a finished training course */
static void course_end(Sim *s, int h, int index, uint8_t *st, uint8_t *sl) {
    const uint8_t *c = course(s, st[S_ROOM]);
    if (c[3]) return;
    const uint8_t *k = program_table(s->prog, COURSES) + c[4] * COURSE_SIZE;
    int g = k[4];
    if (h) {
        int early = world_tail(s->world, TAIL_MONTH) == 1 && world_tail(s->world, TAIL_DAY) < 4;
        g += (int)sim_bits(s, 1);
        if (!early) g += (int)sim_bits(s, 1);
    }
    int at = c[0] ? S_COURSE_SKILL : S_GENERAL_SKILL;
    st[at] = (uint8_t)(st[at] + g);
    if ((int8_t)st[at] >= 120) st[at] = 119;
    add16(st + 16, k[5]);
    st[S_RATING] = (uint8_t)(st[S_RATING] + k[6]);
    if ((int8_t)st[S_RATING] >= 120) st[S_RATING] = 120;
    st[S_SKILL] = (uint8_t)(st[S_SKILL] + k[7]);
    if ((int8_t)st[S_SKILL] >= 120) st[S_SKILL] = 120;
    int grp = st[S_GROUP];
    if (grp != 1 && grp != 2 && grp != 5 && grp != 7 && grp != 9 && grp != 10) {
        st[S_BUST] = (uint8_t)(st[S_BUST] + k[12]);
        if ((int8_t)st[S_BUST] >= 120) st[S_BUST] = 120;
    }
    set_state(st, sl, 0, 1);
    sim_news_course(s, h, index, c[4]); /* 0x495A0 */
}

static void mood_add(uint8_t *st, int d) { st[S_MOOD] = (uint8_t)(st[S_MOOD] + d); }

/* step 9, working rules: returns 1 if the mood falls ("loss"), 0 no change, 2 a rise */
int sim_mood_working(Sim *s, StaffId employee, StaffSlotId slot_id) {
    StaffRecordView st = staff_record_view(s->world, employee), sl = staff_slot_view(s->world, slot_id);
    staff_record_link(employee, sl);
    int w = staff_field_u8(st, STAFF_MOOD_WAGE), d = staff_field_u8(st, STAFF_MOOD_DEMAND);
    int hrs = staff_field_u8(sl, STAFF_SLOT_HOURS), stamina = staff_field_u8(st, STAFF_MOOD_STAMINA);
    if (w == d) { /* SIM-24: both compares take the hours on duty as a signed byte (docs/sim-staff2.md) */
        /* FIX-29 (docs/fixes.md): original compares the hours as a signed byte (from 128 hours the mood rises);
         * fixed compares them unsigned, as the W > D path */
        int sh = fix_on(29) ? hrs : (int8_t)hrs;
        if ((int)sim_bits(s, 7) >= sh) return 2;
        if ((int)sim_bits(s, 15) + 16 >= sh) return 0;
        return (int)sim_bits(s, 63) < stamina ? 0 : 1;
    }
    if (w > d) {
        int e = 2 * (w - d);
        if (hrs >= 24) return 1;
        if (e >= hrs) return 2;
        if (e < (int)sim_bits_site(s, 15, 0x300B0)) return 2;
        return e < (int)sim_bits_site(s, 15, 0x300BA) ? 0 : 1;
    }
    int f = 8 * (d - w) + hrs;
    if (f < (int)sim_bits(s, 31)) return 0;
    return (int)sim_bits(s, 63) < stamina ? 0 : 1;
}

/* step 9, off-duty rules: 1 if the mood rises */
static int mood_off_duty(Sim *s, const uint8_t *st, const uint8_t *sl) {
    int w = st[S_WAGE], d = st[S_DEMAND], off = sl[SL_OFF_DUTY], stamina = st[S_STAMINA];
    if (w == d) {
        if (off < 4) return 0;
        if ((int)sim_bits(s, 15) + 5 < off) return 1;
        return (int)sim_bits(s, 63) < stamina;
    }
    if (w > d) return off >= 3 && (int)sim_bits(s, 31) < 4 * (w - d) + off;
    if (off < 6) return 0;
    if ((int)sim_bits(s, 31) + 8 < off) return 1;
    return (int)sim_bits(s, 63) < stamina;
}

static void clamp_mood(uint8_t *st) {
    if ((int8_t)st[S_MOOD] >= 100) st[S_MOOD] = 100;
    else if ((int8_t)st[S_MOOD] < 0) st[S_MOOD] = 0;
}

static void minute_employee(Sim *s, StaffId employee, StaffSlotId slot_id) {
    int h = slot_id.hospital, index = employee.value;
    uint8_t *st = staff_record_view(s->world, employee).data, *sl = staff_slot_view(s->world, slot_id).data;
    if (st[S_STATE] == 0) leisure_or_rest(s, h, st, sl);
    if (st[S_STATE] == GOLF && st[S_SEX] == 0) golf_leave(s, h, index, st, sl);
    if (golf_join(s, h, index, st, sl)) return;
    if (st[S_STATE] == TRAINING) {
        course_end(s, h, index, st, sl);
        return;
    }
    if (sl[SL_STATE] == RESTING) {
        if (sl[SL_REST_LEFT] && --sl[SL_REST_LEFT]) return;
        sl[SL_REST] = 0;
        st[S_MOOD] = 100;
        set_state(st, sl, 0, 0);
        room_return(s, h, index, st, sl);
        return;
    }
    if (sl[SL_STATE] == EXHAUSTED) {
        if (world_tail(s->world, TAIL_HOUR) < 19) return;
        sl[SL_HOURS] = 1, sl[SL_ON_DUTY] = 0, sl[SL_OFF_DUTY] = 1;
        st[S_STATE] = sl[SL_STATE] = OFF_DUTY;
        st[S_ROOM] = sl[SL_ROOM] = 0;
        st[S_BYTE7] = (uint8_t)(sim_bits(s, 31) + 60);
        return;
    }
    if (sl[SL_STATE] == AWAY) return;
    if (st[S_STATE] != OFF_DUTY) {
        int grp = sl[SL_GROUP];
        if (sl[SL_REST] && (grp == 3 || grp == 4 || grp == 6 || grp == 8)) sl[SL_REST] = 0;
        int r = sim_mood_working(s, employee, slot_id);
        if (r == 2) mood_add(st, 1);
        else if (r == 1) mood_add(st, st[S_GROUP] == 4 ? -3 : -1);
        clamp_mood(st);
        if (st[S_MOOD] < 5) {
            if (st[S_STATE] == 0) { /* off duty; byte 23 and slot +16/+17 are kept */
                sl[SL_HOURS] = sl[SL_ON_DUTY] = sl[SL_OFF_DUTY] = 0;
                st[S_STATE] = sl[SL_STATE] = OFF_DUTY;
                st[S_ROOM] = sl[SL_ROOM] = 0;
            } else {
                st[S_MOOD] = 0;
            }
        }
        return;
    }
    if (st[S_MARK] == 0xF9) {
        if (st[S_MOOD] >= 85) st[S_MARK] = 0;
        else if (sim_bits(s, 3) != 2) return;
    }
    if (mood_off_duty(s, st, sl)) mood_add(st, 1);
    clamp_mood(st);
    if (sl[SL_OFF_DUTY] < 8 || st[S_MOOD] < 95 || sim_bits(s, 31) != 13) return;
    if (st[S_MARK] >= 0xFA && st[S_MARK] <= 0xFE) return;
    int grp = st[S_GROUP];
    int doctor = grp != 1 && grp != 3 && grp != 4 && grp != 6 && grp != 8;
    if (doctor && (int8_t)st[S_BYTE7] < (int8_t)st[S_BUST] && world_tail(s->world, TAIL_HOUR) < 6) {
        set_state(st, sl, EXHAUSTED, 1);
        return;
    }
    sl[SL_REST] = 0xFF;
    set_state(st, sl, 0, 1);
    if (st[11]) room_return(s, h, index, st, sl);
}

/* 0x2FC77 (tick 79, once per game minute): every employee of hospitals 0-15 */
static void minute_pass(Sim *s) {
    SimIn outer = s->rng_in;
    for (int h = 0; h < 16; h++)
        for (int i = 0; i < STAFF_SLOTS; i++) {
            StaffSlotId slot_id = {h, i};
            int index = (int16_t)staff_field_u16(staff_slot_view(s->world, slot_id), STAFF_SLOT_EMPLOYEE);
            if (index <= 0 || index >= STAFF_COUNT) continue; /* same empty/invalid-slot guard as slot_staff */
            /* context P for every draw (docs/rng-sites.md 3.8): D = the staff record, EBP = the row of h (the golf
             * room's draws: its record), [0x4528] = hospital record h */
            sim_set_inputs(s, sim_in(sim_address(s, staff(s, index)), rng_row(s, h), sim_address(s, hospital(s, h))));
            minute_employee(s, (StaffId){index}, slot_id);
        }
    sim_set_inputs(s, outer);
    rng_scratch_hospital16(s); /* the loop's end pointer (docs/rng-sites.md 2.4) */
}

int sim_staff_run(Sim *s, uint32_t routine) {
    for (int i = 0; i < (int)(sizeof OPERATORS / sizeof *OPERATORS); i++)
        if (routine == OPERATORS[i].routine) {
            operator(s, i);
            return 1;
        }
    switch (routine) {
    case 0x2FC17: { /* even ticks, hours 4-19: one more round of the five operators */
        int hour = world_tail(s->world, TAIL_HOUR);
        if (hour >= 4 && hour <= 19)
            for (int i = 0; i < 5; i++) operator(s, i);
        return 1;
    }
    case 0x62D6A: staffing_need(s); return 1;
    case 0x1F2A8: opening_upkeep(s); return 1;
    case 0x1F1F3: pairing(s); return 1;
    case 0x1F63D: ai_hiring(s); return 1;
    case 0x390BA: furniture_purchase(s); return 1;
    case 0x2B207: sim_recompute_all_wear(s); return 1; /* tick 64, 1 in 8: 0x2B207, 0x2BFB7, 0x2B210 */
    case 0x2BFB7: sim_tidy_lists(s); return 1;
    case 0x2B210: sim_recompute_all(s); return 1;
    case 0x2F3FF: equipment_purchase(s); return 1;
    case 0x1F9B9: store_clerk(s); return 1;
    case 0x22F65: clerk_mood(s); return 1;
    case 0x5B25D: dismissals(s); return 1;
    case 0x2B718: hourly_upkeep(s); return 1;
    case 0x2FC77: minute_pass(s); return 1;
    case 0x3091F: doctors_byte7(s); return 1;
    case 0x42714: new_course(s); return 1;
    case 0x4302C: courses_age(s); return 1;
    case 0x3DF91: book_course(s); return 1;
    default: return 0;
    }
}
