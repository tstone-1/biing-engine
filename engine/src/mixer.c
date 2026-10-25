#include "mixer.h"

void voice_start(Voice *v, const int8_t *data, uint32_t len, int loop) {
    v->data = data;
    v->len = len;
    v->rep = loop ? data : NULL;
    v->rep_len = loop ? len : 0;
    v->pos = 0;
    v->active = data && len > 0;
    v->idle = !v->active;
}

void voice_stop(Voice *v) {
    v->active = 0;
    v->idle = 1;
    v->rep_len = 0;
}

int mixer_volume(int s, int row, int stereo) {
    /* (s + 128) >= 0, so the division is a floor; ceil(r / 2) = (r + 1) / 2 */
    if (stereo) return (s + 128) * (2 * row + 1) / 256 - row;
    return (s + 128) * (row + 1) / 256 - (row + 1) / 2;
}

static int voice_sample(Voice *v, int stereo) {
    if (!v->active) return 0;
    int s = mixer_volume(v->data[v->pos >> 8], v->row, stereo);
    v->pos += v->step >> 8; /* bits 8-31 of the step word: an 8-bit fraction */
    if ((v->pos >> 8) >= v->len) {
        v->idle = 1;
        if (v->rep && v->rep_len >= 4) {
            v->data = v->rep;
            v->len = v->rep_len;
            v->pos = 0; /* the overshoot is dropped */
        } else {
            v->active = 0;
        }
    }
    return s;
}

static int16_t widen(int sum) {
    uint8_t byte = (uint8_t)(0x80 + sum); /* byte wrap-around, no clipping */
    return (int16_t)((byte - 0x80) * 256);
}

void mixer_render(Voice v[MIXER_VOICES], int stereo, int16_t *out, int n) {
    for (int i = 0; i < n; i++) {
        if (stereo) {
            int left = voice_sample(&v[0], 1) + voice_sample(&v[3], 1);
            int right = voice_sample(&v[1], 1) + voice_sample(&v[2], 1);
            out[2 * i] = widen(left);
            out[2 * i + 1] = widen(right);
        } else {
            int sum = 0;
            for (int k = 0; k < MIXER_VOICES; k++) sum += voice_sample(&v[k], 0);
            out[2 * i] = out[2 * i + 1] = widen(sum);
        }
    }
}

int mixer_tick_samples(uint32_t *acc) {
    /* 21,739.13 / 50 = 20,000 / 46 samples per tick */
    *acc += 20000;
    int n = (int)(*acc / MIXER_RATE_DEN);
    *acc %= MIXER_RATE_DEN;
    return n;
}
