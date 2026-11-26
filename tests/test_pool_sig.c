/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_pool_sig.c - def-377 W1 (M1194): the child's progress `sig` line.
 *
 * The pure, unit-reachable half of W1 is the pmsg codec: a child streams
 * {"t":"sig","edits":E,"placeholders":P,"compactions":C,"dup24":D} at every
 * tool result, and the parent parses it. The emit + the counters + the dup24
 * ring are exercised by wave C's driver (notes/M1194.md section 3, cells
 * 2-6/11); here we pin the codec contract.
 *
 * RED on the head: the parser has no "sig" case, so a well-formed sig line
 * reads JC_PMSG_NONE, not JC_PMSG_SIG -- the first check below fails. That is
 * W1.7's compatibility guarantee in reverse: an OLDER parent (no sig case)
 * reads the line as NONE and ignores it. The unknown-t and malformed lines
 * pin the SAME default branch and stay green before AND after the fix.
 */
#include "jc_test.h"
#include "jc_parallel.h"
#include <stdlib.h>
#include <string.h>

void test_pool_sig(void);   /* def-377 W1: file-local prototype (-Wmissing-prototypes) */

static void test_sig_well_formed(void)
{
    struct jc_pmsg m;
    int k;
    /* W1.1: all four values present. RED on the head: k is JC_PMSG_NONE. */
    k = jc_parallel_parse_msg(
        "{\"t\":\"sig\",\"edits\":3,\"placeholders\":2,\"compactions\":1,\"dup24\":0.4}",
        &m);
    JC_CHECK(k == JC_PMSG_SIG);
    JC_CHECK(m.sig_edits == 3);
    JC_CHECK(m.sig_placeholders == 2);
    JC_CHECK(m.sig_compactions == 1);
    JC_CHECK_NEAR(m.sig_dup24, 0.4);
    free(m.answer); free(m.error); free(m.last);
}

static void test_sig_missing_degrades_to_zero(void)
{
    struct jc_pmsg m;
    /* W1.1: the four numbers are all optional on the wire; a missing number
     * degrades to 0 (the lenient absent-means-default rule the rest of the
     * done/tool fields share). RED on the head: k is NONE, not SIG. */
    JC_CHECK(jc_parallel_parse_msg("{\"t\":\"sig\"}", &m) == JC_PMSG_SIG);
    JC_CHECK(m.sig_edits == 0);
    JC_CHECK(m.sig_placeholders == 0);
    JC_CHECK(m.sig_compactions == 0);
    JC_CHECK(m.sig_dup24 == 0.0);
    free(m.answer); free(m.error); free(m.last);
}

static void test_sig_old_parent_none(void)
{
    struct jc_pmsg m;
    /* W1.7: an OLDER parent reads an unknown t as JC_PMSG_NONE (the parser's
     * default branch) and a non-JSON line as NONE. This IS the old reader's
     * code path, so it holds before AND after the fix (the sig case must not
     * change the default). */
    JC_CHECK(jc_parallel_parse_msg("{\"t\":\"otherkind\"}", &m) == JC_PMSG_NONE);
    JC_CHECK(jc_parallel_parse_msg("not json at all", &m) == JC_PMSG_NONE);
    free(m.answer); free(m.error); free(m.last);
}

void test_pool_sig(void)
{
    test_sig_well_formed();
    test_sig_missing_degrades_to_zero();
    test_sig_old_parent_none();
}
