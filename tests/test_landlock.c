/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_landlock.c - the fence's two M852 additions (N36) and M1018 E-02.
 *
 * M852 additions:
 *   jc_landlock_permission_error: the text a refused write leaves in a tool's output,
 *   in the spellings measured (cc, mktemp, sh) or known (Zig, EPERM), case-
 *   insensitively -- and NOT a word like "permissions" on its own.
 *   jc_landlock_write_fence_ex's failed_dir: the kernel refuses a rule for a regular
 *   file listed as a directory, and the index names it. Run in a FORKED child: the
 *   failure comes before landlock_restrict_self, but a kernel that accepted the rule
 *   would restrict the calling process irrevocably, and that must never be the test
 *   runner. (A MISSING directory is skipped, not failed.)
 *
 * M1018 E-02 (byte-packing unit test):
 *   The kernel expects __u64 handled_access_fs in little-endian byte order. On 32-bit
 *   Linux, memcpy from unsigned long (4 bytes) leaves bytes [4..8) uninitialized.
 *   landlock_pack_u64 ensures bytes [4..7] are zero-extended from the 32-bit value. */

#include "jc_test.h"
#include "jc_landlock.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#if defined(__linux__)
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static void test_permission_error(void)
{
    JC_CHECK(jc_landlock_permission_error(
        "cc1: Cannot create temporary file in /tmp/: Permission denied") == 1);
    JC_CHECK(jc_landlock_permission_error(
        "mktemp: failed to create file via template '/tmp/tmp.X': "
        "Permission denied") == 1);
    JC_CHECK(jc_landlock_permission_error(
        "sh: 1: cannot create /work/x.txt: Permission Denied") == 1);
    JC_CHECK(jc_landlock_permission_error("error: AccessDenied") == 1);
    JC_CHECK(jc_landlock_permission_error(
        "mv: cannot move 'a' to 'b': Operation not permitted") == 1);
    /* not a refusal */
    JC_CHECK(jc_landlock_permission_error("Tests passed.") == 0);
    JC_CHECK(jc_landlock_permission_error("permissions.allow: write_file") == 0);
    JC_CHECK(jc_landlock_permission_error("access granted") == 0);
    JC_CHECK(jc_landlock_permission_error("") == 0);
    JC_CHECK(jc_landlock_permission_error(NULL) == 0);
}

static void test_failed_dir(void)
{
#if defined(__linux__)
    const char *path = jc_test_tmp("jichi_landlock_not_a_dir");
    FILE *f = fopen(path, "wb");
    pid_t pid;
    int status = 0;

    JC_CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    fputs("x\n", f);
    fclose(f);
    pid = fork();
    JC_CHECK(pid >= 0);
    if (pid == 0) {
        const char *dirs[1];
        int failed = -2;
        jc_status st;
        dirs[0] = path;
        st = jc_landlock_write_fence_ex(dirs, 1, NULL, 0, &failed);
        if (jc_landlock_abi() >= 1) {
            _exit((st == JC_ERR_IO && failed == 0) ? 0 : 1);
        }
        _exit((st == JC_ERR_DENIED && failed == -1) ? 0 : 2);
    }
    if (pid > 0) {
        (void)waitpid(pid, &status, 0);
        JC_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    remove(path);
#endif
}

/* Byte-packing helper unit test: verify landlock_pack_u64 packs the full 8 bytes
 * in little-endian order, with high 4 bytes zero-extended on 32-bit platforms. */
static void test_pack_u64(void)
{
    unsigned char buf[8];
    unsigned long v;

    /* Test 1: 32-bit value (high bits zero) */
    v = 0x12345678UL;
    memset(buf, 0xAA, 8);  /* fill with guard bytes */
    jc_landlock_pack_u64(buf, v);
    JC_CHECK(buf[0] == 0x78);
    JC_CHECK(buf[1] == 0x56);
    JC_CHECK(buf[2] == 0x34);
    JC_CHECK(buf[3] == 0x12);
    JC_CHECK(buf[4] == 0x00);  /* must be zero on 32-bit */
    JC_CHECK(buf[5] == 0x00);
    JC_CHECK(buf[6] == 0x00);
    JC_CHECK(buf[7] == 0x00);

    /* Test 2: width-aware value (chosen at compile time, so the 64-bit literal
       is never parsed on 32-bit). Low 4 bytes must be 0xEF 0xCD 0xAB 0x89 on
       both widths. On 64-bit, bytes [4..7] are 0x67 0x45 0x23 0x01; on 32-bit
       they remain zero. */
#if ULONG_MAX > 0xFFFFFFFFUL
    v = 0x0123456789ABCDEFUL;
#else
    v = 0x89ABCDEFUL;
#endif
    jc_landlock_pack_u64(buf, v);
    JC_CHECK(buf[0] == 0xEF);
    JC_CHECK(buf[1] == 0xCD);
    JC_CHECK(buf[2] == 0xAB);
    JC_CHECK(buf[3] == 0x89);
    if (sizeof(unsigned long) == 8) {
        JC_CHECK(buf[4] == 0x67);
        JC_CHECK(buf[5] == 0x45);
        JC_CHECK(buf[6] == 0x23);
        JC_CHECK(buf[7] == 0x01);
    } else {
        JC_CHECK(buf[4] == 0x00);
        JC_CHECK(buf[5] == 0x00);
        JC_CHECK(buf[6] == 0x00);
        JC_CHECK(buf[7] == 0x00);
    }

    /* Test 3: zero value (the bug case: high 4 bytes must still be zero) */
    v = 0UL;
    memset(buf, 0xAA, 8);
    jc_landlock_pack_u64(buf, v);
    JC_CHECK(buf[0] == 0x00);
    JC_CHECK(buf[1] == 0x00);
    JC_CHECK(buf[2] == 0x00);
    JC_CHECK(buf[3] == 0x00);
    JC_CHECK(buf[4] == 0x00);
    JC_CHECK(buf[5] == 0x00);
    JC_CHECK(buf[6] == 0x00);
    JC_CHECK(buf[7] == 0x00);
}

void test_landlock(void)
{
    test_permission_error();
    test_failed_dir();
    test_pack_u64();
}
