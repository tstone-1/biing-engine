/* 8-bit frame buffer and blitting. The game draws in 640 x 400 with one 256-colour palette. */
#ifndef BIING_GFX_H
#define BIING_GFX_H

#include "assets.h"

typedef struct {
    int w, h;
    uint8_t px[SCREEN_W * SCREEN_H];
} Surface;

void surface_clear(Surface *s, uint8_t colour);
/* Draws img with its top-left corner at x, y, clipped to the surface. With transparent set, an RLE image with a span
 * mask draws every span pixel (colour 0 included) and leaves its gaps; any other image leaves its colour-0 pixels
 * (docs/screens.md, "Image blitter", SCR-6). Without transparent every pixel is copied. */
void blit(Surface *s, const Image *img, int x, int y, int transparent);
/* Whether an image is drawn with transparency: 0x0040 (colour 0) and RLE (the gaps); 0x0000 is opaque. */
int image_is_transparent(const Image *img);
/* Draws one glyph; colour 0 in the glyph is transparent. Returns the glyph width (0 if missing). */
int draw_glyph(Surface *s, const Font *f, int slot, int x, int y);
/* The width draw_glyph draws for a slot: the glyph's, the header glyph's for a slot whose offset is 0 (docs/gui.md,
 * "Unchecked slots"), 0 for a slot of 128 or more (past the offset table; engine choice). */
int glyph_width(const Font *f, int slot);
/* Converts the surface to 0xFFRRGGBB pixels with the 8-bit palette, as the 6-bit DAC shows it. */
void surface_to_rgb(const Surface *s, const uint8_t palette[768], uint32_t *out);
uint8_t dac_level(uint8_t c);
uint32_t palette_rgb(const uint8_t palette[768], int i);
/* An IFF FORM PBM (Deluxe Paint PC chunky bitmap, docs/formats.md) of the first h rows of the surface: BMHD (w x h, 8
 * planes, no compression), CMAP (the DAC's 6-bit values x 4), BODY (rows as they are). The German CD's `*` screenshot
 * (docs/editions.md 7.2, OTH-DE-12). 0 or -1 when the file cannot be written. */
int pbm_write(const char *path, const Surface *s, int h, const uint8_t palette[768]);

#endif
