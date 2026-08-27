/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_user_tools.c - user-defined tools: config parse, env-name, exec. */

#include "jc_test.h"
#include "jc_tool_user.h"
#include "jc_tool.h"
#include "jc_config.h"
#include "jc_app.h"
#include "jc_mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_env_name(void)
{
    char b[64];
    jc_user_env_name("branch", b, sizeof b);
    JC_CHECK(strcmp(b, "JICHI_ARG_BRANCH") == 0);
    jc_user_env_name("my-arg.x", b, sizeof b);
    JC_CHECK(strcmp(b, "JICHI_ARG_MY_ARG_X") == 0);
    jc_user_env_name("", b, sizeof b);
    JC_CHECK(strcmp(b, "JICHI_ARG_") == 0);
}

/* Execute a registered tool and return a malloc'd result string (caller frees);
 * sets *err to is_error. */
static char *exec_tool(struct jc_tool_registry *reg, struct jc_app *app,
                       const char *name, const char *args, int *err)
{
    struct jc_tool_result res;
    res.content = NULL;
    res.is_error = 0;
    jc_tool_execute(reg, name, args, &res, app);
    *err = res.is_error;
    return res.content;
}

void test_user_tools(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_config cfg;
    struct jc_tool_registry reg;
    struct jc_user_tool_mgr umgr;
    struct jc_app app;
    const char *path = jc_test_tmp("jichi_test_usertools.json");
    int n;
    char *out;
    int err;
    FILE *f;

    test_env_name();

    f = fopen(path, "wb");
    JC_CHECK(f != NULL);
    if (f == NULL) { jc_arena_free(a); return; }
    fputs(
        "{ \"model\": { \"provider\": \"openai\", \"model\": \"m\", "
        "\"apiBase\": \"https://x.test\", \"apiKey\": \"k\" }, "
        "\"tools\": ["
        "  { \"name\": \"catit\", \"description\": \"echo stdin\", "
        "    \"schema\": { \"type\": \"object\", "
        "      \"properties\": { \"msg\": { \"type\": \"string\" } } }, "
        "    \"shell\": \"cat\" },"
        "  { \"name\": \"envcat\", \"shell\": \"printf %s \\\"$JICHI_ARG_MSG\\\"\" },"
        "  { \"name\": \"nope\", \"command\": \"/no/such/binary_xyz\" },"
        "  { \"name\": \"read_file\", \"shell\": \"true\" },"
        "  { \"description\": \"no name\", \"shell\": \"true\" } ] }", f);
    fclose(f);

    jc_config_load(path, 0, &cfg, a);
    /* The no-name entry is dropped at parse; the rest remain. */
    JC_CHECK(cfg.user_tools.len == 4);
    if (cfg.user_tools.len >= 1) {
        struct jc_user_tool_cfg *t0 =
            (struct jc_user_tool_cfg *)jc_vec_at(&cfg.user_tools, 0);
        JC_CHECK_STR(t0->name, "catit");
        JC_CHECK(t0->shell != NULL && strcmp(t0->shell, "cat") == 0);
        JC_CHECK(t0->readonly == 0);
        JC_CHECK(t0->schema_json != NULL &&
                 strstr(t0->schema_json, "msg") != NULL);
    }

    memset(&app, 0, sizeof(app));
    app.arena = a;
    jc_tool_registry_init(&reg);
    jc_tool_register_builtins(&reg);   /* so "read_file" exists -> collision */
    app.tools = &reg;

    jc_user_tools_init(&umgr);
    n = jc_user_tools_register(&umgr, &cfg, &reg);
    JC_CHECK(n == 3);  /* catit, envcat, nope; read_file skipped (shadows) */

    /* cat echoes the stdin JSON back. */
    out = exec_tool(&reg, &app, "catit", "{\"msg\":\"hi\"}", &err);
    JC_CHECK(out != NULL && strstr(out, "\"msg\"") != NULL &&
             strstr(out, "hi") != NULL);
    JC_CHECK(err == 0);
    free(out);

    /* JICHI_ARG_MSG is exported from the scalar arg. */
    out = exec_tool(&reg, &app, "envcat", "{\"msg\":\"hello\"}", &err);
    JC_CHECK(out != NULL && strstr(out, "hello") != NULL);
    JC_CHECK(err == 0);
    free(out);

    /* A non-existent command surfaces an error result, not a crash. */
    out = exec_tool(&reg, &app, "nope", "{}", &err);
    JC_CHECK(out != NULL);
    JC_CHECK(err == 1);
    free(out);

    jc_user_tools_free(&umgr);
    jc_tool_registry_free(&reg);
    jc_config_free(&cfg);
    jc_arena_free(a);
}
