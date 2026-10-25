#include "program.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Table positions (disk build), docs/screens.md "Screen table and script records" and "Fonts and the character
 * map". The character map is mapped from its table proper at 0xB541 (the 32 bytes before it are the end of a string
 * the English build translated, docs/editions.md section 3.1). */
#define SCREEN_TABLE 0xE72F
#define RECORD_LENGTHS 0xB95A /* 18 u16, record length per opcode */
#define CHARMAP_TABLE 0xB541 /* character map from code 32; the map proper starts 32 bytes earlier */
#define CHARMAP_CHECKED 224

#include "editions_tables.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (uint32_t)rd16(p + 2) << 16; }

/* ---- build identification -------------------------------------------------------------------- */

uint32_t program_crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}

const ProgramBuild *program_identify(const uint8_t *file, size_t size, const ProgramBuild *builds, int count) {
    uint32_t crc = 0;
    int have_crc = 0;
    for (int i = 0; i < count; i++) {
        if (builds[i].file_size != size) continue;
        if (!have_crc) crc = program_crc32(file, size), have_crc = 1;
        if (builds[i].crc32 == crc) return &builds[i];
    }
    return NULL;
}

static int unknown_build(const char *path, size_t size, uint32_t crc, const ProgramBuild *builds, int count) {
    char list[400] = "";
    for (int i = 0; i < count; i++) {
        size_t n = strlen(list);
        snprintf(list + n, sizeof list - n, "%s%s (%u bytes)", i ? ", " : "", builds[i].name,
                 (unsigned)builds[i].file_size);
    }
    return asset_fail("%s (%zu bytes, CRC-32 %08X) is not a supported build of the program; supported: %s "
                      "(docs/editions.md)",
                      path, size, (unsigned)crc, list);
}

const ProgramBuild *program_identify_file(const char *exe_path) {
    Blob exe;
    if (blob_read(exe_path, &exe)) return NULL;
    const ProgramBuild *b = program_identify(exe.data, exe.size, PROGRAM_BUILDS, PROGRAM_BUILD_COUNT);
    if (!b) unknown_build(exe_path, exe.size, program_crc32(exe.data, exe.size), PROGRAM_BUILDS, PROGRAM_BUILD_COUNT);
    blob_free(&exe);
    return b;
}

/* ---- offset translation ---------------------------------------------------------------------- */

uint32_t build_at(const ProgramBuild *b, uint32_t disk) {
    if (!b || !b->segment_count) return disk;
    for (int i = 0; i < b->point_count; i++)
        if (b->points[i].disk == disk) return b->points[i].build;
    for (int i = 0; i < b->segment_count; i++) {
        const ProgramSegment *s = &b->segments[i];
        if (disk >= s->disk && disk < s->end) return s->build + (disk - s->disk);
    }
    return PROGRAM_NOWHERE;
}

uint32_t build_routine(const ProgramBuild *b, uint32_t build) {
    if (!b || !b->segment_count) return build;
    for (int i = 0; i < b->routine_count; i++)
        if (b->routines[i].build == build) return b->routines[i].disk;
    for (int i = 0; i < b->segment_count; i++) {
        const ProgramSegment *s = &b->segments[i];
        if (build >= s->build && build - s->build < s->end - s->disk) return s->disk + (build - s->build);
    }
    return ROUTINE_UNMAPPED | build;
}


/* ---- reads ----------------------------------------------------------------------------------- */

/* BIING_MAP_LOG=<file>: every distinct (disk table, index) read through the mapping is appended to the file at exit,
 * one "<table> <index> <build offset>" line each (hex; index "-" for a table pointer), so a script can compare the
 * byte the engine reads in a CD build with the disk build's byte at the same disk offset (docs/editions.md). */
#define MAP_LOG_SLOTS (1u << 20)
static uint64_t *map_log;
static uint32_t *map_log_at;
static FILE *map_log_file;
static void map_log_write(void) {
    for (uint32_t i = 0; i < MAP_LOG_SLOTS; i++) {
        uint64_t k = map_log[i];
        if (!k--) continue;
        uint32_t index = (uint32_t)k;
        if (index == 0xFFFFFFFFu) fprintf(map_log_file, "%X - %X\n", (unsigned)(k >> 32), (unsigned)map_log_at[i]);
        else fprintf(map_log_file, "%X %X %X\n", (unsigned)(k >> 32), (unsigned)index, (unsigned)map_log_at[i]);
    }
    fclose(map_log_file);
}
static void map_log_note(uint32_t table, uint32_t index, uint32_t at) {
    static int checked;
    if (!checked) {
        checked = 1;
        const char *path = getenv("BIING_MAP_LOG");
        if (path && (map_log_file = fopen(path, "a"))) {
            map_log = calloc(MAP_LOG_SLOTS, sizeof *map_log);
            map_log_at = calloc(MAP_LOG_SLOTS, sizeof *map_log_at);
            if (map_log && map_log_at) atexit(map_log_write);
            else {
                fclose(map_log_file);
                free(map_log);
                free(map_log_at);
                map_log_file = NULL;
                map_log = NULL;
                map_log_at = NULL;
            }
        }
    }
    if (!map_log) return;
    uint64_t key = ((uint64_t)table << 32 | index) + 1;
    for (uint32_t h = (uint32_t)(key * 0x9E3779B97F4A7C15ull >> 44), n = 0; n < MAP_LOG_SLOTS; n++, h = (h + 1) & (MAP_LOG_SLOTS - 1)) {
        if (map_log[h] == key) return;
        if (!map_log[h]) {
            map_log[h] = key, map_log_at[h] = at;
            return;
        }
    }
}

uint32_t program_at(const Program *p, uint32_t disk) {
    uint32_t at = build_at(p->build, disk);
    map_log_note(disk, 0xFFFFFFFFu, at);
    return at;
}

/* build offset of table + index, or PROGRAM_NOWHERE */
static uint32_t at_index(const Program *p, uint32_t table, uint32_t index) {
    uint32_t at = build_at(p->build, table);
    map_log_note(table, index, at);
    return at == PROGRAM_NOWHERE ? at : at + index;
}

int program_raw_u8(const Program *p, uint32_t off) { return off < p->image.size ? p->image.data[off] : 0; }
uint16_t program_raw_u16(const Program *p, uint32_t off) {
    return off < p->image.size && p->image.size - off >= 2 ? rd16(p->image.data + off) : 0;
}
uint32_t program_raw_u32(const Program *p, uint32_t off) {
    return off < p->image.size && p->image.size - off >= 4 ? rd32(p->image.data + off) : 0;
}
const uint8_t *program_raw_ptr(const Program *p, uint32_t off, size_t *left) {
    if (off >= p->image.size) {
        if (left) *left = 0;
        return NULL;
    }
    if (left) *left = p->image.size - off;
    return p->image.data + off;
}

int program_contains(const Program *p, const uint8_t *ptr, size_t n) {
    uintptr_t lo = (uintptr_t)p->image.data, at = (uintptr_t)ptr;
    return p->image.data && at >= lo && at - lo <= p->image.size && p->image.size - (at - lo) >= n;
}

int program_u8(const Program *p, uint32_t table, uint32_t index) {
    return program_raw_u8(p, at_index(p, table, index));
}
uint16_t program_word(const Program *p, uint32_t table, uint32_t index) {
    return program_raw_u16(p, at_index(p, table, index));
}
uint32_t program_dword(const Program *p, uint32_t table, uint32_t index) {
    return program_raw_u32(p, at_index(p, table, index));
}
uint16_t program_u16(const Program *p, uint32_t off) { return program_word(p, off, 0); }
uint32_t program_screen_script(const Program *p, int screen) {
    return screen > 0 && screen < SCREEN_COUNT ? program_dword(p, SCREEN_TABLE, 4u * (uint32_t)screen) : 0;
}

const uint8_t *program_table(const Program *p, uint32_t table) {
    static const uint8_t zero[PROGRAM_TABLE_MAX];
    uint32_t at = program_at(p, table);
    return at < p->image.size ? p->image.data + at : zero;
}

/* ---- scripts --------------------------------------------------------------------------------- */

static uint32_t routine_field(const ProgramBuild *b, size_t size, uint32_t v) {
    if (!v || v >= size) return v; /* 0, or not an address (screen 78's frame value 0x90009) */
    return build_routine(b, v);
}

int script_parse_build(const uint8_t *image, size_t size, uint32_t at, uint32_t record_lengths,
                       const ProgramBuild *build, Script *s) {
    memset(s, 0, sizeof *s);
    if (record_lengths >= size || size - record_lengths < 36)
        return asset_fail("record length table missing at 0x%X", (unsigned)record_lengths);
    uint32_t pos = at;
    for (;;) {
        if (pos >= size || size - pos < 2) return asset_fail("script runs past the image at 0x%X", (unsigned)pos);
        uint16_t op = rd16(image + pos);
        uint16_t len = op < 18 ? rd16(image + record_lengths + 2 * op) : 0;
        if (!len) return asset_fail("invalid opcode at 0x%X", (unsigned)pos);
        if (size - pos < len) return asset_fail("record runs past the image at 0x%X", (unsigned)pos);
        const uint8_t *r = image + pos;
        if (op == OP_END) return 0;
        if (op == OP_FRAME) {
            s->frame = routine_field(build, size, rd32(r + 2));
        } else if (op == OP_DRAW || op == OP_CONDITION || op == OP_HOTSPOT) {
            if (s->count == MAX_RECORDS) return asset_fail("too many records at 0x%X", (unsigned)pos);
            Record *e = &s->rec[s->count++];
            memset(e, 0, sizeof *e);
            e->op = op;
            if (op == OP_DRAW) {
                e->image = rd16(r + 2);
                e->dx = (int16_t)rd16(r + 4);
                e->dy = (int16_t)rd16(r + 6);
            } else if (op == OP_CONDITION) {
                e->flag = rd16(r + 2);
            } else {
                e->id = rd16(r + 2);
                e->ax = (int16_t)rd16(r + 4);
                e->ay = (int16_t)rd16(r + 6);
                e->aw = (int16_t)rd16(r + 8);
                e->ah = (int16_t)rd16(r + 10);
                e->hover = rd16(r + 12);
                e->hover_dx = (int16_t)rd16(r + 14);
                e->hover_dy = (int16_t)rd16(r + 16);
                e->normal = rd16(r + 18);
                e->normal_dx = (int16_t)rd16(r + 20);
                e->normal_dy = (int16_t)rd16(r + 22);
                e->target = rd16(r + 40);
                e->click = routine_field(build, size, rd32(r + 42));
                e->enter = routine_field(build, size, rd32(r + 46));
                e->leave = routine_field(build, size, rd32(r + 50));
            }
        }
        /* Other opcodes do not occur in the game's scripts (docs/screens.md); they are skipped. */
        pos += len;
    }
}

int script_parse(const uint8_t *image, size_t size, uint32_t at, Script *s) {
    return script_parse_build(image, size, at, RECORD_LENGTHS, NULL, s);
}

/* ---- loading --------------------------------------------------------------------------------- */

static int unmapped(uint32_t v) { return v != ROUTINE_CD_GOLF && (v & ROUTINE_UNMAPPED); }

/* The load-time self-check (docs/editions.md section 6): the screen table's first script follows the table, every
 * script parses with its routines translated, and the character map is the expected one. A wrong row in the build
 * table shows here instead of as a broken screen later. */
static int load_scripts(Program *p) {
    const Blob *im = &p->image;
    uint32_t table = program_at(p, SCREEN_TABLE), lengths = program_at(p, RECORD_LENGTHS);
    if (table == PROGRAM_NOWHERE || table >= im->size || im->size - table < SCREEN_COUNT * 4)
        return asset_fail("screen table outside the image");
    if (program_raw_u32(p, table + 4) != table + SCREEN_COUNT * 4)
        return asset_fail("self-check: the screen table at 0x%X does not lead to its first script", (unsigned)table);
    for (int i = 1; i < SCREEN_COUNT; i++) {
        uint32_t at = rd32(im->data + table + 4 * i); /* an offset of this build */
        /* Screens 79 and 80 are two-byte stubs with no end record; nothing ever sets them. */
        if (i == 79 || i == 80) continue;
        Script *s = &p->screens[i];
        if (script_parse_build(im->data, im->size, at, lengths, p->build, s)) {
            char msg[520]; /* asset_fail writes the buffer asset_error() returns */
            snprintf(msg, sizeof msg, "%s", asset_error());
            return asset_fail("screen %d: %s", i, msg);
        }
        uint32_t bad = unmapped(s->frame) ? s->frame : 0;
        for (int k = 0; k < s->count && !bad; k++) {
            const Record *e = &s->rec[k];
            bad = unmapped(e->click) ? e->click : unmapped(e->enter) ? e->enter : unmapped(e->leave) ? e->leave : 0;
        }
        if (bad)
            return asset_fail("self-check: screen %d names routine 0x%X of this build, which has no disk counterpart",
                              i, (unsigned)(bad & ~ROUTINE_UNMAPPED));
    }
    return 0;
}

static int load_charmap(Program *p) {
    uint32_t at = program_at(p, CHARMAP_TABLE);
    if (at == PROGRAM_NOWHERE || at < 32 || at >= p->image.size || p->image.size - at < CHARMAP_CHECKED)
        return asset_fail("character map outside the image");
    if (p->build && program_crc32(p->image.data + at, CHARMAP_CHECKED) != p->build->charmap_crc)
        return asset_fail("self-check: the character map at 0x%X is not the expected one", (unsigned)at);
    p->charmap = p->image.data + at - 32;
    return 0;
}

Edition program_edition_of(const ProgramBuild *b) {
    if (b && b->name && !strcmp(b->name, "GOG German CD")) return EDITION_GERMAN_CD;
    if (b && b->name && !strcmp(b->name, "GOG English CD")) return EDITION_ENGLISH_CD;
    return EDITION_DISK;
}

int program_load_builds(Program *p, const char *exe_path, const ProgramBuild *builds, int count) {
    memset(p, 0, sizeof *p);
    Blob exe;
    if (blob_read(exe_path, &exe)) return -1;
    const ProgramBuild *b = program_identify(exe.data, exe.size, builds, count);
    if (!b) {
        uint32_t crc = program_crc32(exe.data, exe.size);
        size_t size = exe.size;
        blob_free(&exe);
        return unknown_build(exe_path, size, crc, builds, count);
    }
    if (exe.size <= b->base) {
        blob_free(&exe);
        return asset_fail("%s: too short for a BIPRO.EXE", exe_path);
    }
    memmove(exe.data, exe.data + b->base, exe.size - b->base);
    exe.size -= b->base;
    p->image = exe;
    p->build = b;
    p->edition = program_edition_of(b);
    if (load_charmap(p) || load_scripts(p)) {
        char msg[600];
        snprintf(msg, sizeof msg, "%s (%s): %s", exe_path, b->name, asset_error());
        program_free(p);
        return asset_fail("%s", msg);
    }
    return 0;
}

int program_load(Program *p, const char *exe_path) {
    return program_load_builds(p, exe_path, PROGRAM_BUILDS, PROGRAM_BUILD_COUNT);
}

void program_free(Program *p) {
    blob_free(&p->image);
    p->build = NULL;
    p->charmap = NULL;
}
