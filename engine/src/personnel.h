/* The player's personnel rules (hospital record 0), from docs/ui-personnel.md: job ads, applications and
 * hiring, the personnel sheet's actions (send home, release, raise, cut, dismiss, works independently,
 * courses) and the room staff roster (screen 54). They work on the simulation's world alone, so the tests run
 * them without a screen; the screens are in personnel_ui.c. A refusal is returned as its BITEXPZ text number,
 * 0 = done. "pb" is the player block (save block 28), whose fields the original uses as the screens' state. */
#ifndef BIING_PERSONNEL_H
#define BIING_PERSONNEL_H

#include "sim.h"

#define PERSONNEL_LIST 1050 /* [0x4656]: 0x834 bytes of u16 */
#define PERSONNEL_POSITIONS 1040

/* duty-state filters of the five sheet buttons (pb+0x82) */
enum { FILTER_IDLE = 0, FILTER_DISMISS = 1, FILTER_ROOMS = 2, FILTER_HOME = 0x13, FILTER_AWAY = 0x16 };

/* player block fields */
enum {
    PB_INDEX = 0, PB_WAGE = 0, PB_BUST = 2, PB_TRAINING = 6, PB_CRITERION = 8, PB_GROUP = 0xA, PB_COURSE = 0xE,
    PB_ROSTER_GROUP = 2, PB_ROSTER_CAT = 0xA, PB_ROSTER_ROOM = 0xC, PB_ROOM_SHOWN = 0x10, PB_ROSTER_FIELD = 0x26,
    PB_AD_GROUP = 0x28, PB_NURSE_BUST = 0x50, PB_NURSE_WAGE = 0x52, PB_NURSE_TRAINING = 0x66,
    PB_DOCTOR_TRAINING = 0x68, PB_DOCTOR_CRITERION = 0x6A, PB_SHEET_GROUP = 0x6C, PB_FILTER = 0x82,
};

enum { /* texts */
    PERS_MARKET_FULL = 555, PERS_AD_PLACED = 554, PERS_BROKE = 750, PERS_ENOUGH_STAFF = 751,
    PERS_ALREADY_HOME = 793, PERS_BUSY = 797, PERS_NOT_ASSIGNED = 799, PERS_NO_COURSE = 53, PERS_DEADLINE = 54,
    PERS_NOBODY_THERE = 329, PERS_NO_FEE = 76, PERS_NOW_DEPENDENT = 125, PERS_NOT_YET = 127,
    PERS_NOW_INDEPENDENT = 129, PERS_DISMISSAL_HOURS = 1313,
};

#define PERSONNEL_SEVERANCE 30

uint8_t *personnel_pb(Sim *s);
int personnel_pb16(Sim *s, int off); /* signed u16 field */
void personnel_set_pb16(Sim *s, int off, int v);
uint8_t *personnel_staff(Sim *s, int index); /* 32-byte staff record */
uint8_t *personnel_slot(Sim *s, int slot); /* 20-byte staff slot of hospital 0 */
uint8_t *personnel_opening(Sim *s, int j);
uint8_t *personnel_course(Sim *s, int entry);
const uint8_t *personnel_catalog(Sim *s, int course_id); /* 22-byte course catalog record at 0x40D9 */

/* ---- the sheet's search (0x3BFDB) -------------------------------------------------------------------- */

int personnel_state_fits(int filter, int state);
int personnel_slot_matches(Sim *s, int slot, int filter, int group);
/* up to `tries` slots from `from` on in steps of `step` (+1 or -1), wrapping 0-199; the slot or -1 */
int personnel_search(Sim *s, int from, int step, int tries, int filter, int group);
/* text of the "nobody" box for a filter (786-790) */
int personnel_nobody_text(int filter);
/* hotspot 9: 0 at hours 0-3, else PERS_DISMISSAL_HOURS */
int personnel_dismissal_allowed(Sim *s);

/* ---- job ads (screens 20 and 46) ---------------------------------------------------------------------- */

void personnel_ad_defaults(Sim *s, int group); /* 0x3A2D0: the form for group g in pb+0..+0xA */
enum { AD_WAGE, AD_SECOND, AD_TRAINING }; /* AD_SECOND: bust for nurses, the second criterion otherwise */
void personnel_ad_arrow(Sim *s, int field, int up);
int personnel_ad_next_group(int group); /* 1-10, after 10 back to 1 */
/* 0x398D1: the opening's index, or -1 (all 200 in use: PERS_MARKET_FULL) */
int personnel_publish(Sim *s);

/* ---- applications (screen 51) ------------------------------------------------------------------------- */

/* 0x1A384: list[i] = i + 1 for i < 1,040, then 6,000 swaps of entries random(1,040) + 1 */
void personnel_shuffle(Sim *s, uint16_t list[PERSONNEL_LIST]);
int personnel_is_applicant(Sim *s, int index);
/* 0x3A78E / 0x3A71A: the next / previous position (1-1,040) holding an applicant, or -1 */
int personnel_next_applicant(Sim *s, const uint16_t *list, int position);
int personnel_prev_applicant(Sim *s, const uint16_t *list, int position);
int personnel_hire(Sim *s, int index, int *slot_out); /* 0x22BE7: 0, PERS_BROKE or PERS_ENOUGH_STAFF */
void personnel_tear_up(Sim *s, int index); /* 0x3A6BA */

/* ---- the personnel sheet (screen 33) ------------------------------------------------------------------ */

int personnel_independence(const uint8_t *st); /* 0x2287A, score X (16-bit) */
/* 0x1D972: clears the employee's field in its room record; 1 = refused ([0x45B4]) */
int personnel_room_release(Sim *s, int slot);
int personnel_send_home(Sim *s, int slot); /* 0x2503A: 0 (text 795), PERS_ALREADY_HOME or PERS_BUSY */
int personnel_release(Sim *s, int slot); /* 0x246F4: 0 (791), PERS_ALREADY_HOME, PERS_NOT_ASSIGNED, PERS_BUSY */
void personnel_raise(Sim *s, int slot); /* 0x250EC */
void personnel_cut(Sim *s, int slot); /* 0x2510D */
int personnel_dismiss(Sim *s, int slot); /* 0x2512E: 1 if dismissed (states 0 and 0x13 only) */
int personnel_toggle_independent(Sim *s, int slot); /* 0x251FB: PERS_NOW_DEPENDENT / NOT_YET / NOW_INDEPENDENT */

/* ---- courses (screens 128 and 129) -------------------------------------------------------------------- */

int personnel_course_fits(Sim *s, int entry, int group);
/* the first fitting entry from `from` (tries entries, wrapping 0-19), or -1 */
int personnel_course_find(Sim *s, int from, int tries, int group);
int personnel_course_in_use(Sim *s, int from, int tries); /* the overview's search */
int personnel_course_price(Sim *s, int entry);
/* 0x42E81: 0 (texts 55/56), PERS_NOBODY_THERE, PERS_DEADLINE or PERS_NO_FEE */
int personnel_course_enrol(Sim *s, int slot, int entry);

/* ---- room staff (screen 54) --------------------------------------------------------------------------- */

typedef struct {
    uint32_t stub; /* entry stub of the room's staff button */
    uint8_t cat, group, field; /* category, job group, staff field in the room record */
    uint32_t back; /* pb+6: the room screen's entry routine */
    uint16_t picture, text; /* the "nobody available" box */
    uint8_t single; /* stub sets pb+0x10 = 0 */
    uint8_t sets_field; /* stub sets pb+0x26 = field (OP, torture, garage, club) */
} RosterStub;
extern const RosterStub PERSONNEL_ROSTER[26];
const RosterStub *personnel_roster_stub(uint32_t routine);

int personnel_slot_of(Sim *s, int index); /* 0x1DD1B: first slot of hospital 0 with that index, or -1 */
/* 0x1D2E6 steps 1-6: writes pb+0, +2, +0xA, +0xC, +0xE; *occupant = [0x5C53] (slot or -1). Returns 0 when
 * the roster opens, -1 when nobody is available (the stub's box). */
int personnel_roster_enter(Sim *s, const RosterStub *st, int *occupant);
/* 0x1D80E / 0x1D85A: the next / previous candidate from pb+0 (up to 219 steps), written to pb+0 */
void personnel_roster_step(Sim *s, int step, int occupant);
enum { ROSTER_ASSIGNED = 0, ROSTER_NOBODY = 1, ROSTER_BACK = 2 };
/* 0x1D364: *replaced = the staff index that was in the field (0 = none) */
int personnel_assign(Sim *s, int occupant, int *replaced);
/* 0x1D8B6: ROSTER_BACK, PERS_NOT_ASSIGNED, PERS_BUSY or 0 (text 791) */
int personnel_roster_release(Sim *s, int occupant);
/* the room field the assignment writes, for the shown group */
uint8_t *personnel_roster_field(Sim *s);

#endif
