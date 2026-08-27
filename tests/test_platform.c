/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_platform.c - file-type predicates and read-path edge cases (M198).
 *
 * These cover the boundary between "a path the user NAMED" and "a path jichi
 * DISCOVERED by scanning a directory it owns". A non-regular file is garbage in
 * the second case (and used to hang the process), but may be legitimate in the
 * first -- `--config <(jq ...)` resolves to a pipe -- so the S_ISREG check lives
 * at the scanning callers, not inside jc_read_file. See
 * docs/proposals/2026-07-robustness-edge-cases.md (#1).
 */

#include "jc_test.h"
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_snprintf.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

void test_platform(void)
{
    struct jc_arena *a = jc_arena_new(0);
    char dir[256];
    char reg[320];
    char sub[320];
    char fifo[320];
    char dangling[320];
    char *text;
    jc_size len;

    /* M461: whatever jc_shell_path() resolves to must be executable HERE.
     * On every FHS platform that is /bin/sh and this is trivially true; on
     * Android, where /bin does not exist, it is the entire point -- sixteen
     * hardcoded "/bin/sh" literals failed 53 checks across 12 files on an
     * Android 4.4.2 tablet before the resolver existed. The check is written
     * as "the shell we will spawn can be spawned", which is the property that
     * actually matters and is the one that varies by platform. */
    JC_CHECK(jc_shell_path() != NULL);
    JC_CHECK(access(jc_shell_path(), X_OK) == 0);


    jc_snprintf(dir, sizeof dir, "%s/jichi_plat_test_%ld", jc_test_tmpdir(), (long)getpid());
    {
        char cmd[512];
        jc_snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
        system(cmd);
    }
    JC_CHECK(jc_mkdir_p(dir) == JC_OK);

    jc_snprintf(reg, sizeof reg, "%s/plain.txt", dir);
    jc_snprintf(sub, sizeof sub, "%s/subdir", dir);
    jc_snprintf(fifo, sizeof fifo, "%s/pipe.json", dir);
    jc_snprintf(dangling, sizeof dangling, "%s/dangling", dir);

    JC_CHECK(jc_write_file(reg, "hello\n", 6) == JC_OK);
    JC_CHECK(jc_mkdir_p(sub) == JC_OK);

    /* --- jc_is_regular_file ------------------------------------------------ */
    JC_CHECK(jc_is_regular_file(reg) == 1);
    JC_CHECK(jc_is_regular_file(sub) == 0);          /* a directory is not */
    JC_CHECK(jc_is_regular_file(dangling) == 0);     /* missing is not */
    JC_CHECK(jc_is_regular_file("/nonexistent/x") == 0);
    /* NULL-ish and empty must not crash. */
    JC_CHECK(jc_is_regular_file("") == 0);

    /* A FIFO is the case that used to hang a scan: it exists, it is readable in
     * principle, and it is NOT a regular file -- which is the only cheap way to
     * know not to open it. */
    if (mkfifo(fifo, 0600) == 0) {
        JC_CHECK(jc_file_exists(fifo) == 1);   /* exists... */
        JC_CHECK(jc_is_regular_file(fifo) == 0); /* ...but must be skipped */
        /* Deliberately NOT calling jc_read_file(fifo): with no writer it would
         * block forever, which is precisely the defect. The predicate is the
         * contract; tests/smoke/degenerate_store.sh proves the scan honours it. */
        remove(fifo);
    }

    /* jc_is_dir stays the complement for directories. */
    JC_CHECK(jc_is_dir(sub) == 1);
    JC_CHECK(jc_is_dir(reg) == 0);

    /* --- jc_read_file: a directory is an ERROR, not an empty file ---------- */
    /* fopen("rb") on a directory SUCCEEDS on Linux; ftell reports a size and
     * fread fails, so this used to return JC_OK with "" -- a silent wrong
     * answer. Rejecting a directory cannot affect a pipe, so unlike S_ISREG it
     * belongs at this chokepoint. */
    text = NULL;
    len = 0;
    JC_CHECK(jc_read_file(sub, &text, &len, a) == JC_ERR_IO);

    /* A regular file still reads normally. */
    text = NULL;
    JC_CHECK(jc_read_file(reg, &text, &len, a) == JC_OK);
    JC_CHECK(text != NULL && strcmp(text, "hello\n") == 0);
    JC_CHECK(len == 6);

    /* A missing path is NOTFOUND, distinct from the directory's IO. */
    JC_CHECK(jc_read_file("/nonexistent/x", &text, &len, a) == JC_ERR_NOTFOUND);

    {
        char cmd[512];
        jc_snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
        system(cmd);
    }
    jc_arena_free(a);
}

/* M528: privacy verified as an EFFECT. The daemon socket's access control is its
 * file mode and nothing else, and the code that sets it says so -- while also
 * naming the case it cannot verify: "there are platforms that ignore the umask
 * for sockets". On such a platform the kernel creates a 0755 socket, every local
 * user gets a shell, and no code path notices. These are the branches of the
 * check that notices, pure so that a platform which misbehaves does not have to
 * be present to test them. */
void test_priv_verdict(void)
{
    /* 0600, ours: the only acceptable answer. */
    JC_CHECK(jc_priv_verdict_of(1000UL, 0100600UL, 1000UL) == JC_PRIV_OK);
    /* A directory at 0700 is equally fine -- the check is on the low 9 bits. */
    JC_CHECK(jc_priv_verdict_of(1000UL, 0040700UL, 1000UL) == JC_PRIV_OK);
    /* Owner-only but with execute set is still owner-only. */
    JC_CHECK(jc_priv_verdict_of(1000UL, 0100700UL, 1000UL) == JC_PRIV_OK);

    /* Any group or other bit is too open -- one bit is enough. */
    JC_CHECK(jc_priv_verdict_of(1000UL, 0100640UL, 1000UL) == JC_PRIV_TOO_OPEN);
    JC_CHECK(jc_priv_verdict_of(1000UL, 0100604UL, 1000UL) == JC_PRIV_TOO_OPEN);
    JC_CHECK(jc_priv_verdict_of(1000UL, 0100601UL, 1000UL) == JC_PRIV_TOO_OPEN);
    /* The umask-ignored-on-bind case this exists for: 0755. */
    JC_CHECK(jc_priv_verdict_of(1000UL, 0140755UL, 1000UL) == JC_PRIV_TOO_OPEN);
    /* And the worst one, which a chmod's return value would not have caught. */
    JC_CHECK(jc_priv_verdict_of(1000UL, 0140777UL, 1000UL) == JC_PRIV_TOO_OPEN);

    /* Not ours outranks the mode: a 0600 file owned by someone else is THEIR
     * private file, and connecting to it is not a privacy question but an
     * identity one. Reported separately so the diagnostic can say which. */
    JC_CHECK(jc_priv_verdict_of(0UL, 0100600UL, 1000UL) == JC_PRIV_NOT_OWNER);
    JC_CHECK(jc_priv_verdict_of(1001UL, 0100777UL, 1000UL) == JC_PRIV_NOT_OWNER);
    /* Running as root, root's own 0600 socket is fine. */
    JC_CHECK(jc_priv_verdict_of(0UL, 0100600UL, 0UL) == JC_PRIV_OK);

    /* Every verdict has a human string, including the failure ones. */
    JC_CHECK(jc_priv_verdict_str(JC_PRIV_OK) != NULL);
    JC_CHECK(jc_priv_verdict_str(JC_PRIV_TOO_OPEN) != NULL);
    JC_CHECK(jc_priv_verdict_str(JC_PRIV_NOT_OWNER) != NULL);
    JC_CHECK(jc_priv_verdict_str(JC_PRIV_NO_STAT) != NULL);
    JC_CHECK(strstr(jc_priv_verdict_str(JC_PRIV_TOO_OPEN), "0600") != NULL);
}

/* And the directory that holds the endpoint: the mode question there is not
 * "can others read it" but "can others REPLACE what we put in it", which the
 * sticky bit answers for /tmp. */
void test_dir_holds_private(void)
{
    JC_CHECK(jc_dir_holds_private(0040700UL) == 1);   /* private dir      */
    JC_CHECK(jc_dir_holds_private(0040755UL) == 1);   /* readable, not writable */
    JC_CHECK(jc_dir_holds_private(0041777UL) == 1);   /* /tmp: sticky     */
    JC_CHECK(jc_dir_holds_private(0041733UL) == 1);   /* sticky, still ok */
    JC_CHECK(jc_dir_holds_private(0040777UL) == 0);   /* world-writable, NOT sticky */
    JC_CHECK(jc_dir_holds_private(0040770UL) == 0);   /* group-writable, not sticky */
    JC_CHECK(jc_dir_holds_private(0040707UL) == 0);   /* other-writable */
}
