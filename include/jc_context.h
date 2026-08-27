/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_context.h - context-budget breakdown (`/context`, M41).
 *
 * Estimates what fills the model's context window for the current state — the
 * system prompt (and its components), the tool definitions, and the
 * conversation history — against the effective context limit, so a user or
 * agent can see where the budget is going and how close compaction is. Read-only
 * (no model call); uses the same byte heuristic as auto-compaction so the
 * numbers line up with when compaction actually fires.
 */
#ifndef JC_CONTEXT_H
#define JC_CONTEXT_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_message.h"

struct jc_app;                 /* jc_app.h      */
struct jc_telemetry_summary;   /* jc_telemetry.h */
struct jc_sb;  /* jc_str.h */

/* Render a context-budget report into `out`. `hist` may be NULL (e.g. the
 * `context` subcommand with no live conversation) — then history reads as 0. */
void jc_context_report(struct jc_app *app, const struct jc_history *hist,
                       struct jc_sb *out);

/* Per-tool definition sizes, largest first, with a cumulative column and a
 * marker for the tools `--tool-profile core` keeps (`context tools`, M313).
 *
 * A SEPARATE view rather than more lines in jc_context_report, deliberately the
 * opposite of what M312 did for the system prompt, because the measured shape
 * differs: there, six lines explained a 15,196-token block whose top item was
 * 10,441: the lines were the answer. Here eighteen lines would explain a
 * ~3,000-token block whose top item is ~330 -- top 5 is 49%, largest 12%, death
 * by a thousand cuts -- and printing them in the default report would push the
 * system-prompt lines off the screen. See docs/proposals/2026-08-per-tool-sizes.md.
 *
 * Sizes what is ACTUALLY advertised: the resolved tool profile's fence applies
 * (M310), so under `core` this lists the seven that are sent. Tokens come from
 * jc_compact_estimate_bytes_cal, the same conversion jc_context_report uses, so
 * the two reports cannot disagree. Read-only; no model call.
 *
 * M314: when `telem` is non-NULL the listing gains a `calls` column joined from a
 * telemetry summary, and a footer totalling what the never-called tools cost on
 * every model call. `label` describes the evidence (log name + scope) and is
 * printed with the turn/tool-call counts, because a conclusion drawn from one
 * turn must be discountable by the reader. `telem` NULL is the no-data case and
 * prints a stated absence rather than a column of zeroes -- telemetry is off by
 * default, so zeroes would read as "you use none of these".
 *
 * jc_telemetry stays pure and registry-unaware: the caller does the file I/O and
 * the workspace filtering, and this function only joins by tool name. */
void jc_context_tools_report(struct jc_app *app,
                             const struct jc_telemetry_summary *telem,
                             const char *label, struct jc_sb *out);

/* --- tools paid for vs called (M314 report, M316 doctor check) --------------
 *
 * Evidence thresholds for ADVISING on tool use. `context tools` reports whatever
 * one log holds and refuses to advise; a doctor check must be a verdict, so it
 * needs a bar the report does not try to clear.
 *
 * Sessions, not turns: one long session is still ONE TASK, and "never called in
 * any of your last three sessions" is a plural claim about different tasks, which
 * is what makes it informative. Both numbers are defensible rather than derived,
 * and conservative in the direction that matters -- a check that stays quiet too
 * long is a nuisance, one that advises too early is a liar. */
#define JC_TOOLUSE_MIN_SESSIONS 3
#define JC_TOOLUSE_MIN_CALLS    20

struct jc_tooluse_stats {
    int  advertised;      /* tools in the resolved (fenced) tool array   */
    int  unused;          /* of those, with zero recorded calls          */
    long unused_tokens;   /* their combined definition cost, per call    */
    int  sessions;        /* distinct sessions in the evidence           */
    long calls;           /* recorded tool calls in the evidence         */
    int  enough;          /* 1 when both thresholds above are met        */
};

/* Join the advertised tool array against a telemetry summary: how many tools were
 * never called, and what they cost on every model call.
 *
 * ONE definition of "unused", used by both the `context tools` footer and the
 * doctor check -- computing it twice is the drift M311/M312/M313 each had to undo.
 * Returns 1 when the stats were computed, 0 when there is no registry or no
 * summary (in which case *out is zeroed and `enough` is 0). */
int jc_context_tool_use(struct jc_app *app,
                        const struct jc_telemetry_summary *telem,
                        struct jc_tooluse_stats *out);

/* Where the HISTORY's tokens went (`context history`, M315): a role summary, a
 * per-tool aggregate over the tool results, and the largest single messages.
 *
 * The history is the part of the window that GROWS -- the part compaction exists
 * for, and the part that dominates a long --auto run -- and until M315 the report
 * said only "46 messages". The three questions it now answers, in the order they
 * are useful: which tool's output is filling the window, how the total splits
 * between user/assistant/tool, and whether one enormous message is the whole
 * story (it often is).
 *
 * Per-message sizes come from jc_compact_estimate_message, the exact term
 * jc_compact_estimate_tokens sums, so the role block sums to the total EXACTLY.
 * The per-tool block is a subset (tool results only) and states its own base --
 * a percentage that silently changes denominator between blocks is how a report
 * stops being trusted.
 *
 * Tool results are attributed by name via tool_call_id against the assistant
 * messages that requested them; a result whose call is no longer in the history
 * (compaction dropped the prefix) is charged to "(unknown)" -- a visible bucket,
 * never a silent shortfall. Message indices are printed so a size can be located
 * in `export --output json`.
 *
 * `label` names the evidence (e.g. a session id). Read-only; no model call. */
void jc_context_history_report(struct jc_app *app,
                               const struct jc_history *hist,
                               const char *label, struct jc_sb *out);

#ifdef __cplusplus
}
#endif
#endif /* JC_CONTEXT_H */

/* M340: the calibrated token estimate of the CACHEABLE PREFIX -- the tool array
 * plus the system prompt, which is the one block jichi's Anthropic breakpoint
 * covers (M31b). Returns -1 when it cannot be computed (no tool registry on the
 * app), so a caller can stay silent rather than report a wrong number.
 *
 * Shares advertised_tools() with jc_context_report so the profile fence is
 * applied identically -- the M311 lesson about two copies of one resolution. */
long jc_context_prefix_tokens(struct jc_app *app);
