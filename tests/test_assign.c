/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_assign.c - assignment spec parse / render / score (M103). */

#include "jc_test.h"
#include "jc_assign.h"
#include "jc_testparse.h"
#include "jc_mem.h"
#include <string.h>

static const char *SPEC =
    "---\n"
    "title: Validate parse_config\n"
    "audience: agent\n"
    "verify: make test\n"
    "setup: git checkout -- .\n"
    "points: 10\n"
    "phase: implementation\n"
    "difficulty: easy\n"
    "---\n"
    "Make parse_config reject malformed input instead of crashing.\n";

static void test_parse_and_render(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_assign_spec spec;
    char *md;

    JC_CHECK(jc_assign_parse(SPEC, &spec, a) == JC_OK);
    JC_CHECK_STR(spec.title, "Validate parse_config");
    JC_CHECK_STR(spec.audience, "agent");
    JC_CHECK_STR(spec.verify, "make test");
    JC_CHECK_STR(spec.setup, "git checkout -- .");
    JC_CHECK(spec.points == 10);
    /* phase/difficulty were documented long before they were parsed (C4,
     * M174); the curriculum listing keys off them. */
    JC_CHECK_STR(spec.phase, "implementation");
    JC_CHECK_STR(spec.difficulty, "easy");
    JC_CHECK(strstr(spec.task, "parse_config") != NULL);

    /* Agent framing exposes the verify command as the success criterion. */
    md = jc_assign_render(&spec, a);
    JC_CHECK(strstr(md, "machine-checkable") != NULL);
    JC_CHECK(strstr(md, "make test") != NULL);

    jc_arena_free(a);
}

static void test_render_audiences(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_assign_spec spec;
    char *md;

    memset(&spec, 0, sizeof(spec));
    spec.title = "T";
    spec.task = "Do the thing.";
    spec.verify = "ctest";

    spec.audience = "junior";
    md = jc_assign_render(&spec, a);
    JC_CHECK(strstr(md, "Suggested steps") != NULL);

    spec.audience = "student";
    md = jc_assign_render(&spec, a);
    JC_CHECK(strstr(md, "Learning goals") != NULL);

    spec.audience = NULL; /* defaults to student framing */
    md = jc_assign_render(&spec, a);
    JC_CHECK(strstr(md, "Objective") != NULL);

    /* M613: the hint-availability note must arrive WHOLE. It was staged
     * through a char[192] and jc_snprintf truncates silently, so every
     * student/junior brief with hints ended mid-word ("...or delega") --
     * 79 of 79 shipped specs, and the exact text `attempt` hands the
     * solver. Substring checks above could never see it; these pin the
     * final sentence of both framings. */
    {
        static const char *H[3] = { "one", "two", "three" };
        spec.hints = H;
        spec.nhints = 3;
        spec.audience = "student";
        md = jc_assign_render(&spec, a);
        JC_CHECK(strstr(md, "delegate a sub-part.\n") != NULL);
        JC_CHECK(strstr(md, "their use is recorded") != NULL);
        spec.audience = "senior";
        md = jc_assign_render(&spec, a);
        JC_CHECK(strstr(md, "3 hint(s) available via the `hint` tool.\n")
                 != NULL);
        /* M618: a CLEAN ladder renders no shortfall note. */
        JC_CHECK(strstr(md, "could not be read") == NULL);
        spec.hints = NULL;
        spec.nhints = 0;
    }

    jc_arena_free(a);
}

static const char *SPEC_HINTS =
    "---\n"
    "title: Hint ladder\n"
    "audience: junior\n"
    "verify: make test\n"
    "hints:\n"
    "  - Look at the parser first.\n"
    "  - Consider the empty-input case.\n"
    "  - Add a length check before the loop.\n"
    "---\n"
    "Implement the thing.\n";

/* M289: an entry with no readable text is NOT a hint. It used to be recorded as
 * "" and still counted, so the ladder advertised hints it could not give: a real
 * run shows `hint` answering "Hint 1 of 4:" with an empty body (59 bytes -- header
 * and footer only), twice out of four. Skipping keeps nhints equal to the number
 * of hints that exist, whether the blank came from the source or from a YAML form
 * this subset cannot read. */
static void test_hints_skip_empty(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_assign_spec spec;
    static const char *SPEC =
        "---\n"
        "title: Ragged ladder\n"
        "audience: junior\n"
        "verify: make test\n"
        "hints:\n"
        "  - \n"
        "  - Bind the four methods on Node.\n"
        "  - Return a copy, not the live list.\n"
        "  -    \n"
        "---\n"
        "Implement the thing.\n";

    JC_CHECK(jc_assign_parse(SPEC, &spec, a) == JC_OK);
    /* Four entries, two of them blank => two usable hints. */
    JC_CHECK(spec.nhints == 2);
    JC_CHECK(spec.hints != NULL);
    /* M618: the shortfall reaches the BRIEF, not only run_hint's stderr. */
    {
        char *md = jc_assign_render(&spec, a);
        JC_CHECK(strstr(md, "could not be read") != NULL);
    }
    /* The survivors keep their order and content, and none is empty. */
    JC_CHECK(strstr(spec.hints[0], "Bind the four methods") != NULL);
    JC_CHECK(strstr(spec.hints[1], "Return a copy") != NULL);
    JC_CHECK(spec.hints[0][0] != '\0');
    JC_CHECK(spec.hints[1][0] != '\0');
    /* And the render advertises the honest count, not the entry count. */
    {
        char *md = jc_assign_render(&spec, a);
        JC_CHECK(md != NULL && strstr(md, "2 hint(s)") != NULL);
    }
    jc_arena_free(a);
}

static void test_hints(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_assign_spec spec;
    char *md;

    /* A `hints:` block sequence parses into the graded ladder. */
    JC_CHECK(jc_assign_parse(SPEC_HINTS, &spec, a) == JC_OK);
    JC_CHECK(spec.nhints == 3);
    JC_CHECK(spec.hints != NULL);
    JC_CHECK(strstr(spec.hints[0], "parser") != NULL);
    JC_CHECK(strstr(spec.hints[2], "length check") != NULL);

    /* The render advertises availability (not the content). */
    md = jc_assign_render(&spec, a);
    JC_CHECK(strstr(md, "3 hint(s)") != NULL);
    JC_CHECK(strstr(md, "Stuck?") != NULL);
    JC_CHECK(strstr(md, "length check") == NULL); /* content NOT rendered */

    /* No hints => no ladder, no advertisement. */
    JC_CHECK(jc_assign_parse(SPEC, &spec, a) == JC_OK);
    JC_CHECK(spec.nhints == 0 && spec.hints == NULL);
    md = jc_assign_render(&spec, a);
    JC_CHECK(strstr(md, "hint(s)") == NULL);

    jc_arena_free(a);
}

static void test_score(void)
{
    struct jc_test_report rep;
    struct jc_assign_result res;

    /* No report + failing verify => 0%, not passed. */
    jc_assign_score(NULL, 0, &res);
    JC_CHECK(res.passed == 0 && res.pct == 0);

    /* No report + passing verify => 100%. */
    jc_assign_score(NULL, 1, &res);
    JC_CHECK(res.passed == 1 && res.pct == 100);

    /* A report with counts scales the percentage. */
    jc_test_report_init(&rep);
    rep.total = 10;
    rep.passed = 7;
    rep.failed = 3;
    jc_assign_score(&rep, 0, &res);
    JC_CHECK(res.pct == 70);
    JC_CHECK(res.tests_failed == 3);
    JC_CHECK(res.passed == 0); /* verify exit != 0 */
    jc_test_report_free(&rep);
}

/* M410, born red: a green verify with goalpost edits must NOT read PASS. */
static void test_attempt_verdict(void)
{
    JC_CHECK_STR(jc_assign_attempt_verdict(1, 0), "PASS");
    JC_CHECK_STR(jc_assign_attempt_verdict(0, 0), "FAIL");
    JC_CHECK_STR(jc_assign_attempt_verdict(0, 7), "FAIL");
    JC_CHECK_STR(jc_assign_attempt_verdict(1, 1), "TAINTED");
    JC_CHECK_STR(jc_assign_attempt_verdict(1, 10), "TAINTED");
}

/* ---- M502: which program would this verify command run? ---------------------
 *
 * The defect: `grade` ran the command and reported the verdict, so grading from
 * the wrong directory printed FAIL / score 0% -- a failing grade on correct work
 * -- and `--expect-fail` reported "RED as expected" for a gate that was red only
 * because its script was missing, certifying the hollow gate it exists to catch.
 *
 * The load-bearing distinction these pin: the PROGRAM, never the arguments. A
 * verify like `test -f docs/DESIGN.md` names a file the LEARNER must create, so
 * treating a missing argument as a broken harness would break every
 * deliverable-shaped assignment in the process track. */
static void test_verify_program(void)
{
    char b[256];

    /* An interpreter hands off to its script. */
    JC_CHECK_STR(jc_assign_verify_program("sh docs/a/test.sh", b, sizeof b),
                 "docs/a/test.sh");
    JC_CHECK_STR(jc_assign_verify_program("python3 grade/run.py", b, sizeof b),
                 "grade/run.py");
    JC_CHECK_STR(jc_assign_verify_program("/bin/sh t/x.sh", b, sizeof b),
                 "t/x.sh");
    /* A non-interpreter IS the program. */
    JC_CHECK_STR(jc_assign_verify_program("./gradlew test", b, sizeof b),
                 "./gradlew");
    JC_CHECK_STR(jc_assign_verify_program("make check", b, sizeof b), "make");
    /* Arguments are never the answer -- this is the deliverable case. */
    JC_CHECK_STR(jc_assign_verify_program("test -s docs/README.md", b, sizeof b),
                 "test");
    JC_CHECK_STR(jc_assign_verify_program("grep -q TODO notes.md", b, sizeof b),
                 "grep");

    /* Shapes whose program cannot be read off the front: NULL, so the caller
     * runs the command as before rather than guessing. Being unsure must cost
     * nothing -- a false "the harness is missing" would refuse a valid grade. */
    JC_CHECK(jc_assign_verify_program("sh -c 'cd sub && make'", b, sizeof b) == NULL);
    JC_CHECK(jc_assign_verify_program("(cd sub && make)", b, sizeof b) == NULL);
    JC_CHECK(jc_assign_verify_program("CC=clang make check", b, sizeof b) == NULL);
    JC_CHECK(jc_assign_verify_program("$TESTCMD", b, sizeof b) == NULL);
    JC_CHECK(jc_assign_verify_program("\"my script.sh\"", b, sizeof b) == NULL);
    JC_CHECK(jc_assign_verify_program("", b, sizeof b) == NULL);
    JC_CHECK(jc_assign_verify_program(NULL, b, sizeof b) == NULL);
    JC_CHECK(jc_assign_verify_program("make", NULL, sizeof b) == NULL);
    /* A token longer than the buffer is unknowable rather than truncated: a
     * truncated path would not exist, and would be reported as a broken
     * harness. */
    {
        char tiny[6];
        JC_CHECK(jc_assign_verify_program("sh docs/a/test.sh", tiny,
                                          sizeof tiny) == NULL);
    }
}

void test_assign(void)
{
    test_verify_program();
    test_attempt_verdict();
    test_parse_and_render();
    test_render_audiences();
    test_hints();
    test_hints_skip_empty();
    test_score();
}

/* M529: the boundary for a name that arrived from outside. The `assignment`
 * daemon verbs accept a NAME, never a path, because grading runs the spec's own
 * `verify` command -- so a caller who could name a location could name a file
 * they wrote. These are the refusals, pure, with no files planted to escape to. */
void test_assign_name_ok(void)
{
    /* Accepted: a plain member of the set. */
    JC_CHECK(jc_assign_name_ok("01-hello.md") == 1);
    JC_CHECK(jc_assign_name_ok("a.md") == 1);
    JC_CHECK(jc_assign_name_ok("with-dashes_and_underscores.md") == 1);
    JC_CHECK(jc_assign_name_ok("UPPER.md") == 1);

    /* Refused: any way of naming a location. */
    JC_CHECK(jc_assign_name_ok("../secrets.md") == 0);
    JC_CHECK(jc_assign_name_ok("../../etc/passwd") == 0);
    JC_CHECK(jc_assign_name_ok("/etc/passwd") == 0);
    JC_CHECK(jc_assign_name_ok("sub/dir.md") == 0);
    JC_CHECK(jc_assign_name_ok("back\\slash.md") == 0);
    JC_CHECK(jc_assign_name_ok("a/../b.md") == 0);
    /* ".." with no slash at all -- refused explicitly, because the name is also
     * used to build a sibling path where a bare ".." would still traverse. */
    JC_CHECK(jc_assign_name_ok("..") == 0);
    JC_CHECK(jc_assign_name_ok("..md") == 0);
    /* A leading dot: no dotfiles, and no way to reach ".jichi". */
    JC_CHECK(jc_assign_name_ok(".hidden.md") == 0);

    /* Refused: not a spec at all. */
    JC_CHECK(jc_assign_name_ok("") == 0);
    JC_CHECK(jc_assign_name_ok(NULL) == 0);
    JC_CHECK(jc_assign_name_ok("noext") == 0);
    JC_CHECK(jc_assign_name_ok("wrong.txt") == 0);
    JC_CHECK(jc_assign_name_ok(".md") == 0);       /* extension only */
}
