#ifndef BIING_ESTATE_UI_H
#define BIING_ESTATE_UI_H

#include "game.h"

/* Runs one of the estate agent's routines (screens 6, 11-14, 16, 17, 3, 4, 104-108; docs/ui-estate.md);
 * 0 if `routine` is not one of them. */
int estate_routine(Biing *b, uint32_t routine);

#endif
