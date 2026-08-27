/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_mdrender.c - streaming markdown + light syntax renderer (see jc_mdrender.h). */

#include "jc_mdrender.h"
#include "jc_str.h"

#include <string.h>

#define M_RST  "\x1b[0m"
#define M_BOLD "\x1b[1m"
#define M_DIM  "\x1b[2m"
#define M_ITAL "\x1b[3m"
#define M_HDR  "\x1b[1;36m"  /* heading: bold cyan       */
#define M_CODE "\x1b[32m"    /* code block text: green   */
#define M_ICODE "\x1b[36m"   /* inline `code`: cyan      */
#define M_STR  "\x1b[33m"    /* code strings: yellow     */
#define M_NUM  "\x1b[35m"    /* code numbers: magenta    */
#define M_CMT  "\x1b[2m"     /* code comments: dim       */
#define M_KW   "\x1b[34m"    /* code keywords: blue      */

static int is_word(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static int is_ident_start(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

/* Per-language highlighting facts, keyed off the ```lang fence tag. `tags` and
 * `kw` are space-separated whole-word lists; `lc` is the line-comment token;
 * `slashstar` enables single-line C-style block comments. The keyword sets are
 * representative, not exhaustive (highlighting is a readability aid). */
struct langdef {
    const char *tags;
    const char *kw;
    const char *lc;
    int         slashstar;
};

static const struct langdef LANGS[] = {
    { "c h cpp cc cxx hpp hh hxx c++ cplusplus",
      "auto break case char const continue default do double else enum extern "
      "float for goto if inline int long register return short signed sizeof "
      "static struct switch typedef union unsigned void volatile while bool true "
      "false class namespace template public private protected virtual new "
      "delete this nullptr using", "//", 1 },
    { "py python python3",
      "def class if elif else for while return import from as with try except "
      "finally raise yield lambda pass break continue global nonlocal in is not "
      "and or None True False async await del assert self", "#", 0 },
    { "js jsx ts tsx javascript typescript mjs cjs node",
      "function var let const if else for while do return class extends super "
      "new this typeof instanceof in of try catch finally throw switch case "
      "break continue default async await yield import export from as null "
      "undefined true false void delete interface type enum implements public "
      "private readonly", "//", 1 },
    { "go golang",
      "func var const type struct interface map chan package import if else "
      "for range return go defer select switch case default break continue "
      "fallthrough nil true false bool string int byte rune error", "//", 1 },
    { "rs rust",
      "fn let mut const static struct enum impl trait mod pub use crate self "
      "super if else match for while loop return ref move as dyn where unsafe "
      "async await box true false Some None Ok Err Option Result Vec String str "
      "usize", "//", 1 },
    { "sh bash shell zsh console",
      "if then else elif fi for while until do done case esac function in "
      "select return local export readonly declare echo exit set unset shift "
      "test", "#", 0 },
    { "lisp scheme scm racket rkt clojure clj cljs cljc elisp el",
      "define lambda let let* letrec if cond else when unless begin set! quote "
      "quasiquote unquote define-syntax syntax-rules require provide module "
      "struct case do and or not car cdr cons list map filter fold defn def fn "
      "loop recur ns defmacro defmodule", ";", 0 },
    { "java",
      "public private protected class interface extends implements new return "
      "if else for while do switch case break continue void int long double "
      "float boolean char byte short static final abstract synchronized try "
      "catch finally throw throws import package this super instanceof null "
      "true false enum volatile transient native", "//", 1 },
    { "rb ruby",
      "def class module if elsif else unless end while until for do return "
      "yield begin rescue ensure raise then case when nil true false self "
      "require require_relative attr_accessor attr_reader attr_writer puts new "
      "lambda proc", "#", 0 },
    { "hs haskell",
      "module import where let in do case of if then else data type newtype "
      "class instance deriving foreign default infixl infixr infix", "--", 0 },
    { "erl erlang",
      "module export import fun case of end if when receive after begin try "
      "catch throw spawn andalso orelse not band bor", "%", 0 },
    { "ex exs elixir",
      "def defp defmodule do end if else unless cond case when fn import alias "
      "require use receive after rescue raise true false nil and or not in "
      "defstruct defprotocol defimpl", "#", 0 },
    { "zig",
      "const var fn pub struct enum union if else for while switch return try "
      "catch defer errdefer comptime test orelse and or null undefined true "
      "false usize void bool anytype inline export extern", "//", 0 }
};

/* Is `id` (length idlen) one of the space-separated whole-word tokens in `list`? */
static int word_in(const char *list, const char *id, jc_size idlen)
{
    const char *p = list;
    while (*p != '\0') {
        jc_size k = 0;
        while (p[k] != '\0' && p[k] != ' ') {
            k++;
        }
        if (k == idlen && memcmp(p, id, idlen) == 0) {
            return 1;
        }
        p += k;
        while (*p == ' ') {
            p++;
        }
    }
    return 0;
}

/* The language definition for a fence tag, or NULL (generic highlight). */
static const struct langdef *lang_lookup(const char *tag)
{
    int n = (int)(sizeof(LANGS) / sizeof(LANGS[0]));
    int t;
    if (tag == NULL || tag[0] == '\0') {
        return NULL;
    }
    for (t = 0; t < n; t++) {
        if (word_in(LANGS[t].tags, tag, (jc_size)strlen(tag))) {
            return &LANGS[t];
        }
    }
    return NULL;
}

/* Inline spans on a non-code line: **bold**, *italic*, `code`. '_' emphasis is
 * deliberately not handled (too common in identifiers/paths). */
static void render_inline(struct jc_sb *out, const char *s, jc_size len)
{
    jc_size i = 0;
    while (i < len) {
        char c = s[i];
        if (c == '`') {
            jc_size j = i + 1;
            while (j < len && s[j] != '`') {
                j++;
            }
            if (j < len) {
                jc_sb_append(out, M_ICODE);
                jc_sb_append_n(out, s + i + 1, j - i - 1);
                jc_sb_append(out, M_RST);
                i = j + 1;
                continue;
            }
        } else if (c == '*' && i + 1 < len && s[i + 1] == '*') {
            jc_size j = i + 2;
            while (j + 1 < len && !(s[j] == '*' && s[j + 1] == '*')) {
                j++;
            }
            if (j + 1 < len && s[j] == '*' && s[j + 1] == '*') {
                jc_sb_append(out, M_BOLD);
                jc_sb_append_n(out, s + i + 2, j - (i + 2));
                jc_sb_append(out, M_RST);
                i = j + 2;
                continue;
            }
        } else if (c == '*') {
            jc_size j = i + 1;
            while (j < len && s[j] != '*') {
                j++;
            }
            if (j < len && j > i + 1) {
                jc_sb_append(out, M_ITAL);
                jc_sb_append_n(out, s + i + 1, j - i - 1);
                jc_sb_append(out, M_RST);
                i = j + 1;
                continue;
            }
        }
        jc_sb_append_char(out, c);
        i++;
    }
}

/* A line inside a code fence, highlighted for `langtag` (NULL/unknown => a
 * generic highlight). Heuristic and line-local: strings, the language's line
 * comment (plus single-line C-style block comments where they apply), numbers,
 * and keywords. Multi-line strings/block comments are not tracked across lines. */
static void render_code(struct jc_sb *out, const char *s, jc_size len,
                        const char *langtag, int *block)
{
    const struct langdef *L = lang_lookup(langtag);
    const char *kw = (L != NULL) ? L->kw : NULL;
    const char *lc = (L != NULL) ? L->lc : NULL;          /* line comment    */
    int slashstar = (L != NULL) ? L->slashstar : 1;       /* C-style comments */
    int hash_cmt = (L == NULL) || (lc != NULL && lc[0] == '#');
    jc_size lclen = (lc != NULL) ? (jc_size)strlen(lc) : 0;
    jc_size i = 0;
    char q = 0;

    jc_sb_append(out, M_CODE);

    /* Continuation of a block comment opened on an earlier line: dim until its
     * closing delimiter (then resume normal highlighting on the same line). */
    if (*block) {
        jc_size p = 0;
        int closed = 0;
        jc_sb_append(out, M_CMT);
        while (p + 1 < len) {
            if (s[p] == '*' && s[p + 1] == '/') { closed = 1; break; }
            p++;
        }
        if (!closed) {
            jc_sb_append_n(out, s, len);   /* whole line still in the comment */
            jc_sb_append(out, M_RST);
            return;
        }
        jc_sb_append_n(out, s, p + 2);     /* up to and including the close */
        jc_sb_append(out, M_CODE);
        *block = 0;
        i = p + 2;
    }

    while (i < len) {
        char c = s[i];
        if (q != 0) {                       /* inside a string */
            if (c == '\\' && i + 1 < len) {
                jc_sb_append_char(out, c);
                jc_sb_append_char(out, s[i + 1]);
                i += 2;
                continue;
            }
            jc_sb_append_char(out, c);
            if (c == q) {
                jc_sb_append(out, M_CODE);
                q = 0;
            }
            i++;
            continue;
        }
        /* Language line comment (";", "--", "%", "//"); "#" handled below. */
        if (lc != NULL && lc[0] != '#' && i + lclen <= len &&
            memcmp(s + i, lc, lclen) == 0) {
            jc_sb_append(out, M_CMT);
            jc_sb_append_n(out, s + i, len - i);
            jc_sb_append(out, M_RST);
            return;
        }
        /* "#" comment (python/shell/ruby/elixir, and generic): only when at the
         * start of a token, so C `#include` and `${x#y}` stay literal. */
        if (hash_cmt && c == '#' &&
            (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')) {
            jc_sb_append(out, M_CMT);
            jc_sb_append_n(out, s + i, len - i);
            jc_sb_append(out, M_RST);
            return;
        }
        if (slashstar && c == '/' && i + 1 < len && s[i + 1] == '/') {
            jc_sb_append(out, M_CMT);
            jc_sb_append_n(out, s + i, len - i);
            jc_sb_append(out, M_RST);
            return;
        }
        if (slashstar && c == '/' && i + 1 < len && s[i + 1] == '*') {
            jc_size j = i + 2;
            int closed = 0;
            while (j + 1 < len) {
                if (s[j] == '*' && s[j + 1] == '/') { closed = 1; break; }
                j++;
            }
            jc_sb_append(out, M_CMT);
            if (closed) {
                jc_sb_append_n(out, s + i, (j + 2) - i);
                jc_sb_append(out, M_CODE);
                i = j + 2;
            } else {
                /* Unterminated on this line -> continues on the next (see the
                 * *block handling at the top). */
                jc_sb_append_n(out, s + i, len - i);
                *block = 1;
                i = len;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            q = c;
            jc_sb_append(out, M_STR);
            jc_sb_append_char(out, c);
            i++;
            continue;
        }
        if (c >= '0' && c <= '9' && (i == 0 || !is_word(s[i - 1]))) {
            jc_size j = i;
            jc_sb_append(out, M_NUM);
            while (j < len && ((s[j] >= '0' && s[j] <= '9') || s[j] == '.' ||
                   s[j] == 'x' || (s[j] >= 'a' && s[j] <= 'f') ||
                   (s[j] >= 'A' && s[j] <= 'F'))) {
                j++;
            }
            jc_sb_append_n(out, s + i, j - i);
            jc_sb_append(out, M_CODE);
            i = j;
            continue;
        }
        if (kw != NULL && is_ident_start(c) && (i == 0 || !is_word(s[i - 1]))) {
            jc_size j = i;
            while (j < len && is_word(s[j])) {
                j++;
            }
            if (word_in(kw, s + i, j - i)) {
                jc_sb_append(out, M_KW);
                jc_sb_append_n(out, s + i, j - i);
                jc_sb_append(out, M_CODE);
            } else {
                jc_sb_append_n(out, s + i, j - i);
            }
            i = j;
            continue;
        }
        jc_sb_append_char(out, c);
        i++;
    }
    jc_sb_append(out, M_RST);
}

/* Classify and render one complete line (without its trailing newline). */
static void render_line(struct jc_mdr *r, const char *line, jc_size len,
                        struct jc_sb *out)
{
    int is_fence = (len >= 3 &&
        ((line[0] == '`' && line[1] == '`' && line[2] == '`') ||
         (line[0] == '~' && line[1] == '~' && line[2] == '~')));

    if (is_fence) {
        if (!r->in_fence) {
            /* Opening fence: capture the language tag (the info-string's first
             * word), lowercased, for per-language highlighting. */
            jc_size p = 0;
            jc_size o = 0;
            char fc = line[0];
            while (p < len && line[p] == fc) {
                p++;
            }
            while (p < len && (line[p] == ' ' || line[p] == '\t')) {
                p++;
            }
            while (p < len && line[p] != ' ' && line[p] != '\t' &&
                   o + 1 < (jc_size)sizeof(r->lang)) {
                char ch = line[p];
                if (ch >= 'A' && ch <= 'Z') {
                    ch = (char)(ch - 'A' + 'a');
                }
                r->lang[o++] = ch;
                p++;
            }
            r->lang[o] = '\0';
        } else {
            r->lang[0] = '\0';
        }
        r->in_block = 0; /* a fence boundary ends any open block comment */
        r->in_fence = !r->in_fence;
        if (r->color) {
            jc_sb_append(out, M_DIM);
            jc_sb_append_n(out, line, len);
            jc_sb_append(out, M_RST);
        } else {
            jc_sb_append_n(out, line, len);
        }
        return;
    }
    if (r->in_fence) {
        if (r->color) {
            render_code(out, line, len, r->lang, &r->in_block);
        } else {
            jc_sb_append_n(out, line, len);
        }
        return;
    }
    if (!r->color) {
        jc_sb_append_n(out, line, len);
        return;
    }

    /* ATX heading: 1-6 '#' then a space. */
    {
        jc_size h = 0;
        while (h < len && line[h] == '#') {
            h++;
        }
        if (h >= 1 && h <= 6 && h < len && line[h] == ' ') {
            jc_sb_append(out, M_HDR);
            jc_sb_append_n(out, line, len);
            jc_sb_append(out, M_RST);
            return;
        }
    }
    /* Blockquote. */
    if (len >= 1 && line[0] == '>') {
        jc_sb_append(out, M_DIM);
        jc_sb_append_n(out, line, len);
        jc_sb_append(out, M_RST);
        return;
    }
    /* Horizontal rule: only -, *, or _ (and spaces), at least 3. */
    {
        char hc = len > 0 ? line[0] : 0;
        if (hc == '-' || hc == '*' || hc == '_') {
            jc_size k;
            int ok = 1;
            for (k = 0; k < len; k++) {
                if (line[k] != hc && line[k] != ' ') {
                    ok = 0;
                    break;
                }
            }
            if (ok && len >= 3) {
                jc_sb_append(out, M_DIM);
                jc_sb_append_n(out, line, len);
                jc_sb_append(out, M_RST);
                return;
            }
        }
    }
    /* List item: indent + (-|*|+) + space, or indent + N. + space. */
    {
        jc_size k = 0;
        while (k < len && line[k] == ' ') {
            k++;
        }
        if (k < len && (line[k] == '-' || line[k] == '*' || line[k] == '+') &&
            k + 1 < len && line[k + 1] == ' ') {
            jc_sb_append_n(out, line, k + 2);
            render_inline(out, line + k + 2, len - (k + 2));
            return;
        }
        {
            jc_size d = k;
            while (d < len && line[d] >= '0' && line[d] <= '9') {
                d++;
            }
            if (d > k && d < len && line[d] == '.' && d + 1 < len &&
                line[d + 1] == ' ') {
                jc_sb_append_n(out, line, d + 2);
                render_inline(out, line + d + 2, len - (d + 2));
                return;
            }
        }
    }
    /* Default paragraph line. */
    render_inline(out, line, len);
}

void jc_mdr_init(struct jc_mdr *r, int color)
{
    jc_sb_init(&r->line);
    r->in_fence = 0;
    r->in_block = 0;
    r->color = color;
    r->lang[0] = '\0';
}

void jc_mdr_reset(struct jc_mdr *r)
{
    jc_sb_clear(&r->line);
    r->in_fence = 0;
    r->in_block = 0;
    r->lang[0] = '\0';
}

void jc_mdr_feed(struct jc_mdr *r, const char *delta, jc_size n,
                 struct jc_sb *out)
{
    jc_sb_append_n(&r->line, delta, n);
    for (;;) {
        char *nl = (r->line.data != NULL)
                   ? (char *)memchr(r->line.data, '\n', r->line.len) : NULL;
        jc_size linelen;
        jc_size rest;
        if (nl == NULL) {
            break;
        }
        linelen = (jc_size)(nl - r->line.data);
        render_line(r, r->line.data, linelen, out);
        jc_sb_append_char(out, '\n');
        rest = r->line.len - linelen - 1;
        memmove(r->line.data, r->line.data + linelen + 1, rest);
        r->line.len = rest;
        r->line.data[r->line.len] = '\0';
    }
}

void jc_mdr_flush(struct jc_mdr *r, struct jc_sb *out)
{
    if (r->line.len > 0) {
        render_line(r, r->line.data, r->line.len, out);
        jc_sb_clear(&r->line);
    }
}

void jc_mdr_free(struct jc_mdr *r)
{
    jc_sb_free(&r->line);
}
