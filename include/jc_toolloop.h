/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_toolloop.h - the in-turn tool-call loop detector (M432).
 *
 * THE GAP. Every recovery jichi has is REACTIVE TO ONE FAILURE: the retry ladder,
 * jc_jsonrepair (M148), the verify fix-forward, mid-turn compaction, rollback.
 * Nothing noticed a PATTERN ACROSS ATTEMPTS within a turn, so a model could call
 * the same tool, fail the same way, and try again -- dozens of times -- and never
 * be told it was repeating itself.
 *
 * Two thirds of this shape already shipped. M89's jc_env_note_failure tells a model
 * its VERIFY keeps failing the same way; M429's jc_env_note_blocked tells it a
 * POLICY-BLOCKED call will not succeed however it is rephrased. Tool calls were the
 * one place the shape was missing, which is what this closes.
 *
 * MEASURED, on three corpora before a line was written:
 *   294 Continue sessions (a DIFFERENT agent)  1,081 calls  50 error-turns  25 (50%)
 *   zigodot   (campaign,     2026-08-13)         595 calls  13 error-turns   4 (31%)
 *   chrtext   (ordinary use, 2026-08-14)      17,553 calls 241 error-turns  97 (40%)
 * Worst single turns: apply_patch 34 fails / 0 successes; one run_terminal_command
 * repeated 59x identically. The Continue row proves the failure mode is real in this
 * workload; the two jichi rows prove jichi shares it.
 *
 * TWO KEYS, SEPARATE THRESHOLDS, because they catch different loop SHAPES and
 * neither alone sees both:
 *   - EXACT (tool, args): gemma's loop repeated byte-identical arguments.
 *   - CLASS (tool, failure class): the 2026-08-09 loop varied one constant -- an
 *     over-cap `git log -N` escalating 300 -> 20,000,000 -- so the exact key scored
 *     ZERO over logs holding that loop 96 times.
 * Thresholds are fitted to the UNION of the two jichi corpora rather than to either:
 * >= 3 exact and >= 4 class fire on every measured loop while staying clear of the
 * 2x tail, which is thick (78 turns on chrtext) and often a legitimate retry.
 *
 * THE CLASSIFIER LIVES HERE, IN THE LOOP, not in an offline reader: at the `metrics`
 * telemetry tier a tool_call event carries only `ok:false` (plus `exit` for shell
 * tools) and NO failure reason, so a reader can see a loop's shape and never its
 * cause.
 *
 * AND THE NOTE MUST NAME A TRUE CAUSE. In the 2026-08-09 loop every kill was
 * mislabelled "exceeded the memory budget" when the output cap had fired (fixed at
 * M342), and a model acting dutifully on wrong advice is a loop AMPLIFIER. So the
 * advice here is per-class, and where the class is unknown the note says less rather
 * than guessing.
 *
 * Per-turn, fixed-size and heap-free -- the jc_editwatch shape (M105), which is the
 * SUCCESS twin of this detector at the identical insertion point in jc_agent.c.
 */
#ifndef JC_TOOLLOOP_H
#define JC_TOOLLOOP_H

#include "jc_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Distinct (tool, key) pairs tracked per turn, and how much of each key is kept.
 * A turn that exceeds the table simply stops tracking new pairs -- the detector is
 * advisory, and a bounded miss is better than an allocation in the tool loop. */
#define JC_TOOLLOOP_MAX_ENTRIES 24
#define JC_TOOLLOOP_TOOL_MAX    40
#define JC_TOOLLOOP_KEY_MAX     160

/* Thresholds, fitted to the union of the zigodot and chrtext corpora (see above).
 * The exact key is the tighter one because byte-identical repetition is never a
 * legitimate retry strategy; a varied-argument loop gets one more attempt before
 * being told, since varying the argument IS a form of trying something different. */
#define JC_TOOLLOOP_EXACT_AT 3
#define JC_TOOLLOOP_CLASS_AT 4

/* Why a tool call failed, as far as the result text can say. The vocabulary is the
 * one the observability-seams proposal names for a future bounded `tool_call`
 * field, so the two designs cannot drift into different words for one thing. */
enum jc_fail_class {
    JC_FAIL_OTHER = 0,     /* unclassified -- the note then says less           */
    JC_FAIL_NOT_FOUND,     /* a path/symbol/string the call named does not exist */
    JC_FAIL_DENIED,        /* refused by a fence, permission or policy          */
    JC_FAIL_BAD_ARGS,      /* malformed or missing arguments                    */
    JC_FAIL_KILLED,        /* timed out, or killed on an output/memory cap      */
    JC_FAIL_NONZERO_EXIT   /* a command ran and returned non-zero               */
};

/* Which key tripped, if any. EXACT outranks CLASS when both do, because it is the
 * more specific finding and the advice for it is stronger. */
enum jc_toolloop_verdict {
    JC_TOOLLOOP_NONE = 0,
    JC_TOOLLOOP_EXACT,
    JC_TOOLLOOP_CLASS
};

struct jc_toolloop {
    char tool[JC_TOOLLOOP_MAX_ENTRIES][JC_TOOLLOOP_TOOL_MAX];
    char key[JC_TOOLLOOP_MAX_ENTRIES][JC_TOOLLOOP_KEY_MAX];
    int  exact[JC_TOOLLOOP_MAX_ENTRIES];   /* 1 => key is the argument text     */
    int  count[JC_TOOLLOOP_MAX_ENTRIES];
    int  told[JC_TOOLLOOP_MAX_ENTRIES];    /* already reported for this pair    */
    int  n;
};

/* Zero the watch. No allocation; safe to leave un-freed. */
void jc_toolloop_init(struct jc_toolloop *w);

/* Classify a failed tool result. `result` is the text the model will read; `exit_status`
 * is the tool's exit code where it has one (pass -1 when it does not). Pure;
 * unit-tested. Order matters: a killed call and a denied call BOTH often mention a
 * path, so the more specific causes are tested first. */
enum jc_fail_class jc_fail_classify(const char *result, int exit_status);

/* Record one FAILED call and return which threshold it just crossed.
 *
 * Call this only for a failure that is neither policy-blocked (M429 owns that, and
 * its advice is the opposite) nor a verify (M89 owns that). Returns NONE when no
 * threshold was crossed, or when it was already reported for this pair -- so the
 * caller may call it unconditionally and each finding is told once per turn.
 * `*count_out` receives the repeat count when the return is not NONE. Pure. */
enum jc_toolloop_verdict jc_toolloop_note(struct jc_toolloop *w, const char *tool,
                                          const char *args, enum jc_fail_class cls,
                                          int *count_out);

/* The note the model reads. Per-class advice, because a wrong cause amplifies a
 * loop; `JC_FAIL_OTHER` deliberately says less rather than guessing. Never names a
 * tool the caller has not told it about, per the M319 rule that a nudge must not
 * point at a tool this run does not advertise. Pure; unit-tested. */
void jc_toolloop_render(enum jc_toolloop_verdict v, const char *tool,
                        enum jc_fail_class cls, int count,
                        char *out, jc_size cap);

/* The class name, for the journal event and telemetry. Pure. */
const char *jc_fail_class_name(enum jc_fail_class c);

#ifdef __cplusplus
}
#endif
#endif /* JC_TOOLLOOP_H */
