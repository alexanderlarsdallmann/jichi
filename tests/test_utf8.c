/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_utf8.c - the M127 UTF-8 codepoint + width helpers, plus the M191
 * well-formedness + boundary-safe truncation helpers. */

#include "jc_test.h"
#include "jc_utf8.h"

#include <stdlib.h>
#include <string.h>

/* U+2014 EM DASH, the character that actually caused M191: a run wedged when a
 * 400-byte elision cut kept only its first byte. */
#define EMDASH "\xe2\x80\x94"

static void test_valid(void)
{
    JC_CHECK(jc_utf8_valid("plain ascii", 11));
    JC_CHECK(jc_utf8_valid(EMDASH, 3));
    JC_CHECK(jc_utf8_valid("a\xe3\x81\x82" "b", 5));
    JC_CHECK(jc_utf8_valid("", 0));
    JC_CHECK(jc_utf8_valid(NULL, 0));
    JC_CHECK(jc_utf8_valid("\xf0\x9f\x98\x80", 4));      /* U+1F600 emoji */

    /* the M191 failure shape: a 3-byte sequence cut after one byte */
    JC_CHECK(!jc_utf8_valid("\xe2", 1));
    JC_CHECK(!jc_utf8_valid("\xe2\x80", 2));
    JC_CHECK(!jc_utf8_valid("Phase \xe2\n", 8));
    /* a stray continuation byte (a kept SUFFIX starting mid-character) */
    JC_CHECK(!jc_utf8_valid("\x94 rest", 6));
    /* strict beyond structure: overlong, surrogate, out of range */
    JC_CHECK(!jc_utf8_valid("\xc0\xaf", 2));             /* overlong '/'    */
    JC_CHECK(!jc_utf8_valid("\xe0\x80\xaf", 3));         /* overlong 3-byte */
    JC_CHECK(!jc_utf8_valid("\xed\xa0\x80", 3));         /* U+D800 surrogate*/
    JC_CHECK(!jc_utf8_valid("\xf4\x90\x80\x80", 4));     /* > U+10FFFF      */
    JC_CHECK(!jc_utf8_valid("\xff", 1));
}

static void test_trunc_len(void)
{
    /* "ab" + em-dash + "cd": bytes 0,1 | 2,3,4 | 5,6 */
    const char *s = "ab" EMDASH "cd";

    JC_CHECK(jc_utf8_trunc_len(s, 2) == 2);   /* boundary already */
    JC_CHECK(jc_utf8_trunc_len(s, 3) == 2);   /* would keep 1 of 3 bytes */
    JC_CHECK(jc_utf8_trunc_len(s, 4) == 2);   /* would keep 2 of 3 bytes */
    JC_CHECK(jc_utf8_trunc_len(s, 5) == 5);   /* whole em-dash kept */
    JC_CHECK(jc_utf8_trunc_len(s, 7) == 7);   /* whole string */
    JC_CHECK(jc_utf8_trunc_len(s, 0) == 0);
    JC_CHECK(jc_utf8_trunc_len(NULL, 4) == 0);
    /* a 4-byte sequence: backing off three bytes is the deepest legal case */
    JC_CHECK(jc_utf8_trunc_len("a\xf0\x9f\x98\x80", 4) == 1);
    /* every prefix of a valid string stays valid after trimming */
    {
        jc_size i;
        for (i = 0; i <= 7; i++) {
            JC_CHECK(jc_utf8_valid(s, jc_utf8_trunc_len(s, i)));
        }
    }
}

static void test_resync(void)
{
    const char *s = "ab" EMDASH "cd";        /* len 7 */

    JC_CHECK(jc_utf8_resync(s, 7, 2) == 2);  /* already a lead byte */
    JC_CHECK(jc_utf8_resync(s, 7, 3) == 5);  /* skip 2 continuation bytes */
    JC_CHECK(jc_utf8_resync(s, 7, 4) == 5);  /* skip 1 continuation byte  */
    JC_CHECK(jc_utf8_resync(s, 7, 5) == 5);
    JC_CHECK(jc_utf8_resync(s, 7, 7) == 7);  /* at the end */
    JC_CHECK(jc_utf8_resync(NULL, 7, 3) == 3);
    /* the kept suffix is well-formed for every start offset */
    {
        jc_size i;
        for (i = 0; i <= 7; i++) {
            jc_size off = jc_utf8_resync(s, 7, i);
            JC_CHECK(jc_utf8_valid(s + off, 7 - off));
        }
    }
}

static void test_sanitize(void)
{
    char *out = NULL;
    jc_size out_len = 0;

    /* well-formed input allocates nothing and reports no repair */
    JC_CHECK(jc_utf8_sanitize("clean " EMDASH " text", 12, &out, &out_len) == 0);
    JC_CHECK(out == NULL);

    /* the wedging byte becomes U+FFFD, and the result is well-formed */
    out = NULL;
    JC_CHECK(jc_utf8_sanitize("Phase \xe2\n", 8, &out, &out_len) == 1);
    JC_CHECK(out != NULL);
    JC_CHECK(jc_utf8_valid(out, out_len));
    JC_CHECK(out_len == 10);                    /* 1 bad byte -> 3 bytes */
    JC_CHECK(memcmp(out, "Phase \xef\xbf\xbd\n", 10) == 0);
    JC_CHECK(out[out_len] == '\0');
    free(out);

    /* surrounding good text is preserved byte for byte */
    out = NULL;
    JC_CHECK(jc_utf8_sanitize("a" EMDASH "\xff" "b", 6, &out, &out_len) == 1);
    JC_CHECK(jc_utf8_valid(out, out_len));
    JC_CHECK(memcmp(out, "a" EMDASH "\xef\xbf\xbd" "b", 8) == 0);
    free(out);

    /* an all-garbage run: one replacement per ill-formed byte */
    out = NULL;
    JC_CHECK(jc_utf8_sanitize("\xff\xfe", 2, &out, &out_len) == 1);
    JC_CHECK(out_len == 6);
    JC_CHECK(jc_utf8_valid(out, out_len));
    free(out);

    JC_CHECK(jc_utf8_sanitize(NULL, 0, &out, NULL) == 0);
}

void test_utf8(void)
{
    /* "aあb" : 'a'(1) + U+3042 hiragana(3) + 'b'(1) = 5 bytes */
    const char *s = "a\xe3\x81\x82" "b";
    jc_size n = 5;
    jc_size adv;
    unsigned long cp;

    /* decode each codepoint + its byte length */
    cp = jc_utf8_decode(s, n, 0, &adv);
    JC_CHECK(cp == (unsigned long)'a' && adv == 1);
    cp = jc_utf8_decode(s, n, 1, &adv);
    JC_CHECK(cp == 0x3042UL && adv == 3);
    cp = jc_utf8_decode(s, n, 4, &adv);
    JC_CHECK(cp == (unsigned long)'b' && adv == 1);

    /* prev/next step over whole codepoints */
    JC_CHECK(jc_utf8_next(s, n, 0) == 1);   /* past 'a' */
    JC_CHECK(jc_utf8_next(s, n, 1) == 4);   /* past あ (3 bytes) */
    JC_CHECK(jc_utf8_next(s, n, 4) == 5);   /* past 'b' */
    JC_CHECK(jc_utf8_next(s, n, 5) == 5);   /* clamped at end */
    JC_CHECK(jc_utf8_prev(s, 5) == 4);      /* start of 'b' */
    JC_CHECK(jc_utf8_prev(s, 4) == 1);      /* start of あ */
    JC_CHECK(jc_utf8_prev(s, 1) == 0);      /* start of 'a' */
    JC_CHECK(jc_utf8_prev(s, 0) == 0);

    /* widths: ASCII=1, hiragana=2, combining=0, emoji=2, fullwidth=2 */
    JC_CHECK(jc_utf8_width((unsigned long)'a') == 1);
    JC_CHECK(jc_utf8_width(0x3042UL) == 2);      /* あ */
    JC_CHECK(jc_utf8_width(0x0301UL) == 0);      /* combining acute */
    JC_CHECK(jc_utf8_width(0x1F600UL) == 2);     /* emoji */
    JC_CHECK(jc_utf8_width(0xFF21UL) == 2);      /* fullwidth 'A' */
    JC_CHECK(jc_utf8_width(0xAC00UL) == 2);      /* Hangul syllable */
    JC_CHECK(jc_utf8_width(0x00E9UL) == 1);      /* é (narrow) */

    /* M523: the other two CJK scripts, and the RANGE BOUNDARIES -- an
     * off-by-one in a range table is the classic defect here, and the table is
     * hand-written (jc_utf8_width, a deliberate alternative to libc wcwidth,
     * see docs/ENCODING.md). Japanese and Hangul were pinned; Chinese was not,
     * and no boundary was. */
    JC_CHECK(jc_utf8_width(0x4E00UL) == 2);      /* 一 CJK ideograph, first */
    JC_CHECK(jc_utf8_width(0x9FFFUL) == 2);      /* CJK ideographs, last     */
    JC_CHECK(jc_utf8_width(0x3001UL) == 2);      /* 、 ideographic comma     */
    JC_CHECK(jc_utf8_width(0x1100UL) == 2);      /* Hangul Jamo initial      */
    JC_CHECK(jc_utf8_width(0xD7A3UL) == 2);      /* last Hangul syllable     */
    JC_CHECK(jc_utf8_width(0x4DBFUL) == 2);      /* CJK ext-A, last          */
    JC_CHECK(jc_utf8_width(0x20000UL) == 2);     /* CJK ext-B (beyond BMP)   */

    /* HALFWIDTH KATAKANA IS NARROW, and it sits immediately after the
     * fullwidth block: FF00-FF60 is wide, FF61 onward is one column. A table
     * that widened all of FFxx would break Japanese text that mixes them, and
     * this pair is what would catch it. */
    JC_CHECK(jc_utf8_width(0xFF60UL) == 2);      /* fullwidth block, last    */
    JC_CHECK(jc_utf8_width(0xFF71UL) == 1);      /* ｱ halfwidth katakana     */
    JC_CHECK(jc_utf8_width(0xFF9FUL) == 1);      /* halfwidth block, last    */
    JC_CHECK(jc_utf8_width(0xFFE0UL) == 2);      /* fullwidth cent, wide again */

    /* And just past a wide range: narrow, not wide. Both directions matter --
     * a table checked only for the codepoints it claims cannot fail. */
    JC_CHECK(jc_utf8_width(0xD7A4UL) == 1);      /* past the Hangul block    */
    JC_CHECK(jc_utf8_width(0x2E7FUL) == 1);      /* just before CJK radicals */

    /* display columns of the whole string: 1 + 2 + 1 = 4 */
    JC_CHECK(jc_utf8_str_cols(s, n) == 4);
    JC_CHECK(jc_utf8_str_cols("hello", 5) == 5);

    /* invalid/truncated sequences degrade safely (no over-read) */
    cp = jc_utf8_decode("\xe3\x81", 2, 0, &adv); /* truncated 3-byte */
    JC_CHECK(cp == 0xFFFDUL);
    JC_CHECK(jc_utf8_next("\xff", 1, 0) == 1);   /* invalid lead advances 1 */
    JC_CHECK(jc_utf8_str_cols(NULL, 0) == 0);

    test_valid();
    test_trunc_len();
    test_resync();
    test_sanitize();
}

/* M472: the one rule shared by the paste (input) side and the terminal-write
 * (output) side. A terminal executes some of these bytes -- OSC 52 writes the
 * user's clipboard, ESC[2K erases what jichi printed -- so untrusted text must
 * not reach one verbatim.
 *
 * Both halves matter together: stripping is worthless if it also mangles
 * legitimate text, so the keep cases are asserted as hard as the strip cases. */
void test_ctrl_sanitize(void)
{
    char *out = NULL;
    jc_size n = 0;

    /* --- the predicate, exhaustively over the byte range that decides it --- */
    {
        int c;
        for (c = 0; c < 32; c++) {
            if (c == '\n' || c == '\t') {
                JC_CHECK(jc_ctrl_display_safe((unsigned char)c) == 1);
            } else {
                JC_CHECK(jc_ctrl_display_safe((unsigned char)c) == 0);
            }
        }
        JC_CHECK(jc_ctrl_display_safe(127) == 0);          /* DEL */
        JC_CHECK(jc_ctrl_display_safe(32) == 1);           /* space */
        JC_CHECK(jc_ctrl_display_safe(126) == 1);          /* ~ */
        /* Every byte >= 0x80 passes: these are UTF-8 continuation and lead
         * bytes, and touching them would corrupt multibyte text -- the same
         * reason M363 gave for not stripping C1 bytewise. */
        for (c = 128; c < 256; c++) {
            JC_CHECK(jc_ctrl_display_safe((unsigned char)c) == 1);
        }
    }

    /* --- clean input allocates nothing (the overwhelmingly common case) --- */
    JC_CHECK(jc_ctrl_sanitize("hello world", 11, &out, &n) == 0);
    JC_CHECK(out == NULL);
    JC_CHECK(jc_ctrl_sanitize("tab\there\nand a newline", 21, &out, &n) == 0);
    JC_CHECK(out == NULL);
    /* UTF-8 is untouched: "grüß" in UTF-8 has four bytes >= 0x80. */
    JC_CHECK(jc_ctrl_sanitize("gr\xc3\xbc\xc3\x9f", 6, &out, &n) == 0);
    JC_CHECK(out == NULL);

    /* --- the attacks from the audit --- */
    /* OSC 52: ESC ] 52 ; c ; <base64> BEL -- writes the system clipboard. */
    if (JC_REQUIRE(jc_ctrl_sanitize("A\x1b]52;c;cHduZWQ=\x07" "B", 18,
                                    &out, &n) == 1)) {
        JC_CHECK(strchr(out, 0x1b) == NULL);
        JC_CHECK(strchr(out, 0x07) == NULL);
        JC_CHECK_STR(out, "A]52;c;cHduZWQ=B");   /* inert, printable residue */
        JC_CHECK(n == strlen(out));
        free(out);
        out = NULL;
    }
    /* ESC[2K erases the line jichi just printed -- how an agent hides what it
     * ran. And OSC 0 sets the window title. */
    if (JC_REQUIRE(jc_ctrl_sanitize("x\x1b[2K\x1b]0;t\x07y", 12, &out, &n) == 1)) {
        JC_CHECK(strchr(out, 0x1b) == NULL);
        JC_CHECK_STR(out, "x[2K]0;ty");
        free(out);
        out = NULL;
    }

    /* --- newline and tab SURVIVE a strip that removes its neighbours --- */
    if (JC_REQUIRE(jc_ctrl_sanitize("a\x1b\nb\tc\x07", 7, &out, &n) == 1)) {
        JC_CHECK_STR(out, "a\nb\tc");
        free(out);
        out = NULL;
    }

    /* --- degenerate inputs must not crash --- */
    JC_CHECK(jc_ctrl_sanitize(NULL, 5, &out, &n) == 0);
    JC_CHECK(jc_ctrl_sanitize("x", 1, NULL, &n) == 0);   /* no out: no crash */
    JC_CHECK(jc_ctrl_sanitize("", 0, &out, &n) == 0);
    /* out_len may be NULL. */
    if (JC_REQUIRE(jc_ctrl_sanitize("a\x1b" "b", 3, &out, NULL) == 1)) {
        JC_CHECK_STR(out, "ab");
        free(out);
        out = NULL;
    }
    /* An all-control input strips to empty, and reports it rather than
     * returning a stale length. */
    if (JC_REQUIRE(jc_ctrl_sanitize("\x1b\x07\x01", 3, &out, &n) == 1)) {
        JC_CHECK(n == 0);
        JC_CHECK(out[0] == '\0');
        free(out);
        out = NULL;
    }
}
