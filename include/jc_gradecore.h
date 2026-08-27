/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_gradecore.h - THE grading mechanic (M529, moved out of main.c at M614).
 *
 * `grade`, `improve`, the daemon's `assignment.grade` verb and the TUI's
 * `/grade` all have to answer the same question, and a grade that differs
 * between surfaces is not a grade. M529 unified the first three inside main.c;
 * the TUI -- the surface a learner actually types at -- was the fourth caller
 * and got missed: it re-implemented setup+verify+score without the M502
 * reachability guard and recorded the result unconditionally, so a verify that
 * could not run from here wrote FAIL 0%% into .jichi/progress.jsonl as if the
 * work were wrong. Moving the mechanic into its own translation unit is what
 * lets jc_tui.c link it (main.c's statics are not linkable from the library
 * objects, and run_tests links the library without main.o).
 *
 * A refusal is NOT a failing grade. JC_GRADE_CANNOT_RUN in particular means
 * "this is not a grade" (M502's own words); callers must not score it as a
 * fail, and must not record it. */
#ifndef JC_GRADECORE_H
#define JC_GRADECORE_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_assign.h"
#include "jc_testparse.h"

enum jc_grade_fail {
    JC_GRADE_NONE = 0,
    JC_GRADE_UNREADABLE,   /* the spec could not be read                     */
    JC_GRADE_NO_TASK,      /* no task body -- not an assignment              */
    JC_GRADE_NO_VERIFY,    /* no `verify` command, so nothing defines success */
    JC_GRADE_CANNOT_RUN    /* M502: the verify program is unreachable here   */
};

struct jc_grade_out {
    struct jc_assign_spec   spec;
    struct jc_assign_result res;
    /* M614: the parsed test report is handed OUT (it used to be freed inside),
     * because the TUI prints the first failure messages. Owned by the caller:
     * jc_grade_out_free releases it. Valid only when have_rep is set. */
    struct jc_test_report   rep;
    int   have_rep;
    int   verify_exit;
    enum  jc_grade_fail fail;
    char  prog[512];      /* JC_GRADE_CANNOT_RUN: the program that is missing */
    /* M617: on a FAIL, a directory the verify command references that does not
     * exist from the grading directory ("" when none). M502's program guard
     * fires only when the program carries '/'; 12 shipped specs -- the intro
     * and plain-language tiers -- use `grep`/`[`, so a wrong-directory grade
     * read as a real FAIL to exactly the learners least able to tell a broken
     * grader from their own mistake. Arguments stay uninspected for the
     * VERDICT (a missing file may be the deliverable); this is a NOTE beside
     * an already-failed grade, never a refusal, and the wording must say a
     * missing directory can also be part of the task. */
    char  miss_dir[512];
    char *text;           /* the spec source (arena), for callers that warn on it */
};

/* Read + parse the spec at `path`, run its `setup` then `verify` via the shell
 * in the CURRENT directory, and score the outcome. Strings land on `arena`.
 * Always fills *o; check o->fail before reading o->res. */
void jc_grade_core(const char *path, struct jc_arena *arena,
                   struct jc_grade_out *o);

/* Release what jc_grade_core allocated outside the arena (the test report). */
void jc_grade_out_free(struct jc_grade_out *o);

/* M617: scan `verify` for a whitespace-separated token containing '/', and
 * report the first whose DIRECTORY part does not exist from the current
 * directory. Leading quotes and option dashes are skipped; the dirname lands
 * in `buf`. Returns 1 when one was found, else 0. Filesystem reads only. */
int jc_grade_missing_dir(const char *verify, char *buf, jc_size cap);

#ifdef __cplusplus
}
#endif
#endif /* JC_GRADECORE_H */
