/* Exercise real loaders with bounded, synthetic libc failures. The substitutions apply only to this target. */
#undef fopen
#undef fclose
#undef fseek
#undef ftell
#undef fread
#undef malloc
#undef calloc
#undef free
#undef getenv
#undef atexit
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "program.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; \
} } while (0)

enum { GOOD, END_FAIL, TELL_FAIL, START_FAIL, READ_FAIL, ALLOC_FAIL, EMPTY };
static int scenario, closes, seeks, reads, allocations;
static int map_mode, calloc_calls, fail_calloc, frees;
static int font_mode, fail_malloc;
static unsigned char font_bytes[288] = {'P', 'C', 0, 2};
static void *buffers[2];
static void (*exit_callback)(void);
static const unsigned char bytes[] = {0x14, 0x82, 0x00, 0xFF};

FILE *loader_test_fopen(const char *path, const char *mode) {
    (void)path;
    FILE *f = tmpfile();
    if (!f) abort();
    if (strcmp(mode, "rb") == 0 && scenario != EMPTY) {
        const unsigned char *data = font_mode ? font_bytes : bytes;
        size_t n = font_mode ? sizeof font_bytes : sizeof bytes;
        if (fwrite(data, 1, n, f) != n) abort();
        rewind(f);
    }
    return f;
}
int loader_test_fclose(FILE *f) {
    closes++;
    return fclose(f);
}
int loader_test_fseek(FILE *f, long offset, int whence) {
    seeks++;
    if ((scenario == END_FAIL && whence == SEEK_END) ||
        (scenario == START_FAIL && whence == SEEK_SET)) return -1;
    return fseek(f, offset, whence);
}
long loader_test_ftell(FILE *f) { return scenario == TELL_FAIL ? -1 : ftell(f); }
size_t loader_test_fread(void *ptr, size_t size, size_t count, FILE *f) {
    reads++;
    /* A missing ftell guard must fail the test without making a SIZE_MAX read into a one-byte buffer. */
    if (size != 1 || count > (font_mode ? sizeof font_bytes : sizeof bytes)) return 0;
    if (scenario == READ_FAIL) return fread(ptr, size, count - 1, f);
    return fread(ptr, size, count, f);
}
void *loader_test_malloc(size_t size) {
    allocations++;
    if (scenario == ALLOC_FAIL || allocations == fail_malloc ||
        size > (font_mode ? sizeof font_bytes : sizeof bytes)) return NULL;
    return malloc(size);
}
void *loader_test_calloc(size_t count, size_t size) {
    calloc_calls++;
    if (calloc_calls == fail_calloc) return NULL;
    if (calloc_calls > 2 || count > (1u << 20) || size > sizeof(uint64_t)) abort();
    void *p = calloc(count, size);
    if (!p) abort();
    buffers[calloc_calls - 1] = p;
    return p;
}
void loader_test_free(void *p) {
    if (p) {
        frees++;
        for (int i = 0; i < 2; i++) if (buffers[i] == p) buffers[i] = NULL;
    }
    free(p);
}
char *loader_test_getenv(const char *name) {
    return map_mode && strcmp(name, "BIING_MAP_LOG") == 0 ? "synthetic-map-log" : NULL;
}
int loader_test_atexit(void (*fn)(void)) {
    CHECK(exit_callback == NULL);
    exit_callback = fn;
    return 0;
}

static void test_blob(void) {
    for (scenario = GOOD; scenario <= EMPTY; scenario++) {
        closes = seeks = reads = allocations = frees = 0;
        Blob b = {(uint8_t *)bytes, 123};
        int result = blob_read("synthetic-blob", &b);
        CHECK(closes == 1);
        if (scenario == GOOD || scenario == EMPTY) {
            CHECK(result == 0 && b.data != NULL);
            CHECK(b.size == (scenario == EMPTY ? 0 : sizeof bytes));
            if (scenario == GOOD && b.data) CHECK(memcmp(b.data, bytes, sizeof bytes) == 0);
            CHECK(seeks == 2 && allocations == 1 && reads == 1);
            blob_free(&b);
            CHECK(frees == 1 && b.data == NULL && b.size == 0);
        } else {
            CHECK(result == -1 && b.data == NULL && b.size == 0);
            CHECK(strstr(asset_error(), "synthetic-blob") != NULL);
            if (scenario <= START_FAIL) CHECK(allocations == 0 && reads == 0);
            if (scenario == TELL_FAIL) CHECK(strstr(asset_error(), "cannot size") != NULL);
            if (scenario == END_FAIL || scenario == START_FAIL)
                CHECK(strstr(asset_error(), "cannot seek") != NULL);
            if (scenario == READ_FAIL) CHECK(allocations == 1 && reads == 1 && frees == 1);
            if (scenario == ALLOC_FAIL) CHECK(allocations == 1 && reads == 0 && frees == 0);
        }
    }
}

static void test_map(int fail_at) {
    map_mode = 1;
    fail_calloc = fail_at;
    Program p = {0};
    CHECK(program_at(&p, 0x1234) == 0x1234);
    CHECK(calloc_calls == 2);
    if (fail_at) {
        CHECK(closes == 1 && frees == 1);
        CHECK(buffers[0] == NULL && buffers[1] == NULL);
        CHECK(exit_callback == NULL);
    } else {
        CHECK(closes == 0 && frees == 0);
        CHECK(buffers[0] != NULL && buffers[1] != NULL && exit_callback != NULL);
    }
    /* Optional logging failure must leave reads usable and must not retry or close the stream twice. */
    CHECK(program_at(&p, 0x5678) == 0x5678);
    CHECK(calloc_calls == 2 && closes == (fail_at ? 1 : 0));
    if (exit_callback) exit_callback();
    CHECK(closes == 1);
    /* Successful buffers normally live until process exit; release them here for leak-checking this harness. */
    for (int i = 0; i < 2; i++) free(buffers[i]);
}

static void test_font_allocations(void) {
    font_mode = 1;
    scenario = GOOD;
    /* Two 3x2 glyphs followed by the header glyph: fail each allocation independently. */
    font_bytes[18] = 1, font_bytes[19] = 16, font_bytes[273] = 3;
    font_bytes[20] = 1, font_bytes[21] = 24, font_bytes[281] = 3;
    for (int load = 0; load < 2; load++) {
        for (fail_malloc = 0; fail_malloc <= 3 + load; fail_malloc++) {
            closes = seeks = reads = allocations = frees = 0;
            Font f = {0};
            int rc = load ? font_load(&f, "synthetic-font") : font_parse(&f, font_bytes, sizeof font_bytes);
            if (fail_malloc) {
                CHECK(rc == -1 && allocations == fail_malloc);
                CHECK(frees == fail_malloc - 1); /* every successful allocation already released */
                CHECK(strstr(asset_error(), load && fail_malloc == 1 ? "cannot read" : "out of memory") != NULL);
                CHECK(f.height == 0 && f.head == NULL && f.head_w == 0 && f.head_h == 0);
                for (int k = 0; k < 128; k++) CHECK(f.glyph[k] == NULL && f.width[k] == 0);
            } else {
                CHECK(rc == 0 && allocations == 3 + load && frees == load);
                CHECK(f.glyph[1] != NULL && f.glyph[2] != NULL && f.head != NULL);
                CHECK(f.height == 2 && f.width[1] == 3 && f.width[2] == 3);
            }
            font_free(&f);
            CHECK(frees == allocations - (fail_malloc != 0));
            CHECK(closes == load);
        }
    }
    font_mode = fail_malloc = 0;
}

int main(int argc, char **argv) {
    if (argc == 1) { test_blob(); test_font_allocations(); }
    else if (argc == 2 && strcmp(argv[1], "font") == 0) test_font_allocations();
    else test_map(atoi(argv[1]));
    if (failures) return 1;
    puts("[OK] loader I/O failures");
    return 0;
}
