#include "room_sound.h"
#include "game.h"
#include "estate_ui.h"
#include "fixes.h"
#include "personnel.h"
#include "personnel_ui.h"
#include "ranking.h"
#include "rooms_ui.h"
#include "bank_ui.h"
#include "depts_ui.h"
#include "prices_ui.h"
#include "purchase_ui.h"
#include "reports_ui.h"
#include "support_ui.h"
#include "garage_ui.h"
#include "desktop_ui.h"
#include "desktop.h"
#include "report.h"

#include <stdio.h>
#include <string.h>


static const char *FONT_FILES[FONT_COUNT] = {"KU.FNT", "XFONT3.FNT", NULL, "ZEI1.FNT",
                                             "ZEI2.FNT", "ZEI3.FNT", "ZEI4.FNT", "ZEI5.FNT"};

/* Images and texts used by the routines below (docs/screens.md). */
enum {
    IMG_OFFICE = 1, IMG_CLOCK_PANEL = 24, IMG_DIGIT_0 = 25, IMG_SITE_MAP = 44, IMG_ESTATE_AGENT = 87,
    IMG_ADMINISTRATION = 104, IMG_PERSONNEL = 133, IMG_PARCHMENT = 165, IMG_POSTER_0 = 501,
    IMG_PAUSE = 517, IMG_SKIP_HOUR = 516, IMG_BANKRUPT = 513, IMG_DUSTBINS = 547, IMG_DESKTOP = 1040,
    IMG_NEWSPAPER = 1069, IMG_DOOR_HOVER = 579, IMG_DOOR_SIGN = 8,
};
enum { TXT_SKIPPED = 1165, TXT_SKIP_QUESTION = 1166, TXT_SKIP_RUNNING = 2752, TXT_PAUSE = 2753, TXT_BANKRUPT = 3016 };
/* docs/office.md */
enum {
    IMG_PHONE_CALL = 569, IMG_MONEY_SHORT = 511, IMG_ADVICE_BOARD = 1001, IMG_ADVICE_THUMB = 1003,
    IMG_NO_PAPER = 518, TXT_NO_PAPER = 517, TXT_ADVICE_TITLE = 2443, SCR_MONEY = 55, SCR_ADVICE = 2,
    STORY_RECORDS = 0x3F71, STORY_SIZE = 10, MONEY_TAIL_STRING = 0x4672,
    /* player block (save block 28) */
    P_CONTINUE = 6, P_DOOR_HOVERED = 0x36, P_FLAG_50_TICKS = 0x3B, P_TOLD_TREATMENT = 0x57,
    P_TOLD_DENTAL = 0x58, P_TOLD_NEUROLOGY = 0x59, P_ADVICE = 0xA2,
    /* tail block */
    T_EVENT_NEW = 0x10, T_EVENT = 0x12, T_EVENT_COUNTDOWN = 0x14, T_PAPER_DATE = 0x16,
};
/* Chief-doctor animations: 12-byte records of four image numbers (docs/office.md, office per-frame 5). */
static const uint32_t ANIMATIONS[4] = {0x6087, 0x6093, 0x609F, 0x60AB};
/* Save and load dialogs (docs/gui.md, "Save and load dialogs"). */
enum {
    IMG_SAVE_HEADER = 256, IMG_LOAD_HEADER = 333, IMG_DIALOG_BODY = 1216, IMG_DIALOG_THUMB = 1247,
    IMG_ARROW_RIGHT = 1383, IMG_ARROW_LEFT = 1385, IMG_OK_PICTURE = 509, IMG_NOT_FOUND = 518, IMG_DAMAGED = 514,
    TXT_LOAD_ASK = 2737, TXT_NOT_FOUND = 2739, TXT_LOADED = 2741, TXT_DAMAGED = 2743, TXT_SAVE_ASK = 2745,
    TXT_SAVED = 2747, SCR_LOAD = 154, SCR_SAVE = 155, SCR_MESSAGE = 77, SLOTS = 9,
};
enum {
    SCR_OFFICE = 1, SCR_SITE_MAP = 5, SCR_ESTATE_AGENT = 6, SCR_ADMINISTRATION = 7, SCR_PERSONNEL = 10,
    SCR_GAME_OVER = 47, SCR_YES_NO = 52, SCR_TITLE = 56, SCR_DESKTOP = 97, SCR_NEWSPAPER = 118,
    SCR_PAUSE = 157, SCR_BANKRUPT = 160,
};

static Biing *of(Dulog *d) { return d->user; }
static uint32_t sound_random(void *sim, uint32_t n) { return sim_random(sim, n); }
static uint32_t sound_bits(void *sim, uint32_t mask) { return sim_bits(sim, mask); }
static void office_entry(Biing *b);

/* ---- clock ---------------------------------------------------------------------------------- */

int biing_clock(const Biing *b, int tail_field) { return world_tail(&b->world, tail_field); }
long biing_cash(const Biing *b) { return world_cash(&b->world, 0); }
void biing_set_cash(Biing *b, long v) { world_set_cash(&b->world, 0, (int32_t)v); }

/* Interrupts per game tick by game hour (docs/mainloop.md, "When a game tick happens"). */
static int interrupts_per_tick(const Biing *b) {
    int hour = biing_clock(b, TAIL_HOUR);
    if (biing_clock(b, TAIL_HOURS) < 2) return 3;
    if (hour >= 22 || hour <= 2) return 1;
    if (hour == 3 || hour == 20 || hour == 21) return 2;
    return 3;
}

/* Idle blanking (docs/gui.md, "Odds and ends", "Idle blanking"; SCR-7): the idle step 0xD2B6 does nothing while the
 * counter is negative, else counts; at exactly 15,000 it uploads black for all 256 colours and sets the counter to
 * -1, else with the blanked flag set it uploads DATA/COL again. The mouse step after it (0xD186) clears the counter on
 * every interrupt in which the quarter-pixel x differs from y, moving or not: the screen goes black only with the
 * cursor on the diagonal, and DATA/COL comes back on the interrupt after the first one off it. */
static void idle_step(Biing *b) {
    if (b->idle_ticks >= 0) {
        if (++b->idle_ticks == BIING_IDLE_TICKS) {
            b->blanked = 1;
            b->idle_ticks = -1;
        } else if (b->blanked) {
            b->blanked = 0;
        }
    }
    if (fix_on(1)) {
        /* FIX-1 (docs/fixes.md): original clears the count when x differs from y (blanks only on the diagonal);
         * fixed clears it when the position moved since the previous interrupt (blanks after 5 minutes at rest) */
        if (b->mouse_qx != b->mouse_pqx || b->mouse_qy != b->mouse_pqy) b->idle_ticks = 0;
    } else if (b->mouse_qx != b->mouse_qy) {
        b->idle_ticks = 0;
    }
    b->mouse_pqx = b->mouse_qx;
    b->mouse_pqy = b->mouse_qy;
}

const uint8_t *biing_palette(const Biing *b) {
    static const uint8_t BLACK[768];
    return b->blanked ? BLACK : b->assets.palette;
}

void biing_interrupt(Biing *b, int button_held) {
    /* BIPRO's sound start-up sets the MIDI driver up (a MIDI card only); the engine does it at the game's first
     * interrupt, so the start programs before it have their own runs first (docs/music.md, "Byte sequences") */
    sound_midi_program(&b->sound, SOUND_PROG_BIPRO);
    sound_interrupt(&b->sound); /* the driver's timer callback: one tick of output, then the music sequencer */
    b->credit++;
    idle_step(b);
    b->button_held = button_held;
    if (!button_held) {
        b->button_countdown = 0;
        b->button_repeating = 0;
        return;
    }
    if (b->button_countdown > 0) {
        b->button_countdown--;
        return;
    }
    b->ui.click_pending = 1;
    b->ui.click_x = b->ui.mouse_x;
    b->ui.click_y = b->ui.mouse_y;
    b->button_countdown = b->button_repeating ? 7 : 20;
    b->button_repeating = 1;
}

static void clock_routine(Biing *b) {
    if (b->clock_stopped) return;
    if (b->credit < interrupts_per_tick(b)) return;
    b->credit = 0; /* reset, not reduced: a slow frame loses time */
    sim_tick(&b->sim);
    sound_game_tick(&b->sound); /* [0x5C86] and the 34-tick ambience flag, before the schedule in the original */
}

/* ---- message boxes (docs/screens.md, "Message boxes") ---------------------------------------- */

static void set_font(Biing *b, int n, TextLayout *l) {
    l->font = &b->fonts[n];
    l->charmap = b->prog.charmap;
    l->letter = 1;
}

static void msg_entry(Biing *b, int entry) {
    size_t n;
    const uint8_t *t = textdb_text(&b->texts, entry, &n);
    msg_append(&b->msg, t, n);
}

/* Parchment, picture, then the message buffer centred in a 226-pixel column from (250, 120). */
static void message_box(Biing *b, int picture) {
    dulog_draw(&b->ui, IMG_PARCHMENT, 0, 0);
    dulog_draw(&b->ui, picture, 0, 0);
    TextLayout l = {0};
    set_font(b, 1, &l);
    l.x = 250;
    l.y = 120;
    l.width = 226;
    l.line_gap = 1;
    l.space = 5;
    text_centred(&b->screen, &l, b->msg.buf, b->msg.len);
}

void biing_msg_entry(Biing *b, int entry) { msg_entry(b, entry); }

void biing_anim_image(Biing *b, int image) {
    if (b->anims_on) dulog_draw(&b->ui, image, 0, 0); /* 0x1AEAC */
}
void biing_anim_list(Biing *b, uint32_t table, int entries, int *pos) {
    biing_anim_image(b, program_word(&b->prog, table, (uint32_t)*pos));
    *pos = (*pos + 2) % (2 * entries);
}
void biing_anim_pick(Biing *b, uint32_t table, int *last) {
    if (sim_bits(&b->sim, 1)) return;
    int image = program_word(&b->prog, table, 2 * sim_bits(&b->sim, 3));
    if (image != *last) *last = image, biing_anim_image(b, image);
}
void biing_office(Biing *b) { office_entry(b); }
/* 0x3B464, the desktop (docs/desktop.md, "The desktop"): the clock runs ([0x5CAC] = 0), draw 1040, screen 97 */
void biing_desktop(Biing *b) {
    b->clock_stopped = 0;
    dulog_draw(&b->ui, IMG_DESKTOP, 0, 0);
    dulog_set_screen(&b->ui, SCR_DESKTOP);
    sound_background(&b->sound, 0xFB, 0x140, 14); /* 0x3B4A0 */
}

void biing_english_article(Biing *b, int name) {
    size_t n, sp;
    const uint8_t *t = textdb_text(&b->texts, name, &n), *space = textdb_text(&b->texts, 386, &sp);
    msg_english_article(&b->msg, t, n, space, sp);
}
void biing_message_box(Biing *b, int picture) { message_box(b, picture); }

static void message_text(Biing *b, int picture, int entry, int screen) {
    msg_clear(&b->msg);
    msg_entry(b, entry);
    message_box(b, picture);
    b->clock_stopped = 1; /* the game's message boxes are modal: no clock while they show */
    dulog_set_screen(&b->ui, screen);
}

/* ---- office ---------------------------------------------------------------------------------- */

static void enter_screen(Biing *b, int image, int screen);

static void draw_clock_digits(Biing *b) {
    int hour = biing_clock(b, TAIL_HOUR), minute = biing_clock(b, TAIL_MINUTE);
    if (hour == b->shown_hour && minute == b->shown_minute) return;
    b->shown_hour = hour;
    b->shown_minute = minute;
    const int dx[4] = {0, 13, 32, 45};
    const int digit[4] = {hour / 10, hour % 10, minute / 10, minute % 10};
    for (int i = 0; i < 4; i++) dulog_draw(&b->ui, IMG_DIGIT_0 + digit[i], dx[i], 0);
}

static uint8_t *player_block(Biing *b) { return b->world.block[BLK_PLAYER0]; }
static void office_entry(Biing *b) {
    player_block(b)[P_DOOR_HOVERED] = 0; /* office entry clears a saved door hover (docs/screens.md, SCR-15) */
    b->clock_stopped = 0;
    sound_background(&b->sound, 0xFC, 0x140, 8); /* 0x2C7B5 */
    dulog_draw(&b->ui, IMG_OFFICE, 0, 0);
    dulog_draw(&b->ui, IMG_POSTER_0 + biing_clock(b, TAIL_DAY) % 8, 0, 0);
    dulog_draw(&b->ui, IMG_CLOCK_PANEL, 0, 0);
    if (world_tail(&b->world, T_EVENT)) dulog_draw(&b->ui, IMG_PHONE_CALL, 0, 0);
    for (int i = 0; i < 4; i++) b->animation_shown[i] = 0;
    b->shown_hour = b->shown_minute = -1;
    draw_clock_digits(b);
    if (biing_cash(b) < 0) {
        message_text(b, IMG_BANKRUPT, TXT_BANKRUPT, SCR_BANKRUPT);
        b->ui.help_enabled = b->sim.help_strip = 0;
        /* 0x2C87B, 0x2C891: busy flags cleared, queues cleared, the bankruptcy sound */
        b->sound.help_pending = b->sound.race_pending = b->sound.race_busy = b->sound.help_busy = 0;
        sound_clear(&b->sound);
        sound_request(&b->sound, 0xF0, 0x136, 63);
        return;
    }
    dulog_set_screen(&b->ui, SCR_OFFICE);
}

static void office_frame(Biing *b) {
    if (biing_cash(b) < 0) {
        office_entry(b); /* takes the bankruptcy path */
        return;
    }
    if (player_block(b)[0x6D]) { /* the daily report, armed by the day change 0x3DCCB */
        reports_daily(b);
        return;
    }
    if (world_tail(&b->world, T_EVENT_NEW)) {
        dulog_draw(&b->ui, IMG_PHONE_CALL, 0, 0);
        world_set_tail(&b->world, T_EVENT_NEW, 0);
    }
    draw_clock_digits(b);
    uint8_t *p = player_block(b);
    if (!p[P_DOOR_HOVERED] && p[P_FLAG_50_TICKS]) {
        for (int i = 0; i < 4; i++) {
            if (sim_bits(&b->sim, 1)) continue;
            int image = program_word(&b->prog, ANIMATIONS[i], 2 * sim_bits(&b->sim, 3));
            if (image == b->animation_shown[i]) continue;
            dulog_draw_quiet(&b->ui, image, 0, 0);
            b->animation_shown[i] = image;
        }
        p[P_FLAG_50_TICKS] = 0;
    }
    /* Single masked draws, without ranged retries (docs/office.md, SCR-16): ambience, queue 1 only;
     * the phone gate consumes a draw only while a story event is active. */
    if (sim_bits(&b->sim, 7) == 0) sound_ambience(&b->sound, 0, 0x4FBA);
    if (world_tail(&b->world, T_EVENT) && sim_bits(&b->sim, 127) == 0) sound_ring(&b->sound);
}

/* 0x1AC97: cash against the day's fixed costs (docs/office.md). */
static void money_box(Biing *b) {
    long cash = biing_cash(b), costs = sim_daily_costs(&b->sim, 0), r = cash - costs;
    int picture;
    msg_clear(&b->msg);
    if (r >= 0 || -r < 120) {
        msg_entry(b, 0);
        msg_append_number(&b->msg, cash);
        msg_entry(b, 1);
        msg_append_number(&b->msg, costs);
        if (r == 0) {
            msg_entry(b, 2);
        } else if (r > 0) {
            msg_entry(b, 3);
            msg_append_number(&b->msg, r);
            msg_entry(b, 4);
        } else {
            msg_entry(b, 9);
            msg_append_number(&b->msg, -r);
            msg_entry(b, 10);
        }
        picture = r >= 0 ? IMG_OK_PICTURE : IMG_MONEY_SHORT;
    } else {
        msg_entry(b, 5);
        msg_append_number(&b->msg, cash);
        msg_entry(b, 6);
        msg_append_number(&b->msg, costs);
        msg_entry(b, 7);
        msg_append_number(&b->msg, -r);
        msg_entry(b, 8);
        picture = IMG_DAMAGED;
    }
    /* the '$'-terminated string at 0x4672 (two CRs) */
    const uint8_t *tail = program_table(&b->prog, MONEY_TAIL_STRING);
    size_t n = 0;
    while (tail[n] != '$' && tail[n] && n < 16) n++;
    msg_append(&b->msg, tail, n);
    /* 0x1A3EA (copy-protection question setter) is not called: the engine has no manual query */
    message_box(b, picture);
    sound_list(&b->sound, 0, 0x468A); /* 0x1ACFC-0x1ADD7: coin and echo into queues 1 and 2 */
    sound_list(&b->sound, 1, 0x468A);
    uint8_t *p = player_block(b);
    for (int i = 0; i < 4; i++) p[P_CONTINUE + i] = (uint8_t)(0x2C79B >> (8 * i));
    dulog_set_screen(&b->ui, SCR_MONEY);
}

/* 0x1AE66: continue with the routine stored at player block +6. */
static uint32_t continue_routine(Biing *b) {
    const uint8_t *p = player_block(b) + P_CONTINUE;
    return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* 0x2F375: the telephone answers only while a story event is active. */
static void telephone(Biing *b) {
    int n = world_tail(&b->world, T_EVENT);
    if (!n) return;
    const uint8_t *rec = program_table(&b->prog, STORY_RECORDS) + n * STORY_SIZE;
    dulog_draw(&b->ui, IMG_PARCHMENT, 0, 0);
    dulog_draw(&b->ui, rec[4] | rec[5] << 8, -30, -20);
    msg_clear(&b->msg);
    if (rec[3]) {
        msg_entry(b, 886);
        msg_entry(b, 887 + (int)sim_random(&b->sim, 12));
        msg_entry(b, 899 + (int)sim_random(&b->sim, 11));
        msg_entry(b, 910);
    } else {
        msg_entry(b, rec[6] | rec[7] << 8);
    }
    TextLayout l = {0};
    set_font(b, 1, &l);
    l.x = 250, l.y = 120, l.width = 226, l.line_gap = 1, l.space = 5;
    text_centred(&b->screen, &l, b->msg.buf, b->msg.len);
    dulog_set_screen(&b->ui, SCR_MESSAGE);
    world_set_tail(&b->world, T_EVENT, 0);
    world_set_tail(&b->world, T_EVENT_COUNTDOWN, ((int)sim_random(&b->sim, 16) + 50) * 8);
}

static void skip_tick_sound(void *b) { sound_game_tick(&((Biing *)b)->sound); }

/* 0x49184 (docs/mainloop.md, docs/ui-sheets.md section 4): box 517 / 2752 while the clock runs, then box 516 / 1165
 * and screen 77 (the OK box back to the office; the pause box 157 would stop the clock) */
static void skip_hour(Biing *b) {
    msg_clear(&b->msg);
    msg_entry(b, TXT_SKIP_RUNNING);
    message_box(b, IMG_PAUSE);
    sim_skip_hour(&b->sim, skip_tick_sound, b);
    message_text(b, IMG_SKIP_HOUR, TXT_SKIPPED, SCR_MESSAGE);
}

static void enter_screen(Biing *b, int image, int screen) {
    dulog_draw(&b->ui, image, 0, 0);
    dulog_set_screen(&b->ui, screen);
}

/* ---- save and load dialogs ------------------------------------------------------------------- */

/* Text entry, slot number, text entry: "... Spielstand Nr. " + "3 " + "...". */
static void msg_slot(Biing *b, int before) {
    msg_clear(&b->msg);
    msg_entry(b, before);
    msg_append_number(&b->msg, b->slot + 1);
    msg_entry(b, before + 1);
}

static void slot_dialog(Biing *b, int header, int text) {
    dulog_draw(&b->ui, header, 6, -22);
    dulog_draw(&b->ui, IMG_DIALOG_BODY, 6, 3);
    dulog_draw(&b->ui, IMG_DIALOG_THUMB, 6, 3);
    dulog_draw(&b->ui, IMG_ARROW_RIGHT, 6, 3);
    dulog_draw(&b->ui, IMG_ARROW_LEFT, 6, 3);
    msg_slot(b, text);
    TextLayout l = {0};
    set_font(b, 7, &l);
    l.x = 114;
    l.y = 120;
    l.width = 420;
    l.line_gap = 1;
    l.space = 4;
    text_left(&b->screen, &l, b->msg.buf, b->msg.len);
}

static void save_dialog(Biing *b) { slot_dialog(b, IMG_SAVE_HEADER, TXT_SAVE_ASK); }
static void load_dialog(Biing *b) { slot_dialog(b, IMG_LOAD_HEADER, TXT_LOAD_ASK); }

static void open_slot_dialog(Biing *b, int load) {
    b->slot = 0;
    if (load) load_dialog(b);
    else save_dialog(b);
    dulog_set_screen(&b->ui, load ? SCR_LOAD : SCR_SAVE);
}

static void step_slot(Biing *b, int delta, int load) {
    b->slot = (b->slot + delta + SLOTS) % SLOTS;
    if (load) load_dialog(b);
    else save_dialog(b);
}

static void slot_path(Biing *b, char *path, size_t n) {
    char rel[32];
    snprintf(rel, sizeof rel, "GAMES/BGAME%d", b->slot + 1);
    game_path(&b->assets, rel, path, n);
}

static void slot_message(Biing *b, int picture, int text) {
    msg_slot(b, text);
    message_box(b, picture);
    dulog_set_screen(&b->ui, SCR_MESSAGE);
}

static void confirm_save(Biing *b) {
    char path[1200];
    slot_path(b, path, sizeof path);
    if (world_save_edition(&b->world, path, &b->prog)) fprintf(stderr, "[ERROR] %s\n", asset_error());
    slot_message(b, IMG_OK_PICTURE, TXT_SAVED);
}

int biing_restore_checkpoint(Biing *b, const char *path) {
    World loaded;
    if (world_load_edition(&loaded, path, &b->prog)) return -1;
    world_free(&b->world);
    b->world = loaded;
    sim_reset_generator(&b->sim, 0);
    b->sim.need4 = 0;
    b->credit = b->button_held = b->button_countdown = b->button_repeating = 0;
    b->input_active = b->load_failed = b->blanked = b->idle_ticks = 0;
    b->key = -1;
    sound_clear(&b->sound);
    dulog_clear_strip(&b->ui);
    office_entry(b);
    return 0;
}

static void confirm_load(Biing *b) {
    char path[1200];
    slot_path(b, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) {
        slot_message(b, IMG_NOT_FOUND, TXT_NOT_FOUND);
        return;
    }
    fclose(f);
    World loaded;
    if (world_load_edition(&loaded, path, &b->prog)) {
        /* The original has overwritten its tables by then and ends after this message; the engine keeps
         * its state but ends the same way. */
        fprintf(stderr, "[ERROR] %s\n", asset_error());
        b->load_failed = 1;
        slot_message(b, IMG_DAMAGED, TXT_DAMAGED);
        return;
    }
    world_free(&b->world);
    b->world = loaded;
    sim_reset_generator(&b->sim, 0); /* S = the start value at every load (docs/mainloop.md section 3) */
    b->sim.need4 = 0;                /* the need table is the image's after a load (SIM-65) */
    slot_message(b, IMG_OK_PICTURE, TXT_LOADED);
    dulog_clear_strip(&b->ui); /* 0xBE21 */
}

/* ---- routine dispatch ------------------------------------------------------------------------ */

static void routine(Dulog *d, uint32_t r) {
    Biing *b = of(d);
    switch (r) {
    case 0x19A26: /* empty routines that only suppress a hotspot's highlight */
    case 0x2F373:
        break;
    case 0x2C74B: /* title face: sample 2 directly on voices 0 and 1, level 0, then the office entry */
        sound_direct(&b->sound, 2, 0, 0x140, 63);
        sound_direct(&b->sound, 2, 1, 0x140, 63);
        sound_set_level(&b->sound, 0);
        if (b->load_failed) b->quit = 1;
        else office_entry(b);
        break;
    case 0x2C79B:
        if (b->load_failed) b->quit = 1;
        else office_entry(b);
        break;
    case 0x20143: open_slot_dialog(b, 0); break;
    case 0x2142B: open_slot_dialog(b, 1); break;
    case 0x20162: step_slot(b, 1, 0); break;
    case 0x20186: step_slot(b, -1, 0); break;
    case 0x2144A: step_slot(b, 1, 1); break;
    case 0x2146E: step_slot(b, -1, 1); break;
    case 0x20260: confirm_save(b); break;
    case 0x21547: confirm_load(b); break;
    case 0x2984A: office_frame(b); break;
    case 0x2EDEE:
        enter_screen(b, IMG_ADMINISTRATION, SCR_ADMINISTRATION);
        room_sound_view(&b->sim, 0); /* 0x2EE07: the office loop */
        break;
    case 0x3B464: biing_desktop(b); break;
    case 0x5253B: /* the desktop's per-frame routine: only selects the player's pointers (docs/desktop.md) */
        break;
    case 0x2F212: /* door enter: "Personalbüro" sign */
        dulog_draw(d, IMG_DOOR_HOVER, 0, 0);
        dulog_draw(d, IMG_DOOR_SIGN, 0, 0);
        player_block(b)[P_DOOR_HOVERED] = 0xFF;
        break;
    case 0x2F236: player_block(b)[P_DOOR_HOVERED] = 0; break;
    case 0x1AC97: money_box(b); break;
    case 0x1AE66: routine(d, continue_routine(b)); break;
    case 0x2F375: telephone(b); break;
    case 0x2C70B:
        message_text(b, IMG_PAUSE, TXT_PAUSE, SCR_PAUSE);
        sound_background(&b->sound, 0xD2, 0x140, 30); /* 0x2C727 */
        break;
    case 0x490DA: { /* the wall clock: one of four songs (0x490F6-0x49150) */
        static const uint16_t SONG[4] = {0xDD, 0xDC, 0xB5, 0xDB};
        message_text(b, IMG_SKIP_HOUR, TXT_SKIP_QUESTION, SCR_YES_NO);
        sound_background(&b->sound, SONG[sim_random(&b->sim, 4)], 0x140, 18);
        break;
    }
    case 0x49184: skip_hour(b); break;
    case 0x2C899: enter_screen(b, IMG_DUSTBINS, SCR_GAME_OVER); break;
    case ROUTINE_CD_GOLF: /* the CD builds' golf flag (screen 42): hands over to the bought-in golf program, which the
                           * engine does not run (AGENTS.md, docs/editions.md section 3.5) */
        fprintf(stderr, "[INFO] the golf game is a separate program; the engine does not start it\n");
        break;
    case 0x37192: /* frame of the game-over screen 47: all four queues from 0x513A every iteration */
        for (int q = 0; q < SOUND_QUEUES; q++) sound_ambience_copy(&b->sound, q, 0x513A);
        break;
    default: {
        if (estate_routine(b, r)) break;
        if (personnel_routine(b, r)) break;
        if (ranking_routine(b, r)) break;
        if (purchase_routine(b, r)) break;
        if (prices_routine(b, r)) break;
        if (depts_routine(b, r)) break;
        if (bank_routine(b, r)) break;
        if (reports_routine(b, r)) break;
        if (support_routine(b, r)) break;
        if (garage_routine(b, r)) break;
        if (desktop_routine(b, r)) break;
        if (rooms_routine(b, r)) break;
        static uint32_t reported[64];
        static int n;
        for (int i = 0; i < n; i++)
            if (reported[i] == r) return;
        if (n < 64) reported[n++] = r;
        fprintf(stderr, "[TODO] routine 0x%X not implemented (screen %d)\n", (unsigned)r, d->current);
    }
    }
}

/* ---- set-up ---------------------------------------------------------------------------------- */

static void resource_read(void *sound, int index) { sound_resource_read(sound, index); }

void biing_routine(Biing *b, uint32_t r) { routine(&b->ui, r); }

int biing_open(Biing *b, const char *root) {
    memset(b, 0, sizeof *b);
    char path[1200];
    if (game_open(&b->assets, root)) return -1;
    game_path(&b->assets, "BIPRO.EXE", path, sizeof path);
    if (program_load(&b->prog, path)) return -1;
    game_path(&b->assets, b->prog.build->texts, path, sizeof path); /* BITEXPZ, English CD BITEXPE */
    if (textdb_load(&b->texts, path)) return -1;
    for (int i = 0; i < FONT_COUNT; i++) {
        if (!FONT_FILES[i]) continue;
        char rel[64];
        snprintf(rel, sizeof rel, "DATA/%s", FONT_FILES[i]);
        game_path(&b->assets, rel, path, sizeof path);
        if (font_load(&b->fonts[i], path)) return -1;
    }
    game_path(&b->assets, b->prog.build->data, path, sizeof path); /* BIDATP, English CD BIDATPE */
    if (datadb_load(&b->people, path)) return -1;
    if (world_new(&b->world, &b->prog, b->people.patients, b->people.staff)) return -1;
    sim_init(&b->sim, &b->world, &b->prog, 0);
    personnel_shuffle(&b->sim, b->staff_list); /* 0x1A384, at program start */
    b->sim.staff_list = b->staff_list;
    b->sim.people = &b->people;
    b->sim.texts = &b->texts;
    b->roster_occupant = -1;
    size_t ailments;
    const uint8_t *table = textdb_raw(&b->texts, 4014, &ailments);
    sim_set_ailments(&b->sim, table, ailments);
    report_new_game(&b->sim); /* the start-up's baseline and expected cash (docs/mainloop.md section 1, SAV-1) */
    b->clock_stopped = 1; /* until the office is entered */
    b->key = -1;
    game_path(&b->assets, "GAMES/QUACK", path, sizeof path); /* 0x6F2CD */
    if (quack_load(path, &b->prog, b->hiscore) < 0) {
        fprintf(stderr, "[ERROR] %s is damaged: the game ends at once, as the original does\n", path);
        b->quit = 1;
    }

    sound_init(&b->sound, root, &b->prog, sound_random, &b->sim);
    sound_set_bits(&b->sound, sound_bits);
    b->sound.tick_credit = &b->credit; /* a sample start sets the clock's credit to 2 (docs/sound.md) */
    b->anims_on = 1; /* BIING!.CFG ANIMS ([0xAF8C], docs/saves.md), read like the sound switch; on without the file */
    game_path(&b->assets, "BIING!.CFG", path, sizeof path);
    Blob cfg;
    if (blob_read(path, &cfg) == 0) {
        b->anims_on = sound_cfg_flag((const char *)cfg.data, cfg.size, "ANIMS", 1);
        blob_free(&cfg);
    }
    b->sim.sound = &b->sound;
    sound_direct(&b->sound, 1, 0, 0x140, 63); /* 0x198F9, 0x1991A: program start */
    sound_direct(&b->sound, 1, 1, 0x140, 63);

    surface_clear(&b->screen, 0);
    dulog_init(&b->ui, &b->assets, &b->prog, &b->screen, routine, b);
    b->ui.load_hook = resource_read;
    b->ui.load_ctx = &b->sound;
    dulog_set_screen(&b->ui, SCR_TITLE); /* its script draws the title picture */
    return 0;
}

void biing_close(Biing *b) {
    sound_free(&b->sound);
    dulog_free(&b->ui);
    for (int i = 0; i < FONT_COUNT; i++) font_free(&b->fonts[i]);
    textdb_free(&b->texts);
    datadb_free(&b->people);
    world_free(&b->world);
    program_free(&b->prog);
}

int biing_screenshot(Biing *b) {
    char name[16], path[1200];
    snprintf(name, sizeof name, "ASA%02d.LBM", ++b->screenshots % 100);
    /* the original writes into its current folder (under GOG the CD drive D:, read-only); the engine into the game
     * folder. The counter starts at 1 in every session (a guess: the counter's start is not read). */
    game_path(&b->assets, name, path, sizeof path);
    if (pbm_write(path, &b->screen, 400, biing_palette(b))) {
        fprintf(stderr, "[ERROR] cannot write %s\n", path);
        return -1;
    }
    fprintf(stderr, "[OK] screenshot %s\n", path);
    return 0;
}

void biing_frame(Biing *b) {
    b->sim.on_applications = b->ui.current == 51; /* 0x1F2A8 keeps the player's ads while they are read */
    b->sim.screen = b->ui.current;
    sound_main_loop(&b->sound, !b->clock_stopped); /* 0x4A0EA, 0x49DF3: main-loop step 2, before the clock */
    clock_routine(b);
    /* [0xB10C], set by the scheduled advice inside the clock; the frame's step 2 (0xBDE6) draws the strip in the same
     * main-loop iteration (docs/screens.md, "Per-frame order") */
    b->ui.help_enabled = b->sim.help_strip;
    if (b->sim.player_image) dulog_draw(&b->ui, b->sim.player_image, 0, 0), b->sim.player_image = 0; /* module D */
    if (b->sim.player_box) { /* 0x2B989's throw-out box, drawn whatever screen is shown (docs/sim-player.md) */
        rooms_ui_box_name(b, 515, 2265, b->sim.player_box, 2264, 53);
        b->sim.player_box = 0;
    }
    dulog_frame(&b->ui);
    sound_cd_frame(&b->sound); /* the tail of the GUI frame: the CD track's loop check (docs/cdrom.md) */
    if (b->key == '*' && program_german_cd(&b->prog)) { /* the call the German CD inserts in the GUI frame */
        biing_screenshot(b);
        if (!b->input_active) b->key = -1; /* the name input reads the key as usual */
    }
    if (b->sim.help_strip && !b->help_strip_was) sound_help(&b->sound); /* 0xBDE6: the strip lights up */
    b->help_strip_was = b->sim.help_strip;
    desktop_frame(b); /* the high-score name input and the step from screen 158 to 159 */
}
