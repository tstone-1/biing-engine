/* The four software-mixed voices of the sound driver (docs/sound.md, "Voices and output"; docs/music.md, "Driver
 * timing and mixing"). Sound effects (sound.c) and the digital music (music.c) share them, as in the original.
 * Screen-free: mixer_render writes samples into a buffer, so tests and headless runs need no audio device. */
#ifndef BIING_MIXER_H
#define BIING_MIXER_H

#include <stddef.h>
#include <stdint.h>

/* Output rate: Sound Blaster time constant 210 = 1,000,000 / 46 Hz = 21,739.13 Hz (docs/music.md). */
#define MIXER_RATE_NUM 1000000
#define MIXER_RATE_DEN 46
#define MIXER_RATE 21739 /* rounded, for the audio device and WAV headers */
#define MIXER_VOICES 4
#define MIXER_MAX_TICK 436 /* output samples per 50 Hz tick: 434 or 435 */

/* A voice works like the Amiga's Paula: a running region (taken at a start) and a repeat region loaded when the
 * running region ends; a repeat length under 4 stops the voice. At a region end the position restarts at 0 (the
 * overshoot is dropped). docs/music.md, "Driver timing and mixing". */
typedef struct {
    const int8_t *data; /* running region */
    uint32_t len;
    const int8_t *rep; /* repeat region */
    uint32_t rep_len;
    uint32_t pos; /* position in the running region, 1/256 source samples */
    uint32_t step; /* step word, 16.16 fixed point; the mixer uses bits 8-31 only */
    int row; /* volume row 0-64 (docs/sound.md, "Volume") */
    int active; /* sounding */
    int idle; /* set when the voice stops and at every wrap into the repeat region */
} Voice;

/* Starts a voice with the region (data, len); loop = the whole region is also the repeat region. The original
 * sets a latch the mixer takes at its next pass; the engine takes it at once (docs/sound.md, "Start": the driver
 * waits for the latch anyway). */
void voice_start(Voice *v, const int8_t *data, uint32_t len, int loop);
void voice_stop(Voice *v);

/* Volume table: a signed sample s (-128..127) at row r (docs/sound.md, "Volume"). Mono: floor((s + 128)(r + 1) / 256)
 * - ceil(r / 2) (measured in all 16,384 entries of the original's table); stereo (SB Pro): floor((s + 128)(2r + 1) /
 * 256) - r (from the code that builds it, not measured). */
int mixer_volume(int s, int row, int stereo);

/* Mixes n output frames into out (interleaved left/right, signed 16-bit). Every voice adds its table value into an
 * unsigned 8-bit byte that starts at 0x80, with byte wrap-around and no clipping (docs/sound.md, "Mixing"); the
 * byte is then widened to 16 bits. Mono: all four voices into both channels. Stereo: voices 0 and 3 left, 1 and 2
 * right (even bytes = left on the SB Pro is a guess, docs/sound.md, "Open"). */
void mixer_render(Voice v[MIXER_VOICES], int stereo, int16_t *out, int n);

/* Output samples of the next 50 Hz tick: 434 or 435, so that 50 ticks give 21,739.13 samples (acc keeps the
 * remainder; start it at 0). */
int mixer_tick_samples(uint32_t *acc);

#endif
