/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_repomap.c - unit tests for the pure repo-map symbol scanner. */

#include "jc_test.h"
#include "jc_repomap.h"
#include "jc_str.h"

#include <stdlib.h>
#include <string.h>

static int has(struct jc_vec *v, const char *name)
{
    jc_size i;
    for (i = 0; i < v->len; i++) {
        char *s = JC_VEC_STR(v, i);
        if (s != NULL && strcmp(s, name) == 0) return 1;
    }
    return 0;
}

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
