/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_toolloop.c - the in-turn tool-call loop detector's pure cores (M432), and
 * its success twin (plan D1, M733). */

#include "jc_test.h"
#include "jc_toolloop.h"
#include "jc_snprintf.h"
#include <string.h>

void test_toolloop(void)
{
    struct jc_toolloop w;
    int n;
    char buf[600];
    int i;

    /* --- the classifier ----------------------------------------------------
     * ORDER IS THE POINT. A denial and a kill both usually quote the path or the
     * command they refused, so testing not-found first would misfile every fence
     * denial as a missing file -- and "the file does not exist" is the wrong advice
     * for "you may not write there". A wrong cause is a loop AMPLIFIER (M342), so
     * these cases are the ones worth pinning. */
    /* Non-shell tool results (edit_file): full result is jichi's message, so
     * substring matching applies. policy_refusal=0 for existing behavior. */
    JC_CHECK(jc_fail_classify(
        "Path src/x.c is outside this run's --edit-scope; the edit was refused.",
        -1, "edit_file", 0) == JC_FAIL_DENIED);
    JC_CHECK(jc_fail_classify(
        "Tool requires approval, unavailable in headless mode", -1, "write_file", 0)
        == JC_FAIL_DENIED);
    /* A kill that quotes a command containing a path is still a kill. */
    JC_CHECK(jc_fail_classify(
        "\n[stopped: command timed out after 60s and was terminated]\n", 124,
        "run_terminal_command", 0) == JC_FAIL_KILLED);
    JC_CHECK(jc_fail_classify(
        "[stopped: output exceeded the 65536-byte capture limit and the command "
        "was killed]", 137, "run_terminal_command", 0) == JC_FAIL_KILLED);

    JC_CHECK(jc_fail_classify("error: string not found in file", -1, "read_file", 0)
             == JC_FAIL_NOT_FOUND);
    JC_CHECK(jc_fail_classify("no such file or directory: build.zig", -1, "read_file", 0)
             == JC_FAIL_NOT_FOUND);
    /* jichi's OWN missing-file wording, which the first cut of the classifier
     * missed -- so a plain missing file was filed OTHER and got generic advice. */
    JC_CHECK(jc_fail_classify("error: could not open 'sub/missing.txt'", -1, "read_file", 0)
             == JC_FAIL_NOT_FOUND);
    /* ...but the fence denial, which M291 split OUT of that wording, stays DENIED. */
    JC_CHECK(jc_fail_classify(
        "error: refused by safety fence (path outside workspace and any "
        "referenceRoots): 'nope/x'", -1, "write_file", 0) == JC_FAIL_DENIED);
    JC_CHECK(jc_fail_classify("error: required parameter 'path' missing", -1, "write_file", 0)
             == JC_FAIL_BAD_ARGS);
    JC_CHECK(jc_fail_classify("invalid JSON in arguments", -1, "write_file", 0)
             == JC_FAIL_BAD_ARGS);

    /* A command that simply returned non-zero, with nothing else to go on. */
    JC_CHECK(jc_fail_classify("zig build\ncompilation aborted\n", 1, "run_terminal_command", 0)
             == JC_FAIL_NONZERO_EXIT);
    /* Nothing recognisable and no exit code: OTHER, so the note says less rather
     * than inventing a cause. */
    JC_CHECK(jc_fail_classify("something went sideways", -1, "read_file", 0) == JC_FAIL_OTHER);
    JC_CHECK(jc_fail_classify(NULL, -1, NULL, 0) == JC_FAIL_OTHER);
    JC_CHECK(jc_fail_classify("", 3, NULL, 0) == JC_FAIL_NONZERO_EXIT);
    /* Case-insensitive: strcasestr is not C89, so the walk is hand-rolled and
     * deserves a check. */
    JC_CHECK(jc_fail_classify("ERROR: No Such File", -1, "read_file", 0) == JC_FAIL_NOT_FOUND);

    JC_CHECK(strcmp(jc_fail_class_name(JC_FAIL_DENIED), "denied") == 0);
    JC_CHECK(strcmp(jc_fail_class_name(JC_FAIL_OTHER), "other") == 0);

    /* --- M1041-b: policy_refusal flag classification ---------------------
     * The executor sets policy_refusal=1 for fence refusals (read-only, main-agent-only).
     * If the flag is set, jc_fail_classify returns DENIED regardless of wording. */
    /* The read-only refusal message with policy_refusal=1 must be DENIED. */
    JC_CHECK(jc_fail_classify(
        "error: tool disabled in read-only mode -- this run may only read and report; "
        "put your findings in your final answer instead of editing",
        -1, "run_terminal_command", 1) == JC_FAIL_DENIED);
    /* The same message with policy_refusal=0 falls back to the wording rules.
     * The result BEGINS with "error: tool disabled in read-only mode", which is
     * in the strict shell tool DENIED list, so it still classifies DENIED.
     * This preserves the old behavior for cases where the flag isn't set. */
    JC_CHECK(jc_fail_classify(
        "error: tool disabled in read-only mode -- this run may only read and report; "
        "put your findings in your final answer instead of editing",
        -1, "run_terminal_command", 0) == JC_FAIL_DENIED);
    /* A message that matches NO wording rule but has policy_refusal=1 must be DENIED.
     * This pins the flag path: "x" doesn't match any denial phrase. */
    JC_CHECK(jc_fail_classify("x", 1, "run_terminal_command", 1) == JC_FAIL_DENIED);

    /* --- the shell tool misclassification fix (card 75) --------------------
     * A shell command whose OUTPUT contains "connection refused"/"denied"/
     * "forbidden" etc. and exits nonzero is a failed command (NONZERO_EXIT),
     * NOT a policy refusal. The substring match was firing anywhere in the
     * result text. policy_refusal=0 for existing behavior. */
    /* RED today: "connection refused" from the command output is misfiled as DENIED */
    /* This must be NONZERO_EXIT, not DENIED: the command ran and failed, but
     * it was not a policy refusal. */
    JC_CHECK(jc_fail_classify("Connection refused by the server", 1,
                              "run_terminal_command", 0) == JC_FAIL_NONZERO_EXIT);
    /* "access denied" in command output should also be NONZERO_EXIT */
    JC_CHECK(jc_fail_classify("Access denied: insufficient permissions", 1,
                              "run_terminal_command", 0) == JC_FAIL_NONZERO_EXIT);
    /* "forbidden" in command output should also be NONZERO_EXIT */
    JC_CHECK(jc_fail_classify("403 Forbidden", 1, "run_terminal_command", 0)
             == JC_FAIL_NONZERO_EXIT);

    /* A fence refusal with the exact phrase at the START is still DENIED */
    JC_CHECK(jc_fail_classify("error: refused by safety fence (path outside workspace)",
                              -1, "run_terminal_command", 0) == JC_FAIL_DENIED);
    /* The result is NOT the command output, it's jichi's refusal message */
    JC_CHECK(jc_fail_classify("Path src/x.c is outside this run's --edit-scope; "
                              "the edit was refused. Writable paths are: src/",
                              -1, "run_terminal_command", 0) == JC_FAIL_DENIED);
    /* "outside this run's" at the start (after "Path") is DENIED */
    JC_CHECK(jc_fail_classify("Path /outside is outside this run's --edit-scope",
                              -1, "run_terminal_command", 0) == JC_FAIL_DENIED);
    /* "refused:" at the start (after trimming) is DENIED */
    JC_CHECK(jc_fail_classify("refused: command is outside this run's scope",
                              -1, "run_terminal_command", 0) == JC_FAIL_DENIED);

    /* --- the EXACT key, threshold 3 ---------------------------------------- */
    jc_toolloop_init(&w);
    JC_CHECK(jc_toolloop_note(&w, "apply_patch", "AAA", JC_FAIL_NOT_FOUND, &n)
             == JC_TOOLLOOP_NONE);
    JC_CHECK(jc_toolloop_note(&w, "apply_patch", "AAA", JC_FAIL_NOT_FOUND, &n)
             == JC_TOOLLOOP_NONE);
    /* The third identical failure is the first one told. Two is a legitimate
     * retry -- and the 2x tail is thick (78 turns on chrtext), which is why the
     * threshold is 3 and not 2. */
    JC_CHECK(jc_toolloop_note(&w, "apply_patch", "AAA", JC_FAIL_NOT_FOUND, &n)
             == JC_TOOLLOOP_EXACT);
    JC_CHECK(n == 3);
    /* ONE NOTE PER LOOP, not one per key. The fourth identical failure must be
     * SILENT: without suppressing the class slot too it would cross the class
     * threshold (4) and tell the model a second time about the same tool, same
     * cause, same arguments -- the M323 nag arriving by a side door. This assertion
     * failed on the module's first run and is the reason the code suppresses both. */
    JC_CHECK(jc_toolloop_note(&w, "apply_patch", "AAA", JC_FAIL_NOT_FOUND, &n)
             == JC_TOOLLOOP_NONE);
    JC_CHECK(jc_toolloop_note(&w, "apply_patch", "AAA", JC_FAIL_NOT_FOUND, &n)
             == JC_TOOLLOOP_NONE);
    /* But the SAME tool failing for a DIFFERENT reason is a different finding and
     * must still be able to fire -- suppression is per (tool, class), not per tool. */
    for (i = 0; i < 3; i++) {
        (void)jc_toolloop_note(&w, "apply_patch", "BBB", JC_FAIL_BAD_ARGS, &n);
    }
    JC_CHECK(n == 3);

    /* A DIFFERENT tool with the same arguments is a different pair. */
    jc_toolloop_init(&w);
    JC_CHECK(jc_toolloop_note(&w, "edit_file", "X", JC_FAIL_OTHER, &n)
             == JC_TOOLLOOP_NONE);
    JC_CHECK(jc_toolloop_note(&w, "write_file", "X", JC_FAIL_OTHER, &n)
             == JC_TOOLLOOP_NONE);
    JC_CHECK(jc_toolloop_note(&w, "edit_file", "X", JC_FAIL_OTHER, &n)
             == JC_TOOLLOOP_NONE);
    JC_CHECK(jc_toolloop_note(&w, "edit_file", "X", JC_FAIL_OTHER, &n)
             == JC_TOOLLOOP_EXACT && n == 3);

    /* --- the CLASS key, threshold 4: the varied-argument loop --------------
     * This is the shape the exact key CANNOT see, and the reason both keys exist:
     * the 2026-08-09 loop varied one constant (`git log -N` escalating 300 ->
     * 20,000,000), so an exact key scored ZERO over logs holding it 96 times. */
    jc_toolloop_init(&w);
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", "git log -n 300",
                              JC_FAIL_KILLED, &n) == JC_TOOLLOOP_NONE);
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", "git log -n 3000",
                              JC_FAIL_KILLED, &n) == JC_TOOLLOOP_NONE);
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", "git log -n 30000",
                              JC_FAIL_KILLED, &n) == JC_TOOLLOOP_NONE);
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", "git log -n 300000",
                              JC_FAIL_KILLED, &n) == JC_TOOLLOOP_CLASS);
    JC_CHECK(n == 4);
    /* Told once. */
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", "git log -n 3000000",
                              JC_FAIL_KILLED, &n) == JC_TOOLLOOP_NONE);

    /* A varied loop whose CLASS also varies never trips: four different causes is
     * four different problems, not a loop. */
    jc_toolloop_init(&w);
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", "a", JC_FAIL_KILLED, &n)
             == JC_TOOLLOOP_NONE);
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", "b", JC_FAIL_NOT_FOUND, &n)
             == JC_TOOLLOOP_NONE);
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", "c", JC_FAIL_BAD_ARGS, &n)
             == JC_TOOLLOOP_NONE);
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", "d", JC_FAIL_OTHER, &n)
             == JC_TOOLLOOP_NONE);

    /* EXACT outranks CLASS when both would trip on the same call: it is the more
     * specific finding and earns the stronger advice. */
    jc_toolloop_init(&w);
    for (i = 0; i < 2; i++) {
        JC_CHECK(jc_toolloop_note(&w, "t", "SAME", JC_FAIL_KILLED, &n)
                 == JC_TOOLLOOP_NONE);
    }
    JC_CHECK(jc_toolloop_note(&w, "t", "SAME", JC_FAIL_KILLED, &n)
             == JC_TOOLLOOP_EXACT);

    /* Degenerate input must not crash or fire. */
    JC_CHECK(jc_toolloop_note(NULL, "t", "a", JC_FAIL_OTHER, &n)
             == JC_TOOLLOOP_NONE);
    jc_toolloop_init(&w);
    JC_CHECK(jc_toolloop_note(&w, NULL, "a", JC_FAIL_OTHER, &n)
             == JC_TOOLLOOP_NONE);
    JC_CHECK(jc_toolloop_note(&w, "", "a", JC_FAIL_OTHER, &n)
             == JC_TOOLLOOP_NONE);
    /* NULL args is an empty exact key, not a crash. */
    jc_toolloop_init(&w);
    for (i = 0; i < 3; i++) {
        (void)jc_toolloop_note(&w, "t", NULL, JC_FAIL_OTHER, &n);
    }
    JC_CHECK(n == 3);

    /* The table is BOUNDED: past its capacity new pairs stop being tracked rather
     * than allocating in the tool loop. A bounded miss is the accepted trade. */
    jc_toolloop_init(&w);
    for (i = 0; i < JC_TOOLLOOP_MAX_ENTRIES + 10; i++) {
        char t[32];
        jc_snprintf(t, sizeof t, "tool%d", i);
        (void)jc_toolloop_note(&w, t, "x", JC_FAIL_OTHER, &n);
    }
    JC_CHECK(w.n <= JC_TOOLLOOP_MAX_ENTRIES);

    /* --- the note ----------------------------------------------------------- */
    jc_toolloop_render(JC_TOOLLOOP_NONE, "t", JC_FAIL_OTHER, 3, buf, sizeof buf);
    JC_CHECK(buf[0] == '\0');   /* nothing to say when nothing tripped */

    jc_toolloop_render(JC_TOOLLOOP_EXACT, "apply_patch", JC_FAIL_NOT_FOUND, 3,
                       buf, sizeof buf);
    JC_CHECK(strstr(buf, "apply_patch") != NULL);
    JC_CHECK(strstr(buf, "SAME arguments") != NULL);
    /* Counted, not ordinal: "3th" is what a naive %d + "th" prints. */
    JC_CHECK(strstr(buf, "failed 3 times") != NULL);
    JC_CHECK(strstr(buf, "3th") == NULL);
    /* Per-class advice, and it must name the TRUE cause: for not_found, look up
     * the real name -- not "try a different fix", which is M89's verify advice and
     * wrong here. */
    JC_CHECK(strstr(buf, "does not exist as written") != NULL);

    jc_toolloop_render(JC_TOOLLOOP_CLASS, "run_terminal_command", JC_FAIL_KILLED,
                       4, buf, sizeof buf);
    JC_CHECK(strstr(buf, "different arguments") != NULL);
    JC_CHECK(strstr(buf, "killed") != NULL);
    /* The killed advice must say NARROW, not retry -- a bigger version of a call
     * killed on a cap gets killed too. */
    JC_CHECK(strstr(buf, "narrow it") != NULL);

    /* DENIED must carry M429's inversion: not "try a different fix" but "it will
     * not succeed by rephrasing". Getting this backwards is what cost one run its
     * entire 150k token budget. */
    jc_toolloop_render(JC_TOOLLOOP_EXACT, "write_file", JC_FAIL_DENIED, 3,
                       buf, sizeof buf);
    JC_CHECK(strstr(buf, "not succeed by rephrasing") != NULL);
    JC_CHECK(strstr(buf, "try a different fix") == NULL);

    /* OTHER says less rather than guessing a cause. */
    jc_toolloop_render(JC_TOOLLOOP_EXACT, "t", JC_FAIL_OTHER, 3, buf, sizeof buf);
    JC_CHECK(strstr(buf, "change the approach") != NULL);

    /* A NULL tool name must not print "(null)" -- C89 undefined behaviour, and the
     * M296 defect that printed "(null) (jlu/...)" to a user. */
    jc_toolloop_render(JC_TOOLLOOP_EXACT, NULL, JC_FAIL_OTHER, 3, buf, sizeof buf);
    JC_CHECK(strstr(buf, "(null)") == NULL);
    JC_CHECK(strstr(buf, "this tool") != NULL);
}

/* R6 (M806): a shell command cut off at its own time limit, repeatedly, is a loop
 * even when a pipe masks the exit code -- and the same command spelled differently
 * must collapse to one key while two DIFFERENT commands must not. */
/* M870 (N21): the novelty window. Run 0012's worker rotated four checks with the
 * same results for 28 minutes; each shell command restarted the others' counts, so
 * M829 never fired. A call is stale when its triple appeared among the window's
 * calls before it; a rotation of four is all stale from its fifth call, and reaches
 * the window at call 28. One result that changes each time keeps it fresh. An edit
 * (jc_noprogress_reset) starts it again. */
void test_noprogress_rotation(void)
{
    struct jc_noprogress w;
    static const char *const cmds[4] = {
        "{\"command\":\"zig build test --summary all\"}",
        "{\"command\":\"ls -la REPORT.md\"}",
        "{\"command\":\"git status\"}",
        "{\"command\":\"git log --oneline -2\"}"
    };
    static const char *const outs[4] = { "832/832 passed", "REPORT.md", "clean",
                                         "abc1 def2" };
    char out[32];
    int i;

    jc_noprogress_init(&w);
    for (i = 0; i < 4 + JC_NOVELTY_WINDOW - 1; i++) {
        (void)jc_noprogress_note(&w, "run_terminal_command", cmds[i % 4], outs[i % 4]);
    }
    JC_CHECK(w.stale == JC_NOVELTY_WINDOW - 1);
    (void)jc_noprogress_note(&w, "run_terminal_command", cmds[0], outs[0]);
    JC_CHECK(w.stale == JC_NOVELTY_WINDOW);

    jc_noprogress_init(&w);
    for (i = 0; i < 100; i++) {
        jc_snprintf(out, sizeof out, "%d", i);
        (void)jc_noprogress_note(&w, "run_terminal_command", cmds[i % 4],
                                 (i % 4 == 2) ? out : outs[i % 4]);
    }
    JC_CHECK(w.stale < 4);

    jc_noprogress_init(&w);
    for (i = 0; i < 30; i++) {
        (void)jc_noprogress_note(&w, "run_terminal_command", cmds[i % 4], outs[i % 4]);
    }
    JC_CHECK(w.stale >= JC_NOVELTY_WINDOW);
    jc_noprogress_reset(&w);
    JC_CHECK(w.stale == 0 && w.ring_n == 0);
}

/* M868 (LOOP-1): a failing call whose arguments run to 160 bytes or more is the
 * same call the second and third time -- the exact note at its third repeat, as
 * for a short one. M432 compared the stored key, cut at 159 bytes, with the whole
 * incoming key, and a long one never matched. Two long calls that share their
 * first 199 bytes stay two calls. */
void test_toolloop_long_key(void)
{
    struct jc_toolloop w;
    char a[260];
    char b[260];
    int n = 0;

    memset(a, 'a', 200);
    a[200] = '\0';
    memcpy(b, a, 201);
    b[199] = 'b';

    jc_toolloop_init(&w);
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", a, JC_FAIL_NONZERO_EXIT, &n)
             == JC_TOOLLOOP_NONE);
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", a, JC_FAIL_NONZERO_EXIT, &n)
             == JC_TOOLLOOP_NONE);
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", a, JC_FAIL_NONZERO_EXIT, &n)
             == JC_TOOLLOOP_EXACT && n == 3);

    /* a, a, b, then a: a's third, with b counted apart (n is 3, not 4) */
    jc_toolloop_init(&w);
    (void)jc_toolloop_note(&w, "run_terminal_command", a, JC_FAIL_DENIED, &n);
    (void)jc_toolloop_note(&w, "run_terminal_command", a, JC_FAIL_DENIED, &n);
    (void)jc_toolloop_note(&w, "run_terminal_command", b, JC_FAIL_DENIED, &n);
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", a, JC_FAIL_DENIED, &n)
             == JC_TOOLLOOP_EXACT && n == 3);
}

void test_toolloop_atbound(void)
{
    struct jc_toolloop w;
    char out[JC_TOOLLOOP_KEY_MAX];
    char buf[400];
    int n;

    /* --- key normalisation: all six spellings of one command collapse -------- */
    jc_toolloop_shell_norm("timeout 2 sh -c \"sleep 10\"", out, sizeof out);
    JC_CHECK(strcmp(out, "sh -c \"sleep 10\"") == 0);
    jc_toolloop_shell_norm("cd . && timeout 2 sh -c \"sleep 10\"", out, sizeof out);
    JC_CHECK(strcmp(out, "sh -c \"sleep 10\"") == 0);
    jc_toolloop_shell_norm("timeout 3 sh -c \"sleep 10\"", out, sizeof out);
    JC_CHECK(strcmp(out, "sh -c \"sleep 10\"") == 0);
    jc_toolloop_shell_norm("timeout 2 sh -c \"sleep 10\" 2>&1", out, sizeof out);
    JC_CHECK(strcmp(out, "sh -c \"sleep 10\"") == 0);
    jc_toolloop_shell_norm("timeout 2 sh -c \"sleep 10\" | head -5", out, sizeof out);
    JC_CHECK(strcmp(out, "sh -c \"sleep 10\"") == 0);
    jc_toolloop_shell_norm("cd . && timeout 2 sh -c \"sleep 10\" | head -5",
                           out, sizeof out);
    JC_CHECK(strcmp(out, "sh -c \"sleep 10\"") == 0);
    /* env assignments, a -k flag, a chained filter, /dev/null redirection. */
    jc_toolloop_shell_norm("FOO=bar timeout -k 5 2 make test | grep -i fail",
                           out, sizeof out);
    JC_CHECK(strcmp(out, "make test") == 0);
    jc_toolloop_shell_norm("timeout 2 make >/dev/null 2>&1", out, sizeof out);
    JC_CHECK(strcmp(out, "make") == 0);

    /* --- the over-normalisation guard: distinct commands stay distinct ------- */
    jc_toolloop_shell_norm("timeout 2 sh -c \"sleep 10\"", buf, sizeof buf);
    jc_toolloop_shell_norm("timeout 2 sh -c \"sleep 20\"", out, sizeof out);
    JC_CHECK(strcmp(buf, out) != 0);   /* sleep 10 vs sleep 20 must NOT collapse */

    /* --- inner timeout parse and the at-bound predicate --------------------- */
    JC_CHECK(jc_toolloop_shell_timeout_secs("timeout 2 sh -c \"x\"") == 2);
    JC_CHECK(jc_toolloop_shell_timeout_secs("cd . && timeout 3 x") == 3);
    JC_CHECK(jc_toolloop_shell_timeout_secs("timeout 1m x") == 60);
    JC_CHECK(jc_toolloop_shell_timeout_secs("just a command") == 0);
    /* Reached its bound: 2 s limit, ~2 s wall. Under its bound: returned early. */
    JC_CHECK(jc_toolloop_at_bound("timeout 2 x", 1950.0) == 1);
    JC_CHECK(jc_toolloop_at_bound("timeout 2 x", 100.0) == 0);
    JC_CHECK(jc_toolloop_at_bound("x with no timeout", 999999.0) == 0);

    /* --- AT_BOUND uses the EXACT key only, and re-fires at the threshold ----- *
     * Six identical (normalised) bound-ended runs: fire at 3 and again at 6, and
     * NOT at 4 or 5. This is the pipe-masked-success case reaching the same
     * counter as the exit-124 case. */
    jc_toolloop_init(&w);
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", "sh -c \"sleep 10\"",
                              JC_FAIL_AT_BOUND, &n) == JC_TOOLLOOP_NONE); /* 1 */
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", "sh -c \"sleep 10\"",
                              JC_FAIL_AT_BOUND, &n) == JC_TOOLLOOP_NONE); /* 2 */
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", "sh -c \"sleep 10\"",
                              JC_FAIL_AT_BOUND, &n) == JC_TOOLLOOP_EXACT && n == 3);
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", "sh -c \"sleep 10\"",
                              JC_FAIL_AT_BOUND, &n) == JC_TOOLLOOP_NONE); /* 4 */
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", "sh -c \"sleep 10\"",
                              JC_FAIL_AT_BOUND, &n) == JC_TOOLLOOP_NONE); /* 5 */
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", "sh -c \"sleep 10\"",
                              JC_FAIL_AT_BOUND, &n) == JC_TOOLLOOP_EXACT && n == 6);

    /* Two DIFFERENT bound-ended commands never trip: distinct exact keys, and
     * AT_BOUND opens no shared class slot to accumulate them. Four calls total,
     * which WOULD cross the class threshold if a class key were used. */
    jc_toolloop_init(&w);
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", "cmd A",
                              JC_FAIL_AT_BOUND, &n) == JC_TOOLLOOP_NONE);
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", "cmd B",
                              JC_FAIL_AT_BOUND, &n) == JC_TOOLLOOP_NONE);
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", "cmd A",
                              JC_FAIL_AT_BOUND, &n) == JC_TOOLLOOP_NONE);
    JC_CHECK(jc_toolloop_note(&w, "run_terminal_command", "cmd B",
                              JC_FAIL_AT_BOUND, &n) == JC_TOOLLOOP_NONE);

    /* --- the AT_BOUND note names the true cause ----------------------------- */
    jc_toolloop_render(JC_TOOLLOOP_EXACT, "run_terminal_command",
                       JC_FAIL_AT_BOUND, 3, buf, sizeof buf);
    JC_CHECK(strstr(buf, "run_terminal_command") != NULL);
    JC_CHECK(strstr(buf, "time limit") != NULL);
    JC_CHECK(strstr(buf, "keep timing out") != NULL);
    /* not the generic "SAME arguments" failure wording */
    JC_CHECK(strstr(buf, "SAME arguments") == NULL);
    JC_CHECK(strcmp(jc_fail_class_name(JC_FAIL_AT_BOUND), "at_bound") == 0);
}

/* The success twin (plan D1, M733): the same call answered the same way. */
void test_noprogress(void)
{
    struct jc_noprogress w;
    char buf[400];
    char key[32];
    int i;

    /* --- the role table ---------------------------------------------------
     * The two RUN tools are counted although the registry calls them mutating:
     * their output is the answer being re-asked for, and they are the loop
     * shapes the corpora measured (13 identical shell calls in M687's turn,
     * run_tests 163 times on the workstation). */
    JC_CHECK(jc_noprogress_role("run_terminal_command", 0) == JC_NOPROGRESS_COUNT);
    JC_CHECK(jc_noprogress_role("run_tests", 0) == JC_NOPROGRESS_COUNT);
    JC_CHECK(jc_noprogress_role("search_code", 1) == JC_NOPROGRESS_COUNT);
    /* Any other mutating tool changed the tree: a repeat after it is new. */
    JC_CHECK(jc_noprogress_role("edit_file", 0) == JC_NOPROGRESS_RESET);
    JC_CHECK(jc_noprogress_role("some_mcp_tool", 0) == JC_NOPROGRESS_RESET);
    /* An answer from outside the tree is new information too. */
    JC_CHECK(jc_noprogress_role("ask_user", 1) == JC_NOPROGRESS_RESET);
    /* Owned elsewhere, or repeated by design. */
    JC_CHECK(jc_noprogress_role("read_file", 1) == JC_NOPROGRESS_IGNORE);
    JC_CHECK(jc_noprogress_role("read_background_output", 1)
             == JC_NOPROGRESS_IGNORE);
    JC_CHECK(jc_noprogress_role("todowrite", 1) == JC_NOPROGRESS_IGNORE);
    JC_CHECK(jc_noprogress_role(NULL, 1) == JC_NOPROGRESS_IGNORE);

    /* --- at three, not before, then again at each multiple (M819) ---------- */
    jc_noprogress_init(&w);
    JC_CHECK(jc_noprogress_note(&w, "search_code", "{\"pattern\":\"x\"}",
                                "(no matches)") == 0);
    JC_CHECK(jc_noprogress_note(&w, "search_code", "{\"pattern\":\"x\"}",
                                "(no matches)") == 0);
    JC_CHECK(jc_noprogress_note(&w, "search_code", "{\"pattern\":\"x\"}",
                                "(no matches)") == 3);
    /* M819 (N4a): re-warn at 6, 9 ... -- a child ran one passing call 1,126 times
     * in its single turn, and "once per turn" warned it once. The 4th and 5th are
     * silent; the 6th fires again. */
    JC_CHECK(jc_noprogress_note(&w, "search_code", "{\"pattern\":\"x\"}",
                                "(no matches)") == 0);
    JC_CHECK(jc_noprogress_note(&w, "search_code", "{\"pattern\":\"x\"}",
                                "(no matches)") == 0);
    JC_CHECK(jc_noprogress_note(&w, "search_code", "{\"pattern\":\"x\"}",
                                "(no matches)") == 6);

    /* --- the key is the RESULT too: the same question, a different answer -- */
    jc_noprogress_init(&w);
    for (i = 0; i < 4; i++) {
        jc_snprintf(buf, sizeof buf, "%d", i);
        JC_CHECK(jc_noprogress_note(&w, "run_terminal_command",
                                    "{\"command\":\"wc -c < c.txt\"}", buf) == 0);
    }
    /* ...and the whole arguments: a different question, the same answer. */
    jc_noprogress_init(&w);
    for (i = 0; i < 4; i++) {
        jc_snprintf(key, sizeof key, "{\"pattern\":\"p%d\"}", i);
        JC_CHECK(jc_noprogress_note(&w, "search_code", key, "(no matches)") == 0);
    }

    /* --- a reset starts the count again; a fresh unbroken loop re-notes (M819) */
    jc_noprogress_init(&w);
    JC_CHECK(jc_noprogress_note(&w, "git_diff", "{}", "same") == 0);
    JC_CHECK(jc_noprogress_note(&w, "git_diff", "{}", "same") == 0);
    jc_noprogress_reset(&w);                     /* an edit landed */
    JC_CHECK(jc_noprogress_note(&w, "git_diff", "{}", "same") == 0);
    JC_CHECK(jc_noprogress_note(&w, "git_diff", "{}", "same") == 0);
    JC_CHECK(jc_noprogress_note(&w, "git_diff", "{}", "same") == 3);
    /* M819: after another reset (a real edit protects iterative work), the SAME
     * call looping three more times with nothing between is a new loop, and is
     * noted again at the 3rd -- what "once, ever" used to hide. */
    jc_noprogress_reset(&w);
    JC_CHECK(jc_noprogress_note(&w, "git_diff", "{}", "same") == 0);
    JC_CHECK(jc_noprogress_note(&w, "git_diff", "{}", "same") == 0);
    JC_CHECK(jc_noprogress_note(&w, "git_diff", "{}", "same") == 3);

    /* --- a shell command resets every OTHER call, not itself --------------- */
    jc_noprogress_init(&w);
    JC_CHECK(jc_noprogress_note(&w, "search_code", "{\"pattern\":\"y\"}", "r") == 0);
    JC_CHECK(jc_noprogress_note(&w, "search_code", "{\"pattern\":\"y\"}", "r") == 0);
    JC_CHECK(jc_noprogress_note(&w, "run_terminal_command",
                                "{\"command\":\"sed -i s/a/b/ f\"}", "") == 0);
    /* The sed may have changed what the search sees: this is its first again. */
    JC_CHECK(jc_noprogress_note(&w, "search_code", "{\"pattern\":\"y\"}", "r") == 0);
    JC_CHECK(jc_noprogress_note(&w, "search_code", "{\"pattern\":\"y\"}", "r") == 0);
    JC_CHECK(jc_noprogress_note(&w, "search_code", "{\"pattern\":\"y\"}", "r") == 3);
    /* The same command answering the same way three times is the loop itself --
     * bonsai 06: the same failing build output three times running. */
    jc_noprogress_init(&w);
    JC_CHECK(jc_noprogress_note(&w, "run_terminal_command",
                                "{\"command\":\"zig build test\"}", "FAIL") == 0);
    JC_CHECK(jc_noprogress_note(&w, "run_terminal_command",
                                "{\"command\":\"zig build test\"}", "FAIL") == 0);
    JC_CHECK(jc_noprogress_note(&w, "run_terminal_command",
                                "{\"command\":\"zig build test\"}", "FAIL") == 3);

    /* --- a full table forgets the call seen longest ago -------------------- */
    jc_noprogress_init(&w);
    JC_CHECK(jc_noprogress_note(&w, "git_log", "{}", "K") == 0);
    JC_CHECK(jc_noprogress_note(&w, "git_log", "{}", "K") == 0);
    for (i = 0; i < JC_NOPROGRESS_MAX_ENTRIES; i++) {
        jc_snprintf(key, sizeof key, "{\"n\":%d}", i);
        (void)jc_noprogress_note(&w, "list_files", key, "same listing");
    }
    /* Evicted, so this is its first again -- a missed note, never a false one. */
    JC_CHECK(jc_noprogress_note(&w, "git_log", "{}", "K") == 0);
    JC_CHECK(w.n == JC_NOPROGRESS_MAX_ENTRIES);

    /* --- the note says what was measured, and no more ----------------------- */
    jc_noprogress_render("search_code", 3, buf, sizeof buf);
    JC_CHECK(strstr(buf, "`search_code`") != NULL);
    JC_CHECK(strstr(buf, "same result 3 times") != NULL);
    JC_CHECK(strstr(buf, "will not change the answer") != NULL);
    /* It cannot know that nothing changed -- a shell command between the repeats
     * may have written a file -- so it must not say so, in any capitalisation. */
    for (i = 0; buf[i] != '\0'; i++) {
        if (buf[i] >= 'A' && buf[i] <= 'Z') {
            buf[i] = (char)(buf[i] - 'A' + 'a');
        }
    }
    JC_CHECK(strstr(buf, "nothing") == NULL);
    jc_noprogress_render(NULL, 3, buf, sizeof buf);
    JC_CHECK(strstr(buf, "(null)") == NULL);
}
