/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_lsp_proto.c - pure LSP framing, URI/language helpers, and diagnostics
 * formatting. No I/O; exercised offline by the test suite.
 */

#include "jc_lsp.h"
#include "jc_json.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

void jc_lsp_frame_encode(const char *body, struct jc_sb *out)
{
    char hdr[64];
    jc_snprintf(hdr, sizeof(hdr), "Content-Length: %lu\r\n\r\n",
                (unsigned long)strlen(body));
    jc_sb_append(out, hdr);
    jc_sb_append(out, body);
}

void jc_lsp_framer_init(struct jc_lsp_framer *f)
{
    jc_sb_init(&f->buf);
}

void jc_lsp_framer_free(struct jc_lsp_framer *f)
{
    jc_sb_free(&f->buf);
}

void jc_lsp_framer_push(struct jc_lsp_framer *f, const char *bytes, jc_size n)
{
    jc_sb_append_n(&f->buf, bytes, n);
}

/* Case-insensitive scan of [s, s+n) for the Content-Length value. Returns the
 * length, or (jc_size)-1 if absent/malformed. */
static jc_size find_content_length(const char *s, jc_size n)
{
    const char *key = "content-length:";
    jc_size klen = 15;
    jc_size i;
    for (i = 0; i + klen <= n; i++) {
        jc_size j;
        for (j = 0; j < klen; j++) {
            int c = s[i + j];
            if (c >= 'A' && c <= 'Z') {
                c += 32;
            }
            if (c != key[j]) {
                break;
            }
        }
        if (j == klen) {
            jc_size v = 0;
            jc_size p = i + klen;
            int seen = 0;
            while (p < n && (s[p] == ' ' || s[p] == '\t')) {
                p++;
            }
            while (p < n && s[p] >= '0' && s[p] <= '9') {
                /* M609: overflow guard. The header comment on JC_LSP_MAX_BODY
                 * called this arithmetic "safe by accident" -- safe only because
                 * a wrapped value tended to fail the malloc or collide with the
                 * (jc_size)-1 sentinel. A value that wraps to something small and
                 * in-range instead (e.g. 2^64+5 -> 5) would make the framer
                 * consume the wrong byte count and desync the stream for good.
                 * An overflowing length is malformed: return the sentinel. */
                if (v > ((jc_size)-1 - 9) / 10) {
                    return (jc_size)-1;
                }
                v = v * 10 + (jc_size)(s[p] - '0');
                p++;
                seen = 1;
            }
            return seen ? v : (jc_size)-1;
        }
    }
    return (jc_size)-1;
}

int jc_lsp_framer_pop(struct jc_lsp_framer *f, char **body_out)
{
    char *data = f->buf.data;
    jc_size len = f->buf.len;
    jc_size i;
    jc_size hdr_len;
    jc_size clen;
    jc_size total;
    char *body;

    *body_out = NULL;
    if (data == NULL || len < 4) {
        return 0;
    }
    hdr_len = (jc_size)-1;
    for (i = 0; i + 3 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n' &&
            data[i + 2] == '\r' && data[i + 3] == '\n') {
            hdr_len = i;
            break;
        }
    }
    if (hdr_len == (jc_size)-1) {
        /* M609: header block not terminated yet. If it has grown past the cap,
         * the terminator is not coming (a hostile or broken server): drop
         * everything and resync, rather than buffer toward an OOM. */
        if (len > (jc_size)JC_LSP_MAX_HEADER) {
            f->buf.len = 0;
            if (data != NULL) {
                data[0] = '\0';
            }
        }
        return 0; /* header block incomplete */
    }
    clen = find_content_length(data, hdr_len);
    if (clen == (jc_size)-1) {
        /* Malformed header block: drop it to resync. */
        jc_size after = hdr_len + 4;
        memmove(data, data + after, len - after);
        f->buf.len = len - after;
        data[f->buf.len] = '\0';
        return 0;
    }
    if (clen > (jc_size)JC_LSP_MAX_BODY) {
        /* Refuse and resync rather than buffer toward it (M472). Dropping the
         * header block is what the malformed-header path above already does, so
         * a server that recovers is not permanently wedged by one bad frame. */
        jc_size after = hdr_len + 4;
        memmove(data, data + after, len - after);
        f->buf.len = len - after;
        data[f->buf.len] = '\0';
        return 0;
    }
    total = hdr_len + 4 + clen;   /* both bounded now: no overflow possible */
    if (len < total) {
        return 0; /* body not fully arrived yet */
    }
    body = (char *)malloc(clen + 1);
    if (body == NULL) {
        return 0;
    }
    memcpy(body, data + hdr_len + 4, clen);
    body[clen] = '\0';
    memmove(data, data + total, len - total);
    f->buf.len = len - total;
    data[f->buf.len] = '\0';
    *body_out = body;
    return 1;
}

void jc_lsp_path_to_uri(const char *path, const char *cwd, char *buf,
                        jc_size cap)
{
    if (path != NULL && path[0] == '/') {
        jc_snprintf(buf, cap, "file://%s", path);
    } else {
        jc_snprintf(buf, cap, "file://%s/%s", cwd != NULL ? cwd : ".",
                    path != NULL ? path : "");
    }
}

const char *jc_lsp_language_id(const char *ext)
{
    if (ext == NULL) {
        return "";
    }
    if (strcmp(ext, "c") == 0 || strcmp(ext, "h") == 0) {
        return "c";
    }
    if (strcmp(ext, "cc") == 0 || strcmp(ext, "cpp") == 0 ||
        strcmp(ext, "cxx") == 0 || strcmp(ext, "hpp") == 0 ||
        strcmp(ext, "hh") == 0) {
        return "cpp";
    }
    if (strcmp(ext, "py") == 0) {
        return "python";
    }
    if (strcmp(ext, "go") == 0) {
        return "go";
    }
    if (strcmp(ext, "rs") == 0) {
        return "rust";
    }
    if (strcmp(ext, "ts") == 0) {
        return "typescript";
    }
    if (strcmp(ext, "js") == 0) {
        return "javascript";
    }
    return ext;
}

int jc_lsp_suggest(const char *lang, struct jc_lsp_suggestion *out)
{
    /* Keyed by both language names and common file extensions. */
    static const struct {
        const char *keys;   /* space-separated aliases */
        struct jc_lsp_suggestion s;
    } T[] = {
        { "c cpp h hpp cc cxx hh c++",
          { "clangd", "c,h,cpp,cc,cxx,hpp,hh", "install clangd (LLVM/clang)" } },
        { "python py",
          { "pyright-langserver", "py",
            "npm i -g pyright (or use pylsp: pip install python-lsp-server)" } },
        { "go",
          { "gopls", "go", "go install golang.org/x/tools/gopls@latest" } },
        { "rust rs",
          { "rust-analyzer", "rs", "rustup component add rust-analyzer" } },
        { "zig",
          { "zls", "zig", "install zls (github.com/zigtools/zls)" } },
        { "typescript javascript ts js tsx jsx",
          { "typescript-language-server", "ts,tsx,js,jsx",
            "npm i -g typescript-language-server typescript" } },
        { "ruby rb",
          { "solargraph", "rb", "gem install solargraph" } }
    };
    int i, n = (int)(sizeof(T) / sizeof(T[0]));
    if (lang == NULL || out == NULL) return 0;
    for (i = 0; i < n; i++) {
        const char *p = T[i].keys;
        jc_size ll = (jc_size)strlen(lang);
        while (*p != '\0') {
            const char *sp = p;
            jc_size kl;
            while (*sp != '\0' && *sp != ' ') sp++;
            kl = (jc_size)(sp - p);
            if (kl == ll && strncmp(p, lang, kl) == 0) {
                *out = T[i].s;
                return 1;
            }
            p = (*sp == ' ') ? sp + 1 : sp;
        }
    }
    return 0;
}

static const char *severity_name(int sev)
{
    switch (sev) {
    case 1: return "error";
    case 2: return "warning";
    case 3: return "info";
    case 4: return "hint";
    }
    return "diag";
}

int jc_lsp_format_diagnostics(const char *params_json, const char *want_uri,
                              const char *display_path, struct jc_sb *out,
                              int *count)
{
    cJSON *root;
    const char *uri;
    cJSON *diags;
    cJSON *d;
    int n = 0;

    if (count != NULL) {
        *count = 0;
    }
    root = jc_json_parse(params_json);
    if (root == NULL) {
        return 0;
    }
    uri = jc_json_get_str(root, "uri", NULL);
    if (uri == NULL || want_uri == NULL || strcmp(uri, want_uri) != 0) {
        cJSON_Delete(root);
        return 0;
    }
    diags = jc_json_get_obj(root, "diagnostics");
    if (cJSON_IsArray(diags)) {
        cJSON_ArrayForEach(d, diags) {
            cJSON *range = jc_json_get_obj(d, "range");
            cJSON *start = (range != NULL) ? jc_json_get_obj(range, "start")
                                           : NULL;
            long line = (start != NULL)
                        ? (long)jc_json_get_num(start, "line", 0.0) : 0;
            long col = (start != NULL)
                       ? (long)jc_json_get_num(start, "character", 0.0) : 0;
            int sev = (int)jc_json_get_num(d, "severity", 1.0);
            const char *msg = jc_json_get_str(d, "message", "");
            jc_sb_append_fmt(out, "%s:%ld:%ld: %s: %s\n",
                             display_path != NULL ? display_path : "",
                             line + 1, col + 1, severity_name(sev), msg);
            n++;
        }
    }
    if (count != NULL) {
        *count = n;
    }
    cJSON_Delete(root);
    return 1;
}

char *jc_lsp_diagnostics_for_line(const char *params_json, const char *want_uri,
                                  long line0)
{
    cJSON *root;
    const char *uri;
    cJSON *diags;
    cJSON *d;
    struct jc_sb out;
    int n = 0;

    jc_sb_init(&out);
    jc_sb_append_char(&out, '[');
    if (params_json == NULL || want_uri == NULL) {
        jc_sb_append_char(&out, ']');
        return jc_sb_finish(&out);
    }
    root = jc_json_parse(params_json);
    if (root == NULL) {
        jc_sb_append_char(&out, ']');
        return jc_sb_finish(&out);
    }
    uri = jc_json_get_str(root, "uri", NULL);
    diags = jc_json_get_obj(root, "diagnostics");
    if (uri != NULL && strcmp(uri, want_uri) == 0 && cJSON_IsArray(diags)) {
        cJSON_ArrayForEach(d, diags) {
            cJSON *range = jc_json_get_obj(d, "range");
            cJSON *start = (range != NULL) ? jc_json_get_obj(range, "start")
                                           : NULL;
            cJSON *end = (range != NULL) ? jc_json_get_obj(range, "end") : NULL;
            long sl = (start != NULL)
                      ? (long)jc_json_get_num(start, "line", 0.0) : 0;
            /* A diagnostic with no explicit end spans just its start line. */
            long el = (end != NULL) ? (long)jc_json_get_num(end, "line", 0.0)
                                    : sl;
            if (line0 >= sl && line0 <= el) {
                char *one = jc_json_print(d);
                if (one != NULL) {
                    if (n > 0) {
                        jc_sb_append_char(&out, ',');
                    }
                    jc_sb_append(&out, one);
                    free(one);
                    n++;
                }
            }
        }
    }
    cJSON_Delete(root);
    jc_sb_append_char(&out, ']');
    return jc_sb_finish(&out);
}

char *jc_lsp_only_array(const char *csv)
{
    cJSON *arr;
    char *out;
    const char *p;
    int n = 0;

    if (csv == NULL) {
        return NULL;
    }
    arr = cJSON_CreateArray();
    if (arr == NULL) {
        return NULL;
    }
    p = csv;
    while (*p != '\0') {
        const char *start;
        char tok[128];
        jc_size len;
        while (*p == ',' || *p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        start = p;
        while (*p != '\0' && *p != ',' && *p != ' ' && *p != '\t') {
            p++;
        }
        len = (jc_size)(p - start);
        if (len > 0 && len < sizeof(tok)) {
            memcpy(tok, start, len);
            tok[len] = '\0';
            cJSON_AddItemToArray(arr, cJSON_CreateString(tok));
            n++;
        }
    }
    if (n == 0) {
        cJSON_Delete(arr);
        return NULL; /* no tokens => no filter */
    }
    out = jc_json_print(arr);
    cJSON_Delete(arr);
    return out;
}

/* Strip a leading "file://" so a URI prints as a plain path. */
static const char *uri_to_path(const char *uri)
{
    if (uri == NULL) {
        return "";
    }
    if (strncmp(uri, "file://", 7) == 0) {
        return uri + 7;
    }
    return uri;
}

/* Append "<path>:<line>:<col>\n" for a Location or LocationLink object. */
static int append_location(cJSON *o, struct jc_sb *out)
{
    const char *uri = jc_json_get_str(o, "uri", NULL);
    cJSON *range = jc_json_get_obj(o, "range");
    cJSON *start;
    long line;
    long col;

    if (uri == NULL) { /* LocationLink shape */
        uri = jc_json_get_str(o, "targetUri", NULL);
        range = jc_json_get_obj(o, "targetRange");
    }
    if (uri == NULL || range == NULL) {
        return 0;
    }
    start = jc_json_get_obj(range, "start");
    line = (start != NULL) ? (long)jc_json_get_num(start, "line", 0.0) : 0;
    col = (start != NULL) ? (long)jc_json_get_num(start, "character", 0.0) : 0;
    jc_sb_append_fmt(out, "%s:%ld:%ld\n", uri_to_path(uri), line + 1, col + 1);
    return 1;
}

int jc_lsp_format_locations(const char *result_json, struct jc_sb *out,
                            int *count)
{
    cJSON *root;
    int n = 0;

    if (count != NULL) {
        *count = 0;
    }
    if (result_json == NULL) {
        return 0;
    }
    root = jc_json_parse(result_json);
    if (root == NULL) {
        return 0;
    }
    if (cJSON_IsArray(root)) {
        cJSON *e;
        cJSON_ArrayForEach(e, root) {
            if (append_location(e, out)) {
                n++;
            }
        }
    } else if (cJSON_IsObject(root)) {
        if (append_location(root, out)) {
            n++;
        }
    }
    if (count != NULL) {
        *count = n;
    }
    cJSON_Delete(root);
    return 1;
}

/* LSP SymbolKind (subset) -> a short name. */
static const char *symbol_kind(int k)
{
    switch (k) {
    case 2:  return "module";
    case 5:  return "class";
    case 6:  return "method";
    case 8:  return "field";
    case 9:  return "constructor";
    case 10: return "enum";
    case 11: return "interface";
    case 12: return "function";
    case 13: return "variable";
    case 14: return "constant";
    case 22: return "enum-member";
    case 23: return "struct";
    }
    return "symbol";
}

/* Append a hierarchical DocumentSymbol (recursing children, depth-bounded). */
static void append_doc_symbol(cJSON *s, struct jc_sb *out, int depth, int *n)
{
    const char *name = jc_json_get_str(s, "name", "?");
    int kind = (int)jc_json_get_num(s, "kind", 0.0);
    cJSON *range = jc_json_get_obj(s, "range");
    cJSON *start = (range != NULL) ? jc_json_get_obj(range, "start") : NULL;
    long line = (start != NULL) ? (long)jc_json_get_num(start, "line", 0.0) : 0;
    cJSON *children;
    int i;

    if (depth > 8) {
        return;
    }
    for (i = 0; i < depth; i++) {
        jc_sb_append(out, "  ");
    }
    jc_sb_append_fmt(out, "%s %s  (line %ld)\n", symbol_kind(kind), name,
                     line + 1);
    (*n)++;
    children = jc_json_get_obj(s, "children");
    if (cJSON_IsArray(children)) {
        cJSON *c;
        cJSON_ArrayForEach(c, children) {
            append_doc_symbol(c, out, depth + 1, n);
        }
    }
}

int jc_lsp_format_symbols(const char *result_json, struct jc_sb *out,
                          int *count)
{
    cJSON *root;
    int n = 0;

    if (count != NULL) {
        *count = 0;
    }
    if (result_json == NULL) {
        return 0;
    }
    root = jc_json_parse(result_json);
    if (root == NULL) {
        return 0;
    }
    if (cJSON_IsArray(root)) {
        cJSON *e;
        cJSON_ArrayForEach(e, root) {
            cJSON *loc = jc_json_get_obj(e, "location");
            if (loc != NULL) {
                /* SymbolInformation / WorkspaceSymbol (flat, with a location). */
                const char *name = jc_json_get_str(e, "name", "?");
                int kind = (int)jc_json_get_num(e, "kind", 0.0);
                const char *uri = jc_json_get_str(loc, "uri", NULL);
                cJSON *range = jc_json_get_obj(loc, "range");
                cJSON *start = (range != NULL) ? jc_json_get_obj(range, "start")
                                               : NULL;
                long line = (start != NULL)
                            ? (long)jc_json_get_num(start, "line", 0.0) : 0;
                jc_sb_append_fmt(out, "%s %s  %s:%ld\n", symbol_kind(kind),
                                 name, uri_to_path(uri), line + 1);
                n++;
            } else {
                append_doc_symbol(e, out, 0, &n);
            }
        }
    }
    if (count != NULL) {
        *count = n;
    }
    cJSON_Delete(root);
    return 1;
}

int jc_lsp_format_code_actions(const char *result_json, struct jc_sb *out,
                               int *count)
{
    cJSON *root;
    cJSON *e;
    int n = 0;

    if (count != NULL) {
        *count = 0;
    }
    if (result_json == NULL) {
        return 0;
    }
    root = jc_json_parse(result_json);
    if (root == NULL) {
        return 0;
    }
    if (cJSON_IsArray(root)) {
        cJSON_ArrayForEach(e, root) {
            const char *title;
            const char *kind;
            if (!cJSON_IsObject(e)) {
                continue;
            }
            title = jc_json_get_str(e, "title", NULL);
            if (title == NULL || title[0] == '\0') {
                continue;
            }
            kind = jc_json_get_str(e, "kind", NULL);
            if (kind != NULL && kind[0] != '\0') {
                jc_sb_append_fmt(out, "%s  [%s]\n", title, kind);
            } else {
                jc_sb_append_fmt(out, "%s\n", title);
            }
            n++;
        }
    }
    if (count != NULL) {
        *count = n;
    }
    cJSON_Delete(root);
    return 1;
}

int jc_lsp_action_command(const char *action_json, char **cmd_out,
                          char **args_json_out)
{
    cJSON *root;
    cJSON *cmd_field;
    const char *cmd_str = NULL;
    cJSON *args = NULL;
    int ok = 0;

    if (cmd_out != NULL) {
        *cmd_out = NULL;
    }
    if (args_json_out != NULL) {
        *args_json_out = NULL;
    }
    if (action_json == NULL || cmd_out == NULL) {
        return 0;
    }
    root = jc_json_parse(action_json);
    if (root == NULL || !cJSON_IsObject(root)) {
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return 0;
    }
    cmd_field = cJSON_GetObjectItem(root, "command");
    if (cJSON_IsString(cmd_field)) {
        /* The node is itself a Command: {title?, command:string, arguments?}. */
        cmd_str = cmd_field->valuestring;
        args = cJSON_GetObjectItem(root, "arguments");
    } else if (cJSON_IsObject(cmd_field)) {
        /* A CodeAction nesting a Command object under `command`. */
        cmd_str = jc_json_get_str(cmd_field, "command", NULL);
        args = cJSON_GetObjectItem(cmd_field, "arguments");
    }
    if (cmd_str != NULL && cmd_str[0] != '\0') {
        *cmd_out = jc_strdup(cmd_str);
        if (args_json_out != NULL && cJSON_IsArray(args)) {
            *args_json_out = jc_json_print(args);
        }
        ok = 1;
    }
    cJSON_Delete(root);
    return ok;
}

int jc_lsp_first_symbol_location(const char *result_json, const char *want_name,
                                 char *uri_buf, jc_size uri_cap, long *line,
                                 long *character)
{
    cJSON *root;
    cJSON *e;
    int found = 0;

    if (result_json == NULL || want_name == NULL) {
        return 0;
    }
    root = jc_json_parse(result_json);
    if (root == NULL || !cJSON_IsArray(root)) {
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return 0;
    }
    cJSON_ArrayForEach(e, root) {
        const char *name = jc_json_get_str(e, "name", NULL);
        cJSON *loc = jc_json_get_obj(e, "location");
        if (name == NULL || loc == NULL || strcmp(name, want_name) != 0) {
            continue;
        }
        {
            const char *uri = jc_json_get_str(loc, "uri", "");
            cJSON *range = jc_json_get_obj(loc, "range");
            cJSON *start = (range != NULL) ? jc_json_get_obj(range, "start")
                                           : NULL;
            jc_snprintf(uri_buf, uri_cap, "%s", uri);
            if (line != NULL) {
                *line = (start != NULL)
                        ? (long)jc_json_get_num(start, "line", 0.0) : 0;
            }
            if (character != NULL) {
                *character = (start != NULL)
                        ? (long)jc_json_get_num(start, "character", 0.0) : 0;
            }
            found = 1;
        }
        break;
    }
    cJSON_Delete(root);
    return found;
}

static int is_ident_char(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

int jc_lsp_locate_symbol(const char *text, const char *symbol, long *line,
                         long *character)
{
    long ln = 0;
    long col = 0;
    jc_size slen;
    const char *p;

    if (text == NULL || symbol == NULL || symbol[0] == '\0') {
        return 0;
    }
    slen = strlen(symbol);
    for (p = text; *p != '\0'; p++) {
        if (*p == '\n') {
            ln++;
            col = 0;
            continue;
        }
        if (strncmp(p, symbol, slen) == 0) {
            int before_ok = (p == text) ||
                            !is_ident_char((unsigned char)p[-1]);
            int after_ok = !is_ident_char((unsigned char)p[slen]);
            if (before_ok && after_ok) {
                if (line != NULL) {
                    *line = ln;
                }
                if (character != NULL) {
                    *character = col;
                }
                return 1;
            }
        }
        col++;
    }
    return 0;
}

/* ----- TextEdit application (M40) -------------------------------------- */

struct te_edit {
    jc_size start;
    jc_size end;
    const char *new_text; /* points into the parsed cJSON (alive during apply) */
};

/* Byte offset of (line,col) in `text`. `ls` holds the byte offset of each
 * line's start (nlines entries). col is clamped to the line so a position can't
 * bleed past its own line; the whole thing is clamped to `len`. */
static jc_size te_offset(jc_size len, const jc_size *ls, jc_size nlines,
                         long line, long col)
{
    jc_size base, line_end, off;
    if (line < 0) {
        line = 0;
    }
    if ((jc_size)line >= nlines) {
        return len;
    }
    base = ls[line];
    line_end = ((jc_size)line + 1 < nlines) ? ls[line + 1] : len;
    off = base + (jc_size)(col < 0 ? 0 : col);
    if (off > line_end) {
        off = line_end;
    }
    if (off > len) {
        off = len;
    }
    return off;
}

int jc_lsp_apply_text_edits(const char *text, const char *edits_json,
                            struct jc_sb *out)
{
    cJSON *root;
    jc_size len, nlines, i, k, ne, pos;
    jc_size *ls;
    struct te_edit *eds;
    int n = 0;
    int applied = 0;

    if (text == NULL || edits_json == NULL) {
        return -1;
    }
    root = jc_json_parse(edits_json);
    if (root == NULL) {
        return -1;
    }
    /* An LSP formatting/edit result is `TextEdit[] | null`; a JSON null (or a
     * server that stringifies "no edits" that way -- e.g. zls formatting an
     * already-formatted file) means zero edits, NOT malformed input. Emit the
     * original text unchanged and report 0 edits so the caller says "already
     * formatted", not "malformed formatting edits". */
    if (cJSON_IsNull(root)) {
        cJSON_Delete(root);
        jc_sb_append_n(out, text, (jc_size)strlen(text));
        return 0;
    }
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return -1;
    }
    len = (jc_size)strlen(text);

    /* Line-start table. */
    nlines = 1;
    for (i = 0; i < len; i++) {
        if (text[i] == '\n') {
            nlines++;
        }
    }
    ls = (jc_size *)malloc(nlines * sizeof(jc_size));
    eds = (struct te_edit *)malloc((jc_size)(cJSON_GetArraySize(root) + 1) *
                                   sizeof(struct te_edit));
    if (ls == NULL || eds == NULL) {
        free(ls);
        free(eds);
        cJSON_Delete(root);
        return -1;
    }
    k = 0;
    ls[k++] = 0;
    for (i = 0; i < len && k < nlines; i++) {
        if (text[i] == '\n') {
            ls[k++] = i + 1;
        }
    }

    /* Parse edits into byte ranges. */
    {
        cJSON *e;
        cJSON_ArrayForEach(e, root) {
            cJSON *range = jc_json_get_obj(e, "range");
            cJSON *st = (range != NULL) ? jc_json_get_obj(range, "start") : NULL;
            cJSON *en = (range != NULL) ? jc_json_get_obj(range, "end") : NULL;
            const char *nt = jc_json_get_str(e, "newText", NULL);
            jc_size s, ee;
            if (st == NULL || en == NULL) {
                continue;
            }
            s = te_offset(len, ls, nlines,
                          (long)jc_json_get_num(st, "line", 0.0),
                          (long)jc_json_get_num(st, "character", 0.0));
            ee = te_offset(len, ls, nlines,
                           (long)jc_json_get_num(en, "line", 0.0),
                           (long)jc_json_get_num(en, "character", 0.0));
            if (ee < s) {
                continue;
            }
            eds[n].start = s;
            eds[n].end = ee;
            eds[n].new_text = (nt != NULL) ? nt : "";
            n++;
        }
    }

    /* Sort ascending by start (insertion sort; n is small). */
    for (i = 1; i < (jc_size)n; i++) {
        struct te_edit tmp = eds[i];
        jc_size j = i;
        while (j > 0 && eds[j - 1].start > tmp.start) {
            eds[j] = eds[j - 1];
            j--;
        }
        eds[j] = tmp;
    }

    /* Single pass: splice, skipping any overlapping edit. */
    pos = 0;
    ne = (jc_size)n;
    for (i = 0; i < ne; i++) {
        if (eds[i].start < pos) {
            continue; /* overlaps a prior edit; skip */
        }
        jc_sb_append_n(out, text + pos, eds[i].start - pos);
        jc_sb_append(out, eds[i].new_text);
        pos = eds[i].end;
        applied++;
    }
    jc_sb_append_n(out, text + pos, len - pos);

    free(ls);
    free(eds);
    cJSON_Delete(root);
    return applied;
}
