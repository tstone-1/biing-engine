/* The computed sheet lines of docs/ui-sheets.md; see sheet.h. Conventions as in the spec: "= n" starts the buffer with
 * text n, "+ n" appends it; a branch that does not touch the buffer leaves the previous line's text in it. Hospital 0
 * throughout (every sheet routine selects the player first, docs/ui-sheets.md "Which hospital"). */
#include "sheet.h"

#include <string.h>

#include "sim_internal.h"

const int SHEET_LINE_Y[SHEET_LINES] = {54, 93, 132, 197, 236, 275, 314};

#define H 0
enum { /* patient fields */
    P_SEX = 0, P_STATE = 4, P_OPINION = 20, P_HUNGER = 40, P_AILMENT = 41, P_DIAGNOSIS = 42, P_TREATMENT = 43,
    P_HEALTH = 44, P_STEP = 45, P_ILLNESS = 56, P_WISH = 57, P_ROOM = 66, P_MOOD = 78, P_IN_TREATMENT = 79,
    P_BODY = 80, P_VARIANT = 84, P_IN_STEP = 93, P_RECOVERY = 102, P_DEAD = 104,
};
enum { /* program image: tables and strings (docs/ui-sheets.md, docs/sim-rooms2.md) */
    ILLNESSES = 0x954, TREATMENTS = 0x270C, WISHES = 0x2066, STEP_MINUTES = 0x44A5, OP_ROWS = 0x208E,
    TORTURE_ROWS = 0x242E, HARVEST_ROWS = 0xA749, ACT_1 = 0x6A71, ACT_0 = 0x875B, MOOD_1 = 0x6C2D, MOOD_0 = 0x8917,
    SOME_DEVICE = 0xA769, /* "einem Gerät" */
    HOME_ROWS = 0x604F, THUG_PERSON_ROWS = 0x5FF3, THUG_STAFF_ROWS = 0x600F, COURSE_CATALOG = 0x40D9,
};
#define LAST_INDEX 440 /* 4 x step code 110, the last entry of the four tables */

/* ---- the message buffer ------------------------------------------------------------------------------------------ */

/* ib/iw/idw read at a disk table start plus an index (the start mapped to the build, program.h); rb and raw_string
 * at offsets of the build itself (the step tables' entries are pointers of the build) */
static int ib(SheetCtx *c, uint32_t table, uint32_t index) { return program_u8(c->s->prog, table, index); }
static int iw(SheetCtx *c, uint32_t table, uint32_t index) { return program_word(c->s->prog, table, index); }
static uint32_t idw(SheetCtx *c, uint32_t table, uint32_t index) { return program_dword(c->s->prog, table, index); }
static int rb(SheetCtx *c, uint32_t off) { return program_raw_u8(c->s->prog, off); }
static int u16(const uint8_t *p) { return (uint16_t)get16(p); }

static void clr(SheetCtx *c) { msg_clear(c->m); }
static void text(SheetCtx *c, int n) {
    size_t len;
    const uint8_t *t = textdb_text(c->texts, n, &len);
    msg_append(c->m, t, len);
}
static void num(SheetCtx *c, long v) { msg_append_number(c->m, v); }
static void set(SheetCtx *c, int n) { clr(c), text(c, n); }
/* a '$'-terminated string at an offset of the build (the step tables' entries) */
static void raw_string(SheetCtx *c, uint32_t off) {
    size_t left, n = 0;
    const uint8_t *p = program_raw_ptr(c->s->prog, off, &left);
    while (n < left && n < 255 && p[n] != '$') n++;
    if (p) msg_append(c->m, p, n);
}
/* the same at a disk offset ("einem Gerät") */
static void image_string(SheetCtx *c, uint32_t off) { raw_string(c, program_at(c->s->prog, off)); }
static void pool(SheetCtx *c, int which, int i) {
    if (!c->people) return;
    size_t n;
    const uint8_t *p = datadb_pool_entry(c->people, which, i, &n);
    msg_append(c->m, p, n);
}
static void person_name(SheetCtx *c, int i) { pool(c, DAT_PATIENT_NAMES, i); } /* 0x2BE79 */
static void staff_name(SheetCtx *c, int i) { pool(c, DAT_STAFF_NAMES, i); } /* 0x2BE61 */

static int hour(SheetCtx *c) { return world_tail(c->s->world, TAIL_HOUR); }
static int night(SheetCtx *c) { return hour(c) >= 22 || hour(c) <= 3; } /* "hours 22-3", the complement of 4-21 */
static uint32_t r(SheetCtx *c, uint32_t mask) { return sim_bits(c->s, mask); }
static uint32_t rnd(SheetCtx *c, uint32_t n) { return sim_random(c->s, n); }

/* ---- records ----------------------------------------------------------------------------------------------------- */

static uint8_t *person(SheetCtx *c, int i) { return c->s->world->block[BLK_PATIENTS] + i * PATIENT_SIZE; }
static uint8_t *staff(SheetCtx *c, int i) { return c->s->world->block[BLK_STAFF] + i * STAFF_SIZE; }
static int single(int cat) { /* one record per hospital: the instance is ignored (0x1FAE6) */
    return cat == 2 || cat == 4 || cat == 10 || cat == 12 || cat == 13 || cat == 14 || cat == 17 || cat == 18;
}
static uint8_t *room(SheetCtx *c, int cat, int instance) {
    if (cat < 2 || cat > 18) return NULL;
    return sim_room_record(c->s, H, cat, single(cat) ? 0 : instance);
}
static uint32_t treatment_at(int t) { return TREATMENTS + 38u * (uint32_t)t; }
static uint32_t illness_at(int i) { return ILLNESSES + 20u * (uint32_t)i; }
static int tb(SheetCtx *c, int t, int off) { return ib(c, TREATMENTS, 38u * (uint32_t)t + (uint32_t)off); }
static int step_code(SheetCtx *c, const uint8_t *p) { return tb(c, p[P_TREATMENT], 15 + p[P_STEP]); }
static int step_capability(SheetCtx *c, const uint8_t *p) { return tb(c, p[P_TREATMENT], 19 + p[P_STEP]); }

/* ---- equipment --------------------------------------------------------------------------------------------------- */

void sheet_item_name(SheetCtx *c, int id, int cut) {
    const uint8_t *e = sim_equipment_item(c->s, id);
    size_t n;
    const uint8_t *t = textdb_text(c->texts, (int)get32(e), &n);
    if (cut)
        for (size_t i = 0; i < n; i++)
            if (t[i] == '(') { /* the copy ends at the character before the '(' */
                n = i ? i - 1 : 0;
                break;
            }
    msg_append(c->m, t, n);
}

/* + the article of item e: German 1607 + kind (1610, kind 3, is empty); the English CD's device-name helper 0x27CE0
 * appends "a"/"an " by the name's first letter and nothing for kind 3 (docs/editions.md 7.3, rule A) */
static void article(SheetCtx *c, const uint8_t *e) {
    if (!program_english(c->s->prog)) {
        text(c, 1607 + e[7]);
        return;
    }
    if (e[7] >= 3) return;
    size_t n, sp;
    const uint8_t *t = textdb_text(c->texts, (int)get32(e), &n), *space = textdb_text(c->texts, 386, &sp);
    msg_english_article(c->m, t, n, space, sp);
}

static int has_capability(const uint8_t *e, int cap) { return e[10] == cap || e[11] == cap || e[12] == cap || e[13] == cap; }

/* the first item of P's room (category field 4, instance field 66) matching cap (or kind e[6] == cap) */
static int find_item(SheetCtx *c, const uint8_t *p, int cap, int by_kind, int *slot) {
    uint8_t *rec = room(c, p[P_STATE], p[P_ROOM]);
    if (!rec) return 0;
    Description d = sim_description(c->s, p[P_STATE]);
    for (int i = 0; i < d.equipment_slots; i++) {
        int id = u16(rec + d.equipment + 2 * i);
        if (!id) continue;
        const uint8_t *e = sim_equipment_item(c->s, id);
        if (by_kind ? e[6] == cap : has_capability(e, cap)) {
            if (slot) *slot = i;
            return id;
        }
    }
    return 0;
}

/* 0x2C264: + article + name (cut), or "einem Gerät". Original bug, reproduced (the lead's decision): the search
 * wears the found item when the simulation's use flag [0x5B6F] is still set from its last equipment search; the
 * sheet never clears it. */
static void instrument(SheetCtx *c, const uint8_t *p, int cap) {
    int slot = 0, id = find_item(c, p, cap, 0, &slot);
    if (!id) {
        image_string(c, SOME_DEVICE);
        return;
    }
    const uint8_t *e = sim_equipment_item(c->s, id);
    article(c, e);
    sheet_item_name(c, id, 1);
    if (!c->s->equipment_use) return;
    /* FIX-12 (docs/fixes.md): original wears (and can remove) the item on every redraw while the use flag is set;
     * fixed: drawing names the instrument, no draw, no wear */
    if (fix_on(12)) return;
    uint8_t *rec = room(c, p[P_STATE], p[P_ROOM]);
    Description d = sim_description(c->s, p[P_STATE]);
    int wear = e[16] ? e[16] : r(c, 3) == 0;
    int left = rec[d.cond_b + slot] - wear;
    if (left > 0) {
        rec[d.cond_b + slot] = (uint8_t)left;
        return;
    }
    put16(rec + d.equipment + 2 * slot, 0); /* removed from the list, then the list tidy-up 0x2BFB7 */
    rec[d.cond_b + slot] = 0;
    sim_tidy_lists(c->s);
}

/* 0x3116D: the search by kind, no wear; [0x5B71] keeps the previous item when none is found (item 0: "nichts") */
static void kind_item(SheetCtx *c, const uint8_t *p, int kind) {
    int id = find_item(c, p, kind, 1, NULL);
    if (id) c->mem->kind_item = id;
}

/* ---- the step tables --------------------------------------------------------------------------------------------- */

/* the instrument capability of each computed activity entry (docs/ui-sheets.md, "The step tables") */
static int code_capability(int code) {
    switch (code) {
    case 4: case 43: case 66: case 101: return 13;
    case 31: case 68: return 15;
    case 32: return 18;
    case 33: return 12;
    case 36: case 103: return 9;
    case 37: case 42: case 65: return 25;
    case 41: return 6;
    case 48: return 14;
    case 49: return 40;
    case 51: case 52: return 41;
    case 53: return 42;
    case 58: case 88: return 2;
    case 61: return 24;
    case 63: case 85: return 32;
    case 64: case 70: return 49;
    case 69: case 78: case 81: case 104: case 110: return 33;
    case 80: case 107: return 53;
    case 86: return 10;
    case 87: return 39;
    case 89: return 58;
    case 90: return 19;
    case 105: return 62;
    case 106: return 63;
    default: return 0;
    }
}

/* the random-text entries (codes 18, 21, 22, 23, 46); 0 when the code is not one */
static int random_text(SheetCtx *c, int code, int mood) {
    switch (code) {
    case 18: return (mood ? 3795 : 3791) + (int)r(c, 3);
    case 21: return (mood ? 3803 : 3799) + (int)r(c, 3);
    case 22: return mood ? 3812 + (int)r(c, 3) : 3807 + (int)rnd(c, 5);
    case 23: return (mood ? 3821 : 3816) + (int)rnd(c, 5);
    case 46: return (mood ? 3831 : 3826) + (int)rnd(c, 5);
    default: return 0;
    }
}

static int index_ok(int i) { return i >= 0 && i <= LAST_INDEX; }
/* a pointer of the build out of a step table at a disk offset (the table start is mapped, the index is not) */
static uint32_t entry(SheetCtx *c, uint32_t table, int index) { return program_dword(c->s->prog, table, (uint32_t)index); }

/* The activity pair at the step index (one r1 draw picks the wording). random_kind: 1 = run a random-text entry
 * (states 6, 9, 16); 0 = states 5, 11, 15, where the original prints the routine's machine code as text - unreachable
 * with the shipped data (those codes are bed steps), so the engine leaves that line empty. Returns 0 for an index
 * outside the table (the original reads beyond it, or hangs on the neuro-ward sheet; guarded: nothing drawn). */
static int activity_pair(SheetCtx *c, const uint8_t *p, int random_kind) {
    int index = c->mem->step_index;
    uint32_t table = r(c, 1) ? ACT_1 : ACT_0;
    if (!index_ok(index)) return 0;
    uint32_t e = entry(c, table, index);
    clr(c);
    if (rb(c, e) != 0) {
        raw_string(c, e); /* a '$' entry is an empty line */
        return 1;
    }
    int code = index / 4, t = random_text(c, code, 0);
    if (t) {
        if (random_kind) text(c, t);
        return 1;
    }
    raw_string(c, e + 6); /* the argument string, then the instrument */
    instrument(c, p, code_capability(code));
    return 1;
}

/* The mood pair at the step index as it stands. fallback: the text for a '$' entry (0 = empty line). run: a computed
 * entry is run (random text: the neuro ward's own branch); otherwise it gives an empty line (original, reproduced). */
static int mood_pair(SheetCtx *c, int fallback, int run) {
    int index = c->mem->step_index;
    uint32_t table = r(c, 1) ? MOOD_1 : MOOD_0;
    if (!index_ok(index)) return 0;
    uint32_t e = entry(c, table, index);
    clr(c);
    int first = rb(c, e);
    if (first == '$') {
        if (fallback) text(c, fallback);
    } else if (first == 0) {
        int t = run ? random_text(c, index / 4, 1) : 0;
        if (t) text(c, t);
    } else {
        raw_string(c, e);
    }
    return 1;
}

/* ---- the common lines -------------------------------------------------------------------------------------------- */

static void symptom(SheetCtx *c, const uint8_t *p) {
    size_t n;
    const uint8_t *t = textdb_text(c->texts, 3291 + p[P_AILMENT], &n);
    if (n == 0 && t[0] == 0) { /* printed from its second byte, without the severity word (textdb_text stops at the 0) */
        t = textdb_raw(c->texts, 3291 + p[P_AILMENT], &n);
        size_t k = 1;
        while (k < n && t[k] != '$' && t[k] != 0) k++;
        n = k;
        msg_append(c->m, t + 1, n - 1);
        return;
    }
    text(c, 2376 + (100 - p[P_HEALTH]) / 16);
    msg_append(c->m, t, n);
}

static void complaints(SheetCtx *c, const uint8_t *p) {
    clr(c);
    if (p[P_DEAD]) text(c, 235);
    else if (p[P_HEALTH] < 25) text(c, 234);
    else if (p[P_WISH]) {
        text(c, 3281 + p[P_WISH]);
        if (ib(c, WISHES, 4u * p[P_WISH] + 2)) text(c, 3197 + p[P_VARIANT]);
    } else if (!u16(p + P_ILLNESS)) text(c, 233);
    else symptom(c, p);
}

/* 0x27FB0: the body-part variant; it decides by the true illness (field 56) */
static void variant(SheetCtx *c, const uint8_t *p) {
    uint32_t ill = illness_at(p[P_ILLNESS]);
    if (!p[P_BODY] || ib(c, ill + 15, 0) == 3) return;
    text(c, (ib(c, ill + 16, 0) == 3 ? 3203 : 3197) + p[P_BODY]);
}

static void diagnosis(SheetCtx *c, const uint8_t *p) { /* 0x27FF5: "no illness" is u8 field 56 (docs/ui-sheets.md 6.2) */
    if (p[P_WISH]) set(c, 3396 + p[P_WISH]);
    else if (!p[P_ILLNESS]) set(c, 232);
    else set(c, 3210 + p[P_DIAGNOSIS]), variant(c, p);
}
static void diagnosed(SheetCtx *c, const uint8_t *p) { /* states 5 and 15 after the diagnosis */
    if (p[P_WISH]) set(c, 3396 + p[P_WISH]);
    else set(c, 3210 + p[P_DIAGNOSIS]), variant(c, p);
}

/* the drug part: the routine at T +0x1A, by treatment, with the illness record of field 42 */
static void drug(SheetCtx *c, const uint8_t *p) {
    int t = p[P_TREATMENT];
    if (!idw(c, treatment_at(t) + 0x1A, 0)) return;
    uint32_t ill = illness_at(p[P_DIAGNOSIS]);
    switch (t) {
    case 2: num(c, ib(c, ill, 0) + 1), text(c, 3493), text(c, 3777 + ib(c, ill + 5, 0)); break;
    case 3: text(c, 3777 + ib(c, ill + 5, 0)); break;
    case 4: text(c, 3777 + ib(c, ill + 10, 0)); break;
    case 5: text(c, 3118 + p[P_WISH]); break;
    case 6: case 7:
        text(c, 3491), text(c, 3686 + ib(c, ill + 15, 0)), text(c, 3492), text(c, 3655 + ib(c, ill + 1, 0));
        break;
    case 9: case 10: text(c, 3686 + ib(c, ill + 15, 0)), text(c, 3197 + p[P_BODY]), text(c, 3490); break;
    case 14: text(c, 3686 + ib(c, ill + 15, 0)), text(c, 3203 + p[P_BODY]); break;
    case 16: case 17: case 18: case 19: text(c, 3486 + t - 16); break;
    case 28: text(c, 3494); break;
    case 29: text(c, 3495); break;
    case 59: text(c, 3496); break;
    default: break; /* no routine of the eleven: nothing appended (engine) */
    }
}

static void treatment_name(SheetCtx *c, const uint8_t *p, int bed_prefix) {
    int t = p[P_TREATMENT];
    clr(c);
    if (bed_prefix && tb(c, t, 4)) text(c, 3497);
    text(c, iw(c, treatment_at(t) + 0x1E, 0));
    drug(c, p);
}

static void treatment(SheetCtx *c, const uint8_t *p) { /* 0x2805C, with the dosage */
    int t = p[P_TREATMENT];
    if (p[P_WISH]) {
        if (ib(c, WISHES, 4u * p[P_WISH] + 1)) set(c, 2068);
        return; /* wishes 4 and 5: the buffer is not touched, the line repeats the diagnosis */
    }
    if (!p[P_ILLNESS]) {
        set(c, 231);
        return;
    }
    if (t >= 0xFD) { /* engine guard: no treatment record (cannot happen in a bed ward) */
        set(c, t == 0xFD ? 2097 : 2098);
        return;
    }
    treatment_name(c, p, 1);
    int every = tb(c, t, 0x0A);
    if (!tb(c, t, 0x24) || (!every && !tb(c, t, 5))) return;
    int n = 0;
    for (int k = 0; k < 5; k++) n += ib(c, STEP_MINUTES, (uint32_t)tb(c, t, 0x10 + k));
    text(c, 2069), num(c, (long)n * tb(c, t, 0x25));
    text(c, every >= 2 ? 3110 + every : every ? 2071 : 2070);
}

static void opinion(SheetCtx *c, const uint8_t *p) { /* 0x281D8 */
    set(c, p[P_DEAD] ? 2082 : 2383 + p[P_OPINION] / 16);
}
static void cured_mood(SheetCtx *c, const uint8_t *p) { set(c, 226 + p[P_MOOD] / 32); } /* 0x28261 */

/* 0x282DA: the missing equipment (R +0x44 / +0x45) or the state's default text */
static void missing(SheetCtx *c, const uint8_t *rec, int dflt) {
    if (!u16(rec + 0x44)) set(c, dflt);
    else if (rec[0x44]) set(c, 3043 + rec[0x44]);
    else set(c, 3022 + rec[0x45]);
}

/* ---- page 1 by state --------------------------------------------------------------------------------------------- */

typedef struct {
    SheetCtx *c;
    SheetPage *out;
} Page;

static void take(Page *g, int line) {
    g->out->line[line] = *g->c->m;
    g->out->drawn[line] = 1;
}
static void take_text(Page *g, int line, int n) { set(g->c, n), take(g, line); }

/* the hangs: nothing of the five lines is drawn (guarded, docs/engine.md) */
static void hang(Page *g) { (void)g; }

static void state_5(Page *g, const uint8_t *p, const uint8_t *rec) { /* treatment room */
    SheetCtx *c = g->c;
    int diag = u16(rec + 0x0E), t = p[P_TREATMENT];
    if (diag == 999) {
        take_text(g, SHEET_DIAGNOSIS, 2095), take_text(g, SHEET_TREATMENT, 2100);
        missing(c, rec, 2167), take(g, SHEET_ACTIVITY);
        take_text(g, SHEET_MOOD, 1782), opinion(c, p), take(g, SHEET_OPINION);
        return;
    }
    if (diag) {
        take_text(g, SHEET_DIAGNOSIS, 2094), take_text(g, SHEET_TREATMENT, 2100);
        kind_item(c, p, 9);
        set(c, 2171); /* English CD: 2171 + 2172 + name, no article (0x45C48, docs/editions.md 7.3, rule B) */
        if (!program_english(c->s->prog)) text(c, 1607 + sim_equipment_item(c->s, c->mem->kind_item)[7]);
        text(c, 2172);
        sheet_item_name(c, c->mem->kind_item, 0), take(g, SHEET_ACTIVITY);
        take_text(g, SHEET_MOOD, 1779), opinion(c, p), take(g, SHEET_OPINION);
        return;
    }
    if (t == 0xFD || t == 0xFF) {
        take_text(g, SHEET_DIAGNOSIS, t == 0xFD ? 2090 : 2091), take_text(g, SHEET_TREATMENT, t == 0xFD ? 2097 : 2098);
        take_text(g, SHEET_ACTIVITY, 2164), take_text(g, SHEET_MOOD, 1782), take_text(g, SHEET_OPINION, 1774);
        return;
    }
    if (t == 0xFE) { /* the original loops for ever on this sheet */
        hang(g);
        return;
    }
    diagnosed(c, p), take(g, SHEET_DIAGNOSIS);
    treatment_name(c, p, 1), take(g, SHEET_TREATMENT);
    int mood = 1782;
    if (tb(c, t, 4)) set(c, 2152);
    else if (!u16(rec + 0x10)) set(c, tb(c, t, 6) ? 2161 : !tb(c, t, 5) ? 2163 : 2153);
    else if (u16(rec + 0x12) == 999) missing(c, rec, 2160);
    else if (!u16(rec + 0x12)) set(c, u16(rec + 0x10) == 12 ? 2149 : 2150);
    else if (!p[P_STEP]) set(c, 2158);
    else {
        c->mem->step_index = 4 * step_code(c, p);
        if (!activity_pair(c, p, 0)) clr(c);
        take(g, SHEET_ACTIVITY);
        mood_pair(c, 1782, 0), take(g, SHEET_MOOD);
        opinion(c, p), take(g, SHEET_OPINION);
        return;
    }
    take(g, SHEET_ACTIVITY);
    take_text(g, SHEET_MOOD, mood);
    opinion(c, p), take(g, SHEET_OPINION);
}

static void state_11(Page *g, const uint8_t *p, const uint8_t *rec) { /* dental station */
    SheetCtx *c = g->c;
    int diag = u16(rec + 0x0E), t = p[P_TREATMENT];
    if (p[P_WISH] && !diag) { /* engine order: the wish after the two diagnosis states */
        take_text(g, SHEET_DIAGNOSIS, 2092), take_text(g, SHEET_TREATMENT, 2099);
        take_text(g, SHEET_ACTIVITY, 2165), take_text(g, SHEET_MOOD, 1780), take_text(g, SHEET_OPINION, 1778);
        return;
    }
    if (diag == 999) {
        take_text(g, SHEET_DIAGNOSIS, 2095), take_text(g, SHEET_TREATMENT, 2100);
        missing(c, rec, 2167), take(g, SHEET_ACTIVITY);
        take_text(g, SHEET_MOOD, 1780), take_text(g, SHEET_OPINION, 1778);
        return;
    }
    if (diag) {
        take_text(g, SHEET_DIAGNOSIS, 2094), take_text(g, SHEET_TREATMENT, 2100);
        kind_item(c, p, 12);
        set(c, 2169), sheet_item_name(c, c->mem->kind_item, 0), text(c, 2170), take(g, SHEET_ACTIVITY);
        take_text(g, SHEET_MOOD, 1779), opinion(c, p), take(g, SHEET_OPINION);
        return;
    }
    if (t == 0xFF) { /* the original loops for ever (the dental choice never sets FF) */
        hang(g);
        return;
    }
    if (t == 0xFD || t == 0xFE) {
        take_text(g, SHEET_DIAGNOSIS, t == 0xFD ? 2090 : 2092), take_text(g, SHEET_TREATMENT, t == 0xFD ? 2097 : 2099);
    } else {
        set(c, 3210 + p[P_DIAGNOSIS]), text(c, 3406 + p[P_BODY]), take(g, SHEET_DIAGNOSIS);
        treatment_name(c, p, 0), take(g, SHEET_TREATMENT);
    }
    int mood = 1780;
    if (!u16(rec + 0x10)) set(c, t < 0xFD && tb(c, t, 5) ? 2153 : 2162); /* engine reading: as state 5's byte-5 test */
    else if (u16(rec + 0x12) == 999) missing(c, rec, 2159);
    else if (!u16(rec + 0x12)) set(c, 2151);
    else if (!p[P_STEP]) set(c, 2157), mood = 1773;
    else {
        c->mem->step_index = 4 * step_code(c, p);
        if (!activity_pair(c, p, 0)) clr(c);
        take(g, SHEET_ACTIVITY);
        mood_pair(c, 1773, 0), take(g, SHEET_MOOD);
        opinion(c, p), take(g, SHEET_OPINION);
        return;
    }
    take(g, SHEET_ACTIVITY);
    take_text(g, SHEET_MOOD, mood);
    opinion(c, p), take(g, SHEET_OPINION);
}

static void state_15(Page *g, const uint8_t *p, const uint8_t *rec) { /* neurology */
    SheetCtx *c = g->c;
    int diag = u16(rec + 0x0E), t = p[P_TREATMENT];
    if (diag == 999) {
        take_text(g, SHEET_DIAGNOSIS, 2095), take_text(g, SHEET_TREATMENT, 2100);
        take_text(g, SHEET_ACTIVITY, 2166), take_text(g, SHEET_MOOD, 1782);
        opinion(c, p), take(g, SHEET_OPINION);
        return;
    }
    if (diag) {
        take_text(g, SHEET_DIAGNOSIS, 2094), take_text(g, SHEET_TREATMENT, 2100);
        take_text(g, SHEET_ACTIVITY, 2168), take_text(g, SHEET_MOOD, 1775);
        opinion(c, p), take(g, SHEET_OPINION);
        return;
    }
    if (p[P_WISH] || t == 0xFE || t == 0xFF) {
        int w = p[P_WISH] || t == 0xFE;
        take_text(g, SHEET_DIAGNOSIS, w ? 2092 : 2091), take_text(g, SHEET_TREATMENT, w ? 2099 : 2098);
        take_text(g, SHEET_ACTIVITY, 2164), take_text(g, SHEET_MOOD, 1782), take_text(g, SHEET_OPINION, 1774);
        return;
    }
    if (t == 0xFD) { /* the original loops for ever on this sheet */
        hang(g);
        return;
    }
    diagnosed(c, p), take(g, SHEET_DIAGNOSIS);
    treatment_name(c, p, 1), take(g, SHEET_TREATMENT);
    int mood = 1782;
    if (tb(c, t, 4)) set(c, 2152), mood = 1781;
    else if (!u16(rec + 0x10)) set(c, tb(c, t, 6) ? 2156 : !tb(c, t, 5) ? 2163 : 2153);
    else if (u16(rec + 0x12) == 999) missing(c, rec, 2155);
    else if (!u16(rec + 0x12)) set(c, 2150);
    else if (!p[P_STEP]) set(c, 2154);
    else {
        c->mem->step_index = 4 * step_code(c, p);
        if (!activity_pair(c, p, 0)) clr(c);
        take(g, SHEET_ACTIVITY);
        mood_pair(c, 1782, 0), take(g, SHEET_MOOD);
        opinion(c, p), take(g, SHEET_OPINION);
        return;
    }
    take(g, SHEET_ACTIVITY);
    take_text(g, SHEET_MOOD, mood);
    opinion(c, p), take(g, SHEET_OPINION);
}

/* State 6's mood line (also the neuro ward's): own = the neuro ward's own branch (K +6 set) */
static void ward_mood(SheetCtx *c, const uint8_t *p, const uint8_t *k, int own) {
    int hunger = (int8_t)p[P_HUNGER], day = !night(c);
    if (hunger >= 80) set(c, day ? 1783 : 1769 + (int)r(c, 3));
    else if (hunger >= 60) set(c, day ? 1784 : 1765 + (int)r(c, 3));
    else if (hunger >= 40) set(c, day ? 1785 : 1761 + (int)r(c, 3));
    else if (!day) set(c, 1753 + (int)r(c, 7));
    else if (!u16(p + P_ILLNESS)) cured_mood(c, p);
    else if (!u16(k + 8)) set(c, 1671);
    else if (!mood_pair(c, own ? 0 : 1777, own)) clr(c);
}

static void state_6(Page *g, const uint8_t *p, const uint8_t *k, int neuro) { /* Krankenzimmer, neuro ward */
    SheetCtx *c = g->c;
    diagnosis(c, p), take(g, SHEET_DIAGNOSIS);
    treatment(c, p), take(g, SHEET_TREATMENT);
    /* the neuro ward joins state 6's code (activity and mood) only in the step part with K +6 = 0; the lines before
     * it keep the neuro ward's own mood (docs/ui-sheets.md 6.5) */
    int own = neuro, drawn = 1;
    if (!u16(p + P_ILLNESS)) set(c, 230);
    else if (p[P_RECOVERY]) set(c, neuro ? 2063 : 2062);
    else if (night(c)) set(c, 2072);
    else if (!p[P_IN_STEP] || !p[P_STEP]) set(c, 2073);
    else {
        if (!u16(k + 6)) own = 0;
        int cap = step_capability(c, p);
        if (u16(k + 6) && cap && !find_item(c, p, cap, 0, NULL)) set(c, 3043 + cap); /* 0x311B8; the index stays */
        else {
            c->mem->step_index = 4 * step_code(c, p);
            if (!u16(k + 8)) set(c, 1670);
            else drawn = activity_pair(c, p, 1); /* the neuro ward hangs on an index past the table: guarded */
        }
    }
    if (drawn) take(g, SHEET_ACTIVITY);
    opinion(c, p), take(g, SHEET_OPINION);
    ward_mood(c, p, k, own), take(g, SHEET_MOOD);
}

static void state_9(Page *g, const uint8_t *p, const uint8_t *rec) { /* intensive care */
    SheetCtx *c = g->c;
    diagnosis(c, p), take(g, SHEET_DIAGNOSIS);
    treatment(c, p), take(g, SHEET_TREATMENT);
    if (!u16(p + P_ILLNESS)) set(c, 230);
    else if (p[P_RECOVERY]) set(c, 2062);
    else if (p[P_HEALTH] >= 50 || night(c)) set(c, 2072);
    else if (rec[0x0E] && p[P_STEP]) {
        c->mem->step_index = 4 * step_code(c, p);
        if (!activity_pair(c, p, 1)) clr(c);
    } else set(c, 2073);
    take(g, SHEET_ACTIVITY);
    if (!u16(p + P_ILLNESS)) cured_mood(c, p);
    else if (!mood_pair(c, 1782, 0)) clr(c); /* the index as it stands, also when this sheet did not set it */
    take(g, SHEET_MOOD);
    opinion(c, p), take(g, SHEET_OPINION);
}

static void state_7_8(Page *g, const uint8_t *p, const uint8_t *rec) { /* OP, torture chamber */
    SheetCtx *c = g->c;
    int op = p[P_STATE] == 7;
    diagnosis(c, p), take(g, SHEET_DIAGNOSIS);
    treatment(c, p), take(g, SHEET_TREATMENT);
    if (p[P_DEAD]) {
        take_text(g, SHEET_ACTIVITY, 2373), take_text(g, SHEET_MOOD, 2374), take_text(g, SHEET_OPINION, 2375);
        return;
    }
    if (u16(rec + 0x0E) == 999) {
        if (get16(rec + 0x12) == -1) set(c, op ? 2236 : 2237);
        else if (rec[4]) set(c, 3043 + rec[4]);
        else set(c, op ? 2235 : 2238);
    } else {
        int row = u16(rec + 0x10), i = u16(rec + 0x12);
        int stage = i <= 10 ? ib(c, (op ? OP_ROWS : TORTURE_ROWS) + 11u * (uint32_t)row + (uint32_t)i, 0) : 0;
        int n = (op ? 4116 : 4083) + stage;
        size_t len;
        const uint8_t *t = textdb_raw(c->texts, n, &len);
        clr(c);
        if (op && len > 2 && t[0] == 0) { /* a text made by a routine: its second byte names it */
            const uint8_t *s = t + 2;
            size_t sl = 0;
            while (sl < len - 2 && s[sl] != '$' && s[sl]) sl++;
            msg_append(c->m, s, sl);
            if (t[1] == 1) sheet_item_name(c, u16(rec + 0x76), 1);
            else if (t[1] == 2) variant(c, p);
        } else {
            text(c, n);
        }
    }
    take(g, SHEET_ACTIVITY);
    take_text(g, SHEET_MOOD, p[P_RECOVERY] ? (op ? 2064 : 2065) : (op ? 2066 : 2067));
    opinion(c, p), take(g, SHEET_OPINION);
}

static void state_18(Page *g, const uint8_t *p, const uint8_t *rec) { /* pathology */
    SheetCtx *c = g->c;
    if (!p[P_DIAGNOSIS]) set(c, 2074);
    else if (p[P_WISH]) set(c, 2075);
    else {
        set(c, 3210 + p[P_DIAGNOSIS]);
        if (ib(c, illness_at(p[P_DIAGNOSIS]) + 8, 0) == 1) text(c, 3406 + p[P_BODY]);
        else variant(c, p);
    }
    take(g, SHEET_DIAGNOSIS);
    take_text(g, SHEET_TREATMENT, 2076);
    int activity = 2077;
    if (u16(rec + 0x0E) != 999) { /* row u16 +0x14 of 0x1F108, index +0x12 */
        int row = u16(rec + 0x14), i = u16(rec + 0x12);
        if (row <= 1 && i < 16) activity = 4060 + ib(c, HARVEST_ROWS, 16u * (uint32_t)row + (uint32_t)i);
    }
    take_text(g, SHEET_ACTIVITY, activity);
    take_text(g, SHEET_AUTOPSY, u16(rec + 0x0E) == 999 ? 2079 : 2078);
    take_text(g, SHEET_MOOD, 2080);
    take_text(g, SHEET_OPINION, 2082);
}

static void state_2_3(Page *g, const uint8_t *p) { /* reception, waiting room (docs/ui-rooms.md, page 1) */
    SheetCtx *c = g->c;
    int st = p[P_STATE];
    take_text(g, SHEET_DIAGNOSIS, st == 2 ? 2093 : 2096);
    take_text(g, SHEET_TREATMENT, 2101);
    take_text(g, SHEET_ACTIVITY, st == 2 ? 2175 : room(c, 3, p[P_ROOM])[0x0F] ? 2173 : 2174);
    if (p[P_DEAD]) set(c, 2080); /* 0x2837E */
    else if (p[P_HEALTH] < 25) set(c, 2081);
    else set(c, (p[P_HEALTH] < 65 ? 2390 : 2397) + p[P_MOOD] / 16);
    take(g, SHEET_MOOD);
    if (p[P_DEAD]) set(c, 2082);
    else set(c, (p[P_HEALTH] < 25 ? 2083 : 2383) + p[P_OPINION] / 16);
    take(g, SHEET_OPINION);
}

void sheet_page1(SheetCtx *c, int index, SheetPage *out) {
    memset(out->drawn, 0, sizeof out->drawn);
    for (int k = 0; k < SHEET_LINES; k++) out->line[k].len = 0;
    Page g = {c, out};
    const uint8_t *p = person(c, index);
    int st = p[P_STATE];
    complaints(c, p), take(&g, SHEET_COMPLAINTS);
    /* no autopsy line on the pathology's own and the intensive care's sheet (docs/ui-sheets.md 6.3) */
    if (st != 18 && st != 9) take_text(&g, SHEET_AUTOPSY, p[P_DEAD] ? 1751 : 2142);
    const uint8_t *rec = room(c, st, p[P_ROOM]);
    switch (st) {
    case 2: case 3: state_2_3(&g, p); break;
    case 5: state_5(&g, p, rec); break;
    case 6: state_6(&g, p, rec, 0); break;
    case 16: state_6(&g, p, rec, 1); break;
    case 7: case 8: state_7_8(&g, p, rec); break;
    case 9: state_9(&g, p, rec); break;
    case 11: state_11(&g, p, rec); break;
    case 15: state_15(&g, p, rec); break;
    case 18: state_18(&g, p, rec); break;
    default: break; /* not on a sheet */
    }
}

/* ---- the personnel sheet's activity line ------------------------------------------------------------------------- */

static int lo_add(int x, int b) { return (x & 0xFF00) | ((x + b) & 0xFF); } /* "+lo b": no carry into the high byte */

int sheet_selector(const uint8_t *s, int d, int h, int which) {
    int w6 = u16(s + 6), w8 = u16(s + 8), w16 = u16(s + 16), b21 = s[21], b10 = s[10];
    int x;
    switch (which) {
    case SEL_A: case SEL_C: case SEL_D:
        x = (lo_add(((w6 + d) * 5 + h) & 0xFFFF, b21) + w16) * 7 + w8;
        return (x & 0xFFFF) % (which == SEL_A ? 4 : which == SEL_C ? 2 : 8);
    case SEL_F: case SEL_G: case SEL_H:
        x = (lo_add(((w6 + d) * 3 + d) & 0xFFFF, b21) + w16) * 7 + w8;
        return (x & 0xFFFF) % (which == SEL_F ? 4 : which == SEL_G ? 2 : 8);
    case SEL_E:
        x = (lo_add(((w6 + d + 3) * 7 + h * 3) & 0xFFFF, b21) + w16) * 3 + w8;
        return (x & 0xFFFF) % 8;
    default: /* SEL_B */
        x = lo_add((((w6 >> 3) + d) * 3 + h) & 0xFFFF, b21);
        x = (x * 3 + w16 + w8) & 0xFFFF;
        x = lo_add((((x >> 1) + h) * 3) & 0xFFFF, b10);
        return x % 4;
    }
}

typedef struct {
    SheetCtx *c;
    const uint8_t *st, *sl;
    uint8_t *rec;
    int index, n, d, h;
} Staff;

static int sel(Staff *f, int which) { return sheet_selector(f->st, f->d, f->h, which); }

static void duration(Staff *f) { /* 0x225AB */
    int hours = f->sl[7];
    if (hours == 0) text(f->c, 1970);
    else if (hours == 1) text(f->c, 1971);
    else num(f->c, hours), text(f->c, 1972);
}
static int tired(Staff *f) { /* 0x2253E: 1 when a phrase was appended */
    int h = f->sl[10];
    if (f->st[21] < 20) return text(f->c, 1981), 1;
    if (h >= 32) return text(f->c, 1980), 1;
    if (h >= 26) return text(f->c, 1979), 1;
    if (h >= 20) return text(f->c, 1978), 1;
    if (h >= 14) return text(f->c, 1977), 1;
    return 0;
}
static void strip(Staff *f) { text(f->c, 1976); }
/* = a + duration + b [+ n + c] */
static void opening(Staff *f, int a, int b, int c2) {
    set(f->c, a), duration(f), text(f->c, b);
    if (c2) num(f->c, f->n), text(f->c, c2);
}
static void patient_phrase(Staff *f, int who) { /* 0x2F246 */
    static const int16_t BEFORE[8] = {1975, 1984, 1986, 1988, 1989, 1990, 1991, 1993};
    static const int16_t AFTER[8] = {0, 1985, 1987, 0, 0, 0, 1992, 0};
    int k = (int)r(f->c, 7);
    text(f->c, BEFORE[k]), person_name(f->c, who);
    if (AFTER[k]) text(f->c, AFTER[k]);
}
/* up to `tries` random(k) draws over the places at rec + at + 4 i until one is taken; ward_state: only a patient in
 * that state counts, a taken place with another state is drawn again without counting (the original then loops for
 * ever when every place is so taken; capped here) */
static int pick(Staff *f, int at, int k, int tries, int ward_state) {
    for (int t = 0, guard = 0; t < tries && guard < 100000; guard++) {
        int who = u16(f->rec + at + 4 * (int)rnd(f->c, (uint32_t)k));
        if (!who) {
            t++;
            continue;
        }
        if (ward_state && person(f->c, who)[P_STATE] != ward_state) continue;
        return who;
    }
    return 0;
}
/* a member u16 at rec + 6 + 2 random(k), drawn until non-zero; 0 when all are 0 (the original hangs: guarded) */
static int member(Staff *f, int k) {
    int any = 0;
    for (int i = 0; i < k; i++) any |= u16(f->rec + 6 + 2 * i);
    if (!any) return 0;
    for (;;) {
        int who = u16(f->rec + 6 + 2 * (int)rnd(f->c, (uint32_t)k));
        if (who) return who;
    }
}
static int word(SheetCtx *c, uint32_t row, int k) { return iw(c, row, 2u * (uint32_t)k); }

static void at_home(Staff *f) {
    SheetCtx *c = f->c;
    int b23 = f->st[23], group = f->st[1], hours = f->sl[7], h = f->h;
    opening(f, 1409, 1410, 0);
    if (b23 >= 0xF9 && b23 <= 0xFE) {
        text(c, word(c, HOME_ROWS + 12u * f->st[26], 0xFE - b23));
        return;
    }
    enum { NIGHT, RULE_A, RULE_B } rule;
    if (hours == 0) {
        text(c, 1412);
        return;
    }
    if (hours <= 8) rule = h < 8 ? NIGHT : sel(f, SEL_B) == 0 ? RULE_A : hours == 1 ? RULE_B : NIGHT;
    else rule = h < 4 ? NIGHT : RULE_B;
    if (rule == RULE_A) {
        static const int16_t A[10] = {1418, 0, 1430, 1414, 1434, 1438, 1442, 1446, 1450, 1454};
        int sa = sel(f, SEL_A);
        if (group == 1) {
            int sd = sel(f, SEL_D);
            if (sd < 4) set(c, 1426 + sd);
            else text(c, 1422 + sd - 4);
        } else if (group == 7) text(c, 1446 + sa);
        else set(c, (group >= 2 && group <= 9 ? A[group] : A[0]) + sa);
    } else if (rule == RULE_B) {
        static const int16_t B[10] = {2476, 2483, 2490, 2497, 2504, 2511, 2518, 2525, 2532, 2539};
        int sd = sel(f, SEL_D);
        if (sd == 7) text(c, 1413);
        else set(c, (group >= 1 && group <= 9 ? B[group] : B[0]) + sd);
    } else if (h >= 22 || h < 7) {
        text(c, 1411);
    } else {
        static const int16_t N[10] = {1791, 1799, 1807, 1815, 2102, 2110, 2118, 2126, 2134, 1823};
        set(c, (group >= 1 && group <= 9 ? N[group] : N[0]) + sel(f, SEL_E));
    }
}

static void course(Staff *f) {
    SheetCtx *c = f->c;
    const uint8_t *e = c->s->world->block[BLK_COURSES] + 10 * f->st[26];
    uint32_t k = COURSE_CATALOG + 22u * e[4];
    int h = f->h, title = iw(c, k, 0);
    if (e[1]) set(c, 57), text(c, title), text(c, 58);
    else if (!e[3]) set(c, 59), text(c, title), text(c, 60);
    else if (h < 4) set(c, 61);
    else if (h >= 20) set(c, (f->st[0] ? 62 : 66) + sel(f, SEL_F));
    else if (h == 19) set(c, 70);
    else if (h == 12) set(c, 71);
    else set(c, iw(c, k + (h >= 12 ? 0x12u : 0x0Eu) + 2u * (uint32_t)sel(f, SEL_G), 0));
}

int sheet_staff_activity(SheetCtx *c, int slot) {
    Staff f = {c, NULL, NULL, NULL, 0, 0, 0, 0};
    f.sl = staff_slots(c->s, H) + slot * STAFF_SLOT_SIZE;
    f.index = u16(f.sl);
    f.st = staff(c, f.index);
    f.d = world_tail(c->s->world, TAIL_DAY), f.h = hour(c);
    int state = f.st[4], man = !f.st[0];
    f.rec = state >= 2 && state <= 18 ? room(c, state, f.st[26]) : NULL;
    f.n = f.st[26] + 1;
    uint8_t *R = f.rec;
    switch (state) {
    case 0: case 1:
        opening(&f, 1501, 1502, 0);
        if (!tired(&f)) text(c, 1503);
        break;
    case 2:
        opening(&f, 1504, 1505, 0);
        if (tired(&f)) break;
        if (R[0x45]) strip(&f);
        else {
            int who = pick(&f, 0x0A, 4, 100, 0);
            if (who) patient_phrase(&f, who);
            else text(c, 1973);
        }
        break;
    case 3:
        opening(&f, 1856, 1857, 1858);
        if (tired(&f)) break;
        if (R[0x0F]) strip(&f);
        else {
            int who = u16(R + 0x0C) ? pick(&f, 0x14, 10, 100, 0) : 0;
            if (who) patient_phrase(&f, who);
            else text(c, 1974);
        }
        break;
    case 4:
        opening(&f, 1859, 1860, 0);
        if (!tired(&f)) text(c, 1861 + sel(&f, SEL_A));
        break;
    case 5:
        opening(&f, man ? 1831 : 1837, 1832, 1833);
        if (u16(R + 0x0C)) {
            if (man) text(c, 1834), person_name(c, u16(R + 0x0C));
            else text(c, 1838), person_name(c, u16(R + 0x0C)), text(c, 1839);
        } else if (!tired(&f)) {
            if (man) {
                if (u16(R + 8)) text(c, 1836), staff_name(c, u16(R + 8));
                else text(c, 1835);
            } else if (u16(R + 0x0A)) text(c, 1841), staff_name(c, u16(R + 0x0A)), text(c, 1842);
            else text(c, 1840);
        }
        break;
    case 6:
        opening(&f, 1865, 1866, 1867);
        if (tired(&f)) break;
        if (R[0x0F]) strip(&f);
        else {
            int who = u16(R + 0x0C) ? pick(&f, 0x14, 10, 100, 6) : 0;
            if (!who) text(c, 1974);
            else {
                int sa = sel(&f, SEL_A);
                if (sa == 3) patient_phrase(&f, who);
                else if (sa == 2) text(c, 1868), person_name(c, who);
                else text(c, 1869 + sa);
            }
        }
        break;
    case 7:
        if (!man) {
            opening(&f, 1871, 1872, 1873);
            if (R[0x17]) strip(&f);
            else if (u16(R + 0x0C)) text(c, 1874);
            else if (!tired(&f)) text(c, 1875);
        } else {
            int surgeon = f.sl[2] != 2; /* slot group (L +2) 2: 1877 / 1878, other men 1880 / 1879 */
            opening(&f, 1876, 1872, 1873);
            if (u16(R + 0x0C)) text(c, surgeon ? 1880 : 1877);
            else if (!tired(&f)) text(c, surgeon ? 1879 : 1878);
        }
        break;
    case 8:
        opening(&f, man ? 1886 : 1881, 1882, 1883);
        if (u16(R + 0x0C)) {
            if (man) text(c, 1888), person_name(c, u16(R + 0x0C));
            else text(c, 1884);
        } else if (!tired(&f)) text(c, man ? 1887 : 1885);
        break;
    case 9:
        opening(&f, 1889, 1890, 1891);
        if (R[0x0F]) strip(&f);
        else if (u16(R + 0x0C)) text(c, 1892), person_name(c, u16(R + 0x0C));
        else if (!tired(&f)) text(c, 1893);
        break;
    case 10: return -1; /* the garage: garage_activity (personnel_ui.c) */
    case 11:
        if (man) {
            opening(&f, 1904, 1905, 1906);
            if (u16(R + 0x0C)) text(c, 1908), person_name(c, u16(R + 0x0C));
            else if (!tired(&f)) {
                if (!u16(R + 8)) text(c, 1909);
                else if (R[0x19]) text(c, 1912), staff_name(c, u16(R + 8)), text(c, 1913);
                else text(c, 1910), staff_name(c, u16(R + 8)), text(c, 1911);
            }
        } else {
            opening(&f, 1914, 1905, 1907);
            if (R[0x19]) strip(&f);
            else if (u16(R + 0x0C)) text(c, 1915), person_name(c, u16(R + 0x0C)), text(c, 1916);
            else if (!tired(&f)) {
                if (u16(R + 0x0A)) text(c, 1918), staff_name(c, u16(R + 0x0A));
                else text(c, 1917);
            }
        }
        break;
    case 12:
        if (man) {
            int sa = sel(&f, SEL_A);
            if (sa == 0) set(c, 242), staff_name(c, u16(R + 2)), text(c, 243);
            else if (sa == 1) set(c, 244);
            else if (sa == 2) set(c, 245), staff_name(c, u16(R + 2)), text(c, 246);
            else set(c, 247);
        } else {
            opening(&f, 237, 238, 0);
            if (R[5]) {
                int who = member(&f, 10);
                if (!who) return 0; /* the flag set and nobody listed: the original hangs (guarded) */
                text(c, 240), staff_name(c, who), text(c, 241);
            } else text(c, 239);
        }
        break;
    case 13:
        opening(&f, 1919, 1920, 0);
        if (!tired(&f)) text(c, r(c, 1) == 0 ? 1921 : 1922);
        break;
    case 14:
        if (man) {
            static const int16_t A[4] = {763, 765, 767, 769}, B[4] = {764, 766, 768, 770};
            int sa = sel(&f, SEL_A);
            set(c, 762), text(c, A[sa]), staff_name(c, u16(R + (sa < 2 ? 4 : 2))), text(c, B[sa]);
        } else if (u16(R + 4) == f.index || u16(R + 2) == f.index) {
            int bar = u16(R + 4) == f.index;
            opening(&f, bar ? 757 : 752, bar ? 758 : 753, 0);
            if (R[0x19]) {
                int who = member(&f, 9);
                if (!who) return 0; /* the flag set and nobody listed: the original hangs (guarded) */
                text(c, bar ? 760 : 755), staff_name(c, who), text(c, bar ? 761 : 756);
            } else text(c, bar ? 759 : 754);
        } else set(c, 771);
        break;
    case 15:
        opening(&f, man ? 1923 : 237, 1924, 1925);
        if (u16(R + 0x0C)) {
            if (man) text(c, 1926), person_name(c, u16(R + 0x0C));
            else text(c, 1930), person_name(c, u16(R + 0x0C)), text(c, 1931);
        } else if (!tired(&f)) {
            if (man) {
                if (u16(R + 8)) text(c, 1928), staff_name(c, u16(R + 8)), text(c, 1929);
                else text(c, 1927);
            } else if (u16(R + 0x0A)) text(c, 1933), staff_name(c, u16(R + 0x0A)), text(c, 1934);
            else text(c, 1932);
        }
        break;
    case 16:
        opening(&f, 1935, 1936, 1937);
        if (tired(&f)) break;
        if (R[0x0F]) strip(&f);
        else {
            int who = u16(R + 0x0C) ? pick(&f, 0x14, 10, 101, 0) : 0;
            if (!who) text(c, 1974);
            else {
                int k = (int)r(c, 3);
                if (k == 0) patient_phrase(&f, who);
                else if (k == 3) text(c, 1938), person_name(c, who);
                else text(c, k == 1 ? 1939 : 1940);
            }
        }
        break;
    case 17:
        if (man) {
            opening(&f, 1661, 1662, 0);
            if (!tired(&f)) text(c, u16(R + 0x14) ? 1664 : 1663);
        } else {
            opening(&f, 1665, 1666, 0);
            if (!tired(&f)) text(c, get32(R + 0x0C) >= 100 ? 1668 : 1667);
        }
        break;
    case 18:
        opening(&f, 1941, 1942, 0);
        if (u16(R + 0x0C)) text(c, 1944), person_name(c, u16(R + 0x0C));
        else if (!tired(&f)) text(c, 1943);
        break;
    case 0x13: at_home(&f); break;
    case 0x14: {
        int target = u16(f.sl + 0x0C), v = f.sl[0x0E];
        opening(&f, 1945, 1946, 0);
        if (!target) text(c, 1951 + sel(&f, SEL_D));
        else if (v >= 21) text(c, 1947), person_name(c, target), text(c, 1948);
        else if (v < 11) text(c, 1949), person_name(c, target), text(c, 1950);
        else {
            uint32_t row = THUG_PERSON_ROWS + 4u * f.sl[0x0F];
            text(c, word(c, row, 0)), person_name(c, target), text(c, word(c, row, 1));
        }
        break;
    }
    case 0x15: {
        int target = u16(f.sl + 0x0C), v = f.sl[0x0E];
        if (!target) {
            opening(&f, 1959, 1960, 0);
            text(c, 1962 + sel(&f, SEL_D));
        } else if (v < 11) set(c, 1961);
        else {
            int k = v >= 41 ? 0 : v >= 31 ? 2 : v >= 21 ? 4 : 6;
            uint32_t row = THUG_STAFF_ROWS + 16u * f.sl[0x0F];
            set(c, word(c, row, k)), staff_name(c, target), text(c, word(c, row, k + 1));
        }
        break;
    }
    case 0x16: {
        int hours = f.sl[7];
        if (hours <= 3) set(c, 1526 + hours);
        else set(c, 1530), num(c, (uint8_t)(f.sl[10] - hours + 2)), text(c, 1531);
        break;
    }
    case 0x17: {
        int h = f.h;
        set(c, h < 5 ? 1510 : h <= 8 ? 1511 + h - 5 : h <= 15 ? 1515 + sel(&f, SEL_D) : h == 16 ? 1523 : h == 17 ? 1524 : 1525);
        break;
    }
    case 0x18: set(c, 1506 + sel(&f, SEL_F)); break;
    case 0x19: course(&f); break;
    default: break; /* the buffer stays: the line repeats the mood line */
    }
    return 1;
}
