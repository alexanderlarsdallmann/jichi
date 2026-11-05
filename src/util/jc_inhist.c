/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_inhist.c - the TUI's typed-input history, kept across runs (see
 * jc_inhist.h, M763). */

#include "jc_inhist.h"
#include "jc_log.h"
#include "jc_mem.h"
#include "jc_vec.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void jc_inhist_path(char *buf, jc_size cap)
{
    jc_snprintf(buf, cap, "%s/.jichi.d/input_history", jc_home_dir());
}

/* A character a key is made of. */
static int key_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

static int lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

/* Case-insensitive: does `s` start with `p`? */
static int starts_ci(const char *s, const char *p)
{
    while (*p != '\0') {
        if (lower((unsigned char)*s) != lower((unsigned char)*p)) {
            return 0;
        }
        s++;
        p++;
    }
    return 1;
}

/* The key prefixes of common credential formats. Each counts only at the start
 * of a token and when at least 16 key characters follow it, so "ask-user" and
 * "risk-free" are not keys. */
static const char *const KEY_PREFIXES[] = {
    "sk-", "sk_live_", "sk_test_", "rk_live_", "pk_live_",
    "ghp_", "gho_", "ghu_", "ghs_", "ghr_", "github_pat_", "glpat-",
    "xoxb-", "xoxp-", "xoxa-", "xoxs-",
    "AKIA", "ASIA", "AIza", "ya29.", "hf_",
    0
};

/* Words that introduce a secret's value: `password=hunter2`, `token: abc`. */
static const char *const SECRET_WORDS[] = {
    "password", "passwd", "passphrase", "pwd", "api_key", "apikey",
    "api-key", "secret", "token", "access_key", "private_key",
    0
};

int jc_inhist_looks_secret(const char *line)
{
    const char *p;
    int i;

    if (line == NULL) {
        return 0;
    }
    if (strstr(line, "-----BEGIN") != NULL) {
        return 1;   /* a PEM block's first line */
    }
    for (p = line; *p != '\0'; p++) {
        int at_token = (p == line) || !key_char(p[-1]);
        if (!at_token) {
            continue;
        }
        for (i = 0; KEY_PREFIXES[i] != 0; i++) {
            jc_size n = strlen(KEY_PREFIXES[i]);
            if (strncmp(p, KEY_PREFIXES[i], n) == 0) {
                jc_size k = 0;
                while (key_char(p[n + k])) {
                    k++;
                }
                if (k >= 16) {
                    return 1;
                }
            }
        }
        /* `Bearer <token>` */
        if (starts_ci(p, "bearer ")) {
            jc_size k = 0;
            while (key_char(p[7 + k])) {
                k++;
            }
            if (k >= 16) {
                return 1;
            }
        }
        for (i = 0; SECRET_WORDS[i] != 0; i++) {
            jc_size n = strlen(SECRET_WORDS[i]);
            if (starts_ci(p, SECRET_WORDS[i]) && !key_char(p[n])) {
                const char *q = p + n;
                while (*q == ' ') {
                    q++;
                }
                if (*q == '=' || *q == ':') {
                    q++;
                    while (*q == ' ') {
                        q++;
                    }
                    if (*q != '\0' && *q != ' ') {
                        return 1;   /* the word, then a value */
                    }
                }
            }
        }
    }
    return 0;
}

int jc_inhist_keepable(const char *line)
{
    jc_size n;
    char scrubbed[JC_INHIST_LINE_MAX + 1];

    if (line == NULL || line[0] == '\0' || strchr(line, '\n') != NULL) {
        return 0;
    }
    n = strlen(line);
    if (n > JC_INHIST_LINE_MAX) {
        return 0;
    }
    /* A secret jichi registered (a resolved API key): any substitution means
     * the line holds one. */
    if (jc_redact_active() &&
        jc_redact_secrets(line, scrubbed, sizeof(scrubbed)) > 0) {
        return 0;
    }
    return !jc_inhist_looks_secret(line);
}

jc_status jc_inhist_append(const char *path, const char *line)
{
    FILE *f;
    char dir[1100];
    const char *slash;

    if (path == NULL || !jc_inhist_keepable(line)) {
        return JC_OK;   /* nothing to write is not a failure */
    }
    slash = strrchr(path, '/');
    if (slash != NULL && (jc_size)(slash - path) < sizeof(dir)) {
        memcpy(dir, path, (jc_size)(slash - path));
        dir[slash - path] = '\0';
        (void)jc_mkdir_p_private(dir);
    }
    /* Made private BEFORE the first line is written, as the audit log is. */
    if (!jc_file_exists(path)) {
        f = fopen(path, "ab");
        if (f != NULL) {
            fclose(f);
        }
        (void)jc_make_private(path);
    }
    f = fopen(path, "ab");
    if (f == NULL) {
        return JC_ERR_IO;
    }
    fputs(line, f);
    fputc('\n', f);
    return (fclose(f) == 0) ? JC_OK : JC_ERR_IO;
}

int jc_inhist_load(const char *path, void (*add)(void *user, const char *line),
                   void *user)
{
    struct jc_arena *a;
    char *text = NULL;
    jc_size len = 0, i, start;
    struct jc_vec lines;
    int handed = 0;
    jc_size first;

    if (path == NULL || add == NULL || !jc_is_regular_file(path)) {
        return 0;
    }
    a = jc_arena_new(0);
    if (a == NULL) {
        return 0;
    }
    if (jc_read_file(path, &text, &len, a) != JC_OK || text == NULL) {
        jc_arena_free(a);
        return 0;
    }
    jc_vec_init(&lines, sizeof(char *));
    start = 0;
    for (i = 0; i <= len; i++) {
        if (i == len || text[i] == '\n') {
            if (i > start) {
                char *ln = text + start;
                text[i] = '\0';
                jc_vec_push(&lines, &ln);
            }
            start = i + 1;
        }
    }
    first = (lines.len > JC_INHIST_MAX) ? lines.len - JC_INHIST_MAX : 0;
    for (i = first; i < lines.len; i++) {
        const char *ln = *(char **)jc_vec_at(&lines, i);
        /* A line that would not be written now is not recalled either: the file
         * may predate a key that has since been registered. */
        if (jc_inhist_keepable(ln)) {
            add(user, ln);
            handed++;
        }
    }
    /* Trim: past twice the bound, rewrite the file with what was handed over. */
    if (lines.len > 2 * (jc_size)JC_INHIST_MAX) {
        struct jc_sb sb;
        jc_sb_init(&sb);
        for (i = first; i < lines.len; i++) {
            const char *ln = *(char **)jc_vec_at(&lines, i);
            if (jc_inhist_keepable(ln)) {
                jc_sb_append(&sb, ln);
                jc_sb_append(&sb, "\n");
            }
        }
        if (sb.data != NULL) {
            (void)jc_write_file_atomic(path, sb.data, (jc_size)sb.len);
        }
        jc_sb_free(&sb);
    }
    jc_vec_free(&lines);
    jc_arena_free(a);
    return handed;
}
