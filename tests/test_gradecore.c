/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_gradecore.c - the one grading mechanic (M529/M614). Until M614 this
 * logic was a static in main.c and had NO direct test: run_tests links the
 * library without main.o, so the mechanic every surface depends on was tested
 * only through smoke drivers. Each case here pins one leg of the refusal
 * taxonomy ("a refusal is NOT a failing grade") plus the setup/verify path. */
#include "jc_test.h"
#include "jc_gradecore.h"
#include "jc_platform.h"
#include "jc_snprintf.h"
#include "jc_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_spec(const char *path, const char *verify,
                       const char *setup)
{
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return;
    }
    fprintf(f, "---\ntitle: T\naudience: student\n");
    if (verify != NULL) {
        fprintf(f, "verify: \"%s\"\n", verify);
    }
    if (setup != NULL) {
        fprintf(f, "setup: \"%s\"\n", setup);
    }
    fprintf(f, "---\nDo the thing.\n");
    fclose(f);
}

void test_gradecore(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_grade_out g;
    char dir[256];
    char spec[512];
    char cwd[512];

    jc_snprintf(dir, sizeof dir, "%s", jc_test_tmp("jichi_gradecore"));
    { char cmd[300]; sprintf(cmd, "rm -rf %s", dir);
      if (system(cmd) != 0) { /* ignore */ } }
    JC_CHECK(jc_mkdir_p(dir) == JC_OK);
    if (getcwd(cwd, sizeof cwd) == NULL) {
        cwd[0] = '\0';
    }
    JC_CHECK(chdir(dir) == 0);

    /* UNREADABLE: no such file. */
    jc_grade_core("no_such_spec.md", a, &g);
    JC_CHECK(g.fail == JC_GRADE_UNREADABLE);
    jc_grade_out_free(&g);

    /* NO_TASK: frontmatter only, no body. */
    {
        FILE *f = fopen("notask.md", "w");
        if (f != NULL) { fputs("---\ntitle: X\n---\n", f); fclose(f); }
    }
    jc_grade_core("notask.md", a, &g);
    JC_CHECK(g.fail == JC_GRADE_NO_TASK);
    jc_grade_out_free(&g);

    /* NO_VERIFY: a task with nothing defining success. */
    jc_snprintf(spec, sizeof spec, "noverify.md");
    write_spec(spec, NULL, NULL);
    jc_grade_core(spec, a, &g);
    JC_CHECK(g.fail == JC_GRADE_NO_VERIFY);
    jc_grade_out_free(&g);

    /* CANNOT_RUN (M502): the verify PROGRAM is a path that does not resolve
     * from here -- a broken harness, not a failing grade. */
    write_spec("cannotrun.md", "sh ./no/such/grader.sh", NULL);
    jc_grade_core("cannotrun.md", a, &g);
    JC_CHECK(g.fail == JC_GRADE_CANNOT_RUN);
    JC_CHECK(strstr(g.prog, "no/such/grader.sh") != NULL);
    jc_grade_out_free(&g);

    /* PASS and FAIL, through the real shell. */
    write_spec("pass.md", "true", NULL);
    jc_grade_core("pass.md", a, &g);
    JC_CHECK(g.fail == JC_GRADE_NONE);
    JC_CHECK(g.res.passed == 1);
    JC_CHECK(g.have_rep == 1);
    jc_grade_out_free(&g);

    write_spec("fail.md", "false", NULL);
    jc_grade_core("fail.md", a, &g);
    JC_CHECK(g.fail == JC_GRADE_NONE);
    JC_CHECK(g.res.passed == 0);
    jc_grade_out_free(&g);

    /* setup runs BEFORE verify: the marker it creates satisfies the gate.
     * (A missing marker file is the assignment working, not the harness
     * broken -- `[` has no '/' so M502's probe stays out of the way.) */
    (void)remove("marker");
    write_spec("setup.md", "[ -f marker ]", "touch marker");
    jc_grade_core("setup.md", a, &g);
    JC_CHECK(g.fail == JC_GRADE_NONE);
    JC_CHECK(g.res.passed == 1);
    jc_grade_out_free(&g);

    /* M617: the missing-directory scanner behind the wrong-dir note. */
    {
        char nd[512];
        JC_CHECK(jc_mkdir_p("realdir") == JC_OK);
        /* a referenced directory that is absent from here is named */
        JC_CHECK(jc_grade_missing_dir(
            "grep -qx 'ok' docs/assignments/wd/answer.txt", nd, sizeof nd) == 1);
        JC_CHECK(strstr(nd, "docs/assignments/wd") != NULL);
        /* an existing directory raises nothing */
        JC_CHECK(jc_grade_missing_dir("grep -q x realdir/f.txt",
                                      nd, sizeof nd) == 0);
        /* no '/'-token, nothing to say */
        JC_CHECK(jc_grade_missing_dir("grep -qx ok answer.txt",
                                      nd, sizeof nd) == 0);
        /* option dashes are not paths */
        JC_CHECK(jc_grade_missing_dir("diff -u/dev/nullish a b",
                                      nd, sizeof nd) == 0);
        /* quoted paths are judged by their bytes */
        JC_CHECK(jc_grade_missing_dir("test -f \"no/such/dir/file\"",
                                      nd, sizeof nd) == 1);
        JC_CHECK(strstr(nd, "no/such/dir") != NULL);
    }

    if (cwd[0] != '\0') {
        JC_CHECK(chdir(cwd) == 0);
    }
    { char cmd[300]; sprintf(cmd, "rm -rf %s", dir);
      if (system(cmd) != 0) { /* ignore */ } }
    jc_arena_free(a);
}
