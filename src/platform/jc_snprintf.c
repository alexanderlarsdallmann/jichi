/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_snprintf.c - bounded formatting (see jc_snprintf.h). */

#include "jc_snprintf.h"
#include <stdio.h>
#include <string.h>

#ifdef JC_HAVE_VSNPRINTF

int jc_vsnprintf(char *buf, jc_size cap, const char *fmt, va_list ap)
{
    int n;
    /* vsnprintf is C99; the build probe guarantees it is available here.
     * It already NUL-terminates and returns the would-be length. */
    n = vsnprintf(buf, cap, fmt, ap);
    return n;
}

#else /* fallback formatter for strict-C89 libcs */

#include <limits.h> /* ULONG_MAX -- bound the float->integer conversion */

/* Minimal output sink that tracks how many characters WOULD be written. */
struct sink {
    char   *buf;
    jc_size cap;   /* total buffer size including space for NUL */
    jc_size pos;   /* bytes written so far (not counting NUL)   */
    jc_size want;  /* bytes that would be written if unbounded  */
};

static void sink_putc(struct sink *s, char c)
{
    if (s->pos + 1 < s->cap) {
        s->buf[s->pos] = c;
        s->pos++;
    }
    s->want++;
}

static void sink_puts(struct sink *s, const char *str)
{
    while (*str) {
        sink_putc(s, *str);
        str++;
    }
}

/* Render an unsigned long in the given base (10 or 16). */
static void sink_putul(struct sink *s, unsigned long v, int base, int upper)
{
    char tmp[32];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    if (v == 0) {
        sink_putc(s, '0');
        return;
    }
    while (v > 0 && i < (int)sizeof(tmp)) {
        tmp[i] = digits[v % (unsigned long)base];
        v /= (unsigned long)base;
        i++;
    }
    while (i > 0) {
        i--;
        sink_putc(s, tmp[i]);
    }
}

static void sink_putl(struct sink *s, long v)
{
    unsigned long u;
    if (v < 0) {
        sink_putc(s, '-');
        u = (unsigned long)(-(v + 1)) + 1UL; /* avoid overflow on LONG_MIN */
    } else {
        u = (unsigned long)v;
    }
    sink_putul(s, u, 10, 0);
}

static void sink_putdouble(struct sink *s, double d)
{
    /* Simple fixed 6-decimal rendering; sufficient for our diagnostics.
     * Written straight to the cap-bounded sink rather than sprintf'd into a
     * fixed buffer: `%f` on a large double emits hundreds of digits, which
     * overflowed the old tmp[64] (the sprintf-lint caught it). No sprintf, no
     * fixed buffer, so no overflow is possible regardless of the value. */
    unsigned long ip;
    double frac;
    int i;

    if (d != d) {            /* NaN: not equal to itself */
        sink_puts(s, "nan");
        return;
    }
    if (d < 0.0) {
        sink_putc(s, '-');
        d = -d;
    }
    /* Beyond ULONG_MAX (or +Inf) the integer part does not fit; emit a
     * bounded marker instead of an unbounded/undefined conversion. This is
     * the strict-C89 diagnostics fallback, so a lossy "big" is acceptable --
     * the alternative was a stack overflow. */
    if (!(d <= (double)ULONG_MAX)) {
        sink_puts(s, "big");
        return;
    }
    ip = (unsigned long)d;
    sink_putul(s, ip, 10, 0);
    sink_putc(s, '.');
    frac = d - (double)ip;
    for (i = 0; i < 6; i++) { /* six decimals, truncated (matches the note) */
        int digit;
        frac *= 10.0;
        digit = (int)frac;
        if (digit < 0) { digit = 0; }
        if (digit > 9) { digit = 9; }
        sink_putc(s, (char)('0' + digit));
        frac -= (double)digit;
    }
}

int jc_vsnprintf(char *buf, jc_size cap, const char *fmt, va_list ap)
{
    struct sink s;
    s.buf = buf;
    s.cap = cap;
    s.pos = 0;
    s.want = 0;

    while (*fmt) {
        if (*fmt != '%') {
            sink_putc(&s, *fmt);
            fmt++;
            continue;
        }
        fmt++; /* consume '%' */
        if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'd') {
                sink_putl(&s, va_arg(ap, long));
            } else if (*fmt == 'u') {
                sink_putul(&s, va_arg(ap, unsigned long), 10, 0);
            } else if (*fmt == 'x') {
                sink_putul(&s, va_arg(ap, unsigned long), 16, 0);
            } else if (*fmt == 'X') {
                sink_putul(&s, va_arg(ap, unsigned long), 16, 1);
            } else {
                sink_putc(&s, '%');
                sink_putc(&s, 'l');
                if (*fmt) sink_putc(&s, *fmt);
            }
        } else if (*fmt == 'd') {
            sink_putl(&s, (long)va_arg(ap, int));
        } else if (*fmt == 'u') {
            sink_putul(&s, (unsigned long)va_arg(ap, unsigned int), 10, 0);
        } else if (*fmt == 'x') {
            sink_putul(&s, (unsigned long)va_arg(ap, unsigned int), 16, 0);
        } else if (*fmt == 's') {
            const char *str = va_arg(ap, const char *);
            sink_puts(&s, str ? str : "(null)");
        } else if (*fmt == 'c') {
            sink_putc(&s, (char)va_arg(ap, int));
        } else if (*fmt == 'f') {
            sink_putdouble(&s, va_arg(ap, double));
        } else if (*fmt == '%') {
            sink_putc(&s, '%');
        } else {
            sink_putc(&s, '%');
            if (*fmt) sink_putc(&s, *fmt);
        }
        if (*fmt) fmt++;
    }

    if (s.cap > 0) {
        jc_size term = (s.pos < s.cap) ? s.pos : (s.cap - 1);
        s.buf[term] = '\0';
    }
    return (int)s.want;
}

#endif /* JC_HAVE_VSNPRINTF */

int jc_snprintf(char *buf, jc_size cap, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = jc_vsnprintf(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}
