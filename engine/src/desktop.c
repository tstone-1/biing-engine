/* The patient and holdings reports' counts and the high-score entry (docs/desktop.md, docs/ui-garage.md,
 * docs/saves.md); see desktop.h. */
#include "desktop.h"

#include <stdio.h>
#include <string.h>

#include "sim_internal.h"

#define PERSONS 1009
#define PERSON_SIZE 106
#define ILLNESSES 0x954
#define ILLNESS_SIZE 20
#define QUACK_KEY 0x0B

/* ---- 146 ------------------------------------------------------------------------------------------------- */

void desktop_patient_counts(Sim *s, PatientCounts *c) {
    memset(c, 0, sizeof *c);
    for (int i = 1; i < PERSONS; i++) {
        const uint8_t *p = s->world->block[BLK_PATIENTS] + i * PERSON_SIZE;
        int ill = get16(p + 56) != 0, home = (int8_t)p[3] < 0;
        if (ill) {
            c->ill++;
            int kind = program_u8(s->prog, ILLNESSES, (uint32_t)p[56] * ILLNESS_SIZE + 8);
            if (p[57] || kind == 0) c->other++;
            else if (kind == 1) c->teeth++;
            else c->mind++;
        }
        if (!home) {
            c->in_hospital++;
            int st = p[4];
            if (st == 6 || st == 7 || st == 8 || st == 9 || st == 16 || st == 17) c->inpatients++;
        } else if (ill) {
            if (p[79]) c->at_home++;
            else c->looking++;
        }
        if (p[104]) c->dead++;
    }
}

/* ---- 153 ------------------------------------------------------------------------------------------------- */

void desktop_holdings(Sim *s, int h, int total[19], int rented[19], int owned[19]) {
    for (int k = 0; k < 19; k++) total[k] = rented[k] = owned[k] = 0;
    const uint8_t *slot = room_slots(s, h);
    for (int i = 0; i < ROOM_SLOTS; i++, slot += ROOM_SLOT_SIZE) {
        if (slot[0] >= 0x80) continue;
        int k = catalog_u8(s, slot[0], CATALOG_CATEGORY);
        if (k < 2 || k > 18) continue;
        total[k]++;
        if (slot[1]) rented[k]++;
        else owned[k]++; /* flag 0: bought (docs/desktop.md, corrected 2026-10-19) */
    }
}

void desktop_holdings_step(Sim *s, int step) {
    uint8_t *pb = s->world->block[BLK_PLAYER0];
    int v = get16(pb + 0x0E) + step;
    if (v > 15) v = 0;
    if (v < 0) v = 15;
    put16(pb + 0x0E, v);
}

/* ---- the high-score list ------------------------------------------------------------------------------- */

static const int BONUS[16] = {8000, 6000, 4000, 3000, 2000, 1500, 1100, 900, 700, 600, 500, 400, 300, 200, 100, 90};

int hiscore_bonus(int rank, int days, int month, int day) {
    if (month == 1 && day < 4) return 0;
    if (rank < 1 || rank > 16) return 0; /* engine guard */
    int b = BONUS[rank - 1];
    static const int STEPS[5] = {30, 25, 20, 15, 10};
    for (int i = 0; i < 5; i++)
        if (days < STEPS[i]) b /= 2;
    return b;
}

static int32_t score_of(const uint8_t *e) { return get32(e + 20); }

int hiscore_insert(uint8_t t[HISCORE_SIZE], int32_t score, int days, int rank) {
    uint8_t *last = t + HISCORE_ENTRY * (HISCORE_ENTRIES - 1);
    if (score <= score_of(last)) return -1;
    memset(last, '$', HISCORE_NAME);
    put32(last + 20, score);
    put16(last + 24, days);
    put16(last + 26, rank);
    for (int swapped = 1; swapped;) {
        swapped = 0;
        for (int i = 0; i < HISCORE_ENTRIES - 1; i++) {
            uint8_t *a = t + HISCORE_ENTRY * i, *b = a + HISCORE_ENTRY;
            if (score_of(a) < score_of(b)) {
                uint8_t tmp[HISCORE_ENTRY];
                memcpy(tmp, a, HISCORE_ENTRY), memcpy(a, b, HISCORE_ENTRY), memcpy(b, tmp, HISCORE_ENTRY);
                swapped = 1;
            }
        }
    }
    /* step 6 remembers the LAST row whose name starts with "$$": an older nameless entry above the new one is not it */
    int found = -1;
    for (int i = 0; i < HISCORE_ENTRIES; i++)
        if (t[HISCORE_ENTRY * i] == '$' && t[HISCORE_ENTRY * i + 1] == '$') found = i;
    return found;
}

int hiscore_enter(Sim *s, uint8_t t[HISCORE_SIZE], int *rank_out, int32_t *score_out) {
    sim_opinion_sum(s);
    sim_score(s);
    uint8_t order[16];
    sim_ranking(s, order);
    int r = 0;
    while (r < 16 && order[r] != 0) r++;
    int rank = r + 1;
    const uint8_t *H = hospital(s, 0);
    int days = (uint16_t)get16(H + 0x162);
    int32_t score = get32(H + 0x44) +
                    hiscore_bonus(rank, days, world_tail(s->world, TAIL_MONTH), world_tail(s->world, TAIL_DAY));
    if (rank_out) *rank_out = rank;
    if (score_out) *score_out = score;
    return hiscore_insert(t, score, days, rank);
}

void quack_encode(const uint8_t plain[HISCORE_SIZE], uint8_t out[QUACK_SIZE]) {
    uint8_t key = QUACK_KEY;
    unsigned sum = 0;
    for (int i = 0; i < HISCORE_SIZE; i++) {
        out[i] = (uint8_t)(plain[i] + key);
        key = (uint8_t)(key + plain[i]);
        sum += out[i];
    }
    put16(out + HISCORE_SIZE, (int)((sum & 0xFFFF) >> 1));
}

int quack_decode(const uint8_t *in, size_t n, uint8_t plain[HISCORE_SIZE]) {
    if (n != QUACK_SIZE) return -1;
    unsigned sum = 0;
    uint8_t prev = QUACK_KEY;
    for (int i = 0; i < HISCORE_SIZE; i++) {
        plain[i] = (uint8_t)(in[i] - prev);
        prev = in[i];
        sum += in[i];
    }
    return (uint16_t)get16(in + HISCORE_SIZE) == ((sum & 0xFFFF) >> 1) ? 0 : -2;
}

int quack_load(const char *path, const Program *p, uint8_t t[HISCORE_SIZE]) {
    memset(t, 0, HISCORE_SIZE);
    if (p) {
        size_t left;
        const uint8_t *d = program_raw_ptr(p, program_at(p, HISCORE_DEFAULT), &left);
        if (d && left >= HISCORE_SIZE) memcpy(t, d, HISCORE_SIZE);
    }
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint8_t buf[QUACK_SIZE + 1];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    uint8_t plain[HISCORE_SIZE];
    if (quack_decode(buf, n, plain)) return -1;
    memcpy(t, plain, HISCORE_SIZE);
    return 1;
}

int quack_save(const char *path, const uint8_t t[HISCORE_SIZE]) {
    uint8_t out[QUACK_SIZE];
    quack_encode(t, out);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = fwrite(out, 1, QUACK_SIZE, f);
    return fclose(f) || n != QUACK_SIZE ? -1 : 0;
}

void hiscore_name_start(uint8_t name[HISCORE_NAME], int *count) {
    name[0] = '|';
    memset(name + 1, '$', HISCORE_NAME - 1);
    *count = 0;
}

int dos_key_codes(int key, int ctrl, int alt, int shift, int out[2]) {
    /* BIOS scan codes: Home Up PgUp Left Right End Down PgDn Ins Del; the letter rows Q-P, A-L, Z-M */
    static const uint8_t NAV[10] = {0x47, 0x48, 0x49, 0x4B, 0x4D, 0x4F, 0x50, 0x51, 0x52, 0x53};
    static const char *const ROWS[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    static const uint8_t ROW_SCAN[3] = {0x10, 0x1E, 0x2C};
    int n = 0;
    if (key == DK_ENTER) out[n++] = ctrl ? 0x0A : 13;
    else if (key == DK_BACKSPACE) out[n++] = ctrl ? 0x7F : 8;
    else if (key == DK_ESCAPE) out[n++] = 0x1B;
    else if (key == DK_TAB) {
        if (shift) out[n++] = 0, out[n++] = 0x0F;
        else out[n++] = 9;
    } else if (key >= DK_HOME && key <= DK_DELETE) out[n++] = 0, out[n++] = NAV[key - DK_HOME];
    else if (key >= DK_F1 && key < DK_F1 + 10) out[n++] = 0, out[n++] = 0x3B + key - DK_F1;
    else if (ctrl && !alt) {
        if (key >= 'a' && key <= 'z') out[n++] = key - 'a' + 1;
        else if (key == '[') out[n++] = 0x1B;
        else if (key == '\\') out[n++] = 0x1C;
        else if (key == ']') out[n++] = 0x1D;
        else if (key == '6') out[n++] = 0x1E;
        else if (key == '-') out[n++] = 0x1F;
        else if (key == '2') out[n++] = 0, out[n++] = 3;
    } else if (alt && !ctrl && key >= 'a' && key <= 'z') {
        for (int r = 0; r < 3; r++) {
            const char *p = strchr(ROWS[r], key);
            if (p) out[n++] = 0, out[n++] = ROW_SCAN[r] + (int)(p - ROWS[r]);
        }
    }
    return n;
}

int hiscore_name_key(uint8_t name[HISCORE_NAME], int *count, int key) {
    int c = *count; /* the field counts its characters; it never searches for the cursor */
    if (c < 0 || c > HISCORE_MAX_NAME) return 1; /* the input has ended */
    if (key < 0 || (key & 0xFFFF) == 0xFFFF || (key & 0xFFFF) == 0) return 0; /* none, or an extended key's 0 */
    key &= 0xFF;
    if (key == 0x15 || key == '$' || key == '@') return 0;
    if (key == 13) {
        name[c] = 0; /* over the cursor */
        *count = -1;
        return 1;
    }
    if (key == 8) {
        if (c > 0) name[c] = 0, name[c - 1] = '|', *count = c - 1; /* 0, not '$', at the old cursor place */
        return 0;
    }
    if (c >= HISCORE_MAX_NAME) return 0;
    name[c] = (uint8_t)key;
    name[c + 1] = '|';
    *count = c + 1;
    return 0;
}
