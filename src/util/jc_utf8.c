/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_utf8.c - UTF-8 codepoint + width helpers (see jc_utf8.h). Pure, no locale. */

#include "jc_utf8.h"

#include <stdlib.h>
#include <string.h>

unsigned long jc_utf8_decode(const char *s, jc_size len, jc_size pos,
                             jc_size *adv)
{
    unsigned char c;
    unsigned long cp;
    int n, i;
    if (adv != NULL) *adv = 1;
    if (s == NULL || pos >= len) return 0;
    c = (unsigned char)s[pos];
    if (c < 0x80) {
        return (unsigned long)c;             /* 1-byte / ASCII */
    } else if ((c & 0xE0) == 0xC0) {
        n = 2; cp = (unsigned long)(c & 0x1F);
    } else if ((c & 0xF0) == 0xE0) {
        n = 3; cp = (unsigned long)(c & 0x0F);
    } else if ((c & 0xF8) == 0xF0) {
        n = 4; cp = (unsigned long)(c & 0x07);
    } else {
        return 0xFFFDUL;                     /* invalid lead byte */
    }
    if (pos + (jc_size)n > len) return 0xFFFDUL; /* truncated */
    for (i = 1; i < n; i++) {
        unsigned char cc = (unsigned char)s[pos + (jc_size)i];
        if ((cc & 0xC0) != 0x80) return 0xFFFDUL; /* bad continuation */
        cp = (cp << 6) | (unsigned long)(cc & 0x3F);
    }
    if (adv != NULL) *adv = (jc_size)n;
    return cp;
}

jc_size jc_utf8_prev(const char *s, jc_size pos)
{
    if (s == NULL || pos == 0) return 0;
    pos--;
    while (pos > 0 && ((unsigned char)s[pos] & 0xC0) == 0x80) pos--;
    return pos;
}

jc_size jc_utf8_next(const char *s, jc_size len, jc_size pos)
{
    jc_size adv = 1;
    if (s == NULL || pos >= len) return len;
    (void)jc_utf8_decode(s, len, pos, &adv);
    if (adv == 0) adv = 1;
    return (pos + adv > len) ? len : pos + adv;
}

static int in_range(unsigned long cp, unsigned long lo, unsigned long hi)
{
    return cp >= lo && cp <= hi;
}

int jc_utf8_width(unsigned long cp)
{
    if (cp == 0) return 0;
    /* Zero-width: combining marks + format/zero-width controls (pragmatic
     * subset covering the common cases in code/prose). */
    if (in_range(cp, 0x0300, 0x036F) || in_range(cp, 0x0483, 0x0489) ||
        in_range(cp, 0x0591, 0x05BD) || in_range(cp, 0x0610, 0x061A) ||
        in_range(cp, 0x064B, 0x065F) || cp == 0x0670 ||
        in_range(cp, 0x06D6, 0x06DC) || in_range(cp, 0x0730, 0x074A) ||
        in_range(cp, 0x200B, 0x200F) || in_range(cp, 0x202A, 0x202E) ||
        in_range(cp, 0x2060, 0x2064) || in_range(cp, 0xFE00, 0xFE0F) ||
        cp == 0xFEFF) {
        return 0;
    }
    /* Wide: East-Asian Wide / Fullwidth blocks + common emoji. */
    if (in_range(cp, 0x1100, 0x115F) || in_range(cp, 0x2329, 0x232A) ||
        in_range(cp, 0x2E80, 0x303E) || in_range(cp, 0x3041, 0x33FF) ||
        in_range(cp, 0x3400, 0x4DBF) || in_range(cp, 0x4E00, 0x9FFF) ||
        in_range(cp, 0xA000, 0xA4CF) || in_range(cp, 0xAC00, 0xD7A3) ||
        in_range(cp, 0xF900, 0xFAFF) || in_range(cp, 0xFE10, 0xFE19) ||
        in_range(cp, 0xFE30, 0xFE6F) || in_range(cp, 0xFF00, 0xFF60) ||
        in_range(cp, 0xFFE0, 0xFFE6) || in_range(cp, 0x1F300, 0x1F64F) ||
        in_range(cp, 0x1F900, 0x1F9FF) || in_range(cp, 0x20000, 0x3FFFD)) {
        return 2;
    }
    return 1;
}

int jc_utf8_str_cols(const char *s, jc_size n)
{
    jc_size i = 0;
    int cols = 0;
    if (s == NULL) return 0;
    while (i < n) {
        jc_size adv = 1;
        unsigned long cp = jc_utf8_decode(s, n, i, &adv);
        cols += jc_utf8_width(cp);
        i += (adv == 0) ? 1 : adv;
    }
    return cols;
}

/* ---- well-formedness (M191) --------------------------------------------- *
 * jc_utf8_decode above is deliberately LENIENT: it returns U+FFFD for a bad
 * sequence so the line editor can keep moving. Truncation and sanitisation need
 * the opposite -- a STRICT verdict -- and cannot use that return value, because
 * U+FFFD is itself a legal codepoint a file may contain. Hence a separate
 * validator. Ranges are RFC 3629: overlongs, surrogates and > U+10FFFF are all
 * ill-formed, not merely unusual. */

/* Length of the well-formed sequence at `p` (with `avail` bytes readable), or 0
 * when `p` does not begin one. */
static jc_size seq_ok(const unsigned char *p, jc_size avail)
{
    jc_size n;
    jc_size i;

    if (avail == 0) {
        return 0;
    }
    if (p[0] < 0x80) {
        return 1;
    } else if (p[0] >= 0xC2 && p[0] <= 0xDF) {
        n = 2;
    } else if (p[0] >= 0xE0 && p[0] <= 0xEF) {
        n = 3;
    } else if (p[0] >= 0xF0 && p[0] <= 0xF4) {
        n = 4;
    } else {
        return 0; /* continuation byte, or 0xC0/0xC1/0xF5..0xFF */
    }
    if (n > avail) {
        return 0; /* truncated at the end of the buffer */
    }
    /* The second byte carries the range restriction that rules out overlongs
     * (0xE0/0xF0), the UTF-16 surrogates (0xED) and > U+10FFFF (0xF4). */
    switch (p[0]) {
        case 0xE0: if (p[1] < 0xA0 || p[1] > 0xBF) return 0; break;
        case 0xED: if (p[1] < 0x80 || p[1] > 0x9F) return 0; break;
        case 0xF0: if (p[1] < 0x90 || p[1] > 0xBF) return 0; break;
        case 0xF4: if (p[1] < 0x80 || p[1] > 0x8F) return 0; break;
        default:   if ((p[1] & 0xC0) != 0x80) return 0; break;
    }
    for (i = 2; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            return 0;
        }
    }
    return n;
}

int jc_utf8_valid(const char *s, jc_size len)
{
    const unsigned char *p = (const unsigned char *)s;
    jc_size i = 0;

    if (s == NULL) {
        return 1;
    }
    while (i < len) {
        jc_size n = seq_ok(p + i, len - i);
        if (n == 0) {
            return 0;
        }
        i += n;
    }
    return 1;
}

jc_size jc_utf8_trunc_len(const char *s, jc_size n)
{
    jc_size back = 0;

    if (s == NULL || n == 0) {
        return 0;
    }
    /* s[n] is the first byte that would be DROPPED. If it is a continuation
     * byte, the sequence that starts earlier is being split, so step back to
     * that sequence's lead byte and cut in front of it. A lead byte is at most
     * three continuation bytes away; a longer run means the input was already
     * ill-formed, which is the sanitiser's problem, not the truncator's. */
    while (back < 3 && back < n && ((unsigned char)s[n - back] & 0xC0) == 0x80) {
        back++;
    }
    if (((unsigned char)s[n - back] & 0xC0) == 0x80) {
        return n;
    }
    return n - back;
}

jc_size jc_utf8_resync(const char *s, jc_size len, jc_size off)
{
    jc_size steps = 0;

    if (s == NULL) {
        return off;
    }
    /* Skip the tail end of a sequence whose lead byte lies before `off`, so a
     * kept SUFFIX starts on a character boundary. Bounded for the same reason
     * as above. */
    while (off < len && steps < 3 && ((unsigned char)s[off] & 0xC0) == 0x80) {
        off++;
        steps++;
    }
    return off;
}

int jc_utf8_sanitize(const char *s, jc_size len, char **out, jc_size *out_len)
{
    const unsigned char *p = (const unsigned char *)s;
    jc_size i;
    jc_size bad = 0;
    jc_size o;
    char *buf;

    if (s == NULL || out == NULL) {
        return 0;
    }
    i = 0;
    while (i < len) {
        jc_size n = seq_ok(p + i, len - i);
        if (n == 0) {
            bad++;
            i++;
        } else {
            i += n;
        }
    }
    if (bad == 0) {
        return 0; /* the common case: no scan result to carry, no allocation */
    }
    /* U+FFFD is three bytes, so each replaced byte grows the text by two. */
    buf = (char *)malloc((size_t)(len + 2 * bad + 1));
    if (buf == NULL) {
        return 0; /* caller keeps the original; a backstop must not fail hard */
    }
    i = 0;
    o = 0;
    while (i < len) {
        jc_size n = seq_ok(p + i, len - i);
        if (n == 0) {
            buf[o++] = (char)0xEF;
            buf[o++] = (char)0xBF;
            buf[o++] = (char)0xBD;
            i++;
        } else {
            memcpy(buf + o, p + i, (size_t)n);
            o += n;
            i += n;
        }
    }
    buf[o] = '\0';
    *out = buf;
    if (out_len != NULL) {
        *out_len = o;
    }
    return 1;
}

/* --- C0 control characters and the terminal (M472; see jc_utf8.h) ----------- */

int jc_ctrl_display_safe(unsigned char c)
{
    if (c == (unsigned char)'\n' || c == (unsigned char)'\t') {
        return 1; /* real content; the column math accounts for both */
    }
    if (c < 32 || c == 127) {
        return 0; /* ESC, BEL, NUL, CR, ... and DEL */
    }
    return 1;     /* printable ASCII and every UTF-8 byte >= 0x80 */
}

int jc_ctrl_sanitize(const char *s, jc_size len, char **out, jc_size *out_len)
{
    jc_size i;
    jc_size bad = 0;
    jc_size o;
    char *buf;

    if (s == NULL || out == NULL) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (!jc_ctrl_display_safe((unsigned char)s[i])) {
            bad++;
        }
    }
    if (bad == 0) {
        return 0; /* the common case: nothing to carry, no allocation */
    }
    /* Stripping never grows the text, so len + 1 always suffices. */
    buf = (char *)malloc((size_t)(len + 1));
    if (buf == NULL) {
        return 0; /* caller keeps the original; a backstop must not fail hard */
    }
    o = 0;
    for (i = 0; i < len; i++) {
        if (jc_ctrl_display_safe((unsigned char)s[i])) {
            buf[o++] = s[i];
        }
    }
    buf[o] = '\0';
    *out = buf;
    if (out_len != NULL) {
        *out_len = o;
    }
    return 1;
}
