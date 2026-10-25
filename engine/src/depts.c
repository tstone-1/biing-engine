/* The player's decisions in the specialist departments (docs/ui-departments.md); see depts.h. The order of the
 * refusals is the spec's; the moves are the shared helpers of sim_rooms.c in their player branches. */
#include "fixes.h"
#include "room_sound.h"
#include "depts.h"

#include "sim_internal.h"

#define H 0
enum { P_STATE = 4, P_HEALTH = 44, P_ROOM = 66, P_IN_TREATMENT = 79, P_DEAD = 104 };

static int u16(const uint8_t *p) { return (uint16_t)get16(p); }

uint8_t *depts_room(Sim *s, int cat) { return rooms_record(s, cat, cat == 18 ? 0 : rooms_shown(s)); }

static void sheet_open(Sim *s, int index, int place) {
    rooms_set_pb16(s, RB_PLACE, place);
    rooms_set_pb16(s, RB_PAGE, 0);
    rooms_set_pb16(s, RB_PATIENT, index);
}

static int waiting_total(Sim *s) { /* 0x2D0C1 */
    int n = 0;
    for (int r = 0; r < rooms_count(s, 3) && r < 10; r++) n += rooms_record(s, 3, r)[12];
    return n;
}

/* ---- dental station and neurology ------------------------------------------------------------------------- */

static int dental(int cat) { return cat == 11; }

int depts_clinic_patient(Sim *s, int cat) {
    const uint8_t *r = depts_room(s, cat);
    if (!u16(r + 12)) return dental(cat) ? 399 : 397;
    sheet_open(s, u16(r + 12), 0);
    int b = dental(cat) ? DB_DENTAL_SHEET : DB_NEURO_SHEET;
    rooms_pb(s)[b] = rooms_pb(s)[b + 1] = 0;
    return 0;
}

int depts_clinic_call(Sim *s, int cat, RoomsResult *o) {
    *o = (RoomsResult){0};
    const uint8_t *r = depts_room(s, cat);
    int nurse = u16(r + 8), d = dental(cat);
    if (!u16(r + 10)) return d ? 419 : 418;
    if (!nurse) return d ? 415 : 414;
    if (rooms_independent(s, nurse)) return o->staff = nurse, 539;
    if (u16(r + 12)) return d ? 411 : 410;
    if (!waiting_total(s)) return d ? 409 : 406; /* the dental station prints the torture chamber's 409 (reproduce) */
    rooms_set_pb16(s, RB_CALL, cat);
    rooms_set_pb16(s, RB_CALL_ROOM, rooms_shown(s));
    int from = 0, index = sim_player_call_in(s, H, cat, rooms_shown(s), &from);
    put32(rooms_pb(s) + RB_CALL, 0);
    if (!index) return d ? 409 : 406;
    o->index = index, o->room = from, o->staff = nurse;
    return 1340;
}

/* the refusals of the sheet: the doctor works independently (the disk build prints the staff index in the u16 at
 * R+0x13, as on screen 61: reproduce; both CD builds name the doctor, R+0x0A, docs/editions.md 7.2, OTH-DE-2 and
 * OTH-DE-13), the diagnosis not started or running */
static int doctor_refusal(Sim *s, int cat, RoomsResult *o) {
    const uint8_t *r = depts_room(s, cat);
    /* FIX-3 (docs/fixes.md): original (disk build) names the staff index in the u16 at +0x13; fixed names the
     * doctor at +0x0A, as both CD builds do in both modes */
    if (rooms_independent(s, u16(r + 10)))
        return o->staff = u16(r + (program_cd(s->prog) || fix_on(3) ? 0x0A : 0x13)), 42;
    return 0;
}
static int diagnosis_refusal(Sim *s, int cat) {
    int diag = u16(depts_room(s, cat) + 14);
    return diag == 999 ? (dental(cat) ? 89 : 396) : diag ? (dental(cat) ? 87 : 88) : 0;
}

int depts_clinic_other(Sim *s, int cat, RoomsResult *o) {
    *o = (RoomsResult){0};
    int r = doctor_refusal(s, cat, o);
    if (r) return r;
    if (u16(depts_room(s, cat) + 16)) return 78;
    if ((r = diagnosis_refusal(s, cat))) return r;
    sim_player_propose_in(s, H, cat, rooms_shown(s));
    return 0;
}

static int invoice(Sim *s, int cat, RoomsResult *o) { /* 0x2D976 / 0x2D93D */
    int v;
    o->index = u16(depts_room(s, cat) + 12);
    o->bill = sim_player_bill_in(s, H, cat, rooms_shown(s), &v);
    o->reaction = rooms_reaction(v);
    return 14;
}

int depts_clinic_accept(Sim *s, int cat, RoomsResult *o) {
    *o = (RoomsResult){0};
    int r = doctor_refusal(s, cat, o);
    if (r) return r;
    if ((r = diagnosis_refusal(s, cat))) return r;
    if (u16(depts_room(s, cat) + 16)) return dental(cat) ? 80 : 81;
    int n = 0;
    o->index = u16(depts_room(s, cat) + 12);
    switch (sim_player_after_clinic(s, H, cat, rooms_shown(s), &n)) {
    case AFTER_RUNNING: return dental(cat) ? 84 : 85;
    case AFTER_BILL: return invoice(s, cat, o);
    case AFTER_WAITING: o->room = n; return 32;
    case AFTER_LEFT: return 34;
    case AFTER_IN_ROOM: return 0;
    case AFTER_NO_WARD: return 1462;
    case AFTER_QUEUED: rooms_set_pb16(s, DB_NEURO_WARD, n); return 81;
    default: return -1;
    }
}

int depts_clinic_abort(Sim *s, int cat, RoomsResult *o) {
    *o = (RoomsResult){0};
    int n = 0;
    o->index = u16(depts_room(s, cat) + 12);
    sim_player_abort_in(s, H, cat, rooms_shown(s), &n);
    return 77;
}

int depts_clinic_bill(Sim *s, int cat, RoomsResult *o) {
    *o = (RoomsResult){0};
    const uint8_t *r = depts_room(s, cat);
    int running = dental(cat) ? 84 : 85, e = diagnosis_refusal(s, cat);
    if (e) return e;
    if (u16(r + 18)) return running;
    if (!rooms_person(s, u16(r + 12))[P_IN_TREATMENT]) return running;
    if (dental(cat) && !u16(r + 16)) return 84; /* the neurology's bill (0x35E59) has no such test */
    return invoice(s, cat, o);
}

static int neuro_ward_free(Sim *s, int k) {
    const uint8_t *rec = rooms_record(s, 16, k);
    return rec[12] < rec[10];
}

void depts_neuro_bed_step(Sim *s, int step) {
    int n = rooms_count(s, 16), k = rooms_pb16(s, DB_NEURO_WARD);
    /* with every neuro ward full the original loops for ever (cannot happen from the entry; cap, engine) */
    for (int t = 0; n && t < n; t++) {
        k = (k + step + n) % n;
        if (neuro_ward_free(s, k)) break;
    }
    rooms_set_pb16(s, DB_NEURO_WARD, k);
}

int depts_neuro_bed(Sim *s, RoomsResult *o) {
    *o = (RoomsResult){0};
    const uint8_t *nr = depts_room(s, 15);
    int ward = rooms_pb16(s, DB_NEURO_WARD);
    o->index = u16(nr + 12), o->staff = u16(nr + 8), o->room = ward; /* the neurology's nurse */
    if (sim_player_neuro_bed(s, H, rooms_shown(s), ward) < 0) return -1;
    return 2362;
}

/* ---- neuro ward ---------------------------------------------------------------------------------------------- */

int depts_neuro_ward_bed(Sim *s, int bed) {
    int index = u16(depts_room(s, 16) + 0x14 + 4 * bed);
    if (!index || rooms_person(s, index)[P_STATE] != 16) return 0;
    sheet_open(s, index, bed);
    rooms_pb(s)[0x33] = rooms_pb(s)[0x37] = rooms_pb(s)[0x38] = 0;
    return index;
}

int depts_neuro_discharge(Sim *s, RoomsResult *o) {
    *o = (RoomsResult){0};
    int hour = world_tail(s->world, TAIL_HOUR), index = rooms_pb16(s, RB_PATIENT);
    const uint8_t *p = rooms_person(s, index), *k = depts_room(s, 16);
    o->index = index;
    if (hour >= 21 || hour < 4) return 2282;
    if (p[P_HEALTH] < 5) return 2280; /* no narcosis test (the Krankenzimmer refuses with 2279) */
    /* P not in state 16 or not in the ward: the original loops for ever (cap, engine: nothing happens) */
    int bed = -1;
    for (int i = 0; i < 10 && bed < 0; i++)
        if (u16(k + 0x14 + 4 * i) == index) bed = i;
    if (p[P_STATE] != 16 || bed < 0) return -1;
    int v;
    o->bill = sim_player_discharge_in(s, H, 16, rooms_shown(s), bed, &v);
    o->reaction = rooms_reaction(v);
    return 14;
}

void depts_neuro_step(Sim *s) { sim_player_step(s, rooms_pb16(s, RB_PATIENT)); } /* 0x4F16C or the interrupt */

/* ---- intensive care ------------------------------------------------------------------------------------------ */

int depts_icu_patient(Sim *s) {
    int index = u16(depts_room(s, 9) + 12);
    if (!index) return 399;
    sheet_open(s, index, 0);
    rooms_pb(s)[0x33] = rooms_pb(s)[0x42] = rooms_pb(s)[0x43] = 0;
    return 0;
}

int depts_icu_back(Sim *s, RoomsResult *o) {
    *o = (RoomsResult){0};
    o->index = u16(depts_room(s, 9) + 12);
    int r = sim_player_icu_back(s, H, rooms_shown(s));
    return r == -1 ? 423 : r == -2 ? 424 : 421;
}

/* ---- OP and torture chamber ------------------------------------------------------------------------------------ */

int depts_theatre_patient(Sim *s, int cat) {
    int index = u16(depts_room(s, cat) + 12);
    if (!index) return cat == 7 ? -1 : 83;
    sheet_open(s, index, 0);
    rooms_pb(s)[0x33] = rooms_pb(s)[0x34] = rooms_pb(s)[0x35] = 0;
    return 0;
}

static int beds_taken(Sim *s, int cat) { /* 0x4E696 / 0x4EB5B */
    int n = 0;
    for (int k = 0; k < rooms_count(s, cat) && k < 10; k++) n += rooms_record(s, cat, k)[12];
    return n;
}

int depts_theatre_fetch(Sim *s, int cat, RoomsResult *o) {
    *o = (RoomsResult){0};
    const uint8_t *r = depts_room(s, cat);
    int nurse = u16(r + 8), op = cat == 7;
    if (!op && u16(r + 12)) return depts_theatre_patient(s, cat); /* over the patient's head: the sheet */
    if (!nurse) return op ? 416 : 417;
    if (rooms_independent(s, nurse)) return o->staff = nurse, 539;
    if (op && u16(r + 12)) return 412;
    int none = op ? 407 : 408;
    if (!beds_taken(s, op ? 6 : 16)) return none;
    rooms_set_pb16(s, RB_CALL, cat);
    rooms_set_pb16(s, RB_CALL_ROOM, rooms_shown(s));
    int ward = 0, index = sim_player_theatre_fetch(s, H, cat, rooms_shown(s), &ward);
    put32(rooms_pb(s) + RB_CALL, 0);
    if (!index) return none;
    o->index = index, o->room = ward, o->staff = nurse;
    return 501;
}

int depts_theatre_toggle(Sim *s, int cat, RoomsResult *o) {
    *o = (RoomsResult){0};
    if (rooms_independent(s, u16(depts_room(s, cat) + 10))) return cat == 7 ? 3017 : 3018;
    int r = sim_player_theatre_toggle(s, H, cat, rooms_shown(s));
    if (r == THEATRE_STOPPED || r == THEATRE_STARTED) return 0;
    if (r == THEATRE_NO_ORGAN) return 530;
    o->item = r;
    return cat == 7 ? 531 : 533;
}

int depts_theatre_back(Sim *s, int cat, RoomsResult *o) {
    *o = (RoomsResult){0};
    const uint8_t *r = depts_room(s, cat);
    int index = u16(r + 12);
    o->index = index, o->staff = u16(r + 8);
    int done = sim_player_theatre_back(s, H, cat, rooms_shown(s));
    const uint8_t *p = rooms_person(s, index);
    o->room = p[P_ROOM], o->total = p[P_STATE];
    if (cat == 7) return done == 0 ? 2269 : done == 1 ? 2275 : 2277;
    return done == 0 ? 1331 : done == 1 ? 1336 : 1338;
}

/* ---- pathology --------------------------------------------------------------------------------------------------- */

int depts_path_fetch(Sim *s, RoomsResult *o) {
    *o = (RoomsResult){0};
    const uint8_t *r = depts_room(s, 18);
    if (u16(r + 12)) return 402;
    if (!u16(r + 6)) return 400;
    if (!u16(r + 10)) return 401;
    o->staff = u16(r + 10);
    o->index = sim_player_pathology_take(s, H);
    return 1490;
}

int depts_path_open(Sim *s) {
    int index = u16(depts_room(s, 18) + 12);
    if (!index) return 403;
    sheet_open(s, index, 0);
    rooms_pb(s)[DB_PATH_SHEET] = 0;
    return 0;
}

int depts_path_throw(Sim *s, RoomsResult *o) {
    *o = (RoomsResult){0};
    const uint8_t *r = depts_room(s, 18);
    o->index = u16(r + 12), o->staff = u16(r + 10); /* no pathologist test: an empty field names staff record 0 */
    if (!o->index) return -1; /* engine guard: the button is only on the open case's sheet */
    int st = sim_player_pathology_throw(s, H);
    return st == 0 ? 1488 : st == 1 ? 1486 : 1484;
}

/* ---- shared --------------------------------------------------------------------------------------------------------- */

int depts_show(Sim *s, int cat) {
    uint8_t *r = depts_room(s, cat);
    if (!u16(r + 8)) return cat == 11 ? 552 : cat == 16 ? 549 : cat == 9 ? 553 : 547;
    int at = cat == 11 ? 0x19 : cat == 7 ? 0x17 : 0x0F, was = r[at];
    uint8_t comfort = (uint8_t)(sim_bits(s, 15) + 22); /* 0x2B4A9 */
    s->dup_byte = (uint8_t)was; /* SIM-75: also written when a show was already running. */
    r[at] = comfort;
    room_sound_show(s, cat, r, was); /* 0x2F9A6 */
    return 0;
}

int depts_corpse(Sim *s, int cat, RoomsResult *o) {
    *o = (RoomsResult){0};
    int index = rooms_pb16(s, RB_PATIENT), staff, nurse = u16(depts_room(s, cat) + 8);
    o->index = index;
    int r = sim_player_corpse(s, H, index, cat, rooms_shown(s), rooms_pb16(s, RB_PLACE), &staff);
    o->staff = nurse; /* unlike the core rooms, these name the room's own nurse */
    return r == 0 ? 1340 : r == 1 ? 537 : 538;
}
