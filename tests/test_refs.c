/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_refs.c - @-reference scanning + file expansion. */

#include "jc_test.h"
#include "jc_refs.h"
#include "jc_app.h"
#include "jc_mem.h"
#include "jc_platform.h"
#include "jc_snprintf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct jc_ref *ref_at(struct jc_vec *v, int i)
{
    return (struct jc_ref *)jc_vec_at(v, (jc_size)i);
}

static void test_scan(void)
{
    struct jc_vec refs;
    int n;

    jc_vec_init(&refs, sizeof(struct jc_ref));
    n = jc_refs_scan("see @src/a.c and @diff plus @url:http://x - mail me@host",
                     &refs);
    JC_CHECK(n == 3);
    if (refs.len == 3) {
        JC_CHECK(ref_at(&refs, 0)->kind == JC_REF_FILE);
        JC_CHECK(strcmp(ref_at(&refs, 0)->arg, "src/a.c") == 0);
        JC_CHECK(ref_at(&refs, 1)->kind == JC_REF_DIFF);
        JC_CHECK(ref_at(&refs, 2)->kind == JC_REF_URL);
        JC_CHECK(strcmp(ref_at(&refs, 2)->arg, "http://x") == 0);
    }
    jc_refs_free(&refs);

    /* me@host (mid-word @) is not a reference. */
    jc_vec_init(&refs, sizeof(struct jc_ref));
    JC_CHECK(jc_refs_scan("contact me@host.com please", &refs) == 0);
    jc_refs_free(&refs);

    /* trailing punctuation trimmed from a file token; bare '@' is safe. */
    jc_vec_init(&refs, sizeof(struct jc_ref));
    n = jc_refs_scan("look at @main.c. and a bare @ here", &refs);
    JC_CHECK(n == 1);
    if (refs.len == 1) JC_CHECK(strcmp(ref_at(&refs, 0)->arg, "main.c") == 0);
    jc_refs_free(&refs);

    /* no '@' at all */
    jc_vec_init(&refs, sizeof(struct jc_ref));
    JC_CHECK(jc_refs_scan("nothing to see here", &refs) == 0);
    jc_refs_free(&refs);

    /* @sym:Name -> a symbol reference; trailing punctuation trimmed. */
    jc_vec_init(&refs, sizeof(struct jc_ref));
    n = jc_refs_scan("explain @sym:jc_refs_scan, then @sym:foo.", &refs);
    JC_CHECK(n == 2);
    if (refs.len == 2) {
        JC_CHECK(ref_at(&refs, 0)->kind == JC_REF_SYM);
        JC_CHECK(strcmp(ref_at(&refs, 0)->arg, "jc_refs_scan") == 0);
        JC_CHECK(ref_at(&refs, 1)->kind == JC_REF_SYM);
        JC_CHECK(strcmp(ref_at(&refs, 1)->arg, "foo") == 0);
    }
    jc_refs_free(&refs);

    /* bare "@sym:" with no name is not a reference. */
    jc_vec_init(&refs, sizeof(struct jc_ref));
    JC_CHECK(jc_refs_scan("an empty @sym: here", &refs) == 0);
    jc_refs_free(&refs);

    /* @audio:<path> -> an audio reference (transcribed at expand time, M33). */
    jc_vec_init(&refs, sizeof(struct jc_ref));
    n = jc_refs_scan("transcribe @audio:clip.wav now", &refs);
    JC_CHECK(n == 1);
    if (refs.len == 1) {
        JC_CHECK(ref_at(&refs, 0)->kind == JC_REF_AUDIO);
        JC_CHECK(strcmp(ref_at(&refs, 0)->arg, "clip.wav") == 0);
    }
    jc_refs_free(&refs);

    /* bare "@audio:" with no path is not a reference. */
    jc_vec_init(&refs, sizeof(struct jc_ref));
    JC_CHECK(jc_refs_scan("empty @audio: here", &refs) == 0);
    jc_refs_free(&refs);

    /* @docs:<name> -> a docs reference (resolved via search_docs, M34a). */
    jc_vec_init(&refs, sizeof(struct jc_ref));
    n = jc_refs_scan("how do hooks work? @docs:react", &refs);
    JC_CHECK(n == 1);
    if (refs.len == 1) {
        JC_CHECK(ref_at(&refs, 0)->kind == JC_REF_DOCS);
        JC_CHECK(strcmp(ref_at(&refs, 0)->arg, "react") == 0);
    }
    jc_refs_free(&refs);

    /* bare "@docs:" with no name is not a reference. */
    jc_vec_init(&refs, sizeof(struct jc_ref));
    JC_CHECK(jc_refs_scan("empty @docs: here", &refs) == 0);
    jc_refs_free(&refs);

    /* @ref:<name> -> a config alias (#6). */
    jc_vec_init(&refs, sizeof(struct jc_ref));
    n = jc_refs_scan("pull @ref:prod-db config", &refs);
    JC_CHECK(n == 1);
    if (refs.len == 1) {
        JC_CHECK(ref_at(&refs, 0)->kind == JC_REF_ALIAS);
        JC_CHECK(strcmp(ref_at(&refs, 0)->arg, "prod-db") == 0);
    }
    jc_refs_free(&refs);

    /* bare "@ref:" with no name is not a reference. */
    jc_vec_init(&refs, sizeof(struct jc_ref));
    JC_CHECK(jc_refs_scan("empty @ref: here", &refs) == 0);
    jc_refs_free(&refs);

    /* @mcp:<uri> -> an MCP resource reference (resolved via read_mcp_resource,
     * M47). */
    jc_vec_init(&refs, sizeof(struct jc_ref));
    n = jc_refs_scan("see @mcp:mem://notes for context", &refs);
    JC_CHECK(n == 1);
    if (refs.len == 1) {
        JC_CHECK(ref_at(&refs, 0)->kind == JC_REF_MCP);
        JC_CHECK(strcmp(ref_at(&refs, 0)->arg, "mem://notes") == 0);
    }
    jc_refs_free(&refs);

    /* bare "@mcp:" with no uri is not a reference. */
    jc_vec_init(&refs, sizeof(struct jc_ref));
    JC_CHECK(jc_refs_scan("empty @mcp: here", &refs) == 0);
    jc_refs_free(&refs);

    /* @problems -> an LSP-diagnostics reference (exact, like @diff, F5). */
    jc_vec_init(&refs, sizeof(struct jc_ref));
    n = jc_refs_scan("any @problems left?", &refs);
    JC_CHECK(n == 1);
    if (refs.len == 1) {
        JC_CHECK(ref_at(&refs, 0)->kind == JC_REF_PROBLEMS);
    }
    jc_refs_free(&refs);

    /* "@problemsx" is not @problems (must be the whole boundary token). */
    jc_vec_init(&refs, sizeof(struct jc_ref));
    {
        int np = jc_refs_scan("@problemsx", &refs);
        /* It is classified as a file candidate, not JC_REF_PROBLEMS. */
        JC_CHECK(np == 1 && ref_at(&refs, 0)->kind != JC_REF_PROBLEMS);
    }
    jc_refs_free(&refs);

    /* @folder:<dir> -> a folder reference; trailing punctuation trimmed (F5). */
    jc_vec_init(&refs, sizeof(struct jc_ref));
    n = jc_refs_scan("look at @folder:src/util, please", &refs);
    JC_CHECK(n == 1);
    if (refs.len == 1) {
        JC_CHECK(ref_at(&refs, 0)->kind == JC_REF_FOLDER);
        JC_CHECK(strcmp(ref_at(&refs, 0)->arg, "src/util") == 0);
    }
    jc_refs_free(&refs);

    /* bare "@folder:" with no dir is not a reference. */
    jc_vec_init(&refs, sizeof(struct jc_ref));
    JC_CHECK(jc_refs_scan("empty @folder: here", &refs) == 0);
    jc_refs_free(&refs);
}

static void test_expand(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    const char *path = jc_test_tmp("jichi_test_ref.txt");
    char *out = NULL;
    FILE *f;

    f = fopen(path, "wb");
    if (f != NULL) { fputs("ALPHA-BETA-GAMMA", f); fclose(f); }

    memset(&app, 0, sizeof(app));
    app.arena = a;
    strcpy(app.cwd, "/nonexistent-cwd"); /* force absolute-path resolution */

    /* An absolute @file path is inlined under a referenced-context block. */
    {
        char msg[512];
        jc_snprintf(msg, sizeof(msg), "please read @%s now", path);
        jc_refs_expand(&app, msg, a, &out);
        JC_CHECK(out != NULL);
        JC_CHECK(strstr(out, "referenced context") != NULL);
        JC_CHECK(strstr(out, "ALPHA-BETA-GAMMA") != NULL);
    }

    /* A non-existent file leaves the message unchanged (no context block). */
    jc_refs_expand(&app, "read @/no/such/file.xyz", a, &out);
    JC_CHECK(out != NULL && strcmp(out, "read @/no/such/file.xyz") == 0);

    /* No references => unchanged. */
    jc_refs_expand(&app, "just a normal message", a, &out);
    JC_CHECK(out != NULL && strcmp(out, "just a normal message") == 0);

    /* @sym with no tool registry resolves nothing => message unchanged (the
     * resolver is exercised end-to-end with real tools in the live/E2E path). */
    jc_refs_expand(&app, "explain @sym:Foo", a, &out);
    JC_CHECK(out != NULL && strcmp(out, "explain @sym:Foo") == 0);

    /* @problems with no LSP configured (app.lsp == NULL) appends a note rather
     * than failing (F5). */
    jc_refs_expand(&app, "show @problems here", a, &out);
    JC_CHECK(out != NULL);
    JC_CHECK(strstr(out, "no language server") != NULL);

    /* @folder:<dir> inlines the directory's source files + top-level symbols
     * via the scoped repository map (F5). */
    {
        const char *dir = jc_test_tmp("jichi_test_refs_folder");
        char src[512];
        jc_mkdir_p(dir);
        jc_snprintf(src, sizeof(src), "%s/foo.c", dir);
        f = fopen(src, "wb");
        if (f != NULL) { fputs("int my_func(int x) { return x; }\n", f); fclose(f); }
        {
            char msg[600];
            jc_snprintf(msg, sizeof(msg), "explain @folder:%s", dir);
            jc_refs_expand(&app, msg, a, &out);
            JC_CHECK(out != NULL);
            JC_CHECK(strstr(out, "foo.c") != NULL);
            JC_CHECK(strstr(out, "my_func") != NULL);
        }
        remove(src);
    }

    jc_arena_free(a);
}

void test_refs(void)
{
    test_scan();
    test_expand();
}
