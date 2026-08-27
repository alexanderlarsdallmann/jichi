/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_sse.c - exercises the SSE framer, including split chunk boundaries. */

#include "jc_test.h"
#include "jc_sse.h"
#include "jc_str.h"
#include <string.h>
#include <stdlib.h>

struct collector {
    int   count;
    char  last_event[64];
    char  last_data[256];
};

static void on_event(const struct jc_sse_event *ev, void *user)
{
    struct collector *c = (struct collector *)user;
    c->count++;
    strncpy(c->last_event, ev->event, sizeof(c->last_event) - 1);
    c->last_event[sizeof(c->last_event) - 1] = '\0';
    strncpy(c->last_data, ev->data, sizeof(c->last_data) - 1);
    c->last_data[sizeof(c->last_data) - 1] = '\0';
}

void test_sse(void)
{
    struct collector c;
    struct jc_sse_parser p;

    /* Two complete events in one feed. */
    memset(&c, 0, sizeof(c));
    jc_sse_init(&p, on_event, &c);
    jc_sse_feed(&p, "data: hello\n\ndata: world\n\n", 26);
    JC_CHECK(c.count == 2);
    JC_CHECK_STR(c.last_data, "world");
    jc_sse_free(&p);

    /* Event split across multiple feeds (chunk boundary mid-line). */
    memset(&c, 0, sizeof(c));
    jc_sse_init(&p, on_event, &c);
    jc_sse_feed(&p, "ev", 2);
    jc_sse_feed(&p, "ent: msg\nda", 11);
    jc_sse_feed(&p, "ta: pa", 6);
    jc_sse_feed(&p, "rt\n\n", 4);
    JC_CHECK(c.count == 1);
    JC_CHECK_STR(c.last_event, "msg");
    JC_CHECK_STR(c.last_data, "part");
    jc_sse_free(&p);

    /* CRLF line endings and a comment line are tolerated. */
    memset(&c, 0, sizeof(c));
    jc_sse_init(&p, on_event, &c);
    jc_sse_feed(&p, ": keep-alive\r\ndata: x\r\n\r\n", 25);
    JC_CHECK(c.count == 1);
    JC_CHECK_STR(c.last_data, "x");
    jc_sse_free(&p);

    /* Multiple data: lines concatenate with '\n'. */
    memset(&c, 0, sizeof(c));
    jc_sse_init(&p, on_event, &c);
    jc_sse_feed(&p, "data: a\ndata: b\n\n", 17);
    JC_CHECK(c.count == 1);
    JC_CHECK_STR(c.last_data, "a\nb");
    jc_sse_free(&p);

    /* The [DONE] sentinel is delivered as ordinary data. */
    memset(&c, 0, sizeof(c));
    jc_sse_init(&p, on_event, &c);
    jc_sse_feed(&p, "data: [DONE]\n\n", 14);
    JC_CHECK(c.count == 1);
    JC_CHECK_STR(c.last_data, "[DONE]");
    jc_sse_free(&p);

    /* A hostile stream that never emits a newline must not grow without bound:
     * the line buffer is capped at JC_SSE_FIELD_MAX and `overflowed` is set. */
    {
        char *blob = (char *)malloc((size_t)JC_SSE_FIELD_MAX + 1024);
        if (blob != NULL) {
            jc_size i;
            for (i = 0; i < (jc_size)JC_SSE_FIELD_MAX + 1024; i++) {
                blob[i] = 'a';
            }
            memset(&c, 0, sizeof(c));
            jc_sse_init(&p, on_event, &c);
            jc_sse_feed(&p, blob, (jc_size)JC_SSE_FIELD_MAX + 1024);
            JC_CHECK(p.overflowed == 1);
            JC_CHECK(p.line.len <= (jc_size)JC_SSE_FIELD_MAX);
            jc_sse_free(&p);
            free(blob);
        }
    }

    /* An over-long data: payload is capped too (no event boundary needed to
     * trigger the cap). */
    {
        jc_size big = (jc_size)JC_SSE_FIELD_MAX + 2048;
        char *blob = (char *)malloc(big + 8);
        if (blob != NULL) {
            jc_size i;
            blob[0] = 'd'; blob[1] = 'a'; blob[2] = 't'; blob[3] = 'a';
            blob[4] = ':'; blob[5] = ' ';
            for (i = 6; i < big; i++) {
                blob[i] = 'x';
            }
            blob[big] = '\n';
            blob[big + 1] = '\n';
            memset(&c, 0, sizeof(c));
            jc_sse_init(&p, on_event, &c);
            jc_sse_feed(&p, blob, big + 2);
            JC_CHECK(p.overflowed == 1);
            jc_sse_free(&p);
            free(blob);
        }
    }

    /* Once a data field reaches the cap, empty `data:` lines must not keep
     * growing it one newline at a time. Fill to the cap with one big line, then
     * feed many empty data: lines (same event) and assert it stays bounded. */
    {
        jc_size big = (jc_size)JC_SSE_FIELD_MAX + 16;
        char *blob = (char *)malloc(big + 2);
        if (blob != NULL) {
            jc_size i;
            blob[0]='d'; blob[1]='a'; blob[2]='t'; blob[3]='a'; blob[4]=':';
            for (i = 5; i < big; i++) {
                blob[i] = 'x';
            }
            blob[big] = '\n';
            memset(&c, 0, sizeof(c));
            jc_sse_init(&p, on_event, &c);
            jc_sse_feed(&p, blob, big + 1);   /* drives p->data to ~the cap */
            /* 5000 empty data: lines would each add a newline byte under the
             * bug (unconditional separator), pushing well past the cap; the fix
             * keeps p->data bounded at JC_SSE_FIELD_MAX. */
            for (i = 0; i < 5000; i++) {
                jc_sse_feed(&p, "data:\n", 6);
            }
            JC_CHECK(p.data.len <= (jc_size)JC_SSE_FIELD_MAX);
            jc_sse_free(&p);
            free(blob);
        }
    }
}
