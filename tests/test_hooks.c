/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_hooks.c - lifecycle hooks: pure matchers + config parsing (M25). */

#include "jc_test.h"
#include "jc_hooks.h"
#include "jc_config.h"
#include "jc_mem.h"

#include <stdio.h>

static void test_matches(void)
{
    /* No matcher => fire for everything. */
    JC_CHECK(jc_hook_matches(NULL, "write_file") == 1);
    JC_CHECK(jc_hook_matches("", "write_file") == 1);

    /* Exact + miss. */
    JC_CHECK(jc_hook_matches("write_file", "write_file") == 1);
    JC_CHECK(jc_hook_matches("write_file", "read_file") == 0);

    /* '|' alternation. */
    JC_CHECK(jc_hook_matches("edit_file|write_file", "write_file") == 1);
    JC_CHECK(jc_hook_matches("edit_file|write_file", "edit_file") == 1);
    JC_CHECK(jc_hook_matches("edit_file|write_file", "read_file") == 0);

    /* Globs. */
    JC_CHECK(jc_hook_matches("*_file", "write_file") == 1);
    JC_CHECK(jc_hook_matches("write_*", "write_file") == 1);
    JC_CHECK(jc_hook_matches("git_*", "write_file") == 0);

    /* NULL tool is treated as empty. */
    JC_CHECK(jc_hook_matches("write_file", NULL) == 0);
    JC_CHECK(jc_hook_matches("*", NULL) == 1);
}

static void test_exit_blocks(void)
{
    JC_CHECK(jc_hook_exit_blocks(2) == 1);
    JC_CHECK(jc_hook_exit_blocks(0) == 0);
    JC_CHECK(jc_hook_exit_blocks(1) == 0);
    JC_CHECK(jc_hook_exit_blocks(127) == 0);
}

static void test_config_parse(void)
{
    struct jc_arena *a = jc_arena_new(0);
    struct jc_config cfg;
    const char *path = jc_test_tmp("jichi_test_hooks.json");
    FILE *f = fopen(path, "wb");

    if (f == NULL || a == NULL) {
        if (f != NULL) fclose(f);
        if (a != NULL) jc_arena_free(a);
        return;
    }
    fputs("{\"models\":[{\"name\":\"m\",\"provider\":\"openai\","
          "\"model\":\"x\",\"apiKey\":\"k\"}],"
          "\"hooksEnabled\":true,"
          "\"hooks\":{"
          "\"PreToolUse\":[{\"matcher\":\"write_file|edit_file\","
          "\"commands\":[{\"shell\":\"exit 2\",\"timeout\":5}]}],"
          "\"SessionStart\":[{\"commands\":[{\"command\":\"echo\","
          "\"args\":[\"hi\"]}]}],"
          "\"BogusEvent\":[{\"commands\":[{\"shell\":\"true\"}]}]"
          "}}", f);
    fclose(f);

    JC_CHECK(jc_config_load(path, 0, &cfg, a) == JC_OK);
    JC_CHECK(cfg.hooks_enabled == 1);

    /* PreToolUse: one matcher with one command. */
    JC_CHECK(cfg.hooks.events[JC_HOOK_PRE_TOOL].len == 1);
    {
        struct jc_hook_matcher_cfg *mc = (struct jc_hook_matcher_cfg *)
            jc_vec_at(&cfg.hooks.events[JC_HOOK_PRE_TOOL], 0);
        JC_CHECK_STR(mc->matcher, "write_file|edit_file");
        JC_CHECK(mc->commands.len == 1);
        {
            struct jc_hook_cmd_cfg *cc = (struct jc_hook_cmd_cfg *)
                jc_vec_at(&mc->commands, 0);
            JC_CHECK_STR(cc->shell, "exit 2");
            JC_CHECK(cc->timeout == 5);
        }
    }

    /* SessionStart: matcher-less, argv command. */
    JC_CHECK(cfg.hooks.events[JC_HOOK_SESSION_START].len == 1);

    /* Unknown event names are dropped, not mis-bucketed. */
    JC_CHECK(cfg.hooks.events[JC_HOOK_POST_TOOL].len == 0);
    JC_CHECK(cfg.hooks.events[JC_HOOK_USER_PROMPT].len == 0);
    JC_CHECK(cfg.hooks.events[JC_HOOK_STOP].len == 0);

    jc_config_free(&cfg);
    jc_arena_free(a);
    remove(path);
}

void test_hooks(void)
{
    test_matches();
    test_exit_blocks();
    test_config_parse();
}
