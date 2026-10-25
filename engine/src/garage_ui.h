#ifndef BIING_GARAGE_UI_H
#define BIING_GARAGE_UI_H

#include "game.h"

/* Runs one of the garage's and the race screen's routines (screens 18, 132, 131, 69, 135, 133; docs/ui-garage.md);
 * 0 if `routine` is not one of them. */
int garage_routine(Biing *b, uint32_t routine);

#endif
