/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_toolout.h - M339: keep the remainder of an over-cap tool output.
 *
 * The five output caps (readMaxBytes/runMaxBytes/searchMaxBytes/gitMaxBytes/
 * fetchMaxBytes) all discard what they trim. For an expensive or
 * non-reproducible command -- a build log, a test run -- that tail is not
 * recoverable at any price, and the model's only recourse is to run the whole
 * thing again and pay for all of it. This writes the full capture to a file
 * OUTSIDE the workspace and puts its path in the tool result, so the remainder
 * is addressable in slices by the ordinary read/search tools.
 *
 * Design decisions (docs/proposals/2026-08-managed-tool-output.md):
 *  - outside the workspace, because ANECDOTES #1 is a log a rollback deleted;
 *  - eviction designed WITH the feature, not after it (M338's lesson): the
 *    session's directory goes at teardown and an age sweep collects the ones
 *    crashed sessions left behind;
 *  - a write failure degrades to today's truncation and never turns a working
 *    tool into a failed one.
 */
#ifndef JC_TOOLOUT_H
#define JC_TOOLOUT_H
#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_str.h"

struct jc_app;

/* How much a tool may capture before the spill file itself is truncated. Bounds
 * the peak of one tool call; the per-call tool_scratch arena is reset right
 * after, so this is not cumulative. */
#define JC_TOOLOUT_SPILL_MAX  (1024L * 1024L)

/* Days after which a leftover session directory is collected. */
#define JC_TOOLOUT_MAX_AGE_DAYS 3

/* Pure: how many bytes of `len` to show as head and as tail, given a budget of
 * `show`. Head gets the larger share (the command and its early context) and
 * the tail is never zero when there is room for one, because a build log's
 * error is at the end. Both are 0 when nothing needs eliding. */
void jc_toolout_split(jc_size len, jc_size show, jc_size *head, jc_size *tail);

/* M348: write `text` (len bytes) into the preservation store (the M339 spill
 * directory: 0600, atomic, outside every workspace, fence-readable) under
 * `tag`, and return 1 with the file's path in `path_out`. Returns 0 on any
 * failure -- and then the caller must NOT name a path (the D3 rule: the model
 * must never be given a path that is not there; the WARN to the operator is
 * emitted here). Shared by the over-cap spill below and the mid-turn elision
 * claim ticket, so there is one store and one discipline. */
int jc_toolout_preserve(struct jc_app *app, const char *tag,
                        const char *text, jc_size len,
                        char *path_out, jc_size path_cap);

/* Spill `text` (len bytes) for tool `tag` and append a bounded rendering to
 * `out`: head, a marker naming the full byte count and the file, then tail.
 *
 * Returns 1 when the remainder was preserved and the marker names a real file,
 * 0 when it was not -- in which case `out` gets exactly the old behaviour
 * (`show` bytes plus a plain truncation note) and the caller needs no branch.
 * len <= show appends the text unchanged and returns 0. */
int jc_toolout_spill(struct jc_app *app, const char *tag,
                     const char *text, jc_size len, jc_size show,
                     struct jc_sb *out);

/* The directory tool output is spilled to, written into `buf`; returns buf, or
 * NULL when unavailable. Reads from it are permitted by the path fence; nothing
 * but jichi writes there.
 *
 * Takes a caller buffer rather than returning a static one. The first version
 * cached the path in a `static char[]`, which made it UNCHANGEABLE after the
 * first call -- so a test that set HOME and expected a temp directory silently
 * got whatever HOME was when some earlier code first asked, wrote 5 KB into the
 * real ~/.jichi.d, and passed its own file-exists assertion because both sides
 * of the comparison were equally wrong. Hidden state in a path resolver is a
 * test-integrity defect, not just a style preference. */
const char *jc_toolout_dir(struct jc_app *app, char *buf, jc_size cap);

/* M341: write one model request body to disk for diagnosis, and return 1 if it
 * was written. `dir` is the operator-chosen dump directory (NULL/empty = off, so
 * every call site is a no-op by default). `url` is recorded beside the body,
 * because "which endpoint received this" is half of the question a dump answers.
 *
 * The body is the EXACT string handed to libcurl -- not a re-render of it. That
 * distinction is the whole point: `--log-level full` records prompt *content*,
 * which looks like the request and is not, and mistaking one for the other led
 * to a wrong conclusion about whether jichi emitted cache breakpoints at all
 * (docs/analysis/2026-08-09-hrz-prompt-caching.md section 7).
 *
 * Routed through jc_redact_secrets: a request body can contain anything jichi
 * read, including a key someone pasted into a file. Written 0600. */
int jc_toolout_dump_request(const char *dir, const char *url,
                            const char *body, jc_size len);

/* Remove this session's spill directory, and sweep directories older than
 * JC_TOOLOUT_MAX_AGE_DAYS left by sessions that did not get here. */
void jc_toolout_cleanup(struct jc_app *app);

#ifdef __cplusplus
}
#endif
#endif
