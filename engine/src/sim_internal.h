/* Shared between the simulation's source files: little-endian field access and table layouts
 * (docs/sim.md "Shared tables", docs/sim-ai.md "Tables"). Not part of the engine's public interface. */
#ifndef BIING_SIM_INTERNAL_H
#define BIING_SIM_INTERNAL_H

#include "fixes.h" /* the opt-in fixes of the original's bugs (docs/fixes.md) */
#include "sim.h"
#include "staff_record.h"

/* The price byte of a market offer from a price class value: (value + 2) / 2 as a byte (FIX-16 on: capped at 255;
 * sim_staff.c). */
int sim_offer_price(int value);
/* The quality of an organ taken from a body dead for `dead` ticks (field 104, 0x6F9F2; FIX-23; sim_rooms.c). */
int sim_organ_quality(int dead);

#define H_CASH 0x16
#define H_PLOTS_OWNED 0x1A
#define H_PLOTS_RENTED 0x1C
#define H_PLOTS_BUILT 0x1E
#define ROOM_SLOTS 200
#define ROOM_SLOT_SIZE 2
#define STAFF_SLOTS 200
#define STAFF_SLOT_SIZE 20
#define JOB_OPENINGS 200
#define JOB_OPENING_SIZE 7
#define CATALOG 0x31F2
#define CATALOG_SIZE 60
#define CATALOG_RENT 0x1E
#define CATALOG_PRICE 0x20
#define CATALOG_PLOTS 0x26
#define CATALOG_BASE 0x29
#define CATALOG_CATEGORY 0x2A
#define CATALOG_FURNITURE 0x2C
#define CATALOG_EQUIPMENT 0x34
#define PLOT_RENT 22
#define DEALER 16
#define FURNITURE_IDS 77 /* catalog entries 0-76 */
#define EQUIPMENT_IDS 155 /* catalog entries 0-154 */
#define BLK_STORES 12 /* save block 13: the store-room records, 730 bytes per hospital */
#define MARKET_OFFERS 300 /* offers per market, 8 bytes each (docs/sim-ai.md, "Markets") */
/* The store-room record (category 4, docs/sim-staff.md "The store room", docs/ui-purchasing.md) */
enum { STORE_CLERK = 4, STORE_CAPACITY = 7, STORE_FURNITURE_HELD = 8, STORE_EQUIPMENT_HELD = 9, STORE_FURNITURE = 10,
       STORE_EQUIPMENT = 0xFA, STORE_FURNITURE_COND = 0x1EA, STORE_EQUIPMENT_COND = 0x262, STORE_SLOTS = 120 };

static inline int32_t get32(const uint8_t *p) {
    return (int32_t)(p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}
static inline void put32(uint8_t *p, int32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)((uint32_t)v >> (8 * i));
}
static inline int16_t get16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }
static inline void put16(uint8_t *p, int v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
/* wraps like the original's 32-bit add; a signed int32_t sum would be undefined in C on overflow */
static inline void add32(uint8_t *p, int32_t v) { put32(p, (int32_t)((uint32_t)get32(p) + (uint32_t)v)); }
static inline void add16(uint8_t *p, int v) { put16(p, get16(p) + v); }

static inline uint8_t *hospital(Sim *s, int h) { return world_hospital(s->world, h); }
static inline int32_t cash_of(Sim *s, int h) { return get32(hospital(s, h) + H_CASH); }
static inline uint8_t *room_slots(Sim *s, int h) {
    return s->world->block[BLK_ROOM_SLOTS] + h * ROOM_SLOTS * ROOM_SLOT_SIZE;
}
static inline uint8_t *staff_slots(Sim *s, int h) {
    return s->world->block[BLK_STAFF_SLOTS] + h * STAFF_SLOTS * STAFF_SLOT_SIZE;
}
static inline uint8_t *room_counts(Sim *s, int h) { return s->world->block[BLK_ROOM_COUNTS] + h * 100; }
/* Sim.city: the six city counts the onset stores (SIM-51), by room category */
enum { CITY_5, CITY_11, CITY_15, CITY_OP, CITY_TORTURE, CITY_GARAGE };
/* 0x6953D step 3: the six sums of room-count bytes 5, 11, 15, 7, 8, 10 over hospitals 0-15 into Sim.city (16-bit) */
static inline void sim_store_city(Sim *s) {
    static const int CAT[6] = {5, 11, 15, 7, 8, 10};
    for (int k = 0; k < 6; k++) {
        unsigned n = 0;
        for (int h = 0; h < 16; h++) n += room_counts(s, h)[CAT[k]];
        s->city[k] = (uint16_t)n;
    }
}
static inline uint32_t catalog_at(int type, int off) { return CATALOG + (uint32_t)type * CATALOG_SIZE + (uint32_t)off; }
static inline int catalog_u16(const Sim *s, int type, int off) { return program_word(s->prog, CATALOG, catalog_at(type, off) - CATALOG); }
static inline int catalog_u8(const Sim *s, int type, int off) { return program_u8(s->prog, CATALOG, catalog_at(type, off) - CATALOG); }
static inline int catalog_rent(const Sim *s, int type) { return catalog_u16(s, type, CATALOG_RENT); }

/* The input pairs of docs/rng-sites.md (section 2): measured constants of the original, as addresses of its memory
 * layout. RNG_HOSPITAL16: [0x4528] after any loop over the hospitals (hospital record 16); RNG_AI_PLAYER: the AI's
 * scratch player block that 0x1C927 loads into EDI; RNG_WAITING_ROOMS: block 12's base (the arrival's EBP). */
#define RNG_HOSPITAL16 0x1F21A6u
#define RNG_HOSPITAL0 0x1F0266u /* hospital record 0; record h at + 500 h, also past 16 (docs/rng-sites.md 2.4) */
#define RNG_GARAGES 0x16A270u   /* garage record 0 (block 5); record h at + 158 h */
#define RNG_AI_PLAYER 0x6187u
#define RNG_WAITING_ROOMS 0x16E3F0u
/* The same per build (docs/rng-sites.md section 8): the heap addresses move by the build's heap shift, the scratch
 * player block is the build's offset of 0x6187 (sim_init sets both). */
static inline uint32_t rng_hospital16(const Sim *s) { return RNG_HOSPITAL16 + s->rng_heap; }
static inline uint32_t rng_waiting_rooms(const Sim *s) { return RNG_WAITING_ROOMS + s->rng_heap; }
/* [0x4528]'s writers (docs/rng-sites.md 2.4): the loops over the 16 hospitals leave hospital record 16, the room ticks
 * a room record + 0x14, the home treatment a hospital record or 0, the race finish a garage record */
static inline void rng_scratch_hospital16(Sim *s) { s->rng_scratch = rng_hospital16(s); }
static inline void rng_scratch_room(Sim *s, const uint8_t *room) { s->rng_scratch = sim_address(s, room) + 0x14; }
/* context S: a scheduled routine before it selects a record (D and EBP what the clock call passes, docs/sim-drift8.md
 * section 14, sim_clock_registers; [0x4528] what its last writer left) */
static inline SimIn rng_scheduler(const Sim *s) { return sim_in(s->clock_d, s->clock_ebp, s->rng_scratch); }
/* context M, the offers' ageing (docs/rng-sites.md 2.5, 3.7): D and EBP the clock call's pair, as context S;
 * [0x4528] what its last writer left (the waiting-room tick). The 3.5 % with EBP = hospital record 16 are not modelled. */
static inline SimIn rng_offers(const Sim *s) { return sim_in(s->clock_d, s->clock_ebp, s->rng_scratch); }
/* the address of hospital h's room-count row (the AI operators' EBP) */
static inline uint32_t rng_row(Sim *s, int h) { return sim_address(s, s->world->block[BLK_ROOM_COUNTS] + h * 100); }
/* context O, an AI room operator: D = 0x6187 for the room pick, then the picked room's record; EBP = the row of h */
static inline SimIn rng_operator(Sim *s, int h, const uint8_t *room) {
    return sim_in(room ? sim_address(s, room) : s->rng_player, rng_row(s, h), s->rng_scratch);
}
/* context B, a bed or place of a per-tick room routine: D = the room record, EBP = the person record (or what the
 * caller names), [0x4528] = the room record + 0x14 */
static inline SimIn rng_bed(Sim *s, const uint8_t *room, uint32_t ebp) {
    uint32_t r = sim_address(s, room);
    return sim_in(r, ebp, r + 0x14);
}
/* 0x2B4A9, the comfort helper: r15 + 22, with the same pair as the operator's comfort draw before it */
static inline uint8_t rng_comfort(Sim *s, SimIn in) { return (uint8_t)(sim_bits_in(s, 15, 0x2B4AE, in) + 22); }

/* Room description of a category (docs/sim-ai.md, program image 0x4758). */
typedef struct {
    int furniture, furniture_slots, equipment, equipment_slots, cond_a, cond_b, type;
} Description;

/* sim_ai.c: shared with the other modules */
Description sim_description(Sim *s, int cat);
uint8_t *sim_room_record(Sim *s, int h, int cat, int i); /* room instance i of category cat */
/* [0x4514] writers only: generic room lookups must not change this persistent context. */
void sim_rating_record(Sim *s, const uint8_t *record);
void sim_rating_catalog(Sim *s, int type);
int sim_free_icu(Sim *s, int h); /* first free record, or -1; selects the found room */
void sim_rating_room(Sim *s, int h, int cat, int i); /* i may be the loop's one-past position */
uint8_t sim_rating_byte(const Sim *s);
void sim_rating_advance(Sim *s, size_t bytes); /* advance the retained reference, including a nested writer's */
int sim_rooms_in_use(Sim *s, int h, int cat);
void sim_recompute(Sim *s, int cat); /* ratings, seats, beds of every room of the category */
void sim_recompute_wear(Sim *s, int cat, int with_wear); /* the same, with the wear 0x2B5B3 when with_wear */
void sim_create_room(Sim *s, int h, int cat, int type); /* 0x1DE67, also the player's (estate.c) */
int sim_pick(Sim *s); /* 0x62CFD, the AI picker */
int32_t sim_expected_cash(Sim *s, int h); /* 0x1B015 */
void sim_expected_cash_all(Sim *s); /* 0x1A941: +0xE2 of hospitals 0-15 */
const uint8_t *sim_furniture_item(Sim *s, int id);
const uint8_t *sim_equipment_item(Sim *s, int id);
int sim_ai_run(Sim *s, uint32_t routine); /* 1 if the routine is one of modules F and G */
int sim_project_cost(int type, int level, int duration); /* 0x4D463 */
int sim_ai_choice(Sim *s, int h); /* the category 0x6337D would build for h, 0 for none (for the tests) */
/* sim_patients.c */
int sim_patients_run(Sim *s, uint32_t routine); /* 1 if the routine is one of module B */
/* sim_staff.c */
int sim_staff_run(Sim *s, uint32_t routine); /* 1 if the routine is one of module E */
int sim_assignable(Sim *s, int h, int group, int *slot_out); /* 0x22D5F: staff index or 0 */
int sim_idle_candidate(Sim *s, int h, int group, int *slot_out); /* 0x22D73 without the request flag */
void sim_assign(Sim *s, int h, int index, int slot, int cat, int room, uint8_t *field);
void sim_release(Sim *s, int h, int index); /* 0x22EDD */
int sim_mood_working(Sim *s, StaffId employee, StaffSlotId slot_id); /* step 9: 1 loss, 0 none, 2 rise */
void sim_leave_room(Sim *s, int h, int index); /* 0x2301C: back to idle, no off-duty branch */
int sim_rating_threshold(Sim *s, int h); /* 0x2B0FB */
int sim_furniture_room(Sim *s, int h, int cat, uint8_t *rec); /* 0x2A59A: 1 when a furniture slot is (made) free */
int sim_equipment_room(Sim *s, int h, int cat, uint8_t *rec); /* 0x2A499: 1 when an equipment slot is (made) free */
void sim_fittings(Sim *s, int h, int cat, uint8_t *rec, uint8_t *want); /* the fittings rule of a room operator */
int sim_store_supply(Sim *s, int h, int cat, uint8_t *rec, int id, int by_kind); /* 0x2D003 / 0x2A7C4 + 0x2B07E */
void sim_store_tidy(Sim *s); /* 0x2A873 */
void sim_close_gap(uint8_t *ids, uint8_t *cond, int n); /* 0x2AF09: one gap of a list of n slots */
void sim_tidy_lists(Sim *s); /* 0x2BFB7: the first gap of every room list of hospitals 0-15 */
void sim_recompute_all(Sim *s); /* 0x2B210: the recompute of every category, without wear */
void sim_recompute_all_wear(Sim *s); /* 0x2B207: the same with wear */
/* 0x2F610 (equipment) / 0x391D2 (furniture), the purchase of market offer i by hospital `buyer` into its store, shared
 * by the AI (player = 0, [0x5ABD] = FF: no cash test) and the player's thumb (player = 1). */
enum { BUY_DONE, BUY_OWN, BUY_FREE, BUY_NO_CASH };
int sim_buy_offer(Sim *s, int buyer, int i, int equipment, int player);
int sim_waiting_for(Sim *s, int h, int cat); /* waiting patients for room category 5, 11 or 15 */
uint8_t *sim_market(Sim *s, int i); /* slot i (0-99) of the blood/organ market, save block 24 */
int sim_organ_price(Sim *s, int id); /* base(id) + r7 x (base(id) / 16 + 1), the price of a new organ offer */
/* sim_rooms.c */
int sim_rooms_run(Sim *s, uint32_t routine); /* 1 if the routine is one of module C */
int sim_rooms_after_operator(Sim *s, int h, int cat, int room); /* the patient side of an AI room operator; 1: release check */
void sim_rooms_supplies(Sim *s, int h, int cat, int room); /* the equipment supplies step of operators 5, 11, 15 */
int sim_invoice(Sim *s, int h, const uint8_t *patient); /* 0x2E148 */
void sim_bank_compact(Sim *s); /* 0x4BF9B, the organ-slot compaction of every blood bank */
uint8_t *sim_bank(Sim *s, int h); /* the bank record of hospital h, NULL without a bank (site byte 17) */

/* sim_news.c: module H, the newspaper (docs/mainloop.md) */
int sim_news_run(Sim *s, uint32_t routine); /* 1 if the routine is one of module H */
uint8_t *sim_news_item(Sim *s, int i); /* item i (0-99) of save block 31 */
uint8_t *sim_news_issue(Sim *s); /* the four item numbers of the issue, tail +0x16 */
int sim_news_count(Sim *s);
int sim_news_gate_draw(Sim *s, int chance, int limit); /* 0x42E01: the free slot or -1 */
/* 0x42DAE alone (bytes 1-19 = 0, byte 0 untouched): the start of an item that may stay unwritten (section 9) */
uint8_t *sim_news_clear(Sim *s, int slot);
void sim_news_commit(Sim *s, int slot, int cls); /* byte 0 = the class; counts b20 in news_written */
/* 0x42DAE + byte 0, type (b20), w, d, b6, byte 8, b28 = 0; counts the type in news_written */
uint8_t *sim_news_new_item(Sim *s, int slot, int cls, int type, int weight, int change, int chance, int h);
/* sim_news_gen.c: the scheduled generators of docs/news-generators.md; 1 if the routine is one of them */
int sim_news_gen_run(Sim *s, uint32_t routine);
void sim_news_bill(Sim *s, int h, int index, int bill, int ward); /* 0x42982 / 0x42AEA with the records */
void sim_news_gave_up(Sim *s, int h, int index, int variant); /* 0x493AD / 0x493D0 / 0x493F3 */
void sim_news_home_failure(Sim *s, int index, int type); /* 0x494CB */
void sim_news_death(Sim *s, int h, int index); /* 0x4C904 and 0x4CA9B */
/* 0x4C904 with intensive-care record k of hospital h as the person (0x70321 without a pathology place, SIM-18) */
typedef struct {
    uint32_t c; /* the walk's index; the item keeps c AND 0xFFFF */
    uint32_t offset; /* the name copy's dword, from the person table (mod 2^32) */
    uint32_t linear; /* that dword's linear address in the original */
} IcuDeathWalk;
IcuDeathWalk sim_news_icu_walk(int h, int k);
void sim_news_icu_death(Sim *s, int h, int k, const uint8_t *rec); /* and 0x4CA9B */
void sim_news_long_stay(Sim *s, int h); /* 0x4CE7A */
void sim_news_comfort(Sim *s, int h, int index, int kind, int meal); /* 0x4CDAC; u16 14 = meal */
void sim_news_price(Sim *s, int h); /* 0x4763B */
void sim_news_closure(Sim *s, int h); /* 0x4CB5E */
void sim_news_course(Sim *s, int h, int index, int course); /* 0x495A0: type 0x1F, a finished training course */
void sim_news_campaign(Sim *s, int h); /* 0x4E58A: type 0x15, the end of hospital h's advertising campaign */
/* sim_staff.c */
void sim_staff_break(Sim *s, int h, int cat, int instance, uint8_t *field, uint8_t *extra); /* 0x2E666 */
/* sim_rooms.c */
void sim_reset_healthy(uint8_t *p); /* 0x30DE6 */
void sim_rename_dead(Sim *s, int index); /* 0x3200C: the first word of the name becomes a stock first name */
/* the wrong path of the diagnoses 0x30ED0 / 0x34E36: sixteen candidates within 600 tries, then the pick by pairs with
 * the skill S (0x2D62D) of the room's doctor (+10); the real illness (field 56) when the list stays short */
int sim_wrong_guess(Sim *s, const uint8_t *p, const uint8_t *room);
int sim_bill_reaction(const uint8_t *p, int bill, int ward); /* the opinion step v of 0x320FE / 0x32075 */
void sim_opinion_after_bill(uint8_t *p, int h, int bill, int ward); /* 0x320FE / 0x32075 applied (for the tests) */
/* The player's branches of the room helpers (docs/ui-rooms.md), for rooms.c; h is the acting hospital. */
enum { SEAT_DONE, SEAT_NO_ROOMS, SEAT_FULL, SEAT_NO_FORM, SEAT_NO_PEN };
int sim_player_seat(Sim *s, int h, int *room); /* 0x2B989: reception queue place 1 to a waiting room */
void sim_player_leave(Sim *s, int h); /* 0x2BE91: queue place 1 leaves, opinion halved */
int sim_free_beds(Sim *s, int h, int cat, int *ward, int *total); /* 0x2B4B3 / 0x2B52C */
int sim_player_bed(Sim *s, int h, int ward, int place, int ai); /* 0x2336B: the bed, or -1 (ward full) */
int sim_player_call(Sim *s, int h, int room, int *from); /* 0x2E3F2 + move: the patient or 0 */
void sim_player_propose(Sim *s, int h, int room); /* 0x3104F */
enum { AFTER_IN_ROOM, AFTER_BILL, AFTER_DONOR, AFTER_RUNNING, AFTER_WAITING, AFTER_LEFT, AFTER_QUEUE_FULL,
       AFTER_NO_WARD, AFTER_QUEUED, AFTER_NOTHING };
int sim_player_after_diagnosis(Sim *s, int h, int room, int *n); /* 0x2D11A */
int sim_player_bill(Sim *s, int h, int room, int *v); /* 0x2D957: the bill */
int sim_player_abort(Sim *s, int h, int room, int *n); /* 0x241EC: 1 if the donor-done step ran */
int sim_player_discharge(Sim *s, int h, int ward, int bed, int *v); /* 0x29DE0: the bill */
void sim_player_step(Sim *s, int index); /* 0x4F1B9 */
int sim_player_icu(Sim *s, int h, int ward, int index); /* 0x4F395: the record, -1 none free, -2 no nurse */
int sim_player_corpse(Sim *s, int h, int index, int cat, int room, int place, int *staff);
/* The same helpers by room category (5 treatment room, 11 dental, 15 neurology; 6 / 16 for the wards) and the
 * specialist departments' branches (docs/ui-departments.md) */
void sim_player_propose_in(Sim *s, int h, int cat, int room);
int sim_player_bill_in(Sim *s, int h, int cat, int room, int *v);
int sim_player_abort_in(Sim *s, int h, int cat, int room, int *n);
int sim_player_discharge_in(Sim *s, int h, int cat, int ward, int bed, int *v);
int sim_player_call_in(Sim *s, int h, int cat, int room, int *from);
int sim_player_after_clinic(Sim *s, int h, int cat, int room, int *n); /* 0x34C5C / 0x35E59 */
int sim_player_neuro_bed(Sim *s, int h, int room, int ward); /* 0x235ED: the bed or -1 */
int sim_player_icu_back(Sim *s, int h, int room); /* 0x520DC: the queue place, -1 full, -2 narcosis */
int sim_player_theatre_fetch(Sim *s, int h, int cat, int room, int *ward);
enum { THEATRE_STARTED = 0, THEATRE_NO_ORGAN = -1, THEATRE_STOPPED = -2 }; /* else: the missing capability */
int sim_player_theatre_toggle(Sim *s, int h, int cat, int room);
int sim_player_theatre_back(Sim *s, int h, int cat, int room);
int sim_player_pathology_take(Sim *s, int h);
int sim_player_pathology_throw(Sim *s, int h);
/* sim_news.c: the reception's refusal items 0x42916 (type 0x1C, d -2) and 0x428BD (type 0x1B, d -1) */
void sim_news_refusal(Sim *s, int h, int index, int type);
/* sim_news.c: the thug outcomes of 0x4A822 (docs/mainloop.md, module H item 5): 0x4C2AE (type 6, b21 0, a victim
 * delivered), 0x4C165 (type 6, b21 1, an employee beaten up), 0x4C244 (type 0x0B, b21 0, a person mission lost) and
 * 0x4C1BF (type 0x0B, b21 1, a staff mission lost). victim = person or staff index, victim_h = its hospital (staff) */
void sim_news_thug(Sim *s, int type, int b21, int h, int victim_h, int victim, int thug, int param);
void sim_news_new_room(Sim *s, int h, int cat, int instance); /* 0x46CC4 */
/* sim_rooms.c */
int sim_cook_level(Sim *s, int index); /* 0x4DF60 */
void sim_worsen_in_treatment(Sim *s, int h, uint8_t *p, int flag); /* 0x2E8BB, for the tests */
void sim_worsen(Sim *s, int h, uint8_t *p, int flag); /* 0x2E975, for the tests */
void sim_nurse_care(Sim *s, int h, uint8_t *p, const uint8_t *room, int comfort, int health_flag); /* 0x34D22, tests */
/* sim_thugs.c: the thug missions (docs/ui-support.md, "Thug missions") */
int sim_thugs_run(Sim *s, uint32_t routine); /* 1 for 0x4A822 */
/* 0x4834B: the mission of the staff slot's thug, by the hour; player = 1 for the player's send ([0x5ABD] = 0), which
 * alone makes the sound (the message box 911/912 is the screen's, support_ui.c) */
void sim_thug_send(Sim *s, int h, int slot, int player);
int sim_thug_strength(Sim *s, const uint8_t *st, const uint8_t *sl); /* 0x4AE32, with its own t&63 */
/* sim_race.c: the race and the garages (docs/ui-garage.md) */
int sim_race_run(Sim *s, uint32_t routine); /* 1 for 0x67BFB, 0x6D515, 0x6DA45, 0x57F15, 0x542F8 */
/* sim_news.c: the race item 0x55F4A (type 0x23): the winner's +0x160 + 1 */
void sim_news_race(Sim *s, int winner, int second, int driver1, int driver2, int model, int person);
/* sim_rooms.c: "has" (0x2C20A) a capability among a room's equipment, and a furniture kind (0x2C0BA) */
int sim_has_equipment(Sim *s, uint8_t *rec, int cat, int cap);
int sim_has_furniture_kind(Sim *s, const uint8_t *rec, int cat, int kind);
int sim_has_equipment_kind(Sim *s, uint8_t *rec, int cat, int kind); /* 0x2BD17 */
/* sim_books.c: the scheduled bookkeeping of docs/sim-bookkeeping.md */
int sim_books_run(Sim *s, uint32_t routine);
int sim_use_furniture(Sim *s, uint8_t *rec, int cat, int kind); /* 0x2C0E7: 1 found (and worn), 0 missing */

/* sim_rooms.c: the AI-mode branches module D calls for hospital 0 (docs/sim-player.md) */
int sim_ai_bring(Sim *s, int h, int cat, int room); /* the patient fetched, or 0 */
void sim_ai_choose(Sim *s, int h, int cat, int room);
void sim_ai_act(Sim *s, int h, int cat, int room);
int sim_ai_theatre_start(Sim *s, int h, int cat, int room); /* THEATRE_STARTED, THEATRE_NO_ORGAN or a capability */
/* sim_player.c: module D, the player's independent staff */
int sim_player_run(Sim *s, uint32_t routine); /* 1 for the ten module D routines */
int sim_off_duty_screen(int screen); /* 0x55205: staff may go off duty while this screen is shown */

#endif
