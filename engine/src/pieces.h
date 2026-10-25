/* A text as the original builds it in its message buffer, kept as a list of pieces so that the rules that choose
 * the texts (the daily report, the work advice, the newspaper; docs/ui-reports.md) work without the text database
 * and the tests can read which BITEXPZ entries, numbers and names a rule chose. The screens turn the pieces into
 * the message buffer (reports_ui.c). */
#ifndef BIING_PIECES_H
#define BIING_PIECES_H

#include <stdint.h>

enum {
    PC_TEXT, /* BITEXPZ entry v */
    PC_NUM, /* a number in the format of docs/gui.md (one trailing space) */
    PC_PERSON, /* the name of person v (person-name pool) */
    PC_STAFF, /* the name of employee v (staff-name pool) */
    PC_PROFESSION, /* the profession of person v (pool by person field 1) */
    PC_HOSPITAL, /* the name of hospital v (0x3BCF + 30 v, '$'-terminated) */
    PC_RAW, /* the literal string s */
    PC_NOUN, /* BITEXPZ entry v as a noun; flag 1: the plural "n" rule of docs/ui-reports.md, {F}/{E} */
    PC_ITEM_NAME, /* the name in the bytes 29 on of newspaper item v (0-99), to the '$' */
    PC_ARTICLE_EN, /* the English CD's article for the noun BITEXPZ entry v: "a", then "n " or text 386 (rule A) */
    PC_OVER, /* the character s[0] written over the last byte so far (the newspaper's ")" over a number's trailing
              * space, docs/ui-reports.md, "{bwh} in parentheses"); appended when the text is still empty */
};

typedef struct {
    uint8_t kind, flag;
    int32_t v;
    const char *s;
} Piece;

#define PIECES_MAX 200
typedef struct {
    int n;
    Piece p[PIECES_MAX];
} Pieces;

static inline void pieces_clear(Pieces *t) { t->n = 0; }
static inline void pieces_add(Pieces *t, int kind, int32_t v, int flag, const char *s) {
    if (t->n >= PIECES_MAX) return;
    t->p[t->n++] = (Piece){(uint8_t)kind, (uint8_t)flag, v, s};
}
static inline void pc_text(Pieces *t, int entry) { pieces_add(t, PC_TEXT, entry, 0, 0); }
static inline void pc_num(Pieces *t, long v) { pieces_add(t, PC_NUM, (int32_t)v, 0, 0); }
static inline void pc_person(Pieces *t, int index) { pieces_add(t, PC_PERSON, index, 0, 0); }
static inline void pc_staff(Pieces *t, int index) { pieces_add(t, PC_STAFF, index, 0, 0); }
static inline void pc_profession(Pieces *t, int index) { pieces_add(t, PC_PROFESSION, index, 0, 0); }
static inline void pc_hospital(Pieces *t, int h) { pieces_add(t, PC_HOSPITAL, h, 0, 0); }
static inline void pc_raw(Pieces *t, const char *s) { pieces_add(t, PC_RAW, 0, 0, s); }
static inline void pc_noun(Pieces *t, int entry, int plural) { pieces_add(t, PC_NOUN, entry, plural, 0); }
static inline void pc_item_name(Pieces *t, int item) { pieces_add(t, PC_ITEM_NAME, item, 0, 0); }
static inline void pc_article_en(Pieces *t, int noun) { pieces_add(t, PC_ARTICLE_EN, noun, 0, 0); }
static inline void pc_over(Pieces *t, const char *s) { pieces_add(t, PC_OVER, 0, 0, s); }

#endif
