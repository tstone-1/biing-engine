/* Run the production window/startup audio path with synthetic stereo, no game data.
 * CTest uses dummy devices. For a silent native-output probe, set SDL_VIDEODRIVER=dummy
 * and select the real SDL_AUDIODRIVER: postmix inspects only this logical device and
 * clears its samples before playback. This is not a physical speaker/listening test. */
#undef SDL_OpenAudioDeviceStream
#undef SDL_PutAudioStreamData
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "startup.h"

#undef main
int audio_test_main(int argc, char **argv);

static int failed, intro_case, in_intro, game_ticks, intro_ticks, submitted[2];
static int expected_frames, sample_remainder;
static int16_t expected_left, expected_right;
/* Only the audio thread writes these; read after the production loop destroys the stream
 * and SDL_Quit joins the device thread. No locks or I/O in the callback. */
static unsigned callbacks, matched[2], other;
static int device_rate, device_channels;

static void SDLCALL postmix(void *unused, const SDL_AudioSpec *spec, float *buffer, int bytes) {
    (void)unused;
    callbacks++;
    device_rate = spec->freq;
    device_channels = spec->channels;
    if (spec->channels == 2) {
        for (int i = 0; i + 1 < bytes / (int)sizeof(float); i += 2) {
            float l = buffer[i], r = buffer[i + 1];
            if (SDL_fabsf(l - 0.25f) < 0.001f && SDL_fabsf(r + 0.125f) < 0.001f) matched[0]++;
            else if (SDL_fabsf(l + 0.25f) < 0.001f && SDL_fabsf(r - 0.5f) < 0.001f) matched[1]++;
            else if (SDL_fabsf(l) > 0.001f || SDL_fabsf(r) > 0.001f) other++;
        }
    }
    memset(buffer, 0, (size_t)bytes); /* keep the real-output probe silent */
}

SDL_AudioStream *audio_test_open_stream(SDL_AudioDeviceID device, const SDL_AudioSpec *spec,
                                       SDL_AudioStreamCallback callback, void *userdata) {
    if (!spec || spec->format != SDL_AUDIO_S16 || spec->channels != 2 || spec->freq != 21739) {
        fprintf(stderr, "[FAIL] wrong production audio input format\n");
        failed = 1;
    }
    SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(device, spec, callback, userdata);
    if (!stream || !SDL_SetAudioPostmixCallback(SDL_GetAudioStreamDevice(stream), postmix, NULL)) {
        fprintf(stderr, "[FAIL] cannot observe audio device: %s\n", SDL_GetError());
        failed = 1;
        if (stream) SDL_DestroyAudioStream(stream);
        return NULL;
    }
    /* Deliberately do not resume, feed, drain or reconfigure the stream here. */
    return stream;
}

bool audio_test_put_stream(SDL_AudioStream *stream, const void *data, int bytes) {
    const int16_t *samples = data;
    if (bytes != expected_frames * 4) {
        fprintf(stderr, "[FAIL] audio submission has %d bytes, expected %d\n", bytes, expected_frames * 4);
        failed = 1;
    } else {
        for (int i = 0; i < expected_frames; i++) {
            if (samples[i * 2] != expected_left || samples[i * 2 + 1] != expected_right) {
                fprintf(stderr, "[FAIL] wrong stereo submission at frame %d\n", i);
                failed = 1;
                break;
            }
        }
    }
    submitted[in_intro ? 0 : 1]++;
    return SDL_PutAudioStreamData(stream, data, bytes);
}

static void signal_tick(Sound *sound) {
    sample_remainder += 21739;
    expected_frames = sample_remainder / 50;
    sample_remainder %= 50;
    expected_left = (int16_t)(in_intro ? 8192 : -8192);
    expected_right = (int16_t)(in_intro ? -4096 : 16384);
    memset(sound->out, 0x7f, sizeof sound->out); /* stale tail must not be submitted */
    for (int i = 0; i < expected_frames; i++) {
        sound->out[2 * i] = expected_left;
        sound->out[2 * i + 1] = expected_right;
    }
    sound->out_frames = expected_frames;
}

int biing_open(Biing *b, const char *root) {
    (void)root;
    memset(b, 0, sizeof *b);
    world_alloc(&b->world);
    sim_init(&b->sim, &b->world, &b->prog, 0);
    return 0;
}
void biing_close(Biing *b) { world_free(&b->world); }
void biing_interrupt(Biing *b, int held) { (void)held; in_intro = 0; game_ticks++; signal_tick(&b->sound); }
void biing_frame(Biing *b) { if (game_ticks >= 50) b->quit = 1; }
int view_main(const char *root) { (void)root; return 2; }

int startup_open(Startup *s, const char *root, struct Sound *sound, int write_marker, int marker, uint32_t seed) {
    (void)root; (void)sound; (void)write_marker; (void)marker; (void)seed;
    memset(s, 0, sizeof *s);
    s->plan[0] = START_PRE;
    s->nplan = 1; /* unopened stage renders black; no marker or game files */
    in_intro = 1;
    return 0;
}
void startup_advance(Startup *s, IntroInput *input) {
    (void)input;
    if (++s->units == INTRO_UNITS) s->at = s->nplan;
}
void sound_interrupt(Sound *sound) { intro_ticks++; signal_tick(sound); }

int main(int argc, char **argv) {
    intro_case = argc == 2 && !strcmp(argv[1], "intro");
    char *args[] = {argv[0], "synthetic", "--no-intro"};
    if (audio_test_main(intro_case ? 2 : 3, args)) return 1;
    if (game_ticks != 50 || submitted[1] != game_ticks ||
        intro_ticks != (intro_case ? 50 : 0) || submitted[0] != intro_ticks) {
        fprintf(stderr, "[FAIL] generated/submitted ticks: intro %d/%d game %d/%d\n",
                intro_ticks, submitted[0], game_ticks, submitted[1]);
        failed = 1;
    }
    if (!callbacks || device_channels != 2 || device_rate <= 0 ||
        matched[1] < (unsigned)device_rate / 5 ||
        (intro_case && matched[0] < (unsigned)device_rate / 5) ||
        other > (unsigned)device_rate / 20) {
        fprintf(stderr, "[FAIL] postmix callbacks=%u format=%d/%d frames intro=%u game=%u other=%u\n",
                callbacks, device_rate, device_channels, matched[0], matched[1], other);
        failed = 1;
    }
    if (!failed) printf("[PASS] %s stereo submission and postmix: %u callbacks, %d Hz, %u/%u matching frames\n",
                        intro_case ? "intro/game" : "game", callbacks, device_rate, matched[0], matched[1]);
    return failed;
}
