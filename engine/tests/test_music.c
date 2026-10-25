/* The digital module player (docs/music.md). Synthetic modules built here, big-endian like the format; no bytes
 * from the game. */
#include <stdio.h>
#include <string.h>

#include "music.h"

static int failures;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            failures++;                                                            \
        }                                                                          \
    } while (0)

/* ---- a module builder ---------------------------------------------------------------------- */

typedef struct {
    uint8_t pat[4096], smp[8192], song[256], ins[2048];
    size_t pat_n, smp_n, song_n, ins_n;
    uint8_t file[16384];
    size_t n;
} Mod;

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8), p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v >> 16), put16(p + 2, v & 0xFFFF); }

static void mod_init(Mod *m) {
    memset(m, 0, sizeof *m);
    m->pat_n = 512;
    m->smp_n = 1024 + 64; /* table, then the empty sample 0 */
    put32(m->smp, 0x400);
    put32(m->smp + 0x400 + 0x3C, 64);
    m->ins_n = 512;
}

/* Pattern pn with events (note, duration, instrument, volume), ended by 0xFF. */
static void mod_pattern(Mod *m, int pn, const uint8_t (*ev)[4], int count) {
    put16(m->pat + 2 * pn, (unsigned)m->pat_n);
    memcpy(m->pat + m->pat_n, "PATTERN         ", 16);
    m->pat_n += 16;
    for (int i = 0; i < count; i++) memcpy(m->pat + m->pat_n, ev[i], 4), m->pat_n += 4;
    memset(m->pat + m->pat_n, 0xFF, 4);
    m->pat_n += 4;
}

static void mod_sample(Mod *m, int i, int len, int loop_start, int loop_end, int transpose) {
    put32(m->smp + 4 * i, (uint32_t)m->smp_n);
    uint8_t *r = m->smp + m->smp_n;
    memcpy(r, "SAMPLE          ", 16);
    put16(r + 0x10, (unsigned)len);
    put16(r + 0x12, (unsigned)loop_start);
    put16(r + 0x14, (unsigned)loop_end);
    put16(r + 0x2A, (unsigned)(uint16_t)transpose);
    put32(r + 0x3C, (uint32_t)(64 + len));
    for (int k = 0; k < len; k++) r[0x40 + k] = (uint8_t)(k & 1 ? 100 : 100); /* constant +100 */
    m->smp_n += 64 + (size_t)len;
}

/* Instrument i with attack (ticks, step), decay (ticks, step), sustain and a program of (op, a, b) commands. */
static void mod_instrument(Mod *m, int i, int at, int as, int dt, int ds, int sus, const int (*prog)[3], int cmds) {
    put16(m->ins + 2 * i, (unsigned)m->ins_n);
    uint8_t *r = m->ins + m->ins_n;
    memcpy(r, "INSTRUMENT      ", 16);
    put16(r + 0x10, (unsigned)(6 * cmds));
    r[0x12] = (uint8_t)at, r[0x13] = (uint8_t)as, r[0x14] = (uint8_t)dt, r[0x15] = (uint8_t)ds, r[0x17] = (uint8_t)sus;
    for (int k = 0; k < cmds; k++)
        for (int j = 0; j < 3; j++) put16(r + 0x30 + 6 * k + 2 * j, (unsigned)(uint16_t)prog[k][j]);
    m->ins_n += 0x30 + 6 * (size_t)cmds;
}

/* A song row: patterns of voices 1-4 and the control pattern (entry 6). */
static void mod_row(Mod *m, int p1, int p2, int p3, int p4, int ctl) {
    uint8_t *r = m->song + m->song_n;
    int p[6] = {p1, p2, p3, p4, 0, ctl};
    for (int k = 0; k < 6; k++) put16(r + 2 * k, (unsigned)(p[k] << 8 | 1));
    m->song_n += 12;
}

static void mod_build(Mod *m, int tempo) {
    uint8_t *f = m->file;
    memset(f, 0, 0x40);
    memcpy(f, "TESTSONG        ", 16);
    put32(f + 0x10, (uint32_t)m->pat_n);
    put32(f + 0x14, (uint32_t)m->smp_n);
    put32(f + 0x18, (uint32_t)m->song_n);
    put32(f + 0x1C, (uint32_t)m->ins_n);
    put16(f + 0x20, 3);
    put16(f + 0x22, (unsigned)tempo);
    size_t o = 0x40;
    memcpy(f + o, m->pat, m->pat_n), o += m->pat_n;
    memcpy(f + o, m->smp, m->smp_n), o += m->smp_n;
    memcpy(f + o, m->song, m->song_n), o += m->song_n;
    memcpy(f + o, m->ins, m->ins_n), o += m->ins_n;
    m->n = o;
}

static Mod g_mod;
static Music g_mu;
static Voice g_v[4];

/* A rest pattern 0 (128 rows) as in every file, so unused voices never end a row. */
static void rest_pattern(Mod *m) {
    const uint8_t rest[1][4] = {{0, 128, 0, 0}};
    mod_pattern(m, 0, rest, 1);
}

/* Starts the module and ticks up to and including the tick of its first row step; returns the ticks taken. */
static int first_row(const Mod *m) {
    CHECK(music_start(&g_mu, m->file, m->n) == 0);
    CHECK(g_mu.row_steps == 0); /* no row step at the start */
    int t = 0;
    while (g_mu.row_steps == 0 && t < 100) music_tick(&g_mu, g_v), t++;
    return t;
}

/* ---- tests --------------------------------------------------------------------------------- */

/* With a MIDI card (docs/music.md, "The module's instruments with a MIDI card"): no row step at all while the list is
 * empty; with a list the rows run and only a listed instrument's note starts (BINT's speech and effects). */
static void test_midi_card(void) {
    Mod *m = &g_mod;
    mod_init(m);
    rest_pattern(m);
    const uint8_t ev[2][4] = {{0x40, 1, 1, 40}, {0x42, 1, 2, 40}};
    mod_pattern(m, 1, ev, 2);
    mod_row(m, 1, 0, 0, 0, 0);
    mod_build(m, 187);
    CHECK(music_start(&g_mu, m->file, m->n) == 0);
    music_set_midi(&g_mu, NULL, 0);
    for (int t = 0; t < 5; t++) music_tick(&g_mu, g_v);
    CHECK(g_mu.row_steps == 0 && g_mu.v[0].note_pitch == 0);
    const uint8_t only[1] = {2};
    CHECK(music_start(&g_mu, m->file, m->n) == 0);
    music_set_midi(&g_mu, only, 1);
    music_tick(&g_mu, g_v); /* row step 1: instrument 1, not listed: no note */
    CHECK(g_mu.row_steps == 1 && g_mu.v[0].note_pitch == 0 && g_mu.v[0].instrument == -1);
    music_tick(&g_mu, g_v); /* row step 2: instrument 2 */
    CHECK(g_mu.v[0].note_pitch == 12 * 4 + 2 - 5 && g_mu.v[0].instrument == 2);
    CHECK(music_start(&g_mu, m->file, m->n) == 0); /* a start clears it: every instrument again */
    music_tick(&g_mu, g_v);
    CHECK(!g_mu.midi_card && g_mu.v[0].note_pitch == 12 * 4 - 5);
}

static void test_freq(void) {
    CHECK(music_freq(72) == 262u << 8); /* pitch 72: 1.0234 source samples per output sample */
    CHECK(music_freq(0) == 262u << 2);
    CHECK(music_freq(11) == 494u << 2);
    CHECK(music_freq(12) == 262u << 3);
    CHECK(music_freq(12 * 9 + 9) == 440u << 11);
    CHECK(music_freq(-5) == music_freq(0));
    CHECK(music_rows_per_second(187) == 50.0);
}

static void test_parse(void) {
    Mod *m = &g_mod;
    mod_init(m);
    rest_pattern(m);
    mod_row(m, 0, 0, 0, 0, 0);
    mod_build(m, 120);
    Module mo;
    CHECK(module_parse(&mo, m->file, m->n) == 0);
    CHECK(mo.tempo == 120 && mo.rows == 1);
    CHECK(mo.smp == 0x40 + m->pat_n);
    CHECK(module_parse(&mo, m->file, m->n - 1) != 0); /* 0x40 + four sizes must be the file length */
    CHECK(module_parse(&mo, m->file, 0x20) != 0);
}

/* The pass counter starts at 0 with the song and a row step comes when it reaches a multiple of 4: the first on the
 * 4th pass of 187, none at the start. Tempo 187: four passes per tick, one row step per tick from tick 1; tempo 100:
 * 400 / 187 per tick; tempo 50: one pass per tick (200, 213, 226, 239), the first row step in tick 4. */
static void test_row_timing(void) {
    Mod *m = &g_mod;
    mod_init(m);
    rest_pattern(m);
    mod_row(m, 0, 0, 0, 0, 0);
    mod_build(m, 187);
    CHECK(music_start(&g_mu, m->file, m->n) == 0);
    CHECK(g_mu.row_steps == 0); /* nothing at the start */
    music_tick(&g_mu, g_v);
    CHECK(g_mu.row_steps == 1);
    for (int t = 0; t < 9; t++) music_tick(&g_mu, g_v);
    CHECK(g_mu.row_steps == 10);
    mod_build(m, 100);
    music_start(&g_mu, m->file, m->n);
    for (int t = 0; t < 187 * 4; t++) music_tick(&g_mu, g_v);
    CHECK(g_mu.row_steps == 400); /* 187 x 4 ticks x 400 / 187 / 4 */
    music_start(&g_mu, m->file, m->n);
    music_tick(&g_mu, g_v);
    CHECK(g_mu.row_steps == 0); /* 400: two passes of 187 */
    music_tick(&g_mu, g_v);
    CHECK(g_mu.row_steps == 1); /* 26 + 400: two more, the fourth is the first row step */
    music_tick(&g_mu, g_v);
    CHECK(g_mu.row_steps == 1);
    music_tick(&g_mu, g_v);
    CHECK(g_mu.row_steps == 2);
    mod_build(m, 50);
    CHECK(first_row(m) == 4);
}

/* Durations: the next event comes `duration` row steps later; 0 means 65,536. The end marker moves to the next row,
 * wrapping to row 0. Every note's pitch index is 12 x octave + semitone + MUSIC_TRANSPOSE (-5). */
static void test_durations(void) {
    Mod *m = &g_mod;
    mod_init(m);
    rest_pattern(m);
    const uint8_t ev[3][4] = {{0x40, 3, 0, 40}, {0x42, 2, 0, 40}, {0x44, 1, 0, 40}};
    mod_pattern(m, 1, ev, 3);
    const uint8_t ev2[1][4] = {{0x50, 0, 0, 40}};
    mod_pattern(m, 2, ev2, 1);
    mod_row(m, 1, 0, 0, 0, 0);
    mod_row(m, 2, 0, 0, 0, 0);
    mod_build(m, 187);
    CHECK(MUSIC_TRANSPOSE == -5);
    CHECK(first_row(m) == 1);
    CHECK(g_mu.v[0].note_pitch == 48 - 5); /* octave 4, semitone 0, a fourth down */
    music_tick(&g_mu, g_v);
    music_tick(&g_mu, g_v);
    CHECK(g_mu.v[0].note_pitch == 43);
    music_tick(&g_mu, g_v); /* third row step after the first event */
    CHECK(g_mu.v[0].note_pitch == 45);
    music_tick(&g_mu, g_v);
    CHECK(g_mu.v[0].note_pitch == 45);
    music_tick(&g_mu, g_v);
    CHECK(g_mu.v[0].note_pitch == 47);
    CHECK(g_mu.row == 0);
    music_tick(&g_mu, g_v); /* the end marker: row 1, and the step runs again at once */
    CHECK(g_mu.row == 1);
    CHECK(g_mu.v[0].note_pitch == 55);
    for (int t = 0; t < 120; t++) music_tick(&g_mu, g_v);
    CHECK(g_mu.row == 1 && g_mu.wraps == 0); /* duration 0 = 65,536 rows (the other voices' rests end at 128) */
    CHECK(g_mu.v[0].note_pitch == 55);
}

/* The song loops: after the last row it starts again at row 0. */
static void test_loop(void) {
    Mod *m = &g_mod;
    mod_init(m);
    rest_pattern(m);
    const uint8_t ev[1][4] = {{0x40, 2, 0, 40}};
    mod_pattern(m, 1, ev, 1);
    mod_row(m, 1, 0, 0, 0, 0);
    mod_row(m, 1, 0, 0, 0, 0);
    mod_build(m, 187);
    first_row(m);
    for (int t = 0; t < 2; t++) music_tick(&g_mu, g_v);
    CHECK(g_mu.row == 1 && g_mu.wraps == 0);
    for (int t = 0; t < 2; t++) music_tick(&g_mu, g_v);
    CHECK(g_mu.row == 0 && g_mu.wraps == 1);
}

/* The control pattern sets (not adds) a transpose per voice, 5 = all voices; the global -5 comes on top. */
static void test_control(void) {
    Mod *m = &g_mod;
    mod_init(m);
    rest_pattern(m);
    const uint8_t ev[2][4] = {{0x40, 1, 0, 40}, {0x40, 1, 0, 40}};
    mod_pattern(m, 1, ev, 2);
    const uint8_t ctl[2][4] = {{1, 1, 1, 0x45}, {1, 5, 5, 3}}; /* & 63: 5; then all: 3 */
    mod_pattern(m, 3, ctl, 2);
    mod_row(m, 1, 1, 0, 0, 3);
    mod_build(m, 187);
    first_row(m);
    CHECK(g_mu.v[0].note_pitch == 48 + 5 - 5);
    CHECK(g_mu.v[1].note_pitch == 48 - 5);
    music_tick(&g_mu, g_v);
    CHECK(g_mu.v[0].note_pitch == 48 + 3 - 5 && g_mu.v[1].note_pitch == 48 + 3 - 5);
}

/* Program "1 n 0, 15 0 0, 0 0 0": silence, start, then the repeat region = the sample's loop. Envelope (a step every
 * ticks + 1 ticks) and volume row. Sample transpose. */
static void test_program_play(void) {
    Mod *m = &g_mod;
    mod_init(m);
    rest_pattern(m);
    const uint8_t ev[1][4] = {{0x60, 0, 1, 70}}; /* volume 70: stored as given, the row takes 63 */
    mod_pattern(m, 1, ev, 1);
    mod_sample(m, 1, 400, 100, 300, -12);
    const int prog[3][3] = {{1, 1, 0}, {15, 0, 0}, {0, 0, 0}};
    mod_instrument(m, 1, 1, 128, 2, 64, 100, prog, 3);
    mod_row(m, 1, 0, 0, 0, 0);
    mod_build(m, 50);
    memset(g_v, 0, sizeof g_v);
    g_v[0].active = 1; /* something sounding before: the first tick of the play silences it */
    first_row(m); /* tick 4: the note, the program's first tick, the first attack step */
    CHECK(!g_v[0].active);
    CHECK(g_mu.v[0].volume == 70);
    CHECK(g_mu.v[0].pitch == 72 - 5 - 12 && g_v[0].step == music_freq(55));
    CHECK(g_mu.v[0].env == 128);
    music_tick(&g_mu, g_v);
    CHECK(g_v[0].active && g_v[0].len == 400 && g_v[0].pos == 0);
    CHECK(g_mu.v[0].env == 128); /* attack ticks 1: a step every second tick */
    CHECK(g_v[0].row == (63 * 128 >> 8) * 64 >> 6);
    music_tick(&g_mu, g_v);
    CHECK(g_v[0].rep_len == 200 && g_v[0].rep == g_v[0].data + 100);
    CHECK(g_mu.v[0].env == 256); /* capped at 256 */
    CHECK(g_v[0].row == 63); /* min(70, 63) x 256 >> 8, master 64 */
    music_tick(&g_mu, g_v);
    CHECK(g_mu.v[0].env == 256); /* the last attack step loaded the attack's tick byte (1) */
    music_tick(&g_mu, g_v);
    CHECK(g_mu.v[0].env == 192); /* then decay ticks 2: a step every third tick */
    music_tick(&g_mu, g_v);
    music_tick(&g_mu, g_v);
    CHECK(g_mu.v[0].env == 192);
    music_tick(&g_mu, g_v);
    CHECK(g_mu.v[0].env == 128);
    for (int t = 0; t < 10; t++) music_tick(&g_mu, g_v);
    CHECK(g_mu.v[0].env == 100); /* held at the sustain level */
    CHECK(g_v[0].row == 63 * 100 / 256);
    g_mu.master = 32;
    music_tick(&g_mu, g_v);
    CHECK(g_v[0].row == (63 * 100 >> 8) * 32 >> 6);
}

/* Command 9 adds to the note's volume as given and keeps it in 0-64; only the row takes min(volume, 63): a note at
 * 64 with -16 is 48, one at 100 with -16 is 64. The row is ((min(vol, 63) x env) >> 8) x master >> 6. */
static void test_note_volume(void) {
    const int vols[3] = {64, 100, 40}, want[3] = {48, 64, 24};
    for (int i = 0; i < 3; i++) {
        Mod *m = &g_mod;
        mod_init(m);
        rest_pattern(m);
        const uint8_t ev[1][4] = {{0x60, 0, 1, (uint8_t)vols[i]}};
        mod_pattern(m, 1, ev, 1);
        const int prog[2][3] = {{9, -16, 0}, {0, 0, 0}};
        mod_instrument(m, 1, 0, 255, 0, 0, 255, prog, 2);
        mod_row(m, 1, 0, 0, 0, 0);
        mod_build(m, 187);
        music_start(&g_mu, m->file, m->n);
        g_mu.master = 54; /* a song level of 63: (6 x 63 >> 3) + 7 */
        music_tick(&g_mu, g_v); /* tempo 187: the first row step, the program and the first attack step */
        CHECK(g_mu.v[0].volume == want[i]);
        int vol = want[i] > 63 ? 63 : want[i];
        /* the envelope at 255: the two shifts round separately, 48 x 255 >> 8 = 47, x 54 >> 6 = 39 (one shift
         * of the whole product would give 40) */
        CHECK(g_mu.v[0].env == 255 && g_v[0].row == (vol * 255 >> 8) * 54 >> 6);
        if (i == 0) CHECK(g_v[0].row == 39);
        music_tick(&g_mu, g_v);
        CHECK(g_mu.v[0].env == 256 && g_v[0].row == vol * 54 >> 6);
    }
}

/* Commands 2 (wait), 7 (slide), 8 (transpose), 9 (volume), 10/11 (loop), 12/13 (variables), 3 with a variable. */
static void test_program_commands(void) {
    Mod *m = &g_mod;
    mod_init(m);
    rest_pattern(m);
    const uint8_t ev[1][4] = {{0x30, 0, 1, 20}};
    mod_pattern(m, 1, ev, 1);
    mod_sample(m, 1, 1000, 0, 100, 0);
    const int prog[][3] = {
        {1, 1, 0},       /* 0: select sample 1 */
        {15, 0, 0},      /* 1: play (three ticks) */
        {9, 50, 0},      /* 2: volume 20 + 50 -> 64 */
        {8, 2, 0},       /* 3: transpose +2 */
        {13, 7, 0},      /* 4: var 7 = 0 */
        {10, 3, 0},      /* 5: loop start, var 3 = 0 */
        {12, 7, 1},      /* 6: var 7 += 1 */
        {11, 4, 1},      /* 7: var 3 += 1, back while var 3 <= 4 */
        {13, 9, 300},    /* 8: var 9 = 300 */
        {0x4003, 9, 0},  /* 9: the repeat region starts var 9 = 300 bytes into the sample */
        {2, 3, 0},       /* 10: wait 3 ticks */
        {7, 1, 0},       /* 11: slide down by 12 x k */
        {9, -100, 0},    /* 12: volume below 0 -> 0 */
        {0, 0, 0},       /* 13: stop */
    };
    mod_instrument(m, 1, 1, 255, 1, 0, 255, prog, 14);
    mod_row(m, 1, 0, 0, 0, 0);
    mod_build(m, 50);
    memset(g_v, 0, sizeof g_v);
    first_row(m);           /* tick 1 of the play */
    music_tick(&g_mu, g_v); /* tick 2 */
    music_tick(&g_mu, g_v); /* tick 3: then commands 2-10 */
    MusicVoice *v = &g_mu.v[0];
    CHECK(v->volume == 64);
    CHECK(v->pitch == 36 - 5 + 2);
    CHECK(v->var[7] == 5); /* the loop ran for var 3 = 0..4 */
    CHECK(v->var[3] == 5);
    CHECK(g_v[0].rep == g_v[0].data + 300 && g_v[0].rep_len == 100);
    uint32_t f = v->freq;
    CHECK(f == music_freq(33));
    music_tick(&g_mu, g_v);
    music_tick(&g_mu, g_v);
    CHECK(v->freq == f); /* still waiting */
    music_tick(&g_mu, g_v); /* the third tick: slide and volume */
    int k = f >= 0x7148 ? 16 : f >= 0x55F0 ? 8 : f >= 0x38A4 ? 4 : 1;
    CHECK(v->freq == f - 12u * (uint32_t)k);
    CHECK(v->volume == 0);
    CHECK(g_v[0].row == 0);
    CHECK(v->pc == 13);
    music_tick(&g_mu, g_v);
    CHECK(v->pc == 13); /* command 0 stays */
}

/* Slide factors by frequency band, and transposing below pitch 0 silences the voice. */
static void test_slide_and_silence(void) {
    Mod *m = &g_mod;
    mod_init(m);
    rest_pattern(m);
    const uint8_t ev[1][4] = {{0x06, 0, 1, 40}}; /* pitch 6 - 5 = 1 */
    mod_pattern(m, 1, ev, 1);
    mod_sample(m, 1, 100, 0, 0, 0);
    const int prog[][3] = {{1, 1, 0}, {15, 0, 0}, {8, -2, 0}, {0, 0, 0}};
    mod_instrument(m, 1, 1, 255, 1, 0, 255, prog, 4);
    mod_row(m, 1, 0, 0, 0, 0);
    mod_build(m, 50);
    memset(g_v, 0, sizeof g_v);
    first_row(m);
    CHECK(g_mu.v[0].pitch == 1 && !g_mu.v[0].silenced);
    for (int t = 0; t < 2; t++) music_tick(&g_mu, g_v);
    CHECK(g_mu.v[0].silenced && !g_v[0].active && g_v[0].row == 0);
    /* the slide bands: a = 1 at four frequencies (a wait first, so the frequency can be set before the slide) */
    const uint32_t fr[4] = {0x7148, 0x55F0, 0x38A4, 0x38A3};
    const uint32_t step[4] = {16 * 12, 8 * 12, 4 * 12, 12};
    for (int i = 0; i < 4; i++) {
        mod_init(m);
        rest_pattern(m);
        mod_pattern(m, 1, ev, 1);
        const int p2[][3] = {{2, 1, 0}, {7, 1, 0}, {0, 0, 0}};
        mod_instrument(m, 1, 1, 255, 1, 0, 255, p2, 3);
        mod_row(m, 1, 0, 0, 0, 0);
        mod_build(m, 50);
        first_row(m);
        g_mu.v[0].freq = fr[i];
        music_tick(&g_mu, g_v);
        CHECK(g_mu.v[0].freq == fr[i] - step[i]);
    }
}

int main(void) {
    test_freq();
    test_midi_card();
    test_parse();
    test_row_timing();
    test_durations();
    test_loop();
    test_control();
    test_program_play();
    test_note_volume();
    test_program_commands();
    test_slide_and_silence();
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] music tests passed\n");
    return 0;
}
