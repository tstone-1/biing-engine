/* The tutorial's files (docs/cdrom.md, "Video tutorial: VPX/"; docs/intro.md, "ENCODE.EXE - the CD video tutorial"):
 * the VPX1 video with its sound, the screen deltas PSCENnn.DLT, and the subtitle file TEXT13.TXT. Screen-free: the
 * decoders write into caller buffers.
 *
 * VPX1: a 0x78-byte text, u16 width, height, fps, sample rate, "COLS" + 768 bytes of 8-bit palette, then chunks: "SOUN"
 * + u32 size + unsigned 8-bit mono PCM, "CODE" + 600 bytes (no size), "VIDE" + u32 size + block data. One frame = one
 * CODE and the VIDE after it. A frame is 40 x 30 blocks of 8 x 8 pixels, one 4-bit code per block (high nibble first,
 * row-major); VIDE holds the data of the non-zero blocks in order. */
#ifndef BIING_VPX_H
#define BIING_VPX_H

#include <stddef.h>
#include <stdint.h>

#define VPX_W 320
#define VPX_H 240
#define VPX_BW 40
#define VPX_BH 30
#define VPX_CODE_BYTES 600
#define VPX_FILL 2 /* the video buffer starts as colour 2 (docs/intro.md, "Video decoding") */

typedef struct {
    const uint8_t *code; /* 600 bytes */
    const uint8_t *data; /* the VIDE bytes */
    uint32_t n;
} VpxFrame;

typedef struct {
    int w, h, fps, rate;
    uint8_t pal[768]; /* 8-bit as stored */
    VpxFrame *frame;
    int nframes;
    uint8_t *pcm; /* every SOUN chunk in file order */
    size_t pcm_n;
} Vpx;

/* Parses a whole file (the frames point into d, which must outlive the Vpx); 0 on success. */
int vpx_parse(Vpx *v, const uint8_t *d, size_t n);
void vpx_free(Vpx *v);

/* Bytes of VIDE data a block code takes (table: 1:16, 2:32, 3:64, 4:1, 5:16, 6:32, 7:64; 0 and 8-15: 0). */
int vpx_code_bytes(int code);
/* Phase (row, column offset in the 8 x 8 block) of group g (0-3) in a frame of the given parity: even frames (0, 0),
 * (1, 1), (0, 1), (1, 0); odd frames (0, 1), (1, 0), (0, 0), (1, 1). */
void vpx_phase(int parity, int group, int *row, int *col);
/* Decodes one frame into the 320 x 240 buffer: code 0 keeps the block, 1 draws one 16-byte group on both phases of
 * the frame's pair (groups 1 and 2), 2 one group on each, 3 four groups on all four phases, 4 one colour on the pair;
 * codes 5-7 take their bytes and draw nothing (no file uses them). Returns the VIDE bytes used, -1 if the data runs
 * out. parity = frame number & 1, counted from 0 at the scene start. */
long vpx_decode(uint8_t *buf, const uint8_t code[VPX_CODE_BYTES], const uint8_t *data, size_t n, int parity);

/* PSCENnn.DLT: 0x50 bytes of text, 40 LE u16 times (1/50 s, ascending, FFFF-padded), from 0xA0 one record more than
 * there are times. A record runs over the 640-wide screen from offset 0: u16 skip and u16 copy counts in dwords, each
 * copy followed by its 4 x copy bytes; a zero word ends the record, except as the very first skip. */
#define DLT_TIMES 40
#define DLT_HEADER 0x50
#define DLT_DATA 0xA0
#define DLT_SCREEN_W 640
#define DLT_SCREEN_H 480

typedef struct {
    int ntimes;
    uint16_t time[DLT_TIMES];
    int nrec; /* ntimes + 1 */
    const uint8_t *rec[DLT_TIMES + 1];
    size_t rec_n[DLT_TIMES + 1];
} Dlt;

int dlt_parse(Dlt *t, const uint8_t *d, size_t n);
/* Draws one record onto a 640 x 480 screen; returns the bytes it takes, -1 if it is cut short or leaves the screen. */
long dlt_apply(const uint8_t *rec, size_t n, uint8_t *screen);

/* TEXT13.TXT: "*NNNN*" marks (text may follow the digits before the closing star), each followed by one text line
 * (CR LF); "*9999*" ends. A mark's time is (NNNN + 1) x 4 ticks of the 50 Hz scene clock. */
#define SUB_MAX 160
#define SUB_LINE 96

typedef struct {
    int n;
    int time[SUB_MAX];
    char text[SUB_MAX][SUB_LINE];
} Subtitles;

int subtitles_parse(Subtitles *s, const uint8_t *d, size_t n);

#endif
