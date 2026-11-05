/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_toolloop.c - the in-turn tool-call loop detector (see jc_toolloop.h). */

#include "jc_toolloop.h"
#include "jc_reread.h"
#include "jc_snprintf.h"

#include <string.h>

void jc_toolloop_init(struct jc_toolloop *w)
{
    if (w != NULL) {
        memset(w, 0, sizeof(*w));
    }
}

const char *jc_fail_class_name(enum jc_fail_class c)
{
    switch (c) {
    case JC_FAIL_NOT_FOUND:    return "not_found";
    case JC_FAIL_DENIED:       return "denied";
    case JC_FAIL_BAD_ARGS:     return "bad_args";
    case JC_FAIL_KILLED:       return "killed";
    case JC_FAIL_NONZERO_EXIT: return "nonzero_exit";
    case JC_FAIL_AT_BOUND:     return "at_bound";
    case JC_FAIL_OTHER:        break;
    }
    return "other";
}

/* Case-insensitive substring, so a classifier is not defeated by capitalisation.
 * strcasestr is not C89, hence the hand-rolled walk. */
static int has(const char *hay, const char *needle)
{
    jc_size i;
    jc_size j;

    if (hay == NULL || needle == NULL || needle[0] == '\0') {
        return 0;
    }
    for (i = 0; hay[i] != '\0'; i++) {
        for (j = 0; needle[j] != '\0'; j++) {
            char a = hay[i + j];
            char b = needle[j];
            if (a == '\0') {
                return 0;
            }
            if (a >= 'A' && a <= 'Z') { a = (char)(a - 'A' + 'a'); }
            if (b >= 'A' && b <= 'Z') { b = (char)(b - 'A' + 'a'); }
            if (a != b) {
                break;
            }
        }
        if (needle[j] == '\0') {
            return 1;
        }
    }
    return 0;
}

/* Check if `hay` starts with `needle` (case-insensitive), skipping leading whitespace.
 * Returns 1 if `hay` begins with `needle`, 0 otherwise. */
static int starts_with(const char *hay, const char *needle)
{
    const char *p;
    int i, j;

    if (hay == NULL || needle == NULL || needle[0] == '\0') {
        return 0;
    }
    /* Skip leading whitespace in haystack */
    p = hay;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    /* Case-insensitive comparison */
    for (i = 0; p[i] != '\0'; i++) {
        for (j = 0; needle[j] != '\0'; j++) {
            char a = p[i + j];
            char b = needle[j];
            if (a == '\0') {
                return 0;   /* haystack ended before needle */
            }
            if (a >= 'A' && a <= 'Z') { a = (char)(a - 'A' + 'a'); }
            if (b >= 'A' && b <= 'Z') { b = (char)(b - 'A' + 'a'); }
            if (a != b) {
                break;
            }
        }
        if (needle[j] == '\0') {
            return 1;   /* needle matched */
        }
        /* Only match at the start, not later in the string */
        break;
    }
    return 0;
}

enum jc_fail_class jc_fail_classify(const char *result, int exit_status,
                                     const char *tool, int policy_refusal)
{
    int is_shell_tool;

    /* If the executor has set policy_refusal, the refusal is by fence/policy,
     * not by capability or accident. Return DENIED immediately, bypassing the
     * wording-based classification. This fixes M1041-b: the read-only refusal
     * was classified DENIED before M1041 and `other` after. */
    if (policy_refusal) {
        return JC_FAIL_DENIED;
    }

    if (result == NULL || result[0] == '\0') {
        return (exit_status > 0) ? JC_FAIL_NONZERO_EXIT : JC_FAIL_OTHER;
    }

    /* For shell tools (run_terminal_command, run_tests), use stricter rules:
     * DENIED only when the result BEGINS with jichi's refusal phrases.
     * For other tools, use substring matching (their results are jichi's messages). */
    is_shell_tool = (tool != NULL && (strcmp(tool, "run_terminal_command") == 0 ||
                                       strcmp(tool, "run_tests") == 0));

    if (is_shell_tool) {
        /* Strict check: must BEGIN with jichi's refusal phrases.
         * Note: jichi's denial messages often start with "error:" prefix, so
         * we check for "error: refused by safety fence" and "error: ...".
         * The read-only message starts with "error: tool disabled in read-only mode"
         * which contains "read-only" but doesn't start with it, so we check for
         * the full phrase. */
        if (starts_with(result, "error: refused by safety fence") ||
            starts_with(result, "refused by safety fence") ||
            starts_with(result, "outside this run's") ||
            starts_with(result, "outside the workspace") ||
            starts_with(result, "not permitted") ||
            starts_with(result, "refused:") ||
            starts_with(result, "requires approval") ||
            starts_with(result, "read-only") ||
            starts_with(result, "forbidden") ||
            starts_with(result, "error: tool disabled in read-only mode") ||
            (starts_with(result, "Path") && has(result, "outside this run's"))) {
            return JC_FAIL_DENIED;
        }
    } else {
        /* Substring match: for non-shell tools, the entire result is jichi's message */
        if (has(result, "denied") || has(result, "outside this run's") ||
            has(result, "outside the workspace") || has(result, "not permitted") ||
            has(result, "refused") || has(result, "requires approval") ||
            has(result, "read-only") || has(result, "forbidden")) {
            return JC_FAIL_DENIED;
        }
    }
    /* KILLED next, for the same reason: a timeout message often quotes a command
     * that also mentions a path. jichi's own wording is "[stopped: ...]". */
    if (has(result, "timed out") || has(result, "was terminated") ||
        has(result, "capture limit") || has(result, "memory budget") ||
        has(result, "[stopped:") || has(result, "killed")) {
        return JC_FAIL_KILLED;
    }
    /* "could not open/read/extract" is jichi's OWN missing-file wording -- M291's
     * comment in jc_tool_read.c says as much, having split the denial out of it
     * precisely because "could not open" reads like a missing file. Omitting these
     * filed every plain missing-file failure as OTHER, so the note said the generic
     * "change your approach" where it could have said "find its real name". Found by
     * this detector's own smoke driver on the first run. Safe after the DENIED test
     * above, since the denial wording is "refused by safety fence". */
    if (has(result, "not found") || has(result, "no such") ||
        has(result, "does not exist") || has(result, "string not found") ||
        has(result, "no match") || has(result, "could not open") ||
        has(result, "could not read") || has(result, "could not extract")) {
        return JC_FAIL_NOT_FOUND;
    }
    if (has(result, "required parameter") || has(result, "missing '") ||
        has(result, "invalid json") || has(result, "expected") ||
        has(result, "unknown argument") || has(result, "malformed")) {
        return JC_FAIL_BAD_ARGS;
    }
    if (exit_status > 0) {
        return JC_FAIL_NONZERO_EXIT;
    }
    return JC_FAIL_OTHER;
}

/* ---- R6: shell-command key normalisation (see jc_toolloop.h) --------------- */

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

static int id_start(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int id_char(char c)
{
    return id_start(c) || (c >= '0' && c <= '9');
}

/* Advance past one whitespace-delimited token, honouring simple quoting so a
 * quoted space does not end the token. */
static const char *skip_token(const char *p)
{
    while (*p != '\0' && *p != ' ' && *p != '\t') {
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            while (*p != '\0' && *p != q) {
                p++;
            }
            if (*p != '\0') {
                p++;
            }
        } else {
            p++;
        }
    }
    return p;
}

/* Skip a leading `cd DIR &&`, `NAME=val` env assignments and a `timeout [-k V]
 * SECS`, returning the pointer to the core command. When a `timeout` was stripped
 * its seconds are written to *tsecs (0 otherwise). */
static const char *lead_core(const char *cmd, long *tsecs)
{
    const char *p;

    if (tsecs != NULL) {
        *tsecs = 0;
    }
    if (cmd == NULL) {
        return NULL;
    }
    p = skip_ws(cmd);
    for (;;) {
        /* NAME=value ... */
        if (id_start(*p)) {
            const char *q = p;
            while (id_char(*q)) {
                q++;
            }
            if (*q == '=') {
                p = skip_ws(skip_token(q + 1));
                continue;
            }
        }
        /* cd DIR && ... */
        if (strncmp(p, "cd ", 3) == 0 || strncmp(p, "cd\t", 3) == 0) {
            const char *amp = strstr(p, "&&");
            if (amp != NULL) {
                p = skip_ws(amp + 2);
                continue;
            }
        }
        /* timeout [options] SECS ... */
        if (strncmp(p, "timeout ", 8) == 0 || strncmp(p, "timeout\t", 8) == 0) {
            const char *q = skip_ws(p + 7);
            while (*q == '-') {
                int is_k = (strncmp(q, "-k", 2) == 0 &&
                            (q[2] == ' ' || q[2] == '\t' || q[2] == '\0')) ||
                           (strncmp(q, "--kill-after", 12) == 0 &&
                            (q[12] == ' ' || q[12] == '\t' || q[12] == '\0'));
                q = skip_ws(skip_token(q));
                if (is_k) {
                    q = skip_ws(skip_token(q));
                }
            }
            if (*q >= '0' && *q <= '9') {
                long v = 0;
                while (*q >= '0' && *q <= '9') {
                    v = v * 10 + (*q - '0');
                    q++;
                }
                if (*q == 's') { q++; }
                else if (*q == 'm') { v *= 60; q++; }
                else if (*q == 'h') { v *= 3600; q++; }
                else if (*q == 'd') { v *= 86400; q++; }
                if (tsecs != NULL) {
                    *tsecs = v;
                }
                p = skip_ws(q);
                continue;
            }
            /* `timeout` with no numeric duration is not this shape: leave it. */
        }
        break;
    }
    return p;
}

/* Does the trimmed pipeline segment `s` begin with a pure output filter whose
 * removal does not change what the command DOES to the tree? */
static int is_filter_seg(const char *s)
{
    static const char *const F[] = {
        "head", "tail", "grep", "wc", "cat", "sort", "uniq", "less", "more",
        "sed", "awk", "tr", "cut", "tee", "nl", "tac", "rev", "column", NULL
    };
    int i;

    s = skip_ws(s);
    for (i = 0; F[i] != NULL; i++) {
        jc_size n = (jc_size)strlen(F[i]);
        if (strncmp(s, F[i], n) == 0 &&
            (s[n] == '\0' || s[n] == ' ' || s[n] == '\t' || s[n] == '-')) {
            return 1;
        }
    }
    return 0;
}

long jc_toolloop_shell_timeout_secs(const char *cmd)
{
    long secs = 0;
    (void)lead_core(cmd, &secs);
    return secs;
}

jc_size jc_toolloop_shell_norm(const char *cmd, char *out, jc_size cap)
{
    const char *core;
    jc_size len;

    if (out == NULL || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    core = lead_core(cmd, NULL);
    if (core == NULL) {
        return 0;
    }
    jc_snprintf(out, cap, "%s", core);

    /* Strip trailing wrapping in place: whitespace, a redirection, or a pipe into
     * a pure filter -- repeatedly, so `cmd | grep x | head` collapses fully. */
    len = (jc_size)strlen(out);
    for (;;) {
        char *bar;
        while (len > 0 && (out[len - 1] == ' ' || out[len - 1] == '\t')) {
            out[--len] = '\0';
        }
        if (len >= 4 && strcmp(out + len - 4, "2>&1") == 0) {
            len -= 4;
            out[len] = '\0';
            continue;
        }
        /* a trailing redirection to /dev/null: `[N|&]>[>] /dev/null` */
        {
            char *dn = strstr(out, "/dev/null");
            if (dn != NULL && out + strlen(out) - 9 == dn) {
                char *r = dn;
                while (r > out && (r[-1] == ' ' || r[-1] == '\t')) { r--; }
                while (r > out && r[-1] == '>') { r--; }
                if (r > out && (r[-1] == '2' || r[-1] == '1' || r[-1] == '&')) {
                    r--;
                }
                if (r > out && (r[-1] == ' ' || r[-1] == '\t')) {
                    *r = '\0';
                    len = (jc_size)strlen(out);
                    continue;
                }
            }
        }
        bar = strrchr(out, '|');
        if (bar != NULL && is_filter_seg(bar + 1)) {
            *bar = '\0';
            len = (jc_size)strlen(out);
            continue;
        }
        break;
    }
    while (len > 0 && (out[len - 1] == ' ' || out[len - 1] == '\t')) {
        out[--len] = '\0';
    }
    return len;
}

int jc_toolloop_at_bound(const char *cmd, double dur_ms)
{
    long secs = jc_toolloop_shell_timeout_secs(cmd);
    if (secs <= 0) {
        return 0;
    }
    /* Cut off at its own bound: the wall-time reached (>= 90% of) the limit. */
    return dur_ms >= 0.9 * (double)secs * 1000.0;
}

/* Find or append a (tool, key, exact) row. Returns its index, or -1 when the table
 * is full and the pair is new. */
static int slot(struct jc_toolloop *w, const char *tool, const char *key, int exact)
{
    int i;
    /* M868 (LOOP-1): compared by hash and length, as the success-side watch
     * (jc_noprogress) compares its triples -- the whole key, however long. */
    unsigned long kl = (unsigned long)strlen(key);
    unsigned long kh = jc_reread_hash(key, kl);

    for (i = 0; i < w->n; i++) {
        if (w->exact[i] == exact && w->keylen[i] == kl && w->keyhash[i] == kh &&
            strcmp(w->tool[i], tool) == 0) {
            return i;
        }
    }
    if (w->n >= JC_TOOLLOOP_MAX_ENTRIES) {
        return -1;
    }
    i = w->n++;
    jc_snprintf(w->tool[i], JC_TOOLLOOP_TOOL_MAX, "%s", tool);
    w->keyhash[i] = kh;
    w->keylen[i] = kl;
    w->exact[i] = exact;
    w->count[i] = 0;
    w->told[i] = 0;
    return i;
}

enum jc_toolloop_verdict jc_toolloop_note(struct jc_toolloop *w, const char *tool,
                                          const char *args, enum jc_fail_class cls,
                                          int *count_out)
{
    int ie;
    int ic;

    if (count_out != NULL) {
        *count_out = 0;
    }
    if (w == NULL || tool == NULL || tool[0] == '\0') {
        return JC_TOOLLOOP_NONE;
    }

    /* Both keys are bumped on every failure, always -- a loop that starts identical
     * and then varies its argument must not reset the class count on the way.
     *
     * R6 EXCEPTION: JC_FAIL_AT_BOUND uses the EXACT key ONLY (no class slot). "Ran
     * to its own time limit" is a shape, not a cause; lumping every distinct
     * bound-ended command into one class slot would fire on a working agent that
     * legitimately runs several different capped commands. The loop is the SAME
     * (normalised) command cut off again and again, which the exact key catches. */
    ie = slot(w, tool, (args != NULL) ? args : "", 1);
    ic = (cls == JC_FAIL_AT_BOUND)
             ? -1 : slot(w, tool, jc_fail_class_name(cls), 0);
    if (ie >= 0) {
        w->count[ie]++;
    }
    if (ic >= 0) {
        w->count[ic]++;
    }

    /* EXACT outranks CLASS: it is the more specific finding, and byte-identical
     * repetition earns the stronger advice.
     *
     * EXACT RE-FIRES at each multiple of the threshold (R6). The old code told
     * once and fell silent -- which is exactly how the 2026-09-26 drive's loop ran
     * for three hours after a single ignored note. One note per THRESHOLD-worth of
     * repeats (every 3rd, 6th, 9th...) escalates without becoming the per-call nag
     * M323 measured. The paired class slot is still marked told the first time, so
     * one problem is never told through two keys in the same turn. */
    if (ie >= 0 && w->count[ie] >= JC_TOOLLOOP_EXACT_AT &&
        (w->count[ie] % JC_TOOLLOOP_EXACT_AT) == 0) {
        w->told[ie] = 1;
        if (ic >= 0) {
            w->told[ic] = 1;
        }
        if (count_out != NULL) {
            *count_out = w->count[ie];
        }
        return JC_TOOLLOOP_EXACT;
    }
    if (ic >= 0 && !w->told[ic] && w->count[ic] >= JC_TOOLLOOP_CLASS_AT) {
        w->told[ic] = 1;
        if (count_out != NULL) {
            *count_out = w->count[ic];
        }
        return JC_TOOLLOOP_CLASS;
    }
    return JC_TOOLLOOP_NONE;
}

void jc_toolloop_render(enum jc_toolloop_verdict v, const char *tool,
                        enum jc_fail_class cls, int count,
                        char *out, jc_size cap)
{
    const char *advice;

    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (v == JC_TOOLLOOP_NONE) {
        return;
    }

    /* R6: a command cut off at its own time limit, again and again. Its own note,
     * because the cause is neither the arguments nor a missing target -- the
     * command does not finish in the time it is given, and re-running it will time
     * out again. Named separately from the failure classes below. */
    if (cls == JC_FAIL_AT_BOUND) {
        jc_snprintf(out, cap,
            "\n\n[jichi] NOTE: `%s` has now run to its own time limit and been cut "
            "off %d times this turn (the same command, however it is wrapped). It is "
            "not completing and will keep timing out -- change what you run or how "
            "you wait for it, rather than running it again.",
            (tool != NULL) ? tool : "this command", count);
        return;
    }

    /* Per-class advice. A wrong cause is a loop AMPLIFIER (M342), so where the class
     * is unknown this says less rather than guessing -- the honest floor is "stop
     * repeating and change approach", which is true of every class. */
    switch (cls) {
    case JC_FAIL_NOT_FOUND:
        advice = "the thing this call names does not exist as written -- read the "
                 "file or list the directory to find its real name or exact text, "
                 "rather than adjusting the same call again";
        break;
    case JC_FAIL_DENIED:
        advice = "this is refused by policy, not failing by accident: it will not "
                 "succeed by rephrasing. Work within what is permitted, or say in "
                 "your final answer that the task needs something you were denied";
        break;
    case JC_FAIL_BAD_ARGS:
        advice = "the arguments are the problem, not the target -- re-read this "
                 "tool's schema and send the shape it asks for";
        break;
    case JC_FAIL_KILLED:
        advice = "the call was killed for exceeding a limit, so a larger or slower "
                 "version of it will be killed too -- narrow it (fewer results, a "
                 "range, a smaller scope) rather than retrying it";
        break;
    case JC_FAIL_NONZERO_EXIT:
        advice = "the command ran and reported failure -- read its output for the "
                 "cause before running it again";
        break;
    case JC_FAIL_OTHER:
    default:
        advice = "repeating it is not working -- change the approach rather than "
                 "the wording";
        break;
    }

    if (v == JC_TOOLLOOP_EXACT) {
        /* Counted, not ordinal: "the 3th time" is what a %d + "th" produces, and
         * ordinal logic is not worth a branch in a note. */
        jc_snprintf(out, cap,
            "\n\n[jichi] NOTE: `%s` has now failed %d times this turn with the "
            "SAME arguments. %s.",
            (tool != NULL) ? tool : "this tool", count, advice);
    } else {
        jc_snprintf(out, cap,
            "\n\n[jichi] NOTE: `%s` has now failed %d times this turn for the same "
            "reason (%s), with different arguments each time. Varying the argument "
            "is not addressing the cause: %s.",
            (tool != NULL) ? tool : "this tool", count,
            jc_fail_class_name(cls), advice);
    }
}

/* ---- the success twin (plan D1, M733) -- see jc_toolloop.h ---------------- */

void jc_noprogress_init(struct jc_noprogress *w)
{
    if (w != NULL) {
        memset(w, 0, sizeof(*w));
    }
}

enum jc_noprogress_role jc_noprogress_role(const char *tool, int readonly)
{
    static const char *const IGNORED[] = {
        "read_file", "read_background_output", "todowrite", "todoread", NULL
    };
    static const char *const OUTSIDE[] = {
        "ask_user", "ask_for_help", "hint", NULL
    };
    int i;

    if (tool == NULL || tool[0] == '\0') {
        return JC_NOPROGRESS_IGNORE;
    }
    for (i = 0; IGNORED[i] != NULL; i++) {
        if (strcmp(tool, IGNORED[i]) == 0) {
            return JC_NOPROGRESS_IGNORE;
        }
    }
    for (i = 0; OUTSIDE[i] != NULL; i++) {
        if (strcmp(tool, OUTSIDE[i]) == 0) {
            return JC_NOPROGRESS_RESET;
        }
    }
    if (strcmp(tool, "run_terminal_command") == 0 ||
        strcmp(tool, "run_tests") == 0) {
        return JC_NOPROGRESS_COUNT;
    }
    return readonly ? JC_NOPROGRESS_COUNT : JC_NOPROGRESS_RESET;
}

void jc_noprogress_reset(struct jc_noprogress *w)
{
    int i;

    if (w == NULL) {
        return;
    }
    for (i = 0; i < w->n; i++) {
        w->count[i] = 0;
    }
    /* M870: an edit is something new; the window starts again. */
    w->ring_n = 0;
    w->ring_at = 0;
    w->stale = 0;
}

int jc_noprogress_note(struct jc_noprogress *w, const char *tool,
                       const char *args, const char *result)
{
    unsigned long th, ah, an, rh, rn;
    int i;
    int hit = -1;

    if (w == NULL || tool == NULL || tool[0] == '\0') {
        return 0;
    }
    if (args == NULL) {
        args = "";
    }
    if (result == NULL) {
        result = "";
    }
    th = jc_reread_hash(tool, (unsigned long)strlen(tool));
    an = (unsigned long)strlen(args);
    ah = jc_reread_hash(args, an);
    rn = (unsigned long)strlen(result);
    rh = jc_reread_hash(result, rn);
    w->clock++;

    for (i = 0; i < w->n; i++) {
        if (w->tool_h[i] == th && w->args_h[i] == ah && w->args_n[i] == an &&
            w->res_h[i] == rh && w->res_n[i] == rn) {
            hit = i;
            break;
        }
    }
    if (hit < 0) {
        if (w->n < JC_NOPROGRESS_MAX_ENTRIES) {
            hit = w->n++;
        } else {
            /* Full: forget the call seen longest ago. A forgotten call starts
             * again at one, so the cost is a missed or a later note. */
            hit = 0;
            for (i = 1; i < w->n; i++) {
                if (w->last[i] < w->last[hit]) {
                    hit = i;
                }
            }
        }
        w->tool_h[hit] = th;
        w->args_h[hit] = ah;
        w->args_n[hit] = an;
        w->res_h[hit] = rh;
        w->res_n[hit] = rn;
        w->count[hit] = 0;
        w->told[hit] = 0;
    }
    w->count[hit]++;
    w->last[hit] = w->clock;

    /* M870 (N21): the novelty window -- independent of the counts above and of
     * the shell rule below, which is why it sees a rotation they cannot. */
    {
        unsigned long ch = th * 1000003UL ^ ah * 31UL ^ an ^ (rh * 7UL) ^ (rn << 1);
        int seen = 0;
        int k;
        for (k = 0; k < w->ring_n; k++) {
            if (w->ring[k] == ch) {
                seen = 1;
                break;
            }
        }
        w->stale = seen ? w->stale + 1 : 0;
        w->ring[w->ring_at] = ch;
        w->ring_at = (w->ring_at + 1) % JC_NOVELTY_WINDOW;
        if (w->ring_n < JC_NOVELTY_WINDOW) {
            w->ring_n++;
        }
    }

    /* A shell command may have changed what every OTHER call observes, and
     * nothing here can see whether it did, so their repeats start again. Its own
     * count is kept: the same command answering the same way is the loop. */
    if (strcmp(tool, "run_terminal_command") == 0) {
        for (i = 0; i < w->n; i++) {
            if (i != hit) {
                w->count[i] = 0;
            }
        }
    }
    /* M819 (N4a, threadwork's weekend drive): RE-WARN at each multiple of the
     * threshold, as M806 does for the failure detector's exact key -- not once and
     * then silent. On the drive a worker made the same passing call 1,126 times in
     * one turn and the note fired once; the loop ran until a chance compaction
     * elided the repeats. A child's whole task is one turn, so "once per turn" is
     * "once, ever". Firing at 3, 6, 9 ... keeps the loss in view. A genuine edit
     * still resets every count (the RESET role), so this only re-fires inside an
     * unbroken same-call loop. `told` is kept only to mark the first note for a
     * reader; the firing no longer depends on it. */
    if (w->count[hit] >= JC_NOPROGRESS_NOTE_AT &&
        (w->count[hit] % JC_NOPROGRESS_NOTE_AT) == 0) {
        w->told[hit] = 1;
        return w->count[hit];
    }
    return 0;
}

void jc_noprogress_render(const char *tool, int count, char *out, jc_size cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    /* Only what was measured: the same arguments and the same result. Not
     * "nothing changed" -- a shell command between the repeats may have changed
     * files, and the loop cannot tell. */
    jc_snprintf(out, cap,
        "\n\n[jichi] NOTE: this exact `%s` call has now returned the same result "
        "%d times this turn. Repeating it will not change the answer -- use the "
        "result you already have, or change the approach.",
        (tool != NULL && tool[0] != '\0') ? tool : "tool", count);
}
