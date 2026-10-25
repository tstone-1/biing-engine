/* The newspaper's texts (docs/ui-reports.md, section 3): the date and the text builder 0x670C7 over the items of
 * module H (save block 31, sim_news.c). Screen-free: the builder returns pieces (pieces.h), the page 118 is drawn in
 * reports_ui.c. Building a text draws no random numbers. The copy-protection setter 0x1A3EA the original's handlers
 * call is left out. */
#ifndef BIING_NEWSPAPER_H
#define BIING_NEWSPAPER_H

#include <stddef.h>

#include "pieces.h"
#include "sim.h"

/* Item number n (1-100) of the issue slot's byte, headline (1) or body (0); 0 when the text is empty (an unknown
 * type, or an unhandled b21 or selector value). */
int news_text(Sim *s, int number, int headline, Pieces *t);
/* The same with the item number [0x45CF] as a variable the builder may change: type 0x27 stores its b22 into it, so
 * the page, which sets the number once per slot before the headline, builds that slot's body from item b22. */
int news_text_at(Sim *s, int *number, int headline, Pieces *t);
/* "DD.MM.199Y" (0x47419): out gets 10 characters and a 0. */
void news_date(Sim *s, char out[11]);
/* The four issue bytes all set ([0x6961]-[0x6964]): the page can be shown. */
int news_issue_ready(Sim *s);
/* The plural "n" step of {F}/{E} (docs/editions.md 7.3.1, the code's rule): the noun text t (n bytes, raw and
 * case-sensitive, hyphenation marks counted) copied into out (at most cap bytes; returns the length) and changed at the
 * first '(' at p: unless the byte at p - 2 is 'n' or 's', the byte at p - 1 becomes 'n' and a space is inserted
 * before the '(' ("Soup (hot)" -> "Soupn (hot)", "Stew(hot)" -> "Sten (hot)"); with no '(' an 'n' is appended unless
 * the last byte is 'n', 's' or ')'. */
size_t news_plural(const uint8_t *t, size_t n, uint8_t *out, size_t cap);

#endif
