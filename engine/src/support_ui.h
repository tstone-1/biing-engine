#ifndef BIING_SUPPORT_UI_H
#define BIING_SUPPORT_UI_H

#include "game.h"

/* Runs one of the support rooms' and administration extras' routines (screens 9, 103, 8, 24, 42, 148, 119, 121,
 * 130, 134, 110; docs/ui-support.md); 0 if `routine` is not one of them. */
int support_routine(Biing *b, uint32_t routine);

#endif
