/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_inhist.c - the TUI's typed-input history kept across runs (M763). */

#include "jc_test.h"
#include "jc_inhist.h"
#include "jc_log.h"
#include "jc_mem.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <stdio.h>
#include <string.h>

struct seen {
    int n;
    char last[64];
    char first[64];
};

static void count_cb(void *user, const char *line)
{
    struct seen *s = (struct seen *)user;
    if (s->n == 0) {
        jc_snprintf(s->first, sizeof(s->first), "%s", line);
    }
    jc_snprintf(s->last, sizeof(s->last), "%s", line);
    s->n++;
}

void test_inhist(void)
{
    char path[512];
    char text[64];
    struct seen s;
    FILE *f;
    int i;

    /* --- the credential shapes ------------------------------------------------
     * Positives: a known prefix with 16+ key characters, the words that
     * introduce a value, a bearer token, a PEM header. */
    JC_CHECK(jc_inhist_looks_secret("use sk-abcdefghijklmnop0123 for it") == 1);
    JC_CHECK(jc_inhist_looks_secret("sk-ant-api03-abcdefghijklmnopqrstu") == 1);
    JC_CHECK(jc_inhist_looks_secret("token ghp_0123456789abcdefABCDEF") == 1);
    JC_CHECK(jc_inhist_looks_secret("AKIAABCDEFGHIJKLMNOP is the id") == 1);
    JC_CHECK(jc_inhist_looks_secret("export PASSWORD=hunter2") == 1);
    JC_CHECK(jc_inhist_looks_secret("my password: correct horse") == 1);
    JC_CHECK(jc_inhist_looks_secret("api_key = abc") == 1);
    JC_CHECK(jc_inhist_looks_secret("Authorization: Bearer abcdefghijklmnopqrstuvwx") == 1);
    JC_CHECK(jc_inhist_looks_secret("-----BEGIN OPENSSH PRIVATE KEY-----") == 1);
    /* Negatives: the prefixes inside words, a prefix with too little after it,
     * the words with no value, and ordinary requests. */
    JC_CHECK(jc_inhist_looks_secret("fix the ask-user flow") == 0);
    JC_CHECK(jc_inhist_looks_secret("a risk-free refactor of the parser") == 0);
    JC_CHECK(jc_inhist_looks_secret("sk-short") == 0);
    JC_CHECK(jc_inhist_looks_secret("why does the password check fail?") == 0);
    JC_CHECK(jc_inhist_looks_secret("count the tokens in this file") == 0);
    JC_CHECK(jc_inhist_looks_secret("/status") == 0);
    JC_CHECK(jc_inhist_looks_secret("") == 0);
    JC_CHECK(jc_inhist_looks_secret(NULL) == 0);

    /* --- keepable ------------------------------------------------------------- */
    JC_CHECK(jc_inhist_keepable("explain the lease lock") == 1);
    JC_CHECK(jc_inhist_keepable("") == 0);
    JC_CHECK(jc_inhist_keepable("two\nlines") == 0);
    JC_CHECK(jc_inhist_keepable("my password: x") == 0);
    {
        char big[JC_INHIST_LINE_MAX + 2];
        memset(big, 'a', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';
        JC_CHECK(jc_inhist_keepable(big) == 0);   /* one byte over the bound */
    }
    /* A secret jichi registered is never written, whatever its shape. The
     * registry stores the pointer, so the string must outlive the process. */
    jc_redact_register("RegisteredValue4711");
    JC_CHECK(jc_inhist_keepable("please use RegisteredValue4711 here") == 0);

    /* --- append and load: 0600, oldest first, keepable lines only ----------- */
    jc_snprintf(path, sizeof(path), "%s", jc_test_tmp("jichi_inhist_test/input_history"));
    (void)jc_test_rm_rf(jc_test_tmp("jichi_inhist_test"));
    JC_CHECK(jc_inhist_append(path, "first line") == JC_OK);
    JC_CHECK(jc_inhist_append(path, "sk-abcdefghijklmnop0123 leaked") == JC_OK);
    JC_CHECK(jc_inhist_append(path, "second line") == JC_OK);
    JC_CHECK(jc_file_mode(path) >= 0 && (jc_file_mode(path) & 077) == 0);
    memset(&s, 0, sizeof(s));
    JC_CHECK(jc_inhist_load(path, count_cb, &s) == 2);
    JC_CHECK(s.n == 2 && strcmp(s.first, "first line") == 0 &&
             strcmp(s.last, "second line") == 0);

    /* --- the bound: the newest JC_INHIST_MAX are recalled, and a file past
     * twice the bound is rewritten with them -------------------------------- */
    (void)jc_test_rm_rf(jc_test_tmp("jichi_inhist_test"));
    JC_CHECK(jc_inhist_append(path, "seed") == JC_OK);   /* creates it, 0600 */
    f = fopen(path, "ab");
    JC_CHECK(f != NULL);
    if (f != NULL) {
        for (i = 1; i <= 2 * JC_INHIST_MAX + 10; i++) {
            fprintf(f, "entry %d\n", i);
        }
        fclose(f);
    }
    memset(&s, 0, sizeof(s));
    JC_CHECK(jc_inhist_load(path, count_cb, &s) == JC_INHIST_MAX);
    jc_snprintf(text, sizeof(text), "entry %d", 2 * JC_INHIST_MAX + 10);
    JC_CHECK(strcmp(s.last, text) == 0);
    jc_snprintf(text, sizeof(text), "entry %d", JC_INHIST_MAX + 11);
    JC_CHECK(strcmp(s.first, text) == 0);
    /* ...and the FILE now holds exactly those. Counted in the file itself: a
     * second load hands over the newest JC_INHIST_MAX whether or not the file
     * was trimmed, so asking the loader again proved nothing -- the first
     * version of this check did, and removing the trim left it green. */
    {
        struct jc_arena *ta = jc_arena_new(0);
        char *body = NULL;
        jc_size blen = 0, k;
        int nl = 0;
        JC_CHECK(ta != NULL && jc_read_file(path, &body, &blen, ta) == JC_OK);
        for (k = 0; body != NULL && k < blen; k++) {
            if (body[k] == '\n') {
                nl++;
            }
        }
        JC_CHECK(nl == JC_INHIST_MAX);
        JC_CHECK(body != NULL && strncmp(body, text, strlen(text)) == 0);
        jc_arena_free(ta);
    }
    JC_CHECK(jc_file_mode(path) >= 0 && (jc_file_mode(path) & 077) == 0);

    /* A missing file is no history, not an error. */
    memset(&s, 0, sizeof(s));
    JC_CHECK(jc_inhist_load(jc_test_tmp("jichi_inhist_none"), count_cb, &s) == 0);
    (void)jc_test_rm_rf(jc_test_tmp("jichi_inhist_test"));
}
