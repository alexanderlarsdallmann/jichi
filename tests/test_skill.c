/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_skill.c - offline tests for the pure skill helpers (jc_skill.c).
 *
 * Parsing, find, and catalog rendering are pure (no filesystem); directory
 * discovery, the load_skill tool, and model usage are verified end-to-end. */

#include "jc_test.h"
#include "jc_skill.h"
#include "jc_app.h"
#include "jc_tool.h"
#include "jc_mem.h"
#include "jc_str.h"
#include "jc_vec.h"

#include <string.h>

static void test_parse(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_skill sk;

    /* Frontmatter name + description; body after the fence. */
    jc_skill_parse(
        "---\nname: changelog\ndescription: Add a CHANGELOG entry.\n---\n"
        "# Steps\n1. Edit CHANGELOG.md\n",
        "folder-name", "/p/.jichi/skills/changelog", a, &sk);
    JC_CHECK_STR(sk.name, "changelog");
    JC_CHECK_STR(sk.description, "Add a CHANGELOG entry.");
    JC_CHECK(strstr(sk.body, "# Steps") != NULL);
    JC_CHECK(strstr(sk.body, "1. Edit CHANGELOG.md") != NULL);
    JC_CHECK_STR(sk.dir, "/p/.jichi/skills/changelog");

    /* No name in frontmatter => default to the folder name. */
    jc_skill_parse("---\ndescription: d\n---\nbody", "myfolder", "/d", a, &sk);
    JC_CHECK_STR(sk.name, "myfolder");
    JC_CHECK_STR(sk.description, "d");

    /* No frontmatter at all => default name, empty description, whole text body. */
    jc_skill_parse("just instructions", "deploy", "/d", a, &sk);
    JC_CHECK_STR(sk.name, "deploy");
    JC_CHECK_STR(sk.description, "");
    JC_CHECK(strstr(sk.body, "just instructions") != NULL);
    JC_CHECK(sk.tools.len == 0); /* no allowed-tools => no fence */

    jc_arena_free(a);
}

static void test_allowed_tools(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_skill sk;
    const char *t0, *t1;

    /* `allowed-tools` is parsed into the fence list. */
    jc_skill_parse(
        "---\nname: fmt\ndescription: d\nallowed-tools:\n  - read_file\n"
        "  - edit_file\n---\nbody",
        "fmt", "/d", a, &sk);
    JC_CHECK(sk.tools.len == 2);
    t0 = JC_VEC_STR(&sk.tools, 0);
    t1 = JC_VEC_STR(&sk.tools, 1);
    JC_CHECK_STR(t0, "read_file");
    JC_CHECK_STR(t1, "edit_file");
    jc_vec_free(&sk.tools);

    /* `tools` works as an alias. */
    jc_skill_parse("---\nname: x\ntools:\n  - run_terminal_command\n---\nb",
                   "x", "/d", a, &sk);
    JC_CHECK(sk.tools.len == 1);
    JC_CHECK(sk.restrict_tools == 0); /* off unless declared */
    jc_vec_free(&sk.tools);

    /* restrict-tools: true is parsed into the flag. */
    jc_skill_parse("---\nname: r\nrestrict-tools: true\nallowed-tools:\n"
                   "  - read_file\n---\nb",
                   "r", "/d", a, &sk);
    JC_CHECK(sk.restrict_tools == 1);
    JC_CHECK(sk.tools.len == 1);
    jc_vec_free(&sk.tools);

    /* Any non-"true" value leaves it off. */
    jc_skill_parse("---\nname: r2\nrestrict-tools: false\n---\nb",
                   "r2", "/d", a, &sk);
    JC_CHECK(sk.restrict_tools == 0);
    jc_vec_free(&sk.tools);

    jc_arena_free(a);
}

static void test_find_and_catalog(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_skill_set set;
    struct jc_skill sk;
    struct jc_sb sb;

    jc_skill_set_init(&set);

    jc_skill_parse("---\nname: alpha\ndescription: do alpha\n---\nA body",
                   "alpha", "/d/alpha", a, &sk);
    jc_vec_push(&set.skills, &sk);
    jc_skill_parse("---\nname: beta\ndescription: do beta\n---\nB body",
                   "beta", "/d/beta", a, &sk);
    jc_vec_push(&set.skills, &sk);

    JC_CHECK(jc_skill_count(&set) == 2);
    JC_CHECK(jc_skill_find(&set, "alpha") != NULL);
    JC_CHECK_STR(jc_skill_find(&set, "beta")->body, "B body");
    JC_CHECK(jc_skill_find(&set, "missing") == NULL);

    jc_sb_init(&sb);
    jc_skill_render_catalog(&set, &sb);
    JC_CHECK(strstr(sb.data, "Available skills") != NULL);
    JC_CHECK(strstr(sb.data, "load_skill") != NULL);
    JC_CHECK(strstr(sb.data, "- alpha: do alpha") != NULL);
    JC_CHECK(strstr(sb.data, "- beta: do beta") != NULL);
    jc_sb_free(&sb);

    jc_skill_set_free(&set);
    jc_arena_free(a);
}

static void test_empty_catalog(void)
{
    struct jc_skill_set set;
    struct jc_sb sb;
    jc_skill_set_init(&set);
    jc_sb_init(&sb);
    jc_skill_render_catalog(&set, &sb);   /* empty set => no output */
    JC_CHECK(sb.len == 0);
    jc_sb_free(&sb);
    jc_skill_set_free(&set);
}

/* Integration (no model): executing the real load_skill tool returns the
 * skill body and renders allowed-tools as an *advisory* hint -- it does NOT
 * fence the agent. (Skills are guidance-only; restriction lives in subagent
 * profiles + modes/permissions. See docs/SKILLS.md "Design note".) */
static void test_load_skill_no_fence(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_app app;
    struct jc_tool_registry reg;
    struct jc_skill sk;
    struct jc_tool_result res;

    memset(&app, 0, sizeof(app));
    app.arena = a;
    app.agent_depth = 0;
    jc_skill_set_init(&app.skills);

    jc_skill_parse("---\nname: inspect\ndescription: read only\n"
                   "allowed-tools:\n  - read_file\n---\nRead the file.",
                   "inspect", "/d", a, &sk);
    jc_vec_push(&app.skills.skills, &sk);

    jc_tool_registry_init(&reg);
    jc_tool_registry_register(&reg, jc_tool_skill());

    jc_tool_execute(&reg, "load_skill", "{\"name\":\"inspect\"}", &res, &app);
    JC_CHECK(res.is_error == 0);
    JC_CHECK(strstr(res.content, "Read the file.") != NULL);
    /* allowed-tools is surfaced as an advisory hint, not a restriction. */
    JC_CHECK(strstr(res.content, "Suggested tools") != NULL);
    JC_CHECK(strstr(res.content, "read_file") != NULL);
    JC_CHECK(strstr(res.content, "recommendations, not restrictions") != NULL);
    jc_tool_result_free(&res);

    /* Unknown skill => error. */
    jc_tool_execute(&reg, "load_skill", "{\"name\":\"nope\"}", &res, &app);
    JC_CHECK(res.is_error == 1);
    jc_tool_result_free(&res);

    jc_tool_registry_free(&reg);
    jc_skill_set_free(&app.skills);
    jc_arena_free(a);
}

void test_skill(void)
{
    test_parse();
    test_allowed_tools();
    test_find_and_catalog();
    test_empty_catalog();
    test_load_skill_no_fence();
}
