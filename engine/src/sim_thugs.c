/* The thugs' missions, from docs/ui-support.md ("110 - thugs", send out, and "Thug missions: 0x4A822"): the send-out
 * part 0x4834B shared by the player's screen (and, once it is in the engine, the AI thug routine 0x48215), the
 * strength 0x4AE32 and the per-minute routine 0x4A822 with its person and staff missions. The copy-protection hooks
 * inside 0x4A822 (0x45FF4, 0x2EB23) are left out (docs/mainloop.md, section 7). */
#include "sound.h"
#include "sim_internal.h"

#define PERSONS 1009
#define PERSON_SIZE 106
#define STAFF_SIZE 32
#define STAFF_LAST 1040
#define MISSION_ROOMS 0xA8E1 /* three room categories a person mission needs one of: 5, 11, 15 */
#define INJURY_PICK 0x5FDB /* 8 bytes per r: the injury entry */
#define INJURIES 0x5FC3 /* 4 bytes per entry: illness (field 56), field 80, field 41 */

enum { SL_INDEX = 0, SL_GROUP = 2, SL_STATE = 3, SL_ROOM = 4, SL_HOURS = 7, SL_CUSTODY = 0x0A, SL_OFF_DUTY = 0x0B,
       SL_VICTIM = 0x0C, SL_COUNT = 0x0E, SL_PARAM = 0x0F };
enum { T_STATE = 4, T_MOOD = 21, T_MARK = 23, T_ROOM = 26 };
enum { PERSON_MISSION = 0x14, STAFF_MISSION = 0x15, CUSTODY = 0x16, OFF_DUTY = 0x13, THUGS = 8 };

static uint8_t *staff_rec(Sim *s, int i) { return s->world->block[BLK_STAFF] + i * STAFF_SIZE; }
static uint8_t *person_rec(Sim *s, int i) { return s->world->block[BLK_PATIENTS] + i * PERSON_SIZE; }
static int image_byte(Sim *s, uint32_t table, uint32_t index) { return program_u8(s->prog, table, index); }

/* pb' +0x9E = FF: the player block of hospital 0 or 1; the other hospitals write a dummy */
static void redraw(Sim *s, int h) {
    if (h == 0) s->world->block[BLK_PLAYER0][0x9E] = 0xFF;
    else if (h == 1) s->world->block[BLK_PLAYER1][0x9E] = 0xFF;
}

static void start(uint8_t *sl, uint8_t *st, int state) {
    sl[SL_STATE] = st[T_STATE] = (uint8_t)state;
    sl[SL_ROOM] = 0, st[T_ROOM] = 0, sl[SL_HOURS] = 0;
    put16(sl + SL_VICTIM, 0);
}

void sim_thug_send(Sim *s, int h, int slot, int player) { /* 0x4834B */
    uint8_t *sl = staff_slots(s, h) + slot * STAFF_SLOT_SIZE, *st = staff_rec(s, get16(sl + SL_INDEX));
    int hour = world_tail(s->world, TAIL_HOUR);
    if (hour >= 4 && hour <= 18) {
        start(sl, st, PERSON_MISSION);
        int r = 0;
        for (int i = 0; i < 50; i++) { /* after 50 failures the last r stays */
            r = (int)sim_random(s, 3);
            if (room_counts(s, h)[image_byte(s, MISSION_ROOMS, (uint32_t)r)]) break;
        }
        sl[SL_PARAM] = (uint8_t)image_byte(s, INJURY_PICK, 8u * (uint32_t)r + sim_bits(s, 7));
        sl[SL_COUNT] = 30;
    } else {
        start(sl, st, STAFF_MISSION);
        sl[SL_PARAM] = (uint8_t)sim_random(s, 4);
        sl[SL_COUNT] = 50;
    }
    if (!player) return; /* AI sends ([0x5ABD] set): no message and no sound */
    sound_list(s->sound, 0, 0xA881); /* 0x48455, 0x4845F: with the player's message 911/912 */
    sound_list(s->sound, 1, 0xA8B1);
}

/* 0x4AE32, in 16 bits; the two "mod 256" terms are 8-bit differences */
int sim_thug_strength(Sim *s, const uint8_t *st, const uint8_t *sl) {
    int v = 16 * (st[12] + st[13]) + st[15] + 2 * st[5] + (uint8_t)(st[9] - 140) + (uint8_t)(st[10] - 45) +
            (uint16_t)get16(st + 16) - 16 * sl[SL_HOURS] - (int)sim_bits(s, 63);
    if (st[3]) v += 50;
    return (int16_t)v;
}

static void idle(uint8_t *sl, uint8_t *st) {
    sl[SL_STATE] = sl[SL_ROOM] = st[T_STATE] = st[T_ROOM] = sl[SL_HOURS] = 0;
}

static void custody(Sim *s, uint8_t *sl, uint8_t *st) {
    sl[SL_STATE] = st[T_STATE] = CUSTODY;
    sl[SL_ROOM] = st[T_ROOM] = 0;
    sl[SL_CUSTODY] = (uint8_t)(20 + sim_bits(s, 15));
    st[T_MOOD] = 0;
    sl[SL_HOURS] = 0;
}

static void home(uint8_t *p) {
    p[3] = p[4] = 0xFF;
    p[66] = 0;
}

static void person_mission(Sim *s, int h, uint8_t *sl, uint8_t *st) {
    int victim = (uint16_t)get16(sl + SL_VICTIM), thug = get16(sl + SL_INDEX);
    if (!victim) {
        if (sim_bits(s, 127) != 13) return;
        int i = (int)sim_random(s, 1008) + 1;
        uint8_t *p = person_rec(s, i);
        if (p[104] || !p[1] || p[79] || p[41] || (int8_t)p[3] >= 0 || p[57] || p[105]) return;
        p[3] = (uint8_t)h, p[4] = 2, p[66] = 1;
        p[44] = (uint8_t)(70 + sim_bits(s, 15));
        const uint32_t e = INJURIES + 4u * sl[SL_PARAM];
        p[56] = (uint8_t)image_byte(s, e, 0), p[80] = (uint8_t)image_byte(s, e + 1, 0), p[41] = (uint8_t)image_byte(s, e + 2, 0);
        for (int k = 48; k <= 55; k++) p[k] = 0;
        put16(sl + SL_VICTIM, i);
        add16(st + 16, 2);
        redraw(s, h);
        return;
    }
    if (sl[SL_COUNT]) {
        sl[SL_COUNT]--;
        if (sl[SL_COUNT]) {
            if (sl[SL_COUNT] == 20 || sl[SL_COUNT] == 10) redraw(s, h);
            return;
        }
    }
    if (!room_counts(s, h)[2]) return; /* no reception: the thug waits with the victim */
    uint8_t *rec = sim_room_record(s, h, 2, 0), *p = person_rec(s, victim);
    if (!get16(rec + 8)) { /* no receptionist: the victim goes home, no news */
        home(p);
        idle(sl, st);
        add16(st + 16, 1);
        redraw(s, h);
        put16(sl + SL_VICTIM, 0);
        return;
    }
    int place = -1;
    for (int k = 0; k < 4 && place < 0; k++)
        if (!get16(rec + 0x0A + 4 * k)) place = k;
    if (place < 0) return; /* tried again next minute */
    int strength = sim_thug_strength(s, st, sl);
    int resist = (int16_t)(p[5] + (uint8_t)(p[9] - 130) + (uint8_t)(p[10] - 30) + 64 * p[2] - (int)sim_bits(s, 63));
    if (strength >= resist) {
        sim_news_thug(s, 6, 0, h, 0, victim, thug, sl[SL_PARAM]); /* 0x4C2AE */
        put16(rec + 0x0A + 4 * place, victim);
        put16(rec + 0x0C + 4 * place, 0);
        idle(sl, st);
        add16(st + 16, 3);
        redraw(s, h);
        add16(hospital(s, h) + 0xE6, 1);
        p[66] = 0;
        put16(sl + SL_VICTIM, 0);
    } else {
        sim_news_thug(s, 0x0B, 0, h, 0, victim, thug, sl[SL_PARAM]); /* 0x4C244 */
        home(p);
        custody(s, sl, st);
        sl[SL_OFF_DUTY] = 0;
        redraw(s, h);
        put16(sl + SL_VICTIM, 0);
        add16(hospital(s, h) + 0xF0, 1);
    }
}

static void staff_mission(Sim *s, int h, uint8_t *sl, uint8_t *st) {
    int victim = (uint16_t)get16(sl + SL_VICTIM), thug = get16(sl + SL_INDEX);
    if (!victim) {
        if (sim_bits(s, 31) != 7) return;
        int i = (int)sim_random(s, STAFF_LAST) + 1;
        uint8_t *v = staff_rec(s, i);
        int vh = (int8_t)v[3];
        if (vh < 0 || vh == h || vh > 16 || cash_of(s, vh) < 0 || v[T_STATE] != OFF_DUTY || v[1] == THUGS ||
            (v[T_MARK] >= 0xF9 && v[T_MARK] <= 0xFE))
            return;
        v[T_MARK] = 0xFE;
        v[T_ROOM] = sl[SL_PARAM];
        put16(sl + SL_VICTIM, i);
        redraw(s, h);
        add16(st + 16, 2);
        return;
    }
    uint8_t *v = staff_rec(s, victim);
    if (sl[SL_COUNT]) sl[SL_COUNT]--;
    int c = sl[SL_COUNT];
    if (!c) { /* the beating */
        sim_news_thug(s, 6, 1, h, (int8_t)v[3], victim, thug, sl[SL_PARAM]); /* 0x4C165 */
        v[T_MOOD] = 0;
        v[T_MARK] = 0xF9;
        idle(sl, st);
        add16(st + 16, 3);
        redraw(s, h);
        add16(hospital(s, h) + 0xE8, 1);
        put16(sl + SL_VICTIM, 0);
    } else if (c == 40) {
        v[T_MARK] = 0xFD, redraw(s, h);
    } else if (c == 30) { /* the fight; the draws in the spec's order */
        int a = sim_thug_strength(s, st, sl);
        a += (int)sim_bits(s, 127);
        a += (int)sim_bits(s, 127);
        int d = 16 * v[13] + v[15] + (uint8_t)(v[9] - 150) + (uint8_t)(v[10] - 50) + (uint16_t)get16(v + 16);
        d += (int)sim_bits(s, 63);
        d += (int)sim_bits(s, 31);
        a += (int)sim_bits(s, 63);
        if ((int16_t)a < (int16_t)d) {
            sim_news_thug(s, 0x0B, 1, h, (int8_t)v[3], victim, thug, sl[SL_PARAM]); /* 0x4C1BF */
            v[T_MARK] = 0;
            custody(s, sl, st); /* S +0x0B is not cleared here */
            add16(v + 16, 3);
            redraw(s, h);
            put16(sl + SL_VICTIM, 0);
            add16(hospital(s, h) + 0xF2, 1);
        } else {
            v[T_MARK] = 0xFC, redraw(s, h);
        }
    } else if (c == 20) {
        v[T_MARK] = 0xFB, redraw(s, h);
    } else if (c == 10) {
        v[T_MARK] = 0xFA, redraw(s, h);
    }
}

static void missions(Sim *s) { /* 0x4A822 */
    for (int h = 0; h < 16; h++) {
        uint8_t *sl = staff_slots(s, h);
        for (int i = 0; i < STAFF_SLOTS; i++, sl += STAFF_SLOT_SIZE) {
            int index = get16(sl + SL_INDEX);
            if (index <= 0 || index > STAFF_LAST) continue;
            uint8_t *st = staff_rec(s, index);
            if (st[1] != THUGS) continue;
            if (sl[SL_STATE] == PERSON_MISSION) person_mission(s, h, sl, st);
            else if (sl[SL_STATE] == STAFF_MISSION) staff_mission(s, h, sl, st);
        }
    }
    rng_scratch_hospital16(s); /* [0x4528]: the loop's end pointer, every call (docs/rng-sites.md 2.4) */
}

int sim_thugs_run(Sim *s, uint32_t routine) {
    if (routine != 0x4A822) return 0;
    missions(s);
    return 1;
}
