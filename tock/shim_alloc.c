/* malloc for a program that barely uses it.
 *
 * Doom allocates its own way: Z_Init takes one enormous block and the zone
 * hands out everything after that. Measured on the headless build, the whole
 * run makes a couple of dozen malloc calls, one of which is the zone and the
 * rest are short strings that are never freed.
 *
 * So this is a bump allocator. free() releases only the most recent block,
 * which costs four lines and covers the alloc-then-immediately-free pattern;
 * anything else is a no-op and the space is not reclaimed. That is a real
 * limitation, not an oversight: tock_alloc_stats() reports what leaked so it
 * can be checked rather than assumed.
 */
#include "tock_shim.h"

/* Declared here rather than pulled from <stdlib.h>/<string.h> so this file
 * depends on no headers at all, which is what lets the host test compile it
 * with its entry points renamed out of the way of the system libc. */
void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);

#define ALIGN 8

static size_t used;
static size_t last_offset = (size_t)-1;   /* start of the most recent block */
static size_t peak;
static unsigned long calls, frees, freed_bytes, dropped_frees;

void tock_alloc_stats(size_t *out_used, size_t *out_peak,
                      unsigned long *out_calls, unsigned long *out_dropped) {
    if (out_used)    *out_used = used;
    if (out_peak)    *out_peak = peak;
    if (out_calls)   *out_calls = calls;
    if (out_dropped) *out_dropped = dropped_frees;
}

static size_t round_up(size_t n) { return (n + (ALIGN - 1)) & ~(size_t)(ALIGN - 1); }

void *malloc(size_t n) {
    if (n == 0) n = 1;
    size_t want = round_up(n);
    /* The header is one size_t so free() and realloc() know the block length.
     * It is placed before the returned pointer and kept ALIGN-sized so the
     * payload stays aligned. */
    size_t need = want + ALIGN;
    if (need < want || used + need > tock_heap_size) return 0;   /* overflow or full */

    size_t off = used;
    unsigned char *base = tock_heap_base + off;
    *(size_t *)base = want;
    used += need;
    if (used > peak) peak = used;
    last_offset = off;
    calls++;
    return base + ALIGN;
}

void *calloc(size_t n, size_t sz) {
    /* Reject an overflowing product rather than allocate a short block. */
    if (sz && n > (size_t)-1 / sz) return 0;
    size_t total = n * sz;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void free(void *p) {
    if (!p) return;
    frees++;
    unsigned char *base = (unsigned char *)p - ALIGN;
    size_t off = (size_t)(base - tock_heap_base);
    if (off == last_offset) {
        used = off;
        freed_bytes += *(size_t *)base;
        last_offset = (size_t)-1;
    } else {
        dropped_frees++;
    }
}

void *realloc(void *p, size_t n) {
    if (!p) return malloc(n);
    if (n == 0) { free(p); return 0; }
    size_t old = *(size_t *)((unsigned char *)p - ALIGN);
    if (n <= old) return p;                  /* shrinking in place is free */
    void *q = malloc(n);
    if (!q) return 0;
    memcpy(q, p, old);
    free(p);
    return q;
}
