/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_refs.c - @-references (see jc_refs.h). */

#include "jc_refs.h"
#include "jc_platform.h"
#include "jc_app.h"
#include "jc_pdf.h"
#include "jc_tool.h"
#include "jc_json.h"
#include "jc_str.h"
#include "jc_snprintf.h"
#include "jc_log.h"
#include "jc_image.h"
#include "jc_lsp.h"
#include "jc_repomap.h"
#include "jc_proc.h"
#include "jc_http.h"
#include "jc_rss.h"
#include "jc_untrusted.h"
#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

#define REF_BLOCK_MAX (32 * 1024)
#define REF_TOTAL_MAX (96 * 1024)

static int rsp(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static void push_ref(struct jc_vec *out, int kind, const char *s, jc_size n)
{
    struct jc_ref r;
    char *arg = (char *)malloc(n + 1);
    if (arg == NULL) return;
    if (n > 0) memcpy(arg, s, n);
    arg[n] = '\0';
    r.kind = kind;
    r.arg = arg;
    jc_vec_push(out, &r);
}

int jc_refs_scan(const char *text, struct jc_vec *out)
{
    jc_size i = 0;
    int count = 0;

    if (text == NULL) return 0;
    while (text[i] != '\0') {
        if (text[i] == '@' && (i == 0 || rsp((unsigned char)text[i - 1]))) {
            jc_size s = i + 1;
            jc_size e = s;
            while (text[e] != '\0' && !rsp((unsigned char)text[e])) e++;
            if (e > s) {
                jc_size n = e - s;
                if (n == 4 && strncmp(text + s, "diff", 4) == 0) {
                    push_ref(out, JC_REF_DIFF, "", 0);
                    count++;
                } else if (n == 8 && strncmp(text + s, "problems", 8) == 0) {
                    push_ref(out, JC_REF_PROBLEMS, "", 0);
                    count++;
                } else if (n >= 7 && strncmp(text + s, "folder:", 7) == 0) {
                    /* @folder:<dir> -> a bounded tree + outlines; trim trailing
                     * sentence punctuation like the file case. A bare "@folder:"
                     * with no dir is ignored (not a file). */
                    jc_size fl = n - 7;
                    while (fl > 0) {
                        char c = text[s + 7 + fl - 1];
                        if (c == '.' || c == ',' || c == ';' || c == ':' ||
                            c == '!' || c == '?' || c == ')') fl--;
                        else break;
                    }
                    if (fl > 0) {
                        push_ref(out, JC_REF_FOLDER, text + s + 7, fl);
                        count++;
                    }
                } else if (n > 4 && strncmp(text + s, "url:", 4) == 0) {
                    push_ref(out, JC_REF_URL, text + s + 4, n - 4);
                    count++;
                } else if (n >= 4 && strncmp(text + s, "rss:", 4) == 0) {
                    /* @rss:<url> -> a fetched RSS/Atom feed reduced to text (W4);
                     * a bare "@rss:" with no url is ignored. */
                    if (n > 4) {
                        push_ref(out, JC_REF_RSS, text + s + 4, n - 4);
                        count++;
                    }
                } else if (n >= 4 && strncmp(text + s, "mcp:", 4) == 0) {
                    /* @mcp:<uri> -> an MCP resource's text (M47); a bare
                     * "@mcp:" with no uri is ignored (consumed, not a file). */
                    if (n > 4) {
                        push_ref(out, JC_REF_MCP, text + s + 4, n - 4);
                        count++;
                    }
                } else if (n > 4 && strncmp(text + s, "img:", 4) == 0) {
                    /* Explicit image attachment (forces any extension). */
                    push_ref(out, JC_REF_IMAGE, text + s + 4, n - 4);
                    count++;
                } else if (n >= 6 && strncmp(text + s, "audio:", 6) == 0) {
                    /* Explicit audio transcription (forces any extension); a
                     * bare "@audio:" with no path is ignored (not a file). */
                    if (n > 6) {
                        push_ref(out, JC_REF_AUDIO, text + s + 6, n - 6);
                        count++;
                    }
                } else if (n >= 5 && strncmp(text + s, "docs:", 5) == 0) {
                    /* @docs:<name> -> the named external doc source; a bare
                     * "@docs:" with no name is ignored. The query is the whole
                     * message (resolved at expand time). */
                    if (n > 5) {
                        push_ref(out, JC_REF_DOCS, text + s + 5, n - 5);
                        count++;
                    }
                } else if (n >= 4 && strncmp(text + s, "ref:", 4) == 0) {
                    /* @ref:<name> -> a config-defined alias (#6); a bare
                     * "@ref:" with no name is ignored. */
                    if (n > 4) {
                        push_ref(out, JC_REF_ALIAS, text + s + 4, n - 4);
                        count++;
                    }
                } else if (n >= 4 && strncmp(text + s, "sym:", 4) == 0) {
                    /* @sym:Name -> a symbol's definition; trim trailing
                     * sentence punctuation like the file case. A bare "@sym:"
                     * with no name is ignored. */
                    jc_size sm = n - 4;
                    while (sm > 0) {
                        char c = text[s + 4 + sm - 1];
                        if (c == '.' || c == ',' || c == ';' || c == ':' ||
                            c == '!' || c == '?' || c == ')') sm--;
                        else break;
                    }
                    if (sm > 0) {
                        push_ref(out, JC_REF_SYM, text + s + 4, sm);
                        count++;
                    }
                } else {
                    /* file candidate: trim trailing sentence punctuation */
                    jc_size m = n;
                    while (m > 0) {
                        char c = text[s + m - 1];
                        if (c == '.' || c == ',' || c == ';' || c == ':' ||
                            c == '!' || c == '?' || c == ')') m--;
                        else break;
                    }
                    if (m > 0) {
                        /* An image extension => attach as an image, not inline
                         * text. push_ref copies, so a temporary NUL-terminated
                         * path for the media-type sniff is built on the stack. */
                        char probe[260];
                        int is_img = 0;
                        if (m < sizeof(probe)) {
                            memcpy(probe, text + s, m);
                            probe[m] = '\0';
                            is_img = (jc_image_media_type(probe) != NULL);
                        }
                        push_ref(out, is_img ? JC_REF_IMAGE : JC_REF_FILE,
                                 text + s, m);
                        count++;
                    }
                }
            }
            i = e;
        } else {
            i++;
        }
    }
    return count;
}

void jc_refs_free(struct jc_vec *refs)
{
    jc_size i;
    for (i = 0; i < refs->len; i++) {
        free(((struct jc_ref *)jc_vec_at(refs, i))->arg);
    }
    jc_vec_free(refs);
}

/* Append a fenced "@label:\n```\n<content>\n```" block, content capped. */
static void append_block(struct jc_sb *sb, const char *label,
                         const char *content, jc_size clen)
{
    jc_sb_append(sb, "\n@");
    jc_sb_append(sb, label);
    jc_sb_append(sb, ":\n```\n");
    if (clen > REF_BLOCK_MAX) {
        jc_sb_append_n(sb, content, REF_BLOCK_MAX);
        jc_sb_append(sb, "\n... [reference truncated]");
    } else {
        jc_sb_append_n(sb, content, clen);
    }
    jc_sb_append(sb, "\n```\n");
}

/* Run a read-only tool by name (if present) and append its output as a block. */
static int resolve_tool(struct jc_app *app, struct jc_sb *sb,
                        const char *label, const char *tool,
                        const char *args_json)
{
    struct jc_tool_result res;
    if (app->tools == NULL || jc_tool_registry_find(app->tools, tool) == NULL) {
        return 0;
    }
    res.content = NULL;
    res.is_error = 0;
    jc_tool_execute(app->tools, tool, args_json, &res, app);
    if (res.content != NULL) {
        append_block(sb, label, res.content, (jc_size)strlen(res.content));
    }
    free(res.content);
    return 1;
}

/* Cap a fetched feed before reducing it to text. */
#define REF_RSS_FETCH_MAX (2 * 1024 * 1024)

/* Fetch an RSS/Atom feed URL and append its reduced-to-text digest as a block.
 * Direct fetch (not the fetch_url tool, which returns raw HTML/XML) so the model
 * sees clean text. Appends a note on failure. Always appends a block. */
static void resolve_rss(struct jc_app *app, struct jc_sb *sb, const char *url)
{
    struct jc_http_request req;
    long status = 0;
    char *body = NULL;
    jc_size len = 0;
    jc_status st;
    char label[200];

    jc_snprintf(label, sizeof(label), "rss:%s", url);
    memset(&req, 0, sizeof(req));
    req.method = "GET";
    req.url = url;
    req.timeout_secs = 30;
    req.abort_flag = &app->abort_flag;
    /* A feed is a CONTENT fetch with no credential attached, and feed URLs
     * redirect as a matter of course. One of the three callers that should
     * follow (M472); without it the `status >= 400` check below would let an
     * unfollowed 302 through as an empty feed. */
    req.follow_redirects = 1;
    st = jc_http_perform(&req, &status, &body, &len);
    if (st != JC_OK || status >= 400 || body == NULL) {
        char note[256];
        jc_snprintf(note, sizeof(note), "(could not fetch feed: %s)",
                    st != JC_OK ? jc_status_str(st) : "HTTP error");
        append_block(sb, label, note, (jc_size)strlen(note));
        free(body);
        return;
    }
    if (len > REF_RSS_FETCH_MAX) {
        body[REF_RSS_FETCH_MAX] = '\0';
    }
    {
        struct jc_sb text;
        struct jc_sb fenced;
        jc_sb_init(&text);
        jc_rss_to_text(body, &text);
        /* M300: a feed is external content reached by a URL, so it is fenced like
         * a fetched page. This path does its OWN fetch (not the fetch_url tool),
         * which is exactly why it needed its own call -- @url: inherits the
         * tool's fence, @rss: would not have. */
        jc_sb_init(&fenced);
        jc_untrusted_wrap("RSS feed", label,
                          text.len > 0 ? text.data : "(no feed items found)",
                          &fenced);
        append_block(sb, label, fenced.data != NULL ? fenced.data : "",
                     fenced.len);
        jc_sb_free(&fenced);
        jc_sb_free(&text);
    }
    free(body);
}

/* Most files to run diagnostics over for @problems (each spawns/queries the LSP
 * with a timeout, so bound the work). */
#define REF_PROBLEMS_MAX_FILES 16

/* Append current LSP diagnostics for the files touched this session as a
 * @problems block. Always appends a block (a note when there's nothing). */
static void resolve_problems(struct jc_app *app, struct jc_sb *sb)
{
    struct jc_sb body;
    jc_size i;
    jc_size k;
    int checked = 0;
    int any = 0;

    if (app->lsp == NULL) {
        const char *note = "(no language server configured; set \"lspServers\")";
        append_block(sb, "problems", note, (jc_size)strlen(note));
        return;
    }

    jc_sb_init(&body);
    for (i = 0; i < app->read_files.len &&
                checked < REF_PROBLEMS_MAX_FILES; i++) {
        const char *path = *(char **)jc_vec_at(&app->read_files, i);
        char *diag;
        int count = 0;
        int dup = 0;

        for (k = 0; k < i; k++) {
            if (strcmp(path, *(char **)jc_vec_at(&app->read_files, k)) == 0) {
                dup = 1;
                break;
            }
        }
        if (dup || !jc_lsp_handles(app->lsp, path)) {
            continue;
        }
        checked++;
        diag = jc_lsp_diagnostics(app->lsp, path, &count);
        if (diag != NULL) {
            if (count > 0) {
                jc_sb_append_fmt(&body, "%s:\n%s\n", path, diag);
                any = 1;
            }
            free(diag);
        }
        if (body.len > REF_BLOCK_MAX) {
            break;
        }
    }
    if (!any) {
        jc_sb_append(&body,
                     "(no diagnostics in the files touched this session)");
    }
    append_block(sb, "problems", body.data != NULL ? body.data : "",
                 body.data != NULL ? (jc_size)strlen(body.data) : 0);
    jc_sb_free(&body);
}

/* Resolve a secret alias (key/token) for PRESENCE only: run its env/cmd, strip
 * trailing whitespace, register the value with jc_redact so it is scrubbed from
 * logs, and emit a redacted note -- NEVER the value. Returns the arena-owned
 * value (for redaction registration) or NULL if unset. */
static const char *alias_secret_value(struct jc_app *app,
                                      const struct jc_alias_cfg *al,
                                      struct jc_arena *a)
{
    const char *raw = NULL;
    char *val;
    jc_size n;
    if (al->is_cmd) {
        struct jc_sb out;
        char *argv[4];
        jc_sb_init(&out);
        argv[0] = (char *)jc_shell_path(); argv[1] = "-c";
        argv[2] = al->value; argv[3] = 0;
        if (jc_proc_capture(argv, NULL, NULL, &out, 65536, 15,
                            &app->abort_flag) == 0 && out.data != NULL) {
            raw = jc_arena_strdup(a, out.data);
        }
        jc_sb_free(&out);
    } else {
        const char *e = getenv(al->value);
        if (e != NULL && e[0] != '\0') {
            raw = jc_arena_strdup(a, e);
        }
    }
    if (raw == NULL) {
        return NULL;
    }
    val = (char *)raw;
    n = (jc_size)strlen(val);
    while (n > 0 && (val[n - 1] == '\n' || val[n - 1] == '\r' ||
                     val[n - 1] == ' ' || val[n - 1] == '\t')) {
        val[--n] = '\0';
    }
    return (val[0] != '\0') ? val : NULL;
}

/* Resolve an @ref:<name> alias into `sb`. Returns 1 if it produced a block. */
static int resolve_alias(struct jc_app *app, struct jc_sb *sb,
                         const struct jc_alias_cfg *al, struct jc_arena *a)
{
    char label[192];
    if (al == NULL) {
        return 0;
    }
    jc_snprintf(label, sizeof label, "ref:%s", al->name);
    if (al->is_secret) {
        const char *v = alias_secret_value(app, al, a);
        if (v != NULL) {
            const char *note = "[secret alias resolved -- present, redacted]";
            jc_redact_register(v); /* scrub from logs; never inline the value */
            append_block(sb, label, note, (jc_size)strlen(note));
        } else {
            const char *note = "[secret alias -- NOT set]";
            append_block(sb, label, note, (jc_size)strlen(note));
        }
        return 1;
    }
    if (strcmp(al->type, "file") == 0) {
        char full[1100];
        char *data = NULL;
        jc_size len = 0;
        if (al->value[0] == '/') {
            jc_snprintf(full, sizeof full, "%s", al->value);
        } else {
            jc_snprintf(full, sizeof full, "%s/%s", app->cwd, al->value);
        }
        if (jc_file_exists(full) &&
            jc_read_file(full, &data, &len, a) == JC_OK) {
            append_block(sb, label, data, len);
            return 1;
        }
        return 0;
    }
    if (strcmp(al->type, "dir") == 0) {
        char *map = jc_repomap_build_dir(app, al->value);
        if (map != NULL) {
            append_block(sb, label, map, (jc_size)strlen(map));
            return 1;
        }
        return 0;
    }
    if (strcmp(al->type, "url") == 0) {
        cJSON *o = cJSON_CreateObject();
        char *aj;
        int did = 0;
        cJSON_AddStringToObject(o, "url", al->value);
        aj = jc_json_print(o);
        cJSON_Delete(o);
        if (aj != NULL) {
            did = resolve_tool(app, sb, label, "fetch_url", aj);
            free(aj);
        }
        return did;
    }
    /* ssh / text / other: inject the target string itself as context. */
    {
        struct jc_sb b;
        jc_sb_init(&b);
        jc_sb_append(&b, al->type);
        jc_sb_append(&b, ": ");
        jc_sb_append(&b, al->value);
        append_block(sb, label, b.data != NULL ? b.data : al->value,
                     b.data != NULL ? b.len : (jc_size)strlen(al->value));
        jc_sb_free(&b);
        return 1;
    }
}

jc_status jc_refs_expand(struct jc_app *app, const char *raw,
                         struct jc_arena *a, char **out)
{
    struct jc_vec refs;
    struct jc_sb sb;
    jc_size i;
    int resolved = 0;
    int header = 0;

    jc_vec_init(&refs, sizeof(struct jc_ref));
    if (jc_refs_scan(raw, &refs) == 0) {
        jc_refs_free(&refs);
        *out = jc_arena_strdup(a, raw != NULL ? raw : "");
        return JC_OK;
    }

    jc_sb_init(&sb);
    jc_sb_append(&sb, raw != NULL ? raw : "");

    for (i = 0; i < refs.len; i++) {
        struct jc_ref *r = (struct jc_ref *)jc_vec_at(&refs, i);
        int did = 0;
        if (sb.len > REF_TOTAL_MAX) {
            jc_sb_append(&sb, "\n... [more references omitted]\n");
            break;
        }
        if (!header) {
            jc_sb_append(&sb, "\n\n--- referenced context ---\n");
            header = 1; /* tentatively; removed below if nothing resolves */
        }
        if (r->kind == JC_REF_FILE) {
            char full[1100];
            char *data = NULL;
            jc_size len = 0;
            if (r->arg[0] == '/') {
                jc_snprintf(full, sizeof(full), "%s", r->arg);
            } else {
                jc_snprintf(full, sizeof(full), "%s/%s", app->cwd, r->arg);
            }
            if (jc_pdf_is_pdf(full)) {
                /* PDF: inline its extracted text (M42), not raw binary. */
                struct jc_sb px;
                jc_sb_init(&px);
                if (jc_file_exists(full) &&
                    jc_pdf_extract(full, app->config.pdf_command,
                                   (jc_size)(256 * 1024), &px,
                                   &app->abort_flag) == JC_OK) {
                    append_block(&sb, r->arg, px.data != NULL ? px.data : "",
                                 px.len);
                    did = 1;
                }
                jc_sb_free(&px);
            } else if (jc_file_exists(full) &&
                jc_read_file(full, &data, &len, a) == JC_OK) {
                append_block(&sb, r->arg, data, len);
                did = 1;
            }
        } else if (r->kind == JC_REF_DIFF) {
            did = resolve_tool(app, &sb, "diff", "git_diff", "{}");
            if (!did) {
                append_block(&sb, "diff", "(git diff unavailable here)", 27);
                did = 1;
            }
        } else if (r->kind == JC_REF_PROBLEMS) {
            resolve_problems(app, &sb);
            did = 1;
        } else if (r->kind == JC_REF_FOLDER) {
            char label[160];
            char *map = jc_repomap_build_dir(app, r->arg);
            const char *content = (map != NULL)
                ? map : "(no recognised source files in this directory)";
            jc_snprintf(label, sizeof(label), "folder:%s", r->arg);
            append_block(&sb, label, content, (jc_size)strlen(content));
            did = 1;
        } else if (r->kind == JC_REF_URL) {
            cJSON *o = cJSON_CreateObject();
            char *aj;
            cJSON_AddStringToObject(o, "url", r->arg);
            aj = jc_json_print(o);
            cJSON_Delete(o);
            if (aj != NULL) {
                did = resolve_tool(app, &sb, r->arg, "fetch_url", aj);
                free(aj);
            }
        } else if (r->kind == JC_REF_RSS) {
            resolve_rss(app, &sb, r->arg);
            did = 1;
        } else if (r->kind == JC_REF_AUDIO) {
            /* Transcribe the audio file via the transcribe_audio tool (present
             * only when a "transcribe"-role model is configured; otherwise this
             * ref silently doesn't resolve, like @url/@sym without their tool). */
            char label[160];
            cJSON *o = cJSON_CreateObject();
            char *aj;
            jc_snprintf(label, sizeof(label), "audio:%s", r->arg);
            cJSON_AddStringToObject(o, "path", r->arg);
            aj = jc_json_print(o);
            cJSON_Delete(o);
            if (aj != NULL) {
                did = resolve_tool(app, &sb, label, "transcribe_audio", aj);
                free(aj);
            }
        } else if (r->kind == JC_REF_DOCS) {
            /* Retrieve the most relevant passages of the named doc source for
             * this message via search_docs (present only when a docs source +
             * embed model are configured; otherwise this ref silently doesn't
             * resolve). The query is the whole user message. */
            char label[160];
            cJSON *o = cJSON_CreateObject();
            char *aj;
            jc_snprintf(label, sizeof(label), "docs:%s", r->arg);
            cJSON_AddStringToObject(o, "name", r->arg);
            cJSON_AddStringToObject(o, "query", raw != NULL ? raw : "");
            aj = jc_json_print(o);
            cJSON_Delete(o);
            if (aj != NULL) {
                did = resolve_tool(app, &sb, label, "search_docs", aj);
                free(aj);
            }
        } else if (r->kind == JC_REF_MCP) {
            /* Inline an MCP resource's text via read_mcp_resource (present only
             * when a connected server advertises resources; otherwise this ref
             * silently doesn't resolve). Addressed by exact uri. */
            char label[160];
            cJSON *o = cJSON_CreateObject();
            char *aj;
            jc_snprintf(label, sizeof(label), "mcp:%s", r->arg);
            cJSON_AddStringToObject(o, "uri", r->arg);
            aj = jc_json_print(o);
            cJSON_Delete(o);
            if (aj != NULL) {
                did = resolve_tool(app, &sb, label, "read_mcp_resource", aj);
                free(aj);
            }
        } else if (r->kind == JC_REF_SYM) {
            char label[160];
            cJSON *o;
            char *aj;
            jc_snprintf(label, sizeof(label), "sym:%s", r->arg);
            /* Prefer the language server's definition; if no LSP nav tool is
             * registered, fall back to a code search for the name. */
            o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "symbol", r->arg);
            aj = jc_json_print(o);
            cJSON_Delete(o);
            if (aj != NULL) {
                did = resolve_tool(app, &sb, label, "find_definition", aj);
                free(aj);
            }
            if (!did) {
                o = cJSON_CreateObject();
                cJSON_AddStringToObject(o, "pattern", r->arg);
                aj = jc_json_print(o);
                cJSON_Delete(o);
                if (aj != NULL) {
                    did = resolve_tool(app, &sb, label, "search_code", aj);
                    free(aj);
                }
            }
        } else if (r->kind == JC_REF_ALIAS) {
            /* @ref:<name>: a config-defined alias (#6). Unknown names get a
             * short note so a typo is visible rather than silently dropped. */
            const struct jc_alias_cfg *al =
                jc_config_find_alias(&app->config, r->arg);
            if (al != NULL) {
                did = resolve_alias(app, &sb, al, a);
            } else {
                char label[192];
                const char *note = "(no such alias in config)";
                jc_snprintf(label, sizeof label, "ref:%s", r->arg);
                append_block(&sb, label, note, (jc_size)strlen(note));
                did = 1;
            }
        }
        if (did) resolved++;
    }

    jc_refs_free(&refs);
    if (resolved == 0) {
        /* Nothing actually resolved: return the original text unchanged. */
        jc_sb_free(&sb);
        *out = jc_arena_strdup(a, raw != NULL ? raw : "");
        return JC_OK;
    }
    *out = jc_arena_strdup(a, sb.data != NULL ? sb.data : "");
    jc_sb_free(&sb);
    return JC_OK;
}

int jc_refs_attach_images(struct jc_app *app, const char *raw,
                          struct jc_message *m)
{
    struct jc_vec refs;
    jc_size i;
    int attached = 0;

    jc_vec_init(&refs, sizeof(struct jc_ref));
    jc_refs_scan(raw, &refs);
    for (i = 0; i < refs.len; i++) {
        struct jc_ref *r = (struct jc_ref *)jc_vec_at(&refs, i);
        char full[1100];
        const char *path;
        if (r->kind != JC_REF_IMAGE) {
            continue;
        }
        if (r->arg[0] == '/') {
            path = r->arg;
        } else {
            jc_snprintf(full, sizeof(full), "%s/%s", app->cwd, r->arg);
            path = full;
        }
        if (jc_app_load_image(app, path, m) == JC_OK) {
            attached++;
        } else {
            /* Don't silently drop a referenced image: the prompt text still
             * mentions it, so warn rather than let the model assume it's
             * attached (e.g. fence-denied, oversized, or unreadable). */
            jc_logf(JC_LOG_WARN, "could not attach image '%s' (skipped)",
                    r->arg);
        }
    }
    jc_refs_free(&refs);
    return attached;
}
