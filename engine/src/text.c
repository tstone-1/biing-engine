#include "text.h"

#include <stdio.h>
#include <string.h>

#define CR 0x0D

static int is_end(const uint8_t *t, size_t len, size_t i) { return i >= len || t[i] == '$' || t[i] == 0; }

static int slot_of(const TextLayout *l, uint8_t c) { return l->charmap[c]; }

static int advance(const TextLayout *l, uint8_t c) {
    if (c == ' ') return l->space;
    return glyph_width(l->font, slot_of(l, c)) + l->letter;
}

static int line_height(const TextLayout *l) { return l->font->height + l->line_gap; }

/* Draws one character at pen x (relative to l->x) and returns the new pen x. */
static int put(Surface *s, const TextLayout *l, uint8_t c, int pen, int y) {
    if (c != ' ') draw_glyph(s, l->font, slot_of(l, c), l->x + pen, y);
    return pen + advance(l, c);
}

int text_plain(Surface *s, const TextLayout *l, const uint8_t *t, size_t len) {
    int pen = 0, y = l->y, lines = 1;
    for (size_t i = 0; !is_end(t, len, i); i++) {
        if (t[i] == CR) {
            pen = 0;
            y += line_height(l);
            lines++;
        } else { /* every other byte through the character map, control codes included (docs/gui.md, "Plain") */
            pen = put(s, l, t[i], pen, y);
        }
    }
    return lines;
}

int text_left(Surface *s, const TextLayout *l, const uint8_t *t, size_t len) {
    int pen = 0, y = l->y, lines = 1;
    int after_hyphen = 0; /* the previous segment ended at a '_' */
    size_t i = 0;
    while (!is_end(t, len, i)) {
        /* 1. measure the segment up to the next space, control byte, end or '_' */
        size_t j = i;
        int w = 0;
        while (!is_end(t, len, j) && t[j] != ' ' && t[j] >= 0x20 && t[j] != '_') w += advance(l, t[j++]);
        /* 2. break first if it does not fit */
        if (pen > 0 && pen + w >= l->width) {
            if (after_hyphen) put(s, l, '-', pen, y);
            pen = 0;
            y += line_height(l);
            lines++;
        }
        /* 3. draw it, then handle what ended it */
        for (; i < j; i++) pen = put(s, l, t[i], pen, y);
        after_hyphen = 0;
        if (is_end(t, len, i)) break;
        if (t[i] == ' ') {
            pen += l->space;
        } else if (t[i] == '_') {
            after_hyphen = 1;
        } else if (t[i] == CR) {
            pen = 0;
            y += line_height(l);
            lines++;
        }
        i++;
    }
    return lines;
}

int text_centred(Surface *s, const TextLayout *l, const uint8_t *t, size_t len) {
    const int dash = advance(l, '-');
    int y = l->y, lines = 0;
    size_t start = 0;
    while (!is_end(t, len, start)) {
        /* 1. find the last allowed break of this line and the width D it leaves */
        size_t brk = 0, j = start;
        int found = 0, w = 0, d = 0, at_hyphen = 0, by_cr = 0;
        for (;; j++) {
            if (is_end(t, len, j) || t[j] == CR) {
                if (w < l->width) {
                    brk = j;
                    d = w + l->space;
                    at_hyphen = 0;
                    by_cr = !is_end(t, len, j);
                    found = 1;
                }
                break;
            }
            uint8_t c = t[j];
            if (c == ' ') {
                if (w >= l->width) break;
                w += l->space;
                brk = j + 1;
                d = w;
                at_hyphen = 0;
                found = 1;
            } else if (c == '_') {
                /* the test comes before the mark adds its letter spacing; every mark adds it, though nothing is
                 * drawn for it, and at a break D is that width (0xC517, docs/gui.md) */
                if (w + dash >= l->width) break;
                w += l->letter;
                brk = j;
                d = w;
                at_hyphen = 1;
                found = 1;
            } else if (c >= 0x20) {
                w += advance(l, c);
                if (c == '-' && w < l->width) {
                    brk = j + 1;
                    d = w + l->space;
                    at_hyphen = 0;
                    found = 1;
                }
            }
        }
        int h = at_hyphen ? dash : 0;
        if (!found) { /* no break point: D = 0, and the whole rest up to the next CR or the end goes on this line,
                       * over the edge; h from the byte before the line start (docs/gui.md) */
            brk = start;
            while (!is_end(t, len, brk) && t[brk] != CR) brk++;
            by_cr = !is_end(t, len, brk);
            d = 0;
            h = start > 0 && t[start - 1] == '_' ? dash : 0;
        }
        /* 2. indent so that the line without its trailing space is centred */
        int pen = (l->width - d - h + l->space) / 2;
        /* 3. draw up to the break; '_' only at the break, as '-' */
        for (size_t i = start; i < brk; i++) {
            if (t[i] == '_' || t[i] < 0x20) continue;
            pen = put(s, l, t[i], pen, y);
        }
        if (at_hyphen) put(s, l, '-', pen, y);
        lines++;
        y += line_height(l);
        size_t line_start = start;
        start = brk + (at_hyphen || by_cr ? 1 : 0);
        if (start <= line_start) start = line_start + 1; /* never loop on an empty line */
    }
    return lines;
}

void msg_clear(Message *m) { m->len = 0; }

void msg_append(Message *m, const uint8_t *p, size_t n) {
    if (n > sizeof m->buf - m->len) n = sizeof m->buf - m->len;
    memcpy(m->buf + m->len, p, n);
    m->len += n;
}

void msg_append_number(Message *m, long v) {
    if (v > 999999999L) v = 999999999L;
    if (v < -999999999L) v = -999999999L;
    char s[16];
    int n = snprintf(s, sizeof s, "%ld ", v);
    msg_append(m, (const uint8_t *)s, (size_t)n);
}

void msg_over(Message *m, uint8_t c) {
    if (m->len) m->buf[m->len - 1] = c;
    else msg_append(m, &c, 1);
}

void msg_english_article(Message *m, const uint8_t *name, size_t n, const uint8_t *space, size_t space_len) {
    msg_append(m, (const uint8_t *)"a", 1);
    int c = n ? name[0] | 0x20 : 0;
    if (n && (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y'))
        msg_append(m, (const uint8_t *)"n ", 2);
    else
        msg_append(m, space, space_len);
}
