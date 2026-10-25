/* The price screen 122 ("Kostenschlüssel", docs/ui-prices.md): the panel with the eight price lines, the comparison
 * text and the back thumb. The rules are in prices.c; this file draws. */
#include "prices_ui.h"

#include "prices.h"

enum { IMG_ADMIN_HOVER = 1097, IMG_DARK = 1089, IMG_PANEL = 1088, IMG_THUMB = 1095, IMG_TEXT_BOX = 1098,
       IMG_NUMBER = 1093, SCR_PRICES = 122, FONT = 7 };

/* x, y offsets of each line's number window and text field (the screen's order) */
static const struct { int16_t dx, dy; } LINE[PRICE_LINES] = {{0, 0}, {0, 47}, {0, 94}, {0, 141},
                                                              {218, 0}, {218, 47}, {218, 94}, {218, 141}};
static const struct { int16_t entry, x, y, width; } LABEL[9] = {
    {131, 117, 70, 420}, {132, 117, 102, 140}, {133, 117, 149, 140}, {134, 117, 196, 140}, {135, 117, 243, 140},
    {136, 335, 102, 140}, {137, 335, 149, 140}, {138, 335, 196, 140}, {139, 335, 243, 140}};

static TextLayout layout(Biing *b, int x, int y, int width, int gap) {
    return (TextLayout){&b->fonts[FONT], b->prog.charmap, x, y, width, gap, 4, 1}; /* font 7, space width 4 */
}

static void left(Biing *b, int entry, int x, int y, int width) { /* 0x358A1, line spacing 2 */
    msg_clear(&b->msg);
    biing_msg_entry(b, entry);
    TextLayout l = layout(b, x, y, width, 2);
    text_left(&b->screen, &l, b->msg.buf, b->msg.len);
}

static void comparison(Biing *b) { /* 0x48D48 */
    dulog_draw(&b->ui, IMG_TEXT_BOX, 0, 0);
    left(b, prices_comparison(&b->sim), 117, 290, 365);
}

static void numbers(Biing *b) { /* 0x48DF0 */
    for (int i = 0; i < PRICE_LINES; i++) {
        dulog_draw(&b->ui, IMG_NUMBER, LINE[i].dx, LINE[i].dy);
        msg_clear(&b->msg);
        msg_append_number(&b->msg, prices_value(&b->sim, i)); /* "N ": the number and one blank */
        TextLayout l = layout(b, 269 + LINE[i].dx, 110 + LINE[i].dy, 30, 1);
        text_centred(&b->screen, &l, b->msg.buf, b->msg.len);
    }
}

static void entry(Biing *b) { /* 0x48B0D */
    dulog_draw(&b->ui, IMG_ADMIN_HOVER, 0, 0);
    dulog_draw(&b->ui, IMG_DARK, 0, 0);
    dulog_draw(&b->ui, IMG_PANEL, 0, 0);
    dulog_draw(&b->ui, IMG_THUMB, 0, 0);
    for (int i = 0; i < 9; i++) left(b, LABEL[i].entry, LABEL[i].x, LABEL[i].y, LABEL[i].width);
    comparison(b);
    numbers(b);
    dulog_set_screen(&b->ui, SCR_PRICES);
}

static void change(Biing *b, int line, int delta) { /* 0x49090 + 6 i / 0x49046 + 6 i */
    if (!prices_change(&b->sim, line, delta)) return; /* at a limit: no text, no redraw */
    if (delta > 0) comparison(b), numbers(b);
    else numbers(b), comparison(b);
}

int prices_routine(Biing *b, uint32_t r) {
    if (r == 0x48B0D) {
        entry(b);
        return 1;
    }
    if (r == 0x49007) { /* back: the news rule, then administration */
        prices_back(&b->sim);
        b->ui.call(&b->ui, 0x2EDEE);
        return 1;
    }
    if (r >= 0x49090 && r <= 0x490BA && (r - 0x49090) % 6 == 0) return change(b, (int)(r - 0x49090) / 6, 1), 1;
    if (r >= 0x49046 && r <= 0x49070 && (r - 0x49046) % 6 == 0) return change(b, (int)(r - 0x49046) / 6, -1), 1;
    return 0;
}
