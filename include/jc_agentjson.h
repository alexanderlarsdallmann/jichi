/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_agentjson.h - structured output builders for the headless/agent contract.
 *
 * Headless `--output json` emits one object at the end; `--output jsonl` streams
 * one JSON object per event (message_start / text / tool_call / tool_result /
 * usage / done) so an automation or another AI agent driving jichi can
 * monitor a run in real time and still get a rich final result. These builders
 * are pure (no I/O); main.c prints the objects and handles the pipe. Every
 * object carries "v" (schema version) and "type".
 */
#ifndef JC_AGENTJSON_H
#define JC_AGENTJSON_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_json.h" /* cJSON */

#define JC_AGENTJSON_VERSION 1

/* A fresh event object stamped {"v":JC_AGENTJSON_VERSION,"type":type}. The
 * caller adds event-specific fields and frees with cJSON_Delete. */
cJSON *jc_agentjson_event(const char *type);

/* M439: a bounded preview for a jsonl string field, cut on a UTF-8 boundary.
 *
 * The `tool_result.preview` field was built with jc_snprintf into a 513-byte
 * buffer. jc_snprintf is not UTF-8 aware, so a cut landing mid-codepoint put
 * INVALID UTF-8 on a JSON line -- in a surface docs/EMBEDDING.md calls **stable**
 * and tells consumers to parse. A strict JSON reader is entitled to reject the
 * whole line, which turns a truncated preview into a lost event.
 *
 * Lives here rather than inline at the call site because this module owns the
 * jsonl schema: a rule about what may appear in a stable field belongs with the
 * field. Pure, so the boundary behaviour is unit-testable without a run.
 *
 * `cap` is the buffer size including the NUL. Writes at most cap-1 bytes, ending
 * on a codepoint boundary; an empty or NULL source yields "". */
void jc_agentjson_preview(const char *s, char *out, jc_size cap);

/* M97: broad tool category, for the per-run tool mix a driving agent uses to spot a
 * read-heavy run. Pure classifier below. */
enum jc_tool_cat { JC_TOOLCAT_READ = 0, JC_TOOLCAT_WRITE, JC_TOOLCAT_SHELL, JC_TOOLCAT_OTHER };

/* Classify a tool name into a broad category (Phase M97): read_file / list_files /
 * search_code / codebase_search / find_* / read_* -> READ; write_file / edit_file /
 * apply_patch / rename_symbol / format_file / apply_code_action -> WRITE;
 * run_terminal_command / run_tests -> SHELL; everything else -> OTHER. Pure;
 * unit-tested. */
enum jc_tool_cat jc_agent_tool_category(const char *name);

/* M97: optional run-economics for a driving agent, added to the terminal result
 * when non-NULL (all fields omitted otherwise). Lets a headless driver decide to
 * re-scope / raise the budget / split the task / switch backend. */
struct jc_agent_econ {
    int    starved;         /* M96 all-reads-no-synthesis run (read-heavy bust) */
    const char *budget_kind;/* which budget tripped: "tokens"/"deadline"/
                             * "toolcalls"/"reads"/"" (none) */
    double budget_used;     /* envelope-metered tokens (cache-inclusive); 0 if none */
    double budget_limit;    /* token-budget cap; 0 = unset */
    double peak_input;      /* largest single-call input tokens (the ramp signal) */
    double cache_read;      /* cache-read tokens (0 on a cacheless backend)   */
    double cache_write;     /* cache-write tokens                              */
    int    reads;           /* tool mix: READ-category tool calls             */
    int    writes;          /* WRITE-category */
    int    shells;          /* SHELL-category */
    int    other_tools;     /* OTHER-category */
    /* M443: the three degraded-decision counts (see jc_app.h). Rendered as a
     * `degraded` object on `done`, and only when at least one is non-zero -- an
     * always-present `degraded:false` trains a reader to skip the field on the one
     * run where it matters, which is the M420 argument for `goalposts=N`. */
    int    deg_unanswered;
    int    deg_approval;
    int    deg_privilege;
};

/* The terminal result/"done" object:
 *   {v, type:"done", text, model, session_id, run, tokens:{input,output},
 *    cost, tool_calls, aborted, stop_reason, work_kept
 *    [, error:{code,type,message}]}
 * M431c: `run` is the envelope's run id -- the join key the journal and telemetry
 * both stamp. Omitted when no envelope is armed (there is then no journal to join
 * to). See the `run_start` event for the same id delivered at the START of a run,
 * which is what a supervisor needs to tail the journal while the run is live.
 * NULL string fields become "" (or are omitted, for session_id/run/error). An
 * `error` object is added only when err_msg is non-NULL. `work_kept` (M92-S1) is
 * a bool an automation reads to tell, after a `stop_reason:"budget"`/`
 * verify_failed`, whether the run's work survived (1) or was rolled back to the
 * last green checkpoint (0); pass 1 when no envelope reverted anything.
 * M97: `econ` (nullable) adds run economics -- `starved`, `budget:{used,limit}`,
 * `budget_kind`, `peak_input`, `cache:{read,write}`, `tools:{read,write,shell,
 * other}` -- for a driving agent; NULL omits all of it. Pure. */
cJSON *jc_agentjson_result(const char *text, const char *model,
                           const char *session_id, const char *run_id,
                           double in_tok,
                           double out_tok, double cost, int tool_calls,
                           int aborted, const char *stop_reason, int work_kept,
                           int err_code, const char *err_type,
                           const char *err_msg,
                           const struct jc_agent_econ *econ);

#ifdef __cplusplus
}
#endif
#endif /* JC_AGENTJSON_H */
