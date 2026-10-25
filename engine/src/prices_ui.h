#ifndef BIING_PRICES_UI_H
#define BIING_PRICES_UI_H

#include "game.h"

/* Runs one of the price screen's routines (screen 122 from administration hotspot 10; docs/ui-prices.md); 0 if
 * `routine` is not one of them. */
int prices_routine(Biing *b, uint32_t routine);

#endif
