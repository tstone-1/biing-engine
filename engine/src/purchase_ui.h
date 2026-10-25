#ifndef BIING_PURCHASE_UI_H
#define BIING_PURCHASE_UI_H

#include "game.h"

/* Runs one of the purchasing and store routines (screens 7's frame, 151, 152, 26, 28, 27, 111, 65, 41, 147, 22 and
 * the inventory buttons of the room screens; docs/ui-purchasing.md); 0 if `routine` is not one of them. */
int purchase_routine(Biing *b, uint32_t routine);

#endif
