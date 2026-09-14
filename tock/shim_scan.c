/* One integer in from a string, and nothing more.
 *
 * Kept apart from shim_stdio.c so it can be compiled and tested on the host,
 * where that file's FILE definition collides with the system one.
 */
#include <stdarg.h>
#include <stddef.h>

/* Local, so this file needs no headers beyond stdarg: the host test compiles
 * it with sscanf renamed, and a system <ctype.h> would drag in declarations
 * that the rename then mangles. */
static int sp(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
static int dg(int c) { return c >= '0' && c <= '9'; }
static int al(int c) { return (c | 32) >= 'a' && (c | 32) <= 'z'; }

/* A deliberately narrow sscanf.
 *
 * Every call in the tree parses one integer: M_StrToInt tries " 0x%x",
 * " 0X%x", " 0%o" and " %d" in turn, and m_config.c reads "%x" or "%i". So
 * whitespace in the format skips whitespace, a literal must match, and one
 * integer conversion is understood. An unsupported conversion returns the
 * count so far rather than guessing, which reads to the caller as a failed
 * parse -- the same answer a real sscanf gives for input it cannot match.
 */
int sscanf(const char *s, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int matched = 0;

    for (; *fmt; fmt++) {
        if (sp((unsigned char)*fmt)) {
            while (sp((unsigned char)*s)) s++;
            continue;
        }
        if (*fmt != '%') {
            if (*s != *fmt) goto done;
            s++;
            continue;
        }
        fmt++;
        while (*fmt >= '0' && *fmt <= '9') fmt++;      /* field width, ignored */
        while (*fmt == 'l' || *fmt == 'h') fmt++;

        unsigned base;
        switch (*fmt) {
        case 'd': case 'u': base = 10; break;
        case 'x': case 'X': base = 16; break;
        case 'o':           base = 8;  break;
        case 'i':           base = 0;  break;
        default:            goto done;              /* not supported: stop */
        }

        while (sp((unsigned char)*s)) s++;
        int sign = 1;
        if (*s == '-') { sign = -1; s++; } else if (*s == '+') s++;
        if (base == 0) {
            if (s[0] == '0' && (s[1] | 32) == 'x') { s += 2; base = 16; }
            else if (s[0] == '0' && s[1]) { s++; base = 8; }
            else base = 10;
        }
        const char *digits_start = s;
        unsigned long v = 0;
        for (;;) {
            int d;
            if (dg((unsigned char)*s)) d = *s - '0';
            else if (al((unsigned char)*s)) d = (*s | 32) - 'a' + 10;
            else break;
            if ((unsigned)d >= base) break;
            v = v * base + (unsigned)d;
            s++;
        }
        if (s == digits_start) goto done;            /* no digits: no match */
        *va_arg(ap, int *) = (int)((long)v * sign);
        matched++;
    }

done:
    va_end(ap);
    return matched;
}
