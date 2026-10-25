#ifndef BIING_DEPTS_UI_H
#define BIING_DEPTS_UI_H

#include "game.h"

/* Runs one of the specialist departments' routines (screens 21, 70, 71, 93, 31, 75, 74, 94, 81, 40, 43, 45, 83, 96,
 * 39, 84, 72, 30, 73, 67, 68, 32, 126, 125, 127, 44, 90, 91; docs/ui-departments.md); 0 if `routine` is not one. */
int depts_routine(Biing *b, uint32_t routine);

#endif
