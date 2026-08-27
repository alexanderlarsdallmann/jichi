/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_jsonrepair.c - the conservative nearly-JSON repairer (M148).
 *
 * Table-driven: each repairable class alone, combinations, and -- just as
 * important -- the inputs a CONSERVATIVE repairer must refuse (unquoted
 * keys, ambiguous quotes, unterminated strings, plain garbage). */

#include "jc_test.h"
#include "jc_jsonrepair.h"

#include <stdlib.h>
#include <string.h>

static void ok_case(const char *in, const char *want)
{
    char *got = jc_jsonrepair(in);
    JC_CHECK(got != NULL);
    if (got != NULL) {
        JC_CHECK(strcmp(got, want) == 0);
        free(got);
    }
}

static void null_case(const char *in)
{
    char *got = jc_jsonrepair(in);
    JC_CHECK(got == NULL);
    if (got != NULL) {
        free(got);
    }
}

void test_jsonrepair(void)
{
    /* Trailing commas. */
    ok_case("{\"path\": \"a.c\",}", "{\"path\": \"a.c\"}");
    ok_case("{\"a\": [1, 2, 3,],}", "{\"a\": [1, 2, 3]}");
    ok_case("{\"a\": 1, \n}", "{\"a\": 1 \n}");

    /* Missing closers, appended in nesting order. */
    ok_case("{\"a\": {\"b\": 1", "{\"a\": {\"b\": 1}}");
    ok_case("{\"a\": [1, 2", "{\"a\": [1, 2]}");

    /* Python literals -- outside strings only. */
    ok_case("{\"on\": True, \"off\": False, \"nil\": None}",
            "{\"on\": true, \"off\": false, \"nil\": null}");
    ok_case("{\"note\": \"True story\", \"flag\": True}",
            "{\"note\": \"True story\", \"flag\": true}");

    /* Single quotes, only when no double quote exists anywhere. */
    ok_case("{'path': 'a.c'}", "{\"path\": \"a.c\"}");
    /* Mixed quotes are ambiguous: refuse. */
    null_case("{'path': \"a.c'}");

    /* Combinations. */
    ok_case("{'flag': True, 'xs': [1,", "{\"flag\": true, \"xs\": [1]}");

    /* REJECTED class: unquoted keys. */
    null_case("{path: \"a.c\"}");

    /* Unterminated string: not conservatively repairable. */
    null_case("{\"path\": \"a.c");

    /* Plain garbage / empties. */
    null_case("not json at all");
    null_case("");
    null_case(NULL);

    /* A word literal fused to an identifier is NOT a Python literal. */
    null_case("{\"a\": TrueX}");
}
