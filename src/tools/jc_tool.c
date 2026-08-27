/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool.c - tool registry, schema emission, execution (see jc_tool.h). */

#include "jc_tool.h"
#include "jc_app.h"
#include "jc_config.h"
#include "jc_skill.h"
#include "jc_sound.h"
#include "jc_perm.h"
#include "jc_json.h"
#include "jc_jsonrepair.h"
#include "jc_compact.h"
#include "jc_eventlog.h"
#include "jc_log.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

void jc_tool_registry_init(struct jc_tool_registry *r)
{
    jc_vec_init(&r->tools, sizeof(const struct jc_tool *));
}

void jc_tool_registry_free(struct jc_tool_registry *r)
{
    jc_vec_free(&r->tools);
}

void jc_tool_registry_register(struct jc_tool_registry *r,
                               const struct jc_tool *t)
{
    jc_vec_push(&r->tools, &t);
}

int jc_tool_result_is_malfunction(const struct jc_tool_result *r)
{
    if (r == NULL || !r->is_error) {
        return 0;
    }
    /* M291: a policy refusal is not a malfunction. The tool did its job; the
     * request was out of bounds. Escalating to a stronger model would meet the
     * same fence, so it is pure cost -- which is exactly what the first route
     * event on a newly fenced project turned out to be. */
    if (r->policy_refusal) {
        return 0;
    }
    /* A non-negative exit_status means a command ran and reported it, so the
     * tool itself worked. Every other error path leaves the -1 that
     * jc_tool_execute initialises (a tool that runs nothing can only fail by
     * malfunctioning), including the "failed to start command" path -- which is
     * a real malfunction and correctly keeps -1. */
    return r->exit_status < 0;
}

const char *jc_tool_canonical_name(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    if (strcmp(name, "todoadd") == 0 || strcmp(name, "todo_add") == 0 ||
        strcmp(name, "todo_write") == 0 || strcmp(name, "add_todo") == 0 ||
        strcmp(name, "todoedit") == 0 || strcmp(name, "todo_edit") == 0) {
        return "todowrite";
    }
    if (strcmp(name, "todo_read") == 0 || strcmp(name, "read_todo") == 0) {
        return "todoread";
    }
    /* M219: transparent aliases are only ever added when the ARGUMENT SCHEMA
     * is compatible -- write_file takes path+content and run_terminal_command
     * takes command, which is exactly what a model guessing these names
     * sends. grep/cat-style guesses stay hint-only in
     * jc_tool_semantic_alias (their schemas differ; a resolved call would
     * fail argument validation and teach nothing). A telemetry pass over a
     * long unattended workload priced each unknown-name miss at one full
     * failed round-trip (~100k input tokens on that history).
     *
     * M324: `glob` GRADUATED from hint-only to a transparent alias, because the
     * schema objection was fixed rather than argued with. list_files gained an
     * optional `pattern`, so a glob call carrying a `pattern` key now maps onto
     * it unchanged -- same key, same meaning. The measurement that forced it: 46
     * glob calls, never once successful, in one 13,783-call workload, beside
     * 7,761 run_terminal_command calls (56% of every tool call). The model
     * wanted pattern-based file finding badly enough to keep inventing the tool
     * and then to shell out; the honest fix was to offer it. */
    if (strcmp(name, "glob") == 0 || strcmp(name, "fd") == 0 ||
        strcmp(name, "find_files") == 0) {
        return "list_files";
    }
    if (strcmp(name, "create_file") == 0 || strcmp(name, "new_file") == 0) {
        return "write_file";
    }
    if (strcmp(name, "run_shell_command") == 0 ||
        strcmp(name, "shell_command") == 0) {
        return "run_terminal_command";
    }
    return name;
}

const struct jc_tool *jc_tool_registry_find(const struct jc_tool_registry *r,
                                            const char *name)
{
    jc_size i;
    const char *canon;

    if (name == NULL) {
        return NULL;
    }
    /* Exact match wins; otherwise an alias (e.g. todoadd -> todowrite) so a
     * model's plausible guess resolves instead of failing as unknown. */
    canon = jc_tool_canonical_name(name);
    for (i = 0; i < r->tools.len; i++) {
        const struct jc_tool *t =
            *(const struct jc_tool **)jc_vec_at((struct jc_vec *)&r->tools, i);
        if (strcmp(t->name, name) == 0 || strcmp(t->name, canon) == 0) {
            return t;
        }
    }
    return NULL;
}

const char *jc_tool_semantic_alias(const char *name)
{
    if (name == NULL) {
        return NULL;
    }
    if (strcmp(name, "grep") == 0 || strcmp(name, "rg") == 0 ||
        strcmp(name, "ripgrep") == 0 || strcmp(name, "ack") == 0 ||
        strcmp(name, "search_files") == 0 || strcmp(name, "search") == 0) {
        return "search_code";
    }
    /* M324: glob/fd/find_files moved to jc_tool_canonical_name -- list_files now
     * takes a `pattern`, so those calls RESOLVE instead of being hinted at.
     * `find`, `ls` and `dir` stay hints: they are shell commands whose arguments
     * are flags rather than a pattern, so a transparent alias would hand
     * list_files something it cannot read. */
    if (strcmp(name, "find") == 0 ||
        strcmp(name, "ls") == 0 || strcmp(name, "dir") == 0) {
        return "list_files";
    }
    if (strcmp(name, "bash") == 0 || strcmp(name, "shell") == 0 ||
        strcmp(name, "sh") == 0 || strcmp(name, "run_command") == 0 ||
        strcmp(name, "execute_command") == 0 || strcmp(name, "exec") == 0 ||
        strcmp(name, "terminal") == 0) {
        return "run_terminal_command";
    }
    if (strcmp(name, "cat") == 0 || strcmp(name, "view_file") == 0 ||
        strcmp(name, "open_file") == 0 || strcmp(name, "view") == 0) {
        return "read_file";
    }
    if (strcmp(name, "web_fetch") == 0 || strcmp(name, "fetch") == 0 ||
        strcmp(name, "curl") == 0 || strcmp(name, "http_get") == 0) {
        return "fetch_url";
    }
    return NULL;
}

/* True if a tool with this exact name is registered. */
static int tool_is_registered(const struct jc_tool_registry *r, const char *name)
{
    jc_size i;
    for (i = 0; i < r->tools.len; i++) {
        const struct jc_tool *t =
            *(const struct jc_tool **)jc_vec_at((struct jc_vec *)&r->tools, i);
        if (strcmp(t->name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

const char *jc_tool_suggest_name(const struct jc_tool_registry *r,
                                 const char *unknown)
{
    jc_size i;
    const char *best = NULL;
    const char *sem;
    int best_d = 1000000;
    jc_size ulen;

    if (r == NULL || unknown == NULL || unknown[0] == '\0') {
        return NULL;
    }
    ulen = strlen(unknown);
    for (i = 0; i < r->tools.len; i++) {
        const struct jc_tool *t =
            *(const struct jc_tool **)jc_vec_at((struct jc_vec *)&r->tools, i);
        int d = jc_str_edit_distance(unknown, t->name);
        if (d >= 0 && d < best_d) {
            best_d = d;
            best = t->name;
        }
    }
    /* M345: the closeness rule ("~half the queried length, clamped to [2,4],
     * so a wild guess like `glob` yields no misleading suggestion") moved to
     * the shared jc_str_close_enough -- the human's slash-command suggester
     * uses the same predicate, so the two kindnesses cannot drift apart. */
    if (best != NULL && jc_str_close_enough(ulen, best_d)) {
        return best;
    }
    /* M91: no close typo -- try a semantic synonym (grep -> search_code, etc.),
     * but only suggest it when that tool is actually registered in this run. */
    sem = jc_tool_semantic_alias(unknown);
    if (sem != NULL && tool_is_registered(r, sem)) {
        return sem;
    }
    return NULL;
}

cJSON *jc_tool_unwrap_self_named(cJSON *args, const char *tool_name)
{
    cJSON *only;
    if (args == NULL || tool_name == NULL || tool_name[0] == '\0') {
        return NULL;
    }
    if (!cJSON_IsObject(args)) {
        return NULL;
    }
    only = args->child;
    if (only == NULL || only->next != NULL) {
        return NULL;                    /* must be the ONLY member */
    }
    if (only->string == NULL || strcmp(only->string, tool_name) != 0) {
        return NULL;                    /* must be named after the tool */
    }
    if (!cJSON_IsObject(only)) {
        return NULL;                    /* must wrap an argument object */
    }
    /* Return an independent copy so the caller can delete the outer object.
     * Serialise/re-parse is this tree's deep-copy idiom (see map_tools in the
     * providers); our cJSON API subset has no cJSON_Duplicate/Detach, and adding
     * one just for this would widen the surface for no gain. */
    {
        char *s = cJSON_PrintUnformatted(only);
        cJSON *copy = (s != NULL) ? cJSON_Parse(s) : NULL;
        free(s);
        return copy;
    }
}

/* M193: the declared type of `key` in a JSON-Schema `properties` block, or NULL.
 * Only the two nested types matter here; anything else returns NULL so the
 * caller leaves the value alone. */
static const char *declared_nested_type(const cJSON *schema, const char *key)
{
    const cJSON *props;
    const cJSON *p;
    const cJSON *ty;
    if (schema == NULL || key == NULL) {
        return NULL;
    }
    props = cJSON_GetObjectItem((cJSON *)schema, "properties");
    if (props == NULL || !cJSON_IsObject(props)) {
        return NULL;
    }
    p = cJSON_GetObjectItem((cJSON *)props, key);
    if (p == NULL || !cJSON_IsObject(p)) {
        return NULL;
    }
    ty = cJSON_GetObjectItem((cJSON *)p, "type");
    if (ty == NULL || !cJSON_IsString(ty) || ty->valuestring == NULL) {
        return NULL;
    }
    if (strcmp(ty->valuestring, "array") == 0 ||
        strcmp(ty->valuestring, "object") == 0) {
        return ty->valuestring;
    }
    return NULL;
}

int jc_tool_unstring_args(cJSON *args, const cJSON *schema)
{
    cJSON *m;
    int fixed = 0;

    if (args == NULL || !cJSON_IsObject(args) || schema == NULL) {
        return 0;
    }
    m = args->child;
    while (m != NULL) {
        cJSON *next = m->next;   /* replacement relinks, so capture first */
        const char *want;
        if (m->string == NULL || !cJSON_IsString(m) || m->valuestring == NULL) {
            m = next;
            continue;
        }
        want = declared_nested_type(schema, m->string);
        if (want == NULL) {
            m = next;            /* not declared array/object: leave it alone */
            continue;
        }
        {
            /* The value must parse AND be exactly the declared type. A string
             * that merely looks structured (a path, a prose sentence, a regex)
             * fails one of those and is left untouched. */
            cJSON *parsed = cJSON_Parse(m->valuestring);
            int ok = 0;
            if (parsed != NULL) {
                ok = (strcmp(want, "array") == 0) ? cJSON_IsArray(parsed)
                                                  : cJSON_IsObject(parsed);
            }
            if (!ok) {
                if (parsed != NULL) {
                    cJSON_Delete(parsed);
                }
                m = next;
                continue;
            }
            /* Replace frees the old string item and hands the key over. */
            if (cJSON_ReplaceItemInObject(args, m->string, parsed)) {
                fixed++;
            } else {
                cJSON_Delete(parsed);
            }
        }
        m = next;
    }
    return fixed;
}

/* ---- prose-tool-call scanner (M147) -------------------------------------- */

/* Extract the balanced JSON object starting at `p` (which must point at '{'),
 * string- and escape-aware. Returns the length of the object, or 0 if it
 * never closes within `max` bytes. */
static jc_size tc_object_len(const char *p, jc_size max)
{
    jc_size i;
    int depth = 0;
    int in_str = 0;
    int esc = 0;
    for (i = 0; i < max && p[i] != '\0'; i++) {
        char c = p[i];
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
        } else if (c == '{') {
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0) {
                return i + 1;
            }
        }
    }
    return 0;
}

/* Parse the object at `p` and, when it names a REGISTERED tool (key "name"
 * or "tool"; `need_args` additionally requires arguments/args/parameters),
 * copy the registry's resolved name and return 1. */
static int tc_try_object(const char *p, jc_size max, int need_args,
                         const struct jc_tool_registry *r,
                         char *name_out, jc_size cap)
{
    jc_size len = tc_object_len(p, max);
    char *slice;
    cJSON *o;
    const char *nm = NULL;
    const struct jc_tool *t = NULL;

    if (len == 0 || len > 65536) {
        return 0;
    }
    slice = (char *)malloc(len + 1);
    if (slice == NULL) {
        return 0;
    }
    memcpy(slice, p, len);
    slice[len] = '\0';
    o = cJSON_Parse(slice);
    free(slice);
    if (o == NULL) {
        return 0;
    }
    nm = jc_json_get_str(o, "name", NULL);
    if (nm == NULL) {
        nm = jc_json_get_str(o, "tool", NULL);
    }
    if (nm != NULL &&
        (!need_args || cJSON_GetObjectItem(o, "arguments") != NULL ||
         cJSON_GetObjectItem(o, "args") != NULL ||
         cJSON_GetObjectItem(o, "parameters") != NULL)) {
        t = jc_tool_registry_find(r, nm);
    }
    if (t != NULL) {
        jc_snprintf(name_out, cap, "%s", t->name);
    }
    cJSON_Delete(o);
    return t != NULL;
}

#define TC_SCAN_MAX 16384 /* scan window: a narrated call sits early anyway */

int jc_toolcall_scan(const char *text, const struct jc_tool_registry *r,
                     char *name_out, jc_size cap)
{
    const char *p;
    jc_size n;

    if (text == NULL || r == NULL || name_out == NULL || cap == 0) {
        return 0;
    }
    name_out[0] = '\0';
    n = strlen(text);
    if (n > TC_SCAN_MAX) {
        n = TC_SCAN_MAX;
    }

    /* Pattern 1: a fenced block containing a JSON object with a name key
     * (the fence language tag, if any, is skipped implicitly by looking for
     * the first '{' inside the fence). */
    for (p = strstr(text, "```"); p != NULL && (jc_size)(p - text) < n;
         p = strstr(p + 3, "```")) {
        const char *close = strstr(p + 3, "```");
        const char *b = p + 3;
        while (*b != '\0' && *b != '{' &&
               (close == NULL || b < close)) {
            b++;
        }
        if (*b == '{' && (close == NULL || b < close) &&
            tc_try_object(b, n - (jc_size)(b - text), 0, r, name_out, cap)) {
            return 1;
        }
        if (close == NULL) {
            break;
        }
        p = close; /* resume after this fence's close */
    }

    /* Pattern 2: a line-anchored bare JSON object with name + args-ish key. */
    for (p = text; p != NULL && (jc_size)(p - text) < n; ) {
        const char *ls = p;
        while (*ls == ' ' || *ls == '\t') {
            ls++;
        }
        if (*ls == '{' &&
            tc_try_object(ls, n - (jc_size)(ls - text), 1, r, name_out, cap)) {
            return 1;
        }
        p = strchr(p, '\n');
        if (p != NULL) {
            p++;
        }
    }

    /* Pattern 3: XML-ish tags. */
    {
        static const char *const tags[] = { "<tool_call>", "<function_call>",
                                            NULL };
        int i;
        for (i = 0; tags[i] != NULL; i++) {
            p = strstr(text, tags[i]);
            if (p != NULL && (jc_size)(p - text) < n) {
                const char *b = p + strlen(tags[i]);
                while (*b == ' ' || *b == '\n' || *b == '\r' || *b == '\t') {
                    b++;
                }
                if (*b == '{' &&
                    tc_try_object(b, n - (jc_size)(b - text), 0, r,
                                  name_out, cap)) {
                    return 1;
                }
            }
        }
        p = strstr(text, "<invoke name=\"");
        if (p != NULL && (jc_size)(p - text) < n) {
            char nm[64];
            const char *b = p + 14;
            jc_size k = 0;
            const struct jc_tool *t;
            while (b[k] != '\0' && b[k] != '"' && k + 1 < sizeof(nm)) {
                nm[k] = b[k];
                k++;
            }
            nm[k] = '\0';
            t = (b[k] == '"') ? jc_tool_registry_find(r, nm) : NULL;
            if (t != NULL) {
                jc_snprintf(name_out, cap, "%s", t->name);
                return 1;
            }
        }
    }
    return 0;
}

/* See jc_tool.h. Order mirrors main()'s registration so the advertised array is
 * byte-identical between a report and a real turn. */
void jc_tool_register_configured(struct jc_tool_registry *r, struct jc_app *app)
{
    if (r == NULL || app == NULL) {
        return;
    }
    /* Background process management (M26): always available -- the tools read
     * app->bg at call time and no-op gracefully when none is set. */
    jc_tool_registry_register(r, jc_tool_read_background());
    jc_tool_registry_register(r, jc_tool_kill_background());

    if (app->config.search.url != NULL && app->config.search.url[0] != '\0') {
        jc_tool_registry_register(r, jc_tool_web_search());
    }
    if (app->config.sound.play_command != NULL ||
        app->config.sound.play_shell != NULL) {
        jc_tool_registry_register(r, jc_tool_play_audio());
    }
    if (app->config.sound.record_command != NULL ||
        app->config.sound.record_shell != NULL) {
        jc_tool_registry_register(r, jc_tool_record_audio());
    }
    if (app->config.assignments) {
        jc_tool_registry_register(r, jc_tool_hint());
        jc_tool_registry_register(r, jc_tool_ask_for_help());
    }
    if (app->config.board) {
        jc_tool_registry_register(r, jc_tool_board());
    }
    if (app->config.docs.len > 0 &&
        jc_config_find_by_role(&app->config, JC_ROLE_EMBED) >= 0) {
        jc_tool_registry_register(r, jc_tool_search_docs());
    }
    if (jc_config_find_by_role(&app->config, JC_ROLE_IMAGE) >= 0) {
        jc_tool_registry_register(r, jc_tool_generate_image(app));
    }
    if (jc_config_find_by_role(&app->config, JC_ROLE_AUDIO) >= 0) {
        jc_tool_registry_register(r, jc_tool_generate_audio());
    }
    if (jc_config_find_by_role(&app->config, JC_ROLE_TRANSCRIBE) >= 0) {
        jc_tool_registry_register(r, jc_tool_transcribe_audio());
    }
    /* load_skill only when skills are loaded AND some exist -- a caller that has
     * not loaded them gets no entry, which is correct rather than optimistic. */
    if (jc_skill_count(&app->skills) > 0) {
        jc_tool_registry_register(r, jc_tool_skill());
    }
    if (app->config.lsp_servers.len > 0) {
        jc_tool_registry_register(r, jc_tool_find_definition());
        jc_tool_registry_register(r, jc_tool_find_references());
        jc_tool_registry_register(r, jc_tool_list_symbols());
        jc_tool_registry_register(r, jc_tool_rename_symbol());
        jc_tool_registry_register(r, jc_tool_list_code_actions());
        jc_tool_registry_register(r, jc_tool_apply_code_action());
    }
    /* format_file has TWO backends (M263), so it is advertised when EITHER can
     * serve: a language server, or a configured formatCommand for the languages
     * no LSP formats. Transcribing the LSP gate here instead of this one was a
     * real bug in the first draft of this function -- the exact drift that
     * factoring is supposed to remove, reintroduced by copying rather than
     * moving. */
    if (app->config.lsp_servers.len > 0 ||
        (app->config.format_command != NULL &&
         app->config.format_command[0] != '\0')) {
        jc_tool_registry_register(r, jc_tool_format_file());
    }
    /* The one gate that is a probe rather than a config read: one `git rev-parse`
     * in the workspace. Cheap, and without it a report in a git repo would miss
     * eight tools. */
    if (jc_tool_git_available(app->cwd)) {
        jc_tool_registry_register(r, jc_tool_git_status());
        jc_tool_registry_register(r, jc_tool_git_diff());
        jc_tool_registry_register(r, jc_tool_git_log());
        jc_tool_registry_register(r, jc_tool_git_blame());
        jc_tool_registry_register(r, jc_tool_git_add());
        jc_tool_registry_register(r, jc_tool_git_commit());
        jc_tool_registry_register(r, jc_tool_git_branch());
        jc_tool_registry_register(r, jc_tool_git_stash());
    }
}

void jc_tool_register_builtins(struct jc_tool_registry *r)
{
    jc_tool_registry_register(r, jc_tool_read());
    jc_tool_registry_register(r, jc_tool_ls());
    jc_tool_registry_register(r, jc_tool_search());
    jc_tool_registry_register(r, jc_tool_fetch());
    jc_tool_registry_register(r, jc_tool_codebase_search());
    jc_tool_registry_register(r, jc_tool_write());
    jc_tool_registry_register(r, jc_tool_edit());
    jc_tool_registry_register(r, jc_tool_apply_patch());
    jc_tool_registry_register(r, jc_tool_run());
    jc_tool_registry_register(r, jc_tool_run_tests());
    jc_tool_registry_register(r, jc_tool_subagent());
    jc_tool_registry_register(r, jc_tool_parallel());
    jc_tool_registry_register(r, jc_tool_todowrite());
    jc_tool_registry_register(r, jc_tool_todoread());
    jc_tool_registry_register(r, jc_tool_remember());
    jc_tool_registry_register(r, jc_tool_ask_user());
}

cJSON *jc_tool_build_neutral(const struct jc_tool_registry *r,
                             int include_mutating,
                             const struct jc_permissions *perm)
{
    return jc_tool_build_neutral_ex(r, include_mutating, perm, NULL, NULL,
                                    NULL, 0);
}

/* Membership test against a jc_vec of char* tool names. */
static int name_in_list(const struct jc_vec *list, const char *name)
{
    jc_size i;
    for (i = 0; i < list->len; i++) {
        const char *t = *(char **)jc_vec_at((struct jc_vec *)list, i);
        if (strcmp(t, name) == 0) {
            return 1;
        }
    }
    return 0;
}

int jc_tool_allowed(const struct jc_vec *allow, const char *name)
{
    if (allow == NULL || allow->len == 0 || name == NULL) {
        return 1; /* no fence => everything permitted */
    }
    if (strcmp(name, "load_skill") == 0) {
        return 1; /* always exempt, so the agent can switch skills */
    }
    return name_in_list(allow, name);
}

/* M360: cap for the names a fence refusal lists. Enough to show the whole
 * core profile (8 tools); a longer fence gets the first 10 + "+N more". */
#define JC_REFUSAL_LIST_MAX 10

void jc_tool_refusal_render(const struct jc_vec *allow, const char *name,
                            struct jc_sb *out)
{
    jc_size i, n, shown;

    if (out == NULL) {
        return;
    }
    jc_sb_append(out, "Tool '");
    jc_sb_append(out, name != NULL ? name : "");
    jc_sb_append(out, "' is not available to this agent");
    if (allow == NULL || allow->len == 0) {
        jc_sb_append(out, ". Use only the tools advertised to you.");
        return;
    }
    n = allow->len;
    shown = (n > (jc_size)JC_REFUSAL_LIST_MAX) ? (jc_size)JC_REFUSAL_LIST_MAX
                                               : n;
    jc_sb_append(out, ". Available tools: ");
    for (i = 0; i < shown; i++) {
        const char *t = *(char **)jc_vec_at((struct jc_vec *)allow, i);
        if (i > 0) {
            jc_sb_append(out, ", ");
        }
        jc_sb_append(out, t != NULL ? t : "");
    }
    if (n > shown) {
        jc_sb_append_fmt(out, " (+%lu more)", (unsigned long)(n - shown));
    }
    jc_sb_append(out, ". Use one of those instead; '");
    jc_sb_append(out, name != NULL ? name : "");
    jc_sb_append(out, "' will not become available in this run.");
}

int jc_tool_allow_intersect(const struct jc_vec *a_list,
                            const struct jc_vec *b_list,
                            struct jc_arena *a, struct jc_vec *out)
{
    int empty_a = (a_list == NULL || a_list->len == 0);
    int empty_b = (b_list == NULL || b_list->len == 0);
    const struct jc_vec *src;
    const struct jc_vec *other;
    jc_size i;

    /* A NULL/empty list is "no fence" (identity for intersection). */
    if (empty_a && empty_b) {
        return 0;
    }
    if (empty_a) {
        src = b_list; other = NULL;
    } else if (empty_b) {
        src = a_list; other = NULL;
    } else {
        src = a_list; other = b_list;
    }
    for (i = 0; i < src->len; i++) {
        const char *t = *(char **)jc_vec_at((struct jc_vec *)src, i);
        if (other == NULL || name_in_list(other, t)) {
            char *c = jc_arena_strdup(a, t);
            if (c != NULL) {
                jc_vec_push(out, &c);
            }
        }
    }
    return (int)out->len;
}

/* The lean core set: the minimal loop a coding agent needs. Heavy/rare tools
 * (git_*, LSP nav/refactor, subagents, fetch/web, media, todo, mcp resources,
 * run_tests) are dropped to shrink the tool-definition footprint on a small
 * context window -- run_terminal_command still covers running tests/git. */
static const char *const JC_CORE_TOOL_NAMES[] = {
    "read_file",
    "write_file",
    "edit_file",
    "apply_patch",
    "list_files",
    "search_code",
    "run_terminal_command"
};

/* Every tool name jichi can ever register as a built-in (M285). Conditional
 * registration means the live registry is a SUBSET of this: git_* only inside a
 * repo, the LSP tools only with lspServers, load_skill only when skills exist,
 * web_search only with search.url, the media tools only when a model declares the
 * role, and so on. So this table answers a different, useful question -- "is this
 * a real tool name at all?" -- which is what an agent profile's or skill's
 * `tools:` fence needs, since a name that is not one silently narrows the fence
 * and the model can never call it.
 *
 * NOT hand-verified: tests/smoke/tool_names_lint.sh extracts the names from the
 * tool definitions in src/tools/ and fails the build when this table disagrees.
 * The M262 lesson -- a hand-maintained invariant is a promise nobody can keep --
 * applied before it could rot rather than after. */
static const char *const JC_ALL_TOOL_NAMES[] = {
    "apply_code_action",
    "apply_patch",
    "ask_for_help",
    "ask_user",
    "board",
    "codebase_search",
    "edit_file",
    "fetch_url",
    "find_definition",
    "find_references",
    "format_file",
    "generate_audio",
    "generate_image",
    "git_add",
    "git_blame",
    "git_branch",
    "git_commit",
    "git_diff",
    "git_log",
    "git_stash",
    "git_status",
    "hint",
    "kill_background",
    "list_code_actions",
    "list_files",
    "list_symbols",
    "load_skill",
    "play_audio",
    "read_background_output",
    "read_file",
    "read_mcp_resource",
    "record_audio",
    "remember",
    "rename_symbol",
    "run_terminal_command",
    "run_tests",
    "search_code",
    "search_docs",
    "spawn_parallel",
    "spawn_subagent",
    "todoread",
    "todowrite",
    "transcribe_audio",
    "web_search",
    "write_file"
};

int jc_tool_name_known(const char *name)
{
    jc_size i;
    jc_size n = sizeof(JC_ALL_TOOL_NAMES) / sizeof(JC_ALL_TOOL_NAMES[0]);

    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (strcmp(JC_ALL_TOOL_NAMES[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

int jc_tool_name_count(void)
{
    return (int)(sizeof(JC_ALL_TOOL_NAMES) / sizeof(JC_ALL_TOOL_NAMES[0]));
}

const char *jc_tool_name_at(int i)
{
    int n = jc_tool_name_count();
    if (i < 0 || i >= n) {
        return NULL;
    }
    return JC_ALL_TOOL_NAMES[i];
}

int jc_tool_is_core(const char *name)
{
    jc_size i;
    jc_size n = sizeof(JC_CORE_TOOL_NAMES) / sizeof(JC_CORE_TOOL_NAMES[0]);

    if (name == NULL) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (strcmp(JC_CORE_TOOL_NAMES[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

void jc_tool_core_allow(struct jc_vec *out)
{
    jc_size i;
    jc_size n = sizeof(JC_CORE_TOOL_NAMES) / sizeof(JC_CORE_TOOL_NAMES[0]);

    if (out == NULL) {
        return;
    }
    for (i = 0; i < n; i++) {
        const char *p = JC_CORE_TOOL_NAMES[i];
        jc_vec_push(out, &p);
    }
}

cJSON *jc_tool_build_neutral_ex(const struct jc_tool_registry *r,
                                int include_mutating,
                                const struct jc_permissions *perm,
                                const char *exclude_name,
                                const char *exclude_name2,
                                const struct jc_vec *allow,
                                int agent_depth)
{
    cJSON *arr;
    jc_size i;
    if (r->tools.len == 0) {
        return NULL;
    }
    arr = cJSON_CreateArray();
    for (i = 0; i < r->tools.len; i++) {
        const struct jc_tool *t =
            *(const struct jc_tool **)jc_vec_at((struct jc_vec *)&r->tools, i);
        cJSON *entry;
        if (!include_mutating && !t->readonly) {
            continue;
        }
        if (exclude_name != NULL && strcmp(t->name, exclude_name) == 0) {
            continue; /* e.g. spawn_subagent is hidden from subagents */
        }
        if (exclude_name2 != NULL && strcmp(t->name, exclude_name2) == 0) {
            continue; /* e.g. spawn_parallel is hidden from subagents too */
        }
        if (perm != NULL && jc_perm_name_in_deny(perm, t->name)) {
            continue; /* denied tools are not advertised to the model */
        }
        if (!jc_tool_allowed(allow, t->name)) {
            continue; /* a skill's or agent profile's allowed-tools fence */
        }
        if (t->main_agent_only && agent_depth > 0) {
            /* M436: do not advertise what the depth gate in jc_tool_execute will
             * refuse. The model cannot learn from that refusal anything omission
             * would not have told it sooner and cheaper -- and on a cacheless
             * backend at the measured 25-42k input tokens per call, each wasted
             * call is real money. The GATE is right (a delegate must not stomp
             * the user's list); the advertisement was the bug. */
            continue;
        }
        entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "name", t->name);
        cJSON_AddStringToObject(entry, "description", t->description);
        cJSON_AddItemToObject(entry, "parameters",
                              t->schema_ctx != NULL ? t->schema_ctx(t->ctx)
                                                    : t->schema());
        cJSON_AddItemToArray(arr, entry);
    }
    if (cJSON_GetArraySize(arr) == 0) {
        cJSON_Delete(arr);
        return NULL;
    }
    return arr;
}

void jc_tool_result_free(struct jc_tool_result *res)
{
    free(res->content);
    res->content = NULL;
}

static void set_error(struct jc_tool_result *out, const char *msg)
{
    out->content = jc_strdup(msg);
    out->is_error = 1;
}

/* M148: append the tool's expected argument shape (from its own schema) to an
 * arg-parse error, so the model sees the exact keys/types it should emit. */
static void args_schema_hint(const struct jc_tool *t, struct jc_sb *es)
{
    cJSON *schema = t->schema_ctx != NULL ? t->schema_ctx(t->ctx)
                                          : t->schema();
    cJSON *props;
    cJSON *req;
    cJSON *p;
    int first = 1;

    if (schema == NULL) {
        return;
    }
    props = cJSON_GetObjectItem(schema, "properties");
    if (cJSON_IsObject(props)) {
        jc_sb_append(es, ". Expected arguments: {");
        cJSON_ArrayForEach(p, props) {
            const char *ty = jc_json_get_str(p, "type", "any");
            if (!first) {
                jc_sb_append(es, ", ");
            }
            first = 0;
            jc_sb_append(es, p->string != NULL ? p->string : "?");
            jc_sb_append(es, ": ");
            jc_sb_append(es, ty);
        }
        jc_sb_append(es, "}");
    }
    req = cJSON_GetObjectItem(schema, "required");
    if (cJSON_IsArray(req) && cJSON_GetArraySize(req) > 0) {
        jc_sb_append(es, "; required: ");
        first = 1;
        cJSON_ArrayForEach(p, req) {
            if (cJSON_IsString(p)) {
                if (!first) {
                    jc_sb_append(es, ", ");
                }
                first = 0;
                jc_sb_append(es, p->valuestring);
            }
        }
    }
    jc_sb_append(es, ".");
    cJSON_Delete(schema);
}

jc_status jc_tool_execute(const struct jc_tool_registry *r,
                          const char *name, const char *arguments_json,
                          struct jc_tool_result *out, struct jc_app *app)
{
    const struct jc_tool *t;
    cJSON *args;
    jc_status st;
    int repaired = 0; /* M353: the M148 repair ran and produced the args used */

    out->content = NULL;
    out->is_error = 0;
    out->exit_status = -1; /* M168: -1 = this tool runs no command */
    out->policy_refusal = 0; /* M291: set only by the fence checks */

    /* M585: a call that carries NO NAME is not a wrong guess -- it is a
     * malformed call, and saying "unknown tool ''" answers a question the model
     * did not ask. Measured on a real workload: seven such calls across three
     * sessions, every one in a BURST of two or three inside a single turn. That
     * burst is the signature of a message that does not fit the mistake: told it
     * guessed a bad name, the model tries to correct a name it never sent, and
     * produces the same empty call again. The wording rule is M342/M360's --
     * name what to do instead, because a refusal that states only a cause is the
     * message class that amplifies retry loops.
     *
     * Deliberately NOT a new telemetry event. The `tool_call` event already
     * records the empty `name` and ok=false; what was missing was a READER, which
     * is M584's lesson one milestone later. Counting it from the existing field
     * also means every log already on disk answers the question retroactively --
     * a new event type would have started from zero. */
    if (name == NULL || name[0] == '\0') {
        set_error(out,
            "error: this tool call arrived with no tool name. That is a "
            "malformed call rather than a wrong guess -- no name was sent, so "
            "there is nothing to correct by renaming, and re-sending the same "
            "call will fail identically. Send the call again with a non-empty "
            "`name` naming one of the tools you were given, and put the "
            "arguments in `arguments` as a JSON object. If you meant to answer "
            "rather than to act, reply with text and make no tool call.");
        return JC_OK;
    }

    t = jc_tool_registry_find(r, name);
    if (t == NULL) {
        const char *sug = jc_tool_suggest_name(r, name);
        char buf[160];
        if (sug != NULL) {
            jc_snprintf(buf, sizeof buf,
                        "error: unknown tool '%s' (did you mean '%s'?)",
                        name != NULL ? name : "", sug);
        } else {
            jc_snprintf(buf, sizeof buf, "error: unknown tool '%s'",
                        name != NULL ? name : "");
        }
        set_error(out, buf);
        return JC_OK;
    }
    if (!t->readonly && app->readonly) {
        set_error(out, "error: tool disabled in read-only mode -- this run "
                       "may only read and report; put your findings in your "
                       "final answer instead of editing");
        return JC_OK;
    }
    /* M436: the depth gate, in ONE place. It lived inside three tool bodies as
     * `app->agent_depth > 0` checks, which meant the fact was stated three times
     * and read by nothing that builds the advertisement -- so these tools were
     * advertised to every subagent and then refused at runtime. Here it is a
     * backstop to the omission in jc_tool_build_neutral_ex, exactly as the
     * allowed-tools fence is a backstop to its own omission.
     *
     * The wording follows M342/M360: name what to do instead, since a refusal
     * that states only a cause is the message class that amplifies retry loops. */
    if (t->main_agent_only && app->agent_depth > 0) {
        char buf[220];
        jc_snprintf(buf, sizeof buf,
                    "error: '%s' belongs to the main agent -- a delegated task "
                    "shares neither the user's task list nor the board. Report "
                    "what you would have recorded in your final answer instead; "
                    "the agent that delegated this can act on it.",
                    (name != NULL) ? name : "this tool");
        out->policy_refusal = 1;
        set_error(out, buf);
        return JC_OK;
    }

    args = (arguments_json != NULL && arguments_json[0] != '\0')
           ? cJSON_Parse(arguments_json) : cJSON_CreateObject();
    if (args == NULL && app != NULL && app->last_response_truncated) {
        /* M334: the response was cut off at the model's output-token ceiling,
         * so these arguments are INCOMPLETE rather than malformed. M148's
         * repair would "succeed" here -- closing the braces yields a valid
         * object with no fields -- and the model would then be told its
         * argument SHAPE was wrong, which is a problem it does not have.
         * Measured: 197 such calls in one run at 6,977,850 tokens, and 9 more
         * in the re-drive, every one identical.
         *
         * Refuse the repair and name the cause instead. This is the M38
         * philosophy applied to a truncation: tell the model what actually
         * happened, not a verdict it cannot act on. */
        struct jc_sb ts;
        jc_sb_init(&ts);
        jc_sb_append_fmt(&ts,
            "error: your response was cut off at the model's output-token "
            "limit, so the arguments to `%s` are incomplete -- this is NOT a "
            "problem with their shape. Produce less output in one call: write "
            "the file in several smaller pieces (a first write_file, then "
            "edit_file to append), or split the work across turns.", t->name);
        set_error(out, ts.data != NULL ? ts.data
                       : "error: response truncated at the output-token limit");
        jc_sb_free(&ts);
        {
            cJSON *o = jc_app_telem_begin(app, "args_truncated");
            if (o != NULL) {
                cJSON_AddStringToObject(o, "tool", t->name);
            }
            jc_app_telem_end(app, o);
        }
        return JC_OK;
    }
    if (args == NULL) {
        /* M148: conservative repair, only after a real parse failure --
         * trailing commas, missing closers, Python literals, unambiguous
         * quote swaps. A repaired string is validated before use and the
         * attempt is counted either way (`args_repair` telemetry). */
        char *fixed = jc_jsonrepair(arguments_json);
        {
            cJSON *o = jc_app_telem_begin(app, "args_repair");
            if (o != NULL) {
                cJSON_AddStringToObject(o, "tool", t->name);
                cJSON_AddBoolToObject(o, "ok", fixed != NULL);
            }
            jc_app_telem_end(app, o);
        }
        if (fixed != NULL) {
            jc_logf(JC_LOG_INFO, "tools: repaired malformed arguments for %s",
                    t->name);
            args = cJSON_Parse(fixed);
            free(fixed);
            repaired = (args != NULL);
        }
    }
    if (args == NULL) {
        /* M148: echo the expected shape from the tool's own schema -- the
         * M38 philosophy applied to arguments: give the model the exact
         * bytes it should have produced, not just a verdict. */
        struct jc_sb es;
        jc_sb_init(&es);
        jc_sb_append(&es, "error: could not parse tool arguments as JSON");
        args_schema_hint(t, &es);
        set_error(out, es.data != NULL ? es.data
                       : "error: could not parse tool arguments as JSON");
        jc_sb_free(&es);
        return JC_OK;
    }

    /* M289: the model echoed back jichi's OWN argument-elision placeholder.
     *
     * M218 replaces an oversized `arguments_json` in history with a small valid
     * JSON object; that object sits in the arguments slot, which the model reads
     * as an example of what a call to this tool looks like -- and it copied the
     * shape back. On one measured run 18 of 19 argument-shape failures were this,
     * across edit_file/write_file/todo_write/run_terminal_command, each costing a
     * full uncached round-trip and answered with a generic "'path', 'old_string'
     * and 'new_string' are required" that explained nothing.
     *
     * There is nothing to repair -- the real arguments are gone by construction --
     * so the fix is an answer the model can act on, and a counted event so the
     * rate stays visible. Detected before the shape checks below so the specific
     * diagnosis wins over the generic one. */
    {
        cJSON *el = cJSON_GetObjectItem(args, JC_COMPACT_ELIDED_KEY);
        if (cJSON_IsString(el)) {
            const char *path = jc_json_get_str(args, "path", NULL);
            struct jc_sb es;
            cJSON *o = jc_app_telem_begin(app, "args_repair");
            if (o != NULL) {
                cJSON_AddStringToObject(o, "tool", t->name);
                cJSON_AddStringToObject(o, "kind", "elided_placeholder");
                cJSON_AddBoolToObject(o, "ok", 0); /* unrepairable by design */
            }
            jc_app_telem_end(app, o);
            jc_sb_init(&es);
            jc_sb_append(&es,
                "error: those are not arguments -- you copied a placeholder "
                "jichi put in your history after dropping this call's real "
                "arguments to save context. They cannot be recovered.");
            if (path != NULL && path[0] != '\0') {
                jc_sb_append(&es, " Re-send the full arguments for '");
                jc_sb_append(&es, path);
                jc_sb_append(&es, "'.");
            } else {
                jc_sb_append(&es, " Re-send the full arguments.");
            }
            args_schema_hint(t, &es);
            set_error(out, es.data != NULL ? es.data
                           : "error: those are not arguments (elided "
                             "placeholder); re-send the real ones");
            jc_sb_free(&es);
            cJSON_Delete(args);
            return JC_OK;
        }
    }

    /* M172: unwrap a self-named argument wrapper. A model sometimes nests the
     * arguments under the tool's own name --
     *   {"edit_file": {"path": ..., "old_string": ...}}
     * instead of {"path": ..., "old_string": ...}. That is VALID JSON, so the
     * M148 repair above never sees it: the parse succeeds, and the tool then
     * finds every required argument missing. Observed live on the zigodot
     * program (docs/analysis/2026-07-27-zigodot-telemetry.md, finding F8).
     *
     * Safe because the key must equal the tool's OWN name, and no tool has a
     * parameter named after itself -- parameters are short generics (path,
     * content, command, query), tool names are verb_noun compounds. Counted as
     * an args_repair with kind:"unwrap", so it shows up in telemetry and stays
     * distinguishable from a JSON-syntax repair. */
    {
        cJSON *inner = jc_tool_unwrap_self_named(args, t->name);
        if (inner != NULL) {
            cJSON *o = jc_app_telem_begin(app, "args_repair");
            if (o != NULL) {
                cJSON_AddStringToObject(o, "tool", t->name);
                cJSON_AddStringToObject(o, "kind", "unwrap");
                cJSON_AddBoolToObject(o, "ok", 1);
            }
            jc_app_telem_end(app, o);
            jc_logf(JC_LOG_INFO, "tools: unwrapped self-named arguments for %s",
                    t->name);
            cJSON_Delete(args);
            args = inner;
        }
    }

    /* M193: coerce a stringified nested structure back to the declared shape.
     * The sibling of the unwrap above, in the same slot and for the same reason:
     * the JSON parses cleanly, so M148's repair never sees it, and the tool then
     * rejects a semantically wrong shape. 28 of 36 todo_write calls in the
     * zigodot log failed exactly this way, all with `'todos' must be an array`.
     * Fires only on a declared array/object parameter whose value is a string
     * that parses as that type; the tool's own validation still runs after. */
    {
        cJSON *sch = (t->schema_ctx != NULL) ? t->schema_ctx(t->ctx)
                   : (t->schema != NULL) ? t->schema() : NULL;
        int nfix = jc_tool_unstring_args(args, sch);
        if (nfix > 0) {
            cJSON *o = jc_app_telem_begin(app, "args_repair");
            if (o != NULL) {
                cJSON_AddStringToObject(o, "tool", t->name);
                cJSON_AddStringToObject(o, "kind", "unstring");
                cJSON_AddNumberToObject(o, "fields", (double)nfix);
                cJSON_AddBoolToObject(o, "ok", 1);
            }
            jc_app_telem_end(app, o);
            jc_logf(JC_LOG_INFO,
                    "tools: parsed %d stringified argument(s) for %s",
                    nfix, t->name);
        }
        if (sch != NULL) {
            cJSON_Delete(sch);
        }
    }

    st = t->run_ctx != NULL ? t->run_ctx(t->ctx, args, out, app)
                            : t->run(args, out, app);
    cJSON_Delete(args);
    if (st != JC_OK && out->content == NULL) {
        set_error(out, "error: tool execution failed");
    }
    /* M353: a successful repair must not be silent to its author. The
     * unrepairable path teaches (the schema echo above); until now the
     * repairABLE path told the operator (INFO log + args_repair telemetry)
     * and never the model -- which then ran on conservatively repaired
     * arguments, subtly different from what it sent, learned "my JSON was
     * fine", and kept the habit. A per-call fact rides its own result, like
     * the exit status; the M323 throttle governs run-level warnings, not
     * facts about one call. True on error results too: the tool still ran
     * on repaired input. */
    if (repaired && out->content != NULL) {
        struct jc_sb ns;
        jc_sb_init(&ns);
        jc_sb_append(&ns, out->content);
        jc_sb_append(&ns,
            "\n[note: the arguments you sent for this call were not valid "
            "JSON and were conservatively repaired before running -- send "
            "strictly valid JSON (double-quoted keys and strings, no "
            "trailing commas, no Python literals)]");
        if (ns.data != NULL) {
            free(out->content);
            out->content = jc_sb_finish(&ns);
        } else {
            jc_sb_free(&ns);
        }
    }
    return JC_OK;
}
