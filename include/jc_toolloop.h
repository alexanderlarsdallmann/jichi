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
    JC_FAIL_NONZERO_EXIT,  /* a command ran and returned non-zero               */
    JC_FAIL_AT_BOUND       /* ran until its OWN time limit and was cut off (R6) */
};
#define JC_FAIL_CLASS_COUNT 7  /* M876: the values above, for per-class counts */

/* Which key tripped, if any. EXACT outranks CLASS when both do, because it is the
 * more specific finding and the advice for it is stronger. */
enum jc_toolloop_verdict {
    JC_TOOLLOOP_NONE = 0,
    JC_TOOLLOOP_EXACT,
    JC_TOOLLOOP_CLASS
};

struct jc_toolloop {
    char tool[JC_TOOLLOOP_MAX_ENTRIES][JC_TOOLLOOP_TOOL_MAX];
    /* M868 (LOOP-1): the key as a hash and a length. M432 kept the key's text cut
     * to JC_TOOLLOOP_KEY_MAX - 1 bytes and compared it with the WHOLE incoming key,
     * so two failures whose arguments ran to 160 bytes or more never matched: 77%
     * of the demos drive's failed calls (a worktree's `cd .../wt-N &&` prefix alone
     * is ~75 bytes) went unseen, each adding a row until the table was full. */
    unsigned long keyhash[JC_TOOLLOOP_MAX_ENTRIES];
    unsigned long keylen[JC_TOOLLOOP_MAX_ENTRIES];
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
 * path, so the more specific causes are tested first.
 *
 * `tool` is the tool name, used to apply stricter rules for shell tools:
 * for `run_terminal_command` and `run_tests`, DENIED is only returned when
 * the result BEGINS with jichi's own refusal phrases (e.g., "refused by safety fence");
 * a nonzero exit with ordinary output (including text like "connection refused"
 * from the command itself) is JC_FAIL_NONZERO_EXIT. For all other tools, the
 * current wording-based classification is preserved.
 *
 * `policy_refusal` is the flag set by the executor's fence checks ( jc_tool.c:
 * read-only gate, main-agent-only gate). If non-zero, the result is classified
 * as JC_FAIL_DENIED regardless of the wording, because the refusal is by policy
 * (the fence) not by capability or accident. This fixes M1041-b: the read-only
 * refusal was classified DENIED before M1041 and `other` after, and the driver
 * could not verify the shell branch because it read advice prose instead of the
 * producer's class. */
enum jc_fail_class jc_fail_classify(const char *result, int exit_status,
                                     const char *tool, int policy_refusal);

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

/* ---- R6: a shell command that keeps ending at its own time limit -------------
 *
 * THE GAP THIS CLOSES. On the 2026-09-26 drive one worker ran the SAME hanging
 * check for three hours, and the loop detector above never fired -- for two
 * reasons this addresses. (1) The check was spelled a little differently each
 * time (`timeout 2 X`, `cd . && timeout 2 X`, `timeout 3 X`, `... 2>&1`,
 * `... | head`), so the EXACT key -- the raw argument text -- matched none of its
 * own repeats. (2) Half the runs were piped into `head`, which exits 0, so they
 * were SUCCESSES and never reached the failure detector at all; yet each one had
 * run its inner `timeout` to the end and been cut off. A call that is cut off at
 * its own bound is not making progress whether the pipeline reports 0 or 124.
 *
 * `jc_toolloop_shell_norm` derives a stable key: it strips the wrapping that does
 * not change what the command DOES -- a leading `cd DIR &&`, leading `NAME=val`
 * environment assignments, a leading `timeout [-k N] SECS`, and a trailing
 * redirection (`2>&1`, `>/dev/null`) or filter (`| head`/`| tail`/`| grep ...`).
 * It strips ONLY this wrapping: two genuinely different commands stay different
 * keys, because over-normalising would muzzle a working agent that is legitimately
 * varying what it runs. Writes at most `cap-1` bytes plus a NUL; returns the
 * written length. On NULL/empty input it writes "" and returns 0.
 *
 * `jc_toolloop_shell_timeout_secs` returns the SECONDS of a leading `timeout`
 * (after any `cd &&`/env prefix), or 0 when there is none.
 *
 * `jc_toolloop_at_bound` is the predicate the agent loop uses: the command has an
 * inner `timeout N` AND the call's measured wall-time reached it (>= 90% of N
 * seconds), so it was cut off at its own bound. Pure -- `dur_ms` is the caller's
 * measurement. These feed `jc_toolloop_note` with `JC_FAIL_AT_BOUND` and the
 * normalised key, on the SUCCESS path as well as the failure path. */
jc_size jc_toolloop_shell_norm(const char *cmd, char *out, jc_size cap);
long jc_toolloop_shell_timeout_secs(const char *cmd);
int jc_toolloop_at_bound(const char *cmd, double dur_ms);

/* ---- the success twin: the same call, the same answer (plan D1, M733) -------
 *
 * THE GAP. Everything above counts FAILED calls. A model that re-issues a call
 * that SUCCEEDS, and gets the same answer back, was invisible to it: M687 made 200
 * successful calls with 0 errors, the answer in hand by the sixteenth, and only
 * --max-tool-calls stopped it. On the 2026-09-23 drive one turn ran the same
 * search_code 35 times in a row, answered "(no matches)" each time -- truly.
 *
 * THE KEY is (tool, the whole arguments, the result), each kept as a hash and a
 * length: the same question and the same answer. A result that embeds a clock or
 * a counter never repeats, so it needs no exclusion of its own.
 *
 * THE THRESHOLD was fitted, not chosen (M731, docs/analysis/2026-09-24-the-corpus-
 * drive.md): over 47 turns of two models every futile repeat reached three and no
 * productive turn passed two -- M432's exact threshold and margin. Provisional
 * until the workstation's corpus is read the same way. The rule below, re-measured
 * as built on that drive, notes the four futile turns and no other (M733).
 *
 * WHAT A SUCCESSFUL CALL DOES TO THE WATCH (jc_noprogress_role):
 *   COUNT  - a read-only tool, and the two that RUN things (run_terminal_command,
 *            run_tests) although the registry calls them mutating: their output
 *            is exactly the answer being asked for again.
 *   RESET  - any other mutating tool: the tree changed, so a repeat after it is a
 *            new question. Its own repeats are not counted -- an edit's are
 *            M105's. The tools that bring an answer from outside (ask_user,
 *            ask_for_help, hint) reset too.
 *   IGNORE - read_file (M287 already tells a byte-identical re-read), polling a
 *            background job (repeating is what polling is), and the todo list
 *            (bookkeeping, not a question).
 * A successful shell command ALSO resets every other call's count, because it may
 * have changed what they observe and the loop cannot see whether it did -- M689's
 * sweep runs at turn end, not per command. Its own count survives: the same
 * command answering the same way is the loop itself (M687's 13 identical shell
 * calls). Failed calls do not touch this watch; they are M432's.
 *
 * COMPACTION DOES NOT RESET IT, though the plan's sketch said it should. Replayed
 * on the drive, a reset at every mid-turn elision erased bonsai 06's loop -- the
 * same failing build three times running -- because under pressure a pass elides
 * something nearly every round (9 of the 21 rounds around that loop), and what it
 * elides is OLD: recent results are protected, so the repeats stay in view. And
 * the note is appended to the newest result, which is always whole, so "use the
 * result you already have" stays true whatever an earlier pass elided.
 *
 * A NOTE AT EACH MULTIPLE OF THE THRESHOLD (M819, N2's sibling N4a), as M806 does
 * for the failure detector -- not once and then silent. The note claims only what
 * was measured (the same arguments, the same result); it never says "nothing
 * changed", which the loop cannot know for a shell command. A genuine edit resets
 * every count, so the re-warn only fires inside an unbroken same-call loop. Past
 * the table the least recently seen call is forgotten: the worst case is a missed
 * note, never an allocation in the loop. (It used to fire once per turn; on the
 * weekend drive a child made one passing call 1,126 times in its single turn and
 * was warned once, so "once per turn" was "once, ever".)
 *
 * THE STOP, at depth >= 1 (M829, N4b). The plan's second threshold ends the turn;
 * at the TOP level that is wrong -- a run has many turns and an operator to steer
 * it, and the drive's 47 turns cannot fit one stop. But a spawn_parallel/subagent
 * CHILD's whole task is ONE turn, so a loop there never gets a second chance: the
 * caller ends the child when the same successful call reaches JC_NOPROGRESS_STOP_AT
 * (after two warnings, at 3 and 6), keeping its last answer and recording the repeat
 * count in its end record (M808's `repeated`). The top level still only warns. */
#define JC_NOPROGRESS_MAX_ENTRIES 32
#define JC_NOPROGRESS_STOP_AT     9   /* M829 (N4b): end a depth>=1 child here */
/* M869 (N27 (b)): copied placeholders in a row that end a depth>=1 child. A model
 * that copies M218's placeholder as arguments has lost its thread, and rephrasing
 * cannot bring back arguments that are gone: run 0015's worker did it 742 times. */
#define JC_PLACEHOLDER_STOP_AT    3
#define JC_NOPROGRESS_NOTE_AT     3

enum jc_noprogress_role {
    JC_NOPROGRESS_COUNT = 0,
    JC_NOPROGRESS_RESET,
    JC_NOPROGRESS_IGNORE
};

/* M870 (N21): the novelty window. A call is STALE when its (tool, args, result)
 * triple appeared among the JC_NOVELTY_WINDOW calls before it; JC_NOVELTY_WINDOW
 * stale calls in a row mean the run is repeating itself in some order -- run 0012's
 * worker rotated four checks (a build, `ls`, `git status`, `git log`) for 28
 * minutes, and M829 never fired because each shell command restarts the other
 * calls' counts. A depth >= 1 child is ended there. */
#define JC_NOVELTY_WINDOW 24

struct jc_noprogress {
    unsigned long ring[JC_NOVELTY_WINDOW];   /* M870: the last calls' triples */
    int  ring_n;                             /* how many are held (<= window) */
    int  ring_at;                            /* the next slot to write        */
    int  stale;                              /* stale calls in a row          */
    unsigned long tool_h[JC_NOPROGRESS_MAX_ENTRIES];
    unsigned long args_h[JC_NOPROGRESS_MAX_ENTRIES];
    unsigned long args_n[JC_NOPROGRESS_MAX_ENTRIES];
    unsigned long res_h[JC_NOPROGRESS_MAX_ENTRIES];
    unsigned long res_n[JC_NOPROGRESS_MAX_ENTRIES];
    long last[JC_NOPROGRESS_MAX_ENTRIES];  /* when last seen, for eviction */
    int  count[JC_NOPROGRESS_MAX_ENTRIES]; /* since the last reset         */
    int  told[JC_NOPROGRESS_MAX_ENTRIES];  /* survives a reset: once a turn */
    long clock;
    int  n;
};

/* Zero the watch. No allocation; safe to leave un-freed. */
void jc_noprogress_init(struct jc_noprogress *w);

/* What a SUCCESSFUL call of `tool` does to the watch; `readonly` is the registry's
 * flag (0 for a tool the registry does not know). Pure; unit-tested. */
enum jc_noprogress_role jc_noprogress_role(const char *tool, int readonly);

/* Record one successful COUNT call. Returns its repeat count at each multiple of
 * JC_NOPROGRESS_NOTE_AT for its key (3, 6, 9 ...), else 0 -- so the caller may call
 * it for every COUNT call and a stuck loop is re-flagged, not flagged once (M819). A
 * run_terminal_command also clears every other key's count. Pure. */
int jc_noprogress_note(struct jc_noprogress *w, const char *tool,
                       const char *args, const char *result);

/* Something changed (a RESET call): every count starts again, so a fresh same-call
 * loop is measured (and re-notable) from zero. Pure. */
void jc_noprogress_reset(struct jc_noprogress *w);

/* The note the model reads. Pure; unit-tested. */
void jc_noprogress_render(const char *tool, int count, char *out, jc_size cap);

#ifdef __cplusplus
}
#endif
#endif /* JC_TOOLLOOP_H */
