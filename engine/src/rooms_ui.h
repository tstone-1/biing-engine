#ifndef BIING_ROOMS_UI_H
#define BIING_ROOMS_UI_H

#include "game.h"
#include "rooms.h"

/* Runs one of the site-map and core-room routines (screens 5, 133, 19, 29, 23, 38, the patient sheets 57, 59, 61,
 * 62, 109 and the screens 64, 149, 98, 53, 58, 60, 63, 92, 95; docs/ui-sitemap.md, docs/ui-rooms.md); 0 if
 * `routine` is not one of them. */
int rooms_routine(Biing *b, uint32_t routine);

/* Shared with the specialist departments (depts_ui.c): the message buffer, the message boxes (a picture, then the
 * OK screen), the invoice box, the corpse button's box, the room selector, the site map, the patient sheet 0x43547
 * and its frame, the bed-assignment drawing of screens 64 / 81 (pb+0xC = the ward), and the occupants signature the
 * room screens redraw on (engine choice, docs/engine.md). */
void rooms_ui_clear(Biing *b);
void rooms_ui_text(Biing *b, int entry);
void rooms_ui_num(Biing *b, long v);
void rooms_ui_patient_name(Biing *b, int index);
void rooms_ui_staff_name(Biing *b, int index);
void rooms_ui_him_her(Biing *b, int index);
void rooms_ui_box(Biing *b, int picture, int screen);
void rooms_ui_box_text(Biing *b, int picture, int entry, int screen);
void rooms_ui_box_name(Biing *b, int picture, int before, int index, int after, int screen);
void rooms_ui_box_staff(Biing *b, int picture, int before, int staff, int after, int screen);
void rooms_ui_invoice(Biing *b, const RoomsResult *o, int picture, int screen);
void rooms_ui_corpse_box(Biing *b, int code, const RoomsResult *o, int screen);
void rooms_ui_selector(Biing *b, int flag, int sx, int sy);
void rooms_ui_site_map(Biing *b);
void rooms_ui_sheet(Biing *b);
void rooms_ui_sheet_frame(Biing *b);
void rooms_ui_assign(Biing *b, int index, int cat);
uint32_t rooms_ui_occupants(const uint8_t *rec);

#endif
