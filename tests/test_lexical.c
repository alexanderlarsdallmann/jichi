/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_lexical.c - BM25-lite lexical scoring (jc_lexical_topn). */

#include "jc_test.h"
#include "jc_lexical.h"

void test_lexical(void)
{
    const char *docs[4];
    int idx[4];
    double sc[4];
    int n;

    docs[0] = "the quick brown fox";
    docs[1] = "lazy dog sleeps all day";
    docs[2] = "quantum entanglement physics";
    docs[3] = "the fox and the hound";

    /* "fox" appears in docs 0 and 3 only. */
    n = jc_lexical_topn(docs, 4, "fox", 4, idx, sc);
    JC_CHECK(n == 2);
    JC_CHECK(sc[0] >= sc[1]);          /* descending */
    JC_CHECK((idx[0] == 0 || idx[0] == 3));
    JC_CHECK(sc[0] > 0.0);

    /* No matching term => no results. */
    n = jc_lexical_topn(docs, 4, "zzz", 4, idx, sc);
    JC_CHECK(n == 0);

    /* Empty / whitespace query => no usable terms. */
    n = jc_lexical_topn(docs, 4, "", 4, idx, sc);
    JC_CHECK(n == 0);
    n = jc_lexical_topn(docs, 4, "   ", 4, idx, sc);
    JC_CHECK(n == 0);

    /* Identifier splitting: a query term matches a piece of jc_lexical_topn. */
    {
        const char *code[2];
        code[0] = "void jc_lexical_topn(const char *query)";
        code[1] = "unrelated documentation paragraph here";
        n = jc_lexical_topn(code, 2, "lexical", 2, idx, sc);
        JC_CHECK(n == 1);
        JC_CHECK(idx[0] == 0);
    }

    /* top_n caps the result count. */
    n = jc_lexical_topn(docs, 4, "the", 1, idx, sc);
    JC_CHECK(n == 1);

    /* Guards. */
    JC_CHECK(jc_lexical_topn(NULL, 4, "x", 4, idx, sc) == 0);
    JC_CHECK(jc_lexical_topn(docs, 0, "x", 4, idx, sc) == 0);
    JC_CHECK(jc_lexical_topn(docs, 4, NULL, 4, idx, sc) == 0);
    JC_CHECK(jc_lexical_topn(docs, 4, "x", 0, idx, sc) == 0);
}
