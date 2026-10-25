/* The work advice's rule walk (docs/ui-reports.md, section 2, "The work rules, in code order"). The rule names A1-D24
 * are the spec's. The scratch words of the original that the rules test after a helper ran - [0x45C8] (byte), [0x45B4],
 * [0x45B8], [0x5B69] and [0x5BA9] - are kept in the walk's context, with their stale values: the rules that test a
 * value another helper left (C25, C29, C30, C34, C37, D8a; the free beds and seats of a category with no rooms) do so
 * here too. Engine choices: the scratch values start at 0 at each walk (the original keeps whatever the previous code
 * left); the free-place test of D18 reads the reception record and that of D20 the last dental record the walk
 * selected (the original reads the record pointer [0x451C] the earlier rules left); the endless loop of the
 * buy-furniture tail with cash 0-5 (A19 / A22 jump back to A19) goes on with the next rule instead; [0x5BAA] is never
 * set by the walk, so the idle search 0x22D73 writes no hire request H +0xE0. */
#include "fixes.h"
#include "advice.h"

#include "personnel.h"
#include "sim_internal.h"

#define PERSON_SIZE 106
#define STAFF_SIZE 32
#define ILLNESSES 0x954
#define TREATMENTS 0x270C
#define ORGAN_GENDER 0x4713
#define PB_KEEP 200
#define SCREEN_INVENTORY 0x41

enum { GO_ON = -1, TO_A19 = -10, TO_B16 = -11, TO_END = -12 };
enum { OFF_DUTY = 0x13, RESTING = 0x18 };
/* person fields */
enum { F_SEX = 0, F_STATE = 4, F_DIAGNOSIS = 42, F_TREATMENT = 43, F_HEALTH = 44, F_STEPS = 45, F_ILLNESS = 56,
       F_IN_STEP = 93, F_FIELD100 = 100, F_BLEEDING = 101, F_RECOVERY = 102, F_DEAD = 104 };

typedef struct {
    Sim *s;
    Pieces *t;
    uint8_t *pb, *h, *site;
    int chief, hour;
    int32_t cash, e;
    int c8, b4, b8, organ, dentist; /* [0x45C8], [0x45B4], [0x45B8], [0x5B69], [0x5BA9] */
    const uint8_t *dental_last; /* the record D20's free-place test reads */
} A;

static int u16(const uint8_t *p) { return (uint16_t)get16(p); }
static uint8_t *room(A *a, int cat, int i) { return sim_room_record(a->s, 0, cat, i); }
static int count(A *a, int cat) { return sim_rooms_in_use(a->s, 0, cat); }
static uint8_t *person(A *a, int i) { return a->s->world->block[BLK_PATIENTS] + (size_t)(i % 1009) * PERSON_SIZE; }
static uint8_t *staff(A *a, int i) { return a->s->world->block[BLK_STAFF] + (size_t)(i % 1041) * STAFF_SIZE; }
static int independent(A *a, int i) { return staff(a, i)[11] != 0; }
static uint8_t *store(A *a) { return room(a, 4, 0); }
static int clerk(A *a) { return a->site[4] && u16(store(a) + STORE_CLERK); }
static const uint8_t *treatment(A *a, int id) { return program_table(a->s->prog, TREATMENTS) + id * 38; }
static const uint8_t *illness(A *a, int id) { return program_table(a->s->prog, ILLNESSES) + id * 20; }
static void set_instance(A *a, int i) {
    put16(a->pb + AP_INSTANCE, i);
    put16(a->pb + AP_ROOM, i);
}
/* the end of a looped rule without a hit: the counters pb +0xA6 / +0x10 were incremented after each of the n instances,
 * so they stand at n; a count of 0 leaves them (docs/ui-reports.md, "Run against the engine (2026-10-20, U-3)", rule 3) */
static void loop_done(A *a, int n) {
    if (n > 0) set_instance(a, n);
}

/* ---- text ----------------------------------------------------------------------------------------------- */

static void start(A *a, int entry) {
    pieces_clear(a->t);
    pc_text(a->t, entry);
}
static void txt(A *a, int entry) { pc_text(a->t, entry); }
static void nr(A *a, int i) { pc_num(a->t, i + 1); } /* "Nr." n = instance + 1 */
static void kind_name(A *a, int k) { txt(a, 3836 + k); } /* 0x272E4 */
static void cap_name(A *a, int c) { txt(a, 3858 + c); } /* 0x272F5 */
static void corpse(A *a, int i, int index) { /* 0x2727C */
    nr(a, i), txt(a, 2804), pc_person(a->t, index), txt(a, 2805);
}
int advice_organ_article(Sim *s, int organ) { return 1611 + program_u8(s->prog, ORGAN_GENDER, 2u * (organ & 0xFF)); }
static void organ_text(A *a, int organ) { /* 2782 + article + organ name */
    start(a, 2782);
    txt(a, advice_organ_article(a->s, organ));
    txt(a, 3662 + organ);
}

/* ---- helpers (the spec's table "Helpers the work rules use") -------------------------------------------- */

static const uint8_t *equipment_entry(A *a, int id) { return sim_equipment_item(a->s, id); }
static const uint8_t *furniture_entry(A *a, int id) { return sim_furniture_item(a->s, id); }
static int has_caps(const uint8_t *it, int cap) { return it[10] == cap || it[11] == cap || it[12] == cap || it[13] == cap; }

/* 0x2C20A / 0x2BD17 / 0x2C0BA: the room's list holds an item with the capability / kind; empty slots resolve to
 * catalog entry 0 */
static int has_cap(A *a, int cat, const uint8_t *r, int cap) {
    Description d = sim_description(a->s, cat);
    for (int i = 0; i < d.equipment_slots; i++)
        if (has_caps(equipment_entry(a, u16(r + d.equipment + 2 * i)), cap)) return 1;
    return 0;
}
static int has_kind(A *a, int cat, const uint8_t *r, int kind) {
    Description d = sim_description(a->s, cat);
    for (int i = 0; i < d.equipment_slots; i++)
        if (equipment_entry(a, u16(r + d.equipment + 2 * i))[6] == kind) return 1;
    return 0;
}
static int has_furniture(A *a, int cat, const uint8_t *r, int kind) {
    Description d = sim_description(a->s, cat);
    for (int i = 0; i < d.furniture_slots; i++)
        if (furniture_entry(a, u16(r + d.furniture + 2 * i))[6] == kind) return 1;
    return 0;
}
static int free_furniture_place(A *a, int cat, const uint8_t *r) { /* 0x2A59A for the player: no sale */
    Description d = sim_description(a->s, cat);
    for (int i = 0; i < d.furniture_slots; i++)
        if (!u16(r + d.furniture + 2 * i)) return 1;
    return 0;
}

/* 0x2D003 / 0x2A7C4 / 0x2A718, looking only ([0x5B79] clear): the store's first +9 (+8) items */
static int store_has(A *a, int id, int mode) { /* mode 0 capability, 1 equipment kind, 2 furniture kind */
    const uint8_t *st = store(a);
    int held = st[mode == 2 ? STORE_FURNITURE_HELD : STORE_EQUIPMENT_HELD];
    for (int k = 0; k < held && k < STORE_SLOTS; k++) {
        int item = u16(st + (mode == 2 ? STORE_FURNITURE : STORE_EQUIPMENT) + 2 * k);
        int hit = mode == 0 ? has_caps(equipment_entry(a, item), id)
                            : (mode == 1 ? equipment_entry(a, item) : furniture_entry(a, item))[6] == id;
        if (hit) {
            a->b4 = item;
            return 1;
        }
    }
    a->b4 = 0;
    uint8_t *want = a->h + (mode == 0 ? 0xDA : mode == 1 ? 0xDB : 0xDC);
    if (!*want) *want = (uint8_t)id;
    return 0;
}

/* 0x27306 / 0x27358 / 0x2739B: the market offers an item with that capability / kind */
static int market_offers(A *a, int id, int mode) {
    const uint8_t *m = a->s->world->block[mode == 2 ? BLK_FURNITURE_MARKET : BLK_EQUIPMENT_MARKET];
    for (int i = 0; i < MARKET_OFFERS; i++, m += 8) {
        if ((int8_t)m[0] < 0 || m[5]) continue;
        int item = u16(m + 2);
        int hit = mode == 0 ? has_caps(equipment_entry(a, item), id)
                            : (mode == 1 ? equipment_entry(a, item) : furniture_entry(a, item))[6] == id;
        if (hit) return a->c8 = id & 0xFF, 1;
    }
    a->c8 = 0;
    return 0;
}

/* 0x2C06B: the store fetches by itself (no advice needed) */
static int store_fetches(A *a) {
    int yes = a->s->screen != SCREEN_INVENTORY && clerk(a) && independent(a, u16(store(a) + STORE_CLERK));
    a->c8 = yes;
    return yes;
}

/* 0x22D73 without the hire request: pb +0 = the slot or FFFF */
static int idle(A *a, int group) {
    int slot = -1;
    int who = sim_idle_candidate(a->s, 0, group, &slot);
    put16(a->pb + AP_SAVED0, who ? slot : 0xFFFF);
    return who != 0;
}

/* 0x273DE: [0x45C8] = the group when nobody of it is off duty, resting or (groups other than 1) in state 0x0C */
static int nobody_away(A *a, int group) {
    const uint8_t *sl = staff_slots(a->s, 0);
    for (int i = 0; i < STAFF_SLOTS; i++, sl += STAFF_SLOT_SIZE) {
        if (!get16(sl) || sl[2] != group) continue;
        if (sl[3] == OFF_DUTY || sl[3] == RESTING || (group != 1 && sl[3] == 0x0C)) return a->c8 = 0, 0;
    }
    a->c8 = group & 0xFF;
    return a->c8 != 0;
}

static int opening(A *a, int group) { /* 0x2745D */
    const uint8_t *j = a->s->world->block[BLK_JOB_OPENINGS];
    for (int i = 0; i < JOB_OPENINGS; i++, j += JOB_OPENING_SIZE)
        if (j[0] == 0 && j[1] == group) return 1;
    return 0;
}

static int applicant(A *a) { /* 0x3A78E from list position pb +0 + 1, with pb +0 = pb +2 = 0 first */
    put16(a->pb + AP_SAVED0, 0);
    put16(a->pb + AP_WORD2, 0);
    int found = -1;
    for (int p = 0, k = 0; k < 1051 && found < 0; k++) {
        if (++p > PERSONNEL_POSITIONS) p = 1;
        int index = a->s->staff_list ? a->s->staff_list[p] : p;
        if (!personnel_is_applicant(a->s, index)) continue;
        if (a->dentist && staff(a, index)[1] != a->dentist) continue;
        found = p;
    }
    a->dentist = 0;
    put16(a->pb + AP_SAVED0, found >= 0 ? found : 0xFFFF);
    return found >= 0;
}

/* the staff chain S(g; A, T1; T2; X, T3) */
static int chain(A *a, int g, int adv_idle, int t1, int t2, int adv_ad, int t3) {
    if (idle(a, g)) return txt(a, t1), adv_idle;
    if (!(nobody_away(a, g) && a->chief && a->cash >= 20)) return GO_ON;
    if (opening(a, g)) return applicant(a) ? (txt(a, t2), 7) : GO_ON;
    if (g == 1) return nobody_away(a, 1), txt(a, 2879), 8; /* 0x5CE6F tests 0x273DE(1) again: still set */
    return txt(a, t3), adv_ad;
}

static int pathology_free(A *a) { /* 0x272B0: the word [0x45B4] */
    a->b4 = 0;
    if (a->site[18]) {
        const uint8_t *p = room(a, 18, 0);
        a->b4 = (uint16_t)(u16(p + 8) - u16(p + 6));
    }
    return a->b4 != 0;
}

/* The corpse rules C25, C29, C30, C34, D8a: they call 0x272B0 and test the byte [0x45C8] */
static int corpse_rule_fires(A *a) {
    int free_places = pathology_free(a);
    /* FIX-5 (docs/fixes.md): original tests the stale byte [0x45C8]; fixed tests the free pathology places [0x45B4] */
    if (fix_on(5)) return free_places;
    return a->c8 != 0;
}

static int organ_held(A *a, int organ) { /* 0x2722D */
    const uint8_t *b = sim_bank(a->s, 0);
    a->c8 = 0;
    if (!b) return 0;
    for (int k = 0; k < get16(b + 0x14) && k < 30; k++)
        if (b[0x58 + 2 * k] == organ) a->c8 = organ;
    return a->c8 != 0;
}

static int organ_offered(A *a) { /* 0x271DB: [0x5B69], or H +0x86 when that is 0 */
    int id = a->organ ? a->organ : a->h[0x86];
    for (int i = 0; i < 100; i++) {
        const uint8_t *m = sim_market(a->s, i);
        if ((int8_t)m[0] >= 0 && !m[8] && m[1] && m[2] == id) return a->c8 = 0xFF, 1;
    }
    return a->c8 = 0, 0;
}

/* 0x270B1 (OP) / 0x26FEE (torture): a bed patient waiting for the operation */
static int waiting_for_theatre(A *a, int torture) {
    int cat = torture ? 16 : 6, state = torture ? 0x10 : 6, code = torture ? 0x47 : 11, found = 0;
    for (int w = 0; w < count(a, cat); w++) {
        const uint8_t *k = room(a, cat, w);
        for (int b = 0; b < 10; b++) {
            int index = u16(k + 0x14 + 4 * b);
            if (!index) continue;
            const uint8_t *p = person(a, index);
            if (p[F_DEAD] || p[F_RECOVERY] || p[F_STATE] != state) continue;
            const uint8_t *t = treatment(a, p[F_TREATMENT]);
            if (!torture && !t[1]) continue;
            if (t[0x0F + p[F_STEPS]] != code) continue;
            if (torture || t[3] == 0 || organ_held(a, t[3])) {
                found = 1;
                continue;
            }
            if (sim_bank(a->s, 0)) {
                int keep = a->organ;
                a->organ = t[3];
                if (!organ_offered(a)) a->organ = keep;
            }
        }
    }
    a->c8 = found ? 0xFF : 0;
    return found;
}

int advice_free_places(int *b8, int rooms, int sum) {
    if (!rooms) {
        /* FIX-6 (docs/fixes.md): original keeps the stale [0x45B8] without a room of the category; fixed writes 0 */
        if (fix_on(6)) *b8 = 0;
        return *b8 != 0;
    }
    *b8 = (uint16_t)sum;
    return *b8 != 0;
}

static int free_beds(A *a, int cat) { /* 0x1B2DC / 0x1B262 / 0x1B200 (cat 3: seats); stale without rooms */
    int sum = 0;
    if (a->site[cat])
        for (int i = 0; i < count(a, cat); i++) sum += room(a, cat, i)[10] - room(a, cat, i)[12];
    return advice_free_places(&a->b8, a->site[cat], sum);
}

static int free_icu(A *a) { /* 0x1B43F */
    int i = sim_free_icu(a->s, 0);
    a->b4 = i < 0 ? 0xFFFF : i;
    return i >= 0;
}

static int rating(const uint8_t *r) { return r[0] + r[1]; }

/* ---- the patterns ------------------------------------------------------------------------------------------ */

static int buy_equipment(A *a) { return a->cash >= 6 ? (txt(a, 2716), 0x3C) : TO_B16; } /* 0x5D4AC */
static int buy_food(A *a) { return a->cash >= 6 ? (txt(a, 2715), 0x3C) : TO_B16; } /* 0x5D489 */
static int buy_furniture(A *a) { return a->cash >= 6 ? (txt(a, 2687), 0x48) : TO_A19; } /* 0x5C0EC */

/* E(X, T, N): equipment of capability X missing in a single room; the text is started by the caller */
static int equipment_rule(A *a, int cap, int advice) {
    a->pb[AP_CAPABILITY] = (uint8_t)cap, a->pb[AP_KIND] = 0;
    if (store_has(a, cap, 0)) return store_fetches(a) ? GO_ON : (txt(a, 2780), advice);
    return market_offers(a, cap, 0) ? buy_equipment(a) : GO_ON;
}
/* F(K) of a single room with the furniture-market test first (golf room, garage) */
static int furniture_rule_market(A *a, int kind, int advice) {
    if (!market_offers(a, kind, 2)) return GO_ON;
    a->pb[AP_KIND] = (uint8_t)kind, a->pb[AP_CAPABILITY] = 0;
    if (store_has(a, kind, 2)) return store_fetches(a) ? GO_ON : (txt(a, 2688), advice);
    return buy_furniture(a);
}
/* F(K) of the rooms with a request byte (no market test) */
static int furniture_rule(A *a, int kind, int advice) {
    a->pb[AP_KIND] = (uint8_t)kind, a->pb[AP_CAPABILITY] = 0;
    if (store_has(a, kind, 2)) return store_fetches(a) ? GO_ON : (txt(a, 2688), advice);
    return buy_furniture(a);
}

/* the request refresh: q rewritten to the first missing kind of the list */
static int refresh(A *a, int cat, const uint8_t *r, uint8_t *q, const int *kinds, int n) {
    for (int i = 0; i < n; i++)
        if (*q == 0 || *q == kinds[i]) *q = has_furniture(a, cat, r, kinds[i]) ? 0 : (uint8_t)kinds[i];
    return *q;
}

/* A12-A18: furniture requests; prefix = the room text, mid = 2701 or 2700, tail = the closing text */
static int request_rule(A *a, int cat, int i, uint8_t *r, int off, const int *kinds, int n, int prefix, int mid,
                        int tail, int advice) {
    uint8_t *q = r + off;
    if (!*q || !refresh(a, cat, r, q, kinds, n)) return GO_ON;
    start(a, prefix);
    if (cat != 2) nr(a, i);
    txt(a, mid), kind_name(a, *q), txt(a, tail);
    return furniture_rule(a, *q, advice);
}

/* R(C, K), the capability half: prefix + n + 2712 + the name */
static int cap_request(A *a, int cat, int i, const uint8_t *r, int c, int prefix, int advice) {
    if (!c || has_cap(a, cat, r, c)) return GO_ON;
    start(a, prefix), nr(a, i), txt(a, 2712), cap_name(a, c);
    a->pb[AP_CAPABILITY] = (uint8_t)c, a->pb[AP_KIND] = 0;
    if (!store_has(a, c, 0)) return market_offers(a, c, 0) ? buy_equipment(a) : GO_ON;
    return store_fetches(a) ? GO_ON : (txt(a, 2779), advice);
}
/* the kind half; fetch_test 0 for the rooms that skip it */
static int kind_request(A *a, int cat, int i, const uint8_t *r, int k, int prefix, int advice, int fetch_test) {
    if (!k || has_kind(a, cat, r, k)) return GO_ON;
    start(a, prefix), nr(a, i), txt(a, 2712), kind_name(a, k);
    a->pb[AP_KIND] = (uint8_t)k, a->pb[AP_CAPABILITY] = 0;
    if (!store_has(a, k, 1)) return buy_equipment(a);
    if (fetch_test && store_fetches(a)) return GO_ON;
    return txt(a, 2779), advice;
}

#define RESULT(x)                                                                                                       \
    do {                                                                                                               \
        int r_ = (x);                                                                                                  \
        if (r_ != GO_ON) return r_;                                                                                    \
    } while (0)

/* ---- part A ------------------------------------------------------------------------------------------------ */

static int bank_rules(A *a) { /* A2-A4 */
    const uint8_t *b = sim_bank(a->s, 0);
    if (!a->site[17] || !b || !clerk(a) || a->cash < 7) return GO_ON;
    /* A2 runs E's body also with the containers present: a capacity of 3,000 already means two of them, so it
     * fires through the free-space condition (as the original) */
    if (get32(b + 0x10) < 3000 || get32(b + 0x10) - get32(b + 0x0C) < 500 || !has_cap(a, 17, b, 0x3D)) {
        start(a, 2844);
        RESULT(equipment_rule(a, 0x3D, 0x6F));
    }
    if (!has_cap(a, 17, b, 0x33)) {
        start(a, 2842);
        RESULT(equipment_rule(a, 0x33, 0x6F));
    }
    if (!has_cap(a, 17, b, 0x3C)) {
        start(a, 2843);
        RESULT(equipment_rule(a, 0x3C, 0x6F));
    }
    return GO_ON;
}

static int pathology_rules(A *a) { /* A5-A11 */
    if (!a->site[18] || !a->site[4] || a->cash < 7) return GO_ON;
    static const int CAPS[7] = {9, 0x33, 0x3C, 0x0D, 0x18, 0x19, 0x24};
    static const int TEXTS[7] = {2839, 2840, 2841, 2835, 2836, 2837, 2838};
    const uint8_t *p = room(a, 18, 0);
    for (int k = 0; k < 7; k++) {
        if (has_cap(a, 18, p, CAPS[k])) continue;
        start(a, TEXTS[k]);
        RESULT(equipment_rule(a, CAPS[k], 0x70));
    }
    return GO_ON;
}

static int part_a_furniture(A *a) { /* A12-A18 behind the furniture gate */
    static const int K15[2] = {1, 5}, K85[2] = {8, 5}, K5[1] = {5}, K8[1] = {8};
    if (!clerk(a)) return GO_ON;
    if (a->site[2]) RESULT(request_rule(a, 2, 0, room(a, 2, 0), 4, K15, 2, 2702, 2701, 2699, 0x47));
    for (int i = 0; i < count(a, 5); i++) {
        set_instance(a, i);
        uint8_t *r = room(a, 5, i);
        if (!u16(r + 0x0C)) RESULT(request_rule(a, 5, i, r, 4, K15, 2, 2711, 2701, 2698, 0x50));
    }
    loop_done(a, count(a, 5));
    for (int i = 0; i < count(a, 15); i++) {
        set_instance(a, i);
        uint8_t *r = room(a, 15, i);
        if (!u16(r + 0x0C)) RESULT(request_rule(a, 15, i, r, 4, K85, 2, 2706, 2701, 2689, 0x52));
    }
    loop_done(a, count(a, 15));
    for (int i = 0; i < count(a, 11); i++) {
        set_instance(a, i);
        RESULT(request_rule(a, 11, i, room(a, 11, i), 4, K5, 1, 2710, 2701, 2696, 0x51));
    }
    loop_done(a, count(a, 11));
    for (int i = 0; i < count(a, 7); i++) {
        set_instance(a, i);
        uint8_t *r = room(a, 7, i);
        if (!u16(r + 0x0C)) RESULT(request_rule(a, 7, i, r, 0x7A, K8, 1, 2705, 2701, 2696, 0x57));
    }
    loop_done(a, count(a, 7));
    for (int i = 0; i < count(a, 8); i++) {
        set_instance(a, i);
        RESULT(request_rule(a, 8, i, room(a, 8, i), 0x7A, K5, 1, 2704, 2700, 2696, 0x64));
    }
    loop_done(a, count(a, 8));
    for (int i = 0; i < count(a, 9); i++) {
        set_instance(a, i);
        RESULT(request_rule(a, 9, i, room(a, 9, i), 4, K85, 2, 2709, 2701, 2696, 0x58));
    }
    loop_done(a, count(a, 9));
    return GO_ON;
}

/* A19-A24. Engine choice: a buy-furniture tail with cash 0-5 in A19 or A22 jumps back to A19 in the original, which
 * repeats for ever; here the walk goes on with the rule after it. */
static int no_loop(int r) { return r == TO_A19 ? GO_ON : r; }
static int golf_garage(A *a) {
    if (a->site[12]) {
        const uint8_t *g = room(a, 12, 0);
        if (!has_furniture(a, 12, g, 0x12)) {
            start(a, 2708), txt(a, 2701), txt(a, 2695);
            RESULT(no_loop(furniture_rule_market(a, 0x12, 0x6D)));
        }
        if (!has_cap(a, 12, g, 0x36)) {
            start(a, 2708), txt(a, 2701), txt(a, 2691);
            RESULT(equipment_rule(a, 0x36, 0x6E));
        }
        if (!has_cap(a, 12, g, 0x37)) {
            start(a, 2708), txt(a, 2701), txt(a, 2690);
            RESULT(equipment_rule(a, 0x37, 0x6E));
        }
    }
    if (a->site[10]) {
        const uint8_t *g = room(a, 10, 0);
        if (!has_furniture(a, 10, g, 8)) {
            start(a, 2707), txt(a, 2701), txt(a, 2694);
            RESULT(no_loop(furniture_rule_market(a, 8, 0x71)));
        }
        if (!has_cap(a, 10, g, 0x40)) {
            start(a, 2707), txt(a, 2700), txt(a, 2693);
            RESULT(equipment_rule(a, 0x40, 0x72));
        }
        if (!has_cap(a, 10, g, 0x41)) {
            /* FIX-4 (docs/fixes.md): original starts with 2708 "Dein Golfraum"; fixed with 2707 "Deine Garage" */
            start(a, fix_on(4) ? 2707 : 2708), txt(a, 2701), txt(a, 2692);
            RESULT(equipment_rule(a, 0x41, 0x72));
        }
    }
    return GO_ON;
}

static int reception_staff(A *a) { /* A25 */
    if (!a->site[2] || u16(room(a, 2, 0) + 8)) return GO_ON;
    int jan1 = world_tail(a->s->world, TAIL_MONTH) == 1 && world_tail(a->s->world, TAIL_DAY) == 1;
    if (!jan1 && (a->hour < 3 || a->hour > 17)) return GO_ON;
    start(a, 2449);
    return chain(a, 1, 6, 2889, 2888, 8, 2879);
}

static int theatre_requests(A *a, int cat, int prefix, int advice) { /* A26 / A27 */
    for (int i = 0; i < count(a, cat); i++) {
        set_instance(a, i);
        const uint8_t *r = room(a, cat, i);
        if (!u16(r + 4)) continue;
        RESULT(cap_request(a, cat, i, r, r[4], prefix, advice));
        RESULT(kind_request(a, cat, i, r, r[5], prefix, advice, 0));
    }
    loop_done(a, count(a, cat));
    return GO_ON;
}

/* ---- part B ------------------------------------------------------------------------------------------------ */

static int loop_requests(A *a, int cat, int coff, int koff, int prefix, int advice, int kind_fetch) {
    for (int i = 0; i < count(a, cat); i++) {
        set_instance(a, i);
        RESULT(cap_request(a, cat, i, room(a, cat, i), room(a, cat, i)[coff], prefix, advice));
    }
    loop_done(a, count(a, cat));
    for (int i = 0; i < count(a, cat); i++) {
        set_instance(a, i);
        RESULT(kind_request(a, cat, i, room(a, cat, i), room(a, cat, i)[koff], prefix, advice, kind_fetch));
    }
    loop_done(a, count(a, cat));
    return GO_ON;
}

static int reception_kind(A *a, int kind) { /* B4 / B5 */
    const uint8_t *r = room(a, 2, 0);
    if (has_kind(a, 2, r, kind)) return GO_ON;
    a->pb[AP_KIND] = (uint8_t)kind, a->pb[AP_CAPABILITY] = 0;
    start(a, 2778), kind_name(a, kind);
    if (!store_has(a, kind, 1)) return buy_equipment(a);
    return store_fetches(a) ? GO_ON : (txt(a, 2779), 0x38);
}

/* B12 / B14: the bed wards' capability requests with the food case */
static int ward_cap(A *a, int cat, int prefix, int advice) {
    for (int i = 0; i < count(a, cat); i++) {
        set_instance(a, i);
        const uint8_t *r = room(a, cat, i);
        int c = r[6];
        if (!c || has_cap(a, cat, r, c)) continue;
        if (c == 43) {
            if (a->hour < 4 || a->hour > 21) continue;
            start(a, prefix), nr(a, i), txt(a, 2713);
            a->pb[AP_CAPABILITY] = 43;
            if (store_has(a, 43, 0)) {
                if (!store_fetches(a)) return txt(a, 2781), advice;
                continue;
            }
            if (!a->site[13] && a->cash >= 30) return buy_food(a);
            continue;
        }
        if (cat == 16 && !market_offers(a, c, 0)) continue;
        start(a, prefix), nr(a, i), txt(a, 2712), cap_name(a, c);
        a->pb[AP_CAPABILITY] = (uint8_t)c, a->pb[AP_KIND] = 0;
        if (!store_has(a, c, 0)) RESULT(buy_equipment(a));
        else if (!store_fetches(a)) return txt(a, 2779), advice;
    }
    loop_done(a, count(a, cat));
    return GO_ON;
}

static int ward_kind(A *a, int cat, int prefix, int advice) { /* B13 / B15: no fetch test */
    for (int i = 0; i < count(a, cat); i++) {
        set_instance(a, i);
        RESULT(kind_request(a, cat, i, room(a, cat, i), room(a, cat, i)[7], prefix, advice, 0));
    }
    loop_done(a, count(a, cat));
    return GO_ON;
}

/* a room of the loop lacks the employee in field f: prefix + n + missing text, then the chain */
static int missing_staff(A *a, int cat, int f, int prefix, int missing, int g, int adv_idle, int t1, int t2, int adv_ad,
                         int t3) {
    for (int i = 0; i < count(a, cat); i++) {
        set_instance(a, i);
        if (u16(room(a, cat, i) + f)) continue;
        if (g == 9) a->dentist = 9;
        start(a, prefix), nr(a, i), txt(a, missing);
        RESULT(chain(a, g, adv_idle, t1, t2, adv_ad, t3));
    }
    loop_done(a, count(a, cat));
    return GO_ON;
}
static int missing_nurse(A *a, int cat, int prefix, int adv) {
    return missing_staff(a, cat, 8, prefix, 2874, 1, adv, 2889, 2888, 8, 2879);
}

static int idle_room(A *a, int cat, int prefix, int text, int advice) { /* B24-B26 */
    for (int i = 0; i < count(a, cat); i++) {
        set_instance(a, i);
        const uint8_t *r = room(a, cat, i);
        int nurse = u16(r + 8);
        if (!nurse || !u16(r + 10) || u16(r + 0x0C) || independent(a, nurse)) continue;
        if (sim_waiting_for(a->s, 0, cat) <= 0) continue;
        return start(a, prefix), nr(a, i), txt(a, text), advice;
    }
    loop_done(a, count(a, cat));
    return GO_ON;
}

static int expansion(A *a, int min_cash, int min_e) { return a->chief && a->hour < 8 && a->cash >= min_cash && a->e >= min_e; }

/* ---- part C ------------------------------------------------------------------------------------------------ */

/* C1-C3 / C4 / C6-C8 */
static int patients_done(A *a, int cat, int prefix, int adv_dead, int adv_bill, int adv_choose) {
    for (int i = 0; i < count(a, cat); i++) {
        set_instance(a, i);
        const uint8_t *r = room(a, cat, i);
        int index = u16(r + 0x0C);
        if (!index) continue;
        const uint8_t *p = person(a, index);
        if (p[F_DEAD]) {
            if (pathology_free(a)) return start(a, prefix), corpse(a, i, index), adv_dead;
            continue;
        }
        if (u16(r + 0x0E)) continue;
        if (u16(r + 0x10) && !u16(r + 0x12))
            return start(a, prefix), nr(a, i), txt(a, 2818), pc_person(a->t, index), txt(a, 2819), adv_bill;
        if (!u16(r + 0x10) && !independent(a, u16(r + 10))) {
            if (cat == 15 && !illness(a, p[F_DIAGNOSIS])[4] && !free_beds(a, 16)) continue;
            return start(a, prefix), nr(a, i), txt(a, 2824), pc_person(a->t, index), txt(a, 2825), adv_choose;
        }
    }
    loop_done(a, count(a, cat));
    return GO_ON;
}

static int operation_room(A *a) { /* C16-C21 */
    for (int i = 0; i < count(a, 7); i++) {
        set_instance(a, i);
        const uint8_t *r = room(a, 7, i);
        if (!u16(r + 6) || !u16(r + 8) || !u16(r + 10)) continue;
        int index = u16(r + 0x0C);
        if (index) {
            const uint8_t *p = person(a, index);
            if (p[F_DEAD]) {
                if (pathology_free(a)) return start(a, 2847), corpse(a, i, index), 0x2B;
                continue;
            }
            if (u16(r + 0x0E) == 999 && !p[F_BLEEDING] && get16(r + 0x12) == -1)
                return start(a, 2847), nr(a, i), txt(a, 2799), pc_person(a->t, index), txt(a, 2801), 0x2B;
            int organ = treatment(a, p[F_TREATMENT])[3];
            if (!organ || organ_held(a, organ)) {
                if (!u16(r + 0x10) && !independent(a, u16(r + 10)))
                    return start(a, 2847), nr(a, i), txt(a, 2797), pc_person(a->t, index), txt(a, 2798), 0x2B;
                continue;
            }
            a->organ = organ;
            if (organ_offered(a) && a->cash >= 20) return organ_text(a, organ), 0x37;
            continue;
        }
        if (waiting_for_theatre(a, 0)) {
            if (!independent(a, u16(r + 8))) return start(a, 2847), nr(a, i), txt(a, 2802), 0x2A;
            continue;
        }
        if (a->organ && a->cash >= 20 && organ_offered(a)) return organ_text(a, a->organ), 0x37;
    }
    loop_done(a, count(a, 7));
    return GO_ON;
}

static int wanted_organ(A *a) { /* C22 */
    if (!a->site[7] || a->cash < 25 || !a->h[0x86]) return GO_ON;
    if (organ_held(a, a->h[0x86])) {
        a->h[0x86] = 0;
        return GO_ON;
    }
    return organ_offered(a) ? (organ_text(a, a->h[0x86]), 0x37) : GO_ON;
}

static int torture_room(A *a) { /* C25-C28 */
    for (int i = 0; i < count(a, 8); i++) {
        set_instance(a, i);
        const uint8_t *r = room(a, 8, i);
        if (!u16(r + 8) || !u16(r + 10)) continue;
        int index = u16(r + 0x0C);
        if (index) {
            const uint8_t *p = person(a, index);
            if (p[F_DEAD]) {
                if (corpse_rule_fires(a)) return start(a, 2845), corpse(a, i, index), 0x61; /* the stale byte */
                continue;
            }
            if (u16(r + 0x0E) == 999 && !p[F_BLEEDING] && get16(r + 0x12) == -1)
                return start(a, 2845), nr(a, i), txt(a, 2800), pc_person(a->t, index), txt(a, 2796), 0x61;
            if (!u16(r + 0x10) && !independent(a, u16(r + 10)))
                return start(a, 2845), nr(a, i), txt(a, 2794), pc_person(a->t, index), txt(a, 2795), 0x61;
            continue;
        }
        if (waiting_for_theatre(a, 1) && !independent(a, u16(r + 8)))
            return start(a, 2845), nr(a, i), txt(a, 2803), 0x62;
    }
    loop_done(a, count(a, 8));
    return GO_ON;
}

static int waiting_corpses(A *a) { /* C29 */
    for (int i = 0; i < count(a, 3); i++) {
        set_instance(a, i);
        const uint8_t *r = room(a, 3, i);
        if (!u16(r + 8) || r[0x0F]) continue; /* the byte W+0x0F (SCR-4) */
        for (int b = 0; b < 10; b++) {
            put16(a->pb + AP_PLACE, b); /* counts every place (SCR-5) */
            int index = u16(r + 0x14 + 4 * b);
            if (!index) continue;
            const uint8_t *p = person(a, index);
            if (p[F_STATE] != 3 || !p[F_DEAD]) continue;
            if (corpse_rule_fires(a)) return start(a, 2871), corpse(a, i, index), 0x43;
        }
        put16(a->pb + AP_PLACE, 10); /* a full place loop without a hit */
    }
    loop_done(a, count(a, 3));
    return GO_ON;
}

static int ward_patients(A *a, int cat, int prefix, int advice) { /* C30-C33 / C34-C36 */
    int state = cat == 6 ? 6 : 0x10;
    for (int i = 0; i < count(a, cat); i++) {
        set_instance(a, i);
        const uint8_t *r = room(a, cat, i);
        if (!u16(r + 8)) continue;
        for (int b = 0; b < 10; b++) {
            put16(a->pb + AP_PLACE, b); /* counts every place (SCR-5) */
            int index = u16(r + 0x14 + 4 * b);
            if (!index) continue;
            const uint8_t *p = person(a, index);
            if (p[F_STATE] != state) continue;
            if (p[F_DEAD]) {
                if (corpse_rule_fires(a)) return start(a, prefix), corpse(a, i, index), advice;
                continue;
            }
            if (get16(p + F_ILLNESS)) {
                if (p[F_IN_STEP]) {
                    if (cat == 6 && a->site[9] && p[F_HEALTH] < 30 && free_icu(a) &&
                        (!treatment(a, p[F_TREATMENT])[1] || p[F_FIELD100]))
                        return start(a, prefix), nr(a, i), txt(a, 2806), pc_person(a->t, index), txt(a, 2807), advice;
                    continue;
                }
                if (!independent(a, u16(r + 8)))
                    return start(a, prefix), nr(a, i), txt(a, 2808), pc_person(a->t, index), txt(a, 2809), advice;
                continue;
            }
            if (a->hour >= 4 && a->hour <= 20 && p[F_HEALTH] >= 100)
                return start(a, prefix), nr(a, i), txt(a, 2811), pc_person(a->t, index), txt(a, 2810), advice;
        }
        put16(a->pb + AP_PLACE, 10); /* a full place loop without a hit */
    }
    loop_done(a, count(a, cat));
    return GO_ON;
}

static int reception_desk(A *a) { /* C37 / C38 */
    if (!a->site[2]) return GO_ON;
    const uint8_t *r = room(a, 2, 0);
    int clerk_index = u16(r + 8), index = u16(r + 0x0A);
    if (!clerk_index || !index || !has_kind(a, 2, r, 11) || !has_kind(a, 2, r, 3)) return GO_ON;
    if (fix_on(5)) {
        /* FIX-5 (docs/fixes.md): original C37 tests the stale byte [0x45C8] and C38 has no alive test; fixed C37 tests
         * the pathology's free places and a dead desk patient never reaches C38 (the next rule) */
        if (person(a, index)[F_DEAD]) return pathology_free(a) ? (start(a, 2868), 9) : GO_ON;
    } else if (person(a, index)[F_DEAD] && a->c8) {
        return start(a, 2868), 9;
    }
    /* C38 has no alive test: a dead desk patient with the stale byte 0 comes here too (SCR-4) */
    if (!independent(a, clerk_index) && free_beds(a, 3)) return start(a, 2877), 9;
    return GO_ON;
}

/* ---- part D ------------------------------------------------------------------------------------------------ */

static int single_staff(A *a, int site, const uint8_t *r, int f, int text, int g, int adv_idle, int t1, int t2, int adv_ad,
                        int t3) {
    if (!a->site[site] || u16(r + f)) return GO_ON;
    set_instance(a, 0);
    start(a, text);
    return chain(a, g, adv_idle, t1, t2, adv_ad, t3);
}

static int icu_patients(A *a) { /* D8 */
    for (int i = 0; i < count(a, 9); i++) {
        set_instance(a, i);
        const uint8_t *r = room(a, 9, i);
        int index = u16(r + 0x0C);
        if (!index) continue;
        const uint8_t *p = person(a, index);
        if (p[F_DEAD]) {
            if (corpse_rule_fires(a)) return start(a, 2872), corpse(a, i, index), 0x30; /* the result is not read */
            continue;
        }
        if (p[F_HEALTH] < 50 || !u16(r + 8) || p[F_RECOVERY]) continue;
        int queue_free = 0;
        if (a->site[2])
            for (int k = 0; k < 6; k++) queue_free |= !u16(room(a, 2, 0) + 0x46 + 4 * k);
        if (!queue_free || !free_beds(a, 6)) continue;
        return start(a, 2872), nr(a, i), txt(a, 2791), pc_person(a->t, index), txt(a, 2792), 0x30;
    }
    loop_done(a, count(a, 9));
    return GO_ON;
}

static int kitchen(A *a) { /* D13 */
    if (!a->site[13] || !clerk(a)) return GO_ON;
    uint8_t *k = room(a, 13, 0);
    static const int KINDS[2] = {1, 5};
    if (refresh(a, 13, k, k + 4, KINDS, 2)) {
        start(a, 2703), kind_name(a, k[4]), txt(a, 2697);
        a->pb[AP_KIND] = k[4], a->pb[AP_CAPABILITY] = 0;
        if (store_has(a, k[4], 2)) {
            if (!store_fetches(a)) return txt(a, 2688), 0x5C;
        } else if (a->cash >= 10) {
            return txt(a, 2687), 0x48;
        }
    }
    if (!u16(k + 6) || !k[5]) return GO_ON;
    if (k[2] && !has_cap(a, 13, k, k[2])) {
        a->pb[AP_CAPABILITY] = k[2], a->pb[AP_KIND] = 0;
        start(a, 2862), cap_name(a, k[2]);
        if (store_has(a, k[2], 0)) {
            if (!store_fetches(a)) return txt(a, 2672), 0x5B;
        } else if (market_offers(a, k[2], 0) && a->cash >= 50) {
            return txt(a, 2714), 0x3C;
        }
    }
    if (k[3] && !has_kind(a, 13, k, k[3])) {
        a->pb[AP_KIND] = k[3], a->pb[AP_CAPABILITY] = 0;
        start(a, 2862), kind_name(a, k[3]);
        if (store_has(a, k[3], 1)) {
            if (!store_fetches(a)) return txt(a, 2672), 0x5B;
        } else if (market_offers(a, k[3], 1) && a->cash >= 50) {
            return txt(a, 2714), 0x3C;
        }
    }
    return GO_ON;
}

static int seats(A *a) { /* D14 */
    if (!a->chief || a->cash < 80) return GO_ON;
    int open = a->site[2] && u16(room(a, 2, 0) + 0x16);
    for (int i = 0; i < count(a, 3); i++) {
        set_instance(a, i);
        const uint8_t *w = room(a, 3, i);
        if (!free_furniture_place(a, 3, w) || !open || w[0x0A] >= 10) continue;
        start(a, 2871), nr(a, i), txt(a, 2684);
        if (store_has(a, 5, 2)) return txt(a, 2683), 0x4A;
        if (a->cash >= 6) return txt(a, 2687), 0x48;
        return TO_END;
    }
    loop_done(a, count(a, 3));
    return GO_ON;
}

static int beds(A *a, int cat, int prefix, int spartan, int adv_fetch, int adv_spartan) { /* D15 / D16 */
    if (!a->site[cat] || !clerk(a)) return GO_ON;
    Description d = sim_description(a->s, cat);
    for (int i = 0; i < count(a, cat); i++) {
        set_instance(a, i);
        const uint8_t *k = room(a, cat, i);
        if (!free_furniture_place(a, cat, k)) continue;
        int missing = catalog_u8(a->s, k[d.type], CATALOG_BASE) - k[0x0A];
        if (missing > 0 && (k[0x0A] < k[0x0C] || a->chief)) {
            start(a, prefix), nr(a, i), txt(a, 2679);
            if (store_has(a, 8, 2)) return txt(a, 2678), adv_fetch;
            if (a->cash >= 6) return txt(a, 2687), 0x48;
            return TO_END;
        }
        if (missing == 0 && a->chief && rating(k) < 20) return start(a, spartan), nr(a, i), txt(a, 2685), adv_spartan;
    }
    loop_done(a, count(a, cat));
    return GO_ON;
}

/* D18-D24: a room of the loop "recht spartanisch eingerichtet"; place = the record whose free place is tested */
static int ratings(A *a) {
    const uint8_t *reception = room(a, 2, 0);
    for (int i = 0; i < count(a, 5); i++) { /* D18: the free place of the reception record (quirk) */
        set_instance(a, i);
        const uint8_t *r = room(a, 5, i);
        /* FIX-7 (docs/fixes.md): original tests the free place of the reception record; fixed the room's own */
        int has_place = fix_on(7) ? free_furniture_place(a, 5, r) : free_furniture_place(a, 2, reception);
        if (a->e >= 0 && !u16(r + 0x0C) && rating(r) < 20 && has_place)
            return start(a, 2677), nr(a, i), txt(a, 2685), 0x53;
    }
    loop_done(a, count(a, 5));
    a->dental_last = reception;
    for (int i = 0; i < count(a, 11); i++) { /* D19 */
        set_instance(a, i);
        const uint8_t *r = room(a, 11, i);
        a->dental_last = r;
        if (a->e >= 0 && !u16(r + 0x0C) && rating(r) < 20 && free_furniture_place(a, 11, r))
            return start(a, 2676), nr(a, i), txt(a, 2685), 0x54;
    }
    loop_done(a, count(a, 11));
    int dental_cat = a->dental_last == reception ? 2 : 11;
    for (int i = 0; i < count(a, 15); i++) { /* D20: the last dental record's free place (quirk) */
        set_instance(a, i);
        const uint8_t *r = room(a, 15, i);
        /* FIX-7 (docs/fixes.md): original tests the last dental record's free place; fixed the neurology's own */
        int has_place = fix_on(7) ? free_furniture_place(a, 15, r) : free_furniture_place(a, dental_cat, a->dental_last);
        if (a->e >= 0 && !u16(r + 0x0C) && rating(r) < 20 && has_place)
            return start(a, 2675), nr(a, i), txt(a, 2685), 0x55;
    }
    loop_done(a, count(a, 15));
    for (int i = 0; i < count(a, 7); i++) { /* D21 */
        set_instance(a, i);
        const uint8_t *r = room(a, 7, i);
        if (a->e >= 0 && !u16(r + 0x0C) && rating(r) < 20 && free_furniture_place(a, 7, r))
            return start(a, 2674), nr(a, i), txt(a, 2685), 0x56;
    }
    loop_done(a, count(a, 7));
    static const struct { uint8_t cat, e_test; int16_t text, advice; } NO_PATIENT_TEST[3] = {
        {3, 1, 2682, 0x4B}, {6, 1, 2681, 0x4D}, {9, 0, 2673, 0x59}};
    for (int k = 0; k < 3; k++) { /* D22-D24 */
        for (int i = 0; i < count(a, NO_PATIENT_TEST[k].cat); i++) {
            set_instance(a, i);
            const uint8_t *r = room(a, NO_PATIENT_TEST[k].cat, i);
            if ((!NO_PATIENT_TEST[k].e_test || a->e >= 0) && free_furniture_place(a, NO_PATIENT_TEST[k].cat, r) &&
                rating(r) < 20)
                return start(a, NO_PATIENT_TEST[k].text), nr(a, i), txt(a, 2685), NO_PATIENT_TEST[k].advice;
        }
        loop_done(a, count(a, NO_PATIENT_TEST[k].cat));
    }
    return GO_ON;
}

/* ---- the walk ---------------------------------------------------------------------------------------------- */

#define STEP(x)                                                                                                         \
    do {                                                                                                               \
        int r_ = (x);                                                                                                  \
        if (r_ >= 0) return r_;                                                                                        \
        if (r_ == TO_A19) goto a19;                                                                                    \
        if (r_ == TO_B16) goto b16;                                                                                    \
        if (r_ == TO_END) return ADVICE_NONE;                                                                          \
    } while (0)

static int work_rules(A *a) {
    /* A1 */
    if (a->site[4] && !u16(store(a) + STORE_CLERK)) {
        start(a, 2863);
        STEP(chain(a, 3, 0x3A, 2854, 2881, 0x3B, 2717));
    }
    STEP(bank_rules(a));
    STEP(pathology_rules(a));
    if (a->site[4]) STEP(part_a_furniture(a)); /* the furniture gate */
a19:
    STEP(golf_garage(a));
    STEP(reception_staff(a));
    if (!clerk(a)) goto b16; /* the request gate for A26-B15 */
    STEP(theatre_requests(a, 7, 2847, 0x42));
    STEP(theatre_requests(a, 8, 2845, 0x63));
    STEP(loop_requests(a, 9, 6, 7, 2872, 0x41, 0)); /* B2, B3 */
    STEP(reception_kind(a, 3)); /* B4 */
    STEP(reception_kind(a, 11)); /* B5 */
    STEP(loop_requests(a, 5, 0x44, 0x45, 2870, 0x3D, 1)); /* B6, B7 */
    STEP(loop_requests(a, 11, 0x44, 0x45, 2848, 0x3E, 1)); /* B8, B9 */
    STEP(loop_requests(a, 15, 0x44, 0x45, 2869, 0x5A, 1)); /* B10, B11 */
    STEP(ward_cap(a, 6, 2873, 0x3F)); /* B12 */
    STEP(ward_kind(a, 6, 2873, 0x3F)); /* B13 */
    STEP(ward_cap(a, 16, 2867, 0x40)); /* B14 */
    STEP(ward_kind(a, 16, 2867, 0x40)); /* B15 */
b16: {
    int jan1 = world_tail(a->s->world, TAIL_MONTH) == 1 && world_tail(a->s->world, TAIL_DAY) == 1;
    if (jan1 || (a->hour >= 3 && a->hour <= 18)) {
        STEP(missing_staff(a, 5, 10, 2870, 2875, 2, 0x0B, 2855, 2882, 0x0C, 2853)); /* B16 */
        STEP(missing_staff(a, 11, 10, 2848, 2849, 9, 0x0D, 2852, 2884, 0x0E, 2834)); /* B17 */
        STEP(missing_staff(a, 15, 10, 2869, 2876, 10, 0x14, 2828, 2883, 0x15, 2827)); /* B18 */
        STEP(missing_nurse(a, 5, 2870, 0x0A)); /* B19 */
        STEP(missing_nurse(a, 11, 2848, 0x0F)); /* B20 */
        STEP(missing_nurse(a, 15, 2869, 0x16)); /* B21 */
    }
}
    { /* B22 / B23 */
        int sum = (uint8_t)(a->site[5] + a->site[15] + a->site[11]);
        if (expansion(a, 800, 300) && a->cash >= 200 && a->site[5] && sum >= 4) {
            if (!a->site[6] && !a->pb[AP_TOLD_WARD]) return a->pb[AP_TOLD_WARD] = 0xFF, start(a, 2817), 0x1E;
            if (a->e >= 320 && a->site[6] && a->cash >= 850 && !a->site[17] && !a->pb[AP_TOLD_BANK])
                return a->pb[AP_TOLD_BANK] = 0xFF, start(a, 2814), 0x23;
        }
    }
    STEP(idle_room(a, 5, 2870, 2830, 0x10)); /* B24 */
    STEP(idle_room(a, 11, 2848, 2831, 0x11)); /* B25 */
    STEP(idle_room(a, 15, 2826, 2832, 0x17)); /* B26 */
    STEP(patients_done(a, 5, 2870, 0x44, 0x19, 0x18)); /* C1-C3 */
    STEP(patients_done(a, 11, 2848, 0x45, 0x1B, 0x1A)); /* C4 */
    if (expansion(a, 800, 300) && a->site[15] && !a->site[16] && !a->pb[AP_TOLD_NEURO_WARD]) /* C5 */
        return a->pb[AP_TOLD_NEURO_WARD] = 0xFF, start(a, 2816), 0x20;
    STEP(patients_done(a, 15, 2869, 0x46, 0x1D, 0x1C)); /* C6-C8 */
    if (a->site[2]) { /* C9 */
        const uint8_t *r = room(a, 2, 0);
        if (u16(r + 8) && !independent(a, u16(r + 8)) && u16(r + 0x46) && free_beds(a, 6)) return start(a, 2815), 0x22;
    }
    STEP(missing_nurse(a, 6, 2873, 0x1F)); /* C10 */
    STEP(missing_nurse(a, 16, 2867, 0x21)); /* C11 */
    if (expansion(a, 750, 250) && a->site[5] && a->site[6] && !a->site[7] && !a->pb[AP_TOLD_OP]) /* C12 */
        return a->pb[AP_TOLD_OP] = 0xFF, start(a, 2813), 0x24;
    if (a->hour >= 3 && a->hour <= 19) {
        STEP(missing_staff(a, 7, 10, 2847, 2850, 5, 0x27, 2846, 2885, 0x28, 2812)); /* C13 */
        STEP(missing_staff(a, 7, 6, 2847, 2851, 2, 0x26, 2855, 2882, 0x0C, 2853)); /* C14 */
    }
    if (a->hour >= 3 && a->hour <= 18) STEP(missing_nurse(a, 7, 2847, 0x25)); /* C15 */
    STEP(operation_room(a)); /* C16-C21 */
    STEP(wanted_organ(a)); /* C22 */
    if (a->hour >= 3 && a->hour <= 19) STEP(missing_staff(a, 8, 10, 2845, 2876, 10, 0x5F, 2828, 2883, 0x15, 2827)); /* C23 */
    if (a->hour >= 3 && a->hour <= 18) STEP(missing_nurse(a, 8, 2845, 0x60)); /* C24 */
    STEP(torture_room(a)); /* C25-C28 */
    STEP(waiting_corpses(a)); /* C29 */
    STEP(ward_patients(a, 6, 2873, 0x29)); /* C30-C33 */
    STEP(ward_patients(a, 16, 2867, 0x31)); /* C34-C36 */
    STEP(reception_desk(a)); /* C37, C38 */
    if (a->hour >= 3 && a->hour <= 18) STEP(missing_nurse(a, 3, 2871, 0x2D)); /* C39 */
    if (expansion(a, 800, 250) && !a->site[9] && a->site[7] && !a->pb[AP_TOLD_ICU]) /* D1 */
        return a->pb[AP_TOLD_ICU] = 0xFF, start(a, 2793), 0x2E;
    STEP(missing_nurse(a, 9, 2872, 0x2F)); /* D2 */
    if (a->site[17]) STEP(single_staff(a, 17, sim_bank(a->s, 0), 8, 2856, 1, 0x69, 2889, 2888, 8, 2879)); /* D3 */
    if (a->hour < 5) STEP(single_staff(a, 12, room(a, 12, 0), 2, 2860, 1, 0x6A, 2889, 2888, 8, 2879)); /* D4 */
    if (a->site[14] && a->hour >= 10 && a->hour <= 15) { /* D5 */
        const uint8_t *c = room(a, 14, 0);
        set_instance(a, 0);
        if (!u16(c + 2)) {
            start(a, 2859);
            STEP(chain(a, 1, 0x6B, 2889, 2888, 8, 2879));
        } else if (!u16(c + 4)) {
            start(a, 2858);
            STEP(chain(a, 1, 0x6C, 2889, 2888, 8, 2879));
        }
    }
    if (a->site[10] && !get16(a->s->world->block[BLK_34])) { /* D6 */
        const uint8_t *g = room(a, 10, 0);
        set_instance(a, 0);
        if (!u16(g + 8)) {
            start(a, 2865);
            STEP(chain(a, 6, 0x65, 2451, 2880, 0x67, 2786));
        }
        if (!u16(g + 6)) {
            start(a, 2866);
            STEP(chain(a, 6, 0x66, 2450, 2880, 0x67, 2786));
        }
    }
    if (expansion(a, 950, 350) && a->site[6] && a->site[7] && !a->site[18] && !a->pb[AP_TOLD_PATHOLOGY]) /* D7 */
        return a->pb[AP_TOLD_PATHOLOGY] = 0xFF, start(a, 2790), 0x32;
    STEP(icu_patients(a)); /* D8 */
    if (a->hour >= 3 && a->hour <= 19)
        STEP(single_staff(a, 18, room(a, 18, 0), 10, 2861, 7, 0x33, 2789, 2886, 0x34, 2785)); /* D9 */
    if (a->site[17]) STEP(single_staff(a, 17, sim_bank(a->s, 0), 10, 2857, 7, 0x68, 2789, 2886, 0x34, 2785)); /* D10 */
    if (a->site[18] && u16(room(a, 18, 0) + 10)) { /* D11 */
        const uint8_t *p = room(a, 18, 0);
        if (!u16(p + 0x0C) && u16(p + 6)) return start(a, 2784), 0x35;
        if (u16(p + 0x0C) && u16(p + 0x0E) == 999) return start(a, 2783), 0x36;
    }
    STEP(single_staff(a, 13, room(a, 13, 0), 6, 2864, 4, 0x5D, 2788, 2887, 0x5E, 2787)); /* D12 */
    STEP(kitchen(a)); /* D13 */
    STEP(seats(a)); /* D14 */
    STEP(beds(a, 6, 2873, 2681, 0x4C, 0x4D)); /* D15 */
    STEP(beds(a, 16, 2867, 2680, 0x4E, 0x4F)); /* D16 */
    if (!a->chief) return ADVICE_NONE; /* D17 on: the chief doctor's rules */
    if (a->site[2] && a->e >= 0) { /* D17 */
        const uint8_t *r = room(a, 2, 0);
        if (rating(r) < 20 && free_furniture_place(a, 2, r)) return start(a, 2686), txt(a, 2685), 0x49;
    }
    STEP(ratings(a));
    return ADVICE_NONE;
}

static int beginner_rules(A *a) { /* 0x5B6A5, chief doctor only (docs/office.md, hotspot 10) */
    const uint8_t *site = a->site;
    int plots = u16(a->h + H_PLOTS_OWNED) + u16(a->h + H_PLOTS_RENTED);
    int none_built = (uint8_t)(site[5] + site[15] + site[11]) == 0;
    int may_spend = none_built || a->e >= 0;
    if (plots == 0) return start(a, 2440), 0;
    if (!site[1] && plots < 4) return start(a, 2439), 0;
    if (!site[1]) return start(a, 2444), 1;
    if (!site[2]) return start(a, 2445), 2;
    if (!site[3]) return start(a, 2446), 3;
    if (!site[4]) return start(a, 2718), 0x39;
    if (!site[5] && !a->pb[AP_TOLD_TREATMENT] && may_spend) return a->pb[AP_TOLD_TREATMENT] = 0xFF, start(a, 2447), 4;
    if (!site[11] && !a->pb[AP_TOLD_DENTAL] && a->cash >= 50 && may_spend)
        return a->pb[AP_TOLD_DENTAL] = 0xFF, start(a, 2448), 5;
    if (!site[15] && !a->pb[AP_TOLD_NEUROLOGY] && a->hour < 8 && a->cash >= 250 && may_spend)
        return a->pb[AP_TOLD_NEUROLOGY] = 0xFF, start(a, 2829), 0x13;
    return ADVICE_NONE;
}

int advice_walk(Sim *s, int entry, Advice *out) {
    if (entry == ADVICE_SCHEDULED && s->advice_pending) return ADVICE_NONE;
    uint8_t *pb = s->world->block[BLK_PLAYER0];
    A a = {0};
    a.s = s, a.t = &out->text, a.pb = pb, a.h = hospital(s, 0), a.site = room_counts(s, 0);
    pieces_clear(a.t);
    int saved0 = get16(pb + AP_SAVED0), saved10 = get16(pb + AP_ROOM);
    put16(pb + AP_ADVICE, 0xFFFF);
    a.e = get32(a.h + 0xE2); /* 0x1B1EB copies the selected hospital's cache; it does not recompute (SIM-102). */
    a.cash = get32(a.h + H_CASH);
    a.hour = world_tail(s->world, TAIL_HOUR);
    pb[AP_CHIEF] = entry == ADVICE_CHIEF ? 0 : 0xFF;
    pb[AP_FLAG5A] = entry == ADVICE_SCHEDULED || entry == ADVICE_STRIP ? 0xFF : 0;
    pb[AP_BACKGROUND] = entry == ADVICE_SCHEDULED ? 0xFF : 0;
    a.chief = pb[AP_CHIEF] == 0;
    out->back = entry == ADVICE_STRIP ? 2 : entry == ADVICE_RECEPTION ? 1 : entry == ADVICE_CHIEF ? 0 : -1;
    if (entry == ADVICE_STRIP) s->advice_pending = 1;
    int advice = ADVICE_NONE;
    if (a.chief) advice = beginner_rules(&a);
    if (advice < 0) advice = work_rules(&a);
    if (advice < 0) { /* 0x616A8 */
        advice = ADVICE_NONE;
        start(&a, a.chief && world_tail(s->world, 0x12) ? 2833 : 2441);
    }
    put16(pb + AP_ADVICE, advice < 0 ? 0xFFFF : advice);
    put16(pb + AP_ROOM, saved10); /* 0x616D9 */
    put16(pb + AP_SAVED0, saved0);
    if (pb[AP_BACKGROUND]) {
        s->help_strip = advice >= 0; /* the clock runs: the walk is called from the schedule */
        pb[AP_BACKGROUND] = 0;
    } else {
        s->advice_pending = advice >= 0;
    }
    out->advice = advice;
    return advice;
}

void advice_scheduled(Sim *s) {
    if (s->fast_forward) return;
    uint8_t keep[PB_KEEP];
    uint8_t *pb = s->world->block[BLK_PLAYER0];
    for (int i = 0; i < PB_KEEP; i++) keep[i] = pb[i];
    Advice a;
    advice_walk(s, ADVICE_SCHEDULED, &a);
    for (int i = 0; i < PB_KEEP; i++) pb[i] = keep[i];
}
