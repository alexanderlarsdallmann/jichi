/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_config.c - JSON configuration loading (see jc_config.h). */

#include "jc_config.h"
#include "jc_jsonc.h"
#include "jc_json.h"
#include "jc_eventlog.h"
#include "jc_http.h"     /* JC_HTTP_*_DEFAULT: the built-in timeout tier */
#include "jc_snprintf.h"
#include "jc_log.h"
#include "jc_perm.h"
#include "jc_str.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

const char *jc_config_default_base(const char *provider)
{
    if (provider != NULL && strcmp(provider, "openai") == 0) {
        return "https://api.openai.com";
    }
    /* Default to Anthropic. */
    return "https://api.anthropic.com";
}

/* Pick a default model id for a provider when none is configured. */
static const char *default_model(const char *provider)
{
    if (provider != NULL && strcmp(provider, "openai") == 0) {
        return "gpt-4o";
    }
    return "claude-opus-4-8";
}

/* Resolve the API key: prefer a literal "apiKey", else getenv("apiKeyEnv"),
 * else a provider-conventional environment variable. */
static char *resolve_key(const cJSON *model, const char *provider,
                         struct jc_arena *a)
{
    const char *literal;
    const char *env_name;
    const char *val;

    literal = jc_json_get_str(model, "apiKey", NULL);
    if (literal != NULL && literal[0] != '\0') {
        return jc_arena_strdup(a, literal);
    }
    env_name = jc_json_get_str(model, "apiKeyEnv", NULL);
    if (env_name != NULL) {
        val = getenv(env_name);
        if (val != NULL && val[0] != '\0') {
            return jc_arena_strdup(a, val);
        }
    }
    /* Provider conventions. */
    if (provider != NULL && strcmp(provider, "openai") == 0) {
        val = getenv("OPENAI_API_KEY");
    } else {
        val = getenv("ANTHROPIC_API_KEY");
    }
    if (val != NULL && val[0] != '\0') {
        return jc_arena_strdup(a, val);
    }
    return NULL;
}

/* Determine the config path per the documented resolution order. */
static jc_status resolve_path(const char *path_or_null, char *buf, jc_size cap)
{
    const char *env;

    if (path_or_null != NULL && path_or_null[0] != '\0') {
        jc_snprintf(buf, cap, "%s", path_or_null);
        return JC_OK;
    }
    env = getenv("JC_CONFIG");
    if (env != NULL && env[0] != '\0') {
        jc_snprintf(buf, cap, "%s", env);
        return JC_OK;
    }
    /* Project-local dev config (git-ignored under local/), picked up
     * automatically when running inside a project that has one. */
    if (jc_file_exists("local/config.json")) {
        jc_snprintf(buf, cap, "%s", "local/config.json");
        return JC_OK;
    }
    jc_snprintf(buf, cap, "%s/.jichi", jc_home_dir());
    return JC_OK;
}

/* Deep-copy a cJSON node using only the in-tree subset's public API (the stub
 * has no cJSON_Duplicate). Handles the value types a config uses. */
static cJSON *config_json_dup(const cJSON *n)
{
    if (n == NULL) {
        return NULL;
    }
    if (cJSON_IsObject(n)) {
        cJSON *o = cJSON_CreateObject();
        const cJSON *it;
        for (it = n->child; it != NULL; it = it->next) {
            if (it->string != NULL) {
                cJSON_AddItemToObject(o, it->string, config_json_dup(it));
            }
        }
        return o;
    }
    if (cJSON_IsArray(n)) {
        cJSON *arr = cJSON_CreateArray();
        const cJSON *it;
        for (it = n->child; it != NULL; it = it->next) {
            cJSON_AddItemToArray(arr, config_json_dup(it));
        }
        return arr;
    }
    if (cJSON_IsString(n)) {
        return cJSON_CreateString(n->valuestring != NULL ? n->valuestring : "");
    }
    if (cJSON_IsNumber(n)) {
        return cJSON_CreateNumber(n->valuedouble);
    }
    if (cJSON_IsBool(n)) {
        return cJSON_CreateBool(cJSON_IsTrue(n) ? 1 : 0);
    }
    return cJSON_CreateNull();
}

/* Overlay `overlay` onto `base` in place (project overlays global). For each
 * overlay key: two arrays are UNIONED with overlay (project) items first (so a
 * project model/alias/doc takes precedence and index-0/active resolution
 * prefers it), then the base (global) items; any other value REPLACES the base
 * key (project scalars win). `overlay` is left intact (items are deep-copied).
 * Public (declared in jc_config.h) so it can be unit-tested directly. */
void jc_config_merge_json(cJSON *base, const cJSON *overlay)
{
    const cJSON *ov;
    if (base == NULL || overlay == NULL) {
        return;
    }
    for (ov = overlay->child; ov != NULL; ov = ov->next) {
        cJSON *ex;
        if (ov->string == NULL) {
            continue;
        }
        ex = cJSON_GetObjectItemCaseSensitive(base, ov->string);
        if (ex != NULL && cJSON_IsArray(ex) && cJSON_IsArray(ov)) {
            cJSON *merged = cJSON_CreateArray();
            const cJSON *it;
            if (merged == NULL) {
                continue;
            }
            for (it = ov->child; it != NULL; it = it->next) {
                cJSON_AddItemToArray(merged, config_json_dup(it));
            }
            for (it = ex->child; it != NULL; it = it->next) {
                cJSON_AddItemToArray(merged, config_json_dup(it));
            }
            cJSON_ReplaceItemInObject(base, ov->string, merged);
        } else if (ex != NULL) {
            cJSON_ReplaceItemInObject(base, ov->string, config_json_dup(ov));
        } else {
            cJSON_AddItemToObject(base, ov->string, config_json_dup(ov));
        }
    }
}

/* Parse the config JSON from `path` (arena-read). NULL on missing/unreadable/
 * malformed (logged); caller owns the returned cJSON. */
static cJSON *config_parse_file(const char *path, struct jc_arena *a)
{
    char *text;
    cJSON *root;
    if (!jc_file_exists(path)) {
        return NULL; /* absent is not an error: the caller falls back */
    }
    if (jc_read_file(path, &text, NULL, a) != JC_OK) {
        /* Present but unreadable. M198 made jc_read_file REJECT a directory
         * rather than return an empty string -- turning a silent wrong answer
         * into an error -- but this branch then dropped that error on the
         * floor, and main's config-load caller returns 1 without a word. The
         * two together made `doctor`/`context`/`map`/`status` exit 1 with
         * nothing on either stream when ~/.jichi was a DIRECTORY, which is the
         * shape an operator actually creates (a stray file copied into it).
         * Name the cause here, at the chokepoint that knows it. */
        if (jc_is_dir(path)) {
            jc_logf(JC_LOG_ERROR,
                    "config path is a directory, not a file: %s", path);
        } else {
            jc_logf(JC_LOG_ERROR, "config file unreadable: %s", path);
        }
        return NULL;
    }
    /* JSONC, because that is what the documentation has always shown. Fifteen
     * config examples across docs/ and README.md carry // comments inside a
     * ```jsonc fence -- CONFIG_TUTORIAL.md twice -- and every one of them was
     * unpasteable: the parser rejects a comment and the reader got only
     * "malformed config JSON" with nothing naming the comment (M459).
     *
     * The machinery already existed and was already trusted: jc_jsonc_strip is
     * unit-tested and is what jichi uses to read Claude Code configs and
     * workflow files. Only jichi's OWN config never got it, which is the
     * awkward shape -- lenient with other tools' files, strict with its own.
     *
     * A strict widening: every config that parsed before still parses, since
     * stripping comments and trailing commas from a file that has neither is
     * the identity. */
    {
        char *clean = jc_jsonc_strip(text, a);
        if (clean != NULL) {
            text = clean;
        }
    }
    root = jc_json_parse(text);
    if (root == NULL) {
        jc_logf(JC_LOG_ERROR, "malformed config JSON: %s", path);
    }
    return root;
}

unsigned jc_config_role_flag(const char *name)
{
    if (name == NULL) {
        return 0u;
    }
    if (strcmp(name, "chat") == 0) {
        return JC_ROLE_CHAT;
    }
    if (strcmp(name, "edit") == 0) {
        return JC_ROLE_EDIT;
    }
    if (strcmp(name, "autocomplete") == 0) {
        return JC_ROLE_AUTOCOMPLETE;
    }
    if (strcmp(name, "embed") == 0) {
        return JC_ROLE_EMBED;
    }
    if (strcmp(name, "rerank") == 0) {
        return JC_ROLE_RERANK;
    }
    if (strcmp(name, "summarize") == 0) {
        return JC_ROLE_SUMMARIZE;
    }
    if (strcmp(name, "apply") == 0) {
        return JC_ROLE_APPLY;
    }
    if (strcmp(name, "image") == 0) {
        return JC_ROLE_IMAGE;
    }
    if (strcmp(name, "audio") == 0) {
        return JC_ROLE_AUDIO;
    }
    if (strcmp(name, "transcribe") == 0) {
        return JC_ROLE_TRANSCRIBE;
    }
    return 0u;
}

/* OR together the role flags named in a JSON "roles" string array. */
static unsigned parse_roles(const cJSON *model)
{
    cJSON *roles = cJSON_GetObjectItem(model, "roles");
    cJSON *r;
    unsigned mask = 0u;

    if (!cJSON_IsArray(roles)) {
        return 0u;
    }
    cJSON_ArrayForEach(r, roles) {
        if (cJSON_IsString(r) && r->valuestring != NULL) {
            mask |= jc_config_role_flag(r->valuestring);
        }
    }
    return mask;
}

/* Normalise a raw timeout value: any negative becomes -1 (unset); 0 stays
 * (explicitly disabled); otherwise the whole number of seconds. */
static long clamp_timeout(double v)
{
    long n = (long)v;
    return n < 0 ? -1 : n;
}

/* Parse a "timeouts" object (connect/stall/request, in seconds) into `t`,
 * resetting it first so an absent block/key leaves -1 (unset). */
static void parse_timeouts(const cJSON *obj, struct jc_timeouts_cfg *t)
{
    t->connect = -1;
    t->stall = -1;
    t->request = -1;
    if (!cJSON_IsObject(obj)) {
        return;
    }
    t->connect = clamp_timeout(jc_json_get_num(obj, "connect", -1.0));
    t->stall = clamp_timeout(jc_json_get_num(obj, "stall", -1.0));
    t->request = clamp_timeout(jc_json_get_num(obj, "request", -1.0));
}

/* Parse one model object (from "model" or an element of "models"). When
 * `model` is NULL a sensible default model is produced. */
static void parse_model(const cJSON *model, struct jc_model_cfg *out,
                        struct jc_arena *a)
{
    const char *prov;
    const char *s;

    memset(out, 0, sizeof(*out));

    prov = jc_json_get_str(model, "provider", NULL);
    if (prov == NULL) {
        prov = getenv("JC_PROVIDER");
        if (prov == NULL || prov[0] == '\0') {
            prov = "anthropic";
        }
    }
    out->provider = jc_arena_strdup(a, prov);

    s = jc_json_get_str(model, "model", NULL);
    out->model_defaulted = (s == NULL) ? 1 : 0;      /* M505 */
    out->model = jc_arena_strdup(a, s != NULL ? s : default_model(prov));

    s = jc_json_get_str(model, "apiBase", NULL);
    out->api_base = jc_arena_strdup(a, s != NULL ? s
                                     : jc_config_default_base(prov));

    s = jc_json_get_str(model, "name", NULL);
    out->name = (s != NULL) ? jc_arena_strdup(a, s) : NULL;

    s = jc_json_get_str(model, "description", NULL);
    out->description = (s != NULL) ? jc_arena_strdup(a, s) : NULL;

    out->api_key = resolve_key(model, prov, a);
    {
        const char *lit = jc_json_get_str(model, "apiKey", NULL);
        const char *envn = jc_json_get_str(model, "apiKeyEnv", NULL);
        out->api_key_literal = (lit != NULL && lit[0] != '\0');
        out->api_key_env = (envn != NULL && envn[0] != '\0')
                           ? jc_arena_strdup(a, envn) : NULL;
    }
    out->temperature = jc_json_get_num(model, "temperature", -1.0);
    out->max_tokens = (long)jc_json_get_num(model, "maxTokens", 0.0);
    out->input_cost = jc_json_get_num(model, "inputCostPer1M", 0.0);
    out->output_cost = jc_json_get_num(model, "outputCostPer1M", 0.0);
    out->cache_read_cost = jc_json_get_num(model, "cacheReadCostPer1M", 0.0);
    out->cache_write_cost = jc_json_get_num(model, "cacheWriteCostPer1M", 0.0);
    out->context_limit = (long)jc_json_get_num(model, "contextLength", 0.0);
    out->roles = parse_roles(model);
    out->vision = jc_json_get_bool_lenient(model, "vision", 0);
    /* M149: tool-calling capability. "native" (default) | "none"; "text" is
     * reserved for a future prompt-based fallback and reads as native. */
    {
        const char *tc = jc_json_get_str(model, "toolCalling", "native");
        if (strcmp(tc, "none") == 0) {
            out->tool_calling = 1;
        } else {
            if (strcmp(tc, "native") != 0) {
                jc_logf(JC_LOG_WARN, "config: toolCalling \"%s\" is unknown/"
                        "reserved; treating as \"native\"", tc);
            }
            out->tool_calling = 0;
        }
    }
    /* Per-model "promptCache" is a raw override: -1 unset (inherit the global)
     * / 0 off / 1 on. The effective prompt_cache is resolved later by
     * jc_config_resolve_prompt_cache (M31d). Provisional effective = on. */
    out->prompt_cache_cfg =
        (jc_json_get_obj(model, "promptCache") != NULL)
            ? (jc_json_get_bool_lenient(model, "promptCache", 1) ? 1 : 0)
            : -1;
    out->prompt_cache = 1;
    s = jc_json_get_str(model, "fallback", NULL);
    out->fallback = (s != NULL) ? jc_arena_strdup(a, s) : NULL;
    parse_timeouts(jc_json_get_obj(model, "timeouts"), &out->timeouts);
}

static void push_model(struct jc_config *out, const cJSON *model,
                       struct jc_arena *a)
{
    struct jc_model_cfg cfg;
    parse_model(model, &cfg, a);
    jc_vec_push(&out->models, &cfg);
}

/* Append the strings of a JSON string array to `vec` (of char*), each copied
 * into the arena. A non-array `item` leaves `vec` empty. */
static void parse_str_array(const cJSON *item, struct jc_vec *vec,
                            struct jc_arena *a)
{
    cJSON *e;
    if (!cJSON_IsArray(item)) {
        return;
    }
    cJSON_ArrayForEach(e, item) {
        if (cJSON_IsString(e) && e->valuestring != NULL) {
            char *copy = jc_arena_strdup(a, e->valuestring);
            jc_vec_push(vec, &copy);
        }
    }
}

/* Append "KEY=VALUE" entries from a JSON object's string members to `vec`. */
static void parse_env_object(const cJSON *obj, struct jc_vec *vec,
                             struct jc_arena *a)
{
    cJSON *e;
    if (!cJSON_IsObject(obj)) {
        return;
    }
    cJSON_ArrayForEach(e, obj) {
        if (cJSON_IsString(e) && e->valuestring != NULL && e->string != NULL) {
            jc_size n = strlen(e->string) + 1 + strlen(e->valuestring) + 1;
            char *kv = (char *)jc_arena_alloc(a, n);
            if (kv != NULL) {
                jc_snprintf(kv, n, "%s=%s", e->string, e->valuestring);
                jc_vec_push(vec, &kv);
            }
        }
    }
}

/* Parse an approval-policy field, which is either a string array of tool
 * names, the string "*", or boolean true (the latter two meaning "all"). */
static void parse_policy_field(const cJSON *srv, const char *key,
                               struct jc_vec *list, int *all,
                               struct jc_arena *a)
{
    cJSON *item = jc_json_get_obj(srv, key);
    *all = 0;
    if (cJSON_IsArray(item)) {
        parse_str_array(item, list, a);
    } else if (cJSON_IsString(item) && item->valuestring != NULL &&
               strcmp(item->valuestring, "*") == 0) {
        *all = 1;
    } else if (cJSON_IsTrue(item)) {
        *all = 1;
    }
}

/* Parse one element of the "mcpServers" array. Unnamed entries are skipped. */
static void push_mcp_server(struct jc_config *out, const cJSON *srv,
                            struct jc_arena *a)
{
    struct jc_mcp_server_cfg cfg;
    const char *s;

    if (!cJSON_IsObject(srv)) {
        return;
    }
    memset(&cfg, 0, sizeof(cfg));
    jc_vec_init(&cfg.args, sizeof(char *));
    jc_vec_init(&cfg.env, sizeof(char *));
    jc_vec_init(&cfg.headers, sizeof(char *));
    jc_vec_init(&cfg.auto_approve, sizeof(char *));
    jc_vec_init(&cfg.deny, sizeof(char *));

    s = jc_json_get_str(srv, "name", NULL);
    cfg.name = (s != NULL) ? jc_arena_strdup(a, s) : NULL;
    s = jc_json_get_str(srv, "command", NULL);
    cfg.command = (s != NULL) ? jc_arena_strdup(a, s) : NULL;
    s = jc_json_get_str(srv, "url", NULL);
    cfg.url = (s != NULL) ? jc_arena_strdup(a, s) : NULL;

    s = jc_json_get_str(srv, "type", NULL);
    if (s == NULL) {
        s = (cfg.url != NULL) ? "http" : "stdio";
    }
    cfg.type = jc_arena_strdup(a, s);

    parse_str_array(jc_json_get_obj(srv, "args"), &cfg.args, a);
    parse_env_object(jc_json_get_obj(srv, "env"), &cfg.env, a);
    parse_str_array(jc_json_get_obj(srv, "headers"), &cfg.headers, a);
    parse_policy_field(srv, "autoApprove", &cfg.auto_approve,
                       &cfg.auto_approve_all, a);
    parse_policy_field(srv, "deny", &cfg.deny, &cfg.deny_all, a);
    cfg.kinetic = jc_json_get_bool_lenient(srv, "kinetic", 0); /* M163a */

    if (cfg.name == NULL || cfg.name[0] == '\0') {
        jc_logf(JC_LOG_WARN, "mcpServers: skipping entry with no name");
        jc_vec_free(&cfg.args);
        jc_vec_free(&cfg.env);
        jc_vec_free(&cfg.headers);
        jc_vec_free(&cfg.auto_approve);
        jc_vec_free(&cfg.deny);
        return;
    }
    jc_vec_push(&out->mcp_servers, &cfg);
}

/* Parse one element of the "docs" array (M34a): {name, path} or {name, url}
 * (M51), the url source optionally carrying type "rss"/"atom"/"feed" to read it
 * as an RSS/Atom feed (W4). An entry needs a name and exactly one of path/url; it
 * is skipped with a warning otherwise. */
static void push_docs(struct jc_config *out, const cJSON *d, struct jc_arena *a)
{
    struct jc_docs_cfg cfg;
    const char *name = jc_json_get_str(d, "name", NULL);
    const char *path = jc_json_get_str(d, "path", NULL);
    const char *url = jc_json_get_str(d, "url", NULL);

    if (!cJSON_IsObject(d)) {
        return;
    }
    if (name == NULL || name[0] == '\0' ||
        ((path == NULL || path[0] == '\0') && (url == NULL || url[0] == '\0'))) {
        jc_logf(JC_LOG_WARN, "docs: skipping entry with no name/path or url");
        return;
    }
    cfg.name = jc_arena_strdup(a, name);
    cfg.path = (path != NULL && path[0] != '\0') ? jc_arena_strdup(a, path)
                                                 : NULL;
    cfg.url = (url != NULL && url[0] != '\0') ? jc_arena_strdup(a, url) : NULL;
    {
        const char *type = jc_json_get_str(d, "type", NULL);
        cfg.feed = (type != NULL &&
                    (strcmp(type, "rss") == 0 || strcmp(type, "atom") == 0 ||
                     strcmp(type, "feed") == 0)) ? 1 : 0;
    }
    jc_vec_push(&out->docs, &cfg);
}

/* Parse one element of the "aliases" array (#6): {name, type, value|path|url|
 * host|env|cmd|text}. The value is read from the type-appropriate key (falling
 * back to a generic "value"). key/token entries are secrets (env or cmd). */
static void push_alias(struct jc_config *out, const cJSON *d, struct jc_arena *a)
{
    struct jc_alias_cfg cfg;
    const char *name = jc_json_get_str(d, "name", NULL);
    const char *type = jc_json_get_str(d, "type", NULL);
    const char *env = jc_json_get_str(d, "env", NULL);
    const char *cmd = jc_json_get_str(d, "cmd", NULL);
    const char *val;

    if (!cJSON_IsObject(d) || name == NULL || name[0] == '\0' ||
        type == NULL || type[0] == '\0') {
        jc_logf(JC_LOG_WARN, "aliases: skipping entry with no name/type");
        return;
    }
    memset(&cfg, 0, sizeof cfg);
    cfg.name = jc_arena_strdup(a, name);
    cfg.type = jc_arena_strdup(a, type);
    cfg.is_secret = (strcmp(type, "key") == 0 || strcmp(type, "token") == 0);
    if (cfg.is_secret) {
        if (cmd != NULL && cmd[0] != '\0') {
            cfg.value = jc_arena_strdup(a, cmd);
            cfg.is_cmd = 1;
        } else if (env != NULL && env[0] != '\0') {
            cfg.value = jc_arena_strdup(a, env);
        } else {
            jc_logf(JC_LOG_WARN, "aliases: secret '%s' has no env/cmd", name);
            return;
        }
    } else {
        /* type-specific key, else a generic "value" */
        val = jc_json_get_str(d, "path", NULL);
        if (val == NULL) val = jc_json_get_str(d, "url", NULL);
        if (val == NULL) val = jc_json_get_str(d, "host", NULL);
        if (val == NULL) val = jc_json_get_str(d, "text", NULL);
        if (val == NULL) val = jc_json_get_str(d, "value", NULL);
        if (val == NULL || val[0] == '\0') {
            jc_logf(JC_LOG_WARN, "aliases: '%s' has no value", name);
            return;
        }
        cfg.value = jc_arena_strdup(a, val);
    }
    jc_vec_push(&out->aliases, &cfg);
}

/* Parse one element of the "lspServers" array. Unnamed/command-less entries are
 * skipped. */
static void push_lsp_server(struct jc_config *out, const cJSON *srv,
                            struct jc_arena *a)
{
    struct jc_lsp_server_cfg cfg;
    const char *s;

    if (!cJSON_IsObject(srv)) {
        return;
    }
    memset(&cfg, 0, sizeof(cfg));
    jc_vec_init(&cfg.args, sizeof(char *));
    jc_vec_init(&cfg.extensions, sizeof(char *));

    s = jc_json_get_str(srv, "name", NULL);
    cfg.name = (s != NULL) ? jc_arena_strdup(a, s) : NULL;
    s = jc_json_get_str(srv, "command", NULL);
    cfg.command = (s != NULL) ? jc_arena_strdup(a, s) : NULL;
    parse_str_array(jc_json_get_obj(srv, "args"), &cfg.args, a);
    parse_str_array(jc_json_get_obj(srv, "extensions"), &cfg.extensions, a);

    if (cfg.name == NULL || cfg.command == NULL || cfg.command[0] == '\0') {
        jc_logf(JC_LOG_WARN,
                "lspServers: skipping entry with no name/command");
        jc_vec_free(&cfg.args);
        jc_vec_free(&cfg.extensions);
        return;
    }
    jc_vec_push(&out->lsp_servers, &cfg);
}

/* Parse one element of the "tools" array (a user-defined tool). Entries with no
 * name, or with neither command nor shell, are skipped. */
static void push_user_tool(struct jc_config *out, const cJSON *t,
                           struct jc_arena *a)
{
    struct jc_user_tool_cfg cfg;
    const char *s;
    cJSON *schema;

    if (!cJSON_IsObject(t)) {
        return;
    }
    memset(&cfg, 0, sizeof(cfg));
    jc_vec_init(&cfg.args, sizeof(char *));
    jc_vec_init(&cfg.env, sizeof(char *));

    s = jc_json_get_str(t, "name", NULL);
    cfg.name = (s != NULL) ? jc_arena_strdup(a, s) : NULL;
    s = jc_json_get_str(t, "description", NULL);
    cfg.description = jc_arena_strdup(a, s != NULL ? s : "");
    s = jc_json_get_str(t, "command", NULL);
    cfg.command = (s != NULL) ? jc_arena_strdup(a, s) : NULL;
    s = jc_json_get_str(t, "shell", NULL);
    cfg.shell = (s != NULL) ? jc_arena_strdup(a, s) : NULL;
    parse_str_array(jc_json_get_obj(t, "args"), &cfg.args, a);
    parse_env_object(jc_json_get_obj(t, "env"), &cfg.env, a);
    cfg.timeout = (long)jc_json_get_num(t, "timeout", 0.0);
    cfg.readonly = jc_json_get_bool_lenient(t, "readonly", 0);
    cfg.kinetic = jc_json_get_bool_lenient(t, "kinetic", 0);
    if (cfg.kinetic && cfg.readonly) {
        /* A tool that actuates hardware is never a safe read. (M163a) */
        jc_logf(JC_LOG_WARN, "config: tool \"%s\" is kinetic; ignoring "
                "readonly:true (a kinetic tool is always mutating)",
                cfg.name != NULL ? cfg.name : "?");
        cfg.readonly = 0;
    }

    schema = jc_json_get_obj(t, "schema");
    if (cJSON_IsObject(schema)) {
        char *txt = jc_json_print(schema);
        if (txt != NULL) {
            cfg.schema_json = jc_arena_strdup(a, txt);
            free(txt);
        }
    }

    if (cfg.name == NULL || cfg.name[0] == '\0' ||
        (cfg.command == NULL && cfg.shell == NULL)) {
        jc_logf(JC_LOG_WARN,
                "tools: skipping entry with no name or no command/shell");
        jc_vec_free(&cfg.args);
        jc_vec_free(&cfg.env);
        return;
    }
    jc_vec_push(&out->user_tools, &cfg);
}

/* Map a hook event name to its enum, or -1. */
static int hook_event_index(const char *name)
{
    if (name == NULL) return -1;
    if (strcmp(name, "PreToolUse") == 0) return JC_HOOK_PRE_TOOL;
    if (strcmp(name, "PostToolUse") == 0) return JC_HOOK_POST_TOOL;
    if (strcmp(name, "UserPromptSubmit") == 0) return JC_HOOK_USER_PROMPT;
    if (strcmp(name, "Stop") == 0) return JC_HOOK_STOP;
    if (strcmp(name, "SessionStart") == 0) return JC_HOOK_SESSION_START;
    return -1;
}

/* Parse one command object ({command,args,shell,timeout}) into `cmds`. */
static void push_hook_cmd(struct jc_vec *cmds, const cJSON *c,
                          struct jc_arena *a)
{
    struct jc_hook_cmd_cfg cfg;
    const char *s;

    if (!cJSON_IsObject(c)) return;
    memset(&cfg, 0, sizeof(cfg));
    jc_vec_init(&cfg.args, sizeof(char *));
    s = jc_json_get_str(c, "command", NULL);
    cfg.command = (s != NULL) ? jc_arena_strdup(a, s) : NULL;
    s = jc_json_get_str(c, "shell", NULL);
    cfg.shell = (s != NULL) ? jc_arena_strdup(a, s) : NULL;
    parse_str_array(jc_json_get_obj(c, "args"), &cfg.args, a);
    cfg.timeout = (long)jc_json_get_num(c, "timeout", 0.0);
    if (cfg.command == NULL && cfg.shell == NULL) {
        jc_logf(JC_LOG_WARN, "hooks: skipping command with no command/shell");
        jc_vec_free(&cfg.args);
        return;
    }
    jc_vec_push(cmds, &cfg);
}

/* Parse one matcher object ({matcher,commands:[...]}) into the event vector. */
static void push_hook_matcher(struct jc_vec *ev, const cJSON *m,
                              struct jc_arena *a)
{
    struct jc_hook_matcher_cfg cfg;
    const char *s;
    cJSON *cmds;
    cJSON *c;

    if (!cJSON_IsObject(m)) return;
    memset(&cfg, 0, sizeof(cfg));
    jc_vec_init(&cfg.commands, sizeof(struct jc_hook_cmd_cfg));
    s = jc_json_get_str(m, "matcher", NULL);
    cfg.matcher = (s != NULL) ? jc_arena_strdup(a, s) : NULL;
    cmds = jc_json_get_obj(m, "commands");
    if (cJSON_IsArray(cmds)) {
        cJSON_ArrayForEach(c, cmds) {
            push_hook_cmd(&cfg.commands, c, a);
        }
    }
    if (cfg.commands.len == 0) {
        jc_vec_free(&cfg.commands);
        return;
    }
    jc_vec_push(ev, &cfg);
}

/* Parse the top-level "hooks" object: { "<Event>": [ {matcher,commands}... ] }. */
static void parse_hooks(struct jc_config *out, const cJSON *hooks,
                        struct jc_arena *a)
{
    cJSON *ev;
    if (!cJSON_IsObject(hooks)) return;
    cJSON_ArrayForEach(ev, hooks) {
        int idx = hook_event_index(ev->string);
        cJSON *m;
        if (idx < 0 || !cJSON_IsArray(ev)) {
            if (idx < 0 && ev->string != NULL) {
                jc_logf(JC_LOG_WARN, "hooks: unknown event '%s' ignored",
                        ev->string);
            }
            continue;
        }
        cJSON_ArrayForEach(m, ev) {
            push_hook_matcher(&out->hooks.events[idx], m, a);
        }
    }
}

/* Shared loader for jc_config_load (file-based) and jc_config_load_json
 * (inline text). When `inline_json` is non-NULL it is parsed as THE config --
 * a single explicit source, no global/project merge -- and the file-discovery
 * path is skipped; otherwise `path_or_null` drives the usual file logic. */
static jc_status config_load_impl(const char *path_or_null,
                                  const char *inline_json, int low_resource,
                                  struct jc_config *out, struct jc_arena *a)
{
    char path[1024];
    cJSON *root;
    cJSON *models;
    cJSON *model;
    int explicit_path = 0;
    /* Did the config TEXT carry a contextLimit, or is the number below
     * lite's default? The audibility check at the end of this function
     * must not warn an operator about a cap they chose themselves. */
    int ctx_limit_explicit = 0;
    /* Provisional: ON and AUTO shape the pre-parse struct defaults; the final
     * resolution (flag > config key > auto) happens once the key is parsed. */
    int lite = (low_resource == JC_LITE_HINT_ON ||
                low_resource == JC_LITE_HINT_AUTO);

    memset(out, 0, sizeof(*out));
    jc_vec_init(&out->models, sizeof(struct jc_model_cfg));
    jc_vec_init(&out->mcp_servers, sizeof(struct jc_mcp_server_cfg));
    jc_vec_init(&out->lsp_servers, sizeof(struct jc_lsp_server_cfg));
    jc_vec_init(&out->user_tools, sizeof(struct jc_user_tool_cfg));
    jc_vec_init(&out->docs, sizeof(struct jc_docs_cfg));
    jc_vec_init(&out->aliases, sizeof(struct jc_alias_cfg));
    {
        int hi;
        for (hi = 0; hi < JC_HOOK_EVENT_COUNT; hi++) {
            jc_vec_init(&out->hooks.events[hi],
                        sizeof(struct jc_hook_matcher_cfg));
        }
    }
    out->hooks_enabled = 0;
    out->config_editable = 0;
    out->mem_budget_mb = 0;
    out->run_timeout = 0;
    out->run_timeout_cli = 0;
    out->accessible = 0;
    jc_vec_init(&out->permissions.allow, sizeof(char *));
    jc_vec_init(&out->permissions.deny, sizeof(char *));
    jc_vec_init(&out->privileged_allow, sizeof(char *)); /* M153 */
    out->privileged_commands = JC_PRIVPOL_ASK;           /* M153 default */
    out->privileged_audit = 1;                           /* M154 default on */
    jc_vec_init(&out->kinetic_allow, sizeof(char *));    /* M163a */
    jc_vec_init(&out->kinetic_prefixes, sizeof(char *)); /* M163a */
    out->kinetic_commands = JC_PRIVPOL_ASK;              /* M163a default */
    out->kinetic_audit = 1;                              /* M163a default on */
    jc_vec_init(&out->sound.play_args, sizeof(char *));  /* M163b */
    jc_vec_init(&out->sound.record_args, sizeof(char *));
    jc_vec_init(&out->instructions, sizeof(char *));
    jc_vec_init(&out->edit_scope, sizeof(char *));
    jc_vec_init(&out->ignore_dirs, sizeof(char *));
    jc_vec_init(&out->design_docs, sizeof(char *));
    jc_vec_init(&out->reference_roots, sizeof(char *));
    out->verify = NULL;
    out->verify_kind = NULL;
    out->test_command = NULL;
    out->time_format = NULL;
    out->group_sep = jc_locale_group_sep(); /* locale sep; "numberFormat" refines */
    if (out->group_sep == 0) {
        out->group_sep = '.'; /* readable default when the locale has none (C) */
    }
    out->notify = NULL;
    out->notify_bell = 0;
    /* Lean mode (--lite) shifts the resource-heavy defaults; these struct
     * defaults cover the no-config-file path, and the jc_json_get_* defaults
     * below cover the config-file path (where explicit keys still win). The
     * single source of truth for each lean value is the `lite ? lean : normal`
     * expression duplicated in both places. */
    out->repo_map = lite ? 0 : 1;
    out->repo_map_limit = 0;
    out->references = lite ? 0 : 1;
    out->markdown = lite ? 0 : 1;
    out->type_ahead = 0;
    out->wisdom = 1;
    out->board = 0;
    out->fuzzy_edit = 1; /* cheap + pure, so on even in lite */
    out->assignments = 0; /* opt-in: off unless the user enables it */
    out->self_review = -1; /* default: on in AUTO mode only */
    out->path_fence = -1;  /* default: on in autonomous postures only */
    /* M338: -1 = unset. Interactive destroys (undo/rewind/revert) then preserve
     * and the envelope's rollback does not -- the two have different risk, and
     * one key with per-mechanism defaults beats two keys that can contradict. */
    out->preserve_discarded = -1;
    out->prompt_cache = -1; /* default: auto (=on); resolved per model (M31d) */
    out->prompt_cache_1h = 0; /* default cache TTL: 5 minutes (M31e) */
    out->cost_model = -1;   /* auto: on only when prompt caching is off (M440) */
    out->max_parallel_agents = lite ? 1 : 0;
    out->parallel_task_timeout = 0; /* 0 => built-in watchdog default (M62) */
    out->daemon_workers = 0;        /* 0 => auto (min(cpu,4)) */
    out->daemon_worker_timeout = 0; /* 0 => built-in 300s watchdog */
    out->low_resource = lite;
    memset(&out->routing, 0, sizeof(out->routing));
    out->routing.enabled = 1;            /* inert until fast+strong resolve */
    out->routing.escalate_on_verify = 1; /* highest-value trigger */
    out->routing.escalate_on_error = 0;
    out->routing.escalate_on_stall = 1;  /* a stall => this model can't serve */
    out->routing.escalate_on_context = JC_ROUTE_CONTEXT_PCT; /* M288: run out of
                                          * room => prefer a wider window over
                                          * discarding history */
    /* Retrieval (M60): hybrid fusion is pure/local/fast, so on even in lite;
     * query rewrite costs a model call, so off by default. */
    out->retrieval.hybrid = -1;          /* -1 auto (=on); explicit key wins  */
    out->retrieval.query_rewrite = JC_QR_OFF;
    out->retrieval.rrf_k = 0;            /* 0 => built-in 60                  */
    /* Tool profile (M74): auto => core when the context is small or lite. */
    out->tool_profile = -1;
    /* Auto-context (M61): opt-in, off by default (conservative). */
    out->auto_context = 0;
    out->auto_context_top_k = 0;         /* 0 => default 5                    */
    out->auto_context_max_tokens = 0;    /* 0 => built-in default             */
    out->auto_context_sources = JC_ACTX_BOTH;
    out->learn_on_stop = 0;              /* opt-in (M71)                      */
    /* Timeout tiers default to "unset" (-1) so the built-in defaults apply
     * unless the config/CLI sets them; memset would leave 0 (= disabled). */
    out->timeouts.connect = out->timeouts.stall = out->timeouts.request = -1;
    out->timeouts_cli.connect = out->timeouts_cli.stall =
        out->timeouts_cli.request = -1;
    out->permissions.allow_all = 0;
    out->permissions.deny_all = 0;
    out->default_mode = JC_MODE_CHAT;
    out->system_prompt_extra = NULL;
    out->max_tool_iters = lite ? 12 : 25;
    out->max_retries = lite ? 2 : 4;
    out->max_subagent_depth = lite ? 0 : 2;
    out->max_subagent_iters = lite ? 12 : 25;
    out->auto_compact = 1;
    out->craft = lite ? 0 : 1;  /* M299 on; M318 measured it off under lite */
    out->voice = 0;             /* M303: opt-in (needs a TTS backend) */
    out->budget_panel = 0;      /* M431f: opt-in, so M347's decision is measured */
    out->budget_panel_every = 5;/* cadence; 0 would silently disable the feature */
    out->context_limit = lite ? 16384 : 0;
    out->snapshots = lite ? 0 : 1;
    out->snapshot_limit = 100;
    /* M599: metrics ON by default -- the operator's decision (2026-08-27): "a
     * learner forgets otherwise". Measured that day: no telemetry existed for
     * jichi's own workspace, so `learn analyze` had nothing to read. `metrics`
     * carries no prompt, response or code (TELEMETRY.md), so the privacy posture
     * does not move; `full` stays opt-in and `--log-level off` / `--log -` /
     * `"logging":{"level":"off"}` turn it off. */
    out->log_level = JC_EVENTLOG_METRICS;
    out->log_path = NULL;
    /* Tool output caps: 0 => the tool's built-in default; lite shrinks them. */
    out->read_max_bytes = lite ? 65536 : 0;
    out->run_max_bytes = lite ? 16384 : 0;
    out->fetch_max_bytes = lite ? 32768 : 0;
    out->search_max_bytes = lite ? 16384 : 0;
    out->git_max_bytes = lite ? 8192 : 0;
    out->image_gen_max_bytes = 0; /* 0 => the tool's built-in cap (M32) */
    out->audio_gen_max_bytes = 0;
    out->transcribe_max_bytes = 0; /* 0 => the tool's built-in cap (M33) */
    out->active = 0;
    out->from_defaults = 0;

    if (inline_json != NULL) {
        /* Inline config (--config-json): parse the given text as THE config;
         * a single explicit source, no global/project merge (like --config). */
        root = jc_json_parse(inline_json);
        if (root == NULL) {
            jc_logf(JC_LOG_ERROR, "--config-json: invalid JSON");
            return JC_ERR_PARSE;
        }
        jc_snprintf(out->config_sources, sizeof out->config_sources,
                    "inline (--config-json)");
    } else {
    /* File-based discovery: explicit --config/$JC_CONFIG, else global+project. */
    {
        const char *jcenv = getenv("JC_CONFIG");
        explicit_path = (path_or_null != NULL && path_or_null[0] != '\0') ||
                        (jcenv != NULL && jcenv[0] != '\0');
    }

    if (explicit_path) {
        /* Explicit --config / $JC_CONFIG: exactly one file, no global+project
         * merge (a predictable, self-contained override). */
        resolve_path(path_or_null, path, sizeof(path));
        if (!jc_file_exists(path)) {
            jc_logf(JC_LOG_ERROR, "config file not found: %s", path);
            return JC_ERR_NOTFOUND;
        }
        root = config_parse_file(path, a);
        if (root == NULL) {
            return JC_ERR_PARSE;
        }
        jc_snprintf(out->config_sources, sizeof out->config_sources,
                    "%s", path);
    } else {
        /* Merge the global config (~/.jichi) with a project config
         * (local/config.json, else .jichi/config.json): the project overlays the
         * global -- scalar keys win, list keys (models/docs/aliases/...) union
         * with project entries first (so a project model/alias takes
         * precedence). Either file alone works; neither => built-in defaults. */
        char ppath[1024];
        cJSON *proj = NULL;
        int have_g;
        int have_p = 0;
        jc_snprintf(path, sizeof(path), "%s/.jichi", jc_home_dir());
        if (jc_file_exists("local/config.json")) {
            jc_snprintf(ppath, sizeof ppath, "%s", "local/config.json");
            have_p = 1;
        } else if (jc_file_exists(".jichi/config.json")) {
            jc_snprintf(ppath, sizeof ppath, "%s", ".jichi/config.json");
            have_p = 1;
        }
        have_g = jc_file_exists(path);
        if (!have_g && !have_p) {
            jc_logf(JC_LOG_DEBUG, "no config file; using defaults");
            out->from_defaults = 1;
            push_model(out, NULL, a);
            jc_config_set_active(out, 0);
            jc_snprintf(out->config_sources, sizeof out->config_sources,
                        "built-in defaults");
            return JC_OK;
        }
        root = have_g ? config_parse_file(path, a) : NULL;
        proj = have_p ? config_parse_file(ppath, a) : NULL;
        if (root != NULL && proj != NULL) {
            jc_config_merge_json(root, proj);
            cJSON_Delete(proj);
            jc_snprintf(out->config_sources, sizeof out->config_sources,
                        "%s + %s (merged)", path, ppath);
        } else if (root == NULL && proj != NULL) {
            root = proj;
            jc_snprintf(out->config_sources, sizeof out->config_sources,
                        "%s", ppath);
        } else if (root != NULL) {
            jc_snprintf(out->config_sources, sizeof out->config_sources,
                        "%s", path);
        }
        if (root == NULL) {
            /* Present but none usable: malformed JSON, unreadable, or a
             * directory where a file must be. config_parse_file has already
             * named which, per path. */
            return JC_ERR_PARSE;
        }
    }
    } /* end of file-based acquisition (inline_json == NULL) */

    /* A "models" array takes precedence; otherwise a singular "model"; failing
     * both, a built-in default. */
    models = jc_json_get_obj(root, "models");
    if (cJSON_IsArray(models) && cJSON_GetArraySize(models) > 0) {
        cJSON_ArrayForEach(model, models) {
            push_model(out, model, a);
        }
    } else {
        model = jc_json_get_obj(root, "model");
        push_model(out, model, a); /* model may be NULL => default */
    }

    /* Lean-profile resolution (M272): an explicit CLI flag wins outright
     * (JC_LITE_HINT_ON/_OFF), else an explicit config "lowResource" key, else
     * the auto-detection hint (JC_LITE_HINT_AUTO, main.c's low-RAM check).
     * The key used to be OR-ed with the hint, so "lowResource": false could
     * not veto auto-lite -- a heuristic overriding the user's explicit word.
     * From here on, `lite` shifts each resource-heavy key's *default* while
     * an explicitly-set key still wins (jc_json_get_* returns the explicit
     * value). */
    {
        int key = jc_json_get_bool_lenient(root, "lowResource", -1);
        if (low_resource == JC_LITE_HINT_ON) {
            lite = 1;
        } else if (low_resource == JC_LITE_HINT_OFF) {
            lite = 0;
        } else if (key != -1) {
            lite = key;
        } else {
            lite = (low_resource == JC_LITE_HINT_AUTO);
        }
    }
    out->low_resource = lite;

    {
        const char *extra = jc_json_get_str(root, "systemPrompt", NULL);
        if (extra != NULL) {
            out->system_prompt_extra = jc_arena_strdup(a, extra);
        }
    }
    out->max_tool_iters = (int)jc_json_get_num(root, "maxToolIters",
                                               lite ? 12.0 : 25.0);
    if (out->max_tool_iters <= 0) {
        out->max_tool_iters = lite ? 12 : 25;
    }
    out->max_retries = (int)jc_json_get_num(root, "maxRetries", lite ? 2.0 : 4.0);
    if (out->max_retries < 0) {
        out->max_retries = 0;
    }
    out->auto_compact = jc_json_get_bool_lenient(root, "autoCompact", 1);
    /* M318: OFF under --lite, measured. The M299 section costs 329-386 tokens on
     * EVERY model call (4-6% of a small prompt), and an 18-run A/B on a 31B local
     * model found ZERO difference: same pass rate on three graded tasks (18/18
     * both ways, including the design task), and on an under-specified probe no
     * design note, no named alternative, no behavioural difference in either
     * condition -- while costing +15% to +69% per run. The claimed value is about
     * larger models and larger tasks, neither of which a lite run is doing, so
     * this joins repoMap/references/markdown as a resource-heavy default the lean
     * tier drops. An explicit `craft` key still wins, in both directions.
     * See docs/analysis/2026-08-06-craft-ab.md. */
    out->craft = jc_json_get_bool_lenient(root, "craft", lite ? 0 : 1);
    out->voice = jc_json_get_bool_lenient(root, "voice", 0);
    out->repo_map = jc_json_get_bool_lenient(root, "repoMap", lite ? 0 : 1);
    out->references = jc_json_get_bool_lenient(root, "references", lite ? 0 : 1);
    out->markdown = jc_json_get_bool_lenient(root, "markdown", lite ? 0 : 1);
    /* M257: OFF by default, and the reason is a user-experience judgement, not
     * a resource one. While the agent works jichi cannot guarantee your typing
     * is visible in every window (see docs/TYPE_AHEAD.md) -- and input you
     * cannot see is input you cannot correct or compose against. Opt in per
     * project with `typeAhead: true`, or per run with --type-ahead. */
    out->type_ahead = jc_json_get_bool_lenient(root, "typeAhead", 0);
    out->wisdom = jc_json_get_bool_lenient(root, "wisdom", 1);
    out->board = jc_json_get_bool_lenient(root, "board", 0);
    out->fuzzy_edit = jc_json_get_bool_lenient(root, "fuzzyEdit", 1);
    out->assignments = jc_json_get_bool_lenient(root, "assignments", 0);
    if (jc_json_get_obj(root, "selfReview") != NULL) {
        out->self_review = jc_json_get_bool_lenient(root, "selfReview", 0) ? 1 : 0;
    }
    if (jc_json_get_obj(root, "pathFence") != NULL) {
        out->path_fence = jc_json_get_bool_lenient(root, "pathFence", 0) ? 1 : 0;
    }
    if (jc_json_get_obj(root, "promptCache") != NULL) {
        out->prompt_cache = jc_json_get_bool_lenient(root, "promptCache", 1) ? 1 : 0;
    }
    if (jc_json_get_obj(root, "costModel") != NULL) {
        out->cost_model = jc_json_get_bool_lenient(root, "costModel", 1) ? 1 : 0;
    }
    {
        const char *ttl = jc_json_get_str(root, "promptCacheTtl", NULL);
        out->prompt_cache_1h = (ttl != NULL && strcmp(ttl, "1h") == 0) ? 1 : 0;
    }
    out->repo_map_limit = (long)jc_json_get_num(root, "repoMapLimit", 0.0);
    if (out->repo_map_limit < 0) {
        out->repo_map_limit = 0;
    }
    out->context_limit = (long)jc_json_get_num(root, "contextLimit",
                                               lite ? 16384.0 : 0.0);
    ctx_limit_explicit = (cJSON_GetObjectItem(root, "contextLimit") != NULL);
    if (out->context_limit < 0) {
        out->context_limit = 0;
    }
    out->snapshots = jc_json_get_bool_lenient(root, "snapshots", lite ? 0 : 1);
    out->snapshot_limit = (int)jc_json_get_num(root, "snapshotLimit", 100.0);
    if (out->snapshot_limit < 0) {
        out->snapshot_limit = 0;
    }
    /* Tool output caps (0 => the tool's built-in default); lite shrinks them. */
    out->read_max_bytes =
        (long)jc_json_get_num(root, "readMaxBytes", lite ? 65536.0 : 0.0);
    out->run_max_bytes =
        (long)jc_json_get_num(root, "runMaxBytes", lite ? 16384.0 : 0.0);
    out->fetch_max_bytes =
        (long)jc_json_get_num(root, "fetchMaxBytes", lite ? 32768.0 : 0.0);
    out->search_max_bytes =
        (long)jc_json_get_num(root, "searchMaxBytes", lite ? 16384.0 : 0.0);
    out->git_max_bytes =
        (long)jc_json_get_num(root, "gitMaxBytes", lite ? 8192.0 : 0.0);
    out->image_gen_max_bytes =
        (long)jc_json_get_num(root, "imageGenMaxBytes", 0.0);
    out->audio_gen_max_bytes =
        (long)jc_json_get_num(root, "audioGenMaxBytes", 0.0);
    out->transcribe_max_bytes =
        (long)jc_json_get_num(root, "transcribeMaxBytes", 0.0);
    if (out->read_max_bytes < 0) { out->read_max_bytes = 0; }
    if (out->run_max_bytes < 0) { out->run_max_bytes = 0; }
    if (out->fetch_max_bytes < 0) { out->fetch_max_bytes = 0; }
    if (out->search_max_bytes < 0) { out->search_max_bytes = 0; }
    if (out->git_max_bytes < 0) { out->git_max_bytes = 0; }
    if (out->image_gen_max_bytes < 0) { out->image_gen_max_bytes = 0; }
    if (out->audio_gen_max_bytes < 0) { out->audio_gen_max_bytes = 0; }
    if (out->transcribe_max_bytes < 0) { out->transcribe_max_bytes = 0; }

    /* Optional event-logging / telemetry sink (M21): "logging":{level,path}. */
    {
        cJSON *lg = jc_json_get_obj(root, "logging");
        if (cJSON_IsObject(lg)) {
            const char *lvl = jc_json_get_str(lg, "level", NULL);
            const char *lpath = jc_json_get_str(lg, "path", NULL);
            if (lvl != NULL) {
                int v = jc_eventlog_level_parse(lvl);
                out->log_level = (v >= 0) ? v : 0;
            }
            if (lpath != NULL) {
                out->log_path = jc_arena_strdup(a, lpath);
            }
        }
    }
    out->max_subagent_depth =
        (int)jc_json_get_num(root, "maxSubagentDepth", lite ? 0.0 : 2.0);
    if (out->max_subagent_depth < 0) {
        out->max_subagent_depth = 0;
    }
    out->max_subagent_iters = (int)jc_json_get_num(root, "maxSubagentIters",
                                                   (double)out->max_tool_iters);
    if (out->max_subagent_iters <= 0) {
        out->max_subagent_iters = out->max_tool_iters;
    }

    {
        cJSON *servers = jc_json_get_obj(root, "mcpServers");
        cJSON *srv;
        if (cJSON_IsArray(servers)) {
            cJSON_ArrayForEach(srv, servers) {
                push_mcp_server(out, srv, a);
            }
        } else if (servers != NULL) {
            /* M395: an ARRAY of entries, each with a "name". Claude Code and
             * Continue use an OBJECT keyed by server name, so that is what a
             * user copies from the internet -- and it used to be ignored in
             * silence: no servers, no tools, no warning, nothing to search for.
             * A configured-but-ignored server is worse than an absent one. */
            jc_logf(JC_LOG_WARN,
                    "mcpServers must be an ARRAY of {name, command, ...} "
                    "entries; got %s -- no MCP servers were configured (see "
                    "docs/MCP.md)",
                    cJSON_IsObject(servers) ? "an object (the Claude Code / "
                                              "Continue shape)" : "another type");
        }
    }

    {
        cJSON *servers = jc_json_get_obj(root, "lspServers");
        cJSON *srv;
        if (cJSON_IsArray(servers)) {
            cJSON_ArrayForEach(srv, servers) {
                push_lsp_server(out, srv, a);
            }
        } else if (servers != NULL) {
            /* M395: same trap, same silence -- see the mcpServers note above. */
            jc_logf(JC_LOG_WARN,
                    "lspServers must be an ARRAY of {language, command, ...} "
                    "entries -- no LSP servers were configured (see docs/LSP.md)");
        }
    }

    {
        cJSON *arr = jc_json_get_obj(root, "docs");
        cJSON *d;
        if (cJSON_IsArray(arr)) {
            cJSON_ArrayForEach(d, arr) {
                push_docs(out, d, a);
            }
        }
    }

    {
        cJSON *arr = jc_json_get_obj(root, "aliases");
        cJSON *d;
        if (cJSON_IsArray(arr)) {
            cJSON_ArrayForEach(d, arr) {
                push_alias(out, d, a);
            }
        }
    }

    {
        cJSON *tools = jc_json_get_obj(root, "tools");
        cJSON *t;
        if (cJSON_IsArray(tools)) {
            cJSON_ArrayForEach(t, tools) {
                push_user_tool(out, t, a);
            }
        }
    }

    parse_hooks(out, jc_json_get_obj(root, "hooks"), a);
    out->hooks_enabled = jc_json_get_bool_lenient(root, "hooksEnabled", 0);
    out->config_editable = jc_json_get_bool_lenient(root, "configEditable", 0);
    out->mem_budget_mb = (long)jc_json_get_num(root, "memBudgetMb", 0.0);
    out->run_timeout = (long)jc_json_get_num(root, "runTimeout", 0.0);
    if (out->run_timeout < 0) { out->run_timeout = 0; }
    out->run_timeout_cli = 0; /* set from --run-timeout in main */
    out->accessible = jc_json_get_bool_lenient(root, "accessible", 0);
    out->output_style = jc_json_dup_str(root, "outputStyle", a);
    out->language = jc_json_dup_str(root, "language", a);

    {
        cJSON *search = jc_json_get_obj(root, "search");
        if (cJSON_IsObject(search)) {
            const char *s = jc_json_get_str(search, "url", NULL);
            const char *lit;
            const char *envn;
            out->search.url = (s != NULL) ? jc_arena_strdup(a, s) : NULL;
            s = jc_json_get_str(search, "provider", NULL);
            out->search.provider = (s != NULL) ? jc_arena_strdup(a, s) : NULL;
            out->search.max_results =
                (long)jc_json_get_num(search, "maxResults", 0.0);
            /* Key: literal apiKey, else getenv(apiKeyEnv). */
            lit = jc_json_get_str(search, "apiKey", NULL);
            if (lit != NULL && lit[0] != '\0') {
                out->search.api_key = jc_arena_strdup(a, lit);
            } else {
                envn = jc_json_get_str(search, "apiKeyEnv", NULL);
                if (envn != NULL) {
                    const char *v = getenv(envn);
                    out->search.api_key_env = jc_arena_strdup(a, envn);
                    if (v != NULL && v[0] != '\0') {
                        out->search.api_key = jc_arena_strdup(a, v);
                    }
                }
            }
        }
    }

    {
        cJSON *perm = jc_json_get_obj(root, "permissions");
        const char *mode_s;
        enum jc_agent_mode m;
        if (cJSON_IsObject(perm)) {
            parse_policy_field(perm, "allow", &out->permissions.allow,
                               &out->permissions.allow_all, a);
            parse_policy_field(perm, "deny", &out->permissions.deny,
                               &out->permissions.deny_all, a);
        }
        mode_s = jc_json_get_str(root, "mode", NULL);
        if (mode_s != NULL && jc_agent_mode_parse(mode_s, &m)) {
            out->default_mode = (int)m;
        }
        parse_str_array(jc_json_get_obj(root, "instructions"),
                        &out->instructions, a);
        out->verify = jc_json_dup_str(root, "verify", a);
        out->verify_kind = jc_json_dup_str(root, "verifyKind", a);
        out->test_command = jc_json_dup_str(root, "testCommand", a);
        out->pdf_command = jc_json_dup_str(root, "pdfCommand", a);
        out->format_command = jc_json_dup_str(root, "formatCommand", a);
        out->time_format = jc_json_dup_str(root, "timeFormat", a);
        {
            /* numberFormat: "off"/"none" => no grouping; "auto"/absent => the OS
             * locale's separator (else '.'); any other value => its first char
             * (".", ",", " "). Keeps token counts readable + locale-aware. */
            const char *nf = jc_json_get_str(root, "numberFormat", NULL);
            if (nf == NULL || strcmp(nf, "auto") == 0) {
                char loc = jc_locale_group_sep();
                out->group_sep = (loc != 0) ? loc : '.';
            } else if (strcmp(nf, "off") == 0 || strcmp(nf, "none") == 0) {
                out->group_sep = 0;
            } else {
                out->group_sep = nf[0];
            }
        }
        out->notify = jc_json_dup_str(root, "notify", a);
        out->notify_bell = jc_json_get_bool_lenient(root, "notifyBell",
                                            out->notify_bell);
        parse_str_array(jc_json_get_obj(root, "editScope"),
                        &out->edit_scope, a);
        parse_str_array(jc_json_get_obj(root, "ignoreDirs"),
                        &out->ignore_dirs, a);
        out->revert_out_of_scope =
            jc_json_get_bool_lenient(root, "revertOutOfScope", 0); /* M142 */
        out->budget_panel =
            jc_json_get_bool_lenient(root, "budgetPanel", 0);      /* M431f: OFF */
        out->budget_panel_every =
            (int)jc_json_get_num(root, "budgetPanelEvery", 5.0); /* M431f */
        out->strict_green =
            jc_json_get_bool_lenient(root, "strictGreen", 0);      /* M332 */
        /* M338: tri-state, following pathFence/promptCache/selfReview. Unset
         * (-1) is NOT "off": it means each mechanism takes its own default,
         * because the risk differs. See jc_config.h. */
        if (jc_json_get_obj(root, "preserveDiscarded") != NULL) {
            out->preserve_discarded =
                jc_json_get_bool_lenient(root, "preserveDiscarded", 0) ? 1 : 0;
        }
        parse_str_array(jc_json_get_obj(root, "referenceRoots"),
                        &out->reference_roots, a);
        /* M462: `design` is a LIST, and the CLI appends to it rather than
         * replacing it -- see docs/DESIGN_INPUT.md for why replacement would
         * silently drop a project's pinned doc while still labelling the
         * section authoritative. */
        parse_str_array(jc_json_get_obj(root, "design"), &out->design_docs, a);
        /* M153/M154: privileged-command policy + allowlist + audit toggle. */
        {
            const char *pc = jc_json_get_str(root, "privilegedCommands",
                                             "ask");
            if (strcmp(pc, "deny") == 0) {
                out->privileged_commands = JC_PRIVPOL_DENY;
            } else if (strcmp(pc, "allow") == 0) {
                out->privileged_commands = JC_PRIVPOL_ALLOW;
            } else {
                if (strcmp(pc, "ask") != 0) {
                    jc_logf(JC_LOG_WARN, "config: privilegedCommands \"%s\" "
                            "unknown; using \"ask\"", pc);
                }
                out->privileged_commands = JC_PRIVPOL_ASK;
            }
            parse_str_array(jc_json_get_obj(root, "privilegedCommandsAllow"),
                            &out->privileged_allow, a);
            out->privileged_audit =
                jc_json_get_bool_lenient(root, "privilegedAudit", 1);
        }
        /* M163a: kinetic (physical-actuation) posture -- mirrors the M153
         * privileged block. Default ask => an unattended run refuses. */
        {
            const char *kc = jc_json_get_str(root, "kineticCommands", "ask");
            if (strcmp(kc, "deny") == 0) {
                out->kinetic_commands = JC_PRIVPOL_DENY;
            } else if (strcmp(kc, "allow") == 0) {
                out->kinetic_commands = JC_PRIVPOL_ALLOW;
            } else {
                if (strcmp(kc, "ask") != 0) {
                    jc_logf(JC_LOG_WARN, "config: kineticCommands \"%s\" "
                            "unknown; using \"ask\"", kc);
                }
                out->kinetic_commands = JC_PRIVPOL_ASK;
            }
            parse_str_array(jc_json_get_obj(root, "kineticCommandsAllow"),
                            &out->kinetic_allow, a);
            parse_str_array(jc_json_get_obj(root, "kineticShellPrefixes"),
                            &out->kinetic_prefixes, a);
            out->kinetic_audit = jc_json_get_bool_lenient(root, "kineticAudit", 1);
        }
        /* M163b: sound I/O backends (config "sound"). */
        {
            const cJSON *snd = jc_json_get_obj(root, "sound");
            if (cJSON_IsObject(snd)) {
                const cJSON *pl = jc_json_get_obj(snd, "play");
                const cJSON *rc = jc_json_get_obj(snd, "record");
                if (cJSON_IsObject(pl)) {
                    const char *s = jc_json_get_str(pl, "command", NULL);
                    out->sound.play_command =
                        (s != NULL) ? jc_arena_strdup(a, s) : NULL;
                    s = jc_json_get_str(pl, "shell", NULL);
                    out->sound.play_shell =
                        (s != NULL) ? jc_arena_strdup(a, s) : NULL;
                    parse_str_array(jc_json_get_obj(pl, "args"),
                                    &out->sound.play_args, a);
                }
                if (cJSON_IsObject(rc)) {
                    const char *s = jc_json_get_str(rc, "command", NULL);
                    out->sound.record_command =
                        (s != NULL) ? jc_arena_strdup(a, s) : NULL;
                    s = jc_json_get_str(rc, "shell", NULL);
                    out->sound.record_shell =
                        (s != NULL) ? jc_arena_strdup(a, s) : NULL;
                    parse_str_array(jc_json_get_obj(rc, "args"),
                                    &out->sound.record_args, a);
                }
                out->sound.play_timeout =
                    (long)jc_json_get_num(snd, "playTimeoutSeconds", 0.0);
                out->sound.record_max =
                    (long)jc_json_get_num(snd, "recordMaxSeconds", 0.0);
            }
        }
        /* M159: `control` opens the mid-run control socket -- boolean true
         * (default path) or a string socket path. Off by default. */
        {
            const cJSON *cv = jc_json_get_obj(root, "control");
            if (cJSON_IsBool(cv)) {
                out->control_on = cJSON_IsTrue(cv) ? 1 : 0;
            } else if (cJSON_IsString(cv) && cv->valuestring != NULL &&
                       cv->valuestring[0] != '\0') {
                out->control_on = 1;
                out->control_path = jc_arena_strdup(a, cv->valuestring);
            }
        }
        out->max_parallel_agents =
            (int)jc_json_get_num(root, "maxParallelAgents", lite ? 1.0 : 0.0);
        out->parallel_verify =
            jc_json_get_bool_lenient(root, "parallelVerify", 0); /* M144 */
        if (out->max_parallel_agents < 0) {
            out->max_parallel_agents = 0;
        }
        out->parallel_task_timeout =
            (long)jc_json_get_num(root, "parallelTaskTimeout", 0.0);
        if (out->parallel_task_timeout < 0) {
            out->parallel_task_timeout = 0;
        }
        out->daemon_workers =
            (int)jc_json_get_num(root, "daemonWorkers", 0.0);
        if (out->daemon_workers < 0) {
            out->daemon_workers = 0;
        }
        out->daemon_worker_timeout =
            (long)jc_json_get_num(root, "daemonWorkerTimeout", 0.0);
        if (out->daemon_worker_timeout < 0) {
            out->daemon_worker_timeout = 0;
        }
        {
            cJSON *route = jc_json_get_obj(root, "routing");
            if (cJSON_IsObject(route)) {
                out->routing.enabled =
                    jc_json_get_bool_lenient(route, "enabled", 1);
                out->routing.fast = jc_json_dup_str(route, "fast", a);
                out->routing.strong = jc_json_dup_str(route, "strong", a);
                out->routing.escalate_on_verify =
                    jc_json_get_bool_lenient(route, "escalateOnVerify", 1);
                out->routing.escalate_on_error =
                    jc_json_get_bool_lenient(route, "escalateOnError", 0);
                out->routing.escalate_on_stall =
                    jc_json_get_bool_lenient(route, "escalateOnStall", 1);
                /* M288: accepts a percentage, or a bool for convenience --
                 * `false` disables, `true` selects the default threshold. */
                {
                    cJSON *ec = cJSON_GetObjectItem(route,
                                                    "escalateOnContext");
                    if (cJSON_IsBool(ec)) {
                        out->routing.escalate_on_context =
                            cJSON_IsTrue(ec) ? JC_ROUTE_CONTEXT_PCT : 0;
                    } else if (ec != NULL) {
                        out->routing.escalate_on_context =
                            (int)jc_json_get_num_lenient(route,
                                                         "escalateOnContext",
                                                         JC_ROUTE_CONTEXT_PCT);
                    }
                }
            }
        }
        /* Retrieval tuning (M60): hybrid lexical+dense fusion + query rewrite. */
        {
            cJSON *ret = jc_json_get_obj(root, "retrieval");
            if (cJSON_IsObject(ret)) {
                if (jc_json_get_obj(ret, "hybrid") != NULL) {
                    out->retrieval.hybrid =
                        jc_json_get_bool_lenient(ret, "hybrid", 1) ? 1 : 0;
                }
                out->retrieval.rrf_k =
                    (int)jc_json_get_num(ret, "rrfK", 0.0);
                {
                    const char *qr = jc_json_get_str(ret, "queryRewrite", NULL);
                    if (qr != NULL) {
                        if (strcmp(qr, "hyde") == 0) {
                            out->retrieval.query_rewrite = JC_QR_HYDE;
                        } else if (strcmp(qr, "multiquery") == 0) {
                            out->retrieval.query_rewrite = JC_QR_MULTIQUERY;
                        } else {
                            out->retrieval.query_rewrite = JC_QR_OFF;
                        }
                    }
                }
            }
        }
        /* Tool profile (M74): "auto" (default) / "core" / "full". */
        {
            const char *tp = jc_json_get_str(root, "toolProfile", NULL);
            if (tp != NULL) {
                if (strcmp(tp, "core") == 0) {
                    out->tool_profile = 1;
                } else if (strcmp(tp, "full") == 0) {
                    out->tool_profile = 0;
                } else {
                    out->tool_profile = -1; /* "auto" or anything else */
                }
            }
        }
        /* Auto-context injection (M61): opt-in auto-RAG. */
        out->auto_context = jc_json_get_bool_lenient(root, "autoContext", 0);
        out->auto_context_top_k =
            (int)jc_json_get_num(root, "autoContextTopK", 0.0);
        out->auto_context_max_tokens =
            (long)jc_json_get_num(root, "autoContextMaxTokens", 0.0);
        {
            const char *src = jc_json_get_str(root, "autoContextSources", NULL);
            if (src != NULL) {
                if (strcmp(src, "codebase") == 0) {
                    out->auto_context_sources = JC_ACTX_CODEBASE;
                } else if (strcmp(src, "docs") == 0) {
                    out->auto_context_sources = JC_ACTX_DOCS;
                } else {
                    out->auto_context_sources = JC_ACTX_BOTH;
                }
            }
        }
        out->learn_on_stop = jc_json_get_bool_lenient(root, "learnOnStop", 0);
        /* Global model-call timeouts (M22); per-model blocks override this. */
        parse_timeouts(jc_json_get_obj(root, "timeouts"), &out->timeouts);
    }

    cJSON_Delete(root);
    jc_config_set_active(out, 0);
    /* Lite CAPS the context budget at 16384 and that is deliberate:
     * LOW_MEMORY.md publishes the number as part of the tier, and the
     * 965 MB Archos row and the Pi boards depend on it. But
     * effective_limit() (jc_compact.c) prefers this top-level number over
     * the active model's contextLength, so a model DECLARING a larger
     * window was budgeted small with nothing said -- a DEFAULT outranking
     * an EXPLICIT declaration, silently. The cap stays; the silence goes
     * (M458). Only warn when the cap came from lite's default: an
     * explicit contextLimit is the operator's own decision.
     */
    if (lite && !ctx_limit_explicit && out->context_limit > 0 &&
        out->model.context_limit > out->context_limit) {
        jc_logf(JC_LOG_WARN,
                "config: lite caps the context budget at %ld tokens, but "
                "the active model declares contextLength %ld -- budgeting "
                "for %ld. Set \"contextLimit\" explicitly to raise it "
                "(see LOW_MEMORY.md).",
                out->context_limit, out->model.context_limit,
                out->context_limit);
    }
    return JC_OK;
}

jc_status jc_config_load(const char *path_or_null, int low_resource,
                         struct jc_config *out, struct jc_arena *a)
{
    return config_load_impl(path_or_null, NULL, low_resource, out, a);
}

jc_status jc_config_load_json(const char *json_text, int low_resource,
                              struct jc_config *out, struct jc_arena *a)
{
    if (json_text == NULL || json_text[0] == '\0') {
        return JC_ERR_INVALID;
    }
    return config_load_impl(NULL, json_text, low_resource, out, a);
}

void jc_config_free(struct jc_config *c)
{
    jc_size i;
    for (i = 0; i < c->mcp_servers.len; i++) {
        struct jc_mcp_server_cfg *s =
            (struct jc_mcp_server_cfg *)jc_vec_at(&c->mcp_servers, i);
        jc_vec_free(&s->args);
        jc_vec_free(&s->env);
        jc_vec_free(&s->headers);
        jc_vec_free(&s->auto_approve);
        jc_vec_free(&s->deny);
    }
    jc_vec_free(&c->mcp_servers);
    for (i = 0; i < c->lsp_servers.len; i++) {
        struct jc_lsp_server_cfg *s =
            (struct jc_lsp_server_cfg *)jc_vec_at(&c->lsp_servers, i);
        jc_vec_free(&s->args);
        jc_vec_free(&s->extensions);
    }
    jc_vec_free(&c->lsp_servers);
    for (i = 0; i < c->user_tools.len; i++) {
        struct jc_user_tool_cfg *t =
            (struct jc_user_tool_cfg *)jc_vec_at(&c->user_tools, i);
        jc_vec_free(&t->args);
        jc_vec_free(&t->env);
    }
    jc_vec_free(&c->user_tools);
    jc_vec_free(&c->docs); /* entries are arena strings; just free the vec (M34a) */
    {
        int hi;
        for (hi = 0; hi < JC_HOOK_EVENT_COUNT; hi++) {
            struct jc_vec *ev = &c->hooks.events[hi];
            jc_size mi;
            for (mi = 0; mi < ev->len; mi++) {
                struct jc_hook_matcher_cfg *mc =
                    (struct jc_hook_matcher_cfg *)jc_vec_at(ev, mi);
                jc_size ci;
                for (ci = 0; ci < mc->commands.len; ci++) {
                    struct jc_hook_cmd_cfg *cc =
                        (struct jc_hook_cmd_cfg *)jc_vec_at(&mc->commands, ci);
                    jc_vec_free(&cc->args);
                }
                jc_vec_free(&mc->commands);
            }
            jc_vec_free(ev);
        }
    }
    jc_vec_free(&c->privileged_allow); /* M153 */
    jc_vec_free(&c->kinetic_allow);    /* M163a */
    jc_vec_free(&c->kinetic_prefixes);
    jc_vec_free(&c->sound.play_args);  /* M163b */
    jc_vec_free(&c->sound.record_args);
    jc_vec_free(&c->permissions.allow);
    jc_vec_free(&c->permissions.deny);
    jc_vec_free(&c->instructions);
    jc_vec_free(&c->edit_scope);
    jc_vec_free(&c->ignore_dirs);
    jc_vec_free(&c->design_docs);
    jc_vec_free(&c->reference_roots);
    jc_vec_free(&c->models);
}

int jc_config_model_count(const struct jc_config *c)
{
    return (int)c->models.len;
}

struct jc_model_cfg *jc_config_model_at(struct jc_config *c, int i)
{
    if (i < 0 || (jc_size)i >= c->models.len) {
        return NULL;
    }
    return (struct jc_model_cfg *)jc_vec_at(&c->models, (jc_size)i);
}

int jc_config_cost_model_on(int cfg, int prompt_cache_on)
{
    if (cfg == 0 || cfg == 1) {
        return cfg;             /* an explicit choice wins outright */
    }
    return prompt_cache_on ? 0 : 1;
}

void jc_config_resolve_prompt_cache(struct jc_config *c)
{
    jc_size i;
    int g = c->prompt_cache; /* global tri-state */
    if (c == NULL) {
        return;
    }
    for (i = 0; i < c->models.len; i++) {
        struct jc_model_cfg *m =
            (struct jc_model_cfg *)jc_vec_at(&c->models, i);
        m->prompt_cache = (m->prompt_cache_cfg != -1) ? m->prompt_cache_cfg
                        : (g != -1 ? g : 1);
        m->prompt_cache_1h = c->prompt_cache_1h; /* global TTL choice (M31e) */
    }
    c->model.prompt_cache = (c->model.prompt_cache_cfg != -1)
                          ? c->model.prompt_cache_cfg
                          : (g != -1 ? g : 1);
    c->model.prompt_cache_1h = c->prompt_cache_1h;
}

jc_status jc_config_set_active(struct jc_config *c, int i)
{
    struct jc_model_cfg *m = jc_config_model_at(c, i);
    if (m == NULL) {
        return JC_ERR_INVALID;
    }
    c->active = i;
    c->model = *m; /* shallow copy; strings are arena-owned and shared */
    jc_config_resolve_prompt_cache(c); /* keep effective prompt_cache current */
    return JC_OK;
}

/* Case-insensitive substring test. */
static int ci_contains(const char *hay, const char *needle)
{
    jc_size hn, nn, i, j;
    if (hay == NULL || needle == NULL) {
        return 0;
    }
    hn = strlen(hay);
    nn = strlen(needle);
    if (nn == 0) {
        return 1;
    }
    if (nn > hn) {
        return 0;
    }
    for (i = 0; i + nn <= hn; i++) {
        for (j = 0; j < nn; j++) {
            int a = tolower((unsigned char)hay[i + j]);
            int b = tolower((unsigned char)needle[j]);
            if (a != b) {
                break;
            }
        }
        if (j == nn) {
            return 1;
        }
    }
    return 0;
}

const struct jc_alias_cfg *jc_config_find_alias(const struct jc_config *c,
                                                const char *name)
{
    jc_size i;
    if (c == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < c->aliases.len; i++) {
        const struct jc_alias_cfg *al =
            (const struct jc_alias_cfg *)jc_vec_at((struct jc_vec *)&c->aliases,
                                                   i);
        if (al->name != NULL && strcmp(al->name, name) == 0) {
            return al;
        }
    }
    return NULL;
}

/* An all-decimal selector is read as a 1-based index. Shared by
 * jc_config_find_model and jc_config_selector_check so the lint cannot drift
 * from the resolver it is meant to predict. */
static int sel_all_digits(const char *s)
{
    const char *p = s;
    if (p == NULL || *p == '\0') {
        return 0;
    }
    while (*p != '\0') {
        if (*p < '0' || *p > '9') {
            return 0;
        }
        p++;
    }
    return 1;
}

int jc_config_find_model(const struct jc_config *c, const char *selector)
{
    int i;
    int n = jc_config_model_count(c);

    if (selector == NULL || selector[0] == '\0') {
        return -1;
    }
    /* All-decimal selector => 1-based index. */
    if (sel_all_digits(selector)) {
        int idx = atoi(selector) - 1;
        if (idx >= 0 && idx < n) {
            return idx;
        }
        return -1;
    }
    /* Otherwise match a substring of the name or model id. */
    for (i = 0; i < n; i++) {
        struct jc_model_cfg *m =
            (struct jc_model_cfg *)jc_vec_at((struct jc_vec *)&c->models,
                                             (jc_size)i);
        if (ci_contains(m->name, selector) ||
            ci_contains(m->model, selector)) {
            return i;
        }
    }
    return -1;
}

enum jc_selector_status jc_config_selector_check(const struct jc_config *c,
                                                const char *selector,
                                                int *nmatch)
{
    int i;
    int n;
    int hits = 0;
    unsigned role;

    if (nmatch != NULL) {
        *nmatch = 0;
    }
    /* An absent selector means "use the active model": always valid. */
    if (selector == NULL || selector[0] == '\0') {
        return JC_SEL_OK;
    }
    n = jc_config_model_count(c);
    if (sel_all_digits(selector)) {
        int idx = atoi(selector) - 1;
        return (idx >= 0 && idx < n) ? JC_SEL_OK : JC_SEL_NONE;
    }
    for (i = 0; i < n; i++) {
        struct jc_model_cfg *m =
            (struct jc_model_cfg *)jc_vec_at((struct jc_vec *)&c->models,
                                             (jc_size)i);
        if (ci_contains(m->name, selector) ||
            ci_contains(m->model, selector)) {
            hits++;
        }
    }
    if (nmatch != NULL) {
        *nmatch = hits;
    }
    if (hits == 1) {
        return JC_SEL_OK;
    }
    if (hits > 1) {
        return JC_SEL_AMBIGUOUS;
    }
    /* No name/id match: the resolvers fall back to reading it as a role. */
    role = jc_config_role_flag(selector);
    if (role != 0u) {
        return (jc_config_find_by_role(c, role) >= 0) ? JC_SEL_OK
                                                      : JC_SEL_ROLE_EMPTY;
    }
    return JC_SEL_NONE;
}

int jc_config_find_by_role(const struct jc_config *c, unsigned role)
{
    int i;
    int n = jc_config_model_count(c);
    for (i = 0; i < n; i++) {
        struct jc_model_cfg *m =
            (struct jc_model_cfg *)jc_vec_at((struct jc_vec *)&c->models,
                                             (jc_size)i);
        if ((m->roles & role) != 0u) {
            return i;
        }
    }
    return -1;
}

void jc_config_models_for_role_list(const struct jc_config *c, unsigned role,
                                    struct jc_sb *sb)
{
    int i;
    int n = jc_config_model_count(c);
    for (i = 0; i < n; i++) {
        struct jc_model_cfg *m =
            (struct jc_model_cfg *)jc_vec_at((struct jc_vec *)&c->models,
                                             (jc_size)i);
        const char *id;
        if ((m->roles & role) == 0u) {
            continue;
        }
        id = (m->name != NULL && m->name[0] != '\0') ? m->name : m->model;
        jc_sb_append(sb, id != NULL ? id : "(unnamed)");
        if (m->description != NULL && m->description[0] != '\0') {
            jc_sb_append(sb, " \xe2\x80\x94 "); /* em dash */
            jc_sb_append(sb, m->description);
        }
        jc_sb_append_char(sb, '\n');
    }
}

/* Resolve a routing selector (name/index, else a role name) to a model index. */
static int routing_resolve_one(const struct jc_config *c, const char *sel)
{
    int idx;
    unsigned role;

    if (sel == NULL || sel[0] == '\0') {
        return -1;
    }
    idx = jc_config_find_model(c, sel);
    if (idx >= 0) {
        return idx;
    }
    role = jc_config_role_flag(sel);
    if (role != 0u) {
        return jc_config_find_by_role(c, role);
    }
    return -1;
}

int jc_config_tool_profile_core(const struct jc_config *c, long context_limit)
{
    if (c == NULL) {
        return 0;
    }
    if (c->tool_profile == 1) {
        return 1;                /* explicit core */
    }
    if (c->tool_profile == 0) {
        return 0;                /* explicit full */
    }
    /* auto: core when lite, or when the budget is known and small. */
    if (c->low_resource) {
        return 1;
    }
    return (context_limit > 0 && context_limit < JC_TOOL_PROFILE_AUTO_BELOW)
           ? 1 : 0;
}

long jc_config_effective_max_tokens(long configured, long context_limit)
{
    long v;
    if (configured > 0) {
        return configured;
    }
    if (context_limit > 0) {
        /* ~1/5 of the window: stays under the ~80% compaction target, so
         * max_tokens + prompt still fits and the proxy won't 400. Bounded so a
         * huge context doesn't request an absurd cap and a tiny one still gets a
         * usable amount. */
        v = context_limit / 5;
        if (v > 16384) {
            v = 16384;
        }
        if (v < 512) {
            v = 512;
        }
        return v;
    }
    return JC_DEFAULT_MAX_TOKENS;
}

int jc_config_context_escalate(long est_tokens, long fast_limit,
                               long strong_limit, int pct)
{
    if (pct <= 0 || fast_limit <= 0 || strong_limit <= 0) {
        return 0;                       /* disabled, or a window is unknown */
    }
    if (strong_limit <= fast_limit) {
        return 0;                       /* no room to gain: never escalate */
    }
    /* Compare in tokens*100 to stay in integer arithmetic. Both sides are
     * bounded by real context windows (~1e6 at the extreme), so the products
     * cannot overflow a 32-bit long. */
    return (est_tokens * 100L >= fast_limit * (long)pct) ? 1 : 0;
}

int jc_config_context_deescalate(long est_tokens, long fast_limit, int pct)
{
    int down;

    if (pct <= 0 || fast_limit <= 0) {
        return 0;                       /* disabled, or the window is unknown */
    }
    down = pct - JC_ROUTE_DEESCALATE_GAP;
    if (down <= 0) {
        return 0;                       /* gap swallows the threshold: never */
    }
    /* Same integer arithmetic and the same bounds argument as the escalate side,
     * so the two thresholds are directly comparable and 55% really is below 75%. */
    return (est_tokens * 100L <= fast_limit * (long)down) ? 1 : 0;
}

int jc_config_routing_resolve(const struct jc_config *c, int *fast_idx,
                              int *strong_idx)
{
    int f;
    int s;

    if (fast_idx != NULL) {
        *fast_idx = -1;
    }
    if (strong_idx != NULL) {
        *strong_idx = -1;
    }
    if (c == NULL || !c->routing.enabled) {
        return 0;
    }
    f = routing_resolve_one(c, c->routing.fast);
    s = routing_resolve_one(c, c->routing.strong);
    if (f < 0 || s < 0 || f == s) {
        return 0; /* unresolvable or not distinct => routing inert */
    }
    if (fast_idx != NULL) {
        *fast_idx = f;
    }
    if (strong_idx != NULL) {
        *strong_idx = s;
    }
    return 1;
}

int jc_config_fallback_chain(const struct jc_config *c, int idx,
                             const unsigned char *reachable, int *out)
{
    int n = jc_config_model_count(c);
    int cur = idx;
    int hops;

    if (out != NULL) {
        *out = idx;
    }
    if (c == NULL || idx < 0 || idx >= n) {
        return 0;
    }
    /* Bounded by the model count so a fallback cycle can't loop forever. */
    for (hops = 0; hops <= n; hops++) {
        struct jc_model_cfg *m;
        if (cur < 0 || cur >= n) {
            break;
        }
        if (reachable == NULL || reachable[cur]) {
            if (out != NULL) {
                *out = cur;
            }
            return 1;
        }
        m = (struct jc_model_cfg *)jc_vec_at((struct jc_vec *)&c->models,
                                             (jc_size)cur);
        if (m->fallback == NULL || m->fallback[0] == '\0') {
            break;
        }
        cur = routing_resolve_one(c, m->fallback);
    }
    if (out != NULL) {
        *out = idx;
    }
    return 0;
}

struct jc_model_cfg *jc_config_model_for_role(struct jc_config *c,
                                              unsigned role)
{
    int i = jc_config_find_by_role(c, role);
    if (i < 0) {
        return NULL;
    }
    return jc_config_model_at(c, i);
}

double jc_config_cost(const struct jc_model_cfg *m, double in_tok,
                      double out_tok, double cache_read, double cache_write)
{
    double read_rate;
    double write_rate;
    if (m == NULL || (m->input_cost <= 0.0 && m->output_cost <= 0.0 &&
                      m->cache_read_cost <= 0.0 && m->cache_write_cost <= 0.0)) {
        return 0.0;
    }
    /* Cached reads/writes bill at their own rate, falling back to the input
     * rate when unpriced (the common case -- cached tokens are simply input). */
    read_rate = (m->cache_read_cost > 0.0) ? m->cache_read_cost : m->input_cost;
    write_rate = (m->cache_write_cost > 0.0) ? m->cache_write_cost
                                             : m->input_cost;
    return (in_tok / 1000000.0) * m->input_cost +
           (cache_read / 1000000.0) * read_rate +
           (cache_write / 1000000.0) * write_rate +
           (out_tok / 1000000.0) * m->output_cost;
}

jc_size jc_config_cap(long configured, jc_size builtin)
{
    return configured > 0 ? (jc_size)configured : builtin;
}

long jc_config_run_timeout(long per_call, long cli, long cfg)
{
    if (per_call > 0) {
        return per_call;
    }
    if (cli > 0) {
        return cli;
    }
    return cfg > 0 ? cfg : 0;
}

/* First tier that is set (>= 0), else the built-in default. */
static long pick_timeout(long cli, long model_v, long global_v, long builtin)
{
    if (cli >= 0) {
        return cli;
    }
    if (model_v >= 0) {
        return model_v;
    }
    if (global_v >= 0) {
        return global_v;
    }
    return builtin;
}

void jc_config_resolve_timeouts(const struct jc_config *c,
                                const struct jc_model_cfg *m,
                                long *connect, long *stall, long *request)
{
    struct jc_timeouts_cfg cli, glob, mod;

    cli.connect = cli.stall = cli.request = -1;
    glob.connect = glob.stall = glob.request = -1;
    mod.connect = mod.stall = mod.request = -1;
    if (c != NULL) {
        cli = c->timeouts_cli;
        glob = c->timeouts;
    }
    if (m != NULL) {
        mod = m->timeouts;
    }
    if (connect != NULL) {
        *connect = pick_timeout(cli.connect, mod.connect, glob.connect,
                                JC_HTTP_CONNECT_TIMEOUT_DEFAULT);
    }
    if (stall != NULL) {
        *stall = pick_timeout(cli.stall, mod.stall, glob.stall,
                              JC_HTTP_STALL_TIMEOUT_DEFAULT);
    }
    if (request != NULL) {
        /* No hard overall cap by default (0 => unbounded; stall guards hangs). */
        *request = pick_timeout(cli.request, mod.request, glob.request, 0L);
    }
}
