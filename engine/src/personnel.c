/* The player's personnel rules (docs/ui-personnel.md). The AI's tables and helpers of sim_staff.c are reused:
 * the idle candidate of 0x22D73 (sim_idle_candidate), the job openings and staff slots. Quirks of the original
 * are kept on purpose and marked "quirk". */
#include "personnel.h"

#include "sim_internal.h"

#define STAFF_RECORDS 1041
#define STAFF_RECORD_SIZE 32
#define COURSES 20
#define COURSE_SIZE 10
#define CATALOG_COURSES 0x40D9
#define CATALOG_COURSE_SIZE 22
#define H_WAGES_DAY 0xA6
#define H_WAGES_TOTAL 0xAA
#define H_COURSES_DAY 0x11C
#define H_COURSES_TOTAL 0x13E

/* staff record and slot bytes (docs/formats.md block B, docs/sim-staff.md) */
enum { S_SEX = 0, S_GROUP = 1, S_RATING = 2, S_HOSPITAL = 3, S_STATE = 4, S_BUST = 5, S_WAIST = 6, S_HIP = 7,
       S_INDEPENDENT = 11, S_COURSE_SKILL = 12, S_GENERAL_SKILL = 13, S_SKILL = 15, S_EXPERIENCE = 16, S_WAGE = 19,
       S_DISMISSED_BY = 22, S_MARK = 23, S_ROOM = 26, S_RESERVED = 27 };
enum { SL_INDEX = 0, SL_GROUP = 2, SL_STATE = 3, SL_ROOM = 4, SL_5 = 5, SL_WAGE = 6, SL_HOURS = 7, SL_ON_DUTY = 10,
       SL_OFF_DUTY = 11, SL_RETURN_CAT = 16, SL_RETURN_ROOM = 17, SL_REST = 18 };
enum { OFF_DUTY = 0x13, TRAINING = 0x19 };

uint8_t *personnel_pb(Sim *s) { return s->world->block[BLK_PLAYER0]; }
int personnel_pb16(Sim *s, int off) { return get16(personnel_pb(s) + off); }
void personnel_set_pb16(Sim *s, int off, int v) { put16(personnel_pb(s) + off, v); }
uint8_t *personnel_staff(Sim *s, int i) { return s->world->block[BLK_STAFF] + i * STAFF_RECORD_SIZE; }
uint8_t *personnel_slot(Sim *s, int i) { return staff_slots(s, 0) + i * STAFF_SLOT_SIZE; }
uint8_t *personnel_opening(Sim *s, int j) { return s->world->block[BLK_JOB_OPENINGS] + j * JOB_OPENING_SIZE; }
uint8_t *personnel_course(Sim *s, int e) { return s->world->block[BLK_COURSES] + e * COURSE_SIZE; }
const uint8_t *personnel_catalog(Sim *s, int id) {
    return program_table(s->prog, CATALOG_COURSES) + (uint32_t)id * CATALOG_COURSE_SIZE;
}

static uint8_t *h0(Sim *s) { return hospital(s, 0); }
static uint8_t *staff_of(Sim *s, int slot) { return personnel_staff(s, (uint16_t)get16(personnel_slot(s, slot))); }

static void pay(Sim *s, int amount, int day, int total) {
    add32(h0(s) + H_CASH, -amount);
    add32(h0(s) + day, amount);
    add32(h0(s) + total, amount);
}

/* ---- the sheet's search ------------------------------------------------------------------------------- */

int personnel_state_fits(int f, int state) {
    switch (f) {
    case FILTER_IDLE: return state == 0;
    case FILTER_DISMISS: return state == 0 || state == OFF_DUTY;
    case FILTER_ROOMS: return (state >= 2 && state <= 18) || state == 0x14 || state == 0x15;
    case FILTER_HOME: return state == OFF_DUTY || state == 0x17 || state == 0x18;
    case FILTER_AWAY: return state == 0x16 || state == TRAINING;
    default: return 1; /* any other filter matches everything */
    }
}

int personnel_slot_matches(Sim *s, int slot, int filter, int group) {
    const uint8_t *sl = personnel_slot(s, slot);
    return get16(sl + SL_INDEX) && (group == 0 || group == sl[SL_GROUP]) && personnel_state_fits(filter, sl[SL_STATE]);
}

int personnel_search(Sim *s, int from, int step, int tries, int filter, int group) {
    int p = ((from % STAFF_SLOTS) + STAFF_SLOTS) % STAFF_SLOTS;
    for (int k = 0; k < tries; k++, p = (p + step + STAFF_SLOTS) % STAFF_SLOTS)
        if (personnel_slot_matches(s, p, filter, group)) return p;
    return -1;
}

int personnel_nobody_text(int filter) {
    switch (filter) {
    case FILTER_IDLE: return 786;
    case FILTER_DISMISS: return 787;
    case FILTER_ROOMS: return 788;
    case FILTER_HOME: return 789;
    case FILTER_AWAY: return 790;
    default: return 1500; /* unreachable from the five buttons */
    }
}

int personnel_dismissal_allowed(Sim *s) {
    return world_tail(s->world, TAIL_HOUR) >= 4 ? PERS_DISMISSAL_HOURS : 0;
}

/* ---- job ads ------------------------------------------------------------------------------------------ */

/* 0x3A2D0 defaults for groups 3-10: wage, training, second criterion (group 2 and 1 read the player block) */
static const uint8_t AD_DEFAULTS[11][3] = {
    [3] = {5, 0, 29}, [4] = {6, 0, 29}, [5] = {10, 5, 70}, [6] = {6, 0, 29}, [7] = {7, 0, 29},
    [8] = {6, 0, 29}, [9] = {9, 4, 72}, [10] = {9, 4, 70},
};

void personnel_ad_defaults(Sim *s, int g) {
    int wage, bust = 0, training, second;
    if (g == 1) {
        wage = personnel_pb16(s, PB_NURSE_WAGE), bust = personnel_pb16(s, PB_NURSE_BUST);
        training = personnel_pb16(s, PB_NURSE_TRAINING), second = 0;
    } else if (g == 2) {
        wage = 9, training = personnel_pb16(s, PB_DOCTOR_TRAINING), second = personnel_pb16(s, PB_DOCTOR_CRITERION);
    } else {
        wage = AD_DEFAULTS[g][0], training = AD_DEFAULTS[g][1], second = AD_DEFAULTS[g][2];
    }
    personnel_set_pb16(s, PB_WAGE, wage);
    personnel_set_pb16(s, PB_BUST, bust);
    personnel_set_pb16(s, PB_TRAINING, training);
    personnel_set_pb16(s, PB_CRITERION, second);
    personnel_set_pb16(s, PB_GROUP, g);
}

/* Each limit is tested before the step ("+5 up to 140": while below 140, so 29 + 5n can reach 144). */
static int arrow(int v, int up, int step, int top, int floor_from) {
    if (up) return v < top ? v + step : v;
    return v >= floor_from ? v - step : v;
}

void personnel_ad_arrow(Sim *s, int field, int up) {
    int nurse = personnel_pb16(s, PB_GROUP) == 1, v;
    switch (field) {
    case AD_WAGE:
        v = arrow(personnel_pb16(s, PB_WAGE), up, 1, nurse ? 40 : 50, 2);
        personnel_set_pb16(s, PB_WAGE, v);
        if (nurse) personnel_set_pb16(s, PB_NURSE_WAGE, v);
        break;
    case AD_SECOND:
        if (nurse) {
            v = arrow(personnel_pb16(s, PB_BUST), up, 1, 150, 70); /* floor 69 */
            personnel_set_pb16(s, PB_BUST, v);
            personnel_set_pb16(s, PB_NURSE_BUST, v);
        } else {
            v = arrow(personnel_pb16(s, PB_CRITERION), up, 5, 140, 30); /* the default 29 cannot go down */
            personnel_set_pb16(s, PB_CRITERION, v);
            personnel_set_pb16(s, PB_DOCTOR_CRITERION, v); /* every group writes it, only group 2 reads it */
        }
        break;
    default:
        v = arrow(personnel_pb16(s, PB_TRAINING), up, 1, 90, 1);
        personnel_set_pb16(s, PB_TRAINING, v);
        personnel_set_pb16(s, nurse ? PB_NURSE_TRAINING : PB_DOCTOR_TRAINING, v);
        break;
    }
}

int personnel_ad_next_group(int g) { return g >= 10 ? 1 : g + 1; }

int personnel_publish(Sim *s) {
    int j = 0;
    while (j < JOB_OPENINGS && (int8_t)personnel_opening(s, j)[0] >= 0) j++;
    if (j == JOB_OPENINGS) return -1;
    uint8_t *o = personnel_opening(s, j);
    o[0] = 0; /* nothing is paid now: 0x2B6C3 charges 1 per hour */
    o[1] = (uint8_t)personnel_pb16(s, PB_GROUP);
    o[2] = (uint8_t)personnel_pb16(s, PB_WAGE);
    o[3] = (uint8_t)personnel_pb16(s, PB_BUST);
    o[4] = (uint8_t)personnel_pb16(s, PB_CRITERION); /* quirk: compared with byte 15 for every group */
    o[5] = (uint8_t)personnel_pb16(s, PB_TRAINING);
    o[6] = 0;
    return j;
}

/* ---- applications ------------------------------------------------------------------------------------- */

void personnel_shuffle(Sim *s, uint16_t list[PERSONNEL_LIST]) {
    for (int i = 0; i < PERSONNEL_LIST; i++) list[i] = (uint16_t)(i < PERSONNEL_POSITIONS ? i + 1 : 0);
    for (int k = 0; k < 6000; k++) { /* quirk: entry 0 never moves, the 0 after the last number can */
        int a = (int)sim_random(s, PERSONNEL_POSITIONS) + 1, b = (int)sim_random(s, PERSONNEL_POSITIONS) + 1;
        uint16_t t = list[a];
        list[a] = list[b];
        list[b] = t;
    }
}

int personnel_is_applicant(Sim *s, int index) {
    if (index < 0 || index >= STAFF_RECORDS) return 0;
    const uint8_t *st = personnel_staff(s, index);
    return (int8_t)st[S_HOSPITAL] < 0 && (int8_t)st[S_RESERVED] >= 0 && personnel_opening(s, st[S_RESERVED])[0] == 0;
}

int personnel_next_applicant(Sim *s, const uint16_t *list, int p) {
    for (int k = 0; k < 1051; k++) {
        if (++p > PERSONNEL_POSITIONS) p = 1;
        if (personnel_is_applicant(s, list[p])) return p;
    }
    return -1;
}

int personnel_prev_applicant(Sim *s, const uint16_t *list, int p) {
    for (int k = 0; k < 1051; k++) { /* engine choice: the same bound as "next" (the spec gives none) */
        if (--p < 1) p = PERSONNEL_POSITIONS;
        if (personnel_is_applicant(s, list[p])) return p;
    }
    return -1;
}

static void release_reservations(Sim *s, int j) {
    for (int i = 0; i < STAFF_RECORDS; i++) {
        uint8_t *st = personnel_staff(s, i);
        if (st[S_GROUP] && (int8_t)st[S_HOSPITAL] < 0 && st[S_RESERVED] == j) st[S_RESERVED] = 0xFF;
    }
}

int personnel_hire(Sim *s, int index, int *slot_out) {
    if (cash_of(s, 0) < 20) return PERS_BROKE;
    int k = 0;
    while (k < STAFF_SLOTS && get16(personnel_slot(s, k) + SL_INDEX)) k++;
    if (k == STAFF_SLOTS) return PERS_ENOUGH_STAFF;
    uint8_t *st = personnel_staff(s, index), *sl = personnel_slot(s, k);
    int j = st[S_RESERVED];
    uint8_t *o = personnel_opening(s, j);
    int wage = o[2]; /* quirk: the ad's wage, not the demand + 2 the AI pays */
    put16(sl + SL_INDEX, index);
    sl[SL_GROUP] = o[1];
    sl[SL_REST] = 0xFF;
    sl[SL_STATE] = sl[SL_ROOM] = sl[SL_5] = 0;
    sl[SL_WAGE] = (uint8_t)wage; /* +7 ... +17, +19 stay as the free slot had them */
    st[S_WAGE] = (uint8_t)wage;
    pay(s, wage, H_WAGES_DAY, H_WAGES_TOTAL); /* one day's wage at once */
    st[S_HOSPITAL] = 0;
    st[S_STATE] = 0;
    st[S_RESERVED] = 0xFF;
    release_reservations(s, j);
    o[0] = 0xFF;
    if (slot_out) *slot_out = k;
    return 0;
}

void personnel_tear_up(Sim *s, int index) {
    uint8_t *st = personnel_staff(s, index);
    st[S_RESERVED] = 0xFF;
    st[S_DISMISSED_BY] = 0; /* quirk: the dismissal mark, so this person never takes a player ad again */
}

/* ---- the personnel sheet ------------------------------------------------------------------------------ */

int personnel_independence(const uint8_t *st) {
    int x, courses = st[S_COURSE_SKILL] + st[S_GENERAL_SKILL], exp = (uint16_t)get16(st + S_EXPERIENCE);
    if (st[S_SEX] == 1) {
        x = 1100 - 8 * st[S_RATING] - 128 * courses;
        if (st[S_BUST] >= 120) x -= st[S_BUST] - 115;
        if (st[S_WAIST] < 52) x -= 4 * (55 - st[S_WAIST]);
        if (st[S_HIP] >= 96) x -= 4 * (st[S_HIP] - 94);
        x -= exp;
    } else {
        x = 1200 - 8 * st[S_RATING] - 128 * courses - st[S_SKILL] - exp;
    }
    return (int16_t)x;
}

static int any16(const uint8_t *rec, const uint8_t *offs, int n) {
    for (int i = 0; i < n; i++)
        if (get16(rec + offs[i])) return 1;
    return 0;
}

int personnel_room_release(Sim *s, int slot) {
    uint8_t *sl = personnel_slot(s, slot);
    int cat = sl[SL_STATE], group = sl[SL_GROUP], index = (uint16_t)get16(sl + SL_INDEX);
    if (cat <= 1) return 0;
    if (cat > 18) return 1; /* 0x13 and above; also keeps sim_room_record inside its table */
    uint8_t *rec = sim_room_record(s, 0, cat, sl[SL_ROOM]);
    static const uint8_t RECEPTION_BUSY[] = {0x0A, 0x0E, 0x12, 0x16, 0x46, 0x4A, 0x4E, 0x52, 0x56, 0x5A};
    switch (cat) {
    case 2:
        if (any16(rec, RECEPTION_BUSY, (int)sizeof RECEPTION_BUSY)) return 1;
        put16(rec + 8, 0);
        rec[0x45] = 0;
        return 0;
    case 3: case 6: case 9: case 16:
        rec[0x0F] = 0;
        put16(rec + 8, 0);
        return 0;
    case 4: put16(rec + 4, 0); return 0;
    case 5: case 8: case 11: case 15:
        if (get16(rec + 0x0C)) return 1;
        if (group == 1) {
            put16(rec + 8, 0);
            if (cat == 11) rec[0x19] = 0;
        } else {
            put16(rec + 0x0A, 0);
        }
        return 0;
    case 7:
        if (get16(rec + 0x0C)) return 1;
        if (group == 1) put16(rec + 8, 0), rec[0x17] = 0;
        else if (group == 2) put16(rec + 6, 0);
        else put16(rec + 0x0A, 0);
        return 0;
    case 10: /* refused while a race runs ([0x552A]), for another group, or while the garage's car is entered */
        if (get16(s->world->block[BLK_34]) || group != 6 || rec[0x0C]) return 1;
        put16(rec + ((uint16_t)get16(rec + 6) == index ? 6 : 8), 0);
        return 0;
    case 12:
        if (group != 1 || rec[5]) return 1;
        put16(rec + 2, 0);
        return 0;
    case 13:
        rec[5] = 0;
        put16(rec + 6, 0);
        put32(rec + 0x0A, 0);
        put16(rec + 0x0E, 0);
        return 0;
    case 14:
        if (group != 1 || rec[0x19]) return 1;
        put16(rec + ((uint16_t)get16(rec + 2) == index ? 2 : 4), 0);
        return 0;
    case 17: put16(rec + (group == 1 ? 8 : 0x0A), 0); return 0;
    case 18:
        if (get16(rec + 0x0C)) return 1;
        put16(rec + 0x0A, 0);
        return 0;
    default: return 1;
    }
}

int personnel_send_home(Sim *s, int slot) {
    uint8_t *sl = personnel_slot(s, slot), *st = staff_of(s, slot);
    if (st[S_STATE] == OFF_DUTY) return PERS_ALREADY_HOME;
    if (personnel_room_release(s, slot)) return PERS_BUSY;
    sl[SL_HOURS] = sl[SL_ON_DUTY] = sl[SL_OFF_DUTY] = 0;
    sl[SL_RETURN_CAT] = sl[SL_RETURN_ROOM] = 0;
    if (st[S_INDEPENDENT]) { /* the room to return to after the rest */
        sl[SL_RETURN_CAT] = sl[SL_STATE];
        sl[SL_RETURN_ROOM] = sl[SL_ROOM];
    }
    sl[SL_STATE] = st[S_STATE] = OFF_DUTY;
    sl[SL_ROOM] = st[S_ROOM] = 0;
    return 0;
}

int personnel_release(Sim *s, int slot) {
    uint8_t *sl = personnel_slot(s, slot), *st = staff_of(s, slot);
    if (st[S_STATE] == OFF_DUTY) return PERS_ALREADY_HOME;
    if (st[S_STATE] < 2) return PERS_NOT_ASSIGNED;
    if (personnel_room_release(s, slot)) return PERS_BUSY;
    sl[SL_HOURS] = 0;
    sl[SL_STATE] = st[S_STATE] = 0; /* idle, not off duty; +10/+11 kept */
    sl[SL_ROOM] = st[S_ROOM] = 0;
    return 0;
}

void personnel_raise(Sim *s, int slot) {
    uint8_t *sl = personnel_slot(s, slot);
    if (sl[SL_WAGE] >= 30) return;
    sl[SL_WAGE]++;
    staff_of(s, slot)[S_WAGE]++;
}

void personnel_cut(Sim *s, int slot) {
    uint8_t *sl = personnel_slot(s, slot);
    if (sl[SL_WAGE] < 3) return; /* floor 2 */
    sl[SL_WAGE]--;
    staff_of(s, slot)[S_WAGE]--;
}

int personnel_dismiss(Sim *s, int slot) {
    uint8_t *sl = personnel_slot(s, slot), *st = staff_of(s, slot);
    if (st[S_STATE] != 0 && st[S_STATE] != OFF_DUTY) return 0;
    pay(s, PERSONNEL_SEVERANCE, H_WAGES_DAY, H_WAGES_TOTAL);
    st[S_HOSPITAL] = st[S_STATE] = 0xFF;
    put16(sl + SL_INDEX, 0); /* the other slot bytes stay */
    st[S_ROOM] = st[20] = 0;
    st[S_DISMISSED_BY] = 0; /* never applies to the player again */
    st[S_RESERVED] = 0xFF;
    st[S_INDEPENDENT] = st[S_MARK] = 0;
    return 1;
}

int personnel_toggle_independent(Sim *s, int slot) {
    uint8_t *st = staff_of(s, slot);
    if (st[S_INDEPENDENT]) {
        st[S_INDEPENDENT] = 0;
        return PERS_NOW_DEPENDENT;
    }
    if (personnel_independence(st) >= 0) return PERS_NOT_YET;
    st[S_INDEPENDENT] = 0xFF;
    return PERS_NOW_INDEPENDENT;
}

/* ---- courses ------------------------------------------------------------------------------------------ */

int personnel_course_fits(Sim *s, int e, int group) {
    const uint8_t *c = personnel_course(s, e);
    return (int8_t)c[0] >= 0 && c[1] && (int8_t)c[3] > 0 && (c[0] == 0 || c[0] == group);
}

int personnel_course_find(Sim *s, int from, int tries, int group) {
    for (int k = 0; k < tries; k++) {
        int e = (from + k) % COURSES;
        if (personnel_course_fits(s, e, group)) return e;
    }
    return -1;
}

int personnel_course_in_use(Sim *s, int from, int tries) {
    for (int k = 0; k < tries; k++) {
        int e = (from + k) % COURSES;
        if ((int8_t)personnel_course(s, e)[0] >= 0) return e;
    }
    return -1;
}

int personnel_course_price(Sim *s, int e) { return personnel_catalog(s, personnel_course(s, e)[4])[9]; }

int personnel_course_enrol(Sim *s, int slot, int e) {
    uint8_t *sl = personnel_slot(s, slot), *st = staff_of(s, slot);
    const uint8_t *c = personnel_course(s, e);
    if (st[S_STATE]) return PERS_NOBODY_THERE;
    /* engine choice: "of another group" read as the search's test (group 0 = any group fits) */
    if ((int8_t)c[0] < 0 || (c[0] && c[0] != sl[SL_GROUP]) || !c[1]) return PERS_DEADLINE;
    int price = personnel_course_price(s, e);
    if (price >= cash_of(s, 0)) return PERS_NO_FEE;
    pay(s, price, H_COURSES_DAY, H_COURSES_TOTAL); /* not +0xA6 */
    st[S_STATE] = sl[SL_STATE] = TRAINING;
    st[S_ROOM] = sl[SL_ROOM] = (uint8_t)e;
    sl[SL_HOURS] = sl[SL_ON_DUTY] = sl[SL_OFF_DUTY] = 0;
    return 0;
}

/* ---- room staff --------------------------------------------------------------------------------------- */

const RosterStub PERSONNEL_ROSTER[26] = {
    {0x1D2AB, 2, 1, 0x08, 0x33D58, 514, 973, 1, 0},  {0x1D115, 3, 1, 0x08, 0x3C08E, 514, 973, 0, 0},
    {0x1CC47, 4, 3, 0x04, 0x2EEE5, 515, 1458, 1, 0}, {0x1CC87, 5, 1, 0x08, 0x3C098, 514, 973, 0, 0},
    {0x1D14F, 5, 2, 0x0A, 0x3C098, 515, 1460, 0, 0}, {0x1D0A1, 6, 1, 0x08, 0x3C084, 514, 973, 0, 0},
    {0x1CCC1, 7, 1, 0x08, 0x3649A, 515, 973, 0, 1},  {0x1D061, 7, 2, 0x06, 0x3649A, 515, 1460, 0, 1},
    {0x1CFE1, 7, 5, 0x0A, 0x3649A, 515, 1460, 0, 1}, {0x1CD01, 8, 1, 0x08, 0x3677D, 515, 973, 0, 1},
    {0x1D021, 8, 10, 0x0A, 0x3677D, 515, 1460, 0, 1}, {0x1CF27, 9, 1, 0x08, 0x31D44, 514, 973, 0, 0},
    {0x1CE07, 10, 6, 0x08, 0x36A1E, 514, 993, 1, 1}, {0x1CE5B, 10, 6, 0x06, 0x36A1E, 514, 993, 1, 1},
    {0x1D23D, 11, 1, 0x08, 0x31A16, 514, 973, 0, 0}, {0x1D274, 11, 9, 0x0A, 0x31A16, 515, 1460, 0, 0},
    {0x1CEA1, 12, 1, 0x02, 0x36A7B, 514, 973, 1, 0}, {0x1CD81, 13, 4, 0x06, 0x36AD9, 514, 1459, 1, 0},
    {0x1CDC1, 14, 1, 0x02, 0x3BD5E, 514, 973, 1, 1}, {0x1CEE1, 14, 1, 0x04, 0x3BD5E, 514, 973, 1, 1},
    {0x24520, 15, 1, 0x08, 0x31EE3, 514, 973, 0, 0}, {0x244E6, 15, 10, 0x0A, 0x31EE3, 515, 1460, 0, 0},
    {0x1D0DB, 16, 1, 0x08, 0x3BCCB, 514, 973, 0, 0}, {0x1CD41, 17, 1, 0x08, 0x3BE59, 514, 973, 1, 0},
    {0x1CFA1, 17, 7, 0x0A, 0x3BE59, 515, 1461, 1, 0}, {0x1CF61, 18, 7, 0x0A, 0x3BDE0, 515, 1461, 1, 0},
};

const RosterStub *personnel_roster_stub(uint32_t r) {
    for (int i = 0; i < 26; i++)
        if (PERSONNEL_ROSTER[i].stub == r) return &PERSONNEL_ROSTER[i];
    return NULL;
}

int personnel_slot_of(Sim *s, int index) {
    for (int i = 0; i < STAFF_SLOTS; i++)
        if ((uint16_t)get16(personnel_slot(s, i) + SL_INDEX) == index) return i;
    return -1;
}

/* engine choice: a negative instance (pb+0x10 = FFFF after an assignment) reads instance 0 */
static int instance(Sim *s, int off) {
    int i = personnel_pb16(s, off);
    return i < 0 ? 0 : i;
}

int personnel_roster_enter(Sim *s, const RosterStub *st, int *occupant) {
    uint8_t *pb = personnel_pb(s);
    /* the driver's stub 0x1CE07 refuses at once while the race word [0x552A] is set (box 515/1669) */
    if (st->stub == 0x1CE07 && get16(s->world->block[BLK_34])) return -2;
    for (int i = 0; i < 4; i++) pb[6 + i] = (uint8_t)(st->back >> (8 * i));
    if (st->single) personnel_set_pb16(s, PB_ROOM_SHOWN, 0);
    if (st->sets_field) personnel_set_pb16(s, PB_ROSTER_FIELD, st->field);
    int room = instance(s, PB_ROOM_SHOWN);
    int held = (uint16_t)get16(sim_room_record(s, 0, st->cat, room) + st->field);
    *occupant = held ? personnel_slot_of(s, held) : -1;
    int candidate;
    if (!sim_idle_candidate(s, 0, st->group, &candidate)) candidate = -1; /* without the request flag */
    personnel_set_pb16(s, PB_INDEX, candidate);
    if (candidate < 0 && !held) return -1;
    if (*occupant >= 0) personnel_set_pb16(s, PB_INDEX, *occupant); /* the occupant is shown first */
    personnel_set_pb16(s, PB_ROSTER_GROUP, st->group);
    personnel_set_pb16(s, PB_ROSTER_CAT, st->cat);
    personnel_set_pb16(s, PB_ROSTER_ROOM, room);
    personnel_set_pb16(s, PB_COURSE, 0);
    return 0;
}

void personnel_roster_step(Sim *s, int step, int occupant) {
    int p = personnel_pb16(s, PB_INDEX), group = personnel_pb16(s, PB_ROSTER_GROUP);
    if (p < 0) p = 0;
    for (int k = 0; k < 219; k++) {
        p = (p + step + STAFF_SLOTS) % STAFF_SLOTS;
        const uint8_t *sl = personnel_slot(s, p);
        if (p == occupant || (get16(sl + SL_INDEX) && sl[SL_STATE] == 0 && sl[SL_GROUP] == group)) { /* no mood test */
            personnel_set_pb16(s, PB_INDEX, p);
            return;
        }
    }
}

uint8_t *personnel_roster_field(Sim *s) {
    int cat = personnel_pb16(s, PB_ROSTER_CAT), g = personnel_pb16(s, PB_ROSTER_GROUP);
    uint8_t *rec = sim_room_record(s, 0, cat, instance(s, PB_ROSTER_ROOM));
    switch (cat) {
    case 4: return rec + 4;
    case 5: return rec + (g == 2 ? 0x0A : 8);
    case 11: return rec + (g == 9 ? 0x0A : 8);
    case 15: return rec + (g == 10 ? 0x0A : 8);
    case 17: return rec + (g == 7 ? 0x0A : 8);
    case 7: case 8: case 10: case 14: return rec + personnel_pb16(s, PB_ROSTER_FIELD);
    case 12: return rec + 2;
    case 13: return rec + 6;
    case 18: return rec + 0x0A;
    default: return rec + 8; /* reception, waiting room, ward, ICU, neuro ward */
    }
}

int personnel_assign(Sim *s, int occupant, int *replaced) {
    *replaced = 0;
    int p = personnel_pb16(s, PB_INDEX);
    if (p < 0) return ROSTER_NOBODY;
    if (p == occupant) return ROSTER_BACK;
    uint8_t *sl = personnel_slot(s, p), *st = staff_of(s, p);
    if (sl[SL_STATE] || st[S_STATE]) return ROSTER_BACK; /* no mood or group test */
    int cat = personnel_pb16(s, PB_ROSTER_CAT), room = personnel_pb16(s, PB_ROSTER_ROOM);
    sl[SL_STATE] = st[S_STATE] = (uint8_t)cat;
    sl[SL_ROOM] = st[S_ROOM] = (uint8_t)room;
    sl[SL_HOURS] = 0;
    uint8_t *field = personnel_roster_field(s);
    int d = (uint16_t)get16(field);
    put16(field, get16(sl + SL_INDEX));
    if (cat == 2 || cat == 4 || cat == 10 || cat == 12 || cat == 13 || cat == 14 || cat == 17 || cat == 18)
        personnel_set_pb16(s, PB_ROOM_SHOWN, -1); /* no room number in the message */
    if (d) { /* the replaced employee becomes idle at once, not off duty */
        uint8_t *ds = personnel_staff(s, d);
        ds[S_STATE] = ds[S_ROOM] = 0;
        int k = personnel_slot_of(s, d);
        if (k >= 0) {
            uint8_t *dsl = personnel_slot(s, k);
            dsl[SL_STATE] = dsl[SL_ROOM] = dsl[SL_HOURS] = 0;
        }
    }
    *replaced = d;
    return ROSTER_ASSIGNED;
}

int personnel_roster_release(Sim *s, int occupant) {
    if (occupant < 0) return ROSTER_BACK;
    uint8_t *sl = personnel_slot(s, occupant), *st = staff_of(s, occupant);
    if (!sl[SL_STATE] || st[S_STATE] < 2) return PERS_NOT_ASSIGNED;
    if (st[S_STATE] >= OFF_DUTY) return ROSTER_BACK;
    if (personnel_room_release(s, occupant)) return PERS_BUSY;
    sl[SL_HOURS] = 0;
    sl[SL_STATE] = st[S_STATE] = 0;
    sl[SL_ROOM] = st[S_ROOM] = 0;
    return 0;
}
