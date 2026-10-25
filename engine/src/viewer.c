/* biing <BIING!-dir> --view: image browser and font sheets, a research aid next to the game.
 *   Tab         image browser / font sheets
 *   Left/Right  previous/next image or font (Shift: 10 images)
 *   Esc         quit
 * The window title shows the mouse position in buffer coordinates; a click prints it to stdout. */
#include <SDL3/SDL.h>
#include <stdio.h>

#include "assets.h"
#include "gfx.h"

static const char *FONT_FILES[] = {"KU.FNT", "XFONT3.FNT", "ZEI1.FNT", "ZEI2.FNT",
                                   "ZEI3.FNT", "ZEI4.FNT", "ZEI5.FNT", "N/BI2PC.FNT"};
#define FONTS ((int)(sizeof FONT_FILES / sizeof *FONT_FILES))

typedef struct {
    Game game;
    Surface screen;
    int fonts_view;
    int image, font;
    char status[256];
} Viewer;

static void draw_image(Viewer *v) {
    surface_clear(&v->screen, 0);
    Image img;
    if (image_load(&v->game, v->image, &img)) {
        snprintf(v->status, sizeof v->status, "%s", asset_error());
        return;
    }
    blit(&v->screen, &img, img.x, img.y, image_is_transparent(&img));
    snprintf(v->status, sizeof v->status, "image %d  %dx%d at %d,%d  flags 0x%04x", v->image, img.w, img.h,
             img.x, img.y, img.flags);
    image_free(&img);
}

static void draw_fonts(Viewer *v) {
    surface_clear(&v->screen, 0);
    char rel[64], path[1200];
    snprintf(rel, sizeof rel, "DATA/%s", FONT_FILES[v->font]);
    game_path(&v->game, rel, path, sizeof path);
    Font f;
    if (font_load(&f, path)) {
        snprintf(v->status, sizeof v->status, "%s", asset_error());
        return;
    }
    /* Slots in rows of 16: slot = row * 16 + column. */
    int cell = f.height + 6;
    for (int slot = 0; slot < 128; slot++) draw_glyph(&v->screen, &f, slot, 8 + slot % 16 * (cell + 12), 8 + slot / 16 * cell);
    snprintf(v->status, sizeof v->status, "font %s, height %d", FONT_FILES[v->font], f.height);
    font_free(&f);
}

static void step_image(Viewer *v, int delta) {
    int dir = delta > 0 ? 1 : -1, i = v->image + delta;
    while (i >= 0 && i < IXY_COUNT && !v->game.ixy[i].w) i += dir;
    if (i >= 0 && i < IXY_COUNT) v->image = i;
}

int view_main(const char *root) {
    static Viewer v;
    if (game_open(&v.game, root)) {
        fprintf(stderr, "[FAIL] %s\n", asset_error());
        return 1;
    }
    v.image = 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    SDL_Window *win;
    SDL_Renderer *ren;
    if (!SDL_CreateWindowAndRenderer("Biing! viewer", SCREEN_W * 2, SCREEN_H * 2, SDL_WINDOW_RESIZABLE, &win, &ren))
        return 1;
    SDL_SetRenderLogicalPresentation(ren, SCREEN_W, SCREEN_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    static uint32_t rgb[SCREEN_W * SCREEN_H];

    draw_image(&v);
    int running = 1, mx = 0, my = 0;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            SDL_ConvertEventToRenderCoordinates(ren, &ev);
            if (ev.type == SDL_EVENT_QUIT) running = 0;
            if (ev.type == SDL_EVENT_MOUSE_MOTION) {
                mx = (int)ev.motion.x;
                my = (int)ev.motion.y;
            }
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) printf("click %d,%d (%s)\n", mx, my, v.status);
            if (ev.type != SDL_EVENT_KEY_DOWN) continue;
            int step = (ev.key.mod & SDL_KMOD_SHIFT) ? 10 : 1;
            if (ev.key.key == SDLK_ESCAPE) running = 0;
            if (ev.key.key == SDLK_TAB) v.fonts_view = !v.fonts_view;
            if (ev.key.key == SDLK_RIGHT) {
                if (v.fonts_view) v.font = (v.font + 1) % FONTS;
                else step_image(&v, step);
            }
            if (ev.key.key == SDLK_LEFT) {
                if (v.fonts_view) v.font = (v.font + FONTS - 1) % FONTS;
                else step_image(&v, -step);
            }
            if (v.fonts_view) draw_fonts(&v);
            else draw_image(&v);
        }
        char title[320];
        snprintf(title, sizeof title, "Biing! viewer - %s - mouse %d,%d", v.status, mx, my);
        SDL_SetWindowTitle(win, title);
        surface_to_rgb(&v.screen, v.game.palette, rgb);
        SDL_UpdateTexture(tex, NULL, rgb, SCREEN_W * 4);
        SDL_RenderClear(ren);
        SDL_RenderTexture(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
