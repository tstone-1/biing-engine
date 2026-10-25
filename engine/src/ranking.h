#ifndef BIING_RANKING_H
#define BIING_RANKING_H

#include "game.h"

/* Runs a ranking window's click routine or its per-minute refresh entry; 0 if `routine` is neither. */
int ranking_routine(Biing *b, uint32_t routine);
/* The text of number column c (0-3) of hospital h in the window opened by `routine`, as it is drawn (centred from the
 * returned x); -1 if there is no such column. */
int ranking_cell(Biing *b, uint32_t routine, int h, int c, char *out, size_t n);

#endif
