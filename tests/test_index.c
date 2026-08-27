/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_index.c - offline tests for the index's pure helpers (chunk grouping
 * and cosine ranking). Building a real index needs an embedding endpoint and
 * is exercised manually. */

#include "jc_test.h"
#include "jc_index.h"
#include "jc_docs.h"
#include "jc_config.h"
#include "jc_json.h"
#include "jc_platform.h"
#include "jc_snprintf.h"
#include "jc_str.h"
#include "jc_vec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_chunk_ranges(void)
{
    /* 30 lines of 100 'x' plus newline (101 chars each) => 3030 chars, which
     * exceeds CHUNK_CHARS (1500) and must split into multiple chunks. */
    char text[31 * 101 + 1];
    struct jc_vec ranges;
    int pos = 0;
    int i;
    jc_size r;

    for (i = 0; i < 30; i++) {
        memset(text + pos, 'x', 100);
        pos += 100;
        text[pos++] = '\n';
    }
    text[pos] = '\0';

    jc_vec_init(&ranges, sizeof(struct jc_line_range));
    JC_CHECK(jc_chunk_ranges(text, (jc_size)pos, &ranges) == JC_OK);
    JC_CHECK(ranges.len >= 2);

    /* Ranges must start at line 1, be contiguous, and cover all 30 lines. */
    {
        struct jc_line_range *first =
            (struct jc_line_range *)jc_vec_at(&ranges, 0);
        struct jc_line_range *last =
            (struct jc_line_range *)jc_vec_at(&ranges, ranges.len - 1);
        JC_CHECK(first->start == 1);
        JC_CHECK(last->end == 30);
    }
    for (r = 1; r < ranges.len; r++) {
        struct jc_line_range *prev =
            (struct jc_line_range *)jc_vec_at(&ranges, r - 1);
        struct jc_line_range *cur =
            (struct jc_line_range *)jc_vec_at(&ranges, r);
        JC_CHECK(cur->start == prev->end + 1);
    }
    jc_vec_free(&ranges);

    /* A single short line is one chunk spanning line 1. */
    {
        struct jc_vec one;
        jc_vec_init(&one, sizeof(struct jc_line_range));
        JC_CHECK(jc_chunk_ranges("hello\n", 6, &one) == JC_OK);
        JC_CHECK(one.len == 1);
        JC_CHECK(((struct jc_line_range *)jc_vec_at(&one, 0))->start == 1);
        jc_vec_free(&one);
    }
}

static void test_cosine(void)
{
    /* Three 2-D rows; query aligned with row 0. */
    float vecs[6];
    float q[2];
    int idx[3];
    double score[3];
    int n;

    vecs[0] = 1.0f; vecs[1] = 0.0f;   /* row 0: same direction as q  */
    vecs[2] = 0.0f; vecs[3] = 1.0f;   /* row 1: orthogonal (score 0) */
    vecs[4] = 0.9f; vecs[5] = 0.1f;   /* row 2: close to q           */
    q[0] = 1.0f; q[1] = 0.0f;

    n = jc_cosine_topn(vecs, 3, 2, q, 2, idx, score);
    JC_CHECK(n == 2);
    JC_CHECK(idx[0] == 0);            /* best match first            */
    JC_CHECK(idx[1] == 2);
    JC_CHECK(score[0] >= score[1]);   /* descending                  */
    JC_CHECK(score[0] > 0.99);

    /* A zero query yields no results. */
    q[0] = 0.0f; q[1] = 0.0f;
    JC_CHECK(jc_cosine_topn(vecs, 3, 2, q, 2, idx, score) == 0);
}

static void test_docs_html(void)
{
    struct jc_sb out;

    /* Tags dropped; <p> boundaries -> newline; entities decoded; script/style
     * bodies skipped; whitespace collapsed. */
    jc_sb_init(&out);
    jc_docs_html_to_text(
        "<html><head><style>body{color:red}</style>"
        "<script>var x = 1 < 2;</script></head><body>"
        "<h1>Title</h1><p>Hello &amp; welcome to <b>jichi</b>.</p>"
        "<p>Line&nbsp;two.</p></body></html>", &out);
    JC_CHECK(out.data != NULL);
    JC_CHECK(strstr(out.data, "Title") != NULL);
    JC_CHECK(strstr(out.data, "Hello & welcome to jichi.") != NULL);
    JC_CHECK(strstr(out.data, "Line two.") != NULL);
    /* No leftover tags or script source. */
    JC_CHECK(strstr(out.data, "<") == NULL);
    JC_CHECK(strstr(out.data, "color:red") == NULL);
    JC_CHECK(strstr(out.data, "var x") == NULL);

    /* M524: NUMERIC character references. Only &#39; was special-cased, so
     * &#160; and &#167; survived verbatim -- measured while indexing the ANSI C
     * Rationale, whose prose is full of both: a learner searching it read
     * "&#167;3.6.2" for "\u00a73.6.2", and the noise went into the embedded
     * chunks, not just the display. Decimal and hex, encoded as UTF-8. */
    jc_sb_free(&out);
    jc_sb_init(&out);
    jc_docs_html_to_text(
        "<p>See &#167;3.6.2.&#160;Also &#xA7;4 and &#8212;dash&#8212;</p>"
        "<p>&#65;&#x42;C</p>", &out);
    JC_CHECK(out.data != NULL);
    /* The literals are SPLIT deliberately: a hex escape consumes every hex
     * digit that follows it, so "\xc2\xa73.6.2" is one out-of-range escape
     * rather than § followed by '3'. This cost three failing checks. */
    JC_CHECK(strstr(out.data, "\xc2\xa7" "3.6.2") != NULL);   /* §  decimal    */
    JC_CHECK(strstr(out.data, "\xc2\xa7" "4") != NULL);       /* §  hex        */
    JC_CHECK(strstr(out.data, "\xe2\x80\x94" "dash") != NULL); /* em dash     */
    JC_CHECK(strstr(out.data, "ABC") != NULL);            /* ASCII by number */
    /* &#160; is a no-break space: it must become whitespace, not vanish into
     * the previous word, and must not leave the raw reference behind. */
    JC_CHECK(strstr(out.data, "&#") == NULL);
    JC_CHECK(strstr(out.data, "Also") != NULL);
    /* Block boundary became a newline between the heading and the paragraph. */
    JC_CHECK(strchr(out.data, '\n') != NULL);
    jc_sb_free(&out);

    /* NULL input is a no-op (no crash). */
    jc_sb_init(&out);
    jc_docs_html_to_text(NULL, &out);
    JC_CHECK(out.len == 0);
    jc_sb_free(&out);
}

/* M136: the manifest endianness tag. The vectors.f32 blob is host-endian, so
 * the tag is what lets a foreign-endian cache be detected and rebuilt. */
static void test_endian_tag(void)
{
    const char *tag = jc_index_endian_tag();
    union { unsigned int u; unsigned char c[sizeof(unsigned int)]; } probe;

    /* Exactly one of the two known tags. */
    JC_CHECK(tag != NULL);
    JC_CHECK(strcmp(tag, "le") == 0 || strcmp(tag, "be") == 0);

    /* Agrees with an independent probe of the host's byte order. */
    probe.u = 1u;
    if (probe.c[0] == 1u) {
        JC_CHECK(strcmp(tag, "le") == 0);
    } else {
        JC_CHECK(strcmp(tag, "be") == 0);
    }

    /* Stable across calls (a manifest written now matches a check later). */
    JC_CHECK(strcmp(tag, jc_index_endian_tag()) == 0);
}

/* Reimplementation of the index's cache-key hash (djb2), so the test can
 * plant a cache where jc_index_build will look for it. */
static unsigned long t_djb2(const char *s)
{
    unsigned long h = 5381u;
    while (*s != '\0') {
        h = ((h << 5) + h) + (unsigned char)*s;
        s++;
    }
    return h;
}

/* M141: a fully-clean cache loads OFFLINE (no embedding endpoint touched)
 * and the old blob is adopted as the resident vectors -- mmap'd read-only
 * where available. Exercises map_blob, identity adoption, the skipped
 * rewrite, and the munmap-vs-free split in jc_index_free (ASan/valgrind
 * verify the release matches the acquisition). */
static void test_cached_load(void)
{
    const char *ws = jc_test_tmp("jichi_idx_ws");
    const char *src = jc_test_tmp("jichi_idx_ws/a.c");
    const char *text = "int a;\nint b;\n";
    char cdir[256];
    char path[300];
    struct jc_model_cfg m;
    struct jc_index *idx = NULL;
    struct jc_index_stats st;
    int pass;

    setenv("HOME", jc_test_tmp("jichi_home_test"), 1);
    jc_mkdir_p(ws);
    JC_CHECK(jc_write_file(src, text, strlen(text)) == JC_OK);

    /* Plant the cache: manifest built through cJSON so the mtime round-trips
     * through the same print/parse path the loader uses (exact == compare). */
    jc_snprintf(cdir, sizeof(cdir), "%s/index/%lu",
                jc_test_tmp("jichi_home_test/.jichi.d"), t_djb2(ws));
    jc_mkdir_p(cdir);
    {
        cJSON *root = cJSON_CreateObject();
        cJSON *farr = cJSON_AddArrayToObject(root, "files");
        cJSON *carr;
        cJSON *fe = cJSON_CreateObject();
        cJSON *ce = cJSON_CreateObject();
        char *mtext;
        cJSON_AddNumberToObject(root, "version", 1);
        cJSON_AddStringToObject(root, "endian", jc_index_endian_tag());
        cJSON_AddStringToObject(root, "root", ws);
        cJSON_AddStringToObject(root, "model", "test-embed");
        cJSON_AddNumberToObject(root, "dim", 2);
        cJSON_AddNumberToObject(root, "count", 1);
        cJSON_AddStringToObject(fe, "path", src);
        cJSON_AddNumberToObject(fe, "mtime", jc_file_mtime(src));
        cJSON_AddNumberToObject(fe, "first", 0);
        cJSON_AddNumberToObject(fe, "count", 1);
        cJSON_AddItemToArray(farr, fe);
        carr = cJSON_AddArrayToObject(root, "chunks");
        cJSON_AddStringToObject(ce, "path", src);
        cJSON_AddNumberToObject(ce, "start", 1);
        cJSON_AddNumberToObject(ce, "end", 2);
        cJSON_AddItemToArray(carr, ce);
        mtext = cJSON_Print(root);
        cJSON_Delete(root);
        JC_CHECK(mtext != NULL);
        jc_snprintf(path, sizeof(path), "%s/manifest.json", cdir);
        JC_CHECK(jc_write_file(path, mtext, strlen(mtext)) == JC_OK);
        free(mtext);
    }
    {
        float v[2];
        v[0] = 3.0f;
        v[1] = 4.0f;
        jc_snprintf(path, sizeof(path), "%s/vectors.f32", cdir);
        JC_CHECK(jc_write_file(path, (const char *)v, sizeof(v)) == JC_OK);
    }

    /* Two passes: adopt -> free -> adopt again (the clean cache survives
     * because the identity load skips the rewrite). No network: a model
     * config with no endpoint would fail any embed call. */
    for (pass = 0; pass < 2; pass++) {
        float q[2];
        int hit = -1;
        double score = 0.0;
        memset(&m, 0, sizeof(m));
        m.model = (char *)"test-embed";
        idx = NULL;
        JC_CHECK(jc_index_build(ws, &m, 0, NULL, &idx, &st, NULL,
                                NULL) == JC_OK);
        JC_CHECK(idx != NULL);
        JC_CHECK(st.embedded == 0);  /* fully served from the cache */
        JC_CHECK(st.reused == 1);
        JC_CHECK(jc_index_count(idx) == 1);
        JC_CHECK(jc_index_dim(idx) == 2);
        JC_CHECK(strcmp(jc_index_chunk_text(idx, 0), text) == 0);
        q[0] = 3.0f;
        q[1] = 4.0f;
        JC_CHECK(jc_index_search(idx, q, 1, &hit, &score) == 1);
        JC_CHECK(hit == 0);
        JC_CHECK(score > 0.999); /* same vector: cosine ~1 */
        jc_index_free(idx);
    }

    remove(src);
    jc_snprintf(path, sizeof(path), "%s/manifest.json", cdir);
    remove(path);
    jc_snprintf(path, sizeof(path), "%s/vectors.f32", cdir);
    remove(path);
    rmdir(cdir);
    rmdir(ws);
}

void test_index(void)
{
    test_chunk_ranges();
    test_cosine();
    test_docs_html();
    test_endian_tag();
    test_cached_load();
}
