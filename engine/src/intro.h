/* What the shared parts of the programs before the game need (docs/intro.md, "Shared facts"): one picture in a
 * video mode with its 6-bit DAC, the clock unit, the input a program polls, the sprites with header of DATA/L and
 * DATA/N (docs/formats-data.md, "Sprite with header"), the 256-step sine circle from a quarter wave, and the
 * conversion of a picture to the 640 x 480 display. Screen-free and device-free.
 *
 * Time: the programs run on three hardware clocks, the 50 Hz and 60 Hz timers and the 70 Hz retrace of mode 13h.
 * The engine advances every program in units of 1/2100 s (2,100 = the least common multiple of 50, 60 and 70), so
 * each clock is an exact number of units: 42 for 50 Hz, 35 for 60 Hz, 30 for a retrace. */
#ifndef BIING_INTRO_H
#define BIING_INTRO_H

#include <stddef.h>
#include <stdint.h>

#define INTRO_UNITS 2100 /* per second */
#define INTRO_U50 42
#define INTRO_U60 35
#define INTRO_U70 30
#define INTRO_MAX_W 640
#define INTRO_MAX_H 480

/* A picture as the video card shows it: w x h colour indices (320 x 200 in mode 13h, 640 x 480 in VESA mode 0x101,
 * text mode shown as a black 320 x 200) and the DAC, 256 x 3 values of 0-63. */
typedef struct {
    int w, h;
    uint8_t px[INTRO_MAX_W * INTRO_MAX_H];
    uint8_t dac[768];
} IntroScreen;

/* Input as the programs read it: key = a key was pressed since the last poll (the DOS key-available test), left and
 * right = the mouse buttons held now (int 33h function 3), mx and my = mouse motion in mickeys since the last read
 * (int 33h function 0Bh; the tutorial's pointer, which clears them when it reads them). */
typedef struct {
    int key;
    int left, right;
    int mx, my;
} IntroInput;

/* Converts the picture to the 640 x 480 display (0xFFRRGGBB). A 320 x 200 mode fills the display as the monitor
 * does: each pixel 2 wide, each row 2.4 rows high (display row y shows mode row y x 200 / 480); 640 x 480 is 1:1.
 * The 6-bit DAC values are widened to 8 bits as the game screens are (v << 2 | v >> 4). Engine choice: the
 * original switched video modes on a 4:3 monitor (docs/intro.md, BINT item 10). */
void intro_compose(const IntroScreen *s, uint32_t *display);

/* Sprite with header (DATA/L/I00 ..., DATA/N/<3 chars>): flag word (0x8000 = colour 0 transparent), plane field, width,
 * height, x, y (big-endian), w x h pixel bytes, a pad byte when w x h is odd, then an optional 96- or 768-byte 8-bit
 * palette (by the file size): the file's last 96 or 768 bytes. */
typedef struct {
    uint16_t flags, planes;
    int w, h, x, y;
    uint8_t *px;
    int pal_n; /* palette entries: 0, 32 or 256 */
    uint8_t pal[768]; /* 8-bit RGB as stored */
} HSprite;

int hsprite_parse(HSprite *s, const uint8_t *d, size_t n);
int hsprite_load(HSprite *s, const char *path);
void hsprite_free(HSprite *s);

/* The path of intro resource number n under DATA/N: three characters n / 100, n / 10 % 10, n % 10, with values
 * above 9 written as letters (1,700 = "H00"); docs/formats-data.md, "Intro script". */
void intro_resource_name(int n, char out[4]);

/* Bit reversal of a byte (the DATA/L sprites store plane 0 in bit 7). */
uint8_t intro_bitrev(uint8_t b);

/* A 256-step circle from a 65-entry quarter wave q[0..64] (0 ... 32767): sin(a) for a = 0 ... 255. */
void intro_sine_circle(const int16_t q[65], int16_t out[256]);
/* The quarter wave the programs use (round(32767 sin(i pi / 128)), i = 0 ... 64). */
const int16_t *intro_quarter(void);

/* Reads a whole file under the game directory (root/rel); 0 on success. */
int intro_read(const char *root, const char *rel, uint8_t **data, size_t *n);

#endif
