/* The daily report (docs/ui-reports.md, section 1): the day change 0x3DCCB that arms it, the baseline writer
 * 0x1A5B0 whose differences it shows, and the four paragraphs with their points and faces. Screen-free like bank.c:
 * the rules work on the simulation's world (player block, hospital record), the screen 100 is drawn in reports_ui.c.
 * The copy-protection parts of the day change (the query counter [0x697F] and pb dword +0xBA) are left out
 * (docs/mainloop.md, section 7). */
#ifndef BIING_REPORT_H
#define BIING_REPORT_H

#include "pieces.h"
#include "sim.h"

enum { /* player block fields */
    RP_REDRAW = 0x33, RP_ARMED = 0x6D, RP_WORTH_BASE = 0x6E, RP_DISCHARGED_BASE = 0x72, RP_CURED_BASE = 0x74,
    RP_UNCURED_BASE = 0x76, RP_WORTH = 0x78, RP_DISCHARGED = 0x7C, RP_CURED = 0x7E, RP_UNCURED = 0x80,
    RP_SCORE = 0x84, RP_SCORE_CHANGE = 0x88, RP_RANK = 0x8C, RP_RANK_CHANGE = 0x90, RP_DEATHS_BASE = 0xA8,
    RP_DEATHS = 0xAA,
};
enum { H_DAYS_PLAYED = 0x162 };

uint8_t *report_pb(Sim *s, int player); /* save block 28 (player 0) or 29 (player 1) */
void report_baseline(Sim *s, int player); /* 0x1A5B0 for player p (hospital record p) */
void report_day_change(Sim *s); /* 0x3DCCB for players 0 and 1 */
/* The new game's start-up steps after the initial hospital values (docs/mainloop.md section 1, "What the engine must
 * change", SAV-1): the baseline 0x1A5B0 twice, both times for player 0 (so every change field is 0), then the
 * expected cash 0x1A941 of hospitals 0-15. */
void report_new_game(Sim *s);

typedef struct {
    Pieces heading;
    Pieces para[4];
    int shown[4]; /* paragraph 3 is left out when the nurses' average bust is 0 */
    int points[4], face[4];
    int total, overall; /* the overall face */
    int sound; /* the sound pair: 0 (total >= 16), 1 (8-15), 2 (below 8) */
} Report;

/* 0x68A45 without its drawing: clears pb +0x6D, recomputes the staff averages (0x629E7) and the opinion sums
 * (0x1A684, which overwrites the score in +0x44 of every hospital) as the original does, and fills r. The
 * reshuffle of the applicant list (0x1A384) and the unused daily costs are the screen's part. */
void report_build(Sim *s, Report *r);

#endif
