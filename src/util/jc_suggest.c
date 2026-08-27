/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_suggest.c - pure prompts + reply cleaners for the TUI's Ctrl-G and
 * Ctrl-Q composing gestures (see jc_suggest.h). */

#include "jc_suggest.h"

#include <string.h>

/* The examples are the load-bearing part. Three continuations, chosen to
 * teach three distinct things:
 *
 *   1. mid-word continuation, no leading space ("this pr" -> "oject?") --
 *      and it is the exact line that failed in the field, so the one case a
 *      user reported is the one the model has seen.
 *   2. word-boundary continuation, WITH a leading space, because the ghost is
 *      appended verbatim and a missing space corrupts the line.
 *   3. a line that reads like a question but must still be continued, not
 *      answered.
 *
 * Then the anti-example, spelled out: models comply better when told what the
 * demonstrations deliberately are not. Split across string literals to stay
 * under C89's 509-char limit. */
/* Split into chunks: C89 guarantees only 509 characters per string literal,
 * and the project builds with -Woverlength-strings as an error (the same
 * constraint jc_scaffold's compiled-in packs work around the same way). The
 * chunks are joined into a caller-provided buffer, so these builders stay
 * pure -- no lazily-initialised statics hiding in a "pure" core. */
static const char *const SUGGEST_CHUNKS[] = {
    "You complete a half-typed line of input. The line is the beginning of a "
    "request that someone is typing to a coding agent.\n"
    "Output ONLY the text that continues their line. Never answer it, never "
    "ask them a question back, never add a preamble or quotes, and never "
    "repeat what they already typed.\n",
    "\nExamples.\n"
    "input: what is the name of this pr\n"
    "output: oject?\n"
    "input: add a test for the arena\n"
    "output:  allocator that asserts the reset boundary\n"
    "input: why does the build fail\n"
    "output:  on aarch64?\n",
    "\nNote what those outputs do NOT do: they do not answer the question and "
    "they do not ask for clarification. \"what is the name of this pr\" "
    "continues as \"oject?\" -- not as \"Could you clarify which PR you "
    "mean?\".\n",
    "Begin your output with a space when the line continues at a word "
    "boundary, and without one when it continues mid-word.",
    NULL
};

static const char *const ADVICE_CHUNKS[] = {
    "Someone is composing a request to a coding agent and wants to know "
    "whether it is clear enough to send. You are not answering the request.\n",
    "Read it and reply with ONE short line -- at most about 100 characters -- "
    "naming the single most useful thing to add or decide. Phrase it as a "
    "brief question or a terse hint. No preamble, no list, no markdown, no "
    "quotes, no restating their request.\n",
    "If the request is already clear and specific enough to act on, reply "
    "with exactly: " JC_ADVICE_CLEAR,
    NULL
};

static jc_size join_chunks(const char *const *chunks, char *out, jc_size cap)
{
    jc_size n = 0;
    jc_size i;

    if (out == NULL || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    for (i = 0; chunks[i] != NULL; i++) {
        jc_size k = (jc_size)strlen(chunks[i]);
        if (n + k + 1 > cap) {
            break;                  /* truncate rather than overrun */
        }
        memcpy(out + n, chunks[i], k);
        n += k;
    }
    out[n] = '\0';
    return n;
}

jc_size jc_suggest_system(char *out, jc_size cap)
{
    return join_chunks(SUGGEST_CHUNKS, out, cap);
}

jc_size jc_advice_system(char *out, jc_size cap)
{
    return join_chunks(ADVICE_CHUNKS, out, cap);
}

/* Skip blank lines at the start; returns a pointer into s. */
static const char *skip_leading_blank_lines(const char *s)
{
    while (*s == '\n' || *s == '\r') {
        s++;
    }
    return s;
}

/* If s starts with `lit` case-insensitively, return the length of the match
 * (plus any spaces after it), else 0. */
static jc_size match_label(const char *s, const char *lit)
{
    jc_size i = 0;
    while (lit[i] != '\0') {
        char a = s[i];
        char b = lit[i];
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return 0;
        }
        i++;
    }
    while (s[i] == ' ' || s[i] == '\t') {
        i++;
    }
    return i;
}

/* Copy one line of `s` into out, capped, trimming trailing whitespace.
 * `keep_leading` preserves leading spaces (ghost text needs them). */
static jc_size copy_one_line(const char *s, char *out, jc_size cap,
                             int keep_leading)
{
    jc_size n = 0;

    if (out == NULL || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (s == NULL) {
        return 0;
    }
    if (!keep_leading) {
        while (*s == ' ' || *s == '\t') {
            s++;
        }
    }
    while (s[n] != '\0' && s[n] != '\n' && s[n] != '\r' && n + 1 < cap) {
        out[n] = s[n];
        n++;
    }
    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t')) {
        n--;                    /* trailing whitespace is never useful */
    }
    out[n] = '\0';
    return n;
}

/* Strip one layer of matching surrounding quotes, in place. */
static jc_size unquote(char *s, jc_size n)
{
    if (n >= 2 && ((s[0] == '"' && s[n - 1] == '"') ||
                   (s[0] == '\'' && s[n - 1] == '\''))) {
        memmove(s, s + 1, n - 2);
        n -= 2;
        s[n] = '\0';
    }
    return n;
}

jc_size jc_suggest_clean(const char *typed, const char *reply,
                         char *out, jc_size cap)
{
    const char *s;
    jc_size n;

    if (out == NULL || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (reply == NULL) {
        return 0;
    }
    s = skip_leading_blank_lines(reply);
    /* A few-shot prompt invites the model to answer in the example's shape,
     * so an "output:" label is a predictable artefact of the fix itself. */
    s += match_label(s, "output:");
    /* An echo of the user's own line: the prompt forbids it and models do it
     * anyway. Only a verbatim full-prefix echo is dropped -- guessing at
     * partial-word overlap would corrupt legitimate continuations. */
    if (typed != NULL && typed[0] != '\0') {
        jc_size tlen = (jc_size)strlen(typed);
        if (strncmp(s, typed, tlen) == 0) {
            s += tlen;
        }
    }
    n = copy_one_line(s, out, cap, 1);
    n = unquote(out, n);
    return n;
}

jc_size jc_advice_clean(const char *reply, char *out, jc_size cap)
{
    const char *s;
    jc_size n;
    jc_size k;

    if (out == NULL || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (reply == NULL) {
        return 0;
    }
    s = skip_leading_blank_lines(reply);
    k = match_label(s, "advice:");
    if (k == 0) {
        k = match_label(s, "hint:");
    }
    s += k;
    n = copy_one_line(s, out, cap, 0);
    n = unquote(out, n);
    return n;
}
