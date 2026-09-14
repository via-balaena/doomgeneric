/* Host tests for the two shim pieces that are easy to get quietly wrong: the
 * bump allocator, and the deliberately narrow sscanf.
 *
 * These are not differential -- there is no reference bump allocator to
 * compare against -- so each case states the property it is checking.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* The shim's own names, renamed on the command line to dodge the host libc. */
void  *shim_malloc(size_t);
void  *shim_calloc(size_t, size_t);
void  *shim_realloc(void *, size_t);
void   shim_free(void *);
int    shim_sscanf(const char *, const char *, ...);
void   tock_alloc_stats(size_t *, size_t *, unsigned long *, unsigned long *);

static uint8_t heap[64 * 1024];
uint8_t *tock_heap_base = heap;
size_t   tock_heap_size = sizeof heap;
void tock_console_write(const char *b, size_t n) { (void)b; (void)n; }

static int checks, bad;
static void ok(int cond, const char *what) {
    checks++;
    if (!cond) { bad++; printf("  FAIL %s\n", what); }
}

int main(void) {
    /* Alignment: Doom stores pointers and 8-byte types in zone blocks. */
    void *a = shim_malloc(1);
    void *b = shim_malloc(1);
    ok(((uintptr_t)a & 7) == 0, "malloc returns 8-byte aligned");
    ok(((uintptr_t)b & 7) == 0, "second allocation stays aligned");
    ok(a != b, "two allocations are distinct");

    /* free() reclaims only the most recent block. That is the documented
     * limit, so pin it rather than let it drift into an assumption. */
    size_t used_before;
    tock_alloc_stats(&used_before, 0, 0, 0);
    void *c = shim_malloc(1000);
    shim_free(c);
    size_t used_after;
    tock_alloc_stats(&used_after, 0, 0, 0);
    ok(used_after == used_before, "freeing the newest block reclaims it");

    unsigned long dropped_before, dropped_after;
    tock_alloc_stats(0, 0, 0, &dropped_before);
    shim_free(a);                       /* not the newest: cannot be reclaimed */
    tock_alloc_stats(0, 0, 0, &dropped_after);
    ok(dropped_after == dropped_before + 1, "an older block is counted as dropped");

    /* calloc zeroes, and refuses an overflowing product instead of
     * allocating a short block. */
    unsigned char *z = shim_calloc(100, 4);
    int zeroed = 1;
    for (int i = 0; i < 400; i++) if (z[i]) zeroed = 0;
    ok(zeroed, "calloc zeroes its block");
    ok(shim_calloc((size_t)-1 / 2, 4) == 0, "calloc refuses an overflowing size");

    /* realloc keeps the contents. */
    char *s = shim_malloc(8);
    memcpy(s, "1234567", 8);
    char *t = shim_realloc(s, 64);
    ok(t && memcmp(t, "1234567", 8) == 0, "realloc preserves the old bytes");
    ok(shim_realloc(0, 16) != 0, "realloc(NULL) behaves as malloc");

    /* Running out is a NULL, not a wrap. */
    ok(shim_malloc(sizeof heap * 2) == 0, "an oversized request fails cleanly");

    /* sscanf, at exactly the four shapes M_StrToInt tries and the two in
     * m_config.c. The octal one is the trap: " 0%o" must match the leading
     * zero as a literal and then read the rest as octal. */
    int v = -1;
    ok(shim_sscanf("0x1f", " 0x%x", &v) == 1 && v == 0x1f, "hex via 0x literal");
    v = -1;
    ok(shim_sscanf("  0X2A", " 0X%x", &v) == 1 && v == 0x2a, "hex via 0X, leading space");
    v = -1;
    ok(shim_sscanf("0755", " 0%o", &v) == 1 && v == 0755, "octal after a literal zero");
    v = -1;
    ok(shim_sscanf(" -42", " %d", &v) == 1 && v == -42, "negative decimal");
    v = -1;
    ok(shim_sscanf("0x30", "%i", &v) == 1 && v == 0x30, "%i detects the 0x prefix");
    v = 999;
    ok(shim_sscanf("abc", " %d", &v) == 0 && v == 999, "no digits means no match, arg untouched");
    v = 999;
    ok(shim_sscanf("12", " 0x%x", &v) == 0, "a literal that does not match fails");

    printf("%d checks, %d failure(s)\n", checks, bad);
    return bad != 0;
}
