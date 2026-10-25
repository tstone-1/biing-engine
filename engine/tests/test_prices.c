/* The price screen (docs/ui-prices.md). Synthetic world and program image (see AGENTS.md): the level table below is
 * made up for the test; the expected values follow the spec's rules. */
#include <stdio.h>
#include <string.h>

#include "prices.h"
#include "sim_internal.h"

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

static const int V[10] = {0, 0, 10, 16, 21, 25, 28, 30, 31, 32}; /* synthetic levels 0-9 (u16 table 0x465E) */

static uint8_t *h(int i) { return world_hospital(&g_world, i); }
static int index_of(int i) { return (uint16_t)get16(h(i) + 0x10A); }
static int price_news(void) {
    int n = 0;
    for (int i = 0; i < 100; i++) n += sim_news_item(s, i)[0] && sim_news_item(s, i)[20] == 0x16;
    return n;
}
static const uint8_t *last_news(void) {
    for (int i = 0; i < 100; i++)
        if (sim_news_item(s, i)[0] && sim_news_item(s, i)[20] == 0x16) return sim_news_item(s, i);
    return NULL;
}

static void fresh(void) {
    world_free(&g_world);
    world_alloc(&g_world);
    memset(g_image, 0, sizeof g_image);
    for (int i = 0; i < 10; i++) put16(g_image + 0x465E + 2 * i, V[i]);
    g_prog.image.data = g_image;
    g_prog.image.size = sizeof g_image;
    world_set_tail(&g_world, TAIL_HOUR, 10);
    sim_init(&g_sim, &g_world, &g_prog, 3);
    for (int i = 0; i < 16; i++) { /* a new game's keys: 2 each, base fee 9 */
        for (int k = 0x20; k <= 0x25; k++) h(i)[k] = 2;
        h(i)[0x94] = 2, h(i)[0x95] = 9;
    }
    world_price_index(&g_world, &g_prog);
    for (int i = 0; i < 16; i++) put16(h(i) + 0x10C, index_of(i));
}

static void test_keys_and_index(void) {
    fresh();
    /* nothing built: Krankenzimmer and OP count level 3, Intensiv 0; four keys of 2, minus 2 x 9 */
    int base = V[3] + V[3] + 4 * V[2] - 18;
    CHECK(index_of(0) == base && prices_average(s) == base && prices_comparison(s) == 144);
    CHECK(prices_value(s, 1) == 2 && prices_change(s, 1, 1) && h(0)[0x25] == 3); /* Schwester + 1 */
    CHECK(index_of(0) == base + V[3] - V[2]);
    CHECK(prices_average(s) == (15 * base + base + V[3] - V[2]) / 16);
    CHECK(prices_change(s, 2, 1) && h(0)[0x22] == 3 && index_of(0) == base + V[3] - V[2]); /* no OP: no effect */
    g_world.block[BLK_ROOM_COUNTS][7] = 1; /* with an OP the key counts */
    CHECK(prices_change(s, 2, -1) && h(0)[0x22] == 2 && index_of(0) == base + V[3] - V[2] + V[2] - V[3]);
    g_world.block[BLK_ROOM_COUNTS][7] = 0;
    CHECK(prices_change(s, 0, -1) && h(0)[0x95] == 8 && index_of(0) == base + V[3] - V[2] + 2); /* base fee */
    /* the limits: 9 and 1, base fee included; nothing changes */
    h(0)[0x95] = 9;
    CHECK(!prices_change(s, 0, 1) && h(0)[0x95] == 9);
    h(0)[0x94] = 1;
    CHECK(!prices_change(s, 7, -1) && h(0)[0x94] == 1);
    CHECK(prices_change(s, 7, 1) && h(0)[0x94] == 2);
    h(0)[0x24] = 8;
    CHECK(prices_change(s, 4, 1) && h(0)[0x24] == 9 && !prices_change(s, 4, 1) && h(0)[0x24] == 9);
    h(0)[0x21] = 2;
    CHECK(prices_change(s, 6, -1) && h(0)[0x21] == 1 && !prices_change(s, 6, -1)); /* Intensiv */
    CHECK(prices_change(s, 5, 1) && h(0)[0x20] == 3 && prices_change(s, 3, 1) && h(0)[0x23] == 3);
    /* the index is floored at 0 (with the rooms built, keys of 1 are worth 0) */
    g_world.block[BLK_ROOM_COUNTS][6] = g_world.block[BLK_ROOM_COUNTS][7] = 1;
    for (int k = 0x20; k <= 0x25; k++) h(0)[k] = 1;
    h(0)[0x94] = 1, h(0)[0x95] = 8;
    CHECK(prices_change(s, 0, 1) && index_of(0) == 0);
}

static void test_bands(void) {
    static const struct { int d, text; } B[] = {
        {0, 144},  {2, 144},  {-2, 144},  {3, 143},   {19, 143},  {20, 141},  {39, 141},  {40, 142},
        {300, 142}, {-3, 146}, {-19, 146}, {-20, 145}, {-39, 145}, {-40, 147}, {-300, 147}};
    for (size_t i = 0; i < sizeof B / sizeof B[0]; i++) CHECK(prices_band(B[i].d) == B[i].text);
    fresh();
    for (int i = 1; i < 16; i++) /* everyone else much cheaper: their indexes far above the player's */
        h(i)[0x23] = h(i)[0x24] = h(i)[0x25] = h(i)[0x94] = 9, h(i)[0x95] = 1;
    CHECK(prices_comparison(s) == 147); /* own index far below the average: "weit über dem Niveau" */
}

static void test_back(void) {
    fresh();
    CHECK(!prices_back(s) && price_news() == 0); /* unchanged: nothing */
    int old = index_of(0);
    prices_change(s, 1, 1), prices_change(s, 1, -1);
    CHECK(!prices_back(s) && price_news() == 0); /* back and forth within one visit: no trace */
    prices_change(s, 1, -1); /* dearer: the index falls */
    CHECK(prices_back(s) && get16(h(0) + 0x10C) == index_of(0) && index_of(0) == old - (V[2] - V[1]));
    const uint8_t *it = last_news();
    CHECK(it && it[0] == 2 && it[4] == 10 && (int8_t)it[5] == -2 && it[6] == 4 && it[21] == 2 && it[8] == 0);
    /* cheaper, outside the hours 4-18: no item, but the last index is written all the same */
    for (int i = 0; i < 100; i++) sim_news_item(s, i)[0] = 0;
    world_set_tail(&g_world, TAIL_HOUR, 19);
    prices_change(s, 1, 1);
    CHECK(prices_back(s) && price_news() == 0 && get16(h(0) + 0x10C) == old);
    world_set_tail(&g_world, TAIL_HOUR, 4);
    prices_change(s, 1, 1);
    CHECK(prices_back(s) && price_news() == 1 && last_news()[21] == 1 && last_news()[4] == 8);
    CHECK((int8_t)last_news()[5] == 1 && last_news()[6] == 3);
}

int main(void) {
    test_keys_and_index();
    test_bands();
    test_back();
    world_free(&g_world);
    if (failures) {
        fprintf(stderr, "[FAIL] %d checks failed\n", failures);
        return 1;
    }
    printf("[OK] all checks passed\n");
    return 0;
}
