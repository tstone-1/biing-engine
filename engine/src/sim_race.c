/* The ambulance race and the garages in the simulation, from docs/ui-garage.md: the start 0x67BFB (tick 51), the
 * minute 0x6D515 (tick 56) with its rounds and the finish (the victim 0x6D3FC, the race news 0x55F4A, the
 * championship points), the season part of 0x474BC (tick 43), the AI garage 0x6DA45 (ticks 9, 27, 42), the garage
 * minute 0x57F15 and the garage's fittings and staff mood 0x542F8 (tick 71). No money moves in the race. Engine
 * choices are marked. */
#include "sound.h"
#include <string.h>

#include "garage.h"
#include "sim_internal.h"

#define PERSONS 1009
#define PERSON_SIZE 106
#define STAFF_SIZE 32

enum { SL_INDEX = 0, SL_GROUP = 2, SL_STATE = 3, SL_ROOM = 4, SL_HOURS = 7, SL_ON_DUTY = 0x0A };
enum { S_HOSPITAL = 3, S_STATE = 4, S_MOOD = 21, S_ROOM = 26, S_EXPERIENCE = 16 };
enum { GARAGE_STATE = 10, OFF_DUTY = 0x13, DRIVERS = 6 };

static uint8_t *person(Sim *s, int i) { return s->world->block[BLK_PATIENTS] + i * PERSON_SIZE; }
static uint8_t *staff(Sim *s, int i) { return s->world->block[BLK_STAFF] + i * STAFF_SIZE; }
static uint8_t *slot_of(Sim *s, int h, int i) { return staff_slots(s, h) + i * STAFF_SLOT_SIZE; }
static uint8_t *rb(Sim *s) { return garage_race(s); }
static int minute_of(Sim *s) { return get16(rb(s) + RACE_MINUTE); }

static int staff_slot(Sim *s, int h, int index) { /* the slot of h holding the staff index, -1 none */
    if (!index) return -1;
    for (int i = 0; i < STAFF_SLOTS; i++)
        if ((uint16_t)get16(slot_of(s, h, i) + SL_INDEX) == index) return i;
    return -1;
}

static int name_length(Sim *s, int p) {
    if (!s->people) return 0;
    size_t n;
    datadb_pool_entry(s->people, DAT_PATIENT_NAMES, p, &n);
    return (int)n;
}

/* ---- the start (0x67BFB) -------------------------------------------------------------------------------- */

int sim_race_start(Sim *s) {
    World *w = s->world;
    if (world_tail(w, T_RACES) >= RACES_PER_SEASON || garage_race_word(s)) return 0;
    int hour = world_tail(w, TAIL_HOUR);
    if (hour < 4 || hour > 17 || sim_bits(s, 63) != 4) return 0;
    /* [0x5BCD] / [0x5BD1], the city's OP and garage counts: what the last onset's step 3 stored (SIM-51; not saved, 0
     * after the program start), not counted now */
    int cars = 0;
    for (int h = 0; h < 16; h++) {
        const uint8_t *g = garage_of(s, h);
        if (g) cars += g[GR_CARS];
    }
    if (s->city[CITY_OP] < 5 || s->city[CITY_GARAGE] < 4 || (uint8_t)cars < 4) return 0;
    int p = 0;
    for (int tries = 0, redraws = 0; tries < 100 && redraws < 100000;) {
        int c = (int)sim_random(s, 1008) + 1;
        if (name_length(s, c) >= 17) { /* redrawn without using up a try */
            redraws++;
            continue;
        }
        tries++;
        const uint8_t *q = person(s, c);
        if (!q[104] && q[1] && !q[79] && !q[41] && (int8_t)q[3] < 0 && !q[57] && !q[105]) {
            p = c;
            break;
        }
    }
    if (!p) return 0;
    uint8_t *r = rb(s);
    put16(r + RACE_WORD, p);
    put16(r + RACE_MINUTE, 0);
    world_set_tail(w, T_RACES, world_tail(w, T_RACES) + 1);
    put16(r + RACE_BITS, (int)sim_bits(s, 3)); /* a word: RB+5 = 0 */
    uint8_t *q = person(s, p);
    q[3] = 16, q[4] = 6;
    q[40] = 0, q[41] = 0x4A, q[42] = 0x2A, q[43] = 0x2C, q[44] = (uint8_t)(sim_bits(s, 31) + 35), q[45] = 0;
    memset(q + 48, 0, 8);
    q[56] = 0x2A, q[57] = 0, q[66] = 0, q[78] = 3, q[79] = 0;
    put16(q + 94, (int)sim_bits(s, 127) * 2 + 270);
    memset(q + 100, 0, 6);
    s->race_redraw = 1; /* [0x5C79] */
    sound_race(s->sound, q[0] + 1); /* 0x67D92: [0x5CA8] = the person's byte 0 + 1 selects the announcement */
    return 1;
}

/* ---- the minute (0x6D515) ------------------------------------------------------------------------------- */

static int entries(Sim *s, int max) { /* the entries before the first negative hospital byte */
    int n = 0;
    while (n < max && (int8_t)garage_entry(s, n)[0] >= 0) n++;
    return n;
}

static int entered(Sim *s) {
    int n = 0;
    for (int k = 0; k < RACE_ENTRIES; k++) n += (int8_t)garage_entry(s, k)[0] >= 0;
    return n;
}

void sim_race_minute(Sim *s) {
    if (!garage_race_word(s)) return;
    uint8_t *r = rb(s);
    if (r[RACE_RESULT]) {
        if (!--r[RACE_RESULT]) sim_race_finish(s);
        return;
    }
    int m = minute_of(s) + 1;
    put16(r + RACE_MINUTE, m);
    if (m == 30) {
        if (s->screen == 133) sound_clear(s->sound); /* 0x6D554 */
        (void)sim_bits_site(s, 31, 0x6D55E); /* an r31 drawn and discarded: the 50 is a constant (SIM-63) */
        r[RACE_LAPS] = (uint8_t)(r[RACE_LAPS] + 50);
    }
    if (m < 30) {
        if (m >= 25 && entered(s) < 2) put16(r + RACE_MINUTE, 10);
        return;
    }
    if (!--r[RACE_LAPS]) {
        r[RACE_RESULT] = 8;
        return;
    }
    sim_race_round(s);
}

int sim_race_grid(int minute, int n) {
    if (minute >= 40) return 0;
    int lead = 40 - minute, b = lead * 16 - (lead > 1 ? lead : 1) * n;
    return b < 0 ? 0 : b;
}

int sim_race_speed(int top, int ps, int c) {
    int speed = top * 4 + ps * 2;
    if (c == 0) return 0;
    if (c < 20) return 5;
    if (c < 75) return speed - (speed / 64) * (100 - c) / 100;
    return speed;
}

int sim_race_driver(const uint8_t *st, int hours) { /* without its r127 */
    int d = 70;
    if (hours >= 10) d = 2;
    else if (hours >= 5) d = 70 - (hours - 3) * 4;
    return st[2] * d / 2 + st[5] * 4 + st[15] + st[12] * 16 + st[13] * 8 + (uint16_t)get16(st + S_EXPERIENCE) / 4 + 40;
}

/* round step 6 (docs/ui-garage.md): the 16-bit distance + v / 2 + b - r15; a result negative as a signed word becomes
 * 1, anything else stands (a step below 1 is kept) */
int sim_race_distance(int distance, int v, int b, int r15) {
    uint16_t d = (uint16_t)(distance + ((uint16_t)v >> 1) + b - r15);
    return (int16_t)d < 0 ? 1 : d;
}

void sim_race_round(Sim *s) {
    int m = minute_of(s), n = entries(s, RACE_LOOP_ENTRIES);
    for (int k = 0; k < n; k++) {
        uint8_t *e = garage_entry(s, k);
        int b = sim_race_grid(m, k);
        int h = e[0];
        uint8_t *g = h < 16 ? garage_of(s, h) : NULL;
        if (!g || e[1] >= GR_PLACE_COUNT) continue; /* engine guard: an entry without a garage or a car place */
        uint8_t *car = g + GR_PLACE + 4 * e[1];
        int model = car[0] - 1, driver = (uint16_t)get16(g + GR_DRIVER), sl = staff_slot(s, h, driver);
        if (model < 0 || model >= CAR_MODELS || sl < 0) continue; /* the original loops for ever without the slot */
        int c = car[1];
        if (sim_bits(s, 15) == 3 && car[1]) car[1]--; /* wear */
        if (sim_bits(s, 63) == 34 && (int)car[1] < (int)sim_bits(s, 127)) { /* breakdown */
            int cond = car[1];
            if (cond >= 70) cond -= (int)sim_bits(s, 31);
            else if (cond >= 50) cond -= (int)sim_bits(s, 63);
            else cond = 0;
            car[1] = (uint8_t)(cond < 0 ? 0 : cond);
        }
        int speed = sim_race_speed(garage_model_u16(s, model, 2), garage_model_u16(s, model, 4), c);
        int d = sim_race_driver(staff(s, driver), slot_of(s, h, sl)[SL_ON_DUTY]) + (int)sim_bits(s, 127);
        speed += (int)sim_bits(s, 127);
        long v = ((long)speed * d) >> 14;
        v += sim_bits(s, 127); /* + r127 + r127 + r63 in this order (docs/ui-garage.md) */
        v += sim_bits(s, 127);
        v += sim_bits(s, 63);
        put16(e + 2, sim_race_distance(get16(e + 2), (int)(uint16_t)v, b, (int)sim_bits(s, 15)));
    }
    for (int swapped = 1; swapped;) { /* by distance, descending, whole entries */
        swapped = 0;
        for (int k = 0; k + 1 < n; k++) {
            uint8_t *a = garage_entry(s, k), *z = garage_entry(s, k + 1);
            if ((uint16_t)get16(a + 2) < (uint16_t)get16(z + 2)) {
                uint8_t t[4];
                memcpy(t, a, 4), memcpy(a, z, 4), memcpy(z, t, 4);
                swapped = 1;
            }
        }
    }
    s->dup_byte = 0; /* the sort's swap flag is [0x45CF] (0x6DA04-0x6DA39), 0 after its last pass (SIM-55) */
}

int sim_race_victim(Sim *s, int p, int winner) { /* 0x6D3FC */
    uint8_t *q = person(s, p);
    int h = winner;
    for (int tries = 0; tries < 300; tries++, h = (int)sim_random(s, 16)) {
        if (h < 0 || h >= 16 || !room_counts(s, h)[7] || !room_counts(s, h)[6]) continue;
        int wards = sim_rooms_in_use(s, h, 6);
        if (wards <= 0) continue;
        for (int pick = 0; pick < 300; pick++) {
            int wi = (int)sim_random(s, (uint32_t)wards);
            uint8_t *k = sim_room_record(s, h, 6, wi);
            if (k[0x0A] <= k[0x0C]) continue;
            for (int bed = 0; bed < 10; bed++) {
                if (get16(k + 0x14 + 4 * bed)) continue;
                k[0x0C]++;
                put16(k + 0x14 + 4 * bed, p);
                put16(k + 0x16 + 4 * bed, 2);
                q[3] = (uint8_t)h, q[66] = (uint8_t)wi, q[48] = 50, q[53] = 50;
                return h;
            }
        }
    }
    sim_reset_healthy(q);
    return -1;
}

static const uint8_t DRIVER_POINTS[17] = {10, 9, 8, 7, 6, 6, 5, 5, 5, 4, 4, 4, 3, 3, 3, 2, 2}; /* 0xAF18 */
static const uint8_t TEAM_POINTS[6] = {10, 6, 4, 3, 2, 1}; /* 0xAF4C, then 0 */

static void sort_drivers(Sim *s) { /* by the points at +4, descending, signed */
    uint8_t *e = garage_driver_table(s);
    for (int swapped = 1; swapped;) {
        swapped = 0;
        for (int i = 0; i < DRIVER_TABLE - 1; i++) {
            uint8_t *a = e + DRIVER_ENTRY * i, *b = a + DRIVER_ENTRY;
            if (get16(a + 4) < get16(b + 4)) {
                uint8_t t[DRIVER_ENTRY];
                memcpy(t, a, DRIVER_ENTRY), memcpy(a, b, DRIVER_ENTRY), memcpy(b, t, DRIVER_ENTRY);
                swapped = 1;
            }
        }
    }
}

void sim_race_finish(Sim *s) {
    uint8_t *r = rb(s);
    int p = garage_race_word(s), n = entries(s, RACE_LOOP_ENTRIES);
    if (n > 0) {
        int win = garage_entry(s, 0)[0], second = n > 1 ? garage_entry(s, 1)[0] : 0xFF;
        sim_race_victim(s, p, win);
        const uint8_t *gw = win < 16 ? garage_of(s, win) : NULL, *gs = second < 16 ? garage_of(s, second) : NULL;
        int place = garage_entry(s, 0)[1], model = gw && place < GR_PLACE_COUNT ? gw[GR_PLACE + 4 * place] - 1 : 0;
        sim_news_race(s, win, second, gw ? get16(gw + GR_DRIVER) : 0, gs ? get16(gs + GR_DRIVER) : 0, model, p);
    } else {
        sim_reset_healthy(person(s, p)); /* engine choice: no entrant, no winner to bring the victim to */
    }
    put16(r + RACE_WORD, 0);
    uint8_t *teams = garage_team_points(s), *table = garage_driver_table(s);
    for (int k = 0; k < RACE_LOOP_ENTRIES; k++) {
        uint8_t *e = garage_entry(s, k);
        if ((int8_t)e[0] < 0) break;
        int h = e[0], pts = k < 6 ? TEAM_POINTS[k] : 0;
        s->rng_scratch = RNG_GARAGES + s->rng_heap + 158u * (uint32_t)h; /* [0x4528] (docs/rng-sites.md 2.4) */
        const uint8_t *g = h < 16 ? garage_of(s, h) : NULL;
        int driver = g ? (uint16_t)get16(g + GR_DRIVER) : 0;
        if (driver) add16(staff(s, driver) + S_EXPERIENCE, DRIVER_POINTS[k]);
        if (h < 16) add16(teams + 2 * h, pts);
        if (driver) {
            int at = -1;
            for (int i = 0; i < DRIVER_TABLE && at < 0; i++)
                if ((uint16_t)get16(table + DRIVER_ENTRY * i) == driver) at = i;
            for (int i = 0; i < DRIVER_TABLE && at < 0; i++)
                if (!get16(table + DRIVER_ENTRY * i)) at = i;
            if (at >= 0) {
                uint8_t *d = table + DRIVER_ENTRY * at;
                put16(d, driver), put16(d + 2, h), add16(d + 4, pts);
            }
        }
        e[0] = 0xFF;
    }
    for (int h = 0; h < 16; h++) sim_room_record(s, h, 10, 0)[GR_ENTERED] = 0; /* all 16 records */
    if (world_tail(s->world, T_RACES) == RACES_PER_SEASON) {
        uint8_t order[16];
        garage_team_order(s, order);
        add16(hospital(s, order[0]) + H_TITLES, 1);
        sort_drivers(s);
        int h = get16(table + 2);
        if (h >= 0 && h < 16) add16(hospital(s, h) + H_TITLES, 1);
    }
}

/* ---- the season (0x474BC, before its news ageing) ------------------------------------------------------- */

void sim_season(Sim *s) {
    World *w = s->world;
    if (world_tail(w, T_RACES) < RACES_PER_SEASON) return;
    uint8_t *t = w->block[WORLD_TAIL];
    if (t[T_FINAL_TEAM] > 1) t[T_FINAL_TEAM]--;
    if (t[T_FINAL_DRIVER] > 1) {
        t[T_FINAL_DRIVER]--;
        return; /* reaching 1 leaves the season pending until the next call */
    }
    if (t[T_FINAL_TEAM] != 1 || t[T_FINAL_DRIVER] != 1) return;
    world_set_tail(w, T_RACES, 0);
    t[T_FINAL_TEAM] = t[T_FINAL_DRIVER] = 0;
    uint8_t *teams = garage_team_points(s);
    for (int h = 0; h < 16; h++) add16(hospital(s, h) + H_SEASONS_POINTS, get16(teams + 2 * h));
    memset(teams, 0, s->world->size[34]);
    memset(garage_driver_table(s), 0, s->world->size[35]);
}

/* ---- the AI garage (0x6DA45) ---------------------------------------------------------------------------- */

static void to_garage(Sim *s, int h, int sl, uint8_t *field) { /* state 10 */
    uint8_t *slp = slot_of(s, h, sl);
    int index = (uint16_t)get16(slp + SL_INDEX);
    put16(field, index);
    slp[SL_STATE] = GARAGE_STATE, slp[SL_ROOM] = 0;
    staff(s, index)[S_STATE] = GARAGE_STATE, staff(s, index)[S_ROOM] = 0;
}

int sim_driver_score(Sim *s, const uint8_t *slp) {
    const uint8_t *st = staff(s, (uint16_t)get16(slp + SL_INDEX));
    int d = 24 - slp[SL_ON_DUTY];
    if (d < 1) d = 1;
    int v = (int16_t)(4 * d + 4 * st[2] + 4 * st[5] + st[15] + (uint16_t)get16(st + S_EXPERIENCE) / 4 + 16 * st[12] +
                      8 * st[13]);
    if (slp[SL_ON_DUTY] >= 10) v /= 2;
    return v;
}

static void car_sale(Sim *s, int h, uint8_t *g, int32_t gap) {
    int32_t x = gap - 450;
    if (gap < 0 || x < 0) return;
    int l2 = x / 128 + 1;
    if (l2 > 7) l2 = 7;
    int k = -1;
    for (int i = 0; i < GR_PLACE_COUNT && k < 0; i++) {
        int m1 = g[GR_PLACE + 4 * i];
        if (m1 && m1 < l2) k = i;
    }
    if (k < 0) return;
    uint8_t *cars = s->world->block[BLK_CARS], *o = NULL;
    for (int i = 0; i < 20 && !o; i++)
        if ((int8_t)cars[i * 20] < 0) o = cars + i * 20;
    if (!o) return;
    uint8_t *place = g + GR_PLACE + 4 * k;
    int model = place[0] - 1, list = garage_model_u16(s, model, 6);
    g[GR_CARS]--;
    o[0] = (uint8_t)h, o[1] = (uint8_t)model;
    put16(o + 2, list - (list / 100 * (100 - place[1])) / 2);
    o[4] = place[1], o[5] = 0;
    memset(place, 0, 4);
}

void sim_ai_garage(Sim *s, int h) {
    if (h <= 0 || h >= 16) return;
    World *w = s->world;
    uint8_t *H = hospital(s, h), *g = garage_of(s, h);
    int32_t cash = cash_of(s, h);
    if (cash < 0 || !g || !room_counts(s, h)[7]) return;
    int32_t k = (int32_t)sim_daily_costs(s, h); /* [0x46BA + 4h]: engine choice, computed now (as everywhere) */
    int hour = world_tail(w, TAIL_HOUR), race = garage_race_word(s), minute = minute_of(s);
    /* A, the car purchase */
    if (g[GR_CARS] < 2) {
        if (s->screen == 130) return;
        if (cash >= 600 && hour >= 20) {
            int32_t limit = cash - k - 200, budget = cash - k - 400;
            if (cash - k >= 0 && budget >= 0) {
                int m = budget / 128 > 6 ? 6 : budget / 128;
                int place = -1;
                for (int i = 0; i < GR_PLACE_COUNT && place < 0; i++)
                    if (!g[GR_PLACE + 4 * i]) place = i;
                if (place < 0) return;
                uint8_t *cars = w->block[BLK_CARS];
                for (int t = 0; t < 80; t++) {
                    uint8_t *o = cars + 20 * sim_random(s, 20);
                    int seller = (int8_t)o[0], price = (uint16_t)get16(o + 2);
                    if (seller < 0 || o[1] != m || price >= limit || price >= cash) continue;
                    add32(H + H_CASH, -price), add32(H + H_CAR_BUY_DAY, price), add32(H + H_CAR_BUY, price);
                    if (seller <= DEALER) {
                        uint8_t *S = hospital(s, seller);
                        add32(S + H_CASH, price), add32(S + H_CAR_SALE_DAY, price), add32(S + H_CAR_SALE, price);
                    }
                    uint8_t *p = g + GR_PLACE + 4 * place;
                    p[0] = (uint8_t)(m + 1), p[1] = o[4], p[2] = p[3] = 0;
                    g[GR_CARS]++;
                    o[0] = 0xFF;
                    break;
                }
                return;
            }
        }
    }
    /* B, the driver */
    if (hour >= 5 && hour <= 18 && !race) {
        put32(g + GR_MECHANIC, 0);
        int best = 0;
        for (int i = 0; i < STAFF_SLOTS; i++) {
            uint8_t *slp = slot_of(s, h, i);
            int index = (uint16_t)get16(slp + SL_INDEX);
            if (!index || slp[SL_GROUP] != DRIVERS || (int8_t)slp[SL_STATE] >= OFF_DUTY) continue;
            slp[SL_STATE] = slp[SL_ROOM] = 0;
            staff(s, index)[S_STATE] = staff(s, index)[S_ROOM] = 0;
            int v = sim_driver_score(s, slp);
            if (v > best) {
                best = v;
                to_garage(s, h, i, g + GR_DRIVER);
            }
        }
    }
    /* C, staffing */
    sim_rating_record(s, g); /* after acquisition and reassignment, before the driver test (SIM-82) */
    int sl;
    if (!get16(g + GR_DRIVER)) {
        if (hour < 5 || hour > 18) return; /* outside the hours the routine ends (docs/sim-drift9.md section 5) */
        if (!sim_idle_candidate(s, h, DRIVERS, &sl)) return;
        to_garage(s, h, sl, g + GR_DRIVER);
    }
    if (!get16(g + GR_MECHANIC)) {
        if (hour < 2 || hour > 21) return; /* likewise */
        if (sim_idle_candidate(s, h, DRIVERS, &sl)) to_garage(s, h, sl, g + GR_MECHANIC);
    }
    if (hour < 14) {
        int mech = (uint16_t)get16(g + GR_MECHANIC), drv = (uint16_t)get16(g + GR_DRIVER);
        for (int i = 0; i < STAFF_SLOTS; i++) {
            uint8_t *slp = slot_of(s, h, i);
            int index = (uint16_t)get16(slp + SL_INDEX);
            if (!index || slp[SL_GROUP] != DRIVERS || index == mech || index == drv || (int8_t)slp[SL_STATE] >= OFF_DUTY)
                continue;
            slp[SL_STATE] = OFF_DUTY, slp[SL_ROOM] = 0, slp[SL_HOURS] = 0;
            put16(slp + SL_ON_DUTY, 0);
            staff(s, index)[S_STATE] = OFF_DUTY, staff(s, index)[S_ROOM] = 0;
        }
    }
    /* D, the race entry */
    if (race && minute < 30) {
        int free_k = -1;
        for (int e = 0; e < RACE_ENTRIES; e++) {
            int8_t eh = (int8_t)garage_entry(s, e)[0];
            if (eh == h) return;
            if (eh < 0 && free_k < 0) free_k = e;
        }
        if (free_k < 0 || !get16(g + GR_DRIVER) || !g[GR_CARS]) return;
        for (int t = 0; t < 80; t++) {
            int place = (int)sim_random(s, 10);
            const uint8_t *p = g + GR_PLACE + 4 * place;
            if (!p[0] || p[1] < 70) continue;
            uint8_t *e = garage_entry(s, free_k);
            e[0] = (uint8_t)h, e[1] = (uint8_t)place;
            put16(e + 2, 0);
            return;
        }
        return;
    }
    /* E, the evening */
    if (hour >= 20 && !race) {
        for (int f = GR_DRIVER; f >= GR_MECHANIC; f -= 2) {
            int index = (uint16_t)get16(g + f);
            if (!index) continue;
            sim_release(s, h, index);
            put16(g + f, 0);
        }
        if (g[GR_CARS] >= 2) car_sale(s, h, g, cash - k);
    }
}

/* ---- the garage minute (0x57F15) ------------------------------------------------------------------------ */

int sim_mechanic_skill(Sim *s, int index) {
    const uint8_t *st = staff(s, index);
    /* "16-bit": engine reading, unsigned (the experience is capped at 30,000, so the sum can pass 32,767) */
    return (uint16_t)(8 * st[2] + st[5] + 16 * st[12] + 16 * st[13] + 4 * st[14] + st[15] +
                      (uint16_t)get16(st + S_EXPERIENCE) + st[21]) / 128 + 5;
}

static int has_entry(Sim *s, int h) {
    for (int k = 0; k < RACE_ENTRIES; k++)
        if ((int8_t)garage_entry(s, k)[0] == h) return 1;
    return 0;
}

void sim_garage_minute(Sim *s) {
    int race = garage_race_word(s);
    for (int h = 0; h < 16; h++) {
        uint8_t *g = garage_of(s, h);
        if (!g) continue;
        if (!race && s->screen == 1)
            for (int i = 0; i + 1 < GR_PLACE_COUNT; i++) {
                uint8_t *a = g + GR_PLACE + 4 * i, *b = a + 4;
                if ((int8_t)a[0] < (int8_t)b[0]) {
                    uint8_t t[4];
                    memcpy(t, a, 4), memcpy(a, b, 4), memcpy(b, t, 4);
                }
            }
        int mech = (uint16_t)get16(g + GR_MECHANIC);
        if (!mech || (race && has_entry(s, h))) continue;
        if (sim_mechanic_skill(s, mech) < (int)sim_bits(s, 127)) continue;
        for (int i = 0; i < GR_PLACE_COUNT; i++) {
            uint8_t *p = g + GR_PLACE + 4 * i;
            if (!p[0] || p[1] >= 100) continue;
            p[1]++;
            add16(staff(s, mech) + S_EXPERIENCE, 1); /* engine reading: only when a car was repaired */
            break;
        }
    }
}

/* ---- fittings and staff mood (0x542F8) ----------------------------------------------------------------- */

void sim_garage_fittings(Sim *s) {
    static const int CAPS[2] = {0x40, 0x41}; /* Werkzeugkiste, Putzlappen */
    for (int h = 0; h < 16; h++) {
        uint8_t *g = garage_of(s, h);
        if (!g) continue;
        uint8_t *st = room_counts(s, h)[4] ? sim_room_record(s, h, 4, 0) : NULL;
        int clerk = st ? (uint16_t)get16(st + STORE_CLERK) : 0;
        int usable = st && (!hospital(s, h)[0x14] || (clerk && staff(s, clerk)[11]));
        if (usable) {
            for (int i = 0; i < 2; i++)
                if (!sim_has_equipment(s, g, 10, CAPS[i]) && sim_store_supply(s, h, 10, g, CAPS[i], 0) < 0) break;
            if (!sim_has_furniture_kind(s, g, 10, 8)) {
                uint8_t want = 8;
                sim_fittings(s, h, 10, g, &want); /* 0x2A59A, 0x2A718, 0x2B003: the shared rule */
            }
        }
        for (int f = GR_MECHANIC; f <= GR_DRIVER; f += 2) {
            int who = (uint16_t)get16(g + f);
            if (!who) continue;
            uint8_t *m = staff(s, who) + S_MOOD;
            if (*m < 100 && sim_bits(s, 7) == 3) (*m)++;
            if ((int8_t)*m < 4 && !garage_race_word(s)) {
                put16(g + f, 0);
                sim_leave_room(s, h, who); /* 0x2301C */
            }
        }
    }
}

/* ---- dispatch ------------------------------------------------------------------------------------------- */

int sim_race_run(Sim *s, uint32_t routine) {
    switch (routine) {
    case 0x67BFB: sim_race_start(s); return 1;
    case 0x6D515: sim_race_minute(s); return 1;
    case 0x6DA45: {
        sim_ai_garage(s, sim_pick(s)); /* the picker, then 0x1C927 (the pick's pointers) */
        return 1;
    }
    case 0x57F15: sim_garage_minute(s); return 1;
    case 0x542F8: sim_garage_fittings(s); return 1;
    default: return 0;
    }
}
