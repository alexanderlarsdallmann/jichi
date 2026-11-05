/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_redact.c - secret redaction core (M24). */

#include "jc_test.h"
#include "jc_fcntl.h"   /* JC_O_BINARY (portability_lint 38: file opens) */
#include "jc_log.h"
#include "jc_mem.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

static void test_registry_copies(void);  /* M926 */
static void test_redact_capture_tail(void);  /* M1040 */
static void test_redact_null_register_and_reset(void);  /* M1040-c */
static void test_logf_secret_straddle(void);  /* M1131 (C-13) */
static void test_log_err_body_cap(void);  /* M1131 (C-13) */
static void test_redact_scrub(void);  /* M1137 (C-19) */

void test_redact(void)
{
    char out[256];
    const char *secrets[2];
    int hits;

    secrets[0] = "sk-ant-SECRETKEY-1234";
    secrets[1] = "OPENAI-TOKEN-abcdef";

    /* A single occurrence is replaced with "***". */
    hits = jc_redact_apply(secrets, 1,
                           "auth: Bearer sk-ant-SECRETKEY-1234 done", out,
                           sizeof(out));
    JC_CHECK(hits == 1);
    JC_CHECK(strstr(out, "sk-ant-SECRETKEY-1234") == NULL);
    JC_CHECK(strstr(out, "***") != NULL);
    JC_CHECK(strstr(out, "auth: Bearer ") != NULL);
    JC_CHECK(strstr(out, " done") != NULL);

    /* Multiple occurrences + multiple secrets all scrubbed. */
    hits = jc_redact_apply(secrets, 2,
        "a sk-ant-SECRETKEY-1234 b OPENAI-TOKEN-abcdef c sk-ant-SECRETKEY-1234",
        out, sizeof(out));
    JC_CHECK(hits == 3);
    JC_CHECK(strstr(out, "SECRETKEY") == NULL);
    JC_CHECK(strstr(out, "OPENAI-TOKEN") == NULL);

    /* Secrets shorter than JC_REDACT_MIN are ignored (too generic). */
    {
        const char *shortsec[1];
        shortsec[0] = "abc"; /* < JC_REDACT_MIN */
        hits = jc_redact_apply(shortsec, 1, "abc def abc", out, sizeof(out));
        JC_CHECK(hits == 0);
        JC_CHECK_STR(out, "abc def abc");
    }

    /* No secret present: text passes through unchanged. */
    hits = jc_redact_apply(secrets, 2, "nothing to see here", out, sizeof(out));
    JC_CHECK(hits == 0);
    JC_CHECK_STR(out, "nothing to see here");

    /* NULL input yields an empty string, no crash. */
    hits = jc_redact_apply(secrets, 2, NULL, out, sizeof(out));
    JC_CHECK(hits == 0);
    JC_CHECK_STR(out, "");

    /* Output is always NUL-terminated within bounds even under a tight cap
     * (strlen stays inside the buffer => no out-of-bounds / uninitialised read). */
    {
        char tiny[5];
        jc_redact_apply(secrets, 2, "sk-ant-SECRETKEY-1234", tiny,
                        sizeof(tiny));
        JC_CHECK(strlen(tiny) < sizeof(tiny));
    }

    /* The registry wrapper scrubs a registered key. */
    JC_CHECK(jc_redact_active() == 0); /* nothing registered yet */
    jc_redact_register("REGISTERED-SECRET-9999");
    JC_CHECK(jc_redact_active() == 1); /* now a content sink knows to scrub */
    hits = jc_redact_secrets("x REGISTERED-SECRET-9999 y", out, sizeof(out));
    JC_CHECK(hits == 1);

    JC_CHECK(strstr(out, "REGISTERED-SECRET-9999") == NULL);
    test_registry_copies();  /* M926 */
    test_redact_capture_tail();  /* M1040 */
    test_redact_null_register_and_reset();  /* M1040-c */
    test_logf_secret_straddle();  /* M1131 (C-13): a secret straddling the old
                                   * 4095 cut must not leak its prefix */
    test_log_err_body_cap();  /* M1131 (C-13): the clients' log-body cap helper */
    test_redact_scrub();  /* M1137 (C-19): the shared fail-closed scrub helper */
}

/* M1137 (C-19): the six malloc-fails-open redaction sinks now share this
 * helper, which fails CLOSED. These rows are its unit teeth (non-FAULT: the
 * probe is inert and a healthy malloc succeeds, so a registered secret must
 * come back redacted and the empty-string edge must be NUL-safe). The RED legs
 * that prove the sinks take their degrade arm live in test_session.c and
 * test_eventlog.c under FAULT=1. The registry is clean on entry (the rows
 * above each end in jc_redact_reset). */
static void test_redact_scrub(void)
{
    char *r;

    jc_redact_register("SCRUB-SECRET-424242");
    JC_CHECK(jc_redact_active() == 1);

    /* A secret-bearing buffer is scrubbed into a fresh malloc'd copy the caller
     * owns: the original stays put, the copy has the secret gone and *** in. */
    r = jc_redact_scrub("x SCRUB-SECRET-424242 y", 23);
    if (JC_REQUIRE(r != NULL)) {
        JC_CHECK_STR(r, "x *** y");
        JC_CHECK(strstr(r, "SCRUB-SECRET-424242") == NULL);
        JC_CHECK(strstr(r, "***") != NULL);
        free(r);
    }

    /* With no hit the copy is a byte-identical NUL-terminated prefix. */
    r = jc_redact_scrub("plain text", 10);
    if (JC_REQUIRE(r != NULL)) {
        JC_CHECK_STR(r, "plain text");
        JC_CHECK(strstr(r, "SCRUB-SECRET-424242") == NULL);
        free(r);
    }

    /* NUL-safe basics: the empty string is fine (malloc(1), copy of ""). */
    r = jc_redact_scrub("", 0);
    if (JC_REQUIRE(r != NULL)) {
        JC_CHECK_STR(r, "");
        free(r);
    }

    jc_redact_reset();  /* clear the registry for the next test */
}

/* M1131 (C-13): a registered secret that straddles the old 4095-byte cut of the
 * secrets path (jc_logf branch 1) used to have only its near half left behind
 * -- redaction is an exact strncmp over the WHOLE secret, so the cut near half
 * did not match and was printed. Redact the FULL text instead: the 15-char
 * near half must be absent, the full secret absent, and *** present.
 *
 * The A-count is 4080 (not 4070): the old cut lands at msg[4096]-1 = 4095, so
 * for the 15-char near half "C13STRADDLESECR" to occupy exactly bytes
 * 4080..4095, the secret must start at offset 4080. The secret is 22 chars
 * ("C13STRADDLESECRETKEYAB"), so it straddles the cut 15/7. */
static void test_logf_secret_straddle(void)
{
    char msg[4200];
    char *content = NULL;
    int fd;
    int saved;
    int prev_level;
    size_t pos, i;
    const char *secret = "C13STRADDLESECRETKEYAB";
    const char *near15 = "C13STRADDLESECR";  /* the leaked prefix pre-fix */

    memset(msg, 'A', 4080);
    strcpy(msg + 4080, secret);  /* 4080 + 22 = 4102 < sizeof(msg) */

    prev_level = jc_log_get_level();
    jc_log_set_level(JC_LOG_DEBUG);
    jc_redact_register(secret);

    fd = open(jc_test_tmp("m1131_stderr.txt"),
              O_WRONLY | O_CREAT | O_EXCL | JC_O_BINARY, 0600);
    if (JC_REQUIRE(fd >= 0)) {
        saved = dup(2);
        if (JC_REQUIRE(saved >= 0)) {
            JC_CHECK(dup2(fd, 2) == 2);  /* redirect stderr to the temp file */
            jc_logf(JC_LOG_DEBUG, "%s", msg);
            dup2(saved, 2);              /* restore stderr */
            close(saved);
            close(fd);
            fd = open(jc_test_tmp("m1131_stderr.txt"), O_RDONLY | JC_O_BINARY, 0);
            if (JC_REQUIRE(fd >= 0)) {
                content = (char *)malloc(8192);
                if (JC_REQUIRE(content != NULL)) {
                    pos = 0;
                    while (1) {
                        ssize_t r = read(fd, content + pos, 8192 - pos - 1);
                        if (r <= 0) {
                            break;
                        }
                        pos += (size_t)r;
                        if (pos >= 8192 - 1) {
                            break;
                        }
                    }
                    content[pos] = '\0';
                }
                close(fd);
            }
            if (content != NULL) {
                /* The full secret must not be present... */
                JC_CHECK(strstr(content, secret) == NULL);
                /* ...and the 15-char near half (the cut prefix) must not be. */
                JC_CHECK(strstr(content, near15) == NULL);
                /* The redaction fired (a *** marker is present). */
                JC_CHECK(strstr(content, "***") != NULL);
                /* The 4080-A region was still emitted (the message was not
                 * lost by the fix -- only its redacted length changed). */
                for (i = 0; i + 10 <= pos; i++) {
                    if (content[i] == 'A') {
                        break;
                    }
                }
                if (i < pos) {
                    JC_CHECK(memcmp(content + i, "AAAAAAAAAA", 10) == 0);
                } else {
                    JC_CHECK(0);  /* no A region found -- the line was cut */
                }
            }
        }
    }
    (void)fd;
    if (content != NULL) {
        free(content);
    }
    unlink(jc_test_tmp("m1131_stderr.txt"));
    jc_redact_reset();
    jc_log_set_level(prev_level);
}

/* M1040-c unit checks: NULL register is a no-op; reset clears the registry. */
static void test_redact_null_register_and_reset(void)
{
    /* (a) jc_redact_register(NULL) is a NO-OP: register a secret, call
     * register(NULL), the secret is still active. */
    {
        const char *sec = "NULL-NO-OP-SECRET";
        jc_redact_register(sec);
        JC_CHECK(jc_redact_active() == 1);
        jc_redact_register(NULL);  /* should be no-op */
        JC_CHECK(jc_redact_active() == 1);  /* secret still active */
        jc_redact_reset();
    }

    /* (b) jc_redact_reset() clears: after reset, jc_redact_active() == 0
     * and a fresh registration works. */
    {
        const char *sec = "RESET-TEST-SECRET";
        jc_redact_register(sec);
        JC_CHECK(jc_redact_active() == 1);
        jc_redact_reset();
        JC_CHECK(jc_redact_active() == 0);
        /* Fresh registration works */
        jc_redact_register(sec);
        JC_CHECK(jc_redact_active() == 1);
        jc_redact_reset();
    }
}

/* M1040 (the 2047-copy-boundary leak) unit checks for the tail-cut helper. */
static void test_redact_capture_tail(void)
{
    char buf[256];
    int hits;
    jc_size new_len;

    /* (a) truncated capture ending in a 6+ byte strict prefix -> cut, new_len correct,
     *     bytes past the stars zeroed, *** present. */
    {
        const char *sec = "SECRETKEY12345678901";  /* 20 chars */
        const char *tail = "SECRETKEY12345";  /* 14 chars, secret[0..13] */
        memset(buf, 'x', 8);
        memcpy(buf + 8, tail, 14);
        buf[22] = '\0';
        jc_redact_register(sec);
        /* cap_len=22, cap_total=256 (non-truncated, no cut) */
        hits = jc_redact_capture_tail(buf, 22, 256, &new_len);
        JC_CHECK(hits == 0);  /* no cut (not truncated) */
        JC_CHECK(new_len == 22);
        JC_CHECK_STR(buf + 8, tail);  /* unchanged */
        jc_redact_reset();
    }

    /* (b) truncated capture ending in a 6+ byte strict prefix -> cut. */
    {
        const char *sec = "SECRETKEY12345678901";  /* 20 chars */
        const char *tail = "SECRETKEY12345";  /* 14 chars, secret[0..13] */
        memset(buf, 'x', 8);
        memcpy(buf + 8, tail, 14);
        buf[22] = '\0';
        jc_redact_register(sec);
        /* cap_len=22, cap_total=23 (truncated, cap_len == cap_total - 1) */
        hits = jc_redact_capture_tail(buf, 22, 23, &new_len);
        JC_CHECK(hits == 1);  /* one tail was cut */
        JC_CHECK(new_len == 11);  /* 8 + 3 stars = 11 */
        JC_CHECK(memcmp(buf + 8, "***", 3) == 0);  /* the 14-byte tail is now *** */
        JC_CHECK(strlen(buf + 11) == 0);  /* NUL-terminated after *** */
        /* Verify bytes past NUL are zeroed */
        JC_CHECK(buf[12] == '\0');
        JC_CHECK(buf[13] == '\0');
        jc_redact_reset();
    }

    /* (c) truncated capture ending in only a 1-5 byte prefix -> NO cut (min length). */
    {
        const char *sec = "SECRETKEY12345678901";  /* 20 chars */
        const char *tail = "SECRE";  /* 5 chars, < JC_REDACT_MIN */
        memset(buf, 'x', 8);
        memcpy(buf + 8, tail, 5);
        buf[13] = '\0';
        jc_redact_register(sec);
        /* cap_len=13, cap_total=14 (truncated) */
        hits = jc_redact_capture_tail(buf, 13, 14, &new_len);
        JC_CHECK(hits == 0);  /* no cut (tail < JC_REDACT_MIN) */
        JC_CHECK(new_len == 13);
        JC_CHECK_STR(buf + 8, tail);  /* unchanged */
        jc_redact_reset();
    }

    /* (d) NON-truncated capture ending in a long strict prefix -> NO cut (truncation gate). */
    {
        const char *sec = "tsecretkey00000000000000000000";  /* 30 chars, starts with 't' */
        const char *body = "too many requests";  /* 17 chars, 7-char prefix 'requests' */
        strcpy(buf, body);
        jc_redact_register(sec);
        /* cap_len=17, cap_total=256 (non-truncated) */
        hits = jc_redact_capture_tail(buf, 17, 256, &new_len);
        JC_CHECK(hits == 0);  /* no cut (not truncated) */
        JC_CHECK(new_len == 17);
        JC_CHECK_STR(buf, body);  /* unchanged - no over-redaction */
        jc_redact_reset();
    }

    /* (d) multiple secrets, tail matches the second. */
    {
        const char *sec1 = "FIRSTSECRET";
        const char *sec2 = "SECONDSECRETKEY123";  /* 20 chars */
        const char *tail2 = "SECONDSECRETKEY12";  /* 17 chars, tail of sec2 */
        memset(buf, 'a', 5);
        memcpy(buf + 5, tail2, 17);
        buf[22] = '\0';
        jc_redact_register(sec1);
        jc_redact_register(sec2);
        /* cap_len=22, cap_total=23 (truncated) */
        hits = jc_redact_capture_tail(buf, 22, 23, &new_len);
        JC_CHECK(hits == 1);  /* one hit from second secret */
        JC_CHECK(new_len == 8);  /* 5 + 3 stars = 8 */
        JC_CHECK(memcmp(buf + 5, "***", 3) == 0);
        jc_redact_reset();
    }

    /* (e) a tail longer than the secret (T >= L never fires) -> no-op. */
    {
        const char *sec = "SHORT";  /* 5 chars */
        (void)sec;  /* not used with stub */
        strcpy(buf, "SHORTER");  /* 7 chars, longer than secret */
        hits = jc_redact_capture_tail(buf, 7, 256, &new_len);
        JC_CHECK(hits == 0);  /* no op */
        JC_CHECK(new_len == 7);
        JC_CHECK_STR(buf, "SHORTER");
    }

    /* (f) cap_len 0 / 1-byte tails -> no crash, no-op. */
    {
        buf[0] = '\0';
        hits = jc_redact_capture_tail(buf, 0, 256, &new_len);
        JC_CHECK(hits == 0);
        JC_CHECK(new_len == 0);
        buf[0] = 'a';
        buf[1] = '\0';
        hits = jc_redact_capture_tail(buf, 1, 256, &new_len);
        JC_CHECK(hits == 0);
        JC_CHECK(new_len == 1);
    }

    /* (g) jc_redact_secrets still redacts a complete match (regression pin). */
    {
        const char *regress_sec = "REGRESS-SECRET-123";
        (void)regress_sec;  /* used for registration */
        jc_redact_register(regress_sec);
        strcpy(buf, "prefix REGRESS-SECRET-123 suffix");
        hits = jc_redact_secrets(buf, buf, sizeof(buf));
        JC_CHECK(hits == 1);
        JC_CHECK(strstr(buf, "REGRESS-SECRET-123") == NULL);
        JC_CHECK(strstr(buf, "***") != NULL);
        jc_redact_reset();  /* clear the registry */
    }

    /* (e) cut tail followed by a length-based read: after the cut, the byte at
     * the OLD tail position is zero (the jc_http_body_ctx_overflow shape). */
    {
        const char *sec = "tsecretkey00000000000000000000";  /* 30 chars */
        const char *tail = "tsecretkey000000";  /* 16 chars, strict prefix */
        memset(buf, 'x', 8);
        memcpy(buf + 8, tail, 16);
        buf[24] = '\0';
        jc_redact_register(sec);
        /* cap_len=24, cap_total=25 (truncated) */
        hits = jc_redact_capture_tail(buf, 24, 25, &new_len);
        JC_CHECK(hits == 1);  /* cut */
        JC_CHECK(new_len == 11);  /* 8 + 3 stars = 11 */
        /* After the NUL at position 11, bytes should be zero */
        JC_CHECK(buf[11] == '\0');
        JC_CHECK(buf[12] == '\0');  /* first zeroed byte */
        JC_CHECK(buf[23] == '\0');  /* last byte of original capture */
        jc_redact_reset();
    }

    /* (f) full secret at the end of a truncated capture -> no cut
     * (tail == whole secret is a COMPLETE match, the normal redactor's job). */
    {
        const char *sec = "FULLSECRET1234";  /* 16 chars */
        strcpy(buf, sec);
        jc_redact_register(sec);
        /* cap_len=16, cap_total=17 (truncated) */
        hits = jc_redact_capture_tail(buf, 16, 17, &new_len);
        JC_CHECK(hits == 0);  /* no cut (tail == whole secret, full match handled elsewhere) */
        JC_CHECK(new_len == 16);
        JC_CHECK_STR(buf, sec);  /* unchanged */
        jc_redact_reset();
    }

    /* (g) NON-truncated capture ending in the full secret -> no cut
     * (truncation gate prevents action, jc_redact_secrets handles full secrets). */
    {
        const char *sec = "FULLSECRET1234";  /* 16 chars */
        strcpy(buf, sec);
        jc_redact_register(sec);
        /* cap_len=16, cap_total=256 (non-truncated) */
        hits = jc_redact_capture_tail(buf, 16, 256, &new_len);
        JC_CHECK(hits == 0);  /* no cut (not truncated) */
        JC_CHECK(new_len == 16);
        JC_CHECK_STR(buf, sec);  /* unchanged */
        /* Full secret is handled by jc_redact_secrets, not this helper */
        hits = jc_redact_secrets(buf, buf, sizeof(buf));
        JC_CHECK(hits == 1);
        JC_CHECK(strstr(buf, "***") != NULL);
        jc_redact_reset();
    }

    /* (h) NULL new_len_out (M1040-d): the guard must NOT dereference a NULL
     * out-param, and the cut still happens in place (the out-param is
     * optional; the buffer is the record). cap_len=20 == cap_total-1 (21)
     * passes the truncation gate; tail "NULL-GUARD-SECRE" (16 bytes,
     * secret[0..15]) matches at S=3 -> *** at buf[3..5], NUL at buf[6]. */
    {
        const char *sec = "NULL-GUARD-SECRET123";  /* 19 chars */
        const char *tail = "NULL-GUARD-SECRE";  /* 16 chars, strict prefix */
        memset(buf, 'x', 3);
        memcpy(buf + 3, tail, 16);
        buf[19] = '\0';
        jc_redact_register(sec);
        /* cap_len=19, cap_total=20 (truncated) */
        hits = jc_redact_capture_tail(buf, 19, 20, NULL);
        JC_CHECK(hits == 1);  /* no crash, and the cut still happened */
        JC_CHECK(memcmp(buf + 3, "***", 3) == 0);  /* in place at S=19-16 */
        JC_CHECK(buf[3 + 3] == '\0');  /* NUL after the stars */
        jc_redact_reset();
    }
}

/* M926 (R05-F1): a secret registered from a turn-lived arena must survive
 * that arena's reset. The @ref: path registers values strdup'd onto the
 * per-turn scratch; the registry kept the POINTER (the contract said "must
 * outlive the process"), so at the next turn boundary jc_arena_reset left it
 * pointing into freed memory -- a UAF, and the redaction silently died. */
static void test_registry_copies(void)
{
    struct jc_arena *a = jc_arena_new(0);
    char *s;
    char out[160];
    int hits;

    s = (char *)jc_arena_alloc(a, 32);
    strcpy(s, "ARENA-SECRET-M926-42");
    jc_redact_register(s);
    JC_CHECK(jc_redact_active() == 1);
    /* The turn boundary: the scratch is reset and the SAME offset is handed
     * back to a DIFFERENT secret. A registry that kept the pointer now
     * redacts the new secret's bytes and leaves the original in the log. */
    jc_arena_reset(a);
    s = (char *)jc_arena_alloc(a, 32);
    strcpy(s, "OVERRUN-OTHER-SECRET-7");
    hits = jc_redact_secrets("leak: ARENA-SECRET-M926-42 done", out,
                             sizeof(out));
    JC_CHECK(hits == 1);
    JC_CHECK(strstr(out, "ARENA-SECRET-M926-42") == NULL);
    jc_arena_free(a);
    jc_redact_reset();  /* cleanup for next test */
}

/* M1131 (C-13): the network clients' log-body cap helper. The five clients log
 * the whole (multi-megabyte, transfer-ceiling-bounded) response body into a
 * single jc_logf; jc_log_err_body_cap copies the first `cap` bytes of it so a
 * huge body cannot be formatted uncut into one line. */
static void test_log_err_body_cap(void)
{
    /* Forward declaration: net_util.h lives in src/net (not on the test
     * include path), but the definition is linked in from src/net/net_util.o.
     * Without the helper the build fails with an undefined reference -- the
     * part-2 red. */
    char *jc_log_err_body_cap(const char *body, jc_size cap);

    char big[5001];
    char *cp;
    int i;

    /* A NULL body returns NULL. */
    JC_CHECK(jc_log_err_body_cap(NULL, 2048) == NULL);

    /* A long body (5000 distinct chars) capped at 2048: exactly 2048 bytes,
     * NUL-terminated, equal to the first 2048 chars. */
    for (i = 0; i < 5000; i++) {
        big[i] = (char)('a' + (i % 26));
    }
    big[5000] = '\0';
    cp = jc_log_err_body_cap(big, 2048);
    if (JC_REQUIRE(cp != NULL)) {
        JC_CHECK(strlen(cp) == 2048);
        JC_CHECK(cp[2048] == '\0');
        JC_CHECK(memcmp(cp, big, 2048) == 0);
        /* It is a heap COPY, not the input: mutating the input is irrelevant,
         * and the pointer is distinct. */
        JC_CHECK(cp != big);
        free(cp);
    }

    /* A short body is copied whole (the cap is a ceiling, not a length). */
    cp = jc_log_err_body_cap("short", 2048);
    if (JC_REQUIRE(cp != NULL)) {
        JC_CHECK_STR(cp, "short");
        free(cp);
    }
}
