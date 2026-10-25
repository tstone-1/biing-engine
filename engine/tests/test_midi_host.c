/* The host MIDI output (midi_host.h): the built-in synthesizer opens, takes a message that sounds nothing (all notes
 * off on every channel) and closes; "none" and an unknown name open nothing. Exit 77 (skipped) where the system has
 * no output to open (Linux, or a session without one). Nothing audible is sent. */
#include <stdio.h>

#include "midi_host.h"

int main(void) {
    int failures = 0;
    if (midi_host_open("none")) fprintf(stderr, "[FAIL] \"none\" opened an output\n"), failures++;
    if (midi_host_open("no output is called like this 7f3a")) fprintf(stderr, "[FAIL] an unknown name opened\n"), failures++;
    MidiHost *h = midi_host_open(NULL);
    if (!h) {
        printf("[SKIP] no MIDI output to open here\n");
        return failures ? 1 : 77;
    }
    if (!midi_host_name(h)[0]) fprintf(stderr, "[FAIL] the output has no name\n"), failures++;
    for (int ch = 0; ch < 16; ch++) {
        uint8_t off[3] = {(uint8_t)(0xB0 + ch), 0x7B, 0x00};
        midi_host_send(h, off, 3);
    }
    midi_host_close(h);
    if (failures) return 1;
    printf("[OK] midi_host: opened %s, closed\n", "the built-in synthesizer");
    return 0;
}
