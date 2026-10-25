/* The rest of the desktop (docs/desktop.md, docs/ui-garage.md "The rest of the desktop"): the patient report 146,
 * the holdings report 153, the quit dialog 156, the high-score entry 0x5A717 with the name input 158 and the list
 * 159, and the program's end 0xE085 that writes GAMES\QUACK. The counts and the table are desktop.c's. The
 * high-score entry requests its song (docs/sound.md, 0x5A74D). */
#include "desktop_ui.h"

#include <stdio.h>
#include <string.h>

#include "desktop.h"
#include "rooms_ui.h"
#include "sim_internal.h"

enum {
    IMG_BODY = 1216, IMG_THUMB = 1247, IMG_ARROW_RIGHT = 1383, IMG_ARROW_LEFT = 1385, IMG_PATIENTS = 1219,
    IMG_HOLDINGS = 1221, IMG_QUIT = 957, IMG_SPACESHIP = 959, IMG_HISCORE = 958, SCR_PATIENTS = 146,
    SCR_HOLDINGS = 153, SCR_QUIT = 156, SCR_ENTRY = 158, SCR_LIST = 159,
};
#define NAMES 0x3BCF
#define FIELD_W 240
#define FIELD_H 20

static void draw(Biing *b, int image, int dx, int dy) { dulog_draw(&b->ui, image, dx, dy); }
static void screen(Biing *b, int n) { dulog_set_screen(&b->ui, n); }
static void clear(Biing *b) { rooms_ui_clear(b); }
static void text(Biing *b, int entry) { rooms_ui_text(b, entry); }
static void num(Biing *b, long v) { rooms_ui_num(b, v); }
static TextLayout layout(Biing *b, int x, int y, int width, int gap) {
    return (TextLayout){&b->fonts[7], b->prog.charmap, x, y, width, gap, 4, 1};
}
static void left(Biing *b, int x, int y, int width) {
    TextLayout l = layout(b, x, y, width, 1);
    text_left(&b->screen, &l, b->msg.buf, b->msg.len);
}
static void centred_str(Biing *b, const char *s, int x, int y, int width) {
    TextLayout l = layout(b, x, y, width, 3);
    text_centred(&b->screen, &l, (const uint8_t *)s, strlen(s));
}
static void number_at(Biing *b, long v, int x, int y) {
    char s[16];
    snprintf(s, sizeof s, "%ld ", v);
    centred_str(b, s, x, y, 85);
}

/* ---- 146 Patientengesamtübersicht (0x3D2FF) --------------------------------------------------------------- */

static void count(Biing *b, int n) { /* 0: entry 1789 (which runs on into 1790), 1: entry 1790, else the number */
    if (n == 0) text(b, 1789);
    else if (n == 1) text(b, 1790);
    else num(b, n);
}

static void patients_body(Biing *b) {
    PatientCounts c;
    desktop_patient_counts(&b->sim, &c);
    draw(b, IMG_BODY, 6, 3);
    draw(b, IMG_THUMB, 6, 3);
    clear(b);
    if (!c.ill) {
        text(b, 1532);
        left(b, 114, 120, 420);
        screen(b, SCR_PATIENTS);
        return;
    }
    text(b, 1533), count(b, c.ill), text(b, 1534), count(b, c.teeth), text(b, 1535), count(b, c.mind), text(b, 1536);
    count(b, c.other), text(b, 1537);
    left(b, 114, 120, 420);
    clear(b);
    if (c.in_hospital == 0) text(b, 1541);
    else if (c.in_hospital == 1) text(b, c.inpatients ? 1543 : 1542);
    else {
        text(b, 1540), num(b, c.in_hospital), text(b, 1538);
        if (c.inpatients == 0) text(b, 1544);
        else if (c.inpatients == 1) text(b, 1545);
        else text(b, 1546), num(b, c.inpatients), text(b, 1539);
    }
    left(b, 114, 172, 420);
    if (c.at_home || c.looking) { /* else the buffer, still paragraph 2, is drawn a second time (as the original) */
        clear(b);
        if (c.at_home == 0) text(b, 1550);
        else if (c.at_home == 1) text(b, 1547);
        else text(b, 1548), num(b, c.at_home), text(b, 1549);
        if (c.looking == 0) text(b, c.at_home ? 1552 : 1551);
        else if (c.looking == 1) text(b, 1553);
        else {
            text(b, 1554), num(b, c.looking);
            int hour = world_tail(&b->world, TAIL_HOUR);
            text(b, hour >= 19 ? 1786 : hour < 4 ? 1787 : 1555);
        }
    }
    left(b, 114, 224, 420);
    clear(b);
    if (c.dead == 0) text(b, 1556);
    else if (c.dead == 1) text(b, 1557);
    else text(b, 1558), num(b, c.dead), text(b, 1559);
    left(b, 114, 276, 420);
    screen(b, SCR_PATIENTS);
}

static void patients(Biing *b) {
    draw(b, IMG_PATIENTS, 6, -22);
    b->world.block[BLK_PLAYER0][0x2E] = 0; /* 0x3D304: the first refresh comes at the next minute */
    patients_body(b);
}

static void patients_frame(Biing *b) { /* 0x3D2F0: once per game minute */
    uint8_t *flag = &b->world.block[BLK_PLAYER0][0x2E];
    if (!*flag) return;
    *flag = 0;
    patients_body(b);
}

/* ---- 153 Firmenbesitz (0x3D826) -------------------------------------------------------------------------- */

static void holdings_draw(Biing *b) { /* from the header on */
    int h = get16(b->world.block[BLK_PLAYER0] + 0x0E);
    if (h < 0 || h > 15) h = 0; /* engine guard */
    draw(b, IMG_HOLDINGS, 6, -22);
    draw(b, IMG_BODY, 6, 3);
    draw(b, IMG_THUMB, 6, 3);
    draw(b, IMG_ARROW_RIGHT, 6, 3);
    draw(b, IMG_ARROW_LEFT, 6, 3);
    clear(b);
    const uint8_t *name = program_table(&b->prog, NAMES) + 30 * h;
    size_t n = 0;
    while (n < 29 && name[n] != '$' && name[n]) n++;
    msg_append(&b->msg, name, n);
    left(b, 114, 96, 350);
    int total[19], rented[19], owned[19];
    desktop_holdings(&b->sim, h, total, rented, owned);
    for (int k = 2; k <= 18; k++) {
        int y = 120 + 13 * (k - 2);
        clear(b), text(b, 2718 + k), left(b, 114, y, 200);
        number_at(b, total[k], 234, y);
        number_at(b, rented[k], 276, y);
        number_at(b, owned[k], 332, y); /* "gekauft": the owned slots */
        if (k == 2) {
            const uint8_t *r = world_hospital(&b->world, h);
            number_at(b, (uint16_t)get16(r + 0x1C), 400, y);
            number_at(b, (uint16_t)get16(r + 0x1A), 466, y);
        }
    }
    screen(b, SCR_HOLDINGS);
}

static void holdings(Biing *b) {
    put16(b->world.block[BLK_PLAYER0] + 0x0E, 0); /* the current player's number */
    holdings_draw(b);
}

/* ---- 156 the quit dialog (0x3BC12) ----------------------------------------------------------------------- */

static void quit_dialog(Biing *b) {
    b->clock_stopped = 1;
    draw(b, IMG_QUIT, 6, -22);
    draw(b, IMG_BODY, 6, 3);
    draw(b, IMG_THUMB, 6, 3);
    draw(b, IMG_SPACESHIP, 6, 3);
    clear(b), text(b, 2751), left(b, 114, 120, 420);
    screen(b, SCR_QUIT);
}

/* ---- the high-score list (0x5A717, 0x5A95E, 0x6D3C1) ------------------------------------------------------ */

static void list_rows(Biing *b) { /* 0x5A95E */
    for (int i = 0; i < HISCORE_ENTRIES; i++) {
        const uint8_t *e = b->hiscore + HISCORE_ENTRY * i;
        int y = 94 + 14 * i;
        char s[8];
        snprintf(s, sizeof s, "%02d.", i + 1);
        centred_str(b, s, 83, y, 35);
        size_t n = 0;
        while (n < HISCORE_NAME && e[n] != '$' && e[n]) n++;
        TextLayout l = layout(b, 128, y, 400, 3);
        text_plain(&b->screen, &l, e, n);
        number_at(b, get32(e + 20), 364, y);
        number_at(b, (uint16_t)get16(e + 24), 441, y);
        number_at(b, (uint16_t)get16(e + 26), 505, y);
    }
}

static void show_list(Biing *b) { /* 0x6D3C1: 958, the list, screen 159 */
    draw(b, IMG_HISCORE, 0, 0);
    list_rows(b);
    screen(b, SCR_LIST);
}

static void field_draw(Biing *b) { /* restore the field's background, then the buffer in the plain renderer */
    for (int y = 0; y < FIELD_H; y++) {
        int sy = b->input_y + y;
        if (sy < 0 || sy >= SCREEN_H) continue;
        for (int x = 0; x < FIELD_W && b->input_x + x < SCREEN_W; x++)
            b->screen.px[sy * SCREEN_W + b->input_x + x] = b->input_back[y * FIELD_W + x];
    }
    uint8_t *name = b->hiscore + HISCORE_ENTRY * b->input_entry;
    size_t n = 0;
    while (n < HISCORE_NAME && name[n] != '$' && name[n]) n++;
    TextLayout l = layout(b, b->input_x, b->input_y, FIELD_W, 3);
    text_plain(&b->screen, &l, name, n);
}

static void hiscore_entry(Biing *b) { /* 0x5A717 */
    b->clock_stopped = 1;
    sound_background(&b->sound, 0xAF, 0x140, 25); /* 0x5A74D: HIFANF, "Gods in White" */
    dulog_clear_strip(&b->ui); /* 0xBE21 */
    b->ui.help_enabled = b->sim.help_strip = 0;
    clear(b);
    dulog_draw_quiet(&b->ui, IMG_HISCORE, 0, 0);
    int entry = hiscore_enter(&b->sim, b->hiscore, NULL, NULL);
    if (entry >= 0) b->hiscore_dirty = 1; /* [0x5CAF] */
    list_rows(b);
    if (entry < 0) {
        show_list(b);
        return;
    }
    b->input_entry = entry;
    b->input_x = 128, b->input_y = 94 + 14 * entry;
    for (int y = 0; y < FIELD_H; y++) /* [0xAFA4]: the field's background */
        for (int x = 0; x < FIELD_W; x++) {
            int sx = b->input_x + x, sy = b->input_y + y;
            b->input_back[y * FIELD_W + x] = sx < SCREEN_W && sy < SCREEN_H ? b->screen.px[sy * SCREEN_W + sx] : 0;
        }
    hiscore_name_start(b->hiscore + HISCORE_ENTRY * entry, &b->input_count);
    field_draw(b);
    b->input_active = 1;
    b->key = -1;
    screen(b, SCR_ENTRY);
}

void desktop_frame(Biing *b) {
    if (b->input_active) {
        int key = b->key;
        b->key = -1;
        if (key < 0) return;
        int done = hiscore_name_key(b->hiscore + HISCORE_ENTRY * b->input_entry, &b->input_count, key);
        field_draw(b);
        if (done) b->input_active = 0; /* [0xB0B4] cleared */
        return;
    }
    if (b->ui.current == SCR_ENTRY) show_list(b); /* the main loop's step 3: 158 with no input open */
}

static void end_program(Biing *b) { /* 0xE085 */
    if (b->hiscore_dirty) {
        char path[1200];
        game_path(&b->assets, "GAMES/QUACK", path, sizeof path);
        if (quack_save(path, b->hiscore)) fprintf(stderr, "[ERROR] cannot write %s\n", path);
        b->hiscore_dirty = 0;
    }
    b->quit = 1;
}

/* ---- dispatch ------------------------------------------------------------------------------------------- */

int desktop_routine(Biing *b, uint32_t r) {
    switch (r) {
    case 0x3D2FF: patients(b); return 1;
    case 0x3D2F0: patients_frame(b); return 1;
    case 0x3D826: holdings(b); return 1;
    case 0x3D7F6: desktop_holdings_step(&b->sim, 1), holdings_draw(b); return 1;
    case 0x3D80E: desktop_holdings_step(&b->sim, -1), holdings_draw(b); return 1;
    case 0x3BC12: quit_dialog(b); return 1;
    case 0x5A717: hiscore_entry(b); return 1;
    case 0x6D3C1: show_list(b); return 1;
    case 0xE085: end_program(b); return 1;
    default: return 0;
    }
}
