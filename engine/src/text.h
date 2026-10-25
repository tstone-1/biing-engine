/* Text output as specified in docs/gui.md, "Text output": the message buffer, the number format and the
 * three renderers (plain, left-aligned with wrap, centred with wrap). */
#ifndef BIING_TEXT_H
#define BIING_TEXT_H

#include "gfx.h"

typedef struct {
    const Font *font;
    const uint8_t *charmap; /* 256 bytes, character code -> glyph slot (BIPRO.EXE 0xB521) */
    int x, y; /* start of the first line */
    int width; /* box width for the wrapping renderers */
    int line_gap; /* extra pixels between lines: line height = font height + line_gap */
    int space; /* width of a space */
    int letter; /* pixels after every glyph; always 1 in the game */
} TextLayout;

/* Each renderer stops at '$', byte 0 or len, whichever comes first, and returns the number of lines. */
int text_plain(Surface *s, const TextLayout *l, const uint8_t *t, size_t len);
int text_left(Surface *s, const TextLayout *l, const uint8_t *t, size_t len);
int text_centred(Surface *s, const TextLayout *l, const uint8_t *t, size_t len);

/* The message buffer: pieces appended one after the other, no terminator kept. */
typedef struct {
    uint8_t buf[2048];
    size_t len;
} Message;

void msg_clear(Message *m);
void msg_append(Message *m, const uint8_t *p, size_t n);
/* Decimal, clamped to +-999,999,999, '-' for negatives, no leading zeros, one trailing space. */
void msg_append_number(Message *m, long v);
/* c over the last byte appended (the newspaper's ")" over a number's trailing space, 0x43527); appended to an empty
 * buffer. */
void msg_over(Message *m, uint8_t c);
/* The English CD's indefinite article (docs/editions.md 7.3, rule A), in place of the German article texts: "a",
 * then "n " when the name's first letter is a, e, i, o, u or y (either case), else `space` (text 386, " "). */
void msg_english_article(Message *m, const uint8_t *name, size_t n, const uint8_t *space, size_t space_len);

#endif
