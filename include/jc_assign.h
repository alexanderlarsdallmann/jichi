/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_assign.h - machine-checkable assignment specs (M103).
 *
 * An assignment spec is a markdown file with YAML frontmatter. It is at once a
 * *tutoring task* (human-readable, audience-framed), a *machine-checkable eval*
 * (a `verify` command whose pass/fail + test counts define success), and the
 * *rehearsal target* the self-improvement loop scores itself against. This
 * header is the pure core: parse a spec, render it for an audience, and score a
 * test report. The `assign`/`grade` subcommand shells live in main.c.
 *
 * Frontmatter keys: title, audience (junior|student|senior|agent), verify
 * (command, required to grade), setup (optional reset command), points
 * (optional rubric weight), phase (SDLC phase, e.g. implementation|testing),
 * difficulty (free-form tier, e.g. intro|easy|medium|hard), hints (an optional
 * block-sequence "ladder" of graded nudges: nudge -> approach -> worked step,
 * revealed on demand by the `hint` tool while solving). The body is the task
 * description. `phase`/`difficulty` were documented long before they were
 * parsed; the curriculum's listing needs them (C4, M174).
 */
#ifndef JC_ASSIGN_H
#define JC_ASSIGN_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_testparse.h"

struct jc_assign_spec {
    const char *title;     /* headline, or NULL                        */
    const char *audience;  /* junior|student|senior|agent, or NULL     */
    const char *verify;    /* command whose result defines success     */
    const char *setup;     /* optional pre-attempt reset command       */
    const char *phase;     /* SDLC phase, or NULL                      */
    const char *difficulty;/* difficulty tier, or NULL                 */
    int         points;    /* rubric weight (0 = unset)                */
    const char *task;      /* the body (task description)              */
    const char **hints;    /* graded hint ladder (arena array), or NULL */
    int          nhints;   /* number of hints                          */
    int          hints_skipped; /* M409: `hints:` entries the YAML subset
                                 * could not read as a scalar (M289 skips
                                 * them so no rung is ever empty) -- counted
                                 * so surfaces can SAY the ladder is shorter
                                 * than the file, instead of presenting the
                                 * truncation as a fact about the assignment */
};

/* Parse a spec from markdown+frontmatter (arena-owned strings). Always JC_OK
 * for well-formed markdown; JC_ERR_INVALID if there is no task body at all. */
/* M502: the PROGRAM or SCRIPT a `verify` command would run -- the first token,
 * or the script argument when that token is an interpreter (`sh t.sh`,
 * `python3 grade.py`). Writes it into `buf` and returns `buf`, or NULL when the
 * command's shape makes it unknowable (a pipeline, a subshell, `sh -c ...`, an
 * assignment prefix).
 *
 * WHY THIS EXISTS. `grade` ran the command and reported the verdict, so a spec
 * graded from the WRONG DIRECTORY printed `FAIL / score: 0%` -- measured: the
 * shell could not open the script and exited 2, and the learner saw a failing
 * grade on correct work. The same hole made `--expect-fail` worse than useless:
 * a gate that is "red" because its script is missing is exactly the hollow gate
 * that check exists to catch, and it reported RED AS EXPECTED.
 *
 * Only the program is examined, never the arguments, and that distinction is
 * load-bearing: a verify like `test -f docs/DESIGN.md` names a file the LEARNER
 * is supposed to create, so a missing argument is the assignment working, while
 * a missing program is the harness broken.
 *
 * Pure: string handling only, no allocation, no filesystem. */
const char *jc_assign_verify_program(const char *cmd, char *buf, jc_size cap);

/* M529: is `name` usable as an assignment identifier ARRIVING FROM OUTSIDE --
 * a daemon request, an HTTP caller, anything not already trusted? Returns 1 for
 * acceptable, 0 for refused.
 *
 * WHY A NAME AND NOT A PATH. The `assignment` verbs let a caller say which spec
 * to read or grade, and grading RUNS THE SPEC'S `verify` COMMAND. A path would
 * make that an arbitrary-file-read and, through a spec the caller wrote, an
 * arbitrary-command-execution convenience -- so the wire names a member of the
 * set (`docs/assignments/<name>`) and the server resolves it. The caller cannot
 * express a location.
 *
 * Refused: anything containing '/' or '\\' (no directories, no absolute paths),
 * any "..", a leading '.', an empty name, and anything not ending in ".md".
 * `..` is refused explicitly as well as by the '/' rule, because a name is also
 * used to build sibling paths (`<base>.solution.md`) and a rule that only holds
 * for one of them is a rule that will be got wrong later.
 *
 * Pure: string handling only, no filesystem -- so the refusals are testable
 * without planting files to escape to. */
int jc_assign_name_ok(const char *name);

jc_status jc_assign_parse(const char *text, struct jc_assign_spec *out,
                          struct jc_arena *a);

struct jc_assign_result {
    int passed;        /* 1 iff the verify command succeeded (exit 0)  */
    int pct;           /* 0-100 score from test counts, else 0/100     */
    int tests_run;     /* observed test count, or 0                     */
    int tests_failed;  /* observed failures, or 0                       */
};

/* Score a (possibly NULL) test report + the verify command's success into a
 * result. Pure. */
void jc_assign_score(const struct jc_test_report *rep, int verify_ok,
                     struct jc_assign_result *out);

/* Render the assignment as markdown framed for its audience (arena-owned):
 * junior gets a step scaffold + hints, student gets learning objectives,
 * senior gets a terse brief, agent gets a machine-checkable spec that names the
 * verify command. Unknown/NULL audience defaults to the student framing. */
char *jc_assign_render(const struct jc_assign_spec *spec, struct jc_arena *a);

/* M410: "PASS" / "FAIL" / "TAINTED" -- TAINTED = verify green but the run
 * modified test assertions (envelope test_edits > 0), so green is not
 * evidence. Pure. */
const char *jc_assign_attempt_verdict(int passed, int test_edits);

#ifdef __cplusplus
}
#endif
#endif /* JC_ASSIGN_H */
