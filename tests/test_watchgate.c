/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_watchgate.c - the pure withdrawal engine (M1153, H9a, behaviour 7).
 *
 * The 13:47 rule: a later SENTENCE of the LOG withdraws the READY commit only
 * if it says WITHDRAWN (case-sensitive), names the READY commit (a commit
 * token whose first seven hex equal c7), and names NO OTHER commit token (a
 * token of 7-40 lowercase hex bounded by non-alphanumerics, containing at
 * least one letter a-f -- so the date 20261105 is not one). An ambiguous
 * sentence does not withdraw: a missed withdrawal costs one gate run the lead
 * can replace; a false one costs a drive that waits silently for a verdict
 * that never comes. The engine is pure (no I/O) and exported from
 * jc_hp_watchgate.c for this table; the smoke driver cell 11 (the a-f / date /
 * decimal / other-commit cases) and cells 7-11 (the 11:32 shape A, the 13:32
 * shape J, the genuine sole withdrawal H, --dry) hold the END-TO-END arms,
 * and this table pins each half at the boundary.
 *
 * A "commit token" is a maximal run of 7-or-more lowercase hex bounded by
 * non-alphanumerics on both sides (the sentence is padded with a space on each
 * side -- the reference awk's u = " " t " " idiom). A run of 6 or fewer hex
 * is no token at all; a run of 41+ is no commit at all (not "other").
 */

#include "jc_test.h"
#include "jc_hp_watchgate.h"

#include <string.h>

/* Local forward declaration (-Wmissing-prototypes; the M1124 idiom -- the
 * runner's declaration in test_main.c is in a different translation unit). */
void test_watchgate(void);

void
test_watchgate(void)
{
    /* c7 = the READY commit's first seven hex. */
    const char *c7 = "abcd123";

    /* (1) The genuine sole withdrawal (H): WITHDRAWN + the READY commit + the
     *     date 20261105 beside it -- the date has no a-f, so it is NOT a
     *     commit token, and the sentence stands as a withdrawal. */
    JC_CHECK(jc_gate_withdrawn_sentence(
        "abcd123e is WITHDRAWN per the note 20261108-2237.", c7) == 1);

    /* (2) The 11:32 shape (A): WITHDRAWN + the READY commit + ANOTHER commit
     *     (m2, 7-hex with an a-f) named beside it -- ambiguous: NOT a
     *     withdrawal. The 307de990 "is WITHDRAWN" clause names m2; the pause-
     *     check clause names the READY abcd123e -- both in one sentence. */
    JC_CHECK(jc_gate_withdrawn_sentence(
        "the batch at abcd123e is still READY; the earlier 307de990 is "
        "WITHDRAWN", c7) == 0);

    /* (3) The 13:32 shape (J): the reference's exact sentence naming the
     *     EARLIER m3 (WITHDRAWN) and m2 beside the READY -- the READY is not
     *     named at all: self=0, NOT a withdrawal. */
    JC_CHECK(jc_gate_withdrawn_sentence(
        "The earlier submissions at 307de990 (WITHDRAWN) and 5f6e7d8c will "
        "not be gated; the gate runs on abcd123e per the READY line", c7) == 0);

    /* (4) No WITHDRAWN: a plain sentence naming the READY commit does not
     *     withdraw. */
    JC_CHECK(jc_gate_withdrawn_sentence(
        "abcd123e is still READY, see the note", c7) == 0);

    /* (5) WITHDRAWN but the READY commit is not named (only another commit):
     *     self=0, NOT a withdrawal. */
    JC_CHECK(jc_gate_withdrawn_sentence(
        "307de990 is WITHDRAWN per the operator", c7) == 0);

    /* (6) Case-sensitive: lowercase "withdrawn" is not WITHDRAWN. */
    JC_CHECK(jc_gate_withdrawn_sentence(
        "abcd123e is withdrawn per the note", c7) == 0);

    /* (7) The READY commit named by a LONGER token (8-hex abcd1234, first
     *     seven = abcd123): self holds at any length (the reference's
     *     substr(x,1,7) == c). */
    JC_CHECK(jc_gate_withdrawn_sentence(
        "abcd1234 is WITHDRAWN", c7) == 1);

    /* (8) The other token is 41+ hex: no commit at all, so not "other" --
     *     the sentence still withdraws. */
    JC_CHECK(jc_gate_withdrawn_sentence(
        "abcd1234 is WITHDRAWN (see "
        "0123456789abcdef0123456789abcdef0123456789ab for the log)", c7) == 1);

    /* (9) A pure-digit 8-hex token (the date 20261105, no a-f) beside the
     *     READY: not a commit token, so the sentence still withdraws. */
    JC_CHECK(jc_gate_withdrawn_sentence(
        "abcd1234 is WITHDRAWN on 20261105", c7) == 1);

    /* (10) A 12-digit decimal ref (no a-f) beside the READY: not a commit
     *      token, so the sentence still withdraws. */
    JC_CHECK(jc_gate_withdrawn_sentence(
        "abcd1234 is WITHDRAWN, see 123456789012", c7) == 1);

    /* (11) The other token is 6 hex (below the 7-min): no token, not
     *      "other" -- the sentence still withdraws. */
    JC_CHECK(jc_gate_withdrawn_sentence(
        "abcd1234 is WITHDRAWN (ref 123456)", c7) == 1);

    /* (12) A token WITH an a-f letter but only 6 hex (12345a): below the
     *      7-min, so no token at all, not "other". */
    JC_CHECK(jc_gate_withdrawn_sentence(
        "abcd1234 is WITHDRAWN (ref 12345a)", c7) == 1);

    /* (13) jc_gate_withdrawn splits on [.!?]+[ \t]+: the withdrawal in the
     *      SECOND sentence of the line (the first names the READY without
     *      WITHDRAWN, the second withdraws it). */
    JC_CHECK(jc_gate_withdrawn(
        "abcd1234 is still READY. abcd1234 is WITHDRAWN per the note.",
        c7) == 1);

    /* (14) jc_gate_withdrawn: WITHDRAWN in one sentence, the READY commit in
     *      ANOTHER (no single sentence names both): NOT a withdrawal. */
    JC_CHECK(jc_gate_withdrawn(
        "abcd1234 is still READY. The earlier 307de990 is WITHDRAWN.",
        c7) == 0);

    /* (15) NULL inputs: not a withdrawal, no crash. */
    JC_CHECK(jc_gate_withdrawn_sentence(NULL, c7) == 0);
    JC_CHECK(jc_gate_withdrawn_sentence("abcd1234 is WITHDRAWN", NULL) == 0);

    /* (16) M1178: jc_gate_withdrawn_n reads the first n bytes only. The tick
     *      hands it a line INSIDE the LOG buffer; the buffer below is two
     *      lines, the first saying WITHDRAWN and the second naming the READY
     *      commit. Bounded to line 1 it is no withdrawal; read to the end
     *      (what the tick did before M1178) the two lines are one sentence
     *      that withdraws. A body that ignored n fails the first check. */
    {
        static const char two[] =
            "the earlier plan is WITHDRAWN\nabcd123e stays the batch\n";
        size_t l1 = (size_t)(strchr(two, '\n') - two);

        JC_CHECK(jc_gate_withdrawn_n(two, l1, c7) == 0);
        JC_CHECK(jc_gate_withdrawn_n(two, strlen(two), c7) == 1);
        JC_CHECK(jc_gate_withdrawn(two, c7) == 1);
        JC_CHECK(jc_gate_withdrawn_n(NULL, 5, c7) == 0);
    }
}
