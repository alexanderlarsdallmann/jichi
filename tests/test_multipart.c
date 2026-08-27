/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_multipart.c - multipart/form-data body builder (M33). */

#include "jc_test.h"
#include "jc_multipart.h"

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
