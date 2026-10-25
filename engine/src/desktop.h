/* The rest of the desktop (docs/desktop.md, docs/ui-garage.md "The rest of the desktop"): the counts of the patient
 * report 146 and the holdings report 153, and the high-score entry 0x5A717 with the table GAMES\QUACK
 * (docs/saves.md, "High scores"). Screen-free, so the tests run them directly; the screens are in desktop_ui.c. */
#ifndef BIING_DESKTOP_H
#define BIING_DESKTOP_H

#include "sim.h"

/* ---- 146 Patientengesamtübersicht ------------------------------------------------------------------------ */

typedef struct {
    int ill, teeth, mind, other, in_hospital, inpatients, at_home, looking, dead;
} PatientCounts;
void desktop_patient_counts(Sim *s, PatientCounts *c);

/* ---- 153 Firmenbesitz: per room category 2-18 the room slots in use, the rented and the owned ones ---------- */

void desktop_holdings(Sim *s, int h, int total[19], int rented[19], int owned[19]);
void desktop_holdings_step(Sim *s, int step); /* 0x3D7F6 / 0x3D80E: pb+0xE + step, 0-15 with wrap-around */

/* ---- the high-score list ------------------------------------------------------------------------------ */

#define HISCORE_ENTRIES 20
#define HISCORE_ENTRY 28
#define HISCORE_SIZE (HISCORE_ENTRIES * HISCORE_ENTRY)
#define QUACK_SIZE (HISCORE_SIZE + 2)
#define HISCORE_NAME 20
#define HISCORE_MAX_NAME 18
#define HISCORE_DEFAULT 0x5D8F /* the built-in table in the program image */

/* the rank bonus of step 4 (0 in the first three days of January) */
int hiscore_bonus(int rank, int days, int month, int day);
/* step 5: when score > the last entry's (signed), the new entry (name 20 x '$') is sorted in; its index, or -1 */
int hiscore_insert(uint8_t table[HISCORE_SIZE], int32_t score, int days, int rank);
/* 0x5A717 steps 2-5 on the world: score, ranking, bonus, insert; *rank_out = R. The index of the new entry or -1. */
int hiscore_enter(Sim *s, uint8_t table[HISCORE_SIZE], int *rank_out, int32_t *score_out);
/* the file: stored = plain + key, key += plain (key 0x0B); then the checksum (sum of the stored bytes) >> 1 */
void quack_encode(const uint8_t plain[HISCORE_SIZE], uint8_t out[QUACK_SIZE]);
int quack_decode(const uint8_t *in, size_t n, uint8_t plain[HISCORE_SIZE]); /* 0, -1 size, -2 checksum */
/* 0x6F2CD: the file if it exists (1), else the built-in table (0); -1 a damaged file (the game ends at once) */
int quack_load(const char *path, const Program *p, uint8_t table[HISCORE_SIZE]);
int quack_save(const char *path, const uint8_t table[HISCORE_SIZE]); /* 0x6F264 */
/* the text input field (docs/gui.md, "Text input field"): the name buffer starts as '|' + 19 '$' and the character
 * count ([0xB0C2]) at 0; one key per frame (a DOS key code 0-255, -1 none); every code but 0, 0x15, '$', '@', 8 and
 * 13 is a character, control codes and '|' included; Backspace leaves 0 at the old cursor place; 1 when Enter ended
 * the input (the count is then -1) */
void hiscore_name_start(uint8_t name[HISCORE_NAME], int *count);
int hiscore_name_key(uint8_t name[HISCORE_NAME], int *count, int key);
/* The DOS key codes (function 0x06, US layout, docs/gui.md "Keyboard") of one key press, for the keys that give no
 * typed text: key = a lower-case letter, a digit or '[' '\\' ']' '-' (with Ctrl or Alt), or one of DK_*; ctrl, alt,
 * shift = the modifiers held. Writes 0-2 codes to out (an extended key: 0, then its scan code, for two successive
 * frames) and returns how many. */
enum { DK_ENTER = 256, DK_BACKSPACE, DK_ESCAPE, DK_TAB, DK_HOME, DK_UP, DK_PAGEUP, DK_LEFT, DK_RIGHT, DK_END, DK_DOWN,
       DK_PAGEDOWN, DK_INSERT, DK_DELETE, DK_F1 /* ... DK_F1 + 9 = F10 */ };
int dos_key_codes(int key, int ctrl, int alt, int shift, int out[2]);

#endif
