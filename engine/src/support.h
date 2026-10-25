/* The player's support rooms and administration extras (docs/ui-support.md): the kitchen's menu (103), the golf
 * report (148), the advertising campaign (119, 121), the vehicle market (130, 134) and the thugs (110). Like bank.c
 * and depts.c these work on the simulation's world alone (hospital record 0, the player block, the kitchen, golf and
 * garage records, the car offers of save block 33, the staff slots), so the tests run them without a screen; the
 * screens are in support_ui.c. A result is the BITEXPZ text number of its message box (0 = done without a box). */
#ifndef BIING_SUPPORT_H
#define BIING_SUPPORT_H

#include "sim.h"

enum { /* kitchen record (block 8, docs/ui-support.md "Kitchen record") */
    KR_COOKING = 5, KR_COOK = 6, KR_RECIPE = 8, KR_MEAL = 0x0A, KR_LEFT = 0x0C, KR_LEVEL = 0x0E,
};
enum { GR_ATTENDANT = 2, GR_PRESENT = 5, GR_GOLFERS = 6 }; /* golf record (block 10) */
enum { /* the campaign fields of the hospital record */
    AD_TYPE = 0xF4, AD_SLOGAN = 0xF5, AD_DURATION = 0xF6, AD_COST = 0xF8, AD_LEFT = 0xFA, AD_GOOD = 0xFC,
    AD_NEUTRAL = 0xFE, AD_BAD = 0x100, AD_DAY = 0x102, AD_SHOWN = 0x106, AD_COOL = 0x108, AD_TOTAL = 0x146,
};
enum { /* garage record (block 5) and car offers (block 33) */
    GA_PLACES = 0x0A, GA_CARS = 0x0B, GA_PLACE = 0x58, GA_PLACE_COUNT = 10,
    CO_SELLER = 0, CO_CAR = 1, CO_PRICE = 2, CO_CONDITION = 4, CO_AGE = 5, CAR_OFFERS = 20, CAR_OFFER_SIZE = 20,
};
enum { SU_P_SHOWN = 0x0E, SU_P_SLOT = 0, SU_P_GROUP = 0x6C }; /* player block: offer / car place shown, thug slot */
enum { /* texts */
    SU_NO_GARAGE = 976, SU_RACE = 1177, SU_NO_OFFER = 979, SU_GARAGE_FULL = 977, SU_NO_CASH = 989, SU_BOUGHT = 990,
    SU_OWN_OFFER = 2749, SU_SELL_NO_GARAGE = 1175, SU_NO_CAR = 1176, SU_NO_CARS = 996, SU_MARKET_FULL = 1178,
    SU_OFFERED = 1179, SU_NO_THUG = 1499, SU_THUG_SENT = 911,
    SU_GOLF_IDLE = 1649, SU_GOLF_NO_NURSE = 1648,
    SU_AD_SIGN = 1471, SU_AD_BANNER = 1472, SU_AD_LEAFLETS = 1473,
};
#define CAR_CATALOG 0x3B43 /* 20 bytes per car id 0-6: +0 name text, +2 speed, +4 power, +6 list price, +8 text, +10 image */
#define CAR_IDS 7

typedef struct {
    int car, price, seller; /* what the message names */
} CarResult;

/* ---- the kitchen's menu (103) ------------------------------------------------------------------------------- */

uint8_t *support_kitchen(Sim *s); /* the player's kitchen record */
void support_recipe_search(Sim *s); /* 0x4DEF3: the next recipe of the chosen level; the dish in progress dropped */
void support_next_dish(Sim *s); /* 0x4DEE3 */
int support_cheaper(Sim *s); /* 0x4DF2B: 1 when something changed (a redraw), 0 nothing */
int support_dearer(Sim *s); /* 0x4DF9E */
void support_cooking(Sim *s); /* 0x4DFE7: the switch */
int support_utensils(Sim *s, int recipe, int out[4]); /* the recipe's utensil numbers (until a negative byte) */

/* ---- the golf report (148) ----------------------------------------------------------------------------------- */

/* the golfers the report names, as 0x3AC80 walks G's list for n = G +5 names (out holds up to n); the count */
int support_golfers(Sim *s, int *out, int max);

/* ---- advertising (119, 121) -------------------------------------------------------------------------------- */

int support_campaign_cost(int type, int slogan, int duration); /* 0x4D463 */
int support_campaign_open(Sim *s); /* 0x4D3EE: 119 (a new one prepared) or 121 (one exists) */
void support_campaign_shorter(Sim *s); /* 0x4E105 */
void support_campaign_longer(Sim *s); /* 0x4E0E0 */
void support_campaign_slogan(Sim *s); /* 0x4E0BA */
void support_campaign_type(Sim *s); /* 0x4E094 */
int support_campaign_start(Sim *s); /* 0x4E014: no cash test; 1471, 1472 or 1473 */
void support_campaign_discard(Sim *s); /* 0x4E127 */
int support_campaign_reached(Sim *s); /* N = the three counts, written to +0x106 (0x4DA2E) */
int support_campaign_changed(Sim *s); /* the frame 0x4DA07: the counts differ from +0x106 */
void support_campaign_confirm(Sim *s); /* 0x4E16D: an ended campaign cleared */

/* ---- the vehicle market (130, 134) ------------------------------------------------------------------------- */

uint8_t *support_car_offer(Sim *s, int i); /* offer i of save block 33 */
uint8_t *support_garage(Sim *s); /* the player's garage record */
int support_car_list(Sim *s, int id); /* the catalog's list price */
int support_car_sale_price(int list, int condition); /* the price of the player's own offer */
int support_buy_open(Sim *s); /* 0x4CEDD: 0 (pb+0x0E = the first offer), 976, 1177 or 979 */
int support_buy_next(Sim *s); /* 0x4EC7F: 0 or 979 */
int support_buy(Sim *s, CarResult *o); /* 0x4ECD5: 977, 989, 990 (bought) or 2749 (one's own offer) */
int support_sell_open(Sim *s); /* 0x4D045: 0 (pb+0x0E = the car place), 1175, 1176 or 1177 */
int support_sell_next(Sim *s); /* 0x502F2: 0 or 996 */
int support_sell(Sim *s, CarResult *o); /* 0x50182: 1178 or 1179 */

/* ---- thugs (110) ------------------------------------------------------------------------------------------- */

int support_thug_fits(Sim *s, int slot, int group); /* a staff index, the group, a state not on a mission or away */
int support_thugs_open(Sim *s); /* 0x48600: 0 (pb+0 = the thug's slot) or 1499 */
int support_thug_sheet_nr(int slot); /* the "Nr." field of the thug sheet (FIX-2, docs/fixes.md) */
int support_thug_step(Sim *s, int step); /* 0x4852F / 0x4859A: 0, or -1 (none: the entry again) */
void support_thug_send(Sim *s); /* 0x4833A: the mission; the box is 911 + name + 912 */

#endif
