/* The start sequence (startup.h). Written from docs/programs.md and docs/saves.md. */
#include "startup.h"
#include "sound.h"

#include <stdio.h>
#include <string.h>

int startup_plan(int intro_on, int tutorial_on, int cd, int out[8]) {
    int n = 0;
    out[n++] = START_PRE;
    out[n++] = START_RELO;
    if (intro_on) out[n++] = START_BINT;
    if (cd && tutorial_on) out[n++] = START_ENCODE;
    out[n++] = START_GAME;
    return n;
}

static int file_exists(const char *root, const char *name) {
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", root, name);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

void startup_config(Startup *s, const char *root) {
    s->intro_on = 1;
    s->tutorial_on = s->quad = 0;
    s->cd = file_exists(root, "CDC");
    Blob b;
    char path[1200];
    snprintf(path, sizeof path, "%s/BIING!.CFG", root);
    if (blob_read(path, &b) == 0) {
        const char *t = (const char *)b.data;
        s->intro_on = sound_cfg_flag(t, b.size, "INTRO", 1);
        s->tutorial_on = sound_cfg_flag(t, b.size, "TUTORIAL", 0);
        s->quad = sound_cfg_flag(t, b.size, "QUAD", 0);
        blob_free(&b);
    }
}

static void close_stage(Startup *s) {
    if (!s->open) return;
    switch (s->plan[s->at]) {
    case START_PRE: pre_free(&s->pre); break;
    case START_RELO: relo_free(&s->relo); break;
    case START_BINT: bint_free(&s->bint); break;
    case START_ENCODE: tutorial_free(&s->tut); break;
    default: break;
    }
    s->open = 0;
}

/* Opens the stage at s->at; a stage that cannot run is skipped (with a message), as B runs the next program. */
static void open_stage(Startup *s) {
    while (s->at < s->nplan && s->plan[s->at] != START_GAME) {
        int rc = -1;
        switch (s->plan[s->at]) {
        case START_PRE: {
            int marker = s->marker >= 0 ? s->marker : pre_marker_exists(s->root);
            if (s->write_marker && pre_marker_write(s->root)) fprintf(stderr, "[WARN] %s\n", asset_error());
            rc = pre_open(&s->pre, s->root, marker);
            break;
        }
        case START_RELO: rc = relo_open(&s->relo, s->root, s->sound); break;
        case START_BINT: rc = bint_open(&s->bint, s->root, s->sound, s->seed); break;
        case START_ENCODE:
            rc = tutorial_open(&s->tut, s->root, s->sound, s->quad, s->tut_scene ? s->tut_scene : 1);
            break;
        default: break;
        }
        if (rc == 0) {
            s->open = 1;
            s->units = 0;
            return;
        }
        fprintf(stderr, "[WARN] start stage %d skipped: %s\n", s->plan[s->at], asset_error());
        s->failed++;
        s->open = 1; /* so close_stage frees what the failed open loaded */
        close_stage(s);
        s->at++;
    }
}

int startup_open(Startup *s, const char *root, struct Sound *sound, int write_marker, int marker, uint32_t seed) {
    memset(s, 0, sizeof *s);
    snprintf(s->root, sizeof s->root, "%s", root);
    s->sound = sound;
    s->write_marker = write_marker;
    s->marker = marker;
    s->seed = seed;
    startup_config(s, root);
    s->nplan = startup_plan(s->intro_on, s->tutorial_on, s->cd, s->plan);
    open_stage(s);
    return 0;
}

int startup_open_one(Startup *s, const char *root, struct Sound *sound, int stage, int marker, uint32_t seed) {
    memset(s, 0, sizeof *s);
    snprintf(s->root, sizeof s->root, "%s", root);
    s->sound = sound;
    s->marker = marker;
    s->seed = seed;
    startup_config(s, root);
    s->plan[0] = stage;
    s->plan[1] = START_GAME;
    s->nplan = 2;
    open_stage(s);
    return 0;
}

int startup_open_tutorial(Startup *s, const char *root, struct Sound *sound, int scene, int quad) {
    memset(s, 0, sizeof *s);
    snprintf(s->root, sizeof s->root, "%s", root);
    s->sound = sound;
    startup_config(s, root);
    if (quad >= 0) s->quad = quad;
    s->tut_scene = scene;
    s->plan[0] = START_ENCODE;
    s->plan[1] = START_GAME;
    s->nplan = 2;
    open_stage(s);
    return 0;
}

void startup_free(Startup *s) {
    if (s->at < s->nplan) close_stage(s);
}

int startup_done(const Startup *s) { return s->at >= s->nplan || s->plan[s->at] == START_GAME; }
int startup_stage(const Startup *s) { return s->at < s->nplan ? s->plan[s->at] : START_GAME; }

int startup_sound_due(const Startup *s) { return (s->units + 1) % INTRO_U50 == 0; }

void startup_advance(Startup *s, IntroInput *in) {
    if (startup_done(s) || !s->open) return;
    s->units++;
    int done = 0;
    switch (s->plan[s->at]) {
    case START_PRE: pre_advance(&s->pre, in), done = s->pre.done; break;
    case START_RELO: relo_advance(&s->relo, in), done = s->relo.done; break;
    case START_BINT: bint_advance(&s->bint, in), done = s->bint.done; break;
    case START_ENCODE:
        tutorial_advance(&s->tut, in), done = s->tut.done;
        if (s->tut.quit) { /* to DOS: B stops, the game never starts */
            close_stage(s);
            s->quit = 1;
            s->at = s->nplan;
            return;
        }
        break;
    default: done = 1; break;
    }
    if (!done) return;
    close_stage(s);
    s->at++;
    open_stage(s);
}

void startup_render(const Startup *s, IntroScreen *out) {
    if (!startup_done(s) && s->open) {
        switch (s->plan[s->at]) {
        case START_PRE: pre_render(&s->pre, out); return;
        case START_RELO: relo_render(&s->relo, out); return;
        case START_BINT: bint_render(&s->bint, out); return;
        case START_ENCODE: tutorial_render(&s->tut, out); return;
        default: break;
        }
    }
    out->w = 320, out->h = 200; /* between programs: text mode, shown black */
    memset(out->px, 0, 320 * 200);
    memset(out->dac, 0, 768);
}

int startup_tick_units(const Startup *s) {
    switch (startup_stage(s)) {
    case START_PRE: return INTRO_U60;
    case START_ENCODE: return s->open ? s->tut.tick_units : INTRO_U50;
    default: return INTRO_U50;
    }
}

int startup_pointer(const Startup *s, int *x, int *y) {
    if (startup_done(s) || !s->open || s->plan[s->at] != START_ENCODE) return -1;
    *x = s->tut.px, *y = s->tut.py; /* as drawn at the last poll, which the motion is measured from */
    return 0;
}
