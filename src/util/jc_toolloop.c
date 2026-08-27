/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_toolloop.c - the in-turn tool-call loop detector (see jc_toolloop.h). */

#include "jc_toolloop.h"
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

enum jc_fail_class jc_fail_classify(const char *result, int exit_status)
{
    if (result == NULL || result[0] == '\0') {
        return (exit_status > 0) ? JC_FAIL_NONZERO_EXIT : JC_FAIL_OTHER;
    }
    /* DENIED first: a refusal usually names the path it refused, so testing
     * not-found before it would misfile every fence denial as a missing file --
     * and "the file does not exist" is the wrong advice for "you may not write
     * there". This is the wrong-cause hazard M342 paid for. */
    if (has(result, "denied") || has(result, "outside this run's") ||
        has(result, "outside the workspace") || has(result, "not permitted") ||
        has(result, "refused") || has(result, "requires approval") ||
        has(result, "read-only") || has(result, "forbidden")) {
        return JC_FAIL_DENIED;
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

/* Find or append a (tool, key, exact) row. Returns its index, or -1 when the table
 * is full and the pair is new. */
static int slot(struct jc_toolloop *w, const char *tool, const char *key, int exact)
{
    int i;

    for (i = 0; i < w->n; i++) {
        if (w->exact[i] == exact &&
            strcmp(w->tool[i], tool) == 0 && strcmp(w->key[i], key) == 0) {
            return i;
        }
    }
    if (w->n >= JC_TOOLLOOP_MAX_ENTRIES) {
        return -1;
    }
    i = w->n++;
    jc_snprintf(w->tool[i], JC_TOOLLOOP_TOOL_MAX, "%s", tool);
    jc_snprintf(w->key[i], JC_TOOLLOOP_KEY_MAX, "%s", key);
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
     * and then varies its argument must not reset the class count on the way. */
    ie = slot(w, tool, (args != NULL) ? args : "", 1);
    ic = slot(w, tool, jc_fail_class_name(cls), 0);
    if (ie >= 0) {
        w->count[ie]++;
    }
    if (ic >= 0) {
        w->count[ic]++;
    }

    /* EXACT outranks CLASS: it is the more specific finding, and byte-identical
     * repetition earns the stronger advice.
     *
     * ONE NOTE PER LOOP, not one per key. When the exact key fires, the class slot
     * is marked told as well -- otherwise the very next identical failure crosses
     * the class threshold and the model is told twice about one problem, same tool,
     * same cause, same arguments. That is the nag M323 measured (1,038 warnings from
     * one unthrottled condition) arriving by a side door. Found by this module's own
     * unit test, on the first run.
     *
     * A LATER failure of the same tool in a DIFFERENT class still has its own slot
     * and can still fire, which is the distinction worth keeping. */
    if (ie >= 0 && !w->told[ie] && w->count[ie] >= JC_TOOLLOOP_EXACT_AT) {
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
