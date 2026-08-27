/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_agentdef.c - named agent profile loading + arg merge. */

#include "jc_test.h"
#include "jc_agentdef.h"
#include "jc_app.h"
#include "jc_platform.h"
#include "jc_mem.h"
#include "jc_str.h"

#include <stdlib.h>
#include <string.h>

void test_agentdef(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_agentdef_set set;
    const struct jc_agentdef *d;

    /* Keep the global agents dir out of the picture. */
    setenv("HOME", jc_test_tmp("jichi_agent_home"), 1);
    jc_mkdir_p(jc_test_tmp("jichi_agent_test/.jichi/agents"));
    {
        const char *md =
            "---\n"
            "description: Reviews code\n"
            "model: fast\n"
            "readonly: true\n"
            "---\n"
            "You are a code reviewer. Be terse.\n";
        jc_write_file(jc_test_tmp("jichi_agent_test/.jichi/agents/reviewer.md"), md,
                      strlen(md));
    }

    jc_agentdef_set_init(&set);
    jc_agentdef_load(&set, jc_test_tmp("jichi_agent_test"), a);

    d = jc_agentdef_find(&set, "reviewer");
    JC_CHECK(d != NULL);
    if (d != NULL) {
        JC_CHECK_STR(d->description, "Reviews code");
        JC_CHECK_STR(d->model, "fast");
        JC_CHECK(d->has_readonly == 1 && d->readonly == 1);
        JC_CHECK(strstr(d->system_prompt, "code reviewer") != NULL);
    }
    JC_CHECK(jc_agentdef_find(&set, "nonexistent") == NULL);

    /* Merge precedence: explicit args override the profile. */
    {
        const char *model;
        int ro;
        /* No explicit args => take from the profile. */
        jc_agentdef_merge(d, NULL, 0, 0, &model, &ro);
        JC_CHECK_STR(model, "fast");
        JC_CHECK(ro == 1);
        /* Explicit model overrides; readonly still from profile. */
        jc_agentdef_merge(d, "big", 0, 0, &model, &ro);
        JC_CHECK_STR(model, "big");
        JC_CHECK(ro == 1);
        /* Explicit readonly arg present (=0) overrides the profile's true. */
        jc_agentdef_merge(d, NULL, 1, 0, &model, &ro);
        JC_CHECK_STR(model, "fast");
        JC_CHECK(ro == 0);
        /* No profile and no args => defaults. */
        jc_agentdef_merge(NULL, NULL, 0, 0, &model, &ro);
        JC_CHECK(model == NULL && ro == 0);
    }

    /* Listing render (the `agents` subcommand core). */
    {
        struct jc_sb sb;
        jc_sb_init(&sb);
        jc_agentdef_render_list(&set, &sb);
        JC_CHECK(sb.data != NULL);
        JC_CHECK(strstr(sb.data, "reviewer") != NULL);
        JC_CHECK(strstr(sb.data, "Reviews code") != NULL);
        JC_CHECK(strstr(sb.data, "model: fast") != NULL);
        JC_CHECK(strstr(sb.data, "readonly") != NULL);
        jc_sb_free(&sb);
    }

    /* Command `agent:` routing: apply a profile to a turn, then restore. */
    {
        struct jc_app app;
        struct jc_command_agent_save sv;
        const struct jc_agentdef *applied;

        memset(&app, 0, sizeof(app));
        app.arena = a;
        app.agents = set;                 /* reuse the loaded set (read-only) */
        app.persona_override = NULL;
        app.readonly = 0;

        /* Apply the readonly "reviewer" profile: it takes over the persona and
         * sets readonly. */
        applied = jc_app_command_agent_apply(&app, "reviewer", &sv);
        JC_CHECK(applied != NULL);
        JC_CHECK(sv.applied == 1);
        JC_CHECK(app.persona_override != NULL &&
                 strstr(app.persona_override, "code reviewer") != NULL);
        JC_CHECK(app.readonly == 1);

        jc_app_command_agent_restore(&app, &sv);
        JC_CHECK(app.persona_override == NULL);
        JC_CHECK(app.readonly == 0);

        /* Unknown / empty profile name: no-op, no state change. */
        app.readonly = 0;
        applied = jc_app_command_agent_apply(&app, "no-such-agent", &sv);
        JC_CHECK(applied == NULL && sv.applied == 0);
        jc_app_command_agent_restore(&app, &sv); /* safe no-op */
        JC_CHECK(app.readonly == 0);
        applied = jc_app_command_agent_apply(&app, NULL, &sv);
        JC_CHECK(applied == NULL && sv.applied == 0);
    }

    /* Command `model:` routing: no-op semantics (NULL/empty/unresolvable leave
     * the active model untouched and record no switch). The live switch path is
     * jc_app_switch_model, exercised elsewhere. */
    {
        struct jc_app app;
        struct jc_command_model_save ms;

        memset(&app, 0, sizeof(app));
        app.arena = a;
        jc_vec_init(&app.config.models, sizeof(struct jc_model_cfg));
        app.config.active = 0;

        jc_app_command_model_apply(&app, NULL, &ms);
        JC_CHECK(ms.switched == 0 && app.config.active == 0);
        jc_app_command_model_restore(&app, &ms); /* safe no-op */
        JC_CHECK(app.config.active == 0);

        jc_app_command_model_apply(&app, "", &ms);
        JC_CHECK(ms.switched == 0);

        /* No models configured => unresolvable => no switch. */
        jc_app_command_model_apply(&app, "no-such-model", &ms);
        JC_CHECK(ms.switched == 0 && app.config.active == 0);

        jc_vec_free(&app.config.models);
    }

    jc_agentdef_set_free(&set);
    jc_arena_free(a);
}
