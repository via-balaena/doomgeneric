/* Files, and the one scanf Doom needs.
 *
 * There is no filesystem. Doom wants one for three things: the WAD, which is
 * in flash and is reached by address rather than by read(); the config file,
 * which it recreates from defaults when it cannot be opened; and savegames,
 * which are not part of this port yet. So fopen() fails and every path that
 * depends on it takes the branch it already has for a missing file.
 *
 * stdout and stderr are distinct objects only so that pointer comparisons in
 * Doom behave; both reach the same console.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "tock_shim.h"

struct _TOCK_FILE { int which; };

static struct _TOCK_FILE tock_stdin_file  = { 0 };
static struct _TOCK_FILE tock_stdout_file = { 1 };
static struct _TOCK_FILE tock_stderr_file = { 2 };

FILE *stdin  = &tock_stdin_file;
FILE *stdout = &tock_stdout_file;
FILE *stderr = &tock_stderr_file;

FILE *fopen(const char *path, const char *mode) {
    (void)path; (void)mode;
    errno = ENOENT;
    return 0;
}

int    fclose(FILE *f)  { (void)f; return 0; }
int    fflush(FILE *f)  { (void)f; return 0; }
int    fileno(FILE *f)  { return f ? f->which : -1; }
size_t fread(void *p, size_t sz, size_t n, FILE *f)  { (void)p; (void)sz; (void)n; (void)f; return 0; }
int    fseek(FILE *f, long off, int whence) { (void)f; (void)off; (void)whence; return -1; }
long   ftell(FILE *f)   { (void)f; return -1; }
int    remove(const char *path) { (void)path; errno = ENOENT; return -1; }
int    rename(const char *a, const char *b) { (void)a; (void)b; errno = ENOENT; return -1; }

/* Writing to a stream means writing to the console: Doom's only fwrite calls
 * on this target are diagnostics. */
size_t fwrite(const void *p, size_t sz, size_t n, FILE *f) {
    (void)f;
    size_t total = sz * n;
    if (sz && total / sz != n) return 0;       /* overflow */
    if (total) tock_console_write((const char *)p, total);
    return n;
}
