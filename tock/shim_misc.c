/* The leftovers: numbers in from strings, time, and the calls Doom makes for
 * an environment a Tock process does not have.
 *
 * The stubs answer "not available" rather than pretending. Doom already
 * handles every one of these failing -- it has to, since they fail on real
 * systems too -- so a clean refusal is both correct and the smaller change.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/mman.h>
#include "tock_shim.h"

int errno;

int atoi(const char *s) {
    int sign = 1, v = 0;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') { sign = -1; s++; } else if (*s == '+') s++;
    while (isdigit((unsigned char)*s)) v = v * 10 + (*s++ - '0');
    return v * sign;
}

long strtol(const char *s, char **end, int base) {
    long v = 0; int sign = 1;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') { sign = -1; s++; } else if (*s == '+') s++;
    if ((base == 16 || base == 0) && s[0] == '0' && (s[1] | 32) == 'x') { s += 2; base = 16; }
    else if (base == 0) base = (*s == '0') ? 8 : 10;
    for (;;) {
        int d;
        if (isdigit((unsigned char)*s)) d = *s - '0';
        else if (isalpha((unsigned char)*s)) d = (*s | 32) - 'a' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
        s++;
    }
    if (end) *end = (char *)s;
    return v * sign;
}

/* Doom reads one float out of a config file and writes it back. Anything
 * beyond that -- exponents, hex floats, rounding to nearest -- it never asks
 * for, and pretending otherwise would be more code with nothing exercising it. */
double atof(const char *s) {
    double v = 0, frac = 0, scale = 1;
    int sign = 1;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') { sign = -1; s++; } else if (*s == '+') s++;
    while (isdigit((unsigned char)*s)) v = v * 10 + (*s++ - '0');
    if (*s == '.') {
        s++;
        while (isdigit((unsigned char)*s)) { frac = frac * 10 + (*s++ - '0'); scale *= 10; }
        v += frac / scale;
    }
    return v * sign;
}

double fabs(double x) { return x < 0 ? -x : x; }

int gettimeofday(struct timeval *tv, struct timezone *tz) {
    (void)tz;
    if (tv) {
        uint32_t ms = tock_ticks_ms();
        tv->tv_sec  = (long)(ms / 1000u);
        tv->tv_usec = (long)((ms % 1000u) * 1000u);
    }
    return 0;
}

void exit(int status) { tock_exit(status); }

/* No environment: DOOMWADDIR, DOOMWADPATH and TEMP all resolve to nothing,
 * which sends Doom down its "look where you are" paths. */
char *getenv(const char *name) { (void)name; return 0; }

/* Only reached by the zenity error-box probe, which treats non-zero as
 * "no zenity here" and moves on. */
int system(const char *cmd) { (void)cmd; return -1; }

int mkdir(const char *path, int mode) { (void)path; (void)mode; errno = EACCES; return -1; }

/* w_file_stdc.c mmaps the WAD so W_CacheLumpNum takes its zero-copy path. On
 * this target the WAD is already addressable in flash, so the Tock backend
 * sets wad_file->mapped directly and never calls this. Refusing loudly beats
 * returning a pointer that is not the WAD. */
void *mmap(void *addr, size_t len, int prot, int flags, int fd, long off) {
    (void)addr; (void)len; (void)prot; (void)flags; (void)fd; (void)off;
    return MAP_FAILED;
}

int munmap(void *addr, size_t len) { (void)addr; (void)len; return -1; }
