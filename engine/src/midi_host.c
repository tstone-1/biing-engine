/* MIDI output to the host (midi_host.h). macOS: AudioToolbox DLS synth or CoreMIDI; Windows: winmm. */
#include "midi_host.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* whether `hay` contains `needle`, case ignored */
static inline int contains(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n) return 1;
    }
    return n == 0;
}

static inline int is_default(const char *name) { return !name || !name[0]; }

void midi_host_message(void *ctx, const uint8_t *msg, int len) {
    if (ctx) midi_host_send(ctx, msg, len);
}

#if defined(__APPLE__)

/* MIDIPacketList is deprecated in favour of the MIDI 2.0 event lists, and still the plain way to send MIDI 1.0 */
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <AudioToolbox/AudioToolbox.h>
#include <CoreMIDI/CoreMIDI.h>

struct MidiHost {
    char name[256];
    AudioUnit synth, output; /* the DLS synth into the default output */
    MIDIClientRef client;
    MIDIPortRef port;
    MIDIEndpointRef dest; /* a CoreMIDI destination; 0 = the DLS synth */
};

static void endpoint_name(MIDIEndpointRef e, char *out, size_t n) {
    CFStringRef s = NULL;
    out[0] = 0;
    if (MIDIObjectGetStringProperty(e, kMIDIPropertyDisplayName, &s) == noErr && s) {
        CFStringGetCString(s, out, (CFIndex)n, kCFStringEncodingUTF8);
        CFRelease(s);
    }
}

int midi_host_list(void) {
    printf("built-in synthesizer (Apple DLS Synth; the default)\n");
    ItemCount n = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < n; i++) {
        char nm[256];
        endpoint_name(MIDIGetDestination(i), nm, sizeof nm);
        printf("%s\n", nm);
    }
    return (int)n + 1;
}

static MidiHost *open_dls(void) {
    MidiHost *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    AudioComponentDescription sd = {kAudioUnitType_MusicDevice, kAudioUnitSubType_DLSSynth,
                                    kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponentDescription od = {kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput,
                                    kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent sc = AudioComponentFindNext(NULL, &sd), oc = AudioComponentFindNext(NULL, &od);
    if (!sc || !oc || AudioComponentInstanceNew(sc, &h->synth) || AudioComponentInstanceNew(oc, &h->output)) goto fail;
    AudioStreamBasicDescription fmt;
    UInt32 size = sizeof fmt;
    if (AudioUnitGetProperty(h->synth, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fmt, &size) ==
        noErr)
        AudioUnitSetProperty(h->output, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &fmt, sizeof fmt);
    AudioUnitConnection conn = {h->synth, 0, 0};
    if (AudioUnitSetProperty(h->output, kAudioUnitProperty_MakeConnection, kAudioUnitScope_Input, 0, &conn,
                             sizeof conn) ||
        AudioUnitInitialize(h->synth) || AudioUnitInitialize(h->output) || AudioOutputUnitStart(h->output))
        goto fail;
    snprintf(h->name, sizeof h->name, "built-in synthesizer (Apple DLS Synth)");
    return h;
fail:
    midi_host_close(h);
    return NULL;
}

static MidiHost *open_coremidi(const char *want) {
    ItemCount n = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < n; i++) {
        MIDIEndpointRef e = MIDIGetDestination(i);
        char nm[256];
        endpoint_name(e, nm, sizeof nm);
        if (!contains(nm, want)) continue;
        MidiHost *h = calloc(1, sizeof *h);
        if (!h) return NULL;
        CFStringRef cn = CFStringCreateWithCString(NULL, "Biing!", kCFStringEncodingUTF8);
        CFStringRef pn = CFStringCreateWithCString(NULL, "music", kCFStringEncodingUTF8);
        OSStatus st = MIDIClientCreate(cn, NULL, NULL, &h->client);
        if (st == noErr) st = MIDIOutputPortCreate(h->client, pn, &h->port);
        CFRelease(cn);
        CFRelease(pn);
        if (st != noErr) {
            fprintf(stderr, "[WARN] MIDI: cannot open %s (CoreMIDI error %d)\n", nm, (int)st);
            midi_host_close(h);
            return NULL;
        }
        h->dest = e;
        snprintf(h->name, sizeof h->name, "%s", nm);
        return h;
    }
    fprintf(stderr, "[WARN] MIDI: no output named like \"%s\" (BIING_MIDI_OUT=list shows them); no MIDI music\n", want);
    return NULL;
}

MidiHost *midi_host_open(const char *name) {
    if (name && !strcmp(name, "none")) return NULL;
    if (name && !strcmp(name, "list")) {
        midi_host_list();
        return NULL;
    }
    if (!is_default(name)) return open_coremidi(name);
    MidiHost *h = open_dls();
    if (!h) fprintf(stderr, "[WARN] MIDI: the built-in synthesizer did not start; no MIDI music\n");
    return h;
}

void midi_host_send(MidiHost *h, const uint8_t *msg, int len) {
    if (!h || len <= 0) return;
    if (h->dest) {
        Byte buf[512];
        MIDIPacketList *pl = (MIDIPacketList *)buf;
        MIDIPacket *p = MIDIPacketListInit(pl);
        p = MIDIPacketListAdd(pl, sizeof buf, p, 0, (ByteCount)len, msg);
        if (p) MIDISend(h->port, h->dest, pl);
        return;
    }
    if (msg[0] == 0xF0) MusicDeviceSysEx(h->synth, msg, (UInt32)len);
    else MusicDeviceMIDIEvent(h->synth, msg[0], len > 1 ? msg[1] : 0, len > 2 ? msg[2] : 0, 0);
}

void midi_host_close(MidiHost *h) {
    if (!h) return;
    if (h->output) {
        AudioOutputUnitStop(h->output);
        AudioUnitUninitialize(h->output);
        AudioComponentInstanceDispose(h->output);
    }
    if (h->synth) {
        AudioUnitUninitialize(h->synth);
        AudioComponentInstanceDispose(h->synth);
    }
    if (h->port) MIDIPortDispose(h->port);
    if (h->client) MIDIClientDispose(h->client);
    free(h);
}

#elif defined(_WIN32)

#include <windows.h>
#include <mmsystem.h>

struct MidiHost {
    char name[256];
    HMIDIOUT out;
    MIDIHDR sysex;
    char sysex_data[256];
    int sysex_prepared, sysex_queued;
};

/* WinMM may still own a SysEx buffer when the bounded send wait expires. Keep both the
 * header and bytes with the handle until the driver returns them and unprepare succeeds. */
static int release_sysex(MidiHost *h) {
    if (!h->sysex_prepared) return 1;
    if (h->sysex_queued && !(h->sysex.dwFlags & MHDR_DONE)) return 0;
    if (midiOutUnprepareHeader(h->out, &h->sysex, (UINT)sizeof h->sysex) != MMSYSERR_NOERROR) return 0;
    h->sysex_prepared = h->sysex_queued = 0;
    return 1;
}

int midi_host_list(void) {
    printf("MIDI mapper (normally Microsoft GS Wavetable Synth; the default)\n");
    UINT n = midiOutGetNumDevs();
    for (UINT i = 0; i < n; i++) {
        MIDIOUTCAPSA caps;
        if (midiOutGetDevCapsA(i, &caps, (UINT)sizeof caps) == MMSYSERR_NOERROR) printf("%s\n", caps.szPname);
    }
    return (int)n + 1;
}

MidiHost *midi_host_open(const char *name) {
    if (name && !strcmp(name, "none")) return NULL;
    if (name && !strcmp(name, "list")) {
        midi_host_list();
        return NULL;
    }
    UINT id = MIDI_MAPPER;
    char nm[256];
    snprintf(nm, sizeof nm, "MIDI mapper");
    if (!is_default(name)) {
        UINT n = midiOutGetNumDevs(), i;
        for (i = 0; i < n; i++) {
            MIDIOUTCAPSA caps;
            if (midiOutGetDevCapsA(i, &caps, (UINT)sizeof caps) == MMSYSERR_NOERROR && contains(caps.szPname, name)) {
                snprintf(nm, sizeof nm, "%s", caps.szPname);
                break;
            }
        }
        if (i == n) {
            fprintf(stderr, "[WARN] MIDI: no output named like \"%s\" (BIING_MIDI_OUT=list shows them); no MIDI music\n",
                    name);
            return NULL;
        }
        id = i;
    }
    MidiHost *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    MMRESULT r = midiOutOpen(&h->out, id, 0, 0, CALLBACK_NULL);
    if (r != MMSYSERR_NOERROR) {
        fprintf(stderr, "[WARN] MIDI: cannot open %s (winmm error %u); no MIDI music\n", nm, (unsigned)r);
        free(h);
        return NULL;
    }
    snprintf(h->name, sizeof h->name, "%s", nm);
    return h;
}

void midi_host_send(MidiHost *h, const uint8_t *msg, int len) {
    if (!h || len <= 0) return;
    if (msg[0] != 0xF0) {
        DWORD m = msg[0] | (DWORD)(len > 1 ? msg[1] : 0) << 8 | (DWORD)(len > 2 ? msg[2] : 0) << 16;
        midiOutShortMsg(h->out, m);
        return;
    }
    if (len > (int)sizeof h->sysex_data || !release_sysex(h)) return;
    memcpy(h->sysex_data, msg, (size_t)len);
    memset(&h->sysex, 0, sizeof h->sysex);
    h->sysex.lpData = h->sysex_data;
    h->sysex.dwBufferLength = (DWORD)len;
    if (midiOutPrepareHeader(h->out, &h->sysex, (UINT)sizeof h->sysex) != MMSYSERR_NOERROR) return;
    h->sysex_prepared = 1;
    h->sysex_queued = midiOutLongMsg(h->out, &h->sysex, (UINT)sizeof h->sysex) == MMSYSERR_NOERROR;
    if (h->sysex_queued)
        for (int i = 0; i < 200 && !(h->sysex.dwFlags & MHDR_DONE); i++) Sleep(1);
    release_sysex(h); /* pending or failed cleanup keeps the storage owned; do not reuse it */
}

void midi_host_close(MidiHost *h) {
    if (!h) return;
    midiOutReset(h->out);
    if (!release_sysex(h) || midiOutClose(h->out) != MMSYSERR_NOERROR) {
        /* A failed driver cleanup must not turn into a write through expired storage.
         * Retain the handle and buffer; a later close can retry, otherwise the OS reclaims them at exit. */
        fprintf(stderr, "[WARN] MIDI: driver cleanup failed; retaining the output handle and its buffer\n");
        return;
    }
    free(h);
}

#else

struct MidiHost {
    char name[8];
};

int midi_host_list(void) {
    printf("(no MIDI output on this system)\n");
    return 0;
}

MidiHost *midi_host_open(const char *name) {
    if (name && (!strcmp(name, "none") || !strcmp(name, "list"))) {
        if (!strcmp(name, "list")) midi_host_list();
        return NULL;
    }
    fprintf(stderr, "[WARN] MIDI: the engine has no MIDI output on this system; no MIDI music (BIING_MIDI_LOG writes "
                    "the stream)\n");
    return NULL;
}

void midi_host_send(MidiHost *h, const uint8_t *msg, int len) {
    (void)h, (void)msg, (void)len;
}

void midi_host_close(MidiHost *h) { free(h); }

#endif

const char *midi_host_name(const MidiHost *h) { return h ? h->name : ""; }
