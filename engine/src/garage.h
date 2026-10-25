/* The garage, the ambulance race and the world championship (docs/ui-garage.md). The player's garage actions (the
 * car list 132, sending a car to the race 131, the championship tables 135, the driver's activity line) and the
 * race's simulation (the start 0x67BFB, the minute 0x6D515 with its finish, the season reset of 0x474BC, the AI
 * garage 0x6DA45, the garage minute 0x57F15 and the garage's fittings and staff mood 0x542F8, in sim_race.c) work on
 * the simulation's world alone, so the tests run them without a screen; the screens are in garage_ui.c. A player
 * result is the BITEXPZ text number of its message box (0 = done without a box). Engine choices are marked. */
#ifndef BIING_GARAGE_H
#define BIING_GARAGE_H

#include "sim.h"

enum { /* the garage record (category 10, save block 5) */
    GR_MECHANIC = 6, GR_DRIVER = 8, GR_PLACES = 0x0A, GR_CARS = 0x0B, GR_ENTERED = 0x0C, GR_PLACE = 0x58,
    GR_PLACE_COUNT = 10,
};
enum { /* the race block (save block 34, [0x552A]) */
    RACE_WORD = 0, RACE_MINUTE = 2, RACE_BITS = 4, RACE_LAPS = 5, RACE_RESULT = 6, RACE_ENTRY0 = 0x0C,
    RACE_ENTRIES = 16, RACE_LOOP_ENTRIES = 17, /* the entry routines use 16, the race loop reads up to 17 */
};
enum { /* tail block: [0x6979] races this season, [0x697B] / [0x697C] the final news countdowns */
    T_RACES = 0x2E, T_FINAL_TEAM = 0x30, T_FINAL_DRIVER = 0x31,
};
enum { RACES_PER_SEASON = 25, DRIVER_TABLE = 100, DRIVER_ENTRY = 6 };
enum { /* hospital record */
    H_SEASONS_POINTS = 0x128, H_TITLES = 0x15E, H_RACES_WON = 0x160, H_CAR_BUY_DAY = 0x120, H_CAR_BUY = 0x13A,
    H_CAR_SALE_DAY = 0x124, H_CAR_SALE = 0x12E,
};
enum { /* texts */
    GA_NO_CAR = 996, GA_NO_USABLE = 997, GA_NO_RACE = 994, GA_ENTERED = 995, GA_NO_DRIVER = 998,
    GA_RACE_DRIVER = 1669, GA_NOBODY = 993,
};
#define CAR_MODELS 7
#define CAR_CATALOG_AT 0x3B43 /* 20 bytes per model: +0 name, +2 km/h, +4 PS, +6 list price, +8 text, +0x0A picture */

uint8_t *garage_of(Sim *s, int h); /* G(h), NULL without a garage (site byte 10) */
uint8_t *garage_race(Sim *s); /* the race block */
int garage_race_word(Sim *s);
uint8_t *garage_entry(Sim *s, int k); /* entry k (0-16) of the race block */
uint8_t *garage_team_points(Sim *s); /* save block 35: s16 team points per hospital */
uint8_t *garage_driver_table(Sim *s); /* save block 36, 100 entries of 6 bytes */
int garage_model_u16(Sim *s, int model, int off); /* catalog field of model 0-6, 0 outside */

/* ---- the player's garage (18, 132, 131, 135) ------------------------------------------------------------- */

int garage_list_open(Sim *s); /* 0x521C4: 0 (pb+0xE = the first car), 996 or 997 */
int garage_list_next(Sim *s); /* 0x50344: 0 or 996 */
int garage_usable(const uint8_t *place); /* a car, condition >= 25, byte 2 = 0 */
int garage_send_open(Sim *s); /* 0x52221: 0 (pb+0xE = the first usable car), 994, 995, 998, 996 or 997 */
/* 0x50036: 0 and *slot = the entry k, or the refusal of tests 1-5 */
int garage_send(Sim *s, int *slot);
int garage_races_shown(Sim *s); /* the n of the tables' titles */
void garage_team_order(Sim *s, uint8_t order[16]); /* 0x6CF80's bubble sort by the team points */
int garage_driver_rows(Sim *s); /* 0x6D197: sorts the driver table in place; the rows it shows (at most 19) */
/* The driver's activity line on the personnel sheet: up to three pieces. kind: 0 = text a + duration + text b,
 * 1 = text a + person + text b, 2 = text a + person + text c + position + text b, 3 = a + duration + b, 4 = a alone. */
typedef struct {
    int kind, a, b, c, position;
} GarageLine;
int garage_activity(Sim *s, int staff_index, int hospital, GarageLine *out); /* 1 if the person is in the garage */
int garage_player_position(Sim *s); /* 1-based position of the hospital-0 entry, 20 if none (0x22587) */
/* the gap "0:00,000s" of a running race's row (g in tenths of a second, two random digits d1 d2) */
void garage_gap_text(int g, int d1, int d2, char out[16]);

/* ---- the race (sim_race.c) -------------------------------------------------------------------------------- */

int sim_race_start(Sim *s); /* 0x67BFB: 1 when a race was announced */
void sim_race_minute(Sim *s); /* 0x6D515 */
void sim_race_round(Sim *s); /* one round of the minute */
int sim_race_grid(int minute, int n); /* the grid bonus B of entry n */
int sim_race_speed(int top, int ps, int condition); /* S before its r127 */
int sim_race_driver(const uint8_t *staff, int hours_on_duty); /* the driver factor D before its r127 */
int sim_race_distance(int distance, int v, int b, int r15); /* round step 6: the entry's new distance */
void sim_race_finish(Sim *s); /* the finish, when the result countdown reaches 0 */
int sim_race_victim(Sim *s, int person, int winner); /* 0x6D3FC: the hospital, or -1 (home healthy) */
void sim_season(Sim *s); /* the season part of 0x474BC */
void sim_ai_garage(Sim *s, int h); /* 0x6DA45 for hospital h (the picker's choice) */
void sim_garage_minute(Sim *s); /* 0x57F15 */
void sim_garage_fittings(Sim *s); /* 0x542F8 */
int sim_mechanic_skill(Sim *s, int index); /* the repair skill of 0x57F15 */
int sim_driver_score(Sim *s, const uint8_t *slot); /* step B of 0x6DA45 */

#endif
