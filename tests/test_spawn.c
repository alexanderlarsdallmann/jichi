/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_spawn.c - jc_spawn's parent-built child environment (M832).
 *
 * The posix_spawn backend cannot run the child-side scrub, so it builds the
 * child's envp in the parent (jc_spawn_build_envp). That builder is a FENCE
 * component -- what it keeps out is the provider keys -- so its three duties are
 * checked directly, not only through a live child: the registered and built-in
 * secret names are dropped, a configured "KEY=VALUE" replaces the inherited
 * entry of the same key (never duplicates it), and everything else passes
 * through, NULL-terminated. Registry hygiene per M724: what this test adds it
 * truncates away, and the process env it touches is restored.
 *
 * On a build without JC_HAVE_POSIX_SPAWN the builder returns NULL by contract;
 * the backend selector must then answer fork whatever the env var says. */

#include "jc_test.h"
#include "jc_spawn.h"
#include "jc_proc.h"
#include "jc_vec.h"
#include <stdlib.h>
#include <string.h>

#ifdef JC_FAULT
#include "jc_fault.h"      /* jc_fault_reset + the spawn-restore vanish site (M1116) */
#include "jc_platform.h"   /* jc_mkdir_p, JC_OK, jc_status */
#include "jc_snprintf.h"   /* jc_snprintf for the fixture paths */
#include <stdio.h>
#include <sys/stat.h>   /* struct stat, st_ino, st_dev, mkdir, mode_t */
#include <sys/types.h>
#include <sys/wait.h>   /* waitpid */
#include <unistd.h>     /* getcwd, chdir, getpid */
/* Test seam in jc_spawn.c (JC_FAULT-only, no public header): re-read the
 * backend after an earlier test pinned it to fork. */
extern void jc_spawn_backend_reset(void);
#endif

#if defined(JC_HAVE_POSIX_SPAWN)
static int envp_has(char **envp, const char *needle_prefix)
{
    int i;
    for (i = 0; envp[i] != NULL; i++) {
        if (strncmp(envp[i], needle_prefix, strlen(needle_prefix)) == 0) {
            return 1;
        }
    }
    return 0;
}

static int envp_count(char **envp, const char *needle_prefix)
{
    int i;
    int n = 0;
    for (i = 0; envp[i] != NULL; i++) {
        if (strncmp(envp[i], needle_prefix, strlen(needle_prefix)) == 0) {
            n++;
        }
    }
    return n;
}
#endif

#ifdef JC_FAULT
/* M1116 (B-09): the posix_spawn backend's cwd RESTORE must survive the original
 * directory VANISHING mid-call. It does so by holding an open descriptor on the
 * original directory and fchdir()'ing back to it; an open fd keeps the inode
 * alive even when the PATH is renamed away, so the restore cannot leave jichi
 * running in a silently-moved cwd.
 *
 * The fault site JC_FAULT_SPAWN_RESTORE_VANISH (armed by
 * JICHI_FAULT_SPAWN_RESTORE_VANISH_AFTER=0) renames the original directory's
 * PATH out of the way at the restore point -- modelling exactly "vanished
 * mid-call". On the OLD path-based restore (chdir by the saved string) that
 * rename makes the change-back fail and the cwd stays in the child's subdir;
 * on the NEW fchdir-restore it succeeds and the cwd is the original directory.
 * The assertion compares the current directory's (st_dev, st_ino) to the
 * original's, which is RED on the old restore and GREEN on the new one.
 *
 * Runs only under `make FAULT=1` (the smoke-faults tier) -- in a release build
 * this whole block is compiled out and the site is inert. */
static void test_spawn_restore_vanish(void)
{
#if defined(JC_HAVE_POSIX_SPAWN)
    char start_cwd[4096];
    char orig[4096];
    char orig_vanished[4120];
    char sub[4120];
    struct stat orig_st;
    struct stat now_st;
#ifdef JC_HAVE_FCHDIR
    struct stat gone_st;   /* B-09 teeth: prove the fault renamed orig away */
#endif
    pid_t pid;
    int wstatus;
    int chdir_ok;
    int spawn_ok;

    if (getcwd(start_cwd, sizeof start_cwd) == NULL) {
        return; /* cannot proceed; leave the suite where it was */
    }

    /* A fresh original directory, uniquely named by pid (the fixture dir is
     * shared and other workers may run the same suite). */
    jc_snprintf(orig, sizeof orig, "%s/m1116_spawn_orig_%d",
                jc_test_tmpdir(), (int)getpid());
    jc_snprintf(orig_vanished, sizeof orig_vanished, "%s.vanished", orig);
    jc_snprintf(sub, sizeof sub, "%s/sub", orig);
    (void)jc_test_rm_rf(orig);
    (void)jc_test_rm_rf(orig_vanished);
    if (mkdir(orig, 0700) != 0) {
        return;
    }
    if (chdir(orig) != 0) {
        jc_test_rm_rf(orig);
        (void)chdir(start_cwd);
        return;
    }
    /* Record the original directory's identity by INODE (not path -- the path
     * is exactly what the fault vanishes). */
    if (stat(".", &orig_st) != 0) {
        jc_test_rm_rf(orig);
        (void)chdir(start_cwd);
        return;
    }

    /* The child's working directory: a real subdir of the original. Its inode
     * differs from the original's, so a silently-moved cwd is detectable.
     * We STAY in orig/ -- spawn_posix will chdir into sub itself. */
    if (mkdir("sub", 0700) != 0) {
        jc_test_rm_rf(orig);
        (void)chdir(start_cwd);
        return;
    }
    JC_CHECK(stat(sub, &now_st) == 0); /* sub really is a different inode */
    JC_CHECK(now_st.st_ino != orig_st.st_ino);

    /* Arm the vanish fault on the FIRST restore and force the spawn backend.
     * The backend is cached per process and an earlier test pinned it to fork,
     * so the JC_FAULT seam re-reads the environment. */
    setenv("JICHI_FAULT_SPAWN_RESTORE_VANISH_AFTER", "0", 1);
    jc_fault_reset();
    setenv("JICHI_SPAWN_BACKEND", "spawn", 1);
    jc_spawn_backend_reset();
    JC_CHECK(jc_spawn_backend() == 1); /* the flip took */

    {
        struct jc_spawn_req req;
        memset(&req, 0, sizeof req);
        req.shell_cmd = "exit 0";
        req.fd_stdin  = JC_SPAWN_DEVNULL;
        req.fd_stdout = JC_SPAWN_DEVNULL;
        req.fd_stderr = JC_SPAWN_DEVNULL;
        req.cwd       = sub;
        /* F2 (M1116-audit 2026-11-02-M1116-audit.md, hunk 11 / finding F2):
         * ASSERT the spawn succeeded. The old code silently swallowed a
         * failed spawn (e.g. the early chdir-into-`sub` failure path in
         * spawn_posix) -- then the cwd never moved, the fault site never
         * fired, and the inode assertion below went green without the
         * vanish scenario ever running: a vacuous pass. */
        spawn_ok = (jc_spawn(&req, &pid) == JC_OK);
        JC_CHECK(spawn_ok);
        if (spawn_ok) {
            (void)waitpid(pid, &wstatus, 0); /* reap; a trivial `exit 0` child */
        }
    }

    /* The process's cwd must be the ORIGINAL directory's inode, whatever the
     * PATH now says. The fault renames orig -> orig_vanished AT the restore
     * point, so both cases below leave us under the renamed inode; what
     * distinguishes them is whether we landed in `sub` (a different inode)
     * or back in `orig` (the same inode):
     *   OLD restore (RED): chdir(cwd_save) fails on the renamed path, cwd
     *                       stays in sub -> st_ino MISMATCH.
     *   NEW restore (GRN): fchdir(dirfd) returns to orig's inode -> MATCH.
     *
     * F5 (M1116-audit, hunk 11 / finding F5): a build with posix_spawn but
     * NO fchdir (JC_HAVE_FCHDIR undefined) keeps the old path-based restore,
     * which fails on the fault-renamed path -- the documented B-09
     * degradation. There the honest assertion is the NEGATIVE one (cwd is
     * NOT the original's inode), so the FAULT suite stays green on such a
     * host instead of being permanently red; the positive assertion is the
     * teeth, and it is live only where fchdir is. */
    /* B-09 teeth (M1116-c, finding SV:1639): on a host WITH fchdir the positive
     * inode assertion below also passes when the fault NEVER fired --
     * fchdir(dirfd) trivially succeeds and the inode is of course unchanged,
     * so the test would prove nothing. Assert that the vanish actually took
     * place first: the fault renames orig -> <orig>.vanished at the restore
     * point, so afterwards orig must no longer stat and orig_vanished must.
     * Live only here (the fchdir branch); the no-fchdir branch keeps
     * asserting the documented degradation and is untouched. */
#ifdef JC_HAVE_FCHDIR
    JC_CHECK(stat(orig, &gone_st) != 0);       /* the rename moved it away */
    JC_CHECK(stat(orig_vanished, &gone_st) == 0); /* the fault's target exists */
#endif
    if (stat(".", &now_st) == 0) {
#ifdef JC_HAVE_FCHDIR
        JC_CHECK(now_st.st_ino == orig_st.st_ino &&
                 now_st.st_dev == orig_st.st_dev);
#else
        JC_CHECK(now_st.st_ino != orig_st.st_ino);
#endif
    }

    /* Hygiene: the fault renames orig to orig_vanished AT the restore point, so
     * after the assertion we are either in <orig>.vanished (GREEN: fchdir
     * returned to orig's inode under its new name) or in <orig>.vanished/sub
     * (RED: the restore failed and left us in the child's subdir). Both paths
     * are below the fixture dir, so: chdir home FIRST (no path is busy any
     * more), then remove the (possibly renamed) tree. orig is the pre-rename
     * name and orig_vanished the post-rename name -- removing both covers
     * whichever the fault left behind. */
    unsetenv("JICHI_SPAWN_BACKEND");
    jc_spawn_backend_reset();
    unsetenv("JICHI_FAULT_SPAWN_RESTORE_VANISH_AFTER");
    jc_fault_reset();
    chdir_ok = (chdir(start_cwd) == 0);
    JC_CHECK(chdir_ok);
    (void)jc_test_rm_rf(orig);
    (void)jc_test_rm_rf(orig_vanished);
#else
    /* No posix_spawn in this build: the backend is always fork and the
     * restore-vanish scenario does not exist. Nothing to assert. */
#endif
}
#endif /* JC_FAULT */

void test_spawn(void)
{
#if defined(JC_HAVE_POSIX_SPAWN)
    int reg0 = jc_proc_secret_env_count();
    char **envp;
    struct jc_vec extra;

    /* the name predicate the builder leans on: built-ins, registered, others */
    JC_CHECK(jc_proc_env_name_is_secret("JICHI_API_KEY", 13) == 1);
    JC_CHECK(jc_proc_env_name_is_secret("JICHI_API_KEY_X", 15) == 0);
    JC_CHECK(jc_proc_env_name_is_secret("JICHI_API_KE", 12) == 0);
    JC_CHECK(jc_proc_env_name_is_secret("PATH", 4) == 0);
    JC_CHECK(jc_proc_env_name_is_secret(NULL, 4) == 0);
    JC_CHECK(jc_proc_secret_env_add("JC_TEST_SPAWN_SECRET") == 0);
    JC_CHECK(jc_proc_env_name_is_secret("JC_TEST_SPAWN_SECRET", 20) == 1);

    setenv("JC_TEST_SPAWN_SECRET", "s3", 1);
    setenv("JC_TEST_SPAWN_PLAIN", "keepme", 1);
    setenv("JC_TEST_SPAWN_OVR", "old", 1);

    /* scrub on, no extras: secret out, plain through */
    envp = jc_spawn_build_envp(1, NULL);
    JC_CHECK(envp != NULL);
    JC_CHECK(!envp_has(envp, "JC_TEST_SPAWN_SECRET="));
    JC_CHECK(envp_has(envp, "JC_TEST_SPAWN_PLAIN=keepme"));
    free(envp);

    /* scrub off: the secret entry passes (the fork backend's callers that never
     * scrubbed -- git, the LSP -- keep their historical posture) */
    envp = jc_spawn_build_envp(0, NULL);
    JC_CHECK(envp != NULL);
    JC_CHECK(envp_has(envp, "JC_TEST_SPAWN_SECRET=s3"));
    free(envp);

    /* a configured entry REPLACES its key and appends a new one; the replaced
     * key appears exactly once (setenv-overwrite semantics, parent-side) */
    jc_vec_init(&extra, sizeof(char *));
    {
        char *kv1 = (char *)"JC_TEST_SPAWN_OVR=new";
        char *kv2 = (char *)"JC_TEST_SPAWN_ADDED=hello";
        char *bad = (char *)"NO_EQUALS_SIGN";
        jc_vec_push(&extra, &kv1);
        jc_vec_push(&extra, &kv2);
        jc_vec_push(&extra, &bad);   /* malformed: must be skipped, not kept */
    }
    envp = jc_spawn_build_envp(1, &extra);
    JC_CHECK(envp != NULL);
    JC_CHECK(envp_count(envp, "JC_TEST_SPAWN_OVR=") == 1);
    JC_CHECK(envp_has(envp, "JC_TEST_SPAWN_OVR=new"));
    JC_CHECK(envp_has(envp, "JC_TEST_SPAWN_ADDED=hello"));
    JC_CHECK(!envp_has(envp, "NO_EQUALS_SIGN"));
    free(envp);
    jc_vec_free(&extra);

    /* a configured entry wins over the scrub: an explicitly-set key is re-added
     * (the M130 order, scrub before configured env) */
    jc_vec_init(&extra, sizeof(char *));
    {
        char *kv = (char *)"JC_TEST_SPAWN_SECRET=explicit";
        jc_vec_push(&extra, &kv);
    }
    envp = jc_spawn_build_envp(1, &extra);
    JC_CHECK(envp != NULL);
    JC_CHECK(envp_count(envp, "JC_TEST_SPAWN_SECRET=") == 1);
    JC_CHECK(envp_has(envp, "JC_TEST_SPAWN_SECRET=explicit"));
    free(envp);
    jc_vec_free(&extra);

    unsetenv("JC_TEST_SPAWN_SECRET");
    unsetenv("JC_TEST_SPAWN_PLAIN");
    unsetenv("JC_TEST_SPAWN_OVR");
    jc_proc_secret_env_truncate(reg0);
    JC_CHECK(jc_proc_secret_env_count() == reg0);
#else
    /* No posix_spawn in this build: the builder refuses and the selector answers
     * fork whatever the environment says -- the env var must be inert. */
    JC_CHECK(jc_spawn_build_envp(1, NULL) == NULL);
    JC_CHECK(jc_spawn_backend() == 0);
#endif

#ifdef JC_FAULT
    /* M1116 (B-09): the cwd-restore-vanish scenario, FAULT-only (runs at the
     * END of test_spawn so it does not interleave with the builder checks, and
     * it restores every environment variable and cwd it touches). */
    test_spawn_restore_vanish();
#endif
}
