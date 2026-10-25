/* Module H, part 2: the scheduled newspaper generators of docs/news-generators.md - the five short random items, the
 * city statistics (type 3), training (type 0x28), the eighteen room reviews, and the English nurses' figure item.
 * Written from that document only. The copy-protection checks 0x2EB23 / 0x45FF4 that ten of the room reviews call
 * after the item is complete are left out (docs/news-generators.md, "Common rules"). Engine choices are marked. */
#include "sim_internal.h"

enum { N_HOSP = 8, N_PARAM = 21, N_TAG = 28 };
#define FURNITURE_LOW 0xE0 /* the low byte of the furniture catalog's address 0xEE0 (the low-byte quirk) */

static int hour_in(Sim *s, int a, int b) {
    int hour = world_tail(s->world, TAIL_HOUR);
    return hour >= a && hour <= b;
}
static int r1(Sim *s) { return (int)sim_bits(s, 1); }
static int r3(Sim *s) { return (int)sim_bits(s, 3); }
static int r7(Sim *s) { return (int)sim_bits(s, 7); }

/* ---- 1. the short random items ---------------------------------------------------------------------------------- */

/* 0x476E3 (types 1 / 2), 0x47766 (9 / 0x0A), 0x42D30 (0x0C / 0x0D): gate 50/30, r3 = 1 gives the class-1 variant */
static void short_pair(Sim *s, int head_type, int type) {
    int slot = sim_news_gate_draw(s, 50, 30);
    if (slot < 0) return;
    uint8_t *it;
    if (r3(s) == 1) {
        it = sim_news_new_item(s, slot, 1, head_type, 0, 0, 0, 0);
        if (head_type == 1) {
            it[N_PARAM] = (uint8_t)r7(s);
            it[22] = (uint8_t)r3(s), it[23] = (uint8_t)r3(s);
        } else {
            it[N_PARAM] = (uint8_t)r3(s);
        }
    } else {
        it = sim_news_new_item(s, slot, 2, type, 0, 0, 0, 0);
        it[N_PARAM] = (uint8_t)r3(s);
    }
    it[N_TAG] = 1;
}

/* 0x42CB6 (type 0x20) and 0x42CF3 (type 0x1E): gate 50/50, class 1, b21 = r7, b28 = 0 */
static void short_head(Sim *s, int type) {
    int slot = sim_news_gate_draw(s, 50, 50);
    if (slot < 0) return;
    uint8_t *it = sim_news_new_item(s, slot, 1, type, 0, 0, 0, 0);
    it[N_PARAM] = (uint8_t)r7(s);
}

/* ---- 2. the city statistics, type 3 (0x477D9) ------------------------------------------------------------------ */

static int in_business(Sim *s, int h) { return hospital(s, h)[0x15] != 0; }

/* the hospital in business with the largest (most) or smallest value; ties to the first for the largest, to the last
 * for the smallest; -1 when none is in business. v is read by `get`. */
typedef long (*Field)(Sim *s, int h);
static long opinion(Sim *s, int h) { return get32(hospital(s, h) + 0x44); }
static long not_cured(Sim *s, int h) { return get16(hospital(s, h) + 0x2A); }
static long deliveries(Sim *s, int h) { return get16(hospital(s, h) + 0xE6); }
static long billed(Sim *s, int h) { return (uint16_t)get16(hospital(s, h) + 0x26); }
static int extreme(Sim *s, Field get, int most, long *value) {
    int pick = -1;
    long best = 0;
    for (int h = 0; h < 16; h++) {
        if (!in_business(s, h)) continue;
        long v = get(s, h);
        if (pick < 0 || (most ? v > best : v <= best)) pick = h, best = v;
    }
    *value = best;
    return pick;
}
static long city_sum(Sim *s, Field get) {
    long sum = 0;
    for (int h = 0; h < 16; h++)
        if (in_business(s, h)) sum += get(s, h);
    return sum;
}

static void city_statistics(Sim *s) {
    int slot = sim_news_gate_draw(s, 120, 30);
    if (slot < 0) return;
    /* the header is cleared and b20 / b21 / b28 written before any condition, so a case without an item leaves them in
     * the free slot (docs/news-generators.md section 9.4 row 1); the count's + 1 / - 1 is the engine's live count */
    uint8_t *it = sim_news_clear(s, slot);
    int k = (int)sim_random(s, 7), subject = 0, cls = 1, w = 1, d = 0, b6 = 0, record = -1;
    it[20] = 3, it[N_PARAM] = (uint8_t)k, it[N_TAG] = 0;
    long v;
    switch (k) {
    case 0:
    case 1: {
        sim_opinion_sum(s); /* 0x1A684 on every attempt of cases 0 and 1, item or not */
        if (city_sum(s, billed) < (k == 0 ? 150 : 100)) return;
        int h = extreme(s, opinion, k == 0, &v);
        /* case 0: the highest sum must be above 0, otherwise byte 8 keeps its preset 0 (the doc's corner, kept) */
        if (h >= 0 && (k == 1 || v > 0)) subject = h;
        break;
    }
    case 2: {
        int h = extreme(s, not_cured, 1, &v);
        if (h < 0 || v < 7) return; /* the maximum starts at 0: nobody in business, no item */
        subject = h, cls = 2, w = 2, d = -1, b6 = 2;
        break;
    }
    case 3: { /* the minimum starts at 12,000 with subject 0, so nobody in business still gives an item for hospital 0 */
        long low = 12000;
        for (int h = 0; h < 16; h++)
            if (in_business(s, h) && not_cured(s, h) <= low) low = not_cured(s, h), subject = h;
        if (low < 2) return;
        cls = 2, w = 2, d = 1, b6 = 2;
        break;
    }
    case 4: {
        if (city_sum(s, deliveries) < 20) return;
        int h = extreme(s, deliveries, 1, &v);
        if (h >= 0) subject = h;
        w = 3, d = -2, b6 = 4;
        break;
    }
    default: { /* 5: the treatment-room record, 6: the ward record; the holder is not tested for being in business */
        int ward = k == 6;
        record = world_tail(s->world, ward ? 0x28 : 0x26); /* [0x6971] / [0x6973] */
        if (record < (ward ? 130 : 20)) return;
        subject = world_tail(s->world, ward ? 0x2C : 0x2A); /* [0x6975] / [0x6977] */
        w = 5, d = -1, b6 = 5;
        break;
    }
    }
    it = sim_news_new_item(s, slot, cls, 3, w, d, b6, subject);
    it[N_PARAM] = (uint8_t)k;
    if (record >= 0) put16(it + 22, record);
}

/* ---- 3. training, type 0x28 (0x49924) ---------------------------------------------------------------------------- */

static void training(Sim *s) {
    if (!hour_in(s, 5, 19)) return;
    int slot = sim_news_gate_draw(s, 500, 50);
    if (slot < 0) return;
    /* 0x38E77: 0x38D39, then the order list 0-15 bubble-sorted by +0x136 x 64 + +0x134, descending, stable */
    sim_staff_training(s);
    uint8_t order[16];
    for (int i = 0; i < 16; i++) order[i] = (uint8_t)i;
    for (int swapped = 1; swapped;) {
        swapped = 0;
        for (int i = 0; i < 15; i++) {
            const uint8_t *a = hospital(s, order[i]), *c = hospital(s, order[i + 1]);
            int32_t ka = (int32_t)((uint32_t)(uint16_t)get16(a + 0x136) * 64u + (uint16_t)get16(a + 0x134));
            int32_t kc = (int32_t)((uint32_t)(uint16_t)get16(c + 0x136) * 64u + (uint16_t)get16(c + 0x134));
            if (ka < kc) {
                uint8_t t = order[i];
                order[i] = order[i + 1], order[i + 1] = t, swapped = 1;
            }
        }
    }
    int h = order[r3(s)];
    const uint8_t *r = hospital(s, h);
    if (get16(r + 0x134) < 7 || get16(r + 0x136) < 90) return;
    uint8_t *it = sim_news_new_item(s, slot, 2, 0x28, 11, 1, 3, h);
    it[N_PARAM] = (uint8_t)r1(s);
    it[22] = (uint8_t)r1(s);
    put16(it + 10, get16(r + 0x134));
    put16(it + 12, get16(r + 0x136));
    put16(it + 14, get16(r + 0x138));
}

/* ---- 4. the room reviews ------------------------------------------------------------------------------------------ */

/* One fittings scan (docs/news-generators.md, "The fittings scans"). */
enum { K_DECO = 1 << 2 | 1 << 4 | 1 << 6 | 1 << 7 };
typedef struct {
    uint8_t on; /* 0: no scan (u24 is not written) */
    uint8_t equipment; /* the list walked: 0 furniture, 1 equipment */
    uint8_t rate_furniture; /* rated by the furniture catalog (quirk 2: 0x56DF3 rates its equipment list so) */
    uint8_t worst;
    uint8_t all_slots; /* 0: holes stop */
    uint8_t stop_u22; /* quirk 3: the walk ends at the id scan 1 found */
    uint8_t low_byte; /* quirk 1: the running value becomes s8(low byte of the item's catalog address) */
    int8_t min_q, max_q; /* considered only when min_q <= q <= max_q */
    uint32_t kinds; /* bit per accepted kind, 0 = any */
} Scan;
#define BEST_DECO(list) {1, 0, 0, 0, list, 0, 0, 2, 127, K_DECO}
#define HOLES 0
#define ALL 1

typedef struct {
    uint32_t routine;
    uint8_t type, cat, two_staff, ward, worst, min_score, city_cat, weight;
    int8_t change;
    uint8_t chance;
    Scan scan1, scan2;
} Review;

#define KB(k) (1u << (k))
static const Review REVIEWS[18] = {
    {0x47B6A, 0x0F, 2, 0, 0, 0, 30, 0, 2, 1, 3, BEST_DECO(HOLES), {0}},
    {0x47D46, 0x18, 2, 0, 0, 1, 0, 0, 2, -1, 4, {1, 0, 0, 1, HOLES, 0, 0, -128, 2, 0}, {0}},
    {0x4968E, 0x10, 3, 0, 0, 0, 40, 0, 2, 1, 4, BEST_DECO(HOLES), {1, 0, 0, 0, HOLES, 0, 0, -128, 127, KB(5)}},
    {0x56B68, 0x19, 3, 0, 0, 1, 0, 0, 2, -1, 4, {1, 0, 0, 1, HOLES, 0, 0, -128, 127, 0},
     {1, 0, 0, 1, HOLES, 1, 0, -128, 127, KB(5)}},
    {0x51838, 0x11, 5, 1, 0, 0, 30, 0, 2, 1, 4, BEST_DECO(ALL), {1, 1, 0, 0, ALL, 0, 0, -128, 127, KB(9) | KB(10) | KB(14)}},
    {0x510A6, 0x21, 5, 1, 0, 1, 0, 0, 2, -1, 4, {1, 0, 0, 1, HOLES, 0, 1, -128, 127, 0}, {1, 1, 0, 1, HOLES, 0, 0, -128, 127, 0}},
    {0x562CB, 0x13, 11, 1, 0, 0, 30, 0, 2, 1, 4, BEST_DECO(ALL),
     {1, 1, 0, 0, ALL, 0, 0, -128, 127, KB(10) | KB(12) | KB(13) | KB(14)}},
    {0x4C304, 0x22, 11, 1, 0, 1, 0, 0, 2, -1, 4, {1, 0, 0, 1, HOLES, 0, 0, -128, 127, 0}, {1, 1, 0, 1, HOLES, 0, 0, -128, 127, 0}},
    {0x55FEE, 0x14, 15, 1, 0, 0, 30, 0, 3, 1, 4, BEST_DECO(ALL), {1, 1, 0, 0, ALL, 0, 0, -128, 127, KB(9) | KB(10) | KB(14)}},
    {0x51338, 0x29, 15, 1, 0, 1, 0, 0, 2, -1, 4, {1, 0, 0, 1, HOLES, 0, 0, -128, 127, 0}, {1, 1, 0, 1, HOLES, 0, 0, -128, 127, 0}},
    {0x565AE, 0x2A, 6, 0, 1, 0, 20, 0, 2, 1, 4, BEST_DECO(HOLES), {1, 0, 0, 0, HOLES, 0, 0, -128, 127, KB(8)}},
    {0x56DF3, 0x2C, 6, 0, 1, 1, 0, 0, 2, -1, 4, {1, 1, 1, 1, HOLES, 0, 0, -128, 127, 0}, {1, 0, 0, 1, HOLES, 1, 0, -128, 127, KB(8)}},
    {0x5688B, 0x2B, 16, 0, 1, 0, 20, 0, 2, 1, 4, BEST_DECO(HOLES), {1, 0, 0, 0, HOLES, 0, 0, -128, 127, KB(8)}},
    {0x5708D, 0x2D, 16, 0, 1, 1, 0, 0, 2, -1, 4, {1, 1, 0, 1, HOLES, 0, 0, -128, 127, 0}, {1, 0, 0, 1, HOLES, 1, 0, -128, 127, KB(8)}},
    {0x57861, 0x2F, 7, 1, 0, 0, 30, 7, 3, 1, 4, {1, 0, 0, 0, ALL, 0, 0, 2, 127, KB(8)},
     {1, 1, 0, 0, ALL, 0, 0, -128, 127, KB(10) | KB(13) | KB(15)}},
    {0x57327, 0x30, 7, 1, 0, 1, 0, 7, 3, -1, 4, {1, 0, 0, 1, HOLES, 0, 0, -128, 127, KB(8)}, {1, 1, 0, 1, HOLES, 0, 0, -128, 127, 0}},
    {0x57B35, 0x31, 8, 1, 0, 0, 30, 8, 3, 1, 4, {1, 0, 0, 0, ALL, 0, 0, 2, 127, KB(5)}, {1, 1, 0, 0, ALL, 0, 0, -128, 127, KB(13) | KB(14)}},
    {0x575C4, 0x32, 8, 1, 0, 1, 0, 8, 3, -1, 4, {1, 0, 0, 1, HOLES, 0, 0, -128, 127, KB(5)}, {1, 1, 0, 1, HOLES, 0, 0, -128, 127, 0}},
};

static int type_in_table(Sim *s, int type) { /* 0x47B3B: any item of the type, any hospital, printed or not */
    for (int i = 0; i < 100; i++) {
        const uint8_t *it = sim_news_item(s, i);
        if (it[0] && it[20] == type) return 1;
    }
    return 0;
}

/* One scan over a room record's slot list: the id of the last item taken, or -1. `start` is the running value's start
 * (best: 0 for scan 1, -1 for scan 2; worst: 100); u22 is scan 1's id for the stop of quirk 3. */
static int fittings_scan(Sim *s, const uint8_t *rec, const Description *d, const Scan *sc, int start, int u22) {
    int off = sc->equipment ? d->equipment : d->furniture, n = sc->equipment ? d->equipment_slots : d->furniture_slots;
    int v = start, found = -1;
    for (int i = 0; i < n; i++) {
        int id = (uint16_t)get16(rec + off + 2 * i);
        if (!sc->all_slots && id == 0) break; /* holes stop: the pointer is not advanced on an empty slot */
        if (sc->stop_u22 && id == u22) break;
        int furniture = sc->equipment ? sc->rate_furniture : 1;
        const uint8_t *e = furniture ? sim_furniture_item(s, id) : sim_equipment_item(s, id);
        if (!program_contains(s->prog, e, 7)) continue; /* engine guard: an id outside the image */
        int q = (int8_t)e[4], kind = e[6];
        if (sc->kinds && (kind > 31 || !(sc->kinds & 1u << kind))) continue;
        if (q < sc->min_q || q > sc->max_q) continue;
        if (sc->worst ? q > v : q <= v) continue;
        found = id;
        v = sc->low_byte ? (int8_t)(uint8_t)((FURNITURE_LOW + 18 * id) & 0xFF) : q;
    }
    return found;
}

static int city_count(Sim *s, int cat) {
    /* [0x5BCD] (OPs) / [0x5BCF] (torture chambers): what the last onset's step 3 stored (SIM-51), not counted now */
    return s->city[cat == 7 ? CITY_OP : CITY_TORTURE];
}

static void room_review(Sim *s, const Review *rv) {
    int slot = sim_news_gate_draw(s, 250, 40);
    if (slot < 0 || !hour_in(s, 4, 18)) return;
    if (rv->city_cat && city_count(s, rv->city_cat) < 3) return;
    if (type_in_table(s, rv->type)) return;
    int skip0 = !rv->worst && r1(s) == 0; /* the r1 rule for hospital 0 ([0x5CB3]) */
    int best = rv->worst ? 10000 : 0, ph = -1, pi = -1;
    for (int h = 0; h < 16; h++) {
        if (h == 0 && skip0) continue;
        int rooms = sim_rooms_in_use(s, h, rv->cat); /* engine guard: at most the table's records */
        for (int i = 0; i < rooms; i++) {
            const uint8_t *rec = sim_room_record(s, h, rv->cat, i);
            if (!get16(rec + 8) || (rv->two_staff && !get16(rec + 0x0A))) continue;
            int score = (int16_t)(2 * rec[0] + rec[1]);
            if (rv->ward) score = (int16_t)(score + 40 - 4 * get16(rec + 0x0A));
            if (rv->worst ? score <= best : score >= best) best = score, ph = h, pi = i;
        }
    }
    if (ph < 0 || (!rv->worst && best < rv->min_score)) return;
    const uint8_t *rec = sim_room_record(s, ph, rv->cat, pi);
    /* once the room is chosen the item's bytes are written before the scans, b21's r1 drawn here, and each scan's
     * result stored before it is tested (FFFF for none), so a scan without a result leaves them in the free slot
     * (docs/news-generators.md section 9.4 row 2) */
    uint8_t *it = sim_news_clear(s, slot);
    it[4] = rv->weight, it[5] = (uint8_t)rv->change, it[6] = rv->chance, it[N_HOSP] = (uint8_t)ph;
    put16(it + 14, get16(rec + 8)); /* the nurse; the receptionist for the reception */
    if (rv->two_staff) put16(it + 10, get16(rec + 0x0A)); /* the doctor */
    it[20] = rv->type;
    it[N_PARAM] = (uint8_t)r1(s);
    if (rv->cat != 2) it[26] = (uint8_t)(pi + 1);
    it[N_TAG] = 0;
    sim_rating_record(s, rec); /* the winning room survives failed fittings scans */
    Description d = sim_description(s, rv->cat);
    int u22 = fittings_scan(s, rec, &d, &rv->scan1, rv->scan1.worst ? 100 : 0, -1);
    put16(it + 22, u22 < 0 ? 0xFFFF : u22);
    if (u22 < 0) return;
    if (rv->scan2.on) {
        int u24 = fittings_scan(s, rec, &d, &rv->scan2, rv->scan2.worst ? 100 : -1, u22);
        put16(it + 24, u24 < 0 ? 0xFFFF : u24);
        if (u24 < 0) return;
    }
    sim_news_commit(s, slot, 2);
    /* the answer check 0x2EB23 / 0x45FF4 that ten reviews call here is copy protection: left out
     * (docs/news-generators.md, "Common rules") */
}

/* ---- 5. English tick 39, nurses' figure news (SIM-104) --------------------------------------------------------- */

static void nurses_figure(Sim *s) {
    if (!hour_in(s, 3, 19)) return;
    int slot = sim_news_gate_draw(s, 500, 50);
    if (slot < 0) return;
    sim_staff_averages(s);
    uint8_t order[16];
    for (int i = 0; i < 16; i++) order[i] = (uint8_t)i;
    for (int swapped = 1; swapped;) {
        swapped = 0;
        for (int i = 0; i < 15; i++) {
            if (get16(hospital(s, order[i]) + 0x4A) < get16(hospital(s, order[i + 1]) + 0x4A)) {
                uint8_t t = order[i];
                order[i] = order[i + 1], order[i + 1] = t, swapped = 1;
            }
        }
    }
    int h = order[sim_bits_site(s, 3, 0x628D3)];
    const uint8_t *r = hospital(s, h);
    if (get16(r + 0x16A) < 4 || get16(r + 0x16C) <= 0) return;
    uint8_t *it = sim_news_new_item(s, slot, 2, 0x2E, 11, 1, 3, h);
    it[21] = (uint8_t)sim_bits_site(s, 1, 0x62915);
    it[22] = (uint8_t)sim_bits_site(s, 1, 0x6291D);
    put16(it + 10, get16(r + 0x4A));
    put16(it + 12, get16(r + 0x16A));
    put16(it + 14, get16(r + 0x16C));
}

/* ---- dispatch ----------------------------------------------------------------------------------------------------- */

int sim_news_gen_run(Sim *s, uint32_t routine) {
    switch (routine) {
    case 0x476E3: short_pair(s, 1, 2); return 1;
    case 0x47766: short_pair(s, 9, 0x0A); return 1;
    case 0x42D30: short_pair(s, 0x0C, 0x0D); return 1;
    case 0x42CB6: short_head(s, 0x20); return 1;
    case 0x42CF3: short_head(s, 0x1E); return 1;
    case 0x477D9: city_statistics(s); return 1;
    case 0x49924: training(s); return 1;
    case 0x62888:
        if (program_english(s->prog)) nurses_figure(s);
        return 1; /* disk and German tick 39 still enter the preceding no-op */
    default: break;
    }
    for (int i = 0; i < 18; i++)
        if (REVIEWS[i].routine == routine) {
            room_review(s, &REVIEWS[i]);
            return 1;
        }
    return 0;
}
