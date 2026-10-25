/* Sound effects (docs/sound.md): loader, rate and volume rules, mixer, queues and their draining, the busy
 * sequences, the background slot, ambience and the off switches. Synthetic sample files (written into the working
 * directory, the build tree under ctest) and a synthetic program image with made-up lists at the spec's offsets. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "portable.h"
#include "sim.h"
#include "sound.h"

static int failures;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            failures++;                                                            \
        }                                                                          \
    } while (0)

#define DIR_ "test_sound_data"

/* A scripted random source: returns the queued values in order, then 0. */
static uint32_t rq[64];
static int rq_n, rq_i;
static uint32_t scripted(void *ctx, uint32_t n) {
    (void)ctx;
    uint32_t v = rq_i < rq_n ? rq[rq_i++] : 0;
    return n ? v % n : 0;
}
static void script(int n, const uint32_t *v) {
    memcpy(rq, v, sizeof *v * (size_t)n);
    rq_n = n;
    rq_i = 0;
}

static uint8_t g_image[0x10000];
static Sound g_s;

static void put_entry(uint32_t off, int id, int period, int vol) {
    uint8_t *p = g_image + off;
    p[0] = (uint8_t)id, p[1] = (uint8_t)(id >> 8), p[2] = (uint8_t)period, p[3] = (uint8_t)(period >> 8);
    p[4] = (uint8_t)vol, p[5] = (uint8_t)(vol >> 8);
}

static void write_file(const char *rel, const uint8_t *d, size_t n) {
    char path[256];
    snprintf(path, sizeof path, DIR_ "/%s", rel);
    /* make the directories */
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

static void make_data(void) {
    test_mkdir(DIR_);
    uint8_t buf[600];
    for (int i = 0; i < 600; i++) buf[i] = (uint8_t)(i & 1 ? 0x10 : 0xF0); /* +16 / -16 */
    write_file("DATA/S/0/1", buf, 5); /* id 1: odd size, plays 4 bytes */
    write_file("DATA/S/0/2", buf, 600); /* id 2 */
    write_file("DATA/S/0/3", buf, 8); /* id 3 */
    write_file("DATA/U/0/1", buf, 64); /* id 201 */
    remove(DIR_ "/DATA/U/2/2"); /* id 222 = 0xDE, the site-map song: kept missing (an older run wrote it) */
    write_file("DATA/S/5/2", buf, 100); /* id 52 */
    write_file("DATA/S/6/0", buf, 100); /* id 60 */
    /* id 175 (T/7/5): a music bank with a minimal module that parses */
    uint8_t mod[0x40 + 512 + 1088 + 12 + 512];
    memset(mod, 0, sizeof mod);
    memcpy(mod, "BONGTEST        ", 16);
    mod[0x12] = 512 >> 8, mod[0x16] = 1088 >> 8, mod[0x17] = 1088 & 255, mod[0x1B] = 12, mod[0x1E] = 512 >> 8;
    mod[0x23] = 120;
    uint8_t *smp = mod + 0x40 + 512;
    smp[2] = 0x04; /* sample table entry 0 = 0x400 */
    write_file("DATA/T/7/5", mod, sizeof mod);
    const char cfg_on[] = "SOUND     -JA   | x\r\n";
    write_file("BIING!.CFG", (const uint8_t *)cfg_on, sizeof cfg_on - 1);
    const char snd[] = "DCARD     -SB   | x\r\nMCARD     -SB   | x\r\n";
    write_file("SOUND.CFG", (const uint8_t *)snd, sizeof snd - 1);
}

static Sound *fresh(void) {
    sound_free(&g_s);
    static Program prog; /* a synthetic image laid out like the disk build */
    prog.image.data = g_image, prog.image.size = sizeof g_image;
    sound_init(&g_s, DIR_, &prog, scripted, NULL);
    rq_n = rq_i = 0;
    return &g_s;
}

/* Lets every voice run to its end (one-shots finish). */
static void finish(Sound *s) {
    for (int t = 0; t < 200; t++) sound_interrupt(s);
}

/* ---- loader, rate, rows -------------------------------------------------------------------- */

static void test_loader(void) {
    Sound *s = fresh();
    CHECK(sound_load(s, 1) == 1);
    CHECK(s->file_len[1] == 5 && s->sample_len[1] == 4); /* 16-bit words: the odd byte is lost */
    CHECK(s->sample[1][0] == -16 && s->sample[1][1] == 16); /* signed */
    CHECK(sound_load(s, 2) == 1 && s->sample_len[2] == 600);
    CHECK(sound_load(s, 201) == 1 && s->sample_len[201] == 64); /* U/0/1 */
    CHECK(sound_load(s, 175) == 2); /* BONG: a music bank */
    CHECK(sound_load(s, 4) == -1); /* missing */
    CHECK(sound_load(s, 0) == -1 && sound_load(s, 256) == -1);
    CHECK(sound_load(s, 4) == -1); /* remembered */
}

static void test_rate(void) {
    CHECK(sound_step(0x140) == 0x802C); /* the RAM dump's step for period 0x140 */
    double r = sound_rate(0x140);
    CHECK(r > 10869 && r < 10870.5);
    const int period[6] = {0xBE, 0xDC, 0xFA, 0x118, 0x1F4, 0x294};
    const int hz[6] = {18257, 15795, 13927, 12398, 6963, 5265}; /* docs/sound.md, the period table */
    for (int i = 0; i < 6; i++) {
        double x = sound_rate(period[i]);
        CHECK(x > hz[i] - 1 && x < hz[i] + 1);
    }
    CHECK(sound_step(0) == 0);
}

static void test_rows(void) {
    CHECK(sound_row(0) == 7);
    CHECK(sound_row(8) == 13);
    CHECK(sound_row(63) == 54);
    CHECK(sound_row(64) == 54 && sound_row(500) == 54); /* clamped to 0-63 */
    CHECK(sound_row(-3) == 7);
    CHECK(sound_row(10) == 14 && sound_row(11) == 15);
    CHECK(mixer_volume(127, 54, 0) == 27); /* floor(255 x 55 / 256) - 27 = 54 - 27 */
    CHECK(mixer_volume(-128, 54, 0) == -27);
    CHECK(mixer_volume(-1, 7, 0) == -1); /* floor(127 x 8 / 256) - 4 = 3 - 4 */
    CHECK(mixer_volume(0, 7, 0) == 0 && mixer_volume(1, 7, 0) == 0); /* floor(1032 / 256) - 4 */
    CHECK(mixer_volume(127, 54, 1) == 54); /* stereo: floor(255 x 109 / 256) - 54 = 108 - 54 */
    CHECK(mixer_volume(-128, 54, 1) == -54);
    CHECK(mixer_volume(0, 64, 0) == 0);
    /* the whole table against the formulas of docs/sound.md, "Volume" (rows 0-63, samples -128..127), computed here
     * in floating point: mono floor((s + 128)(r + 1) / 256) - ceil(r / 2), stereo floor((s + 128)(2r + 1) / 256) - r */
    int bad_mono = 0, bad_stereo = 0;
    for (int r = 0; r < 64; r++)
        for (int x = -128; x < 128; x++) {
            int mono = (int)floor((x + 128) * (r + 1) / 256.0) - (int)ceil(r / 2.0);
            int stereo = (int)floor((x + 128) * (2 * r + 1) / 256.0) - r;
            bad_mono += mixer_volume(x, r, 0) != mono;
            bad_stereo += mixer_volume(x, r, 1) != stereo;
        }
    CHECK(bad_mono == 0 && bad_stereo == 0);
}

/* ---- mixer --------------------------------------------------------------------------------- */

static void test_mixer(void) {
    static const int8_t a[4] = {100, 100, 100, 100}, b[2] = {-60, -60};
    Voice v[4];
    memset(v, 0, sizeof v);
    for (int k = 0; k < 4; k++) v[k].idle = 1;
    voice_start(&v[0], a, 4, 0);
    v[0].step = sound_step(0x140); /* 0x802C >> 8 = 0x80: half a source sample per output sample */
    v[0].row = 64;
    int16_t out[2 * 20];
    mixer_render(v, 0, out, 10);
    CHECK(out[0] == 25 * 256 && out[1] == 25 * 256); /* 100 x 64 / 256 */
    CHECK(out[2 * 7] == 25 * 256);
    CHECK(out[2 * 8] == 0); /* 4 source bytes at a half step: 8 output samples, then silence */
    CHECK(!v[0].active && v[0].idle);
    /* two voices summed in mono; stereo puts voices 0 and 3 left, 1 and 2 right */
    voice_start(&v[0], a, 4, 1);
    voice_start(&v[1], b, 2, 1);
    v[0].step = v[1].step = 1u << 16;
    v[0].row = v[1].row = 64;
    mixer_render(v, 0, out, 1);
    CHECK(out[0] == (25 - 15) * 256);
    mixer_render(v, 1, out, 1);
    CHECK(out[0] == 50 * 256 && out[1] == -30 * 256);
    v[1].active = 0;
    v[3] = v[0];
    v[3].row = 64;
    mixer_render(v, 1, out, 1);
    CHECK(out[0] == 100 * 256 && out[1] == 0);
    /* byte wrap-around, no clipping: 0x80 + 50 + 100 wraps */
    v[3].row = 128;
    mixer_render(v, 1, out, 1);
    CHECK(out[0] == (int16_t)((uint8_t)(0x80 + 50 + 100) - 0x80) * 256);
    /* a loop wraps to the start and sets the idle flag at every wrap */
    memset(v, 0, sizeof v);
    voice_start(&v[0], a, 4, 1);
    v[0].step = 1u << 16;
    v[0].row = 64;
    CHECK(!v[0].idle);
    mixer_render(v, 0, out, 3);
    CHECK(!v[0].idle);
    mixer_render(v, 0, out, 2);
    CHECK(v[0].idle && v[0].active && (v[0].pos >> 8) == 1);
    /* a repeat length under 4 stops the voice */
    voice_start(&v[0], b, 2, 1);
    v[0].step = 1u << 16;
    mixer_render(v, 0, out, 2);
    CHECK(!v[0].active);
    /* samples per tick: 434 or 435, 21,739 per 50 ticks */
    uint32_t acc = 0;
    int total = 0, lo = 1000, hi = 0;
    for (int t = 0; t < 50; t++) {
        int n = mixer_tick_samples(&acc);
        total += n;
        lo = n < lo ? n : lo;
        hi = n > hi ? n : hi;
    }
    CHECK(total == 21739 && lo == 434 && hi == 435);
}

/* ---- the play call ------------------------------------------------------------------------- */

static void test_play(void) {
    Sound *s = fresh();
    CHECK(s->effects_on && s->dcard == 1 && !s->stereo);
    sound_play(s, 2, 0, 0x140, 63, 0);
    CHECK(s->voice[0].active && s->voice[0].len == 600 && s->voice[0].row == 54 && s->voice[0].step == 0x802C);
    CHECK(!s->voice[0].idle);
    sound_play(s, 2, 3, 0x140, 63, 0); /* voice 3 is never started */
    CHECK(!s->voice[3].active);
    /* voice 2: also voice 3, looping */
    sound_play(s, 3, 2, 0xDC, 30, 1);
    CHECK(s->voice[2].active && s->voice[3].active && s->voice[3].data == s->voice[2].data);
    CHECK(s->voice[2].rep_len == 8 && s->voice_id[3] == 3);
    /* voice 2 again with the same id: voices 2 and 3 are cleared and nothing restarts */
    sound_play(s, 3, 2, 0x140, 63, 1);
    CHECK(!s->voice[2].active && !s->voice[3].active);
    /* a music bank starts the song at the request's level */
    sound_play(s, 175, 2, 0x140, 0, 1);
    CHECK(s->music.playing && s->song == 175 && s->music.master == 7);
    /* any other background request stops it */
    sound_play(s, 52, 2, 0x140, 10, 1); /* not 222 = 0xDE, the site-map song's id */
    CHECK(!s->music.playing && !s->song && s->voice[2].active);
    /* the site-map song clears voices 2 and 3 too, before the load (here: a missing file; SND-15) */
    sound_play(s, 0xDE, 2, 0x140, 10, 1);
    CHECK(!s->voice[2].active && !s->voice[3].active);
    sound_play(s, 52, 2, 0x140, 10, 1);
    sound_play(s, 4, 2, 0x140, 10, 1); /* any other id as well, even a missing one */
    CHECK(!s->voice[2].active && !s->voice[3].active);
    /* a missing id plays nothing */
    sound_play(s, 4, 0, 0x140, 63, 0);
    CHECK(s->voice[0].data == (const int8_t *)s->sample[2]);
}

/* The driver's delay after a voice start (docs/sound.md, "Voices and output"): the clock's tick credit is set to 2 at
 * every real sample start, whatever it was; not for a BONG song nor any early return of the play call. */
static void test_tick_credit(void) {
    int credit = 7;
    Sound *s = fresh();
    sound_play(s, 2, 0, 0x140, 63, 0); /* no credit wired: nothing to set, no crash */
    s->tick_credit = &credit;
    sound_play(s, 2, 0, 0x140, 63, 0); /* a one-shot */
    CHECK(credit == 2);
    credit = 0;
    sound_play(s, 3, 1, 0x140, 63, 1); /* a loop, from a credit below 2 */
    CHECK(credit == 2);
    credit = 5;
    sound_play(s, 52, 2, 0x140, 10, 1); /* the background voice: voices 2 and 3, still 2 at the end */
    CHECK(credit == 2 && s->voice[3].active);
    credit = 5;
    sound_play(s, 52, 2, 0x140, 10, 1); /* v = 2 with the id already in its buffer: returns early */
    CHECK(credit == 5);
    sound_play(s, 2, 3, 0x140, 63, 0); /* voice 3 is never started */
    CHECK(credit == 5);
    sound_play(s, 175, 2, 0x140, 0, 1); /* a BONG song: through the music table, no delay */
    CHECK(s->music.playing && credit == 5);
    sound_play(s, 4, 0, 0x140, 63, 0); /* a missing file starts nothing */
    CHECK(credit == 5);
    s->effects_on = 0; /* SOUND off */
    sound_play(s, 2, 0, 0x140, 63, 0);
    CHECK(credit == 5);
    s->effects_on = 1;
    s->dcard = 0; /* DCARD OFF */
    sound_play(s, 2, 1, 0x140, 63, 0);
    CHECK(credit == 5);
    s = fresh(); /* all voices idle, so the drain can start the request */
    s->tick_credit = &credit;
    sound_request(s, 1, 0x140, 63); /* a queued request: set when the drain starts it, not when it is queued */
    CHECK(credit == 5);
    sound_drain(s);
    CHECK(s->voice[0].data == (const int8_t *)s->sample[1] && credit == 2);
    s->tick_credit = NULL;
}

static void test_stereo_rule(void);
static void test_off_switches(void) {
    const char on[] = "FONTS -JA\r\nSOUND     -JA   | Soundeffekte\r\n";
    const char off[] = "SOUND     -NEIN | Soundeffekte\r\n";
    const char later[] = "SOUND -JA\r\nSOUND -nein\r\n";
    CHECK(sound_cfg_flag(on, sizeof on - 1, "SOUND", 0) == 1);
    CHECK(sound_cfg_flag(off, sizeof off - 1, "SOUND", 1) == 0);
    CHECK(sound_cfg_flag(later, sizeof later - 1, "SOUND", 1) == 0); /* a later line overwrites */
    CHECK(sound_cfg_flag(on, sizeof on - 1, "SVGA", 1) == 1); /* missing: the default */
    const char sb[] = "DCARD     -SB   | x\r\nMCARD     -CD   | x\r\n";
    const char sbp[] = "DCARD -SBP\r\nMCARD -GM\r\n";
    const char sb16[] = "DCARD -SB16\r\nMCARD -ROL\r\n";
    const char o[] = "DCARD     -OFF  | x\r\nMCARD -OFF\r\n";
    CHECK(sound_cfg_card(sb, sizeof sb - 1, "DCARD", 9) == 1 && sound_cfg_card(sb, sizeof sb - 1, "MCARD", 9) == 8);
    CHECK(sound_cfg_card(sbp, sizeof sbp - 1, "DCARD", 9) == 2 && sound_cfg_card(sbp, sizeof sbp - 1, "MCARD", 9) == 7);
    CHECK(sound_cfg_card(sb16, sizeof sb16 - 1, "DCARD", 9) == 3 && sound_cfg_card(sb16, sizeof sb16 - 1, "MCARD", 9) == 6);
    CHECK(sound_cfg_card(o, sizeof o - 1, "DCARD", 9) == 0 && sound_cfg_card(o, sizeof o - 1, "MCARD", 9) == 0);
    /* SOUND off: nothing starts, songs included; the queues still drain */
    Sound *s = fresh();
    s->effects_on = 0;
    sound_play(s, 2, 0, 0x140, 63, 0);
    sound_play(s, 175, 2, 0x140, 0, 1);
    CHECK(!s->voice[0].active && !s->music.playing);
    sound_request(s, 2, 0x140, 63);
    sound_drain(s);
    CHECK(!s->queue[0][0].id && !s->voice[0].active);
    /* DCARD OFF likewise */
    s = fresh();
    s->dcard = 0;
    sound_play(s, 2, 1, 0x140, 63, 0);
    sound_background(s, 175, 0x140, 20);
    sound_background_step(s, 0);
    CHECK(!s->voice[1].active && !s->music.playing && s->bg_playing == 175);
    /* the files: SOUND -NEIN and DCARD -OFF read by sound_init; SBP gives stereo */
    const char cfg_off[] = "SOUND -NEIN\r\n";
    write_file("BIING!.CFG", (const uint8_t *)cfg_off, sizeof cfg_off - 1);
    s = fresh();
    CHECK(!s->effects_on && !sound_enabled(s));
    const char cfg_on[] = "SOUND     -JA   | x\r\n";
    write_file("BIING!.CFG", (const uint8_t *)cfg_on, sizeof cfg_on - 1);
    write_file("SOUND.CFG", (const uint8_t *)o, sizeof o - 1);
    s = fresh();
    CHECK(s->effects_on && s->dcard == 0 && !sound_enabled(s));
    write_file("SOUND.CFG", (const uint8_t *)sbp, sizeof sbp - 1);
    s = fresh();
    CHECK(sound_enabled(s) && s->stereo && s->mcard == 7);
    write_file("SOUND.CFG", (const uint8_t *)sb16, sizeof sb16 - 1);
    s = fresh();
    CHECK(s->stereo && s->dcard == 3); /* SB16: stereo too */
    test_stereo_rule();
    /* MCARD OFF: no song (engine reading) */
    s = fresh();
    s->mcard = 0;
    sound_play(s, 175, 2, 0x140, 0, 1);
    CHECK(!s->music.playing);
    const char sb_file[] = "DCARD     -SB   | x\r\nMCARD     -SB   | x\r\n";
    write_file("SOUND.CFG", (const uint8_t *)sb_file, sizeof sb_file - 1);
}

/* ---- writers -------------------------------------------------------------------------------- */

static void test_writers(void) {
    Sound *s = fresh();
    s->queue[0][1].id = 9;
    sound_request(s, 2, 0x1A4, 30);
    CHECK(s->queue[0][0].id == 2 && s->queue[0][0].period == 0x1A4 && s->queue[0][0].volume == 30);
    CHECK(s->queue[1][0].id == 2 && s->queue[0][1].id == 9 && s->sticky);
    s = fresh();
    s->help_pending = 1;
    sound_request(s, 2, 0x140, 30);
    CHECK(!s->queue[0][0].id && !s->sticky);
    s->help_pending = 0, s->race_pending = 2;
    sound_request(s, 2, 0x140, 30);
    CHECK(!s->queue[0][0].id);
    s->race_pending = 0, s->race_busy = 1; /* 0x278D1 does not check [0x5CA9] */
    sound_request(s, 2, 0x140, 30);
    CHECK(s->queue[0][0].id == 2);
    /* the list copies: only queue 1 checks the busy flags */
    for (int i = 0; i < 8; i++) put_entry(0x468A + 6 * (uint32_t)i, i < 2 ? 0x4A : 0, 0x140, i ? 15 : 25);
    s = fresh();
    s->queue[0][5].id = 77;
    sound_list(s, 0, 0x468A);
    CHECK(s->queue[0][0].id == 0x4A && s->queue[0][0].volume == 25 && s->queue[0][1].volume == 15);
    CHECK(s->queue[0][5].id == 0); /* the whole 48 bytes */
    CHECK(!s->sticky);
    s = fresh();
    s->race_busy = 1;
    sound_list(s, 0, 0x468A);
    sound_list(s, 1, 0x468A);
    CHECK(!s->queue[0][0].id && s->queue[1][0].id == 0x4A);
    s->race_busy = 0, s->help_pending = 1;
    sound_list(s, 0, 0x468A);
    CHECK(!s->queue[0][0].id);
    /* the clear: refused while busy, but [0x5CAE] is set either way */
    s = fresh();
    s->queue[2][3].id = 5;
    s->race_pending = 1;
    sound_clear(s);
    CHECK(s->queue[2][3].id == 5 && s->sticky);
    s->race_pending = 0;
    sound_clear(s);
    CHECK(!s->queue[2][3].id);
    /* the ring: queue 2 head, entry 2 cleared */
    s = fresh();
    s->queue[1][1].id = 4, s->queue[1][2].id = 5;
    sound_ring(s);
    CHECK(s->queue[1][0].id == 0xC6 && s->queue[1][0].volume == 63 && !s->queue[1][1].id && s->queue[1][2].id == 5);
    CHECK(!s->sticky);
    /* the show sound: one of eight lists into each queue; without a patient entry 2 is cleared */
    for (int k = 0; k < 8; k++)
        for (int i = 0; i < 6; i++) put_entry(0x48AA + 48u * (uint32_t)k + 6u * (uint32_t)i, 0xDF + 0 * k, 0x140 + k, 60 - i);
    s = fresh();
    script(2, (uint32_t[]){3, 6});
    sound_show(s, 1);
    CHECK(s->queue[0][0].period == 0x143 && s->queue[1][0].period == 0x146 && s->queue[0][1].id == 0xDF && s->sticky);
    s = fresh();
    script(2, (uint32_t[]){0, 7});
    sound_show(s, 0);
    CHECK(s->queue[0][0].id == 0xDF && !s->queue[0][1].id && s->queue[0][2].id == 0xDF && !s->queue[1][1].id);
    CHECK(s->queue[0][0].period == 0x140 && s->queue[1][0].period == 0x147); /* lists 0 and 7 */
    /* the invoice: 0x4A2A + 48 c, no busy check */
    for (int c = 0; c < 9; c++) put_entry(0x4A2A + 48u * (uint32_t)c, 51 + c, 0x140, 63);
    s = fresh();
    s->help_pending = 1;
    sound_invoice(s, 0);
    CHECK(s->queue[0][0].id == 51 && s->queue[1][0].id == 51 && s->sticky);
    sound_invoice(s, 8);
    CHECK(s->queue[0][0].id == 59);
    sound_invoice(s, 9);
    CHECK(s->queue[0][0].id == 59);
}

/* ---- draining ------------------------------------------------------------------------------ */

static void test_drain(void) {
    Sound *s = fresh();
    s->queue[0][0] = (SoundEntry){2, 0x140, 63};
    s->queue[0][1] = (SoundEntry){3, 0x140, 40};
    sound_drain(s);
    CHECK(s->voice[0].active && s->voice_id[0] == 2 && s->queue[0][0].id == 3 && !s->queue[0][1].id);
    CHECK(s->voice[0].rep_len == 0); /* a queued sound plays once */
    sound_drain(s); /* voice 0 busy: waits */
    CHECK(s->queue[0][0].id == 3);
    finish(s);
    sound_drain(s);
    CHECK(s->voice_id[0] == 3 && !s->queue[0][0].id && s->voice[0].row == sound_row(40));
    /* sticky: queues 1 and 2 advance only while both voices are idle */
    s = fresh();
    s->sticky = 1;
    sound_play(s, 2, 1, 0x140, 63, 0);
    s->queue[0][0] = (SoundEntry){3, 0x140, 63};
    sound_drain(s);
    CHECK(s->queue[0][0].id == 3 && !s->voice[0].active);
    finish(s);
    sound_drain(s);
    CHECK(!s->queue[0][0].id && s->voice[0].active);
    /* without the sticky flag queue 1 does not wait for voice 1 */
    s = fresh();
    sound_play(s, 2, 1, 0x140, 63, 0);
    s->queue[0][0] = (SoundEntry){3, 0x140, 63};
    sound_drain(s);
    CHECK(!s->queue[0][0].id && s->voice[0].active);
    /* queue 3 plays on voices 2 and 3; queue 4 is discarded */
    s = fresh();
    s->queue[2][0] = (SoundEntry){3, 0x140, 63};
    s->queue[3][0] = (SoundEntry){2, 0x140, 63};
    s->queue[3][1] = (SoundEntry){3, 0x140, 63};
    sound_drain(s);
    CHECK(s->voice[2].active && s->voice[3].active && s->voice_id[2] == 3);
    CHECK(s->queue[3][0].id == 2); /* voice 3 got queue 3's sound in the same pass: not idle */
    s->voice[3].idle = 1;
    sound_drain(s);
    CHECK(s->queue[3][0].id == 3 && s->voice_id[3] == 3); /* discarded: voice 3 kept what voice 2 gave it */
    /* the site-map song blocks the queues (after the special sequences) */
    s = fresh();
    s->bg_playing = SOUND_SITE_MAP_SONG;
    s->queue[0][0] = (SoundEntry){2, 0x140, 63};
    sound_drain(s);
    CHECK(s->queue[0][0].id == 2 && !s->voice[0].active);
}

static void test_sequences(void) {
    /* help jingle: when queues 1 and 2 are empty and both voices idle */
    Sound *s = fresh();
    sound_help(s);
    sound_play(s, 2, 1, 0x140, 63, 0);
    sound_drain(s);
    CHECK(s->help_pending && !s->help_busy);
    finish(s);
    sound_drain(s);
    CHECK(!s->help_pending && s->help_busy);
    CHECK(s->voice_id[0] == 1 && s->voice_id[1] == 1 && s->voice[0].row == 54); /* sample 1, 0x140, 63, both */
    CHECK(!s->queue[0][0].id);
    sound_drain(s); /* both queues are empty already: [0x5CAB] clears here while the jingle still plays */
    finish(s);
    CHECK(!s->help_busy); /* both queues were empty at that pass */
    s->help_busy = s->race_busy = 1;
    s->queue[1][0] = (SoundEntry){4, 0x140, 63}; /* queue 2 not empty: the flags stay */
    s->voice[1].idle = 0;
    s->sticky = 0;
    sound_drain(s);
    CHECK(s->help_busy);
    s->voice[1].idle = 1;
    sound_drain(s);
    sound_drain(s);
    CHECK(!s->help_busy);
    /* with a queue not empty, the jingle waits */
    s = fresh();
    sound_help(s);
    s->queue[1][0] = (SoundEntry){0, 0, 0};
    s->queue[0][0] = (SoundEntry){4, 0x140, 63}; /* a missing id */
    sound_drain(s);
    CHECK(s->help_pending);
    sound_drain(s);
    CHECK(!s->help_pending);
    /* race announcement: six entries of the list by the value */
    for (int i = 0; i < 6; i++) { /* six entries each: the two lists are 40 bytes apart */
        put_entry(0x558E + 6u * (uint32_t)i, 70 + i, 0xFA, 63);
        put_entry(0x55B6 + 6u * (uint32_t)i, 170 + i, 0xFA, 63);
    }
    s = fresh();
    sound_race(s, 1);
    sound_drain(s);
    CHECK(!s->race_pending && s->race_busy);
    CHECK(s->queue[0][0].id == 71 && s->queue[1][0].id == 71 && s->queue[0][4].id == 75 && !s->queue[0][5].id);
    CHECK(s->voice_id[0] == 70 || s->status[70] == -1);
    s = fresh();
    sound_race(s, 2);
    sound_drain(s);
    CHECK(s->queue[1][4].id == 175);
    /* while the announcement runs the busy writers keep off */
    sound_list(s, 0, 0x468A);
    CHECK(s->queue[0][0].id == 171);
    /* help before race when both are pending */
    s = fresh();
    sound_help(s);
    sound_race(s, 1);
    sound_drain(s);
    CHECK(!s->help_pending && s->race_pending == 1);
}

/* ---- background slot ----------------------------------------------------------------------- */

static void test_background(void) {
    Sound *s = fresh();
    sound_background(s, 3, 0xDC, 8);
    sound_background_step(s, 0); /* level 0: starts at once */
    CHECK(s->bg_playing == 3 && !s->bg_request && s->voice[2].active && s->voice[3].active);
    CHECK(s->voice[2].rep_len == 8 && s->voice[2].step == sound_step(0xDC));
    CHECK(s->voice[2].row == 7); /* row 7 already at level 0 */
    for (int i = 0; i < 5; i++) sound_background_step(s, 0);
    CHECK(s->bg_level == 5 && s->voice[2].row == sound_row(5) && s->voice[3].row == sound_row(5));
    for (int i = 0; i < 10; i++) sound_background_step(s, 0);
    CHECK(s->bg_level == 8); /* the target */
    /* a request fades out one step per action, then starts */
    sound_background(s, 52, 0x140, 20);
    sound_background_step(s, 0);
    CHECK(s->bg_level == 7 && s->bg_playing == 3);
    for (int i = 0; i < 6; i++) sound_background_step(s, 0);
    CHECK(s->bg_level == 1 && s->bg_playing == 3 && s->bg_request == 52);
    sound_background_step(s, 0); /* the step that brings the level to 0 starts it (SND-14) */
    CHECK(s->bg_level == 0 && s->bg_playing == 52 && s->voice_id[2] == 52 && !s->bg_request);
    /* the same id again: dropped, the level dips one step and comes back */
    for (int i = 0; i < 30; i++) sound_background_step(s, 0);
    CHECK(s->bg_level == 20);
    sound_background(s, 52, 0x140, 20);
    sound_background_step(s, 0);
    CHECK(s->bg_level == 19 && !s->bg_request && s->voice[2].active);
    sound_background_step(s, 0);
    CHECK(s->bg_level == 20);
    /* a lower target: never falls toward it, only a new request lowers the level */
    s->bg_target = 5;
    sound_background_step(s, 0);
    CHECK(s->bg_level == 20);
    /* pace: while the clock runs, at most once per 4 game ticks */
    s = fresh();
    sound_background(s, 3, 0x140, 30);
    sound_background_step(s, 1);
    CHECK(s->bg_playing == 3 && s->pace == 4);
    sound_background_step(s, 1);
    sound_background_step(s, 1);
    CHECK(s->bg_level == 0);
    for (int t = 0; t < 3; t++) sound_game_tick(s), sound_background_step(s, 1);
    CHECK(s->bg_level == 0);
    sound_game_tick(s);
    sound_background_step(s, 1);
    CHECK(s->bg_level == 1);
    sound_background_step(s, 0); /* clock stopped: every iteration */
    sound_background_step(s, 0);
    CHECK(s->bg_level == 3);
    /* a song at its level: master = (6 L >> 3) + 7 */
    s = fresh();
    sound_background(s, 175, 0x140, 18);
    sound_background_step(s, 0);
    CHECK(s->music.playing && s->music.master == 7);
    for (int i = 0; i < 18; i++) sound_background_step(s, 0);
    CHECK(s->music.master == 7); /* the step does not write it */
    sound_interrupt(s); /* the music player takes the level at its tick */
    CHECK(s->music.master == sound_row(18));
    s->bg_playing = s->song = SOUND_SITE_MAP_SONG; /* the site-map song follows the level too (engine reading) */
    s->bg_level = 16;
    sound_interrupt(s);
    CHECK(s->music.master == sound_row(16));
    s->bg_playing = s->song = 175;
    s->bg_level = 18;
    /* another background stops the song */
    sound_background(s, 3, 0x140, 8);
    for (int i = 0; i < 17; i++) sound_background_step(s, 0);
    CHECK(s->music.playing && s->bg_level == 1);
    sound_background_step(s, 0);
    CHECK(!s->music.playing && s->bg_playing == 3 && s->bg_level == 0);
    /* nothing touched while the site-map song plays */
    s->bg_playing = SOUND_SITE_MAP_SONG;
    s->voice[2].row = 33;
    s->bg_target = 30;
    sound_background_step(s, 0);
    CHECK(s->voice[2].row == 33);
    CHECK(sound_background_requested_or_playing(s, SOUND_SITE_MAP_SONG));
}

/* The three rules of docs/sound.md, "Event sounds and background changes checked against the original (U-9,
 * 2026-10-20)", "What the engine must change" 1-3 (SND-14). */
static void test_background_u9(void) {
    /* 1: level 1, a request for another id, clock stopped: one step -> level 0 and the new id on voice 2 */
    Sound *s = fresh();
    sound_play(s, 3, 2, 0xDC, 0, 1);
    s->bg_playing = 3, s->bg_level = 1;
    sound_background(s, 52, 0x140, 20);
    sound_background_step(s, 0);
    CHECK(s->bg_level == 0 && s->bg_playing == 52 && s->voice_id[2] == 52 && s->voice[2].active && !s->bg_request);
    /* level 2: one step -> level 1, nothing started */
    s = fresh();
    sound_play(s, 3, 2, 0xDC, 0, 1);
    s->bg_playing = 3, s->bg_level = 2;
    sound_background(s, 52, 0x140, 20);
    sound_background_step(s, 0);
    CHECK(s->bg_level == 1 && s->bg_playing == 3 && s->voice_id[2] == 3 && s->bg_request == 52);
    /* a dropped request is cleared before the decrement: reaching level 0 starts nothing */
    s->bg_level = 1;
    sound_background(s, 3, 0xDC, 20);
    uint32_t started = s->started[3];
    sound_background_step(s, 0);
    CHECK(s->bg_level == 0 && !s->bg_request && s->voice[2].active && s->started[3] == started);
    /* 2: the id playing, requested at level 0: the play call is made again (its step 2 clears voices 2 and 3, step 3
     * returns for the same id, docs/sound.md "The play call") */
    s = fresh();
    sound_play(s, 3, 2, 0xDC, 0, 1);
    s->bg_playing = 3, s->bg_level = 0;
    CHECK(s->voice[2].active);
    sound_background(s, 3, 0xDC, 8);
    sound_background_step(s, 0);
    CHECK(!s->bg_request && s->bg_playing == 3 && !s->voice[2].active && !s->voice[3].active);
    /* at level 3: level 2, no play call, request cleared */
    s = fresh();
    sound_play(s, 3, 2, 0xDC, 0, 1);
    s->bg_playing = 3, s->bg_level = 3;
    sound_background(s, 3, 0xDC, 8);
    sound_background_step(s, 0);
    CHECK(s->bg_level == 2 && !s->bg_request && s->voice[2].active);
    /* 3: clock stopped, level 0 below target 10, no request: one step -> level 1 and pace 4 */
    s = fresh();
    s->bg_target = 10;
    sound_background_step(s, 0);
    CHECK(s->bg_level == 1 && s->pace == 4);
    /* then the clock runs: no action until the clock has counted the pace to 0 */
    for (int t = 0; t < 3; t++) {
        sound_game_tick(s);
        sound_background_step(s, 1);
    }
    CHECK(s->bg_level == 1 && s->pace == 1);
    sound_game_tick(s);
    sound_background_step(s, 1);
    CHECK(s->bg_level == 2 && s->pace == 4);
    /* a start and a fade-out step with the clock stopped leave the pace */
    s = fresh();
    s->pace = 2;
    sound_background(s, 3, 0xDC, 8);
    sound_background_step(s, 0);
    CHECK(s->bg_playing == 3 && s->pace == 2);
    s->bg_level = 5;
    sound_background(s, 52, 0x140, 8);
    sound_background_step(s, 0);
    CHECK(s->bg_level == 4 && s->pace == 2);
}

/* The play call's clear for the background voice (docs/sound.md, "The play call's clear for the background voice",
 * SND-15): the site-map song clears all four voices and stops the module, any other id with no module only voices 2
 * and 3; the site-map song requested again goes silent, its CD track plays on. */
static void write_track(const char *rel, int n, int16_t left, int16_t right, int channels, int rate);
static void put32_at(uint32_t off, uint32_t v);
static void test_play_clear_snd15(void) {
    /* 1: one-shots id 2 on voices 0 and 1, the loop id 3 on voices 2 and 3, then 0xDE (no file): all four stopped */
    Sound *s = fresh();
    sound_play(s, 2, 0, 0x140, 63, 0);
    sound_play(s, 2, 1, 0x140, 63, 0);
    sound_play(s, 3, 2, 0xDC, 0, 1);
    CHECK(s->voice[0].active && s->voice[1].active && s->voice[2].active && s->voice[3].active);
    sound_play(s, 0xDE, 2, 0x140, 10, 1);
    CHECK(!s->voice[0].active && !s->voice[1].active && !s->voice[2].active && !s->voice[3].active);
    /* with the module of id 175 playing: no module plays after the call, song 0 */
    s = fresh();
    sound_play(s, 175, 2, 0x140, 0, 1);
    CHECK(s->music.playing && s->song == 175);
    sound_play(s, 0xDE, 2, 0x140, 10, 1);
    CHECK(!s->music.playing && s->song == 0);
    /* 2: any other id with no module: voices 2 and 3 only */
    s = fresh();
    sound_play(s, 2, 0, 0x140, 63, 0);
    sound_play(s, 3, 2, 0xDC, 0, 1);
    sound_play(s, 52, 2, 0x140, 10, 1);
    CHECK(s->voice[0].active && s->voice_id[2] == 52 && s->voice[2].active);
    /* 3: a module file at 0xDE: started once, the same call again returns with nothing playing */
    uint8_t mod[4096];
    FILE *f = fopen(DIR_ "/DATA/T/7/5", "rb");
    size_t mod_n = f ? fread(mod, 1, sizeof mod, f) : 0;
    if (f) fclose(f);
    write_file("DATA/U/2/2", mod, mod_n);
    s = fresh();
    sound_play(s, 0xDE, 2, 0x140, 0, 1);
    CHECK(s->music.playing && s->song == 0xDE && s->started[0xDE] == 1);
    for (int t = 0; t < 20; t++) sound_interrupt(s);
    sound_play(s, 0xDE, 2, 0x140, 0, 1);
    CHECK(!s->music.playing && s->started[0xDE] == 1);
    CHECK(!s->voice[0].active && !s->voice[1].active && !s->voice[2].active && !s->voice[3].active);
    /* in CD mode (0xDE -> track 5): the track plays on, no module */
    memset(g_image + 0xB97E, 0, 24);
    put32_at(0xB97E, 2800 + 0xDE), put32_at(0xB97E + 8, 5);
    put32_at(0xB97E + 12, 0xFFFFFFFFu);
    write_track("CDAUDIO/TRACK05.WAV", 3000, 1000, -1000, 2, 44100);
    const char cd_cfg[] = "DCARD     -SB16 | x\r\nMCARD     -CD   | x\r\nMIXER     -FCC9L| x\r\n";
    write_file("SOUND.CFG", (const uint8_t *)cd_cfg, sizeof cd_cfg - 1);
    s = fresh();
    CHECK(s->cd_mode);
    sound_play(s, 0xDE, 2, 0x140, 0, 1);
    CHECK(s->cd_song == 5 && s->cd.busy && s->music.playing);
    sound_play(s, 0xDE, 2, 0x140, 0, 1);
    CHECK(s->cd_song == 5 && s->cd.busy && !s->music.playing);
    remove(DIR_ "/DATA/U/2/2");
    remove(DIR_ "/CDAUDIO/TRACK05.WAV");
    memset(g_image + 0xB97E, 0, 24);
    const char sb_file[] = "DCARD     -SB   | x\r\nMCARD     -SB   | x\r\n";
    write_file("SOUND.CFG", (const uint8_t *)sb_file, sizeof sb_file - 1);
}

/* ---- ambience ------------------------------------------------------------------------------ */

static void test_ambience(void) {
    for (int r = 0; r < 8; r++) {
        put_entry(0x4FBA + 12u * (uint32_t)r, 100 + r, 0x140, 58);
        put_entry(0x4FBA + 12u * (uint32_t)r + 6, 110 + r, 0x140, 48);
    }
    Sound *s = fresh();
    script(2, (uint32_t[]){4, 5});
    sound_ambience(s, 0, 0x4FBA); /* no 34-tick flag: nothing, no draw */
    CHECK(!s->queue[0][0].id && rq_i == 0);
    /* [0x5C89]: the flag first after 50 game ticks of the program, then every 34 (docs/mainloop.md, "Rest of the
     * clock routine", SCR-8) */
    for (int t = 0; t < 49; t++) sound_game_tick(s);
    CHECK(!s->ambience);
    sound_game_tick(s);
    CHECK(s->ambience);
    s->ambience = 0;
    for (int t = 0; t < 33; t++) sound_game_tick(s);
    CHECK(!s->ambience);
    sound_game_tick(s);
    CHECK(s->ambience);
    script(2, (uint32_t[]){3, 5});
    sound_ambience(s, 0, 0x4FBA); /* draw 3: not 4 */
    CHECK(!s->queue[0][0].id && rq_i == 1);
    script(2, (uint32_t[]){4, 5});
    sound_ambience(s, 0, 0x4FBA);
    CHECK(s->queue[0][0].id == 105 && s->queue[0][1].id == 115 && s->queue[0][1].volume == 48);
    CHECK(s->ambience); /* queue 1 never clears the flag */
    /* first free entry of the first six */
    s->queue[0][2].id = 9, s->queue[0][3].id = 9;
    script(2, (uint32_t[]){4, 0});
    sound_ambience(s, 0, 0x4FBA);
    CHECK(s->queue[0][4].id == 100 && s->queue[0][5].id == 110);
    script(2, (uint32_t[]){4, 1});
    sound_ambience(s, 0, 0x4FBA);
    CHECK(s->queue[0][6].id == 0 && rq_i == 1); /* none of the six free: no record draw */
    /* queue 1 refuses while a busy flag is set; queue 2 does not, and clears the flag once its draw passed */
    s = fresh();
    s->ambience = 1;
    s->race_busy = 1;
    script(3, (uint32_t[]){4, 4, 2});
    sound_ambience(s, 0, 0x4FBA);
    CHECK(!s->queue[0][0].id);
    sound_ambience(s, 1, 0x4FBA);
    CHECK(s->queue[1][0].id == 102 && !s->ambience);
    s = fresh();
    s->ambience = 1;
    script(1, (uint32_t[]){63});
    sound_ambience(s, 1, 0x4FBA);
    CHECK(s->ambience); /* the draw failed: the flag stays */
    /* the pair, and the show music */
    s = fresh();
    s->ambience = 1;
    script(4, (uint32_t[]){4, 1, 4, 2});
    sound_ambience_pair(s, 0x4FBA);
    CHECK(s->queue[0][0].id == 101 && s->queue[1][0].id == 102 && !s->ambience);
    for (int r = 0; r < 8; r++) put_entry(0x50DA + 12u * (uint32_t)r, 200 + r, 0x140, 30);
    s = fresh();
    s->ambience = 1;
    sound_show_music(s, 0); /* no show: no draw */
    CHECK(rq_i == 0);
    script(1, (uint32_t[]){0});
    sound_show_music(s, 1);
    CHECK(rq_i == 1 && !s->queue[0][0].id && !s->queue[1][0].id);
    script(4, (uint32_t[]){1, 1, 4, 3});
    sound_show_music(s, 2);
    CHECK(s->queue[0][0].id == 203 && !s->queue[1][0].id);
    /* the game-over copy: no flag, no draw of 64 */
    s = fresh();
    script(1, (uint32_t[]){6});
    sound_ambience_copy(s, 3, 0x4FBA);
    CHECK(s->queue[3][0].id == 106 && rq_i == 1);
}

static uint32_t ambience_sim_random(void *ctx, uint32_t n) { return sim_random(ctx, n); }
static uint32_t ambience_sim_bits(void *ctx, uint32_t mask) { return sim_bits(ctx, mask); }

/* SND-16: use the real generator adapters, not the modulo-only fixture above. Values 8, 15 and FFFFFFFF
 * would make random(8) retry; AND 7 must consume exactly one selection draw. All data here is synthetic. */
static void test_ambience_raw_selection(void) {
    const uint32_t raw[] = {0, 8, 15, UINT32_MAX};
    const uint32_t table = 0x4FBA;
    for (unsigned r = 0; r < 8; r++) {
        put_entry(table + 12 * r, 100 + (int)r, 300 + (int)r, 40 + (int)r);
        put_entry(table + 12 * r + 6, 110 + (int)r, 400 + (int)r, 50 + (int)r);
    }
    for (int queue = 0; queue < 2; queue++) {
        for (int filled = 0; filled <= 6; filled++) {
            for (size_t r = 0; r < sizeof raw / sizeof *raw; r++) {
                for (int busy = 0; busy < 2; busy++) {
                    Sound *s = fresh();
                    uint32_t values[] = {4, raw[r], 0}; /* gate, selection, a finite retry sentinel */
                    SimScript draws = {.v = values, .n = sizeof values / sizeof *values};
                    Sim sim = {.rng_script = &draws};
                    s->random = ambience_sim_random;
                    s->bits = ambience_sim_bits;
                    s->random_ctx = &sim;
                    s->ambience = 1;
                    s->race_busy = busy;
                    for (int q = 0; q < SOUND_QUEUES; q++) {
                        for (int i = 0; i < SOUND_QUEUE_LEN; i++) {
                            SoundEntry e = {(uint16_t)(200 + q * 8 + i), (uint16_t)(500 + i), (uint16_t)(20 + q)};
                            s->queue[q][i] = e;
                        }
                    }
                    for (int i = filled; i < 6; i++) s->queue[queue][i].id = 0;
                    SoundEntry expected[SOUND_QUEUES][SOUND_QUEUE_LEN];
                    memcpy(expected, s->queue, sizeof expected);
                    int eligible = filled < 6 && (queue != 0 || !busy);
                    if (eligible) {
                        unsigned pick = raw[r] & 7;
                        expected[queue][filled] = (SoundEntry){(uint16_t)(100 + pick), (uint16_t)(300 + pick),
                                                              (uint16_t)(40 + pick)};
                        expected[queue][filled + 1] = (SoundEntry){(uint16_t)(110 + pick), (uint16_t)(400 + pick),
                                                                  (uint16_t)(50 + pick)};
                    }
                    sound_ambience(s, queue, table);
                    CHECK(draws.used == (size_t)(eligible ? 2 : 1) && draws.extra == 0);
                    CHECK(memcmp(expected, s->queue, sizeof expected) == 0);
                    CHECK(s->ambience == (queue == 0));
                    CHECK(s->race_busy == busy);
                }
            }
        }
    }
    /* fresh() owns one persistent Sound; do not leave callbacks pointing at a completed stack frame. */
    fresh();
}

/* SND-18: real generator adapters distinguish raw AND 1 from random(2). The trailing zeroes bound mutant
 * retries. Compare all queues and flags, including ignored busy flags and full-queue tick clearing. */
static void show_music_case(int count, uint32_t gate, uint32_t selector, uint32_t ambience_gate, uint32_t pick,
                            int tick, int busy, int filled) {
    Sound *s = fresh();
    uint32_t values[] = {gate, selector, ambience_gate, pick, 0, 0, 0, 0};
    SimScript draws = {.v = values, .n = sizeof values / sizeof *values};
    Sim sim = {.rng_script = &draws};
    s->random = ambience_sim_random;
    sound_set_bits(s, ambience_sim_bits);
    s->random_ctx = &sim;
    s->sticky = 11, s->help_pending = busy & 1 ? 3 : 0, s->race_pending = busy & 2 ? 5 : 0;
    s->race_busy = busy & 4 ? 7 : 0, s->help_busy = busy & 8 ? 9 : 0;
    s->ambience = tick, s->ambience_count = 17, s->pace = 19;
    int flags[] = {s->sticky, s->help_pending, s->race_pending, s->race_busy, s->help_busy,
                   s->ambience, s->ambience_count, s->pace};
    int queue = selector & 1 ? 0 : 1;
    for (int q = 0; q < SOUND_QUEUES; q++)
        for (int i = 0; i < SOUND_QUEUE_LEN; i++) {
            s->queue[q][i] = (SoundEntry){(uint16_t)(300 + q * 8 + i), (uint16_t)(600 + i), (uint16_t)(20 + q)};
            if (q == queue && i >= filled && i < 6) s->queue[q][i].id = 0;
        }
    SoundEntry expected[SOUND_QUEUES][SOUND_QUEUE_LEN];
    memcpy(expected, s->queue, sizeof expected);
    size_t used = 0;
    if (count) {
        used++;
        if (gate & 1) {
            used++;
            if (tick) {
                used++;
                if ((ambience_gate & 63) == 4 && (queue || !(busy & 7))) {
                    if (queue) flags[5] = 0;
                    if (filled < 6) {
                        used++;
                        unsigned row = pick & 7;
                        expected[queue][filled] = (SoundEntry){(uint16_t)(100 + row), (uint16_t)(300 + row),
                                                              (uint16_t)(40 + row)};
                        expected[queue][filled + 1] = (SoundEntry){(uint16_t)(110 + row), (uint16_t)(400 + row),
                                                                  (uint16_t)(50 + row)};
                    }
                }
            }
        }
    }
    int before = failures;
    sound_show_music(s, count);
    int actual[] = {s->sticky, s->help_pending, s->race_pending, s->race_busy, s->help_busy,
                    s->ambience, s->ambience_count, s->pace};
    CHECK(draws.used == used && draws.extra == 0);
    CHECK(memcmp(expected, s->queue, sizeof expected) == 0);
    CHECK(memcmp(flags, actual, sizeof flags) == 0);
    if (before != failures) fprintf(stderr, "  SND-18 count %d raw %u/%u/%u/%u tick %d busy %d filled %d\n",
                                   count, gate, selector, ambience_gate, pick, tick, busy, filled);
}
static void test_show_music_raw(void) {
    for (unsigned row = 0; row < 8; row++) {
        put_entry(0x50DA + 12 * row, 100 + (int)row, 300 + (int)row, 40 + (int)row);
        put_entry(0x50DA + 12 * row + 6, 110 + (int)row, 400 + (int)row, 50 + (int)row);
    }
    static const uint32_t RAW[] = {0, 1, 2, 3, 0x10000, 0x10001, 0xFFFFFFFEu, UINT32_MAX};
    static const int BUSY[] = {0, 1, 2, 4, 8, 7, 15}, FILLED[] = {0, 5, 6};
    for (size_t gate = 0; gate < sizeof RAW / sizeof *RAW; gate++)
        for (size_t selector = 0; selector < sizeof RAW / sizeof *RAW; selector++)
            for (size_t busy = 0; busy < sizeof BUSY / sizeof *BUSY; busy++)
                for (int filled = 0; filled < 3; filled++)
                    for (int tick = 0; tick <= 1; tick++)
                        show_music_case(1, RAW[gate], RAW[selector], 68, (uint32_t)(gate + selector) | 0x10000u,
                                        tick ? 255 : 0, BUSY[busy], FILLED[filled]);
    /* Every caller counter byte; all first-free positions; both accepted and refused ambience gates. */
    for (int count = 0; count < 256; count++)
        show_music_case(count, 3, (uint32_t)count, 4, UINT32_MAX, 1, 0, count % 7);
    static const uint32_t AMBIENCE[] = {0, 3, 63, 0x10004, 0xFFFFFFC4u, UINT32_MAX};
    for (size_t a = 0; a < sizeof AMBIENCE / sizeof *AMBIENCE; a++)
        for (int selector = 0; selector < 2; selector++)
            show_music_case(255, 1, (uint32_t)selector, AMBIENCE[a], 15, 1, 0, 2);
    fresh(); /* release callbacks to the completed stack frame */
}

/* ---- output and capture -------------------------------------------------------------------- */

/* SB Pro stereo (docs/sound.md, "Stereo (SB Pro)", SND-10): the driver is stereo only when DCARD is SBP or SB16 and
 * neither card is plain SB; voices 0 and 3 go to the even bytes (left), 1 and 2 to the odd ones (right), with the
 * stereo table; a mono driver writes one byte per frame with the mono table, so both WAV sides are equal. */
static int stereo_for(const char *cfg) {
    write_file("SOUND.CFG", (const uint8_t *)cfg, strlen(cfg));
    return fresh()->stereo;
}
static void test_stereo_rule(void) {
    CHECK(!stereo_for("DCARD -SBP\r\nMCARD -SB\r\n")); /* the card pair that was stereo in the engine before */
    CHECK(stereo_for("DCARD -SBP\r\nMCARD -SBP\r\n"));
    CHECK(stereo_for("DCARD -SB16\r\nMCARD -CD\r\n"));
    CHECK(!stereo_for("DCARD -SB\r\nMCARD -SBP\r\n"));
    CHECK(stereo_for("DCARD -SBP\r\nMCARD -OFF\r\n"));
    CHECK(!stereo_for("DCARD -SB16\r\nMCARD -SB\r\n"));
    /* the stereo table's values (r = 54 and 7) */
    CHECK(mixer_volume(127, 54, 1) == 54 && mixer_volume(-128, 54, 1) == -54 && mixer_volume(0, 54, 1) == 0);
    CHECK(mixer_volume(127, 7, 1) == 7 && mixer_volume(-128, 7, 1) == -7 && mixer_volume(-1, 7, 1) == 0);
    /* a voice-1 sample: mono puts it on both sides with the mono table, stereo only on the right with the stereo one */
    Sound *s = fresh(); /* the last file: DCARD SB16, MCARD SB = mono */
    sound_play(s, 2, 1, 0x140, 63, 0);
    sound_interrupt(s);
    CHECK(s->out[0] == mixer_volume(-16, 54, 0) * 256 && s->out[1] == s->out[0] && s->out[0] != 0);
    stereo_for("DCARD -SBP\r\nMCARD -SBP\r\n");
    s = &g_s;
    sound_play(s, 2, 1, 0x140, 63, 0);
    sound_interrupt(s);
    CHECK(s->out[0] == 0 && s->out[1] == mixer_volume(-16, 54, 1) * 256 && s->out[1] != 0);
    sound_play(s, 2, 0, 0x140, 63, 0); /* voice 0: the left side */
    sound_interrupt(s);
    CHECK(s->out[0] == mixer_volume(-16, 54, 1) * 256);
}

static void test_output(void) {
    Sound *s = fresh();
    sound_capture(s, 1);
    sound_play(s, 2, 0, 0x140, 63, 0);
    sound_interrupt(s);
    CHECK(s->out_frames == 434 && s->capture_frames == 434);
    CHECK(s->out[0] == mixer_volume(-16, 54, 0) * 256);
    for (int t = 0; t < 49; t++) sound_interrupt(s);
    CHECK(s->capture_frames == 21739);
    CHECK(sound_write_wav(DIR_ "/out.wav", s->capture, s->capture_frames) == 0);
    FILE *f = fopen(DIR_ "/out.wav", "rb");
    long size = 0;
    if (f) fseek(f, 0, SEEK_END), size = ftell(f), fclose(f);
    CHECK(size == 44 + 21739 * 4);
    /* 600 bytes at a half step = 1,200 output samples, then silence */
    int nonzero = 0;
    for (size_t i = 0; i < s->capture_frames; i++) nonzero += s->capture[2 * i] != 0;
    CHECK(nonzero == 1200);
    sound_capture(s, 0);
    /* the interrupt mixes first, then runs the music tick: what the tick writes is heard from the next interrupt.
     * The test module has no notes, so its tick writes row 0 and step 0 into every voice. */
    s = fresh();
    sound_play(s, 175, 2, 0x140, 63, 1);
    CHECK(s->music.playing);
    sound_load(s, 2);
    voice_start(&s->voice[0], s->sample[2], 600, 1);
    s->voice[0].row = 54, s->voice[0].step = 0x802C;
    sound_interrupt(s);
    CHECK(s->out[0] == mixer_volume(-16, 54, 0) * 256 && s->out[0] != 0); /* mixed at row 54 */
    CHECK(s->voice[0].row == 0 && s->voice[0].step == 0); /* then the tick */
    sound_interrupt(s);
    CHECK(s->out[0] == 0);
}

/* ---- CD audio (docs/cdrom.md, "CD audio music in BIPRO.EXE") ------------------------------------------------- */

/* a synthetic CD track: n frames of (left, right), 16-bit, at the channel count and rate given */
static void write_track(const char *rel, int n, int16_t left, int16_t right, int channels, int rate) {
    size_t bytes = (size_t)n * 2 * (size_t)channels;
    uint8_t *w = calloc(1, 44 + bytes);
    if (!w) return;
    memcpy(w, "RIFF", 4), memcpy(w + 8, "WAVEfmt ", 8), memcpy(w + 36, "data", 4);
    uint32_t v[] = {(uint32_t)(36 + bytes), 16, (uint32_t)rate, (uint32_t)(rate * 2 * channels), (uint32_t)bytes};
    int at[] = {4, 16, 24, 28, 40};
    for (int k = 0; k < 5; k++)
        for (int b = 0; b < 4; b++) w[at[k] + b] = (uint8_t)(v[k] >> (8 * b));
    w[20] = 1, w[22] = (uint8_t)channels, w[32] = (uint8_t)(2 * channels), w[34] = 16;
    for (int i = 0; i < n * channels; i++) {
        int16_t x = i % channels ? right : left;
        w[44 + 2 * i] = (uint8_t)x, w[45 + 2 * i] = (uint8_t)((uint16_t)x >> 8);
    }
    write_file(rel, w, 44 + bytes);
    free(w);
}

static void put32_at(uint32_t off, uint32_t v) {
    for (int b = 0; b < 4; b++) g_image[off + b] = (uint8_t)(v >> (8 * b));
}

static void test_cd_audio(void) {
    /* the MIXER value's characters as hex digits: master, voice, line, CD */
    const char mx[] = "MIXER     -FCC9L| x\r\n", lower[] = "MIXER -FCCc\r\n", none[] = "DCARD -SB\r\n";
    CHECK(sound_cfg_mixer(mx, sizeof mx - 1, 0, 0) == 15 && sound_cfg_mixer(mx, sizeof mx - 1, 3, 0) == 9);
    CHECK(sound_cfg_mixer(lower, sizeof lower - 1, 3, 0) == 12 && sound_cfg_mixer(none, sizeof none - 1, 3, 7) == 7);
    CHECK(sound_cfg_mixer(mx, sizeof mx - 1, 4, 7) == 7); /* 'L' is no hex digit */
    /* the WAV reader takes 16-bit stereo 44,100 Hz only */
    write_track("CDAUDIO/TRACK05.WAV", 100, 1000, -1000, 2, 44100);
    write_track("CDAUDIO/TRACK06.WAV", 3000, 3000, 3000, 2, 44100);
    write_track("CDAUDIO/TRACK07.WAV", 100, 1000, 1000, 1, 44100); /* mono: refused */
    write_track("CDAUDIO/TRACK08.WAV", 1000, 15000, -6000, 2, 44100);
    write_track("CDAUDIO/TRACK09.WAV", 100, 1000, 1000, 2, 22050); /* another rate: refused */
    /* the music table at 0xB97E: song 175 -> track 8, song 176 -> track 6, song 177 -> track 7, then -1 */
    memset(g_image + 0xB97E, 0, 48);
    put32_at(0xB97E, 2800 + 175), put32_at(0xB97E + 8, 8);
    put32_at(0xB97E + 12, 2800 + 176), put32_at(0xB97E + 20, 6);
    put32_at(0xB97E + 24, 2800 + 177), put32_at(0xB97E + 32, 7);
    put32_at(0xB97E + 36, 0xFFFFFFFFu);
    FILE *f = fopen(DIR_ "/DATA/T/7/5", "rb"); /* a second and a third song: the same module */
    uint8_t mod[4096];
    size_t mod_n = f ? fread(mod, 1, sizeof mod, f) : 0;
    if (f) fclose(f);
    write_file("DATA/T/7/6", mod, mod_n);
    write_file("DATA/T/7/7", mod, mod_n);
    const char cd_cfg[] = "DCARD     -SB16 | x\r\nMCARD     -CD   | x\r\nMIXER     -FCC9L| x\r\n";
    write_file("SOUND.CFG", (const uint8_t *)cd_cfg, sizeof cd_cfg - 1);

    Sound *s = fresh();
    CHECK(s->mcard == 8 && s->cd_mode && s->cd_level == 9);
    CHECK(sound_song_track(s, 175) == 8 && sound_song_track(s, 176) == 6 && sound_song_track(s, 2) == 0);
    /* a song: its module is set up but plays no notes, its CD track plays at the mixer's CD level */
    sound_load(s, 2);
    voice_start(&s->voice[0], s->sample[2], 600, 1);
    s->voice[0].row = 54, s->voice[0].step = 0x802C;
    sound_play(s, 175, 2, 0x140, 0, 1);
    CHECK(s->song == 175 && s->music.playing && s->cd_song == 8 && s->cd.track == 8 && s->cd.busy);
    voice_stop(&s->voice[0]);
    sound_interrupt(s);
    CHECK(s->out[0] == 15000 * 9 / 15 && s->out[1] == -6000 * 9 / 15);
    CHECK(s->voice[0].row == 54 && s->voice[0].step == 0x802C); /* no module tick wrote the voices */
    /* the loop check pins a song level above 0 at 1 */
    s->bg_level = 5;
    sound_cd_frame(s);
    CHECK(s->bg_level == 1);
    s->bg_level = 0;
    sound_cd_frame(s);
    CHECK(s->bg_level == 0);
    /* 1,000 frames last 493 output frames: two ticks; then the drive stands until the loop check restarts it */
    sound_interrupt(s);
    CHECK(!s->cd.busy && s->out[2 * 50] == 15000 * 9 / 15 && s->out[2 * 433] == 0);
    sound_interrupt(s);
    CHECK(s->out[0] == 0 && s->cd_song == 8);
    sound_cd_frame(s);
    CHECK(s->cd.busy && s->cd.track == 8 && s->cd.pos == 0);
    sound_interrupt(s);
    CHECK(s->out[0] == 15000 * 9 / 15);
    /* another song: stop, then its track */
    sound_play(s, 176, 2, 0x140, 0, 1);
    CHECK(s->song == 176 && s->cd_song == 6 && s->cd.track == 6 && s->cd.busy);
    sound_interrupt(s);
    CHECK(s->out[0] == 3000 * 9 / 15);
    /* the same song again through the play call: stopped as the module is (voice 2's cache then skips the start;
     * the background slot never makes this call, it drops a request for what plays) */
    sound_play(s, 176, 2, 0x140, 0, 1);
    CHECK(s->song == 0 && s->cd_song == 0 && !s->cd.busy);
    /* a song whose track is not a usable WAV: the drive stands, the module stays silent */
    sound_play(s, 177, 2, 0x140, 0, 1);
    CHECK(s->song == 177 && s->cd_song == 7 && !s->cd.busy);
    sound_cd_frame(s);
    CHECK(!s->cd.busy);
    /* a background that is not a song stops the track and clears the track word */
    sound_play(s, 176, 2, 0x140, 0, 1);
    sound_play(s, 2, 2, 0x140, 30, 1);
    CHECK(s->song == 0 && s->cd_song == 0 && !s->cd.busy);
    sound_cd_frame(s);
    CHECK(!s->cd.busy);
    /* the level 0: the drive plays on, silently */
    s->cd.level = 0;
    sound_play(s, 175, 2, 0x140, 0, 1);
    voice_stop(&s->voice[2]), voice_stop(&s->voice[3]);
    sound_interrupt(s);
    CHECK(s->out[0] == 0 && s->cd.busy && s->cd.pos > 0);
    /* a module the start programs play themselves (no song through the play call) still plays with CD audio */
    sound_play(s, 2, 2, 0x140, 30, 1); /* the song stops */
    CHECK(s->song == 0 && music_start(&s->music, mod, mod_n) == 0);
    voice_start(&s->voice[0], s->sample[2], 600, 1);
    s->voice[0].row = 54, s->voice[0].step = 0x802C;
    sound_interrupt(s);
    CHECK(s->voice[0].row == 0); /* the module ticked */
    music_stop(&s->music, s->voice);

    /* a CD read stops the track for good: CDC present and the resource's DATA/ONHD bit clear (bit i, LSB first) */
    uint8_t onhd[400] = {0};
    onhd[1001 >> 3] |= (uint8_t)(1u << (1001 & 7));
    onhd[(2800 + 176) >> 3] |= (uint8_t)(1u << ((2800 + 176) & 7));
    write_file("DATA/ONHD", onhd, sizeof onhd);
    write_file("CDC", (const uint8_t *)"BIING! CD", 9);
    s = fresh();
    CHECK(s->cdc && s->onhd_bits == 3200);
    sound_play(s, 176, 2, 0x140, 0, 1); /* its module on the hard disk: no CD read */
    CHECK(s->cd_song == 6 && s->cd.busy);
    sound_resource_read(s, 1001); /* on the hard disk */
    CHECK(s->cd_song == 6 && s->cd.busy);
    sound_resource_read(s, 1000); /* from the CD */
    CHECK(s->cd_song == 0 && !s->cd.busy);
    sound_cd_frame(s);
    CHECK(!s->cd.busy); /* not resumed */
    sound_play(s, 175, 2, 0x140, 0, 1); /* the next song starts the music again; its module is read from the CD */
    CHECK(s->cd_song == 8 && s->cd.busy && s->last_resource == 2800 + 175);
    sound_resource_read(s, 2800 + 175); /* the resource read last is kept: no drive access */
    CHECK(s->cd.busy);
    sound_resource_read(s, 3500); /* beyond DATA/ONHD: not a CD read */
    CHECK(s->cd.busy);
    sound_resource_read(s, 1000);
    CHECK(!s->cd.busy && s->cd_song == 0);
    /* without CDC no read goes to the CD */
    remove(DIR_ "/CDC");
    s = fresh();
    sound_play(s, 175, 2, 0x140, 0, 1);
    sound_resource_read(s, 1000);
    CHECK(s->cd_song == 8 && s->cd.busy);

    /* the start programs' tracks (docs/intro.md, SND-13): the CD level, no track word, so an ended track stays ended
     * (the loop check does not restart it); a missing track is silence; the stop is immediate */
    s = fresh();
    CHECK(sound_program_track(s, 8) == 1 && s->cd.track == 8 && s->cd.busy && s->cd_song == 0);
    sound_interrupt(s);
    CHECK(s->out[0] == 15000 * 9 / 15 && s->out[1] == -6000 * 9 / 15 && s->cd.busy);
    sound_interrupt(s); /* 1,000 frames: two ticks */
    CHECK(!s->cd.busy);
    sound_cd_frame(s);
    sound_interrupt(s);
    CHECK(!s->cd.busy && s->out[0] == 0 && s->out[2 * 300] == 0);
    CHECK(sound_program_track(s, 3) == 1 && !s->cd.busy); /* no TRACK03.WAV */
    sound_program_track(s, 6);
    sound_program_track_stop(s);
    sound_interrupt(s);
    CHECK(!s->cd.busy && s->out[0] == 0);
    sound_play(s, 176, 2, 0x140, 0, 1); /* a game song's track is not a start program's: the stop leaves it */
    sound_program_track_stop(s);
    CHECK(s->cd.busy && s->cd_song == 6);

    /* the WAV reader refuses another rate, mono and 8-bit data */
    {
        uint8_t w[48] = {0};
        memcpy(w, "RIFF", 4), memcpy(w + 8, "WAVEfmt ", 8), memcpy(w + 36, "data", 4);
        w[16] = 16, w[20] = 1, w[22] = 2, w[24] = 0x44, w[25] = 0xAC, w[34] = 16, w[40] = 4; /* 44,100 */
        const int16_t *pcm;
        uint32_t frames;
        CHECK(cd_wav_parse(w, sizeof w, &pcm, &frames) == 0 && frames == 1);
        w[24] = 0x22, w[25] = 0x56; /* 22,050 */
        CHECK(cd_wav_parse(w, sizeof w, &pcm, &frames) == -1);
        w[24] = 0x44, w[25] = 0xAC, w[22] = 1;
        CHECK(cd_wav_parse(w, sizeof w, &pcm, &frames) == -1);
        w[22] = 2, w[34] = 8;
        CHECK(cd_wav_parse(w, sizeof w, &pcm, &frames) == -1);
        w[34] = 16, w[20] = 3; /* not PCM */
        CHECK(cd_wav_parse(w, sizeof w, &pcm, &frames) == -1);
        w[20] = 1, w[40] = 200; /* a data chunk past the file */
        CHECK(cd_wav_parse(w, sizeof w, &pcm, &frames) == -1);
    }
    /* without the tracks (or with another music card) the songs play as modules */
    const char sb_cd[] = "DCARD     -SB16 | x\r\nMCARD     -SB   | x\r\n";
    write_file("SOUND.CFG", (const uint8_t *)sb_cd, sizeof sb_cd - 1);
    s = fresh();
    CHECK(!s->cd_mode && sound_program_track(s, 2) == 0 && !s->cd.busy); /* the start programs keep their modules */
    const char gm_cd[] = "DCARD     -SB16 | x\r\nMCARD     -GM   | x\r\n", rol_cd[] = "MCARD -ROL\r\n";
    write_file("SOUND.CFG", (const uint8_t *)gm_cd, sizeof gm_cd - 1);
    s = fresh();
    CHECK(s->mcard == 7 && !s->cd_mode);
    write_file("SOUND.CFG", (const uint8_t *)rol_cd, sizeof rol_cd - 1);
    s = fresh();
    CHECK(s->mcard == 6 && !s->cd_mode);
    write_file("SOUND.CFG", (const uint8_t *)cd_cfg, sizeof cd_cfg - 1);
    remove(DIR_ "/CDAUDIO/TRACK05.WAV");
    s = fresh();
    CHECK(s->mcard == 8 && !s->cd_mode);
    sound_load(s, 2);
    voice_start(&s->voice[0], s->sample[2], 600, 1);
    s->voice[0].row = 54, s->voice[0].step = 0x802C;
    sound_play(s, 175, 2, 0x140, 63, 1);
    CHECK(s->song == 175 && s->cd_song == 0);
    sound_interrupt(s);
    CHECK(s->voice[0].row == 0); /* the module ticks */

    remove(DIR_ "/DATA/ONHD");
    const char sb_file[] = "DCARD     -SB   | x\r\nMCARD     -SB   | x\r\n";
    write_file("SOUND.CFG", (const uint8_t *)sb_file, sizeof sb_file - 1);
}

static void test_null(void) {
    /* every entry point accepts a NULL Sound */
    sound_request(NULL, 1, 1, 1);
    sound_list(NULL, 0, 0);
    sound_clear(NULL);
    sound_help(NULL);
    sound_race(NULL, 1);
    sound_ring(NULL);
    sound_show(NULL, 1);
    sound_invoice(NULL, 1);
    sound_ambience(NULL, 0, 0);
    sound_show_music(NULL, 1);
    sound_background(NULL, 1, 1, 1);
    sound_main_loop(NULL, 1);
    sound_game_tick(NULL);
    sound_interrupt(NULL);
    sound_play(NULL, 1, 0, 1, 1, 0);
    CHECK(!sound_enabled(NULL));
}

int main(int argc, char **argv) {
    make_data();
    if (argc == 2 && !strcmp(argv[1], "show-music")) {
        test_ambience();
        test_ambience_raw_selection();
        test_show_music_raw();
        test_null();
        sound_free(&g_s);
        if (failures) return 1;
        puts("[OK] show-music and ambience controls");
        return 0;
    }
    test_loader();
    test_rate();
    test_rows();
    test_mixer();
    test_play();
    test_tick_credit();
    test_off_switches();
    test_writers();
    test_drain();
    test_sequences();
    test_background();
    test_background_u9();
    test_play_clear_snd15();
    test_ambience();
    test_ambience_raw_selection();
    test_show_music_raw();
    test_output();
    test_cd_audio();
    test_null();
    sound_free(&g_s);
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] sound tests passed\n");
    return 0;
}
