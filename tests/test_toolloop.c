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
    JC_CHECK(jc_fail_classify(
        "Path src/x.c is outside this run's --edit-scope; the edit was refused.",
        -1) == JC_FAIL_DENIED);
    JC_CHECK(jc_fail_classify(
        "Tool requires approval, unavailable in headless mode", -1)
        == JC_FAIL_DENIED);
    /* A kill that quotes a command containing a path is still a kill. */
    JC_CHECK(jc_fail_classify(
        "\n[stopped: command timed out after 60s and was terminated]\n", 124)
        == JC_FAIL_KILLED);
    JC_CHECK(jc_fail_classify(
        "[stopped: output exceeded the 65536-byte capture limit and the command "
        "was killed]", 137) == JC_FAIL_KILLED);

    JC_CHECK(jc_fail_classify("error: string not found in file", -1)
             == JC_FAIL_NOT_FOUND);
    JC_CHECK(jc_fail_classify("no such file or directory: build.zig", -1)
             == JC_FAIL_NOT_FOUND);
    /* jichi's OWN missing-file wording, which the first cut of the classifier
     * missed -- so a plain missing file was filed OTHER and got generic advice. */
    JC_CHECK(jc_fail_classify("error: could not open 'sub/missing.txt'", -1)
             == JC_FAIL_NOT_FOUND);
    /* ...but the fence denial, which M291 split OUT of that wording, stays DENIED. */
    JC_CHECK(jc_fail_classify(
        "error: refused by safety fence (path outside workspace and any "
        "referenceRoots): 'nope/x'", -1) == JC_FAIL_DENIED);
    JC_CHECK(jc_fail_classify("error: required parameter 'path' missing", -1)
             == JC_FAIL_BAD_ARGS);
    JC_CHECK(jc_fail_classify("invalid JSON in arguments", -1)
             == JC_FAIL_BAD_ARGS);

    /* A command that simply returned non-zero, with nothing else to go on. */
    JC_CHECK(jc_fail_classify("zig build\ncompilation aborted\n", 1)
             == JC_FAIL_NONZERO_EXIT);
    /* Nothing recognisable and no exit code: OTHER, so the note says less rather
     * than inventing a cause. */
    JC_CHECK(jc_fail_classify("something went sideways", -1) == JC_FAIL_OTHER);
    JC_CHECK(jc_fail_classify(NULL, -1) == JC_FAIL_OTHER);
    JC_CHECK(jc_fail_classify("", 3) == JC_FAIL_NONZERO_EXIT);
    /* Case-insensitive: strcasestr is not C89, so the walk is hand-rolled and
     * deserves a check. */
    JC_CHECK(jc_fail_classify("ERROR: No Such File", -1) == JC_FAIL_NOT_FOUND);

    JC_CHECK(strcmp(jc_fail_class_name(JC_FAIL_DENIED), "denied") == 0);
    JC_CHECK(strcmp(jc_fail_class_name(JC_FAIL_OTHER), "other") == 0);

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

    /* --- at three, not before, and once ----------------------------------- */
    jc_noprogress_init(&w);
    JC_CHECK(jc_noprogress_note(&w, "search_code", "{\"pattern\":\"x\"}",
                                "(no matches)") == 0);
    JC_CHECK(jc_noprogress_note(&w, "search_code", "{\"pattern\":\"x\"}",
                                "(no matches)") == 0);
    JC_CHECK(jc_noprogress_note(&w, "search_code", "{\"pattern\":\"x\"}",
                                "(no matches)") == 3);
    /* Told once per turn: bonsai 19 made this call 35 times in a row. */
    JC_CHECK(jc_noprogress_note(&w, "search_code", "{\"pattern\":\"x\"}",
                                "(no matches)") == 0);

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

    /* --- a reset starts the count again, and keeps what was told ----------- */
    jc_noprogress_init(&w);
    JC_CHECK(jc_noprogress_note(&w, "git_diff", "{}", "same") == 0);
    JC_CHECK(jc_noprogress_note(&w, "git_diff", "{}", "same") == 0);
    jc_noprogress_reset(&w);                     /* an edit landed */
    JC_CHECK(jc_noprogress_note(&w, "git_diff", "{}", "same") == 0);
    JC_CHECK(jc_noprogress_note(&w, "git_diff", "{}", "same") == 0);
    JC_CHECK(jc_noprogress_note(&w, "git_diff", "{}", "same") == 3);
    jc_noprogress_reset(&w);
    for (i = 0; i < 3; i++) {
        JC_CHECK(jc_noprogress_note(&w, "git_diff", "{}", "same") == 0);
    }

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
