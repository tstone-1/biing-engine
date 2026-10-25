/* The digital module player, written from docs/music.md. Readings the spec leaves open are marked "engine reading"
 * and listed in docs/engine.md (sound section). */
#include "music.h"

#include <string.h>

static uint32_t be16(const Module *m, uint32_t o) { return o + 2 <= m->n ? (uint32_t)(m->d[o] << 8 | m->d[o + 1]) : 0; }
static uint32_t be32(const Module *m, uint32_t o) {
    return o + 4 <= m->n ? (uint32_t)m->d[o] << 24 | (uint32_t)m->d[o + 1] << 16 | (uint32_t)m->d[o + 2] << 8 | m->d[o + 3]
                         : 0;
}
static uint8_t byte(const Module *m, uint32_t o) { return o < m->n ? m->d[o] : 0xFF; }

int module_parse(Module *m, const uint8_t *d, size_t n) {
    memset(m, 0, sizeof *m);
    m->d = d;
    m->n = n;
    if (n < 0x40) return -1;
    memcpy(m->name, d, 16);
    m->pat_size = be32(m, 0x10);
    m->smp_size = be32(m, 0x14);
    m->song_size = be32(m, 0x18);
    m->ins_size = be32(m, 0x1C);
    m->tempo = (int)be16(m, 0x22);
    uint64_t total = 0x40ull + m->pat_size + m->smp_size + m->song_size + m->ins_size;
    if (total != n) return -1;
    m->pat = 0x40;
    m->smp = m->pat + m->pat_size;
    m->song = m->smp + m->smp_size;
    m->ins = m->song + m->song_size;
    m->rows = (int)(m->song_size / 12);
    return m->rows > 0 ? 0 : -1;
}

/* ---- tables -------------------------------------------------------------------------------- */

static const uint16_t NOTE_HZ[12] = {262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494};

uint32_t music_freq(int pitch) {
    if (pitch < 0) pitch = 0;
    if (pitch > 119) pitch = 119; /* 10 octaves */
    return (uint32_t)NOTE_HZ[pitch % 12] << (pitch / 12 + 2);
}

double music_rows_per_second(int tempo) { return 50.0 * tempo / 187.0; }

/* A pattern's first event (file offset), or 0 for an unused pattern. */
static uint32_t pattern(const Module *m, int pn) {
    uint32_t off = be16(m, m->pat + 2u * (uint32_t)pn);
    if (!off || off + 16 >= m->pat_size) return 0;
    return m->pat + off + 16; /* a 16-byte name, then 4-byte events */
}

/* Sample record i: 0 if unused. */
static uint32_t sample_rec(const Module *m, int i) {
    if (i < 0 || i > 255) return 0;
    uint32_t off = be32(m, m->smp + 4u * (uint32_t)i);
    if (!off || off + 0x40 > m->smp_size) return 0;
    return m->smp + off;
}
static uint32_t sample_play_len(const Module *m, uint32_t rec) { return rec ? be16(m, rec + 0x10) : 0; }

static uint32_t instrument_rec(const Module *m, int i) {
    if (i < 0 || i > 255) return 0;
    uint32_t off = be16(m, m->ins + 2u * (uint32_t)i);
    if (!off || off + 0x30 > m->ins_size) return 0;
    return m->ins + off;
}

/* Clamps a region (file offset, length) to the file. */
static uint32_t fit(const Module *m, uint32_t start, uint32_t len) {
    if (start >= m->n) return 0;
    return len > m->n - start ? (uint32_t)(m->n - start) : len;
}
static const int8_t *at(const Module *m, uint32_t o) { return (const int8_t *)(m->d + o); }

/* ---- sequencer ----------------------------------------------------------------------------- */

static void load_row(Music *mu) {
    const Module *m = &mu->m;
    uint32_t r = m->song + 12u * (uint32_t)mu->row;
    for (int k = 0; k < MIXER_VOICES; k++) {
        mu->v[k].ev = pattern(m, (int)(be16(m, r + 2u * (uint32_t)k) >> 8));
        mu->v[k].counter = 0;
    }
    mu->ctl_ev = pattern(m, (int)(be16(m, r + 10) >> 8)); /* entry 6: the control pattern; entry 5 is never played */
    mu->ctl_counter = 0;
    mu->ctl_held = 0;
}

static int is_end(const Module *m, uint32_t ev) { return !ev || byte(m, ev) == 0xFF || byte(m, ev + 2) == 0xFF; }

static uint32_t duration(const Module *m, uint32_t ev) {
    uint32_t d = byte(m, ev + 1);
    return d ? d : 65536; /* 0 means 65,536 */
}

static int listed(const Music *mu, int instrument) {
    for (int i = 0; i < mu->only_n; i++)
        if (mu->only[i] == instrument) return 1;
    return 0;
}

/* The note start; with a MIDI card an instrument outside the list does not start (the event is read and its duration
 * counted as usual, the voice goes on as it is: engine reading of "its note start plays only instruments in that
 * list", docs/music.md). */
static void note_on(Music *mu, int k, uint32_t ev) {
    const Module *m = &mu->m;
    MusicVoice *v = &mu->v[k];
    uint8_t note = byte(m, ev);
    if (!note) return; /* a rest: the voice goes on as it is (engine reading) */
    if (mu->midi_card && !listed(mu, byte(m, ev + 2))) return; /* with a MIDI card: listed instruments only */
    v->note_pitch = 12 * (note >> 4) + (note & 15) + v->ctl_transpose + MUSIC_TRANSPOSE;
    v->pitch = v->note_pitch;
    v->freq = music_freq(v->pitch);
    v->volume = byte(m, ev + 3); /* stored as given; the row takes min(volume, 63) */
    v->instrument = byte(m, ev + 2);
    uint32_t rec = instrument_rec(m, v->instrument);
    v->prog = rec ? rec + 0x30 : 0;
    v->prog_cmds = rec ? be16(m, rec + 0x10) / 6 : 0;
    v->pc = 0;
    v->wait = 0;
    v->phase15 = 0;
    v->env = 0;
    v->env_phase = 0;
    v->env_count = 0;
    v->silenced = 0;
}

/* Control pattern events: byte 0 non-zero, byte 1 duration, byte 2 voice 1-4 or 5 for all, byte 3 & 63 the
 * transpose, set for that voice's following notes. An end marker holds it. */
static void control_step(Music *mu) {
    const Module *m = &mu->m;
    if (mu->ctl_held || !mu->ctl_ev) return;
    if (mu->ctl_counter > 0 && --mu->ctl_counter > 0) return;
    for (;;) {
        uint32_t ev = mu->ctl_ev;
        if (is_end(m, ev)) {
            mu->ctl_held = 1;
            return;
        }
        int voice = byte(m, ev + 2), t = byte(m, ev + 3) & 63;
        if (byte(m, ev)) {
            for (int k = 0; k < MIXER_VOICES; k++)
                if (voice == 5 || voice == k + 1) mu->v[k].ctl_transpose = t;
        }
        mu->ctl_counter = duration(m, ev);
        mu->ctl_ev += 4;
        return;
    }
}

/* A row step: the control pattern, then voices 1-4. A voice whose counter runs out reads its next event (engine
 * reading of "a voice whose counter is non-zero counts down": the counter is decremented first, which is what makes
 * a duration of 0 mean 65,536). An end marker moves the song to the next row and runs the whole step again. */
static void row_step(Music *mu) {
    const Module *m = &mu->m;
    mu->row_steps++;
    for (int again = 0; again <= m->rows; again++) {
        control_step(mu);
        int ended = 0;
        for (int k = 0; k < MIXER_VOICES && !ended; k++) {
            MusicVoice *v = &mu->v[k];
            if (v->counter > 0 && --v->counter > 0) continue;
            if (is_end(m, v->ev)) {
                ended = 1;
                break;
            }
            v->counter = duration(m, v->ev);
            note_on(mu, k, v->ev);
            v->ev += 4;
        }
        if (!ended) return;
        mu->row = mu->row + 1 >= m->rows ? 0 : mu->row + 1;
        if (!mu->row) mu->wraps++;
        load_row(mu);
    }
}

/* ---- instrument programs ------------------------------------------------------------------- */

static void select_sample(Music *mu, MusicVoice *v, int a) {
    const Module *m = &mu->m;
    uint32_t rec = sample_rec(m, a);
    v->sample = a;
    v->start = rec ? rec + 0x40 : 0;
    v->length = sample_play_len(m, rec);
    int16_t transpose = rec ? (int16_t)be16(m, rec + 0x2A) : 0;
    v->pitch = v->note_pitch + transpose;
    v->freq = music_freq(v->pitch);
}

static void slide(MusicVoice *v, int a) {
    uint32_t f = v->freq;
    int k = f >= 0x7148 ? 16 : f >= 0x55F0 ? 8 : f >= 0x38A4 ? 4 : 1;
    int64_t nf = (int64_t)f - (int64_t)a * 12 * k; /* a signed, positive = down */
    v->freq = nf < 0 ? 0 : (uint32_t)nf;
}

/* Runs the voice's program until a command waits (docs/music.md, jump table). */
static void program(Music *mu, int k, Voice *out) {
    const Module *m = &mu->m;
    MusicVoice *v = &mu->v[k];
    if (!v->prog) return;
    if (v->wait > 0 && --v->wait > 0) return;
    for (int guard = 0; guard < 256; guard++) {
        if (v->pc >= v->prog_cmds) return;
        uint32_t c = v->prog + 6 * v->pc;
        uint32_t op = be16(m, c);
        int a = (int16_t)be16(m, c + 2), b = (int16_t)be16(m, c + 4);
        /* the flag bits of the opcode word: 0x4000 = a from variable a, 0x8000 = b from variable b (docs/music.md
         * writes them as bits 6 and 7; the files use 0x4003, so they are bits 6 and 7 of the high byte) */
        int ia = a & 255, ib = b & 255;
        if (op & 0x4000) a = v->var[ia];
        if (op & 0x8000) b = v->var[ib];
        switch (op & 31) {
        case 0: return; /* stop: stay on this command */
        case 1: select_sample(mu, v, a); break;
        case 2: v->wait = a; v->pc++; return;
        case 3: /* the repeat region starts a bytes into the current sample */
            if (v->start) {
                uint32_t s = v->start + (uint32_t)(uint16_t)a;
                out->rep = at(m, s);
                out->rep_len = fit(m, s, out->rep_len);
            }
            break;
        case 4: { /* sample a as the repeat region, pointer only */
            uint32_t rec = sample_rec(m, a);
            if (rec) {
                out->rep = at(m, rec + 0x40);
                out->rep_len = fit(m, rec + 0x40, out->rep_len);
            }
            break;
        }
        case 5: v->length = (uint16_t)a; out->rep_len = v->start ? fit(m, v->start, (uint16_t)a) : 0; break;
        case 6: { /* sample a's play length as the repeat length, the pointer and data start stay */
            uint32_t rec = sample_rec(m, a);
            out->rep_len = sample_play_len(m, rec);
            break;
        }
        case 7: slide(v, a); break;
        case 8:
            v->pitch += a;
            if (v->pitch < 0) v->silenced = 1, voice_stop(out);
            else v->freq = music_freq(v->pitch);
            break;
        case 9: v->volume += a; v->volume = v->volume < 0 ? 0 : v->volume > 64 ? 64 : v->volume; break;
        case 10: v->loop_pc = v->pc; v->loop_var = ia; v->var[ia] = (int16_t)b; break;
        case 11:
            v->var[v->loop_var] = (int16_t)(v->var[v->loop_var] + b);
            if (v->var[v->loop_var] <= a) {
                v->pc = v->loop_pc + 1; /* back to the command after the loop start (engine reading) */
                continue;
            }
            break;
        case 12: v->var[ia] = (int16_t)(v->var[ia] + b); break;
        case 13: v->var[ia] = (int16_t)b; break;
        case 15: /* play over three ticks */
            if (v->phase15 == 0) {
                voice_stop(out); /* tick 1: silence */
                v->phase15 = 1;
                return;
            }
            if (v->phase15 == 1) { /* tick 2: start the sample */
                uint32_t len = v->start ? fit(m, v->start, v->length) : 0;
                voice_start(out, v->start ? at(m, v->start) : NULL, len, 0);
                v->silenced = 0;
                v->phase15 = 2;
                return;
            }
            { /* tick 3: the repeat region = the sample's loop, or nothing; then on with the program */
                uint32_t rec = sample_rec(m, v->sample);
                uint32_t ls = rec ? be16(m, rec + 0x12) : 0, le = rec ? be16(m, rec + 0x14) : 0;
                if (rec && le > ls) {
                    out->rep = at(m, v->start + ls);
                    out->rep_len = fit(m, v->start + ls, le - ls);
                } else {
                    out->rep = NULL;
                    out->rep_len = 0;
                }
                v->phase15 = 0;
            }
            break;
        default: break; /* 14, 16-31: no effect */
        }
        v->pc++;
    }
}

static void envelope(Music *mu, int k) {
    const Module *m = &mu->m;
    MusicVoice *v = &mu->v[k];
    uint32_t rec = instrument_rec(m, v->instrument);
    if (!rec) return;
    int at_ticks = byte(m, rec + 0x12), at_step = byte(m, rec + 0x13);
    int de_ticks = byte(m, rec + 0x14), de_step = byte(m, rec + 0x15), sustain = byte(m, rec + 0x17);
    /* after each step the counter is loaded with the tick byte and counted down: a step every (ticks + 1) ticks */
    /* phase 2 (hold) needs no branch of its own: a decay step from the sustain level clamps back to it */
    if (v->env_count > 0) {
        v->env_count--;
        return;
    }
    if (v->env_phase == 0) {
        v->env += at_step;
        if (v->env >= 256) v->env = 256, v->env_phase = 1;
        v->env_count = at_ticks;
    } else {
        v->env -= de_step;
        if (v->env <= sustain) v->env = sustain, v->env_phase = 2;
        v->env_count = de_ticks;
    }
}

int music_start(Music *mu, const uint8_t *d, size_t n) {
    memset(mu, 0, sizeof *mu);
    if (module_parse(&mu->m, d, n)) return -1;
    for (int k = 0; k < MIXER_VOICES; k++) mu->v[k].instrument = -1;
    mu->master = 64;
    mu->playing = 1;
    load_row(mu); /* the pass counter starts at 0: the first row step comes on the 4th pass of 187 */
    return 0;
}

void music_stop(Music *mu, Voice v[MIXER_VOICES]) {
    mu->playing = 0;
    for (int k = 0; k < MIXER_VOICES; k++) voice_stop(&v[k]);
}

void music_set_midi(Music *mu, const uint8_t *list, int n) {
    mu->midi_card = 1;
    mu->only_n = n < 0 ? 0 : n > (int)sizeof mu->only ? (int)sizeof mu->only : n;
    if (mu->only_n) memcpy(mu->only, list, (size_t)mu->only_n);
}

void music_tick(Music *mu, Voice v[MIXER_VOICES]) {
    if (!mu->playing) return;
    if (mu->midi_card && !mu->only_n) return; /* a MIDI card with an empty list: no row step, no voice (0x72C17) */
    mu->acc += 4u * (uint32_t)mu->m.tempo;
    while (mu->acc >= 187) { /* "each time it passes 187": >= is the engine reading */
        mu->acc -= 187;
        if (++mu->sub == 4) {
            mu->sub = 0;
            row_step(mu);
        }
    }
    for (int k = 0; k < MIXER_VOICES; k++) {
        MusicVoice *mv = &mu->v[k];
        program(mu, k, &v[k]);
        envelope(mu, k);
        v[k].step = mv->freq;
        /* ((min(volume, 63) x envelope) >> 8) x the master row >> 6 (docs/music.md, "Instrument programs") */
        int vol = mv->volume > 63 ? 63 : mv->volume;
        v[k].row = mv->silenced ? 0 : (vol * mv->env >> 8) * mu->master >> 6;
    }
}
