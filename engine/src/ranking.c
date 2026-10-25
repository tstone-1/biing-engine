/* The 25 ranking windows of the Bing-Desktop (docs/desktop.md, "Ranking table" and "The ranking windows"):
 * one generic table drawn from a parameter row per window. */
#include "ranking.h"

#include <stdio.h>
#include <string.h>

/* A value is a sum of up to six terms, each a hospital-record field times a factor. */
enum { U16 = 1, S16, S32, S8, BLOCK35 /* the hospital's u16 in save block 35, [0x5C71] */ };
typedef struct {
    uint8_t kind;
    uint16_t off;
    int32_t mul;
} Term;
typedef struct {
    Term t[7];
} Expr;
enum { COL_NUMBER, COL_NUMBER_CLAMP0, COL_NUMBER_U16, COL_PREV_RANK, COL_AVG_OPINION };
typedef struct {
    uint8_t kind;
    int16_t x;
    Expr e;
} Column;
enum { PRE_NONE, PRE_COSTS, PRE_OPINION, PRE_SCORE, PRE_STAFF_AVG, PRE_STAFF_TRAINING, PRE_NURSE_EXPERIENCE };
enum { SORT_KEY, SORT_GOODS };
typedef struct {
    uint32_t routine, refresh; /* click routine; per-frame refresh entry (0 = none) */
    int16_t header, screen;
    uint8_t pre, sort;
    int16_t rank_x, name_x;
    uint8_t keep_bankrupt_rows; /* the Rangliste leaves an empty row */
    uint16_t prev_rank_field; /* +0xEA / +0xEB, updated after a row is drawn; 0 = none */
    Expr key;
    Column col[4];
} Window;

#define DAILY_COST 0xFFFF /* pseudo field: the hospital's daily cost from 0x1AEC0 */
#define T(k, o, m) {k, o, m}
#define F(o) {{T(U16, o, 1)}}
#define F32(o) {{T(S32, o, 1)}}
static const Window WINDOWS[] = {
    {0x48059, 0x48041, 1217, 112, PRE_SCORE, SORT_KEY, 112, 210, 1, 0xEA, F32(0x44),
     {{COL_PREV_RANK, 156, {{{0}}}}, {COL_NUMBER, 388, F32(0x44)}}},
    {0x389F2, 0x389DA, 1220, 113, PRE_COSTS, SORT_KEY, 114, 154, 0, 0, {{T(S32, 0x16, 1), T(S32, DAILY_COST, -1)}},
     {{COL_NUMBER, 302, F32(0x16)}, {COL_NUMBER, 370, {{T(S32, DAILY_COST, 1)}}},
      {COL_NUMBER, 430, {{T(S32, 0x16, 1), T(S32, DAILY_COST, -1)}}}}},
    {0x3E185, 0x3E16D, 1222, 114, PRE_NONE, SORT_KEY, 114, 154, 0, 0,
     {{T(U16, 0x26, 16), T(U16, 0x12A, 16), T(U16, 0x12C, 16), T(U16, 0x28, 1)}},
     {{COL_NUMBER, 299, {{T(U16, 0x26, 1), T(U16, 0x12A, 1), T(U16, 0x12C, 1)}}}, {COL_NUMBER, 370, F(0x28)},
      {COL_NUMBER, 441, F(0x2A)}}},
    {0x25697, 0x2567C, 1218, 115, PRE_OPINION, SORT_KEY, 114, 210, 0, 0xEB, F32(0x44),
     {{COL_PREV_RANK, 156, {{{0}}}}, {COL_AVG_OPINION, 366, F32(0x44)}, {COL_NUMBER, 420, F(0x164)},
      {COL_NUMBER, 468, F(0x166)}}},
    {0x50A6B, 0x50A53, 1224, 116, PRE_NONE, SORT_KEY, 114, 154, 0, 0, {{T(U16, 0x30, 16), T(U16, 0x54, 1)}},
     {{COL_NUMBER, 299, F(0x30)}, {COL_NUMBER, 370, F(0x54)}, {COL_NUMBER, 441, F(0xEC)}}},
    {0x50C6F, 0x50C57, 1225, 117, PRE_NONE, SORT_KEY, 114, 154, 0, 0, {{T(U16, 0x4E, 16), T(U16, 0x52, 1)}},
     {{COL_NUMBER, 299, F(0x4E)}, {COL_NUMBER, 370, F(0x52)}, {COL_NUMBER, 441, F(0xEE)}}},
    {0x49211, 0x491F9, 1231, 120, PRE_NONE, SORT_KEY, 114, 154, 0, 0, F32(0x90),
     {{COL_NUMBER, 318, F32(0x8C)}, {COL_NUMBER, 424, F32(0x90)}}},
    {0x386DA, 0, 1238, 123, PRE_NONE, SORT_KEY, 114, 154, 0, 0, {{T(S16, 0x10E, 1)}},
     {{COL_NUMBER, 300, {{T(S16, 0x10E, 1)}}}}},
    {0x38870, 0, 1239, 123, PRE_NONE, SORT_KEY, 114, 154, 0, 0, {{T(S16, 0x110, 1)}},
     {{COL_NUMBER, 300, {{T(S16, 0x110, 1)}}}}},
    {0x507CC, 0x507B4, 1223, 136, PRE_NONE, SORT_KEY, 114, 154, 0, 0,
     {{T(U16, 0x26, 16), T(U16, 0x12A, 16), T(U16, 0x12C, 16), T(U16, 0x30, -16), T(U16, 0x4E, -16), T(U16, 0x28, 1)}},
     {{COL_NUMBER_U16, 299, {{T(U16, 0x26, 1), T(U16, 0x12A, 1), T(U16, 0x12C, 1), T(U16, 0x30, -1), T(U16, 0x4E, -1)}}},
      {COL_NUMBER_CLAMP0, 370,
       {{T(U16, 0x28, 1), T(U16, 0x52, -1), T(U16, 0x54, -1), T(U16, 0x12A, -1), T(U16, 0x12C, -1), T(U16, 0x4C, -1),
         T(U16, 0x50, 1)}}},
      {COL_NUMBER_CLAMP0, 441, {{T(U16, 0x2A, 1), T(U16, 0xEC, -1), T(U16, 0xEE, -1), T(U16, 0x12A, -1), T(U16, 0x12C, -1)}}}}},
    {0x51EFD, 0x51EE5, 1226, 137, PRE_NONE, SORT_KEY, 114, 154, 0, 0, {{T(U16, 0x32, 16), T(U16, 0x34, 1)}},
     {{COL_NUMBER, 299, F(0x32)}, {COL_NUMBER, 370, F(0x34)}, {COL_NUMBER, 441, F(0x36)}}},
    {0x52DA8, 0x52D90, 1227, 138, PRE_NONE, SORT_KEY, 114, 154, 0, 0, {{T(U16, 0x114, 16), T(U16, 0x116, 1)}},
     {{COL_NUMBER, 299, F(0x114)}, {COL_NUMBER, 370, F(0x116)}, {COL_NUMBER, 441, F(0x118)}}},
    {0x531AE, 0x53196, 1228, 139, PRE_NONE, SORT_KEY, 114, 154, 0, 0, {{T(U16, 0x2E, 16), T(U16, 0x48, 1)}},
     {{COL_NUMBER, 299, F(0x2E)}, {COL_NUMBER, 370, F(0x48)}, {COL_NUMBER, 441, F(0x12A)}}},
    {0x52FB4, 0x52F9C, 1229, 140, PRE_NONE, SORT_KEY, 114, 154, 0, 0, {{T(U16, 0x4C, 16), T(U16, 0x50, 1)}},
     {{COL_NUMBER, 299, F(0x4C)}, {COL_NUMBER, 370, F(0x50)}, {COL_NUMBER, 441, F(0x12C)}}},
    {0x505D2, 0x505BA, 1240, 141, PRE_NONE, SORT_KEY, 114, 154, 0, 0, {{T(S16, 0x38, 1)}},
     {{COL_NUMBER, 308, F(0x38)}, {COL_NUMBER, 376, F(0x3C)}, {COL_NUMBER, 451, F(0x3A)}}},
    {0x499F7, 0, 1242, 123, PRE_STAFF_AVG, SORT_KEY, 112, 154, 0, 0, {{T(S16, 0x4A, 1)}},
     {{COL_NUMBER, 372, F(0x4A)}}},
    {0x503A0, 0x50388, 1232, 142, PRE_NONE, SORT_GOODS, 114, 154, 0, 0, {{{0}}},
     {{COL_NUMBER, 318, {{T(S32, 0xCA, 1), T(S32, 0xD2, 1), T(S32, 0x124, 1)}}},
      {COL_NUMBER, 424, {{T(S32, 0xCE, 1), T(S32, 0xD6, 1), T(S32, 0x12E, 1)}}}}},
    {0x3E490, 0x3E478, 1243, 143, PRE_STAFF_TRAINING, SORT_KEY, 114, 154, 0, 0, {{T(U16, 0x136, 64), T(U16, 0x134, 1)}},
     {{COL_NUMBER, 302, F(0x134)}, {COL_NUMBER, 366, F(0x136)}, {COL_NUMBER, 437, F(0x138)}}},
    {0x51CC9, 0x51CB1, 1236, 144, PRE_STAFF_TRAINING, SORT_KEY, 114, 154, 0, 0, {{T(S32, 0xAA, 1), T(S32, 0x13E, 1)}},
     {{COL_NUMBER, 302, F(0x134)}, {COL_NUMBER, 376, {{T(S32, 0xA6, 1), T(S32, 0x11C, 1)}}},
      {COL_NUMBER, 446, {{T(S32, 0xAA, 1), T(S32, 0x13E, 1)}}}}},
    {0x4F9E5, 0x4F9CD, 1234, 145, PRE_NONE, SORT_KEY, 114, 154, 0, 0,
     {{T(S32, 0xBE, 1), T(S32, 0xC6, 1), T(S32, 0x13A, 1), T(S32, 0x6E, 1)}},
     {{COL_NUMBER, 334, {{T(S32, 0xBA, 1), T(S32, 0xC2, 1), T(S32, 0x120, 1), T(S32, 0x72, 1)}}},
      {COL_NUMBER, 426, {{T(S32, 0xBE, 1), T(S32, 0xC6, 1), T(S32, 0x13A, 1), T(S32, 0x6E, 1)}}}}},
    {0x38EF2, 0, 1237, 123, PRE_NONE, SORT_KEY, 114, 152, 0, 0, F32(0x146),
     {{COL_NUMBER, 292, F32(0x102)}, {COL_NUMBER, 366, F32(0x146)}, {COL_NUMBER, 440, F32(0x142)}}},
    {0x591E9, 0, 1245, 123, PRE_NONE, SORT_KEY, 114, 154, 0, 0,
     {{T(U16, 0xE6, 1), T(U16, 0xE8, 1), T(U16, 0xF0, 1), T(U16, 0xF2, 1)}},
     {{COL_NUMBER_U16, 302, {{T(U16, 0xE6, 1), T(U16, 0xE8, 1), T(U16, 0xF0, 1), T(U16, 0xF2, 1)}}},
      {COL_NUMBER, 372, {{T(U16, 0xE6, 1), T(U16, 0xE8, 1)}}}, {COL_NUMBER, 440, {{T(U16, 0xF0, 1), T(U16, 0xF2, 1)}}}}},
    {0x5940F, 0, 1233, 123, PRE_NONE, SORT_KEY, 114, 154, 0, 0, F32(0x76),
     {{COL_NUMBER, 318, F32(0x7A)}, {COL_NUMBER, 424, F32(0x76)}}},
    {0x59040, 0, 1230, 123, PRE_NONE, SORT_KEY, 114, 154, 0, 0, {{T(U16, 0x3E, 1 << 19), T(S32, 0x40, 1)}},
     {{COL_NUMBER, 308, F(0x3E)}, {COL_NUMBER, 392, F32(0x40)}}},
    {0x58E64, 0, 1235, 123, PRE_NONE, SORT_KEY, 114, 154, 0, 0, {{T(S32, 0x9A, 1), T(S32, 0xA2, 1)}},
     {{COL_NUMBER, 313, F32(0x96)}, {COL_NUMBER, 382, F32(0x9A)}, {COL_NUMBER, 451, {{T(S32, 0x152, 1), T(S32, 0x15A, -1)}}}}},
    {0x595A1, 0, 1241, 123, PRE_NONE, SORT_KEY, 114, 154, 0, 0,
     {{T(U16, 0x15E, 1 << 20), T(U16, 0x160, 1 << 15), T(U16, 0x128, 1), T(BLOCK35, 0, 1)}},
     {{COL_NUMBER, 308, F(0x15E)}, {COL_NUMBER, 368, F(0x160)}, {COL_NUMBER, 433, {{T(U16, 0x128, 1), T(BLOCK35, 0, 1)}}}}},
    {0x49B0C, 0, 1244, 123, PRE_STAFF_AVG, SORT_KEY, 112, 154, 0, 0, {{T(S8, 0x14C, 1)}},
     {{COL_NUMBER, 372, {{T(S8, 0x14C, 1)}}}}},
};
#define WINDOW_COUNT ((int)(sizeof WINDOWS / sizeof *WINDOWS))

/* German CD ("censored, 16"): icon 19 ranks the nurses' average experience +0x194 instead of the bust +0x4A, same
 * place and renderer; its header picture 1242 is the CD's own (docs/editions.md 7.2, OTH-DE-8). */
static const Window NURSE_EXPERIENCE = {0x499F7, 0, 1242, 123, PRE_NURSE_EXPERIENCE, SORT_KEY, 112, 154, 0, 0,
                                        F32(0x194), {{COL_NUMBER, 372, F32(0x194)}}};

/* window i as the edition shows it */
static const Window *window(const Biing *b, int i) {
    if (WINDOWS[i].routine == 0x499F7 && program_german_cd(&b->prog)) return &NURSE_EXPERIENCE;
    return &WINDOWS[i];
}

enum { IMG_BODY = 1216, IMG_THUMB = 1247, IMG_PLAYER_ROW = 1047, NAMES = 0x3BCF, NAME_SIZE = 30 };

static uint32_t field(Biing *b, int h, const Term *t) {
    if (t->kind == S32 && t->off == DAILY_COST) return (uint32_t)sim_daily_costs(&b->sim, h);
    if (t->kind == BLOCK35) {
        const uint8_t *p = b->world.block[34] + 2 * h;
        return (uint16_t)(p[0] | p[1] << 8);
    }
    const uint8_t *r = world_hospital(&b->world, h) + t->off;
    switch (t->kind) {
    case U16: return (uint16_t)(r[0] | r[1] << 8);
    case S16: return (uint32_t)(int16_t)(r[0] | r[1] << 8);
    case S8: return (uint32_t)(int8_t)r[0];
    case S32: return r[0] | r[1] << 8 | (uint32_t)r[2] << 16 | (uint32_t)r[3] << 24;
    default: return 0;
    }
}

/* 32-bit arithmetic as in the original, so packed keys wrap the same way. */
static int32_t eval(Biing *b, int h, const Expr *e) {
    uint32_t v = 0;
    for (int i = 0; i < 7 && e->t[i].kind; i++) v += field(b, h, &e->t[i]) * (uint32_t)e->t[i].mul;
    return (int32_t)v;
}

static void sort(Biing *b, const Window *w, uint8_t order[16]) {
    for (int i = 0; i < 16; i++) order[i] = (uint8_t)i;
    for (int swapped = 1; swapped;) {
        swapped = 0;
        for (int i = 0; i < 15; i++) {
            int a = order[i], c = order[i + 1], swap;
            if (w->sort == SORT_GOODS) {
                /* the original's register slip: +0xD6 + +0x12E of the right entry land in the left sum */
                const Term ce = {S32, 0xCE, 1}, d6 = {S32, 0xD6, 1}, x12e = {S32, 0x12E, 1};
                int32_t left = (int32_t)(field(b, a, &ce) + field(b, a, &d6) + field(b, a, &x12e) + field(b, c, &d6) +
                                         field(b, c, &x12e));
                swap = left < (int32_t)field(b, c, &ce);
            } else {
                swap = eval(b, a, &w->key) < eval(b, c, &w->key);
            }
            if (swap) {
                order[i] = (uint8_t)c;
                order[i + 1] = (uint8_t)a;
                swapped = 1;
            }
        }
    }
}

static TextLayout layout(Biing *b, int x, int y, int width) {
    TextLayout l = {&b->fonts[7], b->prog.charmap, x, y, width, 1, 4, 1};
    return l;
}

static void centred(Biing *b, const char *s, int x, int y, int width) {
    TextLayout l = layout(b, x, y, width);
    text_centred(&b->screen, &l, (const uint8_t *)s, strlen(s));
}

static void number_text(long v, char *s, size_t n) {
    if (v > 999999999L) v = 999999999L;
    if (v < -999999999L) v = -999999999L;
    snprintf(s, n, "%ld ", v);
}

/* The text of one number column of hospital h, as drawn centred in the returned width. */
static int cell(Biing *b, const Window *w, const Column *col, int h, char *s, size_t n) {
    const uint8_t *r = world_hospital(&b->world, h);
    int32_t v = eval(b, h, &col->e);
    switch (col->kind) {
    case COL_PREV_RANK: snprintf(s, n, "(%02d)", r[w->prev_rank_field]); return 35;
    case COL_AVG_OPINION: {
        long a = (long)v * 100 / 1008;
        /* the template "00,00   $" (0x467E): the three trailing spaces count in the centring */
        snprintf(s, n, "%c%c,%02ld   ", (char)('0' + a / 1000), (char)('0' + a % 1000 / 100), a % 100);
        return 85;
    }
    case COL_NUMBER_CLAMP0: number_text(v < 0 ? 0 : v, s, n); return 85;
    case COL_NUMBER_U16: number_text((uint16_t)v, s, n); return 85;
    default: number_text(v, s, n); return 85;
    }
}

static void rows(Biing *b, const Window *w) {
    uint8_t order[16];
    sort(b, w, order);
    int n = 0, rank = 0;
    for (int i = 0; i < 16; i++) {
        int h = order[i];
        uint8_t *r = world_hospital(&b->world, h);
        int y = 120 + 13 * n;
        if (h == 0) dulog_draw(&b->ui, IMG_PLAYER_ROW, 0, 36 + 13 * n);
        int32_t cash = (int32_t)(r[0x16] | r[0x17] << 8 | (uint32_t)r[0x18] << 16 | (uint32_t)r[0x19] << 24);
        if (cash < 0) {
            if (w->keep_bankrupt_rows) n++, rank++;
            continue;
        }
        rank++;
        char s[16];
        snprintf(s, sizeof s, "%02d.", rank);
        centred(b, s, w->rank_x, y, 35);
        const uint8_t *name = program_table(&b->prog, NAMES) + h * NAME_SIZE;
        size_t len = 0;
        while (len < NAME_SIZE - 1 && name[len] != '$') len++;
        TextLayout l = layout(b, w->name_x, y, 400);
        text_left(&b->screen, &l, name, len);
        for (int c = 0; c < 4 && w->col[c].x; c++) {
            int width = cell(b, w, &w->col[c], h, s, sizeof s);
            centred(b, s, w->col[c].x, y, width);
        }
        if (w->prev_rank_field) r[w->prev_rank_field] = (uint8_t)rank;
        n++;
    }
}

static void body(Biing *b, const Window *w) {
    switch (w->pre) {
    case PRE_SCORE: {
        sim_opinion_sum(&b->sim);
        sim_score(&b->sim);
        uint8_t order[16];
        sim_ranking(&b->sim, order);
        break;
    }
    case PRE_OPINION: sim_opinion_sum(&b->sim); break;
    case PRE_STAFF_AVG: sim_staff_averages(&b->sim); break;
    case PRE_STAFF_TRAINING: sim_staff_training(&b->sim); break;
    case PRE_NURSE_EXPERIENCE: sim_nurse_experience(&b->sim); break;
    default: break; /* PRE_COSTS: the daily cost is computed per row */
    }
    dulog_draw(&b->ui, IMG_BODY, 6, 3);
    dulog_draw(&b->ui, IMG_THUMB, 6, 3);
    rows(b, w);
    dulog_set_screen(&b->ui, w->screen);
}

/* The sounds of opening a window (docs/sound.md, "Desktop (screen 97) and its windows"). */
static void window_sound(Biing *b, uint32_t routine) {
    Sound *s = &b->sound;
    switch (routine) {
    case 0x48059: sound_request(s, 0xBA, 0x17C, 60); break; /* Rangliste */
    case 0x389F2: sound_list(s, 0, 0x468A), sound_list(s, 1, 0x468A); break; /* Kapital */
    case 0x505D2: sound_request(s, 0x55, 0x28A, 40); break; /* Tote */
    case 0x25697: { /* Patientenindex, by the player's average x 100 (the column's value) */
        const uint8_t *r = world_hospital(&b->world, 0) + 0x44;
        long a = (long)(int32_t)(r[0] | r[1] << 8 | (uint32_t)r[2] << 16 | (uint32_t)r[3] << 24) * 100 / 1008;
        if (a >= 5500) sound_request(s, 0x82, 0xFA, 40);
        else if (a >= 5200) sound_request(s, 0x77, 0xFA, 40);
        else if (a >= 5000) sound_request(s, 0x70, 0xDC, 40);
        else sound_request(s, a >= 4800 ? 0x1C : a >= 4600 ? 0x34 : 0x39, 0x140, 40);
        break;
    }
    default: break;
    }
}

/* Opening a table clears the per-minute flag (player block +0x2E): every refreshing table, and seven of the
 * screen-123 tables, which never refresh (docs/desktop.md, "Opening a table clears +0x2E too"). */
static int clears_minute_flag(const Window *w) {
    static const uint32_t SCREEN_123[] = {0x386E6, 0x3887C, 0x38EFE, 0x58E70, 0x5904C, 0x591F5, 0x5941B};
    if (w->refresh) return 1;
    for (size_t i = 0; i < sizeof SCREEN_123 / sizeof SCREEN_123[0]; i++)
        if (w->routine == SCREEN_123[i]) return 1;
    return 0;
}

int ranking_cell(Biing *b, uint32_t routine, int h, int c, char *out, size_t n) {
    for (int i = 0; i < WINDOW_COUNT; i++) {
        const Window *w = window(b, i);
        if (routine != w->routine) continue;
        if (c < 0 || c > 3 || !w->col[c].x) return -1;
        cell(b, w, &w->col[c], h, out, n);
        return w->col[c].x;
    }
    return -1;
}

int ranking_routine(Biing *b, uint32_t routine) {
    for (int i = 0; i < WINDOW_COUNT; i++) {
        const Window *w = window(b, i);
        if (routine == w->routine) {
            dulog_draw(&b->ui, w->header, 6, -22);
            if (clears_minute_flag(w)) b->world.block[BLK_PLAYER0][0x2E] = 0; /* first refresh at the next minute */
            body(b, w);
            window_sound(b, routine);
            return 1;
        }
        if (w->refresh && routine == w->refresh) {
            uint8_t *flag = &b->world.block[BLK_PLAYER0][0x2E]; /* set every game minute */
            if (*flag) {
                *flag = 0;
                body(b, w);
            }
            return 1;
        }
    }
    return 0;
}
