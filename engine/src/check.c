/* biing-check <BIING!-dir>: decodes every asset the engine reads and prints one line per item:
 *   img <index> <w> <h> <flags> <fnv1a64 of the pixels>
 *   txt <index> <fnv1a64 of the entry bytes>
 *   pool <pool> <index> <fnv1a64>      and   rec patients|staff <fnv1a64 of the whole table>
 *   fnt <file> <height> <slot> <width> <fnv1a64 of the row-major glyph>
 *   spr <L|N>/<name> <w> <h> <x> <y> <flags> <palette entries> <fnv1a64 of the pixels> <fnv1a64 of the palette>
 *       (every sprite with header in DATA/L and DATA/N, names sorted; the palette is the file's last 96 or 768 bytes)
 * tools/engine_parity.py prints the same lines from the Python decoders; diff the two outputs.
 *
 * biing-check at <BIING!-dir> <hex disk offset> ...: where each disk offset lies in the directory's program build
 * (program_at) and the 8 bytes there.  biing-check scripts <BIING!-dir>: every screen script as the engine reads it,
 * routines as disk offsets, to diff the editions (docs/editions.md).
 *
 * biing-check vpx <file.VPX> <out-prefix> <every>: decodes a tutorial video with the engine's decoder (vpx.c) and
 * writes frame n (numbered from 1) as <out-prefix>_<nnnnn>.ppm when n % every == 1 (every 1: all), in the file's
 * 8-bit palette, for comparing with tools/biing_cdrom.py vpx --every; prints one line per frame: vpx <n> <fnv1a64>. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>

#include "assets.h"
#include "intro.h"
#include "program.h"
#include "vpx.h"

static uint64_t fnv(const uint8_t *p, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}

static int cmp_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/* The sprites with header of DATA/<dir>: every file without an extension except N/801 (the tile map) and the IFF
 * PBM and PHAS files, in name order. */
static int check_sprites(const char *root, const char *dir) {
    char path[1200];
    snprintf(path, sizeof path, "%s/DATA/%s", root, dir);
    DIR *d = opendir(path);
    if (!d) {
        fprintf(stderr, "[FAIL] cannot list %s\n", path);
        return 1;
    }
    char *names[512];
    int n = 0, failed = 0;
    for (struct dirent *e; (e = readdir(d)) && n < 512;)
        if (e->d_name[0] != '.' && !strchr(e->d_name, '.')) names[n++] = strdup(e->d_name);
    closedir(d);
    qsort(names, (size_t)n, sizeof *names, cmp_name);
    for (int i = 0; i < n; i++) {
        snprintf(path, sizeof path, "%s/DATA/%s/%s", root, dir, names[i]);
        Blob b;
        if (!strcmp(dir, "N") && !strcmp(names[i], "801")) goto next;
        if (blob_read(path, &b)) {
            failed++;
            goto next;
        }
        if (b.size >= 4 && (!memcmp(b.data, "FORM", 4) || !memcmp(b.data, "PHAS", 4))) {
            blob_free(&b);
            goto next;
        }
        HSprite s;
        if (hsprite_parse(&s, b.data, b.size)) {
            fprintf(stderr, "[FAIL] %s/%s: %s\n", dir, names[i], asset_error());
            failed++;
        } else {
            printf("spr %s/%s %d %d %d %d %d %d %016llx %016llx\n", dir, names[i], s.w, s.h, s.x, s.y, s.flags, s.pal_n,
                   (unsigned long long)fnv(s.px, (size_t)s.w * s.h), (unsigned long long)fnv(s.pal, (size_t)s.pal_n * 3));
            hsprite_free(&s);
        }
        blob_free(&b);
    next:
        free(names[i]);
    }
    return failed;
}

static int check_vpx(const char *file, const char *prefix, int every) {
    Blob b;
    Vpx v;
    if (blob_read(file, &b) || vpx_parse(&v, b.data, b.size)) {
        fprintf(stderr, "[FAIL] %s\n", asset_error());
        return 1;
    }
    static uint8_t buf[VPX_W * VPX_H];
    memset(buf, VPX_FILL, sizeof buf);
    for (int k = 0; k < v.nframes; k++) {
        if (vpx_decode(buf, v.frame[k].code, v.frame[k].data, v.frame[k].n, k & 1) != (long)v.frame[k].n)
            fprintf(stderr, "[WARN] frame %d: not all VIDE bytes used\n", k + 1);
        printf("vpx %d %016llx\n", k + 1, (unsigned long long)fnv(buf, sizeof buf));
        if (every <= 0 || (k + 1) % every != 1 % every) continue;
        char path[1200];
        snprintf(path, sizeof path, "%s_%05d.ppm", prefix, k + 1);
        FILE *f = fopen(path, "wb");
        if (!f) continue;
        fprintf(f, "P6\n%d %d\n255\n", VPX_W, VPX_H);
        for (int i = 0; i < VPX_W * VPX_H; i++) fwrite(v.pal + 3 * buf[i], 1, 3, f);
        fclose(f);
    }
    vpx_free(&v);
    blob_free(&b);
    return 0;
}

static int load(const char *root, Program *p) {
    Game g;
    char path[1200];
    if (game_open(&g, root)) return fprintf(stderr, "[FAIL] %s\n", asset_error()), 1;
    game_path(&g, "BIPRO.EXE", path, sizeof path);
    if (program_load(p, path)) return fprintf(stderr, "[FAIL] %s\n", asset_error()), 1;
    fprintf(stderr, "[OK] %s\n", p->build->name);
    return 0;
}

/* where disk offsets lie in the directory's build, and the 8 bytes there */
static int check_at(const char *root, int n, char **hex) {
    Program p;
    if (load(root, &p)) return 1;
    for (int i = 0; i < n; i++) {
        uint32_t d = (uint32_t)strtoul(hex[i], NULL, 16), at = program_at(&p, d);
        if (at == PROGRAM_NOWHERE) {
            printf("at %X -\n", (unsigned)d);
            continue;
        }
        printf("at %X %X", (unsigned)d, (unsigned)at);
        for (uint32_t k = 0; k < 8; k++) printf(" %02X", program_raw_u8(&p, at + k));
        printf("\n");
    }
    program_free(&p);
    return 0;
}

/* every screen script as the engine reads it, routines as disk offsets: diff two editions' outputs */
static int check_scripts(const char *root) {
    Program p;
    if (load(root, &p)) return 1;
    for (int i = 1; i < SCREEN_COUNT; i++) {
        const Script *s = &p.screens[i];
        printf("scr %d frame %X records %d\n", i, (unsigned)s->frame, s->count);
        for (int k = 0; k < s->count; k++) {
            const Record *e = &s->rec[k];
            if (e->op == OP_DRAW) printf("  draw %d %d %d\n", e->image, e->dx, e->dy);
            else if (e->op == OP_CONDITION) printf("  cond %X\n", e->flag);
            else
                printf("  hot %d %d %d %d %d %d/%d,%d %d/%d,%d -> %d click %X enter %X leave %X\n", e->id, e->ax, e->ay,
                       e->aw, e->ah, e->hover, e->hover_dx, e->hover_dy, e->normal, e->normal_dx, e->normal_dy,
                       e->target, (unsigned)e->click, (unsigned)e->enter, (unsigned)e->leave);
        }
    }
    program_free(&p);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 5 && !strcmp(argv[1], "vpx")) return check_vpx(argv[2], argv[3], atoi(argv[4]));
    if (argc >= 3 && !strcmp(argv[1], "at")) return check_at(argv[2], argc - 3, argv + 3);
    if (argc == 3 && !strcmp(argv[1], "scripts")) return check_scripts(argv[2]);
    if (argc != 2) {
        fprintf(stderr, "usage: %s <BIING!-dir> | at <BIING!-dir> <hex disk offset> ... | scripts <BIING!-dir> | vpx ...\n",
                argv[0]);
        return 2;
    }
    Game g;
    if (game_open(&g, argv[1])) {
        fprintf(stderr, "[FAIL] %s\n", asset_error());
        return 1;
    }
    /* the program is loaded as the engine loads it (build identification and self-check), so the check fails on a
     * folder the engine would refuse; the text and data files are named by the build (BITEXPZ/BIDATP, English CD
     * BITEXPE/BIDATPE) */
    char exe[1200];
    game_path(&g, "BIPRO.EXE", exe, sizeof exe);
    Program prog;
    if (program_load(&prog, exe)) {
        fprintf(stderr, "[FAIL] %s\n", asset_error());
        return 1;
    }
    const ProgramBuild *build = prog.build; /* a static table entry: stays valid */
    program_free(&prog);
    const char *texts = build->texts, *data = build->data;
    int failed = 0;
    for (int i = 0; i < IXY_COUNT; i++) {
        if (!g.ixy[i].w) continue;
        Image img;
        if (image_load(&g, i, &img)) {
            fprintf(stderr, "[FAIL] %s\n", asset_error());
            failed++;
            continue;
        }
        printf("img %d %d %d %d %016llx\n", i, img.w, img.h, img.flags,
               (unsigned long long)fnv(img.px, (size_t)img.w * img.h));
        image_free(&img);
    }

    char path[1200];
    TextDb t;
    game_path(&g, texts, path, sizeof path);
    if (textdb_load(&t, path)) {
        fprintf(stderr, "[FAIL] %s\n", asset_error());
        return 1;
    }
    for (int i = 0; i < TEXT_COUNT; i++) {
        size_t n;
        const uint8_t *e = textdb_entry(&t, i, &n);
        printf("txt %d %016llx\n", i, (unsigned long long)fnv(e, n));
    }
    textdb_free(&t);

    DataDb d;
    game_path(&g, data, path, sizeof path);
    if (datadb_load(&d, path)) {
        fprintf(stderr, "[FAIL] %s\n", asset_error());
        return 1;
    }
    printf("rec patients %016llx\n", (unsigned long long)fnv(d.patients, (size_t)PATIENT_COUNT * PATIENT_SIZE));
    printf("rec staff %016llx\n", (unsigned long long)fnv(d.staff, (size_t)STAFF_COUNT * STAFF_SIZE));
    for (int p = DAT_PATIENT_NAMES; p < DAT_POOLS_END; p++)
        for (int i = 0; i < d.pools[p - DAT_PATIENT_NAMES].count; i++) {
            size_t n;
            const uint8_t *e = datadb_pool_entry(&d, p, i, &n);
            printf("pool %d %d %016llx\n", p - DAT_PATIENT_NAMES, i, (unsigned long long)fnv(e, n));
        }
    datadb_free(&d);

    static const char *fonts[] = {"KU.FNT", "XFONT3.FNT", "ZEI1.FNT", "ZEI2.FNT", "ZEI3.FNT",
                                  "ZEI4.FNT", "ZEI5.FNT", "N/BI2PC.FNT"};
    for (size_t k = 0; k < sizeof fonts / sizeof *fonts; k++) {
        char rel[64];
        snprintf(rel, sizeof rel, "DATA/%s", fonts[k]);
        game_path(&g, rel, path, sizeof path);
        Font f;
        if (font_load(&f, path)) {
            fprintf(stderr, "[FAIL] %s: %s\n", fonts[k], asset_error());
            failed++;
            continue;
        }
        for (int s = 0; s < 128; s++)
            if (f.width[s])
                printf("fnt %s %d %d %d %016llx\n", fonts[k], f.height, s, f.width[s],
                       (unsigned long long)fnv(f.glyph[s], (size_t)f.width[s] * f.height));
        font_free(&f);
    }
    failed += check_sprites(argv[1], "L");
    failed += check_sprites(argv[1], "N");
    if (failed) fprintf(stderr, "[FAIL] %d items failed (%s)\n", failed, build->name);
    else fprintf(stderr, "[OK] all assets decoded: %s\n", build->name);
    return failed ? 1 : 0;
}
