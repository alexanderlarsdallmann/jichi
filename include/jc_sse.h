/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_sse.h - Server-Sent Events framing.
 *
 * libcurl hands us arbitrary byte chunks; SSE events are delimited by a blank
 * line, with `data:` and `event:` fields. This parser buffers across chunk
 * boundaries (a single event may span several write callbacks, and a single
 * chunk may contain several events) and dispatches one callback per complete
 * event.
 *
 * The `data` passed to the callback is the concatenation of all `data:` lines
 * in the event, joined by '\n' (per the SSE spec). `event` is the event type
 * ("" if none was given). The OpenAI `[DONE]` sentinel arrives as an event
 * whose data is exactly "[DONE]"; the parser does not special-case it.
 */
#ifndef JC_SSE_H
#define JC_SSE_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_str.h"

struct jc_sse_event {
    const char *event; /* event type, "" if absent */
    const char *data;  /* concatenated data payload */
};

typedef void (*jc_sse_cb)(const struct jc_sse_event *ev, void *user);

/* Hard cap on a single accumulated line or data payload (bytes). A well-behaved
 * server never approaches this; the cap bounds memory against a hostile or
 * broken stream that never emits a newline / event boundary (M24). Past the cap
 * the parser stops appending and sets `overflowed`. */
#define JC_SSE_FIELD_MAX (4L * 1024L * 1024L)

struct jc_sse_parser {
    struct jc_sb line;   /* current partial line (no terminator yet)     */
    struct jc_sb data;   /* accumulated data: lines for the current event */
    struct jc_sb evtype; /* current event: value                          */
    int          have_data; /* a data: field was seen for this event      */
    int          overflowed; /* a field hit JC_SSE_FIELD_MAX and was capped */
    jc_sse_cb    cb;
    void        *user;
};

void jc_sse_init(struct jc_sse_parser *p, jc_sse_cb cb, void *user);
void jc_sse_free(struct jc_sse_parser *p);

/* Feed `n` raw bytes; dispatches the callback for each completed event. */
void jc_sse_feed(struct jc_sse_parser *p, const char *bytes, jc_size n);

#ifdef __cplusplus
}
#endif
#endif /* JC_SSE_H */
