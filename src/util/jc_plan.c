/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_plan.c - see the header. */
#include "jc_plan.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <string.h>
#include <stdlib.h>

enum sec { S_NONE = 0, S_CLAIM, S_REJECTED, S_FALSIFIER, S_NOTGOALS, S_TOUCHES };

void jc_plan_init(struct jc_plan *p)
{
    memset(p, 0, sizeof *p);
    jc_vec_init(&p->touches, sizeof(char *));
}

void jc_plan_free(struct jc_plan *p)
{
    if (p != NULL) {
        jc_vec_free(&p->touches);
    }
}

static int heading_is(const char *line, jc_size len, const char *name)
{
    jc_size n = strlen(name);
    if (len < 3 + n || line[0] != '#' || line[1] != '#' || line[2] != ' ') {
        return 0;
    }
    if (strncmp(line + 3, name, n) != 0) {
        return 0;
    }
    /* exact heading, tolerating trailing spaces */
    {
        jc_size i = 3 + n;
        while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
        return i == len;
    }
}

static int has_reason(const char *s, jc_size len)
{
    jc_size i;
    for (i = 0; i + 1 < len; i++) {
        if (s[i] == '-' && s[i + 1] == '-') return 1;
        /* em dash U+2014 = E2 80 94 */
        if (i + 2 < len && (unsigned char)s[i] == 0xE2 &&
            (unsigned char)s[i + 1] == 0x80 && (unsigned char)s[i + 2] == 0x94) {
            return 1;
        }
    }
    return 0;
}

static int nonblank(const char *s, jc_size len)
{
    jc_size i;
    for (i = 0; i < len; i++) {
        if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r') return 1;
    }
    return 0;
}

void jc_plan_parse(const char *text, struct jc_plan *p, struct jc_arena *a)
{
    const char *cur = text;
    enum sec s = S_NONE;
    int claim_body = 0, fals_body = 0, ng_body = 0;

    jc_plan_init(p);
    if (text == NULL) {
        return;
    }
    while (*cur != '\0') {
        const char *nl = strchr(cur, '\n');
        jc_size len = (nl != NULL) ? (jc_size)(nl - cur) : (jc_size)strlen(cur);
        if (len > 0 && cur[len - 1] == '\r') len--;

        if (heading_is(cur, len, "Claim"))          { s = S_CLAIM;     p->has_claim = 1; }
        else if (heading_is(cur, len, "Rejected"))  { s = S_REJECTED;  p->has_rejected = 1; }
        else if (heading_is(cur, len, "Falsifier")) { s = S_FALSIFIER; p->has_falsifier = 1; }
        else if (heading_is(cur, len, "Not-goals")) { s = S_NOTGOALS;  p->has_notgoals = 1; }
        else if (heading_is(cur, len, "Touches"))   { s = S_TOUCHES;   p->has_touches = 1; }
        else if (len >= 2 && cur[0] == '#' && cur[1] == '#') { s = S_NONE; }
        else if (s == S_CLAIM && nonblank(cur, len))     { claim_body = 1; }
        else if (s == S_FALSIFIER && nonblank(cur, len)) { fals_body = 1; }
        else if (s == S_NOTGOALS && nonblank(cur, len))  { ng_body = 1; }
        else if (s == S_REJECTED && len >= 2 && cur[0] == '-' && cur[1] == ' ') {
            if (has_reason(cur + 2, len - 2)) p->n_rejected++;
            else p->n_rejected_bare++;
        } else if (s == S_TOUCHES && nonblank(cur, len)) {
            const char *b = cur;
            jc_size bl = len;
            while (bl > 0 && (*b == ' ' || *b == '\t')) { b++; bl--; }
            if (bl >= 2 && b[0] == '-' && b[1] == ' ') { b += 2; bl -= 2; }
            while (bl > 0 && (*b == ' ' || *b == '`')) { b++; bl--; }
            while (bl > 0 && (b[bl - 1] == ' ' || b[bl - 1] == '`' || b[bl - 1] == ',')) bl--;
            if (bl > 0) {
                char *path = (char *)jc_arena_alloc(a, bl + 1);
                if (path != NULL) {
                    memcpy(path, b, bl);
                    path[bl] = '\0';
                    jc_vec_push(&p->touches, &path);
                }
            }
        }
        if (nl == NULL) break;
        cur = nl + 1;
    }
    /* a heading with an empty body is not a section */
    if (p->has_claim && !claim_body) p->has_claim = 0;
    if (p->has_falsifier && !fals_body) p->has_falsifier = 0;
    if (p->has_notgoals && !ng_body) p->has_notgoals = 0;
}


const char *jc_plan_missing(const struct jc_plan *p)
{
    if (!p->has_claim) {
        return "the plan is missing '## Claim' -- what will change, and why?";
    }
    if (!p->has_rejected) {
        return "the plan is missing '## Rejected' -- what alternative did you "
               "consider and drop? A plan with no rejected alternative was not "
               "decided, it was defaulted";
    }
    if (p->n_rejected == 0) {
        if (p->n_rejected_bare > 0) {
            return "every bullet under '## Rejected' needs the reason it lost, "
                   "after ' -- ': an alternative without its reason is a list, "
                   "not an argument";
        }
        return "'## Rejected' has no bullet -- name at least one alternative "
               "and why it lost ('- <alternative> -- <reason>')";
    }
    if (!p->has_falsifier) {
        return "the plan is missing '## Falsifier' -- what result would show "
               "this plan wrong? A plan nothing could refute cannot be checked";
    }
    if (!p->has_notgoals) {
        return "the plan is missing '## Not-goals' -- what does this leave alone "
               "on purpose?";
    }
    if (!p->has_touches || p->touches.len == 0) {
        return "the plan is missing '## Touches' -- which files does the work "
               "expect to change, one per line? The run is reconciled against "
               "this list";
    }
    return NULL;
}

static void append_list(struct jc_sb *sb, const char *list)
{
    /* newline- or "; "-separated items become "- item" lines */
    const char *cur = list;
    while (cur != NULL && *cur != '\0') {
        const char *end = cur;
        jc_size len;
        while (*end != '\0' && *end != '\n' && !(*end == ';' && end[1] == ' ')) end++;
        len = (jc_size)(end - cur);
        while (len > 0 && (*cur == ' ' || *cur == '\t')) { cur++; len--; }
        if (len >= 2 && cur[0] == '-' && cur[1] == ' ') { cur += 2; len -= 2; }
        while (len > 0 && (cur[len - 1] == ' ' || cur[len - 1] == '\r')) len--;
        if (len > 0) {
            jc_sb_append(sb, "- ");
            jc_sb_append_n(sb, cur, len);
            jc_sb_append(sb, "\n");
        }
        if (*end == '\0') break;
        cur = end + ((*end == ';') ? 2 : 1);
    }
}

char *jc_plan_render(const char *claim, const char *rejected,
                     const char *falsifier, const char *notgoals,
                     const char *touches)
{
    struct jc_sb sb;
    jc_sb_init(&sb);
    jc_sb_append(&sb, "# Plan\n\n## Claim\n");
    jc_sb_append(&sb, claim != NULL ? claim : "");
    jc_sb_append(&sb, "\n\n## Rejected\n");
    append_list(&sb, rejected != NULL ? rejected : "");
    jc_sb_append(&sb, "\n## Falsifier\n");
    jc_sb_append(&sb, falsifier != NULL ? falsifier : "");
    jc_sb_append(&sb, "\n\n## Not-goals\n");
    jc_sb_append(&sb, notgoals != NULL ? notgoals : "");
    jc_sb_append(&sb, "\n\n## Touches\n");
    append_list(&sb, touches != NULL ? touches : "");
    return jc_sb_finish(&sb);
}

static int path_eq(const char *a, const char *b)
{
    /* tolerate a leading "./" on either side */
    if (strncmp(a, "./", 2) == 0) a += 2;
    if (strncmp(b, "./", 2) == 0) b += 2;
    return strcmp(a, b) == 0;
}

int jc_plan_drift(const struct jc_plan *p, const struct jc_vec *wrote,
                  struct jc_vec *out, struct jc_arena *a)
{
    jc_size i, j;
    int n = 0;
    if (p == NULL || wrote == NULL) {
        return 0;
    }
    for (i = 0; i < wrote->len; i++) {
        const char *w = *(const char *const *)jc_vec_at((struct jc_vec *)wrote, i);
        int named = 0;
        if (w == NULL) continue;
        if (strncmp(w, ".jichi/", 7) == 0 || strncmp(w, "./.jichi/", 9) == 0) {
            continue; /* the plan file and the records are never drift */
        }
        for (j = 0; j < p->touches.len; j++) {
            const char *t = *(const char *const *)jc_vec_at((struct jc_vec *)&p->touches, j);
            if (t != NULL && path_eq(w, t)) { named = 1; break; }
        }
        if (!named) {
            char *copy = jc_arena_strdup(a, w);
            if (out != NULL && copy != NULL) {
                jc_vec_push(out, &copy);
            }
            n++;
        }
    }
    return n;
}

int jc_plan_load(const char *root, struct jc_plan *p, struct jc_arena *a)
{
    char path[1200];
    char *text = NULL;
    jc_plan_init(p);
    if (root == NULL) {
        return 0;
    }
    jc_snprintf(path, sizeof path, "%s/%s", root, JC_PLAN_PATH);
    if (jc_read_file(path, &text, NULL, a) != JC_OK) {
        return 0;
    }
    jc_plan_parse(text, p, a);
    return 1;
}
