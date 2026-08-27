/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_agent.h - the streaming agent loop.
 *
 * Ports streamChatResponse.ts: build the request from history + system + tools,
 * stream the response (accumulating text and tool calls), execute any tool
 * calls, append their results, and repeat until the model stops requesting
 * tools or the iteration cap is hit.
 *
 * The callbacks are the seam to the front-end (headless printer or TUI).
 */
#ifndef JC_AGENT_H
#define JC_AGENT_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_app.h"
#include "jc_message.h"
#include "cJSON.h"

struct jc_agent_callbacks {
    /* Streamed assistant text delta (live display). */
    void (*on_assistant_text)(void *user, const char *delta, jc_size n);
    /* A tool is about to run. `id` is the provider's tool_call id for THIS call
     * (M442) -- the value that appears in the request's tool_calls array and in
     * the matching tool result, so a consumer can pair them. Never NULL, but may
     * be "" if a provider omitted one. */
    void (*on_tool_start)(void *user, const char *name, const char *args_json,
                          const char *id);
    /* A tool finished. `id` matches the on_tool_start that opened it. */
    void (*on_tool_result)(void *user, const char *name,
                           const char *result, int is_error, const char *id);
    /* Permission gate: return 1 to allow, 0 to deny. NULL => always allow.
     * `edited` is an out-param initialised to NULL; a front-end may set it to a
     * malloc'd replacement args JSON (the user edited the call before approving)
     * — the loop then runs the tool with the edited args and frees it (M68). */
    int  (*confirm_tool)(void *user, const char *name, const char *args_json,
                         char **edited);
    /* Privileged-command gate (M153): a shell command launched under sudo/
     * doas/pkexec/su/run0. Return 1 to allow this one call, 0 to refuse.
     * Unlike confirm_tool this MUST NOT be short-circuited by any per-tool
     * "always" grant -- privilege is asked afresh every time. `launcher` is
     * the detected tool name (e.g. "sudo"); `command` the full shell string.
     * NULL => the front-end offers no privileged prompt (unattended): the
     * loop refuses. */
    int  (*confirm_privileged)(void *user, const char *launcher,
                               const char *command);
    /* Confirm a KINETIC action -- a tool/command that moves mass or energy in
     * the physical world (M163a). Same contract as confirm_privileged: return
     * 1 to allow this one call, 0 to refuse; MUST NOT be short-circuited by an
     * "always" grant; NULL => unattended => the loop refuses. `subject` is
     * "tool:<name>" or "shell"; `detail` the tool args or the shell command. */
    int  (*confirm_kinetic)(void *user, const char *subject,
                            const char *detail);
    /* Marks the start/end of an assistant message (e.g. to print a newline). */
    void (*on_message_begin)(void *user);
    void (*on_message_end)(void *user);
    /* Token usage for one model call (0 if the provider did not report it).
     * cache_read/cache_write are the prompt-cache hit/write portions of in_tok
     * (0 when unknown or unsupported -- see jc_provider get_cache_usage). */
    void (*on_usage)(void *user, double in_tok, double out_tok,
                     double cache_read, double cache_write);
    /* A dim, free-form status line (e.g. a subagent banner or a parallel-agent
     * board update). Optional; NULL => not shown. */
    void (*on_status)(void *user, const char *line);
    /* Periodic liveness tick during a model call (incl. while waiting for the
     * first byte), so a front-end can animate a "working" spinner. Optional;
     * called from libcurl's progress callback, so throttle inside it (M66). */
    void (*on_progress)(void *user);
    /* Mid-turn steering from the human (M254): at each top-level tool-call
     * boundary the loop asks the front-end whether anything was typed while the
     * agent worked. Return a malloc'd NUL-terminated string -- the loop appends
     * it as ONE "[operator]" user message (jc_history_add_operator, the same
     * fold the control channel uses) and frees it -- or NULL for nothing
     * queued. Called only at agent_depth 0, only between tool rounds, so it can
     * never interleave with a tool or a nested run. Optional; NULL => no
     * keyboard steering (headless, ACP, subagents). */
    char *(*take_input)(void *user);
    void *user;
};

/* Run one user turn to completion (through any number of tool iterations).
 * The user's message must already be appended to `hist`. On return, `hist`
 * contains the assistant message(s) and tool results produced. */
jc_status jc_agent_run_turn(struct jc_app *app, struct jc_history *hist,
                            const struct jc_agent_callbacks *cb);

struct jc_provider; /* jc_provider.h */

/* Run a self-contained nested (sub)agent against `provider`, advertising a
 * restricted tool set (mutating tools gated by `include_mutating`; the
 * spawn_subagent tool is always excluded), with an auto-approve sandbox (ASK
 * verdicts run without a prompt; DENY is still refused), capped at `max_iters`.
 * `allow_tools` (a jc_vec of char* tool names, or NULL/empty) further fences the
 * subagent to exactly that allow-list -- a named agent profile's `tools:` field;
 * it composes with `include_mutating` (both must permit a tool). `hist` must
 * already be seeded with the task as a user message. On success the subagent's
 * final assistant text is copied (arena-owned) into *answer_out, or NULL if it
 * produced none. Used by the spawn_subagent / spawn_parallel tools. */
jc_status jc_agent_run_subagent(struct jc_app *app, struct jc_history *hist,
                                struct jc_provider *provider,
                                const char *system_msg,
                                int include_mutating, int max_iters,
                                const struct jc_vec *allow_tools,
                                const struct jc_agent_callbacks *cb,
                                char **answer_out);

/* Run a custom command's prompt as an isolated subtask (its `subtask: true`
 * frontmatter). The work runs in a nested agent on a throwaway history so the
 * command's intermediate tool calls/reasoning never enter the main
 * conversation; only the user prompt and the subtask's final answer are appended
 * to `hist`. `cb` is forwarded so the answer streams to the user as a normal
 * turn would. The active model (from `model:`) applies because the caller
 * sets it before calling; the `agent:` persona applies because this builds its
 * system message from app->persona_override (M596 -- until then the comment
 * here claimed it and the code built the generic delegate prompt). The
 * profile's tools fence is NOT applied: a command runs under the current
 * permission posture, tightened only by a readonly profile (jc_app.h).
 *
 * `language` (M597, or NULL) is the command's `language:` frontmatter; it wins
 * over config `language`, and either reaches the subtask's prompt because the
 * answer goes to the user with no parent agent to translate it.
 *
 * `output_path` (M79, or NULL) is a workspace-relative file the command declares
 * it produces (frontmatter `output:`). If the subtask leaves that file unchanged
 * -- i.e. the model narrated its result instead of calling write_file -- the
 * subtask's final answer is persisted there so the work isn't lost. */
jc_status jc_agent_run_command_subtask(struct jc_app *app,
                                       struct jc_history *hist,
                                       const char *prompt,
                                       const char *output_path,
                                       const char *language,
                                       const struct jc_agent_callbacks *cb);

/* The most recent non-empty assistant message text in `hist`, or NULL. */
const char *jc_agent_last_assistant_text(const struct jc_history *hist);

/* Whether an agent currently at `agent_depth` (0 = top level) may spawn another
 * (sub)agent, given `max_depth` (config.max_subagent_depth). The child runs at
 * agent_depth+1, so nesting is allowed while agent_depth < max_depth. The default
 * max_depth of 2 allows a top-level agent to spawn a subagent that itself spawns
 * one grandchild (bounded two-level nesting); spawn_parallel stays top-level only.
 * Pure; the single predicate the spawn tools (advertise + backstop) consult. */
int jc_subagent_can_spawn(int agent_depth, int max_depth);

/* Floor on a tapered per-subagent iteration budget: however deep the nesting, a
 * subagent still gets at least this many tool iterations to make progress. */
/* M572: consecutive refusals in one turn, tool-independent, before jichi stops
 * asking. THREE, from a measurement rather than taste: the operator's transcript
 * ran to TEN prompts for one rename because M570's per-tool counters gave four
 * rotating tools their own budgets. The cost of stopping too early is one
 * retyped request; the cost of stopping too late is every prompt and every diff
 * read aloud again. An approval resets the count, so refusing some proposals and
 * accepting another is unaffected. */
#define JC_DENY_STOP_AT 3

#define JC_SUBAGENT_MIN_ITERS 4

/* Per-depth iteration taper: a subagent spawned at `depth` (the child's own
 * agent_depth, >= 1) gets roughly base_iters halved per level, floored at
 * JC_SUBAGENT_MIN_ITERS. This keeps a deep synchronous chain from multiplying the
 * total tool-call budget combinatorially while still letting each level work.
 * Pure; unit-tested. depth <= 0 returns base_iters unchanged. */
int jc_subagent_iters_at_depth(int base_iters, int depth);

#ifdef __cplusplus
}
#endif
#endif /* JC_AGENT_H */
