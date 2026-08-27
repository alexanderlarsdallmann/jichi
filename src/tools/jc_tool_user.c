/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_user.c - user-defined tools (config "tools"). See jc_tool_user.h.
 *
 * A config tool is registered as a dynamic jc_tool: schema_ctx serves the
 * configured JSON Schema; run_ctx fork/exec's the command, writing the
 * arguments as JSON on stdin (and exporting scalar args as JICHI_ARG_<NAME>),
 * capturing combined stdout+stderr (bounded, timed). The command line is fixed
 * by config, so model-supplied argument values never reach it.
 */

#include "jc_tool_user.h"
#include "jc_platform.h"
#include "jc_tool.h"
#include "jc_config.h"
#include "jc_app.h"
#include "jc_json.h"
#include "jc_str.h"
#include "jc_snprintf.h"
#include "jc_log.h"
#include "jc_proc.h"
#include "cJSON.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>

#define USER_TOOL_MAX_OUTPUT (32 * 1024)

void jc_user_env_name(const char *arg, char *buf, jc_size cap)
{
    const char *pfx = "JICHI_ARG_";
    jc_size i = 0;
    jc_size j;

    if (cap == 0) return;
    for (j = 0; pfx[j] != '\0' && i < cap - 1; j++) buf[i++] = pfx[j];
    for (j = 0; arg != NULL && arg[j] != '\0' && i < cap - 1; j++) {
        char c = arg[j];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        else if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) c = '_';
        buf[i++] = c;
    }
    buf[i] = '\0';
}

/* Format a scalar JSON value into `buf`; returns 1 if `v` was a scalar. */
static int scalar_value(const cJSON *v, char *buf, jc_size cap)
{
    if (cJSON_IsString(v) && v->valuestring != NULL) {
        jc_snprintf(buf, cap, "%s", v->valuestring);
        return 1;
    }
    if (cJSON_IsBool(v)) {
        jc_snprintf(buf, cap, "%s", cJSON_IsTrue(v) ? "true" : "false");
        return 1;
    }
    if (cJSON_IsNumber(v)) {
        double d = v->valuedouble;
        if (d == (double)(long)d) jc_snprintf(buf, cap, "%ld", (long)d);
        else jc_snprintf(buf, cap, "%g", d);
        return 1;
    }
    return 0;
}

/* The fork/exec + concurrent stdin/stdout capture lives in jc_proc_capture
 * (src/util/jc_proc.c), shared with the lifecycle-hook runner. */

/* schema_ctx: serve the configured schema (fallback: empty object schema). */
static cJSON *user_tool_schema(void *vctx)
{
    const struct jc_user_tool_cfg *c = (const struct jc_user_tool_cfg *)vctx;
    cJSON *s = (c->schema_json != NULL) ? jc_json_parse(c->schema_json) : NULL;
    if (s == NULL) {
        s = cJSON_CreateObject();
        cJSON_AddStringToObject(s, "type", "object");
        cJSON_AddItemToObject(s, "properties", cJSON_CreateObject());
    }
    return s;
}

static jc_status user_tool_run(void *vctx, const cJSON *args,
                               struct jc_tool_result *out, struct jc_app *app)
{
    const struct jc_user_tool_cfg *c = (const struct jc_user_tool_cfg *)vctx;
    char *argv[72];
    int n = 0;
    struct jc_vec env;
    struct jc_sb sb;
    char *stdin_json;
    const cJSON *m;
    int code;
    jc_size i;

    /* Build argv: shell form, else command + args. */
    if (c->shell != NULL) {
        argv[n++] = (char *)jc_shell_path();
        argv[n++] = (char *)"-c";
        argv[n++] = (char *)c->shell;
    } else {
        argv[n++] = (char *)c->command;
        for (i = 0; i < c->args.len && n < 70; i++) {
            argv[n++] = *(char **)jc_vec_at((struct jc_vec *)&c->args, i);
        }
    }
    argv[n] = NULL;

    /* Env: configured entries + JICHI_ARG_<NAME> for scalar arguments. */
    jc_vec_init(&env, sizeof(char *));
    for (i = 0; i < c->env.len; i++) {
        char *kv = jc_strdup(*(char **)jc_vec_at((struct jc_vec *)&c->env, i));
        if (kv != NULL) jc_vec_push(&env, &kv);
    }
    if (cJSON_IsObject(args)) {
        cJSON_ArrayForEach(m, args) {
            char val[1024];
            if (m->string != NULL && scalar_value(m, val, sizeof(val))) {
                char name[280];
                struct jc_sb kv;
                jc_user_env_name(m->string, name, sizeof(name));
                jc_sb_init(&kv);
                jc_sb_append(&kv, name);
                jc_sb_append(&kv, "=");
                jc_sb_append(&kv, val);
                if (kv.data != NULL) jc_vec_push(&env, &kv.data);
                /* ownership transferred to env; do not free kv here */
            }
        }
    }

    stdin_json = jc_json_print(args);
    jc_sb_init(&sb);
    code = jc_proc_capture(argv, &env, stdin_json, &sb, USER_TOOL_MAX_OUTPUT,
                       c->timeout, &app->abort_flag);
    free(stdin_json);
    for (i = 0; i < env.len; i++) free(*(char **)jc_vec_at(&env, i));
    jc_vec_free(&env);

    if (code == -1) {
        jc_sb_free(&sb);
        out->content = jc_strdup("error: failed to start the tool command");
        out->is_error = 1;
        return JC_OK;
    }
    if (code == -2) {
        jc_sb_append(&sb, "\n[tool timed out and was killed]");
    }
    if (sb.len == 0) jc_sb_append(&sb, "(no output)");
    jc_sb_append_fmt(&sb, "\n[exit status: %d]", code);
    out->content = jc_sb_finish(&sb);
    out->is_error = (code != 0);
    jc_sb_free(&sb);
    return JC_OK;
}

void jc_user_tools_init(struct jc_user_tool_mgr *m)
{
    jc_vec_init(&m->tools, sizeof(struct jc_tool *));
}

int jc_user_tools_register(struct jc_user_tool_mgr *m,
                           const struct jc_config *cfg,
                           struct jc_tool_registry *reg)
{
    jc_size i;
    int count = 0;

    for (i = 0; i < cfg->user_tools.len; i++) {
        struct jc_user_tool_cfg *c = (struct jc_user_tool_cfg *)
            jc_vec_at((struct jc_vec *)&cfg->user_tools, i);
        struct jc_tool *tool;
        if (jc_tool_registry_find(reg, c->name) != NULL) {
            jc_logf(JC_LOG_WARN,
                    "tools: '%s' shadows an existing tool; skipping", c->name);
            continue;
        }
        tool = (struct jc_tool *)calloc(1, sizeof(*tool));
        if (tool == NULL) break;
        tool->name = c->name;
        tool->description = c->description;
        tool->readonly = c->readonly;
        tool->schema = NULL;
        tool->run = NULL;
        tool->ctx = c;
        tool->schema_ctx = user_tool_schema;
        tool->run_ctx = user_tool_run;
        jc_vec_push(&m->tools, &tool);
        jc_tool_registry_register(reg, tool);
        count++;
    }
    return count;
}

void jc_user_tools_free(struct jc_user_tool_mgr *m)
{
    jc_size i;
    for (i = 0; i < m->tools.len; i++) {
        free(*(struct jc_tool **)jc_vec_at(&m->tools, i));
    }
    jc_vec_free(&m->tools);
}
