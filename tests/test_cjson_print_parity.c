/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_cjson_print_parity.c - M1084 (F-06): byte-identical parity for the
 * JSON printer's string fast path and jc_utf8_sanitize's ASCII skip.
 *
 * F-06's two fast paths change HOW the bytes get written, not WHAT bytes get
 * written. The finding therefore asks for a parity test rather than a
 * red-first one: the oracles below are SECOND COPIES of the pre-fix code,
 * compiled into this file. Every corpus string is printed through the real
 * printer and through the oracle, and the two outputs must be identical
 * byte for byte; jc_utf8_sanitize is compared the same way against the
 * pre-fix per-position loop, including its return value.
 *
 * This file was committed before the fast paths existed: the byte-parity
 * part of the gate is the invariant the fast paths must not break (green on
 * the unfixed tree, and still green on the fixed one). The proof that the
 * memcpy run actually FIRES, rather than just being present, is the
 * cJSON_pb_run_bytes hook in src/json/cJSON.c, asserted here for every
 * corpus string: every no-escape content byte must have moved through the
 * fast path.
 */
#include "jc_test.h"
#include "jc_json.h"
#include "jc_utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The fast-path hook (M1084), defined in src/json/cJSON.c: the content
 * bytes pb_put_string copied through its no-escape run. Declared here,
 * not in cJSON.h, because it is a test instrument, the same shape as
 * cJSON_jc_string_bytes but kept out of the public header.
 * (test_main.c forward-declares test_json_print_exact the same way.) */
extern void   cJSON_pb_run_reset(void);
extern size_t cJSON_pb_run_bytes(void);

/* ---- oracle 1: the ORIGINAL per-byte string printer (pre-M1084) ---------- *
 * A verbatim copy of pb_put_string as it stood at the F-06 confirmation
 * (cJSON.c:898-927): one pb_ensure+store per content byte, the two-letter
 * escape tokens through pb_puts. The fast path may not touch it; if it does,
 * the oracle is no longer independent and this test proves nothing. */

static struct {
    char  *data;
    size_t len;
    size_t cap;
    int    oom;
} g_orb;

static void orb_ensure(size_t extra)
{
    size_t need = g_orb.len + extra + 1;
    size_t nc;
    char *p;
    if (g_orb.oom) {
        return;
    }
    if (need <= g_orb.cap) {
        return;
    }
    nc = (g_orb.cap == 0) ? 256 : g_orb.cap;
    while (nc < need) {
        nc *= 2;
    }
    p = (char *)realloc(g_orb.data, nc);
    if (p == NULL) {
        g_orb.oom = 1;
        return;
    }
    g_orb.data = p;
    g_orb.cap = nc;
}

static void orb_putc(char c)
{
    orb_ensure(1);
    if (g_orb.oom) return;
    g_orb.data[g_orb.len++] = c;
}

static void orb_puts(const char *s)
{
    size_t n = strlen(s);
    orb_ensure(n);
    if (g_orb.oom) return;
    memcpy(g_orb.data + g_orb.len, s, n);
    g_orb.len += n;
}

static char *oracle_put_string(const char *s)
{
    /* M1087 (SV:1802, batch 9): release the buffer a PREVIOUS call left
     * behind before dropping the pointer. This oracle returns g_orb.data to
     * its caller and does not free it (the caller owns the comparison), so
     * each call's result is freed by the next call. free(NULL) is safe, so
     * the very first call is unaffected. The last live buffer is released
     * at the end of test_cjson_print_parity. */
    free(g_orb.data);
    g_orb.data = NULL;
    g_orb.len = 0;
    g_orb.cap = 0;
    g_orb.oom = 0;
    orb_putc('"');
    if (s != NULL) {
        const unsigned char *p = (const unsigned char *)s;
        while (*p) {
            unsigned char ch = *p;
            switch (ch) {
                case '"':  orb_puts("\\\""); break;
                case '\\': orb_puts("\\\\"); break;
                case '\b': orb_puts("\\b");  break;
                case '\f': orb_puts("\\f");  break;
                case '\n': orb_puts("\\n");  break;
                case '\r': orb_puts("\\r");  break;
                case '\t': orb_puts("\\t");  break;
                default:
                    if (ch < 0x20) {
                        char tmp[8];
                        if (snprintf(tmp, sizeof tmp, "\\u%04x", (unsigned int)ch) < 0)
                            JC_CHECK(0);
                        orb_puts(tmp);
                    } else {
                        orb_putc((char)ch);
                    }
                    break;
            }
            p++;
        }
    }
    orb_putc('"');
    orb_ensure(1);
    if (g_orb.oom) {
        free(g_orb.data);
        return NULL;
    }
    g_orb.data[g_orb.len] = '\0';
    return g_orb.data;
}

/* ---- the corpus ------------------------------------------------------------
 * Every escape class, every control byte, every UTF-8 width, long no-escape
 * runs interrupted at the start / middle / end, raw invalid bytes, and the
 * degenerate empty / one-char cases. Byte values are spelled with escapes,
 * so this file itself stays ASCII. */

#define LONG_ASCII_RUN "0123456789abcdefghijklmnopqrstuvwxyz " \
                       "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()-_=+[]{};" \
                       "':,.<>/?|`~ the quick brown fox jumps over the lazy dog."

static const char *const corpus[] = {
    /* degenerate */
    "",
    "x",
    /* the six two-letter escapes, alone and interleaved */
    "\"",
    "\\",
    "\b", "\f", "\n", "\r", "\t",
    "a\"b\\c\bd\fe\ng\ro\ts",
    /* every control byte 0x01-0x1f (five of them take the two-letter form).
     * 0x00 itself cannot cross the public API: cJSON_CreateString strcpy's
     * its argument, so a NUL terminates the stored string before the printer
     * ever sees it. The printer's own while(*p) loop stops at the same byte,
     * so the oracles agree there by construction. */
    "\x01\x02\x03\x04\x05\x06\x07",
    "\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f",
    "\x10\x11\x12\x13\x14\x15\x16\x17",
    "\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f",
    "\x1f\x0b\x10\x07\x01",
    "\x01", "\x02", "\x1e", "\x1f",
    /* DEL 0x7f needs no JSON escape; 0x20-0x7e neither */
    "\x7f",
    " !\"#$%&'()*+,-./",
    "0123456789:;<=>?",
    "@ABCDEFGHIJKLMNO",
    "PQRSTUVWXYZ[\\]^_",
    "`abcdefghijklmno",
    "pqrstuvwxyz{|}~ ",
    /* non-ASCII: every width, boundary codepoints, and a mixed run */
    "\xc3\xa4",                              /* U+00E4, 2 bytes */
    "\xe2\x80\x94",                          /* U+2014 em dash, 3 bytes */
    "\xf0\x9f\x98\x80",                      /* U+1F600, 4 bytes */
    "\xc2\x80\xc2\xbf\xe2\x80\x8a\xe2\x80\x8b\xe2\x9a\x80",
    "\xf0\x90\x80\x80\xf0\x80\x80\x80\xf4\x8f\xbf\xbf\xf4\x90\x80\x80",
    "caf\xc3\xa9 \xe2\x80\x94 \xf0\x9f\x98\x80 \xc3\xbc",
    /* raw bytes >= 0x80 that are NOT valid sequences: the printer passes
     * them through untouched (it does not validate), so they belong here */
    "\x80", "\xff", "\xc0", "\xc1", "\xf5", "\xc3",
    "\xc3\x28",                              /* truncated 2-byte */
    /* the no-escape run: what the memcpy fast path is for */
    LONG_ASCII_RUN,
    LONG_ASCII_RUN LONG_ASCII_RUN,
    /* runs interrupted by one escape, at the start / middle / end */
    "\"" LONG_ASCII_RUN,
    LONG_ASCII_RUN "\"",
    LONG_ASCII_RUN "\\\\" LONG_ASCII_RUN,
    LONG_ASCII_RUN "\n" LONG_ASCII_RUN,
    "\x01" LONG_ASCII_RUN "\x1f",
    /* escape-only content (the slow path, token by token) */
    "\\\"\b\f\n\r\t\x01\x02\x7f",
    /* the shape of a real request field */
    "  path: src/json/cJSON.c\n  line: 898\n  \"name\": \"pb_put_string\""
};

#define CORPUS_N ((int)(sizeof corpus / sizeof corpus[0]))

/* How many bytes of the content need no escape at all: the count the
 * fast-path hook must report after printing s. */
static size_t noescape_count(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    size_t n = 0;
    while (*p) {
        unsigned char ch = *p;
        if (ch >= 0x20 && ch != 0x22 && ch != 0x5c) {
            n++;
        }
        p++;
    }
    return n;
}

static void test_print_parity_one(const char *s)
{
    cJSON *item;
    char *real, *want;
    int same = -1;

    item = cJSON_CreateString(s);
    JC_CHECK(item != NULL);
    if (item == NULL) {
        return;
    }
    cJSON_pb_run_reset();
    real = cJSON_PrintUnformatted(item);
    want = oracle_put_string(s);
    JC_CHECK(real != NULL && want != NULL);
    if (real != NULL && want != NULL) {
        same = (strcmp(real, want) == 0);
        JC_CHECK(same == 1);
        if (same != 1) {
            printf("    real: %.120s\n    want: %.120s\n", real, want);
        }
        /* The fast path must have fired: every no-escape content byte went
         * through the memcpy run; every escape byte through the slow path.
         * For all-escape strings the right answer is 0. */
        {
            size_t run = cJSON_pb_run_bytes();
            size_t expect = noescape_count(s);
            JC_CHECK(run == expect);
            if (run != expect) {
                printf("    run=%lu expect=%lu\n",
                       (unsigned long)run, (unsigned long)expect);
            }
        }
        free(real);
    }
    cJSON_Delete(item);
}

void test_cjson_print_parity(void)
{
    int i;
    static char long_run[4097];
    char *p;

    printf("  cjson print parity: %d corpus strings\n", CORPUS_N);
    for (i = 0; i < CORPUS_N; i++) {
        test_print_parity_one(corpus[i]);
    }

    /* A genuinely long no-escape run (the memcpy fast path, once it lands,
     * is the point): 4096 printable ASCII bytes plus a NUL (CreateString
     * strlens its argument), and a second one with a single quote in the
     * middle (runs of 2048 + 2047 around it). */
    for (i = 0; i < 4096; i++) {
        long_run[i] = (char)('a' + (i % 26));
    }
    long_run[4096] = '\0';
    test_print_parity_one(long_run);
    p = long_run + 4096 / 2;
    *p = '"';
    test_print_parity_one(long_run);

    /* A few strings are easier to read as explicit expectations than as
     * oracle comparisons; both must hold, and the explicit ones also pin
     * the ORACLE itself (a corrupted oracle would pass a corrupted
     * printer, so the oracle needs its own teeth too). */
    JC_CHECK_STR(oracle_put_string(""), "\"\"");
    JC_CHECK_STR(oracle_put_string("a\"b"), "\"a\\\"b\"");
    JC_CHECK_STR(oracle_put_string("a\\b"), "\"a\\\\b\"");
    JC_CHECK_STR(oracle_put_string("a\nb\tc"), "\"a\\nb\\tc\"");
    JC_CHECK_STR(oracle_put_string("\x01"), "\"\\u0001\"");
    JC_CHECK_STR(oracle_put_string("\x1f"), "\"\\u001f\"");
    JC_CHECK_STR(oracle_put_string("\x7f"), "\"\x7f\"");
    JC_CHECK_STR(oracle_put_string("caf\xc3\xa9"), "\"caf\xc3\xa9\"");

    /* NULL string: CreateString normalises NULL to "", so the printer
     * writes "" and the oracle agrees. */
    {
        cJSON *n = cJSON_CreateString(NULL);
        char *printed;
        JC_CHECK(n != NULL);
        if (n != NULL) {
            printed = cJSON_PrintUnformatted(n);
            JC_CHECK(printed != NULL);
            if (printed != NULL) {
                JC_CHECK_STR(printed, "\"\"");
                free(printed);
            }
            cJSON_Delete(n);
        }
    }

    /* M1087: release the last live oracle buffer; every earlier one was
     * freed at the top of the next oracle_put_string call. */
    free(g_orb.data);
    g_orb.data = NULL;
}

/* ---- oracle 2: the ORIGINAL per-position jc_utf8_sanitize loop ----------- *
 * seq_ok's verdict, re-implemented independently here (the same RFC 3629
 * strictness: overlongs, surrogates and > U+10FFFF are ill-formed), applied
 * at EVERY position, in the pre-fix two-pass shape. The ASCII skip may only
 * change behaviour if seq_ok(p, avail) did not return 1 for a byte < 0x80
 * (jc_utf8.c:123-124: it does, at every avail > 0). */

/* The oracle's strict sequence length, or 0 when p[0..avail) begins none. */
static int orb_seq(const unsigned char *p, int avail)
{
    int n;
    int i;

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

static int oracle_sanitize(const unsigned char *p, int len,
                           char **out, int *out_len)
{
    int i;
    int bad = 0;
    int o;
    char *buf;

    for (i = 0; i < len; ) {
        int n = orb_seq(p + i, len - i);
        if (n == 0) {
            bad++;
            i++;
        } else {
            i += n;
        }
    }
    if (bad == 0) {
        *out = NULL;
        *out_len = 0;
        return 0;
    }
    buf = (char *)malloc((size_t)(len + 2 * bad + 1));
    if (buf == NULL) {
        *out = NULL;
        *out_len = 0;
        return 0;
    }
    i = 0;
    o = 0;
    while (i < len) {
        int n = orb_seq(p + i, len - i);
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
    *out_len = o;
    return 1;
}

static void compare_sanitize(const unsigned char *p, int len)
{
    char *real_out = NULL, *want_out = NULL;
    jc_size real_n = 0, want_n = 0;
    int want_n_i;
    int real_rc, want_rc;
    int same = -1;

    real_rc = jc_utf8_sanitize((const char *)p, (jc_size)len,
                               &real_out, &real_n);
    want_rc = oracle_sanitize(p, len, &want_out, &want_n_i);
    want_n = (jc_size)want_n_i;
    JC_CHECK(real_rc == want_rc);
    if (real_rc && want_rc) {
        JC_CHECK(real_n == want_n);
        same = (real_out != NULL && want_out != NULL &&
                memcmp(real_out, want_out, (size_t)real_n) == 0);
        JC_CHECK(same == 1);
        if (same != 1) {
            jc_size i;
            printf("    len=%d real_rc=%d want_rc=%d real_n=%lu want_n=%lu\n",
                   len, real_rc, want_rc, (unsigned long)real_n,
                   (unsigned long)want_n);
            for (i = 0;
                 i < real_n && i < want_n &&
                 (unsigned char)real_out[i] == (unsigned char)want_out[i];
                 i++) {
            }
            if (i < real_n || i < want_n) {
                printf("    first diff at %lu: %02x vs %02x\n",
                       (unsigned long)i,
                       i < real_n ? (unsigned char)real_out[i] : 0,
                       i < want_n ? (unsigned char)want_out[i] : 0);
            }
        }
    }
    free(real_out);
    free(want_out);
}

static const unsigned char sanitize_cases[] = {
    /* clean ASCII: the common case the skip makes cheap (0x7f is fine too) */
    'a', 'b', ' ', 'z', '\n', '\t', 0x7f,
    /* clean 2/3/4-byte sequences */
    0xC3, 0xA4, 0xE2, 0x80, 0x94, 0xF0, 0x9F, 0x98, 0x80,
    /* mixed: a valid run around each bad byte */
    'c', 0xC3, 0xA9, ' ', 0xFF, 0xE2, 0x80, 0x94, ' ',
    0xF0, 0x9F, 0x98, 0x80, 0x80, 0x80,
    /* overlongs, surrogates, > U+10FFFF, truncated tails */
    0xC0, 0xAF, 0xC1, 0xBF, 0xE0, 0x80, 0xBF, 0xED, 0xA0, 0xBF,
    0xF0, 0x80, 0x80, 0x80, 0xF4, 0x90, 0x80, 0x80, 0xE2, 0x80,
    /* a NUL is ASCII and passes (the scan is length-based, not NUL-based) */
    0, 1, 2, 3, 4, 5, 6, 7
};

#define SAN_CASES_N (int)(sizeof sanitize_cases)

void test_utf8_sanitize_parity(void)
{
    int cut;

    printf("  utf8 sanitize parity: fixture of %d bytes, every prefix\n",
           SAN_CASES_N);
    /* Every prefix of the fixture: a sequence cut off by len (truncated
     * tails) lands at the end of the input on its way. */
    for (cut = 0; cut <= SAN_CASES_N; cut++) {
        compare_sanitize(sanitize_cases, cut);
    }

    /* A long all-ASCII run: the skip's main case. It must come back as
     * "nothing to fix" (return 0, no allocation), exactly as the oracle. */
    {
        static char ascii_run[4096];
        int i;
        for (i = 0; i < (int)sizeof ascii_run; i++) {
            ascii_run[i] = (char)('a' + (i % 26));
        }
        compare_sanitize((const unsigned char *)ascii_run,
                         (int)sizeof ascii_run);
    }
    /* And a long run with one bad byte in the middle: the skip must not
     * swallow it, and the replacement must land at the same offset. */
    {
        static unsigned char mixed_run[4096];
        int i;
        for (i = 0; i < (int)sizeof mixed_run; i++) {
            mixed_run[i] = (unsigned char)('a' + (i % 26));
        }
        mixed_run[sizeof mixed_run / 2] = 0xFF;
        compare_sanitize(mixed_run, (int)sizeof mixed_run);
    }
}
