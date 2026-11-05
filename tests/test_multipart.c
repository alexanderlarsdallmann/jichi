/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_multipart.c - multipart/form-data body builder (M33). */

#include "jc_test.h"
#include "jc_multipart.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

/* Search `hay` (length `n`, may contain NULs) for the NUL-terminated `needle`. */
static int mem_has(const char *hay, jc_size n, const char *needle)
{
    jc_size nl = (jc_size)strlen(needle);
    jc_size i;
    if (nl == 0 || nl > n) {
        return 0;
    }
    for (i = 0; i + nl <= n; i++) {
        if (memcmp(hay + i, needle, nl) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Count the occurrences of the NUL-terminated `needle` in `hay` (length `n`,
 * may contain NULs). Binary-safe (memcmp, not strstr). The delimiter this
 * counts is "--" + a boundary that ends in hex characters, so it cannot
 * overlap itself; advancing a full match after a hit is exact. */
static int mem_count(const char *hay, jc_size n, const char *needle)
{
    jc_size nl = (jc_size)strlen(needle);
    jc_size i;
    jc_size c = 0;
    if (nl == 0 || nl > n) {
        return 0;
    }
    for (i = 0; i + nl <= n; i++) {
        if (memcmp(hay + i, needle, nl) == 0) {
            c++;
            i += nl - 1;
        }
    }
    return (int)c;
}

void test_multipart(void)
{
    struct jc_multipart mp;
    char ctype[160];
    char *body;
    jc_size len = 0;
    /* File bytes with an embedded NUL -> must survive (binary-safe). */
    const unsigned char audio[6] = { 'a', 0x00, 'b', 0xff, 'c', '\n' };
    int nul_found = 0;
    jc_size i;

    jc_multipart_init(&mp);
    jc_multipart_field(&mp, "model", "whisper-1");
    jc_multipart_field(&mp, "response_format", "json");
    jc_multipart_file(&mp, "file", "clip.wav", "audio/wav", audio, sizeof(audio));
    jc_multipart_content_type(&mp, ctype, sizeof(ctype));
    body = jc_multipart_finish(&mp, &len);
    jc_multipart_free(&mp);

    JC_CHECK(body != NULL && len > 0);

    /* Content-Type names the boundary used in the body. */
    JC_CHECK(strncmp(ctype, "multipart/form-data; boundary=", 30) == 0);
    JC_CHECK(mem_has(body, len, ctype + 30)); /* boundary appears in the body */

    /* Field + file part headers. */
    JC_CHECK(mem_has(body, len, "Content-Disposition: form-data; name=\"model\""));
    JC_CHECK(mem_has(body, len, "whisper-1"));
    JC_CHECK(mem_has(body, len,
                     "name=\"file\"; filename=\"clip.wav\""));
    JC_CHECK(mem_has(body, len, "Content-Type: audio/wav"));

    /* The raw (binary) bytes survived: an embedded NUL is present and the body
     * length exceeds strlen (proving length-based, not NUL-truncated). */
    for (i = 0; i < len; i++) {
        if (body[i] == '\0') {
            nul_found = 1;
            break;
        }
    }
    JC_CHECK(nul_found);
    JC_CHECK(len > (jc_size)strlen(body));

    /* Closing boundary "--<boundary>--" appears after the NUL. */
    {
        char close[160];
        const char *b = ctype + 30;
        close[0] = '-'; close[1] = '-';
        memcpy(close + 2, b, strlen(b) + 1);
        strcat(close, "--");
        JC_CHECK(mem_has(body, len, close));
    }

    free(body);
}

/* M472: a header parameter value cannot break out of its quoting.
 *
 * jc_multipart_file interpolated `filename` straight into
 * `Content-Disposition: ...; filename="<here>"` with no escaping of '"', CR or LF,
 * and the filename it receives is MODEL-CHOSEN (jc_transcribe passes the path the
 * model asked to upload). A filename containing CRLF therefore injects arbitrary
 * headers -- or a whole extra part -- into the request body. */
void test_multipart_header_injection(void)
{
    struct jc_multipart mp;
    char *body;
    jc_size len;

    jc_multipart_init(&mp);
    /* The attack: close the quote, end the header block, open a new part. */
    jc_multipart_file(&mp, "file",
                      "a\"\r\nX-Injected: yes\r\n\r\n--evil\r\nContent-Disposition: form-data; name=\"stolen\"\r\n\r\nvalue\r\n",
                      "audio/wav", (const unsigned char *)"DATA", 4);
    body = jc_multipart_finish(&mp, &len);
    if (JC_REQUIRE(body != NULL)) {
        /* The escape works by removing the CRLF, which leaves the attacker's text
         * as INERT CONTENT inside the quoted parameter -- so a bare strstr for
         * "X-Injected" still finds it, and asserting on that would be asserting
         * the wrong thing (it failed exactly that way first). What must be absent
         * is the STRUCTURE: a header at the start of a line, and a boundary
         * delimiter. */
        JC_CHECK(strstr(body, "\r\nX-Injected") == NULL);
        JC_CHECK(strstr(body, "\r\n--evil") == NULL);
        JC_CHECK(strstr(body, "\r\n\r\n--evil") == NULL);
        /* The quote that would have closed the parameter is gone. */
        JC_CHECK(strstr(body, "filename=\"a\"") == NULL);
        /* ...and the payload still arrived, which a too-eager strip would break. */
        JC_CHECK(strstr(body, "DATA") != NULL);
        /* Exactly one part: the boundary appears twice (opening + closing). */
        {
            const char *p = body;
            int n = 0;
            jc_size blen = (jc_size)strlen(mp.boundary);
            while ((p = strstr(p, mp.boundary)) != NULL) { n++; p += blen; }
            JC_CHECK(n == 2);
        }
        free(body);
    }
    jc_multipart_free(&mp);

    /* A legitimate filename with spaces and UTF-8 is untouched. */
    jc_multipart_init(&mp);
    jc_multipart_file(&mp, "file", "my recording \xc3\xbc.wav", "audio/wav",
                      (const unsigned char *)"X", 1);
    body = jc_multipart_finish(&mp, &len);
    if (JC_REQUIRE(body != NULL)) {
        JC_CHECK(strstr(body, "filename=\"my recording \xc3\xbc.wav\"") != NULL);
        free(body);
    }
    jc_multipart_free(&mp);

    /* The field path shares the escaper. */
    jc_multipart_init(&mp);
    jc_multipart_field(&mp, "model\"\r\nX-Bad: 1", "m");
    body = jc_multipart_finish(&mp, &len);
    if (JC_REQUIRE(body != NULL)) {
        JC_CHECK(strstr(body, "\r\nX-Bad") == NULL);
        free(body);
    }
    jc_multipart_free(&mp);
}

/* C-10 (M1121, half A): a FILE PAYLOAD that contains the delimiter "--<boundary>"
 * must not reach the rendered body. The test may read mp.boundary to plant it --
 * that is the same information a payload's author has (the finding's named
 * mechanism: a model-chosen workspace file that happens to match a guessable
 * boundary, under the weak rand() fallback). The builder's contract is to
 * regenerate the boundary on a match, so the delimiter appears exactly twice in
 * the finished body: the opening part delimiter and the closing one. PRE-FIX the
 * planted bytes add a third occurrence -> RED (witnessed: 'FAIL
 * tests/test_multipart.c:223: n == 2'). */
void test_multipart_payload_boundary(void)
{
    struct jc_multipart mp;
    char del[64];
    char *body;
    jc_size len;
    int n;
    jc_status st;

    /* Positive control: a payload WITHOUT the boundary renders with the
     * delimiter exactly twice and succeeds. So the check below is not a
     * blanket refusal. */
    jc_multipart_init(&mp);
    jc_snprintf(del, sizeof(del), "--%s", mp.boundary);
    JC_REQUIRE(strlen(del) > 2);
    st = jc_multipart_file(&mp, "file", "clean.wav", "audio/wav",
                           (const unsigned char *)"PLAIN AUDIO BYTES", 17);
    JC_CHECK(st == JC_OK);
    body = jc_multipart_finish(&mp, &len);
    if (JC_REQUIRE(body != NULL)) {
        JC_CHECK(mem_count(body, len, del) == 2);
        free(body);
    }
    jc_multipart_free(&mp);

    jc_multipart_init(&mp);
    jc_snprintf(del, sizeof(del), "--%s", mp.boundary);
    JC_REQUIRE(strlen(del) > 2);

    /* The attack payload: filler around a planted "--<boundary>". The plant
     * is read from mp before the call (the same information a payload's
     * author has); it matches the boundary as it stands at the call, so on an
     * UNFIXED tree the count below goes to 3. The counted delimiter, though,
     * is "--" + the FINAL boundary (re-read after the call, which the fix
     * may have regenerated): on a fixed tree the planted bytes survive in the
     * payload as inert content under a new boundary, and "--<final>" appears
     * exactly twice -- opening part delimiter + closing. */
    {
        char payload[128];
        jc_size pl = (jc_size)jc_snprintf(payload, sizeof(payload),
                        "LEAD%sTRAIL", del);
        st = jc_multipart_file(&mp, "file", "hostile.wav", "audio/wav",
                               (const unsigned char *)payload, pl);
        JC_CHECK(st == JC_OK);
        jc_snprintf(del, sizeof(del), "--%s", mp.boundary);
        body = jc_multipart_finish(&mp, &len);
        if (JC_REQUIRE(body != NULL)) {
            n = mem_count(body, len, del);
            JC_CHECK(n == 2);
            /* The planted bytes still shipped, unchanged -- a regeneration,
             * not a modification of the payload. */
            JC_CHECK(mem_has(body, len, "LEAD") &&
                     mem_has(body, len, "TRAIL"));
            free(body);
        }
        jc_multipart_free(&mp);
    }
}
