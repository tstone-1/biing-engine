/* Exercise the production Windows backend with the real SDK types and controlled WinMM
 * calls. No MIDI device is opened. Delayed completion must never target expired storage. */
#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "[FAIL] line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static void *allocation;
static size_t allocation_size;
static MIDIHDR *prepared, *queued;
static int sleeps, complete_after, prepare_calls, long_calls, unprepare_calls, reset_calls, close_calls, frees;
static int fail_prepare, fail_send, fail_unprepare, fail_reset, fail_close;
static DWORD short_message;

static int owned(const void *p, size_t n) {
    uintptr_t begin = (uintptr_t)allocation, at = (uintptr_t)p;
    return allocation && at >= begin && n <= allocation_size && at - begin <= allocation_size - n;
}
static void *test_calloc(size_t n, size_t size) {
    CHECK(!allocation);
    allocation = calloc(n, size);
    CHECK(allocation);
    allocation_size = n * size;
    return allocation;
}
static void test_free(void *p) {
    CHECK(p == allocation && !prepared && !queued);
    free(p);
    allocation = NULL;
    frees++;
}
static void complete(void) {
    CHECK(queued);
    queued->dwFlags |= MHDR_DONE;
    queued = NULL;
}
static void test_sleep(DWORD ms) {
    CHECK(ms == 1);
    sleeps++;
    if (queued && complete_after > 0 && sleeps == complete_after) complete();
}
static MMRESULT test_open(LPHMIDIOUT out, UINT id, DWORD_PTR callback, DWORD_PTR instance, DWORD flags) {
    CHECK(id == MIDI_MAPPER && !callback && !instance && flags == CALLBACK_NULL);
    *out = (HMIDIOUT)(uintptr_t)1;
    return MMSYSERR_NOERROR;
}
static MMRESULT test_prepare(HMIDIOUT out, LPMIDIHDR hdr, UINT size) {
    CHECK(out && size == sizeof *hdr && !prepared);
    prepare_calls++;
    if (fail_prepare) return MMSYSERR_ERROR;
    prepared = hdr;
    hdr->dwFlags |= MHDR_PREPARED;
    return MMSYSERR_NOERROR;
}
static MMRESULT test_long(HMIDIOUT out, LPMIDIHDR hdr, UINT size) {
    CHECK(out && size == sizeof *hdr && hdr == prepared && !queued);
    long_calls++;
    if (fail_send) return MMSYSERR_ERROR;
    queued = hdr;
    if (complete_after == 0) complete();
    return MMSYSERR_NOERROR;
}
static MMRESULT test_unprepare(HMIDIOUT out, LPMIDIHDR hdr, UINT size) {
    CHECK(out && size == sizeof *hdr && hdr == prepared);
    unprepare_calls++;
    if (queued) return MIDIERR_STILLPLAYING;
    if (fail_unprepare) return MMSYSERR_ERROR;
    hdr->dwFlags &= ~MHDR_PREPARED;
    prepared = NULL;
    return MMSYSERR_NOERROR;
}
static MMRESULT test_reset(HMIDIOUT out) {
    CHECK(out);
    reset_calls++;
    if (fail_reset) return MMSYSERR_ERROR;
    if (queued) complete();
    return MMSYSERR_NOERROR;
}
static MMRESULT test_close(HMIDIOUT out) {
    CHECK(out && !prepared && !queued);
    close_calls++;
    return fail_close ? MMSYSERR_ERROR : MMSYSERR_NOERROR;
}
static MMRESULT test_short(HMIDIOUT out, DWORD msg) { CHECK(out); short_message = msg; return MMSYSERR_NOERROR; }

/* Include after the SDK headers so their imported declarations are not renamed. */
#define calloc test_calloc
#define free test_free
#define Sleep test_sleep
#define midiOutOpen test_open
#define midiOutPrepareHeader test_prepare
#define midiOutLongMsg test_long
#define midiOutUnprepareHeader test_unprepare
#define midiOutReset test_reset
#define midiOutClose test_close
#define midiOutShortMsg test_short
#include "../src/midi_host.c"

static MidiHost *begin(int after) {
    CHECK(!allocation && !prepared && !queued);
    sleeps = prepare_calls = long_calls = unprepare_calls = reset_calls = close_calls = frees = 0;
    fail_prepare = fail_send = fail_unprepare = fail_reset = fail_close = 0;
    complete_after = after;
    short_message = 0;
    MidiHost *h = midi_host_open(NULL);
    CHECK(h);
    return h;
}
static const uint8_t sysex[] = {0xF0, 0x7D, 1, 2, 0xF7}; /* synthetic noncommercial message */

int main(void) {
    MidiHost *h = begin(0);
    uint8_t note[] = {0x90, 60, 100}, oversized[257] = {0xF0};
    midi_host_send(h, note, 3);
    CHECK(short_message == 0x643c90 && !prepare_calls);
    midi_host_send(h, sysex, 0);
    midi_host_send(h, oversized, sizeof oversized);
    CHECK(!prepare_calls);
    midi_host_send(h, sysex, sizeof sysex);
    CHECK(long_calls == 1 && unprepare_calls == 1 && !prepared && !sleeps);
    midi_host_close(h);
    CHECK(frees == 1 && close_calls == 1);

    h = begin(3);
    midi_host_send(h, sysex, sizeof sysex);
    CHECK(sleeps == 3 && !prepared);
    midi_host_close(h);

    h = begin(-1);
    midi_host_send(h, sysex, sizeof sysex);
    CHECK(sleeps == 200 && queued && prepared);
    /* Check lifetime after send returned, before writing a delayed driver completion. */
    CHECK(owned(queued, sizeof *queued));
    CHECK(owned(queued->lpData, queued->dwBufferLength));
    CHECK(!unprepare_calls && !memcmp(queued->lpData, sysex, sizeof sysex));
    uint8_t replacement[] = {0xF0, 0x7D, 3, 4, 0xF7};
    midi_host_send(h, replacement, sizeof replacement);
    CHECK(long_calls == 1 && !memcmp(queued->lpData, sysex, sizeof sysex));
    complete();
    complete_after = 0;
    midi_host_send(h, replacement, sizeof replacement);
    CHECK(long_calls == 2 && unprepare_calls == 2 && !prepared);
    midi_host_close(h);

    h = begin(0);
    fail_prepare = 1;
    midi_host_send(h, sysex, sizeof sysex);
    CHECK(!long_calls && !unprepare_calls);
    midi_host_close(h);

    h = begin(0);
    fail_send = 1;
    midi_host_send(h, sysex, sizeof sysex);
    CHECK(long_calls == 1 && unprepare_calls == 1 && !prepared);
    midi_host_close(h);

    h = begin(-1);
    midi_host_send(h, sysex, sizeof sysex);
    fail_reset = 1;
    midi_host_close(h);
    CHECK(!frees && !close_calls && owned(queued, sizeof *queued));
    fail_reset = 0;
    midi_host_close(h);
    CHECK(frees == 1 && reset_calls == 2 && close_calls == 1);

    h = begin(0);
    fail_unprepare = 1;
    midi_host_send(h, sysex, sizeof sysex);
    CHECK(prepared && !queued);
    midi_host_send(h, replacement, sizeof replacement);
    CHECK(long_calls == 1); /* failed unprepare must not permit storage reuse */
    midi_host_close(h);
    CHECK(!frees && !close_calls);
    fail_unprepare = 0;
    midi_host_close(h);
    CHECK(frees == 1);

    h = begin(0);
    fail_close = 1;
    midi_host_close(h);
    CHECK(!frees);
    fail_close = 0;
    midi_host_close(h);
    CHECK(frees == 1);
    puts("[PASS] WinMM message lifetime, delayed completion, reuse and failure cleanup");
    return 0;
}
