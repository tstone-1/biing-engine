/* Exercise the real main/--shot runner with a synthetic scene. Only the game opening/frame callbacks are
 * replaced; RNG parsing, ranged draws, generator resets, save/load and all shot step handling stay real. */
#undef fopen
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "game.h"
#include "portable.h"
#undef startup_open
#undef startup_advance
#include "startup.h"

int shot_main(int argc, char **argv);
#undef main

int biing_open(Biing *b, const char *root) {
    memset(b, 0, sizeof *b);
    (void)root;
    world_alloc(&b->world);
    sim_init(&b->sim, &b->world, &b->prog, 0);
    /* Restore uses the real office-entry renderer. Supply its normal surface/UI
     * dependencies; absent synthetic image entries are handled by the real loader. */
    b->screen.w = SCREEN_W;
    b->screen.h = SCREEN_H;
    dulog_init(&b->ui, &b->assets, &b->prog, &b->screen, NULL, b);
    return 0;
}

static unsigned frames;
static const char *window_text;
static int window_expected[32], window_count, window_seen, window_failed, window_case;
static int mouse_test, mouse_case, mouse_held, mouse_failed, mouse_completed;
static unsigned mouse_step;
static SDL_WindowID mouse_window;
static int focus_test, focus_intro, focus_failed, focus_completed;
static unsigned focus_step;
static SDL_WindowID focus_window;
static const struct { SDL_EventType type; Uint8 button; int left, right; } focus_steps[] = {
    {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 1, 0},
    {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, 1, 1},
    {SDL_EVENT_WINDOW_MOUSE_LEAVE, 0, 1, 1}, /* leaving the window is not losing keyboard focus */
    {SDL_EVENT_WINDOW_FOCUS_LOST, 0, 0, 0},
    {SDL_EVENT_WINDOW_FOCUS_GAINED, 0, 0, 0},
    {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 1, 0},
    {SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, 0, 0},
    {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, 0, 1},
    {SDL_EVENT_WINDOW_FOCUS_LOST, 0, 0, 0},
    {SDL_EVENT_WINDOW_FOCUS_GAINED, 0, 0, 0},
    {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, 0, 1},
    {SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_RIGHT, 0, 0},
};

/* Observe both real window event loops at their consumer boundary. No missing button-up
 * is supplied by the fixture; subsequent presses prove focus loss did not disable input. */
static int focus_frame(int left, int right) {
    if (!focus_window) {
        int count;
        SDL_Window **windows = SDL_GetWindows(&count);
        if (!windows || count != 1) exit(1);
        focus_window = SDL_GetWindowID(windows[0]);
        SDL_free(windows);
    }
    if (SDL_HasEvents(SDL_EVENT_WINDOW_FIRST, SDL_EVENT_WINDOW_LAST) ||
        SDL_HasEvents(SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_UP)) return 0;
    if (focus_step) {
        unsigned prev = focus_step - 1;
        int want_right = focus_intro ? focus_steps[prev].right : 0;
        if (left != focus_steps[prev].left || right != want_right) {
            fprintf(stderr, "[FAIL] focus %s step %u: buttons %d,%d expected %d,%d\n",
                    focus_intro ? "intro" : "game", prev, left, right, focus_steps[prev].left, want_right);
            focus_failed = 1;
        }
    }
    if (focus_step == sizeof focus_steps / sizeof *focus_steps) { focus_completed = 1; return 1; }
    SDL_Event event = {0};
    event.type = focus_steps[focus_step].type;
    if (focus_steps[focus_step].button) {
        event.button.windowID = focus_window;
        event.button.button = focus_steps[focus_step].button;
        event.button.down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.x = 600;
        event.button.y = 440;
    } else event.window.windowID = focus_window;
    if (!SDL_PushEvent(&event)) exit(1);
    focus_step++;
    return 0;
}

int shot_test_startup_open(Startup *s, const char *root, struct Sound *sound, int write_marker, int marker, uint32_t seed) {
    if (!focus_intro) return startup_open(s, root, sound, write_marker, marker, seed);
    memset(s, 0, sizeof *s);
    s->plan[0] = START_PRE;
    s->nplan = 1; /* unopened synthetic stage renders black, with no game files or marker writes */
    return 0;
}

void shot_test_startup_advance(Startup *s, IntroInput *in) {
    if (!focus_intro) { startup_advance(s, in); return; }
    if (focus_frame(in->left, in->right)) { s->at = s->nplan; s->quit = 1; }
}
static const struct { int w, h; float scale, left, top; } mouse_sizes[] = {
    {1280, 960, 2, 0, 0},       /* initial double-size display */
    {1280, 720, 1.5f, 160, 0},  /* bars on the left and right */
    {640, 960, 1, 0, 240},      /* bars above and below */
    {960, 720, 1.5f, 0, 0},     /* fractional scaling, no bars */
};
static const struct {
    SDL_EventType type;
    Uint8 button;
    float x, y; /* logical display coordinates; the game buffer starts at y=20 */
    int px, py, qx, qy, held;
} mouse_steps[] = {
    {SDL_EVENT_MOUSE_MOTION, 0, 100.25f, 120.75f, 100, 100, 401, 403, 0},
    {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 300.5f, 220.25f, 300, 200, 1202, 801, 1},
    {SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, 400.75f, 320.5f, 400, 300, 1603, 1202, 0},
    {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, 200, 170, 200, 150, 800, 600, 0},
    {SDL_EVENT_MOUSE_MOTION, 0, -5, -10, 0, 0, 0, 0, 0},
    {SDL_EVENT_MOUSE_MOTION, 0, 670, 500, 639, 439, 2559, 1759, 0},
};

static void mouse_frame(Biing *b) {
    if (frames == 1) {
        int count, w, h;
        SDL_Window **windows = SDL_GetWindows(&count);
        if (!windows || count != 1 || !SDL_SetWindowSize(windows[0], mouse_sizes[mouse_case].w,
                                                       mouse_sizes[mouse_case].h) ||
            !SDL_GetWindowSize(windows[0], &w, &h) || w != mouse_sizes[mouse_case].w || h != mouse_sizes[mouse_case].h) {
            fprintf(stderr, "[FAIL] could not establish mouse-test window size: %s\n", SDL_GetError());
            exit(1);
        }
        mouse_window = SDL_GetWindowID(windows[0]);
        SDL_free(windows);
    }
    /* Allow the real event loop to process the resize and each event before observing it.
     * Timer catch-up can call this callback several times before polling again. Check the
     * queue without consuming events, rather than assuming a frame count means delivery.
     * Expected coordinates are fixed above; no inverse SDL coordinate conversion supplies them. */
    if (frames < 4 || SDL_HasEvents(SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_UP)) return;
    unsigned step = mouse_step;
    if (step) {
        unsigned prev = step - 1;
        if (b->ui.mouse_x != mouse_steps[prev].px || b->ui.mouse_y != mouse_steps[prev].py ||
            b->mouse_qx != mouse_steps[prev].qx || b->mouse_qy != mouse_steps[prev].qy ||
            mouse_held != mouse_steps[prev].held) {
            fprintf(stderr, "[FAIL] mouse size %d step %u: pixel %d,%d quarter %d,%d held %d; expected %d,%d %d,%d %d\n",
                    mouse_case, prev, b->ui.mouse_x, b->ui.mouse_y, b->mouse_qx, b->mouse_qy, mouse_held,
                    mouse_steps[prev].px, mouse_steps[prev].py, mouse_steps[prev].qx, mouse_steps[prev].qy,
                    mouse_steps[prev].held);
            mouse_failed = 1;
        }
    }
    if (step == sizeof mouse_steps / sizeof *mouse_steps) { mouse_completed = 1; b->quit = 1; return; }
    float x = mouse_steps[step].x * mouse_sizes[mouse_case].scale + mouse_sizes[mouse_case].left;
    float y = mouse_steps[step].y * mouse_sizes[mouse_case].scale + mouse_sizes[mouse_case].top;
    SDL_Event event = {0};
    event.type = mouse_steps[step].type;
    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        event.motion.windowID = mouse_window;
        event.motion.x = x;
        event.motion.y = y;
    } else {
        event.button.windowID = mouse_window;
        event.button.button = mouse_steps[step].button;
        event.button.down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.x = x;
        event.button.y = y;
    }
    if (!SDL_PushEvent(&event)) {
        fprintf(stderr, "[FAIL] could not queue mouse event: %s\n", SDL_GetError());
        exit(1);
    }
    mouse_step++;
}

static void push_text(const char *text) {
    SDL_Event event = {0};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = text;
    if (!SDL_PushEvent(&event)) {
        fprintf(stderr, "[FAIL] could not queue text event: %s\n", SDL_GetError());
        exit(1);
    }
}

static void push_key(SDL_EventType type, SDL_Keycode key, SDL_Keymod mod) {
    SDL_Event event = {0};
    event.type = type;
    event.key.key = key;
    event.key.mod = mod;
    if (!SDL_PushEvent(&event)) {
        fprintf(stderr, "[FAIL] could not queue key event: %s\n", SDL_GetError());
        exit(1);
    }
}

/* The production SDL event loop consumes these strings. Dummy video/audio keep this a test
 * of event translation and queue delivery, without exercising or controlling the host desktop. */
static void window_frame(Biing *b) {
    if (frames == 1) {
        if (window_case == 5) {
            push_key(SDL_EVENT_KEY_DOWN, SDLK_A, SDL_KMOD_CTRL);
            push_text("a"); /* the text paired with Ctrl+A must be suppressed */
            push_key(SDL_EVENT_KEY_UP, SDLK_A, SDL_KMOD_NONE);
            push_text("B\xC3\xA4");
            push_key(SDL_EVENT_KEY_DOWN, SDLK_UP, SDL_KMOD_NONE);
            push_key(SDL_EVENT_KEY_DOWN, SDLK_RETURN, SDL_KMOD_NONE);
        } else if (window_case == 6) {
            push_key(SDL_EVENT_KEY_DOWN, SDLK_A, SDL_KMOD_NONE);
            push_text("a"); /* printable key-down must not duplicate committed text */
            push_key(SDL_EVENT_KEY_UP, SDLK_A, SDL_KMOD_NONE);
        } else {
            push_text(window_text);
            if (!*window_text) push_text("C");
        }
        return;
    }
    if (b->key >= 0) {
        if (window_seen >= window_count || b->key != window_expected[window_seen]) {
            fprintf(stderr, "[FAIL] text byte %d: received %d, expected %d\n", window_seen,
                    b->key, window_seen < window_count ? window_expected[window_seen] : -1);
            window_failed = 1;
        }
        window_seen++;
    }
    if (frames >= 40) b->quit = 1;
}
static Uint32 close_window(void *unused, SDL_TimerID timer, Uint32 interval) {
    (void)unused; (void)timer; (void)interval;
    SDL_Event event = {0};
    event.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&event);
    return 0;
}
void biing_close(Biing *b) {
    const char *close = getenv("BIING_TEST_CONTROL_CLOSE");
    if (close && (!b->quit || (!strcmp(close, "idle") ? frames != 1 : frames >= 30001))) {
        fprintf(stderr, "[FAIL] window close was not serviced: quit=%d frames=%u\n", b->quit, frames);
        exit(1);
    }
    world_free(&b->world);
}
void biing_interrupt(Biing *b, int held) { (void)b; mouse_held = held; }
void biing_frame(Biing *b) {
    frames++;
    if (focus_test) {
        b->quit = focus_frame(mouse_held, 0);
        return;
    }
    if (mouse_test) {
        mouse_frame(b);
        return;
    }
    if (window_text) {
        window_frame(b);
        return;
    }
    const char *close = getenv("BIING_TEST_CONTROL_CLOSE");
    if (close) {
        if (frames == 1) SDL_AddTimer(100, close_window, NULL);
        if (!strcmp(close, "busy")) SDL_Delay(1);
    }
    /* A non-power-of-two range proves rejection draws count as raw steps, not as one frame. */
    world_set_cash(&b->world, 0, sim_random_site(&b->sim, 10, 0x1234));
}
int view_main(const char *root) { (void)root; return 2; }

FILE *shot_test_fopen(const char *path, const char *mode) {
    /* A portable write-error seam: return an existing read-only stream for this trace destination.
     * This exercises the real fprintf failure path without requiring Linux's /dev/full. */
    return fopen(path, strstr(path, "readonly-trace") ? "r" : mode);
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "window-focus")) {
        focus_test = 1;
        for (int i = 0; i < 2; i++) {
            focus_intro = i;
            focus_window = 0;
            focus_step = 0;
            focus_completed = 0;
            frames = 0;
            char *args[] = {argv[0], "synthetic", "--no-intro"};
            if (shot_main(i ? 2 : 3, args)) return 1;
            if (!focus_completed) { fprintf(stderr, "[FAIL] focus sequence stopped early\n"); focus_failed = 1; }
        }
        if (!focus_failed) puts("[PASS] Game and intro release buttons on focus loss and accept new input");
        return focus_failed;
    }
    if (argc == 2 && !strcmp(argv[1], "window-mouse")) {
        mouse_test = 1;
        for (size_t i = 0; i < sizeof mouse_sizes / sizeof *mouse_sizes; i++) {
            mouse_case = (int)i;
            frames = 0;
            mouse_held = 0;
            mouse_step = 0;
            mouse_completed = 0;
            char *args[] = {argv[0], "synthetic", "--no-intro"};
            if (shot_main(3, args)) return 1;
            if (!mouse_completed) {
                fprintf(stderr, "[FAIL] mouse size %zu stopped early after %u frames\n", i, frames);
                mouse_failed = 1;
            }
        }
        if (!mouse_failed) puts("[PASS] SDL mouse scaling, letterboxing, quarter pixels, button positions and clamps");
        return mouse_failed;
    }
    if (argc == 2 && !strcmp(argv[1], "window-input")) {
        static const struct { const char *text; int keys[32]; int count; } cases[] = {
            {"Anna", {'A', 'n', 'n', 'a'}, 4},
            {"J\xC3\xB6rg \xC3\xA4\xC3\xB6\xC3\xBC\xC3\x84\xC3\x96\xC3\x9C\xC3\x9F!",
             {'J', 0x94, 'r', 'g', ' ', 0x84, 0x94, 0x81, 0x8E, 0x99, 0x9A, 0xE1, '!'}, 13},
            {"\xE2\x82\xAC" "A\xF0\x9F\x98\x80" "B", {'A', 'B'}, 2},
            {"", {'C'}, 1},
            {"0123456789012345678901234567890123456789",
             {'0','1','2','3','4','5','6','7','8','9','0','1','2','3','4','5','6','7','8','9',
              '0','1','2','3','4','5','6','7','8','9','0','1'}, 32},
            {"modifiers", {1, 'B', 0x84, 0, 0x48, 13}, 6},
            {"plain-key", {'a'}, 1},
        };
        for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
            window_text = cases[i].text;
            memcpy(window_expected, cases[i].keys, sizeof window_expected);
            window_count = cases[i].count;
            window_case = (int)i;
            window_seen = 0;
            frames = 0;
            char *args[] = {argv[0], "synthetic", "--no-intro"};
            if (shot_main(3, args)) return 1;
            if (window_seen != window_count) {
                fprintf(stderr, "[FAIL] text case %zu delivered %d of %d characters\n",
                        i, window_seen, window_count);
                window_failed = 1;
            }
        }
        if (!window_failed) puts("[PASS] SDL text strings: ASCII, CP437, unsupported Unicode, empty, queue limit and modifiers");
        return window_failed;
    }
    if (argc == 4 && !strcmp(argv[1], "check")) {
        World w;
        if (world_load(&w, argv[2])) return 1;
        long cash = world_cash(&w, 0), expected = strtol(argv[3], NULL, 10);
        world_free(&w);
        if (cash != expected) fprintf(stderr, "[FAIL] saved draw %ld, expected %ld\n", cash, expected);
        return cash != expected;
    }
    return shot_main(argc, argv);
}
