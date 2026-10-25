/* The start sequence that replaces B.EXE (docs/programs.md, "Start chain (B.EXE)", "What the engine needs from the
 * chain"; docs/saves.md, "Start chain and the small programs"). The engine is one program, so B's list of EXEC calls
 * becomes a fixed order of internal stages, and the DAC colour 0 hand-over between the programs becomes the stage
 * order itself:
 *
 *   (installer, SETSOUND, CONFIG: not needed, the engine has defaults for missing config files)
 *   -> PRE (always; SECRET.MRK decides skip and cut, and is written before the text)
 *   -> RELO (always)
 *   -> BINT (when the INTRO line of BIING!.CFG is on)
 *   -> ENCODE (CD data present and TUTORIAL on; QUAD picks the 25 fps files; "to DOS" there ends everything)
 *   -> the game.
 *
 * Not reimplemented (AGENTS.md, Rules): the golf hand-over of the CD version (B runs VGAGOLF.COM after BIPRO when DAC
 * colour 0 red is non-zero, then BIPRO again with 0x10); the engine's game ends where B would stop, and a CD golf
 * flag, once the engine has it, at most hands over to the user's own copy. */
#ifndef BIING_STARTUP_H
#define BIING_STARTUP_H

#include "intro_bint.h"
#include "intro_pre.h"
#include "intro_relo.h"
#include "tutorial.h"

struct Sound;

enum { START_PRE, START_RELO, START_BINT, START_ENCODE, START_GAME };

/* The stages for a configuration, in order, ending with START_GAME; returns their number. cd = CD data (a file CDC in
 * the game directory). */
int startup_plan(int intro_on, int tutorial_on, int cd, int out[8]);

typedef struct {
    char root[1024];
    struct Sound *sound;
    int plan[8], nplan, at; /* the stages and the one running */
    int write_marker; /* write SECRET.MRK before the text, as PRE does (off for --shot runs) */
    int marker; /* -1: read the marker file; 0/1: force first run / later run */
    uint32_t seed; /* the starfield's seed */
    int intro_on, tutorial_on, quad, cd;
    Pre pre;
    Relo relo;
    Bint bint;
    Tutorial tut;
    int tut_scene; /* the tutorial's start scene (1 unless a --shot step picks one) */
    int open; /* the current stage's program is open */
    uint64_t units; /* units the current stage's program has run (0 at its open) */
    int failed; /* a program could not open: it is skipped */
    int quit; /* the tutorial's "to DOS": the game does not start */
} Startup;

/* Reads BIING!.CFG (INTRO, TUTORIAL, QUAD; missing file: INTRO on, the tutorial off) and the CDC file. */
void startup_config(Startup *s, const char *root);
/* The whole sequence. */
int startup_open(Startup *s, const char *root, struct Sound *sound, int write_marker, int marker, uint32_t seed);
/* One program alone (START_PRE, START_RELO, START_BINT or START_ENCODE), then the game. */
int startup_open_one(Startup *s, const char *root, struct Sound *sound, int stage, int marker, uint32_t seed);
/* The tutorial alone from a scene (1-13), then the game; quad -1 = as BIING!.CFG says, 0/1 = 15/25 fps (-d/-q). */
int startup_open_tutorial(Startup *s, const char *root, struct Sound *sound, int scene, int quad);
void startup_free(Startup *s);
/* One unit of 1/2100 s of the running program; opens the next stage when it ends. */
void startup_advance(Startup *s, IntroInput *in);
int startup_done(const Startup *s); /* the game's turn */
int startup_stage(const Startup *s);
void startup_render(const Startup *s, IntroScreen *out);
/* Whether the sound driver's 50 Hz interrupt falls before the next unit: every 42nd unit of the running program,
 * counted from its open, so the driver's interrupt and the program's own 50 Hz tick are one interrupt (the timer
 * callback runs the driver, then the program's main loop sees the tick; docs/music.md "Timing"). */
int startup_sound_due(const Startup *s);
/* Units per tick of the running program's own clock: PRE 35 (60 Hz), RELO and BINT 42 (50 Hz), ENCODE 42 (50 Hz, 25 fps)
 * or 70 (30 Hz, 15 fps). */
int startup_tick_units(const Startup *s);
/* The tutorial's pointer (its program pixels), for --shot steps that move it to a point; 0 if the tutorial runs. */
int startup_pointer(const Startup *s, int *x, int *y);

#endif
