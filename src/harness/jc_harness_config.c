/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_harness_config.c - the harness configuration loader (M1051, H1a).
 *
 * Ports the split between the reference drive's two config sources:
 * harness/drive.env (KEY=value settings, incl. the three secret references
 * ZD_KEY_FILE / ZD_ENV_FILE / ZD_TOKEN_FILE) and harness/config.json (the jichi
 * config, which the harness references but does NOT parse here). D13 renames
 * the harness's own settings to the JICHI_HARNESS_ prefix; this file carries
 * the three names as constants in jc_harness_config.h.
 *
 * The port keeps every behaviour the design note names (its eleven-line list):
 * a credential is a REFERENCE (git-credential-from-file's model: the token is
 * read at the moment git asks, never at load); a credential that holds a
 * value is refused (the config must stay committable and shareable); an
 * unknown credential type or server kind is refused; a tenant with a non-local
 * push target but no branch prefix is refused; booleans are read leniently
 * (M519: a strict read once silently disabled a fence); an env setting wins
 * over its config twin; and a malformed config is a jc_status, never a crash.
 * The loader never opens a secret file and never runs git (behaviour 11):
 * H1a carries no secret at all, so there is nothing to leak yet.
 */

#include "jc_harness_config.h"

#include "jc_json.h"
#include "jc_snprintf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- small helpers -------------------------------------------------------- */

/* Duplicate a string into the arena; NULL in -> NULL out. */
static char *
dup_in_arena(struct jc_arena *a, const char *s)
{
    if (s == NULL) {
        return NULL;
    }
    return jc_arena_strdup(a, s);
}

/* A non-empty string field, or NULL when absent/empty (absent and empty both
 * mean "not written"; refusing them is the caller's choice). */
static const char *
opt_str(const cJSON *o, const char *key)
{
    const cJSON *v = (o != NULL) ? cJSON_GetObjectItem(o, key) : NULL;
    if (v == NULL || !cJSON_IsString(v) || v->valuestring == NULL) {
        return NULL;
    }
    if (v->valuestring[0] == '\0') {
        return NULL;
    }
    return v->valuestring;
}

/* Refuse: record a non-JC_OK status and an arena message naming the problem.
 * behaviour 10: the message carries no secret -- every value that appears in
 * one is a reference, a name or a kind; behaviour 3 refuses a secret before it
 * is stored. */
static void
refuse(struct jc_harness_config *out, struct jc_arena *a, jc_status st,
       const char *msg)
{
    out->status = st;
    out->message = jc_arena_strdup(a, msg);
}

/* Build "prefix: msg" into arena (the credential/server/tenant the refusal
 * belongs to), replacing the message a sub-parser set. */
static void
refuse_named(struct jc_harness_config *out, struct jc_arena *a, jc_status st,
             const char *prefix, const char *msg)
{
    char buf[640];
    jc_snprintf(buf, sizeof buf, "%s: %s", prefix,
                (msg != NULL) ? msg : "(no message)");
    out->status = st;
    out->message = jc_arena_strdup(a, buf);
}

/* The six kinds (D4) are the universe. */
static int
kind_from_word(const char *w, jc_harness_kind *out)
{
    if (strcmp(w, "gitlab") == 0) {
        *out = JC_HARNESS_KIND_GITLAB;
        return 1;
    }
    if (strcmp(w, "github") == 0) {
        *out = JC_HARNESS_KIND_GITHUB;
        return 1;
    }
    if (strcmp(w, "gitea") == 0) {
        *out = JC_HARNESS_KIND_GITEA;
        return 1;
    }
    if (strcmp(w, "bitbucket") == 0) {
        *out = JC_HARNESS_KIND_BITBUCKET;
        return 1;
    }
    if (strcmp(w, "ssh") == 0) {
        *out = JC_HARNESS_KIND_SSH;
        return 1;
    }
    if (strcmp(w, "local") == 0) {
        *out = JC_HARNESS_KIND_LOCAL;
        return 1;
    }
    return 0;
}

/* behaviour 5: the kind fixes the user-name rule. */
jc_harness_user_name_rule
jc_harness_user_name_rule_of(jc_harness_kind kind)
{
    switch (kind) {
    case JC_HARNESS_KIND_GITLAB:
        return JC_HARNESS_RULE_OAUTH2;
    case JC_HARNESS_KIND_GITHUB:
        return JC_HARNESS_RULE_X_ACCESS_TOKEN;
    case JC_HARNESS_KIND_GITEA:
        return JC_HARNESS_RULE_GIT;
    case JC_HARNESS_KIND_BITBUCKET:
        return JC_HARNESS_RULE_BITBUCKET;
    case JC_HARNESS_KIND_SSH:
    case JC_HARNESS_KIND_LOCAL:
    default:
        return JC_HARNESS_RULE_NONE;
    }
}

/* behaviour 2 + 3 + 4: a credential is read by reference only. On failure the
 * working message (in `out`) names the problem, to be re-attributed by the
 * caller. A `token`/`key`/`password` string field is a secret verbatim and is
 * refused before it is stored, so the value is never copied into the arena. */
static jc_status
parse_credential(const cJSON *j, struct jc_harness_config *out,
                 struct jc_arena *a, struct jc_harness_credential *cred)
{
    const char *type;
    const char *ref;
    static const char *value_fields[3] = { "token", "key", "password" };
    int i;

    memset(cred, 0, sizeof *cred);

    type = opt_str(j, "type");
    if (type == NULL) {
        refuse(out, a, JC_ERR_INVALID,
               "credential has no \"type\"; the four reference types are "
               "tokenFile, env, helper, sshKey");
        return JC_ERR_INVALID;
    }

    for (i = 0; i < 3; i++) {
        const char *v = opt_str(j, value_fields[i]);
        if (v != NULL) {
            char msg[320];
            jc_snprintf(msg, sizeof msg,
                        "credential holds a secret verbatim in field "
                        "\"%s\"; the configuration never holds a secret; "
                        "name a reference (tokenFile/env/helper/sshKey), "
                        "not the value",
                        value_fields[i]);
            refuse(out, a, JC_ERR_INVALID, msg);
            return JC_ERR_INVALID;
        }
    }

    if (strcmp(type, "tokenFile") == 0) {
        ref = opt_str(j, "path");
        if (ref == NULL) {
            refuse(out, a, JC_ERR_INVALID,
                   "credential of type \"tokenFile\" has no \"path\"");
            return JC_ERR_INVALID;
        }
        cred->type = JC_HARNESS_CRED_TOKEN_FILE;
    } else if (strcmp(type, "env") == 0) {
        ref = opt_str(j, "name");
        if (ref == NULL) {
            refuse(out, a, JC_ERR_INVALID,
                   "credential of type \"env\" has no \"name\"");
            return JC_ERR_INVALID;
        }
        cred->type = JC_HARNESS_CRED_ENV;
    } else if (strcmp(type, "helper") == 0) {
        ref = opt_str(j, "command");
        if (ref == NULL) {
            refuse(out, a, JC_ERR_INVALID,
                   "credential of type \"helper\" has no \"command\"");
            return JC_ERR_INVALID;
        }
        cred->type = JC_HARNESS_CRED_HELPER;
    } else if (strcmp(type, "sshKey") == 0) {
        ref = opt_str(j, "path");
        if (ref == NULL) {
            refuse(out, a, JC_ERR_INVALID,
                   "credential of type \"sshKey\" has no \"path\"");
            return JC_ERR_INVALID;
        }
        cred->type = JC_HARNESS_CRED_SSH_KEY;
    } else {
        char msg[320];
        jc_snprintf(msg, sizeof msg,
                    "credential of unknown type \"%s\"; the four reference "
                    "types are tokenFile, env, helper, sshKey",
                    type);
        refuse(out, a, JC_ERR_INVALID, msg);
        return JC_ERR_INVALID;
    }

    cred->ref = jc_arena_strdup(a, ref);
    return JC_OK;
}

/* behaviour 6: one server element. */
static jc_status
parse_server(const cJSON *j, int idx, struct jc_harness_config *out,
             struct jc_arena *a)
{
    struct jc_harness_server *s = &out->servers[idx];
    char who[160];
    char msg[384];
    const char *name;
    const char *kind;
    const char *url;
    const cJSON *jc;
    jc_status st;

    memset(s, 0, sizeof *s);
    jc_snprintf(who, sizeof who, "server[%d]", idx);

    name = opt_str(j, "name");
    if (name == NULL) {
        refuse(out, a, JC_ERR_INVALID,
               "server has no \"name\"");
        refuse_named(out, a, JC_ERR_INVALID, who, out->message);
        return JC_ERR_INVALID;
    }
    s->name = dup_in_arena(a, name);
    jc_snprintf(who, sizeof who, "server \"%s\"", name);

    kind = opt_str(j, "kind");
    if (kind == NULL) {
        refuse_named(out, a, JC_ERR_INVALID, who,
                     "has no \"kind\"; the six kinds are gitlab, github, "
                     "gitea, bitbucket, ssh, local");
        return JC_ERR_INVALID;
    }
    if (!kind_from_word(kind, &s->kind)) {
        jc_snprintf(msg, sizeof msg,
                    "has unknown kind \"%s\"; the six kinds are gitlab, "
                    "github, gitea, bitbucket, ssh, local",
                    kind);
        refuse_named(out, a, JC_ERR_INVALID, who, msg);
        return JC_ERR_INVALID;
    }

    url = opt_str(j, "url");
    if (url == NULL) {
        refuse_named(out, a, JC_ERR_INVALID, who,
                     "has no \"url\"");
        return JC_ERR_INVALID;
    }
    /* behaviour 13: a `://` url whose authority part contains an `@`
     * (user:secret) is refused. An `@` in the authority IS userinfo, and
     * userinfo is credentials -- the config must stay committable and
     * shareable, so it never carries a secret verbatim. The message names the
     * rule (the CONCEPT "user:secret") and echoes NO part of the url or the
     * userinfo. A schemeless value (an ssh host, a local path) has no
     * authority to inspect and is accepted as-is. */
    {
        const char *sep;
        const char *auth;
        const char *limit;
        const char *at;
        sep = strstr(url, "://");
        if (sep != NULL) {
            auth = sep + 3;
            limit = strchr(auth, '/');
            if (limit == NULL) {
                limit = auth + strlen(auth);
            }
            at = auth;
            while (at < limit) {
                if (*at == '@') {
                    break;
                }
                at++;
            }
            if (at < limit) {
                refuse_named(out, a, JC_ERR_INVALID, who,
                             "url embeds credentials in its authority "
                             "(user:secret@...); credentials are by "
                             "reference only; strip the user:secret");
                return JC_ERR_INVALID;
            }
        }
    }
    s->url = dup_in_arena(a, url);

    jc = cJSON_GetObjectItem(j, "credential");
    if (jc != NULL) {
        if (!cJSON_IsObject(jc)) {
            refuse_named(out, a, JC_ERR_INVALID, who,
                         "has a \"credential\" that is not an object");
            return JC_ERR_INVALID;
        }
        st = parse_credential(jc, out, a, &s->credential);
        if (st != JC_OK) {
            refuse_named(out, a, st, who, out->message);
            return st;
        }
    }

    return JC_OK;
}

/* behaviour 7: one tenant element. Needs the server table to decide whether
 * the push target is local. */
static jc_status
parse_tenant(const cJSON *j, int idx, struct jc_harness_config *out,
             struct jc_arena *a)
{
    struct jc_harness_tenant *t = &out->tenants[idx];
    char who[160];
    char msg[384];
    const char *name;
    int i;
    int push_is_local = 0;

    memset(t, 0, sizeof *t);
    jc_snprintf(who, sizeof who, "tenant[%d]", idx);

    name = opt_str(j, "name");
    if (name == NULL) {
        refuse_named(out, a, JC_ERR_INVALID, who, "has no \"name\"");
        return JC_ERR_INVALID;
    }
    t->name = dup_in_arena(a, name);
    jc_snprintf(who, sizeof who, "tenant \"%s\"", name);

    t->home = dup_in_arena(a, opt_str(j, "home"));
    t->jichi_config = dup_in_arena(a, opt_str(j, "jichiConfig"));
    t->workdir = dup_in_arena(a, opt_str(j, "workdir"));
    t->mirror = dup_in_arena(a, opt_str(j, "mirror"));
    t->server = dup_in_arena(a, opt_str(j, "server"));
    t->push_target = dup_in_arena(a, opt_str(j, "pushTarget"));
    t->branch_prefix = dup_in_arena(a, opt_str(j, "branchPrefix"));

    if (t->push_target != NULL) {
        const char *target = t->push_target;
        int found = 0;
        for (i = 0; i < out->n_servers; i++) {
            if (strcmp(out->servers[i].name, target) == 0) {
                push_is_local = (out->servers[i].kind == JC_HARNESS_KIND_LOCAL);
                found = 1;
                break;
            }
        }
        if (!found) {
            jc_snprintf(msg, sizeof msg,
                        "pushes to unknown server \"%s\"", target);
            refuse_named(out, a, JC_ERR_INVALID, who, msg);
            return JC_ERR_INVALID;
        }
    }

    if (t->push_target != NULL && !push_is_local &&
        t->branch_prefix == NULL) {
        refuse_named(out, a, JC_ERR_INVALID, who,
                     "has a non-local push target but no \"branchPrefix\"; "
                     "the harness pushes a branch under a fixed prefix, "
                     "never with --force");
        return JC_ERR_INVALID;
    }

    return JC_OK;
}

/* behaviour 8 + the drive's identity. */
static jc_status
parse_drive(const cJSON *j, struct jc_harness_config *out, struct jc_arena *a)
{
    const cJSON *limits;
    const char *v;

    v = opt_str(j, "branchPrefix");
    if (v != NULL) {
        out->drive_branch_prefix = dup_in_arena(a, v);
    }
    v = opt_str(j, "end");
    if (v != NULL) {
        out->drive_end = dup_in_arena(a, v);
    }
    v = opt_str(j, "leadModel");
    if (v != NULL) {
        out->drive_lead_model = dup_in_arena(a, v);
    }
    /* drive.landingPolicy (M1202 D1): a WORD read as-is, NOT a lenient bool --
     * the gate decides the DEFAULT HOLD (D2), not the loader. The host_*
     * idiom: opt_str, NULL when the key is absent (no error), so the loader
     * invents nothing. */
    out->drive_landing_policy = dup_in_arena(a, opt_str(j, "landingPolicy"));

    limits = cJSON_GetObjectItem(j, "limits");
    if (limits != NULL) {
        if (!cJSON_IsObject(limits)) {
            refuse(out, a, JC_ERR_INVALID,
                   "\"drive.limits\" is not an object");
            return JC_ERR_INVALID;
        }
        out->limits_shell_fence =
            jc_json_get_bool_lenient(limits, "shellFence", 0);
        out->limits_path_fence =
            jc_json_get_bool_lenient(limits, "pathFence", 0);
        out->limits_disk_fence =
            jc_json_get_bool_lenient(limits, "diskFence", 0);
    }

    /* The team push's switch (behaviour 3, SV:2037.1): a lenient BOOL read
     * under `drive`, default TRUE (the reference's `TEAM_PUSH=${ZD_TEAM_PUSH:-1}`
     * -- push-on). The lenient reader answers 1 for an absent key and the
     * value for a present one, so this is the same as the reference's `:-1`:
     * an absent key keeps push-on, a `false` (or `0`) switches it off. When
     * FALSE the loop's save_team still fetches but skips the push. */
    out->drive_team_push = jc_json_get_bool_lenient(j, "teamPush", 1);

    /* The page/park block (M1213, H12, wave 1): the shimmable host commands
     * the page / page-watch / probe verbs consume. All optional, opt_str
     * as-is, NULL when absent -- the other host fields' idiom. The loader
     * does NOT refuse them: the commands decide. */
    out->drive_loop_unit = dup_in_arena(a, opt_str(j, "loopUnit"));
    out->drive_container_name = dup_in_arena(a, opt_str(j, "containerName"));
    /* The park/restart intervals (M1213, H12, T3, wave 2): all optional,
     * opt_str as-is, NULL when absent -- the other host fields' idiom.
     * The loader does NOT refuse them: the command decides. */
    out->drive_post_start = dup_in_arena(a, opt_str(j, "postStart"));
    out->drive_wait_interval = dup_in_arena(a, opt_str(j, "waitInterval"));
    out->drive_ready_interval = dup_in_arena(a, opt_str(j, "readyInterval"));
    out->drive_not_ready_interval = dup_in_arena(a, opt_str(j, "notReadyInterval"));

    return JC_OK;
}

/* behaviour 9: an env setting wins over its config twin, and the struct says
 * which source won. `env_value` is NULL when unset. */
static char *
resolve_ref(const char *env_value, const char *config_value,
            jc_harness_source *src_out, struct jc_arena *a)
{
    if (env_value != NULL) {
        *src_out = JC_HARNESS_SRC_ENV;
        return jc_arena_strdup(a, env_value);
    }
    *src_out = JC_HARNESS_SRC_CONFIG;
    return (config_value != NULL) ? jc_arena_strdup(a, config_value) : NULL;
}

/* ---- the loader ------------------------------------------------------------ */

jc_status
jc_harness_config_load(const char *path, struct jc_harness_config *out,
                       struct jc_arena *a)
{
    char *text = NULL;
    jc_size len = 0;
    char *path_or_null;
    jc_status st;
    cJSON *root = NULL;
    const cJSON *arr;
    const cJSON *item;
    const char *v;
    int n_servers = 0;
    int n_tenants = 0;

    memset(out, 0, sizeof *out);
    out->status = JC_OK;
    /* drive.teamPush defaults TRUE (the reference's `:-1`); parse_drive only
     * overwrites it when the key is present, so a config without it keeps the
     * push-on default. */
    out->drive_team_push = 1;
    path_or_null = (path != NULL) ? (char *)path : (char *)"(null)";

    /* Read the file. A missing dir or file is an error value, not a crash
     * (behaviour 10). */
    st = jc_read_file(path, &text, &len, a);
    if (st != JC_OK) {
        char msg[1024];
        jc_snprintf(msg, sizeof msg,
                    "harness config: could not read \"%s\" (%s)",
                    path_or_null, jc_status_str(st));
        refuse(out, a, st, msg);
        return st;
    }

    root = jc_json_parse(text);
    if (root == NULL) {
        char msg[1024];
        jc_snprintf(msg, sizeof msg,
                    "harness config \"%s\" is not valid JSON",
                    path_or_null);
        refuse(out, a, JC_ERR_PARSE, msg);
        return JC_ERR_PARSE;
    }
    if (!cJSON_IsObject(root)) {
        char msg[1024];
        jc_snprintf(msg, sizeof msg,
                    "harness config \"%s\" is not a JSON object (found %s)",
                    path_or_null,
                    cJSON_IsArray(root) ? "an array" : "another value");
        cJSON_Delete(root);
        refuse(out, a, JC_ERR_PARSE, msg);
        return JC_ERR_PARSE;
    }

    /* Top-level references (behaviours 2, 9, 10): `dir` is required, the
     * other three are optional; each may be overridden by its env setting,
     * which wins (the config may be shared, a dir is per-machine). */
    v = opt_str(root, "dir");
    if (v == NULL) {
        cJSON_Delete(root);
        refuse(out, a, JC_ERR_INVALID,
               "harness config is missing required top-level \"dir\"");
        return JC_ERR_INVALID;
    }
    out->dir = resolve_ref(getenv(JC_HARNESS_ENV_DIR), v,
                           &out->dir_source, a);
    out->has_dir = (out->dir != NULL);

    out->model_key_file = resolve_ref(getenv(JC_HARNESS_ENV_MODEL_KEY_FILE),
                                      opt_str(root, "modelKeyFile"),
                                      &out->model_key_file_source, a);
    out->has_model_key_file = (out->model_key_file != NULL);

    out->git_token_file = resolve_ref(getenv(JC_HARNESS_ENV_GIT_TOKEN_FILE),
                                      opt_str(root, "gitTokenFile"),
                                      &out->git_token_file_source, a);
    out->has_git_token_file = (out->git_token_file != NULL);

    v = opt_str(root, "jichiConfig");
    if (v != NULL) {
        out->jichi_config = dup_in_arena(a, v);
        out->has_jichi_config = 1;
    }

    /* The push's kept-attempts gate (M1056, the port of mirror.sh's
     * PUSH_KEPT=1): a top-level BOOL read LENIENTLY (the loader's existing
     * helper, M519: a strict read once silently disabled a fence), default
     * false (absent key = off). It is a CONFIG KEY ONLY -- there is no
     * JICHI_HARNESS_* env twin for it, and none may be added (the doctor's
     * settings-table lint reads exactly the three JC_HARNESS_ENV_* names out
     * of the header). */
    out->push_kept = jc_json_get_bool_lenient(root, "pushKept", 0);

    /* The server table (behaviours 2-6). */
    arr = cJSON_GetObjectItem(root, "servers");
    if (arr != NULL) {
        if (!cJSON_IsArray(arr)) {
            cJSON_Delete(root);
            refuse(out, a, JC_ERR_INVALID, "\"servers\" is not an array");
            return JC_ERR_INVALID;
        }
        cJSON_ArrayForEach(item, arr) {
            n_servers++;
        }
        if (n_servers > 0) {
            out->servers = (struct jc_harness_server *)
                jc_arena_calloc(a, (jc_size)n_servers *
                                sizeof(struct jc_harness_server));
            if (out->servers == NULL) {
                cJSON_Delete(root);
                refuse(out, a, JC_ERR_OOM,
                       "harness config: out of memory");
                return JC_ERR_OOM;
            }
        }
        n_servers = 0;
        cJSON_ArrayForEach(item, arr) {
            st = parse_server(item, n_servers, out, a);
            if (st != JC_OK) {
                cJSON_Delete(root);
                return st;
            }
            n_servers++;
        }
    }
    out->n_servers = n_servers;

    /* The tenant table (behaviour 7). */
    arr = cJSON_GetObjectItem(root, "tenants");
    if (arr != NULL) {
        if (!cJSON_IsArray(arr)) {
            cJSON_Delete(root);
            refuse(out, a, JC_ERR_INVALID, "\"tenants\" is not an array");
            return JC_ERR_INVALID;
        }
        cJSON_ArrayForEach(item, arr) {
            n_tenants++;
        }
        if (n_tenants > 0) {
            out->tenants = (struct jc_harness_tenant *)
                jc_arena_calloc(a, (jc_size)n_tenants *
                                sizeof(struct jc_harness_tenant));
            if (out->tenants == NULL) {
                cJSON_Delete(root);
                refuse(out, a, JC_ERR_OOM,
                       "harness config: out of memory");
                return JC_ERR_OOM;
            }
        }
        n_tenants = 0;
        cJSON_ArrayForEach(item, arr) {
            st = parse_tenant(item, n_tenants, out, a);
            if (st != JC_OK) {
                cJSON_Delete(root);
                return st;
            }
            n_tenants++;
        }
    }
    out->n_tenants = n_tenants;

    /* The drive (behaviour 8). */
    {
        const cJSON *drive = cJSON_GetObjectItem(root, "drive");
        if (drive != NULL) {
            if (!cJSON_IsObject(drive)) {
                cJSON_Delete(root);
                refuse(out, a, JC_ERR_INVALID,
                       "\"drive\" is not an object");
                return JC_ERR_INVALID;
            }
            st = parse_drive(drive, out, a);
            if (st != JC_OK) {
                cJSON_Delete(root);
                return st;
            }
        }
    }

    /* The host push block (M1147, batch H7): a top-level "host" object with
     * string keys pushHome / image / pageCommand / pushOnCalendar (all
     * optional) and dir (optional -- when absent the config's own top-level
     * dir is reused as host_dir, the note's "the config's own dir field is
     * reused when absent"). Parsed leniently: strings as-is, no bools in this
     * block. An absent block leaves every field NULL and host_dir_present 0
     * (no error: the push commands refuse a missing host block with the named
     * reason, the loader does not). The loader still refuses a "host" that is
     * not an object, the way it does "drive". */
    {
        const cJSON *host = cJSON_GetObjectItem(root, "host");
        if (host != NULL) {
            if (!cJSON_IsObject(host)) {
                cJSON_Delete(root);
                refuse(out, a, JC_ERR_INVALID, "\"host\" is not an object");
                return JC_ERR_INVALID;
            }
            out->host_dir_present = 1;
            out->host_push_home = dup_in_arena(a, opt_str(host, "pushHome"));
            out->host_image = dup_in_arena(a, opt_str(host, "image"));
            out->host_page_command = dup_in_arena(a, opt_str(host, "pageCommand"));
            /* host.pushOnCalendar (SV:1639): the generated jhp-push.timer's
             * OnCalendar value, taken from the config. Parsed exactly like
             * host_pageCommand above (opt_str, as-is, NULL when absent).
             * A config without it must NOT fall back to a schedule that may
             * collide with another drive's push on the same host, so the
             * `push units` body refuses when it is absent -- the loader does
             * not refuse, mirroring the other host fields (the COMMANDS
             * decide). */
            out->host_push_oncalendar = dup_in_arena(a,
                                                     opt_str(host,
                                                             "pushOnCalendar"));
            /* The gate's notifier fields (M1152, batch H8, `notify`): both
             * optional, opt_str as-is, NULL when absent -- the other host
             * fields' idiom. The loader does NOT refuse them: `notify` decides
             * (a gateHome default <dir>/gate, named in the code; an absent
             * notifyCommand runs nothing, the pageCommand idiom). */
            out->host_gate_home = dup_in_arena(a, opt_str(host, "gateHome"));
            out->host_notify_command =
                dup_in_arena(a, opt_str(host, "notifyCommand"));
            /* The gate watcher's fields (M1153, batch H9a, `watch gate` /
             * `watch units`): both optional, opt_str as-is, NULL when absent --
             * the gateHome / notifyCommand idiom. The loader does NOT refuse
             * them: the command decides (`watch units` refuses an absent
             * gateOnCalendar naming it, SV:1639's idiom; the start refuses an
             * absent gateRun naming it, exit 1). */
            out->host_gate_oncalendar =
                dup_in_arena(a, opt_str(host, "gateOnCalendar"));
            out->host_gate_run = dup_in_arena(a, opt_str(host, "gateRun"));
            /* The gate watcher's shimmable host plumbing (M1153, H9a): the
             * container cannot run systemctl / systemd-run, so the tick's
             * three external host commands and its push.git are named by the
             * config, each optional (the unit-state/unit-list/unit-run defaults
             * are the reference's argv; pushRepo defaults to
             * <host.pushHome>/push.git, the decision in jc_hp_watchgate.c). */
            out->host_unit_state = dup_in_arena(a, opt_str(host, "unitState"));
            out->host_unit_list = dup_in_arena(a, opt_str(host, "unitList"));
            out->host_unit_run = dup_in_arena(a, opt_str(host, "unitRun"));
            out->host_push_repo = dup_in_arena(a, opt_str(host, "pushRepo"));
            /* The gate RUN's ledger fields (M1154, batch H9b, `gate run --stage
             * ledger`): both optional, opt_str as-is, NULL when absent -- the
             * other host fields' idiom. The loader does NOT refuse them: the
             * command decides (the required --ledger-address refusal is the
             * ledger stage's, T2; the absent accept list means no DD11
             * exemption). */
            out->host_ledger_address = dup_in_arena(a,
                                                    opt_str(host, "ledgerAddress"));
            out->host_ledger_accept = dup_in_arena(a,
                                                   opt_str(host, "ledgerAccept"));
            /* The sweep stage's shimmable surfaces (M1154 T5, wave 2): the
             * ledgerAddress/ledgerAccept idiom -- opt_str as-is, NULL when
             * absent. The loader does NOT refuse them: the command decides
             * (an absent host.bin means the sweep's own stub binary; an
             * absent host.buildRun / mutantRun / unitRunSan / unitRunO2 /
             * containerRun means the reference's system make/podman command
             * in the clone). */
            out->host_bin = dup_in_arena(a, opt_str(host, "bin"));
            out->host_build_run = dup_in_arena(a, opt_str(host, "buildRun"));
            out->host_mutant_run = dup_in_arena(a, opt_str(host, "mutantRun"));
            out->host_unit_run_san =
                dup_in_arena(a, opt_str(host, "unitRunSan"));
            out->host_unit_run_o2 = dup_in_arena(a, opt_str(host, "unitRunO2"));
            out->host_container_run =
                dup_in_arena(a, opt_str(host, "containerRun"));
            /* The ci stage's shimmable surfaces (M1154 T8, wave 3): the
             * containerRun idiom -- opt_str as-is, NULL when absent. The
             * loader does NOT refuse them: the command decides (an absent
             * ciRun means the reference's `make ci`; an absent ciLogDir
             * means <work>/ci; an absent ciSmokeKeepGoing means "0", and a
             * NON-NUMERIC value is the stage's usage refusal exit 2, the
             * sweep idiom). */
            out->host_ci_run =
                dup_in_arena(a, opt_str(host, "ciRun"));
            out->host_ci_log_dir =
                dup_in_arena(a, opt_str(host, "ciLogDir"));
            out->host_ci_smoke_keep_going =
                dup_in_arena(a, opt_str(host, "ciSmokeKeepGoing"));
            /* The redfirst stage's shimmable surfaces (M1157 T9, wave 3): the
             * ciSmokeKeepGoing idiom -- opt_str as-is, NULL when absent. The
             * loader does NOT refuse them: the command decides (an absent
             * redfirstRun means the reference's driver form; an absent
             * redfirstUnitRun means the reference's `make -j8 WERROR=1
             * test`). */
            out->host_redfirst_run =
                dup_in_arena(a, opt_str(host, "redfirstRun"));
            out->host_redfirst_unit_run =
                dup_in_arena(a, opt_str(host, "redfirstUnitRun"));
            /* The re-pin's shimmable surfaces (M1203, H11, wave 2): the
             * redfirst idiom -- opt_str as-is, NULL when absent. The loader
             * does NOT refuse them: the command decides (the note's CHANGED
             * lines name the reference's form as the absent-field default). */
            out->host_repin_build_dir =
                dup_in_arena(a, opt_str(host, "repinBuildDir"));
            out->host_repin_clone =
                dup_in_arena(a, opt_str(host, "repinClone"));
            out->host_repin_models =
                dup_in_arena(a, opt_str(host, "repinModels"));
            out->host_repin_doctor =
                dup_in_arena(a, opt_str(host, "repinDoctor"));
            out->host_repin_live_doctor =
                dup_in_arena(a, opt_str(host, "repinLiveDoctor"));
            out->host_repin_build_run =
                dup_in_arena(a, opt_str(host, "repinBuildRun"));
            out->host_repin_out =
                dup_in_arena(a, opt_str(host, "repinOut"));
            out->host_repin_pins_dir =
                dup_in_arena(a, opt_str(host, "pinsDir"));
            out->host_repin_bin_dir =
                dup_in_arena(a, opt_str(host, "binDir"));
            out->host_repin_backup_dir =
                dup_in_arena(a, opt_str(host, "backupDir"));
            /* The page/park block (M1213, H12, wave 1): the shimmable host
             * commands the page / page-watch / probe verbs consume. All
             * optional, opt_str as-is, NULL when absent -- the other host
             * fields' idiom. The loader does NOT refuse them: the commands
             * decide. */
            out->host_page_unit_state =
                dup_in_arena(a, opt_str(host, "pageUnitState"));
            out->host_page_unit_failed =
                dup_in_arena(a, opt_str(host, "pageUnitFailed"));
            out->host_page_gateway_probe =
                dup_in_arena(a, opt_str(host, "pageGatewayProbe"));
            out->host_page_model_probe =
                dup_in_arena(a, opt_str(host, "pageModelProbe"));
            out->host_page_gateway =
                dup_in_arena(a, opt_str(host, "pageGateway"));
            /* The park/restart shimmable surfaces (M1213, H12, T3, wave 2): the
             * pageCommand idiom -- opt_str as-is, NULL when absent. The
             * loader does NOT refuse them: the command decides. */
            out->host_page_unit_run =
                dup_in_arena(a, opt_str(host, "pageUnitRun"));
            out->host_page_container_gone =
                dup_in_arena(a, opt_str(host, "pageContainerGone"));
            out->host_page_pid_list =
                dup_in_arena(a, opt_str(host, "pagePidList"));
            /* The orchestrator's land / repin steps (M1159 wave 4): the
             * redfirstUnitRun idiom -- opt_str as-is, NULL when absent.
             * host.gateHome REUSES the existing host_gate_home (the
             * notify's digest home is the same <dir>/gate). The loader
             * does NOT refuse them: the command decides (an absent
             * landRun = the green-land-refused terminal, an absent
             * repinRun = the NOT re-pinned half -- Task C's arms; this
             * wave reads them). */
            out->host_land_run = dup_in_arena(a, opt_str(host, "landRun"));
            out->host_repin_run =
                dup_in_arena(a, opt_str(host, "repinRun"));
            out->host_dir = dup_in_arena(a, opt_str(host, "dir"));
            if (out->host_dir == NULL) {
                /* host.dir absent: reuse the config's own top-level dir (the
                 * env-resolved one, which is the dir the run would actually
                 * use) as the host dir for the run-drive argv. */
                out->host_dir = dup_in_arena(a, out->dir);
            }
        }
    }

    cJSON_Delete(root);
    out->status = JC_OK;
    return JC_OK;
}

/* behaviour 10: the refusal's message, or NULL on a successful load. */
const char *
jc_harness_config_error(const struct jc_harness_config *cfg)
{
    return (cfg->status != JC_OK) ? cfg->message : NULL;
}
