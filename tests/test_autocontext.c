/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_autocontext.c - auto-context budget sizing (jc_autocontext_budget). */

#include "jc_test.h"
#include "jc_autocontext.h"

void test_autocontext(void)
{
    /* cap below a third of the limit => the cap wins. */
    JC_CHECK(jc_autocontext_budget(30000, 3000) == 3000);

    /* cap above a third of the limit => clamped to limit/3. */
    JC_CHECK(jc_autocontext_budget(6000, 3000) == 2000);

    /* no limit => the cap is returned unchanged. */
    JC_CHECK(jc_autocontext_budget(0, 3000) == 3000);

    /* cap unset (<=0) => the built-in default (3000). */
    JC_CHECK(jc_autocontext_budget(30000, 0) == 3000);
    JC_CHECK(jc_autocontext_budget(0, 0) == 3000);

    /* negative limit behaves like no limit. */
    JC_CHECK(jc_autocontext_budget(-1, 1500) == 1500);
}
