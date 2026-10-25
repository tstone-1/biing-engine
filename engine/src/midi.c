/* The driver's MIDI stream, written from docs/music.md, "MIDI output (SND-9)". Readings the spec leaves open are
 * marked "engine reading" (docs/engine.md, "SND-9: MIDI music"). */
#include "midi.h"

#include <stdio.h>
#include <string.h>

/* ---- bounded reads of the buffers (the driver reads unchecked; outside a buffer the engine reads 0, and the event
 * byte 0xFF, so a broken pattern ends) ----------------------------------------------------------------------------- */

static uint32_t rd32(const uint8_t *b, size_t size, uint32_t off) {
    if (off >= size || size - off < 4) return 0;
    return (uint32_t)b[off] | (uint32_t)b[off + 1] << 8 | (uint32_t)b[off + 2] << 16 | (uint32_t)b[off + 3] << 24;
}
static uint8_t pb(const Midi *m, uint32_t off) { return off < MIDI_PDAT_SIZE ? m->pdat[off] : 0xFF; }
static uint16_t pw(const Midi *m, uint32_t off) {
    return off < MIDI_PDAT_SIZE - 1 ? (uint16_t)(m->pdat[off] | m->pdat[off + 1] << 8) : 0;
}
static uint8_t ib(const Midi *m, uint32_t off) { return off < MIDI_IDAT_SIZE ? m->idat[off] : 0; }
static uint16_t iw(const Midi *m, uint32_t off) {
    return off < MIDI_IDAT_SIZE - 1 ? (uint16_t)(m->idat[off] | m->idat[off + 1] << 8) : 0;
}

/* the track's channel: DATA byte 10 + track, added to a status byte unmasked */
static uint8_t chan(const Midi *m, int track) { return m->data[10 + track]; }

/* ---- output ----------------------------------------------------------------------------------------------------- */

static void emit(Midi *m, uint8_t b, int command) {
    if (!m->running) return;
    for (int i = 0; i < m->nsinks; i++) m->sink[i].fn(m->sink[i].ctx, m->interrupts, b, command);
    m->sent++;
}
static void emit2(Midi *m, uint8_t a, uint8_t b) { emit(m, a, 0), emit(m, b, 0); }
static void emit3(Midi *m, uint8_t a, uint8_t b, uint8_t c) { emit(m, a, 0), emit(m, b, 0), emit(m, c, 0); }

static void all_notes_off(Midi *m) {
    for (int n = 0; n < 16; n++) emit3(m, (uint8_t)(0xB0 + n), 0x7B, 0x00);
}

void midi_init(Midi *m) { memset(m, 0, sizeof *m); }

int midi_add_sink(Midi *m, MidiSinkFn fn, void *ctx) {
    if (m->nsinks >= MIDI_SINKS) return -1;
    m->sink[m->nsinks].fn = fn;
    m->sink[m->nsinks].ctx = ctx;
    m->nsinks++;
    return 0;
}

void midi_remove_sink(Midi *m, MidiSinkFn fn, void *ctx) {
    for (int i = 0; i < m->nsinks; i++)
        if (m->sink[i].fn == fn && m->sink[i].ctx == ctx) {
            memmove(&m->sink[i], &m->sink[i + 1], sizeof m->sink[0] * (size_t)(m->nsinks - i - 1));
            m->nsinks--;
            return;
        }
}

/* ---- program start and end -------------------------------------------------------------------------------------- */

void midi_begin(Midi *m, int device, int timer) {
    /* the run's state from 0; the buffers are allocated fresh at start-up (0x70CF1): zero (engine reading) */
    m->device = device;
    m->running = 1;
    m->timer = timer;
    m->interrupts = 0;
    m->acc = 0;
    memset(m->instrument, 0, sizeof m->instrument);
    memset(m->volume, 0, sizeof m->volume);
    memset(m->off, 0, sizeof m->off);
    m->noff = 0;
    m->playing = 0;
    m->tick = 0;
    m->sysex_due = 0;
    memset(m->data, 0, sizeof m->data);
    memset(m->trak, 0, sizeof m->trak);
    memset(m->ptab, 0, sizeof m->ptab);
    memset(m->pdat, 0, sizeof m->pdat);
    memset(m->itab, 0, sizeof m->itab);
    memset(m->idat, 0, sizeof m->idat);
    for (int k = 0; k < MIDI_TRACKS; k++) m->list[k] = MIDI_NONE, m->trk[k].entry = MIDI_NONE, m->trk[k].running = 0;
    /* 0x71390: reset (the MPU acknowledges it, so FF once), UART mode, then the data bytes */
    emit(m, 0xFF, 1);
    emit(m, 0x3F, 1);
    emit2(m, 0xC3, 0x37); /* program change on channel 3 to 55, as written */
    for (int n = 0; n < 16; n++) emit3(m, (uint8_t)(0xB0 + n), 0x5D, 0x70);
}

void midi_end(Midi *m) {
    if (!m->running) return;
    all_notes_off(m);
    emit(m, 0xFF, 1); /* reset, no acknowledge awaited */
    m->running = 0;
    m->playing = 0;
}

/* ---- the score ---------------------------------------------------------------------------------------------------- */

static const char CHUNKS[6][5] = {"DATA", "TRAK", "PTAB", "PDAT", "ITAB", "IDAT"};

/* the start and length of each of the six chunks; -1 if the file is no score */
static int chunks(const uint8_t *d, size_t n, uint32_t at[6], uint32_t len[6]) {
    if (!d || n < 12 || memcmp(d, "SCMF", 4)) return -1;
    size_t o = 12; /* SCMF, LE u32 4, LE u32 5 */
    for (int c = 0; c < 6; c++) {
        if (n - o < 8 || memcmp(d + o, CHUNKS[c], 4)) return -1; /* the loader would hang on a wrong tag */
        uint32_t l = rd32(d, n, (uint32_t)o + 4);
        if (l > n - o - 8) return -1;
        at[c] = (uint32_t)o + 8;
        len[c] = l;
        o += 8 + (size_t)l;
    }
    return 0;
}

int midi_score_check(const uint8_t *d, size_t n) {
    uint32_t at[6], len[6];
    return chunks(d, n, at, len);
}

int midi_score_tempo(const uint8_t *d, size_t n) {
    uint32_t at[6], len[6];
    if (chunks(d, n, at, len) || len[0] < 2) return -1;
    return d[at[0]] | d[at[0] + 1] << 8;
}

int midi_tempo(const Midi *m) { return m->data[0] | m->data[1] << 8; }

static void copy_chunk(uint8_t *buf, size_t size, const uint8_t *src, uint32_t len) {
    memcpy(buf, src, len < size ? len : size); /* from the buffer's start; a shorter chunk keeps the rest */
}

/* Track k's list: after the k-th dword -1 of TRAK, counting dwords from the start (track 0 at the start). */
static uint32_t list_start(const Midi *m, int k) {
    if (k == 0) return 0;
    int found = 0;
    for (uint32_t o = 0; o + 4 <= MIDI_TRAK_SIZE; o += 4)
        if (rd32(m->trak, MIDI_TRAK_SIZE, o) == 0xFFFFFFFFu && ++found == k) return o + 4;
    return MIDI_NONE;
}

static int entry_end(const Midi *m, uint32_t e) {
    return e == MIDI_NONE || e > MIDI_TRAK_SIZE - 8 || rd32(m->trak, MIDI_TRAK_SIZE, e + 4) == 0xFFFFFFFFu;
}
static uint32_t entry_start(const Midi *m, uint32_t e) { return rd32(m->trak, MIDI_TRAK_SIZE, e); }

static void load(Midi *m, const uint8_t *d, size_t n) {
    uint32_t at[6], len[6];
    chunks(d, n, at, len);
    memset(m->trak, 0, 16 * 8);
    for (uint32_t k = 0; k < 16; k++) { /* 16 empty lists (0x2EE00, -1) */
        uint8_t *e = m->trak + 8 * k;
        e[0] = 0x00, e[1] = 0xEE, e[2] = 0x02, e[3] = 0x00;
        e[4] = e[5] = e[6] = e[7] = 0xFF;
    }
    memset(m->ptab, 0, sizeof m->ptab);
    memset(m->itab, 0, sizeof m->itab);
    copy_chunk(m->data, sizeof m->data, d + at[0], len[0]);
    copy_chunk(m->trak, sizeof m->trak, d + at[1], len[1]);
    copy_chunk(m->ptab, sizeof m->ptab, d + at[2], len[2]);
    copy_chunk(m->pdat, sizeof m->pdat, d + at[3], len[3]);
    copy_chunk(m->itab, sizeof m->itab, d + at[4], len[4]);
    copy_chunk(m->idat, sizeof m->idat, d + at[5], len[5]);
    for (int k = 0; k < MIDI_TRACKS; k++) m->list[k] = list_start(m, k);
}

/* the block of instrument i for the device: IDAT + r + the word at IDAT + r + 4 + 2 x device, r = ITAB[i] */
static uint32_t block(const Midi *m, uint8_t i) {
    uint32_t r = rd32(m->itab, MIDI_ITAB_SIZE, 4u * i);
    return r + iw(m, r + 4 + 2u * (uint32_t)m->device);
}

/* 0x71034: bank, program, pan, reverb, chorus; no volume */
static void program_change(Midi *m, int track, uint8_t i) {
    uint8_t ch = chan(m, track);
    m->instrument[ch] = i;
    uint32_t b = block(m, i);
    uint8_t cc = (uint8_t)(0xB0 + ch);
    emit3(m, cc, 0x00, ib(m, b + 1));
    emit2(m, (uint8_t)(0xC0 + ch), ib(m, b + 0));
    emit3(m, cc, 0x0A, ib(m, b + 6));
    emit3(m, cc, 0x5B, ib(m, b + 7));
    emit3(m, cc, 0x5D, ib(m, b + 8));
}

/* 0x70FA0: the instrument's volume + the global volume + the pattern's offset, 0-127, sent only when it changed */
static void volume(Midi *m, int track, uint8_t offset) {
    uint8_t ch = chan(m, track);
    int v = (int8_t)ib(m, block(m, m->instrument[ch]) + 4) + m->data[0x4B] + (int8_t)offset;
    v = v < 0 ? 0 : v > 127 ? 127 : v;
    if (v == m->volume[ch]) return;
    m->volume[ch] = (uint8_t)v;
    emit3(m, (uint8_t)(0xB0 + ch), 0x07, (uint8_t)v);
}

/* a delta time: 1 byte, or 2 when bit 7 of the first is set: (b0 & 0x7F) << 7 + b1 */
static uint32_t delta(const Midi *m, uint32_t *ev) {
    uint8_t b0 = pb(m, (*ev)++);
    if (!(b0 & 0x80)) return b0;
    return (uint32_t)(b0 & 0x7F) << 7 | pb(m, (*ev)++);
}

/* Opens the pattern of entry e: header and first event offsets. */
static void open_pattern(const Midi *m, uint32_t e, uint32_t *hdr, uint32_t *ev) {
    uint32_t index = rd32(m->trak, MIDI_TRAK_SIZE, e + 4);
    uint32_t pat = index < MIDI_PTAB_SIZE / 4 ? rd32(m->ptab, MIDI_PTAB_SIZE, 4 * index) : 0;
    *hdr = pat + pw(m, pat + 4);
    *ev = pat + pw(m, pat + 6);
}

/* the pattern start's sends: a program change if header +6 is not 0, then the volume with header +4 */
static void pattern_sends(Midi *m, int k) {
    const MidiTrack *t = &m->trk[k];
    if (pb(m, t->hdr + 6)) program_change(m, k, pb(m, t->hdr + 6));
    volume(m, k, pb(m, t->hdr + 4));
}

/* Positioning a track at the tick (0x7147D, 0x714B7): the last entry starting at or before it (else the first); its
 * pattern opened and its events skipped up to the tick, nothing sent; a pattern found running sends its program
 * change and volume. */
static void position(Midi *m, int k, uint32_t tick) {
    MidiTrack *t = &m->trk[k];
    t->running = 0;
    t->wait = 0;
    uint32_t e = m->list[k];
    t->entry = e;
    if (entry_end(m, e)) return; /* an empty list: the track is idle */
    while (!entry_end(m, e + 8) && entry_start(m, e + 8) <= tick) e += 8;
    t->entry = e;
    uint32_t start = entry_start(m, e);
    if (tick < start) return; /* it starts in its step */
    uint32_t hdr, ev;
    open_pattern(m, e, &hdr, &ev);
    int64_t rem = (int64_t)tick - start - delta(m, &ev);
    for (int guard = 0; rem > 0 && guard < MIDI_PDAT_SIZE; guard++) {
        uint8_t b = pb(m, ev);
        if (b == 0xFF) { /* the pattern ended before the tick: the next entry, nothing running, nothing sent */
            t->entry = e + 8;
            return;
        }
        ev += b < 0x80 ? 4 : b == 0xC0 ? 2 : 3; /* 0x00 takes 4 bytes here */
        rem -= delta(m, &ev);
    }
    t->running = 1;
    t->hdr = hdr;
    t->ev = ev;
    t->wait = rem < 0 ? (uint32_t)-rem : 0;
    pattern_sends(m, k);
}

static void add_off(Midi *m, int track, uint8_t note, uint16_t ticks) {
    MidiOff o = {(uint8_t)track, note, ticks};
    for (int i = 0; i < m->noff; i++)
        if (!m->off[i].note) {
            m->off[i] = o;
            return;
        }
    if (m->noff < MIDI_OFF_SLOTS) m->off[m->noff++] = o;
}

static void note(Midi *m, int k, uint32_t ev, int itr) {
    const MidiTrack *t = &m->trk[k];
    uint8_t ch = chan(m, k), n = pb(m, ev), v = pb(m, ev + 1);
    uint16_t ticks = (uint16_t)(pb(m, ev + 2) | pb(m, ev + 3) << 8);
    int8_t vel_offset = (int8_t)pb(m, t->hdr + 3);
    uint8_t sent = ch == 9 ? n : (uint8_t)(n + (int8_t)pb(m, t->hdr + 2) + (int8_t)m->data[0x4A] + itr);
    uint8_t mute = pb(m, t->hdr + 5);
    if (mute & 0x80 || mute & (1u << m->device)) return;
    uint8_t vel = (uint8_t)(v + vel_offset);
    if (vel > 0x7F) vel = vel_offset < 0 ? 0x00 : 0x7F;
    emit3(m, (uint8_t)(0x90 + ch), sent, vel);
    add_off(m, k, sent, ticks);
}

/* The step of a track (0x70DA9). */
static void step(Midi *m, int k) {
    MidiTrack *t = &m->trk[k];
    for (int guard = 0; guard < 4096; guard++) {
        /* 1. the entry; with no running pattern it starts when its start tick is this tick */
        if (entry_end(m, t->entry)) return;
        if (!t->running) {
            if (entry_start(m, t->entry) != m->tick) return;
            open_pattern(m, t->entry, &t->hdr, &t->ev);
            t->running = 1;
            pattern_sends(m, k);
            t->wait = delta(m, &t->ev);
        }
        /* 2. the instrument transpose of this step: the channel's instrument as it is now */
        int itr = (int8_t)ib(m, block(m, m->instrument[chan(m, k)]) + 2);
        /* 3. the events */
        int ended = 0;
        for (int g = 0; g < MIDI_PDAT_SIZE && !ended; g++) {
            if (t->wait) {
                t->wait--;
                return;
            }
            uint8_t b = pb(m, t->ev);
            if (b == 0xFF) { /* the next entry; the step begins again in the same tick */
                t->entry += 8;
                t->running = 0;
                ended = 1;
                break;
            }
            if (b == 0x00) t->ev += 2;
            else if (b == 0xC0) program_change(m, k, pb(m, t->ev + 1)), t->ev += 2;
            else if (b >= 0x80) t->ev += 3; /* controllers and pitch bends: read, never sent */
            else note(m, k, t->ev, itr), t->ev += 4;
            t->wait = delta(m, &t->ev);
        }
        if (!ended) return;
    }
}

/* the Roland DT1 messages, device id 10, checksum (128 - (sum of address and data) mod 128) mod 128 */
static void roland(Midi *m, uint8_t model, uint8_t a0, uint8_t a1, uint8_t a2, const uint8_t *d, int n) {
    unsigned sum = (unsigned)a0 + a1 + a2;
    emit(m, 0xF0, 0), emit(m, 0x41, 0), emit(m, 0x10, 0), emit(m, model, 0), emit(m, 0x12, 0);
    emit(m, a0, 0), emit(m, a1, 0), emit(m, a2, 0);
    for (int i = 0; i < n; i++) emit(m, d[i], 0), sum += d[i];
    emit(m, (uint8_t)((128 - sum % 128) % 128), 0);
    emit(m, 0xF7, 0);
}

static void sysex(Midi *m) { /* 0x7118F */
    if (m->device == MIDI_GM) {
        roland(m, 0x42, 0x40, 0x01, 0x30, m->data + 0x5B, 1); /* reverb macro */
        roland(m, 0x42, 0x40, 0x01, 0x38, m->data + 0x5C, 1); /* chorus macro */
        roland(m, 0x42, 0x40, 0x00, 0x04, m->data + 0x5A, 1); /* master volume */
    } else {
        roland(m, 0x16, 0x10, 0x00, 0x00, m->data + 0x5D, 4); /* master tune, reverb mode, time, level */
        roland(m, 0x16, 0x10, 0x00, 0x16, m->data + 0x61, 1); /* master volume */
    }
}

void midi_tick(Midi *m) {
    /* 1. note-offs: a slot's ticks count down; at 0 (or 0 already) its note-off goes out and the slot is freed */
    int last = -1;
    for (int i = 0; i < m->noff; i++) {
        MidiOff *o = &m->off[i];
        if (!o->note) continue;
        if (o->ticks && --o->ticks) {
            last = i;
            continue;
        }
        emit3(m, (uint8_t)(0x80 + chan(m, o->track)), o->note, 0x00);
        o->note = 0;
    }
    m->noff = last + 1;
    /* 2. the SysEx in the song's first tick */
    if (m->sysex_due) {
        m->sysex_due = 0;
        sysex(m);
    }
    /* 3. the tracks */
    for (int k = 0; k < MIDI_TRACKS; k++) step(m, k);
    /* 4. the advance; at the loop end (unsigned compare) the loop start, every track positioned there */
    m->tick++;
    if (m->tick >= rd32(m->data, sizeof m->data, 0x2E)) {
        m->tick = rd32(m->data, sizeof m->data, 0x26);
        for (int k = 0; k < MIDI_TRACKS; k++) position(m, k, m->tick);
    }
}

/* ---- songs -------------------------------------------------------------------------------------------------------- */

int midi_song_start(Midi *m, const uint8_t *d, size_t n) {
    if (!m->running || midi_score_check(d, n)) return -1;
    m->tick = 0;
    load(m, d, n);
    m->sysex_due = 1;
    memset(m->off, 0, sizeof m->off); /* emptied without sending anything */
    m->noff = 0;
    all_notes_off(m);
    for (int k = 0; k < MIDI_TRACKS; k++) position(m, k, 0);
    m->playing = 1;
    m->timer = 1;
    return 0;
}

void midi_song_stop(Midi *m) {
    all_notes_off(m);
    m->playing = 0;
}

void midi_halt(Midi *m) { m->playing = 0; }

void midi_interrupt(Midi *m) {
    if (!m->running) return;
    if (m->timer) m->interrupts++;
    if (!m->playing) return;
    m->acc = (uint16_t)(m->acc + 3 * midi_tempo(m));
    while (m->acc >= 187 && m->playing) {
        m->acc = (uint16_t)(m->acc - 187);
        midi_tick(m);
    }
}

/* ---- receivers ---------------------------------------------------------------------------------------------------- */

int midi_log_open(MidiLog *l, const char *path, int append, const char *comment) {
    memset(l, 0, sizeof *l);
    FILE *f = fopen(path, append ? "ab" : "wb"); /* binary: LF line ends on every platform, as compare_midi.py reads them */
    if (!f) return -1;
    if (comment) fprintf(f, "# %s\n", comment);
    l->f = f;
    return 0;
}

void midi_log_byte(void *log, uint32_t interrupt, uint8_t byte, int command) {
    MidiLog *l = log;
    if (!l || !l->f) return;
    if (!l->open || l->line != interrupt) {
        if (l->open) fputc('\n', (FILE *)l->f);
        fprintf((FILE *)l->f, "%u:", (unsigned)interrupt);
        l->open = 1;
        l->line = interrupt;
    }
    fprintf((FILE *)l->f, command ? " !%02X" : " %02X", byte);
}

void midi_log_close(MidiLog *l) {
    if (!l->f) return;
    if (l->open) fputc('\n', (FILE *)l->f);
    fclose((FILE *)l->f);
    l->f = NULL;
    l->open = 0;
}

static void deliver(MidiAssembler *a) {
    if (a->fn && a->n) a->fn(a->ctx, a->buf, a->n);
    a->n = 0;
    a->need = 0;
}

void midi_assembler_byte(void *assembler, uint32_t interrupt, uint8_t byte, int command) {
    MidiAssembler *a = assembler;
    (void)interrupt;
    if (command) return; /* MPU-401 commands are the interface's, not MIDI */
    if (byte & 0x80) {
        if (byte == 0xF7 && a->need < 0) {
            if (a->n < (int)sizeof a->buf) {
                a->buf[a->n++] = byte;
                deliver(a);
            } else a->n = a->need = 0; /* too long: dropped */
            return;
        }
        a->n = 0;
        a->buf[a->n++] = byte;
        uint8_t hi = byte & 0xF0;
        a->need = byte == 0xF0 ? -1 : hi == 0xF0 ? 1 : hi == 0xC0 || hi == 0xD0 ? 2 : 3;
        if (a->need == 1) deliver(a);
        return;
    }
    if (!a->need) return; /* a data byte without a status */
    if (a->n < (int)sizeof a->buf) a->buf[a->n++] = byte;
    if (a->need > 0 && a->n == a->need) deliver(a);
}
