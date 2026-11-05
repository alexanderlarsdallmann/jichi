/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_telemetry.h - offline summary of a telemetry JSONL log (the `telemetry`
 * subcommand). Pure: jc_telemetry_summarize parses the JSONL text and
 * jc_telemetry_render formats a report; the I/O (finding/reading the file) lives
 * in main.c. See jc_eventlog.h for the event schema and docs/TELEMETRY.md. */
#ifndef JC_TELEMETRY_H
#define JC_TELEMETRY_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_vec.h"
#include <stdio.h>   /* jc_telemetry_feed_file takes a FILE* (F-04) */

struct jc_sb; /* jc_str.h */

/* Per-model aggregate (from `model_call` events). */
struct jc_telem_model {
    /* Display label: the model's CONFIG name (or its wire id when unnamed). */
    char   name[96];
    /* Grouping key (M289): the WIRE model id when the event carries one, else
     * the name. Renaming a model in the config used to split its history into two
     * reader rows -- a real rename did exactly that, showing 777 calls under the
     * new name and 4585 under the old, each with its own est-vs-real ratio
     * computed on a fraction of the data -- while calibration.json, which keys by
     * wire id, correctly kept one entry. The reader now agrees with it. Old logs
     * carry no `model_id`, so they key by name exactly as before. */
    char   key[96];
    long   calls;
    long   errors;
    long   timeouts;   /* failed calls with result=="timeout" (stalls, M22c) */
    double in_tok;
    double out_tok;
    double cache_read;  /* cached input tokens served (M31a) */
    double cache_write; /* input tokens written to cache (Anthropic) */
    double cost;       /* USD */
    double lat_sum;    /* latency_ms sum, for the mean */
    double lat_max;
    long   lat_n;
    /* M897: first-token time (`ttft_ms`), over the calls that carried it --
     * post-M897 calls whose stream produced output. 0 calls: not rendered. */
    double ttft_sum;
    double ttft_max;
    long   ttft_n;
    /* M192 input attribution: the estimated composition of each call's prompt
     * (system prompt / tool schemas / history), summed. `attr_n` counts only the
     * calls that carried the fields, so a pre-M192 log leaves it 0 and the
     * attribution line is simply not rendered. `drift_sum`/`drift_n` accumulate
     * real in_tok / estimated total -- the M77 calibration ratio, observed. */
    double sys_tok;
    double tools_tok;
    double hist_tok;
    long   attr_n;
    double drift_sum;
    long   drift_n;
};

/* Per-workspace aggregate (from the "ws" stamp, M56). Lets the summarizer show
 * which projects a shared log mixes (M59). */
struct jc_telem_ws {
    char ws[1024];
    long events;
    long turns;
};

/* Per-tool aggregate (from `tool_call` events). */
/* M591: a call that arrived under a name that is not the tool's own.
 *
 * M532 decided, deliberately, that telemetry records the RAW wire name -- "a
 * gate must decide on what will run; a message must say what was asked". That
 * decision stands and this does not change the log. It changes the READER,
 * which keyed its per-tool row on the raw name and therefore split one tool
 * across two rows: a real workspace log shows `todo_write calls=1` beside
 * `todowrite calls=2`, and every statistic on that tool -- ok-rate, latency,
 * output bytes -- was computed on a fraction of its calls.
 *
 * The requested spelling is kept and counted rather than discarded, because it
 * is the actionable half: a model that keeps reaching for `todo_write` is
 * telling you what the tool list taught it. */
struct jc_telem_alias {
    char requested[64];
    char canonical[64];
    long calls;
};

struct jc_telem_tool {
    char   name[64];
    long   calls;
    long   ok;
    double dur_sum;    /* duration_ms sum, for the mean */
    double dur_max;
    double out_bytes;
    /* M168: of the !ok calls, how many were a NON-ZERO COMMAND EXIT rather than
     * a tool failure (from the `exit` field, emitted only by tools that run a
     * command). A red gate is the agent doing its job in a fix-forward loop, so
     * folding it into the ok-rate makes a healthy run look like a broken tool:
     * real dogfood data showed run_tests at 73% "ok" whose tool-level success
     * was 97%. Zero for every tool that runs no command, and for logs written
     * before M168 -- so old reports are unchanged. */
    long   cmd_fail;
    /* Recency (M-band recency-aware insights): the `ts` of this tool's last
     * call overall, its last OK call, and its last FAILED call. Let the insight
     * ranker tell a currently-failing tool from one that failed historically but
     * has since recovered (last_ok_ts >= last_fail_ts) or gone quiet (last_ts far
     * below the log's max_ts). 0 when unknown (pre-recency logs / hand-built
     * summaries), which the ranker treats as "no recency data" (old behavior). */
    double last_ts;
    double last_ok_ts;
    double last_fail_ts;
};

/* Per-session timeline entry (M82). One per distinct `sid`, in first-seen order,
 * so a multi-run log reads as a timeline: which run/phase spent the tokens, hit
 * the budget, or needed multiple passes. Surfaces the per-call input ramp
 * (`peak_in`) the missing prompt-cache makes dominant. */
struct jc_telem_session {
    char   sid[48];
    long   order;      /* first-seen index, for stable timeline ordering */
    long   calls;      /* model_call count */
    double in_tok;     /* summed prompt tokens (incl. cached) */
    double out_tok;
    double cost;       /* USD */
    double peak_in;    /* largest single-call input (in_tok+cache) -- the ramp */
    long   tools;
    long   tool_ok;
    long   compacts;   /* mid-turn compaction events */
    /* M592: cache, PER SESSION. The aggregate figure is a trap on a log that
     * spans a change: four sessions of one 2026-08-25 drive read 0% and the
     * fifth read 92.4%, because the deployment's prefix caching was switched on
     * between them -- and the summary's single number reported 8.0%, which
     * describes no session that ever ran. This file's own `min_ts` comment warns
     * about exactly that shape for tool ok-rates ("crossed M168 ... read as a
     * live defect that had in fact been fixed weeks earlier"); the same hazard
     * applies to anything a SERVER can change under a long log, and a server
     * setting is not even visible in the tree's history. */
    double cache_read; /* summed cache_read_in */
    double uncached;   /* summed in_tok (the part the server billed as new) */
};

/* One jichi build seen in a log (M290). */
struct jc_telem_version {
    char ver[32];
    long events;
};

struct jc_telemetry_summary {
    /* When non-empty, only events whose "ws" field equals this are counted
     * (M56). Set it after init, before feeding. Empty => all events. */
    char ws_filter[1024];
    /* When > 0, only events with `ts` >= this are counted (M286). Set it after
     * init, before feeding; 0 => all events, so every existing caller is
     * unchanged.
     *
     * Why a summarizer needs a window at all: a long-lived log spans code
     * changes. One project's 34 MB log ran six weeks and crossed M168 (which
     * taught the reader to separate a red gate from a broken tool), M192 (input
     * attribution) and M219 (tool-name aliases) -- so the single aggregate
     * ok-rate it printed mixed events from before and after each fix, and read
     * as a live defect that had in fact been fixed weeks earlier. `runs
     * --since` (M160) and `audit --since` (M158) already existed; this closes
     * the gap on the reader most likely to be pointed at a big old log, and
     * that the /learn mentor reads. */
    double min_ts;
    long events;
    long turns;
    long retries;
    long routes;
    long compacts;
    /* M192: how many mid-turn elisions were the ZERO-LOSS superseded-read pass
     * (M93/M94) versus the lossy age-based fallback. Before this split the
     * merged `elided` count could not say which mechanism was doing the work. */
    long compact_dup;
    long compact_age;
    /* M323: mid-turn passes that ran under pressure and could NOT reach their
     * target -- the request went out over the configured limit anyway. A
     * workload with 1,038 mid-turn compactions and 3.1% of calls over budget
     * could not see this at all: the event recorded only what was elided, never
     * whether it was enough. */
    /* M326x: mid-turn passes where the high-water trigger actually FIRED, as
     * opposed to the eager zero-loss dedup that runs every round. 44% of one
     * workload's 1,057 mid-turn events were the latter, so the raw compaction
     * count read as alarm when most of it was routine housekeeping. */
    long compact_midturn;   /* phase == "midturn" (the rest are between-turn) */
    long compact_pressed;
    /* M326y: pressured passes that ended STILL above the high-water, so they
     * re-trigger next round. The repeating case: measured reclaim decays to
     * zero after ~2 passes within a turn, so a long turn fires this every round
     * for no gain. Distinct from compact_short (missed the 60% target but may
     * still have dropped under the 80% trigger, which buys quiet rounds).
     * Requires the M323 before/after fields, so it stays 0 on older logs -- an
     * absence the renderer states rather than infers. */
    long compact_unrelieved;
    long compact_short;
    long errors;       /* failed model_call count */
    long timeouts;     /* failed model_call count with result=="timeout" (M22c) */
    /* M321: transport failures (status 0) split by cause, from the `transport`
     * field. A 34,216-event workload had 2,402 of these (15% of all calls) and
     * the summary could only call them "errors" -- so the operator raised the
     * wrong timeout and lost 6.5 hours. `connect_fail` is the one worth naming:
     * it means no request was ever sent, and it has a knob. */
    long transport_fail;   /* status 0 with a transport diagnosis    */
    long connect_fail;     /* of those, a connect-phase timeout      */
    /* M92: autonomy-envelope outcome breakdown (from turn_end `outcome` +
     * `rolled_back`), so a budget stop that KEPT green work isn't misread as a
     * failure. Counted only for turns that carry an envelope outcome. */
    long out_completed;       /* outcome == "ok"                               */
    long out_verify_failed;   /* outcome == "verify_failed"                    */
    long out_budget_kept;     /* "budget_exhausted", work kept (not reverted)  */
    long out_budget_reverted; /* "budget_exhausted", rolled back to green      */
    /* M167: small-model self-correction counters. The `nudge` (M147) and
     * `args_repair` (M148) events were emitted from the start but had no reader
     * here, so two rows of the small-model measurement plan
     * (docs/DEFERRED_LOCAL_GPU.md §4) were invisible to the shipped summarizer
     * and had to be counted by hand. `nudge_fired` counts prose-tool-call
     * detections, `nudge_recovered` the retries that then produced a native
     * call; `repair_*` counts malformed-argument repair attempts and successes.
     * A recovery rate well below the fire rate means the model is not merely
     * sloppy about syntax -- it may not be calling tools natively at all. */
    long nudge_fired;
    long nudge_recovered;
    /* M949 (def-325): the text-tool-call recovery pass. `md_recovered`
     * counts tool calls recovered from a reply written as markdown blocks
     * (model textToolCalls / toolCalling:"text"); a high count on a session is
     * the signature of a model that narrates instead of calling. */
    long md_recovered;
    /* M951 (V2): the relay's leg-budget measurement. `relay_budget` counts
     * the `relay` events at phase "budget" -- one per handover where the next
     * leg's tool-call budget was measured from this leg's prompt growth. 0
     * means no relay ran, or its legs never measured a growth (the budget
     * then stays the configured cap, not a number worth reporting). */
    long relay_budget;
    /* M937 (D3): the delegate "give your answer" nudge. `empty_reasoning_fired`
     * counts a reasoning-only (no text, no tool call, NOT cut) reply that was
     * asked once for the final answer; `empty_reasoning_recovered` the retries
     * that then carried answer text (so the run is not running with the review
     * owed and missing). */
    long empty_reasoning_fired;
    long empty_reasoning_recovered;
    long repair_total;
    long repair_ok;
    /* M1067: a tool call whose arguments carried top-level keys the schema does
     * not declare (the M1067 note block in jc_tool.c, `undeclared_args`). The
     * call still ran on the declared arguments and names the ignored keys on
     * its own result; this counts how many such calls happened. The `count`
     * field on each event is the number of ignored keys in that call. */
    long undeclared_args_calls;   /* the call ran on declared args only        */
    long undeclared_args_keys;    /* sum of each call's `count` field          */
    /* M975 (D6): the scoped edit check emitted `scoped_edit` events. */
    long scoped_edit_trip;     /* kind == "trip"                              */
    long scoped_edit_detected; /* kind == "detected"                          */
    /* M417: `test_edit` events -- M88's moved-goalpost heuristic firing (a
     * test assertion MODIFIED during an autonomous run). Counted offline so
     * the learn loop can turn the richest lesson a run produces ("the model
     * edited the gate") into a drafted note instead of a warning nobody mines. */
    long test_edits;

    /* M584 (seams D6). Eight telemetry event types were emitted every run and
     * read by NOTHING -- the lints guarantee every event is DOCUMENTED, and
     * nothing guaranteed any event is READ. Measured on the only real corpus
     * available (42,652 events, one workload): of those eight, `hook` fired 15
     * times and `privileged` twice; the other six never fired at all, because
     * the features behind them are off by default (auto-context), need a
     * violation (history_check) or need hardware (kinetic). Counters exist for
     * all of them anyway -- an event that never fires here still fires on
     * somebody's machine, and a reader that silently drops it is how these
     * eight accumulated. The renderer prints a line only when a count is
     * non-zero, so a quiet log stays quiet. */
    long hook_start_failed;   /* the hook process never started (-1)          */
    long hook_timeout;        /* killed at its configured timeout (-2)        */
    long hook_not_runnable;   /* exit 126/127: THE CHECK DID NOT RUN          */
    long hook_nonzero;        /* any other nonzero exit: ran, complained      */
    long privileged_total;    /* a sudo/doas escalation was proposed          */
    long privileged_refused;  /* ...and refused (decision != *_approved)      */
    long kinetic_total;       /* a mass/energy-moving tool was gated          */
    long kinetic_refused;
    long prefix_churn;        /* the cached prompt prefix moved (M365)        */
    long prefix_churn_max;    /* longest streak seen                          */
    long shell_fence_total;   /* write children the fence was set up for (M831)*/
    long shell_fence_unfenced;/* of those, how many ran UNFENCED (no Landlock)*/
    long shell_fence_refused; /* ...and how many were REFUSED: the fence could */
                              /* not be applied on a Landlock kernel (M852)   */
    long cut_continue;        /* M859: a cut reply was continued, once        */
    long task_budget_notes;   /* M861: a task's four-fifths budget note       */
    long task_budget_stops;   /* M861: a task stopped at its max_tool_calls   */
    long pre_discard_kept;    /* M863: a write child's worktree kept before a */
    long pre_discard_failed;  /* discarding git command; ...could not be kept */
    long test_edit_holds;     /* M865: a child held for an edited assertion   */
    long new_dir_holds;       /* M888: tasks whose new directories were held  */
    long new_dir_hold_dirs;   /* M888: ... the directories held               */
    long new_dir_hold_files;  /* M888: ... their files                        */
    long new_dir_hold_bytes;  /* M888: ... their bytes                        */
    long no_merge_kept;       /* M890: write tasks kept, "merge": false       */
    long metric_gate_merged;  /* M875: a pin edit the metric let merge,       */
    long metric_gate_held;    /* ...one it held because the metric fell,      */
    long metric_gate_unknown; /* ...and one it could not measure (held)       */
    /* M1006: the dirty-tree dispatch guard. `dirty_dispatch` is one per CHECKED
     * write dispatch (off is unchecked -- it skips the whole check); counted
     * here so `jichi telemetry` does not require hand-counting (the M167
     * "emitted but had no reader" gap). `dirty_refused` is the subset that
     * ended `refused` -- the one that matters: the guard actually stopped a
     * dispatch from a dirty base. */
    long dirty_dispatch;      /* a write dispatch was checked for a dirty base */
    long dirty_refused;       /* of those, refused (outcome == "refused")     */
    long dirty_unknown;       /* M1006-a: the probe FAILED (outcome ==
                               * "proceeded_unknown"); the tree state could
                               * not be read -- distinct from clean */
    long loop_stop_same;      /* M869: children a loop stop ended: the same   */
    long loop_stop_refused;   /* successful result (M829), the same refused   */
    long loop_stop_placeholder; /* call (N27 (a)), copied placeholders ((b))  */
    long loop_stop_rotation;  /* M870 (N21): a rotation with nothing new      */
    long finish_alias;        /* a delegate ended its turn by a finish-like   */
                              /* call, its message the answer (M853)          */
    long retrieve_calls;      /* auto-context attached passages (M61)         */
    long retrieve_blocks;
    long retrieve_tokens;
    long args_truncated;      /* the model's arguments hit the output cap     */
    long history_checks;      /* the history wire contract was violated (M364)*/
    long constraints;         /* a constraint was inferred/adopted            */
    long constraint_exempts;

    /* M585: tool calls that arrived with NO NAME. Not a wrong guess -- a
     * malformed call, and the two need different answers. Counted from the
     * `tool_call` event's existing empty `name`, deliberately NOT a new event
     * type, so every log already on disk answers the question retroactively.
     * `nameless_bursts` counts the runs of consecutive nameless calls within a
     * single (sid, turn): a burst is the signature of a model retrying a call it
     * cannot fix, and one burst of three costs three round-trips for one
     * mistake. */
    long tool_nameless;
    long tool_nameless_bursts;
    /* burst bookkeeping: the (sid, turn) of the previous nameless call */
    char nameless_sid[64];
    long nameless_turn;
    double max_ts;            /* newest event `ts` seen (recency reference)    */
    /* M290: the distinct jichi BUILDS that produced the counted events, in
     * first-seen order, and how many events each contributed. A log outlives the
     * code it describes, and reading one era's numbers as current is a mistake
     * this project has now made twice in one session -- `run_tests` at 75% and
     * `format_file` at 0/3 were both reported as live defects and both had been
     * fixed weeks earlier. Nothing in the log said so, and `v` (the event schema)
     * looks enough like a version that nobody asks. A log spanning more than one
     * build is now stated outright. Empty for pre-M290 logs, which render
     * unchanged. */
    struct jc_vec versions;   /* of struct jc_telem_version */
    struct jc_vec models; /* of struct jc_telem_model */
    struct jc_vec tools;  /* of struct jc_telem_tool  */
    struct jc_vec aliases; /* of struct jc_telem_alias (M591) */
    struct jc_vec workspaces; /* of struct jc_telem_ws (M59) */
    struct jc_vec sessions;   /* of struct jc_telem_session (M82 timeline) */
};

/* Initialize an empty summary. */
void jc_telemetry_summary_init(struct jc_telemetry_summary *s);

/* Parse the JSONL `text` (one event per line; malformed lines are skipped) and
 * accumulate into `s` (which must be initialized). Pure. */
void jc_telemetry_feed(struct jc_telemetry_summary *s, const char *text);

/* Streaming variant of jc_telemetry_feed: read `f` one line at a time and
 * accumulate into `s` (which must be initialized). F-04: the telemetry log is
 * one compact JSON line per event with no rotation, so a long-lived workspace
 * crosses JC_READ_FILE_MAX (64 MiB, include/jc_platform.h) while every line is
 * still valid. `jc_read_file` refuses to slurp past that cap, which is how the
 * readers went blind. This path never holds the whole log: the peak is one
 * line plus `s`, whose vectors hold only DISTINCT names (models/tools/
 * workspaces/sessions, versions capped at 32) and do not grow with the line
 * count. A NULL `f` (the caller's fopen failed) returns JC_ERR_IO so the
 * caller can distinguish "no file" from "file present" and report it; an open
 * file (even one that later errors) returns JC_OK with whatever fed.
 * <stdio.h> is required here for FILE*. */
jc_status jc_telemetry_feed_file(struct jc_telemetry_summary *s, FILE *f);

/* Convenience: init + feed in one call. */
void jc_telemetry_summarize(const char *text, struct jc_telemetry_summary *out);

/* Render a human-readable report of `s` into `out` (a jc_sb). Pure. */
void jc_telemetry_render(const struct jc_telemetry_summary *s, struct jc_sb *out);

/* Release the model/tool vectors. */
/* M589: the floor of calls below which a model's `lat_max` is one slow call
 * rather than a tail worth warning about. Kept here beside the field it bounds,
 * so a reader of `lat_max` meets the caveat with the data. */
#define JC_DOCTOR_LAT_MIN_CALLS 5

void jc_telemetry_summary_free(struct jc_telemetry_summary *s);

/* M599: the default telemetry log for a workspace --
 * <home>/.jichi.d/telemetry/<basename>-<key>.jsonl, where <basename> is the
 * workspace's last path component with anything outside [A-Za-z0-9._-]
 * replaced by '_' (at most 40 bytes) and <key> is jc_workspace_key(workspace),
 * the same 64-hex SHA-256 digest the checkpoint store and the run lease use
 * (M1097/D-09: was a decimal djb2). One derivation,
 * used by the writer (main.c's event-log open) and by every reader
 * (jc_app_pick_telemetry_log), so a run writes where `learn analyze` reads
 * (the M533 rule). Appended across runs: one file per project is the shape the
 * miner needs -- one <run-id>.jsonl per run gave it a one-run memory. Pure;
 * `home`/`workspace` NULL read as "." */
void jc_telemetry_default_path(const char *home, const char *workspace,
                               char *buf, jc_size cap);

#ifdef __cplusplus
}
#endif
#endif /* JC_TELEMETRY_H */
