#ifndef BIING_DESKTOP_UI_H
#define BIING_DESKTOP_UI_H

#include "game.h"

/* Runs one of the desktop's remaining routines (the patient report 146, the holdings report 153, the quit dialog
 * 156, the high-score entry 158 and list 159; docs/desktop.md, docs/ui-garage.md); 0 if `routine` is not one. */
int desktop_routine(Biing *b, uint32_t routine);
/* Once per frame: the high-score name input (one key) and the main loop's step from 158 to 159 (0x6D3C1). */
void desktop_frame(Biing *b);

#endif
