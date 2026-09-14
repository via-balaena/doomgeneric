/* One formatter, and the printf family as thin wrappers over it.
 *
 * The conversions supported are the ones Doom uses, counted across every
 * format string in the tree: %i (154), %s (124), %d (93), %x (12), %p (10),
 * %c (8), %% (3), %f (3), %u (2), %o (1), with flags `-` and `0`, width,
 * precision, and the `l` length modifier. %X is here too because it costs one
 * character. Anything else prints as a literal rather than silently consuming
 * an argument and desynchronising every conversion after it.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "tock_shim.h"

typedef struct {
    char  *buf;        /* NULL means the console */
    size_t cap;        /* bytes in buf, including room for the NUL */
    size_t len;        /* what a full result WOULD be -- snprintf semantics */
    char   line[128];  /* console staging, so one call is one write */
    size_t nline;
} sink;

static void sink_flush(sink *s) {
    if (!s->buf && s->nline) {
        tock_console_write(s->line, s->nline);
        s->nline = 0;
    }
}

static void emit(sink *s, char c) {
    if (s->buf) {
        if (s->cap && s->len + 1 < s->cap) s->buf[s->len] = c;
    } else {
        s->line[s->nline++] = c;
        if (s->nline == sizeof(s->line)) sink_flush(s);
    }
    s->len++;
}

static void emit_pad(sink *s, char c, int n) { while (n-- > 0) emit(s, c); }

/* Renders into a caller-supplied buffer backwards, which avoids needing to
 * know the digit count up front. */
static int render_uint(char *out, unsigned long long v, unsigned base, int upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    do { out[n++] = digits[v % base]; v /= base; } while (v);
    return n;
}

static void put_number(sink *s, unsigned long long mag, int negative, unsigned base,
                       int upper, int width, int prec, int left, int zero, char sign) {
    char digits[24];
    int ndig = render_uint(digits, mag, base, upper);
    /* A precision of 0 renders zero as nothing at all, which is what the
     * standard says and what %.0d in a field relies on. */
    if (prec == 0 && mag == 0) ndig = 0;
    int zeros = (prec > ndig) ? prec - ndig : 0;
    int signlen = (negative || sign) ? 1 : 0;
    int body = ndig + zeros + signlen;
    /* `0` is ignored when a precision is given, as the standard requires. */
    int padzero = (zero && !left && prec < 0) ? width - body : 0;
    int padspace = width - body - (padzero > 0 ? padzero : 0);

    if (!left) emit_pad(s, ' ', padspace);
    if (negative) emit(s, '-'); else if (sign) emit(s, sign);
    emit_pad(s, '0', padzero);
    emit_pad(s, '0', zeros);
    while (ndig--) emit(s, digits[ndig]);
    if (left) emit_pad(s, ' ', padspace);
}

/* Doom formats a float in exactly three places, none of them on a frame path:
 * the -timedemo result, a config value, and an SDL sound message not built
 * here. Fixed-point with a default of six decimals covers all three. Values
 * too large for the integer part are printed as `inf` rather than wrapping. */
static void put_double(sink *s, double v, int width, int prec, int left, int zero) {
    if (prec < 0) prec = 6;
    int negative = v < 0;
    if (negative) v = -v;
    if (v >= 1.8e19) {
        int pad = width - (negative ? 4 : 3);
        if (!left) emit_pad(s, ' ', pad);
        if (negative) emit(s, '-');
        emit(s, 'i'); emit(s, 'n'); emit(s, 'f');
        if (left) emit_pad(s, ' ', pad);
        return;
    }
    unsigned long long whole = (unsigned long long)v;
    double rest = v - (double)whole;
    /* Round at the requested precision, carrying into the integer part.
     * Half-to-even, not half-up: that is what IEEE 754 and every libc do, and
     * a differential test against the host catches the difference the moment
     * a value lands exactly on a tie (-2.25 at %.1f is 2.2, not 2.3). */
    double scale = 1;
    for (int i = 0; i < prec; i++) scale *= 10;
    double scaled = rest * scale;
    unsigned long long frac = (unsigned long long)scaled;
    double excess = scaled - (double)frac;
    if (excess > 0.5 || (excess == 0.5 && (frac & 1ull))) frac++;
    if (frac >= (unsigned long long)scale) { frac -= (unsigned long long)scale; whole++; }

    char ipart[24];
    int ni = render_uint(ipart, whole, 10, 0);
    int body = ni + (negative ? 1 : 0) + (prec > 0 ? 1 + prec : 0);
    int pad = width - body;

    if (!left && !zero) emit_pad(s, ' ', pad);
    if (negative) emit(s, '-');
    if (!left && zero) emit_pad(s, '0', pad);
    while (ni--) emit(s, ipart[ni]);
    if (prec > 0) {
        emit(s, '.');
        char fpart[24];
        int nf = render_uint(fpart, frac, 10, 0);
        emit_pad(s, '0', prec - nf);
        while (nf--) emit(s, fpart[nf]);
    }
    if (left) emit_pad(s, ' ', pad);
}

static int format(sink *s, const char *fmt, va_list ap) {
    for (; *fmt; fmt++) {
        if (*fmt != '%') { emit(s, *fmt); continue; }
        const char *start = fmt;
        fmt++;

        int left = 0, zero = 0, alt = 0;
        char sign = 0;
        for (;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') zero = 1;
            else if (*fmt == '+') sign = '+';
            else if (*fmt == ' ') { if (!sign) sign = ' '; }
            else if (*fmt == '#') alt = 1;
            else break;
        }
        int width = 0;
        if (*fmt == '*') { width = va_arg(ap, int); fmt++; if (width < 0) { left = 1; width = -width; } }
        else while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');

        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            else while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0');
            if (prec < 0) prec = -1;
        }
        int lng = 0;
        for (;;) {
            if (*fmt == 'l') { lng++; fmt++; }
            else if (*fmt == 'h' || *fmt == 'z' || *fmt == 'j' || *fmt == 't') { fmt++; }
            else break;
        }

        switch (*fmt) {
        case 'd': case 'i': {
            long long v = (lng >= 2) ? va_arg(ap, long long)
                        : (lng == 1) ? va_arg(ap, long)
                                     : va_arg(ap, int);
            unsigned long long mag = (v < 0) ? (unsigned long long)(-(v + 1)) + 1ull
                                             : (unsigned long long)v;
            put_number(s, mag, v < 0, 10, 0, width, prec, left, zero, sign);
            break;
        }
        case 'u': case 'o': case 'x': case 'X': {
            unsigned long long v = (lng >= 2) ? va_arg(ap, unsigned long long)
                                 : (lng == 1) ? va_arg(ap, unsigned long)
                                              : va_arg(ap, unsigned int);
            unsigned base = (*fmt == 'u') ? 10u : (*fmt == 'o') ? 8u : 16u;
            if (alt && base == 16 && v) { emit(s, '0'); emit(s, *fmt == 'X' ? 'X' : 'x'); width -= 2; }
            put_number(s, v, 0, base, *fmt == 'X', width, prec, left, zero, 0);
            break;
        }
        case 'c': {
            int c = va_arg(ap, int);
            if (!left) emit_pad(s, ' ', width - 1);
            emit(s, (char)c);
            if (left) emit_pad(s, ' ', width - 1);
            break;
        }
        case 's': {
            const char *p = va_arg(ap, const char *);
            if (!p) p = "(null)";
            int n = 0;
            while (p[n] && (prec < 0 || n < prec)) n++;
            if (!left) emit_pad(s, ' ', width - n);
            for (int i = 0; i < n; i++) emit(s, p[i]);
            if (left) emit_pad(s, ' ', width - n);
            break;
        }
        case 'p': {
            void *p = va_arg(ap, void *);
            emit(s, '0'); emit(s, 'x');
            put_number(s, (unsigned long long)(uintptr_t)p, 0, 16, 0,
                       width > 2 ? width - 2 : 0, prec, left, zero, 0);
            break;
        }
        case 'f': case 'F': case 'g': case 'G': case 'e': case 'E':
            put_double(s, va_arg(ap, double), width, prec, left, zero);
            break;
        case '%':
            emit(s, '%');
            break;
        default:
            /* Unknown conversion. Consuming an argument here would throw off
             * every conversion after it, so print what was written instead. */
            for (const char *q = start; q <= fmt && *q; q++) emit(s, *q);
            if (!*fmt) return (int)s->len;
            break;
        }
    }
    return (int)s->len;
}

int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap) {
    sink s = { buf, cap, 0, { 0 }, 0 };
    int n = format(&s, fmt, ap);
    if (buf && cap) buf[(s.len < cap - 1) ? s.len : cap - 1] = '\0';
    return n;
}

int snprintf(char *buf, size_t cap, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

/* Every FILE in this port is the console: nothing else can be opened. */
int vfprintf(FILE *f, const char *fmt, va_list ap) {
    (void)f;
    sink s = { 0, 0, 0, { 0 }, 0 };
    int n = format(&s, fmt, ap);
    sink_flush(&s);
    return n;
}

int fprintf(FILE *f, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

int puts(const char *s) {
    tock_console_write(s, strlen(s));
    tock_console_write("\n", 1);
    return 0;
}

int putchar(int c) { char ch = (char)c; tock_console_write(&ch, 1); return c; }
int fputc(int c, FILE *f) { (void)f; return putchar(c); }
