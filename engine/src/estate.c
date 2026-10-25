/* The estate agent's transactions (docs/ui-estate.md, "Summary for an implementer" and the confirm sections).
 * Quirks of the original are kept on purpose and marked "quirk". */
#include "estate.h"

#include "sim_internal.h"

#define CATALOG_PREREQ 0x27 /* category that must exist first, FF = none */
#define CATALOG_MAX 0x28 /* maximum rooms of the category, 0 = one */
#define H_ROOM_RENT_DAY 0x96
#define H_ROOM_RENT_TOTAL 0x9A
#define H_INVEST_DAY 0x14E
#define H_INVEST_TOTAL 0x152
#define NO_CATEGORY 0xFF

static uint8_t *h0(Sim *s) { return hospital(s, 0); }
uint8_t *estate_slots(Sim *s) { return room_slots(s, 0); }
uint8_t *estate_counts(Sim *s) { return room_counts(s, 0); }

int estate_owned(Sim *s) { return get16(h0(s) + H_PLOTS_OWNED); }
int estate_rented(Sim *s) { return get16(h0(s) + H_PLOTS_RENTED); }
int estate_free(Sim *s) { return estate_owned(s) + estate_rented(s) - get16(h0(s) + H_PLOTS_BUILT); }
static int32_t cash(Sim *s) { return cash_of(s, 0); }

/* ---- plots ------------------------------------------------------------------------------------- */

int estate_plot_cost(int buy, int n) { return buy ? 300 * n + 300 * n / 4 : 2 * PLOT_RENT * n; }

int estate_rent_plots(Sim *s, int n) {
    /* FIX-24 (docs/fixes.md): original checks 22 n (half the 44 n charged); fixed checks the 44 n */
    int checked = fix_on(24) ? 2 * PLOT_RENT * n : PLOT_RENT * n;
    if (n == 0 || checked > cash(s)) return ESTATE_NO_CASH_RENT; /* quirk: half the price is checked */
    add32(h0(s) + H_CASH, -PLOT_RENT * n);
    add32(h0(s) + H_CASH, -PLOT_RENT * n);
    add16(h0(s) + H_PLOTS_RENTED, n); /* nothing booked in +0x9E/+0xA2 */
    return 0;
}

int estate_buy_plots(Sim *s, int n, int *converted) {
    int price = estate_plot_cost(1, n);
    if (n == 0 || price > cash(s)) return ESTATE_NO_CASH_BUY;
    uint8_t *h = h0(s);
    add32(h + H_CASH, -price);
    add32(h + H_INVEST_DAY, price);
    add32(h + H_INVEST_TOTAL, price);
    int c = n < estate_rented(s) ? n : estate_rented(s); /* rented plots are converted first */
    if (c < 0) c = 0;
    add16(h + H_PLOTS_RENTED, -c);
    add16(h + H_PLOTS_OWNED, n);
    if (converted) *converted = c;
    return 0;
}

void estate_cancel_plots(Sim *s, int n) {
    add16(h0(s) + H_PLOTS_RENTED, -n);
    add32(h0(s) + H_CASH, -4 * n);
}

void estate_sell_plots(Sim *s, int n) {
    add16(h0(s) + H_PLOTS_OWNED, -n);
    add32(h0(s) + H_CASH, 290 * n - 4 * n); /* investments are not reduced */
}

int estate_cancel_plots_allowed(Sim *s) {
    if (!(estate_owned(s) || estate_rented(s)) || !estate_free(s) || !estate_rented(s)) return ESTATE_NOTHING_TO_CANCEL;
    return 0;
}

int estate_sell_plots_allowed(Sim *s) {
    if (!(estate_owned(s) || estate_rented(s)) || !estate_free(s) || !estate_owned(s)) return ESTATE_NOTHING_TO_SELL;
    return 0;
}

int estate_more(Sim *s, int screen, int n) {
    int16_t v = (int16_t)n;
    if (screen == 11 || screen == 13) {
        v = (int16_t)(v + 1);
        if (estate_plot_cost(screen != 11, v) > cash(s)) v = (int16_t)(v - 1);
        return v;
    }
    int limit = screen == 14 ? estate_rented(s) : estate_owned(s);
    if (estate_free(s) > v && limit > v) v = (int16_t)(v + 1);
    return v;
}

int estate_fewer(int n) { return (int16_t)n >= 2 ? n - 1 : n; }

/* ---- catalog ----------------------------------------------------------------------------------- */

int estate_unit_rent(Sim *s, int type) { return catalog_rent(s, type); }
int estate_unit_base(Sim *s, int type) { return catalog_u16(s, type, CATALOG_PRICE); }
int estate_unit_price(Sim *s, int type) { return estate_unit_base(s, type) + estate_unit_base(s, type) / 4; }
int estate_unit_plots(Sim *s, int type) { return catalog_u8(s, type, CATALOG_PLOTS); }
static int category(Sim *s, int type) { return catalog_u8(s, type, CATALOG_CATEGORY); }

const uint8_t *estate_room_name(Sim *s, int type, size_t *len) {
    const uint8_t *p = program_table(s->prog, CATALOG) + catalog_at(type, 0) - CATALOG;
    size_t n = 0;
    while (n < 30 && p[n] != '$') n++;
    *len = n;
    return p;
}

/* ---- units ------------------------------------------------------------------------------------- */

static int has_room(const uint8_t *slot) { return (int8_t)slot[0] >= 0; }

/* Some slot holds a room of category cat (the type search's test). */
static int slot_of_category(Sim *s, int cat) {
    const uint8_t *slot = estate_slots(s);
    for (int i = 0; i < ROOM_SLOTS; i++, slot += 2)
        if (has_room(slot) && category(s, slot[0]) == cat) return 1;
    return 0;
}

int estate_build_blocked(Sim *s, int cat, int type) {
    const uint8_t *counts = estate_counts(s);
    int pre = catalog_u8(s, type, CATALOG_PREREQ), max = catalog_u8(s, type, CATALOG_MAX);
    if (pre != NO_CATEGORY && !counts[pre]) return 1;
    return counts[cat] && (max == 0 || counts[cat] >= max);
}

int estate_rented_slot_of_category(Sim *s, int cat) {
    const uint8_t *slot = estate_slots(s);
    for (int i = 0; i < ROOM_SLOTS; i++, slot += 2)
        if (has_room(slot) && slot[1] && category(s, slot[0]) == cat) return i;
    return -1;
}

int estate_type_search(Sim *s, int *type, int step) {
    int t = *type;
    for (int tries = 0; tries < 300; tries++) {
        int pre = catalog_u8(s, t, CATALOG_PREREQ), max = catalog_u8(s, t, CATALOG_MAX), cat = category(s, t);
        int pre_ok = pre == NO_CATEGORY || slot_of_category(s, pre);
        int room_ok = max == 0 ? !slot_of_category(s, cat) : estate_counts(s)[cat] != max; /* quirk: "differs" */
        if (pre_ok && room_ok) {
            *type = t;
            return 1;
        }
        t += step;
        if (t < 0) t = ESTATE_TYPES - 1; /* wrapped to the end, not by the step */
        if (t > ESTATE_TYPES - 1) t = 0;
    }
    *type = t;
    return 0;
}

/* Fields of the last room record that must be 0 before a unit of the category may go. */
static const uint8_t IDLE_FIELDS[19][12] = {
    [2] = {8, 0x0A, 0x0E, 0x12, 0x16, 0x46, 0x4A, 0x4E, 0x52, 0x56, 0x5A},
    [3] = {8, 0x0C}, [4] = {4}, [5] = {8, 0x0A, 0x0C}, [6] = {8, 0x0C}, [7] = {6, 8, 0x0A, 0x0C},
    [8] = {8, 0x0A, 0x0C}, [9] = {8, 0x0C}, [10] = {6, 8}, [11] = {8, 0x0A, 0x0C}, [12] = {2},
    [13] = {6}, [14] = {2, 4}, [15] = {8, 0x0A, 0x0C}, [16] = {8, 0x0C}, [17] = {8, 0x0A},
    [18] = {6, 0x0A, 0x0C},
};

static int fields_zero(const uint8_t *rec, const uint8_t *fields) {
    for (int i = 0; i < 12 && fields[i]; i++)
        if (get16(rec + fields[i])) return 0;
    return 1;
}

static int none_of(const uint8_t *counts, const int *cats, int n) {
    for (int i = 0; i < n; i++)
        if (counts[cats[i]]) return 0;
    return 1;
}

static int room_idle(Sim *s, int cat, const uint8_t *rec) {
    const uint8_t *counts = estate_counts(s);
    if (!fields_zero(rec, IDLE_FIELDS[cat])) return 0;
    const uint8_t *reception = sim_room_record(s, 0, 2, 0); /* read at the start of the table: hospital 0 */
    static const int AFTER_RECEPTION[] = {5, 9, 11, 15}, AFTER_WAITING[] = {5, 11, 15};
    switch (cat) {
    case 2: return none_of(counts, AFTER_RECEPTION, 4);
    case 3: return !get16(reception + 8) && (counts[3] >= 2 || none_of(counts, AFTER_WAITING, 3));
    case 4: return !sim_room_record(s, 0, 13, 0)[5]; /* the kitchen record's byte +5 */
    case 6: return !get16(reception + 8);
    case 10: return !get16(s->world->block[BLK_34]); /* no ambulance race running ([0x552A] = 0) */
    case 16:
        for (int i = 0; i < sim_rooms_in_use(s, 0, 15); i++)
            if (!fields_zero(sim_room_record(s, 0, 15, i), IDLE_FIELDS[15])) return 0;
        return 1;
    default: return 1;
    }
}

int estate_removal_slot(Sim *s, int cat, int rented, int *type) {
    const uint8_t *counts = estate_counts(s);
    if (cat < 2 || cat > ESTATE_CELLS || !counts[cat]) return -1; /* the Grundeinheit never goes */
    int last = sim_rooms_in_use(s, 0, cat) - 1;
    if (last < 0) return -1;
    const uint8_t *rec = sim_room_record(s, 0, cat, last);
    int r = rec[sim_description(s, cat).type];
    if (type) *type = r;
    const uint8_t *slot = estate_slots(s);
    int i = 0;
    while (i < ROOM_SLOTS && !(slot[2 * i] == r && category(s, r) == cat)) i++;
    if (i == ROOM_SLOTS) return -1;
    if (rented ? !slot[2 * i + 1] : slot[2 * i + 1]) return -1; /* quirk: only the first slot of the type counts */
    if (!room_idle(s, cat, rec)) return -1;
    return i;
}

int estate_removal_pick(Sim *s, int cat, int rented, int *type) {
    int r;
    if (estate_removal_slot(s, cat, rented, &r) < 0) return -1;
    const uint8_t *slot = estate_slots(s);
    int i = 0; /* the click's own search: the first slot of type R that is rented (owned) */
    while (i < ROOM_SLOTS && !(slot[2 * i] == r && (rented ? slot[2 * i + 1] != 0 : slot[2 * i + 1] == 0))) i++;
    if (i == ROOM_SLOTS) return -1; /* texts 577/578 in the original; unreachable after the check */
    estate_counts(s)[cat]--;
    if (type) *type = r;
    return i;
}

void estate_removal_back(Sim *s, int category_index) { estate_counts(s)[category_index + 1]++; }

static void book_unit(Sim *s, int slot, int type, int rented) {
    uint8_t *sl = estate_slots(s) + 2 * slot;
    sl[0] = (uint8_t)type;
    sl[1] = (uint8_t)rented;
    add16(h0(s) + H_PLOTS_BUILT, estate_unit_plots(s, type));
    sim_create_room(s, 0, category(s, type), type);
}

int estate_rent_unit(Sim *s, int type) {
    const uint8_t *slot = estate_slots(s);
    int i = 0;
    while (i < ROOM_SLOTS && !(slot[2 * i] == 0xFF && slot[2 * i + 1] == 0xFF)) i++; /* only FF FF slots */
    if (i == ROOM_SLOTS) return ESTATE_NO_SLOT;
    sim_rating_catalog(s, type);
    if (estate_free(s) < estate_unit_plots(s, type)) return ESTATE_TOO_FEW_PLOTS;
    int rent = estate_unit_rent(s, type);
    if (rent > cash(s)) return ESTATE_TOO_EXPENSIVE;
    uint8_t *h = h0(s);
    add32(h + H_CASH, -rent);
    add32(h + H_ROOM_RENT_DAY, rent); /* the first day's rent */
    add32(h + H_ROOM_RENT_TOTAL, rent);
    book_unit(s, i, type, 1);
    return 0;
}

static void pay_price(Sim *s, int price) {
    uint8_t *h = h0(s);
    add32(h + H_CASH, -price);
    add32(h + H_INVEST_DAY, price);
    add32(h + H_INVEST_TOTAL, price);
}

int estate_buy_unit(Sim *s, int type, int *bought_out, int *price_out) {
    uint8_t *slot = estate_slots(s);
    int price = estate_unit_price(s, type);
    if (bought_out) *bought_out = 0;
    if (price_out) *price_out = price;
    for (int i = 0; i < ROOM_SLOTS; i++)
        if (slot[2 * i] == type && slot[2 * i + 1] == 1) { /* buy-out of a rented unit */
            if (price > cash(s)) return ESTATE_TOO_EXPENSIVE;
            pay_price(s, price);
            slot[2 * i + 1] = 0; /* the day's rent already paid is not returned */
            if (bought_out) *bought_out = 1;
            return 0;
        }
    int i = 0;
    while (i < ROOM_SLOTS && slot[2 * i] != 0xFF) i++; /* quirk: the rented byte is not tested */
    if (i == ROOM_SLOTS) return ESTATE_NO_SLOT;
    sim_rating_catalog(s, type);
    if (estate_free(s) < estate_unit_plots(s, type)) return ESTATE_TOO_FEW_PLOTS;
    if (price > cash(s)) return ESTATE_TOO_EXPENSIVE;
    pay_price(s, price);
    book_unit(s, i, type, 0);
    return 0;
}

/* The room count was lowered by the grid click; the slot keeps its rented byte (FF 01 / FF 00). */
static int free_slot(Sim *s, int slot) {
    uint8_t *sl = estate_slots(s) + 2 * slot;
    int type = sl[0];
    add16(h0(s) + H_PLOTS_BUILT, -estate_unit_plots(s, type));
    sl[0] = 0xFF;
    return type;
}

int estate_cancel_unit(Sim *s, int slot) {
    int type = free_slot(s, slot);
    int fee = 2 * estate_unit_rent(s, type) + 8;
    add32(h0(s) + H_CASH, -fee); /* no cash check */
    return fee;
}

int estate_sell_unit(Sim *s, int slot) {
    int type = free_slot(s, slot);
    int base = estate_unit_base(s, type), proceeds = base - (base / 16 + 8);
    add32(h0(s) + H_CASH, proceeds); /* investments unchanged */
    return proceeds;
}

void estate_unit_counts(Sim *s, int *owned, int *rented) {
    const uint8_t *slot = estate_slots(s);
    *owned = *rented = 0;
    for (int i = 0; i < ROOM_SLOTS; i++, slot += 2) {
        if (!has_room(slot)) continue;
        if (slot[1]) ++*rented;
        else ++*owned;
    }
}
