/* The switch for the opt-in fixes of the original's bugs (docs/fixes.md, section 1). */
#include "fixes.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t g_mask; /* bit n-1 = FIX-n; 0 = the original (default) */

int fix_switchable(int n) { return n >= 1 && n <= FIXES_LAST && (FIXES_ALL >> (n - 1) & 1u); }

int fix_on(int n) { return fix_switchable(n) && (g_mask >> (n - 1) & 1u); }

void fixes_set(uint64_t mask) { g_mask = mask; }

uint64_t fixes_mask(void) { return g_mask; }

static int same_word(const char *a, size_t n, const char *b) {
    if (strlen(b) != n) return 0;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)a[i]) != b[i]) return 0;
    return 1;
}

static void copy_bad(char *bad, size_t badn, const char *w, size_t n) {
    if (!bad || !badn) return;
    if (n >= badn) n = badn - 1;
    memcpy(bad, w, n);
    bad[n] = 0;
}

int fixes_parse(const char *value, uint64_t *mask, char *bad, size_t badn) {
    *mask = 0;
    if (!value) return 0;
    size_t len = strlen(value);
    if (len == 0 || same_word(value, len, "0") || same_word(value, len, "off")) return 0;
    if (same_word(value, len, "all") || same_word(value, len, "1") || same_word(value, len, "on")) {
        *mask = FIXES_ALL;
        return 0;
    }
    uint64_t m = 0;
    const char *p = value;
    for (;;) {
        const char *end = strchr(p, ',');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        const char *w = p;
        size_t wn = n;
        while (wn && isspace((unsigned char)*w)) w++, wn--;
        while (wn && isspace((unsigned char)w[wn - 1])) wn--;
        const char *d = w;
        size_t dn = wn;
        if (dn > 4 && same_word(d, 4, "fix-")) d += 4, dn -= 4;
        int num = 0, ok = dn > 0 && dn <= 2;
        for (size_t i = 0; ok && i < dn; i++) {
            if (!isdigit((unsigned char)d[i])) ok = 0;
            else num = num * 10 + (d[i] - '0');
        }
        if (!ok || !fix_switchable(num)) {
            copy_bad(bad, badn, w, wn);
            *mask = 0;
            return -1;
        }
        m |= 1ull << (num - 1);
        if (!end) break;
        p = end + 1;
    }
    *mask = m;
    return 0;
}

void fixes_describe(uint64_t mask, char *out, size_t n) {
    if (!n) return;
    out[0] = 0;
    if (!mask) {
        snprintf(out, n, "off");
        return;
    }
    if (mask == FIXES_ALL) { /* "all (FIX-1 to FIX-32, FIX-47, ...)": runs of three or more as "a to b" */
        size_t at = (size_t)snprintf(out, n, "all (");
        for (int i = 1; i <= FIXES_LAST && at < n; i++) {
            if (!(mask >> (i - 1) & 1u)) continue;
            int j = i; /* the run i..j */
            while (j < FIXES_LAST && (mask >> j & 1u)) j++;
            const char *sep = at > 5 ? ", " : "";
            int w = j - i >= 2 ? snprintf(out + at, n - at, "%sFIX-%d to FIX-%d", sep, i, j)
                               : snprintf(out + at, n - at, "%sFIX-%d", sep, i);
            if (w < 0) break;
            at += (size_t)w;
            if (j - i >= 2) i = j;
        }
        if (at < n) snprintf(out + at, n - at, ")");
        return;
    }
    size_t at = 0;
    for (int i = 1; i <= FIXES_LAST; i++) {
        if (!(mask >> (i - 1) & 1u)) continue;
        int w = snprintf(out + at, n - at, "%sFIX-%d", at ? ", " : "", i);
        if (w < 0 || (size_t)w >= n - at) break;
        at += (size_t)w;
    }
}

int fixes_init(int all, int announce) {
    const char *v = getenv("BIING_FIXES");
    uint64_t m;
    char bad[64];
    if (fixes_parse(v, &m, bad, sizeof bad)) {
        fprintf(stderr, "[ERROR] BIING_FIXES: unknown fix '%s'\n", bad);
        return 2;
    }
    if (all) m = FIXES_ALL;
    fixes_set(m);
    if (!announce && !m) return 0;
    char text[400];
    fixes_describe(m, text, sizeof text);
    fprintf(stderr, "[INFO] fixes of the original's bugs: %s\n", text);
    return 0;
}
