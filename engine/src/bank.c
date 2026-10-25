/* The rules of the player's blood and organ bank, from docs/ui-bank.md. The original's faults that change the game's
 * numbers are reproduced (the spec's "Faults of the original seen here"): an organ purchase books the organs held
 * before it instead of the price, the blood bought beyond the capacity is added to the bank record's dword +0x66
 * (organ slots 7 and 8) instead of the hospital's "blood lost", and the "less blood" floor of 200 ignores the stock.
 * The two that hang or crash the original are not: see bank_value_per_5000 and bank_sell_organ_open. */
#include "bank.h"

#include "rooms.h"
#include "sim_internal.h"

enum { H_BLOOD_SOLD = 0x5E, H_BLOOD_BOUGHT = 0x62, H_SPENT = 0x6E, H_SPENT2 = 0x72, H_EARNED = 0x76, H_EARNED2 = 0x7A,
       H_ORGANS_BOUGHT = 0x7E, H_ORGANS_SOLD = 0x80 };

static int u16(const uint8_t *p) { return (uint16_t)get16(p); }
static uint8_t *pb(Sim *s) { return rooms_pb(s); }

uint8_t *bank_record(Sim *s) { return sim_bank(s, 0); }
int bank_slot_shown(Sim *s) {
    int i = u16(pb(s) + BANK_P_OFFER);
    return i < 100 ? i : 0; /* engine choice: a stale P+0 outside the 100 slots reads slot 0 */
}
uint8_t *bank_offer(Sim *s) { return sim_market(s, bank_slot_shown(s)); }

static int kind_matches(const uint8_t *m, int organ) { return organ ? m[BM_KIND] != 0 : m[BM_KIND] == 0; }
static int buyable(const uint8_t *m, int organ) { /* seller not negative, not reserved, of the kind */
    return (int8_t)m[BM_SELLER] >= 0 && !m[BM_RESERVED] && kind_matches(m, organ);
}
static int none_text(int organ) { return organ ? BANK_NO_ORGAN_OFFER : BANK_NO_BLOOD_OFFER; }

/* ---- buying ----------------------------------------------------------------------------------------------------- */

int bank_buy_open(Sim *s, int organ) {
    for (int i = 0; i < 100; i++) { /* the player's own offers included */
        uint8_t *m = sim_market(s, i);
        if (!buyable(m, organ)) continue;
        put16(pb(s) + BANK_P_OFFER, i);
        m[BM_RESERVED] = 0xFF;
        return 0;
    }
    return none_text(organ);
}

int bank_buy_step(Sim *s, int organ, int step) {
    int i = bank_slot_shown(s);
    sim_market(s, i)[BM_RESERVED] = 0;
    for (int k = 0; k < 200; k++) {
        i = (i + step + 100) % 100;
        uint8_t *m = sim_market(s, i);
        if (!buyable(m, organ)) continue;
        put16(pb(s) + BANK_P_OFFER, i);
        m[BM_RESERVED] = 0xFF;
        return 0;
    }
    return none_text(organ); /* only when the shown offer vanished */
}

void bank_back(Sim *s) {
    uint8_t *m = bank_offer(s);
    m[BM_RESERVED] = 0;
    if (get16(m + BM_AGE) < 0) { /* a draft is freed */
        put16(m + BM_AGE, 0);
        m[BM_SELLER] = 0xFF;
    }
}

static void result(const uint8_t *m, BankResult *o) {
    o->seller = (int8_t)m[BM_SELLER];
    o->quantity = u16(m + BM_QUANTITY);
    o->price = u16(m + BM_PRICE);
    o->id = m[BM_ID];
    o->quality = m[BM_QUALITY];
}

/* the money of a purchase: the player pays v, seller k gets it */
static void book(Sim *s, int k, int v) {
    uint8_t *h = hospital(s, 0), *seller = hospital(s, k);
    add32(h + H_CASH, -v);
    add32(h + H_SPENT, v);
    add32(h + H_SPENT2, v);
    add32(seller + H_CASH, v);
    add32(seller + H_EARNED, v);
    add32(seller + H_EARNED2, v);
}

int bank_buy_blood(Sim *s, BankResult *o) {
    uint8_t *m = bank_offer(s), *B = bank_record(s);
    result(m, o);
    int k = o->seller, q = o->quantity;
    if (o->price > cash_of(s, 0)) {
        m[BM_RESERVED] = 0;
        return BANK_BLOOD_NO_CASH;
    }
    if (k != 0) {
        book(s, k, o->price);
        add32(hospital(s, 0) + H_BLOOD_BOUGHT, q);
        add32(hospital(s, k) + H_BLOOD_SOLD, q);
    }
    if (B) { /* no capacity test before the purchase */
        int32_t stock = get32(B + BR_STOCK) + q, cap = get32(B + BR_CAPACITY);
        if (cap < stock) {
            /* FIX-18 (docs/fixes.md): original adds the excess to the bank record's +0x66 (organ slots 7 and 8);
             * fixed to the hospital's "blood lost" +0x66 */
            if (fix_on(18)) add32(hospital(s, 0) + 0x66, stock - cap);
            else add32(B + BR_LOST, stock - cap); /* fault, reproduced: meant is the hospital's +0x66 */
            stock = cap;
        }
        put32(B + BR_STOCK, stock);
    }
    m[BM_SELLER] = 0xFF; /* the reserved flag stays FF on the freed slot */
    return k ? BANK_BLOOD_BOUGHT : BANK_BLOOD_OWN;
}

int bank_buy_organ(Sim *s, BankResult *o) {
    uint8_t *m = bank_offer(s), *B = bank_record(s);
    result(m, o);
    int k = o->seller;
    if (o->price > cash_of(s, 0)) {
        m[BM_RESERVED] = 0;
        return BANK_ORGAN_NO_CASH;
    }
    int held = B ? get16(B + BR_HELD) : 0;
    if (!B || held == get16(B + BR_PLACES)) {
        m[BM_RESERVED] = 0;
        return BANK_ORGAN_FULL;
    }
    if (k != 0) {
        /* FIX-19 (docs/fixes.md): original books the organs held before the purchase; fixed books the price */
        book(s, k, fix_on(19) ? o->price : held);
        add16(hospital(s, 0) + H_ORGANS_BOUGHT, 1);
        add16(hospital(s, k) + H_ORGANS_SOLD, 1);
    }
    if (held >= 0 && held < BR_SLOT_COUNT) { /* engine choice: a slot number outside the 30 slots writes nothing */
        B[BR_SLOTS + 2 * held] = (uint8_t)o->id;
        B[BR_SLOTS + 2 * held + 1] = (uint8_t)o->quality;
    }
    put16(B + BR_HELD, held + 1);
    m[BM_SELLER] = 0xFF;
    return BANK_ORGAN_BOUGHT;
}

/* ---- selling ---------------------------------------------------------------------------------------------------- */

static int free_slot(Sim *s) {
    for (int i = 0; i < 100; i++)
        if ((int8_t)sim_market(s, i)[BM_SELLER] < 0) return i;
    return -1;
}

static uint8_t *draft(Sim *s, int i, int kind) {
    uint8_t *m = sim_market(s, i);
    put16(pb(s) + BANK_P_OFFER, i);
    put16(m + BM_AGE, -1);
    m[BM_RESERVED] = 0xFF;
    m[BM_SELLER] = 0; /* [0xB054] = 0, the player */
    m[BM_KIND] = (uint8_t)kind;
    return m;
}

int bank_sell_blood_open(Sim *s) {
    uint8_t *B = bank_record(s);
    int32_t stock = B ? get32(B + BR_STOCK) : 0;
    if (stock < 100) return BANK_LOW_STOCK;
    int i = free_slot(s);
    if (i < 0) return BANK_MARKET_FULL;
    uint8_t *m = draft(s, i, 0);
    int q = stock < 500 ? (int)stock : 500; /* the stock is not touched yet */
    put16(m + BM_QUANTITY, q);
    put16(m + BM_PRICE, q / 32 + 6 + (int)sim_bits(s, 7));
    return 0;
}

static void organ_from(Sim *s, uint8_t *m, const uint8_t *slot) {
    m[BM_ID] = slot[0];
    m[BM_QUALITY] = slot[1];
    put16(m + BM_PRICE, sim_organ_price(s, slot[0]));
}

int bank_sell_organ_open(Sim *s) {
    uint8_t *B = bank_record(s);
    if (!B || get16(B + BR_HELD) == 0) return BANK_NO_ORGANS;
    int i = free_slot(s);
    if (i < 0) return BANK_MARKET_FULL;
    uint8_t *m = draft(s, i, 1);
    /* The first slot with an organ id. Engine choice: the original searches without end when the held count is not
     * 0 but all 30 slots are empty (it hangs); the search stops after the 30 slots, the draft is freed again and the
     * empty-bank refusal is shown. */
    for (int j = 0; j < BR_SLOT_COUNT; j++) {
        if (!B[BR_SLOTS + 2 * j]) continue;
        put16(pb(s) + BANK_P_SLOT, j);
        organ_from(s, m, B + BR_SLOTS + 2 * j);
        return 0;
    }
    bank_back(s);
    return BANK_NO_ORGANS;
}

void bank_blood_more(Sim *s) {
    uint8_t *m = bank_offer(s), *B = bank_record(s);
    int32_t q = u16(m + BM_QUANTITY) + 100, stock = B ? get32(B + BR_STOCK) : 0;
    if (q >= stock) q = stock;
    if (q >= 10000) q = 10000;
    put16(m + BM_QUANTITY, (int)q);
}

void bank_blood_less(Sim *s) {
    uint8_t *m = bank_offer(s);
    int q = (int16_t)(u16(m + BM_QUANTITY) - 100);
    if (fix_on(21)) {
        /* FIX-21 (docs/fixes.md): original floors at 200 even above the stock; fixed floors at 200 or the stock when
         * that is smaller, as "more" caps at the stock */
        const uint8_t *B = bank_record(s);
        int32_t stock = B ? get32(B + BR_STOCK) : 0;
        if (q < 200) q = stock < 200 ? (stock > 0 ? (int)stock : 0) : 200;
    } else if (q < 200) {
        q = 200; /* fault, reproduced: even above the stock */
    }
    put16(m + BM_QUANTITY, q);
}

void bank_price_step(Sim *s, int step) {
    uint8_t *m = bank_offer(s);
    int p = u16(m + BM_PRICE) + step;
    if (p > 600) p = 600;
    if (p < 1) p = 1;
    put16(m + BM_PRICE, p);
}

int bank_organ_step(Sim *s, int step) {
    uint8_t *B = bank_record(s);
    if (!B) return -1;
    int j = u16(pb(s) + BANK_P_SLOT) % BR_SLOT_COUNT;
    for (int k = 0; k < 51; k++) { /* the held count is not used */
        j = (j + step + BR_SLOT_COUNT) % BR_SLOT_COUNT;
        if (!B[BR_SLOTS + 2 * j]) continue;
        put16(pb(s) + BANK_P_SLOT, j);
        organ_from(s, bank_offer(s), B + BR_SLOTS + 2 * j); /* a new random price */
        return 0;
    }
    return -1;
}

static void publish(uint8_t *m) {
    put16(m + BM_AGE, 2);
    m[BM_RESERVED] = 0;
}

int bank_publish_blood(Sim *s) {
    uint8_t *m = bank_offer(s), *B = bank_record(s);
    publish(m);
    if (B) add32(B + BR_STOCK, -u16(m + BM_QUANTITY)); /* no test: the stock can go negative */
    return BANK_BLOOD_PUBLISHED;
}

int bank_publish_organ(Sim *s) {
    uint8_t *m = bank_offer(s), *B = bank_record(s);
    publish(m);
    if (B) {
        add16(B + BR_HELD, -1);
        put16(B + BR_SLOTS + 2 * (u16(pb(s) + BANK_P_SLOT) % BR_SLOT_COUNT), 0);
        sim_bank_compact(s);
    }
    return BANK_ORGAN_PUBLISHED;
}

/* ---- the offer text and the reports ------------------------------------------------------------------------------ */

uint32_t bank_value_per_5000(int price, int quantity) {
    /* Fault, reproduced: the divisor is the low byte of the quantity. Engine choice: a quantity that is a multiple of
     * 256 divides by zero in the original (a CPU exception); here that divisor counts as 1. */
    uint32_t d = (uint32_t)quantity & 255u;
    /* FIX-20 (docs/fixes.md): original divides by the quantity's low byte; fixed by the full u16 quantity */
    if (fix_on(20)) d = (uint32_t)quantity & 0xFFFFu;
    if (!d) d = 1;
    return ((uint32_t)price * 16384u) / d * 5000u / 16384u;
}

uint32_t bank_average_blood(Sim *s, uint32_t *last) {
    uint32_t q = 0, sum = 0;
    int last_price = 0;
    for (int i = 0; i < 100; i++) {
        const uint8_t *m = sim_market(s, i);
        if ((int8_t)m[BM_SELLER] < 0 || get16(m + BM_AGE) < 0 || m[BM_KIND] != 0) continue; /* drafts left out */
        q += (uint32_t)u16(m + BM_QUANTITY);
        sum += (uint32_t)u16(m + BM_PRICE);
        last_price = u16(m + BM_PRICE);
    }
    /* "none" is a last matching price of 0, as in the original; engine choice: a quantity sum of 0 keeps the old
     * value too (the original divides by it) */
    if (last_price && q) *last = sum * 16384u / q * 5000u / 16384u;
    return *last;
}

int bank_organ_offers(Sim *s, int id, int *price_sum) {
    int n = 0;
    *price_sum = 0;
    for (int i = 0; i < 100; i++) { /* reserved ones and drafts included, the shown one too */
        const uint8_t *m = sim_market(s, i);
        if ((int8_t)m[BM_SELLER] < 0 || m[BM_KIND] == 0 || m[BM_ID] != id) continue;
        n++;
        *price_sum += u16(m + BM_PRICE);
    }
    return n;
}

static int held_slots(const uint8_t *B) { /* engine choice: the slots read are limited to the 30 of the record */
    int n = get16(B + BR_HELD);
    return n < 0 ? 0 : n > BR_SLOT_COUNT ? BR_SLOT_COUNT : n;
}

void bank_organ_report(Sim *s, BankOrganReport *r) {
    const uint8_t *B = bank_record(s);
    r->held = r->places = r->free_places = r->average = 0;
    r->minimum = 100;
    if (!B) return;
    r->held = get16(B + BR_HELD);
    r->places = get16(B + BR_PLACES);
    r->free_places = r->places - r->held;
    int n = held_slots(B), sum = 0;
    for (int i = 0; i < n; i++) { /* the first n slots, empty ones counting 0 */
        int q = B[BR_SLOTS + 2 * i + 1] * (B[BR_SLOTS + 2 * i] != 0);
        sum += q;
        if (q < r->minimum) r->minimum = q;
    }
    if (r->held > 0) r->average = sum / r->held;
}

void bank_report_page(Sim *s, int step) {
    int p = get16(pb(s) + BANK_P_SLOT) + step;
    if (p == 3) p = 0;
    if (p == -1) p = 2;
    put16(pb(s) + BANK_P_SLOT, p);
}

int bank_report_list(Sim *s, int page, int *first) {
    const uint8_t *B = bank_record(s);
    *first = 0;
    if (!B || page < 1 || page > 2) return 0;
    int n = held_slots(B), count;
    if (page == 1) count = n < 18 ? n : 18;
    else if (n >= 19) *first = 18, count = n - 18 < 18 ? n - 18 : 18;
    else count = 0;
    for (int i = 0; i < count; i++)
        if (!B[BR_SLOTS + 2 * (*first + i)]) return i; /* the list stops at the first empty slot */
    return count;
}
