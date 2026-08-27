/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_arena.c - the arena allocator's footprint gauge (M140) and the
 * reset contract it makes observable: reset frees all but the oldest block
 * back to the system and rewinds that one. */

#include "jc_test.h"
#include "jc_mem.h"

#include <string.h>

void test_arena(void)
{
    struct jc_arena *a = jc_arena_new(0);
    jc_size cap = 0;
    jc_size used;
    int i;

    JC_CHECK(a != NULL);

    /* Fresh arena: one empty default block. */
    used = jc_arena_used(a, &cap);
    JC_CHECK(used == 0);
    JC_CHECK(cap >= 8192);

    /* Allocations are counted (aligned, so >= the request). */
    JC_CHECK(jc_arena_alloc(a, 100) != NULL);
    used = jc_arena_used(a, NULL); /* cap_out is optional */
    JC_CHECK(used >= 100);

    /* Grow past several blocks; used and cap both climb. */
    for (i = 0; i < 8; i++) {
        JC_CHECK(jc_arena_alloc(a, 4000) != NULL);
    }
    used = jc_arena_used(a, &cap);
    JC_CHECK(used >= 100 + 8 * 4000);
    JC_CHECK(cap >= used);

    /* An oversized request gets its own exactly-sized block. */
    JC_CHECK(jc_arena_alloc(a, 100000) != NULL);
    used = jc_arena_used(a, &cap);
    JC_CHECK(used >= 100000);

    /* Reset: everything is handed back except one retained block --
     * used drops to 0 and cap falls back to a single block's worth. */
    jc_arena_reset(a);
    used = jc_arena_used(a, &cap);
    JC_CHECK(used == 0);
    JC_CHECK(cap <= 8192);

    /* The retained block is reusable. */
    JC_CHECK(jc_arena_strdup(a, "after reset") != NULL);
    JC_CHECK(jc_arena_used(a, NULL) >= 12);

    /* NULL arena: 0, and cap_out is zeroed, no crash. */
    cap = 77;
    JC_CHECK(jc_arena_used(NULL, &cap) == 0);
    JC_CHECK(cap == 0);

    jc_arena_free(a);
}
