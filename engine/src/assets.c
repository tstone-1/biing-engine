#include "assets.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char g_error[512];

int asset_fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_error, sizeof g_error, fmt, ap);
    va_end(ap);
    return -1;
}

const char *asset_error(void) { return g_error; }

static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }
static uint32_t be32(const uint8_t *p) { return be16(p) << 16 | be16(p + 2); }
static uint32_t le32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int blob_read(const char *path, Blob *out) {
    out->data = NULL;
    out->size = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return asset_fail("cannot open %s", path);
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return asset_fail("cannot seek %s", path);
    }
    long n = ftell(f);
    if (n < 0) {
        fclose(f);
        return asset_fail("cannot size %s", path);
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return asset_fail("cannot seek %s", path);
    }
    out->data = malloc(n > 0 ? (size_t)n : 1);
    if (!out->data || fread(out->data, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        blob_free(out);
        return asset_fail("cannot read %s", path);
    }
    fclose(f);
    out->size = (size_t)n;
    return 0;
}

void blob_free(Blob *b) {
    free(b->data);
    b->data = NULL;
    b->size = 0;
}

void game_path(const Game *g, const char *rel, char *buf, size_t n) { snprintf(buf, n, "%s/%s", g->root, rel); }

static int is_file(const char *dir, const char *name, int want_dir) {
    char path[1200];
    struct stat st;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    return stat(path, &st) == 0 && (want_dir ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode));
}
static int is_game_folder(const char *dir) { return is_file(dir, "BIPRO.EXE", 0) && is_file(dir, "DATA", 1); }

/* A folder that is not the game folder (the user's commonest mistake, MANUAL.md): say so, and name the game folder
 * when it is one or two levels further down (the disk zip's top folder or its BIING folder). */
static int not_a_game_folder(const char *root) {
    static const char *below[] = {"BIING!", "BIING/BIING!", "Biing! Diskversion/BIING/BIING!"};
    for (size_t i = 0; i < sizeof below / sizeof *below; i++) {
        char sub[1200];
        snprintf(sub, sizeof sub, "%s/%s", root, below[i]);
        if (is_game_folder(sub))
            return asset_fail("%s: not a BIING! game folder (no BIPRO.EXE or DATA); the game folder is %s", root, sub);
    }
    return asset_fail("%s: not a BIING! game folder (no BIPRO.EXE or DATA)", root);
}

int game_open(Game *g, const char *root) {
    memset(g, 0, sizeof *g);
    snprintf(g->root, sizeof g->root, "%s", root);
    char path[1200];
    Blob b;
    if (!is_game_folder(root)) return not_a_game_folder(root);

    game_path(g, "DATA/COL", path, sizeof path);
    if (blob_read(path, &b)) return -1;
    if (b.size != 768) {
        blob_free(&b);
        return asset_fail("%s: %zu bytes, expected 768", path, b.size);
    }
    memcpy(g->palette, b.data, 768);
    blob_free(&b);

    game_path(g, "DATA/IXY", path, sizeof path);
    if (blob_read(path, &b)) return -1;
    if (b.size != IXY_COUNT * 8) {
        blob_free(&b);
        return asset_fail("%s: %zu bytes, expected %d", path, b.size, IXY_COUNT * 8);
    }
    for (int i = 0; i < IXY_COUNT; i++) {
        const uint8_t *p = b.data + i * 8;
        g->ixy[i] = (IxyEntry){(uint16_t)be16(p), (uint16_t)be16(p + 2), (uint16_t)be16(p + 4), (uint16_t)be16(p + 6)};
    }
    blob_free(&b);
    return 0;
}

/* ---- images ---------------------------------------------------------------------------------- */

int image_rel_path(int index, char *buf, size_t n) {
    static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (index < 0 || index >= 3600) return asset_fail("image index %d out of range", index);
    snprintf(buf, n, "DATA/%c/%d/%d", digits[index / 100], index / 10 % 10, index % 10);
    return 0;
}

/* A span number: every 0xFF adds 255 and continues. Bound each addition to the remaining row space;
 * -1 means truncated, 1 means too wide. Even a long extension chain cannot overflow the accumulator. */
static int rle_number(const uint8_t *b, size_t n, size_t *i, int limit, int *out) {
    int total = 0;
    for (;;) {
        if (*i >= n) return -1;
        int part = b[(*i)++];
        if (part > limit - total) return 1;
        total += part;
        if (part != 0xFF) {
            *out = total;
            return 0;
        }
    }
}

/* mask (optional): 1 for every pixel a span wrote, 0 for the gaps - a span's colour-0 byte is drawn, a gap is not
 * (docs/screens.md, "Image blitter"). */
static int decode_rle(const uint8_t *b, size_t n, int w, int h, uint8_t *out, uint8_t *mask) {
    memset(out, 0, (size_t)w * h);
    if (mask) memset(mask, 0, (size_t)w * h);
    size_t i = 0;
    for (int y = 0; y < h; y++) {
        int x = 0;
        for (;;) {
            int count, gap;
            int rc = rle_number(b, n, &i, w - x, &count);
            if (rc) return asset_fail("RLE row %d: %s", y, rc < 0 ? "data ends" : "span past width or data");
            if (count == 0) break;
            rc = rle_number(b, n, &i, w - x - count, &gap);
            if (rc) return asset_fail("RLE row %d: %s", y, rc < 0 ? "data ends" : "span past width or data");
            if ((size_t)count > n - i) return asset_fail("RLE row %d: span past width or data", y);
            x += gap;
            memcpy(out + (size_t)y * w + x, b + i, (size_t)count);
            if (mask) memset(mask + (size_t)y * w + x, 1, (size_t)count);
            i += (size_t)count;
            x += count;
        }
    }
    if (i != n) return asset_fail("RLE: %zu bytes left after %d rows", n - i, h);
    return 0;
}

int image_decode(const uint8_t *raw, size_t n, int w, int h, uint8_t *out, uint16_t *flags) {
    return image_decode_mask(raw, n, w, h, out, NULL, flags);
}

int image_decode_mask(const uint8_t *raw, size_t n, int w, int h, uint8_t *out, uint8_t *mask, uint16_t *flags) {
    if (n < 4) return asset_fail("image shorter than its header");
    if (w < 0 || h < 0 || (h && (size_t)w > SIZE_MAX / (size_t)h))
        return asset_fail("invalid image dimensions %dx%d", w, h);
    uint16_t flag = (uint16_t)be16(raw);
    if (flags) *flags = flag;
    const uint8_t *body = raw + 4;
    size_t len = n - 4;
    switch (flag) {
    case IMG_RAW:
    case IMG_RAW_40:
        if (len != (size_t)w * h) return asset_fail("raw image: %zu bytes for %dx%d", len, w, h);
        memcpy(out, body, len);
        return 0;
    case IMG_RLE:
        return decode_rle(body, len, w, h, out, mask);
    default:
        return asset_fail("unknown image flag 0x%04x", flag);
    }
}

int image_load(const Game *g, int index, Image *img) {
    memset(img, 0, sizeof *img);
    if (index < 0 || index >= IXY_COUNT || !g->ixy[index].w) return asset_fail("no IXY entry for image %d", index);
    char rel[32], path[1200];
    image_rel_path(index, rel, sizeof rel);
    game_path(g, rel, path, sizeof path);
    Blob b;
    if (blob_read(path, &b)) return -1;
    const IxyEntry *e = &g->ixy[index];
    *img = (Image){index, e->w, e->h, e->x, e->y, 0, malloc((size_t)e->w * e->h), NULL};
    int rle = b.size >= 2 && be16(b.data) == IMG_RLE;
    if (rle) img->mask = malloc((size_t)e->w * e->h);
    int rc = img->px && (!rle || img->mask)
                 ? image_decode_mask(b.data, b.size, e->w, e->h, img->px, img->mask, &img->flags)
                 : asset_fail("out of memory");
    blob_free(&b);
    if (rc) {
        image_free(img);
        char cause[sizeof g_error];
        memcpy(cause, g_error, sizeof cause); /* the message must not be formatted from itself */
        return asset_fail("image %d: %s", index, cause);
    }
    return 0;
}

void image_free(Image *img) {
    free(img->px);
    free(img->mask);
    img->px = NULL;
    img->mask = NULL;
}

/* ---- BITEXPZ -------------------------------------------------------------------------------- */

void unscramble_sum(uint8_t *buf, size_t n) {
    uint8_t key = 0x2C;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = buf[i];
        buf[i] = (uint8_t)(c - key);
        key = (uint8_t)(key + c);
    }
}

int textdb_parse(TextDb *db, Blob file) {
    memset(db, 0, sizeof *db);
    db->file = file;
    const size_t table = TEXT_COUNT * 4;
    if (file.size < 4) return asset_fail("text file too short");
    uint32_t n = le32(file.data);
    if (4 + table + n != file.size) return asset_fail("text file: 4 + %zu + %u != %zu", table, n, file.size);
    uint8_t *t = file.data + 4, *text = t + table;
    unscramble_sum(t, table);
    unscramble_sum(text, n);
    for (int i = 0; i < TEXT_COUNT; i++) {
        db->offs[i] = le32(t + 4 * i);
        if (db->offs[i] > n || (i && db->offs[i] < db->offs[i - 1])) return asset_fail("text offset %d out of order", i);
    }
    db->text = text;
    db->text_size = n;
    return 0;
}

int textdb_load(TextDb *db, const char *path) {
    Blob b;
    if (blob_read(path, &b)) return -1;
    if (textdb_parse(db, b)) {
        textdb_free(db);
        return -1;
    }
    return 0;
}

static const uint8_t *strip_terminator(const uint8_t *p, size_t *len) {
    while (*len && p[*len - 1] == 0) (*len)--;
    if (*len && p[*len - 1] == '$') (*len)--;
    return p;
}

const uint8_t *textdb_entry(const TextDb *db, int i, size_t *len) {
    if (i < 0 || i >= TEXT_COUNT) {
        *len = 0;
        return (const uint8_t *)"";
    }
    uint32_t end = i + 1 < TEXT_COUNT ? db->offs[i + 1] : db->text_size;
    *len = end - db->offs[i];
    return strip_terminator(db->text + db->offs[i], len);
}

const uint8_t *textdb_text(const TextDb *db, int i, size_t *len) {
    *len = 0;
    if (i < 0 || i >= TEXT_COUNT) return (const uint8_t *)"";
    const uint8_t *p = db->text + db->offs[i];
    size_t max = db->text_size - db->offs[i];
    while (*len < max && p[*len] != '$' && p[*len] != 0) (*len)++;
    return p;
}

const uint8_t *textdb_raw(const TextDb *db, int i, size_t *len) {
    *len = 0;
    if (i < 0 || i >= TEXT_COUNT) return (const uint8_t *)"";
    uint32_t end = i + 1 < TEXT_COUNT ? db->offs[i + 1] : db->text_size;
    *len = end - db->offs[i];
    return db->text + db->offs[i];
}

void textdb_free(TextDb *db) {
    blob_free(&db->file);
    db->text = NULL;
}

/* ---- BIDATP --------------------------------------------------------------------------------- */

void unscramble_ror(uint8_t *buf, size_t n) {
    for (size_t i = 0; i < n; i++) {
        uint8_t v = (uint8_t)(buf[i] - 0x4D);
        buf[i] = (uint8_t)(v >> 3 | v << 5);
    }
}

int datadb_parse(DataDb *db, Blob file) {
    /* Block order: A, B, then per pool its text (size from the header) and its offset table. */
    static const uint32_t tables[6] = {0xFC4, 0x400, 0xFC4, 0x1044, 0x1044, 0x1044};
    memset(db, 0, sizeof *db);
    db->file = file;
    if (file.size < 24) return asset_fail("BIDATP too short");
    size_t pos = 24;
    const size_t fixed = (size_t)PATIENT_COUNT * PATIENT_SIZE + (size_t)STAFF_COUNT * STAFF_SIZE;
    size_t total = pos + fixed;
    for (int k = 0; k < 6; k++) total += le32(file.data + 4 * k) + tables[k];
    if (total != file.size) return asset_fail("BIDATP: layout covers %zu bytes, file has %zu", total, file.size);
    unscramble_ror(file.data + pos, file.size - pos);
    db->patients = file.data + pos;
    pos += (size_t)PATIENT_COUNT * PATIENT_SIZE;
    db->staff = file.data + pos;
    pos += (size_t)STAFF_COUNT * STAFF_SIZE;
    for (int k = 0; k < 6; k++) {
        TextPool *p = &db->pools[k];
        p->text = file.data + pos;
        p->text_size = le32(file.data + 4 * k);
        pos += p->text_size;
        p->table = file.data + pos;
        p->count = (int)(tables[k] / 4);
        pos += tables[k];
        for (int i = 0; i < p->count; i++)
            if (le32(p->table + 4 * i) > p->text_size || (i && le32(p->table + 4 * i) < le32(p->table + 4 * i - 4)))
                return asset_fail("BIDATP pool %d: offset %d out of order", k, i);
    }
    return 0;
}

int datadb_load(DataDb *db, const char *path) {
    Blob b;
    if (blob_read(path, &b)) return -1;
    if (datadb_parse(db, b)) {
        datadb_free(db);
        return -1;
    }
    return 0;
}

const uint8_t *datadb_pool_entry(const DataDb *db, int pool, int i, size_t *len) {
    const TextPool *p = &db->pools[pool - DAT_PATIENT_NAMES];
    if (i < 0 || i >= p->count) {
        *len = 0;
        return (const uint8_t *)"";
    }
    uint32_t a = le32(p->table + 4 * i);
    uint32_t b = i + 1 < p->count ? le32(p->table + 4 * i + 4) : (uint32_t)p->text_size;
    *len = b - a;
    return strip_terminator(p->text + a, len);
}

void datadb_free(DataDb *db) { blob_free(&db->file); }

/* ---- fonts ---------------------------------------------------------------------------------- */

int font_parse(Font *f, const uint8_t *raw, size_t n) {
    memset(f, 0, sizeof *f);
    if (n < 16 + 256 || raw[0] != 'P' || raw[1] != 'C') return asset_fail("not a PC font");
    f->height = (int)be16(raw + 2);
    /* BI2PC.FNT has u32 offsets: its first offset then points just past a 512-byte table. */
    int wide = n >= 20 && be32(raw + 16) == 16 + 128 * 4;
    for (int k = 0; k < 128; k++) {
        size_t o = wide ? be32(raw + 16 + 4 * k) : be16(raw + 16 + 2 * k);
        if (!o) continue;
        if (o > n || n - o < 2) {
            asset_fail("glyph %d offset past the end", k);
            goto fail;
        }
        int w = raw[o + 1]; /* the code reads the width's low byte only (docs/gui.md, "Unchecked slots") */
        size_t size = (size_t)w * f->height;
        if (size > n - o - 2) {
            asset_fail("glyph %d runs past the end", k);
            goto fail;
        }
        uint8_t *g = malloc(size ? size : 1);
        if (!g) {
            asset_fail("out of memory");
            goto fail;
        }
        for (int x = 0; x < w; x++)
            for (int y = 0; y < f->height; y++) g[(size_t)y * w + x] = raw[o + 2 + (size_t)x * f->height + y];
        f->width[k] = w;
        f->glyph[k] = g;
    }
    /* the header glyph of the slots whose offset is 0 */
    f->head_w = raw[1], f->head_h = raw[3];
    f->head = malloc((size_t)f->head_w * f->head_h + 1);
    if (!f->head) {
        asset_fail("out of memory");
        goto fail;
    }
    for (int x = 0; x < f->head_w; x++)
        for (int y = 0; y < f->head_h; y++) {
            size_t at = 2 + (size_t)x * f->head_h + y;
            f->head[(size_t)y * f->head_w + x] = at < n ? raw[at] : 0;
        }
    return 0;
fail:
    /* Failed parses release glyphs completed before the error; callers can still safely free the font. */
    font_free(f);
    return -1;
}

int font_load(Font *f, const char *path) {
    Blob b;
    if (blob_read(path, &b)) return -1;
    int rc = font_parse(f, b.data, b.size);
    blob_free(&b);
    return rc;
}

void font_free(Font *f) {
    for (int k = 0; k < 128; k++) free(f->glyph[k]);
    free(f->head);
    memset(f, 0, sizeof *f);
}
