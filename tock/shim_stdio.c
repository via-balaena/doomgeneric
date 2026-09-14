/* Files, of which there is exactly one.
 *
 * There is no filesystem. Doom wants one for three things:
 *
 *   the WAD      which IS here -- in the app's own flash, XIP-mapped and
 *                readable in place. So this answers for it, and for nothing
 *                else. `mmap` on a file already in the address space returns
 *                where it is, which is not a trick: that is what mapping it
 *                means. w_file_stdc.c then sets `mapped` and W_CacheLumpNum
 *                takes its zero-copy path with no change to Doom at all.
 *   the config   which Doom recreates from defaults when it cannot be opened
 *   savegames    not part of this port yet
 *
 * So fopen() succeeds for one name and fails for everything else, and the two
 * paths that depend on failing take the branch they already have.
 *
 * stdout and stderr are distinct objects only so pointer comparisons behave;
 * both reach the console.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "tock_shim.h"

#define FD_STDIN  0
#define FD_STDOUT 1
#define FD_STDERR 2
#define FD_WAD    3

struct _TOCK_FILE { int which; size_t pos; };

static struct _TOCK_FILE tock_stdin_file  = { FD_STDIN,  0 };
static struct _TOCK_FILE tock_stdout_file = { FD_STDOUT, 0 };
static struct _TOCK_FILE tock_stderr_file = { FD_STDERR, 0 };
static struct _TOCK_FILE tock_wad_file    = { FD_WAD,    0 };

FILE *stdin  = &tock_stdin_file;
FILE *stdout = &tock_stdout_file;
FILE *stderr = &tock_stderr_file;

static int is_wad(FILE *f) { return f == &tock_wad_file; }

FILE *fopen(const char *path, const char *mode) {
    (void)mode;
    if (path && tock_wad_name && tock_wad_base && tock_wad_length
        && strcmp(path, tock_wad_name) == 0) {
        tock_wad_file.pos = 0;
        return &tock_wad_file;
    }
    errno = ENOENT;
    return 0;
}

int fclose(FILE *f) { (void)f; return 0; }
int fflush(FILE *f) { (void)f; return 0; }
int fileno(FILE *f) { return f ? f->which : -1; }

size_t fread(void *p, size_t sz, size_t n, FILE *f) {
    if (!is_wad(f) || sz == 0) return 0;
    size_t want = sz * n;
    if (want / sz != n) return 0;                       /* overflow */
    if (f->pos >= tock_wad_length) return 0;
    size_t avail = tock_wad_length - f->pos;
    if (want > avail) want = avail;
    memcpy(p, tock_wad_base + f->pos, want);
    f->pos += want;
    return want / sz;                                   /* whole items only */
}

int fseek(FILE *f, long off, int whence) {
    if (!is_wad(f)) return -1;
    long base = (whence == SEEK_SET) ? 0
              : (whence == SEEK_CUR) ? (long)f->pos
              : (whence == SEEK_END) ? (long)tock_wad_length
              : -1;
    if (base < 0) return -1;
    long target = base + off;
    if (target < 0 || (size_t)target > tock_wad_length) return -1;
    f->pos = (size_t)target;
    return 0;
}

long ftell(FILE *f) { return is_wad(f) ? (long)f->pos : -1; }

int remove(const char *path) { (void)path; errno = ENOENT; return -1; }
int rename(const char *a, const char *b) { (void)a; (void)b; errno = ENOENT; return -1; }

/* Writing to a stream means writing to the console: every fwrite Doom makes
 * on this target is a diagnostic. */
size_t fwrite(const void *p, size_t sz, size_t n, FILE *f) {
    (void)f;
    size_t total = sz * n;
    if (sz && total / sz != n) return 0;
    if (total) tock_console_write((const char *)p, total);
    return n;
}
