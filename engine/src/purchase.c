/* Purchasing and the store room for the player, from docs/ui-purchasing.md (see purchase.h). The purchase itself is
 * the AI's routine (sim_buy_offer in sim_staff.c, 0x2F610 / 0x391D2 with the player's flag), the store tidy-up is
 * sim_store_tidy (0x2A873) and the room ratings sim_recompute, so the rules exist once. */
#include "purchase.h"

#include "rooms.h"
#include "sim_internal.h"

enum { H = 0 }; /* the player's hospital record */

static int pb16(Sim *s, int off) { return rooms_pb16(s, off); }
static void set16(Sim *s, int off, int v) { rooms_set_pb16(s, off, v); }
static uint8_t *counts(Sim *s) { return room_counts(s, H); }
static int u16(const uint8_t *p) { return (uint16_t)get16(p); }

uint8_t *purchase_store(Sim *s) { return sim_room_record(s, H, 4, 0); }
uint8_t *purchase_offer(Sim *s, int eq, int i) {
    return s->world->block[eq ? BLK_EQUIPMENT_MARKET : BLK_FURNITURE_MARKET] + i * 8;
}
const uint8_t *purchase_item(Sim *s, int eq, int id) { return eq ? sim_equipment_item(s, id) : sim_furniture_item(s, id); }
int purchase_item_valid(int eq, int id) { return id > 0 && id < (eq ? EQUIPMENT_IDS : FURNITURE_IDS); }

/* ---- the offers ------------------------------------------------------------------------------------------ */

void purchase_set_filters(Sim *s, int capability, int kind) {
    uint8_t *pb = rooms_pb(s);
    pb[PU_CAPABILITY] = (uint8_t)capability;
    pb[PU_KIND] = (uint8_t)kind;
}

int purchase_matches(Sim *s, int eq, int i) {
    const uint8_t *o = purchase_offer(s, eq, i), *pb = rooms_pb(s);
    if ((int8_t)o[0] < 0 || o[5]) return 0;
    const uint8_t *it = purchase_item(s, eq, u16(o + 2));
    if (pb[PU_KIND] && pb[PU_KIND] != it[6]) return 0;
    int cap = eq ? pb[PU_CAPABILITY] : 0;
    if (cap && it[10] != cap && it[11] != cap && it[12] != cap && it[13] != cap) return 0;
    return 1;
}

static void reserve(Sim *s, int eq, int i, int on) {
    if (i >= 0 && i < MARKET_OFFERS) purchase_offer(s, eq, i)[5] = on ? 0xFF : 0;
}

int purchase_search(Sim *s, int eq) {
    /* German CD: every furniture search first frees all 300 furniture offers, before the store-room test, so offers
     * left reserved by an offer screen quit without "back" come back (docs/editions.md 7.2, OTH-DE-5) */
    if (!eq && program_german_cd(s->prog))
        for (int i = 0; i < MARKET_OFFERS; i++) reserve(s, 0, i, 0);
    if (!counts(s)[4]) return PU_NO_STORE_BUY;
    const uint8_t *st = purchase_store(s);
    if ((uint8_t)(st[STORE_CAPACITY] - st[STORE_FURNITURE_HELD] - st[STORE_EQUIPMENT_HELD]) == 0) return PU_STORE_FULL;
    for (int i = 0; i < MARKET_OFFERS; i++) {
        if (!purchase_matches(s, eq, i)) continue;
        set16(s, PU_INDEX, i);
        reserve(s, eq, i, 1);
        return 0;
    }
    set16(s, PU_INDEX, MARKET_OFFERS); /* left at 300; nothing reads it before the next search */
    return eq ? PU_NO_EQUIPMENT_OFFER : PU_NO_FURNITURE_OFFER;
}

void purchase_step(Sim *s, int eq, int step) {
    int start = pb16(s, PU_INDEX), i = start;
    reserve(s, eq, start, 0);
    do i = (i + step + MARKET_OFFERS) % MARKET_OFFERS;
    while (i != start && !purchase_matches(s, eq, i));
    set16(s, PU_INDEX, i);
    reserve(s, eq, i, 1);
}

void purchase_leave(Sim *s, int eq) { reserve(s, eq, pb16(s, PU_INDEX), 0); }

int purchase_buy(Sim *s, int eq, PurchaseResult *o) {
    int i = pb16(s, PU_INDEX);
    const uint8_t *offer = purchase_offer(s, eq, i);
    *o = (PurchaseResult){u16(offer + 2), offer[4], offer[1], offer[0]};
    switch (sim_buy_offer(s, H, i, eq, 1)) {
    case BUY_NO_CASH: return PU_NO_CASH;
    case BUY_OWN: return PU_OWN;
    default: return PU_BOUGHT; /* BUY_FREE cannot happen for the player: the cash was checked */
    }
}

/* ---- 111 ------------------------------------------------------------------------------------------------- */

int purchase_sell_open(Sim *s, int eq) {
    const uint8_t *st = purchase_store(s);
    if (!counts(s)[4]) return PU_NO_STORE_SELL;
    if (!u16(st + STORE_CLERK)) return PU_NO_CLERK_SELL;
    if (eq ? !st[STORE_EQUIPMENT_HELD] : !st[STORE_FURNITURE_HELD]) return eq ? PU_NO_EQUIPMENT_HELD : PU_NO_FURNITURE_HELD;
    rooms_pb(s)[PU_MISSING] = 0;
    set16(s, PU_CAT, 4);
    set16(s, PU_VIEW, PU_ROOM_VIEW); /* the store shown as the "room" of category 4 */
    set16(s, PU_EQUIPMENT, eq);
    set16(s, PU_ROW, 0);
    set16(s, PU_INDEX, 0);
    return 0;
}

int purchase_sell(Sim *s, PurchaseResult *o) {
    int eq = pb16(s, PU_EQUIPMENT) != 0, slot = -1;
    for (int i = 0; i < MARKET_OFFERS && slot < 0; i++)
        if ((int8_t)purchase_offer(s, eq, i)[0] < 0) slot = i;
    if (slot < 0) return eq ? PU_MARKET_FULL_EQUIPMENT : PU_MARKET_FULL_FURNITURE;
    uint8_t *st = purchase_store(s);
    int j = pb16(s, PU_INDEX) + pb16(s, PU_ROW);
    uint8_t *id = st + (eq ? STORE_EQUIPMENT : STORE_FURNITURE) + 2 * j;
    if (!u16(id)) return 0; /* nothing happens, the screen stays */
    int item = u16(id), condition = st[(eq ? STORE_EQUIPMENT_COND : STORE_FURNITURE_COND) + j];
    put16(id, 0); /* the condition byte stays */
    st[eq ? STORE_EQUIPMENT_HELD : STORE_FURNITURE_HELD]--;
    uint8_t *offer = purchase_offer(s, eq, slot);
    int price = sim_offer_price(u16(purchase_item(s, eq, item) + 8)); /* FIX-16 (docs/fixes.md) inside */
    put16(offer + 2, item);
    offer[4] = (uint8_t)condition;
    offer[1] = (uint8_t)price;
    offer[5] = 0;
    put16(offer + 6, 0);
    offer[0] = 0; /* the player */
    *o = (PurchaseResult){item, condition, price, 0};
    sim_store_tidy(s);
    return PU_OFFERED;
}

/* ---- the lists ------------------------------------------------------------------------------------------- */

int purchase_room_instance(Sim *s) {
    int i = rooms_shown(s), cat = pb16(s, PU_CAT);
    /* engine guard: an instance beyond the category's records (P+0x10 left by another screen) reads instance 0 */
    return cat >= 2 && cat <= 18 && i < sim_rooms_in_use(s, H, cat) ? i : 0;
}

typedef struct {
    uint8_t *base;
    int ids, conds, slots;
} List;

/* the list on screen; slots = the number of slots the list has */
static List list_of(Sim *s, int view, int eq) {
    if (view == PU_STORE_VIEW)
        return (List){purchase_store(s), eq ? STORE_EQUIPMENT : STORE_FURNITURE,
                      eq ? STORE_EQUIPMENT_COND : STORE_FURNITURE_COND, STORE_SLOTS};
    int cat = pb16(s, PU_CAT);
    if (cat < 2 || cat > 18) cat = 4; /* engine guard */
    Description d = sim_description(s, cat);
    return (List){sim_room_record(s, H, cat, purchase_room_instance(s)), eq ? d.equipment : d.furniture,
                  eq ? d.cond_b : d.cond_a, eq ? d.equipment_slots : d.furniture_slots};
}

/* reads that would leave the world's block are 0 (engine guard; the store view pages up to slot 127) */
static int in_block(Sim *s, const uint8_t *p, int n) {
    for (int b = 0; b < WORLD_BLOCKS; b++) {
        const uint8_t *base = s->world->block[b];
        if (base && p >= base && p + n <= base + s->world->size[b]) return 1;
    }
    return 0;
}

int purchase_list_item(Sim *s, int j, int *condition) {
    List l = list_of(s, pb16(s, PU_VIEW), pb16(s, PU_EQUIPMENT) != 0);
    uint8_t *id = l.base + l.ids + 2 * j, *c = l.base + l.conds + j;
    if (condition) *condition = in_block(s, c, 1) ? *c : 0;
    return in_block(s, id, 2) ? get16(id) : 0;
}

void purchase_page_up(Sim *s) {
    int i = pb16(s, PU_INDEX);
    set16(s, PU_INDEX, i < 8 ? 0 : i - 8);
}

int purchase_page_down(Sim *s) {
    int view = pb16(s, PU_VIEW), i = pb16(s, PU_INDEX);
    int limit = view == PU_STORE_VIEW ? purchase_store(s)[STORE_CAPACITY]
                                      : list_of(s, view, pb16(s, PU_EQUIPMENT) != 0).slots - 8;
    /* FIX-14 (docs/fixes.md): original pages the store view up to its capacity (an empty last page); fixed to the
     * capacity - 8, as the room lists */
    if (fix_on(14) && view == PU_STORE_VIEW) limit -= 8;
    if (limit - i <= 0) return 0;
    set16(s, PU_INDEX, i + (limit - i < 8 ? limit - i : 8));
    return 1;
}

void purchase_select(Sim *s, int row) { set16(s, PU_ROW, row); }

/* ---- 65 -------------------------------------------------------------------------------------------------- */

int purchase_inventory_button(Sim *s, int cat, uint32_t back) {
    uint8_t *pb = rooms_pb(s);
    pb[PU_MISSING] = 0;
    set16(s, PU_VIEW, PU_ROOM_VIEW);
    if (cat == 4) { /* the warehouse: store view */
        set16(s, PU_VIEW, PU_STORE_VIEW);
        pb[PU_MISSING] = 0xFF;
    } else if (cat == 5 || cat == 11 || cat == 15) { /* 0x1B808 / 0x1B99F / 0x1BAB8 */
        const uint8_t *r = sim_room_record(s, H, cat, rooms_shown(s));
        if (u16(r + 12) && !r[0x44]) return cat == 5 ? PU_DOCTOR_BUSY : cat == 11 ? PU_DENTIST_BUSY : PU_NEURO_BUSY;
        if (u16(r + 12)) set16(s, PU_VIEW, PU_STORE_VIEW), pb[PU_MISSING] = 0xFF; /* fetch the missing item */
    } else if (cat == 7 || cat == 8) { /* an operation running */
        const uint8_t *r = sim_room_record(s, H, cat, rooms_shown(s));
        if (u16(r + 12) && u16(r + 14) != 999) return cat == 7 ? PU_OPERATION : PU_NEURO_BUSY;
    }
    set16(s, PU_CAT, cat);
    for (int i = 0; i < 4; i++) pb[PU_BACK + i] = (uint8_t)(back >> (8 * i));
    return 0;
}

void purchase_inventory_open(Sim *s) {
    set16(s, PU_EQUIPMENT, 1);
    set16(s, PU_ROW, 0);
    set16(s, PU_INDEX, 0);
}

void purchase_toggle_kind(Sim *s) {
    set16(s, PU_EQUIPMENT, !pb16(s, PU_EQUIPMENT));
    set16(s, PU_ROW, 0);
    set16(s, PU_INDEX, 0);
}

int purchase_toggle_view(Sim *s) {
    if (pb16(s, PU_VIEW) == PU_STORE_VIEW) set16(s, PU_VIEW, PU_ROOM_VIEW);
    else if (counts(s)[4]) set16(s, PU_VIEW, PU_STORE_VIEW);
    else return 0;
    set16(s, PU_ROW, 0);
    set16(s, PU_INDEX, 0);
    return 1;
}

void purchase_recount(Sim *s) {
    for (int h = 0; h < 16; h++) {
        if (!room_counts(s, h)[4]) continue;
        uint8_t *st = sim_room_record(s, h, 4, 0);
        int f = 0, e = 0;
        if (fix_on(15)) {
            /* FIX-15 (docs/fixes.md): original counts non-zero bytes of the first 120 bytes (slots 0-59); fixed counts
             * the slots with a non-zero id among all 120 */
            for (int k = 0; k < STORE_SLOTS; k++) {
                f += get16(st + STORE_FURNITURE + 2 * k) != 0;
                e += get16(st + STORE_EQUIPMENT + 2 * k) != 0;
            }
        } else {
            for (int k = 0; k < STORE_SLOTS; k++) { /* bytes, not ids: slots 0-59 only (the original's fault) */
                f += st[STORE_FURNITURE + k] != 0;
                e += st[STORE_EQUIPMENT + k] != 0;
            }
        }
        st[STORE_FURNITURE_HELD] = (uint8_t)f;
        st[STORE_EQUIPMENT_HELD] = (uint8_t)e;
    }
}

/* Step 6 of the move: 0x2A873, 0x2BFB7 (the first gap of every room list of every hospital, the store's again through
 * 0x2A873), 0x2B210 (the recompute of every category), then the recount 0x3C35C (docs/mainloop.md, tick 64 row). */
static void tidy(Sim *s) {
    sim_store_tidy(s);
    sim_tidy_lists(s);
    sim_recompute_all(s);
    purchase_recount(s);
}

int purchase_move(Sim *s) {
    int cat = pb16(s, PU_CAT), eq = pb16(s, PU_EQUIPMENT) != 0;
    if (cat == 4) return PU_BACK_ONLY; /* the warehouse's inventory is view-only */
    if (!counts(s)[4]) return PU_NO_STORE_MOVE;
    uint8_t *st = purchase_store(s), *held = st + (eq ? STORE_EQUIPMENT_HELD : STORE_FURNITURE_HELD);
    int clerk = u16(st + STORE_CLERK);
    if (!clerk) return PU_NO_CLERK_MOVE;
    List store = list_of(s, PU_STORE_VIEW, eq), room = list_of(s, PU_ROOM_VIEW, eq);
    int j = pb16(s, PU_INDEX) + pb16(s, PU_ROW);
    uint8_t *h0 = hospital(s, H);
    if (pb16(s, PU_VIEW) == PU_STORE_VIEW) { /* store -> room */
        if (!*held) return eq ? PU_STORE_EMPTY_EQUIPMENT : PU_STORE_EMPTY_FURNITURE;
        int k = 0;
        while (k < room.slots && get16(room.base + room.ids + 2 * k)) k++;
        if (k == room.slots) return eq ? PU_ROOM_FULL_EQUIPMENT : PU_ROOM_FULL_FURNITURE;
        (*held)--; /* before the row is checked: the recount repairs it */
        uint8_t *id = store.base + store.ids + 2 * j;
        if (!get16(id)) {
            purchase_recount(s);
            return PU_RECOUNTED;
        }
        put16(room.base + room.ids + 2 * k, get16(id));
        room.base[room.conds + k] = store.base[store.conds + j];
        put16(id, 0);
        tidy(s);
        add16(h0 + 0x11A, 1);
        if (get16(h0 + 0x11A) >= 0) add16(rooms_staff(s, clerk) + 0x10, 1);
        return PU_MOVED;
    }
    /* room -> store */
    int free_slots = (int8_t)(st[STORE_CAPACITY] - st[STORE_FURNITURE_HELD] - st[STORE_EQUIPMENT_HELD]);
    if (free_slots <= 0) return PU_STORE_PACKED; /* negative: the original loops for ever (0x2691B); capped here */
    uint8_t *id = room.base + room.ids + 2 * j;
    if (j >= room.slots || !get16(id)) { /* engine guard: j inside the room's list */
        purchase_recount(s);
        return PU_RECOUNTED;
    }
    int k = 0;
    while (k < STORE_SLOTS && get16(store.base + store.ids + 2 * k)) k++;
    if (k == STORE_SLOTS) return PU_STORE_PACKED; /* no free slot: the original loops for ever (0x2691D); capped */
    (*held)++; /* furniture also sets [0x5CB2] = FF, which nothing reads: left out */
    put16(store.base + store.ids + 2 * k, get16(id));
    store.base[store.conds + k] = room.base[room.conds + j];
    put16(id, 0); /* the condition byte stays */
    tidy(s);
    add16(h0 + 0x11A, -2);
    return PU_MOVED;
}

int purchase_info(Sim *s, int *condition) {
    int j = pb16(s, PU_INDEX) + pb16(s, PU_ROW);
    int id = purchase_list_item(s, j, condition);
    if (id <= 0) return 0;
    /* the original's fault (0x25F1B): equipment in the store view shows the byte S + id as its condition */
    /* FIX-13 (docs/fixes.md): original shows the byte S + id; fixed keeps the store slot's own condition byte */
    if (pb16(s, PU_EQUIPMENT) && pb16(s, PU_VIEW) == PU_STORE_VIEW && condition && !fix_on(13))
        *condition = purchase_store(s)[id];
    return id;
}
