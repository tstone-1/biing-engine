/* The player's decisions in the core rooms (docs/ui-rooms.md, docs/ui-sitemap.md); see rooms.h. The order of the
 * refusals is the spec's; the moves themselves are the shared helpers of sim_rooms.c in their player branches. */
#include "fixes.h"
#include "room_sound.h"
#include "rooms.h"

#include "purchase.h"

#include <string.h>

#include "personnel.h"
#include "sim_internal.h"

#define H 0
#define PATIENTS 1008
enum { P_HOSPITAL = 3, P_STATE = 4, P_DIAGNOSIS = 42, P_TREATMENT = 43, P_HEALTH = 44, P_WISH = 57, P_ROOM = 66,
       P_PREV_HEALTH = 67, P_IN_TREATMENT = 79, P_IN_STEP = 93, P_RECOVERY = 102, P_DEAD = 104 };

uint8_t *rooms_pb(Sim *s) { return personnel_pb(s); }
int rooms_pb16(Sim *s, int off) { return personnel_pb16(s, off); }
void rooms_set_pb16(Sim *s, int off, int v) { personnel_set_pb16(s, off, v); }
uint8_t *rooms_person(Sim *s, int index) { return s->world->block[BLK_PATIENTS] + index * 106; }
uint8_t *rooms_staff(Sim *s, int index) { return personnel_staff(s, index); }
int rooms_count(Sim *s, int cat) { return room_counts(s, H)[cat]; }
int rooms_shown(Sim *s) {
    int i = rooms_pb16(s, RB_ROOM);
    return i < 0 ? 0 : i;
}
uint8_t *rooms_record(Sim *s, int cat, int i) { return sim_room_record(s, H, cat, i); }
uint8_t *rooms_reception(Sim *s) { return rooms_record(s, 2, 0); }
int rooms_independent(Sim *s, int staff_index) { return staff_index && rooms_staff(s, staff_index)[11]; }

static int u16(const uint8_t *p) { return (uint16_t)get16(p); }

/* ---- site map ------------------------------------------------------------------------------------------- */

/* docs/ui-sitemap.md, "Clicking a building": hotspot order of the script */
const SiteBuilding ROOMS_SITE[17] = {
    {0x33D58, 2, SELECT_NONE, 0xAD, 0, -1, 0, 0, 172, 19},     {0x32D77, 3, SELECT_IF_BUILT, 0x44, 0, 0, 0, 0, 198, 29},
    {0x2EEC4, 4, SELECT_ALWAYS, 0, 0, -1, 0, 0, 181, 22},      {0x36474, 7, SELECT_ALWAYS, 0x65, 0, 4, 380, 15, 201, 30},
    {0x3675B, 8, SELECT_ALWAYS, 0, 0, 0x0C, 240, 0, 213, 32},  {0x36D4C, 6, SELECT_ALWAYS, 0x62, 0, 3, 184, 0, 219, 38},
    {0x31D28, 9, SELECT_IF_BUILT, 0, 0, 6, 0, 0, 225, 39},     {0x319F5, 11, SELECT_ALWAYS, 0x5D, 0, 7, 400, 0, 178, 21},
    {0x31EC7, 15, SELECT_IF_BUILT, 0x5E, 0, 8, 112, 0, 210, 31}, {0x3BCAF, 16, SELECT_ALWAYS, 0x63, 0, 9, 184, 0, 219, 40},
    {0x36A7B, 12, SELECT_NONE, 0, 0, -1, 0, 0, 243, 42},       {0x36AD9, 13, SELECT_NONE, 0, 0, -1, 0, 0, 126, 9},
    {0x36D20, 5, SELECT_ALWAYS, 0x5C, 0xAC, 2, 215, 0, 184, 23}, {0x3BD5E, 14, SELECT_NONE, 0, 0, -1, 0, 0, 123, 8},
    {0x3BDE0, 18, SELECT_NONE, 0, 0, -1, 0, 0, 237, 44},       {0x3BE59, 17, SELECT_NONE, 0, 0, -1, 0, 0, 192, 25},
    {0x36A1E, 10, SELECT_NONE, 0, 0, -1, 0, 0, 169, 18},
};

const SiteBuilding *rooms_site_building(uint32_t click) {
    for (int i = 0; i < 17; i++)
        if (ROOMS_SITE[i].click == click) return &ROOMS_SITE[i];
    return NULL;
}

int rooms_site_click(Sim *s, uint8_t *flags, const SiteBuilding *b) {
    if (b->select == SELECT_ALWAYS) rooms_set_pb16(s, RB_ROOM, 0);
    int n = rooms_count(s, b->cat);
    if (!n) return 0; /* no refusal message: the click has no effect */
    if (b->select == SELECT_IF_BUILT) rooms_set_pb16(s, RB_ROOM, 0);
    if (b->clears) rooms_pb(s)[b->clears] = 0;
    if (b->clears2) rooms_pb(s)[b->clears2] = 0;
    if (b->fb >= 0) flags[b->fb] = n == 1 ? 0 : 0xFF;
    return 1;
}

void rooms_selector_step(Sim *s, int cat, int step) {
    int n = rooms_count(s, cat), i = rooms_shown(s) + step;
    if (i >= n) i = 0; /* engine choice: "exceed" read as leaving the valid indices 0 .. n - 1 */
    if (i < 0) i = n > 0 ? n - 1 : 0;
    rooms_set_pb16(s, RB_ROOM, i);
}

/* ---- the patient sheet ---------------------------------------------------------------------------------- */

static void sheet_open(Sim *s, int index, int place) {
    rooms_set_pb16(s, RB_PLACE, place);
    rooms_set_pb16(s, RB_PAGE, 0);
    rooms_set_pb16(s, RB_PATIENT, index);
}

void rooms_sheet_flags(Sim *s, uint8_t *flags, int notify) {
    const uint8_t *p = rooms_person(s, rooms_pb16(s, RB_PATIENT));
    int dead = p[P_DEAD] != 0;
    flags[FB_ALIVE] = dead ? 0 : 0xFF;
    flags[FB_DEAD] = dead ? 0xFF : 0;
    if (!notify || p[P_HEALTH] >= 25 || p[P_HEALTH] == p[P_PREV_HEALTH]) return;
    if (!dead && p[P_PREV_HEALTH] >= 25) rooms_pb(s)[RB_LOW_HEALTH] = 0xFF;
    if (dead) rooms_pb(s)[RB_DIED] = 0xFF;
}

void rooms_page_turn(Sim *s) { rooms_set_pb16(s, RB_PAGE, (rooms_pb16(s, RB_PAGE) + 1) % 3); }

/* ---- reception ------------------------------------------------------------------------------------------ */

static int clerk(Sim *s) { return u16(rooms_reception(s) + 8); }

/* the clerk checks shared by the desk and the bed assignment: 975 (no clerk), 539 (works independently) */
static int clerk_refusal(Sim *s, RoomsResult *o) {
    if (!clerk(s)) return 975;
    if (rooms_independent(s, clerk(s))) return o->staff = clerk(s), 539;
    return 0;
}

int rooms_desk(Sim *s, RoomsResult *o) {
    *o = (RoomsResult){0};
    const uint8_t *r = rooms_reception(s);
    int index = u16(r + 10);
    if (!clerk(s)) return 975;
    if ((!index || !rooms_person(s, index)[P_DEAD]) && rooms_independent(s, clerk(s))) return o->staff = clerk(s), 539;
    if (!index) return 974;
    sheet_open(s, index, 0);
    rooms_pb(s)[RB_MINUTE] = rooms_pb(s)[0x2F] = 0;
    return 0;
}

int rooms_admit(Sim *s, int to_waiting, RoomsResult *o) {
    *o = (RoomsResult){0};
    int index = u16(rooms_reception(s) + 10); /* queue place 1, whatever its state (no check, as the original) */
    rooms_pb(s)[RB_DECISION] = to_waiting ? 0xFF : 0;
    o->index = index;
    if (!to_waiting) {
        sim_news_refusal(s, H, index, 0x1C); /* 0x42916 */
        sim_player_leave(s, H);
        return 2266;
    }
    int room;
    switch (sim_player_seat(s, H, &room)) {
    case SEAT_NO_ROOMS: sim_player_leave(s, H); return 2265;
    case SEAT_FULL: sim_news_refusal(s, H, index, 0x1B), sim_player_leave(s, H); return 2259; /* 0x428BD */
    case SEAT_NO_FORM: return 2257;
    case SEAT_NO_PEN: return 2258;
    default: o->room = room; return 2261;
    }
}

int rooms_bed_open(Sim *s, RoomsResult *o) {
    *o = (RoomsResult){0};
    if (!rooms_count(s, 6)) return 2371;
    int ward = 0, total = 0, free_beds = sim_free_beds(s, H, 6, &ward, &total);
    if (!free_beds) return 2370;
    rooms_set_pb16(s, RB_WARD, ward);
    int r = clerk_refusal(s, o);
    if (r) return r;
    const uint8_t *queue = rooms_reception(s) + 0x46;
    int last = -1;
    for (int i = 0; i < 6; i++)
        if (u16(queue + 4 * i)) last = i;
    if (last < 0) {
        o->total = total, o->free_beds = free_beds;
        return 2367;
    }
    rooms_set_pb16(s, RB_QUEUE, 0x46 + 4 * last);
    return 0;
}

static int ward_free(Sim *s, int k) {
    const uint8_t *rec = rooms_record(s, 6, k);
    return rec[10] - rec[12] != 0;
}

void rooms_bed_ward_step(Sim *s, int step) {
    int n = rooms_count(s, 6), k = rooms_pb16(s, RB_WARD);
    /* the original loops for ever with every ward full; the entry made sure one has a free bed (cap, engine) */
    for (int t = 0; t < n; t++) {
        k = (k + step + n) % n;
        if (ward_free(s, k)) break;
    }
    rooms_set_pb16(s, RB_WARD, k);
}

int rooms_bed_assign(Sim *s, RoomsResult *o) {
    *o = (RoomsResult){0};
    int ward = rooms_pb16(s, RB_WARD), place = rooms_pb16(s, RB_QUEUE);
    int index = u16(rooms_reception(s) + place);
    int bed = sim_player_bed(s, H, ward, place, 0);
    if (bed < 0) return -1;
    o->index = index, o->room = ward, o->staff = clerk(s);
    o->screen = rooms_pb(s)[RB_FROM_98] ? 60 : 53;
    rooms_pb(s)[RB_FROM_98] = 0;
    return 2362;
}

int rooms_occupancy_open(Sim *s) { /* the free-bed sums of 0x2B4B3 / 0x2B52C are not shown on 149 */
    if (!rooms_count(s, 6) && !rooms_count(s, 16)) return 2371;
    rooms_set_pb16(s, RB_WARD, 0);
    rooms_set_pb16(s, RB_QUEUE, rooms_count(s, 6) ? 0 : -1);
    return 0;
}

void rooms_occupancy_step(Sim *s, int step) {
    /* all Krankenzimmer (pb+0xA = 0), then all neuro wards (FFFF), in a cycle; a kind with no rooms is skipped */
    int kind = rooms_pb16(s, RB_QUEUE) ? 1 : 0, k = rooms_pb16(s, RB_WARD) + step;
    int n[2] = {rooms_count(s, 6), rooms_count(s, 16)};
    if (k >= n[kind]) {
        if (n[!kind]) kind = !kind;
        k = 0;
    } else if (k < 0) {
        if (n[!kind]) kind = !kind;
        k = n[kind] - 1;
    }
    rooms_set_pb16(s, RB_QUEUE, kind ? -1 : 0);
    rooms_set_pb16(s, RB_WARD, k);
}

/* the seat or bed index of a patient in a waiting room or bed ward (0x23D6C) */
static int place_of(Sim *s, int index, int cat, int room) {
    const uint8_t *rec = rooms_record(s, cat, room);
    for (int i = 0; i < 10; i++)
        if (u16(rec + 0x14 + 4 * i) == index) return i;
    return 0;
}

int rooms_overview(Sim *s, int first) {
    if (first) {
        rooms_set_pb16(s, RB_PLACE, 0), rooms_set_pb16(s, RB_PAGE, 0);
        rooms_set_pb16(s, RB_PATIENT, 0), rooms_set_pb16(s, RB_ROOM, 0);
    }
    int i = rooms_pb16(s, RB_PATIENT);
    for (int t = 0; t < 1018; t++) { /* 0x23CDC */
        i = i + 1 > PATIENTS ? 1 : i + 1;
        const uint8_t *p = rooms_person(s, i);
        if (p[P_HOSPITAL] != H || (p[P_STATE] == 2 && p[P_ROOM])) continue;
        rooms_set_pb16(s, RB_PATIENT, i);
        rooms_set_pb16(s, RB_PAGE, 0);
        if (p[P_STATE] == 3 || p[P_STATE] == 6 || p[P_STATE] == 16) {
            int cat = p[P_STATE] == 3 ? 3 : p[P_STATE];
            rooms_set_pb16(s, RB_PLACE, place_of(s, i, cat, p[P_ROOM]));
            rooms_set_pb16(s, RB_ROOM, p[P_ROOM]);
        }
        rooms_pb(s)[RB_MINUTE] = 0;
        return 0;
    }
    return 541;
}

/* ---- waiting room, ward, nurse shows ----------------------------------------------------------------------- */

int rooms_seat(Sim *s, int seat) {
    int index = u16(rooms_record(s, 3, rooms_shown(s)) + 0x14 + 4 * seat);
    if (!index) return 0;
    sheet_open(s, index, seat);
    rooms_pb(s)[RB_MINUTE] = rooms_pb(s)[0x30] = 0;
    return index;
}

int rooms_bed_click(Sim *s, int bed) {
    int index = u16(rooms_record(s, 6, rooms_shown(s)) + 0x14 + 4 * bed);
    if (!index || rooms_person(s, index)[P_STATE] != 6) return 0;
    sheet_open(s, index, bed);
    rooms_pb(s)[0x33] = rooms_pb(s)[0x34] = rooms_pb(s)[0x35] = 0;
    return index;
}

int rooms_show(Sim *s, int cat) {
    uint8_t *rec = cat == 2 ? rooms_reception(s) : rooms_record(s, cat, rooms_shown(s));
    if (!u16(rec + 8)) return cat == 2 ? 550 : cat == 3 ? 551 : 548;
    int at = cat == 2 ? 0x45 : 0x0F, was = rec[at];
    uint8_t comfort = (uint8_t)(sim_bits(s, 15) + 22); /* 0x2B4A9 */
    s->dup_byte = (uint8_t)was; /* SIM-75: the show's old comfort is the newspaper scratch byte. */
    rec[at] = comfort;
    room_sound_show(s, cat, rec, was); /* 0x2F9A6 */
    return 0;
}

/* ---- treatment room ------------------------------------------------------------------------------------- */

static uint8_t *treatment_room(Sim *s) { return rooms_record(s, 5, rooms_shown(s)); }

int rooms_couch(Sim *s) {
    int index = u16(treatment_room(s) + 12);
    if (!index) return 398;
    sheet_open(s, index, 0);
    rooms_pb(s)[0x31] = rooms_pb(s)[0x32] = 0;
    return 0;
}

static int waiting_total(Sim *s) { /* 0x2D0C1 */
    int n = 0;
    for (int r = 0; r < rooms_count(s, 3) && r < 20; r++) n += rooms_record(s, 3, r)[12];
    return n;
}

int rooms_call(Sim *s, RoomsResult *o) {
    *o = (RoomsResult){0};
    const uint8_t *tr = treatment_room(s);
    int nurse = u16(tr + 8);
    if (!u16(tr + 10)) return 418;
    if (!nurse) return 414;
    if (rooms_independent(s, nurse)) return o->staff = nurse, 539;
    if (u16(tr + 12)) return 410;
    if (!waiting_total(s)) return 406;
    rooms_set_pb16(s, RB_CALL, 5);
    rooms_set_pb16(s, RB_CALL_ROOM, rooms_shown(s));
    if (!u16(tr + 8) || !u16(tr + 10)) return 500; /* 0x2E22C: cannot happen between two clicks in the engine */
    int from = 0, index = sim_player_call(s, H, rooms_shown(s), &from);
    put32(rooms_pb(s) + RB_CALL, 0);
    if (!index) return 406;
    o->index = index, o->room = from, o->staff = nurse;
    return 1340;
}

/* the refusals shared by the sheet's decisions (docs/ui-rooms.md, "61"): the doctor works independently - the
 * name printed is the staff index in the u16 at T+0x13, not the doctor's (an original fault, reproduced on the disk
 * build); both CD builds name the doctor, T+0x0A (docs/editions.md 7.2, OTH-DE-2) */
static int doctor_refusal(Sim *s, RoomsResult *o) {
    const uint8_t *tr = treatment_room(s);
    /* FIX-3 (docs/fixes.md): original (disk build) names the staff index in the u16 at +0x13; fixed names the
     * doctor at +0x0A, as both CD builds do in both modes */
    if (rooms_independent(s, u16(tr + 10)))
        return o->staff = u16(tr + (program_cd(s->prog) || fix_on(3) ? 0x0A : 0x13)), 42;
    return 0;
}
static int diagnosis_refusal(Sim *s) {
    int diag = u16(treatment_room(s) + 14);
    return diag == 999 ? 89 : diag ? 87 : 0;
}

int rooms_other(Sim *s, RoomsResult *o) {
    *o = (RoomsResult){0};
    const uint8_t *tr = treatment_room(s);
    int r = doctor_refusal(s, o);
    if (r) return r;
    if (u16(tr + 16)) return 78;
    if ((r = diagnosis_refusal(s))) return r;
    if (u16(tr + 18)) return 84;
    sim_player_propose(s, H, rooms_shown(s));
    return 0;
}

int rooms_reaction(int v) {
    if (v > 0) return 5 + (v > 3 ? 3 : v);
    int loss = -v;
    return loss < 3 ? 5 : loss < 8 ? 4 : loss < 12 ? 3 : loss < 16 ? 2 : loss < 20 ? 1 : 0;
}

static int invoice(Sim *s, RoomsResult *o) { /* 0x2D957 with [0x4879] = 60 */
    int v;
    o->index = u16(treatment_room(s) + 12);
    o->bill = sim_player_bill(s, H, rooms_shown(s), &v);
    o->reaction = rooms_reaction(v);
    return 14;
}

static int donor_text(int result) { return result == 0 ? 36 : result == 1 ? 38 : 40; }

int rooms_accept(Sim *s, RoomsResult *o) {
    *o = (RoomsResult){0};
    const uint8_t *tr = treatment_room(s);
    int r = doctor_refusal(s, o);
    if (r) return r;
    if ((r = diagnosis_refusal(s))) return r;
    if (u16(tr + 16)) return 79;
    int n = 0, index = u16(tr + 12);
    o->index = index;
    switch (sim_player_after_diagnosis(s, H, rooms_shown(s), &n)) {
    case AFTER_IN_ROOM: return 0;
    case AFTER_BILL: return invoice(s, o);
    case AFTER_DONOR: return donor_text(n);
    case AFTER_RUNNING: return 84;
    case AFTER_WAITING: o->room = n; return 32;
    case AFTER_LEFT: return 34;
    case AFTER_QUEUE_FULL: return 427;
    case AFTER_NO_WARD: return 2371;
    default: break;
    }
    /* queued for a bed: screen 98 when a ward has a free bed (0x1B2DC); its "assign" image only with a clerk who
     * does not work independently; with an independent clerk the screen stays 60 */
    int ward = 0;
    o->screen = 60;
    if (rooms_count(s, 6) && sim_free_beds(s, H, 6, &ward, NULL)) {
        if (!clerk(s)) o->screen = 98;
        else if (!rooms_independent(s, clerk(s))) o->screen = 98, o->bed_button = 1;
    }
    return 425;
}

int rooms_abort(Sim *s, RoomsResult *o) {
    *o = (RoomsResult){0};
    int n = 0;
    o->index = u16(treatment_room(s) + 12);
    if (sim_player_abort(s, H, rooms_shown(s), &n)) return donor_text(n);
    return 77;
}

int rooms_hand_bill(Sim *s, RoomsResult *o) {
    *o = (RoomsResult){0};
    const uint8_t *tr = treatment_room(s);
    const uint8_t *p = rooms_person(s, u16(tr + 12));
    int r = diagnosis_refusal(s);
    if (r) return r;
    if (u16(tr + 18)) return 84;
    if (p[P_WISH] == 5) return 86;
    if (!p[P_IN_TREATMENT]) return 84;
    return invoice(s, o);
}

/* the inventory buttons' checks and fields are purchase.c's (docs/ui-purchasing.md, "Entry from a room") */
int rooms_inventory(Sim *s, int cat, uint32_t back) { return purchase_inventory_button(s, cat, back); }

/* ---- ward patient and the corpse button ------------------------------------------------------------------ */

static uint8_t *ward_room(Sim *s) { return rooms_record(s, 6, rooms_shown(s)); }

int rooms_discharge(Sim *s, RoomsResult *o) {
    *o = (RoomsResult){0};
    int hour = world_tail(s->world, TAIL_HOUR), index = rooms_pb16(s, RB_PATIENT);
    const uint8_t *p = rooms_person(s, index);
    o->index = index;
    if (hour >= 21 || hour < 4) return 2282;
    if (p[P_RECOVERY]) return 2279;
    if (p[P_HEALTH] < 5) return 2280;
    int v;
    o->bill = sim_player_discharge(s, H, rooms_shown(s), rooms_pb16(s, RB_PLACE), &v);
    o->reaction = rooms_reaction(v);
    return 14;
}

void rooms_step(Sim *s) { sim_player_step(s, rooms_pb16(s, RB_PATIENT)); }

int rooms_icu(Sim *s, RoomsResult *o) {
    *o = (RoomsResult){0};
    int index = rooms_pb16(s, RB_PATIENT);
    o->index = index, o->staff = u16(ward_room(s) + 8);
    int r = sim_player_icu(s, H, rooms_shown(s), index);
    if (r == -1) return 535;
    if (r == -2) return 536;
    o->room = r;
    return 1340;
}

int rooms_corpse(Sim *s, int cat, RoomsResult *o) {
    *o = (RoomsResult){0};
    int index = rooms_pb16(s, RB_PATIENT), room = cat == 2 ? 0 : rooms_shown(s);
    o->index = index;
    int r = sim_player_corpse(s, H, index, cat, room, rooms_pb16(s, RB_PLACE), &o->staff);
    return r == 0 ? 1340 : r == 1 ? 537 : 538;
}
