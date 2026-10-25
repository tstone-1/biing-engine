/* The rooms' sound requests, from docs/sound.md ("Room entries", "Room frames") with the record words of
 * docs/ui-sitemap.md ("Clicking a building") and the room docs (ui-rooms.md, ui-departments.md, ui-support.md,
 * ui-garage.md, ui-bank.md, ui-purchasing.md, ui-estate.md, ui-personnel.md). */
#include "room_sound.h"

#include "rooms.h"
#include "sim_internal.h"
#include "sound.h"

static int u16(const uint8_t *p) { return (uint16_t)get16(p); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8) | (uint32_t)(p[2] | p[3] << 8) << 16; }
static int hour(Sim *s) { return world_tail(s->world, TAIL_HOUR); }
static void bg(Sim *s, int id, int period, int level) { sound_background(s->sound, id, period, level); }

/* the record a view reads: the shown room, or record 0 of a single room */
static uint8_t *shown(Sim *s, int cat) {
    int single = cat == 2 || cat == 4 || cat == 10 || cat == 12 || cat == 13 || cat == 14 || cat == 17 || cat == 18;
    return rooms_record(s, cat, single ? 0 : rooms_shown(s));
}

int room_sound_quiet_night(Sim *s, const uint8_t *rec, int with8) {
    if ((with8 && u16(rec + 8)) || u16(rec + 0x0C) || u16(rec + 0x0A)) return 0;
    int h = hour(s);
    if (h < 20 && h >= 3) return 0;
    bg(s, 0xAB, 0x140, 7);
    return 1;
}

void room_sound_waiting_level(Sim *s, int frame) {
    Sound *snd = s->sound;
    if (!snd || sound_background_requested_or_playing(snd, 0xAB)) return;
    /* the number waiting W+0x0C (0x32E43; docs/sound.md, SND-11: the engine counted the seat words before) */
    int level = 13 + 4 * (u16(shown(s, 3) + 0x0C) - 1);
    level = level < 13 ? 13 : level > 63 ? 63 : level;
    if (frame && snd->bg_playing == 0xF2 && !snd->bg_request) snd->bg_target = (uint16_t)level;
    else bg(s, 0xF2, 0x140, level);
}

void room_sound_view(Sim *s, int cat) {
    if (!s->sound) return;
    uint8_t *r = shown(s, cat);
    switch (cat) {
    case 0: bg(s, 0xFC, 0x140, 8); break; /* administration, personnel office, estate agent: the office loop */
    case 2: { /* reception 0x33E04, then its own quiet night: desk, the six bed-queue places, the clerk */
        bg(s, 0xF1, 0x140, 10);
        int anyone = u16(r + 0x0A) || u16(r + 0x08);
        for (int i = 0; i < 6; i++) anyone |= u16(r + 0x46 + 4 * i) != 0;
        int h = hour(s);
        if (!anyone && (h >= 20 || h < 3)) bg(s, 0xAB, 0x140, 7);
        break;
    }
    case 3: /* waiting room 0x32DC2; quiet night on +0x0C (waiting) and +0x08 (nurse) */
        room_sound_waiting_level(s, 0);
        if (!u16(r + 0x0C) && !u16(r + 0x08) && (hour(s) >= 20 || hour(s) < 3)) bg(s, 0xAB, 0x140, 7);
        break;
    case 4: u16(r + 4) ? bg(s, 0xF9, 0x140, 17) : bg(s, 0xAB, 0x140, 7); break; /* warehouse: the clerk */
    case 5: /* treatment room 0x31C79 */
        u16(r + 0x0C) ? bg(s, 0xF1, 0x140, 12) : bg(s, 0xD3, 0x140, 12);
        room_sound_quiet_night(s, r, 1);
        break;
    case 6: bg(s, 0xB2, 0x118, 50); break; /* ward; [0x5C8B] = 0x118 is never read */
    case 7: /* operating theatre: staff word +0x0C; quiet night when the surgeon (+6) is missing */
        u16(r + 0x0C) ? bg(s, 0x69, 0x140, 30) : bg(s, 0xD2, 0x140, 30);
        if (!u16(r + 6)) room_sound_quiet_night(s, r, 1);
        break;
    case 8: bg(s, 0x6A, 0x118, 49); break; /* torture chamber */
    case 9: /* intensive care: the heartbeat, then by nurse (+8) and patient (+0x0C) */
        bg(s, 0x58, 0x294, 63);
        if (!u16(r + 8)) u16(r + 0x0C) ? bg(s, 0x69, 0x140, 30) : bg(s, 0xAB, 0x140, 7);
        else if (!u16(r + 0x0C)) bg(s, 0xD3, 0x140, 12);
        break;
    case 11: /* dental station */
        u16(r + 0x0C) ? bg(s, 0xDA, 0x140, 34) : bg(s, 0xD3, 0x140, 12);
        room_sound_quiet_night(s, r, 1);
        break;
    case 15: /* neurology */
        u16(r + 0x0C) ? bg(s, 0xEC, 0x140, 30) : bg(s, 0xD3, 0x140, 12);
        room_sound_quiet_night(s, r, 1);
        break;
    case 16: bg(s, 0xB0, 0x140, 35); break; /* neuro ward */
    case 12: u16(r + 2) ? bg(s, 0xEA, 0x140, 40) : bg(s, 0xAB, 0x140, 7); break; /* golf: the attendant */
    case 13: /* kitchen: the cook +6, cooking +5 */
        if (!u16(r + 6)) bg(s, 0xAB, 0x140, 7);
        else bg(s, 0x68, 0xBE, r[5] ? 20 : 1);
        break;
    case 14: /* club room: dword +2, word +2 */
        if (!u32(r + 2)) bg(s, 0xAB, 0x140, 7);
        else bg(s, 0xBE, 0x12C, u16(r + 2) ? 50 : 10);
        break;
    case 18: /* pathology: the pathologist +0x0A */
        if (u16(r + 0x0A)) bg(s, 0xA2, 0x140, 60);
        else bg(s, 0xA2, 0x140, 12), room_sound_quiet_night(s, r, 0);
        break;
    case 17: bg(s, 0x5A, 0x294, 63); break; /* blood and organ bank */
    case 10: u32(r + 6) ? bg(s, 0xCE, 0x140, 36) : bg(s, 0xAB, 0x140, 7); break; /* garage: a car */
    default: break;
    }
}

void room_sound_show(Sim *s, int cat, const uint8_t *rec, int was) {
    if (!s->sound || was) return;
    int patient;
    switch (cat) {
    case 2: patient = u16(rec + 0x0A); break;
    case 11: patient = u16(rec + 0x0C) + u16(rec + 0x0A); break;
    case 7: patient = u16(rec + 0x0A) + u16(rec + 6) + u16(rec + 0x0C); break;
    default: patient = u16(rec + 0x0C); break; /* waiting room, ward, neuro ward, intensive care */
    }
    sound_show(s->sound, patient != 0);
}

/* ---- frames -------------------------------------------------------------------------------------------- */

static uint32_t bits(Sim *s, uint32_t mask) { return sim_bits(s, mask); }
enum { PATHOLOGY_D = 0x631B, PATHOLOGY_E = 0x2051A7 }; /* docs/sound.md, "The pathology's draws without a case" */
static uint32_t rnd(Sim *s, uint32_t n) { return sim_random(s, n); }

int room_sound_frame(Sim *s, uint32_t frame) {
    Sound *snd = s->sound;
    if (!snd) return 0;
    uint8_t *pb = rooms_pb(s);
    switch (frame) {
    case 0x1FEF4: { /* reception: every frame (the tick gates only the animations), anyone at the desk or in the bed
                     * queue, r15 = 0 (docs/sound.md, "Room frames") */
        uint8_t *r = shown(s, 2);
        int anyone = u16(r + 0x0A) != 0;
        for (int i = 0; i < 6; i++) anyone |= u16(r + 0x46 + 4 * i) != 0;
        if (anyone && bits(s, 15) == 0) {
            sound_show_music(snd, r[0x45]);
            sound_ambience_pair(snd, 0x4DDA);
        }
        return 1;
    }
    case 0x2007F: /* treatment room: a patient */
        if (u16(shown(s, 5) + 0x0C)) sound_ambience_pair(snd, 0xA6DA);
        return 1;
    case 0x23E02: { /* waiting room: the level again; random(10) < the number waiting */
        uint8_t *w = shown(s, 3);
        room_sound_waiting_level(s, 1);
        if ((int)rnd(s, 10) < u16(w + 0x0C)) {
            sound_show_music(snd, w[0x0F]);
            sound_ambience_pair(snd, 0x4E3A);
        }
        return 1;
    }
    case 0x23E5A: { /* dental station: a patient */
        uint8_t *r = shown(s, 11);
        if (u16(r + 0x0C)) sound_show_music(snd, r[0x19]), sound_ambience_pair(snd, 0xA67A);
        return 1;
    }
    case 0x2535A: /* administration */
        if (bits(s, 31) == 0) sound_ambience_pair(snd, 0x4F5A);
        return 1;
    case 0x371BB: /* personnel office: the t&31 draw */
        if (bits(s, 31) == 0) sound_ambience_pair(snd, 0x4F5A);
        return 1;
    case 0x3BE9E: /* estate agent */
        if (bits(s, 31) == 0) sound_ambience_pair(snd, 0xA3F6);
        return 1;
    case 0x25604: /* neurology: a patient and r3 = 0 */
        if (u16(shown(s, 15) + 0x0C) && bits(s, 3) == 0) {
            sound_ambience(snd, 0, 0xA5BA);
            sound_ambience(snd, 1, 0xA61A);
        }
        return 1;
    case 0x27588: case 0x275E0: { /* ward, neuro ward: random(10) < the patients (K+0xC) */
        uint8_t *k = shown(s, frame == 0x27588 ? 6 : 16);
        int patients = u16(k + 0x0C); /* stored count, not a bed recount; no draw when empty (SND-17) */
        if (patients && (int)rnd(s, 10) < patients) {
            sound_show_music(snd, k[0x0F]);
            sound_ambience_pair(snd, frame == 0x27588 ? 0x4E9A : 0x4EFA);
        }
        return 1;
    }
    case 0x4A392: { /* operating theatre: on the ambience tick with a patient */
        uint8_t *r = shown(s, 7);
        if (pb[RB_TICK] && u16(r + 0x0C)) sound_show_music(snd, r[0x17]), sound_ambience_pair(snd, 0x525A);
        return 1;
    }
    case 0x37153: /* torture chamber: a patient */
        if (u16(shown(s, 8) + 0x0C)) {
            sound_ambience(snd, 0, 0x519A);
            sound_ambience(snd, 1, 0x51FA);
        }
        return 1;
    case 0x5B3D0: { /* intensive care: the heartbeat's level once a minute, then r1 = 0 with a patient */
        uint8_t *r = shown(s, 9);
        int p = u16(r + 0x0C);
        if (pb[RB_MINUTE] && snd->bg_playing == 0x58 && !snd->bg_request && p) {
            if (rooms_person(s, p)[0x68]) snd->bg_target = 1; /* no draw */
            else { /* two single draws (SND-11): t AND 63 for the period word, which has no audible effect and is
                    * not written, then target 63 - (t AND 31) */
                (void)bits(s, 63);
                snd->bg_target = (uint16_t)(63 - (int)bits(s, 31));
            }
        }
        if (bits(s, 1) == 0 && p) sound_show_music(snd, r[0x0F]), sound_ambience_pair(snd, 0x549A);
        return 1;
    }
    case 0x370CD: { /* pathology: a pathologist and (a case on the table or r15 = 0) */
        uint8_t *r = shown(s, 18);
        if (!u16(r + 0x0A)) return 1;
        if (u16(r + 0x0C)) { /* a case: no gate draw, the picks as everywhere (279 / 280 starts, docs/sound.md) */
            sound_ambience_pair(snd, 0x543A);
            return 1;
        }
        /* no case, an uneven site (docs/sound.md, SND-11; docs/rng-sites.md): the gate's t AND 15 and the picks' t AND
         * 63 take the measured input pair D 0x631B, E 0x2051A7; engine reading: the record pick of a passed pick too.
         * Measured 2026-10-20: the pair on the case path too made 256 starts against the original's 279 there. */
        SimIn old = sim_set_inputs(s, sim_in(PATHOLOGY_D, PATHOLOGY_E, 0));
        if (bits(s, 15) == 0) sound_ambience_pair(snd, 0x543A);
        sim_set_inputs(s, old);
        return 1;
    }
    case 0x370A8: /* blood and organ bank: r7 = 0 */
        if (bits(s, 7) == 0) sound_ambience_pair(snd, 0x53DA);
        return 1;
    case 0x36FF7: /* garage: a car and r3 = 0 */
        if (u32(shown(s, 10) + 6) && bits(s, 3) == 0) sound_ambience_pair(snd, 0x507A);
        return 1;
    case 0x4A2AB: /* warehouse: a clerk, t AND 7 = 0 (random(8)) per queue (docs/sound.md, corrected 2026-10-19) */
        if (u16(shown(s, 4) + 4)) {
            if (bits(s, 7) == 0) sound_ambience(snd, 0, 0x531A);
            if (bits(s, 7) == 0) sound_ambience(snd, 1, 0x531A);
        }
        return 1;
    case 0x3710B: { /* kitchen: a cook; cooking: t&3 = 0, else two t&3 draws both 0 */
        uint8_t *r = shown(s, 13);
        if (!u16(r + 6)) return 1;
        int go = bits(s, 3) == 0; /* both draws are made, as two t&3 tests */
        if (!r[5]) go = (bits(s, 3) == 0) && go;
        if (go) sound_ambience_pair(snd, 0x537A);
        return 1;
    }
    case 0x3702D: { /* club room: +2 and +4, t&3 = 0 */
        uint8_t *r = shown(s, 14);
        if (u16(r + 2) && u16(r + 4) && bits(s, 3) == 0) sound_ambience_pair(snd, 0x52BA);
        return 1;
    }
    case 0x3706B: { /* golf room: an attendant (+2) and golfers (+5), t&3 = 0 */
        uint8_t *r = shown(s, 12);
        if (u16(r + 2) && r[5] && bits(s, 3) == 0) sound_ambience_pair(snd, 0x501A);
        return 1;
    }
    case 0x3717D: sound_ambience_pair(snd, 0x50DA); return 1; /* club close-up: every iteration */
    case 0x5AABB: { /* race screen: by the race's phase RB+2 and result RB+6 */
        const uint8_t *rb = s->world->block[BLK_34];
        if (!u16(rb)) return 1;
        uint32_t table = get16(rb + 2) < 29 ? 0xA49A : rb[6] ? 0xA55A : 0xA4FA;
        sound_ambience_pair(snd, table);
        return 1;
    }
    default: return 0;
    }
}
