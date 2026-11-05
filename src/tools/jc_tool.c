/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool.c - tool registry, schema emission, execution (see jc_tool.h). */

#include "jc_tool.h"
#include "jc_app.h"
#include "jc_config.h"
#include "jc_message.h"
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
#include "jc_path.h"

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

/* M965 (O1): append-only registration with warn on duplicate. This prevents
 * a one-tool fix (replace for spawn_subagent) from becoming a rule for every
 * registration. A future duplicate (e.g. two MCP servers with colliding names)
 * would vanish without a word if we kept silent replace. Instead, register()
 * warns (naming the existing tool) and ignores the duplicate, keeping the FIRST.
 * Only spawn_subagent's dynamic form uses replace() which intentionally replaces
 * the static one. */
void jc_tool_registry_register(struct jc_tool_registry *r,
                               const struct jc_tool *t)
{
    jc_size i;
    const char *name = t->name;

    /* Check if a tool with this name already exists; if so, warn and ignore. */
    for (i = 0; i < r->tools.len; i++) {
        const struct jc_tool *existing =
            *(const struct jc_tool **)jc_vec_at((struct jc_vec *)&r->tools, i);
        if (strcmp(existing->name, name) == 0) {
            jc_logf(JC_LOG_WARN, "tool registry: ignoring duplicate registration of '%s' "
                    "(already registered as '%s')", name, existing->name);
            return;
        }
    }
    /* No existing entry: append as usual. */
    jc_vec_push(&r->tools, &t);
}

/* M965 (O1): replace the same-name entry in place if present, else append.
 * Used ONLY where spawn_subagent's dynamic form intentionally replaces the
 * static one. */
void jc_tool_registry_replace(struct jc_tool_registry *r,
                              const struct jc_tool *t)
{
    jc_size i;
    const char *name = t->name;

    /* Check if a tool with this name already exists; if so, replace it. */
    for (i = 0; i < r->tools.len; i++) {
        const struct jc_tool *existing =
            *(const struct jc_tool **)jc_vec_at((struct jc_vec *)&r->tools, i);
        if (strcmp(existing->name, name) == 0) {
            /* Replace in place: keep position, update pointer. */
            *(const struct jc_tool **)jc_vec_at((struct jc_vec *)&r->tools, i) = t;
            return;
        }
    }
    /* No existing entry: append as usual. */
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

/* M853 (N32): see jc_tool.h. The other side of jc_tool_semantic_alias: a name
 * from another agent's toolset whose jichi equivalent is not a tool at all but a
 * plain reply. */
int jc_tool_is_finish_alias(const char *name)
{
    static const char *const names[] = {
        "finish", "done", "complete", "submit", "task_complete",
        "final_answer"
    };
    jc_size i;

    if (name == NULL) {
        return 0;
    }
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (strcmp(name, names[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* M855/M863: the git subcommand of the `git` at `g` (a word of `cmd`): options
 * are skipped (-C and -c take the next word), and the first other word is the
 * subcommand. Returns 1 with *sub and *sublen set, *rest just after it, 0 when `g`
 * does not start a `git` command word or has no subcommand. */
static int git_subcommand(const char *cmd, const char *g, const char **sub,
                          jc_size *sublen, const char **rest)
{
    const char *q = g + 3;
    int skip_next = 0;

    if (g != cmd && strchr(" \t\n;&|(", g[-1]) == NULL) {
        return 0;
    }
    if (*q != ' ' && *q != '\t') {
        return 0;
    }
    for (;;) {
        const char *w;
        jc_size wl;
        while (*q == ' ' || *q == '\t') {
            q++;
        }
        w = q;
        while (*q != '\0' && strchr(" \t\n;&|)", *q) == NULL) {
            q++;
        }
        wl = (jc_size)(q - w);
        if (wl == 0) {
            return 0;
        }
        if (skip_next) {
            skip_next = 0;
            continue;
        }
        if (w[0] == '-') {
            if (wl == 2 && (w[1] == 'C' || w[1] == 'c')) {
                skip_next = 1;
            }
            continue;
        }
        *sub = w;
        *sublen = wl;
        *rest = q;
        return 1;
    }
}

int jc_cmd_runs_git_stash(const char *cmd)
{
    const char *g;

    if (cmd == NULL) {
        return 0;
    }
    for (g = strstr(cmd, "git"); g != NULL; g = strstr(g + 3, "git")) {
        const char *sub;
        const char *rest;
        jc_size sl;
        if (git_subcommand(cmd, g, &sub, &sl, &rest) &&
            sl == 5 && strncmp(sub, "stash", 5) == 0) {
            return 1;
        }
    }
    return 0;
}

/* M863: does the rest of this simple command (up to ; & | newline or `)`) carry
 * one of `words` as a whole word? A short-option cluster ("-fdn") matches a
 * one-letter option ("-n") it contains when `cluster` is set. */
static int args_have(const char *rest, const char *const *words, int cluster)
{
    const char *q = rest;
    for (;;) {
        const char *w;
        jc_size wl;
        int k;
        while (*q == ' ' || *q == '\t') {
            q++;
        }
        if (*q == '\0' || strchr(";&|\n)", *q) != NULL) {
            return 0;
        }
        w = q;
        while (*q != '\0' && strchr(" \t\n;&|)", *q) == NULL) {
            q++;
        }
        wl = (jc_size)(q - w);
        for (k = 0; words[k] != NULL; k++) {
            jc_size kl = strlen(words[k]);
            if (wl == kl && strncmp(w, words[k], kl) == 0) {
                return 1;
            }
            if (cluster && kl == 2 && words[k][0] == '-' && words[k][1] != '-' &&
                wl > 2 && w[0] == '-' && w[1] != '-' &&
                memchr(w + 1, words[k][1], wl - 1) != NULL) {
                return 1;
            }
        }
    }
}

/* M863: does the rest of this simple command carry any word at all? */
static int args_any(const char *rest)
{
    const char *q = rest;
    while (*q == ' ' || *q == '\t') {
        q++;
    }
    return *q != '\0' && strchr(";&|\n)", *q) == NULL;
}

int jc_cmd_git_discards(const char *cmd, char *what, jc_size cap)
{
    static const char *const help[] = { "-h", "--help", NULL };
    static const char *const new_branch[] = {
        "-b", "-B", "-c", "-C", "--create", "--force-create", "--orphan", NULL
    };
    static const char *const staged[] = { "--staged", "-S", NULL };
    static const char *const worktree[] = { "--worktree", "-W", NULL };
    static const char *const dry[] = { "-n", "--dry-run", NULL };
    const char *g;

    if (what != NULL && cap > 0) {
        what[0] = '\0';
    }
    if (cmd == NULL) {
        return 0;
    }
    for (g = strstr(cmd, "git"); g != NULL; g = strstr(g + 3, "git")) {
        const char *sub;
        const char *rest;
        jc_size sl;
        int hit = 0;
        if (!git_subcommand(cmd, g, &sub, &sl, &rest) || args_have(rest, help, 0)) {
            continue;
        }
        if ((sl == 8 && strncmp(sub, "checkout", 8) == 0) ||
            (sl == 6 && strncmp(sub, "switch", 6) == 0)) {
            hit = !args_have(rest, new_branch, 0);   /* a new branch where HEAD is */
        } else if (sl == 7 && strncmp(sub, "restore", 7) == 0) {
            hit = !args_have(rest, staged, 0) || args_have(rest, worktree, 0);
        } else if (sl == 5 && strncmp(sub, "reset", 5) == 0) {
            hit = args_any(rest);                    /* a bare reset unstages */
        } else if (sl == 5 && strncmp(sub, "clean", 5) == 0) {
            hit = !args_have(rest, dry, 1);
        } else if (sl == 6 && strncmp(sub, "rebase", 6) == 0) {
            hit = 1;
        }
        if (hit) {
            if (what != NULL && cap > 0) {
                jc_size k = (sl < cap - 1) ? sl : cap - 1;
                memcpy(what, sub, k);
                what[k] = '\0';
            }
            return 1;
        }
    }
    return 0;
}

char *jc_tool_finish_alias_message(const char *arguments_json)
{
    cJSON *aj;
    const char *m;
    const char *p;
    char *out = NULL;

    aj = jc_json_parse(arguments_json);
    if (aj == NULL) {
        return NULL;
    }
    m = cJSON_IsObject(aj) ? jc_json_get_str(aj, "message", NULL) : NULL;
    if (m != NULL) {
        p = m;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            p++;
        }
        if (*p != '\0') {
            out = jc_strdup(m);
        }
    }
    cJSON_Delete(aj);
    return out;
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

/* M981 (B-04 + A-01): normalise the raw arguments exactly as the executor does.
 * The fence and the executor MUST read the same tree, or an argument shape
 * bypass opens (face two: self-named wrapper, stringified edits).
 * This is ONE PLACE both readers can call, ensuring they see the same bytes.
 *
 * Returns a newly allocated cJSON tree (caller owns, must delete), or NULL if
 * the executor would reject the call (parse failed, truncated, etc.).
 *
 * The executor's sequence (from jc_tool_execute, line 1693-1871):
 *   1. parse raw JSON (cJSON_Parse); on NULL and not truncated, repair + re-parse
 *   2. M334: if response truncated, refuse (not repairable)
 *   3. M289: elided_placeholder check (cannot repair)
 *   4. M172: unwrap self-named wrapper
 *   5. M193: unstring array/object args
 * This helper reproduces that EXACT sequence so a path it sees is a path the
 * tool will act on. The caller provides the tool name (and schema if unstring
 * is needed) so the helper can perform the same unwrapping and unstringing. */
cJSON *jc_tool_normalize_args(const char *name, const cJSON *schema,
                              const char *arguments_json, struct jc_app *app,
                              int *repaired_out)
{
    cJSON *args;
    int repaired = 0;
    int unwrapped = 0;
    int unstr_fixed = 0;

    if (arguments_json == NULL) {
        arguments_json = "";
    }

    /* Step 1: parse the raw arguments. */
    args = (arguments_json[0] != '\0') ? cJSON_Parse(arguments_json)
                                        : cJSON_CreateObject();
    if (args == NULL && app != NULL && app->last_response_truncated) {
        /* M334: truncated response -> reject, not repairable. */
        if (repaired_out) *repaired_out = 0;
        return NULL;
    }
    if (args == NULL) {
        /* M148: conservative repair only after a real parse failure. */
        char *fixed = jc_jsonrepair(arguments_json);
        if (app != NULL) {
            cJSON *o = jc_app_telem_begin(app, "args_repair");
            if (o != NULL) {
                cJSON_AddStringToObject(o, "tool", name ? name : "");
                cJSON_AddStringToObject(o, "kind", "json");
                cJSON_AddBoolToObject(o, "ok", fixed != NULL);
            }
            jc_app_telem_end(app, o);
        }
        if (fixed != NULL) {
            args = cJSON_Parse(fixed);
            repaired = (args != NULL);
            free(fixed);
        }
    }
    if (args == NULL) {
        /* Unrepairable JSON -> NULL. */
        if (repaired_out) *repaired_out = 0;
        return NULL;
    }

    /* M289: elided_placeholder -> unrepairable, emit telemetry, return NULL. */
    if (app != NULL) {
        cJSON *el = cJSON_GetObjectItem(args, JC_COMPACT_ELIDED_KEY);
        if (cJSON_IsString(el)) {
            cJSON_Delete(args);
            {
                /* Emit the same telemetry the inline code did. */
                cJSON *o = jc_app_telem_begin(app, "args_repair");
                if (o != NULL) {
                    cJSON_AddStringToObject(o, "tool", name ? name : "");
                    cJSON_AddStringToObject(o, "kind", "elided_placeholder");
                    cJSON_AddBoolToObject(o, "ok", 0); /* unrepairable by design */
                }
                jc_app_telem_end(app, o);
            }
            if (repaired_out) *repaired_out = 0;
            return NULL;
        }
    }

    /* Step 4: unwrap self-named wrapper (if any). */
    if (name != NULL && name[0] != '\0') {
        cJSON *inner = jc_tool_unwrap_self_named(args, name);
        if (inner != NULL) {
            cJSON_Delete(args);
            args = inner;
            unwrapped = 1;
        }
    }

    /* Step 5: unstring array/object args (if schema provided). */
    if (schema != NULL) {
        unstr_fixed = jc_tool_unstring_args(args, schema);
    }

    /* Track repairs for telemetry: one args_repair event per transformation
     * that actually fired (unwrap, unstring). The M148 JSON syntax repair
     * event is emitted above in the repair branch. The executor is the sole
     * emitter of every kind. */
    if (app != NULL) {
        if (unwrapped) {
            cJSON *o = jc_app_telem_begin(app, "args_repair");
            if (o != NULL) {
                cJSON_AddStringToObject(o, "tool", name ? name : "");
                cJSON_AddStringToObject(o, "kind", "unwrap");
                cJSON_AddBoolToObject(o, "ok", 1);
            }
            jc_app_telem_end(app, o);
        }
        if (unstr_fixed > 0) {
            cJSON *o = jc_app_telem_begin(app, "args_repair");
            if (o != NULL) {
                cJSON_AddStringToObject(o, "tool", name ? name : "");
                cJSON_AddStringToObject(o, "kind", "unstring");
                cJSON_AddNumberToObject(o, "fields", (double)unstr_fixed);
                cJSON_AddBoolToObject(o, "ok", 1);
            }
            jc_app_telem_end(app, o);
            jc_logf(JC_LOG_INFO,
                    "tools: parsed %d stringified argument(s) for %s",
                    unstr_fixed, name ? name : "");
        }
    }

    if (repaired_out) *repaired_out = repaired;
    return args;
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

/* ---- text-tool-call recovery (M949) ---------------------------------------- */

#define TC_RECOVER_MAX 16 /* hard bound on calls per reply */

/* Push one recovered call onto `out`, synthesising its id from `name`.
 * `args_json` ownership transfers to the vector on success. Returns 1 on
 * success, 0 when the bound is hit or memory fails (args_json stays with the
 * caller on failure). */
static int tcr_push(struct jc_vec *out, const char *name, char *args_json)
{
    struct jc_tool_call *tc;
    size_t id_cap;
    if (out->len >= TC_RECOVER_MAX) {
        free(args_json);
        return 0;
    }
    if (args_json == NULL || name == NULL) {
        return 0;
    }
    tc = (struct jc_tool_call *)jc_vec_push_slot(out);
    if (tc == NULL) {
        return 0;
    }
    id_cap = strlen(name) + 32;
    tc->id = (char *)malloc(id_cap);
    tc->name = (char *)malloc(strlen(name) + 1);
    if (tc->id == NULL || tc->name == NULL) {
        free(tc->id);
        free(tc->name);
        return 0;
    }
    jc_snprintf(tc->id, id_cap, "rec-%s-%lu", name,
                (unsigned long)(out->len + 1));
    memcpy(tc->name, name, strlen(name) + 1);
    tc->arguments_json = args_json; /* transferred */
    return 1;
}

/* One recovery candidate: a balanced JSON object at `p` (within `max` bytes)
 * that is exactly {"name"|"tool": <registered tool>,
 * "arguments"|"args"|"parameters": <object>}. On success it pushes the call
 * onto `out` and returns 1. On failure it appends a short note to `reason`
 * and returns 0. */
static int tcr_try_candidate(const char *p, jc_size max,
                             const struct jc_tool_registry *r,
                             struct jc_vec *out, struct jc_sb *reason)
{
    jc_size len;
    char *slice;
    cJSON *o;
    const char *nm;
    cJSON *args;
    char *args_json;
    const struct jc_tool *t;

    len = tc_object_len(p, max);
    if (len == 0 || len > 65536) {
        jc_sb_append(reason, "a JSON object never closes; ");
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
    if (o == NULL || !cJSON_IsObject(o)) {
        jc_sb_append(reason, "a block is not parseable JSON; ");
        return 0;
    }
    nm = jc_json_get_str(o, "name", NULL);
    if (nm == NULL) {
        nm = jc_json_get_str(o, "tool", NULL);
    }
    t = (nm != NULL) ? jc_tool_registry_find(r, nm) : NULL;
    if (t == NULL) {
        if (nm != NULL) {
            jc_sb_append_fmt(reason, "block names unregistered tool \"%s\"; ",
                             nm);
        } else {
            jc_sb_append(reason, "a block has no \"name\" key; ");
        }
        cJSON_Delete(o);
        return 0;
    }
    args = jc_json_get_obj(o, "arguments");
    if (args == NULL) {
        args = jc_json_get_obj(o, "args");
    }
    if (args == NULL) {
        args = jc_json_get_obj(o, "parameters");
    }
    if (args == NULL || !cJSON_IsObject(args)) {
        jc_sb_append(reason, "a block's arguments are not an object; ");
        cJSON_Delete(o);
        return 0;
    }
    args_json = cJSON_PrintUnformatted(args);
    cJSON_Delete(o);
    if (args_json == NULL) {
        return 0;
    }
    { int rv = tcr_push(out, t->name, args_json); return rv; }
}

/* Shape 1: a fence whose LANGUAGE TAG is a registered tool name; the body
 * must be exactly one JSON object (the arguments). Pushes the call; returns
 * 1 on success. */
static int tcr_try_tagged_fence(const char *tag, const char *body,
                                jc_size body_len,
                                const struct jc_tool_registry *r,
                                struct jc_vec *out, struct jc_sb *reason)
{
    const struct jc_tool *t;
    const char *p;
    jc_size i;
    jc_size len;
    jc_size j;

    t = jc_tool_registry_find(r, tag);
    if (t == NULL) {
        return 0; /* not a registered tool: a prose example, skip silently */
    }
    for (i = 0; i < body_len; i++) {
        char c = body[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
    }
    if (i >= body_len || body[i] != '{') {
        jc_sb_append_fmt(reason, "the %s block has no JSON object; ", tag);
        return 0;
    }
    p = body + i;
    len = tc_object_len(p, body_len - i);
    if (len == 0) {
        jc_sb_append_fmt(reason, "the %s block's object never closes; ", tag);
        return 0;
    }
    {
        char *slice = (char *)malloc(len + 1);
        if (slice == NULL) {
            return 0;
        }
        memcpy(slice, p, len);
        slice[len] = '\0';
        {
            cJSON *o = cJSON_Parse(slice);
            if (o == NULL || !cJSON_IsObject(o)) {
                jc_sb_append_fmt(reason,
                                 "the %s block's object does not parse; ",
                                 tag);
                free(slice);
                return 0;
            }
            {
                char *args_json = cJSON_PrintUnformatted(o);
                cJSON_Delete(o);
                free(slice);
                if (args_json == NULL) {
                    return 0;
                }
                for (j = i + len; j < body_len; j++) {
                    char c = body[j];
                    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
                        break;
                    }
                }
                if (j < body_len) {
                    free(args_json);
                    jc_sb_append_fmt(reason,
                                     "the %s block has text after its "
                                     "object; ", tag);
                    return 0;
                }
                return tcr_push(out, t->name, args_json);
            }
        }
    }
}

int jc_toolcall_recover(const char *text, const struct jc_tool_registry *r,
                        struct jc_vec *out, struct jc_sb *reason)
{
    jc_size n;
    jc_size i = 0;
    int recovered = 0;

    if (text == NULL || r == NULL || out == NULL) {
        return 0;
    }
    n = strlen(text);
    while (i < n) {
        const char *ls;
        jc_size line_end;
        ls = text + i;
        while ((jc_size)(ls - text) < n && (*ls == ' ' || *ls == '\t')) {
            ls++;
        }
        line_end = i;
        while (line_end < n && text[line_end] != '\n') {
            line_end++;
        }
        if ((jc_size)(ls - text) < n && *ls == '`') {
            jc_size tick = 0;
            while (tick < n - (ls - text) && ls[tick] == '`') {
                tick++;
            }
            if (tick >= 3 && tick <= 5) {
                const char *after = ls + tick;
                const char *tag_end = after;
                char tag[96];
                jc_size tag_len;
                while ((jc_size)(tag_end - text) < line_end &&
                       (*tag_end != ' ' && *tag_end != '\t')) {
                    tag_end++;
                }
                tag_len = tag_end - after;
                if (tag_len >= sizeof(tag)) {
                    tag_len = sizeof(tag) - 1;
                }
                if (tag_len == 0) {
                    /* an empty-tag fence is a closing (or a bare) fence --
                     * never a recovery candidate: the opening fence was
                     * either consumed above or is not one of the accepted
                     * shapes. Skip the line. */
                    i = line_end < n ? line_end + 1 : n;
                    continue;
                }
                memcpy(tag, after, tag_len);
                tag[tag_len] = '\0';
                {
                    jc_size close_off = n;
                    int closed = 0;
                    char fc = (char)ls[0];
                    jc_size line = (line_end < n && text[line_end] == '\n')
                                       ? line_end + 1
                                       : line_end;
                    jc_size start;
                    const char *body;
                    jc_size body_len;
                    const struct jc_tool *t;
                    while (line < n) {
                        jc_size le2 = line;
                        const char *cl;
                        jc_size ck = 0;
                        jc_size lind = 0;
                        while (le2 < n && text[le2] != '\n') {
                            le2++;
                        }
                        cl = text + line;
                        while (line + ck < n && ck < 10 &&
                               cl[ck] == fc) {
                            ck++;
                        }
                        if (ck >= 3) {
                            const char *aft2 = cl + ck;
                            while ((jc_size)(aft2 - text) < le2 &&
                                   *aft2 != '\0') {
                                lind++;
                                aft2++;
                            }
                            if (lind == 0) {
                                close_off = line;
                                closed = 1;
                                break;
                            }
                        }
                        line = (le2 < n) ? le2 + 1 : n;
                    }
                    if (!closed) {
                        jc_sb_append(reason, "a code block is not closed; ");
                    }
                    {
                        /* Treat a fence with no following line as an empty body. */
                        start = line_end + 1;
                        if (start > n) start = n;
                        body = text + start;
                        body_len = (close_off > start) ? (close_off - start) : 0;
                        t = jc_tool_registry_find(r, tag);
                        if (t != NULL) {
                            if (tcr_try_tagged_fence(tag, body, body_len, r,
                                                     out, reason)) {
                                recovered++;
                            }
                        } else if (tag_len == 4 &&
                                   (tag[0] == 'j' || tag[0] == 'J') &&
                                   (tag[1] == 's' || tag[1] == 'S') &&
                                   (tag[2] == 'o' || tag[2] == 'O') &&
                                   (tag[3] == 'n' || tag[3] == 'N')) {
                            jc_size w = 0;
                            while (w < body_len &&
                                   (body[w] == ' ' || body[w] == '\t' ||
                                    body[w] == '\r' || body[w] == '\n')) {
                                w++;
                            }
                            if (w < body_len && body[w] == '{') {
                                if (tcr_try_candidate(body + w, body_len - w,
                                                      r, out, reason)) {
                                    recovered++;
                                }
                            } else {
                                jc_sb_append(reason,
                                             "the json block has no JSON "
                                             "object; ");
                            }
                        }
i = closed ? close_off : n;
                        continue;
                    }
                }
            }
        }
        if ((jc_size)(ls - text) < n && *ls == '{') {
            if (tcr_try_candidate(ls, n - (ls - text), r, out, reason)) {
                recovered++;
            }
        }
        i = line_end < n ? line_end + 1 : n;
    }
    if (reason != NULL && reason->len > 512) {
        /* a pathological reply: keep the tail (the newest notes). */
        jc_size keep = 512;
        jc_size skip = reason->len - keep;
        memmove(reason->data, reason->data + skip, keep + 1);
        reason->len = keep;
    }
    return recovered;
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
    /* M965: spawn_subagent is dynamic to include the configured models in its
     * schema description. The static tool is kept in builtins for backward
     * compatibility (e.g. tests). Use replace() so the dynamic one replaces the
     * static one, producing exactly one spawn_subagent entry in the registry. */
    jc_tool_registry_replace(r, jc_tool_subagent_with_app(app));
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
     /* spawn_subagent is now dynamic (M965) for production use (includes model list
      * in schema). Keep static version in builtins for backward compatibility. */
     jc_tool_registry_register(r, jc_tool_subagent());
     jc_tool_registry_register(r, jc_tool_parallel());
    jc_tool_registry_register(r, jc_tool_todowrite());
    jc_tool_registry_register(r, jc_tool_todoread());
    jc_tool_registry_register(r, jc_tool_remember());
    jc_tool_registry_register(r, jc_tool_ask_user());
    jc_tool_registry_register(r, jc_tool_write_plan()); /* M631 */
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
    /* M848: two fences that share NO tool admit nothing. An empty list reads as
     * "no fence" to jc_tool_allowed, so leaving `out` empty here made the
     * strictest outcome the widest one: a profile and a restrict-tools skill
     * with nothing in common ran their subagent with every tool. Push the
     * sentinel, which matches no tool name, and return 0 so the caller can say
     * so. A static string, not an arena copy, so it cannot fail to land. */
    if (other != NULL && out->len == 0) {
        static char none[] = JC_TOOL_ALLOW_NONE;
        char *np = none;
        jc_vec_push(out, &np);
        return 0;
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
    "write_plan",
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

/* M846 (PAR-2): the ONE place that decides a built-in tool carries a shell
 * command the user wrote. `run_tests` runs its `command` argument through the
 * shell exactly as `run_terminal_command` does (M501), so every gate that means
 * "a shell command is about to run" -- the privileged/sudo gate, the kinetic
 * gate, the --strict-scope refusal, and the shell_ran signal -- must treat the
 * two alike. They did not: --strict-scope matched the name
 * `run_terminal_command` only, so `run_tests {"command": ...}` wrote outside an
 * edit scope and the envelope still reported the run verified. A predicate here
 * keeps the five gate sites from each carrying their own list, which is how they
 * drifted apart. NOT the model's own `run_in_background` tool handle, and NOT a
 * user-defined or MCP tool (those carry no free-form shell string the user
 * wrote; the config fixes their command line). */
int jc_tool_name_runs_shell(const char *name)
{
    if (name == NULL) {
        return 0;
    }
    return strcmp(name, "run_terminal_command") == 0 ||
           strcmp(name, "run_tests") == 0;
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
        if (!include_mutating && !t->readonly && !t->plan_allowed) {
            continue; /* M631: write_plan is the one write plan mode advertises */
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

/* M1067: capture the top-level property NAMES the tool's schema declares
 * (schema()["properties"] keys), into a fresh vector of malloc'd copies.
 *
 * The executor builds the schema (jc_tool_normalize_args's caller) and DELETES
 * it right after normalising the args, so the names must be captured BEFORE
 * that delete -- this is what jc_tool_execute's schema block calls it for.
 * Returns NULL when there is no schema or no "properties" (a tool that
 * declares nothing: every key it receives is then undeclared, which is the
 * right answer, since there is no declared set to belong to).
 * OOM during a push leaves the vector partially filled and it is returned
 * (a missing name can only make a valid key read as undeclared; the note
 * stays advisory and the run is unaffected). */
static struct jc_vec *declared_arg_names(const cJSON *schema)
{
    struct jc_vec *v;
    cJSON *props;
    cJSON *p;

    v = (struct jc_vec *)malloc(sizeof(struct jc_vec));
    if (v == NULL) {
        return NULL;
    }
    jc_vec_init(v, sizeof(char *));
    if (schema == NULL) {
        return v;
    }
    props = cJSON_GetObjectItem((cJSON *)schema, "properties");
    if (!cJSON_IsObject(props)) {
        return v;
    }
    cJSON_ArrayForEach(p, props) {
        if (p->string != NULL) {
            char *dup = jc_strdup(p->string);
            if (dup != NULL) {
                if (jc_vec_push(v, &dup) != JC_OK) {
                    free(dup);
                }
            }
        }
    }
    return v;
}

/* M1067: the top-level keys of the NORMALISED args that are not among the
 * schema's declared property names -- the ones a tool's tu_arg_* lookup will
 * silently ignore. Returned as a fresh vector of malloc'd copies in args
 * order (cJSON preserves insertion order), so the note can name them in the
 * order the model sent them.
 *
 * ONLY the top level is compared: keys inside a nested object are the tool's
 * own business (tu_arg_* reads the declared key and hands the value through),
 * never an undeclared argument. */
static struct jc_vec *undeclared_arg_keys(const cJSON *args,
                                          struct jc_vec *declared)
{
    struct jc_vec *u;
    cJSON *a;

    u = (struct jc_vec *)malloc(sizeof(struct jc_vec));
    if (u == NULL) {
        return NULL;
    }
    jc_vec_init(u, sizeof(char *));
    if (args == NULL || !cJSON_IsObject(args)) {
        return u;
    }
    cJSON_ArrayForEach(a, args) {
        const char *k;
        char *dup;
        jc_size i;
        int found = 0;

        if (a->string == NULL) {
            continue;
        }
        k = a->string;
        for (i = 0; declared->len > i; i++) {
            if (strcmp(*(char **)jc_vec_at(declared, i), k) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            dup = jc_strdup(k);
            if (dup != NULL) {
                if (jc_vec_push(u, &dup) != JC_OK) {
                    free(dup);
                }
            }
        }
    }
    return u;
}

/* M1067: free a declared/undeclared vector and its malloc'd string slots. */
static void arg_names_free(struct jc_vec *v)
{
    jc_size i;
    if (v == NULL) {
        return;
    }
    for (i = 0; v->len > i; i++) {
        free(*(char **)jc_vec_at(v, i));
    }
    jc_vec_free(v);
    free(v);
}

jc_status jc_tool_execute(const struct jc_tool_registry *r,
                          const char *name, const char *arguments_json,
                          struct jc_tool_result *out, struct jc_app *app)
{
    const struct jc_tool *t;
    cJSON *args;
    jc_status st;
    int repaired = 0; /* M353: the M148 repair ran and produced the args used */
    /* M1067: the schema's declared property names, captured before the schema
     * is deleted; the undeclared top-level arg keys, filled before the args
     * are deleted. Both NULL on the paths where the call never normalised. */
    struct jc_vec *declared = NULL;
    struct jc_vec *undeclared = NULL;

    out->content = NULL;
    out->is_error = 0;
    out->exit_status = -1; /* M168: -1 = this tool runs no command */
    out->policy_refusal = 0; /* M291: set only by the fence checks */
    out->has_range = 0;      /* M715: set only by a ranged read */
    out->range_offset = 0;
    out->range_limit = 0;

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
        const char *sug;
        char buf[160];
        /* M853 (N32): a name a model calls to END its turn. Told only "unknown
         * tool 'finish'", the demos drive's workers called it again, then ran
         * `echo "Done"` for hundreds of calls until stopped. So say how a turn
         * ends -- M342/M360's rule, name what to do instead. Whether this wording
         * ends such a loop is not measured. In a delegate, a call that carries a
         * `message` never reaches here: the agent loop ends the turn with it. */
        if (jc_tool_is_finish_alias(name)) {
            char fb[320];
            jc_snprintf(fb, sizeof fb,
                        "error: unknown tool '%s'. There is no '%s' tool: to "
                        "end your turn, reply with your final message as plain "
                        "text and make no tool call.", name, name);
            set_error(out, fb);
            return JC_OK;
        }
        sug = jc_tool_suggest_name(r, name);
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
    /* M631: plan mode sets app->readonly (jc_app_set_mode), so the one write
     * plan mode exists to produce -- write_plan, `plan_allowed` -- must pass
     * this fence too, and ONLY here: a read-only agent definition or a
     * read-only subagent sets the same flag for a different reason, and for
     * them the plan file is a write like any other. The permission verdict
     * (jc_perm_for_tool) and the advertiser make the same exception; three
     * places consult one field, which is the point of a field. */
    if (!t->readonly && app->readonly &&
        !(t->plan_allowed && app->mode == JC_MODE_PLAN)) {
        out->policy_refusal = 1;
        app->tool_refusals++; /* M638 */
        jc_app_why_note(app, name, arguments_json, 1,
            app->mode == JC_MODE_PLAN
                ? "plan mode runs no tool that changes anything"
                : "this run is read-only: it may read and report, not change",
            "error: tool disabled in read-only mode");
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
        app->tool_refusals++; /* M638 */
        jc_app_why_note(app, name, arguments_json, 1,
            "it belongs to the main agent, and a delegated task called it", buf);
        set_error(out, buf);
        return JC_OK;
    }

    /* M981: normalise arguments exactly as the fence does (face two:
     * self-named wrapper and stringified edits). The helper returns NULL
     * when the executor would reject (parse failed, truncated, etc.), so we
     * handle those cases by returning the same error the executor would.
     *
     * Note: the elided_placeholder case requires special handling before
     * calling the helper because the helper emits telemetry but doesn't
     * generate the specific error message with path and "re-send" text. */
    {
        int norm_repaired = 0;
        cJSON *sch = (t->schema_ctx != NULL) ? t->schema_ctx(t->ctx)
                   : (t->schema != NULL) ? t->schema() : NULL;
        /* Check for elided_placeholder before normalising. If present,
         * emit the same error message the original inline code did. */
        if (app != NULL && arguments_json != NULL && arguments_json[0] != '\0') {
            cJSON *tmp_args = cJSON_Parse(arguments_json);
            if (tmp_args != NULL) {
                cJSON *el = cJSON_GetObjectItem(tmp_args, JC_COMPACT_ELIDED_KEY);
                if (cJSON_IsString(el)) {
                    const char *path = jc_json_get_str(tmp_args, "path", NULL);
                    struct jc_sb es;
                    /* Emit the same telemetry the helper would emit. */
                    {
                        cJSON *o = jc_app_telem_begin(app, "args_repair");
                        if (o != NULL) {
                            cJSON_AddStringToObject(o, "tool", t->name);
                            cJSON_AddStringToObject(o, "kind", "elided_placeholder");
                            cJSON_AddBoolToObject(o, "ok", 0); /* unrepairable */
                        }
                        jc_app_telem_end(app, o);
                    }
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
                    cJSON_Delete(tmp_args);
                    if (sch != NULL) cJSON_Delete(sch);
                    return JC_OK;
                }
                cJSON_Delete(tmp_args);
            }
        }
        args = jc_tool_normalize_args(t->name, sch, arguments_json, app,
                                      &norm_repaired);
        /* M1067: capture the schema's declared property names BEFORE sch is
         * deleted: the undeclared-args note needs them after the run. */
        if (sch != NULL) {
            declared = declared_arg_names(sch);
            cJSON_Delete(sch);
        }
        /* norm_repaired tracks JSON syntax repair (kind:"json") only.
         * The M353 note must only fire when JSON syntax repair occurred. */
        repaired = norm_repaired;
        if (args == NULL) {
            /* The helper returns NULL when the response was truncated or
             * when the arguments could not be parsed and repaired.
             * Emit the same errors the inline code did. */
            if (app != NULL && app->last_response_truncated) {
                /* M334: truncated response. A-07 / M1090: name the cause. When
                 * jichi's own per-stream cap clipped the arguments, say so --
                 * the model never hit its token ceiling, so blaming the limit
                 * is false and misdirects the repair. The "less output per
                 * call" advice is still right (a smaller single call clears
                 * the cap), but the framing differs. */
                int by_cap = app->last_response_truncated_by_cap;
                struct jc_sb ts;
                jc_sb_init(&ts);
                if (by_cap) {
                    jc_sb_append_fmt(&ts,
                        "error: the arguments to `%s` were cut at jichi's "
                        "per-call size limit, so they are incomplete -- this is "
                        "NOT a problem with their shape and NOT the model's "
                        "output limit. Produce less output in one call: write "
                        "the file in several smaller pieces (a first write_file, "
                        "then edit_file to append), or split the work across "
                        "turns.", t->name);
                } else {
                    jc_sb_append_fmt(&ts,
                        "error: your response was cut off at the model's "
                        "output-token limit, so the arguments to `%s` are "
                        "incomplete -- this is NOT a problem with their shape. "
                        "Produce less output in one call: write the file in "
                        "several smaller pieces (a first write_file, then "
                        "edit_file to append), or split the work across turns.",
                        t->name);
                }
                set_error(out, ts.data != NULL ? ts.data
                               : "error: response truncated at the output-token limit");
                jc_sb_free(&ts);
                {
                    cJSON *o = jc_app_telem_begin(app, "args_truncated");
                    if (o != NULL) {
                        cJSON_AddStringToObject(o, "tool", t->name);
                        cJSON_AddStringToObject(o, "cause",
                                                by_cap ? "cap" : "model");
                    }
                    jc_app_telem_end(app, o);
                }
                arg_names_free(declared);  /* M1067: no run, no note, no leak */
                return JC_OK;
            }
            /* Unrepairable JSON -> error with schema hint. */
            {
                struct jc_sb es;
                jc_sb_init(&es);
                jc_sb_append(&es, "error: could not parse tool arguments as JSON");
                args_schema_hint(t, &es);
                set_error(out, es.data != NULL ? es.data
                               : "error: could not parse tool arguments as JSON");
                jc_sb_free(&es);
            }
            arg_names_free(declared);  /* M1067: no run, no note, no leak */
            return JC_OK;
        }
    }

    /* M1067: compute the undeclared keys on the args the tool ran on, before
     * they are deleted -- this is the last thing that reads args. Both
     * vectors are freed at the end of the function. */
    if (declared != NULL) {
        undeclared = undeclared_arg_keys(args, declared);
    }
    st = t->run_ctx != NULL ? t->run_ctx(t->ctx, args, out, app)
                            : t->run(args, out, app);
    cJSON_Delete(args);
    if (st != JC_OK && out->content == NULL) {
        set_error(out, "error: tool execution failed");
    }
    /* M638: a tool's own fence (tu_err_policy: the path fence, the write
     * tools' scope checks) is a refusal too; counted where every tool's
     * result passes. */
    if (out->policy_refusal && app != NULL) {
        app->tool_refusals++;
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
    /* M1067: an undeclared top-level argument key -- one the tool's schema does
     * not declare and a tu_arg_* lookup therefore never reads -- used to be
     * dropped silently. The tool ran fine on the declared arguments, so the
     * model saw a clean result and concluded its extra key had been honoured.
     * The run-0078 incident: git_commit's schema declares only message/all, so
     * the `trailer` block the agent sent was dropped and never reached the
     * commit. Like the M353 repair, a per-call fact rides its own result: name
     * each ignored key so the model can stop sending what is not read.
     *
     * Only the NORMALISED top level is compared (keys inside a nested object
     * are the tool's own business), and ONLY when out->content is present (a
     * successful, or at least content-bearing, run -- the extras are ignored,
     * not an error). The note is advisory: is_error is left untouched. */
    if (undeclared != NULL && undeclared->len > 0 && out->content != NULL) {
        struct jc_sb ns;
        jc_size i;
        jc_sb_init(&ns);
        jc_sb_append(&ns, out->content);
        jc_sb_append(&ns,
            "\n[note: this call ran with the declared arguments only; the "
            "argument key(s)");
        for (i = 0; undeclared->len > i; i++) {
            jc_sb_append(&ns, i > 0 ? ", " : " ");
            jc_sb_append(&ns, "`");
            jc_sb_append(&ns, *(char **)jc_vec_at(undeclared, i));
            jc_sb_append(&ns, "`");
        }
        jc_sb_append(&ns, " are not declared by this tool's schema and were "
            "ignored, not applied.]");
        if (ns.data != NULL) {
            free(out->content);
            out->content = jc_sb_finish(&ns);
        } else {
            jc_sb_free(&ns);
        }
        /* Telemetry: one event per call, like the unstring event (tool + a
         * count of the ignored keys). Advisory only. */
        if (app != NULL) {
            cJSON *o = jc_app_telem_begin(app, "undeclared_args");
            if (o != NULL) {
                cJSON_AddStringToObject(o, "tool", t->name);
                cJSON_AddNumberToObject(o, "count", (double)undeclared->len);
            }
            jc_app_telem_end(app, o);
        }
    }
    arg_names_free(undeclared);
    arg_names_free(declared);
    return JC_OK;
}
