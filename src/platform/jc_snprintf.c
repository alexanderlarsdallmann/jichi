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

/* Minimal output sink that tracks how many characters WOULD be written.
 * content/len carry ONE conversion's full text (width/precision aside),
 * so a renderer can measure it first and then pad, instead of writing to
 * the cap-bounded buffer before it knows how long the conversion is. */
struct sink {
    char   *buf;
    jc_size cap;   /* total buffer size including space for NUL */
    jc_size pos;   /* bytes written so far (not counting NUL)   */
    jc_size want;  /* bytes that would be written if unbounded  */
    char   *content;
    jc_size len;
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

/* Conversion flags (A-10 / M1079: the fallback now parses the full
 * flag/width/precision grammar, not just the trailing class char). */
#define F_MINUS  0x01
#define F_PLUS   0x02
#define F_SPACE  0x04
#define F_ZERO   0x08
#define F_LONG   0x10

static void emit_spaces(struct sink *s, jc_size n)
{
    while (n > 0) {
        sink_putc(s, ' ');
        n--;
    }
}

/* Append v's digits to s->content at its current position (s->len) -- the
 * WIDTH-PRECISION-ASIDE rendering of one conversion, measured before it is
 * padded and written to the cap-bounded sink. The content buffer is 80
 * bytes: at most 1 sign + 20 digits + 1 dot + 48 decimals fit. */
static void ul_append(struct sink *s, unsigned long v, int base, int upper)
{
    char tmp[24];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    int j;
    if (v == 0) {
        tmp[i++] = '0';
    } else {
        while (v > 0 && i < 20) {
            tmp[i] = digits[v % (unsigned long)base];
            v /= (unsigned long)base;
            i++;
        }
    }
    for (j = i; j > 0; j--) {
        s->content[s->len++] = tmp[j - 1];
    }
}

static void render_unsigned(struct sink *s, unsigned long v, int base, int upper)
{
    s->len = 0;
    ul_append(s, v, base, upper);
}

static void render_long(struct sink *s, long v, int flags)
{
    unsigned long u;
    s->len = 0;
    if (v < 0) {
        s->content[s->len++] = '-';
        u = (unsigned long)(-(v + 1)) + 1UL; /* avoid overflow on LONG_MIN */
    } else {
        if (flags & F_PLUS) {
            s->content[s->len++] = '+';
        } else if (flags & F_SPACE) {
            s->content[s->len++] = ' ';
        }
        u = (unsigned long)v;
    }
    ul_append(s, u, 10, 0);
}

/* Fixed-precision decimal rendering (default 6 for %f/%g/%e). Written to
 * s->content, never sprintf'd into a fixed temp buffer: beyond ULONG_MAX
 * (or +Inf) the integer part does not fit, so a bounded "big" marker is
 * emitted instead -- this is the strict-C89 diagnostics fallback, where a
 * lossy "big" is acceptable (the alternative was a stack overflow). */
static void render_double(struct sink *s, double d, int prec, int flags)
{
    unsigned long ip;
    double frac;
    int i;
    s->len = 0;
    if (prec < 0) { prec = 6; }
    if (prec > 48) { prec = 48; }
    if (d != d) {            /* NaN: not equal to itself */
        s->content[s->len++] = 'n';
        s->content[s->len++] = 'a';
        s->content[s->len++] = 'n';
        return;
    }
    if (d < 0.0) {
        s->content[s->len++] = '-';
        d = -d;
    } else if (flags & F_PLUS) {
        s->content[s->len++] = '+';
    } else if (flags & F_SPACE) {
        s->content[s->len++] = ' ';
    }
    if (!(d <= (double)ULONG_MAX)) {
        s->content[s->len++] = 'b';
        s->content[s->len++] = 'i';
        s->content[s->len++] = 'g';
        return;
    }
    ip = (unsigned long)d;
    ul_append(s, ip, 10, 0);
    if (prec > 0) {
        s->content[s->len++] = '.';
        frac = d - (double)ip;
        for (i = 0; i < prec; i++) { /* truncated decimals (matches the note) */
            int digit;
            frac *= 10.0;
            digit = (int)frac;
            if (digit < 0) { digit = 0; }
            if (digit > 9) { digit = 9; }
            s->content[s->len++] = (char)('0' + digit);
            frac -= (double)digit;
        }
    }
}

/* Write s->content[0..len) to the sink with the width applied. The zero
 * flag pads with '0' (after the sign, if one was emitted) and only when no
 * precision is present, as C99 7.19.6.1 requires; minus moves the padding
 * to the right. */
static void emit_padded(struct sink *s, int flags, int width, int prec_set)
{
    jc_size pad = (s->len < (jc_size)width) ? (jc_size)width - s->len : 0;
    char padch = ' ';
    int has_sign = (s->len > 0 && (s->content[0] == '-' || s->content[0] == '+'));
    /* The sign goes BEFORE the padding in both layouts, and the 0-flag
     * fills after it (C99 7.19.6.1). */
    if (has_sign) {
        sink_putc(s, s->content[0]);
        s->content++;
        s->len--;
    }
    if (!(flags & F_MINUS) && (flags & F_ZERO) && !prec_set) {
        padch = '0';
    }
    if (!(flags & F_MINUS)) {
        while (pad > 0) {
            sink_putc(s, padch);
            pad--;
        }
    }
    while (s->len > 0) {
        sink_putc(s, *s->content);
        s->content++;
        s->len--;
    }
    if (flags & F_MINUS) {
        while (pad > 0) {
            sink_putc(s, padch);
            pad--;
        }
    }
}

/* %s with optional precision and width: MEASURE first (the precision may be
 * larger than any fixed buffer, so the string never goes through
 * s->content), pad, then copy. */
static void render_str(struct sink *s, const char *str, int prec, int flags, int width)
{
    jc_size n = 0;
    jc_size i;
    if (str == NULL) {
        str = "(null)";
    }
    if (prec >= 0) {
        while (n < (jc_size)prec && str[n] != '\0') { n++; }
    } else {
        while (str[n] != '\0') { n++; }
    }
    if (!(flags & F_MINUS) && n < (jc_size)width) {
        emit_spaces(s, (jc_size)width - n);
    }
    for (i = 0; i < n; i++) {
        sink_putc(s, str[i]);
    }
    if ((flags & F_MINUS) && n < (jc_size)width) {
        emit_spaces(s, (jc_size)width - n);
    }
}

struct spec {
    int flags;
    int width;      /* 0 = none; -1 = '*' (consumed right after parsing) */
    int prec;       /* -1 = none; '*' is marked by prec_star            */
    int prec_star;
};

static int jc_isdigit_c(char c)
{
    return c >= '0' && c <= '9';
}

/* Parse the flags/width/precision grammar ("[-+ 0] width[.precision]") that
 * follows '%'; width and precision may be '*' (an int argument, consumed in
 * STRING order, before the conversion char is even known). */
static void spec_parse(struct spec *sp, const char **pp)
{
    const char *p = *pp;
    sp->flags = 0;
    sp->width = 0;
    sp->prec = -1;
    sp->prec_star = 0;
    for (;;) {
        if (*p == '-') {
            sp->flags |= F_MINUS;
        } else if (*p == '+') {
            sp->flags |= F_PLUS;
        } else if (*p == ' ') {
            sp->flags |= F_SPACE;
        } else if (*p == '0') {
            sp->flags |= F_ZERO;
        } else {
            break;
        }
        p++;
    }
    if (*p == '*') {
        sp->width = -1;
        p++;
    } else {
        while (jc_isdigit_c(*p)) {
            sp->width = sp->width * 10 + (*p - '0');
            p++;
        }
    }
    if (*p == '.') {
        p++;
        if (*p == '*') {
            sp->prec_star = 1;
            p++;
        } else {
            sp->prec = 0;
            while (jc_isdigit_c(*p)) {
                sp->prec = sp->prec * 10 + (*p - '0');
                p++;
            }
        }
    }
    *pp = p;
}

/* THE ALIGNMENT CORE (A-10): a conversion this fallback does not render
 * must still CONSUME the argument its class implies -- never consume-less-
 * print. Integer-ish classes take an int (promoted to long with 'l'), the
 * l-forms a long; %n takes an int * (a pointer, 8 bytes on x86-64 SysV);
 * everything else (floats, pointers, unknowns) consumes a double -- the
 * largest argument the class can carry, so the va_arg list stays in step
 * whatever the format string then asks for. */
static void consume_class(va_list ap, char conv, int flags)
{
    if (conv == 'n') {
        /* %n's operand is an int *, NOT an int (M1079-b / A-10). Reading it
         * back as va_arg(ap,int) advances the va_list by 4 bytes; the
         * pointer occupies an 8-byte slot on x86-64 SysV, so any conversion
         * AFTER a %n re-desyncs the list -- the exact SEGV class A-10 exists
         * to eliminate. Consume the pointer. Do NOT write through it: the
         * fallback may not write outside its sink, and %n is a side-effecting
         * spec -- consuming the slot and ignoring the write is the safe,
         * documented behaviour. */
        (void)va_arg(ap, int *);
    } else if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'o' ||
        conv == 'x' || conv == 'X' ||
        conv == 'l' || conv == 'h' || conv == 'H' || conv == 't' ||
        conv == 'j' || conv == 'q' || conv == 'Q') {
        if (flags & F_LONG) { (void)va_arg(ap, long); }
        else { (void)va_arg(ap, int); }
    } else if (conv == 'p') {
        (void)va_arg(ap, void *);
    } else {
        (void)va_arg(ap, double);
    }
}

int jc_vsnprintf(char *buf, jc_size cap, const char *fmt, va_list ap)
{
    struct sink s;
    char content[80]; /* one conversion's width/precision-aside text       */

    s.buf = buf;
    s.cap = cap;
    s.pos = 0;
    s.want = 0;
    s.content = content;
    s.len = 0;

    while (*fmt) {
        struct spec sp;
        char conv;

        if (*fmt != '%') {
            sink_putc(&s, *fmt);
            fmt++;
            continue;
        }
        fmt++; /* consume '%' */
        spec_parse(&sp, &fmt);

        /* The '*' args come BEFORE the conversion char, in string order:
         * the width star, then the precision star. (A-10's desync: the old
         * code saw '%.*s' as an unknown conversion and consumed nothing.) */
        if (sp.width == -1) {
            int w = va_arg(ap, int);
            sp.width = (w < 0) ? 0 : w;
        }
        if (sp.prec_star) {
            int p = va_arg(ap, int);
            sp.prec = (p < 0) ? -1 : p;
        }
        conv = *fmt;

        /* Length modifier: the only one this fallback's codebase needs is
         * 'l'. A second modifier letter (ll, lz, ...) falls into the else
         * below and is consumed as integer-ish, then printed crudely. */
        if (conv == 'l') {
            sp.flags |= F_LONG;
            fmt++;
            conv = *fmt;
        }
        if (conv == '\0') {
            break; /* trailing '%': print nothing, consume nothing */
        }

        if (conv == 'd' || conv == 'i') {
            long v;
            if (sp.flags & F_LONG) { v = va_arg(ap, long); }
            else { v = (long)va_arg(ap, int); }
            render_long(&s, v, sp.flags);
            emit_padded(&s, sp.flags, sp.width, 0);
        } else if (conv == 'u' || conv == 'o' || conv == 'x' || conv == 'X') {
            unsigned long v;
            int base = (conv == 'x' || conv == 'X') ? 16
                       : (conv == 'o' ? 8 : 10);
            if (sp.flags & F_LONG) { v = va_arg(ap, unsigned long); }
            else { v = (unsigned long)va_arg(ap, unsigned int); }
            render_unsigned(&s, v, base, conv == 'X');
            emit_padded(&s, sp.flags, sp.width, 0);
        } else if (conv == 's') {
            const char *str = va_arg(ap, const char *);
            render_str(&s, str, sp.prec, sp.flags, sp.width);
        } else if (conv == 'c') {
            int c = va_arg(ap, int);
            s.len = 0;
            s.content[s.len++] = (char)c;
            emit_padded(&s, sp.flags, sp.width, 0);
        } else if (conv == 'f' || conv == 'g' || conv == 'e') {
            double d = va_arg(ap, double);
            /* %g and %e render like %f (fixed, default-6): honest in
             * jc_snprintf.h; the finding's class is the desync, not the
             * float fidelity. %f honours the precision; width pads. */
            render_double(&s, d, sp.prec, sp.flags);
            emit_padded(&s, sp.flags, sp.width, sp.prec >= 0);
        } else if (conv == 'p') {
            (void)va_arg(ap, void *);
            sink_puts(&s, "(ptr)"); /* consume, then print the literal class */
        } else if (conv == '%') {
            sink_putc(&s, '%');
        } else {
            /* Unsupported conversion: CONSUME its class (alignment core),
             * then print the specifier crudely -- never consume-less-print. */
            consume_class(ap, conv, sp.flags);
            sink_puts(&s, "%");
            if (sp.flags & F_LONG) { sink_putc(&s, 'l'); }
            sink_putc(&s, conv);
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
