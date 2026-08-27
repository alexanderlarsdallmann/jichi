/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_cli.c - CLI helper parsing. */

#include "jc_test.h"
#include "jc_cli.h"

#include <string.h>

static void test_arg_summary(void)
{
    char b[128];

    jc_tool_arg_summary("edit_file", "{\"path\":\"src/x.c\"}", b, sizeof b);
    JC_CHECK_STR(b, "src/x.c");

    jc_tool_arg_summary("run_terminal_command", "{\"command\":\"make test\"}",
                        b, sizeof b);
    JC_CHECK_STR(b, "make test");

    jc_tool_arg_summary("search_code", "{\"query\":\"foo\"}", b, sizeof b);
    JC_CHECK_STR(b, "foo");

    /* M571: AN UNRECOGNISED SHAPE IS ANNOUNCED BY KEY NAME, NEVER BY VALUE.
     * This assertion survived the rewrite unchanged and its comment did not:
     * it used to say "raw JSON fallback", and the raw form happened to contain
     * the key too. Both halves are now pinned, because only the second one is
     * the fix -- the value is what was being read aloud. */
    jc_tool_arg_summary("x", "{\"zzz\":\"q\"}", b, sizeof b);
    JC_CHECK(strstr(b, "zzz") != NULL);
    JC_CHECK(strstr(b, "q") == NULL);

    /* M571: MALFORMED JSON NOW SAYS NOTHING. The old assertion here was
     * `b[0] != '\0'` -- which encoded the old BEHAVIOUR (dump the raw bytes)
     * rather than the safety property it claimed to test ("no crash,
     * NUL-terminated"). Empty is NUL-terminated and it is the better answer for
     * a listener: "Calling the tool x." beats "Calling the tool x, with brace
     * not json." Raw arguments remain reachable through -v and the view key. */
    jc_tool_arg_summary("x", "{not json", b, sizeof b);
    JC_CHECK(b[0] == '\0');

    /* M571: THE TWO SHAPES THE OPERATOR ACTUALLY HEARD AS JSON.
     * apply_patch keeps its path NESTED inside edits[], where no top-level
     * lookup could reach it; ask_user's key is `question`, which was simply not
     * in the list. Both were announced as escaped JSON, braces and all. */
    jc_tool_arg_summary("apply_patch",
        "{\"edits\":[{\"path\":\"src/greet.c\",\"old_string\":\"a\\nb\"}]}",
        b, sizeof b);
    JC_CHECK_STR(b, "src/greet.c");
    JC_CHECK(strstr(b, "old_string") == NULL);   /* no shape, no escapes */

    /* More than one edit: the count is worth hearing, the paths are not. */
    jc_tool_arg_summary("apply_patch",
        "{\"edits\":[{\"path\":\"a.c\"},{\"path\":\"b.c\"},{\"path\":\"c.c\"}]}",
        b, sizeof b);
    JC_CHECK_STR(b, "a.c and 2 more");

    jc_tool_arg_summary("ask_user", "{\"question\":\"which one?\"}",
                        b, sizeof b);
    JC_CHECK_STR(b, "which one?");

    jc_tool_arg_summary("remember", "{\"note\":\"prefers digits\"}",
                        b, sizeof b);
    JC_CHECK_STR(b, "prefers digits");

    /* An array of objects that names no target falls through to stage 3 -- the
     * key, not the items. This is todowrite, and it is why stage 2 tests for a
     * path rather than merely for an array. */
    jc_tool_arg_summary("todowrite",
        "{\"todos\":[{\"content\":\"do a thing\",\"status\":\"pending\"}]}",
        b, sizeof b);
    JC_CHECK_STR(b, "todos");

    /* No arguments at all: nothing to say, and nothing said. */
    jc_tool_arg_summary("git_status", "{}", b, sizeof b);
    JC_CHECK(b[0] == '\0');

    /* NULL / empty -> empty */
    jc_tool_arg_summary("x", NULL, b, sizeof b);
    JC_CHECK(b[0] == '\0');

    /* truncation respects cap */
    jc_tool_arg_summary("edit_file",
        "{\"path\":\"0123456789abcdef\"}", b, 6);
    JC_CHECK(strlen(b) == 5);
}

static void test_model_short(void)
{
    JC_CHECK_STR(jc_model_short_name("jlu/qwen3-coder-next"), "qwen3-coder-next");
    JC_CHECK_STR(jc_model_short_name("local-gemma"), "local-gemma");
    JC_CHECK_STR(jc_model_short_name("a/b/c"), "c");
    JC_CHECK_STR(jc_model_short_name(""), "");
    JC_CHECK_STR(jc_model_short_name(NULL), "");
}

/* M296: the TUI named the TIER (`fast`) and never the model. `fast` is a config
 * intent label, chosen so an agent profile can pin a tier without naming a vendor
 * id -- so it says which tier is active and nothing about which model answered. */
static void test_model_display(void)
{
    char b[192];

    /* The intended case: an intent name plus the model it currently resolves to. */
    jc_model_display("fast", "jlu/qwen3-coder-next", b, sizeof b);
    JC_CHECK_STR(b, "fast (jlu/qwen3-coder-next)");

    /* The id half is the FULL id, never shortened. Shortening it is the precision
     * bug this milestone fixed: two vendors' models whose trailing segments match
     * must stay distinguishable. */
    jc_model_display("fast", "jlu/coder", b, sizeof b);
    JC_CHECK_STR(b, "fast (jlu/coder)");
    jc_model_display("fast", "other/coder", b, sizeof b);
    JC_CHECK_STR(b, "fast (other/coder)");

    /* No config `name` -- the COMMON case. `name` is NULL (it is not mirrored from
     * the wire id, which the M296 plan assumed), so this used to render "" in the
     * header and "(null)" via "%s" in /status: undefined behaviour in C89. */
    jc_model_display(NULL, "jlu/qwen3-coder-next", b, sizeof b);
    JC_CHECK_STR(b, "jlu/qwen3-coder-next");
    jc_model_display("", "jlu/qwen3-coder-next", b, sizeof b);
    JC_CHECK_STR(b, "jlu/qwen3-coder-next");

    /* A config that named the model after its own wire id gains no parenthetical:
     * the same string twice carries nothing. The full id is kept, so the vendor
     * prefix is not silently dropped on the way. */
    jc_model_display("jlu/coder", "jlu/coder", b, sizeof b);
    JC_CHECK_STR(b, "jlu/coder");

    /* The other duplicate form: the SHORT name has collapsed onto the id. */
    jc_model_display("vendor/coder", "coder", b, sizeof b);
    JC_CHECK_STR(b, "coder");

    /* The name half IS shortened -- that is where shortening is the point. */
    jc_model_display("tiers/fast", "jlu/qwen3-235b", b, sizeof b);
    JC_CHECK_STR(b, "fast (jlu/qwen3-235b)");

    /* No id: fall back to the name rather than rendering nothing. */
    jc_model_display("fast", NULL, b, sizeof b);
    JC_CHECK_STR(b, "fast");
    jc_model_display("fast", "", b, sizeof b);
    JC_CHECK_STR(b, "fast");

    /* Nothing at all still renders something: a display function must not print
     * an empty slot where a model belongs. */
    jc_model_display(NULL, NULL, b, sizeof b);
    JC_CHECK_STR(b, "?");
    jc_model_display("", "", b, sizeof b);
    JC_CHECK_STR(b, "?");

    /* Truncation NUL-terminates, and a zero/NULL destination is a no-op. */
    jc_model_display("fast", "jlu/qwen3-coder-next", b, 6);
    JC_CHECK(strlen(b) == 5);
    JC_CHECK(jc_model_display("fast", "x", b, 0) == 0);
    JC_CHECK(jc_model_display("fast", "x", NULL, sizeof b) == 0);
}

static void test_color(void)
{
    JC_CHECK(jc_color_enabled(-1, 1) == 1);
    JC_CHECK(jc_color_enabled(-1, 0) == 0);
    JC_CHECK(jc_color_enabled(0, 1) == 0);
    JC_CHECK(jc_color_enabled(1, 0) == 1);

    /* Mode colors (enum jc_agent_mode: 0=chat, 1=plan, 2=auto). Each is a
     * non-NULL, distinct ANSI escape; out-of-range falls back to cyan. */
    JC_CHECK(strcmp(jc_mode_color(0), "\x1b[32m") == 0); /* chat: green  */
    JC_CHECK(strcmp(jc_mode_color(1), "\x1b[34m") == 0); /* plan: blue   */
    JC_CHECK(strcmp(jc_mode_color(2), "\x1b[33m") == 0); /* auto: yellow */
    JC_CHECK(strcmp(jc_mode_color(99), "\x1b[36m") == 0);/* other: cyan  */
    JC_CHECK(strcmp(jc_mode_color(0), jc_mode_color(2)) != 0); /* distinct */
}

static void test_reltime(void)
{
    char b[32];
    /* jc_fmt_elapsed (M346): the working line's counting-up display. The
     * sub-minute form must stay byte-identical to the raw "%.1fs" it
     * replaced; above it, zero-padded so the width does not shimmer. */
    jc_fmt_elapsed(0.0, b, sizeof b);      JC_CHECK_STR(b, "0.0s");
    jc_fmt_elapsed(12.34, b, sizeof b);    JC_CHECK_STR(b, "12.3s");
    jc_fmt_elapsed(59.90, b, sizeof b);    JC_CHECK_STR(b, "59.9s");
    jc_fmt_elapsed(60.0, b, sizeof b);     JC_CHECK_STR(b, "1m 00s");
    jc_fmt_elapsed(127.4, b, sizeof b);    JC_CHECK_STR(b, "2m 07s");
    jc_fmt_elapsed(3599.0, b, sizeof b);   JC_CHECK_STR(b, "59m 59s");
    jc_fmt_elapsed(3600.0, b, sizeof b);   JC_CHECK_STR(b, "1h 00m");
    jc_fmt_elapsed(3849.0, b, sizeof b);   JC_CHECK_STR(b, "1h 04m");
    jc_fmt_elapsed(-3.0, b, sizeof b);     JC_CHECK_STR(b, "0.0s");

    jc_reltime(5, b, sizeof b);       JC_CHECK_STR(b, "just now");
    jc_reltime(90, b, sizeof b);      JC_CHECK_STR(b, "1m ago");
    jc_reltime(7200, b, sizeof b);    JC_CHECK_STR(b, "2h ago");
    jc_reltime(172800, b, sizeof b);  JC_CHECK_STR(b, "2d ago");
    jc_reltime(1209600, b, sizeof b); JC_CHECK_STR(b, "2w ago");
    jc_reltime(-10, b, sizeof b);     JC_CHECK_STR(b, "just now");
}

static void test_prefix(void)
{
    static const char *ids[] = { "3f9aaa", "9c12bb", "3f9bcc" };
    JC_CHECK(jc_id_prefix_unique(ids, 3, "9c") == 1);     /* unique     */
    JC_CHECK(jc_id_prefix_unique(ids, 3, "3f9a") == 0);   /* unique     */
    JC_CHECK(jc_id_prefix_unique(ids, 3, "3f9") == -2);   /* ambiguous  */
    JC_CHECK(jc_id_prefix_unique(ids, 3, "zz") == -1);    /* none       */
    JC_CHECK(jc_id_prefix_unique(ids, 3, "3f9bcc") == 2); /* exact wins */
    JC_CHECK(jc_id_prefix_unique(ids, 3, "") == -1);      /* empty      */
}

static void test_overflow(void)
{
    /* Recognized: the messages real servers actually return. */
    JC_CHECK(jc_text_is_context_overflow(
        "n_keep: 8458 >= n_ctx: 8192. Try a larger context length.") == 1);
    JC_CHECK(jc_text_is_context_overflow("Context size has been exceeded.")
             == 1);
    JC_CHECK(jc_text_is_context_overflow(
        "the number of tokens is greater than the context length") == 1);
    JC_CHECK(jc_text_is_context_overflow(
        "This model's maximum context length is 8192 tokens") == 1);
    JC_CHECK(jc_text_is_context_overflow(
        "{\"code\":\"context_length_exceeded\"}") == 1);
    /* Not a false positive on an ordinary answer. */
    JC_CHECK(jc_text_is_context_overflow(
        "The watchdog timeout default is 300 seconds.") == 0);
    JC_CHECK(jc_text_is_context_overflow("") == 0);
    JC_CHECK(jc_text_is_context_overflow(NULL) == 0);
}

static void test_indicator(void)
{
    char b[128];
    /* nothing noteworthy -> empty, returns 0 */
    JC_CHECK(jc_tui_indicator(0, 0, 0, 0, b, sizeof b) == 0);
    JC_CHECK(b[0] == '\0');
    /* one item, singular */
    JC_CHECK(jc_tui_indicator(1, 0, 0, 0, b, sizeof b) > 0);
    JC_CHECK(strstr(b, "1 constraint") != NULL);
    JC_CHECK(strstr(b, "constraints") == NULL); /* singular, not plural */
    /* plural + multiple parts joined */
    jc_tui_indicator(2, 1, 3, 0, b, sizeof b);
    JC_CHECK(strstr(b, "2 constraints") != NULL);
    JC_CHECK(strstr(b, "1 job") != NULL);
    JC_CHECK(strstr(b, "3 todos") != NULL);
    JC_CHECK(strstr(b, " / ") != NULL); /* ASCII separator when !unicode */
    /* only the non-zero items appear */
    jc_tui_indicator(0, 2, 0, 0, b, sizeof b);
    JC_CHECK(strstr(b, "2 jobs") != NULL);
    JC_CHECK(strstr(b, "constraint") == NULL);
    JC_CHECK(strstr(b, "todo") == NULL);
}

static void test_interrupt(void)
{
    /* First empty-prompt Ctrl-C stays (0); the second consecutive one exits (1). */
    JC_CHECK(jc_interrupt_should_exit(1) == 0);
    JC_CHECK(jc_interrupt_should_exit(2) == 1);
    JC_CHECK(jc_interrupt_should_exit(3) == 1);
    JC_CHECK(jc_interrupt_should_exit(0) == 0);
}


/* M459: the wizard's OS line, bounded. The long string here is verbatim from
 * the bench tablet -- Android puts a git hash and a build number in uname's
 * release, and that is what pushed the line to 94 columns against the wizard's
 * 76-column accessibility contract. */
static void test_os_line(void)
{
    char b[160];
    const char *android = "5.15.185-android13-8-00044-g051a97cf151a-ab14024729";

    /* The case that was broken. The wizard prints "  %s\n", so the contract
     * covers the two-space indent, which jc_os_line is told about via max_cols
     * and must therefore subtract itself.
     *
     * On its own line the real Android release FITS -- 50 characters against a
     * 58-character budget -- so it survives INTACT. That is the point: moving
     * the field off the shared line is what fixes the actual device, and the
     * truncation below is only a backstop for a release longer than any seen.
     * Asserting truncation here would have been asserting the wrong fix. */
    jc_os_line("Linux", android, "aarch64", 76, b, sizeof b);
    JC_CHECK(strlen(b) + 2 <= 76);
    JC_CHECK(strstr(b, android) != NULL);
    JC_CHECK(strstr(b, "...") == NULL);
    JC_CHECK(strstr(b, "(aarch64)") != NULL);

    /* The backstop, on a release long enough to need it: bounded, and MARKED
     * -- this line reports what was probed, so a silent shortening would make
     * it lie. */
    jc_os_line("Linux", "6.6.0-verylongvendorbuild-0123456789abcdef-0123456789ab"
               "cdef-99999999", "aarch64", 76, b, sizeof b);
    JC_CHECK(strlen(b) + 2 <= 76);
    JC_CHECK(strstr(b, "...") != NULL);
    JC_CHECK(strncmp(b, "Linux 6.6.0-verylong", 20) == 0);
    JC_CHECK(strstr(b, "(aarch64)") != NULL);

    /* An ordinary release is left completely alone: no truncation, no marker.
     * This is the half that proves the bound is not paid for by every other
     * platform. */
    jc_os_line("Linux", "6.1.0-13-amd64", "x86_64", 76, b, sizeof b);
    JC_CHECK_STR(b, "Linux 6.1.0-13-amd64 (x86_64)");
    JC_CHECK(strstr(b, "...") == NULL);

    /* No machine field: the parenthetical is dropped, not rendered empty. */
    jc_os_line("Linux", "6.1.0", "", 76, b, sizeof b);
    JC_CHECK_STR(b, "Linux 6.1.0");

    /* Never pass NULL to "%s" (C89 undefined behaviour -- the M296 lesson). */
    jc_os_line(NULL, NULL, NULL, 76, b, sizeof b);
    JC_CHECK(b[0] != '\0');

    /* A caller with no room at all must not be handed a buffer of dots. */
    jc_os_line("Linux", android, "aarch64", 12, b, sizeof b);
    JC_CHECK(b[0] != '\0');
}

void test_cli(void)
{
    int j = -1;
    JC_CHECK(jc_output_format_parse("text", &j) == 0 && j == 0);
    JC_CHECK(jc_output_format_parse("json", &j) == 0 && j == 1);
    JC_CHECK(jc_output_format_parse("jsonl", &j) == 0 && j == 2);
    JC_CHECK(jc_output_format_parse("xml", &j) != 0);
    JC_CHECK(jc_output_format_parse("", &j) != 0);
    JC_CHECK(jc_output_format_parse(NULL, &j) != 0);
    test_arg_summary();
    test_model_short();
    test_model_display();
    test_color();
    test_os_line();
    test_reltime();
    test_prefix();
    test_overflow();
    test_interrupt();
    test_indicator();
}
