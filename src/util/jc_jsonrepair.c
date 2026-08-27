/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_jsonrepair.c - conservative repair of nearly-JSON (see jc_jsonrepair.h). */

#include "jc_jsonrepair.h"
#include "jc_str.h"
#include "cJSON.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define JR_MAX_DEPTH 64

/* Is s[i] inside a JSON string, given the scan state? The passes below all
 * walk with this little state machine so string CONTENT is never touched. */

/* Pass: single -> double quotes, only when the input has no '"' at all. */
static char *pass_quotes(const char *s)
{
    char *out;
    jc_size i;
    if (strchr(s, '"') != NULL || strchr(s, '\'') == NULL) {
        return NULL; /* ambiguous or nothing to do */
    }
    out = (char *)malloc(strlen(s) + 1);
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; s[i] != '\0'; i++) {
        out[i] = (s[i] == '\'') ? '"' : s[i];
    }
    out[i] = '\0';
    return out;
}

/* Word-boundary check for the literal pass. */
static int jr_word_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

/* Pass: Python literals True/False/None -> true/false/null, outside strings.
 * Works in place on a malloc'd copy (the replacements never grow: True->true
 * same length, False->false same, None->null same). */
static void pass_python_literals(char *s)
{
    jc_size i = 0;
    int in_str = 0;
    int esc = 0;
    while (s[i] != '\0') {
        char c = s[i];
        if (esc) {
            esc = 0;
        } else if (in_str) {
            if (c == '\\') {
                esc = 1;
            } else if (c == '"') {
                in_str = 0;
            }
        } else if (c == '"') {
            in_str = 1;
        } else if (!jr_word_char(i > 0 ? s[i - 1] : ' ')) {
            if (strncmp(s + i, "True", 4) == 0 && !jr_word_char(s[i + 4])) {
                memcpy(s + i, "true", 4);
                i += 4;
                continue;
            }
            if (strncmp(s + i, "False", 5) == 0 && !jr_word_char(s[i + 5])) {
                memcpy(s + i, "false", 5);
                i += 5;
                continue;
            }
            if (strncmp(s + i, "None", 4) == 0 && !jr_word_char(s[i + 4])) {
                memcpy(s + i, "null", 4);
                i += 4;
                continue;
            }
        }
        i++;
    }
}

/* Pass: drop trailing commas (a ',' whose next non-ws char is '}' or ']'),
 * and append any missing closers at the end, in nesting order. Returns a
 * malloc'd string. */
static char *pass_commas_and_closers(const char *s)
{
    struct jc_sb sb;
    char stack[JR_MAX_DEPTH];
    int depth = 0;
    int in_str = 0;
    int esc = 0;
    jc_size i;

    jc_sb_init(&sb);
    for (i = 0; s[i] != '\0'; i++) {
        char c = s[i];
        if (esc) {
            esc = 0;
        } else if (in_str) {
            if (c == '\\') {
                esc = 1;
            } else if (c == '"') {
                in_str = 0;
            }
        } else if (c == '"') {
            in_str = 1;
        } else if (c == '{' || c == '[') {
            if (depth >= JR_MAX_DEPTH) {
                jc_sb_free(&sb);
                return NULL;
            }
            stack[depth++] = (c == '{') ? '}' : ']';
        } else if (c == '}' || c == ']') {
            if (depth > 0) {
                depth--;
            }
        } else if (c == ',') {
            /* Trailing comma? Peek at the next non-whitespace char. */
            jc_size k = i + 1;
            while (s[k] == ' ' || s[k] == '\t' || s[k] == '\n' ||
                   s[k] == '\r') {
                k++;
            }
            if (s[k] == '}' || s[k] == ']' || s[k] == '\0') {
                continue; /* drop it */
            }
        }
        jc_sb_append_char(&sb, c);
    }
    /* An unterminated string cannot be repaired conservatively. */
    if (in_str) {
        jc_sb_free(&sb);
        return NULL;
    }
    while (depth > 0) {
        jc_sb_append_char(&sb, stack[--depth]);
    }
    return jc_sb_finish(&sb);
}

char *jc_jsonrepair(const char *s)
{
    char *work;
    char *next;
    cJSON *chk;

    if (s == NULL || s[0] == '\0') {
        return NULL;
    }
    /* Quote conversion first (it changes what "inside a string" means for
     * the later, string-aware passes). */
    work = pass_quotes(s);
    if (work == NULL) {
        work = (char *)malloc(strlen(s) + 1);
        if (work == NULL) {
            return NULL;
        }
        strcpy(work, s);
    }
    pass_python_literals(work);
    next = pass_commas_and_closers(work);
    free(work);
    if (next == NULL) {
        return NULL;
    }
    /* A repair is only a repair if the result actually parses. */
    chk = cJSON_Parse(next);
    if (chk == NULL) {
        free(next);
        return NULL;
    }
    cJSON_Delete(chk);
    return next;
}
