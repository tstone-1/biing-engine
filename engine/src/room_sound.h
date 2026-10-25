/* The rooms' sound requests (docs/sound.md, "Room entries" and "Room frames"; the record words are those of
 * docs/ui-sitemap.md, "Clicking a building", column "Sound request", and the room docs). Screen-free: they read the
 * player's room records through the simulation and write to its sound driver (s->sound; NULL = nothing). */
#ifndef BIING_ROOM_SOUND_H
#define BIING_ROOM_SOUND_H

#include "sim.h"

/* The background request of a room's view routine, by category (2 reception ... 18 pathology; 0 = the office loop of
 * administration, personnel office and estate agent). Reads the shown room (pb+0x10) or, for the single rooms, the
 * category's record 0. */
void room_sound_view(Sim *s, int cat);
/* Quiet night (0x31B78; with8 = 0: 0x31B7F, which skips the word +0x08): the record's words +0x08, +0x0C, +0x0A all
 * 0 and the hour 20 or later or before 3: 0xAB / 0x140 / 7. Returns 1 if it requested. */
int room_sound_quiet_night(Sim *s, const uint8_t *rec, int with8);
/* 0x32E43: the waiting room's level 13 + 4 x (seats taken - 1), 13-63, not while 0xAB is requested or playing. As the
 * frame's "sound level again" it only moves the target once 0xF2 plays (engine reading, docs/engine.md). */
void room_sound_waiting_level(Sim *s, int frame);
/* 0x2F9A6's sound when a nurse show starts in a room of category cat (rec = its record; was = the show counter before
 * the start): unless a show was already running. Without a patient (the words of the room docs: reception +0x0A,
 * waiting room and wards +0x0C, dental +0x0C + 0x0A, intensive care +0x0C, OP +0x0A + 6 + 0x0C) the second entries
 * are cleared. */
void room_sound_show(Sim *s, int cat, const uint8_t *rec, int was);
/* The ambience of a room frame by its routine (docs/sound.md, "Room frames"): the gate's draws and the picks.
 * Returns 0 for a routine without one. */
int room_sound_frame(Sim *s, uint32_t frame);

#endif
