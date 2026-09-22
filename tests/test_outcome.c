/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_outcome.c - why a run stopped, decided once for every surface (M688).
 *
 * The mechanism that keeps the five surfaces honest is `-Wswitch`: a new
 * `jc_run_stop` value fails the build until each renderer handles it. That is a
 * COMPILE-time guarantee and nothing here can test it. What these tests hold is
 * the part a compiler cannot: that the mapping is right, and in particular that
 * "something went wrong" and "the answer is cut off" stay DIFFERENT questions.
 * Collapsing them is how `verify green` came to print beside an empty answer. */

#include "jc_test.h"
#include "jc_outcome.h"

#include <string.h>

static void test_truncating_vs_complete(void)
{
    struct jc_run_outcome o;

    /* Cut off mid-work: the answer is only what had been said by then. */
    jc_run_outcome_set(&o, JC_STOP_MAX_ITERS, 1, 0);
    JC_CHECK(o.answer_complete == 0);
    jc_run_outcome_set(&o, JC_STOP_BUDGET, 0, 0);
    JC_CHECK(o.answer_complete == 0);
    jc_run_outcome_set(&o, JC_STOP_TIMEOUT, 0, 0);
    JC_CHECK(o.answer_complete == 0);
    jc_run_outcome_set(&o, JC_STOP_INTERRUPTED, 0, 0);
    JC_CHECK(o.answer_complete == 0);
    jc_run_outcome_set(&o, JC_STOP_ERROR, 0, 0);
    JC_CHECK(o.answer_complete == 0);

    /* THE DISCRIMINATING PAIR. Both of these FAILED, and both said everything
     * they had to say first -- the verifier refused a finished answer. A rule
     * keyed on "did the run succeed" gets these two wrong, and would report a
     * complete answer as truncated on every red gate. */
    jc_run_outcome_set(&o, JC_STOP_VERIFY_FAILED, 1, 0);
    JC_CHECK(o.answer_complete == 1);
    jc_run_outcome_set(&o, JC_STOP_SCOPE_TAINTED, 1, 0);
    JC_CHECK(o.answer_complete == 1);

    jc_run_outcome_set(&o, JC_STOP_DONE, 1, 0);
    JC_CHECK(o.answer_complete == 1);
}

static void test_tested_is_independent(void)
{
    struct jc_run_outcome o;
    /* A finished answer nothing verified, and a truncated run whose verifier
     * had already concluded. Neither is a contradiction, and a single "was it
     * ok" flag cannot express either. */
    jc_run_outcome_set(&o, JC_STOP_DONE, 0, 0);
    JC_CHECK(o.answer_complete == 1 && o.result_tested == 0);
    jc_run_outcome_set(&o, JC_STOP_MAX_ITERS, 1, 0);
    JC_CHECK(o.answer_complete == 0 && o.result_tested == 1);
}

static void test_wire_strings_are_the_documented_ones(void)
{
    /* docs/EMBEDDING.md lists `stop_reason` as a stability tier, and these
     * strings predate this file. Pinned by value, not by round-trip: a test
     * that compares the enum to itself would pass through a rename. */
    JC_CHECK(strcmp(jc_run_stop_wire(JC_STOP_DONE), "done") == 0);
    JC_CHECK(strcmp(jc_run_stop_wire(JC_STOP_INTERRUPTED), "interrupted") == 0);
    JC_CHECK(strcmp(jc_run_stop_wire(JC_STOP_TIMEOUT), "timeout") == 0);
    JC_CHECK(strcmp(jc_run_stop_wire(JC_STOP_ERROR), "error") == 0);
    JC_CHECK(strcmp(jc_run_stop_wire(JC_STOP_BUDGET), "budget") == 0);
    JC_CHECK(strcmp(jc_run_stop_wire(JC_STOP_VERIFY_FAILED), "verify_failed") == 0);
    JC_CHECK(strcmp(jc_run_stop_wire(JC_STOP_SCOPE_TAINTED), "scope_tainted") == 0);
    JC_CHECK(strcmp(jc_run_stop_wire(JC_STOP_MAX_ITERS), "max_iters") == 0);
}

static void test_clause_is_null_only_when_clean(void)
{
    /* Callers test the NULL rather than comparing strings, so "clean" must be
     * the ONLY case that returns one -- otherwise a stop reason silently
     * renders as a clean run. */
    JC_CHECK(jc_run_stop_clause(JC_STOP_DONE) == NULL);
    JC_CHECK(jc_run_stop_clause(JC_STOP_INTERRUPTED) != NULL);
    JC_CHECK(jc_run_stop_clause(JC_STOP_TIMEOUT) != NULL);
    JC_CHECK(jc_run_stop_clause(JC_STOP_ERROR) != NULL);
    JC_CHECK(jc_run_stop_clause(JC_STOP_BUDGET) != NULL);
    JC_CHECK(jc_run_stop_clause(JC_STOP_VERIFY_FAILED) != NULL);
    JC_CHECK(jc_run_stop_clause(JC_STOP_SCOPE_TAINTED) != NULL);
    JC_CHECK(jc_run_stop_clause(JC_STOP_MAX_ITERS) != NULL);
}

static void test_null_out_is_survivable(void)
{
    jc_run_outcome_set(NULL, JC_STOP_DONE, 1, 0);   /* must not crash */
    JC_CHECK(1);
}


static void test_capped_answer_is_not_a_stop_reason(void)
{
    struct jc_run_outcome o;

    /* THE DEFECT THIS PINS, measured 2026-09-21 on a supervised zigodot drive:
     * the model's last turn hit `finish_reason == "length"`, jichi warned three
     * times on stderr, and the JSONL `done` event still said
     * `"stop_reason": "done"` with the reach footer's truncation clause silent.
     * A supervisor reading --output jsonl saw a clean finish over an answer
     * that stops mid-sentence.
     *
     * AND THE DESIGN DECISION IT PINS, which is the more important half.
     * DEFERRED.md proposed a new `JC_STOP_*` value. It is NOT one, for a reason
     * the enum's own header states: it records "why the RUN stopped", and this
     * run did not stop -- it finished, and the loop ended normally. Adding a
     * value would also change `stop_reason` on the wire for a case that
     * genuinely completed, on an interface docs/EMBEDDING.md declares stable.
     * So truncation is carried BESIDE the stop reason, the way
     * `verifier_concluded` already is, and the wire string must not move. */
    jc_run_outcome_set(&o, JC_STOP_DONE, 1, 1);
    JC_CHECK(o.answer_capped == 1);
    JC_CHECK(o.answer_complete == 0);        /* the answer was cut */
    JC_CHECK(o.stop == JC_STOP_DONE);        /* the RUN still finished */
    JC_CHECK(strcmp(jc_run_stop_wire(o.stop), "done") == 0);
    JC_CHECK(o.result_tested == 1);          /* orthogonal, and unmoved */

    /* The control: the same stop reason WITHOUT the cap is untouched. Without
     * this pair the assertion above could pass on a function that always
     * answers 0, which is the shape a floor cannot validate. */
    jc_run_outcome_set(&o, JC_STOP_DONE, 1, 0);
    JC_CHECK(o.answer_capped == 0);
    JC_CHECK(o.answer_complete == 1);

    /* Already incomplete for another reason, and capped as well: no
     * double-negative, and the cap does not resurrect a cut-off answer. */
    jc_run_outcome_set(&o, JC_STOP_BUDGET, 0, 1);
    JC_CHECK(o.answer_complete == 0);
    JC_CHECK(o.answer_capped == 1);

    /* A refused-but-finished answer that was ALSO capped is incomplete. The
     * discriminating pair above says VERIFY_FAILED leaves a complete answer --
     * that is about the verifier's refusal, not about the model running out of
     * room, and the two reasons must compose rather than override. */
    jc_run_outcome_set(&o, JC_STOP_VERIFY_FAILED, 1, 1);
    JC_CHECK(o.answer_complete == 0);
}

void test_outcome(void)
{
    test_truncating_vs_complete();
    test_capped_answer_is_not_a_stop_reason();
    test_tested_is_independent();
    test_wire_strings_are_the_documented_ones();
    test_clause_is_null_only_when_clean();
    test_null_out_is_survivable();
}
