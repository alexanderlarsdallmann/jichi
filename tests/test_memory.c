/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_memory.c - persistent agent memory: pure helpers + the remember tool. */

#include "jc_test.h"
#include "jc_platform.h"   /* jc_is_dir, jc_mkdir_p */
#include "jc_snprintf.h"
#include "jc_memory.h"
#include "jc_tool.h"
#include "jc_app.h"
#include "jc_mem.h"
#include "jc_vec.h"
#include "jc_str.h"
#include "jc_sysmsg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_clean_note(void)
{
    char buf[64];

    JC_CHECK(jc_memory_clean_note("hello", buf, sizeof buf) == 5);
    JC_CHECK(strcmp(buf, "hello") == 0);

    /* Leading/trailing whitespace trimmed. */
    jc_memory_clean_note("   spaced   ", buf, sizeof buf);
    JC_CHECK(strcmp(buf, "spaced") == 0);

    /* Internal whitespace runs (incl. newlines/tabs) collapse to one space. */
    jc_memory_clean_note("a\n\nb\t c   d", buf, sizeof buf);
    JC_CHECK(strcmp(buf, "a b c d") == 0);

    /* All-whitespace => empty. */
    JC_CHECK(jc_memory_clean_note("  \n\t ", buf, sizeof buf) == 0);
    JC_CHECK(buf[0] == '\0');

    /* NULL note => empty, no crash. */
    JC_CHECK(jc_memory_clean_note(NULL, buf, sizeof buf) == 0);

    /* Respects the cap (never overflows). */
    jc_memory_clean_note("abcdefghij", buf, 5);
    JC_CHECK(strlen(buf) < 5);
}

static void test_has_line(void)
{
    const char *content = "- first note\n- second note\n";
    JC_CHECK(jc_memory_has_line(content, "first note") == 1);
    JC_CHECK(jc_memory_has_line(content, "second note") == 1);
    JC_CHECK(jc_memory_has_line(content, "third note") == 0);
    /* A prefix of an existing line is not a match (exact line only). */
    JC_CHECK(jc_memory_has_line(content, "first") == 0);
    JC_CHECK(jc_memory_has_line(content, "") == 0);
    JC_CHECK(jc_memory_has_line(NULL, "x") == 0);
}

static void test_apply_correction(void)
{
    const char *content = "- keep me\n- delete me now\n- also keep\n";
    struct jc_sb sb;
    int n;

    /* remove: drop the matching bullet, keep the rest. */
    jc_sb_init(&sb);
    n = jc_memory_apply_correction(content, "delete me", NULL, &sb);
    JC_CHECK(n == 1);
    JC_CHECK(sb.data != NULL && strstr(sb.data, "delete me") == NULL);
    JC_CHECK(strstr(sb.data, "- keep me\n") != NULL);
    JC_CHECK(strstr(sb.data, "- also keep\n") != NULL);
    jc_sb_free(&sb);

    /* replace: remove match + append the corrected note. */
    jc_sb_init(&sb);
    n = jc_memory_apply_correction(content, "delete me", "was fixed in abc123",
                                   &sb);
    JC_CHECK(n == 2); /* 1 removed + 1 added */
    JC_CHECK(strstr(sb.data, "delete me") == NULL);
    JC_CHECK(strstr(sb.data, "- was fixed in abc123\n") != NULL);
    jc_sb_free(&sb);

    /* no match: count 0 (the caller then leaves the file untouched). */
    jc_sb_init(&sb);
    JC_CHECK(jc_memory_apply_correction(content, "nonexistent", NULL, &sb) == 0);
    jc_sb_free(&sb);

    /* a replacement already present is not duplicated (still counts the drop). */
    jc_sb_init(&sb);
    n = jc_memory_apply_correction("- old\n- also keep\n", "old", "also keep",
                                   &sb);
    JC_CHECK(n == 1);
    {
        const char *f = strstr(sb.data, "also keep");
        JC_CHECK(f != NULL && strstr(f + 1, "also keep") == NULL);
    }
    jc_sb_free(&sb);

    /* guards. */
    jc_sb_init(&sb);
    JC_CHECK(jc_memory_apply_correction(NULL, "x", NULL, &sb) == 0);
    JC_CHECK(jc_memory_apply_correction("- a\n", NULL, NULL, &sb) == 0);
    JC_CHECK(jc_memory_apply_correction("- a\n", "", NULL, &sb) == 0);
    jc_sb_free(&sb);
}

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    static char buf[4096];
    size_t n;
    if (f == NULL) {
        return NULL;
    }
    n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static void test_add_and_tool(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;
    const char *dir = jc_test_tmp("jichi_mem_test");
    char path[600];
    char rm[512];
    char *content;
    int was_new = 0;

    memset(&app, 0, sizeof(app));
    app.arena = a;
    jc_vec_init(&app.read_files, sizeof(char *));
    jc_vec_init(&app.read_recs, sizeof(struct jc_read_rec));
    app.auto_approve = 1;
    jc_tool_registry_init(&reg);
    jc_tool_register_builtins(&reg);
    app.tools = &reg;

    /* Fresh workspace dir as cwd, made without a shell (M728). */
    (void)jc_test_rm_rf(dir);
    JC_CHECK(jc_mkdir_p(dir) == JC_OK);
    strcpy(app.cwd, dir);
    jc_snprintf(path, sizeof path, "%s/.jichi/memory.md", dir);

    /* remember is a registered builtin. */
    JC_CHECK(jc_tool_registry_find(&reg, "remember") != NULL);

    /* Empty initially. */
    JC_CHECK(jc_memory_load(&app) == NULL);

    /* Add a note (creates .jichi/memory.md). */
    JC_CHECK(jc_memory_add(&app, "use tabs not spaces", &was_new) == JC_OK);
    JC_CHECK(was_new == 1);
    content = slurp(path);
    JC_CHECK(content != NULL && strcmp(content, "- use tabs not spaces\n") == 0);

    /* Duplicate is not appended. */
    JC_CHECK(jc_memory_add(&app, "use tabs not spaces", &was_new) == JC_OK);
    JC_CHECK(was_new == 0);
    content = slurp(path);
    JC_CHECK(content != NULL && strcmp(content, "- use tabs not spaces\n") == 0);

    /* A note normalized to the same text is also a duplicate. */
    JC_CHECK(jc_memory_add(&app, "  use   tabs\nnot spaces ", &was_new) == JC_OK);
    JC_CHECK(was_new == 0);

    /* Load reflects the saved note. M199: the result is malloc-owned (it used to
     * sit on the session arena, leaking a copy per reload), so free it. */
    {
        char *got = jc_memory_load(&app);
        JC_CHECK(got != NULL && strstr(got, "use tabs not spaces") != NULL);
        free(got);
    }

    /* The remember tool appends a second note. */
    strcpy(rm, "{\"note\":\"prefer small commits\"}");
    jc_tool_execute(&reg, "remember", rm, &res, &app);
    JC_CHECK(res.is_error == 0);
    jc_tool_result_free(&res);
    content = slurp(path);
    if (JC_REQUIRE(content != NULL)) { /* a guard (M729) */
        JC_CHECK(strstr(content, "- use tabs not spaces\n") != NULL);
        JC_CHECK(strstr(content, "- prefer small commits\n") != NULL);
    }

    /* remember is a mutating tool (gated like other writes). */
    JC_CHECK(jc_tool_registry_find(&reg, "remember")->readonly == 0);

    /* M78: correct (supersede) a note in the real file. */
    {
        int changed = 0;
        JC_CHECK(jc_memory_correct(&app, "use tabs not spaces",
                     "spaces are fine now (M78)", &changed) == JC_OK);
        JC_CHECK(changed == 2); /* 1 removed + 1 added */
        content = slurp(path);
        if (JC_REQUIRE(content != NULL)) { /* a guard (M729) */
            JC_CHECK(strstr(content, "use tabs not spaces") == NULL);
            JC_CHECK(strstr(content, "- spaces are fine now (M78)\n") != NULL);
            /* the other note is untouched. */
            JC_CHECK(strstr(content, "- prefer small commits\n") != NULL);
        }
    }
    /* Correcting a note that isn't there is a harmless no-op (no write). */
    {
        int changed = 0;
        JC_CHECK(jc_memory_correct(&app, "no such note", NULL, &changed)
                 == JC_OK);
        JC_CHECK(changed == 0);
    }

    (void)jc_test_rm_rf(dir);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    free(app.memory); /* M199: malloc-owned since the notes reload */
    jc_tool_registry_free(&reg);
    jc_arena_free(a);
}

/* M143: an over-budget memory file is surfaced, not silent. The file keeps
 * everything on disk; only the most recent JC_MEMORY_MAX tail is loaded, and
 * the remember tool's result says so. */
static void test_over_budget(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;
    const char *dir = jc_test_tmp("jichi_mem_big_test");
    char path[600];
    char *loaded;
    FILE *f;
    int i;

    memset(&app, 0, sizeof(app));
    app.arena = a;
    jc_vec_init(&app.read_files, sizeof(char *));
    jc_vec_init(&app.read_recs, sizeof(struct jc_read_rec));
    app.auto_approve = 1;
    jc_tool_registry_init(&reg);
    jc_tool_register_builtins(&reg);
    app.tools = &reg;
    (void)jc_test_rm_rf(dir);
    {
        char jd[600];
        jc_snprintf(jd, sizeof jd, "%s/.jichi", dir);
        (void)jc_mkdir_p(jd);
    }
    /* PRECONDITION, checked once rather than guarded at every deref.
     * Until M728 this directory was made by system(), which runs /bin/sh
     * -- which Android does not have (its shell is /system/bin/sh). There
     * the directory never appeared, and every assertion below was
     * operating on NULL: the run segfaulted rather than reporting anything
     * (M457). jc_mkdir_p needs no shell, and the check stays: one check,
     * one honest red, and the remaining ~9 test files still get to run. */
    {
        char probe[600];
        jc_snprintf(probe, sizeof probe, "%s/.jichi", dir);
        if (!JC_REQUIRE(jc_is_dir(probe))) {
            jc_tool_registry_free(&reg);
            jc_arena_free(a);
            return;
        }
    }
    strcpy(app.cwd, dir);
    jc_snprintf(path, sizeof path, "%s/.jichi/memory.md", dir);

    /* No file: size 0. */
    JC_CHECK(jc_memory_file_size(&app) == 0);

    /* Write ~12 KB of bullets (over the 8 KB budget), newest last.
     * The first note has a LONG heading (60+ chars) so the dropped summary
     * exceeds 160 bytes, testing the new 200-byte field clamp. */
    f = fopen(path, "wb");
    /* Guarded, not merely checked: on a platform where system() cannot
     * create the directory above -- Android has no /bin/sh, which musl's
     * system() invokes -- this fopen returns NULL and the writes below
     * segfaulted the whole run (M457). */
    if (JC_REQUIRE(f != NULL)) {
        /* A heading long enough to trigger the 160->199+ char summary.
         * jc_memory_dropped_summary extracts headings; with 5+ chars per heading
         * and multiple headings, the summary easily exceeds 160 bytes. */
        fprintf(f, "- Build traps and architectural pitfalls: the subtle bugs that "
                  "only appear under load and the hidden memory leaks that creep in "
                  "over time\n");
        for (i = 0; i < 300; i++) {
            fprintf(f, "- old note number %d padding padding padding\n", i);
        }
        fputs("- THE NEWEST NOTE\n", f);
        fclose(f);
    }

    JC_CHECK(jc_memory_file_size(&app) > (long)JC_MEMORY_MAX);

    /* Load keeps the tail: the newest note survives, the oldest does not,
     * and the loaded block respects the budget. */
    loaded = jc_memory_load(&app);
    /* Set app->memory so the sysmsg can use it (M199: malloc-owned, freed below) */
    app.memory = loaded;
    if (JC_REQUIRE(loaded != NULL)) { /* a guard (M729) */
        JC_CHECK(strstr(loaded, "THE NEWEST NOTE") != NULL);
        JC_CHECK(strstr(loaded, "old note number 0 ") == NULL);
        JC_CHECK(strlen(loaded) <= (size_t)JC_MEMORY_MAX);
        /* M933 (D5): the truncation is stated, so the model can see its
         * memory is partial. A file that fits never sets it. */
        JC_CHECK(app.memory_truncated == 1);
        /* M965 (D5 delta): app->memory_dropped holds the dropped headings/words */
        JC_CHECK(app.memory_dropped[0] != '\0');
        /* D5 follow-up: the dropped text must be non-empty, and the sysmsg's
         * parentheses must be balanced even with a long heading that exceeds 160 bytes.
         * The long heading in the first note exercises the 200-byte field clamp. */
        {
            struct jc_sb sb;
            char *msg;
            int open_paren = 0, close_paren = 0;
            jc_sb_init(&sb);
            msg = jc_sysmsg_build_parts(&app, NULL);
            /* Count parentheses: must be balanced (no dangling ')') */
            for (i = 0; msg[i] != '\0'; i++) {
                if (msg[i] == '(') open_paren++;
                if (msg[i] == ')') close_paren++;
            }
            JC_CHECK(open_paren == close_paren);
            /* The dropped summary should appear in the sysmsg */
            JC_CHECK(strstr(msg, "(dropped: ") != NULL);
            /* The dropped text must be non-empty (invariant: memory_truncated => dropped non-empty) */
            JC_CHECK(app.memory_dropped[0] != '\0');
            jc_sb_free(&sb);
        }
    }

    /* The remember tool warns that old notes no longer reach the prompt. */
    jc_tool_execute(&reg, "remember",
                    "{\"note\":\"one more fact\"}", &res, &app);
    JC_CHECK(res.is_error == 0);
    JC_CHECK(res.content != NULL &&
             strstr(res.content, "exceeds the 8 KB injection budget") != NULL);
    jc_tool_result_free(&res);
    /* app.memory holds the same pointer as loaded (M199: malloc-owned) */
    free(app.memory);

    (void)jc_test_rm_rf(dir);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    /* memory is already freed above */
    app.memory = NULL;
    jc_tool_registry_free(&reg);
    jc_arena_free(a);
}

/* M825 (N1): the over-budget warning names WHAT dropped out of the load window. */
static void test_dropped_summary(void)
{
    char out[200];
    const char *h = "## Build traps\n- run the game\n## Old note\nbody\n";
    const char *plain = "just some plain prose with no heading at all here";

    jc_memory_dropped_summary(h, (jc_size)strlen(h), out, sizeof out);
    JC_CHECK(strcmp(out, "Build traps; Old note") == 0);

    /* Any heading level, and the leading #'s and spaces are stripped. */
    {
        const char *lv = "# Top\nx\n### Deep\n";
        jc_memory_dropped_summary(lv, (jc_size)strlen(lv), out, sizeof out);
        JC_CHECK(strcmp(out, "Top; Deep") == 0);
    }
    /* No heading: the first words of the dropped text, not empty. */
    jc_memory_dropped_summary(plain, (jc_size)strlen(plain), out, sizeof out);
    JC_CHECK(out[0] != '\0');
    JC_CHECK(strstr(out, "just some plain prose") != NULL);

    /* Degenerate inputs are safe and empty. */
    jc_memory_dropped_summary(NULL, 10, out, sizeof out);
    JC_CHECK(out[0] == '\0');
    jc_memory_dropped_summary(h, 0, out, sizeof out);
    JC_CHECK(out[0] == '\0');
    /* A tiny cap stays NUL-terminated and in-bounds. */
    {
        char small[8];
        jc_memory_dropped_summary(h, (jc_size)strlen(h), small, sizeof small);
        JC_CHECK(small[7] == '\0' || strlen(small) < 8);
    }
}

void test_memory(void)
{
    test_dropped_summary();
    test_clean_note();
    test_has_line();
    test_apply_correction();
    test_add_and_tool();
    test_over_budget();
}
