/* Purchasing and the store room for the player (hospital record 0), from docs/ui-purchasing.md: the offer search and
 * paging of screens 151/152/26/28, the purchase (the AI's own routine 0x2F610 / 0x391D2, sim_buy_offer), the sale of
 * a store item on 111, the item lists of 65 and 111, the moves room <-> store of 65 and the item information of 147.
 * They work on the simulation's world alone, so the tests run them without a screen; the screens are in
 * purchase_ui.c. A refusal or result is returned as the BITEXPZ text number of its message box (0 = done without a
 * box). "eq" is 1 for equipment, 0 for furniture. The screens' state lives in the player block (save block 28). */
#ifndef BIING_PURCHASE_H
#define BIING_PURCHASE_H

#include "sim.h"

enum { /* player block fields (docs/ui-purchasing.md, "State the screens use") */
    PU_ROW = 0, PU_BACK = 6, PU_INDEX = 0x0A, PU_VIEW = 0x0E, PU_ROOM = 0x10, PU_EQUIPMENT = 0x26, PU_CAT = 0x4C,
    PU_MISSING = 0x4E, PU_CAPABILITY = 0x54, PU_KIND = 0x55,
};
enum { PU_ROOM_VIEW = 0, PU_STORE_VIEW = 1 };
enum { /* texts */
    PU_NO_STORE_BUY = 1625, PU_STORE_FULL = 1624, PU_NO_EQUIPMENT_OFFER = 1623, PU_NO_FURNITURE_OFFER = 1622,
    PU_BOUGHT = 1615, PU_OWN = 1619, PU_NO_CASH = 1621,
    PU_NO_STORE_SELL = 642, PU_NO_CLERK_SELL = 643, PU_NO_EQUIPMENT_HELD = 644, PU_NO_FURNITURE_HELD = 645,
    PU_MARKET_FULL_EQUIPMENT = 640, PU_MARKET_FULL_FURNITURE = 641, PU_OFFERED = 635,
    PU_NO_STORE_MOVE = 632, PU_NO_CLERK_MOVE = 631, PU_STORE_EMPTY_EQUIPMENT = 628, PU_STORE_EMPTY_FURNITURE = 629,
    PU_ROOM_FULL_EQUIPMENT = 627, PU_ROOM_FULL_FURNITURE = 626, PU_STORE_PACKED = 630,
    PU_DOCTOR_BUSY = 375, PU_DENTIST_BUSY = 376, PU_OPERATION = 377, PU_NEURO_BUSY = 378,
};
enum { PU_MOVED = 0, PU_BACK_ONLY = -1, PU_RECOUNTED = -2 }; /* purchase_move without a message box */

typedef struct {
    int id, condition, price, seller; /* the item and the offer the message names */
} PurchaseResult;

uint8_t *purchase_store(Sim *s); /* S, the player's store-room record */
uint8_t *purchase_offer(Sim *s, int eq, int i); /* market offer i (EM or FM) */
const uint8_t *purchase_item(Sim *s, int eq, int id); /* ec(id) or fc(id) */
int purchase_item_valid(int eq, int id); /* id inside the catalog */

/* ---- 151 / 152 and the offer screens 26 / 28 --------------------------------------------------------------- */

void purchase_set_filters(Sim *s, int capability, int kind); /* P+0x54, P+0x55 */
int purchase_matches(Sim *s, int eq, int i); /* in use, not reserved, passes the filters */
int purchase_search(Sim *s, int eq); /* 0x3AF1F / 0x3B02E: 0 (offer P+0x0A reserved), 1625, 1624, 1623 / 1622 */
void purchase_step(Sim *s, int eq, int step); /* previous (-1) / next (+1) matching offer */
void purchase_leave(Sim *s, int eq); /* back: the offer shown is unreserved */
int purchase_buy(Sim *s, int eq, PurchaseResult *o); /* the thumb: 1615, 1619 (own offer) or 1621 */

/* ---- 111: offer for sale ---------------------------------------------------------------------------------- */

int purchase_sell_open(Sim *s, int eq); /* 0x4899D / 0x48A55: 0 (the P fields set), 642, 643, 644 / 645 */
int purchase_sell(Sim *s, PurchaseResult *o); /* 0x486E2: 635, 640 / 641, or 0 (an empty row: nothing) */

/* ---- the list of 65 and 111 ------------------------------------------------------------------------------- */

/* Slot j of the list on screen (view P+0x0E, category P+0x4C, P+0x26): its id (<= 0 = empty), *condition. */
int purchase_list_item(Sim *s, int j, int *condition);
void purchase_page_up(Sim *s);
int purchase_page_down(Sim *s); /* 1 when the list moved (and is redrawn) */
void purchase_select(Sim *s, int row);

/* ---- 65: inventory ---------------------------------------------------------------------------------------- */

/* The inventory button of a room screen (category cat, its entry routine back): 0 (screen 65 opens), or the
 * refusal text of the treatment, dental, neurology rooms, the OP and the torture chamber (375-378). */
int purchase_inventory_button(Sim *s, int cat, uint32_t back);
void purchase_inventory_open(Sim *s); /* 0x26F75: equipment first, row 0, slot 0 */
void purchase_toggle_kind(Sim *s); /* 0x26A3A */
int purchase_toggle_view(Sim *s); /* 0x26A6A: 1 when switched (and redrawn) */
/* 0x3C3BC: PU_MOVED (redraw 65), PU_BACK_ONLY (the warehouse: back to the room), PU_RECOUNTED (empty row), or 632,
 * 631 (screen 55), 628 / 629, 627 / 626 (screen 41), 630 (screen 55). */
int purchase_move(Sim *s);
void purchase_recount(Sim *s); /* 0x3C35C */
/* 0x25E58: the item shown on 147 (0 = an empty row: back to 65), *condition as the original reads it */
int purchase_info(Sim *s, int *condition);
int purchase_room_instance(Sim *s); /* P+0x10 as the lists read it */

#endif
