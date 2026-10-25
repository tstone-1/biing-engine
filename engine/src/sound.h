/* Sound effects and the sound side of the songs: the sample loader, the play call, the four request queues with
 * their writers and draining, the busy sequences, the background slot with its fade, the ambience picks and the off
 * switches (docs/sound.md, "What an engine needs"). Screen-free and device-free: sound_interrupt renders one 50 Hz
 * tick of output into `out`, which the window build hands to SDL and the headless runs may write to a WAV file.
 *
 * Every function accepts a NULL Sound and then does nothing, so the simulation can call the event sounds whether or
 * not a game with sound runs it (biing-save has none). The routines are named by the original's program offsets. */
#ifndef BIING_SOUND_H
#define BIING_SOUND_H

#include "cdaudio.h"
#include "midi.h"
#include "mixer.h"
#include "music.h"
#include "program.h"

/* The program runs the MIDI driver serves (each program sets the driver up at its start and shuts it down at its end,
 * docs/music.md "Byte sequences of a program run"). */
enum { SOUND_PROG_NONE, SOUND_PROG_BIPRO, SOUND_PROG_RELO, SOUND_PROG_BINT };

#define SOUND_IDS 256
#define SOUND_QUEUES 4
#define SOUND_QUEUE_LEN 8
#define SOUND_SITE_MAP_SONG 0xDE

typedef struct {
    uint16_t id, period, volume; /* id 0 = empty */
} SoundEntry;

typedef uint32_t (*SoundRandom)(void *ctx, uint32_t n); /* uniform 0..n-1, the game's generator */
/* one draw AND mask (mask 2^k - 1), the game's generator; the ambience pick's 1-in-64 test is such a single draw, `t` AND
 * 63 (docs/sound.md, "Readings checked against the original (SND-11)") */
typedef uint32_t (*SoundBits)(void *ctx, uint32_t mask);

typedef struct Sound {
    char root[1024]; /* the BIING! directory */
    const Program *prog; /* BIPRO.EXE program image: the request lists and ambience tables (disk offsets) */
    SoundRandom random;
    SoundBits bits; /* NULL: random(mask + 1) */
    void *random_ctx;

    /* off switches and card (docs/saves.md, "BIING!.CFG and SOUND.CFG") */
    int effects_on; /* SOUND in BIING!.CFG ([0xAF82]) */
    int dcard; /* DCARD code ([0x70360]): 0 off, 1 SB, 2 SBP, 3 SB16, 6 ROL, 7 GM, 8 CD */
    int mcard; /* MCARD code */
    int stereo; /* DCARD SBP or SB16 and MCARD not SB: voices 0/3 left (even bytes), 1/2 right (odd) */

    /* samples: loaded once per id; status 0 not tried, 1 PCM, 2 music bank (BONG), -1 missing */
    int8_t *sample[SOUND_IDS];
    uint32_t sample_len[SOUND_IDS]; /* bytes played: the file size rounded down to an even number */
    size_t file_len[SOUND_IDS];
    int8_t status[SOUND_IDS];
    int voice_id[MIXER_VOICES]; /* the id last loaded into each voice's buffer (the driver's cache) */

    Voice voice[MIXER_VOICES];
    Music music;
    int song; /* id of the song that plays, 0 none */

    SoundEntry queue[SOUND_QUEUES][SOUND_QUEUE_LEN]; /* 0x5CCB, 0x5CFB, 0x5D2B, 0x5D5B */
    int sticky; /* [0x5CAE]: queues 1 and 2 advance in step, never cleared */
    int help_pending; /* [0x5CA7] */
    int race_pending; /* [0x5CA8]: 1 or 2, the announcement list */
    int race_busy; /* [0x5CA9] */
    int help_busy; /* [0x5CAB] */
    int ambience; /* [0x5CAD]: set first after 50 game ticks, then every 34 */
    int ambience_count; /* [0x5C89]: counts down to [0x5CAD], from 50, reloaded with 34 (not saved) */
    int pace; /* [0x5C86]: the background slot acts at most once per 4 game ticks while the clock runs */

    /* the background slot (0x4A0EA) */
    uint16_t bg_request; /* [0x5C93], 0 = none */
    uint16_t bg_period; /* [0x5C8F] */
    uint16_t bg_target; /* [0x5C9B], 0-63 */
    uint16_t bg_playing; /* [0x5C97] */
    int bg_level; /* [0x704D6] */

    /* output of the last sound_interrupt, interleaved stereo */
    int16_t out[2 * MIXER_MAX_TICK];
    int out_frames;
    uint32_t out_acc;
    /* optional capture of everything rendered (for WAV files) */
    int16_t *capture;
    size_t capture_frames, capture_cap;
    uint32_t started[SOUND_IDS]; /* play calls that started a voice or a song, per id (for tests and checks) */
    /* a stream mixed into every tick's output after the voices (the tutorial's sound, tutorial.h); NULL none */
    void (*stream)(void *ctx, int16_t *out, int n);
    void *stream_ctx;
    /* the game clock's tick credit [0xB0DE] (the Biing's credit), NULL none: every voice start sets it to 2, the
     * driver's delay after the start (docs/sound.md, "Voices and output": 0 written, then two interrupts polled) */
    int *tick_credit;

    /* CD audio music (docs/cdrom.md, "CD audio music in BIPRO.EXE"): MCARD CD and the game folder's CDAUDIO tracks.
     * Without the tracks the songs play as modules. */
    int cd_mode; /* MCARD CD with tracks: a song plays its CD track, its module is set up but silent */
    CdAudio cd;
    int cd_song; /* the track word: the track of the song that plays, 0 none (cleared by every stop) */
    int cd_level; /* SOUND.CFG MIXER, fourth character (a hex digit): the mixer's CD level 0-15 */
    /* the file loader's CD reads (rule 3 of the stop rules): a file CDC in the game folder, DATA/ONHD (bit i set =
     * resource i on the hard disk) and the resource the loader read last, which it keeps (-1 none) */
    int cdc;
    uint8_t onhd[400];
    int onhd_bits; /* 0: no DATA/ONHD */
    int last_resource;

    /* MIDI music (docs/music.md, "MIDI output (SND-9)"): MCARD GM = device 0, MCARD ROL = device 1. A song plays its
     * score (the music table's path) through the MIDI driver; its module is set up but plays no notes. */
    int midi_prog; /* the program run the driver serves (SOUND_PROG_*), SOUND_PROG_NONE: none set up */
    Midi midi;
    /* BIING_MIDI_LOG=<prefix>: each program run's stream as a stream file <prefix><bipro|relo|bint>.txt (the first run
     * of a program in this process starts the file, a later one appends after a `#` line) */
    char midi_log_prefix[1024];
    MidiLog midi_log;
    int midi_logged[4];
    /* the score of the next song started by the play call (relative to the game folder) instead of the music table's;
     * cleared by that start (biing-sound midi, tests) */
    const char *midi_score;
} Sound;

/* Reads BIING!.CFG and SOUND.CFG from root (missing files: sound on, DCARD SB, MCARD SB). */
void sound_init(Sound *s, const char *root, const Program *prog, SoundRandom random, void *ctx); /* prog may be NULL */
void sound_free(Sound *s);
/* The single masked draw (same ctx as random); without it the masked draws use random(mask + 1). */
void sound_set_bits(Sound *s, SoundBits bits);
/* The two readers of docs/saves.md: a line is identified by its first two characters, the value follows the
 * first '-', a later line overwrites an earlier one. BIING!.CFG: "off" if the value starts with N or n. */
int sound_cfg_flag(const char *text, size_t n, const char *key, int dflt);
/* SOUND.CFG card value by the 2nd and 3rd value characters: SB 1, SBP 2, SB1(6) 3, ROL 6, GM 7, CD 8, else 0. */
int sound_cfg_card(const char *text, size_t n, const char *key, int dflt);
/* All sound on (both switches): what the play call tests first. */
int sound_enabled(const Sound *s);

/* Sample id n = resource number 2800 + n, DATA/<S|T|U>/<b>/<c> (docs/sound.md, "Where the samples live").
 * Loads it if needed; returns its status (1 PCM, 2 music bank, -1 missing). */
int sound_load(Sound *s, int id);
/* Rate in Hz of a period: ((10,500,000 div P) >> 8) / 256 x 21,739.13. */
double sound_rate(int period);
/* Step word of a period: 10,500,000 div P (16.16). */
uint32_t sound_step(int period);
/* Volume row of a request volume: clamped to 0-63, then (6 v >> 3) + 7. */
int sound_row(int volume);

/* 0xDE46, the play call: id on voice v (0-3) at the period and volume, looping or once. */
void sound_play(Sound *s, int id, int v, int period, int volume, int loop);
/* 0x27895: the direct play of the program start and the title click (one-shot). */
void sound_direct(Sound *s, int id, int v, int period, int volume);

/* Writers (docs/sound.md, "Writers"). */
void sound_request(Sound *s, int id, int period, int volume); /* 0x278D1: the head of queues 1 and 2 */
void sound_list(Sound *s, int queue, uint32_t list); /* 0x1AFE7 (queue 0) .. 0x1AFCC: a 48-byte list from the image */
void sound_clear(Sound *s); /* 0x2FA07 */
void sound_help(Sound *s); /* [0x5CA7] = 1: the help jingle */
void sound_race(Sound *s, int value); /* [0x5CA8] = value (1 or 2): the race announcement */
void sound_ring(Sound *s); /* the office ring: (0xC6, 0x140, 63) into the head of queue 2, entry 2 cleared */
/* 0x2F9A6's sound part (a nurse show starts): clear, then one of the eight lists 0x48AA + 48 r into each of queues
 * 1 and 2; without a patient entry 2 of both is cleared. */
void sound_show(Sound *s, int with_patient);
/* 0x32336's sound: the invoice list 0x4A2A + 48 c into queues 1 and 2, c = the opinion change code 0-8. */
void sound_invoice(Sound *s, int c);

/* Ambience (docs/sound.md, "Ambience"): 0x36F3E (queue 0) / 0x36F5C (queue 1) with the table at that offset. */
void sound_ambience(Sound *s, int queue, uint32_t table);
void sound_ambience_pair(Sound *s, uint32_t table); /* "(1 and 2)": the pair with the same table */
/* The copy step alone (steps 4-5): the game-over screen fills all four queues this way every iteration. */
void sound_ambience_copy(Sound *s, int queue, uint32_t table);
/* 0x2004E, the show music: count = the room's nurse-show counter. */
void sound_show_music(Sound *s, int count);

/* Any busy flag set, [0x5CAB] included (the advice board's test), or the site-map song playing. */
int sound_board_blocked(const Sound *s);

/* The background slot: a request writes the three words; the slot fades it in (docs/sound.md). */
void sound_background(Sound *s, int id, int period, int level);
/* The background level (title click: 0 before the office entry). */
void sound_set_level(Sound *s, int level);
int sound_background_requested_or_playing(const Sound *s, int id);

/* Once per main-loop iteration, before the clock: 0x4A0EA (the background slot) and 0x49DF3 (draining).
 * clock_running: the game clock runs ([0x5CAC] = 0). */
void sound_main_loop(Sound *s, int clock_running);
void sound_background_step(Sound *s, int clock_running); /* 0x4A0EA */
void sound_drain(Sound *s); /* 0x49DF3 */
/* Once per game tick (the clock routine): [0x5C86] counts down, [0x5C89] sets [0x5CAD] first after 50 ticks of the
 * program, then every 34 (docs/mainloop.md, "Rest of the clock routine"). */
void sound_game_tick(Sound *s);
/* Once per 50 Hz timer interrupt: one tick of output into out / out_frames, then the music sequencer. */
void sound_interrupt(Sound *s);

/* CD audio (docs/cdrom.md): the loop check of every main-loop pass, at the tail of the GUI frame: with a track word set,
 * a song level above 0 becomes 1, and a track that has ended starts again from its beginning. */
void sound_cd_frame(Sound *s);
/* The start programs' CD tracks (docs/intro.md, "CD audio of the start programs", SND-13): in CD mode RELO plays
 * track 2, BINT tracks 3 and 4, each once from its start; the track word stays clear, so the main game's loop check
 * never restarts them, and a track that ends leaves silence. Nothing outside CD mode. Returns 1 in CD mode (the
 * program then starts no module), else 0. */
int sound_program_track(Sound *s, int track);
/* Stops a start program's track (immediately, no fade); a track of the game's songs (track word set) is left alone. */
void sound_program_track_stop(Sound *s);
/* The file loader reads resource n (an image drawn, a sample loaded into a voice): with CDC present and n's DATA/ONHD
 * bit clear it reads from the CD, which first stops the CD track for good (the track word is cleared). The resource
 * read last is kept by the loader and read again without the drive. */
void sound_resource_read(Sound *s, int resource);
/* SOUND.CFG MIXER: the value's character k (0 master, 1 voice, 2 line, 3 CD) as a hex digit, dflt when missing. */
int sound_cfg_mixer(const char *text, size_t n, int k, int dflt);
/* The CD track of a song id from the music table (0xB97E: module file number, score path, CD track), 0 none. */
int sound_song_track(const Sound *s, int id);

/* MIDI (docs/music.md, "MIDI output (SND-9)"). Nothing happens without a MIDI card (mcard ROL or GM). */
/* The MIDI device of a music card: GM (7) -> 0, ROL (6) -> 1 ((card - 6) xor 1), any other -> -1. */
int sound_midi_device(int mcard);
/* The program prog sets the driver up (its sound start-up), unless it already runs; another program's run is ended
 * first (song stop and program end). BIPRO and BINT run their timer from the start, RELO from its song. */
void sound_midi_program(Sound *s, int prog);
/* The MIDI song start with the score file rel (relative to the game folder, '/' separators): returns the score's
 * tempo, or -1 when there is no driver run or the file is missing or no score (nothing is sent then). */
int sound_midi_score(Sound *s, const char *rel);
/* The song stop (Bn 7B 00 x 16), if a program run is set up. */
void sound_midi_stop(Sound *s);
/* The end of the running program: song stop, then the program end (Bn 7B 00 x 16, command FF). */
void sound_midi_finish(Sound *s);
/* The program end alone (after the program's own song stop). */
void sound_midi_end(Sound *s);
/* A song id's score path from the music table (0xB97E), with '/' separators and in capitals: "DATA/M/CDREAM.SCT".
 * 0, or -1 without a program image or entry. */
int sound_song_score(const Sound *s, int id, char *out, size_t n);

/* WAV capture of everything sound_interrupt renders from now on. */
void sound_capture(Sound *s, int on);
int sound_write_wav(const char *path, const int16_t *frames, size_t n);

#endif
