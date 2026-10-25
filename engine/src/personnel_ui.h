#ifndef BIING_PERSONNEL_UI_H
#define BIING_PERSONNEL_UI_H

#include "game.h"

/* Runs one of the personnel routines (screens 10, 20, 99, 46, 50, 51, 33, 36, 54, 55, 128, 129;
 * docs/ui-personnel.md); 0 if `routine` is not one of them. */
int personnel_routine(Biing *b, uint32_t routine);
/* The personnel sheet 0x64C63 from its step 3 on (portrait, fields, the four lines) for a staff slot of hospital 0;
 * shared with the thugs' screen 110 (support_ui.c). */
void personnel_ui_sheet_body(Biing *b, int slot);
/* the same with the "Nr." field given: the thug sheet prints 17 there (docs/ui-support.md, section 110) */
void personnel_ui_sheet_body_nr(Biing *b, int slot, int number);

#endif
