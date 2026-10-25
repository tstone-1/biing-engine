/* The opt-in fixes of the original's bugs (docs/fixes.md, AGENTS.md "Bug fixes are opt-in"). The default is the
 * original, bugs included; BIING_FIXES (or `biing <dir> --fixes`) turns on the switchable fixes (FIXES_ALL). Every fix site
 * in the engine tests fix_on(n) and carries a comment "FIX-n (docs/fixes.md)". The always-on guards FIX-33 to FIX-46
 * never test the switch. The switch is not saved and changes no file format. */
#ifndef BIING_FIXES_H
#define BIING_FIXES_H

#include <stddef.h>
#include <stdint.h>

#define FIXES_LAST 54 /* the highest FIX number; the switchable ones: fix_switchable */
/* FIX-1 .. FIX-32, FIX-47 .. FIX-49, FIX-53 and FIX-54 */
#define FIXES_ALL (0xFFFFFFFFull | 1ull << 46 | 1ull << 47 | 1ull << 48 | 1ull << 52 | 1ull << 53)

/* 1 when FIX-n can be switched (a bit of FIXES_ALL), else 0. */
int fix_switchable(int n);

/* 1 when FIX-n is on (a switchable n), else 0. */
int fix_on(int n);

/* Sets the mask of fixes on (bit n-1 = FIX-n); for the programs' start and the tests. */
void fixes_set(uint64_t mask);
uint64_t fixes_mask(void);

/* Parses a BIING_FIXES value: NULL, "", "0", "off" = none; "all", "1", "on" = all; else a comma list of switchable
 * numbers (each also as "FIX-n"). Returns 0 and the mask, or -1 with the offending word in bad (NUL-terminated). */
int fixes_parse(const char *value, uint64_t *mask, char *bad, size_t badn);

/* "off" or "FIX-1, FIX-8, FIX-24" */
void fixes_describe(uint64_t mask, char *out, size_t n);

/* A program's start: reads BIING_FIXES (all = 1 forces every fix, the window build's --fixes), sets the mask and
 * prints "[INFO] fixes of the original's bugs: ..." on stderr (always with announce, else only when a fix is on, so
 * the default output of the headless tools stays as it was). Returns 0, or 2 after printing
 * "[ERROR] BIING_FIXES: unknown fix '<word>'" (the caller exits with it). */
int fixes_init(int all, int announce);

#endif
