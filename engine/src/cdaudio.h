/* CD audio music (docs/cdrom.md, "CD audio music in BIPRO.EXE"; docs/parity.md SND-8): the CD drive the original
 * plays its songs on, as far as the engine needs it - a track plays from its start to its end and then stands
 * (the drive is no longer busy); stop stops it. Screen-free and device-free: cd_render mixes the playing track into
 * the driver's output buffer at the Sound Blaster mixer's CD level.
 *
 * Where the audio comes from: the tracks of the user's GOG CD image (game.ins + game.gog), which tools/setup_game.py
 * writes into the game folder as CDAUDIO/TRACKnn.WAV (16-bit stereo 44,100 Hz PCM, the image's audio sectors as
 * they are; tools/biing_cdrom.py extract writes the same files). A game folder without them has no CD audio: the
 * engine then plays the modules (sound.c). */
#ifndef BIING_CDAUDIO_H
#define BIING_CDAUDIO_H

#include <stddef.h>
#include <stdint.h>

#define CD_TRACKS 100
#define CD_RATE 44100 /* frames per second of a CD audio track */

typedef struct {
    char dir[1100]; /* the CDAUDIO folder */
    int16_t *pcm[CD_TRACKS]; /* interleaved stereo; loaded at the track's first play */
    uint32_t frames[CD_TRACKS];
    int8_t state[CD_TRACKS]; /* 0 not tried, 1 loaded, -1 missing or not 16-bit stereo 44.1 kHz PCM */
    int track; /* the track the drive plays, 0 none */
    uint64_t pos; /* position in the track, frames in 32.32 fixed point */
    int busy; /* the drive's busy bit: set by a play, cleared when the track ends or by a stop */
    int level; /* the mixer's CD level 0-15 (SOUND.CFG MIXER, fourth character) */
} CdAudio;

/* The folder holding TRACKnn.WAV (root/CDAUDIO); returns 1 when it holds a file TRACK05.WAV (the first song track),
 * else 0 (no CD audio). The tracks are read at their first play; one that is no 16-bit stereo 44.1 kHz PCM WAV leaves
 * the drive standing (an [ERROR] line). */
int cd_open(CdAudio *cd, const char *root, int level);
void cd_free(CdAudio *cd);
/* WAV data of a CD track: 16-bit, 2 channels, 44,100 Hz PCM; points into the file bytes. 0 or -1. */
int cd_wav_parse(const uint8_t *d, size_t n, const int16_t **pcm, uint32_t *frames);
/* Play request: the whole track from its start (busy until its end). A missing track leaves the drive standing. */
void cd_play(CdAudio *cd, int track);
void cd_stop(CdAudio *cd);
/* Mixes n output frames at the driver's rate (1,000,000 / 46 Hz) into out (interleaved stereo, clipped), scaled by
 * level / 15; advances the track and clears busy at its end. */
void cd_render(CdAudio *cd, int16_t *out, int n);

#endif
