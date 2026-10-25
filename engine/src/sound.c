/* Sound effects and the background slot, written from docs/sound.md (and docs/saves.md for the two config files).
 * Engine readings where the spec allows more than one are marked "engine reading" and listed in docs/engine.md. */
#include "sound.h"
#include "assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- config files -------------------------------------------------------------------------- */

/* Finds the value of the last line whose first two characters are key's; returns its start or NULL. */
static const char *cfg_value(const char *t, size_t n, const char *key, size_t *len) {
    const char *found = NULL;
    size_t i = 0;
    while (i < n) {
        size_t e = i;
        while (e < n && t[e] != '\n') e++;
        if (e - i >= 2 && t[i] == key[0] && t[i + 1] == key[1]) {
            const char *dash = memchr(t + i, '-', e - i);
            if (dash) {
                found = dash + 1;
                *len = (size_t)(t + e - found);
            }
        }
        i = e + 1;
    }
    return found;
}

int sound_cfg_flag(const char *text, size_t n, const char *key, int dflt) {
    size_t len = 0;
    const char *v = cfg_value(text, n, key, &len);
    if (!v) return dflt;
    return !(len > 0 && (v[0] == 'N' || v[0] == 'n'));
}

int sound_cfg_card(const char *text, size_t n, const char *key, int dflt) {
    size_t len = 0;
    const char *v = cfg_value(text, n, key, &len);
    if (!v) return dflt;
    char c1 = len > 1 ? v[1] : ' ', c2 = len > 2 ? v[2] : ' ';
    if (c1 == 'B') return c2 == 'P' ? 2 : c2 == '1' ? 3 : 1;
    if (c1 == 'O' && c2 == 'L') return 6;
    if (c1 == 'M') return 7;
    if (c1 == 'D') return 8;
    return 0;
}

int sound_cfg_mixer(const char *text, size_t n, int k, int dflt) {
    size_t len = 0;
    const char *v = cfg_value(text, n, "MIXER", &len);
    if (!v || (size_t)k >= len) return dflt;
    char c = v[k];
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return dflt;
}

static int read_text(const char *root, const char *name, Blob *b) {
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", root, name);
    return blob_read(path, b);
}

static uint32_t no_random(void *ctx, uint32_t n) {
    (void)ctx;
    (void)n;
    return 0;
}

void sound_set_bits(Sound *s, SoundBits bits) {
    if (s) s->bits = bits;
}

static uint32_t draw_bits(Sound *s, uint32_t mask) {
    return s->bits ? s->bits(s->random_ctx, mask) & mask : s->random(s->random_ctx, mask + 1);
}

void sound_init(Sound *s, const char *root, const Program *prog, SoundRandom random, void *ctx) {
    memset(s, 0, sizeof *s);
    snprintf(s->root, sizeof s->root, "%s", root ? root : ".");
    s->prog = prog;
    s->random = random ? random : no_random;
    s->random_ctx = ctx;
    s->effects_on = 1;
    s->ambience_count = 50; /* the image's start value (docs/mainloop.md, "Rest of the clock routine") */
    s->dcard = s->mcard = 1;
    Blob b;
    if (root && read_text(root, "BIING!.CFG", &b) == 0) {
        s->effects_on = sound_cfg_flag((const char *)b.data, b.size, "SOUND", 1);
        blob_free(&b);
    }
    s->cd_level = 15;
    if (root && read_text(root, "SOUND.CFG", &b) == 0) {
        s->dcard = sound_cfg_card((const char *)b.data, b.size, "DCARD", 1);
        s->mcard = sound_cfg_card((const char *)b.data, b.size, "MCARD", 1);
        s->cd_level = sound_cfg_mixer((const char *)b.data, b.size, 3, 15);
        blob_free(&b);
    }
    /* the driver's stereo flag: set for an SB Pro or SB16 digital card, cleared when DCARD or MCARD is plain SB, so
     * DCARD SBP with MCARD SB is mono (docs/sound.md, "Stereo (SB Pro)"); the volume table follows it from here */
    s->stereo = (s->dcard == 2 || s->dcard == 3) && s->mcard != 1;
    for (int k = 0; k < MIXER_VOICES; k++) s->voice[k].idle = 1;
    s->last_resource = -1;
    if (root && read_text(root, "CDC", &b) == 0) s->cdc = 1, blob_free(&b);
    if (root && read_text(root, "DATA/ONHD", &b) == 0) {
        size_t n = b.size < sizeof s->onhd ? b.size : sizeof s->onhd;
        memcpy(s->onhd, b.data, n);
        s->onhd_bits = (int)(8 * n);
        blob_free(&b);
    }
    const char *log = getenv("BIING_MIDI_LOG");
    if (log) snprintf(s->midi_log_prefix, sizeof s->midi_log_prefix, "%s", log);
    if (root && s->mcard == 8) {
        s->cd_mode = cd_open(&s->cd, root, s->cd_level);
        if (!s->cd_mode)
            fprintf(stderr, "[INFO] SOUND.CFG selects CD audio, but %s has no TRACK05.WAV: the songs play as modules "
                            "(tools/setup_game.py writes the CD tracks)\n", s->cd.dir);
    }
}

void sound_free(Sound *s) {
    if (!s) return;
    sound_midi_finish(s); /* the program's end: song stop and shut-down */
    cd_free(&s->cd);
    for (int i = 0; i < SOUND_IDS; i++) free(s->sample[i]);
    free(s->capture);
    memset(s->sample, 0, sizeof s->sample);
    s->capture = NULL;
}

int sound_enabled(const Sound *s) { return s && s->effects_on && s->dcard != 0; }

/* ---- samples ------------------------------------------------------------------------------- */

int sound_load(Sound *s, int id) {
    if (!s || id <= 0 || id >= SOUND_IDS) return -1;
    if (s->status[id]) return s->status[id];
    char rel[32], path[1200];
    s->status[id] = -1;
    if (image_rel_path(2800 + id, rel, sizeof rel)) return -1;
    snprintf(path, sizeof path, "%s/%s", s->root, rel);
    Blob b;
    if (blob_read(path, &b)) return -1;
    s->sample[id] = (int8_t *)b.data;
    s->file_len[id] = b.size;
    s->sample_len[id] = (uint32_t)(b.size & ~(size_t)1); /* the driver keeps the length in 16-bit words */
    s->status[id] = b.size >= 4 && memcmp(b.data, "BONG", 4) == 0 ? 2 : 1;
    return s->status[id];
}

uint32_t sound_step(int period) { return period > 0 ? 10500000u / (uint32_t)period : 0; }

double sound_rate(int period) {
    return (double)(sound_step(period) >> 8) / 256.0 * MIXER_RATE_NUM / MIXER_RATE_DEN;
}

int sound_row(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 63) volume = 63;
    return (6 * volume >> 3) + 7;
}

/* ---- MIDI (docs/music.md, "MIDI output (SND-9)") ------------------------------------------------------------- */

int sound_midi_device(int mcard) { return mcard == 6 || mcard == 7 ? (mcard - 6) ^ 1 : -1; }

static const char *const PROG_NAME[4] = {"", "bipro", "relo", "bint"};

static void midi_log_stop(Sound *s) {
    if (!s->midi_log.f) return;
    midi_remove_sink(&s->midi, midi_log_byte, &s->midi_log);
    midi_log_close(&s->midi_log);
}

void sound_midi_end(Sound *s) {
    if (!s || !s->midi_prog) return;
    midi_end(&s->midi);
    s->midi_prog = SOUND_PROG_NONE;
    midi_log_stop(s);
}

void sound_midi_stop(Sound *s) {
    if (s && s->midi_prog) midi_song_stop(&s->midi);
}

void sound_midi_finish(Sound *s) {
    if (!s || !s->midi_prog) return;
    sound_midi_stop(s);
    sound_midi_end(s);
}

void sound_midi_program(Sound *s, int prog) {
    if (!s || prog <= SOUND_PROG_NONE || prog > SOUND_PROG_BINT || s->midi_prog == prog) return;
    int device = sound_midi_device(s->mcard);
    if (device < 0) return;
    sound_midi_finish(s);
    if (s->midi_log_prefix[0]) {
        char path[1100], note[64];
        snprintf(path, sizeof path, "%s%s.txt", s->midi_log_prefix, PROG_NAME[prog]);
        snprintf(note, sizeof note, "engine %s %s", PROG_NAME[prog], device == MIDI_GM ? "gm" : "mt32");
        if (midi_log_open(&s->midi_log, path, s->midi_logged[prog], note) == 0) {
            midi_add_sink(&s->midi, midi_log_byte, &s->midi_log);
            s->midi_logged[prog] = 1;
        } else {
            fprintf(stderr, "[WARN] BIING_MIDI_LOG: cannot write %s\n", path);
        }
    }
    s->midi_prog = prog;
    midi_begin(&s->midi, device, prog != SOUND_PROG_RELO);
}

int sound_midi_score(Sound *s, const char *rel) {
    if (!s || !s->midi_prog) return -1;
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", s->root, rel);
    Blob b;
    if (blob_read(path, &b)) {
        fprintf(stderr, "[WARN] MIDI score %s missing: the song is silent\n", path);
        return -1;
    }
    int rc = midi_song_start(&s->midi, b.data, b.size);
    int tempo = rc ? -1 : midi_tempo(&s->midi);
    if (rc) fprintf(stderr, "[WARN] %s is no SCMF score: the song is silent\n", path);
    blob_free(&b);
    return tempo;
}

int sound_song_score(const Sound *s, int id, char *out, size_t n) {
    if (!s || !s->prog || !n) return -1;
    for (uint32_t i = 0; i < 64; i++) { /* 12-byte entries: module file number, score path pointer, CD track */
        uint32_t file = program_dword(s->prog, 0xB97E, 12 * i);
        if (file == 0xFFFFFFFFu) break;
        if (file != (uint32_t)(2800 + id)) continue;
        size_t left;
        const uint8_t *p = program_raw_ptr(s->prog, program_dword(s->prog, 0xB97E, 12 * i + 4), &left);
        if (!p) return -1;
        size_t k = 0;
        for (; k + 1 < n && k < left && p[k]; k++) {
            char c = (char)p[k];
            out[k] = c == '\\' ? '/' : c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
        }
        out[k] = 0;
        return k ? 0 : -1;
    }
    return -1;
}

/* ---- the play call 0xDE46 ----------------------------------------------------------------- */

static void stop_song(Sound *s) {
    if (s->music.playing && sound_midi_device(s->mcard) >= 0) sound_midi_stop(s); /* the device stop of a MIDI card */
    if (s->music.playing) music_stop(&s->music, s->voice); /* all four voices */
    s->song = 0;
    if (s->cd_song) cd_stop(&s->cd); /* the CD track too; the track word is cleared */
    s->cd_song = 0;
}

int sound_song_track(const Sound *s, int id) {
    if (!s || !s->prog) return 0;
    for (uint32_t i = 0; i < 64; i++) { /* 12-byte entries, ended by -1 */
        uint32_t file = program_dword(s->prog, 0xB97E, 12 * i);
        if (file == 0xFFFFFFFFu) break;
        if (file == (uint32_t)(2800 + id)) return (int)program_dword(s->prog, 0xB97E, 12 * i + 8);
    }
    return 0;
}

void sound_resource_read(Sound *s, int resource) {
    if (!s || resource == s->last_resource) return; /* the loader keeps the resource it read last */
    s->last_resource = resource;
    int on_disk = resource < 0 || resource >= s->onhd_bits || (s->onhd[resource >> 3] >> (resource & 7) & 1);
    if (s->cdc && !on_disk && s->cd_song) { /* a CD read: the track stops for good */
        cd_stop(&s->cd);
        s->cd_song = 0;
    }
}

void sound_cd_frame(Sound *s) {
    if (!s || !s->cd_mode || !s->cd_song) return;
    if (s->bg_level > 0) s->bg_level = 1; /* the song level is pinned at 1 while a track plays */
    if (!s->cd.busy) cd_play(&s->cd, s->cd_song); /* ended: stop and the same track again from its beginning */
}

int sound_program_track(Sound *s, int track) {
    if (!s || !s->cd_mode) return 0;
    cd_play(&s->cd, track); /* a missing TRACKnn.WAV leaves the drive standing: silence */
    return 1;
}

void sound_program_track_stop(Sound *s) {
    if (!s || !s->cd_mode || s->cd_song) return;
    cd_stop(&s->cd);
}

void sound_play(Sound *s, int id, int v, int period, int volume, int loop) {
    if (!sound_enabled(s)) return;
    if (v < 0 || v >= 3) return; /* voice 3 is never started */
    if (v == 2 && id == SOUND_SITE_MAP_SONG) {
        /* the site-map song: the module stopped and all four voices cleared, always; no device stop, so a CD track
         * plays on (docs/sound.md, "The play call's clear for the background voice", SND-15) */
        s->music.playing = 0;
        midi_halt(&s->midi); /* [0x704C4] = -1: the MIDI ticks stop too, with no all-notes-off */
        s->song = 0;
        for (int k = 0; k < MIXER_VOICES; k++) voice_stop(&s->voice[k]);
    } else if (v == 2) {
        stop_song(s); /* a module running: all four voices and the MIDI/CD song */
        voice_stop(&s->voice[2]);
        voice_stop(&s->voice[3]);
    }
    int st = sound_load(s, id);
    if (st < 0) return;
    if (s->voice_id[v] == id && v == 2) return; /* nothing restarts */
    if (s->voice_id[v] != id) sound_resource_read(s, 2800 + id); /* the driver's cache misses: the loader reads it */
    s->voice_id[v] = id;
    if (st == 2) {
        /* a music bank: the song through the music table. The module plays with MCARD SB, SBP or SB16; with MCARD OFF
         * no song starts (engine reading, docs/engine.md). With MCARD CD and the tracks the module is set up but plays
         * no notes, and the song's CD track plays (docs/cdrom.md). With MCARD ROL or GM the module is set up silent
         * and the song's score plays through the MIDI driver (docs/music.md, "MIDI output (SND-9)"). */
        if (!s->mcard) return;
        stop_song(s);
        const char *score = s->midi_score;
        s->midi_score = NULL;
        if (music_start(&s->music, (const uint8_t *)s->sample[id], s->file_len[id])) return;
        s->music.master = sound_row(volume);
        s->song = id;
        s->started[id]++;
        if (s->cd_mode) {
            s->cd_song = sound_song_track(s, id);
            cd_play(&s->cd, s->cd_song);
        } else if (sound_midi_device(s->mcard) >= 0) {
            music_set_midi(&s->music, NULL, 0); /* BIPRO lists no instrument: no digital note */
            char rel[128];
            if (score) snprintf(rel, sizeof rel, "%s", score);
            else if (sound_song_score(s, id, rel, sizeof rel)) rel[0] = 0;
            sound_midi_program(s, SOUND_PROG_BIPRO);
            if (rel[0]) sound_midi_score(s, rel);
        }
        return;
    }
    Voice *a = &s->voice[v];
    voice_start(a, s->sample[id], s->sample_len[id], loop);
    a->step = sound_step(period);
    a->row = sound_row(volume);
    if (v == 2) { /* the background plays on both stereo sides */
        s->voice[3] = *a;
        s->voice_id[3] = id;
    }
    s->started[id]++;
    /* the delay 0xE4E9 after the start (CX = 1): the credit is set to 0 and the driver waits two interrupts, so the
     * clock call after it finds 2 (docs/sound.md, "Voices and output"; not for a song nor an early return) */
    if (s->tick_credit) *s->tick_credit = 2;
}

void sound_direct(Sound *s, int id, int v, int period, int volume) { sound_play(s, id, v, period, volume, 0); }

/* ---- queues and writers ------------------------------------------------------------------- */

static int busy(const Sound *s) { return s->help_pending || s->race_pending || s->race_busy; }
static int queue_empty(const Sound *s, int q) { return s->queue[q][0].id == 0; }

/* the 6-byte entry at a disk table start plus a byte index (the table start is mapped to the build, program.h) */
static SoundEntry entry_at(const Sound *s, uint32_t table, uint32_t index) {
    SoundEntry e = {0, 0, 0};
    if (!s->prog) return e;
    size_t left;
    uint32_t at = program_at(s->prog, table);
    const uint8_t *p = at == PROGRAM_NOWHERE ? NULL : program_raw_ptr(s->prog, at + index, &left);
    if (!p || left < 6) return e;
    e.id = (uint16_t)(p[0] | p[1] << 8);
    e.period = (uint16_t)(p[2] | p[3] << 8);
    e.volume = (uint16_t)(p[4] | p[5] << 8);
    return e;
}

static void copy_list(Sound *s, int q, uint32_t list, int count) {
    for (int i = 0; i < count; i++) s->queue[q][i] = entry_at(s, list, 6u * (uint32_t)i);
}

void sound_request(Sound *s, int id, int period, int volume) {
    if (!s || s->help_pending || s->race_pending) return;
    SoundEntry e = {(uint16_t)id, (uint16_t)period, (uint16_t)volume};
    s->queue[0][0] = s->queue[1][0] = e; /* the head of both; the rest stays */
    s->sticky = 1;
}

void sound_list(Sound *s, int queue, uint32_t list) {
    if (!s || queue < 0 || queue >= SOUND_QUEUES) return;
    if (queue == 0 && busy(s)) return; /* only 0x1AFE7 checks the busy flags */
    copy_list(s, queue, list, SOUND_QUEUE_LEN);
}

void sound_clear(Sound *s) {
    if (!s) return;
    if (!busy(s)) memset(s->queue, 0, sizeof s->queue);
    s->sticky = 1;
}

void sound_help(Sound *s) {
    if (s) s->help_pending = 1;
}
void sound_race(Sound *s, int value) {
    if (s) s->race_pending = value;
}

void sound_ring(Sound *s) {
    if (!s) return;
    SoundEntry e = {0xC6, 0x140, 63};
    s->queue[1][0] = e;
    memset(&s->queue[1][1], 0, sizeof(SoundEntry));
}

void sound_show(Sound *s, int with_patient) {
    if (!s) return;
    sound_clear(s);
    for (int q = 0; q < 2; q++) {
        uint32_t list = 0x48AA + 48u * s->random(s->random_ctx, 8);
        copy_list(s, q, list, SOUND_QUEUE_LEN);
        if (!with_patient) memset(&s->queue[q][1], 0, sizeof(SoundEntry));
    }
    s->sticky = 1;
}

void sound_invoice(Sound *s, int c) {
    if (!s || c < 0 || c > 8) return;
    uint32_t list = 0x4A2A + 48u * (uint32_t)c;
    copy_list(s, 0, list, SOUND_QUEUE_LEN);
    copy_list(s, 1, list, SOUND_QUEUE_LEN);
    s->sticky = 1;
}

int sound_board_blocked(const Sound *s) {
    return !s || busy(s) || s->help_busy || s->bg_playing == SOUND_SITE_MAP_SONG;
}

/* ---- ambience ------------------------------------------------------------------------------ */

void sound_ambience_copy(Sound *s, int queue, uint32_t table) {
    if (!s || queue < 0 || queue >= SOUND_QUEUES) return;
    int i = 0;
    while (i < 6 && s->queue[queue][i].id) i++;
    if (i == 6) return; /* none of the first six free */
    uint32_t r = draw_bits(s, 7); /* one raw draw AND 7, without ranged-draw retries (SND-16) */
    s->queue[queue][i] = entry_at(s, table, 12u * r);
    s->queue[queue][i + 1] = entry_at(s, table, 12u * r + 6);
}

void sound_ambience(Sound *s, int queue, uint32_t table) {
    if (!s || !s->ambience) return;
    if (draw_bits(s, 63) != 4) return; /* t AND 63 (0x36F49, SND-11) */
    if (queue == 0 && busy(s)) return;
    if (queue == 1) s->ambience = 0; /* 0x36F5C clears the flag once its draw passed; 0x36F3E never does */
    sound_ambience_copy(s, queue, table);
}

void sound_ambience_pair(Sound *s, uint32_t table) {
    sound_ambience(s, 0, table);
    sound_ambience(s, 1, table);
}

void sound_show_music(Sound *s, int count) {
    if (!s || !count) return;
    if (draw_bits(s, 1) == 0) return; /* SND-18: both show draws are raw AND 1, without ranged retries */
    sound_ambience(s, draw_bits(s, 1) ? 0 : 1, 0x50DA);
}

/* ---- the background slot 0x4A0EA ----------------------------------------------------------- */

void sound_background(Sound *s, int id, int period, int level) {
    if (!s) return;
    s->bg_request = (uint16_t)id;
    s->bg_period = (uint16_t)period;
    s->bg_target = (uint16_t)level;
}

void sound_set_level(Sound *s, int level) {
    if (s) s->bg_level = level;
}

int sound_background_requested_or_playing(const Sound *s, int id) {
    return s && (s->bg_request == id || s->bg_playing == id);
}

/* A PCM loop's voices 2 and 3 follow the level at every step. A song is not touched here: the music player scales it
 * by the level itself at every tick (sound_interrupt). So nothing is written while the site-map song plays, as the
 * spec says, without a test of its own: that id is a song. */
static void apply_level(Sound *s) {
    int row = sound_row(s->bg_level);
    if (s->bg_playing && s->status[s->bg_playing] == 1) {
        if (s->voice_id[2] == s->bg_playing) s->voice[2].row = row;
        if (s->voice_id[3] == s->bg_playing) s->voice[3].row = row;
    }
}

void sound_background_step(Sound *s, int clock_running) {
    if (!s) return;
    if (clock_running) {
        if (s->pace > 0) return;
        s->pace = 4; /* a paced action reloads the gate (docs/sound.md, U-9 rule 3) */
    }
    if (s->bg_request) {
        if (s->bg_level > 0) {
            /* dropped (the id already playing): cleared before the decrement, so the level just dips one step and
             * comes back; the drop test is only reached above level 0 (SND-14) */
            if (s->bg_request == s->bg_playing) s->bg_request = 0;
            s->bg_level--;
        }
        if (s->bg_request && s->bg_level == 0) { /* at level 0, also in the step that brought it there (SND-14) */
            s->bg_playing = s->bg_request; /* the id playing again starts again from its beginning */
            s->bg_request = 0;
            sound_play(s, s->bg_playing, 2, s->bg_period, 0, 1);
        }
    } else if (s->bg_level < s->bg_target) {
        s->bg_level++; /* never falls toward a lower target */
        s->pace = 4; /* after every fade-in step, the clock running or not (SND-14) */
    }
    apply_level(s);
}

/* ---- draining 0x49DF3 ---------------------------------------------------------------------- */

static void shift(Sound *s, int q) {
    memmove(&s->queue[q][0], &s->queue[q][1], sizeof(SoundEntry) * (SOUND_QUEUE_LEN - 1));
    memset(&s->queue[q][SOUND_QUEUE_LEN - 1], 0, sizeof(SoundEntry));
}

void sound_drain(Sound *s) {
    if (!s) return;
    Voice *v = s->voice;
    if (s->sticky && !(v[0].idle && v[1].idle)) return;
    if (queue_empty(s, 0) && queue_empty(s, 1)) s->help_busy = s->race_busy = 0;
    int free12 = queue_empty(s, 0) && queue_empty(s, 1) && v[0].idle && v[1].idle;
    if (s->help_pending && free12) {
        SoundEntry e = {1, 0x140, 63};
        s->queue[0][0] = s->queue[1][0] = e;
        s->help_pending = 0;
        s->help_busy = 1;
        free12 = 0;
    }
    if (s->race_pending && free12) {
        uint32_t list = s->race_pending == 1 ? 0x558E : 0x55B6;
        copy_list(s, 0, list, 6);
        copy_list(s, 1, list, 6);
        s->race_pending = 0;
        s->race_busy = 1;
    }
    if (s->bg_playing == SOUND_SITE_MAP_SONG) return; /* the song uses all four voices */
    for (int q = 0; q < SOUND_QUEUES; q++) {
        SoundEntry e = s->queue[q][0];
        if (!e.id || !v[q].idle) continue;
        shift(s, q);
        sound_play(s, e.id, q, e.period, e.volume, 0); /* queue 4: refused, so discarded */
    }
}

void sound_main_loop(Sound *s, int clock_running) {
    sound_background_step(s, clock_running);
    sound_drain(s);
}

void sound_game_tick(Sound *s) {
    if (!s) return;
    if (s->pace > 0) s->pace--;
    if (--s->ambience_count <= 0) {
        s->ambience_count = 34;
        s->ambience = 1;
    }
}

/* ---- output -------------------------------------------------------------------------------- */

void sound_interrupt(Sound *s) {
    if (!s) return;
    /* the driver's callback mixes first, then runs the module player: what a music tick changes (a note, a row, a
     * step) is heard from the next tick's output (docs/music.md, "Driver timing and mixing") */
    int n = mixer_tick_samples(&s->out_acc);
    mixer_render(s->voice, s->stereo, s->out, n);
    if (s->cd_mode) cd_render(&s->cd, s->out, n); /* the CD's audio through the mixer's CD input */
    /* a song started by the play call plays no notes with CD audio (its track plays instead); the start programs
     * (intro_relo.c, intro_bint.c start the player themselves, no song) start no module in CD mode but their tracks
     * 2, 3 and 4 (sound_program_track, docs/intro.md) */
    if (s->music.playing && !(s->cd_mode && s->song)) {
        /* the music player reads the level [0x704D6] at every tick, so every background song fades in and out with
         * the slot, the site-map song included (settled 2026-10-20 by the headless comparison, docs/engine.md) */
        if (s->song && s->song == s->bg_playing) s->music.master = sound_row(s->bg_level);
        music_tick(&s->music, s->voice);
    }
    midi_interrupt(&s->midi); /* the callback's MIDI part, after the module's (docs/music.md, "Timing") */
    if (s->stream) s->stream(s->stream_ctx, s->out, n);
    s->out_frames = n;
    if (!s->capture_cap) return;
    if (s->capture_frames + (size_t)n > s->capture_cap) {
        size_t cap = s->capture_cap * 2;
        int16_t *p = realloc(s->capture, cap * 2 * sizeof *p);
        if (!p) return;
        s->capture = p;
        s->capture_cap = cap;
    }
    memcpy(s->capture + 2 * s->capture_frames, s->out, (size_t)n * 2 * sizeof *s->out);
    s->capture_frames += (size_t)n;
}

void sound_capture(Sound *s, int on) {
    if (!s) return;
    if (on && !s->capture_cap) {
        s->capture_cap = 1 << 16;
        s->capture = malloc(s->capture_cap * 2 * sizeof *s->capture);
        if (!s->capture) s->capture_cap = 0;
        s->capture_frames = 0;
    } else if (!on) {
        s->capture_cap = 0;
    }
}

static void le32(FILE *f, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    fwrite(b, 1, 4, f);
}
static void le16(FILE *f, uint16_t v) {
    uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    fwrite(b, 1, 2, f);
}

int sound_write_wav(const char *path, const int16_t *frames, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t bytes = (uint32_t)(n * 4);
    fwrite("RIFF", 1, 4, f);
    le32(f, 36 + bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    le32(f, 16);
    le16(f, 1);
    le16(f, 2);
    le32(f, MIXER_RATE);
    le32(f, MIXER_RATE * 4);
    le16(f, 4);
    le16(f, 16);
    fwrite("data", 1, 4, f);
    le32(f, bytes);
    for (size_t i = 0; i < 2 * n; i++) le16(f, (uint16_t)frames[i]);
    return fclose(f) ? -1 : 0;
}
