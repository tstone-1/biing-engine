/* Data read from the user's BIPRO.EXE program image: screen scripts, the character map, start values.
 * Only tables are read (docs/screens.md, docs/gui.md); no code from the program is used.
 *
 * Three builds of the program exist (docs/editions.md): the disk version 1.4, the GOG German CD and the GOG English
 * CD. Every program offset in the engine is an offset of the disk build. program_load identifies the build (file size
 * and CRC-32) and every read of the image maps its disk offset through that build's measured segment table
 * (program_at; docs/editions.md sections 3.2 and 3.5, generated into editions_tables.h). Values read OUT of the image
 * that are themselves offsets (the screen table, the sheet and recipe pointer tables) are offsets of the build: they
 * are read with the program_raw_* accessors, unmapped. Routine addresses in the screen scripts go the other way,
 * build -> disk, once in script_parse, so the dispatch on disk addresses stays the same for every build. */
#ifndef BIING_PROGRAM_H
#define BIING_PROGRAM_H

#include "assets.h"

#define PROGRAM_IMAGE_OFFSET 0x11C0 /* file offset of program offset 0 in the disk build */
#define SCREEN_COUNT 162 /* entry 0 unused */
#define MAX_RECORDS 96
#define PROGRAM_NOWHERE 0xFFFFFFFFu /* program_at: the disk offset has no counterpart in this build */
/* A routine of a CD build that has no disk counterpart: the golf flag hotspot (screen 42), which hands over to the
 * bought-in golf game (AGENTS.md, docs/programs.md). Above every program offset, so it never meets a disk routine. */
#define ROUTINE_CD_GOLF 0xC0000001u
#define ROUTINE_UNMAPPED 0x80000000u /* bit set on a script routine the build table cannot translate */

enum { OP_DRAW = 1, OP_CONDITION = 8, OP_FRAME = 9, OP_HOTSPOT = 16, OP_END = 17 };

typedef struct {
    uint16_t op;
    uint16_t image; /* OP_DRAW */
    int16_t dx, dy; /* OP_DRAW */
    uint16_t flag; /* OP_CONDITION: offset into the player flag block */
    /* OP_HOTSPOT */
    uint16_t id;
    int16_t ax, ay, aw, ah; /* hit-area adjustments */
    uint16_t hover;
    int16_t hover_dx, hover_dy;
    uint16_t normal;
    int16_t normal_dx, normal_dy;
    uint16_t target; /* screen, or 0 */
    uint32_t click, enter, leave; /* routines (disk program offsets), or 0 */
} Record;

typedef struct {
    uint32_t frame; /* per-frame routine (disk program offset), or 0 */
    int count;
    Record rec[MAX_RECORDS];
} Script;

/* One build of BIPRO.EXE, as measured (docs/editions.md). Segments map disk ranges [disk, end) to build + (x - disk);
 * points place single table starts the segments cannot (translated or moved tables); routines translate routine
 * entries that sit on a change point, build -> disk. A build with no segments is the disk build (identity). */
typedef struct {
    uint32_t disk, end, build;
} ProgramSegment;
typedef struct {
    uint32_t disk, build;
} ProgramPoint;
typedef struct {
    uint32_t build, disk;
} ProgramRoutine;
typedef struct {
    const char *name;
    uint32_t file_size, crc32, base; /* base: file offset of program offset 0 */
    const char *texts, *data; /* the text file and the data file this build reads */
    uint32_t charmap_crc; /* CRC-32 of the 224 character-map bytes from the mapped 0xB541 */
    const ProgramSegment *segments;
    int segment_count;
    const ProgramPoint *points;
    int point_count;
    const ProgramRoutine *routines;
    int routine_count;
    uint32_t heap_shift; /* added to the original's heap addresses (the random generator's record inputs, OTH-DE-14) */
} ProgramBuild;

/* The edition a build belongs to: selects the CD builds' rule changes (docs/editions.md section 7), by the build's name
 * in docs/editions.md section 3.5 (program_edition_of). */
typedef enum { EDITION_DISK = 0, EDITION_GERMAN_CD = 1, EDITION_ENGLISH_CD = 2 } Edition;

typedef struct {
    Blob image; /* BIPRO.EXE from the build's base on */
    Script screens[SCREEN_COUNT];
    const uint8_t *charmap; /* 256 bytes: character code -> glyph slot */
    const ProgramBuild *build; /* NULL: a synthetic image laid out like the disk build (the tests) */
    Edition edition; /* the edition's game rules; EDITION_DISK for a synthetic image (tests may set another) */
} Program;

/* whether the program is one of the two CD builds (rules both share, docs/editions.md 7.2) */
static inline int program_cd(const Program *p) { return p && p->edition != EDITION_DISK; }
static inline int program_german_cd(const Program *p) { return p && p->edition == EDITION_GERMAN_CD; }
static inline int program_english(const Program *p) { return p && p->edition == EDITION_ENGLISH_CD; }

/* The supported builds (generated from docs/editions.md) and how many there are. */
extern const ProgramBuild PROGRAM_BUILDS[];
extern const int PROGRAM_BUILD_COUNT;

int program_load(Program *p, const char *exe_path);
/* program_load against another build list (the tests' synthetic builds). */
int program_load_builds(Program *p, const char *exe_path, const ProgramBuild *builds, int count);
/* The edition of a build by its name ("GOG German CD", "GOG English CD"; anything else plays the disk rules). */
Edition program_edition_of(const ProgramBuild *b);
/* The build whose size and CRC-32 match the file bytes, or NULL. */
const ProgramBuild *program_identify(const uint8_t *file, size_t size, const ProgramBuild *builds, int count);
/* The build of the BIPRO.EXE at the path (of PROGRAM_BUILDS), or NULL with asset_error() set. */
const ProgramBuild *program_identify_file(const char *exe_path);
uint32_t program_crc32(const uint8_t *p, size_t n);

/* disk offset -> offset in the build (PROGRAM_NOWHERE when the build has no counterpart) */
uint32_t program_at(const Program *p, uint32_t disk);
uint32_t build_at(const ProgramBuild *b, uint32_t disk);
/* routine address of the build -> disk routine address (ROUTINE_UNMAPPED | build when none) */
uint32_t build_routine(const ProgramBuild *b, uint32_t build);

/* Reads at a disk table start plus an index: the table start is mapped, the index is added in the build (a table is
 * contiguous in every build, also where its contents were translated). 0 outside the image. */
int program_u8(const Program *p, uint32_t table, uint32_t index);
uint16_t program_u16(const Program *p, uint32_t off); /* = program_word(p, off, 0) */
uint16_t program_word(const Program *p, uint32_t table, uint32_t index);
uint32_t program_dword(const Program *p, uint32_t table, uint32_t index);
/* The script address of screen 1-161 as the build's screen table holds it (an offset of the build); 0 otherwise. */
uint32_t program_screen_script(const Program *p, int screen);
/* Pointer to a disk table in the image; a zero page when the table is missing in this build. At most
 * PROGRAM_TABLE_MAX bytes are read from it by any caller. */
#define PROGRAM_TABLE_MAX 0x10000
const uint8_t *program_table(const Program *p, uint32_t table);

/* Offsets of the build itself (values read out of the image): no mapping. */
int program_raw_u8(const Program *p, uint32_t off);
uint16_t program_raw_u16(const Program *p, uint32_t off);
uint32_t program_raw_u32(const Program *p, uint32_t off);
/* Pointer to a build offset and the bytes left in the image from it (NULL, 0 outside). */
const uint8_t *program_raw_ptr(const Program *p, uint32_t off, size_t *left);
/* whether n bytes from ptr (a pointer from program_table) lie inside the image */
int program_contains(const Program *p, const uint8_t *ptr, size_t n);

/* Parses one script starting at a program offset of the build; routine fields are translated to disk offsets
 * through `build` (NULL: unchanged). Used by program_load and the tests. */
int script_parse(const uint8_t *image, size_t size, uint32_t at, Script *s);
int script_parse_build(const uint8_t *image, size_t size, uint32_t at, uint32_t record_lengths,
                       const ProgramBuild *build, Script *s);
void program_free(Program *p);

#endif
