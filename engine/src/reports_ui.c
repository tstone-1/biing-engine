/* The screens of docs/ui-reports.md: the daily report 100, the advice board 2 with its four entries and the jump table
 * of its thumbs-up, and the newspaper 118. The rules are in report.c, advice.c and newspaper.c; this file draws and
 * turns their pieces into the message buffer. The sounds of docs/sound.md: the report's list pair, the help music
 * of the board and the newspaper's sound. */
#include "reports_ui.h"

#include <string.h>

#include "advice.h"
#include "newspaper.h"
#include "personnel.h"
#include "prices.h"
#include "purchase.h"
#include "report.h"
#include "sim_internal.h"

#define NAMES 0x3BCF
#define NAME_SIZE 30
enum { IMG_PARCHMENT_REPORT = 1290, IMG_BOARD = 1001, IMG_BOARD_THUMB = 1003, IMG_PAGE = 1069, IMG_NO_PAPER = 518,
       IMG_AGENT = 87, IMG_PERSONNEL = 133, IMG_ADMIN = 104, IMG_PATHOLOGY = 237, IMG_BANK = 192, IMG_WAREHOUSE = 181,
       IMG_KITCHEN = 126, IMG_GARAGE = 169, IMG_GOLF = 243, IMG_CLUB = 123 };
enum { SCR_ADVICE = 2, SCR_MESSAGE = 77, SCR_REPORT = 100, SCR_NEWSPAPER = 118, SCR_CAMPAIGN = 119, SCR_PRICES = 122 };
enum { TXT_NO_PAPER = 517, TXT_WORK_TITLE = 2442, TXT_BEGINNER_TITLE = 2443 };

static Sim *sim(Biing *b) { return &b->sim; }
static void draw(Biing *b, int image) { dulog_draw(&b->ui, image, 0, 0); }
static void call(Biing *b, uint32_t r) { b->ui.call(&b->ui, r); }
static uint8_t *pb(Biing *b) { return b->world.block[BLK_PLAYER0]; }

/* ---- pieces into the message buffer ------------------------------------------------------------------- */

static void append(Biing *b, const uint8_t *p, size_t n) {
    if (p) msg_append(&b->msg, p, n);
}
static void pool(Biing *b, int which, int i) {
    size_t n;
    const uint8_t *p = datadb_pool_entry(&b->people, which, i, &n);
    append(b, p, n);
}
static void hospital_name(Biing *b, int h) {
    if (h < 0 || h > 16) return;
    const uint8_t *p = program_table(&b->prog, NAMES) + h * NAME_SIZE;
    size_t n = 0;
    while (n < NAME_SIZE - 1 && p[n] != '$' && p[n]) n++;
    append(b, p, n);
}

void reports_render(Biing *b, const Pieces *t) {
    msg_clear(&b->msg);
    for (int i = 0; i < t->n; i++) {
        const Piece *p = &t->p[i];
        switch (p->kind) {
        case PC_TEXT: biing_msg_entry(b, p->v); break;
        case PC_NUM: msg_append_number(&b->msg, p->v); break;
        case PC_PERSON: pool(b, DAT_PATIENT_NAMES, p->v); break;
        case PC_STAFF: pool(b, DAT_STAFF_NAMES, p->v); break;
        case PC_PROFESSION: pool(b, DAT_PROFESSIONS, b->world.block[BLK_PATIENTS][(p->v % 1009) * 106 + 1]); break;
        case PC_HOSPITAL: hospital_name(b, p->v); break;
        case PC_RAW: append(b, (const uint8_t *)p->s, strlen(p->s)); break;
        case PC_NOUN: {
            size_t n;
            const uint8_t *s = textdb_text(&b->texts, p->v, &n);
            while (n && (s[n - 1] == '$' || !s[n - 1])) n--;
            if (!p->flag) {
                append(b, s, n);
            } else {
                uint8_t pl[512];
                append(b, pl, news_plural(s, n, pl, sizeof pl));
            }
            break;
        }
        case PC_ARTICLE_EN: biing_english_article(b, p->v); break;
        case PC_OVER: msg_over(&b->msg, (uint8_t)p->s[0]); break;
        case PC_ITEM_NAME: { /* the name copy of the death items (bytes 29 on, 0x4C8D6); an empty copy prints nothing */
            const uint8_t *it = sim_news_item(sim(b), p->v);
            size_t n = 0;
            while (29 + n < 70 && it[29 + n] && it[29 + n] != '$') n++;
            append(b, it + 29, n);
            break;
        }
        }
    }
}

static int render_with(Biing *b, int font, int space, int gap, int x, int y, int width, int letter) {
    TextLayout l = {&b->fonts[font], b->prog.charmap, x, y, width, gap, space, letter};
    return text_left(&b->screen, &l, b->msg.buf, b->msg.len);
}
static int render_left(Biing *b, int font, int space, int gap, int x, int y, int width) {
    return render_with(b, font, space, gap, x, y, width, 1);
}
/* The newspaper's blocks, with the 1-pixel letter gap of docs/gui.md: every line break of the day-19 page matches a
 * screenshot of the original (docs/ui-reports.md, section 3). */
static int render_paper(Biing *b, int font, int space, int gap, int x, int y, int width) {
    return render_with(b, font, space, gap, x, y, width, 1);
}

/* ---- the daily report 0x68A45 ---------------------------------------------------------------------------- */

void reports_daily(Biing *b) {
    personnel_shuffle(sim(b), b->staff_list); /* 0x1A384: 12,000 draws */
    Report r;
    report_build(sim(b), &r);
    draw(b, IMG_PARCHMENT_REPORT);
    reports_render(b, &r.heading);
    render_left(b, 4, 5, 1, 158, 12, 520);
    static const int Y[4] = {44, 124, 227, 320};
    for (int i = 0; i < 4; i++) {
        if (!r.shown[i]) continue;
        reports_render(b, &r.para[i]);
        render_left(b, 5, 4, 1, 158, Y[i], 520);
        draw(b, r.face[i]);
    }
    draw(b, r.overall);
    dulog_set_screen(&b->ui, SCR_REPORT);
    /* 0x69503-0x69537: the list pair by the total points */
    static const uint32_t LISTS[3][2] = {{0x57A4, 0x57D4}, {0x5804, 0x5834}, {0x5864, 0x5894}};
    sound_list(&b->sound, 0, LISTS[r.sound][0]);
    sound_list(&b->sound, 1, LISTS[r.sound][1]);
}

/* ---- the advice board, screen 2 ---------------------------------------------------------------------------- */

static void board(Biing *b, int entry) {
    Advice a;
    advice_walk(sim(b), entry, &a);
    b->advice_from = a.back;
    draw(b, IMG_BOARD);
    draw(b, IMG_BOARD_THUMB);
    msg_clear(&b->msg);
    biing_msg_entry(b, entry == ADVICE_CHIEF ? TXT_BEGINNER_TITLE : TXT_WORK_TITLE);
    render_left(b, 0, 9, 2, 136, 27, 370);
    reports_render(b, &a.text);
    render_left(b, 0, 9, 2, 136, 60, 370);
    dulog_set_screen(&b->ui, SCR_ADVICE);
    if (!sound_board_blocked(&b->sound)) { /* 0x61779, 0x6178F: the help music */
        sound_clear(&b->sound);
        sound_request(&b->sound, 2, 0x1A4, 30);
    }
}

static void strip(Biing *b) { /* 0x5B4D1 */
    if (b->clock_stopped) return;
    uint8_t *h = world_hospital(&b->world, 0);
    if (b->ui.current == SCR_CAMPAIGN) memset(h + 0xF4, 0, 0x102 - 0xF4); /* the campaign being chosen is dropped */
    if (b->ui.current == SCR_PRICES) prices_back(sim(b)); /* the back thumb's price news */
    board(b, ADVICE_STRIP);
}

/* 0x61B68 */
static void back(Biing *b) {
    sim(b)->advice_pending = 0;
    if (b->advice_from == 2) call(b, 0x3724E);
    else if (b->advice_from == 0) biing_office(b);
    else call(b, 0x33D58);
}

/* the jump table's shorthands */
static void rent(Biing *b, int k) { draw(b, IMG_AGENT), call(b, 0x67DA2 + 4u * (uint32_t)(k - 1)); }
static void ad(Biing *b, int g) { draw(b, IMG_PERSONNEL), call(b, 0x3A555 + 6u * (uint32_t)(g - 1)); }
static void view_then(Biing *b, uint32_t view, uint32_t r) {
    if (view) call(b, view);
    call(b, r);
}
static void inventory(Biing *b, uint32_t view, int cat, uint32_t ret, int store_view, int equipment_first) {
    if (view) call(b, view);
    uint8_t *p = pb(b);
    p[PU_MISSING] = 0;
    put16(p + PU_CAT, cat);
    for (int i = 0; i < 4; i++) p[PU_BACK + i] = (uint8_t)(ret >> (8 * i));
    put16(p + PU_VIEW, store_view ? PU_STORE_VIEW : PU_ROOM_VIEW);
    call(b, equipment_first ? 0x26F75 : 0x26F7B);
}
static void sheet(Biing *b, uint32_t view, int cat, int page, uint32_t r) {
    call(b, view);
    const uint8_t *rec = sim_room_record(sim(b), 0, cat, (uint16_t)get16(pb(b) + AP_ROOM));
    put16(pb(b) + 0x2C, get16(rec + 0x0C));
    put16(pb(b) + 2, 0);
    put16(pb(b) + 0x0E, page);
    call(b, r);
}

/* 0x6179F */
static void go(Biing *b) {
    if (b->clock_stopped) return;
    sim(b)->advice_pending = 0;
    int n = get16(pb(b) + AP_ADVICE);
    if (n < 0) {
        back(b);
        return;
    }
    put16(pb(b) + AP_ROOM, get16(pb(b) + AP_INSTANCE));
    int bed = (uint16_t)get16(pb(b) + AP_PLACE);
    switch (n) {
    case 0: draw(b, IMG_AGENT), call(b, 0x37D5F); break;
    case 1: rent(b, 1); break;
    case 2: rent(b, 2); break;
    case 3: rent(b, 3); break;
    case 4: rent(b, 5); break;
    case 5: rent(b, 11); break;
    case 6: view_then(b, 0x33DF0, 0x1D2AB); break;
    case 7: draw(b, IMG_PERSONNEL), call(b, 0x3BF02); break;
    case 8: ad(b, 1); break;
    case 9:
        pb(b)[0x2A] = get16(sim_room_record(sim(b), 0, 2, 0) + 0x0A) ? 0xFF : 0;
        view_then(b, 0x33DF0, 0x29167);
        break;
    case 10: view_then(b, 0x31BBE, 0x1CC87); break;
    case 11: view_then(b, 0x31BBE, 0x1D14F); break;
    case 12: ad(b, 2); break;
    case 13: view_then(b, 0x31A26, 0x1D274); break;
    case 14: ad(b, 9); break;
    case 15: view_then(b, 0x31A26, 0x1D23D); break;
    case 16: call(b, 0x36D3C); break;
    case 17: call(b, 0x31A16); break;
    case 18: put16(pb(b) + AP_ROOM, bed), call(b, 0x3C07A); break;
    case 19: rent(b, 15); break;
    case 20: view_then(b, 0x31EF3, 0x244E6); break;
    case 21: ad(b, 10); break;
    case 22: view_then(b, 0x31EF3, 0x24520); break;
    case 23: call(b, 0x31EE3); break;
    case 24: case 25: case 68: sheet(b, 0x31BBE, 5, n == 24 ? 1 : n == 25 ? 2 : 0, 0x23EE2); break;
    case 26: case 27: case 69: sheet(b, 0x31A26, 11, n == 26 ? 1 : n == 27 ? 2 : 0, 0x363D5); break;
    case 28: case 29: case 70: sheet(b, 0x31EF3, 15, n == 28 ? 1 : n == 29 ? 2 : 0, 0x24499); break;
    case 30: rent(b, 6); break;
    case 31: view_then(b, 0x36D78, 0x1D0A1); break;
    case 32: rent(b, 16); break;
    case 33: view_then(b, 0x3BCDB, 0x1D0DB); break;
    case 34: view_then(b, 0x33DF0, 0x24356); break;
    case 35: rent(b, 17); break;
    case 36: rent(b, 7); break;
    case 37: view_then(b, 0x364AA, 0x1CCC1); break;
    case 38: view_then(b, 0x364AA, 0x1D061); break;
    case 39: view_then(b, 0x364AA, 0x1CFE1); break;
    case 40: ad(b, 5); break;
    case 41: view_then(b, 0x36D78, 0x27C45 + 4u * (uint32_t)(bed % 10)); break; /* 0x27C6B with CX = the bed */
    case 42: call(b, 0x3649A); break;
    case 43: view_then(b, 0x364AA, 0x344E2); break;
    case 44: put16(pb(b) + AP_ROOM, bed), call(b, 0x3C084); break;
    case 45: view_then(b, 0x32DA9, 0x1D115); break;
    case 46: rent(b, 9); break;
    case 47: view_then(b, 0x31D8A, 0x1CF27); break;
    case 48: view_then(b, 0x31D8A, 0x36373); break;
    case 49: view_then(b, 0x3BCDB, 0x274EA + 4u * (uint32_t)(bed % 10)); break; /* 0x27510 with CX = the bed */
    case 50: rent(b, 18); break;
    case 51: draw(b, IMG_PATHOLOGY), call(b, 0x1CF61); break;
    case 52: ad(b, 7); break;
    case 53: call(b, 0x3BDE0); break;
    case 54: draw(b, IMG_PATHOLOGY), call(b, 0x4AE9B); break;
    case 55: draw(b, IMG_BANK), call(b, 0x4AEDE); break;
    case 56: inventory(b, 0x33DF0, 2, 0x33D58, 1, 1); break;
    case 57: rent(b, 4); break;
    case 58: draw(b, IMG_WAREHOUSE), call(b, 0x1CC47); break;
    case 59: ad(b, 3); break;
    case 60: draw(b, IMG_ADMIN), call(b, 0x3AF1F); break;
    case 61: view_then(b, 0x31BBE, 0x26E52); break;
    case 62: view_then(b, 0x31A26, 0x26DA9); break;
    case 63: inventory(b, 0x36D78, 6, 0x3C084, 1, 1); break;
    case 64: inventory(b, 0x3BCDB, 16, 0x3BCCB, 1, 1); break;
    case 65: inventory(b, 0x31D8A, 9, 0x31D44, 1, 1); break;
    case 66: inventory(b, 0x364AA, 7, 0x3649A, 1, 1); break;
    case 67: view_then(b, 0x32DA9, 0x34461 + 4u * (uint32_t)(bed % 10)); break; /* 0x34487 with AX = the seat */
    case 71: case 73: inventory(b, 0x33DF0, 2, 0x33D58, n == 71, 0); break;
    case 72: draw(b, IMG_ADMIN), call(b, 0x3B02E); break;
    case 74: case 75: inventory(b, 0x32DA9, 3, 0x3C07A, n == 74, 0); break;
    case 76: case 77: inventory(b, 0x36D78, 6, 0x3C084, n == 76, 0); break;
    case 78: case 79: inventory(b, 0x3BCDB, 16, 0x3BCCB, n == 78, 0); break;
    case 80: case 83: inventory(b, 0x31BBE, 5, 0x36D3C, n == 80, 0); break;
    case 81: case 84: inventory(b, 0x31A26, 11, 0x31A16, n == 81, 0); break;
    case 82: case 85: inventory(b, 0x31EF3, 15, 0x31EE3, n == 82, 0); break;
    case 86: case 87: inventory(b, 0x364AA, 7, 0x3649A, n == 87, 0); break; /* 86 room view, 87 store view */
    case 88: case 89: inventory(b, 0x31D8A, 9, 0x31D44, n == 88, 0); break;
    case 90: view_then(b, 0x31EF3, 0x26CDE); break;
    case 91: case 92: draw(b, IMG_KITCHEN), inventory(b, 0, 13, 0x36AD9, 1, n == 91); break;
    case 93: draw(b, IMG_KITCHEN), call(b, 0x1CD81); break;
    case 94: ad(b, 4); break;
    case 95: view_then(b, 0x3657F, 0x1D021); break;
    case 96: view_then(b, 0x3657F, 0x1CD01); break;
    case 97: view_then(b, 0x3657F, 0x34FCC); break;
    case 98: call(b, 0x3677D); break;
    case 99: case 100: inventory(b, 0x3657F, 8, 0x3677D, 1, n == 99); break;
    case 101: draw(b, IMG_GARAGE), call(b, 0x1CE07); break;
    case 102: draw(b, IMG_GARAGE), call(b, 0x1CE5B); break;
    case 103: ad(b, 6); break;
    case 104: draw(b, IMG_BANK), call(b, 0x1CFA1); break;
    case 105: draw(b, IMG_BANK), call(b, 0x1CD41); break;
    case 106: draw(b, IMG_GOLF), call(b, 0x1CEA1); break;
    case 107: draw(b, IMG_CLUB), call(b, 0x1CDC1); break;
    case 108: draw(b, IMG_CLUB), call(b, 0x1CEE1); break;
    case 109: case 110: draw(b, IMG_GOLF), inventory(b, 0, 12, 0x36A7B, 1, n == 110); break;
    case 111: draw(b, IMG_BANK), inventory(b, 0, 17, 0x3BE59, 1, 1); break;
    case 112: draw(b, IMG_PATHOLOGY), inventory(b, 0, 18, 0x3BDE0, 1, 1); break;
    case 113: case 114: draw(b, IMG_GARAGE), inventory(b, 0, 10, 0x36A1E, 1, n == 114); break;
    default: back(b); break; /* engine choice: a number no case covers acts as the thumbs-down */
    }
}

/* ---- the newspaper 0x4704A ---------------------------------------------------------------------------- */

/* SCR-14: the original draws the page's text into its frame buffer without any bound, but presents only the dirty
 * rectangle, which image 1069 alone widens (text draws never do): frame x 88-547 (1069's 91-544 rounded out to 4-pixel
 * groups), y 39-399. The engine shows its whole frame, so it puts back everything outside that rectangle; a body line
 * that straddles row 399 is cut per pixel row, and rows 400-439 keep the strip they showed before. The same for every
 * slot and edition. The hidden pixels are dropped here, where the original keeps them in its frame buffer (whether a
 * later screen can present them was not measured; docs/engine.md, "2026-10-21: SCR-14"). */
void reports_paper_present(Surface *s, const Surface *before) {
    for (int y = 0; y < s->h; y++) {
        uint8_t *row = s->px + (size_t)y * s->w;
        const uint8_t *old = before->px + (size_t)y * s->w;
        if (y < PAPER_Y0 || y > PAPER_Y1) {
            memcpy(row, old, (size_t)s->w);
            continue;
        }
        memcpy(row, old, PAPER_X0);
        memcpy(row + PAPER_X1 + 1, old + PAPER_X1 + 1, (size_t)(s->w - PAPER_X1 - 1));
    }
}

static void newspaper(Biing *b) {
    if (!news_issue_ready(sim(b))) {
        msg_clear(&b->msg);
        biing_msg_entry(b, TXT_NO_PAPER);
        biing_message_box(b, IMG_NO_PAPER);
        dulog_set_screen(&b->ui, SCR_MESSAGE);
        return;
    }
    static Surface before; /* the frame as the screen shows it before the page (SCR-14) */
    before = b->screen;
    draw(b, IMG_PAGE);
    sound_request(&b->sound, 0x61, 0x1F4, 23); /* 0x4710E */
    char date[11];
    news_date(sim(b), date);
    msg_clear(&b->msg);
    msg_append(&b->msg, (const uint8_t *)date, 10);
    render_paper(b, 5, 4, -1, 126, 175, 256);
    const uint8_t *issue = sim_news_issue(sim(b));
    Pieces t;
    news_text(sim(b), issue[0], 1, &t);
    reports_render(b, &t);
    render_paper(b, 3, 5, 0, 99, 53, 440);
    static const struct { uint8_t hfont, bfont, hx, bx; int16_t hy, by; uint16_t hw, bw; int8_t bgap; } SLOT[3] = {
        {4, 5, 224, 224, 95, 114, 311, 311, -1}, {4, 5, 98, 97, 236, 255, 256, 257, -1}, {3, 6, 0, 0, 246, 263, 176, 176, 0}};
    for (int k = 0; k < 3; k++) {
        int hx = k == 2 ? 364 : SLOT[k].hx, bx = k == 2 ? 364 : SLOT[k].bx;
        int number = issue[k + 1]; /* [0x45CF], set once per slot: type 0x27's headline changes it for the body */
        news_text_at(sim(b), &number, 1, &t);
        reports_render(b, &t);
        int lines = render_paper(b, SLOT[k].hfont, 5, -1, hx, SLOT[k].hy, SLOT[k].hw);
        /* the body starts one headline line higher than by + lines x pitch: screenshot of day 19, docs/ui-reports.md */
        int h = (lines - 1) * (b->fonts[SLOT[k].hfont].height - 1);
        news_text_at(sim(b), &number, 0, &t);
        reports_render(b, &t);
        render_paper(b, SLOT[k].bfont, 4, SLOT[k].bgap, bx, SLOT[k].by + h, SLOT[k].bw);
        sim(b)->dup_byte = (uint8_t)number; /* the page leaves the last slot's number in [0x45CF] (SIM-55) */
    }
    reports_paper_present(&b->screen, &before);
    dulog_set_screen(&b->ui, SCR_NEWSPAPER);
}

int reports_routine(Biing *b, uint32_t r) {
    switch (r) {
    case 0x68A45: reports_daily(b); return 1;
    case 0x5B640: board(b, ADVICE_CHIEF); return 1;
    case 0x5B5D6: board(b, ADVICE_RECEPTION); return 1;
    case 0x5B4D1: strip(b); return 1;
    case 0x6179F: go(b); return 1;
    case 0x61B68: back(b); return 1;
    case 0x4704A: newspaper(b); return 1;
    default: return 0;
    }
}
