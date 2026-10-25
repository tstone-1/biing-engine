/* The digital music player: the big-endian module format and its sequencer (docs/music.md, "Digital module",
 * "Song section", "Instrument section and instrument programs", "Driver timing and mixing"). The module plays on
 * the four mixer voices the sound effects use (mixer.h). Screen-free and device-free: music_tick advances one
 * 50 Hz timer tick and only writes voice records.
 *
 * With a MIDI card (MCARD ROL / GM) the score plays through midi.c and the module only sets up: its row steps run
 * only while a list of instruments is set, and its notes start only for those instruments (music_set_midi; BINT's
 * speech and effects, docs/music.md "The module's instruments with a MIDI card"). CD audio: cdaudio.c. */
#ifndef BIING_MUSIC_H
#define BIING_MUSIC_H

#include "mixer.h"

/* The driver's global module transpose: added to every note's pitch index at the note start, after the control
 * transpose (docs/music.md, "Driver timing and mixing"). */
#define MUSIC_TRANSPOSE (-5)

typedef struct {
    const uint8_t *d;
    size_t n;
    char name[17];
    uint32_t pat, smp, song, ins; /* section starts (file offsets) */
    uint32_t pat_size, smp_size, song_size, ins_size;
    int tempo; /* BPM, header 0x22 */
    int rows; /* song rows (12 bytes each) */
} Module;

/* Checks the section sizes against the file length (docs/music.md, header: 0x40 + four sizes = file length). */
int module_parse(Module *m, const uint8_t *d, size_t n);

typedef struct {
    uint32_t ev; /* next event (file offset) in the voice's pattern; 0 = no pattern */
    uint32_t counter; /* rows until the next event */
    int ctl_transpose; /* set by the control pattern for this voice's following notes */
    /* the note */
    int instrument; /* -1 none */
    uint32_t prog; /* program start (file offset) */
    uint32_t prog_cmds;
    uint32_t pc;
    int wait; /* command 2 */
    int phase15; /* command 15 runs over three ticks */
    uint32_t loop_pc;
    int loop_var;
    int16_t var[256];
    int note_pitch; /* pitch index of the note: 12 x octave + semitone + control transpose + MUSIC_TRANSPOSE */
    int pitch; /* current pitch index (note + sample transpose + command 8) */
    uint32_t freq; /* frequency word (16.16 step) */
    int sample; /* selected sample */
    uint32_t start; /* data start of the selected sample (file offset) */
    uint32_t length; /* bytes played from the start */
    int volume; /* the note's volume byte as given (0-255); command 9 adds to it and keeps it in 0-64 */
    int env, env_phase, env_count; /* envelope 0-256; phase 0 attack, 1 decay, 2 hold; ticks left until the next step */
    int silenced; /* command 8 went below pitch 0 */
} MusicVoice;

typedef struct {
    Module m;
    int playing;
    int master; /* the song level's row (6 L >> 3) + 7: scales every voice's row (docs/sound.md "Level to volume") */
    int row;
    uint32_t acc; /* tempo accumulator */
    int sub; /* passes of 187 since the last row step; a row step on every 4th, the first on the song's 4th */
    uint32_t ctl_ev, ctl_counter;
    int ctl_held;
    MusicVoice v[MIXER_VOICES];
    int midi_card; /* a MIDI card plays the score: only the listed instruments sound (music_set_midi) */
    uint8_t only[64]; /* the byte list at [0x7066E]: the instruments that still play digitally */
    int only_n; /* 0: no row step at all */
    uint32_t row_steps; /* counters for tests and checks */
    uint32_t wraps; /* times the song went back to row 0 */
} Music;

/* Frequency word of a pitch index: the C4-B4 table 262 ... 494 shifted left by octave + 2 (docs/music.md). */
uint32_t music_freq(int pitch);
/* Starts the module from row 0. No row step yet: the first comes on the 4th pass of 187 (docs/music.md, "Song
 * section"). */
int music_start(Music *mu, const uint8_t *d, size_t n);
/* Stops the song and clears the four voices. */
void music_stop(Music *mu, Voice v[MIXER_VOICES]);
/* One 50 Hz tick: the sequencer (4 x tempo per tick, a row step every 4th pass of 187), then each voice's program
 * and envelope, then the voice records (region, step, row). The driver runs it after the tick's mix, so what it
 * writes is heard from the next tick's output (sound_interrupt). */
void music_tick(Music *mu, Voice v[MIXER_VOICES]);
/* A MIDI card plays the song (call after music_start): the row steps run only when `list` names instruments, and a
 * note starts only for an instrument in it (n up to 64; n = 0: the module stays silent and does not step). */
void music_set_midi(Music *mu, const uint8_t *list, int n);
/* Rows per second of a tempo: 50 x tempo / 187. */
double music_rows_per_second(int tempo);

#endif
