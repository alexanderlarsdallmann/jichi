/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_harness_gitrun.c - jc_hp_git_run's TOTAL-argv bound (M1151c, SV:1339).
 *
 * The per-input guard (n_args > 48 || n_cred > 4 || n_env > 4 || max_out < 1)
 * admits its own limits; at exactly those values -- workdir set, n_cred 4,
 * n_args 48 -- the argv build writes
 *     11 (git + five -c pairs) + 2 (-C workdir) + 8 (four -c credential
 *     pairs) + 48 (args) + 1 (the terminating NULL) = 70 entries
 * into `char *argv[64]`: six past the end. After M1151(a) made the args
 * parameter incomplete, that guard was the ONLY bound on the total, and it
 * was a bound on the PARTS. The fix bounds the sum and refuses it, before
 * any argv write. This test drives the guard's own limits (RED pre-fix: an
 * ASan stack-buffer-overflow in the argv build, and rc != -1) and a
 * representative caller shape (args[16], total 38: still ALLOWED, reaches
 * the spawn).
 */

#include "jc_test.h"
#include "jc_snprintf.h"
#include "jc_harness_push.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Local forward declaration (-Wmissing-prototypes; the M1147 idiom -- the
 * runner's declaration in test_main.c is in a different translation unit). */
void test_harness_gitrun(void);

void
test_harness_gitrun(void)
{
    char cred[4][96];
    char *cred_flags[4];
    const char *args[48];
    char out[64];
    char err_line[512];
    const char *wd;
    int i;
    int rc;

    for (i = 0; i < 4; i++) {
        jc_snprintf(cred[i], sizeof cred[i],
                    "credential.helper=/tmp/jichi-harness-cred%d", i);
        cred_flags[i] = cred[i];
    }
    for (i = 0; i < 48; i++) {
        args[i] = "a";
    }

    /* A path, not a repo: the refusal (call 1) happens before the spawn, so
     * no git child is needed for it; call 2 spawns real git in a non-repo,
     * which exits nonzero (never -1). */
    wd = jc_test_tmp("jichi_harness_gitrun");
    (void)(mkdir(wd, 0700) != 0);

    /* Call 1 -- the guard's OWN limits: workdir set, n_cred 4, n_args 48.
     * Pre-fix the build writes 70 entries into argv[64] (ASan red). Post-fix
     * the total bound (70 > 64) refuses it, and out/err_line are NULed. */
    memset(out, 0xAA, sizeof out);
    memset(err_line, 0xAA, sizeof err_line);
    rc = jc_hp_git_run(NULL, wd, cred_flags, 4, NULL, 0,
                       args, 48, 64, out, err_line);
    JC_CHECK(rc == -1);
    JC_CHECK(out[0] == '\0');
    JC_CHECK(err_line[0] == '\0');

    /* Call 2 -- the no-change half: a representative caller shape (workdir
     * set, n_cred 4, args[16] -- total 38, above the real maximum of 25 and
     * below the bound) stays ALLOWED and reaches the spawn -- git in a
     * non-repo exits nonzero, never -1. This pins the bound, not a caller
     * (the real callers: notes/M1151.md defect (c)). */
    rc = jc_hp_git_run(NULL, wd, cred_flags, 4, NULL, 0,
                       args, 16, 64, out, err_line);
    JC_CHECK(rc != -1);

    (void)jc_test_rm_rf(wd);
}
