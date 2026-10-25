/* The simulation: the game clock and the scheduled routines of docs/mainloop.md, section 5, implemented
 * from docs/sim.md. Works on the World alone, so it runs with or without the screen. */
#ifndef BIING_SIM_H
#define BIING_SIM_H

#include "program.h"
#include "world.h"

struct Sound; /* sound.h: the event sounds the simulation requests; NULL = none (biing-save) */

/* The clock routine's two animation counters (docs/mainloop.md, "Rest of the clock routine"): decremented every game
 * tick and reloaded when they reach 0, from the image's start values at the program start (SCR-8). */
#define SIM_REDRAW_START 120
#define SIM_REDRAW_PERIOD 50
#define SIM_ANIM_START 120
#define SIM_ANIM_PERIOD 2
#define SIM_TICKS_PER_MINUTE 80
#define SIM_MAX_ROUTINES 256

extern const uint32_t SIM_TICKS[80][8]; /* sim_schedule.c: routines per tick 1-79, 0-terminated */

/* The input pair of one draw (docs/mainloop.md section 3): d = the low 16 bits of the caller's EDI, e = its EBP plus
 * the scratch pointer [0x4528]. on = 0: the default, a fresh pair from the auxiliary generator at every step. */
typedef struct {
    int on;
    uint32_t d, e;
} SimIn;

/* A test aid (tools/diff_routines.py, BIING_RNG_SCRIPT): scripted generator results. While a script is set, every
 * generator step returns the next value v[used] instead of its own t (S still advances by it, S = S + t) and takes no
 * input pair; when the script has run out the steps go back to the generator and `extra` counts them (the run is then
 * not comparable). trace (FILE *, or NULL): one line per draw call, "<site> <b mask | r n> <value> <steps>", site 0
 * where the engine names none. Without a script nothing changes. */
typedef struct {
    const uint32_t *v;
    size_t n, used, extra;
    void *trace;
} SimScript;

/* A live World or mapped room-catalog reference, never a host pointer. For World references, block
 * is a one-based save-block number; zero is the measured initial null context (docs/sim-rooms.md). */
typedef struct {
    unsigned block;
    size_t offset;
    enum { SIM_RATING_WORLD = 0, SIM_RATING_CATALOG } kind;
} SimRatingRef;

typedef struct {
    World *world;
    const Program *prog;
    SimScript *rng_script; /* NULL: the generator's own results (the game) */
    /* Optional diagnostic failure handler. Must not return; called before an exhausted script could fall back
     * to natural draws, or on a trace write failure. NULL preserves the save comparison's counted fallback. */
    void (*rng_failure)(const SimScript *, const char *);
    uint64_t rng;      /* the auxiliary generator (fresh input pairs for the default draws, docs/rng-sites.md section 4) */
    uint32_t rng_s;    /* the original's generator state S ([0x5AA1]) */
    uint32_t rng_time; /* the clock value T that the reseed reads (docs/mainloop.md section 3, the engine's clock rule) */
    SimIn rng_in;      /* the inputs a routine sets for all draws of a stretch (a context of docs/rng-sites.md 2.3) */
    void *rng_log;     /* FILE *: one line per draw of a listed site (biing-save sim with BIING_RNG_LOG), or NULL */
    /* the build's input constants (docs/rng-sites.md section 8, OTH-DE-14): the AI's scratch player block (the build's
     * offset of disk 0x6187) and what the build adds to every heap address of section 2.2 */
    uint32_t rng_player, rng_heap;
    /* [0x4528], the scratch pointer that contexts S, O, A and M read: what its last writer left (docs/rng-sites.md
     * 2.4); 0 at a load and at a new game (not saved) */
    uint32_t rng_scratch;
    SimRatingRef rating_ref; /* [0x4514], unsaved; the chooser reads this record's current byte 1 */
    int tick; /* [0x5AA9], not saved */
    /* [0x5AB1]: counts down to the players' redraw flags pb+0x3B, first after 120 ticks, then every 50 (not saved) */
    int redraw_count;
    /* per scheduled routine: calls so far; `implemented` marks the ones the engine has */
    uint32_t routine[SIM_MAX_ROUTINES];
    uint32_t calls[SIM_MAX_ROUTINES];
    uint8_t implemented[SIM_MAX_ROUTINES];
    int nroutines;
    /* module B (docs/sim-patients.md) */
    const uint8_t *ailments; /* BITEXPZ entry 4014: 4 bytes per illness id */
    size_t ailments_len;
    uint8_t onset_types[91]; /* the onset's scratch list, kept between calls as in the original */
    /* [0x5BC3], [0x5BC5], [0x5BC7], [0x5BCD], [0x5BCF], [0x5BD1]: the city's room counts of categories 5, 11, 15, 7, 8
     * and 10, written only by the illness onset's step 3; not saved, 0 at the program start and kept over a load
     * (docs/news-generators.md, "The city test"; SIM-51). Read by the OP and torture reviews and the race start. */
    uint16_t city[6];
    /* [0x45CF]: the scratch byte the "gave up waiting" item's duplicate test compares b21 with, whatever its last writer
     * left (docs/mainloop.md, module H item 2, "Who leaves the value"; SIM-55). Not saved, 0 at the program start. */
    uint8_t dup_byte;
    /* N4, entry 4 (kitchen staff) of the need table [0x472A] (signed 16-bit): what 0x62D6A and the dismissals 0x5B25D
     * left; catalog row 90's +0x29 is its high byte (the pathology's stale base, SIM-65). Not saved: 0 at the program
     * start and after a load (docs/sim-ai.md, "Row 90 reads the staffing need of job group 4"). */
    int16_t need4;
    int last_illness, last_treatment; /* home treatment keeps the previous person's records */
    int openings; /* [0x5BAB]: job openings counted by 0x1F2A8 */
    int on_applications; /* the player is on screen 51: the player's openings do not expire (0x1F2A8) */
    uint32_t news_written[64]; /* module H: newspaper items written per item type, for the comparisons */
    /* AI hospitals' patient flow per room kind (treatment room, dental ward, neurology), for the oracle comparisons:
     * takes by the patient's type 0 / 1 / 2, diagnoses, right diagnoses (the +3 bonus), returns to a waiting room */
    uint32_t flow[3][6];
    /* the work advice (docs/ui-reports.md, section 2) and what of the screen it reads */
    int advice_pending; /* [0x5C84] */
    int help_strip; /* [0xB10C]: the "Hilfe!" strip lit */
    int fast_forward; /* [0x5C25]: "skip one hour" runs the clock */
    int screen; /* the player's current screen (the store's self-fetch test reads screen 0x41) */
    const uint16_t *staff_list; /* [0x4656], the shuffled applicant list; NULL: position = staff number */
    /* BIDATP: the race start redraws a person with a long name, and the rename of the dead (0x3200C) rewrites a name
     * in its pool C for the rest of the session (never saved); NULL: no name test, no rename */
    DataDb *people;
    const TextDb *texts; /* BITEXPZ: the rename's stock first names (texts 4015-4024); NULL: no rename */
    int race_redraw; /* [0x5C79]: set by the race start, the site map's frame redraws once and clears it */
    struct Sound *sound; /* the game's sound driver, or NULL */
    /* module D (docs/sim-player.md), what the screen draws for it after the tick: the reception show's image 519
     * (set only while screen 19 is shown), and the patient thrown out by an independent receptionist with no waiting
     * room (message box 515 / 2265 + name + 2264, screen 53); 0 = nothing. The frame clears both. */
    int player_image;
    int player_box;
    /* [0x5B6F]: the use flag of the last equipment search (set by a check-and-use, clear after a plain check); the
     * patient sheet's instrument search reads it and never clears it (docs/ui-sheets.md) */
    int equipment_use;
    /* [0x5AB3]: counts down to the players' animation tick pb+0x3C, first after 120 ticks, then every 2 (not saved) */
    int anim_count;
    /* The registers the clock call passes on to context S (docs/sim-drift8.md section 14, docs/rng-sites.md 2.3): EDI
     * (clock_d, low 16 bits used) and EBP (clock_ebp), set by sim_clock_registers from the screen and the skip loop.
     * skip_registers: the clock runs as in "skip one hour" whatever the screen (biing-save sim; an engine choice). */
    uint32_t clock_d, clock_ebp;
    int skip_registers;
    /* A test hook (biing-save sim with BIING_SIM_CALLS, tools/diff_clock.py): when set, called before every call of
     * the schedule (call(): the tick, minute, hour and day lists, the reseed, the store test) with the routine's
     * program offset. NULL in the game and by default; it reads the state, it changes nothing. */
    void (*before_call)(void *ctx, uint32_t routine);
    void *before_call_ctx;
} Sim;

void sim_init(Sim *s, World *w, const Program *p, uint64_t seed);
/* The ailment table (BITEXPZ entry 4014, textdb_raw) that the illness onset needs. */
void sim_set_ailments(Sim *s, const uint8_t *table, size_t len);
/* One game tick (docs/mainloop.md, "When a game tick happens"): the tick's routine list, and at tick 80
 * the minute, hour and day lists as they come due. */
void sim_tick(Sim *s);
/* "Skip one hour" (0x49184): ticks until the minute is back at its start value one hour later; returns the ticks run */
int sim_skip_hour(Sim *s, void (*each)(void *), void *ctx);
/* The clock call's EDI / EBP for the current state (docs/sim-drift8.md section 14.2): while "skip one hour" runs (or
 * with skip_registers) D = lo(0x2F850), EBP 0xB124; on screen 5 (the site map) D 0x631B, EBP 0x552A; on any other
 * screen 1-161 D 0x631B and EBP the screen's script address from the screen table; screen 0 (none shown) 0 / 0, the
 * oracle's start registers. Disk offsets mapped to the build; the script address is the build's own. sim_tick and
 * sim_call call it; tests call it after setting the screen. */
void sim_clock_registers(Sim *s);
/* one call of the original routine at that program offset, as the schedule makes it; 1 if implemented */
int sim_call(Sim *s, uint32_t routine);

/* Random numbers: the original's generator (docs/mainloop.md, section 3) with the input pairs of docs/rng-sites.md.
 * sim_bits(s, mask) = one draw AND mask (mask 2^k - 1); sim_random(s, n) = random(n): 0 without a draw for n 0 or 1,
 * else draws masked with 2^(L+1) - 1 (L = bit length of n) and redrawn while n or more, all with the same inputs.
 * Both take the inputs in s->rng_in (a routine's context), or the default (fresh from the auxiliary generator at every
 * step) when none is set. The _site forms do the same and log the call under the original's site (the return
 * address of its draw call, docs/rng-sites.md); the _in forms take explicit inputs. */
uint32_t sim_bits(Sim *s, uint32_t mask);
uint32_t sim_random(Sim *s, uint32_t n);
uint32_t sim_bits_site(Sim *s, uint32_t mask, uint32_t site);
uint32_t sim_random_site(Sim *s, uint32_t n, uint32_t site);
uint32_t sim_bits_in(Sim *s, uint32_t mask, uint32_t site, SimIn in);
uint32_t sim_random_in(Sim *s, uint32_t n, uint32_t site, SimIn in);
/* an input pair: d = EDI (low 16 bits used), e = EBP + [0x4528] */
static inline SimIn sim_in(uint32_t edi, uint32_t ebp, uint32_t scratch) {
    SimIn in = {1, edi & 0xFFFFu, ebp + scratch};
    return in;
}
#define SIM_DEFAULT_IN ((SimIn){0, 0, 0})
/* sets the routine's inputs for the draws that follow; returns the previous ones (restore them when done) */
SimIn sim_set_inputs(Sim *s, SimIn in);
/* the generator itself, for the tests: one step of state *state with inputs (d, e) returns t; the reseed's closed form
 * with DX = seconds x 256 + hundredths and CX = hours x 256 + minutes */
uint32_t sim_generator_step(uint32_t *state, uint32_t d, uint32_t e);
void sim_generator_reseed(uint32_t *state, uint32_t dx, uint32_t cx);
#define SIM_GENERATOR_START 0xE7C2D08Cu /* S at every load and new game (the RAM dump's value) */
/* S = the start value and T = seed x 100003, as at a load or a new game (BIING_SEED is the seed) */
void sim_reset_generator(Sim *s, uint64_t seed);
/* a test aid: another random sequence for every draw, the routines' contexts included (the auxiliary generator =
 * seed, S = the start value XOR a hash of seed); the game itself always starts from SIM_GENERATOR_START */
void sim_seed(Sim *s, uint64_t seed);
/* reads a script file (one hex value per line, "#" lines ignored) into *v / *n (malloc'd); 0 on success */
int sim_script_read(const char *path, uint32_t **v, size_t *n);
/* 0x1A4C5 with the engine's clock rule: T + 1, then the reseed with the time T gives */
void sim_reseed(Sim *s);
/* the oracle's address of a record in the world's blocks (docs/rng-sites.md section 2.2), 0 when the block has none */
uint32_t sim_address(const Sim *s, const void *record);
/* 0x1AEC0: a hospital's fixed costs per day (rent, plots, wages; docs/sim.md). */
long sim_daily_costs(Sim *s, int hospital);
/* Values the windows compute when they open (docs/sim-patients.md, docs/desktop.md); they write into the
 * hospital records as the original does. */
void sim_opinion_sum(Sim *s); /* 0x1A684: +0x44, +0x164, +0x166 */
void sim_score(Sim *s); /* 0x1A78C: +0x44 */
void sim_ranking(Sim *s, uint8_t order[16]); /* 0x1A730: hospitals by +0x44, descending, stable */
void sim_staff_averages(Sim *s); /* 0x629E7 */
void sim_staff_training(Sim *s); /* 0x38D39 */
void sim_nurse_experience(Sim *s); /* German CD: +0x194, the nurses' average experience (OTH-DE-8) */

#endif
