/* The player's decisions in the core rooms (hospital record 0), from docs/ui-rooms.md and docs/ui-sitemap.md:
 * the site map's building clicks and room selector, the reception (desk, bed assignment, occupancy, patient
 * overview), the waiting room, the treatment room, the Krankenzimmer, the corpse button and the nurse shows.
 * They work on the simulation's world alone (the shared helpers of sim_rooms.c in their player branches), so the
 * tests run them without a screen; the screens are in rooms_ui.c. A result is returned as the first BITEXPZ text
 * number of its message box (0 = done without a box); RoomsResult carries what the message names. "pb" is the
 * player block (save block 28), "fb" the player flag block that the scripts' op 8 tests. */
#ifndef BIING_ROOMS_H
#define BIING_ROOMS_H

#include "sim.h"

enum { /* player block fields (docs/ui-rooms.md, "Player-block and flag-block fields used here") */
    RB_PLACE = 2, RB_QUEUE = 0xA, RB_WARD = 0xC, RB_PAGE = 0xE, RB_ROOM = 0x10, RB_CALL = 0x12, RB_CALL_ROOM = 0x14,
    RB_DESK_ICON = 0x2A, RB_QUEUE_ICON = 0x2B, RB_PATIENT = 0x2C, RB_MINUTE = 0x2E, RB_TICK = 0x3C,
    RB_STAFF_HOVER = 0x3D, RB_WAITING_NEWS = 0x44, RB_DIED = 0x46, RB_LOW_HEALTH = 0x47, RB_MOOD = 0x49,
    RB_BILL = 0x4A, RB_FROM_98 = 0x4B, RB_INV_CAT = 0x4C, RB_INV_MISSING = 0x4E, RB_DECISION = 0x5B,
    RB_TREAT_NEWS = 0x5C, RB_MOVED = 0x5F, RB_WARD_NEWS = 0x62, RB_NURSE_HOVER = 0xAC, RB_DESK_HOVER = 0xAD,
};
enum { FB_WAITING = 0, FB_TREATMENT = 2, FB_WARD = 3, FB_ALIVE = 0xA, FB_DEAD = 0xB };

typedef struct {
    int index; /* the patient the message names */
    int room; /* a room index (printed + 1) */
    int staff; /* a staff index whose name the message prints */
    int bill, reaction; /* invoice: the bill and the reaction step c (texts 14 + 2c, 15 + 2c) */
    int total, free_beds; /* text 2367 */
    int screen; /* where a box returns when the code alone does not say */
    int bed_button; /* screen 98: draw image 962 */
    int item; /* a capability the message names (the OP and torture start) */
} RoomsResult;

uint8_t *rooms_pb(Sim *s);
int rooms_pb16(Sim *s, int off);
void rooms_set_pb16(Sim *s, int off, int v);
uint8_t *rooms_person(Sim *s, int index);
uint8_t *rooms_staff(Sim *s, int index);
int rooms_count(Sim *s, int cat); /* site byte cat: the player's rooms of category cat */
int rooms_shown(Sim *s); /* pb+0x10, a negative value read as 0 (as the roster does) */
uint8_t *rooms_record(Sim *s, int cat, int i);
uint8_t *rooms_reception(Sim *s);
int rooms_independent(Sim *s, int staff_index); /* staff byte 11, "works independently" */

/* ---- the site map (docs/ui-sitemap.md) ------------------------------------------------------------------- */

enum { SELECT_NONE, SELECT_ALWAYS, SELECT_IF_BUILT };
typedef struct {
    uint32_t click; /* the hotspot's click routine */
    uint8_t cat, select, clears, clears2; /* site byte; when pb+0x10 is reset; news bytes cleared (0 = none) */
    int8_t fb; /* selector flag in the flag block, -1 = a single room */
    int16_t sx, sy; /* selector offset */
    uint16_t background;
    uint8_t screen;
} SiteBuilding;
extern const SiteBuilding ROOMS_SITE[17];
const SiteBuilding *rooms_site_building(uint32_t click);
/* The click's state changes: 1 when the building is entered (built), 0 = no effect at all. */
int rooms_site_click(Sim *s, uint8_t *flags, const SiteBuilding *b);
/* The selector arrows: pb+0x10 + step, wrapping inside the category's rooms. */
void rooms_selector_step(Sim *s, int cat, int step);

/* ---- reception (19, 57, 64, 149, 109) -------------------------------------------------------------------- */

int rooms_desk(Sim *s, RoomsResult *o); /* 0x29167: 0 = the sheet (pb set), 975, 539 or 974 */
/* 0x2B989 player branch: 2266 thrown out, 2265 no waiting room, 2259 no seat, 2257 / 2258 no form / pen, 2261 */
int rooms_admit(Sim *s, int to_waiting, RoomsResult *o);
int rooms_bed_open(Sim *s, RoomsResult *o); /* 0x24356: 0 = screen 64, 2371, 2370, 975, 539 or 2367 */
void rooms_bed_ward_step(Sim *s, int step); /* 0x3C1E7 / 0x3C238 */
int rooms_bed_assign(Sim *s, RoomsResult *o); /* 0x2336B: -1 = nothing (ward full), 2362 (screen 53 or 60) */
int rooms_occupancy_open(Sim *s); /* 0x242AF: 0 or 2371 */
void rooms_occupancy_step(Sim *s, int step); /* 0x3D13E / 0x3D191 */
int rooms_overview(Sim *s, int first); /* 0x2387C / 0x238CA: 0 = a patient on the sheet, or 541 */

/* ---- waiting room (29, 59), ward (38, 62), nurse shows ----------------------------------------------------- */

int rooms_seat(Sim *s, int seat); /* 0x34461 + 4 i: the patient (sheet set up) or 0 */
int rooms_bed_click(Sim *s, int bed); /* 0x27C45 + 4 i: the patient or 0 */
int rooms_show(Sim *s, int cat); /* 0x317B9 / 0x316B3 / 0x2EF50: 0, or 550 / 551 / 548 (no nurse) */

/* ---- treatment room (23, 61, 98) ------------------------------------------------------------------------- */

int rooms_couch(Sim *s); /* the sheet (0) or 398 (no patient) */
int rooms_call(Sim *s, RoomsResult *o); /* 0x24015: 418, 414, 539, 410, 406, 500 or 1340 (moved) */
int rooms_other(Sim *s, RoomsResult *o); /* 0x23EFF: 42, 78, 89, 87, 84 or 0 (new proposal) */
/* 0x241A8: 42, 89, 87, 79, or the 0x2D11A outcome: 0 (in the room), 14 (bill), 32 / 34 (to a waiting room / left),
 * 427, 2371, 425 (queued; o->screen 60 or 98), 36 / 38 / 40 (donor done) */
int rooms_accept(Sim *s, RoomsResult *o);
int rooms_abort(Sim *s, RoomsResult *o); /* 0x241EC: 77, or 36 / 38 / 40 */
int rooms_hand_bill(Sim *s, RoomsResult *o); /* 0x24256: 89, 87, 84, 86 or 14 (the invoice) */
int rooms_inventory(Sim *s, int cat, uint32_t back); /* the inventory button: 0 (screen 65) or 375 */
int rooms_reaction(int v); /* [0x5C4D] for an opinion step v: 8, 7, 6 for gains, 5 .. 0 for losses */

/* ---- ward patient (62) and the corpse button ------------------------------------------------------------- */

int rooms_discharge(Sim *s, RoomsResult *o); /* 0x4F229: 2282, 2279, 2280 or 14 (the invoice) */
void rooms_step(Sim *s); /* 0x4F36E */
int rooms_icu(Sim *s, RoomsResult *o); /* 0x4F383: 535, 536 or 1340 (moved; o->room = the record) */
/* The corpse buttons of 57, 59, 61, 62 (cat 2, 3, 5, 6): 1340 (pathology; o->staff), 537 (none), 538 (full) */
int rooms_corpse(Sim *s, int cat, RoomsResult *o);

/* ---- the patient sheet ------------------------------------------------------------------------------------ */

/* Step 2 and 3 of 0x43547: fb+0xA / fb+0xB from P, and the low-health notifications pb+0x47 / pb+0x46. */
void rooms_sheet_flags(Sim *s, uint8_t *flags, int notify);
void rooms_page_turn(Sim *s); /* 0x29373 */

#endif
