/* The CD video tutorial (docs/intro.md, "ENCODE.EXE - the CD video tutorial"; docs/cdrom.md, "Video tutorial: VPX/"):
 * the VPX parser and the checkerboard decode, the DLT screen deltas, the subtitle file, the buttons and the pointer, the
 * sound clock, and the program flow (freeze of scene 1, records at their times, a click to a command, the end of a
 * video, quit and game, the subtitle band, the sound stream). Every fixture is built here: VPX, DLT, BACKDAT, ICONDAT
 * and POINTDAT files in a temporary directory with made-up pixels. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "portable.h"
#include "sound.h"
#include "startup.h"
#include "tutorial.h"

static int failures;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            failures++;                                                            \
        }                                                                          \
    } while (0)

/* ---- builders ---- */

typedef struct {
    uint8_t *d;
    size_t n, cap;
} Buf;

static void put(Buf *b, const void *p, size_t n) {
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2 + 1024;
        b->d = realloc(b->d, b->cap);
    }
    memcpy(b->d + b->n, p, n);
    b->n += n;
}
static void put16(Buf *b, unsigned v) {
    uint8_t x[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    put(b, x, 2);
}
static void put32(Buf *b, uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}
static void put_fill(Buf *b, uint8_t v, size_t n) {
    for (size_t i = 0; i < n; i++) put(b, &v, 1);
}

static void vpx_header(Buf *b, int fps) {
    char text[0x78];
    memset(text, ' ', sizeof text);
    memcpy(text, "VPX1  synthetic", 15);
    put(b, text, sizeof text);
    put16(b, 320), put16(b, 240), put16(b, (unsigned)fps), put16(b, 22222);
    put(b, "COLS", 4);
    for (int i = 0; i < 256; i++) {
        uint8_t c[3] = {(uint8_t)(i & 0xFC), (uint8_t)((255 - i) & 0xFC), 0x40};
        put(b, c, 3);
    }
}
static void soun(Buf *b, uint8_t v, uint32_t n) {
    put(b, "SOUN", 4), put32(b, n), put_fill(b, v, n);
}
/* A frame whose block 0 is code 4 in colour c (all other blocks keep). */
static void frame_solid(Buf *b, uint8_t c) {
    uint8_t code[VPX_CODE_BYTES] = {0x40};
    put(b, "CODE", 4), put(b, code, sizeof code);
    put(b, "VIDE", 4), put32(b, 1), put(b, &c, 1);
}

static void write_file(const char *dir, const char *rel, const void *d, size_t n) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, rel);
    FILE *f = fopen(path, "wb");
    if (!f) {
        CHECK(0);
        return;
    }
    fwrite(d, 1, n, f);
    fclose(f);
}

/* ---- the codec ---- */

static void test_codec(void) {
    static const int BYTES[16] = {0, 16, 32, 64, 1, 16, 32, 64, 0, 0, 0, 0, 0, 0, 0, 0};
    for (int c = 0; c < 16; c++) CHECK(vpx_code_bytes(c) == BYTES[c]);
    static const int EVEN[4][2] = {{0, 0}, {1, 1}, {0, 1}, {1, 0}}, ODD[4][2] = {{0, 1}, {1, 0}, {0, 0}, {1, 1}};
    for (int g = 0; g < 4; g++) {
        int r, c;
        vpx_phase(0, g, &r, &c);
        CHECK(r == EVEN[g][0] && c == EVEN[g][1]);
        vpx_phase(1, g, &r, &c);
        CHECK(r == ODD[g][0] && c == ODD[g][1]);
    }

    static uint8_t buf[VPX_W * VPX_H];
    uint8_t code[VPX_CODE_BYTES] = {0};
    uint8_t data[256];
    for (int i = 0; i < 256; i++) data[i] = (uint8_t)(100 + i % 150);
    /* blocks 0-4: codes 1, 2, 3, 4, 5 (high nibble first); block 41 (row 1, column 1): code 4 */
    code[0] = 0x12, code[1] = 0x34, code[2] = 0x50, code[20] = 0x04;
    memset(buf, 7, sizeof buf);
    long used = vpx_decode(buf, code, data, sizeof data, 0);
    CHECK(used == 16 + 32 + 64 + 1 + 16 + 1);
#define PX(bx, by, r, c) buf[((by) * 8 + (r)) * VPX_W + (bx) * 8 + (c)]
    /* code 1, even frame: group at (0, 0) and (1, 1); (0, 1) and (1, 0) keep */
    CHECK(PX(0, 0, 0, 0) == data[0] && PX(0, 0, 0, 2) == data[1] && PX(0, 0, 2, 0) == data[4] && PX(0, 0, 6, 6) == data[15]);
    CHECK(PX(0, 0, 1, 1) == data[0] && PX(0, 0, 7, 7) == data[15]);
    CHECK(PX(0, 0, 0, 1) == 7 && PX(0, 0, 1, 0) == 7 && PX(0, 0, 7, 6) == 7);
    /* code 2: the second group (bytes 16-31) on (1, 1) */
    CHECK(PX(1, 0, 0, 0) == data[16] && PX(1, 0, 1, 1) == data[32] && PX(1, 0, 7, 7) == data[47] && PX(1, 0, 0, 1) == 7);
    /* code 3: all four phases, group 3 on (0, 1), group 4 on (1, 0) */
    CHECK(PX(2, 0, 0, 0) == data[48] && PX(2, 0, 1, 1) == data[64] && PX(2, 0, 0, 1) == data[80] && PX(2, 0, 1, 0) == data[96]);
    CHECK(PX(2, 0, 7, 6) == data[96 + 15]);
    /* code 4: one colour on the pair */
    CHECK(PX(3, 0, 0, 0) == data[112] && PX(3, 0, 5, 5) == data[112] && PX(3, 0, 0, 1) == 7);
    /* code 5: bytes taken, nothing drawn */
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++) CHECK(PX(4, 0, r, c) == 7);
    CHECK(PX(1, 1, 0, 0) == data[113 + 16] && PX(0, 1, 0, 0) == 7 && PX(2, 1, 0, 0) == 7);
    /* the odd frame draws the other half: (0, 1) and (1, 0) */
    uint8_t one[1] = {9};
    uint8_t c4[VPX_CODE_BYTES] = {0x40};
    vpx_decode(buf, c4, one, 1, 1);
    CHECK(PX(0, 0, 0, 1) == 9 && PX(0, 0, 1, 0) == 9 && PX(0, 0, 6, 7) == 9 && PX(0, 0, 0, 0) == data[0]);
    /* data cut short */
    CHECK(vpx_decode(buf, code, data, 100, 0) == -1);
    uint8_t lone[VPX_CODE_BYTES] = {0};
    lone[VPX_CODE_BYTES - 1] = 0x04; /* the last block (39, 29), low nibble */
    vpx_decode(buf, lone, one, 1, 0);
    CHECK(buf[(29 * 8 + 7) * VPX_W + 39 * 8 + 7] == 9 && buf[(29 * 8) * VPX_W + 39 * 8 + 1] != 9);
#undef PX
}

static void test_vpx_parse(void) {
    Buf b = {0};
    vpx_header(&b, 25);
    soun(&b, 0x90, 300);
    frame_solid(&b, 11);
    soun(&b, 0x70, 100);
    frame_solid(&b, 12);
    Vpx v;
    CHECK(vpx_parse(&v, b.d, b.n) == 0);
    CHECK(v.fps == 25 && v.rate == 22222 && v.nframes == 2 && v.pcm_n == 400);
    CHECK(v.pcm[0] == 0x90 && v.pcm[299] == 0x90 && v.pcm[300] == 0x70 && v.pal[3] == 0x00 && v.pal[4] == 0xFC);
    CHECK(v.frame[1].n == 1 && v.frame[1].data[0] == 12 && v.frame[0].code[0] == 0x40);
    vpx_free(&v);
    CHECK(vpx_parse(&v, b.d, b.n - 1) != 0); /* cut short */
    memcpy(b.d + 0x384, "SOUX", 4);
    CHECK(vpx_parse(&v, b.d, b.n) != 0); /* unknown chunk */
    free(b.d);
}

/* ---- DLT ---- */

static Buf dlt_file(void) {
    Buf b = {0};
    put_fill(&b, ' ', DLT_HEADER);
    put16(&b, 10), put16(&b, 30);
    for (int i = 2; i < DLT_TIMES; i++) put16(&b, 0xFFFF);
    /* record 0: skip 0 (allowed first), copy 1 dword "AAAA", skip 1, copy 1 "BBBB", end */
    put16(&b, 0), put16(&b, 1), put(&b, "AAAA", 4), put16(&b, 1), put16(&b, 1), put(&b, "BBBB", 4), put16(&b, 0);
    /* record 1: to row 1 (160 dwords), copy 2 dwords, end */
    put16(&b, 160), put16(&b, 2), put(&b, "CCCCDDDD", 8), put16(&b, 0);
    /* record 2: a skip, then a zero copy ends it */
    put16(&b, 5), put16(&b, 0);
    return b;
}

static void test_dlt(void) {
    Buf b = dlt_file();
    Dlt t;
    CHECK(dlt_parse(&t, b.d, b.n) == 0);
    CHECK(t.ntimes == 2 && t.nrec == 3 && t.time[0] == 10 && t.time[1] == 30);
    CHECK(t.rec_n[0] == 18 && t.rec_n[1] == 14 && t.rec_n[2] == 4 && t.rec[2] + 4 == b.d + b.n);
    static uint8_t screen[640 * 480];
    memset(screen, 0, sizeof screen);
    CHECK(dlt_apply(t.rec[0], t.rec_n[0], screen) == 18);
    CHECK(!memcmp(screen, "AAAA", 4) && screen[4] == 0 && !memcmp(screen + 8, "BBBB", 4));
    dlt_apply(t.rec[1], t.rec_n[1], screen);
    CHECK(!memcmp(screen + 640, "CCCCDDDD", 8) && screen[639] == 0 && screen[648] == 0);
    CHECK(dlt_parse(&t, b.d, b.n - 1) != 0); /* the last record cut short */
    /* a copy that leaves the screen */
    uint8_t far[8] = {0xFF, 0xFF, 0x02, 0x00};
    CHECK(dlt_apply(far, 4, NULL) == -1);
    uint8_t edge[18] = {0xFF, 0xFF, 0x01, 0x00, 1, 2, 3, 4, 0xFF, 0xFF, 0x01, 0x00, 1, 2, 3, 4, 0, 0};
    CHECK(dlt_apply(edge, 18, NULL) == -1); /* 2 x 65,536 dwords + 1 > 640 x 480 bytes */
    edge[8] = 0, edge[9] = 0;
    CHECK(dlt_apply(edge, 18, NULL) == 10); /* the zero skip after the first ends it */
    free(b.d);
}

/* ---- subtitles ---- */

static void test_subtitles(void) {
    const char *txt = "*0000 a comment*\r\n \r\n*0021*\r\nYes, good\r\n*0040*\r\n\r\n*9999*\r\n-END-\r\n*9999*\r\n";
    Subtitles s;
    CHECK(subtitles_parse(&s, (const uint8_t *)txt, strlen(txt)) == 0);
    CHECK(s.n == 3 && s.time[0] == 4 && s.time[1] == 88 && s.time[2] == 164);
    CHECK(!strcmp(s.text[0], " ") && !strcmp(s.text[1], "Yes, good") && !strcmp(s.text[2], ""));
    const char *bad = "*00x1*\r\nx\r\n";
    CHECK(subtitles_parse(&s, (const uint8_t *)bad, strlen(bad)) != 0);
}

/* ---- buttons, pointer, clock ---- */

static void test_pieces(void) {
    TutButton b[6];
    CHECK(tutorial_buttons(1, 0, b) == 2 && b[0].cmd == 2 && b[0].normal == 8 && b[0].hover == 9 && b[0].x == 164 &&
          b[1].cmd == 3 && b[1].normal == 10 && b[1].x == 396 && b[1].y == 330);
    CHECK(tutorial_buttons(2, 0, b) == 3 && b[0].cmd == 2 && b[0].x == 112 && b[1].cmd == 3 && b[1].x == 214 &&
          b[2].cmd == 4 && b[2].y == 385 && b[2].normal == 6);
    for (int s = 3; s <= 13; s++) {
        CHECK(tutorial_buttons(s, 0, b) == 4 && b[0].cmd == 1 && b[0].x == 20 && b[0].normal == 0 && b[1].cmd == 2);
        CHECK(tutorial_buttons(s, 1, b) == (s == 13 ? 3 : 4));
    }
    tutorial_buttons(13, 1, b);
    CHECK(b[0].cmd == 1 && b[1].cmd == 3 && b[2].cmd == 4);
    tutorial_buttons(3, 0, b);
    CHECK(tutorial_hit(b, 4, 20, 340) == -1 && tutorial_hit(b, 4, 21, 340) == 0 && tutorial_hit(b, 4, 100, 340) == 0);
    CHECK(tutorial_hit(b, 4, 101, 340) == -1 && tutorial_hit(b, 4, 50, 330) == -1 && tutorial_hit(b, 4, 50, 331) == 0);
    CHECK(tutorial_hit(b, 4, 50, 365) == 0 && tutorial_hit(b, 4, 50, 366) == -1 && tutorial_hit(b, 4, 294, 420) == 3);

    int x, y;
    tutorial_video_pos(1, &x, &y);
    CHECK(x == 160 && y == 24);
    tutorial_video_pos(7, &x, &y);
    CHECK(x == 316 && y == 236);
    CHECK(tutorial_pointer_xmax(1) == 639 && tutorial_pointer_xmax(2) == 311);
    int qx = 500, qy = 1500;
    tutorial_pointer_move(&qx, &qy, 1, -1, 311); /* one mickey = half a pixel */
    CHECK(qx == 502 && qy == 1498 && qx / 4 == 125);
    tutorial_pointer_move(&qx, &qy, 1, 0, 311);
    CHECK(qx / 4 == 126);
    tutorial_pointer_move(&qx, &qy, 1000, -1000, 311);
    CHECK(qx == 4 * 312 - 1 && qy == 266 * 4);
    tutorial_pointer_move(&qx, &qy, -1000, 1000, 311);
    CHECK(qx == 0 && qy == 4 * 480 - 1);
    tutorial_pointer_move(&qx, &qy, 2000, 0, 639);
    CHECK(qx == 4 * 640 - 1);

    /* the quarter clamps leave 3 quarters of slack at the right and bottom (INT-6): one mickey back from the edge
     * stays on the edge pixel */
    tutorial_pointer_move(&qx, &qy, 1000, 1000, 311);
    CHECK(qx == 1247 && qy == 1919 && qx / 4 == 311 && qy / 4 == 479);
    tutorial_pointer_move(&qx, &qy, -1, -1, 311);
    CHECK(qx / 4 == 311 && qy / 4 == 479);
    tutorial_pointer_move(&qx, &qy, -1, -1, 311);
    CHECK(qx / 4 == 310 && qy / 4 == 478);

    /* the first half-frame is due at once (INT-9) */
    CHECK(tutorial_halves(0, 25) == 1 && tutorial_halves(439, 25) == 1 && tutorial_halves(440, 25) == 2 &&
          tutorial_halves(22000, 25) == 51);
    CHECK(tutorial_halves(733, 15) == 1 && tutorial_halves(734, 15) == 2 && tutorial_halves(22000, 15) == 31);
    /* the 15 fps clock (INT-8): floor(5h / 3) + 1 from the program start, the word's phase carried on */
    int clock = 0, w = 0;
    static const int SEQ[9] = {2, 4, 6, 7, 9, 11, 12, 14, 16};
    for (int h = 1; h <= 9; h++) {
        tutorial_clock_step(&clock, &w, 15);
        CHECK(clock == SEQ[h - 1] && clock == 5 * h / 3 + 1);
    }
    int c25 = 0, w25 = 0;
    for (int h = 0; h < 7; h++) tutorial_clock_step(&c25, &w25, 25);
    CHECK(c25 == 7 && w25 == 0);
    clock = 0; /* a later scene: the clock from 0, the word where it was: no extra in its first half-frame */
    tutorial_clock_step(&clock, &w, 15);
    CHECK(clock == 1);
    tutorial_clock_step(&clock, &w, 15);
    CHECK(clock == 3);

    static uint8_t screen[640 * 480];
    memset(screen, 99, sizeof screen);
    tutorial_draw_frame(screen, 312, 232);
    CHECK(screen[232 * 640 + 312] == 2 && screen[233 * 640 + 313] == 0x0C && screen[234 * 640 + 314] == 0 &&
          screen[235 * 640 + 315] == 0 && screen[236 * 640 + 316] == 2 && screen[479 * 640 + 639] == 2);
    CHECK(screen[233 * 640 + 400] == 0x0C && screen[478 * 640 + 400] == 0x0C && screen[300 * 640 + 638] == 0x0C &&
          screen[231 * 640 + 400] == 99 && screen[300 * 640 + 311] == 99);
    static uint8_t icons[TUT_ICONDAT_SIZE];
    for (size_t i = 0; i < sizeof icons; i++) icons[i] = (uint8_t)(i % 640 / 80 + 16 * (i / (640 * 35)));
    tutorial_draw_icon(screen, icons, 11, 10, 20); /* strip 1, x 240 */
    CHECK(screen[20 * 640 + 10] == 19 && screen[54 * 640 + 89] == 19 && screen[55 * 640 + 89] == 99 &&
          screen[20 * 640 + 90] == 99);
}

/* ---- the program ---- */

/* Use CTest's writable working directory, also on Windows where /tmp need not exist. */
static char dir[] = "biing_test_tutorial_XXXXXX";

static void make_dir(int frames1, int frames2) {
    char path[512];
    snprintf(path, sizeof path, "%s/VPX", dir);
    test_mkdir(path);
    Buf b = {0};
    for (int i = 0; i < 768; i++) put_fill(&b, (uint8_t)(i % 64), 1);
    for (int i = 0; i < 640 * 480; i++) put_fill(&b, (uint8_t)(20 + i % 5), 1); /* backdrop colours 20-24 */
    write_file(dir, "VPX/BACKDAT", b.d, b.n);
    b.n = 0;
    for (int i = 0; i < TUT_ICONDAT_SIZE; i++) put_fill(&b, (uint8_t)(30 + i % 640 / 80 + 8 * (i / (640 * 35))), 1);
    write_file(dir, "VPX/ICONDAT", b.d, b.n);
    b.n = 0;
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < 2048; i++) put_fill(&b, (uint8_t)(i < 1536 && i % 32 < 2 ? 60 + k : 0), 1);
    write_file(dir, "VPX/POINTDAT", b.d, b.n);
    static const int SCENES[5] = {1, 2, 3, 12, 13};
    for (int k = 0; k < 5; k++) {
        int s = SCENES[k];
        b.n = 0;
        vpx_header(&b, 15);
        soun(&b, 0xA0, 5000);
        for (int f = 0; f < (s == 1 ? frames1 : frames2); f++) frame_solid(&b, (uint8_t)(150 + f % 100));
        snprintf(path, sizeof path, "VPX/VSCEND%02d.VPX", s);
        write_file(dir, path, b.d, b.n);
        snprintf(path, sizeof path, "VPX/VSCENQ%02d.VPX", s);
        write_file(dir, path, b.d, b.n);
    }
    free(b.d);
    Buf d = dlt_file();
    write_file(dir, "VPX/PSCEN02.DLT", d.d, d.n);
    write_file(dir, "VPX/PSCEN03.DLT", d.d, d.n);
    write_file(dir, "VPX/PSCEN12.DLT", d.d, d.n);
    write_file(dir, "VPX/PSCEN13.DLT", d.d, d.n);
    free(d.d);
}

static void ticks(Tutorial *t, IntroInput *in, int n) {
    for (int i = 0; i < n * t->tick_units; i++) tutorial_advance(t, in);
}

static Tutorial tut;
static Startup st;

static void test_program(void) {
    if (!mkdtemp(dir)) {
        CHECK(0);
        return;
    }
    make_dir(800, 12);
    IntroInput in = {0};
    Tutorial *t = &tut;
    CHECK(tutorial_open(t, dir, NULL, 0, 1) == 0);
    CHECK(t->fps == 15 && t->tick_units == 70 && !t->sound_clock && t->nbuttons == 2 && !t->english);
    /* scene 1: backdrop, buttons, frame, video colour 2, VPX palette >> 2 */
    CHECK(t->screen[0] == 20 && t->screen[330 * 640 + 164] == 30 + 8 % 8 + 8 && t->screen[20 * 640 + 156] == 2 &&
          t->screen[21 * 640 + 157] == 0x0C && t->screen[24 * 640 + 160] == 2 && t->dac[3 * 255] == 63);
    ticks(t, &in, 2);
    CHECK(t->decoded == 0 && t->screen[24 * 640 + 160] == 2);
    ticks(t, &in, 1); /* without a sound card one half-frame per tick: the first frame at 3 */
    CHECK(t->decoded == 1 && t->screen[24 * 640 + 160] == 150 && t->screen[24 * 640 + 161] == 2);
    ticks(t, &in, 2);
    CHECK(t->decoded == 2 && t->screen[24 * 640 + 161] == 151 && t->screen[24 * 640 + 160] == 150);
    /* scene 1 freezes at clock 1,214, tested after the frame of a tick that decodes (INT-7): the 15 fps clock is
     * floor(5h / 3) + 1 (INT-8), 1,214 at half-frame 728, which decodes nothing; at 729 (1,216) frame 364 and the stop */
    ticks(t, &in, 728 - 5);
    CHECK(!t->frozen && t->clock == 1214 && t->decoded == 363);
    ticks(t, &in, 1);
    int at = t->decoded;
    CHECK(t->frozen && t->clock == 1216 && at == 364);
    ticks(t, &in, 50);
    CHECK(t->decoded == at && t->clock == 1216 && !t->done);

    /* the pointer, drawn into the screen at the polls (even ticks; INT-5, INT-6): from (125, 375) to the "video
     * explanation" button (164, 330 .. 244, 365); POINTDAT columns 0-1 are colour 60 (+ image) */
    CHECK(t->px == 125 && t->py == 375 && t->hover == -1 && t->shown && t->screen[375 * 640 + 125] == 60);
    in.mx = (200 - 125) * 2, in.my = (350 - 375) * 2;
    ticks(t, &in, 1); /* tick 780, a poll: moved; the frozen scene runs hover every tick: the pointer taken off */
    CHECK(t->px == 200 && t->py == 350 && t->hover == 0 && t->screen[330 * 640 + 164] == 30 + 9 % 8 + 8);
    CHECK(!t->shown && t->screen[375 * 640 + 125] != 60 && t->screen[397 * 640 + 201] != 60);
    IntroScreen *scr = malloc(sizeof *scr);
    ticks(t, &in, 1); /* no poll: still off */
    CHECK(!t->shown);
    ticks(t, &in, 1); /* the next poll draws it again, unmoved */
    tutorial_render(t, scr);
    CHECK(scr->w == 640 && t->shown && scr->px[350 * 640 + 200] == 60 && scr->px[350 * 640 + 202] == t->screen[350 * 640 + 202] &&
          scr->px[397 * 640 + 201] == 60);
    in.left = 1;
    ticks(t, &in, 1); /* tick 783: no poll, the press is not seen */
    CHECK(t->scene == 1 && !t->latched);
    ticks(t, &in, 1); /* tick 784, a poll: latched; the frozen scene's step 4 runs: command 2, scene 2 */
    in.left = 0;
    CHECK(t->scene == 2 && t->nbuttons == 3 && !t->frozen && t->decoded == 0 && !t->shown);
    /* scene 2: record 0 drawn at the start, record 1 once the clock exceeds 10 (15 fps: 7 half-frames = 11) */
    CHECK(!memcmp(t->screen, "AAAA", 4) && t->screen[640] != 'C' && t->screen[236 * 640 + 316] == 2);
    CHECK(t->px == 200 && t->qx / 4 == 200); /* the pointer is not reset; x 200 is inside the limit 311 */
    ticks(t, &in, 6);
    CHECK(t->clock == 10 && t->next_dlt == 0);
    ticks(t, &in, 1);
    CHECK(t->clock == 11 && t->next_dlt == 1 && !memcmp(t->screen + 640, "CCCCDDDD", 8));
    /* the video's end (12 frames, the last decoded at half-frame 25) acts as "next" at half-frame 27 */
    ticks(t, &in, 19);
    CHECK(!t->done && t->scene == 2 && t->decoded == 12);
    ticks(t, &in, 1);
    CHECK(t->scene == 3 && t->nbuttons == 4 && t->decoded == 0);
    /* "previous" from scene 3: the motion is read at the poll (scene tick 1), hover only in a tick that decodes (3) */
    in.mx = (50 - 200) * 2, in.my = (340 - 350) * 2;
    ticks(t, &in, 2);
    CHECK(t->px == 50 && t->hover == -1);
    ticks(t, &in, 1);
    CHECK(t->hover == 0 && t->button[0].cmd == TUT_CMD_PREVIOUS);
    in.left = 1;
    ticks(t, &in, 2); /* latched at the poll of scene tick 5, which decodes: the command at once */
    in.left = 0;
    CHECK(t->scene == 2 && t->decoded == 0);
    /* to scene 3 by its end, then scene 3's end: scene 4 is missing, so the tutorial ends */
    ticks(t, &in, 27 + 26);
    CHECK(t->scene == 3 && !t->done);
    ticks(t, &in, 1);
    CHECK(t->done && !t->quit);
    tutorial_free(t);

    /* the "to DOS" button through the start sequence: everything ends, the sound hook is taken back */
    static Sound snd;
    sound_init(&snd, dir, NULL, NULL, NULL);
    startup_open_tutorial(&st, dir, &snd, 2, 0);
    CHECK(snd.stream == tutorial_audio);
    CHECK(!startup_done(&st) && startup_stage(&st) == START_ENCODE && startup_tick_units(&st) == 70);
    int x, y;
    CHECK(startup_pointer(&st, &x, &y) == 0 && x == 125 && y == 375);
    IntroInput q = {0};
    q.mx = (250 - 125) * 2, q.my = (400 - 375) * 2;
    for (int i = 0; i < 2 * 70; i++) startup_advance(&st, &q);
    q.left = 1;
    for (int i = 0; i < 2 * 70; i++) startup_advance(&st, &q); /* held over a poll */
    q.left = 0;
    for (int i = 0; i < 2 * 70; i++) startup_advance(&st, &q);
    CHECK(startup_done(&st) && st.quit && snd.stream == NULL);
    startup_free(&st);
    /* "start the game" ends the tutorial only */
    startup_open_tutorial(&st, dir, NULL, 2, 1);
    CHECK(startup_tick_units(&st) == 42 && st.tut.fps == 25);
    q.mx = (250 - 125) * 2, q.my = (340 - 375) * 2;
    for (int i = 0; i < 2 * 42; i++) startup_advance(&st, &q);
    q.left = 1;
    for (int i = 0; i < 2 * 42; i++) startup_advance(&st, &q);
    q.left = 0;
    for (int i = 0; i < 2 * 42; i++) startup_advance(&st, &q);
    CHECK(startup_done(&st) && !st.quit);
    startup_free(&st);
    /* a click on no button does nothing */
    CHECK(tutorial_open(t, dir, NULL, 0, 2) == 0);
    in = (IntroInput){0};
    in.mx = -200;
    ticks(t, &in, 1); /* the motion is read at the polls only, every second tick; the pointer not drawn yet */
    CHECK(t->qx == 125 * 4 && t->px == 125 && in.mx == -200 && !t->shown && t->screen[375 * 640 + 125] != 60);
    ticks(t, &in, 1);
    CHECK(t->px == 25 && t->qx == 25 * 4 && in.mx == 0 && t->shown && t->screen[375 * 640 + 25] == 60);
    in.left = 1;
    ticks(t, &in, 1); /* a press released before the next poll is lost */
    in.left = 0;
    ticks(t, &in, 1);
    CHECK(!t->latched && t->img == 0);
    in.left = 1;
    ticks(t, &in, 2); /* seen at the poll: latched, the held image for the next redraw */
    CHECK(t->latched && t->img == 1 && t->screen[375 * 640 + 25] == 60);
    in.left = 0;
    ticks(t, &in, 1); /* the decode tick takes the latch: over no button nothing happens */
    CHECK(!t->latched && t->scene == 2 && !t->done && t->hover == -1);
    ticks(t, &in, 1); /* the poll redraws the unmoved pointer with the pressed image */
    CHECK(t->screen[375 * 640 + 25] == 61 && t->img == 0);
    ticks(t, &in, 2);
    CHECK(t->screen[375 * 640 + 25] == 60);
    /* the pointer is clipped at x 311 in scenes 2-13: at x 300 only 12 columns are drawn; the saved bytes put back */
    in.mx = (300 - 25) * 2;
    ticks(t, &in, 2);
    CHECK(t->px == 300 && t->uw == 12 && t->screen[375 * 640 + 300] == 60 && t->screen[375 * 640 + 25] != 60);
    in.mx = 1000;
    ticks(t, &in, 2);
    CHECK(t->px == 311 && t->uw == 1 && t->screen[375 * 640 + 311] == 60 && t->screen[375 * 640 + 312] != 60);
    /* keys are thrown away */
    in.key = 1;
    ticks(t, &in, 1);
    CHECK(!in.key && !t->done);
    tutorial_free(t);

    /* the sound clock: 22,222 bytes per second played, 440 per half-frame at 25 fps */
    CHECK(tutorial_open(t, dir, &snd, 1, 1) == 0);
    CHECK(t->sound_clock && snd.stream == tutorial_audio && snd.stream_ctx == t);
    ticks(t, &in, 50); /* one second */
    /* the first half-frame is due at once (INT-9): 22,222 / 440 + 1 = 51; one frame per tick at most: 25 */
    CHECK(t->bytes == 22222 && t->halves == 51 && t->clock == 51 && t->decoded == 25 && !t->done);
    tutorial_free(t);
    CHECK(snd.stream == NULL);
    CHECK(tutorial_open(t, dir, &snd, 0, 1) == 0);
    ticks(t, &in, 30);
    /* 15 fps: 22,222 x 3 / 2,200 + 1 = 31 half-frames, clock floor(5 x 31 / 3) + 1 = 52; frames at ticks 2, 4, ... 30 */
    CHECK(t->halves == 31 && t->clock == 52 && t->decoded == 15);
    t->vpx.pcm[0] = 0x80, t->vpx.pcm[1] = 0x90, t->vpx.pcm[2] = 0x70;
    t->audio_out = 0;
    int16_t out[2 * 64] = {0};
    out[0] = 100;
    tutorial_audio(t, out, 64);
    /* output frame i plays source sample i x 22,222 x 46 / 1,000,000: 0, 1, 2, 3 for i = 0, 1, 2, 3 */
    CHECK(out[0] == 100 && out[1] == 0 && out[2] == 0x10 * 256 && out[3] == 0x10 * 256 && out[4] == -0x10 * 256);
    CHECK(out[6] == 0x20 * 256 && t->audio_out == 64);
    t->frozen = 1;
    memset(out, 0, sizeof out);
    tutorial_audio(t, out, 64);
    CHECK(out[2] == 0 && t->audio_out == 64);
    tutorial_free(t);
    sound_free(&snd);

    /* one frame per tick, however many half-frames are due (no catching up in a loop) */
    CHECK(tutorial_open(t, dir, NULL, 1, 2) == 0);
    t->halves = 20;
    ticks(t, &in, 1);
    CHECK(t->halves == 21 && t->decoded == 1);
    ticks(t, &in, 1);
    CHECK(t->decoded == 2);
    tutorial_free(t);

    /* a held button repeats its command at every poll (INT-11): "next" from scene 12 opens scene 13, and held on
     * leaves scene 13 to the game */
    CHECK(tutorial_open(t, dir, NULL, 0, 12) == 0);
    in = (IntroInput){0};
    in.mx = (150 - 125) * 2, in.my = (340 - 375) * 2;
    ticks(t, &in, 2);
    in.left = 1;
    ticks(t, &in, 3); /* latched at the poll of tick 4, the command at the decode tick 5 */
    CHECK(t->scene == 13 && !t->done && t->nbuttons == 4);
    ticks(t, &in, 3); /* latched again at the polls of ticks 6 and 8; tick 8 decodes scene 13's first frame */
    in.left = 0;
    CHECK(t->done && !t->quit);
    tutorial_free(t);

    /* English scene 13: no "next", the subtitle band in the video's last rows (25 fps: clock = half-frames = ticks) */
    write_file(dir, "CDC", "BIING! CD\r\nEnglish texts\r\n", 26);
    CHECK(tutorial_open(t, dir, NULL, 1, 13) == 0);
    CHECK(t->english && !t->have_subs && t->nbuttons == 3 && t->button[1].cmd == 3);
    /* subtitles and a font injected (the loader needs PRE.EXE's map and ZEI1.FNT) */
    const char *txt = "*0000*\r\nab\r\n*0002*\r\nb\r\n*0004*\r\n \r\n*9999*\r\n";
    subtitles_parse(&t->subs, (const uint8_t *)txt, strlen(txt));
    static uint8_t ga[2 * 15], gb[3 * 15];
    memset(ga, 255, sizeof ga), memset(gb, 130, sizeof gb);
    t->font.height = 15, t->font.width[0] = 2, t->font.width[1] = 3, t->font.glyph[0] = ga, t->font.glyph[1] = gb;
    memset(t->map, 200, 256);
    t->map['a'] = 0, t->map['b'] = 1;
    t->have_subs = 1;
    CHECK(tutorial_text_width(&t->font, t->map, "ab b") == 3 + 4 + 6 + 4);
    /* video rows 221 and 237 marked (the frames only change block 0) */
    t->video[221 * 320 + 156] = 99, t->video[237 * 320 + 156] = 98;
    ticks(t, &in, 3); /* clock 3: before the first mark (time 4); frame 0 shown */
    CHECK(t->sub_line[1] == -1 && t->back[192 * 640 + 470] == 20 + (192 * 640 + 470) % 5);
    ticks(t, &in, 1); /* clock 4, but no frame shown: the mark waits (INT-12) */
    CHECK(t->sub_line[1] == -1);
    ticks(t, &in, 1); /* tick 5 shows a frame, then the mark */
    CHECK(t->sub_line[0] == -1 && t->sub_line[1] == 0 && t->sub_line[2] == 1);
    /* "ab" is 7 wide: x = 320 + 313 / 2 = 476, drawn at y 207 (current line) */
    CHECK(t->back[190 * 640 + 320] == 2 && t->back[234 * 640 + 639] == 2 && t->back[207 * 640 + 476] == 255 &&
          t->back[207 * 640 + 478] == 2 && t->back[207 * 640 + 479] == 130 && t->back[221 * 640 + 481] == 130 &&
          t->back[207 * 640 + 482] == 2 && t->back[189 * 640 + 320] != 2);
#define V(r) t->screen[(236 + (r)) * 640 + 316 + 156]
    CHECK(V(221) == 99 && V(237) == 98); /* the frame of tick 5 was put before the mark */
    ticks(t, &in, 2); /* the next shown frame: video rows 220-235 = backdrop rows 206-221, rows 236-239 = 220-223 */
    CHECK(V(219) == 2 && V(220) == 2 && V(221) == 255 && V(235) == 255);
    CHECK(V(236) == 2 && V(237) == 99 && V(239) == 2 && t->screen[(236 + 221) * 640 + 316 + 159] == 130);
    ticks(t, &in, 6); /* tick 13 (clock 13 >= 12): mark 1, "b"; only the current line is drawn (INT-12) */
    CHECK(t->sub_line[1] == 1 && t->sub_line[0] == 0 && V(221) == 255);
    CHECK(t->back[207 * 640 + 478] == 130 && t->back[195 * 640 + 476] == 2 && t->back[207 * 640 + 476] == 2);
    ticks(t, &in, 2);
    CHECK(V(221) == 2 && t->screen[(236 + 221) * 640 + 316 + 158] == 130 && V(237) == 99);
    ticks(t, &in, 8); /* tick 23: mark 2 at tick 21 is a " " line: no band (INT-12) */
    CHECK(t->sub_line[1] == 2 && V(221) == 99 && V(237) == 98 && t->screen[(236 + 221) * 640 + 316 + 158] != 130);
    /* a frame decoded but not shown (4 or more half-frames still due) takes no mark */
    t->next_sub = 0;
    t->halves = 2 * t->decoded + 9;
    ticks(t, &in, 1);
    CHECK(t->halves - 2 * t->decoded >= 4 && t->sub_line[1] == 2);
#undef V

    t->have_subs = 0;
    memset(&t->font, 0, sizeof t->font);
    tutorial_free(t);
    free(scr);

    CHECK(test_rmtree(dir) == 0);
}

int main(void) {
    test_codec();
    test_vpx_parse();
    test_dlt();
    test_subtitles();
    test_pieces();
    test_program();
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] tutorial tests passed\n");
    return 0;
}
