/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_lease.c - the per-workspace run lease's pure cores (M431e). */

#include "jc_test.h"
#include "jc_lease.h"
#include "jc_snapshot.h"
#include "jc_snprintf.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

void test_lease(void)
{
    enum jc_lease_mode m;
    struct jc_lease_info in;
    struct jc_lease_info out;
    char path[512];
    char other[512];
    char *json;

    /* --- the mode word ------------------------------------------------------
     * An unknown word must be REFUSED, not silently defaulted: a posture flag
     * that ignores its own argument is the M429 lesson (two flags that meant
     * less than their names). */
    m = JC_LEASE_FAIL;
    JC_CHECK(jc_lease_mode_parse("warn", &m) == 1 && m == JC_LEASE_WARN);
    JC_CHECK(jc_lease_mode_parse("fail", &m) == 1 && m == JC_LEASE_FAIL);
    JC_CHECK(jc_lease_mode_parse("off", &m) == 1 && m == JC_LEASE_OFF);
    JC_CHECK(jc_lease_mode_parse("nonsense", &m) == 0);
    JC_CHECK(jc_lease_mode_parse(NULL, &m) == 0);
    JC_CHECK(jc_lease_mode_parse("warn", NULL) == 0);
    JC_CHECK(strcmp(jc_lease_mode_name(JC_LEASE_WARN), "warn") == 0);
    JC_CHECK(strcmp(jc_lease_mode_name(JC_LEASE_FAIL), "fail") == 0);
    JC_CHECK(strcmp(jc_lease_mode_name(JC_LEASE_OFF), "off") == 0);

    /* --- the whole policy, every combination -------------------------------
     * held x holder_alive x mode. The table is small enough to state in full,
     * which is the point: a fourth outcome cannot be invented at a call site. */

    /* Free tree: always take, whatever the mode. */
    JC_CHECK(jc_lease_decide(0, 0, JC_LEASE_WARN) == JC_LEASE_TAKE);
    JC_CHECK(jc_lease_decide(0, 0, JC_LEASE_FAIL) == JC_LEASE_TAKE);
    JC_CHECK(jc_lease_decide(0, 0, JC_LEASE_OFF)  == JC_LEASE_TAKE);

    /* Held by a LIVE run: warn-and-take, or refuse. This is the only row where
     * the mode changes the answer, and it is the row the feature exists for. */
    JC_CHECK(jc_lease_decide(1, 1, JC_LEASE_WARN) == JC_LEASE_WARN_TAKE);
    JC_CHECK(jc_lease_decide(1, 1, JC_LEASE_FAIL) == JC_LEASE_REFUSE);

    /* Held by a DEAD run: taken QUIETLY, even under fail. A crashed run must not
     * block every later one -- the classic lockfile failure, and the operator
     * cannot tell a stale file from a live holder by looking. */
    JC_CHECK(jc_lease_decide(1, 0, JC_LEASE_WARN) == JC_LEASE_TAKE);
    JC_CHECK(jc_lease_decide(1, 0, JC_LEASE_FAIL) == JC_LEASE_TAKE);

    /* OFF never consults and never refuses, live holder or not. */
    JC_CHECK(jc_lease_decide(1, 1, JC_LEASE_OFF) == JC_LEASE_TAKE);
    JC_CHECK(jc_lease_decide(1, 0, JC_LEASE_OFF) == JC_LEASE_TAKE);

    /* --- the path, and the key it shares with the checkpoint repo ----------- */
    jc_lease_path("/home/u", "/w/proj", path, sizeof(path));
    JC_CHECK(strstr(path, "/home/u/.jichi.d/leases/") == path);
    JC_CHECK(strstr(path, ".json") != NULL);
    /* Two different trees must not collide. */
    jc_lease_path("/home/u", "/w/other", other, sizeof(other));
    JC_CHECK(strcmp(path, other) != 0);
    /* The same tree is the same lease, twice. */
    jc_lease_path("/home/u", "/w/proj", other, sizeof(other));
    JC_CHECK(strcmp(path, other) == 0);
    /* ONE derivation: the lease's number is the checkpoint repo's number, so a
     * human debugging a project sees the same key in both stores. */
    {
        char buf[64];
        char gitdir[512];
        jc_snprintf(buf, sizeof(buf), "%lu", jc_workspace_key("/w/proj"));
        JC_CHECK(strstr(path, buf) != NULL);
        jc_snapshot_git_dir("/home/u", "/w/proj", gitdir, sizeof(gitdir));
        JC_CHECK(strstr(gitdir, buf) != NULL);
    }
    /* A NULL work tree must not crash; it is simply its own (empty) key. */
    jc_lease_path("/home/u", NULL, path, sizeof(path));
    JC_CHECK(path[0] != '\0');

    /* --- render / parse round trip ------------------------------------------ */
    memset(&in, 0, sizeof(in));
    strcpy(in.run, "3f6b1c22-0000-4000-8000-abcdefabcdef");
    in.pid = 4242;
    in.started = 1786600000L;
    strcpy(in.mode, "auto");
    json = jc_lease_render(&in);
    JC_CHECK(json != NULL);
    if (json != NULL) {
        memset(&out, 0, sizeof(out));
        JC_CHECK(jc_lease_parse(json, &out) == 1);
        JC_CHECK(strcmp(out.run, in.run) == 0);
        JC_CHECK(out.pid == in.pid);
        JC_CHECK(out.started == in.started);
        JC_CHECK(strcmp(out.mode, in.mode) == 0);
        free(json);
    }

    /* Malformed input is "no lease", never an error: a truncated or hand-edited
     * file must not wedge every later run in that workspace. */
    JC_CHECK(jc_lease_parse("not json at all", &out) == 0);
    JC_CHECK(jc_lease_parse("{}", &out) == 0);
    JC_CHECK(jc_lease_parse(NULL, &out) == 0);
    /* A record naming no pid cannot be checked for liveness, so it is not a
     * holder -- otherwise it would be an immortal lease. */
    JC_CHECK(jc_lease_parse("{\"run\":\"x\",\"pid\":0}", &out) == 0);

    /* --- liveness ----------------------------------------------------------- */
    JC_CHECK(jc_lease_pid_alive(0) == 0);
    JC_CHECK(jc_lease_pid_alive(-1) == 0);
    /* Our OWN pid is alive by construction, on every platform. */
    JC_CHECK(jc_lease_pid_alive((long)getpid()) == 1);
    /* pid 1 used to carry this check, on the stated grounds that it "always
     * exists on a POSIX system, and belongs to root -- so this also covers the
     * EPERM branch". Both halves are CONDITIONAL and neither condition was ever
     * checked (M477). Cygwin has no pid 1 at all (kill gives ESRCH, /proc/1 is
     * absent), so jc_lease_pid_alive(1) correctly answers 0 and the assertion was
     * testing the host rather than jichi. And where the suite runs as ROOT --
     * this project's WSL2 and container rows do -- kill(1, 0) SUCCEEDS, so the
     * branch taken is the ordinary one and EPERM is never reached there either.
     * Kept where it is meaningful, skipped where it is not. */
    if (!(kill(1, 0) == -1 && errno == ESRCH)) {
        JC_CHECK(jc_lease_pid_alive(1) == 1);
    }
}
