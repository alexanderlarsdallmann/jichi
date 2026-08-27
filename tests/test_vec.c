/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_vec.c - exercises jc_vec. */

#include "jc_test.h"
#include "jc_vec.h"

void test_vec(void)
{
    struct jc_vec v;
    int i;
    int *slot;

    jc_vec_init(&v, sizeof(int));
    JC_CHECK(v.len == 0);

    for (i = 0; i < 100; i++) {
        jc_vec_push(&v, &i);
    }
    JC_CHECK(v.len == 100);
    JC_CHECK(*(int *)jc_vec_at(&v, 0) == 0);
    JC_CHECK(*(int *)jc_vec_at(&v, 99) == 99);
    JC_CHECK(jc_vec_at(&v, 100) == NULL);

    slot = (int *)jc_vec_push_slot(&v);
    JC_CHECK(slot != NULL);
    JC_CHECK(*slot == 0); /* push_slot zeroes */
    *slot = 12345;
    JC_CHECK(*(int *)jc_vec_at(&v, 100) == 12345);
    JC_CHECK(v.len == 101);

    jc_vec_clear(&v);
    JC_CHECK(v.len == 0);

    jc_vec_free(&v);

    /* Vector of pointers (the common case for strings). */
    {
        struct jc_vec pv;
        const char *a = "one";
        const char *b = "two";
        jc_vec_init(&pv, sizeof(char *));
        jc_vec_push(&pv, &a);
        jc_vec_push(&pv, &b);
        JC_CHECK(pv.len == 2);
        JC_CHECK_STR(JC_VEC_STR(&pv, 1), "two");
        jc_vec_free(&pv);
    }
}
