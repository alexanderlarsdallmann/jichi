/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_outcome.h - why a run stopped, in one place, for every surface (M688).
 *
 * THE PROBLEM THIS EXISTS FOR, measured rather than supposed. Eight stop
 * reasons are reported on five surfaces -- stdout, the exit code, the reach
 * footer, the `[envelope]` verdict and `--output json` -- and each cell was
 * wired by hand. Forty cells. M687 filled one that had sat empty for fifteen
 * milestones AFTER M322 wrote down exactly what was wrong with it, and an audit
 * on 2026-09-21 found another immediately: a run stopped by `--max-tool-calls`
 * printed `not checked: (nothing -- a verifier and an edit scope were armed)`
 * with an EMPTY answer, while its `[envelope]` line said `budget_exhausted`.
 * One surface knew; the other contradicted it.
 *
 * The defect is not carelessness. It is that the surfaces each DERIVE the fact
 * independently, from a different pile of booleans, so a new stop reason can be
 * added to one and forgotten in the rest without anything failing.
 *
 * THE MECHANISM. Every renderer switches on `enum jc_run_stop` with **no
 * `default:` label**. `-Wall` implies `-Wswitch`, and this project builds with
 * `-Werror` in CI, so adding a value to the enum turns into a BUILD ERROR at
 * every site that must handle it. The compiler holds the matrix; a reviewer
 * does not have to.
 *
 * WHAT THIS DELIBERATELY DOES NOT DO: change an exit code, change a
 * stop-reason wire string, or put anything new on stdout (M73 keeps stdout the
 * raw answer so a caller can pipe it). This is a refactor whose correctness
 * condition is that every surface says what it said before, except in the cells
 * the audit proved empty. See docs/plans/2026-09-run-outcome.md. */
#ifndef JC_OUTCOME_H
#define JC_OUTCOME_H

/* Why the run stopped. The wire strings these map to are a STABLE interface
 * (docs/EMBEDDING.md): add values, never renumber or rename. */
enum jc_run_stop {
    JC_STOP_DONE = 0,       /* the model finished and said so                */
    JC_STOP_INTERRUPTED,    /* the operator interrupted it                   */
    JC_STOP_TIMEOUT,        /* a wall-clock deadline fired                   */
    JC_STOP_ERROR,          /* the machinery failed; see the status          */
    JC_STOP_BUDGET,         /* a token / time / tool-call budget was hit     */
    JC_STOP_VERIFY_FAILED,  /* the verifier was still red at the retry limit */
    JC_STOP_SCOPE_TAINTED,  /* verify passed, but outside the edit scope     */
    JC_STOP_MAX_ITERS       /* the per-turn tool-iteration cap fired         */
};

/* The two questions every surface actually branches on, derived from the stop
 * reason exactly once so they cannot disagree between surfaces.
 *
 * `answer_complete` is the one the session that prompted this got wrong: it is
 * 0 for every reason that cuts a turn off mid-work, and an incomplete answer is
 * the largest thing a run leaves NOT CHECKED -- which is why the reach footer
 * reads it rather than re-deriving it.
 *
 * `result_tested` is separate on purpose: a run can finish everything it meant
 * to say and still have had nothing verify it, and a run can be cut off after a
 * verifier has already gone green on work it did. Collapsing the two is how
 * "verify green" came to print beside an empty answer. */
struct jc_run_outcome {
    enum jc_run_stop stop;
    int answer_complete;  /* the model stopped because it was finished      */
    int result_tested;    /* a verifier reached a verdict on what it did    */
    int answer_capped;    /* the last reply was cut at the OUTPUT CEILING   */
};

/* WHY `answer_capped` IS NOT A NEW `jc_run_stop` VALUE, since the register
 * proposed one. The enum above answers "why the RUN stopped", and a reply cut
 * at the output ceiling does not stop the run: the loop ends normally, the work
 * is kept, and it is the ANSWER that was cut. Three consequences decided it:
 *
 *   * `jc_run_stop_wire` is a stable interface (docs/EMBEDDING.md). A new value
 *     would reclassify a run that genuinely completed, so every consumer
 *     branching on `stop_reason == "done"` to mean "finished normally" would
 *     start seeing a failure where there is none.
 *   * the cap can fire on a NON-final turn and the loop carry on. A stop reason
 *     cannot express "something was cut somewhere along the way"; this field
 *     reports on the answer the caller is about to read, which is what a
 *     consumer actually asks.
 *   * the same shape already exists here and for the same reason:
 *     `result_tested` is separate rather than folded into `answer_complete`.
 *
 * It is an INPUT to the derivation rather than a property of the stop reason,
 * exactly like `verifier_concluded` and by the identical argument: only the
 * caller knows whether the provider reported a ceiling. */

/* Fill OUT from STOP and whether a verifier concluded. VERIFIER_CONCLUDED is
 * passed in rather than derived because only the caller knows whether one was
 * armed at all -- "no verifier" and "a verifier that never ran" are different
 * facts and this header refuses to guess between them. */
void jc_run_outcome_set(struct jc_run_outcome *out, enum jc_run_stop stop,
                        int verifier_concluded, int answer_capped);

/* The stable machine string for `--output json`'s `stop_reason`. */
const char *jc_run_stop_wire(enum jc_run_stop stop);

/* A short human clause for the surfaces that print prose, or NULL when the run
 * ended cleanly and there is nothing to say. Never a sentence: the callers
 * embed it. */
const char *jc_run_stop_clause(enum jc_run_stop stop);

#endif /* JC_OUTCOME_H */
