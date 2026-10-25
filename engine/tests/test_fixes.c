/* The switch of the opt-in fixes (docs/fixes.md, section 1): BIING_FIXES values, lists, errors and the default. The
 * fixes themselves are tested next to the code they change (test_*.c, "FIX-n"). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fixes.h"

static int failures;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                            \
        }                                                                          \
    } while (0)

#define ALL FIXES_ALL

#ifdef _WIN32 /* MSYS2 clang64: no setenv / unsetenv */
static int setenv(const char *k, const char *v, int o) { return (void)o, _putenv_s(k, v); }
static int unsetenv(const char *k) { return _putenv_s(k, ""); }
#endif

static int parse(const char *v, uint64_t *m, char *bad) { return fixes_parse(v, m, bad, 64); }

static void test_values(void) {
    uint64_t m = 123;
    char bad[64];
    CHECK(parse(NULL, &m, bad) == 0 && m == 0); /* unset: off */
    CHECK(parse("", &m, bad) == 0 && m == 0);
    CHECK(parse("0", &m, bad) == 0 && m == 0);
    CHECK(parse("off", &m, bad) == 0 && m == 0);
    CHECK(parse("OFF", &m, bad) == 0 && m == 0);
    CHECK(parse("all", &m, bad) == 0 && m == ALL);
    CHECK(parse("1", &m, bad) == 0 && m == ALL); /* "1" is every fix, FIX-1 alone is "FIX-1" */
    CHECK(parse("on", &m, bad) == 0 && m == ALL);
    CHECK(parse("All", &m, bad) == 0 && m == ALL);
}

static void test_lists(void) {
    uint64_t m;
    char bad[64];
    CHECK(parse("1,8,24", &m, bad) == 0 && m == (1u << 0 | 1u << 7 | 1u << 23));
    CHECK(parse("8", &m, bad) == 0 && m == 1u << 7);
    CHECK(parse("FIX-8", &m, bad) == 0 && m == 1u << 7);
    CHECK(parse("fix-1", &m, bad) == 0 && m == 1u);
    CHECK(parse("FIX-1,FIX-32", &m, bad) == 0 && m == (1u | 1u << 31));
    CHECK(parse(" 8 , 24 ", &m, bad) == 0 && m == (1u << 7 | 1u << 23));
    CHECK(parse("32,2", &m, bad) == 0 && m == (1u << 31 | 1u << 1));
    CHECK(parse("8,8", &m, bad) == 0 && m == 1u << 7);
    CHECK(parse("47", &m, bad) == 0 && m == 1ull << 46); /* FIX-47, after the always-on numbers */
    CHECK(parse("FIX-47,1", &m, bad) == 0 && m == (1ull << 46 | 1u));
    CHECK(parse("48", &m, bad) == 0 && m == 1ull << 47);
    CHECK(parse("FIX-48,1", &m, bad) == 0 && m == (1ull << 47 | 1u));
    CHECK(parse("49", &m, bad) == 0 && m == 1ull << 48);
    CHECK(parse("53,54", &m, bad) == 0 && m == (1ull << 52 | 1ull << 53)); /* FIX-53, FIX-54 (SIM-62, SIM-55) */
}

static void test_errors(void) {
    uint64_t m = 7;
    char bad[64];
    CHECK(parse("33", &m, bad) == -1 && !strcmp(bad, "33") && m == 0); /* the always-on guards are not switched */
    CHECK(parse("1,46", &m, bad) == -1 && !strcmp(bad, "46"));
    CHECK(parse("x", &m, bad) == -1 && !strcmp(bad, "x"));
    CHECK(parse("1,8,yes", &m, bad) == -1 && !strcmp(bad, "yes"));
    CHECK(parse("1,", &m, bad) == -1 && !strcmp(bad, ""));
    CHECK(parse("FIX-", &m, bad) == -1 && !strcmp(bad, "FIX-"));
    CHECK(parse("FIX-0", &m, bad) == -1);
    CHECK(parse("-1", &m, bad) == -1);
    CHECK(parse("100", &m, bad) == -1);
    CHECK(parse("50", &m, bad) == -1 && !strcmp(bad, "50"));
    CHECK(parse("55", &m, bad) == -1 && !strcmp(bad, "55"));
    CHECK(parse("true", &m, bad) == -1 && !strcmp(bad, "true"));
}

static void test_describe_and_on(void) {
    char t[400];
    fixes_describe(0, t, sizeof t);
    CHECK(!strcmp(t, "off"));
    fixes_describe(1u << 0 | 1u << 7 | 1u << 23, t, sizeof t);
    CHECK(!strcmp(t, "FIX-1, FIX-8, FIX-24"));
    fixes_describe(ALL, t, sizeof t);
    CHECK(!strcmp(t, "all (FIX-1 to FIX-32, FIX-47 to FIX-49, FIX-53, FIX-54)"));
    fixes_describe(1ull << 47 | 1u << 1, t, sizeof t);
    CHECK(!strcmp(t, "FIX-2, FIX-48"));
    fixes_set(1u << 7);
    CHECK(fix_on(8) && !fix_on(7) && !fix_on(9) && !fix_on(0) && !fix_on(33) && !fix_on(-1));
    fixes_set(ALL);
    CHECK(fix_on(1) && fix_on(32) && !fix_on(33) && !fix_on(46) && fix_on(47) && fix_on(48) && fix_on(49) && !fix_on(50) && fix_on(53) && fix_on(54) && !fix_on(55));
    fixes_set(~0ull); /* bits of numbers that cannot be switched read off */
    CHECK(!fix_on(33) && !fix_on(46) && !fix_on(64) && fix_on(47) && fix_on(48));
    fixes_set(0);
    for (int n = 1; n <= FIXES_LAST; n++) CHECK(!fix_on(n));
}

/* fixes_init: the default (unset) is off, an error returns 2, the window build's --fixes forces all */
static void test_init(void) {
    unsetenv("BIING_FIXES");
    fixes_set(ALL);
    CHECK(fixes_init(0, 0) == 0 && fixes_mask() == 0);
    setenv("BIING_FIXES", "1,8,24", 1);
    CHECK(fixes_init(0, 0) == 0 && fixes_mask() == (1u | 1u << 7 | 1u << 23));
    CHECK(fixes_init(1, 0) == 0 && fixes_mask() == ALL);
    setenv("BIING_FIXES", "bogus", 1);
    fixes_set(0);
    CHECK(fixes_init(0, 0) == 2 && fixes_mask() == 0);
    CHECK(fixes_init(1, 0) == 2); /* a misspelt variable is an error even with --fixes */
    unsetenv("BIING_FIXES");
    CHECK(fixes_init(1, 0) == 0 && fixes_mask() == ALL);
    fixes_set(0);
}

int main(void) {
    test_values();
    test_lists();
    test_errors();
    test_describe_and_on();
    test_init();
    if (failures) {
        fprintf(stderr, "[FAIL] %d check(s) failed\n", failures);
        return 1;
    }
    printf("[OK] fixes switch\n");
    return 0;
}
