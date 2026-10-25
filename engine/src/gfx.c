#include "gfx.h"

#include <stdio.h>
#include <string.h>

void surface_clear(Surface *s, uint8_t colour) {
    s->w = SCREEN_W;
    s->h = SCREEN_H;
    memset(s->px, colour, sizeof s->px);
}

/* mask (RLE images): only pixels with mask 1 are drawn, colour 0 included; without a mask, transparent skips colour 0
 * (flag 0x0040) and otherwise every pixel is copied (flag 0x0000). docs/screens.md, "Image blitter". */
static void blit_pixels(Surface *s, const uint8_t *px, const uint8_t *mask, int w, int h, int x, int y,
                        int transparent) {
    for (int row = 0; row < h; row++) {
        int sy = y + row;
        if (sy < 0 || sy >= s->h) continue;
        const uint8_t *src = px + (size_t)row * w, *m = mask ? mask + (size_t)row * w : NULL;
        uint8_t *dst = s->px + (size_t)sy * s->w;
        for (int col = 0; col < w; col++) {
            int sx = x + col;
            if (sx < 0 || sx >= s->w) continue;
            if (transparent && (m ? !m[col] : !src[col])) continue;
            dst[sx] = src[col];
        }
    }
}

void blit(Surface *s, const Image *img, int x, int y, int transparent) {
    blit_pixels(s, img->px, img->flags == IMG_RLE ? img->mask : NULL, img->w, img->h, x, y, transparent);
}

int image_is_transparent(const Image *img) { return img->flags != IMG_RAW; }

/* A glyph goes into the frame buffer by its linear position, as the original's (one 640-wide buffer, no clip on the
 * right): a column past x 639 lands at the start of the next row (measured with the oracle 2026-10-20, a name of
 * 67-pixel glyphs typed past the screen edge; docs/gui.md, "Text input field"). */
static void blit_glyph(Surface *s, const uint8_t *px, int w, int h, int x, int y) {
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++) {
            long sx = (long)x + col, at = (long)(y + row) * s->w + sx;
            if (sx < 0 || at < 0 || at >= (long)s->w * s->h || !px[(size_t)row * w + col]) continue;
            s->px[at] = px[(size_t)row * w + col];
        }
}

int glyph_width(const Font *f, int slot) {
    if (slot < 0 || slot >= 128) return 0; /* past the offset table: engine choice, nothing (docs/gui.md) */
    return f->glyph[slot] ? f->width[slot] : f->head ? f->head_w : 0;
}

int draw_glyph(Surface *s, const Font *f, int slot, int x, int y) {
    if (slot < 0 || slot >= 128) return 0;
    if (!f->glyph[slot]) { /* offset 0: the font's header drawn as a glyph */
        if (!f->head) return 0;
        blit_glyph(s, f->head, f->head_w, f->head_h, x, y);
        return f->head_w;
    }
    if (!f->width[slot]) return 0;
    blit_glyph(s, f->glyph[slot], f->width[slot], f->height, x, y);
    return f->width[slot];
}

/* The game loads COL >> 2 into the 6-bit VGA DAC once at start (docs/formats.md); the screen shows that value
 * expanded back to 8 bits (the expansion itself is a guess, at most 1 off). */
uint8_t dac_level(uint8_t c) {
    uint8_t v = c >> 2;
    return (uint8_t)(v << 2 | v >> 4);
}

uint32_t palette_rgb(const uint8_t palette[768], int i) {
    return 0xFF000000u | (uint32_t)dac_level(palette[3 * i]) << 16 | (uint32_t)dac_level(palette[3 * i + 1]) << 8 |
           dac_level(palette[3 * i + 2]);
}

void surface_to_rgb(const Surface *s, const uint8_t palette[768], uint32_t *out) {
    uint32_t lut[256];
    for (int i = 0; i < 256; i++) lut[i] = palette_rgb(palette, i);
    for (int i = 0; i < s->w * s->h; i++) out[i] = lut[s->px[i]];
}

static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v; }
static void be16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8), p[1] = (uint8_t)v; }

int pbm_write(const char *path, const Surface *s, int h, const uint8_t palette[768]) {
    if (h > s->h) h = s->h;
    uint32_t body = (uint32_t)(s->w * h), pad = body & 1;
    uint8_t head[12 + 8 + 20 + 8 + 768 + 8];
    memcpy(head, "FORM", 4);
    be32(head + 4, 4 + 8 + 20 + 8 + 768 + 8 + body + pad);
    memcpy(head + 8, "PBM ", 4);
    uint8_t *b = head + 12;
    memcpy(b, "BMHD", 4), be32(b + 4, 20);
    memset(b + 8, 0, 20);
    be16(b + 8, (unsigned)s->w), be16(b + 10, (unsigned)h); /* width, height; x, y 0 */
    b[16] = 8; /* planes; masking 0, compression 0, pad 0, transparent colour 0 */
    b[22] = 5, b[23] = 6; /* the 320 x 200 aspect Deluxe Paint writes for 640 x 400 */
    be16(b + 24, (unsigned)s->w), be16(b + 26, (unsigned)h); /* page size */
    b += 28;
    memcpy(b, "CMAP", 4), be32(b + 4, 768);
    for (int i = 0; i < 768; i++) b[8 + i] = (uint8_t)(palette[i] & 0xFC); /* the DAC's 6-bit values (COL >> 2) x 4 */
    b += 8 + 768;
    memcpy(b, "BODY", 4), be32(b + 4, body);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(head, 1, sizeof head, f) == sizeof head && fwrite(s->px, 1, body, f) == body &&
             (!pad || fputc(0, f) == 0);
    return fclose(f) == 0 && ok ? 0 : -1;
}
