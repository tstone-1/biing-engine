/* Synthetic fixtures only - no bytes from the game files (see AGENTS.md). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "portable.h"
#include "assets.h"
#include "gfx.h"
#include "text.h"

static int failures;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                 \
        }                                                               \
    } while (0)

static void put_le32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

/* Inverses of the two scrambles, so fixtures can be written in plain text. */
static void scramble_sum(uint8_t *buf, size_t n) {
    uint8_t key = 0x2C;
    for (size_t i = 0; i < n; i++) {
        buf[i] = (uint8_t)(buf[i] + key);
        key = (uint8_t)(key + buf[i]);
    }
}

static void scramble_ror(uint8_t *buf, size_t n) {
    for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)((uint8_t)(buf[i] << 3 | buf[i] >> 5) + 0x4D);
}

static void test_raw_image(void) {
    const uint8_t raw[] = {0x00, 0x40, 0, 0, 1, 2, 3, 4, 5, 6};
    uint8_t out[6];
    uint16_t flags;
    CHECK(image_decode(raw, sizeof raw, 3, 2, out, &flags) == 0);
    CHECK(flags == IMG_RAW_40);
    CHECK(memcmp(out, raw + 4, 6) == 0);
    CHECK(image_decode(raw, sizeof raw, 3, 3, out, NULL) != 0); /* size mismatch */
}

static void test_rle_image(void) {
    /* Row 0: gap 1, two pixels; row 1: empty. */
    const uint8_t raw[] = {0x00, 0x80, 0, 0, 2, 1, 7, 8, 0, 0};
    uint8_t out[8];
    CHECK(image_decode(raw, sizeof raw, 4, 2, out, NULL) == 0);
    const uint8_t want[8] = {0, 7, 8, 0, 0, 0, 0, 0};
    CHECK(memcmp(out, want, 8) == 0);
}

static void test_rle_ff_escape(void) {
    /* One row of 300 px: count FF 2D = 300, gap 0, then 300 pixel bytes. */
    static uint8_t raw[4 + 3 + 300 + 1];
    raw[1] = 0x80;
    raw[4] = 0xFF;
    raw[5] = 45;
    raw[6] = 0;
    for (int i = 0; i < 300; i++) raw[7 + i] = (uint8_t)(i % 250 + 1);
    static uint8_t out[300];
    CHECK(image_decode(raw, sizeof raw, 300, 1, out, NULL) == 0);
    CHECK(out[0] == 1 && out[299] == (uint8_t)(299 % 250 + 1));
}

static void test_rle_rejects_overflow(void) {
    const uint8_t raw[] = {0x00, 0x80, 0, 0, 3, 2, 1, 1, 1, 0};
    uint8_t out[8] = {0, 0, 0, 0, 0xAA};
    CHECK(image_decode(raw, sizeof raw, 4, 1, out, NULL) != 0);
    CHECK(out[4] == 0xAA);
}

/* Encode the documented FF continuation rule, including FF 00 and two successive FF bytes. */
static size_t span_number(uint8_t *out, int value) {
    size_t n = 0;
    while (value >= 255) out[n++] = 255, value -= 255;
    out[n++] = (uint8_t)value;
    return n;
}

static void test_rle_exact_width(void) {
    static const int spans[][2] = {{1, 0}, {254, 0}, {255, 0}, {256, 0}, {510, 0}, {1, 255}, {1, 510}, {255, 255}};
    for (size_t k = 0; k < sizeof spans / sizeof *spans; k++) {
        int count = spans[k][0], gap = spans[k][1], width = count + gap;
        uint8_t raw[521] = {0, 0x80, 0, 0}, out[512], mask[512];
        size_t n = 4;
        n += span_number(raw + n, count);
        n += span_number(raw + n, gap);
        for (int i = 0; i < count; i++) raw[n++] = (uint8_t)(i % 251); /* includes drawn colour 0 */
        raw[n++] = 0; /* a full row still needs its count-0 terminator, with no gap byte */
        memset(out, 0xAA, sizeof out), memset(mask, 0xBB, sizeof mask);
        CHECK(image_decode_mask(raw, n, width, 1, out, mask, NULL) == 0);
        for (int x = 0; x < width; x++) {
            CHECK(out[x] == (x < gap ? 0 : (uint8_t)((x - gap) % 251)));
            CHECK(mask[x] == (x < gap ? 0 : 1));
        }
        CHECK(out[width] == 0xAA && mask[width] == 0xBB);
    }
    const uint8_t raw[] = {0, 0x80, 0, 0, 2, 1, 0, 7, 1, 1, 9, 0, 0};
    uint8_t out[10], mask[10]; /* two spans fill row 0 exactly; row 1 is empty */
    const uint8_t want[] = {0, 0, 7, 0, 9, 0, 0, 0, 0, 0}, marks[] = {0, 1, 1, 0, 1, 0, 0, 0, 0, 0};
    CHECK(image_decode_mask(raw, sizeof raw, 5, 2, out, mask, NULL) == 0);
    CHECK(memcmp(out, want, sizeof want) == 0 && memcmp(mask, marks, sizeof marks) == 0);
}

static void test_rle_incomplete_and_excess(void) {
    static const struct {
        uint8_t body[10];
        size_t size;
        int width, written;
        uint8_t pixels[2];
        const char *error;
    } cases[] = {
        {{3, 0, 1, 2, 3, 0}, 6, 2, 0, {0}, "span past width or data"}, /* count > width */
        {{1, 3, 7, 0}, 4, 3, 0, {0}, "span past width or data"}, /* gap reaches the edge without room for count */
        {{2, 2, 7, 8, 0}, 5, 3, 0, {0}, "span past width or data"}, /* sum exceeds width */
        {{0xFF, 0xFF}, 2, 300, 0, {0}, "span past width or data"}, /* reject the second extension before a terminator */
        {{1, 0xFF, 0xFF}, 3, 300, 0, {0}, "span past width or data"},
        {{2, 0, 5, 6, 2}, 5, 3, 2, {5, 6}, "span past width or data"}, /* count exceeds remaining space before gap */
        {{2, 0, 5, 6, 1, 1, 7, 0}, 8, 3, 2, {5, 6}, "span past width or data"},
        {{0}, 0, 3, 0, {0}, "data ends"},
        {{0xFF}, 1, 300, 0, {0}, "data ends"}, /* unfinished count */
        {{1}, 1, 3, 0, {0}, "data ends"}, /* missing gap */
        {{1, 0xFF}, 2, 300, 0, {0}, "data ends"}, /* unfinished gap */
        {{3, 0, 7, 8}, 4, 3, 0, {0}, "span past width or data"}, /* incomplete pixel data is never copied */
        {{1, 0, 7}, 3, 1, 1, {7}, "data ends"}, /* missing row terminator */
        {{0, 9}, 2, 3, 0, {0}, "bytes left"},
    };
    for (size_t k = 0; k < sizeof cases / sizeof *cases; k++) {
        uint8_t raw[521] = {0, 0x80, 0, 0}, out[304], mask[304];
        memcpy(raw + 4, cases[k].body, cases[k].size);
        memset(out, 0xAA, sizeof out), memset(mask, 0xBB, sizeof mask);
        CHECK(image_decode_mask(raw, 4 + cases[k].size, cases[k].width, 1, out, mask, NULL) == -1);
        CHECK(strstr(asset_error(), cases[k].error) != NULL);
        for (int x = 0; x < cases[k].width; x++) {
            CHECK(out[x] == (x < cases[k].written ? cases[k].pixels[x] : 0));
            CHECK(mask[x] == (x < cases[k].written ? 1 : 0));
        }
        for (size_t x = (size_t)cases[k].width; x < sizeof out; x++) CHECK(out[x] == 0xAA && mask[x] == 0xBB);
    }
}

static void test_image_dimensions(void) {
    uint8_t raw[] = {0, 0x80, 0, 0, 0, 0}, out = 0xAA, mask = 0xBB;
    static const int invalid[][2] = {{-1, 0}, {0, -1}, {-1, -1}};
    for (size_t k = 0; k < sizeof invalid / sizeof *invalid; k++) {
        CHECK(image_decode_mask(raw, 4, invalid[k][0], invalid[k][1], &out, &mask, NULL) == -1);
        CHECK(strstr(asset_error(), "invalid image dimensions") != NULL);
        CHECK(out == 0xAA && mask == 0xBB);
    }
    CHECK(image_decode_mask(raw, sizeof raw, 0, 2, &out, &mask, NULL) == 0);
    CHECK(image_decode_mask(raw, 4, 3, 0, &out, &mask, NULL) == 0);
    raw[1] = 0;
    CHECK(image_decode_mask(raw, 4, 0, 0, &out, &mask, NULL) == 0);
    CHECK(out == 0xAA && mask == 0xBB);
}

static void test_textdb(void) {
    const char *plain[] = {"Hallo$", "", "Lümmel$"};
    size_t text_n = 0, offs[3];
    uint8_t text[64];
    for (int i = 0; i < 3; i++) {
        offs[i] = text_n;
        memcpy(text + text_n, plain[i], strlen(plain[i]));
        text_n += strlen(plain[i]);
    }
    size_t size = 4 + TEXT_COUNT * 4 + text_n;
    Blob b = {calloc(1, size), size};
    put_le32(b.data, (uint32_t)text_n);
    for (int i = 0; i < TEXT_COUNT; i++) put_le32(b.data + 4 + 4 * i, (uint32_t)(i < 3 ? offs[i] : text_n));
    memcpy(b.data + 4 + TEXT_COUNT * 4, text, text_n);
    scramble_sum(b.data + 4, TEXT_COUNT * 4);
    scramble_sum(b.data + 4 + TEXT_COUNT * 4, text_n);

    TextDb db;
    CHECK(textdb_parse(&db, b) == 0);
    size_t n;
    const uint8_t *e = textdb_entry(&db, 0, &n);
    CHECK(n == 5 && memcmp(e, "Hallo", 5) == 0);
    textdb_entry(&db, 1, &n);
    CHECK(n == 0);
    e = textdb_entry(&db, 2, &n);
    CHECK(n == strlen("Lümmel") && memcmp(e, "Lümmel", n) == 0);
    textdb_entry(&db, TEXT_COUNT - 1, &n);
    CHECK(n == 0);
    e = textdb_text(&db, 0, &n); /* up to the '$' */
    CHECK(n == 5 && memcmp(e, "Hallo", 5) == 0);
    textdb_free(&db);

    /* an entry without '$' runs on into the next one when the game copies it */
    const char *two = "ab" "cd$";
    size_t size2 = 4 + TEXT_COUNT * 4 + 5;
    Blob b2 = {calloc(1, size2), size2};
    put_le32(b2.data, 5);
    for (int i = 1; i < TEXT_COUNT; i++) put_le32(b2.data + 4 + 4 * i, i == 1 ? 2 : 5);
    memcpy(b2.data + 4 + TEXT_COUNT * 4, two, 5);
    scramble_sum(b2.data + 4, TEXT_COUNT * 4);
    scramble_sum(b2.data + 4 + TEXT_COUNT * 4, 5);
    CHECK(textdb_parse(&db, b2) == 0);
    e = textdb_entry(&db, 0, &n);
    CHECK(n == 2 && memcmp(e, "ab", 2) == 0);
    e = textdb_text(&db, 0, &n);
    CHECK(n == 4 && memcmp(e, "abcd", 4) == 0);
    textdb_free(&db);
}

static void test_datadb(void) {
    static const uint32_t tables[6] = {0xFC4, 0x400, 0xFC4, 0x1044, 0x1044, 0x1044};
    const size_t fixed = (size_t)PATIENT_COUNT * PATIENT_SIZE + (size_t)STAFF_COUNT * STAFF_SIZE;
    size_t size = 24 + fixed + 3; /* pool 3 (staff names) holds "Bo$" */
    for (int k = 0; k < 6; k++) size += tables[k];
    Blob b = {calloc(1, size), size};
    put_le32(b.data + 12, 3);
    uint8_t *p = b.data + 24;
    p[0] = 1; /* patient 0, byte 0 */
    p[PATIENT_COUNT * PATIENT_SIZE + STAFF_SIZE + 8] = 42; /* staff 1, byte 8 */
    p += fixed;
    for (int k = 0; k < 6; k++) {
        if (k == 3) {
            memcpy(p, "Bo$", 3);
            p += 3;
            /* entry 0 = "Bo$", all later entries start at the end */
            for (uint32_t i = 1; i < tables[k] / 4; i++) put_le32(p + 4 * i, 3);
        }
        p += tables[k];
    }
    scramble_ror(b.data + 24, size - 24);

    DataDb db;
    CHECK(datadb_parse(&db, b) == 0);
    CHECK(db.patients[0] == 1);
    CHECK(db.staff[STAFF_SIZE + 8] == 42);
    CHECK(db.pools[DAT_STAFF_NAMES - DAT_PATIENT_NAMES].count == 1041);
    size_t n;
    const uint8_t *e = datadb_pool_entry(&db, DAT_STAFF_NAMES, 0, &n);
    CHECK(n == 2 && memcmp(e, "Bo", 2) == 0);
    datadb_pool_entry(&db, DAT_STAFF_NAMES, 1, &n);
    CHECK(n == 0);
    datadb_free(&db);
}

static void test_datadb_rejects_wrong_size(void) {
    Blob b = {calloc(1, 100), 100};
    DataDb db;
    CHECK(datadb_parse(&db, b) != 0);
    datadb_free(&db);
}

static void test_font_column_major(void) {
    /* height 2, glyph in slot 1, width 3, columns (1,2) (3,4) (5,6) -> rows 1 3 5 / 2 4 6 */
    uint8_t raw[16 + 256 + 2 + 6] = {'P', 'C', 0, 2};
    raw[16 + 2] = 0x01;
    raw[16 + 3] = 0x10; /* slot 1 at 0x110 = 272 */
    raw[273] = 3;
    for (int i = 0; i < 6; i++) raw[274 + i] = (uint8_t)(i + 1);
    Font f;
    CHECK(font_parse(&f, raw, sizeof raw) == 0);
    CHECK(f.height == 2 && f.width[1] == 3 && f.width[0] == 0);
    const uint8_t want[6] = {1, 3, 5, 2, 4, 6};
    CHECK(f.glyph[1] && memcmp(f.glyph[1], want, 6) == 0);
    font_free(&f);
}

static void font_offset(uint8_t *raw, int wide, int slot, uint32_t value) {
    int bytes = wide ? 4 : 2;
    for (int i = 0; i < bytes; i++) raw[16 + bytes * slot + i] = (uint8_t)(value >> (8 * (bytes - 1 - i)));
}

static void test_font_bounds(void) {
    for (int wide = 0; wide < 2; wide++) {
        uint8_t raw[544] = {'P', 'C', 0, 2};
        size_t at = wide ? 530 : 272, n = at + 8;
        if (wide) font_offset(raw, 1, 0, 528); /* wide discriminator; glyph 0 has width 0 */
        font_offset(raw, wide, 1, (uint32_t)at);
        raw[at] = 0xA5, raw[at + 1] = 3; /* only the width's low byte counts */
        for (int i = 0; i < 6; i++) raw[at + 2 + (size_t)i] = (uint8_t)(i + 1);
        Font f;
        const uint8_t want[] = {1, 3, 5, 2, 4, 6};
        CHECK(font_parse(&f, raw, n) == 0); /* last glyph ends exactly at n */
        CHECK(f.height == 2 && f.width[1] == 3 && f.glyph[1] && memcmp(f.glyph[1], want, sizeof want) == 0);
        font_free(&f);
        CHECK(font_parse(&f, raw, n - 1) == -1);
        CHECK(strstr(asset_error(), "glyph 1 runs past the end") != NULL);
        font_free(&f);
        /* Physical padding keeps a regressed guard's reads bounded, while n remains the actual file extent. */
        for (size_t bad = n - 1; bad <= n + 1; bad++) {
            font_offset(raw, wide, 1, (uint32_t)bad);
            raw[bad + 1] = 0;
            CHECK(font_parse(&f, raw, n) == -1);
            CHECK(strstr(asset_error(), "glyph 1 offset past the end") != NULL);
            font_free(&f);
        }
        font_offset(raw, wide, 1, (uint32_t)(n - 2));
        raw[n - 2] = 0xA5, raw[n - 1] = 0;
        CHECK(font_parse(&f, raw, n) == 0); /* zero-width glyph still requires both width bytes */
        CHECK(f.glyph[1] && f.width[1] == 0 && glyph_width(&f, 1) == 0);
        font_free(&f);
        font_offset(raw, wide, 1, 0);
        CHECK(font_parse(&f, raw, n) == 0); /* offset 0 is the header alias, distinct from width 0 */
        CHECK(!f.glyph[1] && glyph_width(&f, 1) == 67 && f.head_h == 2);
        CHECK(f.head && f.head[0] == 0 && f.head[67] == 2);
        font_free(&f);
        raw[3] = 0;
        font_offset(raw, wide, 1, (uint32_t)at);
        raw[at + 1] = 3;
        CHECK(font_parse(&f, raw, at + 2) == 0); /* zero height consumes no pixel bytes */
        CHECK(f.height == 0 && f.glyph[1] && f.width[1] == 3 && f.head_h == 0);
        font_free(&f);
    }
    uint8_t raw[530] = {'P', 'C', 0, 2};
    Font f;
    CHECK(font_parse(&f, raw, 272) == 0); /* complete narrow table, every slot aliases the header */
    font_free(&f);
    CHECK(font_parse(&f, raw, 271) == -1);
    font_free(&f);
    font_offset(raw, 1, 0, 528);
    for (size_t n = 527; n < sizeof raw; n++) {
        CHECK(font_parse(&f, raw, n) == -1); /* wide table/header truncation is caught at glyph 0 */
        CHECK(strstr(asset_error(), "glyph 0 offset past the end") != NULL);
        font_free(&f);
    }
    CHECK(font_parse(&f, raw, sizeof raw) == 0);
    CHECK(f.glyph[0] && f.width[0] == 0);
    font_free(&f);
}

static void test_font_offset_wrap(void) {
    uint8_t raw[530] = {'P', 'C', 0, 2};
    font_offset(raw, 1, 0, 528);
    font_offset(raw, 1, 1, UINT32_MAX);
    Font f;
    /* In the former uint32 arithmetic this offset's +1/+2 wrapped into the header and was accepted. */
    CHECK(font_parse(&f, raw, sizeof raw) == -1);
    CHECK(strstr(asset_error(), "glyph 1 offset past the end") != NULL);
    font_free(&f);
}

static void test_font_failure_cleanup(void) {
    uint8_t raw[280] = {'P', 'C', 0, 2};
    font_offset(raw, 0, 1, 272);
    raw[273] = 3; /* valid first glyph, allocated before either later failure */
    for (int size_error = 0; size_error < 2; size_error++) {
        font_offset(raw, 0, 2, size_error ? 278 : 280);
        raw[279] = 1; /* width fits, but its two pixels do not */
        Font f;
        CHECK(font_parse(&f, raw, sizeof raw) == -1);
        CHECK(strstr(asset_error(), size_error ? "glyph 2 runs past the end" : "glyph 2 offset past the end") != NULL);
        CHECK(f.height == 0 && f.head == NULL && f.head_w == 0 && f.head_h == 0);
        for (int k = 0; k < 128; k++) CHECK(f.glyph[k] == NULL && f.width[k] == 0);
        font_free(&f); /* cleanup remains safe after a failed parse */
    }
}

/* docs/gui.md, "Unchecked slots" and "Plain" (INP-2): a slot whose offset is 0 draws the font's header as a glyph
 * (width = byte 1, 67 for "PC"; column height = byte 3; the bytes from +2 on, column by column, 0 transparent); the
 * width of every glyph is the byte at offset + 1; the plain renderer draws every byte but 0, '$' and CR through the
 * character map, control codes included. */
static void test_header_glyph(void) {
    uint8_t raw[16 + 256 + 2 + 6] = {'P', 'C', 0, 2};
    raw[16 + 2] = 0x01, raw[16 + 3] = 0x10; /* slot 1 at 0x110 */
    raw[272] = 0x01, raw[273] = 3; /* width 0x0103: the code takes the low byte, 3 */
    for (int i = 0; i < 6; i++) raw[274 + i] = 7;
    Font f;
    CHECK(font_parse(&f, raw, sizeof raw) == 0);
    CHECK(f.width[1] == 3);
    CHECK(glyph_width(&f, 5) == 67 && glyph_width(&f, 1) == 3 && glyph_width(&f, 127) == 67);
    CHECK(glyph_width(&f, 128) == 0 && glyph_width(&f, 129) == 0); /* past the table: engine choice */
    CHECK(f.head_w == 67 && f.head_h == 2);
    static Surface s;
    surface_clear(&s, 9);
    CHECK(draw_glyph(&s, &f, 5, 10, 10) == 67);
    /* column 0 = bytes +2, +3 = (0, 2): 0 transparent, 2 drawn; column 8 = bytes +18, +19, slot 1's offset 01 10 */
    CHECK(s.px[10 * SCREEN_W + 10] == 9 && s.px[11 * SCREEN_W + 10] == 2);
    CHECK(s.px[10 * SCREEN_W + 18] == 1 && s.px[11 * SCREEN_W + 18] == 0x10);
    CHECK(s.px[10 * SCREEN_W + 11] == 9 && s.px[11 * SCREEN_W + 77] == 9); /* zero bytes, and past the 67 columns */
    /* the plain renderer: 0x01 -> slot 5 (header glyph, pen 67 + 1), 'A' -> slot 1 (3 + 1), CR a new line, 0x0A ->
     * slot 1 again, then '$' ends */
    uint8_t map[256];
    memset(map, 0, sizeof map);
    map[1] = 5, map['A'] = 1, map[0x0A] = 1, map[0x15] = 129;
    TextLayout l = {&f, map, 100, 50, 640, 0, 4, 1};
    surface_clear(&s, 9);
    const uint8_t t[] = {0x01, 'A', 0x0D, 0x0A, 0x15, 'A', '$', 'A'};
    CHECK(text_plain(&s, &l, t, sizeof t) == 2);
    CHECK(s.px[51 * SCREEN_W + 100] == 2); /* the header glyph's column 0 */
    CHECK(s.px[50 * SCREEN_W + 168] == 7 && s.px[50 * SCREEN_W + 167] == 9); /* 'A' at pen 68 */
    CHECK(s.px[52 * SCREEN_W + 100] == 7); /* 0x0A drawn as slot 1 on the second line */
    CHECK(s.px[52 * SCREEN_W + 104] == 9 && s.px[52 * SCREEN_W + 105] == 7); /* 0x15 (slot 129) only the spacing */
    CHECK(s.px[52 * SCREEN_W + 109] == 9); /* nothing after the '$' */
    /* a glyph past x 639 goes on at the start of the next row (the linear frame buffer) */
    surface_clear(&s, 9);
    draw_glyph(&s, &f, 1, SCREEN_W - 1, 10);
    CHECK(s.px[10 * SCREEN_W + SCREEN_W - 1] == 7 && s.px[11 * SCREEN_W + 0] == 7 && s.px[11 * SCREEN_W + 1] == 7);
    CHECK(s.px[12 * SCREEN_W + 0] == 7 && s.px[12 * SCREEN_W + 2] == 9);
    font_free(&f);
}

/* docs/screens.md, "Image blitter", items 1-2 (SCR-6): a colour-0 byte inside an RLE span is drawn, only the gaps are
 * transparent; 0x0040 raw skips colour 0, 0x0000 raw is opaque. The pixel plane stays as before (asset digests). */
static void test_blit_rle_span_zero(void) {
    static Surface s;
    uint8_t px[4], mask[4] = {7, 7, 7, 7};
    uint16_t flags;
    const uint8_t rle[] = {0x00, 0x80, 0, 0, 0x02, 0x01, 0x00, 0x05, 0x00};
    CHECK(image_decode_mask(rle, sizeof rle, 4, 1, px, mask, &flags) == 0 && flags == IMG_RLE);
    const uint8_t want_px[4] = {0, 0, 5, 0}, want_mask[4] = {0, 1, 1, 0};
    CHECK(memcmp(px, want_px, 4) == 0);
    CHECK(memcmp(mask, want_mask, 4) == 0);
    Image img = {0, 4, 1, 0, 0, IMG_RLE, px, mask};
    CHECK(image_is_transparent(&img));
    surface_clear(&s, 9);
    blit(&s, &img, 0, 0, image_is_transparent(&img));
    CHECK(s.px[0] == 9 && s.px[1] == 0 && s.px[2] == 5 && s.px[3] == 9);
    /* clipped: drawn from x -1, the span's 0 lands at x 0, its 5 at x 1, the trailing gap leaves x 2 */
    surface_clear(&s, 9);
    blit(&s, &img, -1, 0, 1);
    CHECK(s.px[0] == 0 && s.px[1] == 5 && s.px[2] == 9);
    surface_clear(&s, 9);
    blit(&s, &img, SCREEN_W - 2, SCREEN_H - 1, 1); /* right and bottom edge: x 638 gap, 639 the 0 */
    CHECK(s.px[(SCREEN_H - 1) * SCREEN_W + SCREEN_W - 2] == 9 && s.px[(SCREEN_H - 1) * SCREEN_W + SCREEN_W - 1] == 0);

    const uint8_t raw40[] = {0x00, 0x40, 0, 0, 0x00, 0x00, 0x05, 0x00};
    mask[0] = 7;
    CHECK(image_decode_mask(raw40, sizeof raw40, 4, 1, px, mask, &flags) == 0 && flags == IMG_RAW_40);
    CHECK(mask[0] == 7); /* a raw image writes no mask */
    Image r40 = {0, 4, 1, 0, 0, IMG_RAW_40, px, NULL};
    surface_clear(&s, 9);
    blit(&s, &r40, 0, 0, image_is_transparent(&r40));
    CHECK(s.px[0] == 9 && s.px[1] == 9 && s.px[2] == 5 && s.px[3] == 9);
    /* a stray mask on a raw image is not used: 0x0040 still skips colour 0 */
    Image r40m = {0, 4, 1, 0, 0, IMG_RAW_40, px, (uint8_t *)want_mask};
    surface_clear(&s, 9);
    blit(&s, &r40m, 0, 0, 1);
    CHECK(s.px[1] == 9);

    const uint8_t raw0[] = {0x00, 0x00, 0, 0, 0x00, 0x00, 0x05, 0x00};
    CHECK(image_decode_mask(raw0, sizeof raw0, 4, 1, px, NULL, &flags) == 0 && flags == IMG_RAW);
    Image r0 = {0, 4, 1, 0, 0, IMG_RAW, px, NULL};
    CHECK(!image_is_transparent(&r0));
    surface_clear(&s, 9);
    blit(&s, &r0, 0, 0, image_is_transparent(&r0));
    CHECK(s.px[0] == 0 && s.px[1] == 0 && s.px[2] == 5 && s.px[3] == 0);
}

/* image_load gives an RLE image its span mask (and a raw one none), so every game draw follows the rule. */
static void test_image_load_mask(void) {
    static Game g;
    /* Use CTest's writable working directory, also on Windows where /tmp need not exist. */
    char tmpl[] = "biing_test_assets_XXXXXX", path[1200];
    if (!mkdtemp(tmpl)) {
        CHECK(0 && "mkdtemp");
        return;
    }
    snprintf(g.root, sizeof g.root, "%s", tmpl);
    snprintf(path, sizeof path, "%s/DATA", tmpl), test_mkdir(path);
    snprintf(path, sizeof path, "%s/DATA/0", tmpl), test_mkdir(path);
    snprintf(path, sizeof path, "%s/DATA/0/0", tmpl), test_mkdir(path);
    const uint8_t rle[] = {0x00, 0x80, 0, 0, 0x02, 0x01, 0x00, 0x05, 0x00};
    const uint8_t raw[] = {0x00, 0x40, 0, 0, 0x00, 0x00, 0x05, 0x00};
    const uint8_t *files[2] = {rle, raw};
    size_t sizes[2] = {sizeof rle, sizeof raw};
    for (int k = 0; k < 2; k++) {
        snprintf(path, sizeof path, "%s/DATA/0/0/%d", tmpl, k + 1);
        FILE *f = fopen(path, "wb");
        CHECK(f && fwrite(files[k], 1, sizes[k], f) == sizes[k]);
        if (f) fclose(f);
        g.ixy[k + 1] = (IxyEntry){4, 1, 0, 0};
    }
    Image img;
    int loaded = image_load(&g, 1, &img);
    CHECK(loaded == 0);
    if (loaded == 0) {
        CHECK(img.mask && img.mask[0] == 0 && img.mask[1] == 1 && img.mask[2] == 1 && img.mask[3] == 0);
        static Surface s;
        surface_clear(&s, 9);
        blit(&s, &img, 0, 0, image_is_transparent(&img));
        CHECK(s.px[0] == 9 && s.px[1] == 0 && s.px[2] == 5 && s.px[3] == 9);
    }
    image_free(&img);
    CHECK(image_load(&g, 2, &img) == 0);
    CHECK(img.mask == NULL);
    image_free(&img);
    static const char *made[] = {"DATA/0/0/1", "DATA/0/0/2", "DATA/0/0", "DATA/0", "DATA", ""};
    for (int k = 0; k < 6; k++) {
        snprintf(path, sizeof path, "%s/%s", tmpl, made[k]);
        CHECK((k < 2 ? remove(path) : rmdir(path)) == 0);
    }
}

static void test_blit_clips_and_skips_zero(void) {
    static Surface s;
    surface_clear(&s, 9);
    uint8_t px[4] = {0, 5, 6, 7};
    Image img = {0, 2, 2, 0, 0, IMG_RAW_40, px, NULL}; /* no span mask: colour 0 is what is skipped */
    blit(&s, &img, -1, SCREEN_H - 1, 1); /* only pixel (1,0) lands, at (0, H-1) */
    CHECK(s.px[(SCREEN_H - 1) * SCREEN_W] == 5);
    CHECK(s.px[(SCREEN_H - 1) * SCREEN_W + 1] == 9);
    blit(&s, &img, 0, 0, 1);
    CHECK(s.px[0] == 9); /* colour 0 transparent */
    blit(&s, &img, 0, 0, 0);
    CHECK(s.px[0] == 0);
}

static void test_image_rel_path(void) {
    char buf[32];
    CHECK(image_rel_path(1, buf, sizeof buf) == 0 && strcmp(buf, "DATA/0/0/1") == 0);
    CHECK(image_rel_path(1234, buf, sizeof buf) == 0 && strcmp(buf, "DATA/C/3/4") == 0);
    CHECK(image_rel_path(3600, buf, sizeof buf) != 0);
}

static void test_all(void) {
    test_raw_image();
    test_rle_image();
    test_rle_ff_escape();
    test_rle_rejects_overflow();
    test_rle_exact_width();
    test_rle_incomplete_and_excess();
    test_image_dimensions();
    test_textdb();
    test_datadb();
    test_datadb_rejects_wrong_size();
    test_font_column_major();
    test_font_bounds();
    test_font_offset_wrap();
    test_font_failure_cleanup();
    test_blit_clips_and_skips_zero();
    test_blit_rle_span_zero();
    test_image_load_mask();
    test_header_glyph();
    test_image_rel_path();
}

int main(int argc, char **argv) {
    /* Isolate arithmetic mutations: small-offset guards use font-bounds, the wrapping regression uses font-wrap. */
    if (argc == 1) test_all();
    else if (argc == 2 && strcmp(argv[1], "font-bounds") == 0) test_font_bounds();
    else if (argc == 2 && strcmp(argv[1], "font-wrap") == 0) test_font_offset_wrap();
    else if (argc == 2 && strcmp(argv[1], "font-cleanup") == 0) test_font_failure_cleanup();
    else {
        fprintf(stderr, "unknown asset test selection\n");
        return 2;
    }
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] all checks passed\n");
    return 0;
}
