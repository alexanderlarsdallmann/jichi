/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_setup.c - setup-wizard pure core (see jc_setup.h). */

#include "jc_setup.h"
#include "jc_json.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <stdlib.h>
#include <string.h>

/* ----- role presets -------------------------------------------------------
 *
 * Each references an EXISTING jc_scaffold pack (no asset duplication). devops
 * and data map to the closest existing pack for now; M48 phase 5 adds dedicated
 * `devops`/`data` packs and repoints these two. */
static const struct jc_setup_preset PRESETS[] = {
    { "developer",
      "Write + edit code: language pack, LSP, snapshots, test command.",
      "default", NULL, NULL,
      JC_SF_SNAPSHOTS | JC_SF_LSP | JC_SF_TESTCMD | JC_SF_REFERENCES,
      JC_SETUP_TUI, 1, JC_AXIS_ROLE, NULL },
    { "technical-writer",
      "Documentation: audience-aware writer/proofreader agents + retrieval.",
      "docs", NULL, NULL,
      JC_SF_REFERENCES | JC_SF_DOCS | JC_SF_EMBED,
      JC_SETUP_TUI, 0, JC_AXIS_ROLE, NULL },
    { "tester",
      "Working on the tests: test command + verify gate, autonomous "
      "fix-forward.",
      "default", NULL, NULL,
      JC_SF_TESTCMD | JC_SF_VERIFY | JC_SF_SNAPSHOTS | JC_SF_TELEMETRY,
      JC_SETUP_TEST, 1, JC_AXIS_JOURNEY,
      "the verifier gate lets an unattended run fix its own failures" },
    { "reviewer",
      "Reviewing rather than changing: plan mode + architecture agents.",
      "systems-analysis", NULL, "plan",
      JC_SF_SNAPSHOTS,
      JC_SETUP_PLAN, 0, JC_AXIS_JOURNEY,
      "plan mode means it investigates and proposes, and changes nothing" },
    { "generic",
      "Minimal, language-agnostic baseline.",
      "default", NULL, NULL,
      0u,
      JC_SETUP_TUI, 0, JC_AXIS_ROLE, NULL },
    { "devops",
      "Ops/CI: shell + deploy review, test command, hooks.",
      "devops", NULL, NULL,
      JC_SF_TESTCMD | JC_SF_HOOKS | JC_SF_SNAPSHOTS,
      JC_SETUP_TUI, 0, JC_AXIS_ROLE, NULL },
    { "support",
      "User support: support-responder + bugfix-explainer, retrieval.",
      "docs", NULL, NULL,
      JC_SF_REFERENCES | JC_SF_DOCS,
      JC_SETUP_TUI, 0, JC_AXIS_ROLE, NULL },
    { "data",
      "Data/analysis: semantic search over the codebase, plan-lean.",
      "data", NULL, NULL,
      JC_SF_EMBED | JC_SF_REFERENCES,
      JC_SETUP_PLAN, 0, JC_AXIS_ROLE, NULL },
    { "small-local",
      "Small local model (7-14B): lean core tools, small context, snapshots.",
      "default", NULL, NULL,
      JC_SF_LOWRES | JC_SF_SNAPSHOTS | JC_SF_ROUTING,
      JC_SETUP_TUI, 1, JC_AXIS_MACHINE,
      "a small model needs a lean tool set and a short prompt, or it "
      "spends its window on instructions instead of your task" },
    { "constrained",
      "The MACHINE is small, old, or embedded (the model may be large).",
      "default", NULL, NULL,
      JC_SF_LEANHOST | JC_SF_SNAPSHOTS,
      JC_SETUP_TUI, 0, JC_AXIS_MACHINE,
      "keeps jichi's own footprint down -- one agent at a time, lean tools -- "
      "while leaving your model's context window alone" },
    { "existing-tree",
      "You must build against code you do not own and must not change.",
      "default", NULL, NULL,
      JC_SF_REFERENCES | JC_SF_SNAPSHOTS,
      JC_SETUP_TUI, 0, JC_AXIS_MACHINE,
      "the tree is added read-only, so it can be read and searched but never "
      "edited, even by an unattended run" },
    /* The two teaching roles (curriculum C1). Both scaffold the assignments
     * pack and turn the feature on -- before these, a student's first contact
     * was three commands plus a hand-edit of JSON. `learner` keeps chat mode
     * and snapshots (the /undo safety net is the first thing the curriculum
     * teaches); asks_language is 0 for BOTH because the assignments pack IS
     * the point -- the language-pack swap would replace it. */
    { "learner",
      "Study with jichi as tutor: assignments + hints, snapshots, chat mode.",
      "assignments", NULL, NULL,
      JC_SF_ASSIGN | JC_SF_SNAPSHOTS | JC_SF_TELEMETRY,
      JC_SETUP_TUI, 0, JC_AXIS_STANCE,
      "jichi explains and sets exercises rather than just doing the work" },
    { "instructor",
      "Author + grade assignments: writer/checker agents, rubrics, snapshots.",
      "assignments", NULL, NULL,
      JC_SF_ASSIGN | JC_SF_SNAPSHOTS | JC_SF_REFERENCES | JC_SF_TELEMETRY,
      JC_SETUP_TUI, 0, JC_AXIS_STANCE,
      "you write the assignments and the reference solutions; jichi grades" },
    { "composer",
      "Music development: LilyPond notation + MIDI + Ardour workflows; the "
      "engraving gate as verify.",
      "music", NULL, NULL,
      JC_SF_SNAPSHOTS,
      JC_SETUP_TUI, 0, JC_AXIS_ROLE, NULL },
    /* The five project JOURNEYS (M183) -- orthogonal to the roles above:
     * not who you are, but what you are walking into. Interactive setup
     * prints them under their own heading. */
    { "small-project",
      "Journey: start a small project -- verify + snapshots + your language "
      "pack.",
      "default", NULL, NULL,
      JC_SF_SNAPSHOTS | JC_SF_TESTCMD | JC_SF_VERIFY | JC_SF_TELEMETRY,
      JC_SETUP_TUI, 1, JC_AXIS_JOURNEY,
      "a verifier and snapshots from the start, so mistakes are cheap" },

    { "contributor",
      "Journey: bug-fix an existing project (plan mode; reproduce -> failing "
      "test -> minimal diff -> PR).",
      "contributor", NULL, "plan",
      JC_SF_SNAPSHOTS | JC_SF_REFERENCES,
      JC_SETUP_PLAN, 0, JC_AXIS_JOURNEY,
      "reproduce first, then a failing test, then the smallest diff" },

    { "refactor",
      "Journey: refactor an existing codebase under green tests (verify "
      "required, small steps).",
      "refactor", NULL, NULL,
      JC_SF_SNAPSHOTS | JC_SF_TESTCMD | JC_SF_VERIFY | JC_SF_TELEMETRY,
      JC_SETUP_TUI, 0, JC_AXIS_JOURNEY,
      "the tests stay green at every step; behaviour changes get recorded" },

    { "rewrite",
      "Journey: port a codebase to another language (the old tree becomes a "
      "read-only referenceRoots entry).",
      "rewrite", NULL, NULL,
      JC_SF_SNAPSHOTS | JC_SF_REFERENCES | JC_SF_TESTCMD | JC_SF_VERIFY |
          JC_SF_TELEMETRY,
      JC_SETUP_TUI, 0, JC_AXIS_JOURNEY,
      "the old tree is added read-only, so it can be consulted but never edited" },

    { "architect",
      "Journey: build a complex project from the beginning -- the full SDLC "
      "pack, documents before code.",
      "sdlc", NULL, "plan",
      JC_SF_SNAPSHOTS | JC_SF_TESTCMD | JC_SF_VERIFY | JC_SF_REFERENCES |
          JC_SF_DOCS | JC_SF_TELEMETRY,
      JC_SETUP_PLAN, 0, JC_AXIS_JOURNEY,
      "documents before code: requirements, use cases, design, API" }
};

int jc_setup_preset_count(void)
{
    return (int)(sizeof(PRESETS) / sizeof(PRESETS[0]));
}

const struct jc_setup_preset *jc_setup_preset_at(int i)
{
    if (i < 0 || i >= jc_setup_preset_count()) {
        return NULL;
    }
    return &PRESETS[i];
}

const struct jc_setup_preset *jc_setup_find_preset(const char *name)
{
    int i;
    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < jc_setup_preset_count(); i++) {
        if (strcmp(PRESETS[i].name, name) == 0) {
            return &PRESETS[i];
        }
    }
    return NULL;
}

/* ----- answers ------------------------------------------------------------ */

void jc_setup_answers_init(struct jc_setup_answers *a)
{
    memset(a, 0, sizeof(*a));
    a->snapshots = -1;
    a->references = -1;
    a->repo_map = -1;
    a->hooks = -1;
    a->assignments = -1;
}

void jc_setup_apply_preset(struct jc_setup_answers *a,
                           const struct jc_setup_preset *p)
{
    if (a == NULL || p == NULL) {
        return;
    }
    if (a->mode == NULL) {
        a->mode = p->mode;
    }
    if (a->output_style == NULL) {
        a->output_style = p->output_style;
    }
    if (p->features & JC_SF_SNAPSHOTS) a->snapshots = 1;
    if (p->features & JC_SF_ASSIGN) a->assignments = 1;
    if (p->features & JC_SF_REFERENCES) a->references = 1;
    if (p->features & JC_SF_HOOKS) a->hooks = 1;
    if ((p->features & JC_SF_TESTCMD) && a->test_command == NULL) {
        a->test_command = "make test";
    }
    if ((p->features & JC_SF_VERIFY) && a->verify == NULL) {
        a->verify = "make test";
    }
    if ((p->features & JC_SF_TELEMETRY) && a->log_level == NULL) {
        a->log_level = "metrics";
    }
    /* M150 small-local: the lean profile (lowResource => core tool set + small
     * per-tool caps + parallel 1) plus a conservative context window. Set at
     * ~half a typical 8k local window because the byte estimate under-counts
     * (M77 calibration then tightens it). The user's real contextLength/route
     * selectors still win; this is only the floor. */
    if (p->features & JC_SF_LOWRES) {
        if (a->low_resource <= 0) {
            a->low_resource = 1;
        }
        if (a->context_limit == 0) {
            a->context_limit = 6000;
        }
    }
    /* M326p: a small HOST, as opposed to a small model. Both want the lean
     * profile; only the small-model case wants a small context window, because
     * there it is the model that cannot use a big one. Capping parallel agents
     * at 1 is the host-side cost that matters: each one is a forked process
     * with its own libcurl handle and arena. */
    if (p->features & JC_SF_LEANHOST) {
        if (a->low_resource <= 0) {
            a->low_resource = 1;
        }
        a->max_parallel = 1;   /* before setup_apply_machine, which only fills
                                * max_parallel when it is still unset */
    }
    /* JC_SF_LSP / _DOCS / _EMBED / _WEB / _ROUTING describe *intent*; the
     * concrete entries (server commands, doc paths, model ids, urls) are filled
     * by the wizard/flags, so there's nothing to default here. */
}

void jc_setup_apply_complexity(struct jc_setup_answers *a, int complexity)
{
    if (a == NULL) return;
    if (complexity == JC_SETUP_BEGINNER) {
        /* Minimal + safe: keep the safety net, drop power-user surface. */
        a->snapshots = 1;
        a->references = 1;
        a->hooks = 0;
        a->log_level = NULL;   /* the default: metrics (M599), no key written */
        a->route_fast = NULL;  /* single model, no tiered routing */
        a->route_strong = NULL;
        a->search_url = NULL;  /* no web search */
        if (a->mode != NULL && strcmp(a->mode, "auto") == 0) {
            a->mode = NULL;    /* not autonomous by default for a beginner */
        }
    } else if (complexity == JC_SETUP_ADVANCED) {
        /* Everything sensible on (routing needs two real models -> left to the
         * preset / wizard). */
        a->snapshots = 1;
        a->references = 1;
        a->hooks = 1;
        if (a->log_level == NULL) a->log_level = "metrics";
        if (a->verify == NULL && a->test_command != NULL) {
            a->verify = a->test_command;
        }
    }
    /* JC_SETUP_STD: no change. */
}

int jc_setup_complexity_parse(const char *s, int *out)
{
    if (s == NULL || out == NULL) return 0;
    if (strcmp(s, "beginner") == 0) { *out = JC_SETUP_BEGINNER; return 1; }
    if (strcmp(s, "advanced") == 0) { *out = JC_SETUP_ADVANCED; return 1; }
    if (strcmp(s, "standard") == 0 || strcmp(s, "std") == 0) {
        *out = JC_SETUP_STD; return 1;
    }
    return 0;
}

/* ----- config builder ----------------------------------------------------- */

/* Append the comma/space-separated items of `list` as strings to cJSON `arr`. */
static void add_split(cJSON *arr, const char *list, char sep)
{
    const char *p = list;
    if (list == NULL) {
        return;
    }
    while (*p != '\0') {
        const char *start;
        char buf[256];
        jc_size n = 0;
        while (*p == sep || *p == ' ') {
            p++;
        }
        start = p;
        while (*p != '\0' && *p != sep) {
            p++;
        }
        n = (jc_size)(p - start);
        /* trim trailing spaces */
        while (n > 0 && start[n - 1] == ' ') {
            n--;
        }
        if (n > 0 && n < sizeof(buf)) {
            memcpy(buf, start, n);
            buf[n] = '\0';
            cJSON_AddItemToArray(arr, cJSON_CreateString(buf));
        }
    }
}

/* Build one model object {name?,provider,model,apiBase?,apiKeyEnv,roles}. */
static cJSON *model_obj(const char *name, const char *provider,
                        const char *model, const char *api_base,
                        const char *key_env, const char *const *roles,
                        int nroles, long context_length)
{
    cJSON *m = cJSON_CreateObject();
    int i;
    if (name != NULL && name[0] != '\0') {
        cJSON_AddStringToObject(m, "name", name);
    }
    cJSON_AddStringToObject(m, "provider",
                            provider != NULL ? provider : "openai");
    cJSON_AddStringToObject(m, "model", model != NULL ? model : "");
    if (api_base != NULL && api_base[0] != '\0') {
        cJSON_AddStringToObject(m, "apiBase", api_base);
    }
    /* The window jichi may fill with prompt. Written only when known:
     * a GUESS here would be indistinguishable from a measurement to
     * every later reader, including doctor. */
    if (context_length > 0) {
        cJSON_AddNumberToObject(m, "contextLength",
                                (double)context_length);
    }
    /* Secrets are never written: only the env-var name. */
    if (key_env != NULL && key_env[0] != '\0') {
        cJSON_AddStringToObject(m, "apiKeyEnv", key_env);
    }
    if (nroles > 0) {
        cJSON *r = cJSON_AddArrayToObject(m, "roles");
        for (i = 0; i < nroles; i++) {
            cJSON_AddItemToArray(r, cJSON_CreateString(roles[i]));
        }
    }
    return m;
}

/* True if any model in the `models` array declares `role`. */
static int models_have_role(cJSON *models, const char *role)
{
    cJSON *m;
    if (!cJSON_IsArray(models)) {
        return 0;
    }
    cJSON_ArrayForEach(m, models) {
        cJSON *roles = cJSON_GetObjectItem(m, "roles");
        cJSON *r;
        if (cJSON_IsArray(roles)) {
            cJSON_ArrayForEach(r, roles) {
                if (cJSON_IsString(r) && r->valuestring != NULL &&
                    strcmp(r->valuestring, role) == 0) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* Apply `a` onto `root`. When `merge`, existing top-level keys and models are
 * preserved (gap-fill); otherwise everything is written (fresh build). */
static void apply_answers(cJSON *root, const struct jc_setup_answers *a,
                          int merge)
{
    static const char *const CHAT_ROLES[] = { "chat", "edit", "apply" };
    static const char *const EMBED_ROLES[] = { "embed", "rerank" };
    cJSON *models = cJSON_GetObjectItem(root, "models");

#define WANT(k) (!merge || cJSON_GetObjectItemCaseSensitive(root, (k)) == NULL)

    if (!cJSON_IsArray(models)) {
        models = cJSON_AddArrayToObject(root, "models");
    }
    /* Add the chat model only when the answers actually carry one (they always do
     * for build/merge; the inherit path may omit it, relying on the source's
     * models) and, in merge mode, only if no model already holds the chat role. */
    if (a->provider != NULL && a->provider[0] != '\0' &&
        a->model != NULL && a->model[0] != '\0' &&
        (!merge || !models_have_role(models, "chat"))) {
        cJSON_AddItemToArray(models,
            model_obj(a->model_name, a->provider, a->model, a->api_base,
                      a->api_key_env, CHAT_ROLES, 3, a->context_length));
    }
    if (a->embed_model != NULL && a->embed_model[0] != '\0' &&
        (!merge || !models_have_role(models, "embed"))) {
        cJSON_AddItemToArray(models,
            model_obj("embed", a->provider, a->embed_model, a->api_base,
                      a->api_key_env, EMBED_ROLES, 2, 0));
    }

    if (a->mode != NULL && a->mode[0] != '\0' && WANT("mode")) {
        cJSON_AddStringToObject(root, "mode", a->mode);
    }
    if (a->output_style != NULL && a->output_style[0] != '\0' &&
        WANT("outputStyle")) {
        cJSON_AddStringToObject(root, "outputStyle", a->output_style);
    }
    if (a->snapshots >= 0 && WANT("snapshots")) {
        cJSON_AddBoolToObject(root, "snapshots", a->snapshots);
    }
    if (a->references >= 0 && WANT("references")) {
        cJSON_AddBoolToObject(root, "references", a->references);
    }
    if (a->repo_map >= 0 && WANT("repoMap")) {
        cJSON_AddBoolToObject(root, "repoMap", a->repo_map);
    }
    if (a->test_command != NULL && a->test_command[0] != '\0' &&
        WANT("testCommand")) {
        cJSON_AddStringToObject(root, "testCommand", a->test_command);
    }
    if (a->verify != NULL && a->verify[0] != '\0' && WANT("verify")) {
        cJSON_AddStringToObject(root, "verify", a->verify);
    }
    if (a->hooks >= 0 && WANT("hooksEnabled")) {
        cJSON_AddBoolToObject(root, "hooksEnabled", a->hooks);
    }
    if (a->assignments >= 0 && WANT("assignments")) {
        cJSON_AddBoolToObject(root, "assignments", a->assignments);
    }
    if (a->reference_root != NULL && a->reference_root[0] != '\0' &&
        WANT("referenceRoots")) {
        /* M183 (rewrite journey): the old tree, readable but never writable. */
        cJSON *rr = cJSON_AddArrayToObject(root, "referenceRoots");
        cJSON_AddItemToArray(rr, cJSON_CreateString(a->reference_root));
    }
    if (a->log_level != NULL && a->log_level[0] != '\0' && WANT("logging")) {
        cJSON *lg = cJSON_AddObjectToObject(root, "logging");
        cJSON_AddStringToObject(lg, "level", a->log_level);
    }
    if (((a->route_fast != NULL && a->route_fast[0] != '\0') ||
         (a->route_strong != NULL && a->route_strong[0] != '\0')) &&
        WANT("routing")) {
        cJSON *rt = cJSON_AddObjectToObject(root, "routing");
        cJSON_AddBoolToObject(rt, "enabled", 1);
        if (a->route_fast != NULL) {
            cJSON_AddStringToObject(rt, "fast", a->route_fast);
        }
        if (a->route_strong != NULL) {
            cJSON_AddStringToObject(rt, "strong", a->route_strong);
        }
    }
    if (a->search_url != NULL && a->search_url[0] != '\0' && WANT("search")) {
        cJSON *se = cJSON_AddObjectToObject(root, "search");
        cJSON_AddStringToObject(se, "url", a->search_url);
        if (a->search_key_env != NULL && a->search_key_env[0] != '\0') {
            cJSON_AddStringToObject(se, "apiKeyEnv", a->search_key_env);
        }
    }
    if (a->nlsp > 0 && WANT("lspServers")) {
        cJSON *arr = cJSON_AddArrayToObject(root, "lspServers");
        int i;
        for (i = 0; i < a->nlsp && i < JC_SETUP_MAX_LSP; i++) {
            cJSON *s = cJSON_CreateObject();
            cJSON *ext;
            cJSON_AddStringToObject(s, "name",
                a->lsp[i].name != NULL ? a->lsp[i].name : "lsp");
            cJSON_AddStringToObject(s, "command",
                a->lsp[i].command != NULL ? a->lsp[i].command : "");
            ext = cJSON_AddArrayToObject(s, "extensions");
            add_split(ext, a->lsp[i].extensions, ',');
            cJSON_AddItemToArray(arr, s);
        }
    }
    if (a->nmcp > 0 && WANT("mcpServers")) {
        cJSON *arr = cJSON_AddArrayToObject(root, "mcpServers");
        int i;
        for (i = 0; i < a->nmcp && i < JC_SETUP_MAX_MCP; i++) {
            cJSON *s = cJSON_CreateObject();
            cJSON *args;
            cJSON_AddStringToObject(s, "name",
                a->mcp[i].name != NULL ? a->mcp[i].name : "mcp");
            cJSON_AddStringToObject(s, "command",
                a->mcp[i].command != NULL ? a->mcp[i].command : "");
            args = cJSON_AddArrayToObject(s, "args");
            add_split(args, a->mcp[i].args, ' ');
            cJSON_AddItemToArray(arr, s);
        }
    }
    if (a->ndocs > 0 && WANT("docs")) {
        cJSON *arr = cJSON_AddArrayToObject(root, "docs");
        int i;
        for (i = 0; i < a->ndocs && i < JC_SETUP_MAX_DOCS; i++) {
            cJSON *s = cJSON_CreateObject();
            cJSON_AddStringToObject(s, "name",
                a->docs[i].name != NULL ? a->docs[i].name : "docs");
            cJSON_AddStringToObject(s, "path",
                a->docs[i].path != NULL ? a->docs[i].path : ".");
            cJSON_AddItemToArray(arr, s);
        }
    }
    /* Machine profile (from CPU/RAM detection). */
    if (a->sound_play != NULL && a->sound_play[0] != '\0' && WANT("sound")) {
        cJSON *snd = cJSON_AddObjectToObject(root, "sound");
        cJSON_AddStringToObject(snd, "play", a->sound_play);
    }
    if (a->notify_cmd != NULL && a->notify_cmd[0] != '\0' && WANT("notify")) {
        cJSON_AddStringToObject(root, "notify", a->notify_cmd);
    }
    if (a->max_parallel > 0 && WANT("maxParallelAgents")) {
        cJSON_AddNumberToObject(root, "maxParallelAgents",
                                (double)a->max_parallel);
    }
    if (a->low_resource > 0 && WANT("lowResource")) {
        cJSON_AddBoolToObject(root, "lowResource", 1);
    }
    if (a->context_limit > 0 && WANT("contextLimit")) {
        cJSON_AddNumberToObject(root, "contextLimit", (double)a->context_limit);
    }
#undef WANT
}

/* Serialize `root` (indented) into `out`; consumes `root`. */
static jc_status emit_config(cJSON *root, struct jc_sb *out)
{
    char *text = cJSON_Print(root);
    cJSON_Delete(root);
    if (text == NULL) {
        return JC_ERR_OOM;
    }
    jc_sb_append(out, text);
    jc_sb_append_char(out, '\n');
    free(text);
    return JC_OK;
}

jc_status jc_setup_build_config(const struct jc_setup_answers *a,
                                struct jc_sb *out)
{
    cJSON *root;

    if (a == NULL || a->provider == NULL || a->provider[0] == '\0' ||
        a->model == NULL || a->model[0] == '\0') {
        return JC_ERR_INVALID;
    }
    root = cJSON_CreateObject();
    if (root == NULL) {
        return JC_ERR_OOM;
    }
    cJSON_AddStringToObject(root, "_comment",
        "Generated by `jichi setup`. Secrets are read from the apiKeyEnv "
        "environment variable, never stored here. See docs/SETUP_WIZARD.md "
        "and docs/CONFIG_TUTORIAL.md.");
    apply_answers(root, a, 0);
    return emit_config(root, out);
}

jc_status jc_setup_merge_config(const char *existing_json,
                                const struct jc_setup_answers *a,
                                struct jc_sb *out)
{
    cJSON *root;

    if (a == NULL || a->provider == NULL || a->provider[0] == '\0' ||
        a->model == NULL || a->model[0] == '\0') {
        return JC_ERR_INVALID;
    }
    root = (existing_json != NULL) ? jc_json_parse(existing_json) : NULL;
    if (root == NULL || !cJSON_IsObject(root)) {
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return JC_ERR_PARSE;
    }
    apply_answers(root, a, 1);
    return emit_config(root, out);
}

/* CSV membership: is `name` one of the comma-separated tokens in `csv`
 * (whitespace-trimmed)? An empty/NULL csv matches nothing here (callers treat
 * that as "all keys" before calling). */
static int csv_has(const char *csv, const char *name)
{
    const char *p = csv;
    jc_size nlen = (jc_size)strlen(name);
    if (csv == NULL) {
        return 0;
    }
    while (*p != '\0') {
        const char *s;
        const char *e;
        while (*p == ' ' || *p == ',' || *p == '\t') p++;
        s = p;
        while (*p != '\0' && *p != ',') p++;
        e = p;
        while (e > s && (e[-1] == ' ' || e[-1] == '\t')) e--;
        if ((jc_size)(e - s) == nlen && strncmp(s, name, nlen) == 0) {
            return 1;
        }
    }
    return 0;
}

jc_status jc_setup_inherit_config(const char *source_json,
                                  const char *inherit_keys,
                                  const struct jc_setup_answers *a,
                                  struct jc_sb *out)
{
    cJSON *src;
    cJSON *root;

    if (a == NULL) {
        return JC_ERR_INVALID;
    }
    src = (source_json != NULL) ? jc_json_parse(source_json) : NULL;
    if (src == NULL || !cJSON_IsObject(src)) {
        if (src != NULL) {
            cJSON_Delete(src);
        }
        return JC_ERR_PARSE;
    }
    if (inherit_keys != NULL && inherit_keys[0] != '\0') {
        /* Subset: copy only the named top-level keys from the source. The
         * in-tree cJSON has no Duplicate, so each selected value is printed and
         * re-parsed into a fresh node. */
        cJSON *child;
        root = cJSON_CreateObject();
        for (child = src->child; child != NULL; child = child->next) {
            if (child->string != NULL && csv_has(inherit_keys, child->string)) {
                char *txt = cJSON_Print(child);
                cJSON *dup = (txt != NULL) ? cJSON_Parse(txt) : NULL;
                free(txt);
                if (dup != NULL) {
                    cJSON_AddItemToObject(root, child->string, dup);
                }
            }
        }
        cJSON_Delete(src);
    } else {
        /* Whole config: inherit everything from the source. */
        root = src;
    }
    cJSON_AddStringToObject(root, "_comment",
        "Generated by `jichi setup --from-global`: inherits the global config; "
        "runtime still merges ~/.jichi under this. apiKeyEnv only, never "
        "a literal key. See docs/SETUP_WIZARD.md.");
    apply_answers(root, a, 1); /* gap-fill: add only what's still missing */
    return emit_config(root, out);
}

/* ----- start-script builder ----------------------------------------------- */

const char *jc_setup_script_name(const struct jc_setup_preset *preset)
{
    int prof = (preset != NULL) ? preset->profile : JC_SETUP_TUI;
    switch (prof) {
    case JC_SETUP_PLAN: return "review.sh";
    case JC_SETUP_AUTO: return "task.sh";
    case JC_SETUP_TEST: return "test.sh";
    default:            return "run.sh";
    }
}

void jc_setup_start_script(const struct jc_setup_preset *preset,
                           const char *config_path, struct jc_sb *out)
{
    int prof = (preset != NULL) ? preset->profile : JC_SETUP_TUI;
    const char *cfg = (config_path != NULL) ? config_path : "local/config.json";

    jc_sb_append(out, "#!/bin/sh\n");
    jc_sb_append(out, "# Generated by `jichi setup`");
    if (preset != NULL) {
        jc_sb_append_fmt(out, " (preset: %s)", preset->name);
    }
    jc_sb_append(out, ".\n");
    jc_sb_append(out,
        "# jichi is expected on PATH; set JICHI to a local build if not.\n");
    jc_sb_append(out, "JICHI=\"${JICHI:-jichi}\"\n");
    /* M326e: load the private key file if there is one. Guarded, so a machine
     * without it still gets a working script -- and unconditional, because the
     * script is generated before we know whether the user accepted the offer to
     * write one, and sourcing a file that does not exist must not be an error.
     * This is the step the wizard used to leave entirely to the user: it wrote
     * a config naming an env var and a script that never loaded it. */
    jc_sb_append(out,
        "# API keys live in a private file, never in this script or the config\n"
        "# (see docs/CONFIG_TUTORIAL.md section 1.4). Absent is fine.\n");
    jc_sb_append(out,
        "[ -f \"$HOME/.jichi.env\" ] && . \"$HOME/.jichi.env\"\n");
    jc_sb_append_fmt(out, "CONFIG=\"${JICHI_CONFIG:-%s}\"\n", cfg);
    switch (prof) {
    case JC_SETUP_PLAN:
        jc_sb_append(out,
            "exec \"$JICHI\" --config \"$CONFIG\" --plan \"$@\"\n");
        break;
    case JC_SETUP_AUTO:
        jc_sb_append(out,
            "exec \"$JICHI\" --config \"$CONFIG\" --auto \"$@\"\n");
        break;
    case JC_SETUP_TEST:
        jc_sb_append(out,
            "# Run the project's tests and fix failures autonomously.\n"
            "# The run is BOUNDED: the token budget arms the autonomy\n"
            "# envelope, so the model is briefed on its limits at takeoff\n"
            "# and warned at ~80%, and a run journal lands in\n");
        jc_sb_append(out,
            "# ~/.jichi.d/runs/ (read it back: `jichi runs`).\n"
            "# --verify-every banks a green checkpoint every 8 tool calls,\n"
            "# so a budget stop keeps the verified work. Tune to taste.\n"
            "exec \"$JICHI\" --config \"$CONFIG\" --auto \\\n"
            "  --budget-tokens 400k --verify-every 8 \\\n"
            "  -p \"${1:-run the tests and fix any failures}\"\n");
        break;
    default:
        jc_sb_append(out, "exec \"$JICHI\" --config \"$CONFIG\" \"$@\"\n");
        break;
    }
}

/* Lowercase a single ASCII char. */
static char lc(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

const char *jc_setup_lang_for_ext(const char *ext)
{
    static const struct { const char *ext; const char *pack; } MAP[] = {
        { "c",    "c-cli" },
        { "h",    "c-cli" },
        { "cc",   "c-cli" },
        { "cpp",  "c-cli" },
        { "cxx",  "c-cli" },
        { "hpp",  "c-cli" },
        { "py",   "python-cli" },
        { "zig",  "zig-cli" },
        { "gd",   "godot" },
        { "rs",   "rust-cli" },
        { "go",   "go-cli" },
        { "ts",   "web-ts" },
        { "tsx",  "web-ts" },
        { "js",   "web-ts" },
        { "jsx",  "web-ts" },
        { "mjs",  "web-ts" }
    };
    char low[16];
    int i;
    int n = (int)(sizeof(MAP) / sizeof(MAP[0]));
    jc_size j;

    if (ext == NULL || ext[0] == '\0') {
        return NULL;
    }
    for (j = 0; ext[j] != '\0' && j < sizeof(low) - 1; j++) {
        low[j] = lc(ext[j]);
    }
    low[j] = '\0';
    for (i = 0; i < n; i++) {
        if (strcmp(low, MAP[i].ext) == 0) {
            return MAP[i].pack;
        }
    }
    return NULL;
}

void jc_setup_preset_sets(const struct jc_setup_preset *p, char *buf,
                          jc_size cap)
{
    /* Rendered FROM the feature bitmask, never hand-written beside it: a
     * second list of "what this preset does" is a list that rots, and this
     * project has paid for that twice (M262's tool table, M285's fences).
     * Whatever jc_setup_apply_preset acts on is what appears here. */
    static const struct { unsigned bit; const char *key; } KEYS[] = {
        { JC_SF_SNAPSHOTS,  "\"snapshots\"" },
        { JC_SF_TESTCMD,    "\"testCommand\"" },
        { JC_SF_VERIFY,     "\"verify\"" },
        { JC_SF_REFERENCES, "\"references\"" },
        { JC_SF_LSP,        "\"lspServers\"" },
        { JC_SF_EMBED,      "an \"embed\" model" },
        { JC_SF_DOCS,       "\"docs\"" },
        { JC_SF_WEB,        "\"search\"" },
        { JC_SF_HOOKS,      "\"hooksEnabled\"" },
        { JC_SF_ROUTING,    "\"routing\"" },
        { JC_SF_TELEMETRY,  "\"logging\"" },
        { JC_SF_LOWRES,     "\"lowResource\" + \"contextLimit\"" },
        { JC_SF_LEANHOST,   "\"lowResource\" + \"maxParallelAgents\": 1" },
        { JC_SF_ASSIGN,     "\"assignments\"" },
        { 0, 0 }
    };
    jc_size n = 0;
    int i;
    if (buf == NULL || cap == 0) {
        return;
    }
    buf[0] = '\0';
    if (p == NULL) {
        return;
    }
    if (p->mode != NULL && p->mode[0] != '\0') {
        n += (jc_size)jc_snprintf(buf + n, cap - n, "\"mode\": \"%s\"",
                                  p->mode);
    }
    for (i = 0; KEYS[i].key != 0 && n + 2 < cap; i++) {
        if ((p->features & KEYS[i].bit) == 0) {
            continue;
        }
        if (n > 0) {
            n += (jc_size)jc_snprintf(buf + n, cap - n, ", ");
        }
        n += (jc_size)jc_snprintf(buf + n, cap - n, "%s", KEYS[i].key);
    }
    if (n == 0) {
        jc_snprintf(buf, cap, "nothing beyond the model itself");
    }
}
