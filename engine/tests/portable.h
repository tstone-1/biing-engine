/* What the tests need that differs on Windows (MSYS2 clang64): mkdir takes no mode there, memmem is missing, and
 * there is no `rm` for system(). */
#ifndef BIING_TESTS_PORTABLE_H
#define BIING_TESTS_PORTABLE_H

#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _WIN32
#include <direct.h>
#define test_mkdir(path) _mkdir(path)
#else
#define test_mkdir(path) mkdir((path), 0755)
#endif

static inline const void *test_memmem(const void *hay, size_t n, const void *needle, size_t m) {
    if (m == 0) return hay;
    for (size_t i = 0; i + m <= n; i++)
        if (memcmp((const char *)hay + i, needle, m) == 0) return (const char *)hay + i;
    return NULL;
}

/* Removes a folder and everything in it; 0 on success. */
static inline int test_rmtree(const char *path) {
    DIR *d = opendir(path);
    if (!d) return remove(path);
    int bad = 0;
    for (struct dirent *e; (e = readdir(d));) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char sub[1024];
        snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
        struct stat st;
        if (stat(sub, &st) == 0 && S_ISDIR(st.st_mode)) bad |= test_rmtree(sub);
        else bad |= remove(sub) != 0;
    }
    closedir(d);
    return bad | (rmdir(path) != 0);
}

#endif
