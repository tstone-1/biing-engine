#ifndef BIING_CONTROL_H
#define BIING_CONTROL_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Local protocol 1: one complete command per line, one JSON response per line.
 * Coordinates address the 640x440 game buffer (row 20 of the 640x480 picture).
 * The clock advances only through commands; this is the batch input path, not
 * SDL event delivery. No network listener, game-state cheats or routine calls. */
#define CONTROL_LINE_MAX 4096
#define CONTROL_ADVANCE_MAX 30000
typedef struct {
    char text[CONTROL_LINE_MAX + 1];
    size_t used;
    int invalid;
} ControlLine;
/* Feed one byte. A complete line returns 1; inspect invalid before validation,
 * then reset the structure. An unterminated final line may be finished with LF. */
int control_feed(ControlLine *line, unsigned char c);
/* NULL means valid. Validates the entire token before any game-side effects. */
const char *control_validate(const char *command, int intro_active);
typedef struct {
    uint64_t seq, frames;
    int screen, day, hour, minute, quit, intro_active;
} ControlState;
int control_response(FILE *out, const ControlState *s, int ready, int window, const char *error);
#endif
