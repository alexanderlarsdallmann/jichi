/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_yaml.c - minimal block-style YAML parser (see jc_yaml.h). */

#include "jc_yaml.h"
#include "jc_str.h"

#include <stdlib.h>
#include <string.h>

/* A preprocessed input line: indentation depth and the trimmed content.
 * `block` holds a pre-resolved block-scalar value (|, >) when the line declares
 * one, else NULL; the map/seq parsers use it verbatim instead of clean_scalar. */
struct line {
    int   indent;
    char *text;
    char *block;
};

/* --- node constructors --- */

static struct jc_yaml *new_node(struct jc_arena *a, jc_yaml_type t)
{
    struct jc_yaml *n = (struct jc_yaml *)jc_arena_alloc(a, sizeof(*n));
    if (n == NULL) {
        return NULL;
    }
    n->type = t;
    n->scalar = NULL;
    jc_vec_init(&n->items, sizeof(struct jc_yaml *));
    jc_vec_init(&n->keys, sizeof(char *));
    jc_vec_init(&n->vals, sizeof(struct jc_yaml *));
    return n;
}

static struct jc_yaml *new_scalar(struct jc_arena *a, char *s)
{
    struct jc_yaml *n = new_node(a, JC_YAML_SCALAR);
    if (n != NULL) {
        n->scalar = s;
    }
    return n;
}

/* --- scalar cleaning --- */

/* Trim, strip surrounding quotes, and drop a trailing " # comment" on
 * unquoted scalars. Returns an arena copy. */
static char *clean_scalar(struct jc_arena *a, const char *raw)
{
    jc_size len;
    const char *start = raw;
    const char *end;

    while (*start == ' ' || *start == '\t') {
        start++;
    }
    len = strlen(start);
    end = start + len;

    /* Quoted scalar: strip the matching quotes (minimal unescaping). */
    if (len >= 2 && (start[0] == '"' || start[0] == '\'') &&
        end[-1] == start[0]) {
        char q = start[0];
        struct jc_sb sb;
        const char *p;
        char *out;
        jc_sb_init(&sb);
        for (p = start + 1; p < end - 1; p++) {
            if (q == '"' && *p == '\\' && p + 1 < end - 1) {
                p++;
                if (*p == 'n') jc_sb_append_char(&sb, '\n');
                else if (*p == 't') jc_sb_append_char(&sb, '\t');
                else jc_sb_append_char(&sb, *p);
            } else {
                jc_sb_append_char(&sb, *p);
            }
        }
        out = jc_arena_strdup(a, sb.data != NULL ? sb.data : "");
        jc_sb_free(&sb);
        return out;
    }

    /* Unquoted: cut at a " #" inline comment. */
    {
        const char *c;
        for (c = start; c + 1 < end; c++) {
            if (c[0] == ' ' && c[1] == '#') {
                end = c;
                break;
            }
        }
    }
    /* Right-trim. */
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }
    return jc_arena_strndup(a, start, (jc_size)(end - start));
}

/* Find the index of the key/value separator ':' (followed by space or EOL). */
static int find_colon(const char *text)
{
    int i;
    for (i = 0; text[i] != '\0'; i++) {
        if (text[i] == ':' && (text[i + 1] == ' ' || text[i + 1] == '\0')) {
            return i;
        }
    }
    return -1;
}

static int is_dash(const char *text)
{
    return text[0] == '-' && (text[1] == ' ' || text[1] == '\0');
}

/* M409: is this whole line ONE quoted string ("..." or '...'), nothing after
 * the closing quote but whitespace? Then it is a scalar no matter what it
 * contains -- quotes are YAML's way of SAYING "scalar". Before this check,
 * `- "Run it first: cd ..."` fell through to find_colon, which found the `: `
 * INSIDE the quotes and mis-read the item as a mapping; the item's scalar was
 * then NULL and jc_assign's M289 skip silently deleted the hint. 64 of the 80
 * shipped assignment ladders were serving fewer rungs than written.
 *
 * Deliberately narrow: the FIRST closing quote must end the line. A quoted key
 * (`"name": beta`) has content after its closing quote, so it still parses as
 * a mapping; a double-quoted scalar honours backslash escapes the same way
 * clean_scalar does, so the two agree on where the string ends. */
static int quoted_whole_line(const char *text)
{
    const char *p;
    char q = text[0];
    if (q != '"' && q != '\'') {
        return 0;
    }
    for (p = text + 1; *p != '\0'; p++) {
        if (q == '"' && *p == '\\' && p[1] != '\0') {
            p++;
            continue;
        }
        if (*p == q) {
            p++;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            return *p == '\0';
        }
    }
    return 0;
}

/* --- block scalars (|, >) --- */

/* If `s` is a block-scalar indicator (optionally with chomping/indent digit and
 * a trailing comment), set *literal (1 for '|', 0 for '>') and *chomp (-1 strip,
 * 0 clip/default, +1 keep) and return 1. Otherwise return 0. */
static int parse_block_header(const char *s, int *literal, int *chomp)
{
    const char *p = s;
    if (*p != '|' && *p != '>') {
        return 0;
    }
    *literal = (*p == '|');
    *chomp = 0;
    p++;
    while (*p == '-' || *p == '+' || (*p >= '1' && *p <= '9')) {
        if (*p == '-') {
            *chomp = -1;
        } else if (*p == '+') {
            *chomp = 1;
        }
        p++;
    }
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return (*p == '\0' || *p == '#') ? 1 : 0;
}

/* Read a block scalar body. On entry *pp points at the first line after the
 * indicator; content is every following line indented deeper than key_indent
 * (blank lines included). Advances *pp past the body and returns the joined,
 * chomped value (arena-owned). */
static char *read_block_scalar(const char **pp, int key_indent,
                               int literal, int chomp, struct jc_arena *a)
{
    const char *p = *pp;
    int block_indent = -1;
    struct jc_vec lines;
    struct jc_sb sb;
    char *result;
    jc_size i;

    jc_vec_init(&lines, sizeof(char *));
    while (*p != '\0') {
        const char *eol = p;
        const char *c;
        int ind = 0;
        int blank;
        char *dl;

        while (*eol != '\0' && *eol != '\n') {
            eol++;
        }
        c = p;
        while (c < eol && (*c == ' ' || *c == '\t')) {
            ind++;
            c++;
        }
        blank = (c == eol);
        if (!blank && ind <= key_indent) {
            break;
        }
        if (block_indent < 0 && !blank) {
            block_indent = ind;
        }
        {
            const char *s = p;
            const char *e = eol;
            int skip = (block_indent < 0) ? 0 : block_indent;
            int k = 0;
            while (k < skip && s < eol && (*s == ' ' || *s == '\t')) {
                s++;
                k++;
            }
            while (e > s && e[-1] == '\r') {
                e--;
            }
            dl = jc_arena_strndup(a, s, (jc_size)(e - s));
        }
        jc_vec_push(&lines, &dl);
        p = (*eol == '\0') ? eol : eol + 1;
    }
    *pp = p;

    jc_sb_init(&sb);
    if (literal) {
        for (i = 0; i < lines.len; i++) {
            jc_sb_append(&sb, *(char **)jc_vec_at(&lines, i));
            jc_sb_append_char(&sb, '\n');
        }
    } else {
        int prev_nonblank = 0;
        for (i = 0; i < lines.len; i++) {
            const char *ln = *(char **)jc_vec_at(&lines, i);
            if (ln[0] != '\0') {
                if (prev_nonblank) {
                    jc_sb_append_char(&sb, ' ');
                }
                jc_sb_append(&sb, ln);
                prev_nonblank = 1;
            } else {
                jc_sb_append_char(&sb, '\n');
                prev_nonblank = 0;
            }
        }
        if (lines.len > 0) {
            jc_sb_append_char(&sb, '\n');
        }
    }
    jc_vec_free(&lines);

    /* Chomp trailing newlines. */
    if (sb.data != NULL) {
        jc_size n = sb.len;
        while (n > 0 && sb.data[n - 1] == '\n') {
            n--;
        }
        if (chomp < 0) {
            sb.data[n] = '\0';
            sb.len = n;
        } else if (chomp == 0) {
            if (n > 0) {
                sb.data[n] = '\n';
                sb.data[n + 1] = '\0';
                sb.len = n + 1;
            } else {
                sb.data[0] = '\0';
                sb.len = 0;
            }
        }
    }
    result = jc_arena_strdup(a, sb.data != NULL ? sb.data : "");
    jc_sb_free(&sb);
    return result;
}

/* --- line preprocessing --- */

static jc_status split_lines(const char *text, struct jc_vec *out,
                             struct jc_arena *a)
{
    const char *p = text;
    while (*p != '\0') {
        const char *eol = p;
        const char *content;
        int indent = 0;
        jc_size rawlen;
        char *raw;
        const char *q;

        while (*eol != '\0' && *eol != '\n') {
            eol++;
        }
        rawlen = (jc_size)(eol - p);
        raw = jc_arena_strndup(a, p, rawlen);

        /* Count indentation. */
        content = raw;
        while (*content == ' ' || *content == '\t') {
            indent++;
            content++;
        }
        /* Right-trim. */
        {
            char *e = raw + strlen(raw);
            while (e > content && (e[-1] == ' ' || e[-1] == '\t' ||
                                   e[-1] == '\r')) {
                e--;
            }
            *e = '\0';
        }

        q = content;
        if (*q != '\0' && *q != '#' &&
            strcmp(content, "---") != 0 && strcmp(content, "...") != 0) {
            struct line ln;
            int literal = 1;
            int chomp = 0;
            int is_block = 0;
            char *head = NULL;

            ln.indent = indent;
            ln.text = (char *)content;
            ln.block = NULL;

            /* Detect a block-scalar declarer: `key: |` (map) or `- |` (seq). */
            if (is_dash(content)) {
                const char *rest = content + 1;
                while (*rest == ' ') {
                    rest++;
                }
                if (parse_block_header(rest, &literal, &chomp)) {
                    is_block = 1;
                    head = jc_arena_strndup(a, "-", 1);
                }
            } else {
                int ci = find_colon(content);
                if (ci >= 0) {
                    const char *v = content + ci + 1;
                    while (*v == ' ') {
                        v++;
                    }
                    if (parse_block_header(v, &literal, &chomp)) {
                        is_block = 1;
                        head = jc_arena_strndup(a, content, (jc_size)(ci + 1));
                    }
                }
            }

            if (is_block) {
                const char *bp = (*eol == '\0') ? eol : eol + 1;
                ln.text = head;
                ln.block = read_block_scalar(&bp, indent, literal, chomp, a);
                if (jc_vec_push(out, &ln) != JC_OK) {
                    return JC_ERR_OOM;
                }
                p = bp;
                continue;
            }

            if (jc_vec_push(out, &ln) != JC_OK) {
                return JC_ERR_OOM;
            }
        }

        if (*eol == '\0') {
            break;
        }
        p = eol + 1;
    }
    return JC_OK;
}

/* --- recursive descent --- */

static struct jc_yaml *parse_node(struct jc_vec *lines, jc_size *idx,
                                  int indent, struct jc_arena *a);

static struct jc_yaml *parse_seq(struct jc_vec *lines, jc_size *idx,
                                 int indent, struct jc_arena *a)
{
    struct jc_yaml *seq = new_node(a, JC_YAML_SEQ);
    while (*idx < lines->len) {
        struct line *ln = (struct line *)jc_vec_at(lines, *idx);
        const char *rest;
        int col;
        struct jc_yaml *child;

        if (ln->indent != indent || !is_dash(ln->text)) {
            break;
        }
        rest = ln->text + 1;
        while (*rest == ' ') {
            rest++;
        }
        col = indent + (int)(rest - ln->text);

        if (*rest == '\0') {
            (*idx)++;
            if (ln->block != NULL) {
                child = new_scalar(a, ln->block);
            } else if (*idx < lines->len) {
                struct line *nx = (struct line *)jc_vec_at(lines, *idx);
                if (nx->indent > indent) {
                    child = parse_node(lines, idx, nx->indent, a);
                } else {
                    child = new_scalar(a, jc_arena_strdup(a, ""));
                }
            } else {
                child = new_scalar(a, jc_arena_strdup(a, ""));
            }
        } else {
            /* Inline content after the dash: reparse this line at `col`. */
            ln->indent = col;
            ln->text = (char *)rest;
            child = parse_node(lines, idx, col, a);
        }
        jc_vec_push(&seq->items, &child);
    }
    return seq;
}

static struct jc_yaml *parse_map(struct jc_vec *lines, jc_size *idx,
                                 int indent, struct jc_arena *a)
{
    struct jc_yaml *map = new_node(a, JC_YAML_MAP);
    while (*idx < lines->len) {
        struct line *ln = (struct line *)jc_vec_at(lines, *idx);
        int ci;
        char *key;
        const char *valtext;
        struct jc_yaml *val;

        if (ln->indent != indent || is_dash(ln->text)) {
            break;
        }
        /* M409: a whole-line quoted string is a scalar even when it contains
         * a `: ` -- checked BEFORE find_colon, which cannot see quotes. Only
         * in first position (like the bare-scalar branch below): inside an
         * already-started map the line keeps its old meaning. */
        if (map->keys.len == 0 && quoted_whole_line(ln->text)) {
            struct jc_yaml *s = new_scalar(a, clean_scalar(a, ln->text));
            (*idx)++;
            return s;
        }
        ci = find_colon(ln->text);
        if (ci < 0) {
            /* A bare scalar where a mapping was expected. */
            if (map->keys.len == 0) {
                struct jc_yaml *s = new_scalar(a, clean_scalar(a, ln->text));
                (*idx)++;
                return s;
            }
            break;
        }

        key = jc_arena_strndup(a, ln->text, (jc_size)ci);
        {
            /* Right-trim the key. */
            char *e = key + strlen(key);
            while (e > key && (e[-1] == ' ' || e[-1] == '\t')) {
                e--;
            }
            *e = '\0';
        }
        valtext = ln->text + ci + 1;
        while (*valtext == ' ') {
            valtext++;
        }
        (*idx)++;

        if (ln->block != NULL) {
            val = new_scalar(a, ln->block);
        } else if (*valtext != '\0') {
            val = new_scalar(a, clean_scalar(a, valtext));
        } else if (*idx < lines->len) {
            struct line *nx = (struct line *)jc_vec_at(lines, *idx);
            if (nx->indent > indent) {
                val = parse_node(lines, idx, nx->indent, a);
            } else {
                val = new_scalar(a, jc_arena_strdup(a, ""));
            }
        } else {
            val = new_scalar(a, jc_arena_strdup(a, ""));
        }

        jc_vec_push(&map->keys, &key);
        jc_vec_push(&map->vals, &val);
    }
    return map;
}

static struct jc_yaml *parse_node(struct jc_vec *lines, jc_size *idx,
                                  int indent, struct jc_arena *a)
{
    struct line *ln;
    if (*idx >= lines->len) {
        return NULL;
    }
    ln = (struct line *)jc_vec_at(lines, *idx);
    if (is_dash(ln->text)) {
        return parse_seq(lines, idx, indent, a);
    }
    return parse_map(lines, idx, indent, a);
}

struct jc_yaml *jc_yaml_parse(const char *text, struct jc_arena *a)
{
    struct jc_vec lines;
    jc_size idx = 0;
    struct jc_yaml *root;

    if (text == NULL) {
        return NULL;
    }
    jc_vec_init(&lines, sizeof(struct line));
    if (split_lines(text, &lines, a) != JC_OK) {
        jc_vec_free(&lines);
        return NULL;
    }
    if (lines.len == 0) {
        jc_vec_free(&lines);
        return NULL;
    }
    {
        struct line *first = (struct line *)jc_vec_at(&lines, 0);
        root = parse_node(&lines, &idx, first->indent, a);
    }
    jc_vec_free(&lines);
    return root;
}

void jc_yaml_free(struct jc_yaml *node)
{
    jc_size i;
    if (node == NULL) {
        return;
    }
    for (i = 0; i < node->items.len; i++) {
        jc_yaml_free(*(struct jc_yaml **)jc_vec_at(&node->items, i));
    }
    for (i = 0; i < node->vals.len; i++) {
        jc_yaml_free(*(struct jc_yaml **)jc_vec_at(&node->vals, i));
    }
    /* All three vectors are initialised in new_node, so freeing the unused ones
     * for a given node type is a safe no-op. The node and its scalar/keys are
     * arena-owned and released with the arena. */
    jc_vec_free(&node->items);
    jc_vec_free(&node->keys);
    jc_vec_free(&node->vals);
}

/* --- accessors --- */

struct jc_yaml *jc_yaml_get(const struct jc_yaml *node, const char *key)
{
    jc_size i;
    if (node == NULL || node->type != JC_YAML_MAP) {
        return NULL;
    }
    for (i = 0; i < node->keys.len; i++) {
        const char *k = *(char **)jc_vec_at((struct jc_vec *)&node->keys, i);
        if (strcmp(k, key) == 0) {
            return *(struct jc_yaml **)jc_vec_at((struct jc_vec *)&node->vals, i);
        }
    }
    return NULL;
}

const char *jc_yaml_get_str(const struct jc_yaml *node, const char *key,
                            const char *dflt)
{
    struct jc_yaml *v = jc_yaml_get(node, key);
    if (v != NULL && v->type == JC_YAML_SCALAR) {
        return v->scalar;
    }
    return dflt;
}

jc_size jc_yaml_seq_len(const struct jc_yaml *node)
{
    if (node == NULL || node->type != JC_YAML_SEQ) {
        return 0;
    }
    return node->items.len;
}

struct jc_yaml *jc_yaml_seq_at(const struct jc_yaml *node, jc_size i)
{
    if (node == NULL || node->type != JC_YAML_SEQ) {
        return NULL;
    }
    return *(struct jc_yaml **)jc_vec_at((struct jc_vec *)&node->items, i);
}
