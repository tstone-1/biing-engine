/* The scheduled bookkeeping outside the room and newspaper routines, from docs/sim-bookkeeping.md: the per-minute
 * staff mood and furniture use of each room kind (section 1), the rise-only staff mood and fittings of pathology, golf
 * room and club room (2), the comfort countdowns (3), the place re-sorts of hospitals 0 and 1 (4), the staff mood per
 * minute 0x307BF (5), the AI operators of golf room, club room and thugs (6) and the player-block flags (7).
 * Left out as copy protection (section 8): the manual-query display hook inside 0x307BF, 0x50F22 inside 0x1C8A4, the
 * newspaper item 0x4CA3B, and nothing here reads hospital record 1's +0x174 / +0x176 / +0x177. */
#include <string.h>

#include "sim_internal.h"

#define STAFF_RECORD_SIZE 32
#define STAFF_LAST 1040
#define PERSON_SIZE 106
#define PRIORITY_WAITING 0x486F /* 10 bytes: 0 men, 1 women, 2 anyone */
#define PRIORITY_WARDS 0x4DCC

enum { S_STATE = 4, S_DEMAND = 18, S_WAGE = 19, S_MOOD = 21, S_ROOM = 26 };
enum { SL_INDEX = 0, SL_GROUP = 2, SL_STATE = 3, SL_ROOM = 4, SL_HOURS = 7, SL_CUSTODY = 0x0A };
enum { GOLF = 0x0C, CLUB = 0x0E, OFF_DUTY = 0x13, EXHAUSTED = 0x17, SCREEN_STORE = 0x41 };

static uint8_t *staff_rec(Sim *s, int i) { return s->world->block[BLK_STAFF] + i * STAFF_RECORD_SIZE; }
static int human(Sim *s, int h) { return hospital(s, h)[0x14] != 0; }
static uint8_t *store_of(Sim *s, int h) { return room_counts(s, h)[4] ? sim_room_record(s, h, 4, 0) : NULL; }

/* 0x2C06B: the player's store fetches by itself - no player on the inventory screen 0x41 and a clerk whose staff
 * byte 11 is set. Player 2's screen word is always the office (docs/sim-rooms2.md, "The store-screen test"). */
static int store_usable(Sim *s, int h) {
    const uint8_t *st = store_of(s, h);
    int clerk = st ? (uint16_t)get16(st + STORE_CLERK) : 0;
    return st && s->screen != SCREEN_STORE && clerk && staff_rec(s, clerk)[11];
}

/* ---- shared helpers ----------------------------------------------------------------------------------- */

/* 0x2C0E7: the first furniture slot of the room whose item has kind `kind` (catalog byte 6) is used: it loses its
 * catalog byte 16, or 1 with r127 = 47 when that byte is 0; at 0 or less (signed) the slot's id = 0 and the list
 * tidy-up 0x2BFB7 runs. 1 found, 0 missing. */
int sim_use_furniture(Sim *s, uint8_t *rec, int cat, int kind) {
    Description d = sim_description(s, cat);
    for (int f = 0; f < d.furniture_slots; f++) {
        int id = (uint16_t)get16(rec + d.furniture + 2 * f);
        const uint8_t *it = sim_furniture_item(s, id); /* an empty slot resolves to entry 0, kind 0 */
        if (it[6] != kind) continue;
        int w = it[16] ? it[16] : sim_bits(s, 127) == 47;
        uint8_t *c = rec + d.cond_a + f;
        *c = (uint8_t)(*c - w);
        if ((int8_t)*c <= 0) {
            put16(rec + d.furniture + 2 * f, 0);
            sim_tidy_lists(s);
        }
        return 1;
    }
    return 0;
}

/* The wanted-byte rule: W follows the result of a check of kind k only when it is 0 or k. */
static void wanted(uint8_t *w, int k, int found) {
    if (*w == 0 || *w == k) *w = (uint8_t)(found ? 0 : k);
}

/* The player-only fetch of the wanted kind W from the store (0x2C0BA, 0x2A59A, 0x2A718 with take, 0x2B003); W
 * itself is not cleared here. */
static void player_fetch(Sim *s, int h, int cat, uint8_t *rec, int w) {
    if (!w || !human(s, h) || !store_usable(s, h) || sim_has_furniture_kind(s, rec, cat, w)) return;
    uint8_t copy = (uint8_t)w;
    sim_fittings(s, h, cat, rec, &copy); /* with a non-zero wanted byte: the free slot, the take and the install */
}

/* ---- section 1: room staff mood and furniture use ----------------------------------------------------- */

typedef struct {
    uint32_t routine;
    uint8_t cat, use, kinds[2], want, why, staff[3], idle_only;
} RoomMood;

enum { NONE = 0xFF };
static const RoomMood ROOM_MOODS[] = {
    {0x35A3E, 2, 1, {1, 5}, 4, 5, {8, 0, 0}, 0},       {0x32F6C, 5, 1, {1, 5}, 4, 5, {8, 0x0A, 0}, 1},
    {0x33276, 3, 0, {0, 0}, 6, 7, {8, 0, 0}, 0},       {0x333FB, 11, 1, {5, 0}, 4, 5, {8, 0x0A, 0}, 1},
    {0x37583, 15, 1, {8, 5}, 4, 5, {8, 0x0A, 0}, 1},   {0x53845, 9, 0, {8, 5}, 4, 5, {8, 0, 0}, 0},
    {0x53ACD, 13, 0, {1, 5}, 4, NONE, {6, 0, 0}, 0},   {0x5456F, 7, 1, {8, 0}, 0x7A, 0x7B, {8, 0x0A, 6}, 1},
    {0x548EB, 8, 1, {5, 0}, 0x7A, 0x7B, {8, 0x0A, 0}, 1},
};

/* The reception's player-only equipment step: the pen (kind 3) and the form (kind 11); a full list ends the step. */
static void reception_equipment(Sim *s, int h, uint8_t *rec) {
    if (!human(s, h) || !store_usable(s, h)) return;
    static const int KINDS[2] = {3, 11};
    for (int i = 0; i < 2; i++)
        if (!sim_has_equipment_kind(s, rec, 2, KINDS[i]) && sim_store_supply(s, h, 2, rec, KINDS[i], 1) < 0) return;
}

static void leave(Sim *s, int h, const RoomMood *m, uint8_t *rec, uint8_t *field, int why) {
    int who = (uint16_t)get16(field);
    if (m->why != NONE) rec[m->why] = (uint8_t)why;
    put16(field, 0);
    if (m->cat == 3) rec[0x0F] = 0; /* the waiting room also ends its comfort */
    sim_leave_room(s, h, who);
}

static void room_record(Sim *s, int h, const RoomMood *m, uint8_t *rec) {
    uint8_t *w = rec + m->want;
    if (m->cat == 2) reception_equipment(s, h, rec);
    if (m->cat == 3) { /* no furniture check: chairs wanted when someone has been second in the queue */
        const uint8_t *reception = sim_room_record(s, h, 2, 0);
        if (!*w && get16(reception + 0x10) && (uint16_t)get16(rec + 0x0A) < 10) *w = 5;
    }
    for (int k = 0; k < 2 && m->kinds[k]; k++) {
        int kind = m->kinds[k];
        wanted(w, kind, m->use ? sim_use_furniture(s, rec, m->cat, kind) : sim_has_furniture_kind(s, rec, m->cat, kind));
    }
    if (m->cat != 3 && m->cat != 13) player_fetch(s, h, m->cat, rec, *w);
    if (m->idle_only && get16(rec + 0x0C)) return;
    int rating = rec[0] + rec[1]; /* 0x2B0EA */
    for (int f = 0; f < 3 && m->staff[f]; f++) {
        uint8_t *field = rec + m->staff[f];
        int who = (uint16_t)get16(field);
        /* SIM-29 (docs/sim-drift8.md section 22.4): only the first staff field waits for r15 = 7; the further ones
         * (+0x0A, the OP's +6) are updated on every call */
        if (!who || (f == 0 && sim_bits(s, 15) != 7)) continue;
        uint8_t *mood = staff_rec(s, who) + S_MOOD;
        int unsupplied = *w && !(m->cat == 3 && human(s, h)); /* a human waiting room always takes the other branch */
        if (unsupplied) {
            if (*mood) {
                int r = (int)sim_bits(s, 1); /* one statement per draw */
                r += (int)sim_bits(s, 1);
                *mood = (uint8_t)(*mood - r / 2);
            }
            if ((int8_t)*mood < 20) leave(s, h, m, rec, field, 1);
            continue;
        }
        /* the waiting room tests its own byte +0x15 instead of the mood (reproduced: its moods can pass 100) */
        /* FIX-28 (docs/fixes.md): original (waiting room) tests the room's byte +0x15, so the mood climbs past 100;
         * fixed tests the employee's mood, as every other room */
        int8_t tested = (int8_t)(m->cat == 3 && !fix_on(28) ? rec[0x15] : *mood);
        if (tested < 100 && rating >= (int)sim_bits(s, 63)) {
            (*mood)++;
        } else if ((int8_t)*mood >= 4) {
            if (rating < (int)sim_bits(s, 63)) (*mood)--;
        } else {
            leave(s, h, m, rec, field, 2);
        }
    }
}

static void room_moods(Sim *s, const RoomMood *m) {
    for (int h = 0; h < 16; h++)
        for (int i = 0; i < sim_rooms_in_use(s, h, m->cat); i++) room_record(s, h, m, sim_room_record(s, h, m->cat, i));
}

/* ---- section 2: pathology, golf room, club room -------------------------------------------------------- */

/* A rise-only mood: when not 100, + 1 on the draw (and the extra condition); below 4 (and the leave condition) out. */
static void rise_only(Sim *s, int h, uint8_t *field, uint32_t mask, uint32_t hit, int cond, int may_leave) {
    int who = (uint16_t)get16(field);
    if (!who) return;
    uint8_t *mood = staff_rec(s, who) + S_MOOD;
    if (*mood == 100) return;
    if (sim_bits(s, mask) == hit && cond) (*mood)++;
    if ((int8_t)*mood < 4 && may_leave) {
        put16(field, 0);
        sim_leave_room(s, h, who);
    }
}

/* The equipment fittings of pathology and golf room, by capability; a full list (hospital 0) ends the step: 0. */
static int capabilities(Sim *s, int h, int cat, uint8_t *rec, const int *caps, int n) {
    for (int i = 0; i < n; i++)
        if (!sim_has_equipment(s, rec, cat, caps[i]) && sim_store_supply(s, h, cat, rec, caps[i], 0) < 0) return 0;
    return 1;
}
static int fittings_gate(Sim *s, int h) { return store_of(s, h) && (!human(s, h) || store_usable(s, h)); }

/* 0x53F31 */
static void pathology_minute(Sim *s) {
    static const int CAPS[7] = {9, 0x33, 0x3C, 0x0D, 0x18, 0x19, 0x24};
    for (int h = 0; h < 16; h++) {
        if (!room_counts(s, h)[18]) continue;
        uint8_t *rec = sim_room_record(s, h, 18, 0);
        if (fittings_gate(s, h)) capabilities(s, h, 18, rec, CAPS, 7);
        rise_only(s, h, rec + 0x0A, 3, 1, get16(rec + 0x0C) != 0, 1);
    }
}

/* 0x54BCE */
static void golf_minute(Sim *s) {
    static const int CAPS[2] = {0x36, 0x37};
    for (int h = 0; h < 16; h++) {
        if (!room_counts(s, h)[12]) continue;
        uint8_t *rec = sim_room_record(s, h, 12, 0);
        if (fittings_gate(s, h) && capabilities(s, h, 12, rec, CAPS, 2) && !sim_has_furniture_kind(s, rec, 12, 0x12)) {
            uint8_t want = 0x12; /* the carpet: 0x2A59A (no slot ends), 0x2A718 with take, 0x2B003 */
            sim_fittings(s, h, 12, rec, &want);
        }
        rise_only(s, h, rec + 2, 7, 3, 1, rec[5] == 0);
    }
}

/* 0x54DF5 */
static void club_minute(Sim *s) {
    for (int h = 0; h < 16; h++) {
        if (!room_counts(s, h)[14]) continue;
        uint8_t *rec = sim_room_record(s, h, 14, 0);
        rise_only(s, h, rec + 2, 7, 3, 1, 1);
        rise_only(s, h, rec + 4, 7, 3, 1, 1);
    }
}

/* ---- section 3: comfort countdowns --------------------------------------------------------------------- */

static void countdown(Sim *s, int cat, int off) {
    for (int h = 0; h < 16; h++)
        for (int i = 0; i < sim_rooms_in_use(s, h, cat); i++) {
            uint8_t *rec = sim_room_record(s, h, cat, i);
            if (cat == 11 && rec[4] == 1) rec[4] = 0; /* 0x4C0EA: the dental ward's table wish goes */
            if (rec[off] > (human(s, h) ? 1 : 0)) rec[off]--;
        }
}

/* ---- section 4: place re-sorts ------------------------------------------------------------------------- */

static int sex_of(Sim *s, int index) { return s->world->block[BLK_PATIENTS][index * PERSON_SIZE]; }

static void resort(Sim *s, int cat, uint32_t list_at, int screen_a, int screen_b) {
    if (s->screen == screen_a || s->screen == screen_b) return; /* player 2's screen is not modelled */
    const uint8_t *list = program_table(s->prog, list_at);
    for (int h = 0; h < 2; h++)
        for (int r = 0; r < sim_rooms_in_use(s, h, cat); r++) {
            uint8_t *places = sim_room_record(s, h, cat, r) + 0x14;
            for (int i = 0; i < 10; i++) {
                int p = (uint16_t)get16(places + 4 * i);
                if (!p || list[i] == 2 || sex_of(s, p) == list[i]) continue;
                int sex = sex_of(s, p);
                for (int j = 0; j < 10; j++) {
                    int q = (uint16_t)get16(places + 4 * j);
                    if (list[j] != sex || (q && sex_of(s, q) == sex)) continue;
                    uint8_t t[4];
                    memcpy(t, places + 4 * j, 4);
                    memcpy(places + 4 * j, places + 4 * i, 4);
                    memcpy(places + 4 * i, t, 4); /* a free place j leaves place i cleared */
                    break;
                }
            }
        }
}

/* ---- section 5: 0x307BF, staff mood per minute (the manual-query hook is left out) ---------------------- */

static void mood_up(uint8_t *m) {
    (*m)++;
    if ((int8_t)*m >= 100) *m = 100;
}
static void mood_down(uint8_t *m) { /* not below 1: a mood of 0 becomes 1 (oracle, 2026-10-19) */
    *m = (uint8_t)((int8_t)*m - 1 < 1 ? 1 : *m - 1);
}

static void staff_mood(Sim *s) {
    int hour = world_tail(s->world, TAIL_HOUR);
    for (int i = 1; i <= STAFF_LAST; i++) {
        uint8_t *st = staff_rec(s, i), *m = st + S_MOOD;
        if ((int8_t)st[3] < 0) continue;
        int state = st[S_STATE];
        if (state >= 0x13 && state <= 0x19 && state != EXHAUSTED) continue;
        if (state == EXHAUSTED) {
            if (hour < 10) *m = 100;
            else if (sim_bits(s, 7) == 0 && (int8_t)*m >= 4) (*m)--;
            continue;
        }
        int w = (int8_t)st[S_WAGE], d = (int8_t)st[S_DEMAND];
        if (state == 0) {
            if (w < d) {
                if (sim_bits(s, 15) == 1) mood_down(m);
            } else if (w == d) {
                if (sim_bits(s, 31) == 1) mood_up(m);
            } else if (sim_bits(s, 3) == 1) {
                mood_up(m);
            }
            continue;
        }
        if (sim_bits(s, 3) == 0) mood_down(m);
        if (w < d) mood_down(m);
        else if (w > d && w - d >= (int)sim_bits(s, 7)) mood_up(m);
    }
}

/* ---- section 6: AI operators --------------------------------------------------------------------------- */

static int assign_into(Sim *s, int h, uint8_t *field, int state) {
    int k, who = sim_assignable(s, h, 1, &k); /* 0x22D5F */
    if (who) sim_assign(s, h, who, k, state, 0, field);
    return who;
}

/* 0x51BD4 */
static void golf_operator(Sim *s) {
    int h = sim_pick(s);
    if (!h || !room_counts(s, h)[12]) return;
    uint8_t *g = sim_room_record(s, h, 12, 0);
    int hour = world_tail(s->world, TAIL_HOUR), played = world_tail(s->world, TAIL_HOURS);
    int who = (uint16_t)get16(g + 2);
    if (!who) {
        if (played < 5 || (hour >= 2 && hour <= 21 && cash_of(s, h) >= 0)) assign_into(s, h, g + 2, GOLF);
    } else if (hour >= 22) {
        put16(g + 2, 0);
        sim_release(s, h, who); /* 0x22EDD, the evening release */
    }
}

/* 0x533CD */
static void club_operator(Sim *s) {
    int h = sim_pick(s);
    if (!h || !room_counts(s, h)[14]) return;
    uint8_t *c = sim_room_record(s, h, 14, 0);
    sim_rating_record(s, c); /* classroom operator: before fittings or staff searches (SIM-82) */
    if (c[0] + c[1] < sim_rating_threshold(s, h) && sim_furniture_room(s, h, 14, c) && store_of(s, h)) {
        /* r1 = 0: decoration (kind 2), and when the store has none (h+0xDC = 2 when 0) books and papers (kind 6) in
         * the same call; r1 = 1: kind 6 only (docs/sim-drift.md section 4, step 2) */
        int r = (int)sim_bits(s, 1);
        uint8_t want = r == 0 ? 2 : 6;
        sim_fittings(s, h, 14, c, &want);
        if (r == 0 && want) {
            want = 6;
            sim_fittings(s, h, 14, c, &want);
        }
    }
    int hour = world_tail(s->world, TAIL_HOUR), played = world_tail(s->world, TAIL_HOURS);
    int ask = (played < 5 || (hour >= 10 && hour <= 15)) && cash_of(s, h) >= 0;
    if (!get16(c + 2)) {
        if (ask) assign_into(s, h, c + 2, CLUB);
    } else if (!get16(c + 4)) {
        if (ask) assign_into(s, h, c + 4, CLUB);
    } else if (cash_of(s, h) < 0 || hour >= 20) {
        for (int f = 2; f <= 4; f += 2) {
            int who = (uint16_t)get16(c + f);
            put16(c + f, 0);
            sim_release(s, h, who);
        }
    }
}

/* 0x48215 */
static void thug_dispatch(Sim *s) {
    int h = sim_pick(s);
    if (!h || cash_of(s, h) < 0) return;
    const uint8_t *cnt = room_counts(s, h);
    int waiting = sim_waiting_for(s, h, 5) + sim_waiting_for(s, h, 11) + sim_waiting_for(s, h, 15);
    int n = (uint8_t)(cnt[5] + cnt[11] + cnt[15]);
    if (!n) return;
    int hour = world_tail(s->world, TAIL_HOUR);
    if (hour >= 4 && hour <= 18) {
        const uint8_t *reception = sim_room_record(s, h, 2, 0); /* 0x1B65C */
        if ((int8_t)waiting >= n + 1 || !get16(reception + 8) || get16(reception + 0x0A)) return;
    }
    for (int i = 0; i < STAFF_SLOTS; i++) {
        uint8_t *sl = staff_slots(s, h) + i * STAFF_SLOT_SIZE;
        int index = (uint16_t)get16(sl + SL_INDEX);
        if (!index || sl[SL_GROUP] != 8 || (sl[SL_STATE] >= 0x13 && sl[SL_STATE] <= 0x15)) continue;
        uint8_t *st = staff_rec(s, index);
        if (st[S_MOOD] >= 70) {
            sim_thug_send(s, h, i, 0); /* with the AI mode [0x5ABD] set */
            return;
        }
        sl[SL_HOURS] = 0;
        put16(sl + SL_CUSTODY, 0);
        sl[SL_STATE] = st[S_STATE] = OFF_DUTY;
        sl[SL_ROOM] = st[S_ROOM] = 0;
    }
}

/* ---- section 7: player-block flags ---------------------------------------------------------------------- */

static void minute_flags(Sim *s) { /* 0x1C8A4, without the copy-protection test 0x50F22 between the two blocks */
    for (int b = BLK_PLAYER0; b <= BLK_PLAYER1; b++) {
        s->world->block[b][0x2E] = 0xFF; /* the per-minute redraw request (docs/desktop.md) */
        if (sim_bits(s, 3) == 0) s->world->block[b][0x56] = 0xFF;
    }
}

static void hourly_flags(Sim *s) { /* 0x3DC86 */
    s->world->block[BLK_PLAYER0][0x33] = 0xFF;
    s->world->block[BLK_PLAYER1][0x33] = 0xFF;
}

/* ---- dispatch ------------------------------------------------------------------------------------------- */

int sim_books_run(Sim *s, uint32_t routine) {
    for (size_t i = 0; i < sizeof ROOM_MOODS / sizeof *ROOM_MOODS; i++)
        if (ROOM_MOODS[i].routine == routine) {
            room_moods(s, &ROOM_MOODS[i]);
            return 1;
        }
    switch (routine) {
    case 0x53F31: pathology_minute(s); return 1;
    case 0x54BCE: golf_minute(s); return 1;
    case 0x54DF5: club_minute(s); return 1;
    case 0x4A23A: countdown(s, 3, 0x0F); return 1;
    case 0x4C008: countdown(s, 6, 0x0F); return 1;
    case 0x4C079: countdown(s, 16, 0x0F); return 1;
    case 0x4C0EA: countdown(s, 11, 0x19); return 1;
    case 0x30C55: resort(s, 3, PRIORITY_WAITING, 0x1D, 0x3B); return 1;
    case 0x3789E: resort(s, 6, PRIORITY_WARDS, 0x26, 0x3E); return 1;
    case 0x37A2E: resort(s, 16, PRIORITY_WARDS, 0x28, 0x2B); return 1;
    case 0x307BF: staff_mood(s); return 1;
    case 0x51BD4: golf_operator(s); return 1;
    case 0x533CD: club_operator(s); return 1;
    case 0x48215: thug_dispatch(s); return 1;
    case 0x1C8A4: minute_flags(s); return 1;
    case 0x3DC86: hourly_flags(s); return 1;
    default: return 0;
    }
}
