/* The player's decisions in the specialist departments (docs/ui-departments.md): the dental station (category 11),
 * the neurology (15) with the neuro-bed assignment, the neuro ward (16), intensive care (9), the OP (7), the torture
 * chamber (8) and the pathology (18). Like rooms.c these work on the simulation's world alone (the player branches of
 * sim_rooms.c), so the tests run them without a screen; the screens are in depts_ui.c. A result is the first BITEXPZ
 * text number of its message box (0 = done without a box, -1 = nothing happens); RoomsResult carries what the message
 * names. The room shown is pb+0x10 (rooms_shown). */
#ifndef BIING_DEPTS_H
#define BIING_DEPTS_H

#include "rooms.h"

enum { /* player block fields added by docs/ui-departments.md */
    DB_NEURO_WARD = 0xC, /* screen 81: the neuro ward shown */
    DB_DENTAL_SHEET = 0x39, DB_NEURO_SHEET = 0x40, DB_NEURO_BACK = 0x61, DB_PATH_SHEET = 0x45,
    DB_DENTAL_NEWS = 0x5D, DB_NEUROLOGY_NEWS = 0x5E, DB_NEURO_WARD_NEWS = 0x63, DB_OP_NEWS = 0x65,
};
/* selector flags in the flag block */
enum { FB_OP = 4, FB_ICU = 6, FB_DENTAL = 7, FB_NEUROLOGY = 8, FB_NEURO_WARD = 9, FB_TORTURE = 0xC };

uint8_t *depts_room(Sim *s, int cat); /* the record of the room shown */

/* ---- dental station (21, 70) and neurology (31, 75, 81); cat 11 or 15 ----------------------------------- */

int depts_clinic_patient(Sim *s, int cat); /* 0x363AC / 0x2446A: 0 = the sheet (pb set), 399 / 397 */
/* 0x24406 / 0x245B4: 419 / 418, 415 / 414, 539, 411 / 410, 409 / 406, 500 or 1340 (moved; o->room = waiting room) */
int depts_clinic_call(Sim *s, int cat, RoomsResult *o);
int depts_clinic_other(Sim *s, int cat, RoomsResult *o); /* 0x25AC4 / 0x25CEB: 42, 78, 89 / 396, 87 / 88 or 0 */
/* 0x25B0E / 0x25D81: 42, 89 / 396, 87 / 88, 80 / 81, 84 / 85, 0 (in-room treatment), 14 (bill), 32 / 34 (to a waiting
 * room / left), 1462 (no neuro ward), 81 (screen 81 opens; pb+0xC = the ward) or -1 (nothing) */
int depts_clinic_accept(Sim *s, int cat, RoomsResult *o);
int depts_clinic_abort(Sim *s, int cat, RoomsResult *o); /* 0x25BAF / 0x25DC5: 77 */
int depts_clinic_bill(Sim *s, int cat, RoomsResult *o); /* 0x25B58 / 0x25E12: 89 / 396, 87 / 88, 84 / 85 or 14 */
void depts_neuro_bed_step(Sim *s, int step); /* 0x3C28C / 0x3C2DD: the next / previous neuro ward with a free bed */
int depts_neuro_bed(Sim *s, RoomsResult *o); /* 0x235ED: -1 (nothing more) or 2362 (o->room = the ward) */

/* ---- neuro ward (40, 43) --------------------------------------------------------------------------------- */

int depts_neuro_ward_bed(Sim *s, int bed); /* 0x274EA + 4 i: the patient (sheet set up) or 0 */
int depts_neuro_discharge(Sim *s, RoomsResult *o); /* 0x4EEEB: 2282, 2280 or 14 (the invoice) */
void depts_neuro_step(Sim *s); /* 0x4F145 */

/* ---- intensive care (39, 84) ------------------------------------------------------------------------------ */

int depts_icu_patient(Sim *s); /* 0x36373: 0 = the sheet, or 399 */
int depts_icu_back(Sim *s, RoomsResult *o); /* 0x520DC: 423, 424 or 421 */

/* ---- OP (30, 73) and torture chamber (32, 126); cat 7 or 8 ----------------------------------------------- */

int depts_theatre_patient(Sim *s, int cat); /* 0x344E2 / 0x34FCC: 0 = the sheet, -1 (OP: the site map) or 83 */
/* 0x4E63E: 416, 539, 412, 407 or 501 (fetched; o->room = the ward); 0x4E912: 0 (a patient on the table: the sheet),
 * 417, 539, 408 or 501 */
int depts_theatre_fetch(Sim *s, int cat, RoomsResult *o);
/* 0x4FD90 / 0x526FF: 3017 / 3018 (independent), 0 (stopped or started: the sheet), 530 (no organ), 531 / 533 (a
 * capability missing, o->item) */
int depts_theatre_toggle(Sim *s, int cat, RoomsResult *o);
/* 0x4FBBF / 0x52541: 2269 / 2275 / 2277 (OP) or 1331 / 1336 / 1338 (torture) by the operation's state; o->room = the
 * bed ward, o->total = P's state there */
int depts_theatre_back(Sim *s, int cat, RoomsResult *o);

/* ---- pathology (44, 90) ------------------------------------------------------------------------------------ */

int depts_path_fetch(Sim *s, RoomsResult *o); /* 0x50FC7: 402, 400, 401 or 1490 (o->index, o->staff) */
int depts_path_open(Sim *s); /* 0x4AE9B: 0 = the sheet, or 403 */
int depts_path_throw(Sim *s, RoomsResult *o); /* 0x52452: 1488, 1486 or 1484 (o->staff = the pathologist) */

/* ---- shared ------------------------------------------------------------------------------------------------ */

/* The nurse shows 0x3A892 (11), 0x2EFAE (16), 0x3A8DE (9), 0x367A7 (7): 0, or 552 / 549 / 553 / 547 (no nurse) */
int depts_show(Sim *s, int cat);
/* The corpse buttons of 70, 75, 43, 84, 73, 126 (cat 11, 15, 16, 9, 7, 8): 1340 (pathology; o->staff = the room's
 * nurse), 537 (none), 538 (full) */
int depts_corpse(Sim *s, int cat, RoomsResult *o);

#endif
