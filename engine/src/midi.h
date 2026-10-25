/* The MIDI part of the sound driver (docs/music.md, "MIDI output (SND-9)"): from an SCMF score and the music card it
 * produces exactly the bytes the original sends to its MPU-401, per 50 Hz timer interrupt - the program start-up, the
 * song start (all notes off, the tracks positioned at tick 0), every MIDI tick (note-offs, the SysEx of the device,
 * the 16 tracks, the loop), the song stop and the program end. Screen-free and device-free: the bytes go to the sinks
 * attached (a stream-file writer for tools/compare_midi.py, a message assembler for the host output in midi_host.c),
 * so the generator runs in tests and headless runs without any MIDI device.
 *
 * State that lives for the whole program run, as in the driver: the MIDI accumulator, each channel's current
 * instrument and last CC 7, the score buffers (PDAT and IDAT keep old bytes between songs, the DATA bytes too). */
#ifndef BIING_MIDI_H
#define BIING_MIDI_H

#include <stddef.h>
#include <stdint.h>

#define MIDI_GM 0 /* device 0: MCARD GM, instrument block 1, the GS SysEx */
#define MIDI_MT32 1 /* device 1: MCARD ROL, instrument block 2, the MT-32 SysEx */

/* the driver's buffers (docs/music.md, "Buffers") */
#define MIDI_DATA_SIZE 98
#define MIDI_TRAK_SIZE 20000
#define MIDI_PTAB_SIZE 8192
#define MIDI_PDAT_SIZE 65536
#define MIDI_ITAB_SIZE 1024
#define MIDI_IDAT_SIZE 65536
#define MIDI_TRACKS 16
#define MIDI_OFF_SLOTS 1024 /* note-off list slots: the driver's size is not in the spec (engine reading: large enough
                             * for every score; a note that finds no slot gets no note-off) */
#define MIDI_SINKS 4
#define MIDI_NONE 0xFFFFFFFFu

/* A receiver of the stream: every byte as it is sent, with the number of timer interrupts of the program run begun
 * when it was sent (0 = before the first) and whether it went to the MPU-401's command port. */
typedef void (*MidiSinkFn)(void *ctx, uint32_t interrupt, uint8_t byte, int command);

typedef struct {
    uint32_t entry; /* TRAK offset of the current entry (start tick, pattern), MIDI_NONE: no list */
    int running; /* a pattern runs */
    uint32_t hdr; /* PDAT offset of the running pattern's header */
    uint32_t ev; /* PDAT offset of its next event */
    uint32_t wait; /* MIDI ticks until that event */
} MidiTrack;

typedef struct {
    uint8_t track, note; /* note 0 = a free slot */
    uint16_t ticks;
} MidiOff;

typedef struct {
    int device; /* MIDI_GM or MIDI_MT32 */
    int running; /* a program run is set up (midi_begin) and not ended */
    int timer; /* the program's timer runs, so its interrupts are counted (RELO installs it at the music start) */
    uint32_t interrupts; /* timer interrupts begun in this program run */
    uint16_t acc; /* the MIDI accumulator [0x7048E]: 3 x tempo per interrupt, a MIDI tick per 187; never reset */
    uint8_t instrument[256]; /* per channel, [0x70854 + ch] */
    uint8_t volume[256]; /* per channel, the last CC 7 sent, [0x70844 + ch] */
    MidiOff off[MIDI_OFF_SLOTS];
    int noff; /* the list's length: the last used slot + 1 */
    int playing; /* a song runs ([0x704C4] = 0): the interrupts run MIDI ticks */
    uint32_t tick; /* the song's MIDI tick counter */
    int sysex_due;
    uint8_t data[MIDI_DATA_SIZE];
    uint8_t trak[MIDI_TRAK_SIZE], ptab[MIDI_PTAB_SIZE], pdat[MIDI_PDAT_SIZE], itab[MIDI_ITAB_SIZE],
        idat[MIDI_IDAT_SIZE];
    uint32_t list[MIDI_TRACKS]; /* TRAK offset of each track's list (after the k-th dword -1), MIDI_NONE: none */
    MidiTrack trk[MIDI_TRACKS];
    struct {
        MidiSinkFn fn;
        void *ctx;
    } sink[MIDI_SINKS];
    int nsinks;
    uint32_t sent; /* bytes sent in all (tests) */
} Midi;

/* Clears everything, the sinks included. */
void midi_init(Midi *m);
/* Attaches a receiver; returns -1 when all slots are taken. midi_remove_sink detaches it again. */
int midi_add_sink(Midi *m, MidiSinkFn fn, void *ctx);
void midi_remove_sink(Midi *m, MidiSinkFn fn, void *ctx);

/* The sound start-up of a program (0x71390): the run's state from 0, then command FF (reset), command 3F (UART mode),
 * C3 37 and Bn 5D 70 for n = 0 ... 15. timer: the program's timer interrupts run from now on (BIPRO, BINT); RELO
 * passes 0 and its timer starts with the song. */
void midi_begin(Midi *m, int device, int timer);
/* The program end (0x73452): Bn 7B 00 for n = 0 ... 15, then command FF; the run is over. */
void midi_end(Midi *m);

/* Checks an SCMF score: the header and the chunks DATA, TRAK, PTAB, PDAT, ITAB, IDAT in this order. 0 or -1. */
int midi_score_check(const uint8_t *d, size_t n);
/* A score's tempo (DATA word 0), or -1 if it is no score. */
int midi_score_tempo(const uint8_t *d, size_t n);
/* The song start (0x72773, the MIDI part): the score loaded into the buffers, tick 0, the SysEx due, all notes off
 * (the note-off list emptied unsent, Bn 7B 00 for n = 0 ... 15), every track positioned at tick 0. The timer runs from
 * now on. -1 (nothing sent) for a file that is no score: the original would hang in its loader. */
int midi_song_start(Midi *m, const uint8_t *d, size_t n);
/* The song stop (0x72969): Bn 7B 00 for n = 0 ... 15; the MIDI ticks stop. */
void midi_song_stop(Midi *m);
/* The song marked stopped without a device stop ([0x704C4] = -1 alone: the play call for the site-map song): no byte
 * is sent, the notes that sound go on sounding. */
void midi_halt(Midi *m);
/* The driver's tempo: the loaded score's DATA word 0. */
int midi_tempo(const Midi *m);
/* One 50 Hz timer interrupt (the MIDI part of the callback 0x72B99): counted while the timer runs; while a song runs
 * the accumulator gets 3 x tempo and each 187 run one MIDI tick. */
void midi_interrupt(Midi *m);
/* One MIDI tick (0x70D68): note-offs, the SysEx if due, the 16 track steps, the advance with the loop. */
void midi_tick(Midi *m);

/* ---- receivers ----------------------------------------------------------------------------- */

/* The stream file of tools/compare_midi.py (docs/music.md, "The stream file"): `#` comment lines, then `<n>: <bytes>`
 * per interrupt that sent anything, a command-port byte prefixed `!`. */
typedef struct {
    void *f; /* FILE * */
    uint32_t line; /* the interrupt of the open line */
    int open; /* a line is open */
} MidiLog;
int midi_log_open(MidiLog *l, const char *path, int append, const char *comment); /* 0 or -1 */
void midi_log_byte(void *log, uint32_t interrupt, uint8_t byte, int command); /* a MidiSinkFn */
void midi_log_close(MidiLog *l);

/* Whole MIDI messages for a host synthesizer: the data bytes assembled by their status byte (no running status
 * occurs), a SysEx from F0 to F7; the MPU-401 command bytes are dropped (they are the interface's, not MIDI). */
typedef void (*MidiMessageFn)(void *ctx, const uint8_t *msg, int len);
typedef struct {
    MidiMessageFn fn;
    void *ctx;
    uint8_t buf[256];
    int n, need; /* need: bytes of the message being assembled, 0 = none, -1 = a SysEx until F7 */
} MidiAssembler;
void midi_assembler_byte(void *assembler, uint32_t interrupt, uint8_t byte, int command); /* a MidiSinkFn */

#endif
