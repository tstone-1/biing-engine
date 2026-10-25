/* The patient sheet's page-1 lines and the personnel sheet's activity line (docs/ui-sheets.md, sections 1 and 2), the
 * warehouse animation (section 3), the skip-hour loop and the animation tick (section 4, docs/mainloop.md). Synthetic
 * fixtures only (AGENTS.md): every text n is "n|" unless the test sets it, the names are "P<i>" (patients) and "S<i>"
 * (staff), and the program image holds made-up tables. A line is compared as a string. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "fixes.h"
#include "portable.h"
#include "desktop_ui.h"
#include "depts_ui.h"
#include "estate_ui.h"
#include "garage.h"
#include "garage_ui.h"
#include "game.h"
#include "personnel.h"
#include "personnel_ui.h"
#include "purchase_ui.h"
#include "rooms_ui.h"
#include "sheet.h"
#include "sim_internal.h"
#include "support.h"
#include "support_ui.h"

static int failures;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            failures++;                                                            \
        }                                                                          \
    } while (0)

static World g_world;
static Program g_prog;
static uint8_t g_image[0x10000];
static Sim g_sim;
static Sim *s = &g_sim;
static TextDb g_texts;
static DataDb g_people;
static SheetMemory g_mem;
static Message g_msg;
static SheetCtx g_ctx;
static SheetPage g_page;

#define ILL(id) (g_image + 0x954 + (id) * 20)
#define TREAT(id) (g_image + 0x270C + (id) * 38)
#define ITEM(id) (g_image + 0x144A + (id) * 20)
#define WISH(id) (g_image + 0x2066 + (id) * 4)

static uint8_t *P(int i) { return g_world.block[BLK_PATIENTS] + i * 106; }
static uint8_t *ST(int i) { return g_world.block[BLK_STAFF] + i * 32; }
static uint8_t *SL(int i) { return staff_slots(s, 0) + i * STAFF_SLOT_SIZE; }
static uint8_t *rec(int cat, int i) { return sim_room_record(s, 0, cat, i); }
static int u16(const uint8_t *p) { return (uint16_t)get16(p); }

/* ---- texts and names --------------------------------------------------------------------------------------------- */

static struct { int n; const char *t; size_t len; } g_over[64];
static int g_nover;
static uint8_t *g_textblock;

static void text_set(int n, const char *t, size_t len) {
    g_over[g_nover].n = n, g_over[g_nover].t = t, g_over[g_nover].len = len;
    g_nover++;
}
static void texts_build(void) {
    free(g_textblock);
    size_t cap = (size_t)TEXT_COUNT * 16 + 4096, at = 0;
    g_textblock = malloc(cap);
    for (int i = 0; i < TEXT_COUNT; i++) {
        g_texts.offs[i] = (uint32_t)at;
        int k = -1;
        for (int j = 0; j < g_nover; j++)
            if (g_over[j].n == i) k = j;
        if (k >= 0) memcpy(g_textblock + at, g_over[k].t, g_over[k].len), at += g_over[k].len;
        else at += (size_t)sprintf((char *)g_textblock + at, "%d|", i);
        g_textblock[at++] = '$';
    }
    g_texts.text = g_textblock;
    g_texts.text_size = (uint32_t)at;
}

static uint8_t g_pool_text[2][16384], g_pool_table[2][1100 * 4];
static void people_build(void) {
    for (int k = 0; k < 2; k++) {
        size_t at = 0;
        for (int i = 0; i < 1100; i++) {
            uint8_t *t = g_pool_table[k] + 4 * i;
            t[0] = (uint8_t)at, t[1] = (uint8_t)(at >> 8), t[2] = t[3] = 0;
            at += (size_t)sprintf((char *)g_pool_text[k] + at, "%c%d$", k ? 'S' : 'P', i);
        }
        TextPool *pl = &g_people.pools[(k ? DAT_STAFF_NAMES : DAT_PATIENT_NAMES) - DAT_PATIENT_NAMES];
        pl->text = g_pool_text[k], pl->text_size = at, pl->table = g_pool_table[k], pl->count = 1100;
    }
}

/* ---- world ------------------------------------------------------------------------------------------------------- */

static void describe(int cat, int equipment, int slots, int cond_b) {
    const int d[7] = {0x40, 2, equipment, slots, 0x44, cond_b, 1};
    for (int i = 0; i < 7; i++) put16(g_image + 0x4758 + 14 * cat + 2 * i, d[i]);
}

static uint32_t g_strings = 0xC000; /* made-up '$' strings of the program image */
static uint32_t image_str(const char *t, size_t len) {
    uint32_t at = g_strings;
    memcpy(g_image + at, t, len);
    g_image[at + len] = '$';
    g_strings += (uint32_t)len + 1;
    return at;
}
/* step code `code` of a step table: the entry (a '$' string, or a computed entry with its argument) */
static void table_entry(uint32_t table, int code, uint32_t entry) { put32(g_image + table + 4 * code, (int32_t)entry); }
static uint32_t computed(const char *arg) {
    uint32_t at = g_strings;
    memset(g_image + at, 0, 6); /* first byte 0, the routine dword (never called) */
    memcpy(g_image + at + 6, arg, strlen(arg));
    g_image[at + 6 + strlen(arg)] = '$';
    g_strings += 7 + (uint32_t)strlen(arg);
    return at;
}
static void both(uint32_t t1, uint32_t t0, int code, uint32_t entry) { table_entry(t1, code, entry), table_entry(t0, code, entry); }

static void fresh(void) {
    world_free(&g_world);
    world_alloc(&g_world);
    memset(g_image, 0, sizeof g_image);
    g_prog.image.data = g_image;
    g_prog.image.size = sizeof g_image;
    g_strings = 0xC000;
    sim_init(&g_sim, &g_world, &g_prog, 11);
    world_set_tail(&g_world, TAIL_DAY, 5), world_set_tail(&g_world, TAIL_MONTH, 1);
    world_set_tail(&g_world, TAIL_HOUR, 10);
    for (int cat = 2; cat <= 18; cat++) describe(cat, 0x60, 4, 0x70);
    g_nover = 0;
    texts_build();
    people_build();
    memset(&g_mem, 0, sizeof g_mem);
    g_ctx = (SheetCtx){s, &g_texts, &g_people, &g_mem, &g_msg};
    image_str("test device", 11); /* the strings are placed at 0xC000 on; "test device" goes to 0xA769 below */
    memcpy(g_image + 0xA769, "test device$", 12);
}

static int line_is(int k, const char *want) {
    const Message *m = &g_page.line[k];
    int ok = g_page.drawn[k] && m->len == strlen(want) && memcmp(m->buf, want, m->len) == 0;
    if (!ok) fprintf(stderr, "    line %d: drawn %d \"%.*s\", want \"%s\"\n", k, g_page.drawn[k], (int)m->len, m->buf, want);
    return ok;
}
static int msg_is(const char *want) {
    int ok = g_msg.len == strlen(want) && memcmp(g_msg.buf, want, g_msg.len) == 0;
    if (!ok) fprintf(stderr, "    msg \"%.*s\", want \"%s\"\n", (int)g_msg.len, g_msg.buf, want);
    return ok;
}
static int msg_q(const char *want) { return g_msg.len == strlen(want) && memcmp(g_msg.buf, want, g_msg.len) == 0; }
static void sheet(int index) { sheet_page1(&g_ctx, index, &g_page); }

/* a ward patient (state 6, Krankenzimmer 0 with nurse 5): illness 3, treatment 69 at step 1 */
static uint8_t *ward_patient(int i) {
    uint8_t *p = P(i);
    memset(p, 0, 106);
    p[4] = 6, p[66] = 0, p[41] = 1, p[42] = 3, p[43] = 69, p[44] = 70, p[45] = 1, p[56] = 3, p[93] = 0xFF;
    p[20] = 50, p[78] = 40;
    put16(rec(6, 0) + 8, 5);
    put16(TREAT(69) + 0x1E, 500); /* the treatment's name: text 500 */
    TREAT(69)[0x10] = 16, TREAT(69)[0x11] = 15, TREAT(69)[0x12] = 14, TREAT(69)[0x13] = 13;
    return p;
}

/* a symptom text that starts with a 0 byte is printed from its second byte, without the severity word
 * (docs/ui-rooms.md, complaints; the engine printed only the severity word until 2026-10-19) */
static void test_symptom_zero_byte(void) {
    fresh();
    text_set(3292, "\0Ohren", 6);
    texts_build();
    ward_patient(10);
    sheet(10);
    CHECK(line_is(SHEET_COMPLAINTS, "Ohren"));
}

/* ---- the treatment line ------------------------------------------------------------------------------------------ */

static void test_treatment_line(void) {
    fresh();
    ward_patient(10);
    sheet(10);
    CHECK(line_is(SHEET_TREATMENT, "500|")); /* no dosage byte 0x24 */
    CHECK(line_is(SHEET_DIAGNOSIS, "3213|"));
    CHECK(line_is(SHEET_COMPLAINTS, "2377|3292|")); /* severity 2376 + 30 / 16, symptom 3291 + 1 */
    CHECK(line_is(SHEET_AUTOPSY, "2142|"));
    CHECK(line_is(SHEET_OPINION, "2386|")); /* 2383 + 50 / 16 */
    /* the oracle's "alle 85 Minuten eine Spritze": steps 16, 15, 14, 13 of 8 + 3 + 3 + 3 minutes, factor 5 */
    g_image[0x44A5 + 16] = 8, g_image[0x44A5 + 15] = 3, g_image[0x44A5 + 14] = 3, g_image[0x44A5 + 13] = 3;
    TREAT(69)[0x24] = 1, TREAT(69)[0x25] = 5, TREAT(69)[0x0A] = 1;
    sheet(10);
    CHECK(line_is(SHEET_TREATMENT, "500|2069|85 2071|"));
    TREAT(69)[0x0A] = 3; /* a shock strength: 3110 + 3 */
    sheet(10);
    CHECK(line_is(SHEET_TREATMENT, "500|2069|85 3113|"));
    TREAT(69)[0x0A] = 0; /* no interval byte: a dose only when byte 5 is set */
    sheet(10);
    CHECK(line_is(SHEET_TREATMENT, "500|"));
    TREAT(69)[5] = 1;
    sheet(10);
    CHECK(line_is(SHEET_TREATMENT, "500|2069|85 2070|"));
    TREAT(69)[4] = 1; /* a bed treatment */
    sheet(10);
    CHECK(line_is(SHEET_TREATMENT, "3497|500|2069|85 2070|"));
    /* the drug part of treatment 10, with the illness record of field 42 (not 56) */
    uint8_t *p = P(10);
    p[43] = 10, p[80] = 2, p[42] = 4;
    put16(TREAT(10) + 0x1E, 510), put32(TREAT(10) + 0x1A, 0x12345);
    ILL(4)[15] = 7, ILL(3)[15] = 9;
    sheet(10);
    CHECK(line_is(SHEET_TREATMENT, "510|3693|3199|3490|"));
    p[43] = 2; /* I byte 0 + 1 as a number, "mal täglich ", the drug 3777 + I byte 5 */
    put16(TREAT(2) + 0x1E, 520), put32(TREAT(2) + 0x1A, 1), ILL(4)[0] = 2, ILL(4)[5] = 1;
    sheet(10);
    CHECK(line_is(SHEET_TREATMENT, "520|3 3493|3778|"));
    put32(TREAT(2) + 0x1A, 0); /* no routine: no drug part */
    sheet(10);
    CHECK(line_is(SHEET_TREATMENT, "520|"));
    p[43] = 7; /* "Entfernung " + 3686 + I 15 + " aus " + 3655 + I 1 */
    put16(TREAT(7) + 0x1E, 530), put32(TREAT(7) + 0x1A, 1), ILL(4)[1] = 2;
    sheet(10);
    CHECK(line_is(SHEET_TREATMENT, "530|3491|3693|3492|3657|"));
    p[43] = 17;
    put16(TREAT(17) + 0x1E, 540), put32(TREAT(17) + 0x1A, 1);
    sheet(10);
    CHECK(line_is(SHEET_TREATMENT, "540|3487|"));
    /* no illness byte (field 56): 231 */
    p[56] = 0;
    sheet(10);
    CHECK(line_is(SHEET_TREATMENT, "231|"));
    /* a wish: W byte 1 set gives 2068; wishes with byte 1 = 0 repeat the diagnosis line */
    p[56] = 0, p[57] = 4;
    WISH(4)[1] = 0;
    sheet(10);
    CHECK(line_is(SHEET_DIAGNOSIS, "3400|") && line_is(SHEET_TREATMENT, "3400|"));
    WISH(4)[1] = 1;
    sheet(10);
    CHECK(line_is(SHEET_TREATMENT, "2068|"));
}

static void test_diagnosis_variant(void) {
    fresh();
    uint8_t *p = ward_patient(10);
    p[80] = 3;
    sheet(10);
    CHECK(line_is(SHEET_DIAGNOSIS, "3213|3200|")); /* 3197 + field 80 */
    ILL(3)[16] = 3;
    sheet(10);
    CHECK(line_is(SHEET_DIAGNOSIS, "3213|3206|")); /* 3203 + field 80 */
    p[42] = 5; /* the decision reads the true illness (56 = 3), the name the diagnosed one (42) */
    ILL(3)[15] = 3;
    sheet(10);
    CHECK(line_is(SHEET_DIAGNOSIS, "3215|"));
    p[57] = 2; /* a wish */
    sheet(10);
    CHECK(line_is(SHEET_DIAGNOSIS, "3398|"));
    /* the complaints line's wish variant: 3197 + field 84 when the wish record's byte 2 is set */
    CHECK(line_is(SHEET_COMPLAINTS, "3283|"));
    WISH(2)[2] = 1, p[84] = 4;
    sheet(10);
    CHECK(line_is(SHEET_COMPLAINTS, "3283|3201|"));
}

/* ---- the step tables and the kept index -------------------------------------------------------------------------- */

static void tables_for_code(int code, const char *a1, const char *a0, const char *m1, const char *m0) {
    table_entry(0x6A71, code, image_str(a1, strlen(a1)));
    table_entry(0x875B, code, image_str(a0, strlen(a0)));
    table_entry(0x6C2D, code, image_str(m1, strlen(m1)));
    table_entry(0x8917, code, image_str(m0, strlen(m0)));
}

static void test_step_tables(void) {
    fresh();
    ward_patient(10);
    tables_for_code(16, "A1", "A0", "M1", "M0");
    int seen[4] = {0};
    for (int i = 0; i < 200; i++) {
        sheet(10);
        seen[0] += g_page.line[SHEET_ACTIVITY].len == 2 && g_page.line[SHEET_ACTIVITY].buf[1] == '1';
        seen[1] += g_page.line[SHEET_ACTIVITY].len == 2 && g_page.line[SHEET_ACTIVITY].buf[1] == '0';
        seen[2] += g_page.line[SHEET_MOOD].len == 2 && g_page.line[SHEET_MOOD].buf[1] == '1';
        seen[3] += g_page.line[SHEET_MOOD].len == 2 && g_page.line[SHEET_MOOD].buf[1] == '0';
    }
    CHECK(seen[0] + seen[1] == 200 && seen[0] > 60 && seen[1] > 60); /* r1 picks the wording, line by line */
    CHECK(seen[2] + seen[3] == 200 && seen[2] > 60 && seen[3] > 60);
    CHECK(g_mem.step_index == 4 * 16);
    /* no nurse: 1670 and the mood 1671, the index still set */
    put16(rec(6, 0) + 8, 0), g_mem.step_index = 0;
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "1670|") && line_is(SHEET_MOOD, "1671|") && g_mem.step_index == 64);
    put16(rec(6, 0) + 8, 5);
    /* the ward's supply request and a missing step capability: 3043 + c, the index is not set */
    put16(rec(6, 0) + 6, 1), TREAT(69)[20] = 33, g_mem.step_index = 8;
    tables_for_code(2, "B", "B", "N", "N");
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "3076|") && g_mem.step_index == 8 && line_is(SHEET_MOOD, "N"));
    /* the item is there: the step's line again */
    put16(rec(6, 0) + 0x60, 9), ITEM(9)[12] = 33;
    sheet(10);
    CHECK(g_page.line[SHEET_ACTIVITY].buf[0] == 'A' && g_mem.step_index == 64);
    /* the intensive-care sheet without a running step reads the index the ward sheet left (code 16) */
    uint8_t *q = P(20);
    memset(q, 0, 106);
    q[4] = 9, q[56] = 3, q[42] = 3, q[43] = 69, q[44] = 30, q[45] = 1;
    rec(9, 0)[0x0E] = 0;
    sheet(20);
    CHECK(line_is(SHEET_ACTIVITY, "2073|"));
    CHECK(g_page.line[SHEET_MOOD].buf[0] == 'M'); /* M1 / M0 of code 16, not of q's own step */
    g_mem.step_index = 4 * 2; /* a $ entry: the fallback 1782 on this sheet */
    table_entry(0x6C2D, 2, image_str("", 0)), table_entry(0x8917, 2, image_str("", 0));
    sheet(20);
    CHECK(line_is(SHEET_MOOD, "1782|"));
    /* health 50 or more, or the night: 2072 */
    q[44] = 50;
    sheet(20);
    CHECK(line_is(SHEET_ACTIVITY, "2072|"));
    q[44] = 30, rec(9, 0)[0x0E] = 1; /* running: the index is set and the activity pair drawn */
    sheet(20);
    CHECK(g_page.line[SHEET_ACTIVITY].buf[0] == 'A' && g_mem.step_index == 64);
}

static void test_ward_mood(void) {
    fresh();
    uint8_t *p = ward_patient(10);
    tables_for_code(16, "A", "A", "M", "M");
    p[40] = 85;
    sheet(10);
    CHECK(line_is(SHEET_MOOD, "1783|"));
    world_set_tail(&g_world, TAIL_HOUR, 23); /* the night: 1769 + r3 */
    int seen = 0;
    for (int i = 0; i < 100; i++) {
        sheet(10);
        const Message *m = &g_page.line[SHEET_MOOD];
        int n = atoi((const char *)m->buf);
        CHECK(n >= 1769 && n <= 1772);
        seen |= 1 << (n - 1769);
    }
    CHECK(seen == 15);
    CHECK(line_is(SHEET_ACTIVITY, "2072|"));
    p[40] = 61;
    world_set_tail(&g_world, TAIL_HOUR, 3); /* hour 3 is night ("22-3") */
    sheet(10);
    CHECK(atoi((const char *)g_page.line[SHEET_MOOD].buf) >= 1765 && atoi((const char *)g_page.line[SHEET_MOOD].buf) <= 1768);
    world_set_tail(&g_world, TAIL_HOUR, 4);
    sheet(10);
    CHECK(line_is(SHEET_MOOD, "1784|"));
    p[40] = 40;
    sheet(10);
    CHECK(line_is(SHEET_MOOD, "1785|"));
    p[40] = 0, world_set_tail(&g_world, TAIL_HOUR, 22);
    sheet(10);
    CHECK(atoi((const char *)g_page.line[SHEET_MOOD].buf) >= 1753 && atoi((const char *)g_page.line[SHEET_MOOD].buf) <= 1760);
    world_set_tail(&g_world, TAIL_HOUR, 10);
    p[56] = 0; /* cured: 230 and 226 + mood / 32 */
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "230|") && line_is(SHEET_MOOD, "227|"));
    p[56] = 3, p[102] = 1;
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2062|"));
    p[102] = 0, p[93] = 0;
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2073|"));
    p[93] = 0xFF;
    /* a $ mood entry gives 1777; a computed one an empty line (original, reproduced) */
    table_entry(0x6C2D, 16, image_str("", 0)), table_entry(0x8917, 16, image_str("", 0));
    sheet(10);
    CHECK(line_is(SHEET_MOOD, "1777|"));
    uint32_t c = computed("x");
    table_entry(0x6C2D, 16, c), table_entry(0x8917, 16, c);
    sheet(10);
    CHECK(line_is(SHEET_MOOD, ""));
}

static void test_neuro_ward(void) {
    fresh();
    uint8_t *p = ward_patient(10);
    p[4] = 16;
    put16(rec(16, 0) + 8, 5);
    TREAT(69)[0x10] = 46; /* a bed step with random texts */
    both(0x6A71, 0x875B, 46, computed("x"));
    both(0x6C2D, 0x8917, 46, computed("y"));
    int act = 0, mood = 0;
    for (int i = 0; i < 300; i++) { /* K +6 = 0: state 6's code, the computed mood entry is an empty line */
        sheet(10);
        int a = atoi((const char *)g_page.line[SHEET_ACTIVITY].buf);
        CHECK(a >= 3826 && a <= 3830);
        act |= 1 << (a - 3826);
        CHECK(line_is(SHEET_MOOD, ""));
    }
    CHECK(act == 31);
    put16(rec(16, 0) + 6, 1); /* its own branch: the computed mood entry is run */
    for (int i = 0; i < 300; i++) {
        sheet(10);
        int m = atoi((const char *)g_page.line[SHEET_MOOD].buf);
        CHECK(m >= 3831 && m <= 3835);
        mood |= 1 << (m - 3831);
    }
    CHECK(mood == 31);
    both(0x6C2D, 0x8917, 46, image_str("", 0)); /* and a $ entry is an empty line there (1776 never reached) */
    sheet(10);
    CHECK(line_is(SHEET_MOOD, ""));
    put16(rec(16, 0) + 6, 0);
    sheet(10);
    CHECK(line_is(SHEET_MOOD, "1777|"));
    p[102] = 1;
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2063|"));
    p[102] = 0;
    /* an index past the table (a step code above 110): the neuro ward's original hangs; nothing is drawn */
    TREAT(69)[0x10] = 120;
    put16(rec(16, 0) + 6, 1);
    sheet(10);
    CHECK(!g_page.drawn[SHEET_ACTIVITY] && g_page.drawn[SHEET_TREATMENT]);
}

/* ---- instruments ------------------------------------------------------------------------------------------------- */

static void treatment_room_patient(int i) {
    uint8_t *p = P(i);
    memset(p, 0, 106);
    p[4] = 5, p[66] = 1, p[41] = 1, p[42] = 3, p[43] = 30, p[44] = 70, p[45] = 1, p[56] = 3, p[20] = 50;
    uint8_t *r = rec(5, 1);
    put16(r + 0x0E, 0), put16(r + 0x10, 30), put16(r + 0x12, 10);
    put16(TREAT(30) + 0x1E, 600);
    TREAT(30)[0x10] = 48;
}

static void test_instruments(void) {
    fresh();
    treatment_room_patient(10);
    both(0x6A71, 0x875B, 48, computed("Fixture action: "));
    both(0x6C2D, 0x8917, 48, image_str("", 0));
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "Fixture action: test device")); /* no item with capability 14 */
    CHECK(line_is(SHEET_MOOD, "1782|") && line_is(SHEET_TREATMENT, "600|") && line_is(SHEET_DIAGNOSIS, "3213|"));
    static const char POKER[] = "Fixture tool (AA)";
    text_set(700, POKER, strlen(POKER));
    texts_build();
    put32(ITEM(6), 700), ITEM(6)[13] = 14, ITEM(6)[7] = 0, ITEM(6)[16] = 3;
    put32(ITEM(4), 701), ITEM(4)[13] = 14, ITEM(4)[7] = 4;
    uint8_t *r = rec(5, 1);
    g_world.block[BLK_ROOM_COUNTS][5] = 2; /* the tidy-up walks the rooms in use */
    put16(r + 0x60, 6), put16(r + 0x62, 4), r[0x70] = 10, r[0x71] = 10;
    sheet(10); /* the first item with the capability, the name cut before " (" */
    CHECK(line_is(SHEET_ACTIVITY, "Fixture action: 1607|Fixture tool"));
    CHECK(r[0x70] == 10); /* the wear flag clear: nothing worn */
    s->equipment_use = 1; /* a plain check of the simulation clears the use flag [0x5B6F] */
    CHECK(sim_has_equipment(s, r, 5, 14) && s->equipment_use == 0 && r[0x70] == 10);
    s->equipment_use = 1; /* the simulation's last search was a check-and-use: drawing the sheet wears the item */
    sheet(10);
    CHECK(r[0x70] == 7 && r[0x71] == 10);
    sheet(10), sheet(10);
    CHECK(r[0x70] == 1);
    sheet(10); /* at 0 or below it leaves the list (and the tidy-up moves the next item up) */
    CHECK(u16(r + 0x60) == 4 && u16(r + 0x62) == 0 && r[0x70] == 10);
    ITEM(4)[16] = 3, r[0x70] = 6; /* exactly 0 left also removes it */
    sheet(10), sheet(10);
    CHECK(u16(r + 0x60) == 0);
    put16(r + 0x60, 4);
    ITEM(4)[16] = 0, r[0x70] = 250; /* wear byte 0: 1 with r3 = 0 */
    int worn = 0;
    for (int i = 0; i < 400; i++) {
        int before = r[0x70];
        sheet(10);
        worn += before - r[0x70];
    }
    CHECK(worn > 60 && worn < 140); /* 100 expected */
    CHECK(line_is(SHEET_ACTIVITY, "Fixture action: 1611|701|"));
    s->equipment_use = 0;
    /* a random-text entry in the treatment room: the machine-code case, left empty */
    TREAT(30)[0x10] = 46;
    both(0x6A71, 0x875B, 46, computed("x"));
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, ""));
}

static void test_cut_names(void) {
    fresh();
    static const char A[] = "ab (c)", B[] = "(x)", C[] = "plain";
    text_set(710, A, 6), text_set(711, B, 3), text_set(712, C, 5);
    texts_build();
    put32(ITEM(1), 710), put32(ITEM(2), 711), put32(ITEM(3), 712);
    msg_clear(&g_msg), sheet_item_name(&g_ctx, 1, 1);
    CHECK(msg_is("ab"));
    msg_clear(&g_msg), sheet_item_name(&g_ctx, 2, 1);
    CHECK(msg_is(""));
    msg_clear(&g_msg), sheet_item_name(&g_ctx, 3, 1);
    CHECK(msg_is("plain"));
    msg_clear(&g_msg), sheet_item_name(&g_ctx, 1, 0);
    CHECK(msg_is("ab (c)"));
}

/* ---- the treatment room, dental station and neurology by state --------------------------------------------------- */

static void test_treatment_room(void) {
    fresh();
    treatment_room_patient(10);
    uint8_t *p = P(10), *r = rec(5, 1);
    put16(r + 0x0E, 999); /* not started: 0x282DA */
    sheet(10);
    CHECK(line_is(SHEET_DIAGNOSIS, "2095|") && line_is(SHEET_TREATMENT, "2100|") && line_is(SHEET_ACTIVITY, "2167|"));
    CHECK(line_is(SHEET_MOOD, "1782|") && line_is(SHEET_OPINION, "2386|"));
    r[0x44] = 12; /* a missing capability */
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "3055|"));
    r[0x44] = 0, r[0x45] = 2; /* a missing kind */
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "3024|"));
    r[0x45] = 0;
    /* diagnosing: "Folterung mit " + article + "eisgekühlten " + the kind-9 item */
    put16(r + 0x0E, 5);
    put32(ITEM(0), 3500);
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2171|1607|2172|3500|")); /* none found: item 0 ("nichts") */
    put32(ITEM(8), 720), ITEM(8)[6] = 9, ITEM(8)[7] = 2;
    put16(r + 0x60, 8);
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2171|1609|2172|720|") && line_is(SHEET_MOOD, "1779|") && g_mem.kind_item == 8);
    put16(r + 0x60, 0); /* gone again: [0x5B71] keeps the item of the last search that found one */
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2171|1609|2172|720|"));
    put16(r + 0x0E, 0);
    /* psychological (FD) and dental (FF) cases */
    p[43] = 0xFD;
    sheet(10);
    CHECK(line_is(SHEET_DIAGNOSIS, "2090|") && line_is(SHEET_TREATMENT, "2097|") && line_is(SHEET_ACTIVITY, "2164|"));
    CHECK(line_is(SHEET_OPINION, "1774|"));
    p[43] = 0xFF;
    sheet(10);
    CHECK(line_is(SHEET_DIAGNOSIS, "2091|") && line_is(SHEET_TREATMENT, "2098|"));
    p[43] = 0xFE; /* the original hangs: only the complaints and the autopsy line are drawn */
    sheet(10);
    CHECK(g_page.drawn[SHEET_COMPLAINTS] && g_page.drawn[SHEET_AUTOPSY]);
    CHECK(!g_page.drawn[SHEET_DIAGNOSIS] && !g_page.drawn[SHEET_TREATMENT] && !g_page.drawn[SHEET_ACTIVITY]);
    CHECK(!g_page.drawn[SHEET_MOOD] && !g_page.drawn[SHEET_OPINION]);
    p[43] = 30;
    /* after the diagnosis, by the room */
    TREAT(30)[4] = 1;
    sheet(10);
    CHECK(line_is(SHEET_TREATMENT, "3497|600|") && line_is(SHEET_ACTIVITY, "2152|"));
    TREAT(30)[4] = 0;
    put16(r + 0x10, 0);
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2163|"));
    TREAT(30)[5] = 1;
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2153|"));
    TREAT(30)[6] = 1;
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2161|"));
    put16(r + 0x10, 30), put16(r + 0x12, 999);
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2160|"));
    put16(r + 0x12, 0);
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2150|"));
    put16(r + 0x10, 12);
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2149|"));
    put16(r + 0x12, 10), p[45] = 0;
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2158|") && line_is(SHEET_MOOD, "1782|"));
    /* no dosage on this sheet */
    p[45] = 1, TREAT(30)[0x24] = 1, TREAT(30)[0x0A] = 1, TREAT(30)[0x25] = 1;
    sheet(10);
    CHECK(line_is(SHEET_TREATMENT, "600|"));
}

static void test_dental_neurology(void) {
    fresh();
    treatment_room_patient(10);
    uint8_t *p = P(10);
    p[4] = 11, p[80] = 1;
    uint8_t *r = rec(11, 1);
    put16(r + 0x0E, 0), put16(r + 0x10, 30), put16(r + 0x12, 10);
    TREAT(30)[4] = 1; /* never "Stationäre Aufnahme" on the dental sheet */
    tables_for_code(48, "D", "D", "", "");
    sheet(10);
    CHECK(line_is(SHEET_DIAGNOSIS, "3213|3407|") && line_is(SHEET_TREATMENT, "600|"));
    CHECK(line_is(SHEET_ACTIVITY, "D") && line_is(SHEET_MOOD, "1773|"));
    p[45] = 0;
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2157|"));
    put16(r + 0x12, 999);
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2159|") && line_is(SHEET_MOOD, "1780|"));
    put16(r + 0x0E, 999);
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2167|") && line_is(SHEET_OPINION, "1778|"));
    put16(r + 0x0E, 4); /* diagnosing: 2169 + the kind-12 item, no article, + 2170 */
    put32(ITEM(8), 720), ITEM(8)[6] = 12;
    put16(r + 0x60, 8);
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2169|720|2170|") && line_is(SHEET_DIAGNOSIS, "2094|"));
    put16(r + 0x0E, 0), p[43] = 0xFF; /* FF hangs */
    sheet(10);
    CHECK(!g_page.drawn[SHEET_DIAGNOSIS] && !g_page.drawn[SHEET_OPINION]);
    /* neurology */
    fresh();
    treatment_room_patient(10);
    p = P(10);
    p[4] = 15;
    r = rec(15, 1);
    put16(r + 0x0E, 0), put16(r + 0x10, 30), put16(r + 0x12, 10);
    p[43] = 0xFD; /* FD hangs on this sheet */
    sheet(10);
    CHECK(!g_page.drawn[SHEET_DIAGNOSIS] && !g_page.drawn[SHEET_ACTIVITY] && g_page.drawn[SHEET_COMPLAINTS]);
    p[43] = 0xFE;
    sheet(10);
    CHECK(line_is(SHEET_DIAGNOSIS, "2092|") && line_is(SHEET_TREATMENT, "2099|") && line_is(SHEET_OPINION, "1774|"));
    p[43] = 30, TREAT(30)[4] = 1;
    sheet(10);
    CHECK(line_is(SHEET_TREATMENT, "3497|600|") && line_is(SHEET_ACTIVITY, "2152|") && line_is(SHEET_MOOD, "1781|"));
    TREAT(30)[4] = 0, put16(r + 0x12, 999);
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2155|"));
    put16(r + 0x0E, 999);
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2166|"));
    put16(r + 0x0E, 3);
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2168|") && line_is(SHEET_MOOD, "1775|"));
}

/* ---- OP, torture, pathology -------------------------------------------------------------------------------------- */

static void test_stage_texts(void) {
    fresh();
    static const char INSERT[] = "\0\001Fixture stage ", BONE[] = "\0\002Test step";
    text_set(4127, INSERT, sizeof INSERT - 1), text_set(4151, BONE, sizeof BONE - 1);
    static const char LENS[] = "Item (sample)";
    text_set(730, LENS, strlen(LENS));
    texts_build();
    uint8_t *p = P(10);
    memset(p, 0, 106);
    p[4] = 7, p[66] = 0, p[42] = 3, p[56] = 3, p[43] = 30, p[80] = 2, p[20] = 50;
    put16(TREAT(30) + 0x1E, 600);
    uint8_t *r = rec(7, 0);
    put16(r + 0x0E, 1), put16(r + 0x10, 2), put16(r + 0x12, 4);
    g_image[0x208E + 11 * 2 + 4] = 3;
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "4119|") && line_is(SHEET_MOOD, "2066|"));
    g_image[0x208E + 11 * 2 + 4] = 11, put16(r + 0x76, 9), put32(ITEM(9), 730);
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "Fixture stage Item"));
    g_image[0x208E + 11 * 2 + 4] = 35;
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "Test step3199|")); /* + 0x27FB0: 3197 + field 80 */
    put16(r + 0x0E, 999), put16(r + 0x12, 0xFFFF);
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2236|"));
    p[4] = 8; /* torture */
    r = rec(8, 0);
    put16(r + 0x0E, 1), put16(r + 0x10, 1), put16(r + 0x12, 2);
    g_image[0x242E + 11 + 2] = 5;
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "4088|") && line_is(SHEET_MOOD, "2067|"));
    p[104] = 1;
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2373|") && line_is(SHEET_AUTOPSY, "1751|") && line_is(SHEET_OPINION, "2375|"));
    p[104] = 0;
    p[4] = 18; /* pathology: row u16 +0x14, index +0x12 */
    r = rec(18, 0);
    put16(r + 0x0E, 1), put16(r + 0x14, 1), put16(r + 0x12, 3);
    g_image[0xA749 + 16 + 3] = 7;
    ILL(3)[8] = 1;
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "4067|") && line_is(SHEET_DIAGNOSIS, "3213|3408|") && line_is(SHEET_AUTOPSY, "2078|"));
}

/* ---- the personnel sheet's activity line ------------------------------------------------------------------------- */

static int plain_selector(const uint8_t *st, int d, int h, int which) { /* plain additions (docs/ui-sheets.md) */
    int w6 = u16(st + 6), w8 = u16(st + 8), w16 = u16(st + 16), b21 = st[21], b10 = st[10];
    int a = ((((w6 + d) * 5 + h) + b21) + w16) * 7 + w8, f = ((((w6 + d) * 3 + d) + b21) + w16) * 7 + w8;
    int e = ((((w6 + d + 3) * 7 + h * 3) + b21) + w16) * 3 + w8;
    int x = ((((w6 >> 3) + d) * 3 + h) + b21) & 0xFFFF;
    x = (x * 3 + w16 + w8) & 0xFFFF;
    x = (((x >> 1) + h) * 3 + b10) & 0xFFFF;
    switch (which) {
    case SEL_A: return (a & 0xFFFF) % 4;
    case SEL_C: return (a & 0xFFFF) % 2;
    case SEL_D: return (a & 0xFFFF) % 8;
    case SEL_F: return (f & 0xFFFF) % 4;
    case SEL_G: return (f & 0xFFFF) % 2;
    case SEL_H: return (f & 0xFFFF) % 8;
    case SEL_E: return (e & 0xFFFF) % 8;
    default: return x % 4;
    }
}

static void test_selectors(void) {
    uint8_t st[32] = {0};
    put16(st + 6, 1), put16(st + 8, 2), put16(st + 16, 3), st[21] = 4;
    CHECK(sheet_selector(st, 5, 6, SEL_A) == 3 && sheet_selector(st, 5, 6, SEL_D) == 7); /* 43 x 7 + 2 = 303 */
    CHECK(sheet_selector(st, 5, 6, SEL_F) == 0 && sheet_selector(st, 5, 6, SEL_H) == 4); /* (23 + 4 + 3) x 7 + 2 = 212 */
    uint32_t seed = 1;
    int bad = 0;
    for (int i = 0; i < 20000; i++) {
        for (int k = 0; k < 32; k++) seed = seed * 1103515245u + 12345u, st[k] = (uint8_t)(seed >> 16);
        int d = 1 + i % 30, h = i % 24;
        for (int w = SEL_A; w <= SEL_B; w++) bad += sheet_selector(st, d, h, w) != plain_selector(st, d, h, w);
    }
    CHECK(bad == 0);
}

/* employee `index` in slot `slot` of hospital 0, duty state `state`, room instance `room` */
static uint8_t *employ(int index, int slot, int state, int room, int sex) {
    uint8_t *sl = SL(slot), *st = ST(index);
    memset(sl, 0, STAFF_SLOT_SIZE), memset(st, 0, 32);
    put16(sl, index);
    st[0] = (uint8_t)sex, st[4] = (uint8_t)state, st[26] = (uint8_t)room, st[21] = 80;
    return st;
}
static int activity(int slot) {
    msg_clear(&g_msg);
    return sheet_staff_activity(&g_ctx, slot);
}

static void test_staff_lines(void) {
    fresh();
    uint8_t *st = employ(40, 2, 0, 0, 0);
    CHECK(activity(2) == 1 && msg_is("1501|1970|1502|1503|"));
    SL(2)[7] = 1;
    activity(2);
    CHECK(msg_is("1501|1971|1502|1503|"));
    SL(2)[7] = 9, SL(2)[10] = 20; /* tired: the phrase ends the line */
    activity(2);
    CHECK(msg_is("1501|9 1972|1502|1978|"));
    st[21] = 10;
    activity(2);
    CHECK(msg_is("1501|9 1972|1502|1981|"));
    /* the store: 1861 + SA */
    st = employ(41, 3, 4, 0, 0);
    put16(st + 6, 1), put16(st + 8, 2), put16(st + 16, 3), st[21] = 4;
    world_set_tail(&g_world, TAIL_HOUR, 6);
    activity(3);
    CHECK(msg_is("1859|1970|1860|1981|")); /* byte 21 below 20 */
    st[21] = 24; /* A = ((36 + 24) + 3) x 7 + 2 = 443, SA 3 */
    activity(3);
    CHECK(msg_is("1859|1970|1860|1864|"));
    world_set_tail(&g_world, TAIL_HOUR, 10);
    /* the treatment room: a man with a patient, a woman without one and the doctor */
    employ(42, 4, 5, 2, 0);
    put16(rec(5, 2) + 0x0C, 77);
    activity(4);
    CHECK(msg_is("1831|1970|1832|3 1833|1834|P77"));
    employ(43, 5, 5, 2, 1);
    activity(5);
    CHECK(msg_is("1837|1970|1832|3 1833|1838|P771839|"));
    put16(rec(5, 2) + 0x0C, 0), put16(rec(5, 2) + 0x0A, 42);
    activity(5);
    CHECK(msg_is("1837|1970|1832|3 1833|1841|S421842|"));
    /* the waiting room: the strip, or a seat's patient by r7 */
    employ(44, 6, 3, 0, 1);
    rec(3, 0)[0x0F] = 1;
    activity(6);
    CHECK(msg_is("1856|1970|1857|1 1858|1976|"));
    rec(3, 0)[0x0F] = 0;
    activity(6);
    CHECK(msg_is("1856|1970|1857|1 1858|1974|"));
    put16(rec(3, 0) + 0x0C, 1), put16(rec(3, 0) + 0x14 + 4 * 7, 88);
    int kinds = 0;
    for (int i = 0; i < 200; i++) {
        activity(6);
        CHECK(g_msg.len > 0 && test_memmem(g_msg.buf, g_msg.len, "P88", 3) != NULL);
        for (int k = 0; k < 8; k++) {
            static const char *B[8] = {"1975|", "1984|", "1986|", "1988|", "1989|", "1990|", "1991|", "1993|"};
            if (test_memmem(g_msg.buf, g_msg.len, B[k], 5)) kinds |= 1 << k;
        }
    }
    CHECK(kinds == 255);
    /* the ward: only a bed whose patient is in state 6 counts */
    employ(45, 7, 6, 1, 1);
    put16(rec(6, 1) + 0x0C, 2), put16(rec(6, 1) + 0x14, 90), put16(rec(6, 1) + 0x14 + 4 * 3, 91);
    P(90)[4] = 9, P(91)[4] = 6;
    for (int i = 0; i < 50; i++) {
        activity(7);
        CHECK(test_memmem(g_msg.buf, g_msg.len, "P90", 3) == NULL);
    }
    /* the OP: men of slot group 2 and the others */
    employ(46, 8, 7, 0, 0);
    SL(8)[2] = 2;
    put16(rec(7, 0) + 0x0C, 5);
    activity(8);
    CHECK(msg_is("1876|1970|1872|1 1873|1877|"));
    SL(8)[2] = 5;
    activity(8);
    CHECK(msg_is("1876|1970|1872|1 1873|1880|"));
    /* a duty state without a sentence: the buffer stays (the mood line) */
    employ(47, 9, 0x1A, 0, 0);
    msg_clear(&g_msg), msg_append(&g_msg, (const uint8_t *)"mood", 4);
    CHECK(sheet_staff_activity(&g_ctx, 9) == 1 && msg_is("mood"));
    /* custody: hours 0-3, then 1530 + (L +10 - L +7 + 2) + 1531 */
    employ(48, 10, 0x16, 0, 0);
    SL(10)[7] = 2;
    activity(10);
    CHECK(msg_is("1528|"));
    SL(10)[7] = 5, SL(10)[10] = 9;
    activity(10);
    CHECK(msg_is("1530|6 1531|"));
    /* the garage is garage_activity's */
    employ(49, 11, 10, 0, 0);
    CHECK(activity(11) == -1);
}

/* ---- tables by code, the drug part per treatment, every duty state ----------------------------------------------- */

static void test_capability_map(void) {
    /* docs/ui-sheets.md, "Instrument entries": code -> capability */
    static const uint8_t MAP[][2] = {
        {4, 13}, {43, 13}, {66, 13}, {101, 13}, {31, 15}, {68, 15}, {32, 18}, {33, 12}, {36, 9}, {103, 9}, {37, 25},
        {42, 25}, {65, 25}, {41, 6}, {48, 14}, {49, 40}, {51, 41}, {52, 41}, {53, 42}, {58, 2}, {88, 2}, {61, 24},
        {63, 32}, {85, 32}, {64, 49}, {70, 49}, {69, 33}, {78, 33}, {81, 33}, {104, 33}, {110, 33}, {80, 53},
        {107, 53}, {86, 10}, {87, 39}, {89, 58}, {90, 19}, {105, 62}, {106, 63},
    };
    fresh();
    ward_patient(10);
    put32(ITEM(7), 740), ITEM(7)[7] = 5;
    uint8_t *k = rec(6, 0);
    put16(k + 0x60, 7), k[0x70] = 50;
    int n = (int)(sizeof MAP / sizeof *MAP);
    for (int i = 0; i < n; i++) {
        int code = MAP[i][0], cap = MAP[i][1];
        TREAT(69)[0x10] = (uint8_t)code;
        both(0x6A71, 0x875B, code, computed("X "));
        memset(ITEM(7) + 10, 0, 4);
        ITEM(7)[11] = (uint8_t)cap;
        sheet(10);
        CHECK(line_is(SHEET_ACTIVITY, "X 1612|740|"));
        ITEM(7)[11] = (uint8_t)(cap + 1), ITEM(7)[10] = (uint8_t)(cap - 1);
        sheet(10);
        CHECK(line_is(SHEET_ACTIVITY, "X test device"));
    }
}

static void test_random_codes(void) {
    /* activity and mood random-text ranges per code (the mood run only in the neuro ward's own branch) */
    static const int CODE[5] = {18, 21, 22, 23, 46};
    static const int ACT[5][2] = {{3791, 4}, {3799, 4}, {3807, 5}, {3816, 5}, {3826, 5}};
    static const int MOOD[5][2] = {{3795, 4}, {3803, 4}, {3812, 4}, {3821, 5}, {3831, 5}};
    fresh();
    uint8_t *p = ward_patient(10);
    p[4] = 16;
    put16(rec(16, 0) + 8, 5), put16(rec(16, 0) + 6, 1);
    for (int i = 0; i < 5; i++) {
        TREAT(69)[0x10] = (uint8_t)CODE[i];
        both(0x6A71, 0x875B, CODE[i], computed("x"));
        both(0x6C2D, 0x8917, CODE[i], computed("y"));
        int a = 0, m = 0;
        for (int t = 0; t < 300; t++) {
            sheet(10);
            int x = atoi((const char *)g_page.line[SHEET_ACTIVITY].buf) - ACT[i][0];
            int y = atoi((const char *)g_page.line[SHEET_MOOD].buf) - MOOD[i][0];
            CHECK(x >= 0 && x < ACT[i][1] && y >= 0 && y < MOOD[i][1]);
            if (x >= 0 && x < 8) a |= 1 << x;
            if (y >= 0 && y < 8) m |= 1 << y;
        }
        CHECK(a == (1 << ACT[i][1]) - 1 && m == (1 << MOOD[i][1]) - 1);
    }
}

static void test_drugs(void) {
    fresh();
    uint8_t *p = ward_patient(10);
    p[57] = 0, p[80] = 2, p[42] = 4;
    ILL(4)[5] = 3, ILL(4)[10] = 4, ILL(4)[15] = 5, ILL(4)[1] = 6;
    static const struct { int t; const char *want; } CASES[] = {
        {3, "3780|"}, {4, "3781|"}, {5, "3118|"}, {6, "3491|3691|3492|3661|"}, {9, "3691|3199|3490|"},
        {14, "3691|3205|"}, {16, "3486|"}, {18, "3488|"}, {19, "3489|"}, {28, "3494|"}, {29, "3495|"}, {59, "3496|"},
        {11, ""},
    };
    for (size_t i = 0; i < sizeof CASES / sizeof *CASES; i++) {
        int t = CASES[i].t;
        p[43] = (uint8_t)t;
        put16(TREAT(t) + 0x1E, 800), put32(TREAT(t) + 0x1A, 1);
        sheet(10);
        char want[64];
        snprintf(want, sizeof want, "800|%s", CASES[i].want);
        CHECK(line_is(SHEET_TREATMENT, want));
    }
}

static int sel_of(int index, int which) {
    return sheet_selector(ST(index), world_tail(&g_world, TAIL_DAY), world_tail(&g_world, TAIL_HOUR), which);
}
static int activity(int slot);
static uint8_t *employ(int index, int slot, int state, int room, int sex);
static int line_eq(int slot, const char *want) {
    activity(slot);
    return msg_is(want);
}

static void test_every_state(void) {
    fresh();
    char w[256];
    uint8_t *st;
    /* 2 reception */
    employ(70, 1, 2, 0, 1);
    rec(2, 0)[0x45] = 1;
    CHECK(line_eq(1, "1504|1970|1505|1976|"));
    rec(2, 0)[0x45] = 0;
    CHECK(line_eq(1, "1504|1970|1505|1973|"));
    put16(rec(2, 0) + 0x0A + 4 * 2, 95);
    activity(1);
    CHECK(test_memmem(g_msg.buf, g_msg.len, "P95", 3) != NULL);
    /* 5 treatment room, the man's nurse */
    employ(71, 2, 5, 0, 0);
    put16(rec(5, 0) + 8, 33);
    CHECK(line_eq(2, "1831|1970|1832|1 1833|1836|S33"));
    put16(rec(5, 0) + 8, 0);
    CHECK(line_eq(2, "1831|1970|1832|1 1833|1835|"));
    employ(72, 3, 5, 0, 1);
    CHECK(line_eq(3, "1837|1970|1832|1 1833|1840|"));
    /* 6 ward: SA 0-3 */
    st = employ(73, 4, 6, 0, 1);
    put16(rec(6, 0) + 0x0C, 1);
    for (int i = 0; i < 10; i++) put16(rec(6, 0) + 0x14 + 4 * i, 96);
    P(96)[4] = 6;
    int sas = 0;
    for (int v = 0; v < 16; v++) {
        put16(st + 6, v);
        int sa = sel_of(73, SEL_A);
        sas |= 1 << sa;
        activity(4);
        if (sa == 2) snprintf(w, sizeof w, "1865|1970|1866|1 1867|1868|P96");
        else if (sa < 2) snprintf(w, sizeof w, "1865|1970|1866|1 1867|%d|", 1869 + sa);
        else continue;
        CHECK(msg_is(w));
    }
    CHECK(sas == 15);
    rec(6, 0)[0x0F] = 1;
    CHECK(line_eq(4, "1865|1970|1866|1 1867|1976|"));
    /* 7 OP woman, men of the slot groups */
    employ(74, 5, 7, 0, 1);
    rec(7, 0)[0x17] = 1;
    CHECK(line_eq(5, "1871|1970|1872|1 1873|1976|"));
    rec(7, 0)[0x17] = 0, put16(rec(7, 0) + 0x0C, 4);
    CHECK(line_eq(5, "1871|1970|1872|1 1873|1874|"));
    put16(rec(7, 0) + 0x0C, 0);
    CHECK(line_eq(5, "1871|1970|1872|1 1873|1875|"));
    employ(75, 6, 7, 0, 0);
    SL(6)[2] = 2;
    CHECK(line_eq(6, "1876|1970|1872|1 1873|1878|"));
    SL(6)[2] = 3;
    CHECK(line_eq(6, "1876|1970|1872|1 1873|1879|"));
    /* 8 torture */
    employ(76, 7, 8, 0, 0);
    put16(rec(8, 0) + 0x0C, 9);
    CHECK(line_eq(7, "1886|1970|1882|1 1883|1888|P9"));
    put16(rec(8, 0) + 0x0C, 0);
    CHECK(line_eq(7, "1886|1970|1882|1 1883|1887|"));
    employ(77, 8, 8, 0, 1);
    CHECK(line_eq(8, "1881|1970|1882|1 1883|1885|"));
    put16(rec(8, 0) + 0x0C, 9);
    CHECK(line_eq(8, "1881|1970|1882|1 1883|1884|"));
    /* 9 intensive care */
    employ(78, 9, 9, 0, 1);
    CHECK(line_eq(9, "1889|1970|1890|1 1891|1893|"));
    put16(rec(9, 0) + 0x0C, 12);
    CHECK(line_eq(9, "1889|1970|1890|1 1891|1892|P12"));
    rec(9, 0)[0x0F] = 1;
    CHECK(line_eq(9, "1889|1970|1890|1 1891|1976|"));
    /* 11 dental: the man */
    employ(79, 10, 11, 0, 0);
    CHECK(line_eq(10, "1904|1970|1905|1 1906|1909|"));
    put16(rec(11, 0) + 8, 34);
    CHECK(line_eq(10, "1904|1970|1905|1 1906|1910|S341911|"));
    rec(11, 0)[0x19] = 1;
    CHECK(line_eq(10, "1904|1970|1905|1 1906|1912|S341913|"));
    put16(rec(11, 0) + 0x0C, 13);
    CHECK(line_eq(10, "1904|1970|1905|1 1906|1908|P13"));
    /* the woman */
    employ(80, 11, 11, 0, 1);
    CHECK(line_eq(11, "1914|1970|1905|1 1907|1976|"));
    rec(11, 0)[0x19] = 0;
    CHECK(line_eq(11, "1914|1970|1905|1 1907|1915|P131916|"));
    put16(rec(11, 0) + 0x0C, 0);
    CHECK(line_eq(11, "1914|1970|1905|1 1907|1917|"));
    put16(rec(11, 0) + 0x0A, 35);
    CHECK(line_eq(11, "1914|1970|1905|1 1907|1918|S35"));
    /* 12 golf: men by SA */
    st = employ(81, 12, 12, 0, 0);
    put16(rec(12, 0) + 2, 36);
    for (int v = 0; v < 8; v++) {
        put16(st + 6, v);
        static const char *G[4] = {"242|S36243|", "244|", "245|S36246|", "247|"};
        CHECK(line_eq(12, G[sel_of(81, SEL_A)]));
    }
    /* 13 kitchen: r1 */
    employ(82, 13, 13, 0, 0);
    int kitchen = 0;
    for (int i = 0; i < 100; i++) {
        activity(13);
        kitchen |= msg_q("1919|1970|1920|1921|") ? 1 : 0;
        kitchen |= g_msg.len > 15 && memcmp(g_msg.buf + g_msg.len - 5, "1922|", 5) == 0 ? 2 : 0;
    }
    CHECK(kitchen == 3);
    /* 14 club: men by SA */
    st = employ(83, 14, 14, 0, 0);
    put16(rec(14, 0) + 2, 37), put16(rec(14, 0) + 4, 38);
    for (int v = 0; v < 8; v++) {
        put16(st + 6, v);
        static const char *C[4] = {"762|763|S38764|", "762|765|S38766|", "762|767|S37768|", "762|769|S37770|"};
        CHECK(line_eq(14, C[sel_of(83, SEL_A)]));
    }
    /* 15 neurology */
    employ(84, 15, 15, 0, 0);
    CHECK(line_eq(15, "1923|1970|1924|1 1925|1927|"));
    put16(rec(15, 0) + 8, 39);
    CHECK(line_eq(15, "1923|1970|1924|1 1925|1928|S391929|"));
    put16(rec(15, 0) + 0x0C, 14);
    CHECK(line_eq(15, "1923|1970|1924|1 1925|1926|P14"));
    employ(85, 16, 15, 0, 1);
    CHECK(line_eq(16, "237|1970|1924|1 1925|1930|P141931|"));
    put16(rec(15, 0) + 0x0C, 0);
    CHECK(line_eq(16, "237|1970|1924|1 1925|1932|"));
    put16(rec(15, 0) + 0x0A, 40);
    CHECK(line_eq(16, "237|1970|1924|1 1925|1933|S401934|"));
    /* 16 neuro ward: r3 */
    employ(86, 17, 16, 0, 1);
    CHECK(line_eq(17, "1935|1970|1936|1 1937|1974|"));
    put16(rec(16, 0) + 0x0C, 1);
    for (int i = 0; i < 10; i++) put16(rec(16, 0) + 0x14 + 4 * i, 97);
    int ward = 0;
    for (int i = 0; i < 200; i++) {
        activity(17);
        ward |= msg_q("1935|1970|1936|1 1937|1939|") ? 1 : 0;
        ward |= msg_q("1935|1970|1936|1 1937|1940|") ? 2 : 0;
        ward |= msg_q("1935|1970|1936|1 1937|1938|P97") ? 4 : 0;
    }
    CHECK(ward == 7);
    rec(16, 0)[0x0F] = 1;
    CHECK(line_eq(17, "1935|1970|1936|1 1937|1976|"));
    /* 17 bank */
    employ(87, 18, 17, 0, 0);
    CHECK(line_eq(18, "1661|1970|1662|1663|"));
    put16(rec(17, 0) + 0x14, 1);
    CHECK(line_eq(18, "1661|1970|1662|1664|"));
    employ(88, 19, 17, 0, 1);
    put32(rec(17, 0) + 0x0C, 99);
    CHECK(line_eq(19, "1665|1970|1666|1667|"));
    put32(rec(17, 0) + 0x0C, 100);
    CHECK(line_eq(19, "1665|1970|1666|1668|"));
    /* 18 pathology */
    employ(89, 20, 18, 0, 0);
    CHECK(line_eq(20, "1941|1970|1942|1943|"));
    put16(rec(18, 0) + 0x0C, 15);
    CHECK(line_eq(20, "1941|1970|1942|1944|P15"));
    /* 0x14 / 0x15 without a target, 0x14 below 11, the four 0x15 levels */
    employ(90, 21, 0x14, 0, 0);
    snprintf(w, sizeof w, "1945|1970|1946|%d|", 1951 + sel_of(90, SEL_D));
    CHECK(line_eq(21, w));
    put16(SL(21) + 0x0C, 16), SL(21)[0x0E] = 10;
    CHECK(line_eq(21, "1945|1970|1946|1949|P161950|"));
    employ(91, 22, 0x15, 0, 0);
    snprintf(w, sizeof w, "1959|1970|1960|%d|", 1962 + sel_of(91, SEL_D));
    CHECK(line_eq(22, w));
    put16(SL(22) + 0x0C, 41), SL(22)[0x0F] = 0;
    for (int k = 0; k < 8; k++) put16(g_image + 0x600F + 2 * k, 2006 + k);
    static const uint8_t LEVEL[4] = {45, 35, 25, 15};
    for (int k = 0; k < 4; k++) {
        SL(22)[0x0E] = LEVEL[k];
        snprintf(w, sizeof w, "%d|S41%d|", 2006 + 2 * k, 2007 + 2 * k);
        CHECK(line_eq(22, w));
    }
    /* 0x17 by hour, 0x18 by SF */
    employ(92, 23, 0x17, 0, 0);
    static const struct { int h, t; } DAY[] = {{4, 1510}, {5, 1511}, {7, 1513}, {8, 1514}, {16, 1523}, {18, 1525}, {23, 1525}};
    for (size_t i = 0; i < sizeof DAY / sizeof *DAY; i++) {
        world_set_tail(&g_world, TAIL_HOUR, DAY[i].h);
        snprintf(w, sizeof w, "%d|", DAY[i].t);
        CHECK(line_eq(23, w));
    }
    world_set_tail(&g_world, TAIL_HOUR, 12);
    snprintf(w, sizeof w, "%d|", 1515 + sel_of(92, SEL_D));
    CHECK(line_eq(23, w));
    employ(93, 24, 0x18, 0, 0);
    snprintf(w, sizeof w, "%d|", 1506 + sel_of(93, SEL_F));
    CHECK(line_eq(24, w));
    world_set_tail(&g_world, TAIL_HOUR, 10);
}

static void test_home_rules(void) {
    fresh();
    char w[64];
    uint8_t *st = employ(100, 1, 0x13, 0, 0);
    static const int A[10] = {1418, 0, 1430, 1414, 1434, 1438, 1442, 1446, 1450, 1454};
    static const int B[10] = {2476, 2483, 2490, 2497, 2504, 2511, 2518, 2525, 2532, 2539};
    static const int N[10] = {1791, 1799, 1807, 1815, 2102, 2110, 2118, 2126, 2134, 1823};
    int seen_a = 0, seen_n = 0;
    for (int group = 0; group <= 10; group++) {
        int g = group <= 9 ? group : 0;
        st[1] = (uint8_t)group;
        for (int v = 0; v < 64; v++) {
            put16(st + 6, v), put16(st + 8, v * 7);
            /* 9 hours or more at h >= 4: group rule B */
            SL(1)[7] = 9, world_set_tail(&g_world, TAIL_HOUR, 10);
            int sd = sel_of(100, SEL_D);
            if (sd == 7) snprintf(w, sizeof w, "1409|9 1972|1410|1413|");
            else snprintf(w, sizeof w, "%d|", B[g] + sd);
            CHECK(line_eq(1, w));
            /* 1-8 hours at h >= 8: SB = 0 -> group rule A, else (2 hours) the night rule, awake: = N + SE */
            SL(1)[7] = 2;
            if (sel_of(100, SEL_B) == 0) {
                int sa = sel_of(100, SEL_A);
                sd = sel_of(100, SEL_D);
                if (group == 1) {
                    if (sd < 4) snprintf(w, sizeof w, "%d|", 1426 + sd);
                    else snprintf(w, sizeof w, "1409|2 1972|1410|%d|", 1422 + sd - 4);
                } else if (group == 7) snprintf(w, sizeof w, "1409|2 1972|1410|%d|", 1446 + sa);
                else snprintf(w, sizeof w, "%d|", A[g] + sa);
                CHECK(line_eq(1, w));
                seen_a++;
            } else {
                snprintf(w, sizeof w, "%d|", N[g] + sel_of(100, SEL_E));
                CHECK(line_eq(1, w));
                seen_n++;
            }
            /* 1 hour, SB != 0: group rule B */
            SL(1)[7] = 1;
            if (sel_of(100, SEL_B) != 0) {
                sd = sel_of(100, SEL_D);
                if (sd == 7) snprintf(w, sizeof w, "1409|1971|1410|1413|");
                else snprintf(w, sizeof w, "%d|", B[g] + sd);
                CHECK(line_eq(1, w));
            }
            /* 1-8 hours before 8 o'clock, and 9 or more before 4: the night rule, asleep before 7 */
            SL(1)[7] = 4, world_set_tail(&g_world, TAIL_HOUR, 6);
            CHECK(line_eq(1, "1409|4 1972|1410|1411|"));
            SL(1)[7] = 12, world_set_tail(&g_world, TAIL_HOUR, 3);
            CHECK(line_eq(1, "1409|12 1972|1410|1411|"));
        }
    }
    CHECK(seen_a > 20 && seen_n > 20);
    for (int k = 0; k < 6; k++) { /* the thug-attack reactions FE ... F9 */
        st[23] = (uint8_t)(0xFE - k), st[26] = 2;
        put16(g_image + 0x604F + 24 + 2 * k, 2038 + k);
        snprintf(w, sizeof w, "1409|12 1972|1410|%d|", 2038 + k);
        CHECK(line_eq(1, w));
    }
    world_set_tail(&g_world, TAIL_HOUR, 10);
}

static void test_course_rules(void) {
    fresh();
    char w[64];
    uint8_t *st = employ(101, 1, 0x19, 1, 1);
    uint8_t *c = g_world.block[BLK_COURSES] + 10;
    c[4] = 2, c[3] = 0;
    put16(g_image + 0x40D9 + 44, 3002);
    CHECK(line_eq(1, "59|3002|60|"));
    c[3] = 1;
    world_set_tail(&g_world, TAIL_HOUR, 3);
    CHECK(line_eq(1, "61|"));
    world_set_tail(&g_world, TAIL_HOUR, 21);
    snprintf(w, sizeof w, "%d|", 62 + sel_of(101, SEL_F));
    CHECK(line_eq(1, w));
    st[0] = 0;
    snprintf(w, sizeof w, "%d|", 66 + sel_of(101, SEL_F));
    CHECK(line_eq(1, w));
    world_set_tail(&g_world, TAIL_HOUR, 12);
    CHECK(line_eq(1, "71|"));
    for (int k = 0; k < 4; k++) put16(g_image + 0x40D9 + 44 + 0x0E + 2 * k, 3010 + k);
    world_set_tail(&g_world, TAIL_HOUR, 9);
    snprintf(w, sizeof w, "%d|", 3010 + sel_of(101, SEL_G));
    CHECK(line_eq(1, w));
    world_set_tail(&g_world, TAIL_HOUR, 14);
    snprintf(w, sizeof w, "%d|", 3012 + sel_of(101, SEL_G));
    CHECK(line_eq(1, w));
    world_set_tail(&g_world, TAIL_HOUR, 10);
}

static void test_reception_waiting_page(void) {
    fresh();
    uint8_t *p = P(10);
    memset(p, 0, 106);
    p[4] = 2, p[44] = 70, p[78] = 70, p[20] = 40, p[56] = 3, p[41] = 1;
    sheet(10);
    CHECK(line_is(SHEET_DIAGNOSIS, "2093|") && line_is(SHEET_TREATMENT, "2101|") && line_is(SHEET_ACTIVITY, "2175|"));
    CHECK(line_is(SHEET_MOOD, "2401|") && line_is(SHEET_OPINION, "2385|"));
    p[44] = 60;
    sheet(10);
    CHECK(line_is(SHEET_MOOD, "2394|"));
    p[44] = 20;
    sheet(10);
    CHECK(line_is(SHEET_MOOD, "2081|") && line_is(SHEET_OPINION, "2085|"));
    p[4] = 3, p[66] = 1;
    sheet(10);
    CHECK(line_is(SHEET_DIAGNOSIS, "2096|") && line_is(SHEET_ACTIVITY, "2174|"));
    rec(3, 1)[0x0F] = 5;
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2173|"));
    p[44] = 25;
    sheet(10);
    CHECK(line_is(SHEET_OPINION, "2385|"));
    p[104] = 1;
    sheet(10);
    CHECK(line_is(SHEET_MOOD, "2080|") && line_is(SHEET_OPINION, "2082|") && line_is(SHEET_COMPLAINTS, "235|"));
}

/* the boundaries of the branch tests (from the first mutation round's survivors) */
static void test_boundaries(void) {
    fresh();
    uint8_t *p = ward_patient(10);
    p[44] = 25; /* health 25 is not "below 25": the symptom with severity (100 - 25) / 16 */
    sheet(10);
    CHECK(line_is(SHEET_COMPLAINTS, "2380|3292|"));
    p[44] = 24;
    sheet(10);
    CHECK(line_is(SHEET_COMPLAINTS, "234|"));
    p[44] = 36; /* (100 - 36) / 16 = 4 */
    sheet(10);
    CHECK(line_is(SHEET_COMPLAINTS, "2380|3292|"));
    p[44] = 70, p[40] = 80;
    sheet(10);
    CHECK(line_is(SHEET_MOOD, "1783|"));
    p[40] = 79;
    sheet(10);
    CHECK(line_is(SHEET_MOOD, "1784|"));
    p[40] = 0;
    TREAT(69)[0x24] = 1, TREAT(69)[0x25] = 1, TREAT(69)[0x0A] = 2; /* interval byte 2: already a 3110 text */
    sheet(10);
    CHECK(line_is(SHEET_TREATMENT, "500|2069|0 3112|"));
    TREAT(69)[0x24] = 0;
    /* treatment room: diagnosing lines, the OP's missing capability R +4 */
    treatment_room_patient(11);
    put16(rec(5, 1) + 0x0E, 5);
    sheet(11);
    CHECK(line_is(SHEET_DIAGNOSIS, "2094|") && line_is(SHEET_TREATMENT, "2100|"));
    uint8_t *q = P(12);
    memset(q, 0, 106);
    q[4] = 7, q[56] = 3, q[42] = 3, q[43] = 30;
    put16(rec(7, 0) + 0x0E, 999), rec(7, 0)[4] = 6;
    sheet(12);
    CHECK(line_is(SHEET_ACTIVITY, "3049|"));
    /* dental FD and FE, byte 5 / 6 of the neurology */
    q = P(13);
    treatment_room_patient(13);
    q[4] = 11;
    uint8_t *d = rec(11, 1);
    put16(d + 0x0E, 0), put16(d + 0x10, 0);
    q[43] = 0xFD;
    sheet(13);
    CHECK(line_is(SHEET_DIAGNOSIS, "2090|") && line_is(SHEET_TREATMENT, "2097|") && line_is(SHEET_ACTIVITY, "2162|"));
    q[43] = 0xFE;
    sheet(13);
    CHECK(line_is(SHEET_DIAGNOSIS, "2092|") && line_is(SHEET_TREATMENT, "2099|"));
    q[43] = 30, TREAT(30)[5] = 1;
    sheet(13);
    CHECK(line_is(SHEET_ACTIVITY, "2153|"));
    q[4] = 15;
    put16(rec(15, 1) + 0x0E, 0), put16(rec(15, 1) + 0x10, 0);
    sheet(13);
    CHECK(line_is(SHEET_ACTIVITY, "2153|"));
    TREAT(30)[5] = 0;
    sheet(13);
    CHECK(line_is(SHEET_ACTIVITY, "2163|"));
    TREAT(30)[6] = 1;
    sheet(13);
    CHECK(line_is(SHEET_ACTIVITY, "2156|"));
    /* personnel: tiredness thresholds, the patient phrase's endings, thugs' levels, the golf day's hours */
    uint8_t *st = employ(110, 1, 0, 0, 0);
    static const struct { int h; const char *end; } TIRED[] = {
        {13, "1503|"}, {14, "1977|"}, {19, "1977|"}, {20, "1978|"}, {25, "1978|"}, {26, "1979|"}, {31, "1979|"}, {32, "1980|"}};
    char w[64];
    for (size_t i = 0; i < sizeof TIRED / sizeof *TIRED; i++) {
        SL(1)[10] = (uint8_t)TIRED[i].h;
        snprintf(w, sizeof w, "1501|1970|1502|%s", TIRED[i].end);
        CHECK(line_eq(1, w));
    }
    st[21] = 20, SL(1)[10] = 0;
    CHECK(line_eq(1, "1501|1970|1502|1503|"));
    st[21] = 19;
    CHECK(line_eq(1, "1501|1970|1502|1981|"));
    employ(111, 2, 3, 0, 1);
    put16(rec(3, 0) + 0x0C, 1);
    for (int i = 0; i < 10; i++) put16(rec(3, 0) + 0x14 + 4 * i, 98);
    int ends = 0;
    for (int i = 0; i < 400; i++) {
        activity(2);
        static const char *E[3] = {"1984|P981985|", "1986|P981987|", "1991|P981992|"};
        for (int k = 0; k < 3; k++)
            if (g_msg.len > strlen(E[k]) && memcmp(g_msg.buf + g_msg.len - strlen(E[k]), E[k], strlen(E[k])) == 0) ends |= 1 << k;
    }
    CHECK(ends == 7);
    employ(112, 3, 0x14, 0, 0);
    put16(SL(3) + 0x0C, 16), SL(3)[0x0E] = 11, SL(3)[0x0F] = 0;
    put16(g_image + 0x5FF3, 1994), put16(g_image + 0x5FF3 + 2, 1995);
    CHECK(line_eq(3, "1945|1970|1946|1994|P161995|"));
    SL(3)[0x0E] = 21;
    CHECK(line_eq(3, "1945|1970|1946|1947|P161948|"));
    SL(3)[0x0E] = 20;
    CHECK(line_eq(3, "1945|1970|1946|1994|P161995|"));
    employ(113, 4, 0x15, 0, 0);
    put16(SL(4) + 0x0C, 41), SL(4)[0x0F] = 0;
    for (int k = 0; k < 8; k++) put16(g_image + 0x600F + 2 * k, 2006 + k);
    static const struct { int v; int t; } LV[] = {{11, 2012}, {10, 1961}, {41, 2006}, {40, 2008}, {31, 2008}, {30, 2010}, {21, 2010}, {20, 2012}};
    for (size_t i = 0; i < sizeof LV / sizeof *LV; i++) {
        SL(4)[0x0E] = (uint8_t)LV[i].v;
        if (LV[i].t == 1961) snprintf(w, sizeof w, "1961|");
        else snprintf(w, sizeof w, "%d|S41%d|", LV[i].t, LV[i].t + 1);
        CHECK(line_eq(4, w));
    }
    employ(114, 5, 0x17, 0, 0);
    world_set_tail(&g_world, TAIL_HOUR, 9);
    snprintf(w, sizeof w, "%d|", 1515 + sel_of(114, SEL_D));
    CHECK(line_eq(5, w));
    world_set_tail(&g_world, TAIL_HOUR, 15);
    snprintf(w, sizeof w, "%d|", 1515 + sel_of(114, SEL_D));
    CHECK(line_eq(5, w));
    world_set_tail(&g_world, TAIL_HOUR, 17);
    CHECK(line_eq(5, "1524|"));
    /* at home: 8 hours before 8 o'clock is night, 9 hours at 4 is rule B; asleep from 22, awake at 7 */
    st = employ(115, 6, 0x13, 0, 0);
    st[1] = 3;
    SL(6)[7] = 8, world_set_tail(&g_world, TAIL_HOUR, 7);
    snprintf(w, sizeof w, "%d|", 1815 + sel_of(115, SEL_E));
    CHECK(line_eq(6, w));
    SL(6)[7] = 9, world_set_tail(&g_world, TAIL_HOUR, 4);
    int sd = sel_of(115, SEL_D);
    if (sd == 7) snprintf(w, sizeof w, "1409|9 1972|1410|1413|");
    else snprintf(w, sizeof w, "%d|", 2497 + sd);
    CHECK(line_eq(6, w));
    /* the course's afternoon from 12 on: hour 13 */
    employ(116, 7, 0x19, 1, 1);
    uint8_t *c = g_world.block[BLK_COURSES] + 10;
    c[4] = 2, c[3] = 1, c[1] = 0;
    for (int k = 0; k < 4; k++) put16(g_image + 0x40D9 + 44 + 0x0E + 2 * k, 3010 + k);
    /* the night rule asleep from 22 (2 hours at home with SB != 0), and rule A / night at 8 o'clock */
    for (int v = 0; v < 32; v++) {
        put16(st + 6, v);
        SL(6)[7] = 2, world_set_tail(&g_world, TAIL_HOUR, 22);
        if (sel_of(115, SEL_B)) CHECK(line_eq(6, "1409|2 1972|1410|1411|"));
        world_set_tail(&g_world, TAIL_HOUR, 8);
        if (!sel_of(115, SEL_B)) {
            snprintf(w, sizeof w, "%d|", 1414 + sel_of(115, SEL_A));
            CHECK(line_eq(6, w));
        }
    }
    world_set_tail(&g_world, TAIL_HOUR, 13);
    snprintf(w, sizeof w, "%d|", 3012 + sel_of(116, SEL_G));
    CHECK(line_eq(7, w));
    world_set_tail(&g_world, TAIL_HOUR, 11);
    snprintf(w, sizeof w, "%d|", 3010 + sel_of(116, SEL_G));
    CHECK(line_eq(7, w));
    world_set_tail(&g_world, TAIL_HOUR, 10);
}

static void test_club_golf(void) {
    fresh();
    /* the club's bar woman: flag set and nobody listed hangs in the original; nothing is drawn */
    employ(50, 1, 14, 0, 1);
    uint8_t *club = rec(14, 0);
    put16(club + 4, 50), club[0x19] = 1;
    CHECK(activity(1) == 0);
    put16(club + 6 + 2 * 5, 61);
    CHECK(activity(1) == 1 && msg_is("757|1970|758|760|S61761|"));
    club[0x19] = 0;
    activity(1);
    CHECK(msg_is("757|1970|758|759|"));
    put16(club + 4, 0), put16(club + 2, 50); /* the other club woman */
    activity(1);
    CHECK(msg_is("752|1970|753|754|"));
    put16(club + 2, 0);
    activity(1);
    CHECK(msg_is("771|"));
    /* the golf room's woman: flag byte 5, ten members */
    employ(51, 2, 12, 0, 1);
    uint8_t *golf = rec(12, 0);
    golf[5] = 1;
    CHECK(activity(2) == 0);
    put16(golf + 6 + 2 * 9, 62);
    CHECK(activity(2) == 1 && msg_is("237|1970|238|240|S62241|"));
    golf[5] = 0;
    activity(2);
    CHECK(msg_is("237|1970|238|239|"));
}

static void test_home_and_thugs(void) {
    fresh();
    uint8_t *st = employ(60, 1, 0x13, 3, 0);
    st[23] = 0xFC; /* word 2 of the row 0x604F + 12 x 3 */
    put16(g_image + 0x604F + 12 * 3 + 4, 2044);
    activity(1);
    CHECK(msg_is("1409|1970|1410|2044|"));
    st[23] = 0;
    activity(1);
    CHECK(msg_is("1409|1970|1410|1412|"));
    SL(1)[7] = 3;
    world_set_tail(&g_world, TAIL_HOUR, 6); /* 1-8 hours before 8: the night rule, h < 7 */
    activity(1);
    CHECK(msg_is("1409|3 1972|1410|1411|"));
    world_set_tail(&g_world, TAIL_HOUR, 7); /* not asleep: = the group's text + SE */
    st[1] = 4;
    activity(1);
    CHECK(g_msg.len >= 5 && atoi((const char *)g_msg.buf) >= 2102 && atoi((const char *)g_msg.buf) <= 2109);
    /* the thug's person mission: 1945 + duration + 1946, then by L +0x0E */
    employ(61, 2, 0x14, 0, 0);
    put16(SL(2) + 0x0C, 300), SL(2)[0x0E] = 25;
    activity(2);
    CHECK(msg_is("1945|1970|1946|1947|P3001948|"));
    SL(2)[0x0E] = 15, SL(2)[0x0F] = 2;
    put16(g_image + 0x5FF3 + 8, 1998), put16(g_image + 0x5FF3 + 10, 1999);
    activity(2);
    CHECK(msg_is("1945|1970|1946|1998|P3001999|"));
    /* the staff mission */
    employ(62, 3, 0x15, 0, 0);
    put16(SL(3) + 0x0C, 70), SL(3)[0x0E] = 33, SL(3)[0x0F] = 1;
    put16(g_image + 0x600F + 16 + 4, 2012), put16(g_image + 0x600F + 16 + 6, 2013);
    activity(3);
    CHECK(msg_is("2012|S702013|"));
    SL(3)[0x0E] = 5;
    activity(3);
    CHECK(msg_is("1961|"));
    /* the golf day and the lunch break */
    employ(63, 4, 0x17, 0, 0);
    world_set_tail(&g_world, TAIL_HOUR, 17);
    activity(4);
    CHECK(msg_is("1524|"));
    world_set_tail(&g_world, TAIL_HOUR, 6);
    activity(4);
    CHECK(msg_is("1512|"));
    /* a course: C +1 set, then the hour's texts */
    st = employ(64, 5, 0x19, 2, 1);
    uint8_t *c = g_world.block[BLK_COURSES] + 20;
    c[1] = 1, c[4] = 3;
    put16(g_image + 0x40D9 + 22 * 3, 3000);
    activity(5);
    CHECK(msg_is("57|3000|58|"));
    c[1] = 0, c[3] = 1;
    world_set_tail(&g_world, TAIL_HOUR, 19);
    activity(5);
    CHECK(msg_is("70|"));
    world_set_tail(&g_world, TAIL_HOUR, 14);
    put16(g_image + 0x40D9 + 22 * 3 + 0x12, 3001), put16(g_image + 0x40D9 + 22 * 3 + 0x14, 3001);
    activity(5);
    CHECK(msg_is("3001|"));
}

/* ---- the warehouse animation, the skip-hour loop and the animation tick ------------------------------------------ */

static Biing g_b;
static Image g_img[64];
static uint8_t g_px[64];

/* image n (600-607, 620-623, 632, 633): 1 x 1 at x = n - 600 in colour n - 590 */
static void fake_image(int n) {
    int k = n - 600;
    g_img[k] = (Image){n, 1, 1, k, 0, 0, &g_px[k], NULL};
    g_px[k] = (uint8_t)(n - 590);
    g_b.ui.cache[n] = &g_img[k];
}

static void test_warehouse(void) {
    fresh();
    memset(&g_b, 0, sizeof g_b);
    g_b.prog = g_prog;
    world_alloc(&g_b.world);
    sim_init(&g_b.sim, &g_b.world, &g_b.prog, 3);
    g_b.screen.w = SCREEN_W, g_b.screen.h = SCREEN_H;
    g_b.ui.screen = &g_b.screen;
    g_b.ui.prog = &g_b.prog;
    g_b.ui.hover_rect = (Rect){0, 0, -1, -1};
    g_b.anims_on = 1;
    static const uint16_t SPIDER[32] = {600, 601, 600, 601, 602, 603, 604, 605, 606, 607, 606, 607, 606, 607, 606, 607,
                                        606, 607, 606, 605, 604, 603, 602, 601, 600, 601, 600, 601, 600, 601, 600, 601};
    static const uint16_t HAND[16] = {620, 622, 620, 621, 620, 622, 621, 623, 621, 623, 622, 620, 622, 620, 621, 620};
    for (int i = 0; i < 32; i++) put16(g_image + 0x60FF + 2 * i, SPIDER[i]);
    for (int i = 0; i < 16; i++) put16(g_image + 0x6147 + 2 * i, HAND[i]);
    static const uint16_t MOUTH[4] = {632, 633, 632, 633};
    for (int i = 0; i < 4; i++) put16(g_image + 0xE50B + 2 * i, MOUTH[i]);
    for (int n = 600; n <= 607; n++) fake_image(n);
    for (int n = 620; n <= 623; n++) fake_image(n);
    fake_image(632), fake_image(633);
    uint8_t *pb = g_b.world.block[BLK_PLAYER0];
    /* no tick: nothing drawn */
    purchase_routine(&g_b, 0x4A2AB);
    for (int x = 0; x < 40; x++) CHECK(g_b.screen.px[x] == 0);
    /* 64 ticks: the spider loop twice, in order */
    int order_ok = 1, hands = 0;
    for (int t = 0; t < 64; t++) {
        memset(g_b.screen.px, 0, 40);
        pb[0x3C] = 1;
        purchase_routine(&g_b, 0x4A2AB);
        CHECK(pb[0x3C] == 0);
        int k = SPIDER[t % 32] - 600;
        order_ok &= g_b.screen.px[k] == SPIDER[t % 32] - 590;
        for (int n = 620; n <= 623; n++) hands += g_b.screen.px[n - 600] != 0;
        for (int n = 632; n <= 633; n++) CHECK(g_b.screen.px[n - 600] == 0); /* no clerk: no mouth */
    }
    CHECK(order_ok);
    CHECK(hands < 16); /* r15 = 0: 4 expected */
    /* the hand: 1 in 16 ticks, the loop in its own order */
    int hand_draws = 0, hand_order = 1;
    for (int t = 0; t < 1600; t++) {
        memset(g_b.screen.px, 0, 40);
        pb[0x3C] = 1;
        int at = g_b.warehouse_hand;
        purchase_routine(&g_b, 0x4A2AB);
        int drawn = 0;
        for (int n = 620; n <= 623; n++) drawn += g_b.screen.px[n - 600] != 0;
        if (drawn)
            hand_draws++, hand_order &= drawn == 1 && g_b.screen.px[HAND[at / 2] - 600] != 0 &&
                                        g_b.warehouse_hand == (at + 2) % 0x20;
        else hand_order &= g_b.warehouse_hand == at;
    }
    CHECK(hand_order);
    CHECK(hand_draws > 60 && hand_draws < 145); /* 100 expected */
    /* a clerk: the mouth is drawn only when it changes (r3 = 0, r1 = 0, then table[r3]) */
    put16(sim_room_record(&g_b.sim, 0, 4, 0) + 4, 9);
    int mouths = 0, changes = 0, last = 0;
    for (int t = 0; t < 4000; t++) {
        memset(g_b.screen.px, 0, 40);
        pb[0x3C] = 1;
        purchase_routine(&g_b, 0x4A2AB);
        int m = g_b.screen.px[32] ? 632 : g_b.screen.px[33] ? 633 : 0;
        CHECK(!(g_b.screen.px[32] && g_b.screen.px[33]));
        if (m) mouths++, changes += m != last, last = m;
        CHECK(!m || m == g_b.warehouse_mouth);
    }
    CHECK(mouths == changes && mouths > 160 && mouths < 340); /* 1/4 x 1/2 x 1/2 changes: 250 expected */
    /* ANIMS off: nothing drawn, the positions still advance */
    g_b.anims_on = 0;
    int spider = g_b.warehouse_spider;
    memset(g_b.screen.px, 0, 40);
    pb[0x3C] = 1;
    purchase_routine(&g_b, 0x4A2AB);
    for (int x = 0; x < 40; x++) CHECK(g_b.screen.px[x] == 0);
    CHECK(g_b.warehouse_spider == (spider + 2) % 0x40);
    world_free(&g_b.world);
}

static void test_skip_hour_and_tick(void) {
    fresh();
    world_set_tail(&g_world, TAIL_HOUR, 10), world_set_tail(&g_world, TAIL_MINUTE, 17);
    for (int i = 0; i < 37; i++) sim_tick(s);
    CHECK(s->tick == 37 && world_tail(&g_world, TAIL_MINUTE) == 17);
    int n = sim_skip_hour(s, NULL, NULL);
    CHECK(n == 4800 - 37); /* to the start of minute 17, one hour later */
    CHECK(s->tick == 0 && world_tail(&g_world, TAIL_MINUTE) == 17 && world_tail(&g_world, TAIL_HOUR) == 11);
    CHECK(!s->fast_forward);
    CHECK(sim_skip_hour(s, NULL, NULL) == 4800);
    /* the animation tick pb+0x3C: every second game tick once running (the start is in test_sim.c) */
    uint8_t *pb = g_world.block[BLK_PLAYER0], *pb1 = g_world.block[BLK_PLAYER1];
    pb[0x3C] = pb1[0x3C] = 0;
    s->anim_count = 2;
    sim_tick(s);
    CHECK(pb[0x3C] == 0);
    sim_tick(s);
    CHECK(pb[0x3C] == 1 && pb1[0x3C] == 1);
    for (int i = 0; i < 30; i++) sim_tick(s);
    CHECK(pb[0x3C] == 16);
}

/* ---- the survivors of the first mutation run (docs/engine.md, "The 23 survivors re-run") ---------------------------- */

/* step code 0 is an entry of the four tables: index 0 lies inside 0-440 (docs/ui-sheets.md, "The step tables") */
static void test_step_code_zero(void) {
    fresh();
    ward_patient(10);
    TREAT(69)[0x10] = 0; /* step 1: code 0 */
    tables_for_code(0, "Z", "Z", "Y", "Y");
    g_mem.step_index = 64;
    sheet(10);
    CHECK(g_mem.step_index == 0 && line_is(SHEET_ACTIVITY, "Z") && line_is(SHEET_MOOD, "Y"));
}

/* the instrument search walks the list's own slots only: an item in the word after the 4 slots is not found */
static void test_equipment_list_length(void) {
    fresh();
    treatment_room_patient(10);
    both(0x6A71, 0x875B, 48, computed("M "));
    both(0x6C2D, 0x8917, 48, image_str("", 0));
    text_set(700, "Haken", 5);
    texts_build();
    put32(ITEM(6), 700), ITEM(6)[13] = 14, ITEM(6)[7] = 3;
    uint8_t *r = rec(5, 1);
    put16(r + 0x60 + 2 * 4, 6);
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "M test device"));
    put16(r + 0x60 + 2 * 3, 6); /* the last slot of the list */
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "M 1610|Haken"));
}

/* the diagnosing lines print the item's whole name (no cut before " ("; only 0x2C264 cuts) */
static void test_diagnosing_name_uncut(void) {
    fresh();
    treatment_room_patient(10);
    static const char SCOPE[] = "Stethoskop (alt)";
    text_set(701, SCOPE, strlen(SCOPE));
    texts_build();
    put32(ITEM(7), 701), ITEM(7)[6] = 9, ITEM(7)[7] = 0;
    put16(rec(5, 1) + 0x0E, 5), put16(rec(5, 1) + 0x60, 7);
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2171|1607|2172|Stethoskop (alt)"));
    P(10)[4] = 11, ITEM(7)[6] = 12; /* dental: kind 12 */
    put16(rec(11, 1) + 0x0E, 5), put16(rec(11, 1) + 0x60, 7);
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2169|Stethoskop (alt)2170|"));
}

/* the pathology's harvest rows are 2 x 16 bytes: an index of 16 or a row of 2 reads nothing (2077) */
static void test_pathology_row_bounds(void) {
    fresh();
    uint8_t *p = P(10);
    memset(p, 0, 106);
    p[4] = 18, p[42] = 3, p[56] = 3;
    uint8_t *r = rec(18, 0);
    put16(r + 0x0E, 1), put16(r + 0x14, 0), put16(r + 0x12, 15);
    g_image[0xA749 + 15] = 2, g_image[0xA749 + 16] = 7;
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "4062|"));
    put16(r + 0x12, 16);
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2077|"));
    put16(r + 0x14, 2), put16(r + 0x12, 0);
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2077|"));
}

/* a '$'-string of the program image is copied up to 255 bytes */
static void test_image_string_cap(void) {
    fresh();
    ward_patient(10);
    static char LONG[300];
    memset(LONG, 'x', sizeof LONG);
    both(0x6A71, 0x875B, 16, image_str(LONG, sizeof LONG));
    both(0x6C2D, 0x8917, 16, image_str("M", 1));
    sheet(10);
    CHECK(g_page.drawn[SHEET_ACTIVITY] && g_page.line[SHEET_ACTIVITY].len == 255);
    CHECK(line_is(SHEET_MOOD, "M"));
}

/* a read past the end of the program image gives 0: a treatment record cut off before its drug dword has no drug
 * part (treatment 28 would append 3494) */
static void test_image_end(void) {
    fresh();
    ward_patient(10);
    P(10)[43] = 28;
    g_prog.image.size = 0x270C + 38 * 28 + 0x1A + 3; /* the dword at T +0x1A ends one byte past the image */
    sheet(10);
    CHECK(line_is(SHEET_TREATMENT, "0|"));
    g_prog.image.size = 0x270C + 38 * 28 + 0x1A + 4; /* in: the drug part, the name word at +0x1E still out */
    put32(TREAT(28) + 0x1A, 1);
    sheet(10);
    CHECK(line_is(SHEET_TREATMENT, "0|3494|"));
    /* a step string that runs into the end of the image without its '$' stops at the end */
    P(10)[43] = 69;
    memcpy(g_image + 0xE000, "abcZ", 4);
    both(0x6A71, 0x875B, 16, 0xE000);
    g_prog.image.size = 0xE003;
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "abc"));
    g_prog.image.size = sizeof g_image;
}

/* sheet_page1 writes inside its SheetPage only */
static struct {
    SheetPage page;
    uint8_t after[4096];
} g_fenced;
static void test_page_fence(void) {
    fresh();
    ward_patient(10);
    memset(g_fenced.after, 0xA5, sizeof g_fenced.after);
    sheet_page1(&g_ctx, 10, &g_fenced.page);
    int same = 1;
    for (size_t i = 0; i < sizeof g_fenced.after; i++) same &= g_fenced.after[i] == 0xA5;
    CHECK(same && g_fenced.page.drawn[SHEET_OPINION]);
}

/* the place picks: "up to 100 tries of random(k)" (neuro ward 101) over the room's k places, counted on the generator;
 * the ward's pick of beds in another state stops after 100,000 draws (engine cap, docs/engine.md) */
static void test_pick_draws(void) {
    fresh();
    employ(70, 1, 2, 0, 1); /* reception, four empty places */
    Sim ref = g_sim;
    CHECK(line_eq(1, "1504|1970|1505|1973|"));
    for (int i = 0; i < 100; i++) sim_random(&ref, 4);
    CHECK(ref.rng == g_sim.rng && ref.rng_s == g_sim.rng_s);
    employ(71, 2, 16, 0, 1); /* neuro ward: ten places at +0x14, the word after them is not a place */
    uint8_t *w = rec(16, 0);
    put16(w + 0x0C, 1), put16(w + 0x14 + 4 * 10, 97);
    ref = g_sim;
    CHECK(line_eq(2, "1935|1970|1936|1 1937|1974|"));
    for (int i = 0; i < 101; i++) sim_random(&ref, 10);
    CHECK(ref.rng == g_sim.rng && ref.rng_s == g_sim.rng_s);
    employ(72, 3, 6, 0, 1); /* Krankenzimmer: every bed holds a patient in another state */
    uint8_t *k = rec(6, 0);
    put16(k + 0x0C, 1);
    for (int i = 0; i < 10; i++) put16(k + 0x14 + 4 * i, 96);
    P(96)[4] = 5;
    ref = g_sim;
    CHECK(line_eq(3, "1865|1970|1866|1 1867|1974|"));
    for (int i = 0; i < 100000; i++) sim_random(&ref, 10);
    CHECK(ref.rng == g_sim.rng && ref.rng_s == g_sim.rng_s);
}

/* the drawn positions: page 1's seven lines at x 214 and the y of docs/ui-sheets.md (rooms_ui.c), the personnel
 * sheet's activity line at (124, 190) (personnel_ui.c); a guarded hang draws nothing. Font: every glyph one pixel. */
static Font g_dot;
static uint8_t g_dot_px[1] = {1}, g_dot_map[256];
static Image g_blank;
static void ui_fixture(void) {
    memset(&g_b, 0, sizeof g_b);
    g_b.prog = g_prog;
    g_b.world = g_world; /* the same blocks */
    sim_init(&g_b.sim, &g_b.world, &g_b.prog, 3);
    g_b.texts = g_texts, g_b.people = g_people;
    g_b.screen.w = SCREEN_W, g_b.screen.h = SCREEN_H;
    g_b.ui.screen = &g_b.screen;
    g_b.ui.prog = &g_b.prog;
    g_b.ui.hover_rect = (Rect){0, 0, -1, -1};
    for (int i = 1; i < IXY_COUNT; i++) g_b.ui.cache[i] = &g_blank;
    memset(&g_dot, 0, sizeof g_dot);
    g_dot.height = 1;
    for (int k = 0; k < 128; k++) g_dot.width[k] = 1, g_dot.glyph[k] = g_dot_px;
    memset(g_dot_map, 1, sizeof g_dot_map);
    g_b.prog.charmap = g_dot_map;
    for (int f = 0; f < FONT_COUNT; f++) g_b.fonts[f] = g_dot;
}
static int px(int x, int y) { return g_b.screen.px[y * SCREEN_W + x]; }
static int row_drawn(int y, int x0, int x1) {
    for (int x = x0; x < x1; x++)
        if (px(x, y)) return 1;
    return 0;
}

static int page_hover_enters, page_hover_leaves;
static void page_hover_call(Dulog *d, uint32_t routine) {
    if (routine == 0x29551) page_hover_enters++;
    if (routine == 0x29647) page_hover_leaves++;
    CHECK(rooms_routine(d->user, routine));
}

static int same_rect(Rect a, Rect b) {
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}

/* UI-114: sheet hover retains bounds established on an earlier screen by automatic hover. With
 * no earlier automatic hover the zero bounds remain, and a page turn does not restore feedback. */
static void page_turn_hover_case(int prior) {
    fresh();
    ward_patient(10);
    text_set(772, "Turn", 4);
    texts_build();
    ui_fixture();
    static Game game;
    memset(&game, 0, sizeof game);
    game.ixy[543] = (IxyEntry){4, 4, 490, 370};
    static uint8_t background_px[300 * 310], hover_px[16], normal_px[16], strip_px[250 * 15], other_px[1];
    memset(background_px, 7, sizeof background_px);
    memset(hover_px, 9, sizeof hover_px);
    memset(normal_px, 8, sizeof normal_px);
    memset(strip_px, 2, sizeof strip_px);
    static Image background = {535, 300, 310, 210, 90, 0, background_px, NULL};
    static Image hover = {543, 4, 4, 490, 370, 0, hover_px, NULL};
    static Image normal = {544, 4, 4, 490, 370, 0, normal_px, NULL};
    static Image strip = {1140, 250, 15, 210, 360, 0, strip_px, NULL};
    static Image other = {1200, 1, 1, 1, 1, 0, other_px, NULL};
    /* Unused fixture images must not introduce draws at (0,0) that invalidate the initial zero bounds. */
    static Image blank = {0, 0, 0, 1000, 1000, 0, NULL, NULL};
    Image automatic = {1201, 4, 4, prior == 2 ? 490 : 230, prior == 2 ? 370 : 100, 0, hover_px, NULL};
    Dulog *d = &g_b.ui;
    dulog_init(d, &game, &g_b.prog, &g_b.screen, page_hover_call, &g_b);
    for (int i = 1; i < IXY_COUNT; i++) d->cache[i] = &blank;
    d->cache[535] = &background, d->cache[543] = &hover, d->cache[544] = &normal;
    d->cache[1140] = &strip, d->cache[1200] = &other;
    d->cache[1201] = &automatic;
    Rect retained = {0, 0, 0, 0};
    CHECK(same_rect(d->hover_rect, retained));
    if (prior) {
        game.ixy[1201] = (IxyEntry){4, 4, (uint16_t)automatic.x, (uint16_t)automatic.y};
        Script *previous = &g_b.prog.screens[56];
        memset(previous, 0, sizeof *previous);
        previous->count = 1;
        previous->rec[0] = (Record){.op = OP_HOTSPOT, .id = 1, .hover = 1201, .hover_dx = 2, .hover_dy = 1};
        dulog_set_screen(d, 56);
        d->mouse_x = automatic.x + 1, d->mouse_y = automatic.y + 1;
        dulog_frame(d); /* establish bounds through the production automatic-hover path */
        retained = (Rect){automatic.x + 2, automatic.y + 1, automatic.x + 6, automatic.y + 5};
        CHECK(d->hovered == 1 && same_rect(d->hover_rect, retained));
        d->mouse_x = 100;
        dulog_frame(d);
        CHECK(d->hovered == HOVER_NONE && same_rect(d->hover_rect, retained));
    }
    dulog_set_screen(d, 62);
    Script *sc = &g_b.prog.screens[62];
    memset(sc, 0, sizeof *sc);
    sc->count = 1;
    sc->rec[0] = (Record){.op = OP_HOTSPOT, .id = 6, .hover = 543, .normal = 544,
        .click = 0x29373, .enter = 0x29551, .leave = 0x29647, .hover_dx = 2, .hover_dy = 1};
    uint8_t *pb = g_world.block[BLK_PLAYER0];
    put16(pb + RB_PATIENT, 10), put16(pb + RB_PAGE, 0);
    page_hover_enters = page_hover_leaves = 0;
    d->mouse_x = 493, d->mouse_y = 372;
    dulog_frame(d);
    CHECK(d->hovered == 6 && page_hover_enters == 1 && px(492, 371) == 9);
    CHECK(same_rect(d->hover_rect, retained)); /* screen setup and custom enter both retain it */
    int hint_pixel = px(214, 366);
    CHECK(hint_pixel != 2 && hint_pixel != 7);
    /* A separate draw must not provoke an enter callback every frame. */
    dulog_draw(d, 1200, 0, 0);
    dulog_frame(d);
    CHECK(d->hovered == 6 && page_hover_enters == 1 && px(214, 366) == hint_pixel);
    if (prior == 2) {
        /* Calling the custom enter while already hovered exposes its ordinary-draw invalidation.
         * It must neither draw quietly nor replace the prior automatic bounds. */
        page_hover_call(d, 0x29551);
        CHECK(d->hovered == HOVER_NONE && same_rect(d->hover_rect, retained));
        dulog_frame(d);
        CHECK(d->hovered == 6 && page_hover_enters == 3);
    }
    for (int turn = 1; turn <= 3; turn++) {
        int enters = page_hover_enters;
        d->click_pending = 1, d->click_x = d->mouse_x, d->click_y = d->mouse_y;
        dulog_frame(d);
        CHECK(u16(pb + RB_PAGE) == turn % 3);
        CHECK(d->hovered == (prior ? HOVER_NONE : 6) && page_hover_enters == enters && page_hover_leaves == 0);
        CHECK(same_rect(d->hover_rect, retained));
        CHECK(px(492, 371) == 7 && px(214, 366) == 7);
        dulog_frame(d);
        CHECK(d->hovered == 6 && page_hover_enters == enters + !!prior);
        CHECK(px(492, 371) == (prior ? 9 : 7) && px(214, 366) == (prior ? hint_pixel : 7));
        CHECK(same_rect(d->hover_rect, retained));
    }
    int enters = page_hover_enters;
    rooms_ui_sheet(&g_b); /* an explicit redraw has the same invalidation and deferred re-entry */
    CHECK(d->hovered == (prior ? HOVER_NONE : 6) && page_hover_enters == enters && px(492, 371) == 7);
    dulog_frame(d);
    CHECK(d->hovered == 6 && page_hover_enters == enters + !!prior && px(214, 366) == (prior ? hint_pixel : 7));
    d->mouse_x = 100;
    dulog_frame(d);
    CHECK(d->hovered == HOVER_NONE && page_hover_leaves == 1 && px(490, 370) == 8 && px(214, 366) == 2);
    CHECK(same_rect(d->hover_rect, retained));
    d->mouse_x = 493;
    dulog_frame(d);
    CHECK(d->hovered == 6 && page_hover_enters == enters + !!prior + 1 && px(214, 366) == hint_pixel);
    CHECK(same_rect(d->hover_rect, retained));
}

static void test_page_turn_hover(void) {
    for (int prior = 0; prior <= 2; prior++) page_turn_hover_case(prior);
}

/* SIM-82: use the real chooser to distinguish rating 100 (skill 25, two draws) from rating 0
 * (skill 0, five draws). The sheet's patient is separate from this room's patient. */
static size_t rating_choice_steps(void) {
    Sim *u = &g_b.sim;
    uint8_t *tr = sim_room_record(u, 0, 5, 0), *p = P(20);
    put16(tr + 10, 30), put16(tr + 12, 20), tr[1] = 0;
    memset(ST(30), 0, 32);
    p[42] = 7, p[57] = 0;
    ILL(7)[4] = 1, ILL(7)[8] = ILL(7)[15] = 0, ILL(7)[11] = 30;
    static const uint32_t DRAW[] = {1, 20, 0, 0, 0};
    SimScript script = {DRAW, 5, 0, 0, NULL};
    u->rng_script = &script;
    sim_ai_choose(u, 0, 5, 0);
    u->rng_script = NULL;
    CHECK(script.extra == 0);
    return script.used;
}

static void rating_prior(void) {
    uint8_t *r = sim_room_record(&g_b.sim, 3, 11, 0);
    r[1] = 100;
    sim_rating_record(&g_b.sim, r);
}

static void test_rating_sheet_frames(void) {
    static const struct { int screen, state, dept; uint32_t routine; } CASES[] = {
        {43, 16, 1, 0x4EE6E}, {57, 2, 0, 0x29269}, {59, 3, 0, 0x24079}, {61, 5, 0, 0x240F4},
        {62, 6, 0, 0x1F452}, {70, 11, 1, 0x25A51}, {73, 7, 1, 0x1F540}, {75, 15, 1, 0x25C63},
        {84, 9, 1, 0x1F4C9}, {126, 8, 1, 0x1F5B7}};
    for (size_t i = 0; i < sizeof CASES / sizeof *CASES; i++) {
        fresh();
        ui_fixture();
        uint8_t *q = g_world.block[BLK_PLAYER0];
        put16(q + RB_PATIENT, 10), put16(q + RB_PAGE, 0);
        P(10)[1] = 0, P(10)[4] = (uint8_t)CASES[i].state;
        P(11)[1] = 160, P(11)[4] = (uint8_t)CASES[i].state;
        g_b.ui.current = CASES[i].screen;
        rating_prior();
        CHECK(rating_choice_steps() == 2);
        CHECK(CASES[i].dept ? depts_routine(&g_b, CASES[i].routine) : rooms_routine(&g_b, CASES[i].routine));
        CHECK(g_b.sim.rating_ref.block == 1 && g_b.sim.rating_ref.offset == 1060);
        CHECK(rating_choice_steps() == 5);
        put16(q + RB_PATIENT, 11); /* repeated frames follow the selection, not a cached patient */
        CHECK(CASES[i].dept ? depts_routine(&g_b, CASES[i].routine) : rooms_routine(&g_b, CASES[i].routine));
        CHECK(g_b.sim.rating_ref.block == 1 && g_b.sim.rating_ref.offset == 1166);
        CHECK(rating_choice_steps() == 2);
    }
    fresh();
    ui_fixture();
    g_b.ui.current = 57;
    put16(g_world.block[BLK_PLAYER0] + RB_PATIENT, 0);
    rating_prior();
    CHECK(rooms_routine(&g_b, 0x29269));
    CHECK(g_b.sim.rating_ref.block == 1 && g_b.sim.rating_ref.offset == 0); /* index zero is still selected */
}

static void test_rating_sheet_render_branches(void) {
    static const struct { int state, illness, hour, treating, wish, writes; } CASES[] = {
        {6, 0, 4, 0, 0, 1}, {6, 0, 21, 0, 0, 1}, {16, 0, 10, 0, 0, 1},
        {6, 0, 3, 0, 0, 0}, {16, 0, 22, 0, 0, 0}, {6, 7, 10, 0, 0, 0},
        {5, 0, 10, 1, 0, 1}, {11, 7, 0, 1, 0, 1}, {15, 7, 23, 1, 0, 1},
        {5, 0, 10, 0, 0, 0}, {11, 0, 10, 1, 5, 0}, {15, 0, 10, 1, 5, 0},
        {18, 0, 10, 1, 0, 0}, {3, 0, 10, 1, 0, 0}};
    for (size_t i = 0; i < sizeof CASES / sizeof *CASES; i++) {
        fresh();
        ui_fixture();
        uint8_t *q = g_world.block[BLK_PLAYER0], *p = P(10);
        put16(q + RB_PATIENT, 10), put16(q + RB_PAGE, 2);
        p[1] = 0, p[4] = (uint8_t)CASES[i].state, p[79] = (uint8_t)CASES[i].treating;
        put16(p + 56, CASES[i].illness);
        p[57] = (uint8_t)CASES[i].wish; /* the high byte of the illness word, written after that word */
        world_set_tail(&g_world, TAIL_HOUR, CASES[i].hour);
        rating_prior();
        rooms_ui_sheet(&g_b); /* rendering alone, with no frame helper */
        if (sim_rating_byte(&g_b.sim) != (CASES[i].writes ? 0 : 100))
            fprintf(stderr, "  rating render case %zu: byte %u\n", i, sim_rating_byte(&g_b.sim));
        CHECK(sim_rating_byte(&g_b.sim) == (CASES[i].writes ? 0 : 100));
        CHECK(rating_choice_steps() == (CASES[i].writes ? 5u : 2u));
    }
    /* The shared flag helper (notify on and off) and the other pages never select a patient. */
    fresh();
    ui_fixture();
    put16(g_world.block[BLK_PLAYER0] + RB_PATIENT, 10);
    rating_prior();
    uint8_t flags[256] = {0};
    rooms_sheet_flags(&g_b.sim, flags, 1);
    rooms_sheet_flags(&g_b.sim, flags, 0);
    CHECK(sim_rating_byte(&g_b.sim) == 100);
    for (int page = 0; page < 2; page++) {
        put16(g_world.block[BLK_PLAYER0] + RB_PAGE, page);
        rooms_ui_sheet(&g_b);
        CHECK(rating_choice_steps() == 2);
    }
}

static void test_rating_nonwriting_ui_paths(void) {
    static const uint32_t ENTRY[] = {0x33D58, 0x32D77, 0x36D20, 0x36D4C, 0x319F5,
                                    0x31EC7, 0x3BCAF, 0x31D28, 0x36474, 0x3675B, 0x3BDE0};
    fresh();
    ui_fixture();
    rating_prior();
    CHECK(rooms_routine(&g_b, 0x3724E)); /* site map before any entry */
    CHECK(sim_rating_byte(&g_b.sim) == 100);
    for (size_t i = 0; i < sizeof ENTRY / sizeof *ENTRY; i++) {
        const SiteBuilding *building = rooms_site_building(ENTRY[i]);
        CHECK(building != NULL);
        if (!building) continue;
        room_counts(&g_b.sim, 0)[building->cat] = 1;
        rating_prior();
        CHECK(depts_routine(&g_b, ENTRY[i]) || rooms_routine(&g_b, ENTRY[i]));
        CHECK(g_b.ui.current == building->screen); /* prove the building opened, rather than returning early */
        CHECK(rating_choice_steps() == 2);
    }
    /* Pathology and overview frames do not run the common alive/dead helper's patient selection. */
    for (int page = 0; page <= 2; page++) {
        uint8_t *q = g_world.block[BLK_PLAYER0];
        memset(q, 0, g_world.size[BLK_PLAYER0]);
        put16(q + RB_PATIENT, 10), put16(q + RB_PAGE, page);
        P(10)[1] = 0, P(10)[4] = 18;
        g_b.ui.current = 90;
        rating_prior();
        CHECK(depts_routine(&g_b, 0x52416));
        CHECK(rating_choice_steps() == 2);
        q[0x45] = 1; /* pathology's explicit whole-sheet redraw request */
        CHECK(depts_routine(&g_b, 0x52416));
        CHECK(q[0x45] == 0 && rating_choice_steps() == 2);
        g_b.ui.current = 109;
        q[RB_MINUTE] = 1;
        CHECK(rooms_routine(&g_b, 0x23BE8));
        CHECK(rating_choice_steps() == 2);
    }
}
static void test_drawn_positions(void) {
    fresh();
    ward_patient(10);
    tables_for_code(16, "A", "A", "M", "M");
    ui_fixture();
    uint8_t *pb = g_world.block[BLK_PLAYER0];
    put16(pb + RB_PATIENT, 10), put16(pb + RB_PAGE, 1);
    rooms_ui_sheet(&g_b);
    static const int Y[SHEET_LINES] = {54, 93, 132, 197, 236, 275, 314};
    int rows = 0, at = 1;
    for (int y = 40; y < 330; y++) rows += row_drawn(y, 200, 400);
    for (int k = 0; k < SHEET_LINES; k++) at &= px(214, Y[k]) && !px(213, Y[k]) && row_drawn(Y[k], 214, 260);
    CHECK(at && rows == SHEET_LINES);
    /* the treatment-room sheet with field 43 = FE: only complaints and autopsy are drawn */
    treatment_room_patient(11);
    P(11)[43] = 0xFE, put16(rec(5, 1) + 0x10, 0);
    put16(pb + RB_PATIENT, 11);
    memset(g_b.screen.px, 0, sizeof g_b.screen.px);
    rooms_ui_sheet(&g_b);
    rows = 0;
    for (int y = 40; y < 330; y++) rows += row_drawn(y, 200, 400);
    CHECK(rows == 2 && px(214, 54) && px(214, 236));
    /* the personnel sheet: the activity line at (124, 190) */
    employ(40, 2, 0, 0, 0);
    memset(g_b.screen.px, 0, sizeof g_b.screen.px);
    personnel_ui_sheet_body(&g_b, 2);
    CHECK(px(124, 190) && !px(123, 190) && !px(124, 189) && row_drawn(190, 124, 200));
    /* the club's bar woman with the flag set and nobody listed: no activity line */
    employ(50, 2, 14, 0, 1);
    put16(rec(14, 0) + 4, 50), rec(14, 0)[0x19] = 1;
    memset(g_b.screen.px, 0, sizeof g_b.screen.px);
    personnel_ui_sheet_body(&g_b, 2);
    CHECK(!row_drawn(190, 0, SCREEN_W));
}

/* ---- what the comparison with the original's renders found (docs/engine.md, 2026-10-19, "the engine follows the
 * screen comparison") ----------------------------------------------------------------------------------------------- */

static uint8_t g_km_text[2][4096], g_km_table[2][64 * 4];
/* pools K (qualification) and M (peculiarity): entry i is "k<i>" / "m<i>", entry k_long / m_long 60 words long */
static void km_pools(int k_long, int m_long) {
    for (int k = 0; k < 2; k++) {
        size_t at = 0;
        for (int i = 0; i < 64; i++) {
            put32(g_km_table[k] + 4 * i, (int32_t)at);
            if (i == (k ? m_long : k_long))
                for (int w = 0; w < 60; w++) at += (size_t)sprintf((char *)g_km_text[k] + at, "%c ", k ? 'm' : 'k');
            else at += (size_t)sprintf((char *)g_km_text[k] + at, "%c%d", k ? 'm' : 'k', i);
            g_km_text[k][at++] = '$';
        }
        TextPool *pl = &g_people.pools[(k ? DAT_PECULIARITIES : DAT_QUALIFICATIONS) - DAT_PATIENT_NAMES];
        pl->text = g_km_text[k], pl->text_size = at, pl->table = g_km_table[k], pl->count = 64;
    }
}

/* The personnel sheet's lines after the name keep its line spacing 0 (a wrapped line one font height below, 13 px in
 * font 1), and the qualification / peculiarity strings are pool K / M at the staff record's u16 28 / 30, not at the
 * staff index (docs/ui-personnel.md, the letter; docs/formats.md, the staff record). Font height 1 here: the
 * qualification line (y 242) wraps once, so rows 242 and 243 and nothing below. */
static void test_personnel_sheet_lines(void) {
    fresh();
    km_pools(3, 9); /* K 3 long, M 9 (the staff index) long */
    ui_fixture();
    uint8_t *st = employ(9, 2, 0, 0, 1);
    put16(st + 28, 3), put16(st + 30, 4);
    personnel_ui_sheet_body(&g_b, 2);
    CHECK(row_drawn(242, 124, 508) && row_drawn(243, 124, 508));
    CHECK(!row_drawn(244, 0, SCREEN_W) && !row_drawn(245, 0, SCREEN_W) && !row_drawn(246, 0, SCREEN_W));
}

static int drawn_px(int y0, int y1, int x0, int x1) {
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) n += px(x, y) != 0;
    return n;
}

/* The remark's clause with a symptom text that starts with a 0 byte: no 2251 "klagt über " and no severity word,
 * only the text from its second byte (the original's render of screen 109: "... und schlägt wild um sich"). Font:
 * one pixel per glyph, so the two remarks differ by the 10 glyphs of "2251|" and "2377|". */
static void test_remark_bare_symptom(void) {
    int n[2];
    for (int bare = 0; bare < 2; bare++) {
        fresh();
        text_set(3292, bare ? "\0Ohren" : "Ohren", bare ? 6 : 5);
        texts_build();
        ward_patient(10);
        ui_fixture();
        uint8_t *pb = g_world.block[BLK_PLAYER0];
        put16(pb + RB_PATIENT, 10), put16(pb + RB_PAGE, 0);
        rooms_ui_sheet(&g_b);
        n[bare] = drawn_px(262, 276, 214, 468);
    }
    CHECK(n[0] > 10 && n[0] - n[1] == 10);
}

/* The health chart (docs/ui-rooms.md): dot i, then the line from it to dot i + 1, so a dot covers the end of the
 * line arriving at it; the line's minor axis is start + k d / n rounded half up (the original's pixels, 22 segments). */
static uint8_t g_dot16_px[16];
static Image g_dot16 = {16, 4, 4, 0, 0, 0, g_dot16_px, NULL};
static void test_health_chart(void) {
    fresh();
    uint8_t *p = ward_patient(10);
    for (int i = 0; i < 11; i++) p[77 - i] = i == 0 || i == 1 ? 10 : 13;
    p[44] = 13;
    ui_fixture();
    memset(g_dot16_px, 7, sizeof g_dot16_px);
    g_b.ui.cache[16] = &g_dot16;
    uint8_t *pb = g_world.block[BLK_PLAYER0];
    put16(pb + RB_PATIENT, 10), put16(pb + RB_PAGE, 0);
    rooms_ui_sheet(&g_b);
    /* segment 0, (370, 129) - (378, 129): its pixel at 377 lies under dot 1, which starts the next line at 378 */
    CHECK(px(376, 129) == 0x95 && px(377, 129) == 7 && px(378, 129) == 0x95 && px(371, 129) == 0x95);
    /* segment 1, (378, 129) - (386, 126): k 3, 4 at y 128 (-1.125, -1.5 round up to -1), k 5, 6 at 127 */
    CHECK(px(381, 128) == 0x95 && px(382, 128) == 0x95 && px(383, 127) == 0x95 && px(384, 127) == 0x95);
    CHECK(px(382, 127) != 0x95 && px(383, 128) != 0x95 && px(381, 127) != 0x95 && px(384, 128) != 0x95);
}

/* ---- SCR-10: pages 1 and 2 against the original (docs/ui-sheets.md, section 6, "What the engine must change") ----- */

/* page 1: (2) the diagnosis line's "no illness" test is u8 field 56, (3) no autopsy line in state 9, (5) the neuro
 * ward's own mood before the step part */
static void test_scr10_page1(void) {
    fresh();
    uint8_t *p = ward_patient(10);
    p[42] = 0, p[56] = 5; /* (2) field 56 set, field 42 = 0: the illness name 3210 + 0 */
    sheet(10);
    CHECK(line_is(SHEET_DIAGNOSIS, "3210|"));
    p[56] = 0, p[42] = 7; /* field 56 = 0, field 42 = 7: 232 */
    sheet(10);
    CHECK(line_is(SHEET_DIAGNOSIS, "232|"));
    /* (3) intensive care: no autopsy line; the ward (control) has one */
    p = ward_patient(10);
    sheet(10);
    CHECK(line_is(SHEET_AUTOPSY, "2142|"));
    p[4] = 9;
    sheet(10);
    CHECK(!g_page.drawn[SHEET_AUTOPSY] && g_page.drawn[SHEET_DIAGNOSIS]);
    p[104] = 1; /* dead: still none (1751 is not drawn either) */
    sheet(10);
    CHECK(!g_page.drawn[SHEET_AUTOPSY]);
    /* (5) the neuro ward, field 93 = 0 (activity 2073), K +6 = 0, a '$' mood entry at the kept index 0: empty */
    fresh();
    p = ward_patient(10);
    p[4] = 16, p[93] = 0;
    put16(rec(16, 0) + 8, 5);
    both(0x6C2D, 0x8917, 0, image_str("", 0));
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2073|"));
    CHECK(line_is(SHEET_MOOD, ""));
    p[4] = 6; /* the same in the Krankenzimmer: 1777 */
    sheet(10);
    CHECK(line_is(SHEET_MOOD, "1777|"));
    p[4] = 16, p[102] = 1; /* narcosis (2063) on the neuro ward: its own mood as well */
    sheet(10);
    CHECK(line_is(SHEET_ACTIVITY, "2063|") && line_is(SHEET_MOOD, ""));
}

static int row_count(int y, int x0, int x1) {
    int n = 0;
    for (int x = x0; x < x1; x++) n += px(x, y) != 0;
    return n;
}

/* page 2: (1) the room field, (4) the history's variants; (6) a dead patient's buttons; (7) the page-1 frames */
static void test_scr10_ui(void) {
    fresh();
    uint8_t *p = ward_patient(10);
    ui_fixture();
    uint8_t *pb = g_world.block[BLK_PLAYER0];
    put16(pb + RB_PATIENT, 10), put16(pb + RB_PAGE, 1);
    rooms_ui_sheet(&g_b); /* (1) page 1, state 6 in bed 1: the number */
    CHECK(row_count(28, 420, 500) > 0);
    put16(pb + RB_PAGE, 2);
    memset(g_b.screen.px, 0, sizeof g_b.screen.px);
    rooms_ui_sheet(&g_b); /* page 2: nothing at the field */
    CHECK(row_count(28, 420, 500) == 0);
    p[4] = 5; /* a treatment-room patient: "2176|" (5 dots) */
    memset(g_b.screen.px, 0, sizeof g_b.screen.px);
    rooms_ui_sheet(&g_b);
    CHECK(row_count(28, 420, 500) == 5);
    p[4] = 6;
    /* (4) the history at y 54: id 9 (record byte +0x10 = 1, field 84 = 2) "3136|" + 3199 "v" without ", ";
     * then id 13 (field 85 = 1): "1063|" + "3119|"; id 4 (byte 3, field 86 = 2) "1063|3131|" + 3205 "www";
     * id 6 (byte 2): "1063|3133|" only; id 7 (byte 1, field 88 = 0): "1063|3134|" + 3197 "zz" (0 is not tested) */
    text_set(3199, "v", 1), text_set(3205, "www", 3), text_set(3197, "zz", 2);
    texts_build();
    g_b.texts = g_texts;
    ILL(9)[0x10] = 1, ILL(4)[0x10] = 3, ILL(6)[0x10] = 2, ILL(7)[0x10] = 1;
    p[11] = 9, p[12] = 13, p[13] = 4, p[14] = 6, p[15] = 7, p[16] = 0;
    p[84] = 2, p[85] = 1, p[86] = 2, p[87] = 3, p[88] = 0;
    memset(g_b.screen.px, 0, sizeof g_b.screen.px);
    rooms_ui_sheet(&g_b);
    CHECK(row_count(54, 200, 470) == 5 + 1 + (5 + 5) + (5 + 5 + 3) + (5 + 5) + (5 + 5 + 2));
    p[12] = 0; /* the first entry alone: no separator in front */
    memset(g_b.screen.px, 0, sizeof g_b.screen.px);
    rooms_ui_sheet(&g_b);
    CHECK(row_count(54, 200, 470) == 6);
    /* (6) a dead ward patient: the bar 898 again after 918, then 908 */
    static uint8_t bpx[3] = {7, 9, 5};
    static Image b918 = {918, 1, 1, 230, 370, 0, &bpx[0], NULL}, b898 = {898, 1, 1, 230, 370, 0, &bpx[1], NULL},
                 b908 = {908, 1, 1, 340, 370, 0, &bpx[2], NULL};
    g_b.ui.cache[918] = &b918, g_b.ui.cache[898] = &b898, g_b.ui.cache[908] = &b908;
    put16(pb + RB_PAGE, 0);
    rooms_ui_sheet(&g_b);
    CHECK(px(230, 370) == 7 && px(340, 370) != 5); /* alive: 918 shows, no corpse button */
    p[104] = 1;
    rooms_ui_sheet(&g_b);
    CHECK(px(230, 370) == 9 && px(340, 370) == 5);
    p[104] = 0;
    /* (7) screen 57, page 1, health 10 after 30: one redraw, the flag cleared after it */
    p[44] = 10, p[67] = 30;
    put16(pb + RB_PAGE, 1);
    g_b.ui.current = 57;
    rooms_ui_sheet(&g_b);
    CHECK(pb[RB_LOW_HEALTH] == 0xFF);
    memset(g_b.screen.px, 0, sizeof g_b.screen.px);
    rooms_ui_sheet_frame(&g_b);
    CHECK(pb[RB_LOW_HEALTH] == 0 && row_count(54, 200, 470) > 0); /* redrawn once */
    memset(g_b.screen.px, 0, sizeof g_b.screen.px);
    rooms_ui_sheet_frame(&g_b);
    CHECK(row_count(54, 200, 470) == 0); /* not again */
    /* screens 61 and 62 do not run the page-1 part: no redraw, no mood box, the flags stay */
    static const int SCR[2] = {61, 62};
    for (int i = 0; i < 2; i++) {
        g_b.ui.current = SCR[i];
        pb[RB_LOW_HEALTH] = pb[RB_MOOD] = 0xFF;
        memset(g_b.screen.px, 0, sizeof g_b.screen.px);
        rooms_ui_sheet_frame(&g_b);
        CHECK(row_count(54, 200, 470) == 0 && row_count(262, 200, 470) == 0 && pb[RB_MOOD] == 0xFF);
    }
    g_b.ui.current = 0;
}

/* SCR-13 (docs/ui-sheets.md 6.8): the state's bar comes after the upper lines and before the lower ones; a dead
 * patient's redraw after page 1's and 2's lines, before page 0's remarks; a state outside the table draws no bar.
 * The bar is a 1 x 1 picture of colour 9 put under the first glyph of a line. */
static void test_scr13_bar_order(void) {
    fresh();
    uint8_t *p = ward_patient(10);
    ui_fixture();
    uint8_t *pb = g_world.block[BLK_PLAYER0];
    put16(pb + RB_PATIENT, 10);
    static uint8_t bar_px[1] = {9};
    static Image bar = {898, 1, 1, 214, 314, 0, &bar_px[0], NULL};
    g_b.ui.cache[898] = &bar;
    static const struct { int page, y, dead, bar_on_top; } C[] = {
        {1, 314, 0, 0}, {1, 54, 0, 1},  /* page 1: the opinion line over it, the line at y 54 under it */
        {1, 314, 1, 1},                                 /* dead: the bar again after the lines */
        {0, 262, 0, 0}, {0, 262, 1, 0},                 /* page 0: the remarks over it, also when dead */
        {2, 275, 0, 0}, {2, 54, 0, 1}, {2, 275, 1, 1},  /* page 2: the reaction line over it, the history under it */
    };
    for (size_t i = 0; i < sizeof C / sizeof C[0]; i++) {
        bar.y = C[i].y, p[104] = (uint8_t)C[i].dead;
        put16(pb + RB_PAGE, C[i].page);
        memset(g_b.screen.px, 0, sizeof g_b.screen.px);
        rooms_ui_sheet(&g_b);
        int got = px(214, C[i].y);
        if ((got == 9) != C[i].bar_on_top) fprintf(stderr, "  case %zu: px %d\n", i, got);
        CHECK(got != 0 && (got == 9) == C[i].bar_on_top);
    }
    /* a state outside the table (4): no bar at all; state 3: the bar alone */
    p[104] = 0, bar.x = 230, bar.y = 370;
    put16(pb + RB_PAGE, 1);
    static const int STATE[2] = {4, 3}, SHOWN[2] = {0, 9};
    for (int i = 0; i < 2; i++) {
        p[4] = (uint8_t)STATE[i];
        memset(g_b.screen.px, 0, sizeof g_b.screen.px);
        rooms_ui_sheet(&g_b);
        CHECK(px(230, 370) == SHOWN[i]);
    }
    p[4] = 6, g_b.ui.cache[898] = &g_blank;
}

/* The campaign screen 119 (docs/ui-support.md): the heading's line spacing 2 stays for the type text, the slogan and
 * the duration lines (the original's render: 14 px pitch in font 5). Font height 1: the long type text wraps to y 113. */
static void test_campaign_pitch(void) {
    fresh();
    char t[512];
    size_t n = 0;
    for (int w = 0; w < 70; w++) n += (size_t)sprintf(t + n, "t ");
    text_set(1466, t, n);
    text_set(2404, t, n); /* the slogan of slogan 0 (y 180) */
    text_set(1468, t, n); /* the duration line (y 236) */
    texts_build();
    ui_fixture();
    support_routine(&g_b, 0x4D3EE);
    static const int Y[3] = {110, 180, 236};
    for (int k = 0; k < 3; k++)
        CHECK(row_drawn(Y[k], 136, 506) && !row_drawn(Y[k] + 1, 136, 506) && !row_drawn(Y[k] + 2, 136, 506) &&
              row_drawn(Y[k] + 3, 136, 506));
    CHECK(!row_drawn(114, 136, 506));
}

/* The campaign status 121 (docs/ui-support.md, "121 - campaign status", corrected 2026-10-19): a running campaign's
 * counts start with 2422, not 2421 + 2422 (2421 "\r\r" is only the ended state's body), so after the type text's
 * closing "\r\r" the counts follow one empty line down. Font height 1, line spacing 2: a line every 3 px from y 110;
 * the original's render has the counts at y 186 after a three-line type text. */
static void test_campaign_status_counts(void) {
    fresh();
    text_set(1476, "a", 1);
    text_set(2404, "s", 1);
    text_set(2438, "\r\r", 2);
    text_set(2421, "\r\r", 2);
    text_set(2422, "cccccccccccccccccccc", 20); /* 20 glyphs: the counts line is more than "d 9p" */
    text_set(2423, "d ", 2);
    text_set(2424, "p", 1);
    text_set(2427, "\r\re", 3);
    text_set(2428, "z ", 2);
    text_set(2431, "\r\ri", 3);
    text_set(2434, "\r\rv", 3);
    text_set(2437, "w ", 2);
    texts_build();
    ui_fixture();
    uint8_t *r = world_hospital(&g_world, 0);
    r[AD_TYPE] = 1, r[AD_SLOGAN] = 0;
    put16(r + AD_LEFT, 5), put16(r + AD_GOOD, 2), put16(r + AD_NEUTRAL, 3), put16(r + AD_BAD, 4);
    support_routine(&g_b, 0x4D3EE);
    CHECK(g_b.ui.current == 121);
    /* type text 110, counts 116, positive 122, ignored 128, vomited 134, nothing below */
    static const int Y[5] = {110, 116, 122, 128, 134};
    for (int k = 0; k < 5; k++) CHECK(row_drawn(Y[k], 136, 506) && !row_drawn(Y[k] + 3, 136, 506));
    CHECK(!row_drawn(140, 136, 506) && !row_drawn(146, 136, 506));
    CHECK(drawn_px(116, 117, 136, 506) >= 20); /* 2422 starts the counts */
}

/* Firmenbesitz (screen 153, docs/desktop.md, corrected 2026-10-19): the third column (x 332) is the owned slots (flag
 * 0), not the rented ones again. Category 5 (row y 159): 1 rented, 10 owned; one-pixel glyphs, so "10 " draws 2. */
static void test_holdings_columns(void) {
    fresh();
    memset(g_world.block[BLK_ROOM_SLOTS], 0xFF, g_world.size[BLK_ROOM_SLOTS]);
    g_image[CATALOG + 7 * CATALOG_SIZE + CATALOG_CATEGORY] = 5;
    uint8_t *slots = g_world.block[BLK_ROOM_SLOTS];
    for (int i = 0; i < 11; i++) slots[2 * i] = 7, slots[2 * i + 1] = i == 0;
    ui_fixture();
    desktop_routine(&g_b, 0x3D826);
    /* each number is centred in 85 px from its x: total about 276, rented about 318, owned about 374 */
    CHECK(drawn_px(159, 160, 256, 297) == 2 && drawn_px(159, 160, 298, 345) == 1 && drawn_px(159, 160, 350, 400) == 2);
    CHECK(drawn_px(159, 160, 0, SCREEN_W) - drawn_px(159, 160, 0, 256) == 5);
}

/* The "Hilfe!" strip lights (or goes dark) in the main-loop iteration whose clock tick ran the scheduled advice (0x27495 on tick 5):
 * the frame's step 2 (0xBDE6) comes after the clock (docs/screens.md, "Per-frame order"); the engine drew it one
 * iteration later until 2026-10-19 (docs/engine.md, the comparison's desktop cases). */
static void test_help_strip_same_frame(void) {
    fresh();
    ui_fixture();
    g_b.sim.tick = 4;
    g_b.credit = 100;
    g_b.clock_stopped = 0;
    g_b.sim.help_strip = 1, g_b.ui.help_enabled = 1; /* lit; the synthetic hospital has no advice to give */
    biing_frame(&g_b);
    CHECK(g_b.sim.tick == 5 && !g_b.sim.help_strip); /* the walk ran on this tick and found nothing */
    CHECK(!g_b.ui.help_enabled); /* and the strip goes dark in the same iteration */
}

/* ---- the CD builds' rule changes on the screens (docs/editions.md section 7) -------------------------------------- */

static int msgb_is(const char *want) { /* the Biing's message buffer, as the last composed text left it */
    int ok = g_b.msg.len == strlen(want) && memcmp(g_b.msg.buf, want, g_b.msg.len) == 0;
    if (!ok) fprintf(stderr, "    msg \"%.*s\", want \"%s\"\n", (int)g_b.msg.len, g_b.msg.buf, want);
    return ok;
}
static int msgb_starts(const char *want) {
    int ok = g_b.msg.len >= strlen(want) && memcmp(g_b.msg.buf, want, strlen(want)) == 0;
    if (!ok) fprintf(stderr, "    msg \"%.*s\", want it to start \"%s\"\n", (int)g_b.msg.len, g_b.msg.buf, want);
    return ok;
}

/* rule A (the device-name helper 0x27CE0) and rule B (the diagnosing line, 0x45C48) on the patient sheet */
static void test_english_sheet_articles(void) {
    for (int en = 0; en < 2; en++) {
        fresh();
        g_prog.edition = en ? EDITION_ENGLISH_CD : EDITION_DISK;
        treatment_room_patient(10);
        both(0x6A71, 0x875B, 48, computed("T "));
        both(0x6C2D, 0x8917, 48, image_str("", 0));
        text_set(700, "Fixture tool (AA)", 17), text_set(701, "electric shock", 14), text_set(702, "Injections", 10);
        text_set(703, "yoke", 4), text_set(704, "Bait", 4);
        texts_build();
        put32(ITEM(6), 700), ITEM(6)[13] = 14, ITEM(6)[7] = 0;
        uint8_t *r = rec(5, 1);
        g_world.block[BLK_ROOM_COUNTS][5] = 2;
        put16(r + 0x60, 6), r[0x70] = 10;
        sheet(10);
        CHECK(line_is(SHEET_ACTIVITY, en ? "T a386|Fixture tool" : "T 1607|Fixture tool"));
        put32(ITEM(6), 701), ITEM(6)[7] = 1;
        sheet(10);
        CHECK(line_is(SHEET_ACTIVITY, en ? "T an electric shock" : "T 1608|electric shock"));
        put32(ITEM(6), 702), ITEM(6)[7] = 2; /* a capital vowel */
        sheet(10);
        CHECK(line_is(SHEET_ACTIVITY, en ? "T an Injections" : "T 1609|Injections"));
        put32(ITEM(6), 703); /* y counts as a vowel */
        sheet(10);
        CHECK(line_is(SHEET_ACTIVITY, en ? "T an yoke" : "T 1609|yoke"));
        put32(ITEM(6), 704), ITEM(6)[7] = 3; /* kind 3: no article in English, the empty 1610 in German */
        sheet(10);
        CHECK(line_is(SHEET_ACTIVITY, en ? "T Bait" : "T 1610|Bait"));
        /* the diagnosing line: English without the article between 2171 and 2172 */
        put16(r + 0x0E, 5), put16(r + 0x60, 8);
        put32(ITEM(8), 720), ITEM(8)[6] = 9, ITEM(8)[7] = 2;
        sheet(10);
        CHECK(line_is(SHEET_ACTIVITY, en ? "2171|2172|720|" : "2171|1609|2172|720|"));
    }
    g_prog.edition = EDITION_DISK;
}

/* rule A in the purchasing office (0x2F610: the own offer bought back), rule F (estate agent), rule H (advertising
 * report), rule I and J (personnel sheet), OTH-3 (the nurse job ad), OTH-EN-4 (the race screen's names) */
static void test_english_screens(void) {
    for (int en = 0; en < 2; en++) {
        fresh();
        g_prog.edition = en ? EDITION_ENGLISH_CD : EDITION_DISK;
        text_set(730, "Eisen", 5), text_set(731, "Kette", 5);
        texts_build();
        ui_fixture();
        /* the own equipment offer 0 bought back: 1619 + article + name + 1620 */
        uint8_t *o = g_world.block[BLK_EQUIPMENT_MARKET];
        o[0] = 0, o[1] = 0, put16(o + 2, 9), o[4] = 50, o[5] = 0xFF;
        put32(ITEM(9), 730), ITEM(9)[7] = 1;
        purchase_routine(&g_b, 0x2F606);
        CHECK(msgb_is(en ? "1619|an Eisen1620|" : "1619|1612|Eisen1620|"));
        o[0] = 0, o[1] = 0, put16(o + 2, 10), o[4] = 50, o[5] = 0xFF;
        put32(ITEM(10), 731), ITEM(10)[7] = 0;
        purchase_routine(&g_b, 0x2F606);
        CHECK(msgb_is(en ? "1619|a386|Kette1620|" : "1619|1611|Kette1620|"));
        /* the estate agent: "You have " + "rented " / "bought " (German has the verb at the end) */
        world_set_cash(&g_world, 0, 10000000);
        put16(g_world.block[BLK_PLAYER0], 1);
        estate_routine(&g_b, 0x3807A);
        CHECK(msgb_starts(en ? "306|rented 312|311|" : "306|312|311|"));
        estate_routine(&g_b, 0x38135);
        CHECK(msgb_starts(en ? "306|bought " : "306|3"));
        /* the advertising report: "Vomited: " + number + 2424, without 2437 */
        uint8_t *r = world_hospital(&g_world, 0);
        r[AD_TYPE] = 1, r[AD_SLOGAN] = 0;
        put16(r + AD_LEFT, 5), put16(r + AD_GOOD, 2), put16(r + AD_NEUTRAL, 3), put16(r + AD_BAD, 4);
        support_routine(&g_b, 0x4D3EE);
        const char *tail = en ? "2434|4 2424|" : "2434|2437|4 2424|";
        CHECK(g_b.msg.len > strlen(tail) && !memcmp(g_b.msg.buf + g_b.msg.len - strlen(tail), tail, strlen(tail)));
        /* the nurse job ad, bust 110, wage 10, training 5 (the English original's run: "Wanted one voluptuous, randy
         * nurse with at least a 110 cm chest! Education should already be, at least 5 Education points. Payed will be
         * the huge sum of 10 Dongs per day.") */
        uint8_t *pb = g_world.block[BLK_PLAYER0];
        put16(pb + PB_NURSE_BUST, 110), put16(pb + PB_NURSE_WAGE, 10), put16(pb + PB_NURSE_TRAINING, 5);
        personnel_routine(&g_b, 0x3A555);
        CHECK(msgb_is(en ? "2342|2343|2336|2344|110 2345|2332|5 2335|1315|10 2346|2347|"
                         : "2342|2343|2338|2344|5 2345|110 1315|10 2346|2347|"));
        static const int BUST[5] = {79, 80, 90, 100, 109}, WORD[5] = {2339, 2340, 2338, 2337, 2337};
        static const int TRAIN[5] = {1, 2, 4, 8, 12}, PHRASE[5] = {2330, 2331, 2331, 2333, 2334};
        for (int k = 0; k < 5 && en; k++) {
            char want[160];
            put16(pb + PB_NURSE_BUST, BUST[k]), put16(pb + PB_NURSE_TRAINING, TRAIN[k]);
            personnel_routine(&g_b, 0x3A555);
            snprintf(want, sizeof want, "2342|2343|%d|2344|%d 2345|%d|%d 2335|1315|10 2346|2347|", WORD[k], BUST[k],
                     PHRASE[k], TRAIN[k]);
            CHECK(msgb_is(want));
        }
        /* the personnel sheet of a nurse: IQ "--" (text 2176, 5 one-pixel glyphs) instead of the number "70 "; the
         * mood line text 330 instead of the training strings 0xE52D (none in this image) */
        km_pools(3, 9);
        uint8_t *st = employ(9, 2, 0, 0, 1);
        st[1] = 1;
        memset(g_b.screen.px, 0, sizeof g_b.screen.px);
        personnel_ui_sheet_body(&g_b, 2);
        CHECK(drawn_px(91, 92, 440, 520) == (en ? 5 : 2));
        CHECK(drawn_px(143, 144, 254, 520) == (en ? 8 : 4)); /* 330| + 335|, or 335| alone */
        /* the race screen: hospital 3's name at x 443 instead of 456 */
        uint8_t *race = garage_race(s);
        put16(race + RACE_WORD, 1), put16(race + RACE_MINUTE, 0), race[RACE_RESULT] = 0;
        garage_entry(s, 0)[0] = 3, garage_entry(s, 1)[0] = 0xFF;
        memcpy(g_image + 0x3BCF + 30 * 3, "ab$", 3);
        memset(g_b.screen.px, 0, sizeof g_b.screen.px);
        garage_routine(&g_b, 0x5AB2D);
        CHECK(px(en ? 443 : 456, 52) && !px(en ? 456 : 443, 52) && !px(en ? 442 : 455, 52));
    }
    g_prog.edition = EDITION_DISK;
}

/* the English winner line uses the clamped name helper (rule E): the winner's index 0xFF (a negative byte) names
 * hospital 1, 20 names hospital 16; the German builds print no name for an index of 16 or more */
static void test_english_race_winner(void) {
    for (int en = 0; en < 2; en++) {
        fresh();
        g_prog.edition = en ? EDITION_ENGLISH_CD : EDITION_DISK;
        ui_fixture();
        memcpy(g_image + 0x3BCF + 30 * 1, "one$", 4);
        memcpy(g_image + 0x3BCF + 30 * 16, "sixteen$", 8);
        uint8_t *race = garage_race(s);
        put16(race + RACE_WORD, 1), put16(race + RACE_MINUTE, 40), race[RACE_RESULT] = 1;
        garage_entry(s, 0)[0] = 0xFF;
        garage_routine(&g_b, 0x5AB2D);
        CHECK(en ? msgb_is("P11021|2657|one1022|") : msgb_is("P11021|1022|"));
        garage_entry(s, 0)[0] = 20;
        garage_routine(&g_b, 0x5AB2D);
        CHECK(en ? msgb_is("P11021|2657|sixteen1022|") : msgb_is("P11021|1022|"));
    }
    g_prog.edition = EDITION_DISK;
}

/* German CD (OTH-DE-12): the `*` key writes the frame buffer's 400 picture rows and the DAC palette as ASA01.LBM,
 * ASA02.LBM, ... into the game folder; the other builds ignore the key */
static void test_german_screenshot(void) {
    for (int ed = 0; ed < 3; ed++) {
        fresh();
        g_prog.edition = (Edition)ed;
        ui_fixture();
        test_mkdir("test_sheet_shot");
        remove("test_sheet_shot/ASA01.LBM"), remove("test_sheet_shot/ASA02.LBM");
        snprintf(g_b.assets.root, sizeof g_b.assets.root, "test_sheet_shot");
        g_b.assets.palette[3] = 255, g_b.assets.palette[4] = 7;
        g_b.screen.px[0] = 1, g_b.screen.px[640 * 399 + 639] = 9, g_b.screen.px[640 * 400] = 5;
        g_b.key = '*';
        biing_frame(&g_b);
        FILE *f = fopen("test_sheet_shot/ASA01.LBM", "rb");
        static uint8_t d[300000];
        size_t n = f ? fread(d, 1, sizeof d, f) : 0;
        if (f) fclose(f);
        if (ed != EDITION_GERMAN_CD) {
            CHECK(!f && g_b.key == '*'); /* nothing written, the key left as it was */
            continue;
        }
        CHECK(n == 12 + 28 + 776 + 8 + 256000 && g_b.key == -1);
        CHECK(!memcmp(d, "FORM", 4) && !memcmp(d + 8, "PBM BMHD", 8) && !memcmp(d + 40, "CMAP", 4));
        CHECK(d[4] == 0 && d[5] == 0x03 && d[6] == 0xEB && d[7] == 0x30); /* 4 + 28 + 776 + 8 + 256,000 = 256,816 */
        CHECK(d[20] == 2 && d[21] == 128 && d[22] == 1 && d[23] == 144 && d[28] == 8 && d[30] == 0); /* 640 x 400 */
        CHECK(d[48 + 3] == 252 && d[48 + 4] == 4 && d[48 + 0] == 0); /* 6-bit values x 4 */
        CHECK(!memcmp(d + 816, "BODY", 4) && d[824] == 1 && d[824 + 255999] == 9);
        g_b.key = '*';
        biing_frame(&g_b);
        f = fopen("test_sheet_shot/ASA02.LBM", "rb");
        CHECK(f != NULL);
        if (f) fclose(f);
    }
    g_prog.edition = EDITION_DISK;
}

/* OTH-4 through the save dialog: the save confirmation (0x20260) writes player block +6 as the build's address, the
 * load confirmation (0x21547) reads it back as the disk offset; the running game keeps the disk offset. A build that
 * moved everything from disk 0x100 on by +0x50. */
static uint32_t cont_of(const World *w) {
    const uint8_t *c = w->block[BLK_PLAYER0] + 6;
    return c[0] | c[1] << 8 | (uint32_t)c[2] << 16 | (uint32_t)c[3] << 24;
}
static void test_save_load_continue(void) {
    static const ProgramSegment SEG[] = {{0, 0x100, 0}, {0x100, 0x70000, 0x150}};
    static const ProgramBuild B = {"GOG English CD", 0, 0, 0, "T", "D", 0, SEG, 2, NULL, 0, NULL, 0, 0};
    fresh();
    ui_fixture();
    g_b.prog.build = &B;
    g_b.ui.user = &g_b; /* the routine dispatch finds the game through the screen layer */
    test_mkdir("test_sheet_saves");
    test_mkdir("test_sheet_saves/GAMES");
    remove("test_sheet_saves/GAMES/BGAME9");
    snprintf(g_b.assets.root, sizeof g_b.assets.root, "test_sheet_saves");
    g_b.slot = 8;
    put32(g_b.world.block[BLK_PLAYER0] + 6, 0x2C79B);
    biing_routine(&g_b, 0x20260);
    World r;
    CHECK(world_load(&r, "test_sheet_saves/GAMES/BGAME9") == 0 && cont_of(&r) == 0x2C79B + 0x50);
    world_free(&r);
    CHECK(cont_of(&g_b.world) == 0x2C79B);
    put32(g_b.world.block[BLK_PLAYER0] + 6, 0);
    biing_routine(&g_b, 0x21547);
    CHECK(cont_of(&g_b.world) == 0x2C79B && !g_b.load_failed);
    g_world = g_b.world; /* the loaded blocks: the next fresh() frees them */
    g_b.prog.build = NULL;
    remove("test_sheet_saves/GAMES/BGAME9");
}

/* ---- SCR-11 and SCR-12: the sheets' page-1 part and the reception frame (docs/ui-rooms.md) ------------------------ */

/* test images: 1 x 1 at (k, 0) in colour 1, k = the order of registration */
static Image g_fimg[40];
static uint8_t g_fpx[40] = {0};
static int g_fid[40], g_fn;
static void fimg(int id, int x, int y, uint8_t colour) {
    int k = g_fn++;
    g_fid[k] = id, g_fpx[k] = colour;
    g_fimg[k] = (Image){id, 1, 1, x, y, 0, &g_fpx[k], NULL};
    g_b.ui.cache[id] = &g_fimg[k];
}
static void rimg(int id) { fimg(id, g_fn, 0, 1); }
static int rdrawn(int id) { /* 1 when test image id was drawn in this frame */
    for (int k = 0; k < g_fn; k++)
        if (g_fid[k] == id) return px(g_fimg[k].x, g_fimg[k].y) != 0;
    return -1;
}
static int rcount(void) {
    int n = 0;
    for (int x = 0; x < 40; x++) n += px(x, 0) != 0;
    return n;
}

/* seeds g_b.sim so that its next n raw draws satisfy ok() (a draw ANDed with a mask is the draw's low bits) */
static int force_draws(int (*ok)(const uint32_t *), int n) {
    for (uint64_t seed = 1; seed < 20000000; seed++) {
        Sim t = g_b.sim;
        sim_seed(&t, seed);
        uint32_t raw[8];
        for (int i = 0; i < n; i++) raw[i] = sim_bits(&t, 0xFFFFFFFFu);
        if (ok(raw)) {
            sim_seed(&g_b.sim, seed);
            return 1;
        }
    }
    return 0;
}
/* the quiet branch: r1 = 0 (list B), r7 = 0 (pick C), r1 = 0, r3 = 2; the third draw's r7 is not 0, so a frame that
 * made the sound's r15 first would use r1 = 0 for list B and r7 != 0 for pick C, and draw no 717 */
static int quiet_r3_2(const uint32_t *d) {
    return (d[0] & 1) == 0 && (d[1] & 7) == 0 && (d[2] & 1) == 0 && (d[2] & 7) != 0 && (d[3] & 3) == 2;
}
static int show_d(const uint32_t *d) { return (d[0] & 3) == 0 && (d[1] & 1) == 0 && (d[2] & 3) == 0 && (d[3] & 7) == 3; }
static int show_no_d_r7_2(const uint32_t *d) { return (d[0] & 3) != 0 && (d[1] & 7) == 2; }
static int r7_3(const uint32_t *d) { return (d[0] & 7) == 3; }
static int r15_not_0(const uint32_t *d) { return (d[0] & 15) != 0; }
static int quiet_r7_4(const uint32_t *d) {
    return (d[0] & 1) == 0 && (d[1] & 7) == 4 && (d[2] & 1) == 0 && (d[3] & 3) == 1;
}
static int r1_0(const uint32_t *d) { return (d[0] & 1) == 0; }
static int no_clerk_c1(const uint32_t *d) { return (d[0] & 7) == 0 && (d[1] & 1) == 0 && (d[2] & 3) == 1; }

static Sound g_rsnd;
static uint32_t rsnd_random(void *ctx, uint32_t n) { return sim_random(ctx, n); }
static uint32_t rsnd_bits(void *ctx, uint32_t mask) { return sim_bits(ctx, mask); }

/* SCR-12 (docs/ui-rooms.md, screen 19, "What the engine must change"): the tables are made up apart from the entries
 * the doc's test values name (list A[0] 1320, list B[0] 613, pick C[2] 717, pick D[0] 1332, list E[0] 1307) */
static void test_reception_frame(void) {
    fresh();
    ui_fixture();
    g_b.anims_on = 1;
    sound_init(&g_rsnd, NULL, &g_b.prog, rsnd_random, &g_b.sim);
    sound_set_bits(&g_rsnd, rsnd_bits);
    g_b.sim.sound = &g_rsnd;
    for (int i = 0; i < 64; i++) put16(g_image + 0x4C2A + 2 * i, 1320 + i % 4);
    for (int i = 0; i < 32; i++) put16(g_image + 0x4BDA + 2 * i, 613 + i % 4);
    for (int i = 0; i < 32; i++) put16(g_image + 0x60B7 + 2 * i, 1307 + i % 4);
    static const uint16_t C[4] = {715, 716, 717, 718}, D[4] = {1332, 1335, 1331, 1336};
    for (int i = 0; i < 4; i++) put16(g_image + 0x4CAE + 2 * i, C[i]), put16(g_image + 0x4C1E + 2 * i, D[i]);
    g_fn = 0;
    for (int i = 0; i < 4; i++) rimg(1320 + i), rimg(613 + i), rimg(1307 + i), rimg(C[i]), rimg(D[i]);
    uint8_t *pb = g_world.block[BLK_PLAYER0], *r = rooms_reception(&g_b.sim);
    put16(r + 0x0A, 5), put16(r + 8, 7), r[0x45] = 0, pb[RB_DESK_HOVER] = 0, pb[RB_STAFF_HOVER] = 0;
#define RFRAME(tick)                                                                                                   \
    do {                                                                                                               \
        memset(g_b.screen.px, 0, sizeof g_b.screen.px);                                                                \
        pb[RB_TICK] = (tick), pb[0x56] = 1;                                                                            \
        rooms_routine(&g_b, 0x1FEF4);                                                                                  \
    } while (0)
    /* (1) the quiet branch: list A at 0, list B at 0, pick C entry 2 */
    CHECK(force_draws(quiet_r3_2, 4));
    RFRAME(1);
    CHECK(rdrawn(1320) == 1 && rdrawn(613) == 1 && rdrawn(717) == 1 && rcount() == 3);
    CHECK(g_b.reception_a == 2 && g_b.reception_b == 2 && g_b.reception_c == 717);
    CHECK(pb[RB_TICK] == 0 && pb[0x56] == 0);
    /* the same again: pick C's entry equals the last drawn, nothing */
    CHECK(force_draws(quiet_r3_2, 4));
    RFRAME(1);
    CHECK(rdrawn(1321) == 1 && rdrawn(614) == 1 && rdrawn(717) == 0 && rcount() == 2);
    CHECK(g_b.reception_a == 4 && g_b.reception_b == 4 && g_b.reception_c == 717);
    /* the desk hovered: no list A image, its position stays */
    pb[RB_DESK_HOVER] = 0xFF;
    CHECK(force_draws(quiet_r3_2, 4));
    RFRAME(1);
    CHECK(rdrawn(1322) == 0 && g_b.reception_a == 4 && rdrawn(615) == 1);
    pb[RB_DESK_HOVER] = 0;
    /* (2) the show branch: pick D 1332 and list E 1307, no draw of the quiet branch */
    r[0x45] = 9;
    int b_at = g_b.reception_b;
    CHECK(force_draws(show_d, 4));
    RFRAME(1);
    CHECK(rdrawn(1332) == 1 && rdrawn(1307) == 1 && rdrawn(1322) == 1 && rcount() == 3);
    CHECK(g_b.reception_d == 1332 && g_b.reception_e == 2 && g_b.reception_b == b_at && g_b.reception_c == 717);
    /* r7 = 2: no list E image, no pick D (r3 != 0) */
    CHECK(force_draws(show_no_d_r7_2, 2));
    RFRAME(1);
    CHECK(rcount() == 1 && rdrawn(1323) == 1 && g_b.reception_e == 2);
    /* the staff hotspot hovered without the show: the show branch without pick D */
    r[0x45] = 0, pb[RB_STAFF_HOVER] = 0xFF;
    CHECK(force_draws(r7_3, 1));
    RFRAME(1);
    CHECK(rdrawn(1308) == 1 && rdrawn(1320) == 1 && rcount() == 2 && g_b.reception_b == b_at);
    pb[RB_STAFF_HOVER] = 0;
    /* (3) no tick: no draw of step 2 at all (only the sound's r15), pb+0x3C stays 0 */
    CHECK(force_draws(r15_not_0, 1));
    Sim ref = g_b.sim;
    sim_bits(&ref, 15);
    int a_at = g_b.reception_a;
    RFRAME(0);
    CHECK(rcount() == 0 && pb[RB_TICK] == 0 && g_b.reception_a == a_at && pb[0x56] == 0);
    CHECK(g_b.sim.rng == ref.rng && g_b.sim.rng_s == ref.rng_s);
    /* (4) the wrap: list A from 0x7E, list E from 0x3E, to 0 after the draw */
    r[0x45] = 9, g_b.reception_a = 0x7E, g_b.reception_e = 0x3E;
    CHECK(force_draws(show_d, 4));
    RFRAME(1);
    CHECK(rdrawn(1323) == 1 && rdrawn(1310) == 1 && g_b.reception_a == 0 && g_b.reception_e == 0);
    /* (5) ANIMS off: nothing drawn, positions, last drawn and draws advance */
    g_b.anims_on = 0;
    r[0x45] = 0, g_b.reception_c = 0;
    CHECK(force_draws(quiet_r3_2, 4));
    b_at = g_b.reception_b;
    RFRAME(1);
    CHECK(rcount() == 0 && g_b.reception_a == 2 && g_b.reception_b == b_at + 2 && g_b.reception_c == 717);
    g_b.anims_on = 1;
    /* (6) pick C hangs on r7 (here 4), not on its low bits: no pick C although the next draws would pick entry 1 */
    CHECK(force_draws(quiet_r7_4, 4));
    RFRAME(1);
    CHECK(rdrawn(1321) == 1 && rcount() == 2 && !rdrawn(715) && !rdrawn(716) && !rdrawn(717) && !rdrawn(718));
    /* nobody at the desk: no list A, no r7 (and no sound draw): the clerk's r1 is the frame's only draw */
    put16(r + 0x0A, 0);
    CHECK(force_draws(r1_0, 1));
    ref = g_b.sim;
    sim_bits(&ref, 1);
    a_at = g_b.reception_a;
    RFRAME(1);
    CHECK(rcount() == 1 && g_b.reception_a == a_at && g_b.sim.rng == ref.rng && g_b.sim.rng_s == ref.rng_s);
    /* no clerk: no r1 for list B, the first draw is pick C's r7 */
    put16(r + 0x0A, 5), put16(r + 8, 0);
    b_at = g_b.reception_b;
    CHECK(force_draws(no_clerk_c1, 3));
    RFRAME(1);
    CHECK(rdrawn(716) == 1 && g_b.reception_b == b_at && rcount() == 2);
#undef RFRAME
    g_b.sim.sound = NULL;
    sound_free(&g_rsnd);
}

/* SCR-11 (docs/ui-departments.md, "What the engine must change"; docs/ui-rooms.md, "Sheet frame routines"): 75 on page
 * 1 and 59 on page 2 run the page-1 part; the mood box draws both texts; pb+0x47 is cleared after the redraw */
static void test_sheet_page1_part(void) {
    fresh();
    uint8_t *p = ward_patient(10);
    text_set(2392, "xx", 2), text_set(2386, "yyy", 3);
    texts_build();
    ui_fixture();
    g_fn = 0;
    static uint8_t folder_px[2] = {4, 4};
    static Image folder = {535, 2, 1, 5, 5, 0, folder_px, NULL};
    g_b.ui.cache[535] = &folder; /* the folder, drawn first by every redraw: (5, 5) and (6, 5) */
    fimg(949, 5, 5, 9);          /* the mood box at (5, 5) */
    uint8_t *pb = g_world.block[BLK_PLAYER0];
    put16(pb + RB_PATIENT, 10);
#define SFRAME()                                                                                                       \
    do {                                                                                                               \
        memset(g_b.screen.px, 0, sizeof g_b.screen.px);                                                                \
        rooms_ui_sheet_frame(&g_b);                                                                                    \
    } while (0)
    static const int SCR[3] = {75, 59, 57};
    for (int k = 0; k < 3; k++) {
        g_b.ui.current = SCR[k];
        put16(pb + RB_PAGE, SCR[k] == 59 ? 2 : 1);
        /* health 10 after 30 with pb+0x47: one redraw, pb+0x47 = 0 after the frame, none in the next */
        p[44] = 10, p[67] = 30, pb[RB_LOW_HEALTH] = 0xFF, pb[RB_MOOD] = 0, pb[RB_MINUTE] = 0;
        SFRAME();
        CHECK(px(6, 5) == 4 && pb[RB_LOW_HEALTH] == 0);
        SFRAME();
        CHECK(px(6, 5) == 0);
        /* the health changed with the chart request pb+0x2F: one redraw, both cleared; unchanged health: none */
        pb[0x2F] = 0xFF;
        SFRAME();
        CHECK(px(6, 5) == 4 && pb[0x2F] == 0 && pb[RB_LOW_HEALTH] == 0);
        p[67] = 10, pb[0x2F] = 0xFF;
        SFRAME();
        CHECK(px(6, 5) == 0 && pb[0x2F] == 0xFF);
        pb[0x2F] = 0;
        /* pb+0x49: box 949, the numbers and both texts (health 30: 2390 + 40 / 16 "xx", 2383 + 50 / 16 "yyy"), cleared */
        p[44] = p[67] = 30, p[78] = 40, p[20] = 50, pb[RB_MOOD] = 0xFF;
        SFRAME();
        CHECK(px(5, 5) == 9 && px(6, 5) == 0 && pb[RB_MOOD] == 0);
        CHECK(row_count(262, 400, 500) > 0 && row_count(301, 400, 500) > 0);
        CHECK(row_count(275, 200, 470) == 2 && row_count(314, 200, 470) == 3);
    }
    /* screen 75: the page-1 part before its own page-1 request (0x41): the folder over the box.
     * An unrelated minute request survives; it does not stand in for the contextual flag (UI-113). */
    g_b.ui.current = 75, put16(pb + RB_PAGE, 1);
    pb[RB_MOOD] = 0xFF, pb[RB_MINUTE] = 0xFF, pb[0x41] = 0xFF;
    SFRAME();
    CHECK(px(5, 5) == 4 && px(6, 5) == 4 && pb[RB_MOOD] == 0 && pb[RB_MINUTE] == 0xFF && pb[0x41] == 0);
    SFRAME();
    CHECK(px(5, 5) == 0 && px(6, 5) == 0 && pb[RB_MINUTE] == 0xFF);
    pb[RB_MINUTE] = 0;
    /* screen 57 page 2 does not run it: no redraw, the flags stay */
    g_b.ui.current = 57, put16(pb + RB_PAGE, 2);
    p[44] = 10, p[67] = 30, pb[RB_LOW_HEALTH] = pb[RB_MOOD] = 0xFF;
    SFRAME();
    CHECK(px(5, 5) == 0 && px(6, 5) == 0 && pb[RB_LOW_HEALTH] == 0xFF && pb[RB_MOOD] == 0xFF);
    /* nor do 75 and 59 on page 0 */
    static const int P0[2] = {75, 59};
    for (int k = 0; k < 2; k++) {
        g_b.ui.current = P0[k], put16(pb + RB_PAGE, 0);
        SFRAME();
        CHECK(px(5, 5) == 0 && px(6, 5) == 0 && pb[RB_LOW_HEALTH] == 0xFF && pb[RB_MOOD] == 0xFF);
    }
#undef SFRAME
    g_b.ui.current = 0;
}

/* UI-113: every contextual flag, including flags belonging to other sheets, on both pages.
 * The draw hook observes requests before rendering, so clearing too early is distinguishable from
 * clearing afterwards. Pixels also check the actual remarks/chart calls, not only their backgrounds. */
static const uint8_t SHEET_REQUEST_FLAGS[] = {
    0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x37, 0x38,
    0x39, 0x3A, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x9F, 0xA0
};
typedef struct {
    int count, image[8];
    uint8_t flags[8][sizeof SHEET_REQUEST_FLAGS];
} SheetDrawLog;

static void sheet_request_draw(void *ctx, int image) {
    if (image != 535 && image != 576 && image != 577) return;
    SheetDrawLog *log = ctx;
    int i = log->count++;
    CHECK(i < 8);
    if (i >= 8) return;
    log->image[i] = image;
    for (size_t k = 0; k < sizeof SHEET_REQUEST_FLAGS; k++)
        log->flags[i][k] = g_world.block[BLK_PLAYER0][SHEET_REQUEST_FLAGS[k]];
}

static void check_sheet_request_draw(const SheetDrawLog *log, int index, int image, const uint8_t *flags) {
    CHECK(index < log->count && index < 8);
    if (index >= log->count || index >= 8) return;
    CHECK(log->image[index] == image);
    for (size_t k = 0; k < sizeof SHEET_REQUEST_FLAGS; k++)
        CHECK(log->flags[index][k] == flags[SHEET_REQUEST_FLAGS[k]]);
}

static void test_sheet_contextual_requests(void) {
    static const struct { int screen, state, remarks, chart, progress; } CASES[] = {
        {57, 2, 0x2E, 0x2F, 0}, {59, 3, 0x2E, 0x30, 0}, {61, 5, 0x32, 0x31, 0x32},
        {62, 6, 0x33, 0x34, 0x35}, {43, 16, 0x33, 0x37, 0x38}, {70, 11, 0x3A, 0x39, 0x3A},
        {73, 7, 0x2E, 0x3E, 0x3F}, {75, 15, 0x41, 0x40, 0x41},
        {84, 9, 0x33, 0x42, 0x43}, {126, 8, 0x2E, 0x9F, 0xA0}
    };
    for (size_t context = 0; context < sizeof CASES / sizeof *CASES; context++) {
        fresh();
        ui_fixture();
        g_fn = 0;
        fimg(535, 5, 5, 4), fimg(576, 6, 5, 5), fimg(577, 7, 5, 6);
        uint8_t *q = g_world.block[BLK_PLAYER0];
        int remark_flag = CASES[context].remarks, chart_flag = CASES[context].chart;
        int progress_flag = CASES[context].progress;
        for (int page = 0; page < 2; page++) {
            /* No flags; all 20 individual flags; all eight combinations of this context's three requests. */
            for (size_t setting = 0; setting < 1 + sizeof SHEET_REQUEST_FLAGS + 8; setting++) {
                memset(q, 0, g_world.size[BLK_PLAYER0]);
                put16(q + RB_PATIENT, 10), put16(q + RB_PAGE, (uint16_t)page);
                uint8_t *p = ward_patient(10);
                p[4] = (uint8_t)CASES[context].state;
                p[44] = 50;
                memset(p + 67, 50, 11); /* unchanged health/history, no low-health or death request */
                g_b.ui.current = CASES[context].screen;
                if (setting && setting <= sizeof SHEET_REQUEST_FLAGS)
                    q[SHEET_REQUEST_FLAGS[setting - 1]] = 0x7F;
                else if (setting > sizeof SHEET_REQUEST_FLAGS) {
                    size_t bits = setting - sizeof SHEET_REQUEST_FLAGS - 1;
                    if (bits & 1) q[remark_flag] = 1;
                    if (bits & 2) q[chart_flag] = 0xFF;
                    if ((bits & 4) && progress_flag) q[progress_flag] = 0x80;
                }
                uint8_t expected[0xA1] = {0};
                for (size_t k = 0; k < sizeof SHEET_REQUEST_FLAGS; k++)
                    expected[SHEET_REQUEST_FLAGS[k]] = q[SHEET_REQUEST_FLAGS[k]];
                int want_remarks = page == 0 && q[remark_flag];
                int want_chart = page == 0 && q[chart_flag] && !(CASES[context].screen == 43 && want_remarks);
                int want_whole = page == 1 && progress_flag && q[progress_flag];
                SheetDrawLog log = {0};
                g_b.ui.load_hook = sheet_request_draw, g_b.ui.load_ctx = &log;
                memset(g_b.screen.px, 0, sizeof g_b.screen.px);
                rooms_ui_sheet_frame(&g_b);
                int draws = 0;
                if (want_remarks) {
                    check_sheet_request_draw(&log, draws++, 576, expected);
                    expected[remark_flag] = 0;
                    if (progress_flag) expected[progress_flag] = 0;
                    if (CASES[context].screen == 43) expected[chart_flag] = 0;
                }
                if (want_chart) {
                    check_sheet_request_draw(&log, draws++, 577, expected);
                    expected[chart_flag] = 0;
                }
                if (want_whole) {
                    check_sheet_request_draw(&log, draws++, 535, expected);
                    expected[progress_flag] = 0;
                }
                CHECK(log.count == draws);
                CHECK((px(5, 5) == 4) == want_whole);
                CHECK((px(6, 5) == 5) == want_remarks);
                CHECK((px(7, 5) == 6) == want_chart);
                if (page == 0) {
                    CHECK(row_drawn(262, 214, 467) == want_remarks);
                    CHECK((px(370, 89) == 0x95) == want_chart);
                }
                for (size_t k = 0; k < sizeof SHEET_REQUEST_FLAGS; k++)
                    CHECK(q[SHEET_REQUEST_FLAGS[k]] == expected[SHEET_REQUEST_FLAGS[k]]);
                /* No request repeats on the next frame; unrelated requests remain pending. */
                memset(&log, 0, sizeof log);
                memset(g_b.screen.px, 0, sizeof g_b.screen.px);
                rooms_ui_sheet_frame(&g_b);
                CHECK(log.count == 0 && px(5, 5) == 0 && px(6, 5) == 0 && px(7, 5) == 0);
                for (size_t k = 0; k < sizeof SHEET_REQUEST_FLAGS; k++)
                    CHECK(q[SHEET_REQUEST_FLAGS[k]] == expected[SHEET_REQUEST_FLAGS[k]]);
            }
        }
        g_b.ui.load_hook = NULL, g_b.ui.load_ctx = NULL;
    }
}

/* FIX-12 (docs/fixes.md): drawing a sheet with the use flag set: off wears (10 - 3 = 7; wear byte 0: one r3 draw;
 * 2 - 3: removed), on no wear, no draw, no removal */
static void test_fix12_sheet_wear(void) {
    for (int on = 0; on < 2; on++) {
        fixes_set(on ? 1u << 11 : 0);
        fresh();
        treatment_room_patient(10);
        both(0x6A71, 0x875B, 48, computed("Fixture action: "));
        both(0x6C2D, 0x8917, 48, image_str("", 0));
        static const char POKER[] = "Fixture tool (AA)";
        text_set(700, POKER, strlen(POKER));
        texts_build();
        put32(ITEM(6), 700), ITEM(6)[13] = 14, ITEM(6)[7] = 0, ITEM(6)[16] = 3;
        uint8_t *r = rec(5, 1);
        g_world.block[BLK_ROOM_COUNTS][5] = 2;
        put16(r + 0x60, 6), r[0x70] = 10;
        s->equipment_use = 1;
        sheet(10);
        CHECK(r[0x70] == (on ? 10 : 7));
        /* wear byte 0: off one more r3 draw than a draw with the flag clear, on the same draws */
        ITEM(6)[16] = 0, r[0x70] = 10;
        uint32_t s0 = s->rng_s, t0 = s->rng_time;
        uint64_t a0 = s->rng;
        s->equipment_use = 0;
        sheet(10);
        uint32_t s1 = s->rng_s;
        uint64_t a1 = s->rng;
        s->rng_s = s0, s->rng = a0, s->rng_time = t0, s->equipment_use = 1;
        sheet(10);
        CHECK(on ? (s->rng_s == s1 && s->rng == a1 && r[0x70] == 10) : (s->rng_s != s1 || s->rng != a1));
        ITEM(6)[16] = 3, r[0x70] = 2; /* 2 - 3: off removed, on kept */
        sheet(10);
        CHECK(on ? (u16(r + 0x60) == 6 && r[0x70] == 2) : u16(r + 0x60) == 0);
        s->equipment_use = 0;
    }
    fixes_set(0);
}

int main(void) {
    test_page_turn_hover();
    test_rating_sheet_frames();
    test_rating_sheet_render_branches();
    test_rating_nonwriting_ui_paths();
    test_fix12_sheet_wear();
    test_treatment_line();
    test_symptom_zero_byte();
    test_diagnosis_variant();
    test_step_tables();
    test_ward_mood();
    test_neuro_ward();
    test_instruments();
    test_cut_names();
    test_treatment_room();
    test_dental_neurology();
    test_stage_texts();
    test_selectors();
    test_staff_lines();
    test_capability_map();
    test_random_codes();
    test_drugs();
    test_every_state();
    test_home_rules();
    test_course_rules();
    test_reception_waiting_page();
    test_boundaries();
    test_club_golf();
    test_home_and_thugs();
    test_warehouse();
    test_skip_hour_and_tick();
    test_step_code_zero();
    test_equipment_list_length();
    test_diagnosing_name_uncut();
    test_pathology_row_bounds();
    test_image_string_cap();
    test_image_end();
    test_page_fence();
    test_pick_draws();
    test_drawn_positions();
    test_personnel_sheet_lines();
    test_remark_bare_symptom();
    test_health_chart();
    test_scr10_page1();
    test_scr10_ui();
    test_scr13_bar_order();
    test_reception_frame();
    test_sheet_page1_part();
    test_sheet_contextual_requests();
    test_campaign_pitch();
    test_campaign_status_counts();
    test_holdings_columns();
    test_help_strip_same_frame();
    test_english_sheet_articles();
    test_english_screens();
    test_english_race_winner();
    test_german_screenshot();
    test_save_load_continue();
    world_free(&g_world);
    if (failures) {
        fprintf(stderr, "[FAIL] %d check(s) failed\n", failures);
        return 1;
    }
    printf("[OK] sheet\n");
    return 0;
}
