/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_harness_setflag.c - the dangling SET-flag guard (M1147, SV:2040).
 *
 * The unit guard for the SV:1939 fix: M1056's static `credential_args`
 * (src/harness/jc_harness_push.c) used to build the SET credential flag on its
 * OWN stack (`char setflag[PATH_CAP + 32]`) and return `argv_out[1] = setflag`
 * -- a pointer into a frame that dies on return. Every real caller then runs
 * jc_snprintf / credential_env / git children before the forked git reads the
 * bytes, and the child's read hits whatever the caller's later stack use left
 * in that slot. The fix (885aaa64) owns the string in the arena instead:
 * `setflag = jc_arena_calloc(a, PATH_CAP + 32)`.
 *
 * This test is the /tmp standalone red driver (red_setflag.c) ported to the
 * suite:
 *   1. it gets the three -c VALUES through the PUBLIC seam -- the exported
 *      `jc_hp_cred_flags`, the thin one-line wrapper over the fixed static
 *      `credential_args` -- with a FIXED harness path ('/test/jichi-harness',
 *      not argv[0]) so the expectation is deterministic;
 *   2. it calls clobber() (quoted below, verbatim from the driver), which
 *      writes a patterned char[PATH_CAP + 64] onto the stack at the frame
 *      depth the helper used -- at -O0 the dead pre-fix setflag slot sits
 *      under this test's frame, and the pattern lands over it;
 *   3. it compares EACH of the three flags byte-for-byte with its expected
 *      string. Pre-fix, flags[1] reads back the clobber pattern (RED);
 *      post-fix (arena-owning), the buffer survives the clobber (GREEN).
 *
 * The driver's #include-the-TU trick (RED_PUSH_C) was NOT needed: the defect
 * is reachable through the exported wrapper, because the wrapper adds no
 * buffer of its own -- it passes flags[1] straight out of credential_args.
 *
 * The harness-mirror cell 15 (tests/smoke/harness_mirror_push.sh) stayed green
 * on the PRE-fix tree too: on the fatal path the stack slot happened not to be
 * reused, so the bytes survived by luck. That is why this unit check exists --
 * nothing else in the tree guards the fix. The ASan route was attempted and
 * did not fire (the parent never reads the dead buffer; the forked git child
 * reads it in its own address space): the named gap, recorded in
 * docs/internal/drive/2026-11-05-harness-port/reports/2026-11-06-M1147-setflag.md.
 */

#include "jc_test.h"
#include "jc_mem.h"
#include "jc_snprintf.h"
#include "jc_harness_push.h"

#include <string.h>

/* The stack cap the harness TUs use (src/harness/jc_harness_push.c:66); the
 * clobber is sized against it, exactly as the driver's was. */
#define PATH_CAP 4352

/* Quoted verbatim from /tmp/red_setflag.c, the standalone red driver for
 * SV:1939: "Reuse the stack region credential_args' setflag occupied. At -O0
 * this local sits at the same frame depth (both are called from main), so the
 * patterned write lands over the dead setflag slot." (Here the call is one
 * frame deeper -- through the thin jc_hp_cred_flags wrapper -- but both
 * blocks sit under this test's frame at ~4.4 KB, so the overlap stands.) */
static void
clobber(void)
{
    char big[PATH_CAP + 64];
    int i;
    for (i = 0; i < (int)sizeof big; i++) {
        big[i] = (char)('!' + (i % 94));   /* printable noise, never matches */
    }
}

/* Local forward declaration (-Wmissing-prototypes; the M1124 idiom -- the
 * runner's declaration in test_main.c is in a different translation unit). */
void test_harness_setflag(void);

void
test_harness_setflag(void)
{
    struct jc_arena *a = jc_arena_new(0);
    char *flags[4];
    const char *harness = "/test/jichi-harness";
    char expect_set[PATH_CAP + 32];
    int n;

    JC_REQUIRE(a != NULL);
    n = jc_hp_cred_flags(a, harness, flags);
    if (JC_REQUIRE(n == 3)) {
        jc_snprintf(expect_set, sizeof expect_set,
                    "credential.helper=%s credential-helper", harness);

        /* Pre-fix: flags[1] points into credential_args' dead frame.
         * Clobber it. */
        clobber();

        /* Byte-for-byte, EACH flag (the three -c VALUES are byte-identical
         * to the pre-fix local form -- the fix changed lifetime, not bytes). */
        JC_CHECK(flags[0] != NULL);
        JC_CHECK_STR(flags[0], "credential.helper=");
        JC_CHECK(flags[1] != NULL);
        JC_CHECK_STR(flags[1], expect_set);
        JC_CHECK_STR(flags[2], "credential.useHttpPath=true");
        JC_CHECK(flags[3] == NULL);
    }

    jc_arena_free(a);
}
