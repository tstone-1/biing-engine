/* Module D, from docs/sim-player.md: what the player's own staff do without a click. The ten per-minute routines
 * 0x49C65 (reception), 0x4A6C8 (waiting rooms), 0x552E8 (treatment rooms), 0x4A437 (dental ward), 0x54EF2
 * (neurology), 0x55580 / 0x55692 (Krankenzimmer / Neuro-Krankenzimmer), 0x557A4 (intensive care), 0x55B17 (OP) and
 * 0x55D72 (torture chamber), with the helpers 0x55205 (may staff go off duty on this screen) and 0x551AB (off duty,
 * remembering the room). Always hospital 0; only staff with the "works independently" flag (staff byte 11) act. The
 * actions are the AI-mode branches of the shared room routines (sim_rooms.c). Finished treatments, ward discharges,
 * the move out of intensive care and finished operations wait for the player. The unreachable manual-query block
 * inside 0x54EF2 (docs/sim-player.md, "Copy protection") is not part of the engine. */
#include <string.h>

#include "sim_internal.h"

#define PERSON_SIZE 106
enum { P_STATE = 4, P_IN_STEP = 93, P_ILLNESS = 56, P_FIELD100 = 100, P_DEAD = 104 };
enum { S_INDEPENDENT = 11, S_STATE = 4, S_ROOM = 26 };
enum { OFF_DUTY = 0x13 };
/* player block 0x631B (docs/ui-rooms.md, "Player-block fields") */
enum { PB_QUEUE = 0x0A, PB_WARD = 0x0C, PB_ROOM = 0x10, PB_CALL = 0x12, PB_CALL_ROOM = 0x14, PB_SHEET = 0x2C,
       PB_TREAT_NOTE = 0x32, PB_DENTAL_NOTE = 0x3A, PB_NEURO_NOTE = 0x41, PB_WAITING = 0x44, PB_DECISION = 0x5B,
       PB_TREATMENT = 0x5C, PB_DENTAL = 0x5D, PB_NEUROLOGY = 0x5E, PB_MOVED = 0x5F, PB_NEURO_MOVED = 0x61,
       PB_WARD_NEWS = 0x62, PB_OP = 0x65 };
enum { SCR_RECEPTION = 19, SCR_COUCH = 61, SCR_NEURO_SHEET = 75, SCR_ORGAN_OFFER = 150 };
#define IMG_RECEPTION_SHOW 519

static uint8_t *pb(Sim *s) { return s->world->block[BLK_PLAYER0]; }
static uint8_t *person(Sim *s, int i) { return s->world->block[BLK_PATIENTS] + i * PERSON_SIZE; }
static uint8_t *staff(Sim *s, int i) { return s->world->block[BLK_STAFF] + i * 32; }
static int independent(Sim *s, const uint8_t *field) {
    int i = (uint16_t)get16(field);
    return i && staff(s, i)[S_INDEPENDENT];
}
static uint8_t *room(Sim *s, int cat, int i) { return sim_room_record(s, 0, cat, i); }
static int show(Sim *s) { return (int)sim_bits(s, 15) + 22; } /* 0x2B4A9 */

/* ---- the frame of every routine ------------------------------------------------------------------------------ */

typedef struct {
    uint8_t queue[4], room[2], call_room[2], sheet[2];
    int r;
} Frame;

/* Steps 1-2: r1 (the player choice that 0x42AAC overrides: always hospital 0), the saves, the human test (the word
 * at +0x14 for the neurology), the site byte, the screen that stops the routine before the room choice (the OP's
 * 150, -1 = none), then r = random(site byte) into pb+0x10 / pb+0x14. 0: nothing more, but restore. */
static int frame_begin(Sim *s, Frame *f, int cat, int word, int stop_screen) {
    uint8_t *q = pb(s);
    sim_bits(s, 1);
    memcpy(f->queue, q + PB_QUEUE, 4);
    memcpy(f->room, q + PB_ROOM, 2);
    memcpy(f->call_room, q + PB_CALL_ROOM, 2);
    memcpy(f->sheet, q + PB_SHEET, 2);
    const uint8_t *h = hospital(s, 0);
    if (!(word ? get16(h + 0x14) : h[0x14])) return 0;
    if (!room_counts(s, 0)[cat]) return 0;
    if (s->screen == stop_screen) return 0;
    f->r = cat == 2 ? 0 : (int)sim_random(s, (uint32_t)sim_rooms_in_use(s, 0, cat));
    put16(q + PB_ROOM, f->r);
    put16(q + PB_CALL_ROOM, f->r);
    return 1;
}
static void frame_end(Sim *s, const Frame *f) { /* step 4; pb+0x12 is not restored (the neurology leaves 15) */
    uint8_t *q = pb(s);
    memcpy(q + PB_QUEUE, f->queue, 4);
    memcpy(q + PB_SHEET, f->sheet, 2);
    memcpy(q + PB_CALL_ROOM, f->call_room, 2);
    memcpy(q + PB_ROOM, f->room, 2);
}

/* ---- 0x55205, 0x551AB and the evening release ------------------------------------------------------------------ */

int sim_off_duty_screen(int screen) {
    static const uint8_t SCREENS[] = {1,  5,  6,  7,  8,   9,   10,  18,  19,  22,  25,  38,  39,  40, 42,
                                      52, 65, 97, 112, 113, 114, 115, 118, 128, 135, 145, 146, 148, 153};
    for (size_t i = 0; i < sizeof SCREENS; i++)
        if (screen == SCREENS[i]) return 1;
    return 0;
}

/* 0x551AB: the employee goes off duty and remembers the room in slot +16 / +17, so that the off-duty return of
 * 0x2FC77 brings an independent employee back into the same field. With no slot the original loops for ever; the
 * engine does nothing (guard). */
static void off_duty(Sim *s, int index) {
    uint8_t *sl = staff_slots(s, 0);
    for (int i = 0; i < STAFF_SLOTS; i++, sl += STAFF_SLOT_SIZE) {
        if ((uint16_t)get16(sl) != index) continue;
        sl[7] = 0;
        put16(sl + 10, 0);
        sl[16] = sl[3];
        sl[17] = sl[4];
        sl[3] = OFF_DUTY;
        staff(s, index)[S_STATE] = OFF_DUTY;
        sl[4] = 0;
        staff(s, index)[S_ROOM] = 0;
        return;
    }
}

/* The evening release's gates: 0x55205, nobody waiting, the reception unstaffed, and 18:45 or later (late: 19:50 or
 * later); the hour only bounds from below, so after midnight nobody goes until the next evening. */
static int evening(Sim *s, int waiting, int late) {
    if (!sim_off_duty_screen(s->screen) || waiting) return 0;
    if (get16(room(s, 2, 0) + 8)) return 0;
    int hour = world_tail(s->world, TAIL_HOUR), minute = world_tail(s->world, TAIL_MINUTE);
    return late ? hour >= 20 || (hour == 19 && minute >= 50) : hour >= 19 || (hour == 18 && minute >= 45);
}

/* one staff field: an independent employee is taken out of it (with the comfort counter that goes with a nurse) */
static void release(Sim *s, uint8_t *field, uint8_t *comfort) {
    int who = (uint16_t)get16(field);
    if (!who || !staff(s, who)[S_INDEPENDENT]) return;
    put16(field, 0);
    if (comfort) *comfort = 0;
    off_duty(s, who);
}

/* ---- 0x49C65: reception ------------------------------------------------------------------------------------- */

static void reception(Sim *s) {
    Frame f;
    if (frame_begin(s, &f, 2, 0, -1)) {
        uint8_t *r = room(s, 2, 0), *q = pb(s);
        if (!independent(s, r + 8)) goto end;
        if (!r[0x45] && sim_bits(s, 7) == 2) { /* the show */
            r[0x45] = (uint8_t)show(s);
            if (s->screen == SCR_RECEPTION) s->player_image = IMG_RECEPTION_SHOW;
        }
        int index = (uint16_t)get16(r + 10);
        if (index && sim_bits(s, 3)) { /* the patient at the desk: 0x2B989 in AI mode */
            if (person(s, index)[P_DEAD]) goto end;
            q[PB_DECISION] = 0xFF;
            int w = 0, moved = 0;
            switch (sim_player_seat(s, 0, &w)) {
            case SEAT_DONE: moved = 1; break;
            case SEAT_NO_FORM: r[7] = 11; break; /* the supply request; only the AI's operator serves it */
            case SEAT_NO_PEN: r[7] = 3; break;
            case SEAT_NO_ROOMS: /* the original's missing AI-mode test: thrown out, with the player's box (reproduce) */
                sim_player_leave(s, 0);
                s->player_box = index;
                break;
            default: break; /* 199 tries without a free seat: nothing */
            }
            q[PB_WAITING] = moved ? 0xFF : 0;
            goto end;
        }
        /* the Krankenzimmer transfer: the last taken place of the bed queue into a random ward */
        if (!room_counts(s, 0)[6]) goto end;
        int ward = 0;
        if (!sim_free_beds(s, 0, 6, &ward, NULL)) goto end;
        put16(q + PB_WARD, ward);
        int place = -1;
        for (int i = 0; i < 6; i++)
            if (get16(r + 0x46 + 4 * i)) place = i;
        if (place < 0) goto end;
        put16(q + PB_QUEUE, 0x46 + 4 * place);
        ward = (int)sim_random(s, (uint32_t)sim_rooms_in_use(s, 0, 6));
        put16(q + PB_WARD, ward);
        sim_player_bed(s, 0, ward, 0x46 + 4 * place, 1); /* 0x2336B in AI mode: a full ward leaves the patient queued */
        q[PB_WARD_NEWS] = 0xFF;
    }
end:
    frame_end(s, &f);
}

/* ---- 0x4A6C8: waiting rooms ---------------------------------------------------------------------------------- */

static void waiting_room(Sim *s) {
    Frame f;
    if (frame_begin(s, &f, 3, 0, -1)) {
        uint8_t *w = room(s, 3, f.r);
        if (independent(s, w + 8)) {
            if (evening(s, w[12], 0)) {
                release(s, w + 8, w + 0x0F);
            } else if (!w[0x0F] && sim_bits(s, 7) == 0) {
                w[0x0F] = (uint8_t)show(s);
                pb(s)[PB_WAITING] = 0xFF;
            }
        }
    }
    frame_end(s, &f);
}

/* ---- 0x552E8, 0x4A437, 0x54EF2: treatment room, dental ward, neurology ----------------------------------------- */

/* the decision of the doctor (+0x0A) right after the diagnosis; with [0x5ABE] (treatment room, neurology) the act
 * sets the "moved" flag when the player is viewing that patient on its sheet (screen 61 / 75; engine reading: the
 * act took the patient out of the room and it is the sheet's patient pb+0x2C) */
static void decide(Sim *s, int cat, int r, uint8_t *t, int index) {
    uint8_t *q = pb(s);
    q[cat == 5 ? PB_TREATMENT : PB_DENTAL] = 0xFF; /* the neurology writes the dental flag (reproduce) */
    sim_ai_choose(s, 0, cat, r);
    sim_ai_act(s, 0, cat, r);
    if (cat != 11 && (uint16_t)get16(t + 12) != index && (uint16_t)get16(q + PB_SHEET) == index &&
        s->screen == (cat == 5 ? SCR_COUCH : SCR_NEURO_SHEET))
        q[cat == 5 ? PB_MOVED : PB_NEURO_MOVED] = 0xFF;
    q[cat == 5 ? PB_TREAT_NOTE : cat == 11 ? PB_DENTAL_NOTE : PB_NEURO_NOTE] = 0xFF;
}

static void clinic_room(Sim *s, int cat) {
    Frame f;
    if (frame_begin(s, &f, cat, cat == 15, -1)) {
        uint8_t *t = room(s, cat, f.r), *q = pb(s);
        if (cat == 11 && independent(s, t + 8) && sim_bits(s, 7) == 0) { /* no test of +0x19 (reproduce) */
            t[0x19] = (uint8_t)show(s);
            q[PB_DENTAL] = 0xFF;
        }
        int index = (uint16_t)get16(t + 12);
        if (!index) {
            int waiting = sim_waiting_for(s, 0, cat);
            if (evening(s, waiting, 0)) {
                release(s, t + 8, cat == 11 ? t + 0x19 : NULL);
                release(s, t + 0x0A, NULL);
            } else if (get16(t + 0x0A) && get16(t + 8) && independent(s, t + 8) && waiting) {
                q[PB_WAITING] = 0xFF;
                q[cat == 5 ? PB_TREATMENT : cat == 11 ? PB_DENTAL : PB_NEUROLOGY] = 0xFF;
                if (cat == 15) put16(q + PB_CALL, 15); /* not restored */
                sim_ai_bring(s, 0, cat, f.r);
            }
        } else if (!person(s, index)[P_DEAD] && independent(s, t + 0x0A) && !get16(t + 0x0E) && !get16(t + 0x10)) {
            decide(s, cat, f.r, t, index);
        }
        /* a finished treatment (+0x10 set, +0x12 = 0) waits for the player's bill: the original's code for it is
         * unreachable (docs/sim-player.md) */
    }
    frame_end(s, &f);
}

/* ---- 0x55580 / 0x55692: Krankenzimmer and Neuro-Krankenzimmer; 0x557A4: intensive care ------------------------ */

static void ward(Sim *s, int cat) {
    Frame f;
    if (frame_begin(s, &f, cat, 0, -1)) {
        uint8_t *k = room(s, cat, f.r);
        if (independent(s, k + 8)) {
            if (!k[0x0F] && sim_bits(s, 7) == 0) k[0x0F] = (uint8_t)show(s);
            for (int b = 0; b < 10; b++) { /* the first patient with a case and no step: the next bed step */
                int index = (uint16_t)get16(k + 0x14 + 4 * b);
                if (!index) continue;
                const uint8_t *p = person(s, index);
                if (p[P_DEAD] || p[P_STATE] != cat || p[P_IN_STEP] || !get16(p + P_ILLNESS)) continue;
                sim_player_step(s, index); /* 0x29FC8 / 0x4F16C */
                break;
            }
        }
    }
    frame_end(s, &f);
}

static void intensive_care(Sim *s) {
    Frame f;
    if (frame_begin(s, &f, 9, 0, -1)) {
        uint8_t *c = room(s, 9, f.r);
        if (independent(s, c + 8) && !c[0x0F] && sim_bits(s, 7) == 0) c[0x0F] = (uint8_t)show(s);
    }
    frame_end(s, &f);
}

/* ---- 0x55B17 / 0x55D72: OP and torture chamber ------------------------------------------------------------------ */

/* The fetch 0x4E6D1 / 0x4E978 is the engine's bring over hospital 0's own wards. Deviation (guarded, docs/engine.md):
 * the original's count-down loop tests for zero only after its first pass, so with no (neuro) ward it walks 65,535
 * ward records into the other hospitals' tables and takes one of their patients; the engine takes nobody then. */
static void theatre(Sim *s, int cat) {
    int op = cat == 7;
    Frame f;
    if (frame_begin(s, &f, cat, 0, op ? SCR_ORGAN_OFFER : -1)) {
        uint8_t *t = room(s, cat, f.r);
        if (!get16(t + 8)) goto end;
        if (op && independent(s, t + 8) && !t[0x17] && sim_bits(s, 7) == 0) t[0x17] = (uint8_t)show(s);
        if ((op && !get16(t + 6)) || !get16(t + 0x0A)) goto end;
        int index = (uint16_t)get16(t + 0x0C);
        if (!index) {
            if (independent(s, t + 8)) {
                pb(s)[PB_OP] = 0xFF; /* the torture fetch sets the OP's flag too (reproduce) */
                int from = 0;
                sim_player_theatre_fetch(s, 0, cat, f.r, &from);
            }
            if (!get16(t + 0x0C) && evening(s, sim_waiting_for(s, 0, op ? 5 : 15), 1) &&
                !(op && get16(s->world->block[BLK_34]))) {
                release(s, t + 8, op ? t + 0x17 : NULL);
                if (op) release(s, t + 6, NULL);
                release(s, t + 0x0A, NULL);
            }
            goto end;
        }
        const uint8_t *p = person(s, index);
        /* a finished operation (field 100 = 2) stays on the table for the player's "send back" */
        if (!p[P_DEAD] && independent(s, t + 0x0A) && get16(t + 0x0E) == 999 && p[P_FIELD100] != 2)
            sim_ai_theatre_start(s, 0, cat, f.r);
    }
end:
    frame_end(s, &f);
}

/* ---- dispatch ----------------------------------------------------------------------------------------------- */

int sim_player_run(Sim *s, uint32_t routine) {
    switch (routine) {
    case 0x49C65: reception(s); return 1;
    case 0x4A6C8: waiting_room(s); return 1;
    case 0x552E8: clinic_room(s, 5); return 1;
    case 0x4A437: clinic_room(s, 11); return 1;
    case 0x54EF2: clinic_room(s, 15); return 1;
    case 0x55580: ward(s, 6); return 1;
    case 0x55692: ward(s, 16); return 1;
    case 0x557A4: intensive_care(s); return 1;
    case 0x55B17: theatre(s, 7); return 1;
    case 0x55D72: theatre(s, 8); return 1;
    default: return 0;
    }
}
