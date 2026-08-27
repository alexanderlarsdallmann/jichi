/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_patch.c - the pure exact-string edit core (jc_patch) and the atomic
 * apply_patch tool (against a temp directory). */

#include "jc_test.h"
#include "jc_patch.h"
#include "jc_tool.h"
#include "jc_app.h"
#include "jc_mem.h"
#include "jc_path.h"
#include "jc_snprintf.h"
#include "jc_vec.h"
#include "jc_str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static void test_count(void)
{
    JC_CHECK(jc_patch_count("aXbXc", "X") == 2);
    JC_CHECK(jc_patch_count("aaaa", "aa") == 2);   /* non-overlapping */
    JC_CHECK(jc_patch_count("hello", "z") == 0);
    JC_CHECK(jc_patch_count("hello", "") == 0);    /* empty needle */
    JC_CHECK(jc_patch_count(NULL, "x") == 0);
    JC_CHECK(jc_patch_count("x", NULL) == 0);
}

static void test_build(void)
{
    struct jc_sb sb;

    /* First occurrence only. */
    jc_sb_init(&sb);
    jc_patch_build("a X b X c", "X", "Y", 0, &sb);
    JC_CHECK(strcmp(sb.data, "a Y b X c") == 0);
    jc_sb_free(&sb);

    /* All occurrences. */
    jc_sb_init(&sb);
    jc_patch_build("a X b X c", "X", "Y", 1, &sb);
    JC_CHECK(strcmp(sb.data, "a Y b Y c") == 0);
    jc_sb_free(&sb);

    /* No occurrence: text unchanged. */
    jc_sb_init(&sb);
    jc_patch_build("abc", "Z", "Y", 1, &sb);
    JC_CHECK(strcmp(sb.data, "abc") == 0);
    jc_sb_free(&sb);

    /* Empty old_string: text unchanged (caller guards this case). */
    jc_sb_init(&sb);
    jc_patch_build("abc", "", "Y", 0, &sb);
    JC_CHECK(strcmp(sb.data, "abc") == 0);
    jc_sb_free(&sb);

    /* Deletion (empty replacement). */
    jc_sb_init(&sb);
    jc_patch_build("foobar", "foo", "", 0, &sb);
    JC_CHECK(strcmp(sb.data, "bar") == 0);
    jc_sb_free(&sb);
}

/* The M38 resolve+apply core: exact first, then whitespace / anchor fallback. */
static void test_apply(void)
{
    struct jc_sb sb;
    int n;
    enum jc_patch_strategy s;

    /* Exact, single. */
    jc_sb_init(&sb); n = -1;
    s = jc_patch_apply("a X b", "X", "Y", 0, 1, &sb, &n);
    JC_CHECK(s == JC_PATCH_EXACT && n == 1);
    JC_CHECK(strcmp(sb.data, "a Y b") == 0);
    jc_sb_free(&sb);

    /* Exact, replace_all. */
    jc_sb_init(&sb); n = -1;
    s = jc_patch_apply("x x x", "x", "y", 1, 1, &sb, &n);
    JC_CHECK(s == JC_PATCH_EXACT && n == 3);
    JC_CHECK(strcmp(sb.data, "y y y") == 0);
    jc_sb_free(&sb);

    /* Exact, non-unique without replace_all => ambiguous, nothing written. */
    jc_sb_init(&sb); n = -1;
    s = jc_patch_apply("x x x", "x", "y", 0, 1, &sb, &n);
    JC_CHECK(s == JC_PATCH_AMBIGUOUS && n == 3);
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);

    /* Fuzzy whitespace: tabs-vs-spaces indentation (not an exact substring).
     * The whole matched line is replaced by new_string (verbatim). */
    jc_sb_init(&sb); n = -1;
    s = jc_patch_apply("\tif (x) {\n\t\tgo();\n\t}\n",
                       "    if (x) {", "\tif (y) {", 0, 1, &sb, &n);
    JC_CHECK(s == JC_PATCH_WS && n == 1);
    JC_CHECK(strcmp(sb.data, "\tif (y) {\n\t\tgo();\n\t}\n") == 0);
    jc_sb_free(&sb);

    /* Fuzzy whitespace must be UNIQUE: two indentation-equal lines => refuse. */
    jc_sb_init(&sb); n = -1;
    s = jc_patch_apply("\tfoo\nbar\n\tfoo\n", "    foo", "Z", 0, 1, &sb, &n);
    JC_CHECK(s == JC_PATCH_AMBIGUOUS && n == 2);
    jc_sb_free(&sb);

    /* Fuzzy anchor: a misquoted interior line, boundaries intact. The whole
     * 3-line span is replaced. */
    jc_sb_init(&sb); n = -1;
    s = jc_patch_apply("begin\n  middle_actual\nend\n",
                       "begin\n  middle_WRONG\nend", "BEGIN\nMID\nEND",
                       0, 1, &sb, &n);
    JC_CHECK(s == JC_PATCH_ANCHOR && n == 1);
    JC_CHECK(strcmp(sb.data, "BEGIN\nMID\nEND\n") == 0);
    jc_sb_free(&sb);

    /* Fuzzy disabled: an otherwise-matchable edit is not found. */
    jc_sb_init(&sb);
    s = jc_patch_apply("\tif (x) {\n", "    if (x) {", "Y", 0, 0, &sb, NULL);
    JC_CHECK(s == JC_PATCH_NONE && sb.len == 0);
    jc_sb_free(&sb);

    /* Genuinely absent, even with fuzzy on. */
    jc_sb_init(&sb);
    s = jc_patch_apply("hello\nworld\n", "    nonexistent", "x", 0, 1, &sb,NULL);
    JC_CHECK(s == JC_PATCH_NONE);
    jc_sb_free(&sb);

    /* replace_all never engages the fuzzy path. */
    jc_sb_init(&sb);
    s = jc_patch_apply("\tfoo\n", "    foo", "x", 1, 1, &sb, NULL);
    JC_CHECK(s == JC_PATCH_NONE);
    jc_sb_free(&sb);

    JC_CHECK(strcmp(jc_patch_strategy_name(JC_PATCH_EXACT), "exact") == 0);
    JC_CHECK(strcmp(jc_patch_strategy_name(JC_PATCH_NONE), "none") == 0);
}

static void setup_app(struct jc_app *app, struct jc_arena *a,
                      struct jc_tool_registry *reg)
{
    memset(app, 0, sizeof(*app));
    app->arena = a;
    jc_vec_init(&app->read_files, sizeof(char *));
    jc_vec_init(&app->read_recs, sizeof(struct jc_read_rec));
    app->auto_approve = 1;
    jc_tool_registry_init(reg);
    jc_tool_register_builtins(reg);
    app->tools = reg;
    strcpy(app->cwd, jc_test_tmpdir());
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

static void test_apply_tool(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;
    const char *pa = jc_test_tmp("jichi_patch_a.txt");
    const char *pb = jc_test_tmp("jichi_patch_b.txt");
    char args[2048];

    setup_app(&app, a, &reg);

    /* apply_patch is a registered builtin. */
    JC_CHECK(jc_tool_registry_find(&reg, "apply_patch") != NULL);

    /* Seed two files and satisfy the read-before-edit guard. */
    {
        FILE *f = fopen(pa, "wb"); fputs("one two three\n", f); fclose(f);
        f = fopen(pb, "wb"); fputs("alpha beta\n", f); fclose(f);
    }
    jc_app_mark_read(&app, pa);
    jc_app_mark_read(&app, pb);

    /* Atomic multi-file, multi-edit; edits to the same file compound. */
    sprintf(args,
        "{\"edits\":["
        "{\"path\":\"%s\",\"old_string\":\"one\",\"new_string\":\"1\"},"
        "{\"path\":\"%s\",\"old_string\":\"three\",\"new_string\":\"3\"},"
        "{\"path\":\"%s\",\"old_string\":\"beta\",\"new_string\":\"B\"}]}",
        pa, pa, pb);
    jc_tool_execute(&reg, "apply_patch", args, &res, &app);
    JC_CHECK(res.is_error == 0);
    /* The result includes a per-file unified diff. */
    JC_CHECK(strstr(res.content, "@@") != NULL);
    JC_CHECK(strstr(res.content, "+1 two 3") != NULL);
    jc_tool_result_free(&res);
    JC_CHECK(strcmp(slurp(pa), "1 two 3\n") == 0);
    JC_CHECK(strcmp(slurp(pb), "alpha B\n") == 0);

    /* Atomicity: a failing edit (not found) writes nothing, even though an
     * earlier edit in the same call was valid. */
    sprintf(args,
        "{\"edits\":["
        "{\"path\":\"%s\",\"old_string\":\"1\",\"new_string\":\"ONE\"},"
        "{\"path\":\"%s\",\"old_string\":\"NOPE\",\"new_string\":\"x\"}]}",
        pa, pa);
    jc_tool_execute(&reg, "apply_patch", args, &res, &app);
    JC_CHECK(res.is_error == 1);
    jc_tool_result_free(&res);
    JC_CHECK(strcmp(slurp(pa), "1 two 3\n") == 0); /* unchanged */

    /* The read-before-edit guard applies per file. */
    {
        const char *pc = jc_test_tmp("jichi_patch_c.txt");
        FILE *f = fopen(pc, "wb"); fputs("zzz\n", f); fclose(f);
        sprintf(args,
            "{\"edits\":[{\"path\":\"%s\",\"old_string\":\"zzz\","
            "\"new_string\":\"y\"}]}", pc);
        jc_tool_execute(&reg, "apply_patch", args, &res, &app);
        JC_CHECK(res.is_error == 1);   /* not read yet */
        jc_tool_result_free(&res);
        remove(pc);
    }

    /* Non-unique without replace_all fails; with replace_all it succeeds. */
    {
        FILE *f = fopen(pa, "wb"); fputs("x x x\n", f); fclose(f);
        jc_app_mark_read(&app, pa);
        sprintf(args, "{\"edits\":[{\"path\":\"%s\",\"old_string\":\"x\","
                      "\"new_string\":\"y\"}]}", pa);
        jc_tool_execute(&reg, "apply_patch", args, &res, &app);
        JC_CHECK(res.is_error == 1);
        jc_tool_result_free(&res);

        sprintf(args, "{\"edits\":[{\"path\":\"%s\",\"old_string\":\"x\","
                      "\"new_string\":\"y\",\"replace_all\":true}]}", pa);
        jc_tool_execute(&reg, "apply_patch", args, &res, &app);
        JC_CHECK(res.is_error == 0);
        jc_tool_result_free(&res);
        JC_CHECK(strcmp(slurp(pa), "y y y\n") == 0);
    }

    /* With fuzzy on, a space-indented old_string matches a tab-indented file
     * line; the result flags the fuzzy match. (Default app here has fuzzy off,
     * matching exact-only behaviour above.) */
    {
        FILE *f = fopen(pa, "wb"); fputs("\tint x = 1;\n", f); fclose(f);
        jc_app_mark_read(&app, pa);
        app.config.fuzzy_edit = 1;
        sprintf(args, "{\"edits\":[{\"path\":\"%s\","
                      "\"old_string\":\"    int x = 1;\","
                      "\"new_string\":\"\\tint x = 2;\"}]}", pa);
        jc_tool_execute(&reg, "apply_patch", args, &res, &app);
        JC_CHECK(res.is_error == 0);
        JC_CHECK(strstr(res.content, "[fuzzy match]") != NULL);
        jc_tool_result_free(&res);
        JC_CHECK(strcmp(slurp(pa), "\tint x = 2;\n") == 0);
        app.config.fuzzy_edit = 0;
    }

    remove(pa);
    remove(pb);
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    jc_tool_registry_free(&reg);
    jc_arena_free(a);
}

/* M353: a successful M148 repair is announced ON the result -- the
 * unrepairable path always taught (schema echo), but a repairABLE call ran
 * silently on fixed arguments and the model learned "my JSON was fine".
 * Deref checks combined with their guards (the M349 rule). */
static void test_repair_note(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;
    const char *pr = jc_test_tmp("jichi_repair_a.txt");
    char args[512];

    setup_app(&app, a, &reg);
    {
        FILE *f = fopen(pr, "wb");
        fputs("hello repair\n", f);
        fclose(f);
    }
    jc_app_mark_read(&app, pr);

    /* Trailing comma: parse fails, jc_jsonrepair fixes it, the edit runs --
     * and the result must say so, beside the tool's own output. */
    sprintf(args,
        "{\"path\":\"%s\",\"old_string\":\"hello\",\"new_string\":\"hi\",}",
        pr);
    jc_tool_execute(&reg, "edit_file", args, &res, &app);
    JC_CHECK(res.is_error == 0);
    JC_CHECK(res.content != NULL && strstr(res.content, "@@") != NULL);
    JC_CHECK(res.content != NULL &&
             strstr(res.content, "[note: the arguments you sent") != NULL);
    JC_CHECK(res.content != NULL &&
             strstr(res.content, "strictly valid JSON") != NULL);
    jc_tool_result_free(&res);

    /* The pair: strictly valid arguments carry no note. */
    sprintf(args,
        "{\"path\":\"%s\",\"old_string\":\"hi\",\"new_string\":\"hey\"}",
        pr);
    jc_tool_execute(&reg, "edit_file", args, &res, &app);
    JC_CHECK(res.is_error == 0);
    JC_CHECK(res.content == NULL ||
             strstr(res.content, "[note: the arguments") == NULL);
    jc_tool_result_free(&res);

    /* Unrepairable garbage keeps the schema echo, and no repair note. */
    jc_tool_execute(&reg, "edit_file", "not json at all {{{", &res, &app);
    JC_CHECK(res.is_error == 1);
    JC_CHECK(res.content != NULL &&
             strstr(res.content, "could not parse tool arguments") != NULL);
    JC_CHECK(res.content == NULL ||
             strstr(res.content, "[note: the arguments") == NULL);
    jc_tool_result_free(&res);

    remove(pr);
    /* The teardown its siblings all carry -- omitted in the M353 first
     * draft, and only LeakSanitizer notices (192 bytes across the registry
     * and read_files vecs), which only fires when `make ci` actually runs. */
    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    jc_tool_registry_free(&reg);
    jc_arena_free(a);
}

static void test_nearmatch(void)
{
    struct jc_sb sb;
    const char *text =
        "fn foo(a: i32) void {\n"
        "    const bar = compute(a);\n"
        "    return bar;\n"
        "}\n";

    /* A stale old_string whose first line names `compute` + `bar` should point at
     * the matching line, with a numbered excerpt. */
    jc_sb_init(&sb);
    jc_patch_nearmatch_hint(text, "    const bar = compute(a, b);\n", &sb);
    JC_CHECK(sb.data != NULL && sb.len > 0);
    JC_CHECK(strstr(sb.data, "hint:") != NULL);
    JC_CHECK(strstr(sb.data, "2| ") != NULL);            /* the matching line no. */
    JC_CHECK(strstr(sb.data, "const bar = compute(a)") != NULL);
    jc_sb_free(&sb);

    /* No shared significant token => no hint appended. */
    jc_sb_init(&sb);
    jc_patch_nearmatch_hint(text, "zzz qqq wwww\n", &sb);
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);

    /* Empty / NULL inputs are safe no-ops. */
    jc_sb_init(&sb);
    jc_patch_nearmatch_hint("", "abc", &sb);
    jc_patch_nearmatch_hint(text, "", &sb);
    jc_patch_nearmatch_hint(NULL, "abc", &sb);
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);
}

/* M138: the write-phase rollback. A failure while committing the validated
 * buffers must put the originals back in the files already written and
 * report every file's state -- all-or-nothing holds on disk too. */
static void test_apply_tool_write_failure(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_tool_result res;
    char rootc[JC_PATH_MAX];
    char refc[JC_PATH_MAX];
    char pa[JC_PATH_MAX];
    char pb[JC_PATH_MAX];
    char *refptr;
    char args[2048];
    FILE *f;

    /* Fence seam (deterministic, root-proof): A under the workspace root
     * (writable), B under a reference root (readable, write-DENIED; M54) --
     * the write loop commits A, is denied at B, and must revert A. */
    mkdir(jc_test_tmp("jichi_patch_root"), 0755);
    mkdir(jc_test_tmp("jichi_patch_ref"), 0755);
    if (jc_path_resolve(jc_test_tmp("jichi_patch_root"), rootc, sizeof(rootc)) != JC_OK
        || jc_path_resolve(jc_test_tmp("jichi_patch_ref"), refc, sizeof(refc))
           != JC_OK) {
        jc_arena_free(a);
        return; /* cannot resolve /tmp: skip rather than false-fail */
    }

    setup_app(&app, a, &reg);
    app.config.path_fence = 1; /* force the fence on */
    memcpy(app.root, rootc, strlen(rootc) + 1);
    jc_vec_init(&app.config.reference_roots, sizeof(char *));
    refptr = jc_arena_strdup(a, refc);
    jc_vec_push(&app.config.reference_roots, &refptr);

    jc_snprintf(pa, sizeof(pa), "%s/a.txt", rootc);
    jc_snprintf(pb, sizeof(pb), "%s/b.txt", refc);
    f = fopen(pa, "wb"); fputs("one two three\n", f); fclose(f);
    f = fopen(pb, "wb"); fputs("alpha beta\n", f); fclose(f);
    jc_app_mark_read(&app, pa);
    jc_app_mark_read(&app, pb);

    /* A first (touched first => written first), then B (denied). */
    jc_snprintf(args, sizeof(args),
        "{\"edits\":["
        "{\"path\":\"%s\",\"old_string\":\"two\",\"new_string\":\"2\"},"
        "{\"path\":\"%s\",\"old_string\":\"beta\",\"new_string\":\"B\"}]}",
        pa, pb);
    jc_tool_execute(&reg, "apply_patch", args, &res, &app);
    JC_CHECK(res.is_error == 1);
    JC_CHECK(res.content != NULL &&
             strstr(res.content, "reverted to original") != NULL);
    JC_CHECK(res.content != NULL &&
             strstr(res.content, "denied") != NULL);
    JC_CHECK(res.content != NULL &&
             strstr(res.content, "no edits from this call are applied")
             != NULL);
    jc_tool_result_free(&res);
    /* The load-bearing check: A was written, then reverted ON DISK. */
    JC_CHECK(strcmp(slurp(pa), "one two three\n") == 0);
    JC_CHECK(strcmp(slurp(pb), "alpha beta\n") == 0);

    remove(pa);
    remove(pb);
    rmdir(jc_test_tmp("jichi_patch_root"));
    rmdir(jc_test_tmp("jichi_patch_ref"));
    jc_vec_free(&app.config.reference_roots);

    /* chmod seam (the JC_ERR_IO / restore-attempt branch): a 0444 target
     * makes fopen("wb") fail. Root (CAP_DAC_OVERRIDE) writes anyway, so
     * skip under euid 0 and skip at runtime if the write succeeded. */
    if (geteuid() != 0) {
        const char *qa = jc_test_tmp("jichi_patch_wf_a.txt");
        const char *qb = jc_test_tmp("jichi_patch_wf_b.txt");
        app.config.path_fence = 0;
        f = fopen(qa, "wb"); fputs("one two three\n", f); fclose(f);
        f = fopen(qb, "wb"); fputs("alpha beta\n", f); fclose(f);
        jc_app_mark_read(&app, qa);
        jc_app_mark_read(&app, qb);
        chmod(qb, 0444);
        jc_snprintf(args, sizeof(args),
            "{\"edits\":["
            "{\"path\":\"%s\",\"old_string\":\"two\",\"new_string\":\"2\"},"
            "{\"path\":\"%s\",\"old_string\":\"beta\",\"new_string\":\"B\"}]}",
            qa, qb);
        jc_tool_execute(&reg, "apply_patch", args, &res, &app);
        if (res.is_error == 1) { /* else: FS ignored the mode; skip asserts */
            JC_CHECK(res.content != NULL &&
                     strstr(res.content, "write failed") != NULL);
            JC_CHECK(strcmp(slurp(qa), "one two three\n") == 0); /* reverted */
            JC_CHECK(strcmp(slurp(qb), "alpha beta\n") == 0);    /* intact  */
        }
        jc_tool_result_free(&res);
        chmod(qb, 0644);
        remove(qa);
        remove(qb);
    }

    jc_vec_free(&app.read_files);
    jc_vec_free(&app.read_recs);
    jc_tool_registry_free(&reg);
    jc_arena_free(a);
}

/* M208: the ambiguous-match hint names WHERE the collisions are. Measured: 11 of
 * 14 failed edits across four dogfood drives were "old_string is not unique",
 * because the file held 21 near-identical test blocks. A count plus "add more
 * surrounding context" is advice the model cannot act on without knowing which
 * places collided, so every retry was a fresh guess. */
static void test_matchlines_hint(void)
{
    struct jc_sb sb;
    /* Three near-identical blocks, exactly the shape that caused the loop. */
    const char *text =
        "test \"a\" {\n"
        "    const allocator = std.testing.allocator;\n"
        "    try run();\n"
        "}\n"
        "\n"
        "test \"b\" {\n"
        "    const allocator = std.testing.allocator;\n"
        "    try run();\n"
        "}\n"
        "\n"
        "test \"c\" {\n"
        "    const allocator = std.testing.allocator;\n"
        "    try run();\n"
        "}\n";

    jc_sb_init(&sb);
    jc_patch_matchlines_hint(text,
        "    const allocator = std.testing.allocator;\n", &sb);
    JC_CHECK(sb.data != NULL && sb.len > 0);
    JC_CHECK(strstr(sb.data, "hint:") != NULL);
    /* the three colliding lines, named exactly and in order */
    JC_CHECK(strstr(sb.data, "it matches at line 2, 7, 12") != NULL);
    JC_CHECK(strstr(sb.data, "replace_all") != NULL);
    jc_sb_free(&sb);

    /* A unique needle still reports its single line (the caller only invokes
     * this on AMBIGUOUS, but the helper must not lie if asked). */
    jc_sb_init(&sb);
    jc_patch_matchlines_hint(text, "test \"b\" {\n", &sb);
    JC_CHECK(sb.data != NULL &&
             strstr(sb.data, "it matches at line 6 ") != NULL);
    jc_sb_free(&sb);

    /* No match => nothing appended. */
    jc_sb_init(&sb);
    jc_patch_matchlines_hint(text, "nowhere at all\n", &sb);
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);

    /* Empty / NULL inputs are safe no-ops. */
    jc_sb_init(&sb);
    jc_patch_matchlines_hint("", "abc", &sb);
    jc_patch_matchlines_hint(text, "", &sb);
    jc_patch_matchlines_hint(NULL, "abc", &sb);
    jc_patch_matchlines_hint(text, "abc", NULL);
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);

    /* More than the cap: the list is bounded and says it was truncated. */
    {
        struct jc_sb big;
        int i;
        jc_sb_init(&big);
        for (i = 0; i < 20; i++) {
            jc_sb_append(&big, "dup\n");
        }
        jc_sb_init(&sb);
        jc_patch_matchlines_hint(big.data, "dup\n", &sb);
        JC_CHECK(sb.data != NULL && strstr(sb.data, "...") != NULL);
        jc_sb_free(&sb);
        jc_sb_free(&big);
    }
}

void test_patch(void)
{
    test_count();
    test_build();
    test_apply();
    test_apply_tool();
    test_apply_tool_write_failure();
    test_repair_note();
    test_nearmatch();
    test_matchlines_hint();
}
