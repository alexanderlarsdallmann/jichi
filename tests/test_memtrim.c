/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_memtrim.c - smoke-level unit for jc_memtrim (M218).
 *
 * Honest about its depth: the real validation is the measurement tier
 * (tests/measure/soak.py --profile retry, before/after RSS/VmHWM recorded in
 * the M218 ROADMAP entry) -- a unit test cannot assert OS-level heap
 * behaviour portably. What it CAN pin down: both entry points are callable in
 * any order, repeatedly, on both probe outcomes (JC_HAVE_MALLOC_TRIM set or
 * not), and jc_mem_release_os keeps its 0/1 contract even after real
 * malloc/free traffic. */
#include "jc_test.h"
#include "jc_memtrim.h"

#include <stdlib.h>
#include <string.h>

void test_memtrim(void)
{
    int r;
    char *big;

    jc_mem_tune();
    jc_mem_tune();                     /* idempotent */

    r = jc_mem_release_os();
    JC_CHECK(r == 0 || r == 1);

    /* Exercise the pattern the module exists for: a large transient
     * allocation freed again, then a sweep. */
    big = (char *)malloc(512 * 1024);
    JC_CHECK(big != NULL);
    if (big != NULL) {
        memset(big, 'x', 512 * 1024);
        free(big);
    }
    r = jc_mem_release_os();
    JC_CHECK(r == 0 || r == 1);
    r = jc_mem_release_os();           /* repeatable */
    JC_CHECK(r == 0 || r == 1);
}
