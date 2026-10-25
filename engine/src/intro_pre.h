/* PRE.EXE, the scrolling disclaimer (docs/intro.md, "PRE.EXE - the disclaimer", "What the engine needs (PRE)").
 * The text, its character map and the cut point come from the user's PRE.EXE; the font is DATA/ZEI1.FNT, the
 * palette DATA/COL. Mode 13h, a 60 Hz timer, one strip line every 6 ticks. */
#ifndef BIING_INTRO_PRE_H
#define BIING_INTRO_PRE_H

#include "assets.h"
#include "intro.h"

#define PRE_STRIP_W 320
#define PRE_STRIP_H 1400
#define PRE_TEXT_TOP 180 /* strip line of the first text line */
#define PRE_LINE_ADVANCE_EXTRA 2 /* line advance = glyph height + 2 */
#define PRE_SPACE_W 8
#define PRE_LIMIT 320 /* line width limit; >= counts as too wide */
#define PRE_TICKS_PER_LINE 6
#define PRE_MARKER_CUT 120 /* lines the end moves up with the marker */

/* Which copy of PRE.EXE: program offset = file offset - base, text at 0x0ADA, character map at 0x0989; the cut is
 * the second CR after the paragraph that the marker drops (German 0x1055, English CD 0x0FF8); the window stops at
 * line 1,115 (English 1,075), 120 lines earlier with the marker. */
typedef struct {
    const uint8_t *text; /* 0-terminated CP437, CR line ends */
    size_t len; /* up to the end mark (the cut applied when marker) */
    const uint8_t *map; /* 256 bytes: character code -> glyph slot */
    int end_line; /* the window's top line at which the scroll stops */
    int english;
} PreText;

/* Finds the text in a PRE.EXE image (German/disk base 0x11C0, English CD base 0x1130); 0 on success. */
int pre_find_text(const uint8_t *exe, size_t n, int marker, PreText *out);

/* One laid-out line: bytes [start, end) of the text drawn (a soft hyphen at a break drawn as '-'), its width and x. */
typedef struct {
    int start, end;
    int hyphen; /* the line ends at a '_' used as a break: a '-' is drawn after the line */
    int width, x;
} PreLine;

/* Width of a character as the layout counts it: a glyph's stored width + 1, a space 8. */
int pre_char_width(const Font *f, const uint8_t map[256], uint8_t c);
/* Lays the text out into lines (the break rules of docs/intro.md, "Layout"); returns the number of lines. */
int pre_layout(const uint8_t *text, size_t len, const Font *f, const uint8_t map[256], PreLine *lines, int max);
/* Renders the lines into a strip of PRE_STRIP_W x strip_h filled with colour 1, from strip line PRE_TEXT_TOP. */
void pre_render_strip(uint8_t *strip, int strip_h, const uint8_t *text, const PreLine *lines, int nlines,
                      const Font *f, const uint8_t map[256]);

typedef struct {
    uint8_t *exe;
    size_t exe_n;
    Font font;
    PreText text;
    uint8_t strip[PRE_STRIP_W * PRE_STRIP_H];
    uint8_t dac[768];
    int marker; /* the marker existed at the start: skip allowed, text cut */
    uint64_t unit; /* units of 1/2100 s since the start */
    int tick_count; /* the 60 Hz counter that is reset at 6 */
    int ticks; /* 60 Hz ticks since the start */
    int window; /* the strip line the next copy shows */
    int shown; /* the strip line on screen, -1 before the first copy */
    int copies;
    int done;
} Pre;

/* The marker path (docs/intro.md, "Marker"): C:\BIING!\SECRET.MRK when a file CDC is in the current directory,
 * else SECRET.MRK there. The engine's current directory and C:\BIING! are both the game directory, so both
 * cases give root/SECRET.MRK. */
void pre_marker_path(const char *root, char *out, size_t n);
int pre_marker_exists(const char *root);
/* Creates the marker with its ten bytes "SECRET.MRK" (the DOS hidden attribute has no equivalent here). */
int pre_marker_write(const char *root);

/* Opens PRE with the marker state given (the caller decides; the start sequence reads it and writes the marker before
 * the text, as PRE does). */
int pre_open(Pre *p, const char *root, int marker);
void pre_free(Pre *p);
/* One unit of 1/2100 s, with the input polled at each 60 Hz tick. */
void pre_advance(Pre *p, IntroInput *in);
void pre_render(const Pre *p, IntroScreen *s);

#endif
