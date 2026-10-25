/* biing-sound: the engine's sound code on the command line, for listening and checks (docs/sound.md, docs/music.md).
 *   biing-sound <BIING!-dir> sfx <id> [period] <out.wav>      one sample, once, at volume 63 (period default 0x140)
 *   biing-sound <BIING!-dir> song <id> <seconds> <out.wav>    a music bank (0xAF, 0xB5, 0xDB-0xDE) at full level
 *   biing-sound <BIING!-dir> length <id>                      a song's length: ticks until it first returns to row 0
 *   biing-sound <BIING!-dir> module <file> <seconds> <out.wav> any module file (e.g. DATA/N/BFANF.SCT)
 *   biing-sound <BIING!-dir> midi <gm|mt32> <interrupts> <out.txt> <id>[=<score>][,<id>[=<score>]...]
 *       the MIDI driver's stream as a stream file for tools/compare_midi.py (docs/music.md, "MIDI output (SND-9)"):
 *       BIPRO's set-up, then each song as the background slot's play call starts it (a song after the first with the
 *       play call's device stop), <interrupts> timer interrupts each, then the song stop and the program end; the
 *       score is the music table's unless <score> names another file in the game folder (DATA/M/BACTION.SCT).
 *       MCARD GM (gm) or ROL (mt32) whatever SOUND.CFG says; e.g. midi gm 6283 cdream.txt 0xDE
 * Ids may be decimal or 0x-hex. Output: 21,739 Hz, 16-bit stereo WAV, mixed as the game mixes (mono card unless
 * SOUND.CFG names SBP or SB16). */
#include "assets.h"
#include "sound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* midi: the songs one after the other, as tools/oracle_midi.py's bipro mode records them */
static int midi(const char *root, const char *dev, int interrupts, const char *out, char *songs) {
    static Program prog;
    static Sound s;
    char exe[1200];
    snprintf(exe, sizeof exe, "%s/BIPRO.EXE", root);
    if (program_load(&prog, exe)) {
        fprintf(stderr, "[FAIL] %s\n", asset_error());
        return 1;
    }
    sound_init(&s, root, &prog, NULL, NULL);
    s.effects_on = 1;
    if (!s.dcard) s.dcard = 1; /* the play call needs DCARD not OFF */
    s.mcard = !strcmp(dev, "mt32") ? 6 : 7;
    s.cd_mode = 0;
    MidiLog log;
    char note[300];
    snprintf(note, sizeof note, "biing-sound midi %s %d %s", dev, interrupts, songs);
    if (midi_log_open(&log, out, 0, note)) {
        fprintf(stderr, "[FAIL] cannot write %s\n", out);
        return 1;
    }
    midi_add_sink(&s.midi, midi_log_byte, &log);
    sound_midi_program(&s, SOUND_PROG_BIPRO);
    int n = 0;
    for (char *tok = strtok(songs, ","); tok; tok = strtok(NULL, ","), n++) {
        char *eq = strchr(tok, '=');
        if (eq) *eq = 0;
        int id = (int)strtol(tok, NULL, 0);
        s.midi_score = eq ? eq + 1 : NULL;
        if (sound_load(&s, id) != 2) {
            fprintf(stderr, "[FAIL] id %s is not a music bank\n", tok);
            return 1;
        }
        sound_play(&s, id, 2, 0x140, 0, 1);
        for (int t = 0; t < interrupts; t++) sound_interrupt(&s);
    }
    sound_midi_finish(&s); /* the song stop, then the program end */
    uint32_t sent = s.midi.sent;
    midi_log_close(&log);
    sound_free(&s);
    program_free(&prog);
    printf("[OK] %s: %d songs, %u bytes\n", out, n, (unsigned)sent);
    return 0;
}

static int wav(Sound *s, int ticks, const char *out) {
    sound_capture(s, 1);
    for (int t = 0; t < ticks; t++) sound_interrupt(s);
    int rc = sound_write_wav(out, s->capture, s->capture_frames);
    if (rc) fprintf(stderr, "[FAIL] cannot write %s\n", out);
    else printf("[OK] %s: %.2f s\n", out, (double)s->capture_frames / MIXER_RATE);
    return rc ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <BIING!-dir> sfx <id> [period] <out.wav> | song <id> <seconds> <out.wav> | length <id> | "
                        "module <file> <seconds> <out.wav> | midi <gm|mt32> <interrupts> <out.txt> <id>[=<score>][,...]\n",
                argv[0]);
        return 2;
    }
    if (!strcmp(argv[2], "midi")) {
        if (argc != 7 || (strcmp(argv[3], "gm") && strcmp(argv[3], "mt32"))) return 2;
        return midi(argv[1], argv[3], atoi(argv[4]), argv[5], argv[6]) ? 1 : 0;
    }
    static Sound s;
    sound_init(&s, argv[1], NULL, NULL, NULL);
    s.effects_on = 1;
    if (!s.dcard) s.dcard = 1; /* a check tool: play even with DCARD -OFF */
    if (!s.mcard) s.mcard = 1;
    const char *cmd = argv[2];
    int rc = 2;
    if (!strcmp(cmd, "sfx") && argc >= 5) {
        int id = (int)strtol(argv[3], NULL, 0);
        int period = argc >= 6 ? (int)strtol(argv[4], NULL, 0) : 0x140;
        if (sound_load(&s, id) != 1) {
            fprintf(stderr, "[FAIL] id %d is not a PCM sample\n", id);
            return 1;
        }
        sound_play(&s, id, 0, period, 63, 0);
        double secs = s.sample_len[id] / sound_rate(period);
        printf("id %d: %u bytes played, period 0x%X = %.0f Hz, %.2f s\n", id, s.sample_len[id], period, sound_rate(period), secs);
        rc = wav(&s, (int)(secs * 50) + 5, argv[argc - 1]);
    } else if ((!strcmp(cmd, "song") || !strcmp(cmd, "length")) && argc >= 4) {
        int id = (int)strtol(argv[3], NULL, 0);
        if (sound_load(&s, id) != 2) {
            fprintf(stderr, "[FAIL] id %d is not a music bank\n", id);
            return 1;
        }
        if (music_start(&s.music, (const uint8_t *)s.sample[id], s.file_len[id])) {
            fprintf(stderr, "[FAIL] id %d: module does not parse\n", id);
            return 1;
        }
        printf("%s: tempo %d, %d rows\n", s.music.m.name, s.music.m.tempo, s.music.m.rows);
        if (!strcmp(cmd, "length")) {
            int t = 0;
            while (!s.music.wraps && t < 50 * 600) music_tick(&s.music, s.voice), t++;
            printf("length %d ticks = %.1f s\n", t, t / 50.0);
            return 0;
        }
        if (argc < 6) return 2;
        rc = wav(&s, (int)(atof(argv[4]) * 50), argv[5]);
    } else if (!strcmp(cmd, "module") && argc >= 6) {
        Blob b;
        if (blob_read(argv[3], &b) || music_start(&s.music, b.data, b.size)) {
            fprintf(stderr, "[FAIL] %s: not a module\n", argv[3]);
            return 1;
        }
        printf("%s: tempo %d, %d rows\n", s.music.m.name, s.music.m.tempo, s.music.m.rows);
        rc = wav(&s, (int)(atof(argv[4]) * 50), argv[5]);
        blob_free(&b);
    }
    sound_free(&s);
    return rc;
}
