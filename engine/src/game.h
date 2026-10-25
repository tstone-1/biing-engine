/* Game state and the routines of the original that the engine implements, keyed by their program offset
 * (the screen scripts name them that way). Behaviour from docs/screens.md, docs/mainloop.md, docs/gui.md. */
#ifndef BIING_GAME_H
#define BIING_GAME_H

#include "dulog.h"
#include "text.h"
#include "sim.h"
#include "world.h"
#include "sound.h"
#include "sheet.h"

#define FONT_COUNT 8

typedef struct {
    Game assets;
    Program prog;
    TextDb texts;
    Font fonts[FONT_COUNT]; /* number = position in the game's font table; 2 (FX2) is not a .FNT */
    Surface screen;
    Dulog ui;
    Message msg;

    World world; /* the game state, in the original's save blocks: clock, cash, tables */
    int slot; /* save/load dialog slot, 0-8 ([0x5CA1]) */

    /* clock (docs/economy.md, docs/mainloop.md); date and time live in the world's tail block */
    Sim sim; /* game ticks and the scheduled routines */
    int clock_stopped;
    int credit; /* 50 Hz interrupts since the last game tick */
    int shown_hour, shown_minute; /* clock digits on the office wall, -1 = not drawn */

    int button_held, button_countdown, button_repeating; /* left button as the timer interrupt sees it */
    /* idle blanking (docs/gui.md, "Idle blanking"): the counter [0xB0E0] (-1 while black), the blanked flag [0xB0E4],
     * and the cursor's quarter-pixel position [0xB07C] / [0xB07E] (0-2559 / 0-1759, set by the host's mouse input;
     * the interrupt's mouse step clears the counter whenever x differs from y) */
    int idle_ticks, blanked, mouse_qx, mouse_qy;
    int mouse_pqx, mouse_pqy; /* FIX-1 (docs/fixes.md): the previous interrupt's quarter-pixel position */
    int quit;
    int load_failed; /* a damaged save was loaded: the game ends after the message */
    int animation_shown[4]; /* last image of each chief-doctor animation */
    int estate_slot; /* slot chosen on the cancel/sell grids (the original keeps its address at P+0x9A) */
    DataDb people; /* BIDATP: the staff names, qualifications and peculiarities the personnel screens show */
    uint16_t staff_list[1050]; /* [0x4656]: the shuffled staff numbers the applications are browsed by */
    int roster_occupant; /* [0x5C53]: slot of the room's current employee on screen 54, -1 = none */
    int roster_stub; /* the staff button that opened screen 54 (index into PERSONNEL_ROSTER) */
    int advice_from; /* [0x5C85]: where the advice board's thumbs-down returns (0 office, 2 site map, else reception) */
    uint8_t hiscore[560]; /* the high-score table GAMES\QUACK (docs/saves.md), read at the start */
    int hiscore_dirty; /* [0x5CAF]: a new entry; the program's end writes the file */
    int key; /* the key of this frame for the text input field (a DOS code-page character, 8, 13), -1 none */
    int input_active, input_entry, input_x, input_y, input_count; /* the high-score name input (docs/gui.md) */
    uint8_t input_back[240 * 20]; /* [0xAFA4]: the field's background */
    Sound sound; /* docs/sound.md: effects, background slot and songs; the window build plays its output */
    int help_strip_was; /* [0xB10C] at the last frame: the help jingle starts when it lights up (0xBDE6) */
    SheetMemory sheet; /* [0x5AB9] and [0x5B71]: kept between the patient sheets (docs/ui-sheets.md) */
    int anims_on; /* BIING!.CFG ANIMS ([0xAF8C]): the animation images are drawn (docs/saves.md) */
    /* the warehouse animation's loop positions (byte offsets, not saved; docs/ui-sheets.md, section 3) and the
     * clerk's mouth image last drawn */
    int warehouse_spider, warehouse_hand, warehouse_mouth;
    /* the reception animation's state (docs/ui-rooms.md, screen 19's frame; not saved, all start at 0): the
     * positions of lists A, B, E and the last-drawn images of picks C, D */
    int reception_a, reception_b, reception_e, reception_c, reception_d;
    int screenshots; /* German CD: `*` screenshots written so far (ASA01.LBM, ...; docs/editions.md 7.2) */
} Biing;

int biing_open(Biing *b, const char *root);
void biing_close(Biing *b);
/* One 50 Hz timer interrupt: tick credit, the idle palette, then the left button with its auto-repeat. */
void biing_interrupt(Biing *b, int button_held);
#define BIING_IDLE_TICKS 15000 /* 5 minutes with the cursor on the diagonal x = y blank the screen */
/* The palette the DAC holds now: DATA/COL, or all black while the screen is idle-blanked. */
const uint8_t *biing_palette(const Biing *b);
/* One pass of the main loop: the clock (at most one game tick), then the screen layer. */
void biing_frame(Biing *b);
/* Clock and player cash, read from and written to the world. */
int biing_clock(const Biing *b, int tail_field);
long biing_cash(const Biing *b);
void biing_set_cash(Biing *b, long v);
/* Trainer restore: validate before replacing state, then return to the office.
 * A save restores game records, not the former screen or unsaved process caches. */
int biing_restore_checkpoint(Biing *b, const char *path);
/* Shared with the screens in other files (estate_ui.c): append a BITEXPZ entry to the message buffer; draw
 * the message parchment 165, a picture and the message buffer (docs/screens.md, "Message boxes"). */
void biing_msg_entry(Biing *b, int entry);
/* English CD: "a " / "an " by the first letter of text `name` (docs/editions.md 7.3, rule A) */
void biing_english_article(Biing *b, int name);
void biing_message_box(Biing *b, int picture);
/* The office (0x2C79B), for the screens in other files. */
void biing_office(Biing *b);
/* The desktop entry (0x3B464, also every desktop window's thumb): starts the clock, draws 1040, screen 97. */
void biing_desktop(Biing *b);
/* German CD, the GUI frame's `*` key (OTH-DE-12): the frame buffer's 640 x 400 picture as ASAnn.LBM in
 * the game folder; returns 0 when written. */
int biing_screenshot(Biing *b);
/* The room animations' two helpers (docs/ui-rooms.md, "The two kinds of helper"; docs/ui-sheets.md section 3), for
 * the room frames. Every image at its IXY position and only with ANIMS on (0x1AEAC); positions, last-drawn words and
 * draws advance either way. A list (0x29663 / 0x296DD): the u16 image at byte position *pos of the table at `table`
 * (a disk-build offset) is drawn, then *pos advances by 2 and wraps to 0 at entries x 2. A pick (0x29738): draw r1;
 * r1 = 0 -> draw r3, take entry r3 of the four, and when it differs from *last store and draw it. */
void biing_anim_image(Biing *b, int image);
void biing_anim_list(Biing *b, uint32_t table, int entries, int *pos);
void biing_anim_pick(Biing *b, uint32_t table, int *last);
/* Runs a routine of the original as a hotspot would (the screen layer's call), for the tests. */
void biing_routine(Biing *b, uint32_t routine);

#endif
