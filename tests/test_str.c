/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_str.c - exercises jc_sb and jc_strdup. */

#include "jc_test.h"
#include "jc_str.h"
#include <stdlib.h>

void test_str(void)
{
    struct jc_sb b;
    char *out;
    int i;

    jc_sb_init(&b);
    JC_CHECK(b.len == 0);

    jc_sb_append(&b, "hello");
    jc_sb_append_char(&b, ' ');
    jc_sb_append_n(&b, "world!!", 5);
    JC_CHECK(b.len == 11);
    JC_CHECK_STR(b.data, "hello world");

    jc_sb_append_fmt(&b, " %d/%s", 42, "x");
    JC_CHECK_STR(b.data, "hello world 42/x");

    /* Growth across many appends. */
    jc_sb_clear(&b);
    for (i = 0; i < 1000; i++) {
        jc_sb_append(&b, "ab");
    }
    JC_CHECK(b.len == 2000);

    out = jc_sb_finish(&b);
    JC_CHECK(out != NULL);
    JC_CHECK(b.data == NULL);
    free(out);
    jc_sb_free(&b);

    /* M218: jc_sb_clear_shrink -- clear, but also free an outsized backing
     * store. jc_sb_clear keeps capacity by design, which is right for a warm
     * per-call buffer but pins a one-off huge message's high-water for the
     * buffer's lifetime (the provider stream scratch lives as long as the
     * provider). Above max_cap the buffer is released outright. */
    {
        struct jc_sb s;
        jc_sb_init(&s);
        for (i = 0; i < 1024; i++) {
            jc_sb_append(&s, "0123456789abcdef"); /* 16 KB total */
        }
        JC_CHECK(s.cap > 8192);
        jc_sb_clear_shrink(&s, 8192);              /* over: release */
        JC_CHECK(s.data == NULL && s.cap == 0 && s.len == 0);
        jc_sb_append(&s, "still works");           /* usable after */
        JC_CHECK_STR(s.data, "still works");
        {
            jc_size cap_before;
            jc_sb_clear(&s);
            jc_sb_append(&s, "small");
            cap_before = s.cap;
            jc_sb_clear_shrink(&s, 8192);          /* under: keep warm */
            JC_CHECK(s.cap == cap_before && s.len == 0);
            JC_CHECK(s.data != NULL && s.data[0] == '\0');
        }
        jc_sb_free(&s);
    }

    {
        char *d = jc_strdup("copy me");
        JC_CHECK_STR(d, "copy me");
        free(d);
        JC_CHECK(jc_strdup(NULL) == NULL);
    }

    /* jc_str_edit_distance (M90). */
    {
        JC_CHECK(jc_str_edit_distance("abc", "abc") == 0);
        JC_CHECK(jc_str_edit_distance("read_file", "readfile") == 1);
        JC_CHECK(jc_str_edit_distance("search_code", "search_cod") == 1);
        JC_CHECK(jc_str_edit_distance("kitten", "sitting") == 3);
        JC_CHECK(jc_str_edit_distance("", "abc") == 3);
        JC_CHECK(jc_str_edit_distance("abc", "") == 3);
        JC_CHECK(jc_str_edit_distance(NULL, "x") == -1);
        JC_CHECK(jc_str_edit_distance("x", NULL) == -1);
    }

    /* jc_str_close_enough (M345): the shared suggest-or-stay-silent rule. */
    {
        JC_CHECK(jc_str_close_enough(4, 2) == 1);   /* floor: len 4 -> 2   */
        JC_CHECK(jc_str_close_enough(4, 3) == 0);
        JC_CHECK(jc_str_close_enough(2, 2) == 1);   /* floor holds short   */
        JC_CHECK(jc_str_close_enough(20, 4) == 1);  /* ceiling: len 20 -> 4 */
        JC_CHECK(jc_str_close_enough(20, 5) == 0);
        JC_CHECK(jc_str_close_enough(6, 3) == 1);   /* len 6 -> 3          */
        JC_CHECK(jc_str_close_enough(6, 4) == 0);
        JC_CHECK(jc_str_close_enough(9, -1) == 0);  /* error never close   */
    }

    /* jc_str_closest (M345): slash-command tables pass as-is; the returned
     * form has the slash skipped, so every caller prints its own. */
    {
        static const char *CANDS[] =
            { "/help", "/model", "review-diff", NULL };
        const char *s;
        s = jc_str_closest("hlep", CANDS);
        JC_CHECK(s != NULL && strcmp(s, "help") == 0);
        s = jc_str_closest("mdoel", CANDS);
        JC_CHECK(s != NULL && strcmp(s, "model") == 0);
        s = jc_str_closest("reveiw-diff", CANDS);
        JC_CHECK(s != NULL && strcmp(s, "review-diff") == 0);
        JC_CHECK(jc_str_closest("xylophone", CANDS) == NULL); /* wild guess */
        JC_CHECK(jc_str_closest("", CANDS) == NULL);
        JC_CHECK(jc_str_closest(NULL, CANDS) == NULL);
        JC_CHECK(jc_str_closest("help", NULL) == NULL);
    }

    /* jc_group_num (#8): grouped-thousands token display. */
    {
        char b[40];
        jc_group_num(0.0, '.', b, sizeof b);        JC_CHECK_STR(b, "0");
        jc_group_num(999.0, '.', b, sizeof b);      JC_CHECK_STR(b, "999");
        jc_group_num(1000.0, '.', b, sizeof b);     JC_CHECK_STR(b, "1.000");
        jc_group_num(1100.0, '.', b, sizeof b);     JC_CHECK_STR(b, "1.100");
        jc_group_num(1234567.0, '.', b, sizeof b);  JC_CHECK_STR(b, "1.234.567");
        jc_group_num(1234567.0, ',', b, sizeof b);  JC_CHECK_STR(b, "1,234,567");
        jc_group_num(1234567.0, ' ', b, sizeof b);  JC_CHECK_STR(b, "1 234 567");
        jc_group_num(1234567.0, 0, b, sizeof b);    JC_CHECK_STR(b, "1234567");
        /* rounds; negatives use magnitude */
        jc_group_num(1500.7, '.', b, sizeof b);     JC_CHECK_STR(b, "1.501");
        jc_group_num(-2000.0, '.', b, sizeof b);    JC_CHECK_STR(b, "2.000");
        /* tiny buffer never overflows (truncates, NUL-terminates) */
        jc_group_num(1234567.0, '.', b, 4);         JC_CHECK(b[3] == '\0');
    }

    /* jc_group_sep_audience (M555): who gets a separator, and who must not.
     *
     * THE DEFECT. A German-locale screen reader speaks `4.946` as "four Punkt
     * nine four six" -- a punctuation mark inside a numeral defeats the reader's
     * number parser, so a listener gets four digits and the name of a dot
     * instead of a number. Reported by ear by the operator. And it is NOT a
     * German problem: jc_config sets `.` as the fallback separator when the
     * locale supplies none, which is exactly what LC_ALL=C gives, so an English
     * user with no locale configured hears the same thing.
     *
     * Both directions are asserted, because only one of them is the fix and the
     * other is the thing that must not regress: a SIGHTED user keeps grouping
     * (a bare six-digit integer is harder to scan), and an accessible one gets
     * none. The two audiences want opposite things, which is why this is a
     * function and not a constant. */
    {
        char b[40];
        /* the rule itself */
        JC_CHECK(jc_group_sep_audience('.', 0) == '.');
        JC_CHECK(jc_group_sep_audience(',', 0) == ',');
        JC_CHECK(jc_group_sep_audience('.', 1) == 0);
        JC_CHECK(jc_group_sep_audience(',', 1) == 0);
        /* a configured "no separator" stays none in both modes */
        JC_CHECK(jc_group_sep_audience(0, 0) == 0);
        JC_CHECK(jc_group_sep_audience(0, 1) == 0);
        /* and the rule composed with the formatter, which is the whole point:
         * the exact string the operator's reader mispronounced, and what it
         * becomes. 4946 is the smallest interesting case -- three digits or
         * fewer are never grouped, so a test using 999 would pass on a build
         * with the fix reverted. */
        jc_group_num(4946.0, jc_group_sep_audience('.', 0), b, sizeof b);
        JC_CHECK_STR(b, "4.946");
        jc_group_num(4946.0, jc_group_sep_audience('.', 1), b, sizeof b);
        JC_CHECK_STR(b, "4946");
        jc_group_num(4946.0, jc_group_sep_audience(',', 1), b, sizeof b);
        JC_CHECK_STR(b, "4946");
    }

    /* jc_envvar_name_valid (M326e): the predicate behind doctor's apiKeyEnv
     * FAIL, the setup wizard's re-ask, and jc_proc's unset-prefix refusal.
     * The cases that matter are the KEY shapes -- those are what a user
     * pastes at a prompt asking for a variable name. */
    {
        /* valid POSIX names */
        JC_CHECK(jc_envvar_name_valid("JICHI_API_KEY") == 1);
        JC_CHECK(jc_envvar_name_valid("OPENAI_API_KEY") == 1);
        JC_CHECK(jc_envvar_name_valid("_") == 1);
        JC_CHECK(jc_envvar_name_valid("_x9") == 1);
        JC_CHECK(jc_envvar_name_valid("a") == 1);
        JC_CHECK(jc_envvar_name_valid("lower_case_ok") == 1);
        /* pasted keys: the '-' is what catches the common vendor formats */
        JC_CHECK(jc_envvar_name_valid("sk-live-SECRET123") == 0);
        JC_CHECK(jc_envvar_name_valid("sk-ant-api03-abc") == 0);
        JC_CHECK(jc_envvar_name_valid("hf_AbCdEf") == 1); /* honestly a name */
        /* a leading digit is not a name */
        JC_CHECK(jc_envvar_name_valid("9LIVES") == 0);
        /* shell metacharacters and separators */
        JC_CHECK(jc_envvar_name_valid("A=B") == 0);
        JC_CHECK(jc_envvar_name_valid("A B") == 0);
        JC_CHECK(jc_envvar_name_valid("A;rm -rf /") == 0);
        JC_CHECK(jc_envvar_name_valid("$FOO") == 0);
        JC_CHECK(jc_envvar_name_valid("A.B") == 0);
        /* degenerate */
        JC_CHECK(jc_envvar_name_valid("") == 0);
        JC_CHECK(jc_envvar_name_valid(NULL) == 0);
    }
}

/* M472: the two overflow guards in jc_sb_reserve.
 *
 * Neither was reachable when they were added -- every caller's length derives from
 * a real buffer -- and that is the argument for having them rather than against:
 * without them, "is this safe" is a claim about every caller of a general-purpose
 * primitive, to be re-made each time someone adds one. These checks make the
 * primitive answer for itself. */
void test_sb_reserve_bounds(void)
{
    struct jc_sb b;

    /* 1. An `extra` that would wrap `len + extra + 1` is refused, not accepted.
     * Before the guard this returned JC_OK -- the wrapped `need` compared small
     * against cap -- and the caller's memcpy then wrote the UN-wrapped length. */
    jc_sb_init(&b);
    jc_sb_append(&b, "seed");
    JC_CHECK(jc_sb_reserve(&b, (jc_size)-1) == JC_ERR_TOOBIG);
    JC_CHECK(jc_sb_reserve(&b, (jc_size)-2) == JC_ERR_TOOBIG);
    /* The builder is untouched by a refused reserve. */
    JC_CHECK_STR(b.data, "seed");
    JC_CHECK(b.len == 4);
    jc_sb_free(&b);

    /* 2. A size above SIZE_MAX/2 cannot be reached by doubling. Before the guard
     * the loop wrapped newcap to 0 and spun FOREVER -- a hang, not a crash, which
     * is the harder failure to diagnose. Now the allocator is asked for the exact
     * size and refuses, so this returns rather than hanging. If this test ever
     * hangs, that guard is gone. */
    jc_sb_init(&b);
    JC_CHECK(jc_sb_reserve(&b, ((jc_size)-1) / 2 + 2) == JC_ERR_OOM);
    jc_sb_free(&b);

    /* 3. ...and ordinary growth still works, including across several doublings,
     * which is the half a bounds check can quietly break. */
    jc_sb_init(&b);
    {
        int i;
        for (i = 0; i < 500; i++) {
            JC_CHECK(jc_sb_append(&b, "0123456789") == JC_OK);
        }
    }
    JC_CHECK(b.len == 5000);
    JC_CHECK(b.data != NULL && b.data[4999] == '9' && b.data[5000] == '\0');
    jc_sb_free(&b);
}

/* M534: the one boolean dialect. jichi had FOUR opinions about what "true"
 * spells -- the JSON lenient reader (true/yes/1), three hand-rolled
 * `strcmp(str,"true")` readers in the YAML frontmatter path, and `config set`'s
 * validator, which blesses on/off that no reader accepted. Two of those were
 * FENCES: a profile saying `readonly: yes` and a skill saying
 * `restrict-tools: yes` were both read as "writable", because the presence check
 * fired while the value did not match. A boolean's meaning must not depend on
 * which file it lives in. */
void test_bool_from_word(void)
{
    int v;

    /* Everything that means yes, in every spelling jichi writes or accepts. */
    v = 0; JC_CHECK(jc_bool_from_word("true", &v) == 1 && v == 1);
    v = 0; JC_CHECK(jc_bool_from_word("True", &v) == 1 && v == 1);
    v = 0; JC_CHECK(jc_bool_from_word("TRUE", &v) == 1 && v == 1);
    v = 0; JC_CHECK(jc_bool_from_word("yes", &v) == 1 && v == 1);
    v = 0; JC_CHECK(jc_bool_from_word("Yes", &v) == 1 && v == 1);
    v = 0; JC_CHECK(jc_bool_from_word("on", &v) == 1 && v == 1);
    v = 0; JC_CHECK(jc_bool_from_word("ON", &v) == 1 && v == 1);
    v = 0; JC_CHECK(jc_bool_from_word("1", &v) == 1 && v == 1);

    /* And everything that means no. */
    v = 1; JC_CHECK(jc_bool_from_word("false", &v) == 1 && v == 0);
    v = 1; JC_CHECK(jc_bool_from_word("False", &v) == 1 && v == 0);
    v = 1; JC_CHECK(jc_bool_from_word("no", &v) == 1 && v == 0);
    v = 1; JC_CHECK(jc_bool_from_word("off", &v) == 1 && v == 0);
    v = 1; JC_CHECK(jc_bool_from_word("0", &v) == 1 && v == 0);

    /* NOT a boolean: the caller's value must survive untouched, in both
     * directions, so a typo can never flip a fence either way. */
    v = 1; JC_CHECK(jc_bool_from_word("maybe", &v) == 0 && v == 1);
    v = 0; JC_CHECK(jc_bool_from_word("maybe", &v) == 0 && v == 0);
    v = 1; JC_CHECK(jc_bool_from_word("truthy", &v) == 0 && v == 1);
    v = 1; JC_CHECK(jc_bool_from_word("y", &v) == 0 && v == 1);   /* not YAML 1.1 */
    v = 1; JC_CHECK(jc_bool_from_word("2", &v) == 0 && v == 1);
    v = 1; JC_CHECK(jc_bool_from_word("", &v) == 0 && v == 1);
    v = 1; JC_CHECK(jc_bool_from_word(NULL, &v) == 0 && v == 1);
    /* A NULL out must not crash. */
    JC_CHECK(jc_bool_from_word("true", NULL) == 0);
}
