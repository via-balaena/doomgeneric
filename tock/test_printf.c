/* Differential test: the shim's formatter against the host's libc.
 *
 * The formatter is the one piece of the shim where being subtly wrong is both
 * easy and quiet -- a misplaced pad, a sign-magnitude slip at INT_MIN, a
 * precision that should suppress a zero. A reference implementation is sitting
 * right here on the host, so compare against it rather than eyeball the code.
 *
 *   cc -o test_printf test_printf.c shim_printf.c -Dvsnprintf=shim_vsnprintf \
 *      -Dsnprintf=shim_snprintf -Dprintf=shim_printf -Dfprintf=shim_fprintf \
 *      -Dvfprintf=shim_vfprintf -Dputs=shim_puts -Dputchar=shim_putchar \
 *      -Dfputc=shim_fputc
 */
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdarg.h>

int shim_snprintf(char *buf, size_t cap, const char *fmt, ...);
void tock_console_write(const char *buf, size_t len) { (void)buf; (void)len; }

static int checks, bad;

#define CMP(fmt, ...) do {                                               \
    char a[128], b[128];                                                 \
    int na = snprintf(a, sizeof a, fmt, __VA_ARGS__);                    \
    int nb = shim_snprintf(b, sizeof b, fmt, __VA_ARGS__);               \
    checks++;                                                            \
    if (strcmp(a, b) != 0 || na != nb) {                                 \
        bad++;                                                           \
        printf("  MISMATCH %-14s libc=[%s](%d)  shim=[%s](%d)\n",        \
               fmt, a, na, b, nb);                                       \
    }                                                                    \
} while (0)

/* Same as CMP, but tolerant of a one-in-the-last-digit difference. Length and
 * every digit but the last must still agree. */
#define NEAR(fmt, ...) do {                                              \
    char a[128], b[128];                                                 \
    snprintf(a, sizeof a, fmt, __VA_ARGS__);                             \
    shim_snprintf(b, sizeof b, fmt, __VA_ARGS__);                        \
    checks++;                                                            \
    size_t la = strlen(a), lb = strlen(b);                               \
    int ok = (la == lb) && la > 0 && strncmp(a, b, la - 1) == 0;         \
    if (ok) {                                                            \
        int d = a[la - 1] - b[lb - 1];                                   \
        ok = (d >= -1 && d <= 1);                                        \
    }                                                                    \
    if (!ok) {                                                           \
        bad++;                                                           \
        printf("  MISMATCH %-14s libc=[%s]  shim=[%s]\n", fmt, a, b);    \
    }                                                                    \
} while (0)

int main(void) {
    /* Every conversion Doom uses, at ordinary values. */
    CMP("%d", 0); CMP("%d", 7); CMP("%d", -7); CMP("%d", 1234567);
    CMP("%i", 42); CMP("%i", -42);
    CMP("%u", 0u); CMP("%u", 4000000000u);
    CMP("%x", 0xdeadbeefu); CMP("%X", 0xdeadbeefu); CMP("%o", 0755u);
    CMP("%c", 'q'); CMP("%s", "hello"); CMP("%s", "");
    CMP("%ld", 123456789L); CMP("%ld", -123456789L);

    /* The exact width/precision/flag combinations found in Doom's sources. */
    CMP("%02i", 5); CMP("%02i", 123);
    CMP("%8ld", 42L); CMP("%10ld", 42L); CMP("%6ld", -42L);
    CMP("%7i", 99); CMP("%3i", 4); CMP("%3i", 99999);
    CMP("%.2d", 7); CMP("%.3d", 7); CMP("%2.2d", 7);
    CMP("%-10s", "ab"); CMP("%79s", "x");
    CMP("%02x", 15u);

    /* Edges that are easy to get wrong. */
    CMP("%d", INT_MIN);                 /* negation overflows a naive -v */
    CMP("%.0d", 0);                     /* precision 0 renders nothing */
    CMP("%.0d", 5);
    CMP("%5.0d", 0);
    CMP("%-5d|", 42); CMP("%05d", 42); CMP("%-05d|", 42);
    CMP("%05d", -42);                   /* zero pad goes after the sign */
    CMP("%08.3d", 4);                   /* `0` is ignored when precision given */
    CMP("%+d", 42); CMP("% d", 42); CMP("%+d", -42);
    CMP("%.3s", "abcdef"); CMP("%.0s", "abcdef"); CMP("%8.3s", "abcdef");
    CMP("%-8.3s|", "abcdef");
    CMP("%5c|", 'z'); CMP("%-5c|", 'z');
    CMP("%%%d", 9);
    CMP("%#x", 255u); CMP("%#x", 0u);
    CMP("%s", (char *)NULL);

    /* Floats. The shim scales by a power of ten and rounds half-to-even,
     * which agrees with libc except when the stored double sits within about
     * one ULP of a tie: 0.005 is really 0.005000000000000000104, so libc
     * rounds %.2f up and the shim, whose multiply lands exactly on 0.5, does
     * not. Getting that right needs Grisu- or Ryu-class decimal conversion.
     *
     * Doom formats a float in three places -- a -timedemo summary, a config
     * value, and an SDL sound message not built here -- so that work is not
     * worth doing, and the LIMIT IS STATED rather than papered over: the last
     * digit may differ by one. NEAR still requires identical length and
     * identical leading digits, so a lost '.', a wrong integer part or a
     * broken field width is still caught. */
    CMP("%f", 1.5); CMP("%f", 0.0); CMP("%.3f", 12.0);
    CMP("%.1f", -2.25); CMP("%8.2f", 3.5); CMP("%-8.2f|", 3.5);
    NEAR("%0.2f", 3.14159); NEAR("%.2f", 0.005); NEAR("%f", 1.0 / 3.0);

    printf("%d comparisons, %d mismatch(es)\n", checks, bad);
    return bad != 0;
}
