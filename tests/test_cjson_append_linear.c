/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_cjson_append_linear.c - holds cJSON_AddItemToArray to O(1) (M1085/F-05).
 *
 * The defect the test exists for: building an n-element array by repeated
 * `cJSON_CreateNumber` + `cJSON_AddItemToArray` is O(n^2), because AddItemToArray
 * walks the whole list to find its tail on every append:
 *
 *     while (child->next != NULL) child = child->next;
 *
 * Appending item k walks k-1 links, so n appends walk n(n-1)/2 links in total.
 * The parser (parse_array) already kept a local tail and appended in O(1) -- the
 * builder did not, which is exactly the asymmetry F-05 found. The fix caches the
 * array/object tail on the container node (cJSON.tail), maintained in every
 * mutation path; this test holds the cache with the same hook shape M725 used for
 * parse_string sizing (a global counter incremented at the hot site, read through
 * cJSON_jc_append_steps()).
 *
 * The threshold: the finding says "append 10,000 items takes fewer than 20,000
 * steps." The old code takes ~50,000,000 (n(n-1)/2); the fixed code takes ~0
 * (every append finds its tail in the cache). 20,000 = 2n: far above the fixed
 * count, far below the old, so the test is red on the unfixed tree and green on
 * the fixed one, and it still catches a regression that, say, kept one tail walk
 * per 50 appends.
 */

#include "jc_test.h"
#include "cJSON.h"
#include <stdlib.h>

/* Append the value to the chain and record the step counter's delta, so a leg
 * can prove its appends were O(1) without a global "before" that a preceding leg
 * already advanced. */
static void append_number(cJSON *arr, double v)
{
    cJSON *item = cJSON_CreateNumber(v);
    JC_REQUIRE(item != NULL);
    JC_CHECK(cJSON_AddItemToArray(arr, item) != 0);
}

/* Mimic the external prev-based detach in src/config/jc_config_trust.c:434
 * (jc_config_trust.c:434), which unlinks an element without touching the
 * container's cached tail: it walks from the head to find the element and its
 * predecessor, splices the element out, and leaves `obj->tail` alone. Detaching a
 * MID element leaves the tail correct (the last node is untouched); this is the
 * common case, and it must not force the next append to re-walk. */
static void detach_mid(cJSON *obj, cJSON *it)
{
    cJSON *p = obj->child;
    cJSON *before = NULL;
    while (p != NULL && p != it) {
        before = p;
        p = p->next;
    }
    if (p == NULL) {
        return;
    }
    if (before == NULL) {
        obj->child = it->next;
    } else {
        before->next = it->next;
    }
    if (it->next != NULL) {
        it->next->prev = before;
    }
    it->next = NULL;
    it->prev = NULL;
}

void test_cjson_append_linear(void)
{
    int i;
    size_t before;
    size_t steps;
    cJSON *arr;
    int size;
    cJSON *it;

    /* Leg 1: the O(1) append the finding is about. */
    arr = cJSON_CreateArray();
    if (JC_REQUIRE(arr != NULL)) {
        before = cJSON_jc_append_steps();
        for (i = 0; i < 10000; i++) {
            append_number(arr, (double)i);
        }
        steps = cJSON_jc_append_steps() - before;
        /* 10,000 appends: the old code walked ~50,000,000 next steps here. The
         * fixed code walks ~0 (the tail is cached). 2 * 10,000 is the finding's
         * ceiling, with a 10x margin over the fixed count. */
        JC_CHECK(steps < (size_t)20000);
        if (steps >= (size_t)20000) {
            printf("  (cJSON_AddItemToArray walked %lu next-steps appending "
                   "10000 items; the append is not O(1))\n",
                   (unsigned long)steps);
        }
        /* The appends must also have produced a correct chain: 10,000 elements,
         * in order. */
        size = cJSON_GetArraySize(arr);
        JC_CHECK(size == 10000);
        it = cJSON_GetArrayItem(arr, 9999);
        if (JC_REQUIRE(it != NULL)) {
            JC_CHECK(it->valuedouble == (double)9999);
        }
        it = cJSON_GetArrayItem(arr, 0);
        if (JC_REQUIRE(it != NULL)) {
            JC_CHECK(it->valuedouble == 0.0);
        }
        cJSON_Delete(arr);
    }

    /* Leg 2: cJSON_ReplaceItemInObject is the detach path inside cJSON.c that
     * must keep the tail correct. Replacing an object's LAST member swaps in a
     * new node where the old tail was -- the container's cached tail must move
     * to the new node, or the next append would chain after the freed one. */
    {
        cJSON *obj = cJSON_CreateObject();
        cJSON *newv;
        if (JC_REQUIRE(obj != NULL)) {
            cJSON_AddNumberToObject(obj, "a", 1.0);
            cJSON_AddNumberToObject(obj, "b", 2.0);
            cJSON_AddNumberToObject(obj, "c", 3.0);   /* c is the tail */
            before = cJSON_jc_append_steps();
            /* Replace the last member (the tail) in place. */
            newv = cJSON_CreateNumber(99.0);
            if (JC_REQUIRE(newv != NULL)) {
                JC_CHECK(cJSON_ReplaceItemInObject(obj, "c", newv) != 0);
            }
            /* Append a new member: must use the (now newv) tail, not re-walk. */
            append_number(obj, 4.0);
            steps = cJSON_jc_append_steps() - before;
            JC_CHECK(steps < (size_t)5);   /* 2 appends; the tail is cached */
            /* The object must hold a, b, c(=99), and the new member, in order. */
            it = cJSON_GetObjectItem(obj, "c");
            if (JC_REQUIRE(it != NULL)) {
                JC_CHECK(it->valuedouble == 99.0);
            }
            it = cJSON_GetObjectItem(obj, "a");
            if (JC_REQUIRE(it != NULL)) {
                JC_CHECK(it->valuedouble == 1.0);
            }
            /* Walk the object's chain by hand: 4 members, last is value 4. */
            {
                int n = 0;
                cJSON *e = obj->child;
                double last = 0.0;
                while (e != NULL) {
                    n++;
                    last = e->valuedouble;
                    e = e->next;
                }
                JC_CHECK(n == 4);
                JC_CHECK(last == 4.0);
            }
            cJSON_Delete(obj);
        }
    }

    /* Leg 3: the common external detach -- removing a MID element (the last
     * member is untouched) -- must leave the tail valid, so a following append
     * is still O(1) and the chain still holds the right order. */
    arr = cJSON_CreateArray();
    if (JC_REQUIRE(arr != NULL)) {
        for (i = 0; i < 5; i++) {
            append_number(arr, (double)i);   /* [0,1,2,3,4]; tail = 4 */
        }
        /* Detach the middle element (value 2), the jc_config_trust way: the
         * tail (value 4) is untouched, so arr->tail stays correct. */
        it = cJSON_GetArrayItem(arr, 2);
        if (JC_REQUIRE(it != NULL)) {
            detach_mid(arr, it);
            cJSON_Delete(it);
        }
        before = cJSON_jc_append_steps();
        append_number(arr, 5.0);   /* must chain after 4, not re-walk */
        append_number(arr, 6.0);
        steps = cJSON_jc_append_steps() - before;
        JC_CHECK(steps < (size_t)5);
        /* Chain must be [0,1,3,4,5,6] in order: 6 elements, last value 6, and
         * the detached value 2 is gone. */
        size = cJSON_GetArraySize(arr);
        JC_CHECK(size == 6);
        {
            int have2 = 0;
            cJSON *e = arr->child;
            double expect[6] = { 0.0, 1.0, 3.0, 4.0, 5.0, 6.0 };
            int k;
            for (k = 0; e != NULL && k < 6; e = e->next, k++) {
                if (e->valuedouble == 2.0) {
                    have2 = 1;
                }
                JC_CHECK(e->valuedouble == expect[k]);
            }
            JC_CHECK(have2 == 0);
        }
        cJSON_Delete(arr);
    }
}
