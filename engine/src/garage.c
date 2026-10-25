/* The player's garage and the championship tables (docs/ui-garage.md); see garage.h. The race simulation is in
 * sim_race.c. */
#include "garage.h"

#include <string.h>

#include "sim_internal.h"

#define STAFF_SIZE 32

static uint8_t *pb(Sim *s) { return s->world->block[BLK_PLAYER0]; }

uint8_t *garage_of(Sim *s, int h) { return room_counts(s, h)[10] ? sim_room_record(s, h, 10, 0) : NULL; }
uint8_t *garage_race(Sim *s) { return s->world->block[BLK_34]; }
int garage_race_word(Sim *s) { return (uint16_t)get16(garage_race(s) + RACE_WORD); }
uint8_t *garage_entry(Sim *s, int k) { return garage_race(s) + RACE_ENTRY0 + 4 * k; }
uint8_t *garage_team_points(Sim *s) { return s->world->block[34]; }
uint8_t *garage_driver_table(Sim *s) { return s->world->block[35]; }

int garage_model_u16(Sim *s, int model, int off) {
    if (model < 0 || model >= CAR_MODELS) return 0;
    return (uint16_t)program_word(s->prog, CAR_CATALOG_AT, 20u * (uint32_t)model + (uint32_t)off);
}

/* the player's garage: the record of category 10 whether or not the site byte is set (the screens are only reached
 * with a garage) */
static uint8_t *player_garage(Sim *s) { return sim_room_record(s, 0, 10, 0); }

/* ---- the car list (132) --------------------------------------------------------------------------------- */

int garage_list_open(Sim *s) { /* 0x521C4 */
    uint8_t *g = player_garage(s);
    if (!g[GR_CARS]) return GA_NO_CAR;
    for (int k = 0; k < GR_PLACE_COUNT; k++)
        if (g[GR_PLACE + 4 * k]) {
            put16(pb(s) + 0x0E, k);
            return 0;
        }
    return GA_NO_USABLE; /* cars counted, no place occupied: an inconsistent record */
}

int garage_list_next(Sim *s) { /* 0x50344 */
    uint8_t *g = player_garage(s);
    if (!g[GR_CARS]) return GA_NO_CAR;
    int k = get16(pb(s) + 0x0E);
    /* engine choice: one round of the ten places (with no place occupied the original loops for ever) */
    for (int step = 0; step < GR_PLACE_COUNT; step++) {
        k = k + 1 >= GR_PLACE_COUNT || k < 0 ? 0 : k + 1;
        if (g[GR_PLACE + 4 * k]) {
            put16(pb(s) + 0x0E, k);
            return 0;
        }
    }
    return 0;
}

/* ---- sending a car to the race (131) ---------------------------------------------------------------------- */

int garage_usable(const uint8_t *p) { return p[0] && p[1] >= 25 && !p[2]; }

/* tests 1-5 of 0x52221 and 0x50036 */
static int send_tests(Sim *s) {
    if (!garage_race_word(s)) return GA_NO_RACE;
    int free_k = -1;
    for (int k = 0; k < RACE_ENTRIES; k++) {
        int8_t h = (int8_t)garage_entry(s, k)[0];
        if (h == 0) return GA_ENTERED;
        if (h < 0 && free_k < 0) free_k = k;
    }
    /* engine choice: no free entry (the original loops for ever; each hospital enters once, so it cannot happen while
     * the player has none) refuses like an entry of the player */
    if (free_k < 0) return GA_ENTERED;
    uint8_t *g = player_garage(s);
    if (!get16(g + GR_DRIVER)) return GA_NO_DRIVER;
    if (!g[GR_CARS]) return GA_NO_CAR;
    return 0;
}

int garage_send_open(Sim *s) { /* 0x52221 */
    int r = send_tests(s);
    if (r) return r;
    uint8_t *g = player_garage(s);
    for (int k = 0; k < GR_PLACE_COUNT; k++)
        if (garage_usable(g + GR_PLACE + 4 * k)) {
            put16(pb(s) + 0x0E, k);
            return 0;
        }
    return GA_NO_USABLE;
}

int garage_send(Sim *s, int *slot) { /* 0x50036: the car is not tested again */
    int r = send_tests(s);
    if (r) return r;
    int k = 0;
    while ((int8_t)garage_entry(s, k)[0] >= 0) k++;
    uint8_t *e = garage_entry(s, k), *g = player_garage(s);
    int place = get16(pb(s) + 0x0E);
    e[0] = 0; /* the player's hospital [0xB054] */
    e[1] = (uint8_t)place;
    put16(e + 2, 0);
    g[GR_ENTERED] = 0xFF;
    *slot = k;
    return 0;
}

/* ---- the championship tables (135) ----------------------------------------------------------------------- */

int garage_races_shown(Sim *s) {
    return world_tail(s->world, T_RACES) - (garage_race_word(s) ? 1 : 0);
}

void garage_team_order(Sim *s, uint8_t order[16]) { /* 0x385C7, then the sort of 0x6CF80 */
    const uint8_t *pts = garage_team_points(s);
    for (int i = 0; i < 16; i++) order[i] = (uint8_t)i;
    for (int swapped = 1; swapped;) {
        swapped = 0;
        for (int i = 0; i < 15; i++)
            if (get16(pts + 2 * order[i]) < get16(pts + 2 * order[i + 1])) {
                uint8_t t = order[i];
                order[i] = order[i + 1], order[i + 1] = t, swapped = 1;
            }
    }
    s->dup_byte = 0; /* the sort's swap flag is [0x45CF] (0x6D025-0x6D07F), 0 after its last pass (SIM-55) */
}

int garage_driver_rows(Sim *s) { /* 0x6D197 */
    uint8_t *e = garage_driver_table(s);
    for (int i = 0; i < DRIVER_TABLE; i++)
        if (!get16(e + DRIVER_ENTRY * i)) put32(e + DRIVER_ENTRY * i + 2, 0);
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
    int n = 0;
    while (n < 19 && get16(e + DRIVER_ENTRY * n)) n++;
    return n;
}

/* ---- the driver's activity line ----------------------------------------------------------------------- */

int garage_player_position(Sim *s) { /* 0x22587 */
    for (int k = 0; k < RACE_LOOP_ENTRIES; k++) {
        int8_t h = (int8_t)garage_entry(s, k)[0];
        if (h < 0) break;
        if (h == 0) return k + 1;
    }
    return 20;
}

int garage_activity(Sim *s, int index, int h, GarageLine *o) {
    memset(o, 0, sizeof *o);
    if (h < 0 || h >= 16) return 0;
    const uint8_t *g = garage_of(s, h);
    if (!g || s->world->block[BLK_STAFF][index * STAFF_SIZE + 4] != 10) return 0;
    const uint8_t *rb = garage_race(s);
    if ((uint16_t)get16(g + GR_DRIVER) != index) { /* the other garage staff */
        /* while the race word is set, 2475 alone replaces "Liegt seit ..." (docs/ui-sheets.md, "Garage") */
        if (garage_race_word(s)) o->kind = 4, o->a = 2475;
        else o->kind = 3, o->a = 1055, o->b = 1056;
        return 1;
    }
    int minute = get16(rb + RACE_MINUTE), pos = garage_player_position(s);
    o->kind = 1;
    if (!g[GR_ENTERED]) o->kind = 0, o->a = 1023, o->b = 1024;
    else if (minute < 30) o->a = 1025, o->b = 1026;
    else if (minute == 30) o->a = 1027, o->b = 1028;
    else if (rb[RACE_LAPS]) { /* running */
        /* the English CD writes 1042 where the German builds write 1031 (docs/editions.md 7.3, rule K) */
        int place = program_english(s->prog) ? 1042 : 1031;
        if (pos == 1) o->a = 1029, o->b = 1030;
        else if (pos == 2) o->a = place, o->b = 1032;
        else if (pos == 3) o->a = 1033, o->b = 1034;
        else if (pos <= 10) o->a = place, o->b = 1035 + pos - 4; /* 1031, not 1033 (docs/ui-sheets.md) */
        else o->a = 1042, o->b = 1043;
    } else { /* after the last lap */
        if (pos == 1) o->a = 1044, o->b = 1045;
        else if (pos <= 7) o->a = 1046, o->b = 1047 + pos - 2;
        else o->kind = 2, o->a = 1046, o->c = 1053, o->position = pos, o->b = 1054;
    }
    return 1;
}

void garage_gap_text(int g, int d1, int d2, char out[16]) {
    if (g < 0) g = 0;
    int m = g / 600, r = g % 600;
    if (m > 9) m = 9; /* engine guard: the template has one minute digit */
    out[0] = (char)('0' + m), out[1] = ':';
    out[2] = (char)('0' + r / 100), out[3] = (char)('0' + r % 100 / 10), out[4] = ',';
    out[5] = (char)('0' + g % 10), out[6] = (char)('0' + d1 % 10), out[7] = (char)('0' + d2 % 10), out[8] = 's';
    out[9] = 0;
    if (out[0] == '0') { /* the leading "0:" and the following "0" blanked */
        out[0] = out[1] = ' ';
        if (out[2] == '0') out[2] = ' ';
    }
}
