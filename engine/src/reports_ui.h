#ifndef BIING_REPORTS_UI_H
#define BIING_REPORTS_UI_H

#include "game.h"
#include "pieces.h"

/* Runs one of the routines of the daily report, the work advice and the newspaper (screens 100, 2, 118;
 * docs/ui-reports.md); 0 if `routine` is not one. */
int reports_routine(Biing *b, uint32_t routine);
/* The daily report 0x68A45, which the office frame shows instead of everything else while pb +0x6D is set. */
void reports_daily(Biing *b);
/* Pieces into the message buffer (cleared first). */
void reports_render(Biing *b, const Pieces *t);
/* The newspaper page as the original presents it (SCR-14, docs/ui-reports.md section 3, "Where a long body ends"):
 * only frame x 88-547, y 39-399 of what the page drew reaches the screen; every pixel outside it is put back from
 * `before`, the frame as it was before the page was opened. */
enum { PAPER_X0 = 88, PAPER_X1 = 547, PAPER_Y0 = 39, PAPER_Y1 = 399 };
void reports_paper_present(Surface *s, const Surface *before);

#endif
