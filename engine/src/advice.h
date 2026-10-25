/* The work advice (docs/ui-reports.md, section 2): the rule walk 0x5B47B-0x625AC with its four entries, the beginner
 * rules 0x5B6A5, the shared end 0x616D9 and the scheduled call 0x27495. Screen-free: the walk works on the world (the
 * player block pb, hospital record 0, the room records) and returns the advice number and the board's text as pieces;
 * the board, screen 2, and the jump table of its thumbs-up are in reports_ui.c. The walk's writes into room and
 * hospital records stay, also from the scheduled run (docs/ui-reports.md, "State written by the walk"). */
#ifndef BIING_ADVICE_H
#define BIING_ADVICE_H

#include "pieces.h"
#include "sim.h"

enum { ADVICE_SCHEDULED, ADVICE_STRIP, ADVICE_RECEPTION, ADVICE_CHIEF };
enum { /* player block fields */
    AP_SAVED0 = 0, AP_WORD2 = 2, AP_ROOM = 0x10, AP_CHIEF = 0x4F, AP_CAPABILITY = 0x54, AP_KIND = 0x55, AP_FLAG5A = 0x5A,
    AP_TOLD_TREATMENT = 0x57, AP_TOLD_DENTAL = 0x58, AP_TOLD_NEUROLOGY = 0x59, AP_TOLD_BANK = 0x94, AP_TOLD_PATHOLOGY = 0x95,
    AP_TOLD_WARD = 0x96, AP_TOLD_NEURO_WARD = 0x97, AP_TOLD_ICU = 0x98, AP_TOLD_OP = 0x99, AP_BACKGROUND = 0xA1,
    AP_ADVICE = 0xA2, AP_PLACE = 0xA4, AP_INSTANCE = 0xA6,
};
enum { ADVICE_NONE = -1 };

typedef struct {
    int advice; /* pb +0xA2 as a signed number: -1 = none */
    int back; /* [0x5C85]: where the thumbs-down returns (0 office, 1 reception, 2 site map), -1 = not set */
    Pieces text; /* the message buffer */
} Advice;

/* One entry of the walk (0x5B47B, 0x5B4D1, 0x5B5D6, 0x5B640), without drawing. Returns the advice number. The
 * scheduled entry returns at once (ADVICE_NONE, nothing changed) while an advice is pending ([0x5C84]); the strip's
 * screen tidy-up (screens 119 and 122) is the caller's. */
int advice_walk(Sim *s, int entry, Advice *out);
/* 0x27495: skipped while fast-forwarding; the walk with the first 200 bytes of pb saved and restored. */
void advice_scheduled(Sim *s);
/* 0x1B2DC / 0x1B262 / 0x1B200: the free places of a category into the scratch word b8 ([0x45B8]); rooms = the
 * category's count, sum = its free places. Without rooms the word keeps its stale value (FIX-6 on: 0). */
int advice_free_places(int *b8, int rooms, int sum);
/* Organ article of text 2782's tail: text 1611 + the organ's gender byte (0x4713 + 2 id) */
int advice_organ_article(Sim *s, int organ);

#endif
