/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* mm_core.h - the pure core of mockmodel (tests/tools).
 *
 * Two responsibilities, both testable offline in run_tests:
 *
 *   1. The reply table: a line-oriented script (*.mm) parsed into rules,
 *      selected per request by index and/or body-substring predicates,
 *      and rendered into complete HTTP responses (SSE for model replies).
 *
 *   2. The incremental HTTP request parser (mm_http_*): fed raw bytes as
 *      they arrive, it declares a request COMPLETE only when the head has
 *      ended AND the declared Content-Length has fully arrived. This is
 *      the C twin of tests/e2e/_e2e.py's recv_http_request, the shared
 *      reader that fixed the truncated-request flake (docs/ANECDOTES.md
 *      #18, M201). The recv loop in mockmodel.c can only feed bytes; ONLY
 *      this parser can declare completion, so the naive
 *      break-on-first-timeout truncation bug is structurally
 *      unrepresentable here.
 *
 * Reply table format (first matching rule wins; '#' comments; blank lines
 * ignored):
 *
 *   wire openai            # file-level; "anthropic" parses but is rejected
 *   rule                   # starts a rule
 *     count 2              # predicate: only the 2nd request (1-based)
 *     match "substr"       # predicate: C-unescaped substring of the raw
 *                          #   body; several match lines AND together
 *     nomatch "substr"     # predicate: the body must NOT contain this
 *                          #   (the complement of match; ANDs with the
 *                          #   rest -- routes a child's first call from
 *                          #   its tool-result calls, M216)
 *     larger 5000          # predicate: body length > N bytes (a
 *                          #   small-context summarizer mock 400s big
 *                          #   requests -- M212)
 *     text REST OF LINE    # action: SSE content delta + finish stop +
 *                          #   usage + [DONE]
 *     tool NAME {json}     # action: SSE tool_calls delta + [DONE]
 *     status 500           # action: plain (non-SSE) HTTP reply ...
 *     location URL         # ... with this Location: header, which is what
 *                          #   makes a 3xx real (only with status; M472).
 *                          #   Without it a `status 302` has nowhere to
 *                          #   point and no client would follow it, so the
 *                          #   redirect-following check could not be
 *                          #   written in this tier at all.
 *     body {"err":1}       # ... with this body (only with status)
 *     body-file PATH       # ... or with this file's RAW BYTES as the
 *                          #   body (binary-safe: a mock TTS backend
 *                          #   returns audio with NULs -- M212)
 *     stall header         # action: 200 + SSE headers, then hold forever
 *     stall mid            # action: headers + one delta, then hold
 *     sse-file PATH        # action: verbatim SSE body from a file
 *     embed hooks state    # action: an OpenAI /v1/embeddings reply built
 *                          #   FROM the request: for each entry of the
 *                          #   request's "input", the vector is
 *                          #   [count(word1), count(word2), ..., 0.1]
 *                          #   (case-insensitive substring counts; the
 *                          #   0.1 keeps cosine well-defined -- M213)
 *     delay 800            # modifier: sleep this many ms before replying
 *                          #   (timing-sensitive flows, e.g. giving a
 *                          #   background child time to run -- M211)
 *     usage 20 5           # modifier: prompt/completion tokens on the
 *                          #   text action's finish chunk (default 20 5)
 *
 * A rule with no predicates is a catch-all. Exactly one action per rule.
 */
#ifndef MM_CORE_H
#define MM_CORE_H

#include <stddef.h>

enum mm_action {
    MM_ACT_NONE = 0,
    MM_ACT_TEXT,
    MM_ACT_TOOL,
    MM_ACT_STATUS,
    MM_ACT_STALL_HEADER,
    MM_ACT_STALL_MID,
    MM_ACT_SSE_FILE,
    MM_ACT_EMBED
};

#define MM_MAX_MATCH 4
/* A real provider may put SEVERAL tool calls in ONE assistant message, and the
 * agent loop then runs them in a single round (`for (k = 0; k < ncalls; k++)`).
 * With one action per rule the mock could not express that at all, so no smoke
 * driver reached that path -- including the envelope's per-call metering of
 * --max-tool-calls / --max-reads, which is exactly where a bounding control is
 * most likely to miscount. Hence: a rule may now carry up to MM_MAX_TOOLS
 * `tool` lines, emitted as one tool_calls array. */
#define MM_MAX_TOOLS 4

struct mm_rule {
    int count;                  /* 0 = any request, else 1-based index    */
    long larger;                /* 0 = any size, else body must be > N    */
    char *match[MM_MAX_MATCH];  /* unescaped substrings; all must match   */
    size_t match_len[MM_MAX_MATCH]; /* lengths (patterns may embed NUL?
                                       no -- kept for binary-safe search) */
    int match_neg[MM_MAX_MATCH];    /* 1 = must be ABSENT (nomatch)        */
    int nmatch;
    enum mm_action action;
    char *arg1;                 /* text: the text; tool: name;
                                   status: body; sse-file: path           */
    char *arg2;                 /* tool: the arguments JSON (== tool_args[0]
                                   for one tool; kept so single-tool rules and
                                   their unit tests are unchanged)         */
    char *tool_name[MM_MAX_TOOLS];  /* MM_ACT_TOOL: one entry per tool line */
    char *tool_args[MM_MAX_TOOLS];
    int ntools;                 /* 0 for non-tool actions                 */
    char *body_file;            /* status: raw body from this file        */
    char *location;             /* status: a Location: header (M472) --
                                   makes a 3xx expressible, which is what
                                   the "does jichi follow a redirect?"
                                   check needs. NULL = no header.        */
    int status;                 /* MM_ACT_STATUS: the HTTP code           */
    long usage_in;              /* -1 = default (20)                      */
    long usage_out;             /* -1 = default (5)                       */
    long delay_ms;              /* sleep before replying (0 = none)       */
};

struct mm_script {
    struct mm_rule *rules;
    int nrules;
};

/* Parse a reply table. Returns 0 on success, -1 on error with a
 * "line N: message" diagnostic in err (always NUL-terminated). */
int mm_script_parse(const char *text, struct mm_script *out,
                    char *err, size_t errcap);
void mm_script_free(struct mm_script *s);

/* First rule whose predicates all hold for this request body (raw bytes,
 * binary-safe) and 1-based request index; NULL when none match. */
const struct mm_rule *mm_select(const struct mm_script *s,
                                const char *body, size_t body_len,
                                int req_index);

/* Render a rule into response bytes (malloc'd; caller frees).
 *   MM_ACT_TEXT / MM_ACT_TOOL / MM_ACT_STATUS: the COMPLETE response
 *     (status line + headers incl. Content-Length + Connection: close +
 *     body) -- the exact SSE shapes the Python mock drivers send.
 *   MM_ACT_STALL_HEADER: headers only (no Content-Length) -- the caller
 *     sends these and then holds the socket open.
 *   MM_ACT_STALL_MID: headers + one content delta -- then hold.
 *   MM_ACT_SSE_FILE: not rendered here (the main wraps the file bytes);
 *     returns -1.
 * Returns 0 on success. */
int mm_render_response(const struct mm_rule *r, char **out, size_t *outlen);

/* Wrap an externally supplied SSE body (sse-file) in the standard
 * response head. Returns 0 on success; malloc'd *out. */
int mm_render_sse_body(const char *body, size_t body_len,
                       char **out, size_t *outlen);

/* A plain status reply around an arbitrary (binary-safe) body -- the
 * body-file path. Returns 0 on success; malloc'd *out. */
int mm_render_status_body(int status, const char *body, size_t body_len,
                          char **out, size_t *outlen);

/* Case-insensitive, non-overlapping substring count (the embed action's
 * vector components; mirrors Python's str.count on lowered text). */
long mm_count_ci(const char *hay, const char *needle);

/* --- incremental HTTP request parser ------------------------------------ */

enum mm_http_state {
    MM_HTTP_NEED_MORE = 0,
    MM_HTTP_COMPLETE  = 1,
    MM_HTTP_ERROR     = -1
};

struct mm_http {
    char *buf;
    size_t len;
    size_t cap;
    size_t head_end;        /* offset just past the blank line; 0 = unseen */
    long content_length;    /* -1 = not parsed yet / absent                */
    int state;              /* enum mm_http_state                          */
};

void mm_http_init(struct mm_http *h);
void mm_http_free(struct mm_http *h);

/* Append n bytes and re-evaluate. Returns the new state. COMPLETE only
 * when the head has ended and the full declared body has arrived (a
 * missing Content-Length completes at end-of-head, e.g. GET). ERROR on
 * Transfer-Encoding: chunked (never sent by jichi) or a hard cap. */
int mm_http_feed(struct mm_http *h, const char *bytes, size_t n);

/* Accessors; valid once COMPLETE (body may be empty). */
const char *mm_http_head(const struct mm_http *h, size_t *len);
const char *mm_http_body(const struct mm_http *h, size_t *len);

#endif /* MM_CORE_H */
