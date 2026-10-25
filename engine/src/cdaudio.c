/* CD audio tracks for the CD music (docs/cdrom.md, "CD audio music in BIPRO.EXE"); see cdaudio.h. */
#include "cdaudio.h"
#include "assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Output rate of the driver (mixer.h): 1,000,000 / 46 Hz; one output frame advances 44,100 x 46 / 1,000,000 track
 * frames, in 32.32 fixed point. */
#define CD_STEP ((uint64_t)CD_RATE * 46u * 4294967296u / 1000000u)

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static unsigned le16(const uint8_t *p) { return p[0] | p[1] << 8; }

int cd_wav_parse(const uint8_t *d, size_t n, const int16_t **pcm, uint32_t *frames) {
    if (n < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return -1;
    int fmt_ok = 0;
    size_t i = 12;
    while (i + 8 <= n) {
        uint32_t len = le32(d + i + 4);
        const uint8_t *c = d + i + 8;
        if (len > n - i - 8) return -1;
        if (!memcmp(d + i, "fmt ", 4)) {
            if (len < 16) return -1;
            fmt_ok = le16(c) == 1 && le16(c + 2) == 2 && le32(c + 4) == CD_RATE && le16(c + 14) == 16;
        } else if (!memcmp(d + i, "data", 4)) {
            if (!fmt_ok) return -1;
            *pcm = (const int16_t *)(const void *)c;
            *frames = len / 4;
            return 0;
        }
        i += 8 + len + (len & 1);
    }
    return -1;
}

int cd_open(CdAudio *cd, const char *root, int level) {
    memset(cd, 0, sizeof *cd);
    snprintf(cd->dir, sizeof cd->dir, "%s/CDAUDIO", root ? root : ".");
    cd->level = level < 0 ? 0 : level > 15 ? 15 : level;
    char path[1200];
    snprintf(path, sizeof path, "%s/TRACK05.WAV", cd->dir); /* the site map's track, the first song track */
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1; /* the tracks are read at their first play */
}

void cd_free(CdAudio *cd) {
    for (int t = 0; t < CD_TRACKS; t++) free(cd->pcm[t]);
    memset(cd->pcm, 0, sizeof cd->pcm);
}

/* the track's samples, loaded on first use (little-endian bytes copied into host order) */
static int load(CdAudio *cd, int t) {
    if (cd->state[t]) return cd->state[t];
    cd->state[t] = -1;
    char path[1200];
    snprintf(path, sizeof path, "%s/TRACK%02d.WAV", cd->dir, t);
    Blob b;
    if (blob_read(path, &b)) return -1;
    const int16_t *pcm;
    uint32_t frames;
    if (cd_wav_parse(b.data, b.size, &pcm, &frames) == 0 && frames) {
        int16_t *copy = malloc((size_t)frames * 4);
        if (copy) {
            const uint8_t *p = (const uint8_t *)pcm;
            for (size_t k = 0; k < (size_t)frames * 2; k++) copy[k] = (int16_t)(uint16_t)le16(p + 2 * k);
            cd->pcm[t] = copy;
            cd->frames[t] = frames;
            cd->state[t] = 1;
        }
    } else {
        fprintf(stderr, "[ERROR] %s is not a CD audio track (16-bit stereo 44,100 Hz PCM)\n", path);
    }
    blob_free(&b);
    return cd->state[t];
}

void cd_play(CdAudio *cd, int track) {
    cd->track = 0, cd->busy = 0, cd->pos = 0;
    if (track <= 0 || track >= CD_TRACKS || load(cd, track) != 1) return;
    cd->track = track;
    cd->busy = 1;
}

void cd_stop(CdAudio *cd) { cd->busy = 0; }

void cd_render(CdAudio *cd, int16_t *out, int n) {
    if (!cd->busy || !cd->track || !cd->level) {
        if (cd->busy && cd->track) cd->pos += CD_STEP * (uint64_t)n; /* the drive plays on, silent at level 0 */
        if (cd->busy && cd->track && (cd->pos >> 32) >= cd->frames[cd->track]) cd->busy = 0;
        return;
    }
    const int16_t *pcm = cd->pcm[cd->track];
    uint32_t len = cd->frames[cd->track];
    for (int i = 0; i < n; i++) {
        uint32_t f = (uint32_t)(cd->pos >> 32);
        if (f >= len) {
            cd->busy = 0;
            return;
        }
        uint32_t g = f + 1 < len ? f + 1 : f; /* the two track frames the output frame spans, averaged */
        for (int ch = 0; ch < 2; ch++) {
            int v = (pcm[2 * f + ch] + pcm[2 * g + ch]) / 2 * cd->level / 15 + out[2 * i + ch];
            out[2 * i + ch] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        cd->pos += CD_STEP;
    }
    if ((cd->pos >> 32) >= len) cd->busy = 0;
}
