/* The computed text lines of two sheets, from docs/ui-sheets.md: page 1 of the patient sheet (0x43547, screens 43,
 * 57, 59, 61, 62, 70, 73, 75, 84, 90, 109, 126; the treatment line with its drug part and dosage, the step tables'
 * activity and mood lines, the instrument names, the missing-equipment lines, the OP / torture / pathology stage
 * texts) and the activity line of the personnel sheet (screen 33, 0x652B7). The lines are assembled in a message
 * buffer and handed back as strings; rooms_ui.c and personnel_ui.c draw them. No SDL, so the tests compare strings.
 *
 * The step tables (0x6A71 / 0x875B activity, 0x6C2D / 0x8917 mood) and the other strings are read from the user's
 * BIPRO.EXE as data; their "computed" entries are replaced by the rules of the spec. Copy protection: the equipment
 * catalog is read in bytes 0-16 only (bytes 17-19 are the manual query's position table, docs/ui-sheets.md). */
#ifndef BIING_SHEET_H
#define BIING_SHEET_H

#include "assets.h"
#include "sim.h"
#include "text.h"

typedef struct {
    int step_index; /* [0x5AB9]: 4 x the step code last set; shared by every sheet and kept between them */
    int kind_item; /* [0x5B71]: the item of the last search by kind (0x3116D); kept when a search finds none */
} SheetMemory;

typedef struct {
    Sim *s; /* world, program image, random numbers, the wear flag */
    const TextDb *texts;
    const DataDb *people; /* the name pools; NULL = names left out */
    SheetMemory *mem;
    Message *m; /* the message buffer the lines are built in */
} SheetCtx;

enum { SHEET_COMPLAINTS, SHEET_DIAGNOSIS, SHEET_TREATMENT, SHEET_ACTIVITY, SHEET_AUTOPSY, SHEET_MOOD, SHEET_OPINION,
       SHEET_LINES };
extern const int SHEET_LINE_Y[SHEET_LINES]; /* 54, 93, 132, 197, 236, 275, 314 */

typedef struct {
    Message line[SHEET_LINES];
    uint8_t drawn[SHEET_LINES]; /* 0: the line is not drawn (a branch where the original hangs, guarded) */
} SheetPage;

/* Page 1 of the patient sheet for person `index` (of the player's hospital). Draws random numbers, may wear an
 * instrument (the original's wear flag, reproduced) and updates the shared step index. */
void sheet_page1(SheetCtx *c, int index, SheetPage *out);

/* The activity line of the personnel sheet for the player's staff slot `slot`, built in c->m (which holds the mood
 * line drawn just before: a duty state without a sentence leaves it as it is). 1 = draw the buffer; 0 = draw nothing
 * (a club or golf room with its flag set and nobody listed, where the original hangs); -1 = the garage (state 10),
 * whose line garage_activity gives. */
int sheet_staff_activity(SheetCtx *c, int slot);

/* The personnel sheet's eight selectors (docs/ui-sheets.md, section 2) from the staff record and the date. */
enum { SEL_A, SEL_C, SEL_D, SEL_F, SEL_G, SEL_H, SEL_E, SEL_B };
int sheet_selector(const uint8_t *staff, int day, int hour, int which);

/* An equipment item's name (text number at catalog +0), cut before " (" when `cut` (0x2C264's copy). */
void sheet_item_name(SheetCtx *c, int id, int cut);

#endif
