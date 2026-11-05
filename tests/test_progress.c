/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_progress.c - the learner's progress record: scan / row (C5, M174). */

#include "jc_test.h"
#include "jc_progress.h"
#include "jc_snprintf.h"
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>

static const char *LOG =
    "{\"ts\":1,\"spec\":\"docs/assignments/00-hello.md\",\"passed\":false,"
    "\"pct\":0,\"tests_run\":0,\"tests_failed\":0}\n"
    "not json at all -- the learner edited the file\n"
    "{\"ts\":2,\"spec\":\"docs/assignments/00-hello.md\",\"passed\":true,"
    "\"pct\":100,\"tests_run\":0,\"tests_failed\":0,\"hints\":2}\n"
    "{\"ts\":3,\"spec\":\"01-find-the-value.md\",\"passed\":false,"
    "\"pct\":40,\"tests_run\":5,\"tests_failed\":3}\n";

static void test_base(void)
{
    JC_CHECK_STR(jc_progress_base("docs/assignments/00-hello.md"),
                 "00-hello.md");
    JC_CHECK_STR(jc_progress_base("00-hello.md"), "00-hello.md");
    JC_CHECK(jc_progress_base(NULL) == NULL);
}

static void test_scan(void)
{
    struct jc_progress p;

    /* Two records for 00-hello (one failed, one passed): passed wins, the
     * best pct is kept, and the path/basename forms match each other. */
    jc_progress_scan(LOG, "00-hello.md", &p);
    JC_CHECK(p.attempts == 2);
    JC_CHECK(p.passed == 1);
    JC_CHECK(p.best_pct == 100);
    jc_progress_scan(LOG, "docs/assignments/00-hello.md", &p);
    JC_CHECK(p.attempts == 2 && p.passed == 1);

    /* Attempted but never passed. */
    jc_progress_scan(LOG, "01-find-the-value.md", &p);
    JC_CHECK(p.attempts == 1);
    JC_CHECK(p.passed == 0);
    JC_CHECK(p.best_pct == 40);

    /* Never graded; and a missing file (NULL text) is not an error. */
    jc_progress_scan(LOG, "02-unseen.md", &p);
    JC_CHECK(p.attempts == 0 && p.passed == 0 && p.best_pct == 0);
    jc_progress_scan(NULL, "00-hello.md", &p);
    JC_CHECK(p.attempts == 0);
}

static void test_row(void)
{
    struct jc_progress p;
    char row[256];

    memset(&p, 0, sizeof(p));

    /* Untouched spec: phase/points shown, status "-". */
    jc_progress_row("00-hello.md", "implementation", 1, 0, &p,
                    row, sizeof(row));
    JC_CHECK(strstr(row, "00-hello.md") != NULL);
    JC_CHECK(strstr(row, "implementation") != NULL);
    JC_CHECK(strstr(row, "1pt") != NULL);
    JC_CHECK(strstr(row, "(+solution)") == NULL);

    /* Passed, with a solution sibling. */
    p.attempts = 2;
    p.passed = 1;
    p.best_pct = 100;
    jc_progress_row("00-hello.md", NULL, 0, 1, &p, row, sizeof(row));
    JC_CHECK(strstr(row, "passed") != NULL);
    JC_CHECK(strstr(row, "(+solution)") != NULL);

    /* Attempted, not yet passed: the best pct is visible. */
    p.passed = 0;
    p.best_pct = 40;
    jc_progress_row("01-x.md", "testing", 3, 0, &p, row, sizeof(row));
    JC_CHECK(strstr(row, "attempted (best 40%)") != NULL);

    /* The header names the same columns the rows fill. */
    jc_progress_row_header(row, sizeof(row));
    JC_CHECK(strstr(row, "assignment") != NULL);
    JC_CHECK(strstr(row, "phase") != NULL);
    JC_CHECK(strstr(row, "status") != NULL);
}

/* ---- M502: the hint log --------------------------------------------------
 *
 * The defect: 74 shipped specs, the scaffold glossary and CURRICULUM.md have
 * promised hints are "free, and recorded" since M174, and nothing wrote the
 * record -- so a teacher could not see that a learner needed all three rungs,
 * which is the diagnostic half of a hint ladder.
 *
 * The load-bearing property is the SEPARATION: hint pulls must never be
 * readable as graded attempts, because every reader of progress.jsonl counts a
 * line as one. These pin that a hint line is invisible to the progress scanner
 * and vice versa. */
/* ---- M635: predictions ----------------------------------------------------
 * The tally must be order-safe (a resolve with nothing open is ignored), and a
 * prediction line must be invisible to the attempt scanner -- the same
 * separation argument as the hint log, kept true by a different file. */
static void test_predict_scan(void)
{
    static const char *P =
        "{\"ts\":1,\"kind\":\"resolve\",\"right\":true}\n"  /* nothing open */
        "{\"ts\":2,\"kind\":\"predict\",\"text\":\"CRLF fails\"}\n"
        "{\"ts\":3,\"kind\":\"predict\",\"text\":\"second\"}\n"
        "{\"ts\":4,\"kind\":\"resolve\",\"right\":false}\n"
        "not json at all\n";
    struct jc_predictions pr;
    struct jc_progress p;

    jc_progress_predict_scan(P, &pr);
    JC_CHECK(pr.made == 2);
    JC_CHECK(pr.resolved == 1);      /* the first resolve had nothing to close */
    JC_CHECK(pr.right == 0);         /* ...so its right:true counts nowhere    */
    JC_CHECK(pr.open == 1);

    /* The other order: predict, then a right resolve. */
    jc_progress_predict_scan(
        "{\"kind\":\"predict\",\"text\":\"a\"}\n"
        "{\"kind\":\"resolve\",\"right\":true}\n", &pr);
    JC_CHECK(pr.made == 1 && pr.resolved == 1 && pr.right == 1 && pr.open == 0);

    /* Two resolves for one prediction: the second is ignored. */
    jc_progress_predict_scan(
        "{\"kind\":\"predict\",\"text\":\"a\"}\n"
        "{\"kind\":\"resolve\",\"right\":true}\n"
        "{\"kind\":\"resolve\",\"right\":true}\n", &pr);
    JC_CHECK(pr.made == 1 && pr.resolved == 1 && pr.right == 1);

    jc_progress_predict_scan(NULL, &pr);
    JC_CHECK(pr.made == 0 && pr.resolved == 0 && pr.right == 0 && pr.open == 0);

    /* THE SEPARATION: fed to the attempt scanner, prediction lines are not
     * attempts for any spec (no spec field) -- and the files are different
     * anyway, which is what makes the promise true by construction. */
    jc_progress_scan(P, "00-hello.md", &p);
    JC_CHECK(p.attempts == 0);
}

static void test_hints_scan(void)
{
    static const char *HINTS =
        "{\"ts\":1,\"spec\":\"docs/assignments/00-hello.md\",\"rung\":1}\n"
        "{\"ts\":2,\"spec\":\"docs/assignments/00-hello.md\",\"rung\":3}\n"
        "{\"ts\":3,\"spec\":\"01-other.md\",\"rung\":1}\n"
        "not json at all\n";
    struct jc_hints h;
    struct jc_progress p;

    /* Two pulls, deepest rung 3 -- the max, not the last. */
    jc_progress_hints_scan(HINTS, "docs/assignments/00-hello.md", &h);
    JC_CHECK(h.pulls == 2);
    JC_CHECK(h.max_rung == 3);

    /* Basename matching, exactly like the progress scanner: a learner grading
     * from inside the directory and one grading from the root must agree. */
    jc_progress_hints_scan(HINTS, "00-hello.md", &h);
    JC_CHECK(h.pulls == 2 && h.max_rung == 3);

    jc_progress_hints_scan(HINTS, "01-other.md", &h);
    JC_CHECK(h.pulls == 1 && h.max_rung == 1);

    /* Untouched spec, and a NULL log: zero, not garbage. */
    jc_progress_hints_scan(HINTS, "99-never.md", &h);
    JC_CHECK(h.pulls == 0 && h.max_rung == 0);
    jc_progress_hints_scan(NULL, "00-hello.md", &h);
    JC_CHECK(h.pulls == 0 && h.max_rung == 0);

    /* THE SEPARATION. A hint line must not register as an attempt... */
    jc_progress_scan(HINTS, "docs/assignments/00-hello.md", &p);
    JC_CHECK(p.attempts == 2);   /* they parse as lines for this spec ... */
    JC_CHECK(p.passed == 0);     /* ... but carry no verdict */
    JC_CHECK(p.best_pct == 0);
    /* ...which is exactly why the two live in DIFFERENT FILES: the scanner
     * cannot tell them apart, so nothing may put them in one file. This
     * assertion is the reason the design is a separate sink, kept here so a
     * later "simplification" into one file fails a test rather than quietly
     * turning every hint into a failed attempt. */
}

/* M642: a write that fails must be REPORTED. The three appenders ignored
 * fprintf's and fclose's return values and answered JC_OK, so `grade --record`
 * on a full disk printed no warning and the learner's attempt was gone (the
 * refute A/B's first seat claimed it; the second seat dismissed it; the
 * measurement made us test it). /dev/full is a device on which every write
 * fails with ENOSPC, so a progress file that is a symlink to it is exactly that
 * disk. */
static int link_full(const char *sub, const char *name, char *out, size_t cap)
{
    jc_snprintf(out, cap, "%s/%s", sub, name);
    (void)remove(out);
    return symlink("/dev/full", out) == 0;
}

/* M997: the .jichi DIR check does not stop a symlinked LEAF. Here .jichi is a
 * REAL directory, but .jichi/<name>.jsonl is a SYMLINK to an OUTSIDE file. The
 * appenders must refuse (JC_ERR_DENIED) and the outside file must be left
 * byte-for-byte untouched. A real dir + absent leaf is the control: JC_OK. */
static int outside_unchanged(const char *a, const char *b)
{
    FILE *fa, *fb;
    int c;
    fa = fopen(a, "rb");
    fb = fopen(b, "rb");
    if (fa == NULL || fb == NULL) {
        if (fa) fclose(fa);
        if (fb) fclose(fb);
        return 0;
    }
    while ((c = fgetc(fa)) != EOF) {
        if (c != fgetc(fb)) {
            fclose(fa);
            fclose(fb);
            return 0;
        }
    }
    fclose(fa);
    fclose(fb);
    return 1;
}

static void seed_and_backup(const char *src, const char *bak)
{
    FILE *f;
    f = fopen(src, "wb");
    if (f != NULL) {
        fputs("pristine outside line\n", f);
        fclose(f);
    }
    f = fopen(bak, "wb");
    if (f != NULL) {
        fputs("pristine outside line\n", f);
        fclose(f);
    }
}

static void test_leaf_symlink_guard(void)
{
    const char *dir = jc_test_tmp("jichi_progress_leaf");
    char sub[600];
    char outP[700], outH[700], outX[700];
    char bakP[750], bakH[750], bakX[750];
    char l1[700], l2[700], l3[700];
    int linked;
    FILE *f;
    int n;
    int c;
    jc_status st;

    (void)remove(dir);
    if (!JC_REQUIRE(mkdir(dir, 0700) == 0)) {
        return;
    }
    jc_snprintf(sub, sizeof sub, "%s/.jichi", dir);
    if (!JC_REQUIRE(mkdir(sub, 0700) == 0)) {
        return;
    }
    /* Outside files (seeded, with pristine backups) that the links point to:
     * plain siblings of .jichi. A write through the symlink would append to
     * THESE, not to the .jichi/ leaf -- exactly the escape the appenders must
     * refuse. */
    jc_snprintf(outP, sizeof outP, "%s/outside_progress.txt", dir);
    jc_snprintf(outH, sizeof outH, "%s/outside_hints.txt", dir);
    jc_snprintf(outX, sizeof outX, "%s/outside_predict.txt", dir);
    jc_snprintf(bakP, sizeof bakP, "%s.bak", outP);
    jc_snprintf(bakH, sizeof bakH, "%s.bak", outH);
    jc_snprintf(bakX, sizeof bakX, "%s.bak", outX);
    seed_and_backup(outP, bakP);
    seed_and_backup(outH, bakH);
    seed_and_backup(outX, bakX);

    /* .jichi/progress.jsonl -> outside file, and the same for the other two. */
    jc_snprintf(l1, sizeof l1, "%s/progress.jsonl", sub);
    jc_snprintf(l2, sizeof l2, "%s/hints.jsonl", sub);
    jc_snprintf(l3, sizeof l3, "%s/predictions.jsonl", sub);
    (void)remove(l1); (void)remove(l2); (void)remove(l3);
    linked = (symlink(outP, l1) == 0) && (symlink(outH, l2) == 0) &&
             (symlink(outX, l3) == 0);
    if (linked) {
        st = jc_progress_append(dir, "01-x.md", 1, 100, 1, 0, -1);
        JC_CHECK(st == JC_ERR_DENIED);
        JC_CHECK(outside_unchanged(outP, bakP));
        st = jc_progress_hint_append(dir, "01-x.md", 1);
        JC_CHECK(st == JC_ERR_DENIED);
        JC_CHECK(outside_unchanged(outH, bakH));
        st = jc_progress_predict_append(dir, "it will pass");
        JC_CHECK(st == JC_ERR_DENIED);
        JC_CHECK(outside_unchanged(outX, bakX));
        (void)remove(l1); (void)remove(l2); (void)remove(l3);
    }

    /* Control: real dir + ABSENT leaf -> JC_OK, and the leaf holds one line. */
    st = jc_progress_append(dir, "02-c.md", 0, 50, 0, 0, -1);
    JC_CHECK(st == JC_OK);
    st = jc_progress_hint_append(dir, "02-c.md", 2);
    JC_CHECK(st == JC_OK);
    st = jc_progress_predict_append(dir, "control");
    JC_CHECK(st == JC_OK);
    f = fopen(l1, "rb");
    n = 0;
    if (f != NULL) {
        while ((c = fgetc(f)) != EOF) {
            if (c == '\n') n++;
        }
        fclose(f);
    }
    JC_CHECK(n == 1);

    (void)remove(l1); (void)remove(l2); (void)remove(l3);
    (void)remove(outP); (void)remove(outH); (void)remove(outX);
    (void)remove(bakP); (void)remove(bakH); (void)remove(bakX);
    (void)rmdir(sub); (void)rmdir(dir);
}

static void test_write_failure(void)
{
    const char *dir = jc_test_tmp("jichi_progress_full");
    char sub[600];
    char l1[700], l2[700], l3[700];
    FILE *f;
    int n;
    int c;
    jc_status st;

    (void)mkdir(dir, 0700);
    jc_snprintf(sub, sizeof sub, "%s/.jichi", dir);
    (void)mkdir(sub, 0700);
    if (!link_full(sub, "progress.jsonl", l1, sizeof l1) ||
        !link_full(sub, "hints.jsonl", l2, sizeof l2) ||
        !link_full(sub, "predictions.jsonl", l3, sizeof l3)) {
        return; /* no symlinks or no /dev/full here: skip silently */
    }
    /* D-15/M997: a symlinked leaf is refused before the write. M997's leaf
     * guard lstats the leaf and answers JC_ERR_DENIED for a link (the M642
     * /dev/full write-failure, JC_ERR_IO, is the fallback when the link is
     * absent). Either way the write never happens. */
    st = jc_progress_append(dir, "01-x.md", 1, 100, 1, 0, -1);
    JC_CHECK(st == JC_ERR_DENIED || st == JC_ERR_INVALID || st == JC_ERR_IO);
    st = jc_progress_hint_append(dir, "01-x.md", 1);
    JC_CHECK(st == JC_ERR_DENIED || st == JC_ERR_INVALID || st == JC_ERR_IO);
    st = jc_progress_predict_append(dir, "it will pass");
    JC_CHECK(st == JC_ERR_DENIED || st == JC_ERR_INVALID || st == JC_ERR_IO);

    /* Control: the same calls against real files succeed and land one line. */
    (void)remove(l1); (void)remove(l2); (void)remove(l3);
    JC_CHECK(jc_progress_append(dir, "01-x.md", 1, 100, 1, 0, -1) == JC_OK);
    JC_CHECK(jc_progress_hint_append(dir, "01-x.md", 1) == JC_OK);
    JC_CHECK(jc_progress_predict_append(dir, "it will pass") == JC_OK);
    f = fopen(l1, "rb");
    n = 0;
    if (f != NULL) {
        while ((c = fgetc(f)) != EOF) { if (c == '\n') n++; }
        fclose(f);
    }
    JC_CHECK(n == 1);
    (void)remove(l1); (void)remove(l2); (void)remove(l3);
    (void)rmdir(sub); (void)rmdir(dir);
}

/* M642: a real regular file whose write fails at fclose (disk full / file too
 * large). The mechanism is fork+setrlimit(RLIMIT_FSIZE, ...) in the child so
 * fprintf succeeds but fclose fails, returning JC_ERR_IO exactly. This exercises
 * the production write path that the symlink tests skip (they hit JC_ERR_DENIED
 * before any write). If setrlimit is unavailable or the child cannot create the
 * file, skip silently (a skip is honest, a fake green is not). */

/* M996: write helper for fork children. Writes the int st to fd; if the write
 * does not return (int)sizeof(st), the result is recorded and the child exits
 * with 0xFF so the parent's read(...) != sizeof(st) path treats this as skipped.
 * C89: declarations at block top, no %zu. */
static void child_send(int fd, int st)
{
    int result;

    result = write(fd, &st, sizeof(st));
    if (result != (int)sizeof(st)) {
        /* Record the failure by returning a negative value that the parent
         * can detect. The child exits with 0xFF so the parent's read fails. */
        (void)result; /* suppress unused-result warning for the call above */
        _exit(0xFF);
    }
}

static void test_write_failure_regular(void)
{
    const char *dir = jc_test_tmp("jichi_progress_regular");
    char sub[600];
    char path[700];
    int pipefd[2];
    pid_t pid;
    int st;
    int child_status;
    struct rlimit rlim;
    int child_st; /* plain int: the -999 setrlimit marker and the child's jc_status travel as a C int; C++ will not convert int to the jc_status enum implicitly (supervisor 15:17) */

    (void)jc_test_rm_rf(dir);
    (void)mkdir(dir, 0700);
    jc_snprintf(sub, sizeof sub, "%s/.jichi", dir);
    if (!JC_REQUIRE(mkdir(sub, 0700) == 0)) {
        goto cleanup;
    }

    /* Create a pipe to communicate the child's status back to the parent. */
    if (pipe(pipefd) != 0) {
        goto cleanup;
    }

    pid = fork();
    if (pid == -1) {
        /* Fork failed: skip silently. */
        (void)close(pipefd[0]);
        (void)close(pipefd[1]);
        goto cleanup;
    }

    if (pid == 0) {
        /* Child: set RLIMIT_FSIZE to 0 so fclose fails after fprintf succeeds. */
        (void)close(pipefd[0]); /* close read end in child */

        /* Ignore SIGXFSZ BEFORE setting the limit so the write doesn't kill us. */
        signal(SIGXFSZ, SIG_IGN);

        rlim.rlim_cur = 0;
        rlim.rlim_max = 0;
        if (setrlimit(RLIMIT_FSIZE, &rlim) != 0) {
            /* setrlimit failed: skip this case by sending a special marker. */
            child_st = -999;  /* distinct from JC_OK (0) and JC_ERR_IO (2) */
            child_send(pipefd[1], child_st);
            _exit(0);
        }

        /* Create a REAL regular file (no symlink) under .jichi. */
        jc_snprintf(path, sizeof path, "%s/progress.jsonl", sub);
        (void)remove(path);

        /* Attempt to append: fprintf will succeed (buffered) but fclose will fail. */
        child_st = jc_progress_append(dir, "01-x.md", 1, 100, 1, 0, -1);

        /* Signal to parent that we completed and write our status. */
        child_send(pipefd[1], child_st);
        _exit(0);
    } else {
        /* Parent: read the child's status from the pipe. */
        (void)close(pipefd[1]); /* close write end in parent */
        if (read(pipefd[0], &st, sizeof(st)) != sizeof(st)) {
            (void)close(pipefd[0]);
            goto cleanup;
        }
        (void)close(pipefd[0]);

        /* Wait for the child to exit and check its exit code. */
        if (waitpid(pid, &child_status, 0) == -1) {
            goto cleanup;
        }

        /* If setrlimit failed, the test is skipped. The child wrote -999
         * when it couldn't set the limit, so skip silently. */
        if (st == -999) {
            goto cleanup;
        }

        /* The child must have seen a write failure at fclose: assert JC_ERR_IO.
         * If the appender returns JC_OK, the perturbation is in place (the test
         * goes RED because the assertion fails). */
        JC_CHECK(st == JC_ERR_IO);
        (void)rmdir(sub);
        (void)rmdir(dir);
        return;
    }

cleanup:
    /* Skip silently on any failure to set up the test. */
    (void)rmdir(dir);
}

/* M642: a fork+setrlimit test for the hint and predict appenders too. */
static void test_write_failure_hints_regular(void)
{
    const char *dir = jc_test_tmp("jichi_progress_hints_reg");
    char sub[600];
    char path[700];
    int pipefd[2];
    pid_t pid;
    int st;
    int child_status;
    struct rlimit rlim;
    int child_st; /* plain int: the -999 setrlimit marker and the child's jc_status travel as a C int; C++ will not convert int to the jc_status enum implicitly (supervisor 15:17) */

    (void)jc_test_rm_rf(dir);
    (void)mkdir(dir, 0700);
    jc_snprintf(sub, sizeof sub, "%s/.jichi", dir);
    if (!JC_REQUIRE(mkdir(sub, 0700) == 0)) {
        goto cleanup;
    }

    if (pipe(pipefd) != 0) {
        goto cleanup;
    }

    pid = fork();
    if (pid == -1) {
        (void)close(pipefd[0]);
        (void)close(pipefd[1]);
        goto cleanup;
    }

    if (pid == 0) {
        (void)close(pipefd[0]);

        signal(SIGXFSZ, SIG_IGN);

        rlim.rlim_cur = 0;
        rlim.rlim_max = 0;
        if (setrlimit(RLIMIT_FSIZE, &rlim) != 0) {
            child_st = -999;
            child_send(pipefd[1], child_st);
            _exit(0);
        }

        jc_snprintf(path, sizeof path, "%s/hints.jsonl", sub);
        (void)remove(path);

        child_st = jc_progress_hint_append(dir, "01-x.md", 1);

        child_send(pipefd[1], child_st);
        _exit(0);
    } else {
        (void)close(pipefd[1]);
        if (read(pipefd[0], &st, sizeof(st)) != sizeof(st)) {
            (void)close(pipefd[0]);
            goto cleanup;
        }
        (void)close(pipefd[0]);

        if (waitpid(pid, &child_status, 0) == -1) {
            goto cleanup;
        }

        if (st == -999) {
            goto cleanup;
        }

        JC_CHECK(st == JC_ERR_IO);
        (void)rmdir(sub);
        (void)rmdir(dir);
        return;
    }

cleanup:
    (void)rmdir(dir);
}

/* M642: predict appender variant. */
static void test_write_failure_predict_regular(void)
{
    const char *dir = jc_test_tmp("jichi_progress_predict_reg");
    char sub[600];
    char path[700];
    int pipefd[2];
    pid_t pid;
    int st;
    int child_status;
    struct rlimit rlim;
    int child_st; /* plain int: the -999 setrlimit marker and the child's jc_status travel as a C int; C++ will not convert int to the jc_status enum implicitly (supervisor 15:17) */

    (void)jc_test_rm_rf(dir);
    (void)mkdir(dir, 0700);
    jc_snprintf(sub, sizeof sub, "%s/.jichi", dir);
    if (!JC_REQUIRE(mkdir(sub, 0700) == 0)) {
        goto cleanup;
    }

    if (pipe(pipefd) != 0) {
        goto cleanup;
    }

    pid = fork();
    if (pid == -1) {
        (void)close(pipefd[0]);
        (void)close(pipefd[1]);
        goto cleanup;
    }

    if (pid == 0) {
        (void)close(pipefd[0]);

        signal(SIGXFSZ, SIG_IGN);

        rlim.rlim_cur = 0;
        rlim.rlim_max = 0;
        if (setrlimit(RLIMIT_FSIZE, &rlim) != 0) {
            child_st = -999;
            child_send(pipefd[1], child_st);
            _exit(0);
        }

        jc_snprintf(path, sizeof path, "%s/predictions.jsonl", sub);
        (void)remove(path);

        child_st = jc_progress_predict_append(dir, "it will pass");

        child_send(pipefd[1], child_st);
        _exit(0);
    } else {
        (void)close(pipefd[1]);
        if (read(pipefd[0], &st, sizeof(st)) != sizeof(st)) {
            (void)close(pipefd[0]);
            goto cleanup;
        }
        (void)close(pipefd[0]);

        if (waitpid(pid, &child_status, 0) == -1) {
            goto cleanup;
        }

        if (st == -999) {
            goto cleanup;
        }

        JC_CHECK(st == JC_ERR_IO);
        (void)rmdir(sub);
        (void)rmdir(dir);
        return;
    }

cleanup:
    (void)rmdir(dir);
}

void test_progress(void)
{
    test_write_failure();
    test_leaf_symlink_guard(); /* M997 */
    test_write_failure_regular();
    test_write_failure_hints_regular();
    test_write_failure_predict_regular();
    test_hints_scan();
    test_predict_scan(); /* M635 */
    test_base();
    test_scan();
    test_row();
}
