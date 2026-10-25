/* The player's blood and organ bank (docs/ui-bank.md): buying blood and organs from the market (screens 88, 89), the
 * draft offers of 87 and 150 with their buttons and the publish, the numbers of the offer text and of the two stock
 * reports (85, 86). Like rooms.c and depts.c these work on the simulation's world alone (the player's bank record,
 * the blood/organ market of save block 24, hospital record 0 and the seller's record), so the tests run them
 * without a screen; the screens are in bank_ui.c. A result is the BITEXPZ text number of its message box (0 = done
 * without a box). "P" is the player block (save block 28): P+0 is the market slot shown, P+0x0E the bank slot of an
 * organ draft (150) or the page of the organ report (86). */
#ifndef BIING_BANK_H
#define BIING_BANK_H

#include "sim.h"

enum { BANK_P_OFFER = 0, BANK_P_SLOT = 0x0E }; /* player block fields */
enum { /* market slot fields (docs/sim-ai.md, "Markets") */
    BM_SELLER = 0, BM_KIND = 1, BM_QUANTITY = 2, BM_ID = 2, BM_QUALITY = 3, BM_PRICE = 4, BM_AGE = 6, BM_RESERVED = 8,
};
enum { /* the bank record (docs/sim-rooms2.md, "Blood and organ bank") */
    BR_STOCK = 0x0C, BR_CAPACITY = 0x10, BR_HELD = 0x14, BR_PLACES = 0x16, BR_LOST = 0x66, BR_SLOTS = 0x58,
    BR_SLOT_COUNT = 30,
};
enum { /* texts */
    BANK_NO_BLOOD_OFFER = 1642, BANK_NO_ORGAN_OFFER = 1641, BANK_BLOOD_NO_CASH = 1634, BANK_BLOOD_BOUGHT = 1635,
    BANK_BLOOD_OWN = 1631, BANK_ORGAN_NO_CASH = 1633, BANK_ORGAN_FULL = 1632, BANK_ORGAN_BOUGHT = 1639,
    BANK_LOW_STOCK = 2546, BANK_MARKET_FULL = 1644, BANK_NO_ORGANS = 2547, BANK_BLOOD_PUBLISHED = 1643,
    BANK_ORGAN_PUBLISHED = 2548,
};

typedef struct {
    int seller, quantity, price, id, quality; /* what the result message names */
} BankResult;

uint8_t *bank_record(Sim *s); /* B, the player's bank record; NULL without a bank */
uint8_t *bank_offer(Sim *s); /* M[P+0], the market slot shown */
int bank_slot_shown(Sim *s); /* P+0 */

/* ---- 88 / 89: buying -------------------------------------------------------------------------------------- */

int bank_buy_open(Sim *s, int organ); /* 0x4D8E3 / 0x4AEDE: 0 (P+0 set, the offer reserved), 1642 / 1641 */
int bank_buy_step(Sim *s, int organ, int step); /* next (+1) / previous (-1): 0, or 1642 / 1641 */
void bank_back(Sim *s); /* 0x4B5B6: the offer unreserved, a draft freed */
int bank_buy_blood(Sim *s, BankResult *o); /* 0x4F777: 1634, 1635 (bought) or 1631 (own offer) */
int bank_buy_organ(Sim *s, BankResult *o); /* 0x4F693: 1633, 1632 or 1639 (bought, also the own offer) */

/* ---- 87 / 150: selling -------------------------------------------------------------------------------------- */

int bank_sell_blood_open(Sim *s); /* 0x4D947: 0 (a draft in P+0), 2546 or 1644 */
int bank_sell_organ_open(Sim *s); /* 0x4B606: 0 (a draft in P+0, its bank slot in P+0x0E), 2547 or 1644 */
void bank_blood_more(Sim *s); /* 0x4C6E0 */
void bank_blood_less(Sim *s); /* 0x4C74B */
void bank_price_step(Sim *s, int step); /* dearer (+1, at most 600) / cheaper (-1, at least 1) */
int bank_organ_step(Sim *s, int step); /* 0x4F919 / 0x4F8CF: 0 (the next organ), -1 (none: back) */
int bank_publish_blood(Sim *s); /* 0x4BF19: 1643 */
int bank_publish_organ(Sim *s); /* 0x4BF52: 2548 */

/* ---- the offer text (0x4AFB7) and the reports --------------------------------------------------------------- */

/* line 2 of a blood offer: the value per 5,000 cl, divided by the low byte of the quantity (an original fault) */
uint32_t bank_value_per_5000(int price, int quantity);
/* line 3: the average price per 5,000 cl of the published blood offers; *last ([0xAEA0]) is kept when none counts */
uint32_t bank_average_blood(Sim *s, uint32_t *last);
int bank_organ_offers(Sim *s, int id, int *price_sum); /* line 3 of an organ offer: offers of that organ */

typedef struct {
    int held, free_places, places, average, minimum; /* page 0 of the organ report */
} BankOrganReport;
void bank_organ_report(Sim *s, BankOrganReport *r);
void bank_report_page(Sim *s, int step); /* 0x4B7EA / 0x4B7CE: P+0x0E + 1 (3 -> 0) / - 1 (-1 -> 2) */
/* the organs listed on page 1 or 2: slots *first .. *first + n - 1, stopping at the first empty slot */
int bank_report_list(Sim *s, int page, int *first);

#endif
