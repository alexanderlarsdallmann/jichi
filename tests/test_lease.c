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
#include <time.h>

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
    jc_lease_path("/home/u", "/w/proj", NULL, path, sizeof(path));
    JC_CHECK(strstr(path, "/home/u/.jichi.d/leases/") == path);
    JC_CHECK(strstr(path, ".json") != NULL);
    /* Two different trees must not collide. */
    jc_lease_path("/home/u", "/w/other", NULL, other, sizeof(other));
    JC_CHECK(strcmp(path, other) != 0);
    /* The same tree is the same lease, twice. */
    jc_lease_path("/home/u", "/w/proj", NULL, other, sizeof(other));
    JC_CHECK(strcmp(path, other) == 0);
    /* ONE derivation: the lease's key is the checkpoint repo's key, so a human
     * debugging a project sees the same key in both stores. After D-09 the key
     * is a SHA-256 digest rendered as 64 lowercase hex chars (not a decimal
     * unsigned long). We assert the POST-FIX shape: the filename
     * "<key>.json" under ".../leases/" and the dir after ".../checkpoints/"
     * must BOTH be 64 lowercase-hex AND byte-identical. RED on the unfixed
     * tree, where the key is 20 decimal digits. */
    {
        const char *lease_key;   /* the filename minus ".json" */
        const char *cp_key;      /* the part after the last '/' */
        int lease_hex, cp_hex;
        char cp_path[512];
        size_t i;

        jc_snapshot_git_dir("/home/u", "/w/proj", cp_path, sizeof(cp_path));
        /* The lease path is ".../leases/<key>.json". */
        lease_key = strrchr(path, '/');
        JC_REQUIRE(lease_key != NULL);
        lease_key++;
        if (lease_key != NULL) {
            char lk[128];
            jc_snprintf(lk, sizeof(lk), "%s", lease_key);
            /* strip the trailing ".json" */
            {
                size_t len = strlen(lk);
                if (len > 5 && strcmp(lk + len - 5, ".json") == 0)
                    lk[len - 5] = '\0';
            }
            lease_hex = 1;
            if (strlen(lk) != 64) lease_hex = 0;
            for (i = 0; lk[i] != '\0'; i++) {
                char c = lk[i];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                    lease_hex = 0;
                    break;
                }
            }
            JC_CHECK(lease_hex == 1);

            /* The checkpoint git dir is ".../checkpoints/<key>". */
            cp_key = strrchr(cp_path, '/');
            JC_REQUIRE(cp_key != NULL);
            if (cp_key != NULL) {
                cp_key++;
                cp_hex = 1;
                if (strlen(cp_key) != 64) cp_hex = 0;
                for (i = 0; cp_key[i] != '\0'; i++) {
                    char c = cp_key[i];
                    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                        cp_hex = 0;
                        break;
                    }
                }
                JC_CHECK(cp_hex == 1);
                /* the invariant itself: byte-identical keys in both stores */
                JC_CHECK_STR(lk, cp_key);
            }
        }
    }
    /* A NULL work tree must not crash; it is simply its own (empty) key. */
    jc_lease_path("/home/u", NULL, NULL, path, sizeof(path));
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
        JC_CHECK(out.root[0] == '\0' && out.control[0] == '\0');
        free(json);
    }
    /* M759: the root and the control socket survive the round trip, for ps. */
    jc_snprintf(in.root, sizeof(in.root), "%s", "/home/u/work");
    jc_snprintf(in.control, sizeof(in.control), "%s", "/tmp/run.sock");
    json = jc_lease_render(&in);
    JC_CHECK(json != NULL);
    if (json != NULL) {
        memset(&out, 0, sizeof(out));
        JC_CHECK(jc_lease_parse(json, &out) == 1);
        JC_CHECK(strcmp(out.root, "/home/u/work") == 0);
        JC_CHECK(strcmp(out.control, "/tmp/run.sock") == 0);
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

    /* --- M760: the take's lock ----------------------------------------------
     * Only what one process can see. fcntl locks never conflict within the
     * process that holds them, so the cross-process property -- the second run
     * WAITS, then reads the first one's record -- is lease_race.sh's to check.
     * Here: the lock creates its file 0600, is released, and can be taken again;
     * a path whose directory is missing is NONE, not BUSY, since no wait fixes
     * it; and releasing NONE is a no-op, not a close() of a negative number. */
    {
        char lp[512];
        int lk;
        jc_snprintf(lp, sizeof(lp), "%s", jc_test_tmp("jichi_lease_lock_test.lock"));
        (void)jc_test_rm_rf(lp);
        lk = jc_lock_take(lp, 0L);
        JC_CHECK(lk >= 0);
        JC_CHECK(jc_file_exists(lp));
        JC_CHECK(jc_file_mode(lp) >= 0 && (jc_file_mode(lp) & 077) == 0);
        jc_lock_release(lk);
        lk = jc_lock_take(lp, 0L);
        JC_CHECK(lk >= 0);
        jc_lock_release(lk);
        JC_CHECK(jc_lock_take(jc_test_tmp("jichi_no_such_dir_760/x.lock"), 0L)
                 == JC_LOCK_NONE);
        jc_lock_release(JC_LOCK_NONE);
        JC_CHECK(jc_lock_take(NULL, 0L) == JC_LOCK_NONE);
        (void)jc_test_rm_rf(lp);
    }

    /* --- per-holder records (D-17) ------------------------------------------
     * M1101. The lease was ONE file, <key>.json. A second run that warned and
     * proceeded wrote its record OVER the first's, and on exit removed the only
     * record -- so the still-alive first run was left unrecorded, and a third
     * run with --lease fail read "free" and took. The fix is ONE RECORD PER
     * HOLDER: <key>.<run>.json. acquire scans every record of this workspace
     * (any live holder means held; *holder names the most recently started live
     * one), writes its OWN file beside the others, and release removes only its
     * own. This block drives the REAL acquire / scan / release API against a
     * private home and a fixed work tree, with LIVE pids (this test process), so
     * a "stale" misread cannot quietly take over and turn a check green for the
     * wrong reason.
     *
     * RED-FIRST: on the unfixed single-file acquire/release, checks 4 (two
     * per-run files), 7 (a-run survives b-run's release) and 8 (D is still
     * refused after B leaves) go red; the fix makes them green. Checks 3, 5, 6,
     * 9 and 10 pin the compatibility arm, the holder-naming and the end state. */
    {
        const char *ws = "/tmp/jichi_d17_ws";
        const char *tmp_home;
        char home[512];
        char p_null[512], p_empty[512], p_run[512];
        char p_a[512], p_b[512], p_c[512], p_d[512], p_e[512];
        struct jc_lease_info A, B, C, D, E;
        struct jc_lease_info h;
        struct jc_lease_info ml;
        enum jc_lease_verdict vA, vB, vC, vD, vE;
        int n_live, n_stale;
        size_t len;
        const char *d17_dir = "jichi.d/leases";

        tmp_home = jc_test_tmp("leasehome");
        jc_snprintf(home, sizeof(home), "%s", tmp_home);
        (void)jc_test_rm_rf(home);
        /* Create the home; acquire makes home/.jichi.d/leases itself. */
        jc_mkdir_p(home);

        /* 1. run NULL == run empty == the legacy bare <key>.json. Pin the
         * compatibility arm: a run id that is NULL or empty yields the old name. */
        jc_lease_path(home, ws, NULL, p_null, sizeof(p_null));
        jc_lease_path(home, ws, "", p_empty, sizeof(p_empty));
        JC_CHECK(strcmp(p_null, p_empty) == 0);
        len = strlen(p_null);
        JC_CHECK(len > 5 && strcmp(p_null + len - 5, ".json") == 0);

        /* 2. run "a-run" -> <home>/.jichi.d/leases/<key>.a-run.json. */
        jc_lease_path(home, ws, "a-run", p_run, sizeof(p_run));
        jc_snprintf(p_a, sizeof(p_a), "%s", p_run);
        len = strlen(p_run);
        JC_CHECK(strncmp(p_run, home, strlen(home)) == 0 &&
                 strstr(p_run, d17_dir) != NULL);
        JC_CHECK(len > 11 && strcmp(p_run + len - 11, ".a-run.json") == 0);

        /* The per-run file names, for the existence checks below. */
        jc_lease_path(home, ws, "a-run", p_a, sizeof(p_a));
        jc_lease_path(home, ws, "b-run", p_b, sizeof(p_b));
        jc_lease_path(home, ws, "c-run", p_c, sizeof(p_c));
        jc_lease_path(home, ws, "d-run", p_d, sizeof(p_d));
        jc_lease_path(home, ws, "e-run", p_e, sizeof(p_e));

        /* Two holders, this test process (LIVE), started apart so the scan's
         * "most recently started" rule has a definite answer. */
        memset(&A, 0, sizeof(A));
        jc_snprintf(A.run, sizeof(A.run), "%s", "a-run");
        A.pid = (long)getpid();
        A.started = 1000;
        jc_snprintf(A.mode, sizeof(A.mode), "%s", "auto");
        memset(&B, 0, sizeof(B));
        jc_snprintf(B.run, sizeof(B.run), "%s", "b-run");
        B.pid = (long)getpid();
        B.started = 2000;
        jc_snprintf(B.mode, sizeof(B.mode), "%s", "auto");

        /* 3. A takes a free tree quietly; B, with A live, must WARN_TAKE (not
         * TAKE): held is true with a live A. */
        vA = jc_lease_acquire(home, ws, &A, JC_LEASE_WARN, &h);
        JC_CHECK(vA == JC_LEASE_TAKE);
        vB = jc_lease_acquire(home, ws, &B, JC_LEASE_WARN, &h);
        JC_CHECK(vB == JC_LEASE_WARN_TAKE);

        /* 4. THE CORE RED: BOTH per-run files exist on disk. Today the second
         * acquire overwrites the single <key>.json, so the per-run names are
         * absent. (Two files, one per holder.) */
        JC_CHECK(jc_file_exists(p_a));
        JC_CHECK(jc_file_exists(p_b));

        /* 5. The warn-take's *holder names the OTHER run: at B's acquire the
         * only live record is A, so most_live is a-run. */
        JC_CHECK(strcmp(h.run, "a-run") == 0);

        /* 6. A third run in FAIL mode is REFUSED while live holders remain; its
         * *holder names a live run. */
        memset(&C, 0, sizeof(C));
        jc_snprintf(C.run, sizeof(C.run), "%s", "c-run");
        C.pid = (long)getpid();
        C.started = 3000;
        jc_snprintf(C.mode, sizeof(C.mode), "%s", "auto");
        vC = jc_lease_acquire(home, ws, &C, JC_LEASE_FAIL, &h);
        JC_CHECK(vC == JC_LEASE_REFUSE);

        /* 7. release b-run: the b-run file is gone, the a-run file SURVIVES.
         * THE CORE RED: today release removes the only file, so a-run is lost. */
        jc_lease_release(home, ws, "b-run");
        JC_CHECK(!jc_file_exists(p_b));
        JC_CHECK(jc_file_exists(p_a));

        /* 8. After B leaves, D (FAIL) is still REFUSED: A is alive and recorded.
         * THE D-17 RED: today B's release erased the only record, so D reads
         * "free" and is let in -- a third run while A is still working. */
        memset(&D, 0, sizeof(D));
        jc_snprintf(D.run, sizeof(D.run), "%s", "d-run");
        D.pid = (long)getpid();
        D.started = 4000;
        jc_snprintf(D.mode, sizeof(D.mode), "%s", "auto");
        vD = jc_lease_acquire(home, ws, &D, JC_LEASE_FAIL, &h);
        JC_CHECK(vD == JC_LEASE_REFUSE);

        /* 9. release a-run: both per-run files gone. The c/d arms never wrote
         * (both were refused -- no c-run.json, no d-run.json), so the only
         * files that ever existed are a-run.json and b-run.json; b was released
         * at 7, a now. A fresh holder E (FAIL) then finds nothing live and is
         * TAKEN. E is released immediately, so the final scan (10) sees nothing.
         * Arithmetic: files written {a,b}; released {b,a,e}; refused {c,d}. */
        jc_lease_release(home, ws, "a-run");
        JC_CHECK(!jc_file_exists(p_a));
        JC_CHECK(!jc_file_exists(p_b));
        memset(&E, 0, sizeof(E));
        jc_snprintf(E.run, sizeof(E.run), "%s", "e-run");
        E.pid = (long)getpid();
        E.started = 5000;
        jc_snprintf(E.mode, sizeof(E.mode), "%s", "auto");
        vE = jc_lease_acquire(home, ws, &E, JC_LEASE_FAIL, &h);
        JC_CHECK(vE == JC_LEASE_TAKE);
        jc_lease_release(home, ws, "e-run");

        /* 10. The scan over the final state: nothing live, nothing stale,
         * most_live zeroed. (Acquire/scan/release left no record for ws.) */
        n_live = -1;
        n_stale = -1;
        (void)jc_lease_scan(home, ws, &ml, &n_live, &n_stale);
        JC_CHECK(n_live == 0);
        JC_CHECK(n_stale == 0);
        JC_CHECK(ml.run[0] == '\0');

        /* 11. (D-17 review nit 3) MULTI-LIVE SELECTION, pinned. Two live
         * records with DISTINCT started: the scan's most_live is the
         * most-recently-started, independent of name and of readdir order.
         * Planted through the real acquire API -- jc_lease_render writes
         * me->started verbatim (no time(NULL) stamp of its own), so the
         * values below are the ones the scan reads. */
        {
            const char *ws_s = "/tmp/jichi_d17_ws_s";
            char p_s1[512], p_s2[512];
            struct jc_lease_info S1, S2;

            jc_lease_path(home, ws_s, "s1-run", p_s1, sizeof(p_s1));
            jc_lease_path(home, ws_s, "s2-run", p_s2, sizeof(p_s2));
            memset(&S1, 0, sizeof(S1));
            jc_snprintf(S1.run, sizeof(S1.run), "%s", "s1-run");
            S1.pid = (long)getpid();
            S1.started = 100;
            jc_snprintf(S1.mode, sizeof(S1.mode), "%s", "auto");
            memset(&S2, 0, sizeof(S2));
            jc_snprintf(S2.run, sizeof(S2.run), "%s", "s2-run");
            S2.pid = (long)getpid();
            S2.started = 200;
            jc_snprintf(S2.mode, sizeof(S2.mode), "%s", "auto");
            JC_CHECK(jc_lease_acquire(home, ws_s, &S1, JC_LEASE_WARN, &h)
                     == JC_LEASE_TAKE);
            JC_CHECK(jc_lease_acquire(home, ws_s, &S2, JC_LEASE_WARN, &h)
                     == JC_LEASE_WARN_TAKE);
            n_live = -1;
            n_stale = -1;
            (void)jc_lease_scan(home, ws_s, &ml, &n_live, &n_stale);
            JC_CHECK(n_live == 2);
            JC_CHECK(n_stale == 0);
            JC_CHECK(strcmp(ml.run, "s2-run") == 0);   /* the later-started */
            jc_lease_release(home, ws_s, "s1-run");
            jc_lease_release(home, ws_s, "s2-run");
            JC_CHECK(!jc_file_exists(p_s1));
            JC_CHECK(!jc_file_exists(p_s2));
        }

        /* 12. (D-17 review nit 3) THE TIE-BREAK, pinned. Two live records
         * with EQUAL started: most_live is the lexicographically NEWEST name.
         * "t2-run" > "t1-run", so the t2 record wins no matter what order
         * readdir hands the files over. */
        {
            const char *ws_t = "/tmp/jichi_d17_ws_t";
            char p_t1[512], p_t2[512];
            struct jc_lease_info T1, T2;

            jc_lease_path(home, ws_t, "t1-run", p_t1, sizeof(p_t1));
            jc_lease_path(home, ws_t, "t2-run", p_t2, sizeof(p_t2));
            memset(&T1, 0, sizeof(T1));
            jc_snprintf(T1.run, sizeof(T1.run), "%s", "t1-run");
            T1.pid = (long)getpid();
            T1.started = 500;
            jc_snprintf(T1.mode, sizeof(T1.mode), "%s", "auto");
            memset(&T2, 0, sizeof(T2));
            jc_snprintf(T2.run, sizeof(T2.run), "%s", "t2-run");
            T2.pid = (long)getpid();
            T2.started = 500;
            jc_snprintf(T2.mode, sizeof(T2.mode), "%s", "auto");
            JC_CHECK(jc_lease_acquire(home, ws_t, &T1, JC_LEASE_WARN, &h)
                     == JC_LEASE_TAKE);
            JC_CHECK(jc_lease_acquire(home, ws_t, &T2, JC_LEASE_WARN, &h)
                     == JC_LEASE_WARN_TAKE);
            n_live = -1;
            n_stale = -1;
            (void)jc_lease_scan(home, ws_t, &ml, &n_live, &n_stale);
            JC_CHECK(n_live == 2);
            JC_CHECK(n_stale == 0);
            JC_CHECK(strcmp(ml.run, "t2-run") == 0);   /* newest name wins */
            jc_lease_release(home, ws_t, "t1-run");
            jc_lease_release(home, ws_t, "t2-run");
            JC_CHECK(!jc_file_exists(p_t1));
            JC_CHECK(!jc_file_exists(p_t2));
        }

        /* 13. (D-17 review nit 4) THE SCAN'S WORKSPACE BOUNDARY, pinned. The
         * leases directory holds records of TWO workspaces plus the take's
         * lock and a malformed empty-run name; each scan sees exactly its own.
         *
         * (a) foreign: a different work tree's record, <otherkey>.<run>.json,
         * with a LIVE pid, is invisible to ws's scan. The key comes from the
         * same helper the real names use (jc_workspace_key), so this is the
         * shape a sibling project's record has, not a hand-waved name.
         * (b) lock: <key>.lock (a regular file, as jc_lock_take makes it) is
         * the take's lock, not a record of any workspace.
         * (c) empty run: <key>..json has an EMPTY run between the dots. The
         * name is key + "." + run + ".json" with run >= 1 char, so it is not
         * ws's record. THE RED of this block: the matcher's prefix arm
         * required only `len > prefix_len && len >= 6`; this name is
         * prefix_len + 5 long ("..json"), both inequalities hold, and its
         * last five bytes are ".json" -- so the UNFIXED scan counts it: a
         * crafted file wedged a live pid into another run's held-ness.
         *
         * All three carry live pids, so each would count n_live if the scan
         * matched it: the counts below pin the boundary, not the liveness. */
        {
            const char *ws2 = "/tmp/jichi_d17_ws2";
            char p_f[512], p_l[512], p_e[512], p_own[512];
            struct jc_lease_info F, OW;
            char *rec;

            jc_lease_path(home, ws2, "f-run", p_f, sizeof(p_f));
            jc_lease_path(home, ws, NULL, p_e, sizeof(p_e));
            jc_snprintf(p_l, sizeof(p_l), "%s", p_e);   /* <key>.json */
            p_l[strlen(p_l) - 5] = '\0';                /* strip ".json" */
            jc_snprintf(p_l + strlen(p_l), sizeof(p_l) - strlen(p_l),
                        "%s", ".lock");                 /* <key>.lock */
            /* <key>..json: the key plus TWO dots and "json" -- an EMPTY run
             * between them (64-hex key + "..json" = prefix_len + 5 chars).
             * Built from the stripped key, not by mutating the legacy name:
             * a single-char edit there would leave ".json" unchanged. */
            p_e[0] = '\0';
            jc_snprintf(p_e, sizeof(p_e), "%s", p_l);   /* <key>.lock */
            p_e[strlen(p_e) - 5] = '\0';                /* strip ".lock" */
            jc_snprintf(p_e + strlen(p_e), sizeof(p_e) - strlen(p_e),
                        "%s", "..json");                /* <key>..json */
            jc_lease_path(home, ws, "own", p_own, sizeof(p_own));

            memset(&F, 0, sizeof(F));
            jc_snprintf(F.run, sizeof(F.run), "%s", "f-run");
            F.pid = (long)getpid();
            F.started = 10;
            jc_snprintf(F.mode, sizeof(F.mode), "%s", "auto");
            /* The same live record goes to the foreign name and to the
             * empty-run name: a dead pid would be unreadable anyway (parse
             * rejects it) and would not prove the BOUNDARY, which is what the
             * red below is about. */
            rec = jc_lease_render(&F);
            JC_CHECK(rec != NULL);
            JC_CHECK(jc_write_file_atomic(p_f, rec, (jc_size)strlen(rec))
                     == JC_OK);
            JC_CHECK(jc_write_file_atomic(p_l, "", 0) == JC_OK);
            JC_CHECK(jc_write_file_atomic(p_e, rec, (jc_size)strlen(rec))
                     == JC_OK);
            free(rec);

            /* ws's scan: only its own record. The foreign f-run, the lock
             * and the empty-run file all count neither live nor stale. */
            memset(&OW, 0, sizeof(OW));
            jc_snprintf(OW.run, sizeof(OW.run), "%s", "own");
            OW.pid = (long)getpid();
            OW.started = 20;
            jc_snprintf(OW.mode, sizeof(OW.mode), "%s", "auto");
            JC_CHECK(jc_lease_acquire(home, ws, &OW, JC_LEASE_WARN, &h)
                     == JC_LEASE_TAKE);
            n_live = -1;
            n_stale = -1;
            (void)jc_lease_scan(home, ws, &ml, &n_live, &n_stale);
            JC_CHECK(n_live == 1);
            JC_CHECK(n_stale == 0);
            JC_CHECK(strcmp(ml.run, "own") == 0);

            /* ws2's scan: exactly its own one live record, nothing else. */
            n_live = -1;
            n_stale = -1;
            (void)jc_lease_scan(home, ws2, &ml, &n_live, &n_stale);
            JC_CHECK(n_live == 1);
            JC_CHECK(n_stale == 0);
            JC_CHECK(strcmp(ml.run, "f-run") == 0);

            /* The red half, named directly: over ws, the planted foreign
             * file, the lock and the empty-run name must count NOTHING.
             * With ws's own record released below, only they remain, and the
             * fixed scan sees 0 live and 0 stale. */
            jc_lease_release(home, ws, "own");
            n_live = -1;
            n_stale = -1;
            (void)jc_lease_scan(home, ws, &ml, &n_live, &n_stale);
            JC_CHECK(n_live == 0);
            JC_CHECK(n_stale == 0);
            JC_CHECK(ml.run[0] == '\0');

            remove(p_f);
            remove(p_l);
            remove(p_e);
        }

        /* Clean the home and everything beneath it (the leases dir). */
        (void)jc_test_rm_rf(home);
    }
}
