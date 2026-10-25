#ifndef BIING_BANK_UI_H
#define BIING_BANK_UI_H

#include "game.h"

/* Runs one of the blood and organ bank's routines (screens 25, 82, 85-89, 150; docs/ui-bank.md); 0 if `routine` is
 * not one. */
int bank_routine(Biing *b, uint32_t routine);

#endif
