/* ENCODE.EXE, the CD video tutorial (docs/intro.md, "ENCODE.EXE - the CD video tutorial", "What the engine needs
 * (ENCODE)"; docs/cdrom.md, "Video tutorial: VPX/"). 13 scenes of the nurse on a 640 x 480 backdrop: per scene the
 * backdrop, the buttons, the scene's screen deltas (PSCENnn.DLT, scenes 2-13), the 328 x 248 frame and the 320 x 240
 * video with its sound; the English copy shows subtitles in scene 13. The video is clocked by the sound position as
 * the original reads the Sound Blaster's DMA address: one half-frame per 440 bytes (25 fps) or 733.33 bytes (15 fps),
 * i.e. an assumed 22,000 bytes/s, while the sound plays at 22,222 Hz.
 *
 * Time: the program runs on its own timer, 50 Hz at 25 fps, 30 Hz at 15 fps (42 or 70 units of 1/2100 s, intro.h).
 * The sound reaches the engine's mixer through the Sound's stream hook (sound.h), resampled to its 21,739 Hz. */
#ifndef BIING_TUTORIAL_H
#define BIING_TUTORIAL_H

#include "assets.h"
#include "intro.h"
#include "vpx.h"

struct Sound;

#define TUT_SCENES 13
#define TUT_SOUND_RATE 22222 /* bytes per second the sound card plays */
#define TUT_ASSUMED_RATE 22000 /* bytes per second the video clock assumes */
#define TUT_SCENE1_STOP 1214 /* scene clock (1/50 s) at which scene 1 freezes */
#define TUT_PTR_W 32
#define TUT_PTR_H 48
#define TUT_PTR_START_X 125
#define TUT_PTR_START_Y 375
#define TUT_PTR_YMIN 266
#define TUT_PTR_YMAX 479
#define TUT_BUTTON_W 80
#define TUT_BUTTON_H 35
#define TUT_BACKDAT_SIZE (768 + 640 * 480)
#define TUT_ICONDAT_SIZE (2 * 640 * 35)
#define TUT_POINTDAT_SIZE (4 * 2048)

enum { TUT_CMD_PREVIOUS = 1, TUT_CMD_NEXT = 2, TUT_CMD_GAME = 3, TUT_CMD_QUIT = 4 };

typedef struct {
    int cmd; /* the table entry's number is its command word */
    int normal, hover; /* ICONDAT images */
    int x, y; /* 80 x 35 */
} TutButton;

/* The buttons of a scene (1-13); english = the English copy (its scene 13 has no "next"). Returns their number. */
int tutorial_buttons(int scene, int english, TutButton out[6]);
/* The button under (x, y): x in (left, left + 80], y in (top, top + 35]; its index, or -1. */
int tutorial_hit(const TutButton *b, int n, int x, int y);
/* Video and frame position of a scene: video at (316, 236), scene 1 at (160, 24); the frame 4 pixels further out. */
void tutorial_video_pos(int scene, int *x, int *y);
/* Pointer x limit of a scene: 311, scene 1 639. */
int tutorial_pointer_xmax(int scene);
/* Pointer motion: mickeys x 2 added to a quarter-pixel position (1 mickey = half a pixel), clamped in quarter pixels to
 * x 0 .. 4 (xmax + 1) - 1 and y 1,064 .. 1,919 (docs/intro.md, "Run headless": pixels 0..xmax and 266..479, with 3
 * quarters of slack at the right and bottom). */
void tutorial_pointer_move(int *qx, int *qy, int mx, int my, int xmax);
/* Half-frames due after `bytes` of sound: bytes / 440 + 1 at 25 fps, bytes x 3 / 2,200 + 1 at 15 fps (the threshold
 * starts at 0, so the first is due at once; docs/intro.md, "Timing"). */
int tutorial_halves(uint64_t bytes, int fps);
/* One half-frame of the scene clock (1/50 s): + 1 at 25 fps; at 15 fps + 1, and one more while the program-wide word
 * *w is below 3 (then *w += 3), *w -= 2 per half-frame. With *w = 0 at program start the first scene's clock is
 * floor(5h / 3) + 1 after h half-frames (2, 4, 6, 7, 9 ...); the word is never reset, so later scenes carry its
 * phase (docs/intro.md, "Timing", measured 2026-10-20; engine formulation of the measured sequence). */
void tutorial_clock_step(int *clock, int *w, int fps);
/* Draws the 328 x 248 frame with its top-left at (x, y): one line of colour 2, one of 0x0C, two of 0, inside 2. */
void tutorial_draw_frame(uint8_t *screen, int x, int y);
/* ICONDAT image i (80 x 35, opaque) at (x, y): strip i / 8 at x (i % 8) x 80. */
void tutorial_draw_icon(uint8_t *screen, const uint8_t *icondat, int i, int x, int y);

/* The subtitle layout of the English scene 13 (docs/intro.md, "Subtitles"): ZEI1.FNT, space 6, letter gap 1, centred
 * in 320 pixels. Width of a line as drawn. */
int tutorial_text_width(const Font *f, const uint8_t map[256], const char *s);

typedef struct {
    char root[1024];
    struct Sound *sound;
    int quad; /* 25 fps files */
    int fps, tick_units;
    int english;
    int sound_clock; /* clocked by the sound (a sound card is configured); else one half-frame per tick */
    uint8_t *backdat, *icondat, *pointdat;
    Font font;
    int have_font;
    uint8_t map[256];
    Subtitles subs;
    int have_subs;

    int scene; /* 1-13 */
    uint8_t *vpx_raw;
    size_t vpx_n;
    Vpx vpx;
    uint8_t *dlt_raw;
    size_t dlt_n;
    Dlt dlt;
    TutButton button[6];
    int nbuttons;

    uint8_t screen[640 * 480];
    uint8_t back[640 * 480]; /* the off-screen copy of the backdrop (the subtitle bands) */
    uint8_t video[VPX_W * VPX_H];
    uint8_t dac[768];

    uint64_t units; /* since the scene start */
    uint64_t ticks; /* since the tutorial start (the pointer is redrawn every second tick) */
    uint64_t bytes; /* sound played in this scene */
    int halves; /* half-frames counted */
    int decoded; /* frames decoded in this scene */
    int clock; /* scene clock, 1/50 s */
    int next_dlt; /* the next DLT time to wait for */
    int next_sub; /* the next subtitle mark */
    int sub_line[3]; /* previous, current, next: subtitle indices, -1 none */
    int frozen; /* scene 1 after clock 1,214: no decoding, sound masked */
    uint64_t audio_out; /* output frames of sound rendered in this scene */
    int clock_word; /* the 15 fps clock's word: 0 at program start, never reset */

    /* The pointer (docs/intro.md, "Run headless"): read every second tick (the polls), drawn into the screen with the
     * bytes under it saved, clipped at x below xmax + 1 and y below 480. */
    int qx, qy; /* pointer, quarter pixels */
    int px, py; /* pointer position (as drawn at the last poll) */
    int held, latched; /* the button at the last poll; a poll with it down latches a press */
    int img; /* the image the next redraw uses: held (bit 0) */
    int shown; /* the pointer is in the screen */
    int ux, uy, uw, uh, uimg; /* where it was drawn, the saved size, the image drawn */
    uint8_t under[TUT_PTR_W * TUT_PTR_H];
    int hover; /* button index drawn hovered, -1 none */
    int done, quit; /* the game's turn; quit = to DOS (ends everything) */
} Tutorial;

/* Opens the tutorial at the start scene (1-13); quad = 25 fps files. 0 on success. */
int tutorial_open(Tutorial *t, const char *root, struct Sound *sound, int quad, int scene);
void tutorial_free(Tutorial *t);
/* Opens a scene (1-13); returns -1 if its files are missing. */
int tutorial_scene(Tutorial *t, int scene);
/* One unit of 1/2100 s; a tick of the program's timer every tick_units. Reads in->left and in->mx/my. */
void tutorial_advance(Tutorial *t, IntroInput *in);
/* One timer tick (tutorial_advance calls it), in the original's order (docs/intro.md, "Run headless", "One timer
 * interrupt"): the half-frames due and the clock; on every second tick the pointer poll (motion, redraw, button); if
 * at least 3 half-frames are due one frame decoded (else the tick ends here unless scene 1 is frozen); then hover,
 * a latched press's command, one DLT record and scene 1's stop test. Reads in->left and in->mx/my at the polls only
 * (the motion stays in `in` until then). */
void tutorial_tick(Tutorial *t, IntroInput *in);
void tutorial_render(const Tutorial *t, IntroScreen *s);
/* The sound: adds n output frames at the mixer's rate into out (interleaved stereo); the Sound's stream hook. */
void tutorial_audio(void *ctx, int16_t *out, int n);

#endif
