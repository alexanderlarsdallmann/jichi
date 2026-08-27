/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_tool_user.h - user-defined tools (config "tools").
 *
 * Each config tool entry is registered as a dynamic jc_tool (the same
 * ctx/schema_ctx/run_ctx mechanism MCP uses). When the model calls it, jichi
 * fork/exec's the configured command, feeds it the arguments (JSON on stdin +
 * JICHI_ARG_<NAME> env vars), and returns the captured output. Permission-gated
 * like any tool. See docs/USER_TOOLS.md.
 */
#ifndef JC_TOOL_USER_H
#define JC_TOOL_USER_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"

struct jc_config;
struct jc_tool_registry;

/* Owns the heap-allocated jc_tool wrappers built from config (their strings and
 * ctx live in the config arena, so only the wrappers are freed here). */
struct jc_user_tool_mgr {
    struct jc_vec tools; /* of struct jc_tool* */
};

void jc_user_tools_init(struct jc_user_tool_mgr *m);

/* Register every config `tools` entry into `reg` (skipping any whose name
 * already exists). Returns the number registered. */
int jc_user_tools_register(struct jc_user_tool_mgr *m,
                           const struct jc_config *cfg,
                           struct jc_tool_registry *reg);

void jc_user_tools_free(struct jc_user_tool_mgr *m);

/* Pure: the env-var name for a tool argument, "JICHI_ARG_" + uppercased `arg`
 * with non-alphanumerics mapped to '_'. NUL-terminates `buf`. */
void jc_user_env_name(const char *arg, char *buf, jc_size cap);

#ifdef __cplusplus
}
#endif
#endif /* JC_TOOL_USER_H */
