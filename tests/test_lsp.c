/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_lsp.c - offline tests for the LSP protocol layer (jc_lsp_proto.c). */

#include "jc_test.h"
#include "jc_lsp.h"
#include "jc_str.h"

#include <stdlib.h>
#include <string.h>
#include "jc_snprintf.h"

static void test_frame_encode(void)
{
    struct jc_sb sb;
    const char *p;
    jc_sb_init(&sb);
    jc_lsp_frame_encode("hello", &sb);
    p = sb.data;
    JC_CHECK_STR(p, "Content-Length: 5\r\n\r\nhello");
    jc_sb_free(&sb);
}

static void test_framer(void)
{
    struct jc_lsp_framer f;
    struct jc_sb sb;
    char *body = NULL;

    jc_lsp_framer_init(&f);

    /* Two messages in one buffer -> pop both in order. */
    jc_sb_init(&sb);
    jc_lsp_frame_encode("{\"a\":1}", &sb);
    jc_lsp_frame_encode("{\"b\":2}", &sb);
    jc_lsp_framer_push(&f, sb.data, sb.len);
    jc_sb_free(&sb);

    JC_CHECK(jc_lsp_framer_pop(&f, &body) == 1);
    JC_CHECK_STR(body, "{\"a\":1}");
    free(body);
    JC_CHECK(jc_lsp_framer_pop(&f, &body) == 1);
    JC_CHECK_STR(body, "{\"b\":2}");
    free(body);
    JC_CHECK(jc_lsp_framer_pop(&f, &body) == 0);

    /* Split across pushes: incomplete -> 0, then completes -> 1. */
    {
        const char *part = "Content-Length: 3\r\n\r\nx";
        jc_lsp_framer_push(&f, part, (jc_size)strlen(part));
    }
    JC_CHECK(jc_lsp_framer_pop(&f, &body) == 0);
    jc_lsp_framer_push(&f, "yz", 2);
    JC_CHECK(jc_lsp_framer_pop(&f, &body) == 1);
    JC_CHECK_STR(body, "xyz");
    free(body);

    /* M609: a header block that never terminates is bounded, not buffered to
     * OOM. Push well past JC_LSP_MAX_HEADER of non-terminator bytes; pop must
     * report "incomplete" AND the buffer must have been dropped, not retained. */
    {
        char *chunk = (char *)malloc(4096);
        long pushed = 0;
        long target = JC_LSP_MAX_HEADER + 4096;
        if (chunk != NULL) {
            memset(chunk, 'H', 4096); /* no '\r'/'\n': never a terminator */
            while (pushed < target) {
                jc_lsp_framer_push(&f, chunk, 4096);
                pushed += 4096;
                if (jc_lsp_framer_pop(&f, &body) != 0) { free(body); body = NULL; }
            }
            free(chunk);
            /* The pre-M609 framer retained every byte here (f.buf.len == target);
             * the cap drops the buffer once it passes JC_LSP_MAX_HEADER. */
            JC_CHECK((long)f.buf.len <= (long)JC_LSP_MAX_HEADER);
            /* And the framer still works after the resync. */
            {
                struct jc_sb enc;
                jc_sb_init(&enc);
                jc_lsp_frame_encode("{\"ok\":1}", &enc);
                jc_lsp_framer_push(&f, enc.data, enc.len);
                jc_sb_free(&enc);
                JC_CHECK(jc_lsp_framer_pop(&f, &body) == 1);
                JC_CHECK_STR(body, "{\"ok\":1}");
                free(body);
            }
        }
    }

    /* M609: an overflowing Content-Length is malformed, not a wrapped small
     * length. 18446744073709551621 = 2^64 + 5 wraps to 5 on a 64-bit jc_size;
     * the pre-M609 parser would then read 5 body bytes ("ABCDE") and pop a
     * bogus message. The guard returns the sentinel, the header is dropped, and
     * pop reports nothing available. */
    {
        const char *bad =
            "Content-Length: 18446744073709551621\r\n\r\nABCDE";
        jc_lsp_framer_push(&f, bad, (jc_size)strlen(bad));
        JC_CHECK(jc_lsp_framer_pop(&f, &body) == 0);
        if (body != NULL) { free(body); body = NULL; }
    }

    jc_lsp_framer_free(&f);
}

static void test_uri_and_lang(void)
{
    char buf[256];
    const char *p = buf;
    jc_lsp_path_to_uri("/abs/x.c", "/cwd", buf, sizeof(buf));
    JC_CHECK_STR(p, "file:///abs/x.c");
    jc_lsp_path_to_uri("rel/y.c", "/work", buf, sizeof(buf));
    JC_CHECK_STR(p, "file:///work/rel/y.c");

    JC_CHECK_STR(jc_lsp_language_id("c"), "c");
    JC_CHECK_STR(jc_lsp_language_id("h"), "c");
    JC_CHECK_STR(jc_lsp_language_id("cpp"), "cpp");
    JC_CHECK_STR(jc_lsp_language_id("py"), "python");
    JC_CHECK_STR(jc_lsp_language_id("zzz"), "zzz");
}

static void test_format_diagnostics(void)
{
    const char *params =
        "{\"uri\":\"file:///x.c\",\"diagnostics\":["
        "{\"range\":{\"start\":{\"line\":4,\"character\":2}},"
        "\"severity\":1,\"message\":\"expected ;\"},"
        "{\"range\":{\"start\":{\"line\":9,\"character\":0}},"
        "\"severity\":2,\"message\":\"unused var\"}]}";
    struct jc_sb out;
    int cnt = -1;
    int matched;

    jc_sb_init(&out);
    matched = jc_lsp_format_diagnostics(params, "file:///x.c", "x.c",
                                        &out, &cnt);
    JC_CHECK(matched == 1);
    JC_CHECK(cnt == 2);
    JC_CHECK(strstr(out.data, "x.c:5:3: error: expected ;") != NULL);
    JC_CHECK(strstr(out.data, "x.c:10:1: warning: unused var") != NULL);
    jc_sb_free(&out);

    /* Non-matching uri => not matched. */
    jc_sb_init(&out);
    cnt = -1;
    JC_CHECK(jc_lsp_format_diagnostics(params, "file:///other.c", "other.c",
                                       &out, &cnt) == 0);
    jc_sb_free(&out);
}

/* Apply a TextEdit[] and assert the result + count (M40). */
static void check_apply(const char *text, const char *edits, int want_n,
                        const char *want)
{
    struct jc_sb out;
    int n;
    jc_sb_init(&out);
    n = jc_lsp_apply_text_edits(text, edits, &out);
    JC_CHECK(n == want_n);
    if (want != NULL) {
        const char *got = (out.data != NULL) ? out.data : "";
        JC_CHECK(strcmp(got, want) == 0);
    }
    jc_sb_free(&out);
}

static void test_apply_text_edits(void)
{
    /* Two whole-word replacements on separate lines. */
    check_apply("foo bar\nfoo baz\n",
        "[{\"range\":{\"start\":{\"line\":0,\"character\":0},"
        "\"end\":{\"line\":0,\"character\":3}},\"newText\":\"qux\"},"
        "{\"range\":{\"start\":{\"line\":1,\"character\":0},"
        "\"end\":{\"line\":1,\"character\":3}},\"newText\":\"qux\"}]",
        2, "qux bar\nqux baz\n");

    /* Out-of-order edits are sorted before applying => same result. */
    check_apply("foo bar\nfoo baz\n",
        "[{\"range\":{\"start\":{\"line\":1,\"character\":0},"
        "\"end\":{\"line\":1,\"character\":3}},\"newText\":\"qux\"},"
        "{\"range\":{\"start\":{\"line\":0,\"character\":0},"
        "\"end\":{\"line\":0,\"character\":3}},\"newText\":\"qux\"}]",
        2, "qux bar\nqux baz\n");

    /* Insertion (empty range) and deletion (empty newText). */
    check_apply("ab",
        "[{\"range\":{\"start\":{\"line\":0,\"character\":1},"
        "\"end\":{\"line\":0,\"character\":1}},\"newText\":\"X\"}]",
        1, "aXb");
    check_apply("abc",
        "[{\"range\":{\"start\":{\"line\":0,\"character\":0},"
        "\"end\":{\"line\":0,\"character\":1}},\"newText\":\"\"}]",
        1, "bc");

    /* Overlapping edits: the first wins, the overlapper is skipped. */
    check_apply("abcdef",
        "[{\"range\":{\"start\":{\"line\":0,\"character\":0},"
        "\"end\":{\"line\":0,\"character\":3}},\"newText\":\"X\"},"
        "{\"range\":{\"start\":{\"line\":0,\"character\":1},"
        "\"end\":{\"line\":0,\"character\":4}},\"newText\":\"Y\"}]",
        1, "Xdef");

    /* Empty edit set: zero applied, text unchanged. */
    check_apply("abc", "[]", 0, "abc");

    /* JSON null (LSP "no edits" -- e.g. zls formatting an already-formatted
     * file): zero applied, text unchanged -- NOT malformed. Regression for the
     * format_file "malformed formatting edits" false error. */
    check_apply("abc\n", "null", 0, "abc\n");

    /* Malformed input: -1, and out left untouched (empty). */
    check_apply("abc", "not json", -1, "");
    check_apply("abc", "{\"not\":\"array\"}", -1, "");
}

static void test_format_code_actions(void)
{
    struct jc_sb out;
    int n;

    /* A CodeAction (title + kind) and a bare Command (title only). */
    jc_sb_init(&out);
    n = -1;
    JC_CHECK(jc_lsp_format_code_actions(
        "[{\"title\":\"Organize Imports\",\"kind\":\"source.organizeImports\"},"
        "{\"title\":\"Run command\",\"command\":\"foo.run\"}]", &out, &n) == 1);
    JC_CHECK(n == 2);
    JC_CHECK(out.data != NULL &&
             strcmp(out.data,
                    "Organize Imports  [source.organizeImports]\n"
                    "Run command\n") == 0);
    jc_sb_free(&out);

    /* Empty array: parsed, zero actions. */
    jc_sb_init(&out);
    n = -1;
    JC_CHECK(jc_lsp_format_code_actions("[]", &out, &n) == 1 && n == 0);
    jc_sb_free(&out);

    /* Malformed / null: returns 0, count 0, out untouched. */
    jc_sb_init(&out);
    n = -1;
    JC_CHECK(jc_lsp_format_code_actions("not json", &out, &n) == 0 && n == 0);
    JC_CHECK(jc_lsp_format_code_actions(NULL, &out, &n) == 0);
    jc_sb_free(&out);
}

static void test_action_command(void)
{
    char *cmd = NULL;
    char *args = NULL;

    /* A bare Command: command string + arguments at top level. */
    JC_CHECK(jc_lsp_action_command(
        "{\"title\":\"Run\",\"command\":\"foo.run\","
        "\"arguments\":[\"a\",1]}", &cmd, &args) == 1);
    JC_CHECK_STR(cmd, "foo.run");
    JC_CHECK(args != NULL && strstr(args, "\"a\"") != NULL &&
             strstr(args, "1") != NULL);
    free(cmd); free(args); cmd = NULL; args = NULL;

    /* A CodeAction nesting a Command object under `command`. */
    JC_CHECK(jc_lsp_action_command(
        "{\"title\":\"Organize\",\"kind\":\"source\","
        "\"command\":{\"title\":\"x\",\"command\":\"editor.organize\"}}",
        &cmd, &args) == 1);
    JC_CHECK_STR(cmd, "editor.organize");
    JC_CHECK(args == NULL); /* no arguments */
    free(cmd); free(args); cmd = NULL; args = NULL;

    /* An edit-only action (no command) -> 0, outs left NULL. */
    JC_CHECK(jc_lsp_action_command(
        "{\"title\":\"Fix\",\"edit\":{\"changes\":{}}}", &cmd, &args) == 0);
    JC_CHECK(cmd == NULL && args == NULL);

    /* Malformed / null. */
    JC_CHECK(jc_lsp_action_command("not json", &cmd, &args) == 0);
    JC_CHECK(jc_lsp_action_command(NULL, &cmd, &args) == 0);
}

static void test_diagnostics_for_line(void)
{
    /* params with three diagnostics: line 0 (single-line), lines 4..6
       (multi-line range), and line 9. */
    static const char *const PARAMS =
        "{\"uri\":\"file:///p/a.c\",\"diagnostics\":["
        "{\"range\":{\"start\":{\"line\":0,\"character\":2},"
        "\"end\":{\"line\":0,\"character\":5}},\"message\":\"d0\"},"
        "{\"range\":{\"start\":{\"line\":4,\"character\":0},"
        "\"end\":{\"line\":6,\"character\":1}},\"message\":\"d456\"},"
        "{\"range\":{\"start\":{\"line\":9,\"character\":0},"
        "\"end\":{\"line\":9,\"character\":3}},\"message\":\"d9\"}]}";
    char *r;

    /* line 0 -> the first diagnostic only. */
    r = jc_lsp_diagnostics_for_line(PARAMS, "file:///p/a.c", 0);
    JC_CHECK(r != NULL && strstr(r, "d0") != NULL && strstr(r, "d456") == NULL
             && strstr(r, "d9") == NULL);
    free(r);

    /* line 5 falls inside the multi-line range (4..6) -> d456 only. */
    r = jc_lsp_diagnostics_for_line(PARAMS, "file:///p/a.c", 5);
    JC_CHECK(r != NULL && strstr(r, "d456") != NULL && strstr(r, "d0") == NULL);
    free(r);

    /* line 2 covers none -> empty array. */
    r = jc_lsp_diagnostics_for_line(PARAMS, "file:///p/a.c", 2);
    JC_CHECK_STR(r, "[]");
    free(r);

    /* uri mismatch -> empty array. */
    r = jc_lsp_diagnostics_for_line(PARAMS, "file:///p/other.c", 0);
    JC_CHECK_STR(r, "[]");
    free(r);

    /* malformed / NULL -> empty array, never NULL. */
    r = jc_lsp_diagnostics_for_line("not json", "file:///p/a.c", 0);
    JC_CHECK_STR(r, "[]");
    free(r);
    r = jc_lsp_diagnostics_for_line(NULL, "file:///p/a.c", 0);
    JC_CHECK_STR(r, "[]");
    free(r);
}

static void test_only_array(void)
{
    char *r;

    /* comma + space separated -> JSON array of kinds. */
    r = jc_lsp_only_array("quickfix, refactor.extract");
    JC_CHECK(r != NULL && strstr(r, "\"quickfix\"") != NULL &&
             strstr(r, "\"refactor.extract\"") != NULL && r[0] == '[');
    free(r);

    /* single token. */
    r = jc_lsp_only_array("source.organizeImports");
    JC_CHECK_STR(r, "[\"source.organizeImports\"]");
    free(r);

    /* empty / whitespace / NULL -> NULL (no filter). */
    JC_CHECK(jc_lsp_only_array("") == NULL);
    JC_CHECK(jc_lsp_only_array("  , ,  ") == NULL);
    JC_CHECK(jc_lsp_only_array(NULL) == NULL);
}

static void test_lsp_suggestion(void)
{
    struct jc_lsp_suggestion s;
    /* language names */
    JC_CHECK(jc_lsp_suggest("c", &s) == 1 &&
             strcmp(s.command, "clangd") == 0);
    JC_CHECK(jc_lsp_suggest("cpp", &s) == 1 &&
             strcmp(s.command, "clangd") == 0);
    JC_CHECK(jc_lsp_suggest("zig", &s) == 1 && strcmp(s.command, "zls") == 0);
    JC_CHECK(jc_lsp_suggest("go", &s) == 1 && strcmp(s.command, "gopls") == 0);
    JC_CHECK(jc_lsp_suggest("rust", &s) == 1 &&
             strcmp(s.command, "rust-analyzer") == 0);
    JC_CHECK(jc_lsp_suggest("python", &s) == 1);
    /* extension aliases resolve too */
    JC_CHECK(jc_lsp_suggest("rs", &s) == 1 &&
             strcmp(s.command, "rust-analyzer") == 0);
    /* unknown / NULL */
    JC_CHECK(jc_lsp_suggest("cobol", &s) == 0);
    JC_CHECK(jc_lsp_suggest(NULL, &s) == 0);
    /* a prefix must NOT match ("c" is exact, "css" is not "c") */
    JC_CHECK(jc_lsp_suggest("css", &s) == 0);
}

void test_lsp(void)
{
    test_frame_encode();
    test_framer();
    test_uri_and_lang();
    test_format_diagnostics();
    test_apply_text_edits();
    test_format_code_actions();
    test_action_command();
    test_diagnostics_for_line();
    test_only_array();
    test_lsp_suggestion();
}

/* M472: the framing header comes from a third-party program, so Content-Length is
 * attacker-supplied. It had no sanity bound: `Content-Length: 4000000000` made the
 * framer buffer without limit toward a body that never arrives -- an OOM kill on
 * the low-RAM rows, a stall anywhere.
 *
 * The hostile values below were all TESTED before the cap existed and none of them
 * corrupted memory on 64-bit. That is worth knowing, because the reason is not a
 * guard: malloc fails first, and SIZE_MAX collides with find_content_length's
 * (jc_size)-1 error sentinel. Two accidents. The cap is what makes it safe by
 * construction, and these checks pin both halves -- refused above the cap, and
 * still working below it. */
void test_lsp_frame_bounds(void)
{
    struct jc_lsp_framer f;
    char *body = NULL;
    char hdr[128];

    /* A body far above the cap is refused, and the framer does not keep it. */
    jc_lsp_framer_init(&f);
    jc_snprintf(hdr, sizeof hdr, "Content-Length: 4000000000\r\n\r\n{}");
    jc_lsp_framer_push(&f, hdr, (jc_size)strlen(hdr));
    JC_CHECK(jc_lsp_framer_pop(&f, &body) == 0);
    JC_CHECK(body == NULL);
    /* Resynced: the oversized header was dropped, not left to accumulate. */
    JC_CHECK(f.buf.len < (jc_size)strlen(hdr));
    jc_lsp_framer_free(&f);

    /* The classic hostile values, each of which used to reach the arithmetic. */
    {
        static const char *const bad[] = {
            "18446744073709551615",   /* SIZE_MAX -- collides with the sentinel */
            "18446744073709551614",   /* SIZE_MAX-1 */
            "99999999999999999999999999", /* wraps the accumulator */
            "9223372036854775807",    /* LONG_MAX */
            NULL
        };
        int i;
        for (i = 0; bad[i] != NULL; i++) {
            jc_lsp_framer_init(&f);
            jc_snprintf(hdr, sizeof hdr, "Content-Length: %s\r\n\r\n{}", bad[i]);
            jc_lsp_framer_push(&f, hdr, (jc_size)strlen(hdr));
            body = NULL;
            JC_CHECK(jc_lsp_framer_pop(&f, &body) == 0);
            JC_CHECK(body == NULL);
            jc_lsp_framer_free(&f);
        }
    }

    /* ...and a normal frame still works, which is the half a cap can break. */
    jc_lsp_framer_init(&f);
    jc_snprintf(hdr, sizeof hdr, "Content-Length: 9\r\n\r\n{\"ok\":1}\n");
    jc_lsp_framer_push(&f, hdr, (jc_size)strlen(hdr));
    body = NULL;
    if (JC_REQUIRE(jc_lsp_framer_pop(&f, &body) == 1)) {
        JC_CHECK(body != NULL && strncmp(body, "{\"ok\":1}", 8) == 0);
        free(body);
    }
    jc_lsp_framer_free(&f);
}
