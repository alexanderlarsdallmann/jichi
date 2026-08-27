/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_hooks.h - lifecycle hooks (config "hooks", M25).
 *
 * Config-driven shell commands fired at agent lifecycle points (Claude Code
 * parity): SessionStart, UserPromptSubmit, PreToolUse, PostToolUse, Stop. A
 * hook is fork/exec'd with an event JSON on stdin (reusing the user-tool capture
 * pattern); it communicates back via its exit code and optional stdout:
 *
 *   exit 0   -> proceed. For context-bearing events (UserPromptSubmit,
 *               SessionStart, PostToolUse) non-JSON stdout is injected as
 *               additional context.
 *   exit 2   -> block the action (PreToolUse/UserPromptSubmit); stdout is the
 *               reason shown to the model.
 *   other    -> a warning is logged; the action proceeds.
 *
 *   Advanced: stdout that parses as a JSON object honours
 *   {"decision":"block","reason":...,"additionalContext":...}.
 *
 * Hooks can only further restrict an action, never widen it, and run only at
 * the top level (agent_depth == 0). They are opt-in (config "hooksEnabled",
 * --no-hooks kill switch). See docs/HOOKS.md.
 *
 * The enum and config structs live in jc_config.h (so struct jc_config can hold
 * them inline, like user tools); this header adds the runtime API.
 */
#ifndef JC_HOOKS_H
#define JC_HOOKS_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_str.h"
#include "jc_config.h"

struct jc_app; /* jc_app.h */

/* Aggregated result of firing the hooks for one event. */
struct jc_hook_result {
    int          block;   /* 1 => the action is blocked            */
    struct jc_sb reason;  /* block reason (for the model + log)     */
    struct jc_sb context; /* additionalContext to inject, if any    */
};

void jc_hook_result_init(struct jc_hook_result *r);
void jc_hook_result_free(struct jc_hook_result *r);

/* Whether any hooks are active (enabled + at least one is configured). Cheap;
 * the agent loop guards every fire site with this. */
int jc_hooks_active(const struct jc_app *app);

/* Fire all matching hooks for `event`. `tool`/`args_json` apply to
 * PreToolUse/PostToolUse (the matcher is tested against `tool`); `result`/
 * `is_error` to PostToolUse; `prompt` to UserPromptSubmit. Any may be NULL.
 * Results are accumulated into `out` (which the caller inits/frees). A no-op
 * (out->block stays 0) when hooks are inactive. */
void jc_hooks_fire(struct jc_app *app, enum jc_hook_event event,
                   const char *tool, const char *args_json,
                   const char *result, int is_error, const char *prompt,
                   struct jc_hook_result *out);

/* ----- pure cores (unit-tested) ----------------------------------------- */

/* Does tool-name `tool` match `matcher`? A NULL/empty matcher matches all.
 * '|' separates alternatives; each is a glob (jc_glob_match: '*' any run, '?'
 * one char). */
int jc_hook_matches(const char *matcher, const char *tool);

/* Whether a hook exit code means "block this action" (exactly code 2, mirroring
 * Claude Code). */
int jc_hook_exit_blocks(int code);

#ifdef __cplusplus
}
#endif
#endif /* JC_HOOKS_H */
