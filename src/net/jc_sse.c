/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_sse.c - Server-Sent Events framing (see jc_sse.h). */

#include "jc_sse.h"
#include <string.h>

void jc_sse_init(struct jc_sse_parser *p, jc_sse_cb cb, void *user)
{
    jc_sb_init(&p->line);
    jc_sb_init(&p->data);
    jc_sb_init(&p->evtype);
    p->have_data = 0;
    p->overflowed = 0;
    p->cb = cb;
    p->user = user;
}

void jc_sse_free(struct jc_sse_parser *p)
{
    jc_sb_free(&p->line);
    jc_sb_free(&p->data);
    jc_sb_free(&p->evtype);
}

/* Dispatch the accumulated event (if any) and reset for the next one. */
static void dispatch(struct jc_sse_parser *p)
{
    struct jc_sse_event ev;

    if (!p->have_data && p->evtype.len == 0) {
        return; /* nothing to emit (e.g. blank line after blank line) */
    }
    ev.event = (p->evtype.data != NULL) ? p->evtype.data : "";
    ev.data = (p->data.data != NULL) ? p->data.data : "";
    p->cb(&ev, p->user);

    jc_sb_clear(&p->data);
    jc_sb_clear(&p->evtype);
    p->have_data = 0;
}

/* Process one complete line (terminator already stripped). */
static void handle_line(struct jc_sse_parser *p, const char *line, jc_size len)
{
    /* Blank line: event boundary. */
    if (len == 0) {
        dispatch(p);
        return;
    }
    /* Comment line. */
    if (line[0] == ':') {
        return;
    }

    if (len >= 5 && strncmp(line, "data:", 5) == 0) {
        jc_size off = 5;
        if (off < len && line[off] == ' ') {
            off++; /* strip a single leading space */
        }
        /* Cap the accumulated payload against an unbounded hostile stream. The
         * inter-line separator is inside the cap too, so a flood of empty
         * `data:` lines can't grow p->data one byte at a time past the cap. */
        if (p->data.len < (jc_size)JC_SSE_FIELD_MAX) {
            if (p->have_data) {
                jc_sb_append_char(&p->data, '\n');
            }
            jc_sb_append_n(&p->data, line + off, len - off);
        } else {
            p->overflowed = 1;
        }
        p->have_data = 1;
    } else if (len >= 6 && strncmp(line, "event:", 6) == 0) {
        jc_size off = 6;
        if (off < len && line[off] == ' ') {
            off++;
        }
        jc_sb_clear(&p->evtype);
        jc_sb_append_n(&p->evtype, line + off, len - off);
    }
    /* Other fields (id:, retry:) are ignored. */
}

void jc_sse_feed(struct jc_sse_parser *p, const char *bytes, jc_size n)
{
    jc_size i;
    for (i = 0; i < n; i++) {
        char c = bytes[i];
        if (c == '\n') {
            jc_size len = p->line.len;
            /* Strip a trailing CR (CRLF terminators). */
            if (len > 0 && p->line.data[len - 1] == '\r') {
                len--;
            }
            handle_line(p, (p->line.data != NULL) ? p->line.data : "", len);
            jc_sb_clear(&p->line);
        } else if (p->line.len < (jc_size)JC_SSE_FIELD_MAX) {
            jc_sb_append_char(&p->line, c);
        } else {
            p->overflowed = 1; /* drop bytes of a pathologically long line */
        }
    }
}
