/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_mdrender.h - streaming markdown + light syntax renderer for the TUI.
 *
 * Turns the assistant's markdown reply into ANSI-styled terminal output as it
 * streams. Because deltas arrive token-sized (a span like **bold** or a ``` fence
 * can be split across deltas), the renderer is a line-buffered state machine:
 * bytes accumulate until a newline, then the completed line is classified and
 * styled. Block elements handled: fenced code blocks, ATX headings, blockquotes,
 * list markers, horizontal rules; inline: **bold**, *italic*, `code`. Inside code
 * fences a light, language-agnostic highlight colors strings, line comments, and
 * numbers (heuristic).
 *
 * Pure (no I/O): styled bytes are appended to a caller-provided jc_sb, which the
 * TUI writes to stdout. With color disabled every function passes text through
 * unchanged. Unit-tested in tests/test_mdrender.c.
 */
#ifndef JC_MDRENDER_H
#define JC_MDRENDER_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_str.h"

struct jc_mdr {
    struct jc_sb line;     /* bytes of the current, not-yet-complete line  */
    int          in_fence; /* inside a ``` / ~~~ code block                */
    int          in_block; /* inside a C-style block comment (spans lines)  */
    int          color;    /* emit ANSI styling (else pass text through)   */
    char         lang[16]; /* current fence's language tag (lowercased)    */
};

/* Initialize once (allocates the line buffer). */
void jc_mdr_init(struct jc_mdr *r, int color);
/* Clear state for a new message (keeps the allocation; no realloc). */
void jc_mdr_reset(struct jc_mdr *r);
/* Feed `n` bytes of streamed text; append fully-rendered lines to `out`. */
void jc_mdr_feed(struct jc_mdr *r, const char *delta, jc_size n,
                 struct jc_sb *out);
/* Render any trailing partial line (call at end of the message). */
void jc_mdr_flush(struct jc_mdr *r, struct jc_sb *out);
/* Free the line buffer. */
void jc_mdr_free(struct jc_mdr *r);

#ifdef __cplusplus
}
#endif
#endif /* JC_MDRENDER_H */
