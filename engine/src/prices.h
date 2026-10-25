/* The price screen ("Kostenschlüssel", screen 122) for the player, from docs/ui-prices.md: the eight price lines
 * (the invoice base fee and seven keys, 1-9), the price index and its comparison with the average, and the back
 * thumb's newspaper item. Screen-free, so the tests run it directly; the screen is in prices_ui.c. The copy-protection
 * part of the index (docs/mainloop.md, section 7) is left out by design. */
#ifndef BIING_PRICES_H
#define BIING_PRICES_H

#include "sim.h"

#define PRICE_LINES 8
/* the hospital-record byte of each line in the screen's order: base fee, Schwester, OP, Geräte, Arzt, Krankenzimmer,
 * Intensiv, Material */
extern const uint8_t PRICE_FIELD[PRICE_LINES];

int prices_value(Sim *s, int line); /* the player's value of the line */
/* 0x49090 + 6 line (delta +1) / 0x49046 + 6 line (delta -1): 1 when the value changed (then the index of all 16
 * hospitals and the average are recomputed), 0 at the limit 9 / 1 (nothing happens at all) */
int prices_change(Sim *s, int line, int delta);
int prices_average(Sim *s); /* 0x19C13's average [0x45B8]: the sum of the 16 indexes / 16 */
int prices_band(int d); /* the comparison text 141-147 for d = index - average (signed 16 bits) */
/* 0x48D48: the recompute and the comparison text 141-147 for the player's index minus the average */
int prices_comparison(Sim *s);
/* 0x49007: 1 when the index differs from the last index (the news item 0x4763B may be written; the last index takes
 * the index), 0 when they are equal (nothing) */
int prices_back(Sim *s);

#endif
