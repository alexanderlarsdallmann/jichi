/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_reread.c - the pure re-read hash (M231). */

#include "jc_reread.h"
#include "jc_test.h"

void test_reread(void)
{
    /* Deterministic: same bytes, same hash. */
    JC_CHECK(jc_reread_hash("hello", 5) == jc_reread_hash("hello", 5));

    /* A one-byte content change changes the hash (the edit case that must make
     * the re-read check return 0). */
    JC_CHECK(jc_reread_hash("hello", 5) != jc_reread_hash("hellp", 5));

    /* A length change changes the hash. */
    JC_CHECK(jc_reread_hash("hello", 5) != jc_reread_hash("hell", 4));

    /* NUL-safe: bytes after an embedded NUL still count (a C-string hash would
     * miss this). */
    JC_CHECK(jc_reread_hash("a\0b", 3) != jc_reread_hash("a\0c", 3));

    /* NULL data hashes as an empty buffer, not a crash. */
    JC_CHECK(jc_reread_hash(NULL, 0) == jc_reread_hash("", 0));
}
