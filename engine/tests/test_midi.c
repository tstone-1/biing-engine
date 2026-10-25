/* The MIDI stream (docs/music.md, "MIDI output (SND-9)"): synthetic SCMF scores built here, never bytes of the game.
 * The first tests are the spec's synthetic values (item 7 of "What the engine must change (SND-9)"); the others pin
 * one rule each, and the last ones the card selection and the play call through the engine's sound driver. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "midi.h"
#include "portable.h"
#include "sound.h"

static int failures;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            failures++;                                                            \
        }                                                                          \
    } while (0)

/* ---- a score builder ------------------------------------------------------------------------------------------- */

typedef struct {
    uint8_t data[98];
    uint32_t entry[16][8][2]; /* per track: (start, pattern) */
    int nentry[16];
    uint8_t ptab[256], pdat[4096], itab[256], idat[2048];
    size_t ptab_n, pdat_n, itab_n, idat_n;
    uint8_t file[8192];
    size_t n;
} Score;

static void le16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void le32(uint8_t *p, uint32_t v) { le16(p, v & 0xFFFF), le16(p + 2, v >> 16); }

/* tempo, loop (start, end), global volume; track k on channel k, the SysEx bytes 0 */
static void score_init(Score *s, int tempo, uint32_t loop_start, uint32_t loop_end, int volume) {
    memset(s, 0, sizeof *s);
    le16(s->data, (unsigned)tempo);
    for (int k = 0; k < 16; k++) s->data[10 + k] = (uint8_t)k;
    le32(s->data + 0x26, loop_start);
    le32(s->data + 0x2E, loop_end);
    s->data[0x4B] = (uint8_t)volume;
    s->pdat_n = 2; /* PDAT starts with 2 unused bytes */
    le16(s->idat, 2); /* the empty 2-byte record at IDAT 0 (ITAB 0 points at it) */
    s->idat_n = 2;
    s->ptab_n = s->itab_n = 4 * 16;
}

static void score_entry(Score *s, int track, uint32_t start, int pattern) {
    int i = s->nentry[track]++;
    s->entry[track][i][0] = start;
    s->entry[track][i][1] = (uint32_t)pattern;
}

/* pattern pn: header (length, transpose, velocity offset, volume offset, mute, instrument), then the event bytes */
static void score_pattern(Score *s, int pn, int transpose, int vel, int vol, int mute, int instrument, const uint8_t *ev,
                          size_t n) {
    le32(s->ptab + 4 * pn, (uint32_t)s->pdat_n);
    uint8_t *r = s->pdat + s->pdat_n;
    size_t size = 8 + 2 + 12 + n;
    le16(r, (unsigned)size);
    le16(r + 2, 8);
    le16(r + 4, 10); /* header after the name "P\0" */
    le16(r + 6, 22);
    r[8] = 'P';
    uint8_t *h = r + 10;
    le16(h, 192);
    h[2] = (uint8_t)transpose, h[3] = (uint8_t)vel, h[4] = (uint8_t)vol, h[5] = (uint8_t)mute, h[6] = (uint8_t)instrument;
    memcpy(r + 22, ev, n);
    s->pdat_n += size;
}

/* instrument i: a block per device (program, bank, transpose, -, volume, -, pan, reverb, chorus) */
static void score_instrument(Score *s, int i, const int gm[9], const int mt[9]) {
    le32(s->itab + 4 * i, (uint32_t)s->idat_n);
    uint8_t *r = s->idat + s->idat_n;
    le16(r, 12 + 2 + 2 * 10);
    le16(r + 2, 12); /* name */
    le16(r + 4, 14); /* block 1: GM */
    le16(r + 6, 24); /* block 2: MT-32 */
    le16(r + 8, 0);
    r[12] = 'I';
    for (int k = 0; k < 9; k++) r[14 + k] = (uint8_t)gm[k], r[24 + k] = (uint8_t)mt[k];
    s->idat_n += 34;
}

static void chunk(Score *s, const char *tag, const uint8_t *d, size_t n) {
    memcpy(s->file + s->n, tag, 4);
    le32(s->file + s->n + 4, (uint32_t)n);
    if (n) memcpy(s->file + s->n + 8, d, n);
    s->n += 8 + n;
}

static const uint8_t *score_build(Score *s) {
    uint8_t trak[2048];
    size_t t = 0;
    for (int k = 0; k < 16; k++) {
        for (int i = 0; i < s->nentry[k]; i++) le32(trak + t, s->entry[k][i][0]), le32(trak + t + 4, s->entry[k][i][1]), t += 8;
        le32(trak + t, 0x2EE00), le32(trak + t + 4, 0xFFFFFFFFu), t += 8;
    }
    memcpy(s->file, "SCMF", 4);
    le32(s->file + 4, 4);
    le32(s->file + 8, 5);
    s->n = 12;
    chunk(s, "DATA", s->data, sizeof s->data);
    chunk(s, "TRAK", trak, t);
    chunk(s, "PTAB", s->ptab, s->ptab_n);
    chunk(s, "PDAT", s->pdat, s->pdat_n);
    chunk(s, "ITAB", s->itab, s->itab_n);
    chunk(s, "IDAT", s->idat, s->idat_n);
    chunk(s, "END ", (const uint8_t *)"synthetic", 9);
    return s->file;
}

/* The spec's score: tempo 120, loop end 192, global volume 100, track 0 on channel 0: pattern 1 with transpose +2,
 * velocity +10, volume -4, instrument 1 = Piano; tick 0 note 60 vel 100 for 24 ticks, tick 48 C0 2 (Lead) and note 64
 * vel 120 for 0 ticks, the end at 192. */
static const int PIANO_GM[9] = {0, 0, 0, 0, -10, 0, 0x40, 0x10, 0x20};
static const int LEAD_GM[9] = {81, 1, -12, 0, 10, 0, 0x40, 0x10, 0x20};
static const int ZERO[9] = {0};

static void spec_score(Score *s) {
    score_init(s, 120, 0, 192, 100);
    const uint8_t ev[] = {0, 60, 100, 24, 0, /* delta 0, note 60 vel 100, 24 ticks */
                          48, 0xC0, 2, /* delta 48, Lead */
                          0, 64, 120, 0, 0, /* delta 0, note 64 vel 120, 0 ticks */
                          0x81, 0x10, 0xFF}; /* delta 144 (two bytes), the end */
    score_pattern(s, 1, 2, 10, -4, 0, 1, ev, sizeof ev);
    score_instrument(s, 1, PIANO_GM, ZERO);
    score_instrument(s, 2, LEAD_GM, ZERO);
    score_entry(s, 0, 0, 1);
    score_build(s);
}

/* ---- a capturing sink ------------------------------------------------------------------------------------------- */

#define CAP 200000
static struct {
    uint32_t n[CAP];
    uint8_t b[CAP], cmd[CAP];
    int count;
} cap;

static void capture(void *ctx, uint32_t interrupt, uint8_t byte, int command) {
    (void)ctx;
    if (cap.count >= CAP) return;
    cap.n[cap.count] = interrupt, cap.b[cap.count] = byte, cap.cmd[cap.count] = (uint8_t)command;
    cap.count++;
}

/* the bytes sent in interrupt n as "B0 07 56", command bytes "!FF" */
static const char *line(uint32_t n) {
    static char out[CAP];
    size_t o = 0;
    out[0] = 0;
    for (int i = 0; i < cap.count; i++)
        if (cap.n[i] == n) o += (size_t)snprintf(out + o, sizeof out - o, "%s%s%02X", o ? " " : "", cap.cmd[i] ? "!" : "", cap.b[i]);
    return out;
}
/* every interrupt from lo to hi that sent anything, as "n" numbers separated by commas */
static const char *lines(uint32_t lo, uint32_t hi) {
    static char out[4096];
    size_t o = 0;
    out[0] = 0;
    uint32_t last = 0xFFFFFFFFu;
    for (int i = 0; i < cap.count; i++)
        if (cap.n[i] >= lo && cap.n[i] <= hi && cap.n[i] != last) {
            o += (size_t)snprintf(out + o, sizeof out - o, "%s%u", o ? "," : "", (unsigned)cap.n[i]);
            last = cap.n[i];
        }
    return out;
}

static Midi g_m;
static Midi *fresh(int device) {
    midi_init(&g_m);
    midi_add_sink(&g_m, capture, NULL);
    cap.count = 0;
    midi_begin(&g_m, device, 1);
    return &g_m;
}
static void run(Midi *m, int interrupts) {
    for (int i = 0; i < interrupts; i++) midi_interrupt(m);
}
/* one MIDI tick per counted interrupt, whatever the tempo: tick T is sent on line T + 1 */
static void ticks(Midi *m, int n) {
    for (int i = 0; i < n; i++) m->interrupts++, midi_tick(m);
}

#define SETUP "!FF !3F C3 37 B0 5D 70 B1 5D 70 B2 5D 70 B3 5D 70 B4 5D 70 B5 5D 70 B6 5D 70 B7 5D 70 B8 5D 70 " \
              "B9 5D 70 BA 5D 70 BB 5D 70 BC 5D 70 BD 5D 70 BE 5D 70 BF 5D 70"
#define OFF16 "B0 7B 00 B1 7B 00 B2 7B 00 B3 7B 00 B4 7B 00 B5 7B 00 B6 7B 00 B7 7B 00 B8 7B 00 B9 7B 00 BA 7B 00 " \
              "BB 7B 00 BC 7B 00 BD 7B 00 BE 7B 00 BF 7B 00"
#define PIANO "B0 00 00 C0 00 B0 0A 40 B0 5B 10 B0 5D 20"
#define GS_SYSEX "F0 41 10 42 12 40 01 30 00 0F F7 F0 41 10 42 12 40 01 38 00 07 F7 F0 41 10 42 12 40 00 04 00 3C F7"

/* ---- the spec's table ------------------------------------------------------------------------------------------- */

static void test_spec_gm(void) {
    Score s;
    spec_score(&s);
    Midi *m = fresh(MIDI_GM);
    CHECK(midi_song_start(m, s.file, s.n) == 0);
    run(m, 110);
    midi_song_stop(m);
    midi_end(m);
    CHECK(!strcmp(line(0), SETUP " " OFF16 " " PIANO " B0 07 56"));
    CHECK(!strcmp(line(1), GS_SYSEX " 90 3E 6E"));
    CHECK(!strcmp(line(13), "80 3E 00"));
    /* tick 48: Lead, but the note with Piano's transpose; 130 -> 127; its note-off at tick 49, the same interrupt */
    CHECK(!strcmp(line(26), "B0 00 01 C0 51 B0 0A 40 B0 5B 10 B0 5D 20 90 42 7F 80 42 00"));
    CHECK(!strcmp(line(100), PIANO)); /* tick 191: the loop to 0, Piano again, CC 7 unchanged */
    CHECK(!strcmp(line(101), "90 3E 6E"));
    CHECK(!strcmp(line(110), OFF16 " " OFF16 " !FF")); /* stop, then the program end */
    CHECK(!strcmp(lines(0, 200), "0,1,13,26,100,101,110"));
    CHECK(!m->running && !m->playing);
}

static void test_spec_mt32(void) {
    Score s;
    spec_score(&s);
    Midi *m = fresh(MIDI_MT32);
    midi_song_start(m, s.file, s.n);
    run(m, 1);
    /* block 2 is all 0: volume 0 + 100 - 4 */
    CHECK(!strcmp(line(0), SETUP " " OFF16 " B0 00 00 C0 00 B0 0A 00 B0 5B 00 B0 5D 00 B0 07 60"));
    CHECK(!strcmp(line(1), "F0 41 10 16 12 10 00 00 00 00 00 00 70 F7 F0 41 10 16 12 10 00 16 00 5A F7 90 3E 6E"));
}

/* A second start after a stop sends no CC 7 (the channel still has 0x56) and keeps the accumulator: after one
 * interrupt it holds 360 - 187 = 173, so tick 48 comes in the 25th interrupt after the start instead of the 26th. */
static void test_restart(void) {
    Score s;
    spec_score(&s);
    Midi *m = fresh(MIDI_GM);
    midi_song_start(m, s.file, s.n);
    run(m, 1);
    CHECK(m->acc == 173);
    midi_song_stop(m);
    midi_song_start(m, s.file, s.n);
    CHECK(!strcmp(line(1), GS_SYSEX " 90 3E 6E " OFF16 " " OFF16 " " PIANO)); /* stop, start: no CC 7 */
    run(m, 30);
    CHECK(!strcmp(line(2), GS_SYSEX " 90 3E 6E"));
    CHECK(!strcmp(line(14), "80 3E 00")); /* tick 24: (4,675 - 173) / 360 -> the 13th */
    /* the 25th (173 + 25 x 360 = 9,173 >= 187 x 49); tick 49 now needs 9,350: its note-off one interrupt later */
    CHECK(!strcmp(line(26), "B0 00 01 C0 51 B0 0A 40 B0 5B 10 B0 5D 20 90 42 7F"));
    CHECK(!strcmp(line(27), "80 42 00"));
    CHECK(!strcmp(lines(2, 31), "2,14,26,27"));
    /* a new program run starts from 0: the first start sends the CC 7 again */
    midi_end(m);
    cap.count = 0;
    midi_begin(m, MIDI_GM, 1);
    midi_song_start(m, s.file, s.n);
    CHECK(!strcmp(line(0), SETUP " " OFF16 " " PIANO " B0 07 56"));
    CHECK(m->acc == 0);
}

/* The channel's instrument is kept across songs: a pattern with instrument 0 keeps it, and its transpose and volume
 * come from that index in the new song's tables. */
static void test_carry_instrument(void) {
    Score a, b;
    spec_score(&a);
    score_init(&b, 120, 0, 192, 100);
    const uint8_t ev[] = {0, 60, 100, 1, 0, 0x81, 0x10, 0xFF};
    score_pattern(&b, 1, 0, 0, 0, 0, 0, ev, sizeof ev); /* instrument 0: keep the channel's */
    const int other[9] = {5, 0, 5, 0, -50, 0, 1, 2, 3};
    score_instrument(&b, 1, PIANO_GM, ZERO);
    score_instrument(&b, 2, other, ZERO);
    score_entry(&b, 0, 0, 1);
    score_build(&b);
    Midi *m = fresh(MIDI_GM);
    midi_song_start(m, a.file, a.n);
    run(m, 30); /* past tick 48: the channel has Lead (2) */
    CHECK(m->instrument[0] == 2);
    midi_song_stop(m);
    cap.count = 0;
    midi_song_start(m, b.file, b.n);
    run(m, 1);
    /* no program change; CC 7 = -50 + 100 + 0 = 50; the note with instrument 2's transpose +5 of song b */
    CHECK(!strcmp(line(30), OFF16 " B0 07 32"));
    /* the accumulator's 141 left from song a: two ticks in the first interrupt */
    CHECK(!strcmp(line(31), GS_SYSEX " 90 41 64 80 41 00"));
}

/* ---- one rule each ---------------------------------------------------------------------------------------------- */

/* tempo 63: 189 per interrupt, so MIDI tick T is sent in interrupt T + 1 (up to T = 93) */
static void one_pattern(Score *s, int channel, int transpose, int vel, int mute, const uint8_t *ev, size_t n) {
    score_init(s, 63, 0, 1000, 100);
    s->data[10] = (uint8_t)channel;
    score_pattern(s, 1, transpose, vel, 0, mute, 1, ev, n);
    score_instrument(s, 1, PIANO_GM, PIANO_GM);
    score_entry(s, 0, 0, 1);
    score_build(s);
}

static void test_velocity(void) {
    Score s;
    const uint8_t ev[] = {0, 60, 10, 1, 0, 0, 61, 0x7F, 1, 0, 100, 0xFF};
    one_pattern(&s, 0, 0, -20, 0, ev, sizeof ev);
    Midi *m = fresh(MIDI_GM);
    midi_song_start(m, s.file, s.n);
    run(m, 1);
    CHECK(!strcmp(line(1), GS_SYSEX " 90 3C 00 90 3D 6B")); /* 10 - 20 wraps: 0 (negative offset); 127 - 20 = 107 */
    one_pattern(&s, 0, 0, 1, 0, ev, sizeof ev);
    m = fresh(MIDI_GM);
    midi_song_start(m, s.file, s.n);
    run(m, 1);
    CHECK(!strcmp(line(1), GS_SYSEX " 90 3C 0B 90 3D 7F")); /* 127 + 1 = 128: 127 */
}

static void test_transpose(void) {
    Score s;
    const uint8_t ev[] = {0, 60, 100, 1, 0, 100, 0xFF};
    const int piano_t[9] = {0, 0, -12, 0, 0, 0, 0x40, 0, 0};
    for (int ch = 0; ch < 2; ch++) {
        score_init(&s, 63, 0, 1000, 100);
        s.data[10] = 3; /* track 0 on channel 3, track 10 on channel 9 */
        s.data[20] = 9;
        s.data[0x4A] = 3; /* the global transpose */
        score_pattern(&s, 1, 2, 0, 0, 0, 1, ev, sizeof ev);
        score_instrument(&s, 1, piano_t, piano_t);
        score_entry(&s, ch ? 10 : 0, 0, 1); /* a list after the 10th -1 of TRAK */
        score_build(&s);
        Midi *m = fresh(MIDI_GM);
        midi_song_start(m, s.file, s.n);
        run(m, 2);
        if (ch) CHECK(!strcmp(line(1), GS_SYSEX " 99 3C 64") && !strcmp(line(2), "89 3C 00")); /* drums: as written */
        else CHECK(!strcmp(line(1), GS_SYSEX " 93 35 64") && !strcmp(line(2), "83 35 00")); /* 60 + 2 + 3 - 12 */
    }
    /* an 8-bit sum, not clamped: 0x7E + 2 + 3 = 0x83 */
    const uint8_t hi[] = {0, 0x7E, 100, 1, 0, 100, 0xFF};
    score_init(&s, 63, 0, 1000, 100);
    s.data[0x4A] = 3;
    score_pattern(&s, 1, 2, 0, 0, 0, 1, hi, sizeof hi);
    score_instrument(&s, 1, PIANO_GM, PIANO_GM);
    score_entry(&s, 0, 0, 1);
    score_build(&s);
    Midi *m = fresh(MIDI_GM);
    midi_song_start(m, s.file, s.n);
    run(m, 1);
    CHECK(!strcmp(line(1), GS_SYSEX " 90 83 64"));
}

static void test_mute(void) {
    static const struct {
        int mute, device;
        int sounds;
    } C[] = {{0x01, MIDI_GM, 0}, {0x01, MIDI_MT32, 1}, {0x02, MIDI_GM, 1}, {0x02, MIDI_MT32, 0},
             {0x80, MIDI_GM, 0}, {0x80, MIDI_MT32, 0}, {0x04, MIDI_GM, 1}, {0x00, MIDI_MT32, 1}};
    const uint8_t ev[] = {0, 60, 100, 1, 0, 100, 0xFF};
    for (size_t i = 0; i < sizeof C / sizeof *C; i++) {
        Score s;
        one_pattern(&s, 0, 0, 0, C[i].mute, ev, sizeof ev);
        Midi *m = fresh(C[i].device);
        midi_song_start(m, s.file, s.n);
        run(m, 3);
        const char *l1 = line(1);
        CHECK((strstr(l1, "90 3C 64") != NULL) == C[i].sounds);
        CHECK((strstr(line(2), "80 3C 00") != NULL) == C[i].sounds); /* a muted note gets no note-off either */
    }
}

/* The SysEx checksum where address and data add up to a multiple of 128, and the CC 7 clamps: above 127 sent as 127,
 * below 0 as 0, which equals a channel's start value 0 and so is not sent. */
static void test_sysex_volume(void) {
    for (int dev = 0; dev < 2; dev++) {
        Score s;
        score_init(&s, 63, 0, 1000, 100);
        s.data[0x5B] = 0x0F; /* 40 + 01 + 30 + 0F = 0x80 */
        s.data[0x5D] = 0x40, s.data[0x60] = 0x30; /* 10 + 40 + 30 = 0x80 */
        const uint8_t ev[] = {100, 0xFF}; /* a delta, the end */
        const int loud[9] = {0, 0, 0, 0, 40, 0, 0, 0, 0}, quiet[9] = {0, 0, 0, 0, -120, 0, 0, 0, 0};
        score_pattern(&s, 1, 0, 0, 0, 0, 1, ev, sizeof ev);
        score_pattern(&s, 2, 0, 0, 0, 0, 2, ev, sizeof ev);
        score_instrument(&s, 1, loud, loud);
        score_instrument(&s, 2, quiet, quiet);
        score_entry(&s, 0, 0, 1);
        score_entry(&s, 1, 0, 2);
        score_build(&s);
        Midi *m = fresh(dev);
        midi_song_start(m, s.file, s.n);
        run(m, 1);
        CHECK(!strcmp(line(0), SETUP " " OFF16 " B0 00 00 C0 00 B0 0A 00 B0 5B 00 B0 5D 00 B0 07 7F "
                               "B1 00 00 C1 00 B1 0A 00 B1 5B 00 B1 5D 00"));
        if (dev == MIDI_GM)
            CHECK(!strcmp(line(1), "F0 41 10 42 12 40 01 30 0F 00 F7 F0 41 10 42 12 40 01 38 00 07 F7 "
                                   "F0 41 10 42 12 40 00 04 00 3C F7"));
        else CHECK(!strcmp(line(1), "F0 41 10 16 12 10 00 00 40 00 00 30 00 F7 F0 41 10 16 12 10 00 16 00 5A F7"));
    }
}

/* B0 and E0 events (3 bytes) and 00 (2 bytes) are read and never sent; the deltas still count */
static void test_skipped_events(void) {
    Score s;
    const uint8_t ev[] = {0, 0xB0, 7, 100, 2, 0xE0, 0, 64, 3, 0x00, 5, 1, 60, 100, 1, 0, 100, 0xFF};
    one_pattern(&s, 0, 0, 0, 0, ev, sizeof ev);
    Midi *m = fresh(MIDI_GM);
    midi_song_start(m, s.file, s.n);
    run(m, 10);
    CHECK(!strcmp(lines(1, 10), "1,7,8"));
    CHECK(!strcmp(line(1), GS_SYSEX));
    CHECK(!strcmp(line(7), "90 3C 64")); /* tick 0 + 2 + 3 + 1 = 6 */
    CHECK(!strcmp(line(8), "80 3C 00"));
}

/* Note-offs in slot order; a freed slot among the used ones is filled first. */
static void test_noteoff_order(void) {
    Score s;
    /* tick 0: A (60) for 1 tick, B (62) for 3; tick 1: C (64) for 2 -> into A's freed slot; tick 3: C then B */
    const uint8_t ev[] = {0, 60, 100, 1, 0, 0, 62, 100, 3, 0, 1, 64, 100, 2, 0, 100, 0xFF};
    one_pattern(&s, 0, 0, 0, 0, ev, sizeof ev);
    Midi *m = fresh(MIDI_GM);
    midi_song_start(m, s.file, s.n);
    run(m, 6);
    CHECK(!strcmp(line(1), GS_SYSEX " 90 3C 64 90 3E 64"));
    CHECK(!strcmp(line(2), "80 3C 00 90 40 64")); /* the note-off before the tick's notes */
    CHECK(!strcmp(line(4), "80 40 00 80 3E 00"));
    CHECK(!strcmp(lines(1, 6), "1,2,4"));
    CHECK(m->noff == 0);
    /* a note of 0 ticks ends in the next tick, like 1 */
    const uint8_t z[] = {0, 60, 100, 0, 0, 100, 0xFF};
    one_pattern(&s, 0, 0, 0, 0, z, sizeof z);
    m = fresh(MIDI_GM);
    midi_song_start(m, s.file, s.n);
    run(m, 3);
    CHECK(!strcmp(line(2), "80 3C 00"));
}

/* The loop positions every track at the loop start: events before it skipped unsent, the wait to the next one, the
 * running pattern's program change and volume sent; nothing silenced (the running note ends by its note-off). */
static void test_loop_position(void) {
    Score s;
    score_init(&s, 63, 24, 40, 100);
    const uint8_t ev[] = {0, 60, 100, 30, 0, 20, 0xC0, 2, 10, 62, 100, 1, 0, 100, 0xFF};
    score_pattern(&s, 1, 0, 0, 0, 0, 1, ev, sizeof ev);
    score_instrument(&s, 1, PIANO_GM, PIANO_GM);
    score_instrument(&s, 2, LEAD_GM, LEAD_GM);
    score_entry(&s, 0, 0, 1);
    score_build(&s);
    Midi *m = fresh(MIDI_GM);
    midi_song_start(m, s.file, s.n);
    run(m, 60);
    /* tick 20: Lead; tick 30: the note; tick 39 -> the loop to 24: Piano again (the skipped C0 not sent), no CC 7 */
    CHECK(!strcmp(line(21), "B0 00 01 C0 51 B0 0A 40 B0 5B 10 B0 5D 20"));
    /* tick 30: 60 ends before the tick's notes; 62 with Lead's transpose -12 (the C0 came in an earlier step) */
    CHECK(!strcmp(line(31), "80 3C 00 90 32 64"));
    CHECK(!strcmp(line(32), "80 32 00"));
    CHECK(!strcmp(line(40), PIANO));
    /* the loop's tick 24 is interrupt 41; the skipped events leave a wait of 6: the note at tick 30 again, with Piano */
    CHECK(!strcmp(line(47), "90 3E 64"));
    CHECK(!strcmp(line(48), "80 3E 00"));
    CHECK(!strcmp(lines(33, 60), "40,47,48,56"));
    CHECK(!strcmp(line(56), PIANO));
}

/* An entry that ends (FF) moves to the next entry in the same tick, which starts now when it is listed at this tick */
static void test_next_entry(void) {
    Score s;
    score_init(&s, 63, 0, 1000, 100);
    const uint8_t a[] = {0, 60, 100, 1, 0, 5, 0xFF};
    const uint8_t b[] = {0, 62, 100, 1, 0, 100, 0xFF};
    score_pattern(&s, 1, 0, 0, 0, 0, 1, a, sizeof a);
    score_pattern(&s, 2, 0, 0, 0, 0, 0, b, sizeof b);
    score_instrument(&s, 1, PIANO_GM, PIANO_GM);
    score_entry(&s, 0, 0, 1);
    score_entry(&s, 0, 5, 2);
    score_entry(&s, 0, 3, 1); /* after an entry that ends later than its start: never started */
    score_build(&s);
    Midi *m = fresh(MIDI_GM);
    midi_song_start(m, s.file, s.n);
    ticks(m, 200);
    CHECK(!strcmp(line(6), "90 3E 64")); /* tick 5: pattern 1 ends, pattern 2 starts at once */
    CHECK(!strcmp(lines(3, 200), "6,7")); /* pattern 2 ends at tick 105: the entry at 3 does not start */
    /* a two-byte delta: (0x81 & 0x7F) << 7 + 0x02 = 130 */
    const uint8_t c[] = {0, 60, 100, 1, 0, 0x81, 0x02, 62, 100, 1, 0, 100, 0xFF};
    one_pattern(&s, 0, 0, 0, 0, c, sizeof c);
    m = fresh(MIDI_GM);
    midi_song_start(m, s.file, s.n);
    ticks(m, 300);
    CHECK(!strcmp(lines(3, 300), "131,132"));
}

/* The MIDI ticks: 3 x tempo per interrupt, one per 187, the remainder kept */
static void test_accumulator(void) {
    Score s;
    spec_score(&s);
    s.data[0] = 62, s.data[1] = 0; /* 186 per interrupt: one interrupt without a tick */
    score_build(&s);
    Midi *m = fresh(MIDI_GM);
    midi_song_start(m, s.file, s.n);
    run(m, 1);
    CHECK(m->tick == 0 && m->acc == 186);
    run(m, 1);
    CHECK(m->tick == 1 && m->acc == 185);
    /* 3 x 187 = 561: exactly three ticks (each pass needs at least 187, 187 included) */
    s.data[0] = 187;
    score_build(&s);
    midi_song_start(m, s.file, s.n);
    m->acc = 0;
    run(m, 1);
    CHECK(m->tick == 3 && m->acc == 0);
    /* no song: no tick and no accumulation, but the interrupts count */
    midi_song_stop(m);
    uint32_t n = m->interrupts;
    run(m, 5);
    CHECK(m->acc == 0 && m->interrupts == n + 5);
    /* RELO: no counting before the song (its timer starts with the music) */
    midi_end(m);
    midi_begin(m, MIDI_GM, 0);
    run(m, 7);
    CHECK(m->interrupts == 0);
    midi_song_start(m, s.file, s.n);
    run(m, 2);
    CHECK(m->interrupts == 2);
    /* the site-map song's halt: no byte, no more ticks */
    int sent = (int)m->sent;
    midi_halt(m);
    run(m, 50);
    CHECK((int)m->sent == sent && !m->playing);
}

static void test_reject(void) {
    Score s;
    spec_score(&s);
    Midi *m = fresh(MIDI_GM);
    int before = cap.count;
    s.file[12] = 'X'; /* DATA tag broken */
    CHECK(midi_song_start(m, s.file, s.n) == -1 && cap.count == before && !m->playing);
    CHECK(midi_score_tempo(s.file, s.n) == -1);
    spec_score(&s);
    CHECK(midi_score_tempo(s.file, s.n) == 120);
    CHECK(midi_song_start(m, s.file, 40) == -1);
}

/* ---- receivers ---------------------------------------------------------------------------------------------- */

static uint8_t msgs[16][32];
static int msg_len[16], nmsg;
static void on_message(void *ctx, const uint8_t *msg, int len) {
    (void)ctx;
    if (nmsg < 16 && len <= 32) memcpy(msgs[nmsg], msg, (size_t)len), msg_len[nmsg++] = len;
}

static void test_receivers(void) {
    MidiAssembler a = {on_message, NULL, {0}, 0, 0};
    nmsg = 0;
    const uint8_t in[] = {0xFF, 0x3F, 0xC3, 0x37, 0xB0, 0x07, 0x56, 0xF0, 0x41, 0x10, 0xF7, 0x90, 0x3C, 0x64, 0x12};
    const int cmd[] = {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (size_t i = 0; i < sizeof in; i++) midi_assembler_byte(&a, 0, in[i], cmd[i]);
    CHECK(nmsg == 4);
    CHECK(msg_len[0] == 2 && msgs[0][0] == 0xC3 && msgs[0][1] == 0x37);
    CHECK(msg_len[1] == 3 && msgs[1][2] == 0x56);
    CHECK(msg_len[2] == 4 && msgs[2][0] == 0xF0 && msgs[2][3] == 0xF7);
    CHECK(msg_len[3] == 3 && msgs[3][0] == 0x90);

    const char *path = "test_midi_log.txt";
    MidiLog l;
    CHECK(midi_log_open(&l, path, 0, "engine test") == 0);
    midi_log_byte(&l, 0, 0xFF, 1);
    midi_log_byte(&l, 0, 0xC3, 0);
    midi_log_byte(&l, 3, 0x80, 0);
    midi_log_close(&l);
    FILE *f = fopen(path, "rb");
    char text[128] = {0};
    if (f) fread(text, 1, sizeof text - 1, f), fclose(f);
    CHECK(!strcmp(text, "# engine test\n0: !FF C3\n3: 80\n"));
    remove(path);
}

/* ---- the card selection and the play call -------------------------------------------------------------------- */

#define DIR_ "test_midi_data"
static uint8_t g_image[0x10000];

static void write_file(const char *rel, const uint8_t *d, size_t n) {
    char path[256];
    snprintf(path, sizeof path, DIR_ "/%s", rel);
    for (char *p = path + strlen(DIR_) + 1; *p; p++)
        if (*p == '/') *p = 0, test_mkdir(path), *p = '/';
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[FAIL] cannot write %s\n", path);
        failures++;
        return;
    }
    fwrite(d, 1, n, f);
    fclose(f);
}

static void make_data(const char *mcard) {
    test_mkdir(DIR_);
    uint8_t mod[0x40 + 512 + 1088 + 12 + 512]; /* a minimal module that parses (id 175 = T/7/5, 222 = U/2/2) */
    memset(mod, 0, sizeof mod);
    memcpy(mod, "BONGTEST        ", 16);
    mod[0x12] = 512 >> 8, mod[0x16] = 1088 >> 8, mod[0x17] = 1088 & 255, mod[0x1B] = 12, mod[0x1E] = 512 >> 8;
    mod[0x23] = 120;
    mod[0x40 + 512 + 2] = 0x04;
    write_file("DATA/T/7/5", mod, sizeof mod);
    write_file("DATA/U/2/2", mod, sizeof mod);
    Score s;
    spec_score(&s);
    write_file("DATA/M/TEST.SCT", s.file, s.n);
    const char on[] = "SOUND     -JA   | x\r\n";
    write_file("BIING!.CFG", (const uint8_t *)on, sizeof on - 1);
    char snd[128];
    snprintf(snd, sizeof snd, "DCARD     -SB   | x\r\nMCARD     -%s  | x\r\n", mcard);
    write_file("SOUND.CFG", (const uint8_t *)snd, strlen(snd));
    /* the music table at 0xB97E: (module file, score path pointer, CD track) entries, ended by -1 */
    memset(g_image, 0, sizeof g_image);
    le32(g_image + 0xB97E, 2975), le32(g_image + 0xB982, 0x9000), le32(g_image + 0xB986, 8);
    le32(g_image + 0xB98A, 3022), le32(g_image + 0xB98E, 0x9020), le32(g_image + 0xB992, 5);
    le32(g_image + 0xB996, 0xFFFFFFFFu);
    memcpy(g_image + 0x9000, "data\\m\\test.sct", 16);
    memcpy(g_image + 0x9020, "DATA\\M\\test.sct", 16);
}

static Sound g_s;
static Sound *sound(const char *mcard) {
    sound_free(&g_s);
    make_data(mcard);
    static Program prog;
    prog.image.data = g_image, prog.image.size = sizeof g_image;
    sound_init(&g_s, DIR_, &prog, NULL, NULL);
    midi_add_sink(&g_s.midi, capture, NULL);
    cap.count = 0;
    return &g_s;
}

static void test_card(void) {
    CHECK(sound_midi_device(7) == MIDI_GM && sound_midi_device(6) == MIDI_MT32);
    CHECK(sound_midi_device(1) == -1 && sound_midi_device(3) == -1 && sound_midi_device(8) == -1 &&
          sound_midi_device(0) == -1);
    const char gm[] = "MCARD     -GM   | x\r\n", rol[] = "MCARD     -ROL  | x\r\n";
    CHECK(sound_cfg_card(gm, sizeof gm - 1, "MCARD", 1) == 7 && sound_cfg_card(rol, sizeof rol - 1, "MCARD", 1) == 6);

    Sound *s = sound("GM");
    CHECK(sound_midi_device(s->mcard) == MIDI_GM);
    char path[64];
    CHECK(sound_song_score(s, 175, path, sizeof path) == 0 && !strcmp(path, "DATA/M/TEST.SCT"));
    CHECK(sound_song_score(s, 176, path, sizeof path) == -1);
    sound_midi_program(s, SOUND_PROG_BIPRO);
    CHECK(!strcmp(line(0), SETUP));
    sound_play(s, 175, 2, 0x140, 0, 1); /* the background slot's play call: the song with its score */
    CHECK(!strcmp(line(0), SETUP " " OFF16 " " PIANO " B0 07 56"));
    CHECK(s->music.playing && s->music.midi_card && !s->music.only_n);
    for (int i = 0; i < 26; i++) sound_interrupt(s);
    CHECK(!strcmp(line(1), GS_SYSEX " 90 3E 6E"));
    for (int k = 0; k < MIXER_VOICES; k++) CHECK(!s->voice[k].active); /* no module note */
    /* a non-song background with the song running: the device stop, all four voices */
    cap.count = 0;
    sound_play(s, 4, 2, 0x140, 0, 1); /* id 4 has no file: the stop still happens */
    CHECK(!strcmp(line(26), OFF16) && !s->midi.playing);
    /* the site-map song requested twice: the second request halts it with no byte (the notes hang) */
    sound_play(s, 0xDE, 2, 0x140, 0, 1);
    CHECK(!strcmp(line(26), OFF16 " " OFF16 " " PIANO)); /* no stop (nothing ran), the start, no CC 7 */
    for (int i = 0; i < 3; i++) sound_interrupt(s);
    int before = cap.count;
    sound_play(s, 0xDE, 2, 0x140, 0, 1);
    for (int i = 0; i < 30; i++) sound_interrupt(s);
    CHECK(cap.count == before && !s->midi.playing && !s->music.playing);
    /* another song: no device stop (no module runs), its own start */
    s->midi_score = "DATA/M/TEST.SCT";
    sound_play(s, 175, 2, 0x140, 0, 1);
    CHECK(!strcmp(line(59), OFF16 " " PIANO));
    sound_free(s); /* the program end: song stop and shut-down */
    CHECK(!strcmp(line(59), OFF16 " " PIANO " " OFF16 " " OFF16 " !FF"));

    s = sound("ROL ");
    CHECK(sound_midi_device(s->mcard) == MIDI_MT32);
    sound_midi_program(s, SOUND_PROG_BIPRO);
    sound_play(s, 175, 2, 0x140, 0, 1);
    sound_interrupt(s);
    CHECK(!strncmp(line(1), "F0 41 10 16 12", 14));

    s = sound("SB  ");
    CHECK(sound_midi_device(s->mcard) == -1);
    sound_midi_program(s, SOUND_PROG_BIPRO);
    sound_play(s, 175, 2, 0x140, 0, 1);
    for (int i = 0; i < 30; i++) sound_interrupt(s);
    CHECK(cap.count == 0 && !s->midi_prog && s->music.playing && !s->music.midi_card); /* the module plays */

    /* a second program's start ends the running one first (stop and end), and starts its own run from 0 */
    s = sound("GM");
    sound_midi_program(s, SOUND_PROG_BIPRO);
    sound_interrupt(s);
    sound_midi_program(s, SOUND_PROG_RELO);
    CHECK(s->midi_prog == SOUND_PROG_RELO && !s->midi.timer && s->midi.interrupts == 0);
    CHECK(!strcmp(line(0), SETUP " " SETUP)); /* RELO's run counts from 0 */
    CHECK(!strcmp(line(1), OFF16 " " OFF16 " !FF")); /* BIPRO's end after its first interrupt */
    sound_free(s);
}

int main(void) {
    test_spec_gm();
    test_spec_mt32();
    test_restart();
    test_carry_instrument();
    test_velocity();
    test_transpose();
    test_mute();
    test_sysex_volume();
    test_skipped_events();
    test_noteoff_order();
    test_loop_position();
    test_next_entry();
    test_accumulator();
    test_reject();
    test_receivers();
    test_card();
    sound_free(&g_s);
    test_rmtree(DIR_);
    if (failures) {
        fprintf(stderr, "[FAIL] test_midi: %d failures\n", failures);
        return 1;
    }
    printf("[OK] test_midi\n");
    return 0;
}
