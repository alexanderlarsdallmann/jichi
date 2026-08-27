/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_lsp_nav.c - pure LSP navigation parsers/locator (jc_lsp_proto.c).
 * The transport round-trip is verified end-to-end against a real server. */

#include "jc_test.h"
#include "jc_lsp.h"
#include "jc_str.h"

#include <string.h>

static void test_locations(void)
{
    struct jc_sb sb;
    int count = -1;

    /* Single Location object. */
    jc_sb_init(&sb);
    JC_CHECK(jc_lsp_format_locations(
        "{\"uri\":\"file:///a/b.c\",\"range\":{\"start\":"
        "{\"line\":9,\"character\":4}}}", &sb, &count) == 1);
    JC_CHECK(count == 1);
    JC_CHECK(sb.data != NULL && strcmp(sb.data, "/a/b.c:10:5\n") == 0);
    jc_sb_free(&sb);

    /* Location[] with two entries. */
    jc_sb_init(&sb);
    jc_lsp_format_locations(
        "[{\"uri\":\"file:///x.c\",\"range\":{\"start\":{\"line\":0,"
        "\"character\":0}}},{\"uri\":\"file:///y.c\",\"range\":{\"start\":"
        "{\"line\":2,\"character\":1}}}]", &sb, &count);
    JC_CHECK(count == 2);
    JC_CHECK(strstr(sb.data, "/x.c:1:1") != NULL);
    JC_CHECK(strstr(sb.data, "/y.c:3:2") != NULL);
    jc_sb_free(&sb);

    /* LocationLink[] (targetUri/targetRange). */
    jc_sb_init(&sb);
    jc_lsp_format_locations(
        "[{\"targetUri\":\"file:///z.c\",\"targetRange\":{\"start\":"
        "{\"line\":41,\"character\":0}}}]", &sb, &count);
    JC_CHECK(count == 1);
    JC_CHECK(strcmp(sb.data, "/z.c:42:1\n") == 0);
    jc_sb_free(&sb);

    /* null and [] => parsed, zero results. */
    jc_sb_init(&sb);
    JC_CHECK(jc_lsp_format_locations("null", &sb, &count) == 1);
    JC_CHECK(count == 0);
    jc_lsp_format_locations("[]", &sb, &count);
    JC_CHECK(count == 0);
    jc_sb_free(&sb);
}

static void test_symbols(void)
{
    struct jc_sb sb;
    int count = -1;

    /* Hierarchical DocumentSymbol[] with nested children. */
    jc_sb_init(&sb);
    jc_lsp_format_symbols(
        "[{\"name\":\"foo\",\"kind\":12,\"range\":{\"start\":{\"line\":2}},"
        "\"children\":[{\"name\":\"bar\",\"kind\":13,\"range\":{\"start\":"
        "{\"line\":3}}}]}]", &sb, &count);
    JC_CHECK(count == 2);
    JC_CHECK(strstr(sb.data, "function foo  (line 3)") != NULL);
    JC_CHECK(strstr(sb.data, "  variable bar  (line 4)") != NULL); /* indented */
    jc_sb_free(&sb);

    /* Flat SymbolInformation[] (has location). */
    jc_sb_init(&sb);
    jc_lsp_format_symbols(
        "[{\"name\":\"baz\",\"kind\":23,\"location\":{\"uri\":\"file:///z.c\","
        "\"range\":{\"start\":{\"line\":7}}}}]", &sb, &count);
    JC_CHECK(count == 1);
    JC_CHECK(strstr(sb.data, "struct baz  /z.c:8") != NULL);
    jc_sb_free(&sb);

    /* Empty array => zero. */
    jc_sb_init(&sb);
    jc_lsp_format_symbols("[]", &sb, &count);
    JC_CHECK(count == 0);
    jc_sb_free(&sb);
}

static void test_locate(void)
{
    long line = -1;
    long col = -1;

    /* First occurrence, 0-based line/col. */
    JC_CHECK(jc_lsp_locate_symbol("int foo;\nbar(foo);\n", "foo", &line, &col)
             == 1);
    JC_CHECK(line == 0 && col == 4);

    /* Word boundary: skip "foobar", match the standalone "foo". */
    JC_CHECK(jc_lsp_locate_symbol("foobar foo", "foo", &line, &col) == 1);
    JC_CHECK(line == 0 && col == 7);

    /* On a later line. */
    JC_CHECK(jc_lsp_locate_symbol("a\nb\n  zap()", "zap", &line, &col) == 1);
    JC_CHECK(line == 2 && col == 2);

    /* Not found. */
    JC_CHECK(jc_lsp_locate_symbol("nothing here", "missing", &line, &col) == 0);
    JC_CHECK(jc_lsp_locate_symbol("x", "", &line, &col) == 0);
}

void test_lsp_nav(void)
{
    test_locations();
    test_symbols();
    test_locate();
}
