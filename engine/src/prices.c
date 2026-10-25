/* The price screen's rules (docs/ui-prices.md); see prices.h. */
#include "prices.h"

#include "sim_internal.h"

const uint8_t PRICE_FIELD[PRICE_LINES] = {0x95, 0x25, 0x22, 0x23, 0x24, 0x20, 0x21, 0x94};

static uint8_t *player(Sim *s) { return hospital(s, 0); } /* 0x19B76: the current player, record 0 here */

int prices_value(Sim *s, int line) { return player(s)[PRICE_FIELD[line]]; }

int prices_change(Sim *s, int line, int delta) {
    uint8_t *v = player(s) + PRICE_FIELD[line];
    if (delta > 0 ? *v >= 9 : *v < 2) return 0; /* limits 1 and 9, for the base fee too */
    *v = (uint8_t)(*v + (delta > 0 ? 1 : -1));
    world_price_index(s->world, s->prog);
    return 1;
}

int prices_average(Sim *s) {
    unsigned sum = 0;
    for (int h = 0; h < 16; h++) sum += (uint16_t)get16(hospital(s, h) + 0x10A);
    return (int)((uint16_t)sum / 16u); /* a u16 sum, divided unsigned */
}

int prices_comparison(Sim *s) {
    world_price_index(s->world, s->prog);
    return prices_band((int16_t)(uint16_t)((uint16_t)get16(player(s) + 0x10A) - (uint16_t)prices_average(s)));
}

int prices_band(int d) {
    if (d >= -2 && d <= 2) return 144;
    if (d >= 40) return 142;
    if (d >= 20) return 141;
    if (d >= 3) return 143;
    if (d <= -40) return 147;
    if (d <= -20) return 145;
    return 146;
}

int prices_back(Sim *s) {
    uint8_t *r = player(s);
    if (get16(r + 0x10A) == get16(r + 0x10C)) return 0;
    sim_news_price(s, 0); /* [0x45BE] = [0xB054], the player's hospital number */
    put16(r + 0x10C, get16(r + 0x10A));
    return 1;
}
