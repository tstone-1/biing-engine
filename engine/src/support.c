/* The player's support rooms and administration extras (docs/ui-support.md); see support.h. Faults of the original
 * are kept unless they hang the game; those get a defined result marked "engine choice". */
#include "support.h"

#include <string.h>

#include "fixes.h"
#include "sim_internal.h"

#define RECIPES 0xAD90 /* 8 bytes per recipe 0-32: level, minutes, u16 meal, four utensils ended by FF */
#define RECIPE_COUNT 33
#define STAFF_SIZE 32
#define GOLF_RECORD 118

static uint8_t *pb(Sim *s) { return s->world->block[BLK_PLAYER0]; }
static int pb16(Sim *s, int off) { return get16(pb(s) + off); }
static void pset16(Sim *s, int off, int v) { put16(pb(s) + off, v); }
static int site(Sim *s, int cat) { return room_counts(s, 0)[cat]; }
static int race(Sim *s) { return get16(s->world->block[BLK_34]) != 0; } /* [0x552A], the ambulance race */
static int image_byte(Sim *s, uint32_t table, uint32_t index) { return program_u8(s->prog, table, index); }

/* ---- the kitchen's menu ------------------------------------------------------------------------------------ */

uint8_t *support_kitchen(Sim *s) { return sim_room_record(s, 0, 13, 0); }

void support_recipe_search(Sim *s) { /* 0x4DEF3 */
    uint8_t *k = support_kitchen(s);
    int level = k[KR_LEVEL];
    /* every level 0-8 has a recipe, so the original's loop ends; engine choice: at most one round (a level above
     * 8 from a damaged save leaves the recipe where the round ended) */
    for (int i = 0; i < RECIPE_COUNT; i++) {
        k[KR_RECIPE] = (uint8_t)(k[KR_RECIPE] + 1 >= RECIPE_COUNT ? 0 : k[KR_RECIPE] + 1);
        if (image_byte(s, RECIPES, 8u * k[KR_RECIPE]) == level) break;
    }
    memset(k + KR_MEAL, 0, 4); /* the dish in progress and its minutes */
}

void support_next_dish(Sim *s) { support_recipe_search(s); }

int support_cheaper(Sim *s) {
    uint8_t *k = support_kitchen(s);
    if (!get16(k + KR_COOK)) put16(k + KR_LEVEL, 0);
    else if (!get16(k + KR_LEVEL)) return 0;
    else put16(k + KR_LEVEL, get16(k + KR_LEVEL) - 1);
    support_recipe_search(s);
    return 1;
}

int support_dearer(Sim *s) {
    uint8_t *k = support_kitchen(s);
    if (!get16(k + KR_COOK)) put16(k + KR_LEVEL, 0);
    else if (sim_cook_level(s, get16(k + KR_COOK)) <= get16(k + KR_LEVEL)) return 0;
    else put16(k + KR_LEVEL, get16(k + KR_LEVEL) + 1);
    support_recipe_search(s);
    return 1;
}

void support_cooking(Sim *s) {
    uint8_t *k = support_kitchen(s);
    k[KR_COOKING] = k[KR_COOKING] ? 0 : 0xFF;
    memset(k + KR_MEAL, 0, 4);
}

int support_utensils(Sim *s, int recipe, int out[4]) {
    int n = 0;
    if (recipe < 0 || recipe >= RECIPE_COUNT) return 0;
    for (int u = 4; u < 8; u++) {
        int v = image_byte(s, RECIPES, 8u * (uint32_t)recipe + (uint32_t)u);
        if (v & 0x80) break;
        out[n++] = v;
    }
    return n;
}

/* ---- the golf report ---------------------------------------------------------------------------------------- */

/* 0x3AC80: the next non-zero u16 from the previous position on. With +5 larger than the names in the list the
 * original reads past the nine entries into the rest of the record (kept); engine choice: the walk stops at the
 * record's end (118 bytes), and the names it then lacks are left out. */
int support_golfers(Sim *s, int *out, int max) {
    const uint8_t *g = sim_room_record(s, 0, 12, 0);
    int n = g[GR_PRESENT], got = 0, pos = 0;
    while (got < n && got < max) {
        while (GR_GOLFERS + 2 * pos + 1 < GOLF_RECORD && !get16(g + GR_GOLFERS + 2 * pos)) pos++;
        if (GR_GOLFERS + 2 * pos + 1 >= GOLF_RECORD) break;
        out[got++] = (uint16_t)get16(g + GR_GOLFERS + 2 * pos);
        pos++;
    }
    return got;
}

/* ---- advertising ------------------------------------------------------------------------------------------- */

int support_campaign_cost(int type, int slogan, int duration) { return sim_project_cost(type, slogan, duration); }

static void cost(Sim *s) {
    uint8_t *r = hospital(s, 0);
    put16(r + AD_COST, support_campaign_cost(r[AD_TYPE], r[AD_SLOGAN], get16(r + AD_DURATION)));
}

static void clear_campaign(uint8_t *r) { memset(r + AD_TYPE, 0, AD_BAD + 2 - AD_TYPE); } /* +0xF4 .. +0x101 */

int support_campaign_open(Sim *s) {
    uint8_t *r = hospital(s, 0);
    if (r[AD_TYPE]) return 121;
    r[AD_TYPE] = 1, r[AD_SLOGAN] = 0;
    put16(r + AD_DURATION, 20);
    put16(r + AD_COST, 30); /* written, then recomputed */
    cost(s);
    put16(r + AD_LEFT, 0), put16(r + AD_GOOD, 0), put16(r + AD_NEUTRAL, 0), put16(r + AD_BAD, 0);
    return 119;
}

void support_campaign_shorter(Sim *s) {
    uint8_t *r = hospital(s, 0);
    if (get16(r + AD_DURATION) < 10) return;
    put16(r + AD_DURATION, get16(r + AD_DURATION) - 3);
    cost(s);
}

void support_campaign_longer(Sim *s) {
    uint8_t *r = hospital(s, 0);
    if (get16(r + AD_DURATION) >= 60) return;
    put16(r + AD_DURATION, get16(r + AD_DURATION) + 3);
    cost(s);
}

void support_campaign_slogan(Sim *s) {
    uint8_t *r = hospital(s, 0);
    r[AD_SLOGAN] = (uint8_t)(r[AD_SLOGAN] + 1 >= 12 ? 0 : r[AD_SLOGAN] + 1);
    cost(s);
}

void support_campaign_type(Sim *s) {
    uint8_t *r = hospital(s, 0);
    r[AD_TYPE] = (uint8_t)(r[AD_TYPE] + 1 >= 4 ? 1 : r[AD_TYPE] + 1);
    cost(s);
}

int support_campaign_start(Sim *s) {
    uint8_t *r = hospital(s, 0);
    int c = (uint16_t)get16(r + AD_COST);
    add32(r + H_CASH, -c); /* no cash test: the cash may go negative */
    add32(r + AD_DAY, c);
    add32(r + AD_TOTAL, c);
    put16(r + AD_LEFT, get16(r + AD_DURATION));
    /* the cool-down +0x108 is neither tested nor changed */
    return r[AD_TYPE] == 1 ? SU_AD_SIGN : r[AD_TYPE] == 3 ? SU_AD_LEAFLETS : SU_AD_BANNER; /* engine choice: other = 2 */
}

void support_campaign_discard(Sim *s) { clear_campaign(hospital(s, 0)); }

static int counts(const uint8_t *r) {
    return (uint16_t)((uint16_t)get16(r + AD_GOOD) + (uint16_t)get16(r + AD_NEUTRAL) + (uint16_t)get16(r + AD_BAD));
}

int support_campaign_reached(Sim *s) {
    uint8_t *r = hospital(s, 0);
    int n = counts(r);
    put16(r + AD_SHOWN, n);
    return n;
}

int support_campaign_changed(Sim *s) {
    const uint8_t *r = hospital(s, 0);
    return counts(r) != (uint16_t)get16(r + AD_SHOWN);
}

void support_campaign_confirm(Sim *s) {
    uint8_t *r = hospital(s, 0);
    if (get16(r + AD_LEFT) < 0) clear_campaign(r);
}

/* ---- the vehicle market ------------------------------------------------------------------------------------ */

uint8_t *support_car_offer(Sim *s, int i) { return s->world->block[BLK_CARS] + i * CAR_OFFER_SIZE; }
uint8_t *support_garage(Sim *s) { return sim_room_record(s, 0, 10, 0); }

int support_car_list(Sim *s, int id) {
    if (id < 0 || id >= CAR_IDS) return 0;
    return (uint16_t)program_word(s->prog, CAR_CATALOG, 20u * (uint32_t)id + 6);
}

int support_car_sale_price(int list, int condition) { return list - list / 100 * (100 - condition) / 2; }

static int has_seller(Sim *s, int i) { return (int8_t)support_car_offer(s, i)[CO_SELLER] >= 0; }

int support_buy_open(Sim *s) {
    if (!site(s, 10)) return SU_NO_GARAGE;
    if (race(s)) return SU_RACE;
    int first = -1; /* 0x4D01F */
    for (int i = 0; i < CAR_OFFERS && first < 0; i++)
        if (has_seller(s, i)) first = i;
    pset16(s, SU_P_SHOWN, first);
    return first < 0 ? SU_NO_OFFER : 0;
}

int support_buy_next(Sim *s) {
    int i = pb16(s, SU_P_SHOWN);
    for (int step = 0; step < 60; step++) {
        i = i + 1 >= CAR_OFFERS ? 0 : i + 1;
        if (i < 0) i = 0;
        if (has_seller(s, i)) {
            pset16(s, SU_P_SHOWN, i);
            return 0;
        }
    }
    return SU_NO_OFFER;
}

int support_buy(Sim *s, CarResult *o) {
    uint8_t *g = support_garage(s);
    int i = pb16(s, SU_P_SHOWN);
    if (i < 0 || i >= CAR_OFFERS) i = 0; /* engine guard: no offer shown */
    uint8_t *c = support_car_offer(s, i);
    if (!(uint8_t)(g[GA_PLACES] - g[GA_CARS])) return SU_GARAGE_FULL;
    int place = -1;
    for (int k = 0; k < GA_PLACE_COUNT && place < 0; k++)
        if (!g[GA_PLACE + 4 * k]) place = k;
    if (place < 0) return SU_GARAGE_FULL; /* engine choice: the original loops for ever (an inconsistent record) */
    int price = (uint16_t)get16(c + CO_PRICE), seller = (int8_t)c[CO_SELLER];
    if (price >= cash_of(s, 0)) return SU_NO_CASH;
    o->car = c[CO_CAR], o->price = price, o->seller = seller;
    if (seller != 0 && seller <= DEALER) {
        uint8_t *me = hospital(s, 0), *them = hospital(s, seller);
        add32(me + H_CASH, -price), add32(me + 0x120, price), add32(me + 0x13A, price);
        add32(them + H_CASH, price), add32(them + 0x124, price), add32(them + 0x12E, price);
    }
    uint8_t *p = g + GA_PLACE + 4 * place;
    p[0] = (uint8_t)(c[CO_CAR] + 1), p[1] = c[CO_CONDITION], p[2] = 0, p[3] = 0;
    g[GA_CARS]++;
    c[CO_SELLER] = 0xFF;
    return seller == 0 ? SU_OWN_OFFER : SU_BOUGHT;
}

static int sellable(const uint8_t *p) { return p[0] && !p[2]; }

int support_sell_open(Sim *s) {
    if (!site(s, 10)) return SU_SELL_NO_GARAGE;
    uint8_t *g = support_garage(s);
    if (!g[GA_CARS]) return SU_NO_CAR;
    if (race(s)) return SU_RACE;
    for (int k = 0; k < GA_PLACE_COUNT; k++)
        if (sellable(g + GA_PLACE + 4 * k)) {
            pset16(s, SU_P_SHOWN, k);
            return 0;
        }
    return SU_NO_CAR;
}

int support_sell_next(Sim *s) {
    uint8_t *g = support_garage(s);
    if (!g[GA_CARS]) return SU_NO_CARS;
    int k = pb16(s, SU_P_SHOWN);
    /* engine choice: one round of the ten places; with no car that qualifies the original loops for ever, the
     * engine stays on the car shown */
    for (int step = 0; step < GA_PLACE_COUNT; step++) {
        k = k + 1 >= GA_PLACE_COUNT || k < 0 ? 0 : k + 1;
        const uint8_t *p = g + GA_PLACE + 4 * k;
        if (sellable(p) && (int8_t)p[1] >= 25) {
            pset16(s, SU_P_SHOWN, k);
            return 0;
        }
    }
    return 0;
}

int support_sell(Sim *s, CarResult *o) {
    int free_i = -1;
    for (int i = 0; i < CAR_OFFERS && free_i < 0; i++)
        if (!has_seller(s, i)) free_i = i;
    if (free_i < 0) return SU_MARKET_FULL;
    uint8_t *g = support_garage(s);
    int k = pb16(s, SU_P_SHOWN);
    if (k < 0 || k >= GA_PLACE_COUNT) k = 0; /* engine guard */
    uint8_t *p = g + GA_PLACE + 4 * k, *c = support_car_offer(s, free_i);
    g[GA_CARS]--;
    int id = p[0] - 1, price = support_car_sale_price(support_car_list(s, id), p[1]);
    c[CO_SELLER] = 0, c[CO_CAR] = (uint8_t)id;
    put16(c + CO_PRICE, price);
    c[CO_CONDITION] = p[1], c[CO_AGE] = 0;
    memset(p, 0, 4);
    o->car = id, o->price = price, o->seller = 0;
    return SU_OFFERED;
}

/* ---- thugs ------------------------------------------------------------------------------------------------- */

int support_thug_fits(Sim *s, int slot, int group) {
    const uint8_t *sl = staff_slots(s, 0) + slot * STAFF_SLOT_SIZE;
    int st = sl[3];
    return get16(sl) && sl[2] == group && st != 0x13 && st != 0x14 && st != 0x15 && st != 0x16 && st != 0x19;
}

/* The thug sheet's "Nr." (screen 110, docs/ui-support.md): "Nr." is 1 + the first word of the clicked hotspot record
 * (opcode 16) in the original, so 17 for every thug. */
int support_thug_sheet_nr(int slot) {
    /* FIX-2 (docs/fixes.md): original "Nr. 17" for every thug; fixed 1 + the shown thug's slot */
    if (fix_on(2)) return slot + 1;
    return 17;
}

int support_thugs_open(Sim *s) {
    pset16(s, SU_P_GROUP, 8);
    for (int i = 0; i < STAFF_SLOTS; i++)
        if (support_thug_fits(s, i, 8)) {
            pset16(s, SU_P_SLOT, i);
            return 0;
        }
    pset16(s, SU_P_SLOT, 0);
    return SU_NO_THUG;
}

int support_thug_step(Sim *s, int step) {
    int i = pb16(s, SU_P_SLOT), g = pb16(s, SU_P_GROUP);
    for (int k = 0; k < 210; k++) {
        i += step;
        if (i >= STAFF_SLOTS) i = 0;
        if (i < 0) i = STAFF_SLOTS - 1;
        if (support_thug_fits(s, i, g)) {
            pset16(s, SU_P_SLOT, i);
            return 0;
        }
    }
    return -1;
}

void support_thug_send(Sim *s) {
    int slot = pb16(s, SU_P_SLOT);
    if (slot < 0 || slot >= STAFF_SLOTS) return;
    sim_thug_send(s, 0, slot, 1); /* the player ([0x5ABD] = 0) */
}
