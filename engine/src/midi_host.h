/* The engine's MIDI output to the host (an engine choice, not the original's behaviour; docs/music.md, "What the
 * engine must change (SND-9)", item 5): the whole MIDI messages of the driver's stream (midi.h's MidiAssembler) go to
 *   - macOS: the built-in DLS synthesizer (AudioToolbox, a MusicDevice AudioUnit into the default output), or a
 *     CoreMIDI destination by name (an external synthesizer, Munt for the MT-32, an IAC bus);
 *   - Windows: the MIDI mapper (normally "Microsoft GS Wavetable Synth"), or a winmm output device by name;
 *   - other systems: nothing (a message says so; BIING_MIDI_LOG still writes the stream).
 * The production window build and host smoke test link this file with its platform's libraries;
 * the generator itself (midi.c) needs none, and headless game runs never open a device. */
#ifndef BIING_MIDI_HOST_H
#define BIING_MIDI_HOST_H

#include <stdint.h>

typedef struct MidiHost MidiHost;

/* Opens the output that `name` selects: NULL or "" = the built-in synthesizer; "none" = no output; any other text =
 * the first output whose name contains it (case ignored). Prints the outputs and returns NULL for "list"; returns NULL
 * with a message when nothing can be opened. */
MidiHost *midi_host_open(const char *name);
/* The output's name, for messages. */
const char *midi_host_name(const MidiHost *h);
/* Sends one whole message: a channel message of 2 or 3 bytes, or a SysEx from F0 to F7.
 * Windows retains a pending SysEx after the bounded wait and skips further SysEx messages
 * until that buffer can be unprepared; short messages remain available. */
void midi_host_send(MidiHost *h, const uint8_t *msg, int len);
/* A MidiMessageFn (midi.h) that sends to the MidiHost in ctx. */
void midi_host_message(void *ctx, const uint8_t *msg, int len);
/* Prints the available outputs to stdout, one per line; returns how many. */
int midi_host_list(void);
/* Close output. On Windows, failed driver cleanup retains the handle/buffer rather than
 * freeing storage the driver may still own; a later call may retry. */
void midi_host_close(MidiHost *h);

#endif
