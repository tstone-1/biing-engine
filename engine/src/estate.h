/* The estate agent's transactions for the player (hospital record 0): plots and building units, from
 * docs/ui-estate.md. They work on the simulation's world alone, so the tests run them without a screen; the
 * screens that call them are in estate_ui.c. A refusal is returned as its BITEXPZ text number, 0 = done. */
#ifndef BIING_ESTATE_H
#define BIING_ESTATE_H

#include "sim.h"

#define ESTATE_TYPES 38 /* room types 0-37 */
#define ESTATE_CELLS 18 /* grid cells = room categories 1-18 */

enum { /* refusal texts */
    ESTATE_NO_CASH_RENT = 249, ESTATE_NO_CASH_BUY = 248, ESTATE_NOTHING_TO_CANCEL = 318,
    ESTATE_NOTHING_TO_SELL = 319, ESTATE_NO_PLOTS = 582, ESTATE_NO_TYPE = 580, ESTATE_NO_SLOT = 1630,
    ESTATE_TOO_FEW_PLOTS = 581, ESTATE_TOO_EXPENSIVE = 542,
};

int estate_owned(Sim *s); /* h+0x1A */
int estate_rented(Sim *s); /* h+0x1C */
int estate_free(Sim *s); /* owned + rented - built (h+0x1E) */

/* Plot contracts 11, 13, 14, 104. n is the amount on the contract. */
int estate_plot_cost(int buy, int n); /* 44 n (rent) or 300 n + 300 n / 4 (buy) */
int estate_rent_plots(Sim *s, int n); /* the check is 22 n against the cash, the charge 44 n */
int estate_buy_plots(Sim *s, int n, int *converted); /* *converted = rented plots turned into owned ones */
void estate_cancel_plots(Sim *s, int n); /* no cash check */
void estate_sell_plots(Sim *s, int n);
int estate_cancel_plots_allowed(Sim *s); /* entry checks of 14 and 104: 0 or the refusal text */
int estate_sell_plots_allowed(Sim *s);
/* "more" and "fewer": the new amount. buy selects the cost rule of 11/13; screen is 11, 13, 14 or 104. */
int estate_more(Sim *s, int screen, int n);
int estate_fewer(int n);

/* Building units. A slot is an index 0-199 into the player's room slots (type byte, rented byte). */
int estate_build_blocked(Sim *s, int cat, int type); /* 0x1FE90 */
int estate_rented_slot_of_category(Sim *s, int cat); /* first slot of a room of cat that is rented, or -1 */
/* 0x39635: from *type, stepping by step and wrapping into 0-37, up to 300 tries; 0 if none is buildable. */
int estate_type_search(Sim *s, int *type, int step);
/* 0x219D3: the slot that cancelling (rented = 1) or selling (rented = 0) category cat would free, or -1 when
 * the cell is blocked. *type gets the room type of the category's last room. */
int estate_removal_slot(Sim *s, int cat, int rented, int *type);
/* Click on a cancel/sell grid cell: the slot of the unit that goes, or -1 (blocked: the cell does nothing).
 * Lowers the category's room count at once (quirk: before anything is confirmed). */
int estate_removal_pick(Sim *s, int cat, int rented, int *type);
/* "Back" on 106/108 (0x48465): count of category index + 1 raised again, whether or not it was lowered. */
void estate_removal_back(Sim *s, int category_index);
int estate_rent_unit(Sim *s, int type); /* confirm of 16 */
/* confirm of 17; *bought_out = 1 when it bought out a rented unit of the type, *price = what it cost */
int estate_buy_unit(Sim *s, int type, int *bought_out, int *price);
int estate_cancel_unit(Sim *s, int slot); /* confirm of 106: returns the fee 2 x rent + 8 */
int estate_sell_unit(Sim *s, int slot); /* confirm of 108: returns base - base/16 - 8 */
int estate_unit_price(Sim *s, int type); /* base + base/4 */
int estate_unit_rent(Sim *s, int type);
int estate_unit_base(Sim *s, int type);
int estate_unit_plots(Sim *s, int type);
/* The room name: catalog bytes 0-29 up to the first '$'. */
const uint8_t *estate_room_name(Sim *s, int type, size_t *len);
/* Slots with a room: owned (rented byte 0) and rented ones, for the building overview. */
void estate_unit_counts(Sim *s, int *owned, int *rented);
/* The player's room slots and room counts. */
uint8_t *estate_slots(Sim *s);
uint8_t *estate_counts(Sim *s);

#endif
