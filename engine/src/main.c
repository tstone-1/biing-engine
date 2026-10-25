/* biing <BIING!-dir>                 play: the start sequence (disclaimer, logos, intro, CD tutorial; docs/intro.md),
 *                                    then the game
 * biing <BIING!-dir> --no-intro      play without the start sequence
 * biing <BIING!-dir> --inspect       normal playable window with live inspector stdin/stdout (inspector.h).
 *                                    Skips intros; use tools/biing_inspector.py for the companion app.
 * biing <BIING!-dir> --fixes         play with the fixes of the original's bugs (= BIING_FIXES=all, docs/fixes.md);
 *                                    --no-intro and --fixes in any order
 * biing <BIING!-dir> --view          image browser and font sheets (Tab, Left/Right, Shift = 10 images)
 * biing <BIING!-dir> --control [--window]
 *     persistent protocol 1 on stdin/stdout: one validated batch step per line, plus status and quit.
 *     Starts at the title, paused between requests; --window displays the same state silently, discarding
 *     physical input but servicing close events. This does not exercise SDL input translation. See control.h.
 *     Ready/response JSON reports seq, cumulative game frames (initial 1), screen, day/hour/minute, quit and
 *     intro_active; ready also reports protocol and window. Intro ticks do not count as game frames.
 *     Lines are bounded to 4096 bytes, counts to 30000, keys to 128 ASCII characters, coordinates to 639,439.
 *     cash/call/wav and RNG scripting are not control commands. Errors consume a sequence number and leave
 *     the session usable. EOF executes a final unterminated line then exits. Windows requires piped/file stdin.
 * biing <BIING!-dir> --shot <out.ppm> [step ...]
 *     runs without a window and writes the 640 x 480 display as PPM, cursor included. Steps:
 *     move:X,Y  click:X,Y (press and release there)  wait:N (N timer interrupts of 20 ms)
 *     cash:N (set the player's cash, e.g. -500 to test the bankruptcy path)
 *     load:<save> (replace the game state by a save game, e.g. one made in the original)
 *     save:<file> (write the game state as a save game at that point, for biing-save info/diff)
 *     shot:<file> (write the display at that point, as the final picture is written)
 *     call:<hex> (run a routine of the original as a hotspot would, e.g. call:1D2AB opens the reception's
 *     staff roster before the room screens exist)
 *     key:<text> (type the text, one key per frame, as the high-score name input reads it), key:enter, key:back
 *     code:<hex>,... (raw DOS key codes, one per frame, as tools/oracle_screen.py's step: code:1b,0,48 is Escape,
 *     then cursor up as 0 and its scan code; docs/gui.md, "Keyboard")
 *     wav:<file> (record the sound from here to the end of the run, written as a 21,739 Hz stereo WAV when the run
 *     ends; e.g. --shot o.ppm wav:office.wav click:300,140 wait:1500 is 30 s of the office after the title)
 *     e.g. --shot o.ppm click:300,120 wait:10 renders the office after clicking the title face.
 *     BIING_RNG_SCRIPT=<file> supplies one raw hexadecimal generator result per line from the first frame;
 *     BIING_RNG_TRACE=<file> optionally records each draw call (site, b-mask/r-range, result, raw steps).
 *     The stream continues across load: steps. Invalid scripts, exhausted streams and trace errors fail the run;
 *     stderr reports consumption. These diagnostics apply only to --shot, after new-game initialization.
 *   The programs before the game (startup.c; the picture then shows the program, the steps drive it until it ends):
 *     intro:pre (the disclaimer, SECRET.MRK as found; intro:pre:first / intro:pre:again force the first run or a
 *     later one), intro:relo (the logos), intro:bint (the story intro and the credits), intro:start (the whole start
 *     sequence as the window build runs it), intro:encode (the CD tutorial; intro:encode:NN starts at scene NN, as
 *     ENCODE -s NN, and a further :q or :d forces the 25 or 15 fps files, as -q / -d; else QUAD of BIING!.CFG decides).
 *     A --shot run never writes SECRET.MRK, and the starfield seed is fixed.
 *     tick:N advances N ticks of the running program's own clock (PRE 60 Hz, RELO and BINT 50 Hz); wait:N is N x
 *     20 ms; key:<x> is one key press; click:X,Y holds the left button for 20 ms; right:N holds the right button for
 *     N x 20 ms (BINT's fast-forward); raw:<file> writes the program's own picture (320 x 200 or 640 x 480). In the
 *     tutorial tick:N counts its own timer (50 Hz at 25 fps, 30 Hz at 15 fps), move:X,Y moves its pointer there (as
 *     mouse motion read at its next poll, every second tick, so the clamps hold), click:X,Y moves it there and holds
 *     the left button for one tick, and hold:N holds the left button for N ticks.
 *     e.g. --shot o.ppm intro:pre tick:600 shows the disclaimer after 10 s; intro:relo tick:400 the 3D flight.
 *
 * MIDI music (SOUND.CFG MCARD GM or ROL; docs/music.md, "MIDI output (SND-9)"): the window build plays it on the
 * system's synthesizer, or on the output BIING_MIDI_OUT names (part of its name; "list" prints them, "none" plays
 * none). BIING_MIDI_LOG=<prefix> writes every program run's MIDI stream (window or --shot) as a stream file for
 * tools/compare_midi.py: <prefix>bipro.txt, <prefix>relo.txt, <prefix>bint.txt (the interrupts counted per program;
 * a program's later run in the same process appends after a `#` line).
 *
 * The display is 640 x 480 with the 640 x 440 game buffer at row 20, as in the original (docs/screens.md).
 * Game time runs on a simulated 50 Hz timer interrupt; mouse and buttons are sampled there. One main-loop pass follows
 * each interrupt (the ambience rate of docs/sound.md, "What an engine needs", item 7). Only the window build opens an
 * audio device; headless runs render the sound into a buffer and write it only for a wav: step. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#endif

#include "control.h"
#include "desktop.h"
#include "fixes.h"
#include "game.h"
#include "inspector.h"
#include "midi_host.h"
#include "startup.h"

#define DISPLAY_H 480
#define BUFFER_Y 20
#define CURSOR_W 32
#define CURSOR_ROWS 48
#define INTERRUPT_NS 20000000ull /* 50 Hz */

/* The start sequence when it runs (startup.c): its programs advance in units of 1/2100 s; the engine's sound (the
 * module player and the MIDI driver) gets one 50 Hz interrupt every 42 units of the running program, before that
 * unit's program step, so it is the program's own timer interrupt (startup_sound_due). g_units counts the
 * interrupts. */
static Startup g_start;
static int g_intro;
static IntroInput g_in;
static uint64_t g_units;
static IntroScreen g_screen;
static uint64_t g_frames;
static void (*g_control_pump)(Biing *b);

static void intro_units(Biing *b, long n) {
    for (long i = 0; i < n && g_intro; i++) {
        if (g_control_pump && i % INTRO_U50 == 0) {
            g_control_pump(b);
            if (b->quit) break;
        }
        if (startup_sound_due(&g_start)) sound_interrupt(&b->sound), g_units++;
        startup_advance(&g_start, &g_in);
        if (startup_done(&g_start)) {
            if (g_start.quit) b->quit = 1; /* the tutorial's "to DOS" */
            startup_free(&g_start);
            g_intro = 0;
        }
    }
}

static void compose(Biing *b, uint32_t *display) {
    if (g_intro) {
        startup_render(&g_start, &g_screen);
        intro_compose(&g_screen, display);
        return;
    }
    static uint32_t rgb[SCREEN_W * SCREEN_H];
    const uint8_t *pal = biing_palette(b); /* black while the screen is idle-blanked */
    surface_to_rgb(&b->screen, pal, rgb);
    for (int i = 0; i < SCREEN_W * DISPLAY_H; i++) display[i] = 0xFF000000u;
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++) display[(y + BUFFER_Y) * SCREEN_W + x] = rgb[y * SCREEN_W + x];
    /* Cursor: image 0 cut into four 32-pixel columns; shape 1 while the button is held. Its top-left pixel
     * is the hotspot, colour 0 transparent, clipped at x 640 and below buffer row 440. */
    const Image *img = b->ui.cache[0];
    if (!img) return;
    int shape = b->button_held ? 1 : 0;
    for (int y = 0; y < CURSOR_ROWS && y < img->h; y++)
        for (int x = 0; x < CURSOR_W; x++) {
            int sx = b->ui.mouse_x + x, sy = b->ui.mouse_y + y;
            uint8_t c = img->px[y * img->w + shape * CURSOR_W + x];
            if (!c || sx >= SCREEN_W || sy >= SCREEN_H) continue;
            display[(sy + BUFFER_Y) * SCREEN_W + sx] = palette_rgb(pal, c);
        }
}

static void load_cursor(Biing *b) {
    /* Image 0 is outside dulog_image's range (0 means "no image" in the scripts); load it directly. */
    Image *img = malloc(sizeof *img);
    if (img && image_load(&b->assets, 0, img) == 0) b->ui.cache[0] = img;
    else free(img);
}

static int write_ppm(const uint32_t *display, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, DISPLAY_H);
    for (int i = 0; i < SCREEN_W * DISPLAY_H; i++) {
        uint8_t px[3] = {(uint8_t)(display[i] >> 16), (uint8_t)(display[i] >> 8), (uint8_t)display[i]};
        fwrite(px, 1, 3, f);
    }
    return fclose(f) != 0;
}

/* A typed character as the game receives it: DOS code page 437 (docs/gui.md, "Keyboard"), so the umlauts arrive as
 * their CP437 codes; other non-ASCII characters are dropped. */
static int dos_key(uint32_t c) {
    static const struct { uint32_t uni; int dos; } MAP[] = {{0xE4, 0x84}, {0xF6, 0x94}, {0xFC, 0x81}, {0xC4, 0x8E},
                                                            {0xD6, 0x99}, {0xDC, 0x9A}, {0xDF, 0xE1}};
    if (c >= 0x20 && c < 0x7F) return (int)c;
    for (size_t i = 0; i < sizeof MAP / sizeof *MAP; i++)
        if (MAP[i].uni == c) return MAP[i].dos;
    return -1;
}
/* An SDL key as dos_key_codes takes it (desktop.h); 0 for keys that only type text. */
static int sdl_dos_key(SDL_Keycode k) {
    static const struct { SDL_Keycode sdl; int dk; } MAP[] = {
        {SDLK_RETURN, DK_ENTER}, {SDLK_KP_ENTER, DK_ENTER}, {SDLK_BACKSPACE, DK_BACKSPACE}, {SDLK_ESCAPE, DK_ESCAPE},
        {SDLK_TAB, DK_TAB}, {SDLK_HOME, DK_HOME}, {SDLK_UP, DK_UP}, {SDLK_PAGEUP, DK_PAGEUP}, {SDLK_LEFT, DK_LEFT},
        {SDLK_RIGHT, DK_RIGHT}, {SDLK_END, DK_END}, {SDLK_DOWN, DK_DOWN}, {SDLK_PAGEDOWN, DK_PAGEDOWN},
        {SDLK_INSERT, DK_INSERT}, {SDLK_DELETE, DK_DELETE}, {SDLK_F1, DK_F1}, {SDLK_F2, DK_F1 + 1},
        {SDLK_F3, DK_F1 + 2}, {SDLK_F4, DK_F1 + 3}, {SDLK_F5, DK_F1 + 4}, {SDLK_F6, DK_F1 + 5}, {SDLK_F7, DK_F1 + 6},
        {SDLK_F8, DK_F1 + 7}, {SDLK_F9, DK_F1 + 8}, {SDLK_F10, DK_F1 + 9}};
    for (size_t i = 0; i < sizeof MAP / sizeof *MAP; i++)
        if (MAP[i].sdl == k) return MAP[i].dk;
    if ((k >= 'a' && k <= 'z') || (k >= '0' && k <= '9') || k == '[' || k == '\\' || k == ']' || k == '-') return (int)k;
    return 0;
}

static int write_raw(const IntroScreen *s, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    fprintf(f, "P6\n%d %d\n255\n", s->w, s->h);
    for (int i = 0; i < s->w * s->h; i++) {
        const uint8_t *c = s->dac + 3 * s->px[i];
        uint8_t px[3] = {(uint8_t)(c[0] << 2 | c[0] >> 4), (uint8_t)(c[1] << 2 | c[1] >> 4), (uint8_t)(c[2] << 2 | c[2] >> 4)};
        fwrite(px, 1, 3, f);
    }
    return fclose(f) != 0;
}

/* intro:<program>: opens one program of the start sequence (or the whole of it) for the following steps. */
static int intro_step(Biing *b, const char *arg) {
    if (g_intro) startup_free(&g_start), g_intro = 0;
    memset(&g_in, 0, sizeof g_in);
    if (!strcmp(arg, "start")) startup_open(&g_start, b->assets.root, &b->sound, 0, -1, 1);
    else if (!strcmp(arg, "pre")) startup_open_one(&g_start, b->assets.root, &b->sound, START_PRE, -1, 1);
    else if (!strcmp(arg, "pre:first")) startup_open_one(&g_start, b->assets.root, &b->sound, START_PRE, 0, 1);
    else if (!strcmp(arg, "pre:again")) startup_open_one(&g_start, b->assets.root, &b->sound, START_PRE, 1, 1);
    else if (!strcmp(arg, "relo")) startup_open_one(&g_start, b->assets.root, &b->sound, START_RELO, -1, 1);
    else if (!strcmp(arg, "bint")) startup_open_one(&g_start, b->assets.root, &b->sound, START_BINT, -1, 1);
    else if (!strncmp(arg, "encode", 6)) {
        int scene = 1;
        char speed = 0;
        if (arg[6] == ':' && sscanf(arg + 7, "%d:%c", &scene, &speed) < 1) return -1;
        else if (arg[6] && arg[6] != ':') return -1;
        startup_open_tutorial(&g_start, b->assets.root, &b->sound, scene, speed == 'q' ? 1 : speed == 'd' ? 0 : -1);
    } else return -1;
    g_intro = !startup_done(&g_start);
    if (!g_intro) startup_free(&g_start);
    return 0;
}

/* The steps while a start-sequence program runs; returns 1 if the step was taken, -1 on an error. */
static int intro_headless_step(Biing *b, const char *st) {
    int x, y, n;
    if (sscanf(st, "tick:%d", &n) == 1) {
        intro_units(b, (long)n * startup_tick_units(&g_start));
    } else if (sscanf(st, "wait:%d", &n) == 1) {
        intro_units(b, (long)n * INTRO_U50);
    } else if (sscanf(st, "hold:%d", &n) == 1) { /* the left button held for N ticks of the program's clock */
        g_in.left = 1;
        intro_units(b, (long)n * startup_tick_units(&g_start));
        g_in.left = 0;
    } else if (sscanf(st, "right:%d", &n) == 1) {
        g_in.right = 1;
        intro_units(b, (long)n * INTRO_U50);
        g_in.right = 0;
    } else if (sscanf(st, "move:%d,%d", &x, &y) == 2 || sscanf(st, "click:%d,%d", &x, &y) == 2) {
        int px, py, tutorial = startup_pointer(&g_start, &px, &py) == 0;
        if (!tutorial && st[0] == 'm') return 0; /* the other programs have no pointer: a game step */
        if (tutorial) { /* the tutorial's pointer: the motion as mickeys, read at its next tick */
            g_in.mx += (x - px) * 2, g_in.my += (y - py) * 2; /* adds up until the next poll */
            intro_units(b, startup_tick_units(&g_start));
        }
        if (st[0] == 'c') {
            g_in.left = 1;
            intro_units(b, tutorial ? startup_tick_units(&g_start) : INTRO_U50);
            g_in.left = 0;
        }
    } else if (strncmp(st, "key:", 4) == 0) {
        g_in.key = 1;
        intro_units(b, startup_tick_units(&g_start));
    } else if (strncmp(st, "raw:", 4) == 0) {
        startup_render(&g_start, &g_screen);
        if (write_raw(&g_screen, st + 4)) {
            fprintf(stderr, "[FAIL] cannot write %s\n", st + 4);
            return -1;
        }
    } else {
        return 0;
    }
    return 1;
}

static void step(Biing *b, int held) {
    if (g_control_pump) {
        g_control_pump(b);
        if (b->quit) return;
    }
    biing_interrupt(b, held);
    biing_frame(b);
    g_frames++;
}

static int run_headless_step(Biing *b, const char *arg, const char **wav) {
    int x, y, n;
    if (strncmp(arg, "intro:", 6) == 0) {
        if (intro_step(b, arg + 6)) {
            fprintf(stderr, "[FAIL] unknown program in %s\n", arg);
            return 2;
        }
        return 0;
    }
    if (g_intro) {
        int r = intro_headless_step(b, arg);
        if (r < 0) return 2;
        if (r > 0) return 0;
    }
    if (sscanf(arg, "tick:%d", &n) == 1 || strncmp(arg, "raw:", 4) == 0 ||
        sscanf(arg, "right:%d", &n) == 1) {
        fprintf(stderr, "[FAIL] %s needs a running intro: program (intro:...)\n", arg);
        return 2;
    } else if (sscanf(arg, "move:%d,%d", &x, &y) == 2 || sscanf(arg, "click:%d,%d", &x, &y) == 2) {
        b->ui.mouse_x = x;
        b->ui.mouse_y = y;
        /* the quarter-pixel position the idle blanking compares (docs/gui.md, "Idle blanking"), as the oracle's */
        b->mouse_qx = 4 * SDL_clamp(x, 0, SCREEN_W - 1);
        b->mouse_qy = 4 * SDL_clamp(y, 0, SCREEN_H - 1);
        step(b, 0);
        if (arg[0] == 'c') {
            step(b, 1);
            step(b, 0);
        }
    } else if (sscanf(arg, "cash:%d", &n) == 1) {
        biing_set_cash(b, n);
    } else if (strncmp(arg, "load:", 5) == 0) {
        World w;
        if (world_load_edition(&w, arg + 5, &b->prog)) {
            fprintf(stderr, "[FAIL] %s\n", asset_error());
            return 2;
        }
        world_free(&b->world);
        b->world = w; /* the simulation keeps pointing at b->world */
        sim_reset_generator(&b->sim, 0); /* the generator's start state at every load */
        b->sim.need4 = 0;                /* the need table is the image's after a load (SIM-65) */
    } else if (strncmp(arg, "save:", 5) == 0) {
        if (world_save_edition(&b->world, arg + 5, &b->prog)) {
            fprintf(stderr, "[FAIL] %s\n", asset_error());
            return 2;
        }
    } else if (sscanf(arg, "call:%x", (unsigned *)&n) == 1) {
        b->ui.call(&b->ui, (uint32_t)n);
        step(b, 0);
    } else if (strncmp(arg, "shot:", 5) == 0) { /* an intermediate picture */
        static uint32_t frame[SCREEN_W * DISPLAY_H];
        compose(b, frame);
        if (write_ppm(frame, arg + 5)) {
            fprintf(stderr, "[FAIL] cannot write %s\n", arg + 5);
            return 2;
        }
    } else if (strncmp(arg, "key:", 4) == 0) {
        const char *k = arg + 4;
        if (!strcmp(k, "enter") || !strcmp(k, "back")) {
            b->key = k[0] == 'e' ? 13 : 8;
            step(b, 0);
        } else {
            for (; *k; k++) {
                b->key = (unsigned char)*k;
                step(b, 0);
            }
        }
    } else if (strncmp(arg, "code:", 5) == 0) { /* raw DOS key codes in hex, one per frame */
        const char *k = arg + 5;
        while (*k) {
            char *end;
            unsigned long c = strtoul(k, &end, 16);
            if (end == k || c > 0xFF || (*end && *end != ',')) {
                fprintf(stderr, "[FAIL] bad key code in %s\n", arg);
                return 2;
            }
            b->key = (int)c;
            step(b, 0);
            k = *end ? end + 1 : end;
        }
    } else if (strncmp(arg, "wav:", 4) == 0) {
        *wav = arg + 4;
        sound_capture(&b->sound, 1);
    } else if (sscanf(arg, "wait:%d", &n) == 1) {
        while (n-- > 0) step(b, 0);
    } else {
        fprintf(stderr, "[FAIL] unknown step %s\n", arg);
        return 2;
    }
    return 0;
}

static int run_headless(Biing *b, const char *out, int nsteps, char **steps) {
    const char *wav = NULL;
    step(b, 0);
    for (int i = 0; i < nsteps; i++) {
        int rc = run_headless_step(b, steps[i], &wav);
        if (rc) return rc;
    }
    if (wav) {
        if (sound_write_wav(wav, b->sound.capture, b->sound.capture_frames)) {
            fprintf(stderr, "[FAIL] cannot write %s\n", wav);
            return 2;
        }
        printf("[OK] %s: %.1f s of sound\n", wav, (double)b->sound.capture_frames / MIXER_RATE);
    }
    static uint32_t display[SCREEN_W * DISPLAY_H];
    compose(b, display);
    return write_ppm(display, out);
}

/* The controlled window is a display of the same deterministic batch executor.
 * Physical input is deliberately discarded. It has no real-time clock or audio
 * device; closing it is always serviced, including during long commands. */
static struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    Uint64 next_present;
    uint32_t pixels[SCREEN_W * DISPLAY_H];
} control_window;

static void control_pump(Biing *b) {
    if (!control_window.window) return;
    SDL_Event event;
    while (SDL_PollEvent(&event))
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) b->quit = 1;
    Uint64 now = SDL_GetTicksNS();
    if (now < control_window.next_present) return;
    control_window.next_present = now + INTERRUPT_NS;
    compose(b, control_window.pixels);
    SDL_UpdateTexture(control_window.texture, NULL, control_window.pixels, SCREEN_W * 4);
    SDL_RenderClear(control_window.renderer);
    SDL_RenderTexture(control_window.renderer, control_window.texture, NULL, NULL);
    SDL_RenderPresent(control_window.renderer);
}

/* No blocking stdin worker: EOF and window close cannot leave a reader thread
 * stuck in fgets. Windows control sessions accept pipes or redirected files;
 * POSIX also supports canonical terminal input. -2 is idle, -1 is an I/O error. */
static int control_read(unsigned char *buf, size_t size) {
#ifdef _WIN32
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD type = GetFileType(input), available = 0, got = 0;
    if (type == FILE_TYPE_PIPE) {
        if (!PeekNamedPipe(input, NULL, 0, NULL, &available, NULL))
            return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;
        if (!available) return -2;
        if (size > available) size = available;
    } else if (type != FILE_TYPE_DISK) return -1;
    if (!ReadFile(input, buf, (DWORD)size, &got, NULL))
        return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;
    return (int)got;
#else
    struct pollfd p = {STDIN_FILENO, POLLIN, 0};
    int ready = poll(&p, 1, 0);
    if (ready < 0) return errno == EINTR ? -2 : -1;
    if (!ready) return -2;
    ssize_t got = read(STDIN_FILENO, buf, size);
    if (got < 0) return errno == EINTR || errno == EAGAIN ? -2 : -1;
    return (int)got;
#endif
}

static int control_reply(Biing *b, uint64_t seq, int ready, int window, const char *error) {
    ControlState state = {seq, g_frames, b->ui.current, biing_clock(b, TAIL_DAY), biing_clock(b, TAIL_HOUR),
                          biing_clock(b, TAIL_MINUTE), b->quit, g_intro};
    return control_response(stdout, &state, ready, window, error);
}

static int control_command(Biing *b, ControlLine *line, uint64_t seq, int window) {
    const char *error = line->invalid ? "line exceeds 4096 bytes or contains NUL" : control_validate(line->text, g_intro);
    if (!error && !strcmp(line->text, "quit")) b->quit = 1;
    else if (!error && strcmp(line->text, "status")) {
        const char *unused_wav = NULL;
        if (run_headless_step(b, line->text, &unused_wav)) error = "command failed; see stderr";
    }
    control_window.next_present = 0;
    control_pump(b);
    int rc = control_reply(b, seq, 0, window, error);
    memset(line, 0, sizeof *line);
    return rc;
}

static int run_control(Biing *b, int window) {
    int rc = 0;
#ifndef _WIN32
    void (*old_pipe)(int) = signal(SIGPIPE, SIG_IGN);
#endif
    if (window) {
        if (!SDL_Init(SDL_INIT_VIDEO) ||
            !SDL_CreateWindowAndRenderer("Biing! - paused / API-controlled", SCREEN_W * 2, DISPLAY_H * 2,
                                         SDL_WINDOW_RESIZABLE, &control_window.window, &control_window.renderer)) {
            fprintf(stderr, "[FAIL] controlled window: %s\n", SDL_GetError());
            rc = 2;
            goto done;
        }
        SDL_SetRenderLogicalPresentation(control_window.renderer, SCREEN_W, DISPLAY_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        control_window.texture = SDL_CreateTexture(control_window.renderer, SDL_PIXELFORMAT_ARGB8888,
                                                   SDL_TEXTUREACCESS_STREAMING, SCREEN_W, DISPLAY_H);
        if (!control_window.texture) {
            fprintf(stderr, "[FAIL] controlled texture: %s\n", SDL_GetError());
            rc = 2;
            goto done;
        }
        SDL_SetTextureScaleMode(control_window.texture, SDL_SCALEMODE_NEAREST);
    }
    g_control_pump = control_pump;
    step(b, 0);
    control_window.next_present = 0;
    control_pump(b);
    if (control_reply(b, 0, 1, window, NULL)) { rc = 2; goto done; }
    ControlLine line = {0};
    uint64_t seq = 0;
    while (!b->quit) {
        control_pump(b);
        if (b->quit) break;
        unsigned char input[256];
        int got = control_read(input, sizeof input);
        if (got == -2) { SDL_Delay(10); continue; }
        if (got < 0) { fprintf(stderr, "[FAIL] control stdin read (use a pipe or redirected file)\n"); rc = 2; break; }
        if (!got) {
            if (line.used || line.invalid) {
                control_feed(&line, '\n');
                if (control_command(b, &line, ++seq, window)) rc = 2;
            }
            break;
        }
        for (int i = 0; i < got && !b->quit; i++) {
            if (control_feed(&line, input[i]) && control_command(b, &line, ++seq, window)) {
                rc = 2;
                goto done;
            }
        }
    }
done:
    g_control_pump = NULL;
    if (window) {
        SDL_DestroyTexture(control_window.texture);
        SDL_DestroyRenderer(control_window.renderer);
        SDL_DestroyWindow(control_window.window);
        memset(&control_window, 0, sizeof control_window);
        SDL_Quit();
    }
#ifndef _WIN32
    if (old_pipe != SIG_ERR) signal(SIGPIPE, old_pipe);
#endif
    return rc;
}

static void script_report(const SimScript *script) {
    fprintf(stderr, "rng script: %zu of %zu values used, %zu steps after it ran out\n", script->used, script->n,
            script->extra);
}

/* An invalid diagnostic run must stop at the failing draw, before any later save: or shot: can be written.
 * This is a process-level CLI failure; normal gameplay and the save-tool's counted fallback do not use it. */
static void script_failure(const SimScript *script, const char *message) {
    fprintf(stderr, "[FAIL] %s\n", message);
    script_report(script);
    if (script->trace && fclose((FILE *)script->trace)) fprintf(stderr, "[FAIL] cannot close rng trace\n");
    exit(2);
}

static int run_headless_scripted(Biing *b, const char *out, int nsteps, char **steps) {
    const char *path = getenv("BIING_RNG_SCRIPT"), *trace = getenv("BIING_RNG_TRACE");
    if (!path) {
        if (trace) {
            fprintf(stderr, "[FAIL] BIING_RNG_TRACE requires BIING_RNG_SCRIPT\n");
            return 2;
        }
        return run_headless(b, out, nsteps, steps);
    }
    uint32_t *values = NULL;
    SimScript script = {0};
    if (sim_script_read(path, &values, &script.n)) {
        fprintf(stderr, "[FAIL] cannot read rng script %s (expected hexadecimal 32-bit values)\n", path);
        return 2;
    }
    script.v = values;
    if (trace) {
        script.trace = fopen(trace, "w");
        if (!script.trace) {
            fprintf(stderr, "[FAIL] cannot write rng trace %s\n", trace);
            free(values);
            return 2;
        }
        /* Catch device/full-disk failures at the draw, before the following step can write an output. */
        if (setvbuf((FILE *)script.trace, NULL, _IONBF, 0)) script_failure(&script, "cannot configure rng trace");
    }
    b->sim.rng_script = &script;
    b->sim.rng_failure = script_failure;
    int rc = run_headless(b, out, nsteps, steps);
    b->sim.rng_script = NULL;
    b->sim.rng_failure = NULL;
    script_report(&script);
    if (script.trace && fclose((FILE *)script.trace)) {
        fprintf(stderr, "[FAIL] cannot close rng trace\n");
        rc = 2;
    }
    free(values);
    return rc;
}

/* The start sequence in the window, in real time; returns when the game's turn has come or the window was closed. */
static void run_start_window(Biing *b, SDL_Window *win, SDL_Renderer *ren, SDL_Texture *tex, SDL_AudioStream *audio,
                             uint32_t *display) {
    startup_open(&g_start, b->assets.root, &b->sound, 1, -1, (uint32_t)SDL_GetTicksNS());
    g_intro = !startup_done(&g_start);
    if (!g_intro) {
        startup_free(&g_start);
        return;
    }
    SDL_SetWindowTitle(win, "Biing!");
    memset(&g_in, 0, sizeof g_in);
    Uint64 t0 = SDL_GetTicksNS();
    uint64_t done_units = 0;
    float fx = 0, fy = 0; /* mouse motion not yet handed over, in mickeys */
    while (g_intro && !b->quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            SDL_ConvertEventToRenderCoordinates(ren, &ev);
            if (ev.type == SDL_EVENT_QUIT) b->quit = 1;
            if (ev.type == SDL_EVENT_WINDOW_FOCUS_LOST) g_in.left = g_in.right = 0;
            if (ev.type == SDL_EVENT_MOUSE_MOTION) { /* the tutorial's pointer: 1 mickey = half a display pixel */
                fx += ev.motion.xrel * 2, fy += ev.motion.yrel * 2;
                g_in.mx += (int)fx, g_in.my += (int)fy;
                fx -= (float)(int)fx, fy -= (float)(int)fy;
            }
            if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat) g_in.key = 1; /* any key, Escape included */
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                int down = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                if (ev.button.button == SDL_BUTTON_LEFT) g_in.left = down;
                if (ev.button.button == SDL_BUTTON_RIGHT) g_in.right = down;
            }
        }
        uint64_t target = (SDL_GetTicksNS() - t0) * INTRO_UNITS / 1000000000ull;
        if (target > done_units + INTRO_UNITS / 5) done_units = target - INTRO_UNITS / 5; /* after a stall, lose time */
        while (done_units < target && g_intro) {
            uint64_t before = g_units;
            intro_units(b, 1);
            done_units++;
            if (audio && g_units != before) {
                if (SDL_GetAudioStreamQueued(audio) > MIXER_RATE * 4 / 5) SDL_ClearAudioStream(audio);
                SDL_PutAudioStreamData(audio, b->sound.out, b->sound.out_frames * 4);
            }
        }
        compose(b, display);
        SDL_UpdateTexture(tex, NULL, display, SCREEN_W * 4);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }
    if (g_intro) startup_free(&g_start), g_intro = 0;
    memset(b->sound.out, 0, sizeof b->sound.out);
}

static int run_window(Biing *b, int intro, int inspect) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "[FAIL] SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    /* Sound: the mixer's 21,739 Hz output, one 50 Hz tick at a time; SDL resamples to the device. Without a device
     * the game runs silent. */
    SDL_AudioStream *audio = NULL;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, MIXER_RATE};
        audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (audio) SDL_ResumeAudioStreamDevice(audio);
    }
    if (!audio) fprintf(stderr, "[WARN] no audio device: %s\n", SDL_GetError());
    SDL_Window *win;
    SDL_Renderer *ren;
    int window_scale = inspect ? 1 : 2; /* Leave room beside the companion browser. */
    if (!SDL_CreateWindowAndRenderer("Biing!", SCREEN_W * window_scale, DISPLAY_H * window_scale, SDL_WINDOW_RESIZABLE, &win, &ren)) {
        fprintf(stderr, "[FAIL] window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderLogicalPresentation(ren, SCREEN_W, DISPLAY_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderVSync(ren, 1);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, DISPLAY_H);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    SDL_HideCursor();
    SDL_StartTextInput(win);
    static uint32_t display[SCREEN_W * DISPLAY_H];
    /* MIDI music (SOUND.CFG MCARD ROL or GM): the driver's messages go to the host's synthesizer, or to the output
     * BIING_MIDI_OUT names ("list" prints the outputs, "none" plays none); the window build only (midi_host.h) */
    static MidiAssembler midi_asm;
    MidiHost *midi = NULL;
    const char *midi_out = getenv("BIING_MIDI_OUT");
    if (midi_out && !strcmp(midi_out, "list")) midi_host_list();
    else if (sound_midi_device(b->sound.mcard) >= 0 && (midi = midi_host_open(midi_out))) {
        midi_asm.fn = midi_host_message;
        midi_asm.ctx = midi;
        midi_add_sink(&b->sound.midi, midi_assembler_byte, &midi_asm);
        fprintf(stderr, "[INFO] MIDI music (MCARD %s) on %s\n", b->sound.mcard == 7 ? "GM" : "ROL", midi_host_name(midi));
    }
    if (intro) run_start_window(b, win, ren, tex, audio, display);

    Inspector inspector = {0};
    ControlLine inspector_line = {0};
#ifndef _WIN32
    void (*old_inspector_pipe)(int) = SIG_DFL;
    if (inspect) old_inspector_pipe = signal(SIGPIPE, SIG_IGN);
#endif
    if (inspect && inspector_command(&inspector, b, NULL, stdout)) b->quit = 1;

    int held = 0;
    /* the DOS key queue: the game reads one key per GUI frame (docs/gui.md, "Keyboard"), so an extended key's 0 and
     * its scan code arrive in two successive frames */
    int keys[32], nkeys = 0, drop_text = 0;
    Uint64 next = SDL_GetTicksNS();
    while (!b->quit) {
        if (inspect) {
            /* One bounded read per presentation prevents command floods from starving SDL. */
            unsigned char input[256];
            int got = control_read(input, sizeof input);
            if (got == 0 || got == -1) b->quit = 1;
            for (int i = 0; i < got && !b->quit; i++) {
                if (!control_feed(&inspector_line, input[i])) continue;
                int failed = inspector_line.invalid ?
                    inspector_error(&inspector, b, "Invalid command framing", stdout) :
                    inspector_command(&inspector, b, inspector_line.text, stdout);
                memset(&inspector_line, 0, sizeof inspector_line);
                if (failed) b->quit = 1;
            }
        }
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            SDL_ConvertEventToRenderCoordinates(ren, &ev);
            if (ev.type == SDL_EVENT_QUIT) b->quit = 1;
            /* A release can occur in another application after focus loss. Mouse leave alone
             * must not cancel a hold, since the host can capture a drag outside the window. */
            if (ev.type == SDL_EVENT_WINDOW_FOCUS_LOST) held = 0;
            /* No Escape quit: the original reads keys only in the high-score name field (docs/gui.md,
             * "Keyboard"); the game is left through the desktop's "Spiel abbrechen" or by closing the window. */
            if (ev.type == SDL_EVENT_KEY_DOWN) {
                int code[2], n = dos_key_codes(sdl_dos_key(ev.key.key), (ev.key.mod & SDL_KMOD_CTRL) != 0,
                                               (ev.key.mod & SDL_KMOD_ALT) != 0, (ev.key.mod & SDL_KMOD_SHIFT) != 0, code);
                for (int k = 0; k < n && nkeys < 32; k++) keys[nkeys++] = code[k];
                /* a Ctrl or Alt combination gives its code here; the text it may also produce is not typed */
                if (n && (ev.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT))) drop_text = 1;
            }
            if (ev.type == SDL_EVENT_KEY_UP) drop_text = 0;
            if (ev.type == SDL_EVENT_TEXT_INPUT && !drop_text) {
                /* One host text event can commit several characters. Queue all supported codepoints,
                 * still feeding one DOS key per game frame and respecting the existing queue bound. */
                const char *text = ev.text.text;
                uint32_t cp;
                while (nkeys < 32 && (cp = SDL_StepUTF8(&text, NULL)) != 0) {
                    int c = dos_key(cp);
                    if (c >= 0) keys[nkeys++] = c;
                }
            }
            if (ev.type == SDL_EVENT_MOUSE_MOTION || ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                /* A button event carries its own pointer position, even without an intervening motion event. */
                float x = ev.type == SDL_EVENT_MOUSE_MOTION ? ev.motion.x : ev.button.x;
                float y = ev.type == SDL_EVENT_MOUSE_MOTION ? ev.motion.y : ev.button.y;
                b->ui.mouse_x = SDL_clamp((int)x, 0, SCREEN_W - 1);
                b->ui.mouse_y = SDL_clamp((int)y - BUFFER_Y, 0, SCREEN_H - 1);
                /* quarter pixels for the idle blanking, which clears its count whenever x differs from y (docs/gui.md,
                 * "Idle blanking") */
                b->mouse_qx = SDL_clamp((int)(x * 4), 0, 4 * SCREEN_W - 1);
                b->mouse_qy = SDL_clamp((int)((y - BUFFER_Y) * 4), 0, 4 * SCREEN_H - 1);
                if (ev.type != SDL_EVENT_MOUSE_MOTION && ev.button.button == SDL_BUTTON_LEFT)
                    held = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            }
        }
        /* Pausing freezes simulation and UI actions together. Drop held input and queued
         * keys so an edit cannot accidentally trigger a click when play resumes. */
        if (inspect && inspector.paused) {
            held = 0;
            nkeys = 0;
            b->button_held = b->button_countdown = b->button_repeating = 0;
            if (audio) SDL_ClearAudioStream(audio);
        }
        /* Catch up with the 50 Hz timer, one pass of the main loop after each interrupt. */
        Uint64 now = SDL_GetTicksNS();
        int paused = inspect && inspector.paused;
        for (int n = 0; (paused ? inspector.steps > 0 : now >= next) && n < 10 && !b->quit; n++, next += INTERRUPT_NS) {
            biing_interrupt(b, held);
            if (audio && !paused) {
                /* keep at most about 0.2 s queued, so a stall does not pile up latency */
                if (SDL_GetAudioStreamQueued(audio) > MIXER_RATE * 4 / 5) SDL_ClearAudioStream(audio);
                SDL_PutAudioStreamData(audio, b->sound.out, b->sound.out_frames * 4);
            }
            b->key = nkeys ? keys[0] : -1; /* one key per frame */
            if (nkeys) memmove(keys, keys + 1, (size_t)--nkeys * sizeof *keys);
            biing_frame(b);
            if (inspect) inspector.frames++;
            if (paused) inspector.steps--;
        }
        if (paused) next = now + INTERRUPT_NS;
        if (now >= next) next = now + INTERRUPT_NS; /* after a stall, lose time like the game does */

        char title[128];
        snprintf(title, sizeof title, "Biing!%s - day %d, %02d:%02d - screen %d - mouse %d,%d",
                 paused ? " [PAUSED]" : "", biing_clock(b, TAIL_DAY),
                 biing_clock(b, TAIL_HOUR), biing_clock(b, TAIL_MINUTE), b->ui.current, b->ui.mouse_x, b->ui.mouse_y);
        SDL_SetWindowTitle(win, title);
        compose(b, display);
        SDL_UpdateTexture(tex, NULL, display, SCREEN_W * 4);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
    }
    if (midi) { /* the program end reaches the synthesizer before it closes */
        sound_midi_finish(&b->sound);
        midi_remove_sink(&b->sound.midi, midi_assembler_byte, &midi_asm);
        midi_host_close(midi);
    }
    if (audio) SDL_DestroyAudioStream(audio);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
#ifndef _WIN32
    if (inspect) signal(SIGPIPE, old_inspector_pipe);
#endif
    return 0;
}

int view_main(const char *root); /* viewer.c */

static int usage(const char *prog) {
    fprintf(stderr, "usage: %s <BIING!-dir> [--no-intro] [--fixes] [--inspect] | --view | --shot <out.ppm> [step ...] | --control [--window]\n", prog);
    return 2;
}

int main(int argc, char **argv) {
    if (argc < 2) return usage(argv[0]);
    if (argc >= 3 && SDL_strcmp(argv[2], "--view") == 0) return view_main(argv[1]);
    int shot = argc >= 4 && SDL_strcmp(argv[2], "--shot") == 0, intro = 1, all_fixes = 0;
    int control = 0, control_visible = 0, inspect = 0;
    /* the window build's options, in any order after the game folder (docs/fixes.md, section 1) */
    for (int i = 2; !shot && i < argc; i++) {
        if (SDL_strcmp(argv[i], "--no-intro") == 0) intro = 0;
        else if (SDL_strcmp(argv[i], "--fixes") == 0) all_fixes = 1;
        else if (SDL_strcmp(argv[i], "--control") == 0) control = 1;
        else if (SDL_strcmp(argv[i], "--window") == 0) control_visible = 1;
        else if (SDL_strcmp(argv[i], "--inspect") == 0) inspect = 1;
        else return usage(argv[0]);
    }
    if ((control_visible && !control) || (inspect && control)) return usage(argv[0]);
    if (inspect) intro = 0;
    /* BIING_FIXES / --fixes (docs/fixes.md): the window build always names the fixes on stderr; a --shot run only
     * when a fix is on, so the default headless output stays as it was (tools/test_manual.py wants it empty) */
    if (fixes_init(all_fixes, !shot && !control)) return 2;
    static Biing b;
    if (biing_open(&b, argv[1])) {
        fprintf(stderr, "[FAIL] %s\n", asset_error());
        return 1;
    }
    load_cursor(&b);
    int rc = control ? run_control(&b, control_visible) :
             shot ? run_headless_scripted(&b, argv[3], argc - 4, argv + 4) : run_window(&b, intro, inspect);
    if (g_intro) startup_free(&g_start);
    biing_close(&b);
    return rc;
}
