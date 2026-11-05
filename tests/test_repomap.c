/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_repomap.c - unit tests for the pure repo-map symbol scanner. */

#include "jc_test.h"
#include "jc_repomap.h"
#include "jc_walk.h"
#include "jc_str.h"
#include "jc_snprintf.h"
#include "jc_app.h"
#include "jc_mem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

static int has(struct jc_vec *v, const char *name)
{
    jc_size i;
    for (i = 0; i < v->len; i++) {
        char *s = JC_VEC_STR(v, i);
        if (s != NULL && strcmp(s, name) == 0) return 1;
    }
    return 0;
}

/* Forward declarations for tests defined later in this file */
static void test_repomap_outside_link(void);

static void free_syms(struct jc_vec *v)
{
    jc_size i;
    for (i = 0; i < v->len; i++) free(JC_VEC_STR(v, i));
    jc_vec_free(v);
}

static void test_c(void)
{
    const char *src =
        "#include <stdio.h>\n"
        "void bar(void);\n"                 /* prototype: excluded            */
        "struct my_s {\n"
        "    int a;\n"
        "};\n"
        "typedef enum { A, B } my_enum;\n"
        "int foo(int x)\n"
        "{\n"
        "    if (x) { return 1; }\n"        /* indented control: excluded     */
        "    return 0;\n"
        "}\n"
        "while (going) {\n";                /* col-0 control kw: excluded      */
    struct jc_vec v;
    jc_vec_init(&v, sizeof(char *));
    jc_repomap_scan("c", src, &v);
    JC_CHECK(has(&v, "foo"));
    JC_CHECK(has(&v, "my_s"));
    JC_CHECK(has(&v, "my_enum"));
    JC_CHECK(!has(&v, "bar"));
    JC_CHECK(!has(&v, "if"));
    JC_CHECK(!has(&v, "while"));
    free_syms(&v);
}

static void test_py(void)
{
    const char *src =
        "import os\n"
        "def hello(x):\n"
        "    return x\n"
        "class Foo(Base):\n"
        "    def method(self):\n"           /* indented method: excluded      */
        "        pass\n"
        "async def fetch(u):\n"
        "    pass\n";
    struct jc_vec v;
    jc_vec_init(&v, sizeof(char *));
    jc_repomap_scan("py", src, &v);
    JC_CHECK(has(&v, "hello"));
    JC_CHECK(has(&v, "Foo"));
    JC_CHECK(has(&v, "fetch"));
    JC_CHECK(!has(&v, "method"));
    free_syms(&v);
}

/* M523: non-ASCII identifiers. Python 3 permits them, and a Korean or Japanese
 * developer writing `def 계산(a, b)` got a repo map that listed the FILE and no
 * symbols at all -- measured on a real workspace, where 계산기.py and 計算.py
 * both appeared bare. The identifier predicate was ASCII-only, so the scanner
 * saw `def ` followed by nothing it recognised as a name. The map is charged
 * against every request, so a file that contributes only its own path is paying
 * rent for nothing. */
static void test_py_non_ascii_identifiers(void)
{
    const char *src =
        "def \xea\xb3\x84\xec\x82\xb0(a, b):\n"        /* def 계산  (Korean)   */
        "    return a + b\n"
        "def \xe5\x8a\xa0\xe7\xae\x97(a, b):\n"        /* def 加算  (Japanese) */
        "    return a + b\n"
        "class \xe8\xa8\x88\xe7\xae\x97\xe5\x99\xa8:\n" /* class 計算器      */
        "    pass\n"
        "def plain(x):\n"
        "    return x\n";
    struct jc_vec v;
    jc_vec_init(&v, sizeof(char *));
    jc_repomap_scan("py", src, &v);
    JC_CHECK(has(&v, "\xea\xb3\x84\xec\x82\xb0"));
    JC_CHECK(has(&v, "\xe5\x8a\xa0\xe7\xae\x97"));
    JC_CHECK(has(&v, "\xe8\xa8\x88\xe7\xae\x97\xe5\x99\xa8"));
    JC_CHECK(has(&v, "plain"));   /* ASCII still works, unchanged */
    free_syms(&v);
}

static void test_go(void)
{
    const char *src =
        "package main\n"
        "func main() {\n}\n"
        "func (s *Srv) Handle(w int) {\n}\n"
        "type Server struct {\n}\n";
    struct jc_vec v;
    jc_vec_init(&v, sizeof(char *));
    jc_repomap_scan("go", src, &v);
    JC_CHECK(has(&v, "main"));
    JC_CHECK(has(&v, "Handle"));
    JC_CHECK(has(&v, "Server"));
    free_syms(&v);
}

static void test_rs(void)
{
    const char *src =
        "pub fn launch() {}\n"
        "struct Config {}\n"
        "async fn run() {}\n"
        "pub struct Public {}\n";
    struct jc_vec v;
    jc_vec_init(&v, sizeof(char *));
    jc_repomap_scan("rs", src, &v);
    JC_CHECK(has(&v, "launch"));
    JC_CHECK(has(&v, "Config"));
    JC_CHECK(has(&v, "run"));
    JC_CHECK(has(&v, "Public"));
    free_syms(&v);
}

static void test_js(void)
{
    const char *src =
        "import x from 'y';\n"
        "export function init() {}\n"
        "export const API_URL = 'http://x';\n"
        "class Widget {}\n";
    struct jc_vec v;
    jc_vec_init(&v, sizeof(char *));
    jc_repomap_scan("ts", src, &v);
    JC_CHECK(has(&v, "init"));
    JC_CHECK(has(&v, "API_URL"));
    JC_CHECK(has(&v, "Widget"));
    free_syms(&v);
}

static void test_rkt(void)
{
    const char *src =
        "#lang racket/base\n"
        ";; a comment (define ignore-me)\n"
        "(require \"util.rkt\")\n"
        "(provide run-demo)\n"
        "(define x 5)\n"
        "(define (run-demo) (void))\n"
        "(define (stack-empty? s) (null? s))\n"
        "(define (string->thing s) s)\n"
        "(struct point (x y))\n"
        "(define-struct (p3d point) (z))\n"
        "  (define (indented-helper) 1)\n";   /* nested: excluded */
    struct jc_vec v;
    jc_vec_init(&v, sizeof(char *));
    jc_repomap_scan("rkt", src, &v);
    JC_CHECK(has(&v, "x"));
    JC_CHECK(has(&v, "run-demo"));
    JC_CHECK(has(&v, "stack-empty?"));        /* '?' and '-' kept */
    JC_CHECK(has(&v, "string->thing"));       /* '>' kept */
    JC_CHECK(has(&v, "point"));
    JC_CHECK(has(&v, "p3d"));
    JC_CHECK(!has(&v, "indented-helper"));    /* indent != 0 */
    JC_CHECK(!has(&v, "run-demo)"));          /* delimiter stops the name */
    JC_CHECK(!has(&v, "require"));
    JC_CHECK(!has(&v, "provide"));
    free_syms(&v);
}

static void test_zig(void)
{
    const char *src =
        "const std = @import(\"std\");\n"
        "pub fn add(a: i32, b: i32) i32 { return a + b; }\n"
        "fn helper() void {}\n"
        "const Point = struct {\n"
        "    x: i32,\n"
        "    pub fn norm(self: *Point) i32 { return self.x; }\n"  /* indented */
        "};\n"
        "pub const MAX = 100;\n"
        "test \"adds\" { _ = add(1, 2); }\n";
    struct jc_vec v;
    jc_vec_init(&v, sizeof(char *));
    jc_repomap_scan("zig", src, &v);
    JC_CHECK(has(&v, "add"));
    JC_CHECK(has(&v, "helper"));
    JC_CHECK(has(&v, "Point"));
    JC_CHECK(has(&v, "MAX"));
    JC_CHECK(has(&v, "std"));                 /* top-level import binding */
    JC_CHECK(!has(&v, "norm"));               /* indented method excluded */
    JC_CHECK(!has(&v, "test"));               /* test block is not a def */
    free_syms(&v);
}

static void test_clj(void)
{
    const char *src =
        "(ns my.app (:require [clojure.string :as str]))\n"
        "(def ^:private secret 42)\n"
        "(defn my-fn [x] (* x x))\n"
        "(defn- helper [] nil)\n"
        "(defrecord Point [x y])\n"
        "  (defn nested [] 1)\n";                /* indented: excluded */
    struct jc_vec v;
    jc_vec_init(&v, sizeof(char *));
    jc_repomap_scan("clj", src, &v);
    JC_CHECK(has(&v, "my.app"));
    JC_CHECK(has(&v, "secret"));                  /* metadata skipped */
    JC_CHECK(has(&v, "my-fn"));
    JC_CHECK(has(&v, "helper"));
    JC_CHECK(has(&v, "Point"));
    JC_CHECK(!has(&v, "nested"));
    JC_CHECK(!has(&v, "^:private"));
    free_syms(&v);
}

static void test_ex(void)
{
    const char *src =
        "defmodule MyApp.User do\n"
        "  defstruct [:name, :age]\n"
        "  def greet(name), do: name\n"
        "  defp valid?(x) do\n"
        "    x > 0\n"
        "  end\n"
        "  defmacro gen!(x), do: x\n"
        "end\n";
    struct jc_vec v;
    jc_vec_init(&v, sizeof(char *));
    jc_repomap_scan("ex", src, &v);
    JC_CHECK(has(&v, "MyApp.User"));              /* dotted module name */
    JC_CHECK(has(&v, "greet"));                   /* def nested in module */
    JC_CHECK(has(&v, "valid?"));                  /* ? kept */
    JC_CHECK(has(&v, "gen!"));                    /* ! kept */
    JC_CHECK(!has(&v, "name"));                   /* defstruct has no name */
    free_syms(&v);
}

static void test_erl(void)
{
    const char *src =
        "-module(math).\n"
        "-export([factorial/1]).\n"
        "-record(state, {count = 0}).\n"
        "-define(MAX, 100).\n"
        "factorial(0) -> 1;\n"
        "factorial(N) -> N * factorial(N - 1).\n";
    struct jc_vec v;
    int n;
    jc_vec_init(&v, sizeof(char *));
    jc_repomap_scan("erl", src, &v);
    JC_CHECK(has(&v, "state"));                   /* -record */
    JC_CHECK(has(&v, "MAX"));                      /* -define */
    JC_CHECK(has(&v, "factorial"));
    JC_CHECK(!has(&v, "math"));                   /* -module ignored */
    /* the two factorial clauses collapse to one entry (add_sym dedup) */
    n = 0;
    {
        jc_size i;
        for (i = 0; i < v.len; i++) {
            const char *sym = JC_VEC_STR(&v, i);
            if (sym != NULL && strcmp(sym, "factorial") == 0) n++;
        }
    }
    JC_CHECK(n == 1);
    free_syms(&v);
}

static void test_hs(void)
{
    const char *src =
        "module Data.Stack (push) where\n"
        "data Tree a = Leaf | Node a (Tree a)\n"
        "newtype Wrap = Wrap Int\n"
        "class (Eq a) => Ord a where\n"
        "foldl' :: (b -> a -> b) -> b -> [a] -> b\n"
        "foldl' f z xs = z\n"                      /* unsignatured clause: not re-added */
        "  helper x = x\n";                        /* indented: excluded */
    struct jc_vec v;
    jc_vec_init(&v, sizeof(char *));
    jc_repomap_scan("hs", src, &v);
    JC_CHECK(has(&v, "Data.Stack"));
    JC_CHECK(has(&v, "Tree"));
    JC_CHECK(has(&v, "Wrap"));
    JC_CHECK(has(&v, "Ord"));                      /* not "Eq" (the constraint) */
    JC_CHECK(!has(&v, "Eq"));
    JC_CHECK(has(&v, "foldl'"));                   /* prime kept; from signature */
    JC_CHECK(!has(&v, "helper"));
    free_syms(&v);
}

static void test_scheme(void)
{
    const char *src =
        "(define (f x) (+ x 1))\n"
        "(define-record-type point (make-point x y) point?)\n"
        "(define-public (g) 'ok)\n"
        "(define pi 3.14159)\n";
    struct jc_vec v;
    jc_vec_init(&v, sizeof(char *));
    jc_repomap_scan("scm", src, &v);              /* scheme/guile via RL_RKT */
    JC_CHECK(has(&v, "f"));
    JC_CHECK(has(&v, "point"));
    JC_CHECK(has(&v, "g"));
    JC_CHECK(has(&v, "pi"));
    free_syms(&v);
}

static void test_unknown_and_empty(void)
{
    struct jc_vec v;
    jc_vec_init(&v, sizeof(char *));
    JC_CHECK(jc_repomap_scan("txt", "hello world\nfoo bar\n", &v) == 0);
    JC_CHECK(jc_repomap_scan("c", "", &v) == 0);
    JC_CHECK(jc_repomap_scan("c", NULL, &v) == 0);
    JC_CHECK(v.len == 0);
    free_syms(&v);
}

static void test_cap(void)
{
    struct jc_sb in;
    struct jc_vec v;
    int i;
    int n;

    jc_sb_init(&in);
    for (i = 0; i < 100; i++) jc_sb_append_fmt(&in, "def f%d():\n    pass\n", i);
    jc_vec_init(&v, sizeof(char *));
    n = jc_repomap_scan("py", in.data, &v);
    JC_CHECK(n == JC_REPOMAP_SCAN_CAP);
    JC_CHECK(v.len == (jc_size)JC_REPOMAP_SCAN_CAP);
    free_syms(&v);
    jc_sb_free(&in);
}

void test_repomap(void)
{
    test_c();
    test_py();
    test_py_non_ascii_identifiers();
    test_go();
    test_rs();
    test_js();
    test_rkt();
    test_zig();
    test_clj();
    test_ex();
    test_erl();
    test_hs();
    test_scheme();
    test_unknown_and_empty();
    test_cap();
    test_repomap_outside_link();
}

/* M991 (D-27/D-28/D-29/D-30): a symlink that points outside the workspace must
 * NOT appear in the repo map. This is tested through the shared walker. */
static void test_repomap_outside_link(void)
{
    struct jc_walk_ctx ctx;
    char tmpdir[2048];
    char outside_dir[2048];
    char link_path[2048];
    enum jc_walk_kind k;

    /* Create a temporary workspace root */
    jc_snprintf(tmpdir, sizeof(tmpdir), "%s/test_repomap_outside_%d",
                jc_test_tmpdir(), (int)getpid());
    (void)jc_test_rm_rf(tmpdir);
    if (!JC_REQUIRE(mkdir(tmpdir, 0755) == 0)) {
        return;
    }

    /* Create an outside directory with a secret marker */
    jc_snprintf(outside_dir, sizeof(outside_dir), "%s/test_repomap_secret_%d",
                jc_test_tmpdir(), (int)getpid());
    (void)jc_test_rm_rf(outside_dir);
    if (!JC_REQUIRE(mkdir(outside_dir, 0755) == 0)) {
        (void)jc_test_rm_rf(tmpdir);
        return;
    }
    {
        char outside_file[2048];
        FILE *fp;
        jc_snprintf(outside_file, sizeof(outside_file), "%s/secret.txt", outside_dir);
        fp = fopen(outside_file, "wb");
        if (!JC_REQUIRE(fp != NULL)) {
            (void)jc_test_rm_rf(tmpdir);
            (void)jc_test_rm_rf(outside_dir);
            return;
        }
        fprintf(fp, "OUTSIDE_MARKER\n");
        fclose(fp);
    }

    /* Create a symlink in the workspace pointing outside */
    jc_snprintf(link_path, sizeof(link_path), "%s/linked", tmpdir);
    if (!JC_REQUIRE(symlink(outside_dir, link_path) == 0)) {
        (void)jc_test_rm_rf(tmpdir);
        (void)jc_test_rm_rf(outside_dir);
        return;
    }

    /* Init context */
    if (!JC_REQUIRE(jc_walk_ctx_init(&ctx, tmpdir, 10, 1000) == 0)) {
        (void)jc_test_rm_rf(tmpdir);
        (void)jc_test_rm_rf(outside_dir);
        return;
    }

    /* The link should be skipped because it points outside */
    k = jc_walk_classify(&ctx, link_path, 1);
    JC_CHECK(k == JC_WALK_SKIP);
    JC_CHECK(ctx.skipped_outside == 1);

    /* The directory it points to should also be skipped */
    k = jc_walk_classify(&ctx, outside_dir, 1);
    JC_CHECK(k == JC_WALK_SKIP);
    JC_CHECK(ctx.skipped_outside == 2);  /* should count the direct out-of-root case too */

    jc_walk_ctx_free(&ctx);
    jc_test_rm_rf(tmpdir);
    jc_test_rm_rf(outside_dir);
}

/* M520: the directory-skip decision, which BOTH walkers (repo map and search
 * index) now share. It had no test at all -- and the walk it guards is what
 * decides whether the map a model sees describes the project or its
 * dependencies. Measured on a real tree: a Python virtualenv called `advenv`
 * (not a dotdir, so no built-in rule caught it) filled the entire visible map
 * with pip internals while the project's own sources sat below the truncation
 * line. Hence `ignoreDirs`, and hence this. */
void test_walk_skip_dir(void)
{
    struct jc_vec extra;
    char *a = jc_strdup("advenv");
    char *b = jc_strdup("rulebooks");
    char *empty = jc_strdup("");

    /* --- with no configured extras: the built-ins, and nothing else ------- */
    JC_CHECK(jc_walk_skip_dir(".git", NULL) == 1);
    JC_CHECK(jc_walk_skip_dir(".zig-cache", NULL) == 1);
    JC_CHECK(jc_walk_skip_dir("node_modules", NULL) == 1);
    JC_CHECK(jc_walk_skip_dir("target", NULL) == 1);
    JC_CHECK(jc_walk_skip_dir("build", NULL) == 1);
    JC_CHECK(jc_walk_skip_dir("dist", NULL) == 1);
    JC_CHECK(jc_walk_skip_dir("__pycache__", NULL) == 1);
    JC_CHECK(jc_walk_skip_dir("src", NULL) == 0);
    JC_CHECK(jc_walk_skip_dir("advenv", NULL) == 0);   /* the whole defect */
    /* A defensive pair: a NULL or empty name is not a directory to descend. */
    JC_CHECK(jc_walk_skip_dir(NULL, NULL) == 1);
    JC_CHECK(jc_walk_skip_dir("", NULL) == 1);

    /* --- with extras: the operator's names skip too ----------------------- */
    jc_vec_init(&extra, sizeof(char *));
    jc_vec_push(&extra, &a);
    jc_vec_push(&extra, &b);
    jc_vec_push(&extra, &empty);        /* must not match everything */

    JC_CHECK(jc_walk_skip_dir("advenv", &extra) == 1);
    JC_CHECK(jc_walk_skip_dir("rulebooks", &extra) == 1);
    /* Exact names only -- a prefix or suffix must NOT be swept up, or one
     * entry silently hides a sibling the operator meant to keep. */
    JC_CHECK(jc_walk_skip_dir("advenv2", &extra) == 0);
    JC_CHECK(jc_walk_skip_dir("adven", &extra) == 0);
    JC_CHECK(jc_walk_skip_dir("my-rulebooks", &extra) == 0);
    JC_CHECK(jc_walk_skip_dir("src", &extra) == 0);
    /* and the built-ins still apply when extras are present */
    JC_CHECK(jc_walk_skip_dir("__pycache__", &extra) == 1);
    JC_CHECK(jc_walk_skip_dir(".git", &extra) == 1);

    jc_vec_free(&extra);
    free(a); free(b); free(empty);
}

/* M1081 (F-01): the build arena's peak. The rendered map is bounded to
 * JC_REPOMAP_MAX (12 KiB) by the listing loop, but the arena that held the
 * scanned-file TEXTS was released only after the whole loop ran -- so its
 * high-water mark grew by every file read before truncation, i.e. to the SUM
 * of the files, not one of them. Measured on the F-01 fixture (40 x ~56 KiB
 * symbol-sparse .c files): arena peak 2,297,920 B against a 776 B rendered
 * map (2026-10-29-F01-confirm.md). The 12 KiB stop bounds the OUTPUT, not the
 * RETENTION.
 *
 * This test is the finding's red-first recipe: ~40 symbol-sparse files, each
 * one top-level function whose body is a huge string literal (one short map
 * line, the listing far under 12 KiB, so every file is scanned and kept).
 * Assert the arena peak <= max file size + rendered listing + a small
 * per-file margin. Before the fix it reads ~sum-of-files (~2.2 MB) and fails;
 * after, ~one file (~56 KiB + path list) and passes.
 *
 * No output change is permitted: the rendered map must still list all 40
 * functions, untruncated. */
void test_repomap_peak_is_one_file(void)
{
    enum { N = 40 };
    char dir[512];
    char file[560];
    char *map;
    struct jc_app app;
    struct jc_arena *a;
    jc_size fbytes = 60000;     /* >= one fixture file (~58.3 KB), < the 512 KiB cap */
    jc_size total;
    jc_size limit;
    jc_size peak;
    int i;

    a = jc_arena_new(0);
    if (!JC_REQUIRE(a != NULL)) return;

    jc_snprintf(dir, sizeof(dir), "%s/test_repomap_peak_%d",
                jc_test_tmpdir(), (int)getpid());
    (void)jc_test_rm_rf(dir);
    if (!JC_REQUIRE(mkdir(dir, 0755) == 0)) {
        jc_arena_free(a);
        return;
    }

    /* Each file: one top-level function whose body is ~58 KiB of short,
     * indented const declarations (no new symbols, so the scanner emits
     * exactly one line per file: "modNN.c: f_NN"). Short lines, not one
     * 57 KB line, because jc_repomap_scan copies each line into a 1 KB
     * buffer and advances char by char (a single long line would be
     * quadratic). The 40-file listing is ~720 B -- far under the 12 KiB
     * limit, so every file is scanned. */
    {
        char pat[600];
        memset(pat, 0, sizeof(pat));
        for (i = 0; i < 580; i++) pat[i] = (char)('a' + (i % 26));
        total = 0;
        for (i = 0; i < N; i++) {
            FILE *f;
            int row;
            jc_size wrote = 0;
            jc_snprintf(file, sizeof(file), "%s/mod%02d.c", dir, i);
            f = fopen(file, "wb");
            if (!JC_REQUIRE(f != NULL)) break;
            wrote += (jc_size)fprintf(f, "int f_%02d(void)\n{\n", i);
            for (row = 0; row < 96; row++) {
                wrote += (jc_size)fprintf(f,
                    "    const char *s%d = \"%s\";\n", row, pat);
            }
            wrote += (jc_size)fprintf(f, "return 0;\n}\n");
            JC_REQUIRE(wrote > 0);
            if (fclose(f) != 0) JC_CHECK(0);
            total += wrote;
        }
    }
    if (!JC_REQUIRE(total >= (jc_size)N * (jc_size)50000)) {
        (void)jc_test_rm_rf(dir);
        jc_arena_free(a);
        return;
    }

    memset(&app, 0, sizeof(app));
    app.arena = a;
    app.scratch = a;
    jc_snprintf(app.cwd, sizeof(app.cwd), "%s", dir);
    app.config.repo_map_limit = 0;      /* default: JC_REPOMAP_MAX */
    limit = JC_REPOMAP_MAX;

    map = jc_repomap_build(&app);
    if (!JC_REQUIRE(map != NULL)) {
        (void)jc_test_rm_rf(dir);
        jc_arena_free(a);
        return;
    }
    /* The rendered map is UNCHANGED by the fix: every file still appears,
     * untruncated. */
    JC_CHECK(strstr(map, "mod00.c: f_00") != NULL);
    JC_CHECK(strstr(map, "mod39.c: f_39") != NULL);
    JC_CHECK(strstr(map, "truncated") == NULL);
    {
        /* jc_repomap_build prepends the "## Repository map" header (3
         * newlines) before the body; the body is one line per file. */
        int lines = 0;
        const char *p = map;
        while ((p = strchr(p, '\n')) != NULL) { lines++; p++; }
        JC_CHECK(lines == N + 3);
    }
    JC_CHECK((jc_size)strlen(map) < limit);
    /* THE F-01 assertion: peak = one file, not the sum. Before the fix this
     * reads ~total (~2.2 MB) and is the red line; after, ~fbytes + path list. */
    peak = jc_repomap_last_arena_peak();
    JC_CHECK(peak <= fbytes + (jc_size)strlen(map) + (jc_size)4096);

    (void)jc_test_rm_rf(dir);
    jc_arena_free(a);   /* app.arena, app.scratch, map, and any retained path
                          * list all live on `a` */
}
