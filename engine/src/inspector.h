/* Opt-in live inspector. Commands run on the game thread, between frames.
 * Protocol and field definitions are independent of the deterministic control API.
 * No arbitrary writes: only named scalar fields, while paused, with an expected old value. */
#ifndef BIING_INSPECTOR_H
#define BIING_INSPECTOR_H
#include "game.h"
#include <stdio.h>

typedef struct {
    int paused;
    unsigned steps;
    uint64_t seq, frames, edits;
} Inspector;
/* Emit one JSON line. NULL command is the ready message. Returns nonzero on output failure.
 * Commands: status, schema, list:G, read:G:I, raw:B:OFFSET,
 * pause, resume, step:N (1..50), set:G:I:F:EXPECTED:VALUE, save:PATH, load:PATH, quit.
 * Loading requires paused/idle state, returns to the office and resets the edit count.
 * Group, record, field and block numbers are zero-based. */
int inspector_command(Inspector *in, Biing *b, const char *command, FILE *out);
int inspector_error(Inspector *in, Biing *b, const char *error, FILE *out);
#endif
