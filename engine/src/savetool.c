/* biing-save: the engine's save game code on the command line.
 *   biing-save info <save>                      date, player cash, checksum check
 *   biing-save roundtrip <save> <out>           decode and encode again; the output must be byte-identical
 *   biing-save newgame <BIING!-dir> <out>       write the engine's new game as a save game
 *   biing-save diff <BIING!-dir> <save>         per block: bytes where the engine's new game differs from
 *                                               the save (for a save made right after a new game started)
 *   biing-save sim <BIING!-dir> <hours or h:mm> [<save-in> or -] [<save-out> [<every>]]   (BIING_SEED=n: another
 *                                               random sequence; BIING_SCREEN=n: the player's screen for the whole
 *                                               run, default 0, e.g. 65 for the inventory)
 *     (with <every>: also an intermediate save every <every> game minutes, <save-out> + "_<minute>.sav")
 *                                               run the simulation for that much game time from a new game
 *                                               (or from a save) and print the routines it called: which
 *                                               the engine implements and which it still skips
 *   biing-save call <BIING!-dir> <save-in> <routine-hex> <n> <save-out>
 *                                               call one routine of the schedule n times on a save; the
 *                                               counterpart of tools/oracle.py for comparing with the original
 *     (sim and call: BIING_RNG_SCRIPT=<file> scripted generator results, BIING_RNG_TRACE=<file> a line per draw call;
 *      BIING_SIM_FAST_FORWARD=1 skips scheduled advice like the skip-hour clock; all other values keep normal mode;
 *      tools/diff_routines.py; sim: BIING_SIM_CALLS=<file> the clock test's call log, see CallLog below;
 *      tools/diff_clock.py) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fixes.h"
#include "report.h"
#include "sim.h"
#include "world.h"

static int new_world(World *w, const char *root) {
    Game g;
    Program p;
    DataDb d;
    char path[1200];
    if (game_open(&g, root)) return -1;
    game_path(&g, "BIPRO.EXE", path, sizeof path);
    if (program_load(&p, path)) return -1;
    game_path(&g, p.build->data, path, sizeof path);
    if (datadb_load(&d, path)) return -1;
    int rc = world_new(w, &p, d.patients, d.staff);
    if (!rc) { /* the start-up's baseline and expected cash (SAV-1), as the game's new game */
        static Sim s;
        sim_init(&s, w, &p, 1);
        report_new_game(&s);
    }
    datadb_free(&d);
    program_free(&p);
    return rc;
}

static int load_program(const char *root, Program *p) {
    Game g;
    char path[1200];
    if (game_open(&g, root)) return -1;
    game_path(&g, "BIPRO.EXE", path, sizeof path);
    return program_load(p, path);
}

static void info(const World *w);

/* The clock test's call log (tools/diff_clock.py): BIING_SIM_CALLS=<file> writes "M <minutes run> <script steps>" at
 * every minute's end; BIING_SIM_CALL_WINDOW=<first>,<last> (ticks counted from 0 at the run's start) adds
 * "C <tick> <routine> <steps>" before every scheduled call and "E <tick> <steps>" after every tick in the window, and
 * BIING_SIM_CALL_SAVES=<prefix> writes the world before each of those lines as <prefix>_<n>.sav (n from 0, appended to
 * the line; with BIING_SIM_CALL_SAVES_TICKS set, only at the "E" lines). Steps: the scripted generator steps so far (0
 * without BIING_RNG_SCRIPT). Without BIING_SIM_CALLS nothing of this runs. */
typedef struct {
    FILE *log;
    const char *prefix;
    int ticks_only;
    long first, last, tick, seq;
    const World *w;
    const SimScript *script;
} CallLog;

static size_t call_log_steps(const CallLog *c) { return c->script ? c->script->used + c->script->extra : 0; }

static void call_log_snapshot(CallLog *c, int at_call) {
    if (c->prefix && !(at_call && c->ticks_only)) {
        char snap[1200];
        snprintf(snap, sizeof snap, "%s_%ld.sav", c->prefix, c->seq);
        if (world_save(c->w, snap)) fprintf(stderr, "[FAIL] cannot write %s\n", snap);
        fprintf(c->log, " %ld", c->seq++);
    }
    fputc('\n', c->log);
}

static void call_log_before(void *ctx, uint32_t routine) {
    CallLog *c = ctx;
    if (c->tick < c->first || c->tick > c->last) return;
    fprintf(c->log, "C %ld %X %zu", c->tick, routine, call_log_steps(c));
    call_log_snapshot(c, 1);
}

static int simulate(const char *root, int minutes, const char *in, const char *out, uint32_t routine, long times, int every) {
    World w;
    Program p;
    if (load_program(root, &p)) return -1;
    if (in && strcmp(in, "-") ? world_load(&w, in) : new_world(&w, root)) return -1;
    static Sim s;
    const char *seed = getenv("BIING_SEED"); /* another random sequence, to measure the spread of a run */
    sim_init(&s, &w, &p, seed ? strtoull(seed, NULL, 10) : 1);
    const char *fast_forward = getenv("BIING_SIM_FAST_FORWARD");
    s.fast_forward = fast_forward && !strcmp(fast_forward, "1");
    if (s.fast_forward) fprintf(stderr, "simulation mode: fast-forward\n");
    /* the player's screen word (block 28 +4 in the original): no screen is shown here, so 0 unless given; 65 (the
     * inventory) switches the store-screen tests of docs/sim-rooms2.md, as the oracle's screen word 0x41 does */
    const char *screen = getenv("BIING_SCREEN");
    s.screen = screen ? atoi(screen) : 0;
    /* context S's clock registers (docs/sim-drift8.md section 14): the run goes on like the skip loop, so the skip
     * loop's pair whatever the screen (an engine choice, docs/engine.md 2026-10-21) */
    s.skip_registers = 1;
    sim_clock_registers(&s);
    /* the per-site draw log (docs/rng-sites.md section 6): "<site> <b mask | r n> <value>" per draw of a listed site;
     * tools/rng_check.py compares it with the original's shares */
    const char *log_path = getenv("BIING_RNG_LOG");
    FILE *rng_log = log_path ? fopen(log_path, "w") : NULL;
    if (log_path && !rng_log) {
        fprintf(stderr, "[FAIL] cannot write %s\n", log_path);
        return -1;
    }
    s.rng_log = rng_log;
    /* scripted draws (tools/diff_routines.py): BIING_RNG_SCRIPT=<file> (one hex result per line) makes every generator
     * step return the next value; BIING_RNG_TRACE=<file> writes one line per draw call; the count goes to stderr */
    static SimScript script;
    static uint32_t *script_values;
    const char *script_path = getenv("BIING_RNG_SCRIPT");
    if (script_path) {
        if (sim_script_read(script_path, &script_values, &script.n)) {
            fprintf(stderr, "[FAIL] cannot read %s\n", script_path);
            return -1;
        }
        script.v = script_values;
        const char *trace_path = getenv("BIING_RNG_TRACE");
        if (trace_path && !(script.trace = fopen(trace_path, "w"))) {
            fprintf(stderr, "[FAIL] cannot write %s\n", trace_path);
            return -1;
        }
        s.rng_script = &script;
    }
    Game g;
    char path[1200];
    TextDb texts;
    if (game_open(&g, root)) return -1;
    game_path(&g, p.build->texts, path, sizeof path);
    if (textdb_load(&texts, path)) return -1;
    size_t ailments;
    const uint8_t *table = textdb_raw(&texts, 4014, &ailments);
    sim_set_ailments(&s, table, ailments);
    static DataDb people; /* the race start's name test */
    game_path(&g, p.build->data, path, sizeof path);
    if (!datadb_load(&people, path)) s.people = &people;
    s.texts = &texts; /* the rename of the dead */
    if (routine) { /* call: one routine, times times (the counterpart of tools/oracle.py) */
        for (long t = 0; t < times; t++)
            if (!sim_call(&s, routine)) {
                fprintf(stderr, "[FAIL] routine 0x%X is not implemented\n", routine);
                return -1;
            }
        if (rng_log) fclose(rng_log);
        if (s.rng_script) {
            fprintf(stderr, "rng script: %zu of %zu values used, %zu steps after it ran out\n", script.used, script.n,
                    script.extra);
            if (script.trace) fclose((FILE *)script.trace);
            free(script_values);
        }
        int rc = world_save(&w, out);
        world_free(&w), textdb_free(&texts), program_free(&p);
        return rc;
    }
    static CallLog calls;
    const char *calls_path = getenv("BIING_SIM_CALLS");
    if (calls_path) {
        if (!(calls.log = fopen(calls_path, "w"))) {
            fprintf(stderr, "[FAIL] cannot write %s\n", calls_path);
            return -1;
        }
        const char *window = getenv("BIING_SIM_CALL_WINDOW");
        calls.first = 0, calls.last = -1;
        if (window && sscanf(window, "%ld,%ld", &calls.first, &calls.last) != 2) calls.first = 0, calls.last = -1;
        calls.prefix = getenv("BIING_SIM_CALL_SAVES");
        calls.ticks_only = getenv("BIING_SIM_CALL_SAVES_TICKS") != NULL;
        calls.w = &w;
        calls.script = s.rng_script;
        s.before_call = call_log_before;
        s.before_call_ctx = &calls;
    }
    for (long t = 0; t < (long)minutes * SIM_TICKS_PER_MINUTE; t++) {
        calls.tick = t;
        sim_tick(&s);
        if (calls.log && t >= calls.first && t <= calls.last) {
            fprintf(calls.log, "E %ld %zu", t, call_log_steps(&calls));
            call_log_snapshot(&calls, 0);
        }
        if (calls.log && (t + 1) % SIM_TICKS_PER_MINUTE == 0)
            fprintf(calls.log, "M %ld %zu\n", (t + 1) / SIM_TICKS_PER_MINUTE, call_log_steps(&calls));
        long minute = (t + 1) / SIM_TICKS_PER_MINUTE;
        if (every > 0 && out && (t + 1) % SIM_TICKS_PER_MINUTE == 0 && minute % every == 0 && minute < minutes) {
            char snap[1200];
            snprintf(snap, sizeof snap, "%s_%ld.sav", out, minute);
            if (world_save(&w, snap)) return -1;
        }
    }
    int done = 0, skipped = 0;
    long done_calls = 0, skipped_calls = 0;
    for (int i = 0; i < s.nroutines; i++) {
        if (s.implemented[i]) done++, done_calls += s.calls[i];
        else skipped++, skipped_calls += s.calls[i];
    }
    printf("after %d:%02d game time: ", minutes / 60, minutes % 60);
    info(&w);
    printf("routines called: %d, implemented %d (%ld calls), not yet %d (%ld calls)\n", s.nroutines, done,
           done_calls, skipped, skipped_calls);
    printf("not yet, by calls:");
    for (int shown = 0; shown < skipped; shown++) { /* largest first */
        int best = -1;
        for (int i = 0; i < s.nroutines; i++)
            if (!s.implemented[i] && s.calls[i] != UINT32_MAX && (best < 0 || s.calls[i] > s.calls[best])) best = i;
        printf(" 0x%X:%u", s.routine[best], s.calls[best]);
        s.calls[best] = UINT32_MAX; /* listed */
    }
    printf("\nhospital cash:");
    for (int h = 0; h < 16; h++) printf(" %d", world_cash(&w, h));
    printf("\nnewspaper items written (type:count):");
    for (int t = 0; t < 64; t++)
        if (s.news_written[t]) printf(" 0x%02X:%u", t, s.news_written[t]);
    printf("\n");
    static const char *const KIND[3] = {"treatment room", "dental ward", "neurology"};
    for (int k = 0; k < 3; k++)
        printf("AI %s: takes of type 0/1/2 %u/%u/%u, diagnoses %u (%u right), back to a waiting room %u\n", KIND[k],
               s.flow[k][0], s.flow[k][1], s.flow[k][2], s.flow[k][3], s.flow[k][4], s.flow[k][5]);
    if (rng_log) fclose(rng_log);
    if (s.rng_script) {
        fprintf(stderr, "rng script: %zu of %zu values used, %zu steps after it ran out\n", script.used, script.n,
                script.extra);
        if (script.trace) fclose((FILE *)script.trace);
        free(script_values);
    }
    if (calls.log) fclose(calls.log);
    int rc = out ? world_save(&w, out) : 0;
    world_free(&w);
    textdb_free(&texts);
    program_free(&p);
    return rc;
}

static void info(const World *w) {
    printf("date %02d.%02d.%d %02d:%02d, hours played %d, player cash %d\n", world_tail(w, TAIL_DAY),
           world_tail(w, TAIL_MONTH), world_tail(w, TAIL_YEAR), world_tail(w, TAIL_HOUR),
           world_tail(w, TAIL_MINUTE), world_tail(w, TAIL_HOURS), world_cash(w, 0));
}

static int diff(const World *a, const World *b) {
    int total = 0;
    for (int i = 0; i <= WORLD_BLOCKS; i++) {
        int n = 0, first = -1;
        for (size_t k = 0; k < a->size[i]; k++)
            if (a->block[i][k] != b->block[i][k]) {
                if (first < 0) first = (int)k;
                n++;
            }
        if (n) printf("%s %2d: %6d of %6zu bytes differ, first at 0x%X\n", i == WORLD_TAIL ? "tail " : "block",
                      i + 1, n, a->size[i], first);
        total += n;
    }
    printf("%d bytes differ\n", total);
    return total;
}

int main(int argc, char **argv) {
    World a, b;
    if (fixes_init(0, 0)) return 2; /* BIING_FIXES (docs/fixes.md): named on stderr only when a fix is on */
    if (argc == 3 && !strcmp(argv[1], "info")) {
        if (world_load(&a, argv[2])) goto fail;
        info(&a);
        printf("[OK] checksum matches\n");
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "roundtrip")) {
        if (world_load(&a, argv[2]) || world_save(&a, argv[3])) goto fail;
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "newgame")) {
        if (new_world(&a, argv[2]) || world_save(&a, argv[3])) goto fail;
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "diff")) {
        if (new_world(&a, argv[2]) || world_load(&b, argv[3])) goto fail;
        printf("engine new game: ");
        info(&a);
        printf("save:            ");
        info(&b);
        diff(&a, &b);
        return 0;
    }
    if (argc >= 4 && !strcmp(argv[1], "sim")) {
        int h = 0, m = 0;
        if (sscanf(argv[3], "%d:%d", &h, &m) < 1) goto usage;
        if (simulate(argv[2], h * 60 + m, argc > 4 ? argv[4] : NULL, argc > 5 ? argv[5] : NULL, 0, 0, argc > 6 ? atoi(argv[6]) : 0))
            goto fail;
        return 0;
    }
    if (argc == 7 && !strcmp(argv[1], "call")) {
        uint32_t routine = (uint32_t)strtoul(argv[4], NULL, 16);
        if (!routine || simulate(argv[2], 0, argv[3], argv[6], routine, atol(argv[5]), 0)) goto fail;
        printf("[OK] 0x%X x %s -> %s\n", routine, argv[5], argv[6]);
        return 0;
    }
usage:
    fprintf(stderr, "usage: see the comment at the top of savetool.c\n");
    return 2;
fail:
    fprintf(stderr, "[FAIL] %s\n", asset_error());
    return 1;
}
