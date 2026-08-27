/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_compact.h - automatic history compaction.
 *
 * Long sessions are kept within the model's context window by summarizing the
 * older part of the history into a single note and dropping the raw messages it
 * replaces, preserving the most recent turns verbatim. See docs/COMPACTION.md.
 *
 * The decision logic is pure and unit-tested (estimate / cut / render); the
 * orchestration (jc_compact_run) wires in a non-streaming model call and the
 * history rewrite and is verified end-to-end.
 */
#ifndef JC_COMPACT_H
#define JC_COMPACT_H


#ifdef __cplusplus
extern "C" {
#endif
#include "jc_platform.h"
#include "jc_message.h"

struct jc_app;            /* jc_app.h    */
struct jc_arena;          /* jc_mem.h    */
struct jc_agent_callbacks;/* jc_agent.h  */
struct jc_sb;             /* jc_str.h    */

/* Fallback context budget (tokens) when neither the config nor the model
 * declares one. */
#define JC_COMPACT_DEFAULT_LIMIT 32000

/* Fallback context (tokens) for the summarize-role model when it declares none.
 * Summarization is chunked to fit this so a small summarizer can't be sent more
 * than it can hold (the M30 HTTP-400 fix). */
#define JC_COMPACT_SUMMARIZER_DEFAULT 8192

/* Estimate the token cost of the whole history with the byte heuristic
 * (~bytes/4 plus a small per-message overhead). Pure. */
long jc_compact_estimate_tokens(const struct jc_history *hist);

/* Estimate the token cost of a NUL-terminated string with the same byte
 * heuristic (~bytes/4, no per-message overhead). 0 for NULL. Pure. Used by the
 * /context breakdown (M41) so its numbers match the compaction trigger. */
long jc_compact_estimate_text(const char *s);

/* The effective context-window budget (tokens) that drives compaction: config
 * `contextLimit` if set, else the active model's `contextLength`, else the
 * built-in default (JC_COMPACT_DEFAULT_LIMIT). */
long jc_compact_context_limit(struct jc_app *app);

/* M358: the explicitly configured limit (contextLimit, else the active
 * model's contextLength), 0 when only the built-in default would apply. */
long jc_compact_context_limit_explicit(struct jc_app *app);

/* M358: render the once-per-run [context] pressure note from a pressed
 * mid-turn pass's own numbers. No-op when before/limit are not real. */
void jc_compact_pressure_note(long before, long limit, int reached,
                              struct jc_sb *out);

/* The non-history part of a request (system prompt + tool schemas) in byte-estimate
 * tokens: measured on the most recent model call (M286), or a flat fallback before
 * any call has been made. Exposed so every consumer that adds it to a history
 * estimate uses ONE definition -- the drift between two such definitions is what
 * M286 fixed, and duplicating the fallback constant would reintroduce it. */
long jc_compact_nonhist_est(struct jc_app *app);

/* The one quantity every context threshold evaluates: calibrated history plus
 * the measured non-history part. Use this, never the two halves by hand -- the
 * TUI badge did that and under-read by the whole system prompt (M536).
 * tests/smoke/ctx_estimate_lint.sh enforces it. */
long jc_compact_effective_est(struct jc_app *app, const struct jc_history *hist);

/* The same budget for the model at `idx` in the config's models list, rather than
 * the active one -- so routing can compare two tiers' windows before switching
 * (M288). A global `contextLimit` overrides every model, so when one is set this
 * returns the same value for every index: that is the point, not a bug. Returns 0
 * for an out-of-range index. */
long jc_compact_context_limit_at(struct jc_app *app, int idx);

/* The active model's learned estimate->real token multiplier (M77), or 1.0 when
 * uncalibrated. The byte heuristic runs optimistic vs a real tokenizer, so every
 * consumer comparing the estimate to the context limit (the compaction trigger,
 * mid-turn elision, system-prompt fitting, the /context breakdown, the TUI
 * context%) scales by this to reflect real tokens. */
double jc_compact_calibration(struct jc_app *app);

/* jc_compact_estimate_tokens / jc_compact_estimate_text scaled by the active
 * model's calibration (M77). */
long jc_compact_estimate_tokens_cal(struct jc_app *app,
                                    const struct jc_history *hist);
long jc_compact_estimate_text_cal(struct jc_app *app, const char *s);

/* One message's estimate (content + tool-call payloads + a fixed per-message
 * overhead) -- the exact per-message term jc_compact_estimate_tokens sums, exposed
 * at M315 so the /context history breakdown can attribute the total it explains
 * without inventing a second definition of "message size". A breakdown whose parts
 * do not sum to the line above them is the drift M311/M312/M313 each had to undo.
 * Pure. */
long jc_compact_estimate_message(struct jc_message *m);
long jc_compact_estimate_message_cal(struct jc_app *app, struct jc_message *m);

/* The same estimate for a length already known in bytes, for a caller that
 * counted as it built rather than holding a string to measure (M312: the
 * system-prompt breakdown). Exists so the byte->token conversion has ONE
 * definition, for the reason stated above jc_compact_nonhist_est. Pure. */
long jc_compact_estimate_bytes(jc_size nbytes);
long jc_compact_estimate_bytes_cal(struct jc_app *app, jc_size nbytes);

/* Choose the cut index: messages [0, cut) are summarized away and [cut, end)
 * are kept. `cut` always points at a JC_ROLE_USER message so the kept tail is a
 * well-formed request prefix. Keeps the most recent turns that fit within
 * `keep_tokens`; if even the last user turn exceeds it, keeps just that turn.
 * Returns 0 when there is nothing worth compacting. Pure. */
jc_size jc_compact_find_cut(const struct jc_history *hist, long keep_tokens);

/* Apply a finished summary to `hist`: drop messages [0, cut) and prepend the
 * summary (with a marker) to the content of the first kept message. `cut` must
 * be >= 1 and point at a user message (as returned by jc_compact_find_cut).
 * `a` backs a temporary copy. Pure (no network); used by the orchestrator and
 * unit-tested directly. */
void jc_compact_apply(struct jc_history *hist, jc_size cut, const char *summary,
                      struct jc_arena *a);

/* Render messages [start, end) as a plain-text transcript for the summarizer.
 * Per-message content is truncated to keep the request bounded. Allocated from
 * `a`. Returns NULL only on allocation failure. Pure. */
char *jc_compact_render_range(const struct jc_history *hist, jc_size start,
                              jc_size end, struct jc_arena *a);

/* Render messages [0, end) (i.e. render_range from 0). */
char *jc_compact_render_transcript(const struct jc_history *hist, jc_size end,
                                   struct jc_arena *a);

/* Largest message window starting at `start` whose estimated token cost fits
 * `budget_tokens`: returns the exclusive end index. Always advances at least
 * one message (so an oversized single message still makes progress), and never
 * past the end of the history. Pure; used to chunk the prefix so each
 * summarizer call fits the summarize model's context (M30). */
jc_size jc_compact_window_end(const struct jc_history *hist, jc_size start,
                              long budget_tokens);

/* Run auto-compaction on `hist` if the estimate exceeds the trigger. On a
 * compaction, summarizes the old prefix with a (non-streaming) model call and
 * rewrites `hist` in place; sets *did_compact (if non-NULL) to 1. A no-op (and
 * *did_compact = 0) when below threshold or when there is nothing to fold.
 * Never fails the turn: a summarization error leaves `hist` untouched and
 * returns JC_OK. Top-level agent only. */
jc_status jc_compact_run(struct jc_app *app, struct jc_history *hist,
                         const struct jc_agent_callbacks *cb, int *did_compact);

/* Force a compaction now, ignoring the trigger threshold (still a no-op when
 * there's nothing old enough to fold). Used by the TUI `/compact` command. */
jc_status jc_compact_force(struct jc_app *app, struct jc_history *hist,
                           int *did_compact);

/* Mid-turn compaction (M76). Between-turn compaction can't help a SINGLE turn
 * whose own tool churn overflows the context window. This bounds the in-flight
 * history by ELIDING the content of the oldest large tool-result messages
 * (replacing it with a head+tail+marker) until the estimate fits
 * `budget_tokens`, never touching the last `keep_recent` messages. It only
 * shrinks content, so tool_call<->tool_result pairing stays intact and the
 * request stays well-formed; no model call (instant). Returns the number of
 * messages elided. Pure; unit-tested. */
jc_size jc_compact_trim_tool_output(struct jc_history *hist, long budget_tokens,
                                    jc_size keep_recent);

/* M348: a claim-ticket writer for the lossy pass above. Called with the FULL
 * original content of a tool result about to be elided; returns 1 and the
 * file's path in `path_out` when the bytes were preserved (the marker then
 * names that path so the model can retrieve exactly what was taken instead of
 * re-running the original call -- the measured re-read loop: 72% of reads
 * were re-reads, one path 216x, and 82 of 142 advisory-firing re-reads
 * immediately followed another read_file). Returns 0 when preservation
 * failed; the marker then falls back to the old ticketless text (D3: the
 * model must never be given a path that is not there). Kept as a callback so
 * the trim pass stays pure and unit-testable with a stub. */
typedef int (*jc_compact_spill_fn)(void *ctx, const char *text, jc_size len,
                                   char *path_out, jc_size path_cap);

/* The ticket-writing form of the trim pass: identical behaviour, plus each
 * elision offers its full content to `spill` (NULL => ticketless, the old
 * behaviour exactly). `preserved_out` (may be NULL) counts elisions whose
 * marker names a ticket. */
jc_size jc_compact_trim_tool_output_ex(struct jc_history *hist,
                                       long budget_tokens,
                                       jc_size keep_recent,
                                       jc_compact_spill_fn spill,
                                       void *spill_ctx,
                                       jc_size *preserved_out);

/* M93: superseded-read elision. On a cacheless backend the model re-reads the
 * same large files many times (dogfood: 84% of read_file calls were repeats,
 * codegen.zig 93x), each re-injecting the whole file and lingering in history.
 * This elides a `read_file` result whose path is read AGAIN later in `hist` (the
 * later read carries the current content, so the earlier copy is a pure
 * duplicate) -- head+tail+marker like jc_compact_trim_tool_output, never touching
 * the last `keep_recent` messages, stopping once the estimate fits
 * `budget_tokens`. Zero information loss (the newest read of each file is kept).
 * jc_compact_midturn runs this BEFORE the age-based pass so duplicates go first.
 * Returns the number of messages elided. Unit-tested.
 *
 * `cwd` (M192) makes path identity spelling-independent: the model reads the same
 * file both as `src/vm.zig` and as `/abs/src/vm.zig`, and a raw strcmp treats those
 * as two files, so each spelling retains its own full copy (measured: 4 files
 * spelled two ways, ~94 KB held resident in one dogfood log). Paths are compared
 * after jc_path_normalize against `cwd`; a path it refuses (".." -- see jc_path.h)
 * falls back to its raw spelling, so a missed dedup is the worst case. NULL `cwd`
 * disables normalization entirely (raw strcmp, the pre-M192 behaviour). */
jc_size jc_compact_trim_superseded_reads(struct jc_history *hist,
                                         long budget_tokens,
                                         jc_size keep_recent,
                                         const char *cwd);

/* The key M218's argument-elision marker carries, and the guard against the
 * model IMITATING it (M289).
 *
 * The marker replaces an oversized `arguments_json` with a small valid JSON
 * object so both provider serializers stay clean. That object sits in history in
 * the arguments slot -- which the model reads as an example of what a call to
 * that tool looks like. It duly copied the shape back: on one measured run 18 of
 * 19 argument-shape failures were this marker arriving as real tool arguments,
 * for `edit_file`, `write_file`, `todo_write` and `run_terminal_command`. One
 * came back paraphrased ("elided mid-turn to request"), a wording that exists
 * nowhere in jichi -- which is what proves the model retyped it rather than
 * jichi mis-replaying it. Each one cost a full uncached round-trip and produced
 * a generic "'path', 'old_string' and 'new_string' are required", telling the
 * model nothing about what had happened.
 *
 * No tool declares a parameter by this name (parameters are short generics --
 * path, content, command, query), the same reasoning M172 used for the
 * self-named-wrapper unwrap, so its presence is an unambiguous signal. The tool
 * layer detects it and answers with what the model actually needs. */
#define JC_COMPACT_ELIDED_KEY "elided"

/* M218: mid-turn elision of assistant TOOL-CALL ARGUMENTS. The result-side
 * trims above never touch the arguments side of history, and for the mutating
 * tools (write_file / apply_patch / edit_file) `arguments_json` carries a full
 * file body -- in a marathon single turn (hundreds of calls, between-turn
 * compaction never runs) that side grows monotonically. This replaces an old
 * call's oversized arguments with a compact marker, oldest-first, skipping the
 * last `keep_recent` messages and stopping once the estimate fits
 * `budget_tokens`. The marker is a VALID JSON OBJECT
 * (`{"elided":"arguments (N bytes) ...","path":"..."}`, keeping the call's
 * path so the model still knows what it wrote): the Anthropic serializer
 * re-parses arguments_json (invalid JSON degrades to _unparsed_arguments) and
 * the OpenAI one emits it verbatim, so both wires stay clean. `id`/`name` and
 * the paired tool result are untouched. Args at or under ELIDE_MIN_BYTES are
 * skipped -- the idempotence guard (the marker itself is small), and it keeps
 * read_file/search_code arguments intact for the M94 dedup's path lookup.
 * Returns the number of calls elided. Pure; unit-tested. */
jc_size jc_compact_trim_tool_args(struct jc_history *hist, long budget_tokens,
                                  jc_size keep_recent);

/* Run mid-turn compaction if the in-flight history (plus a system/tools
 * overhead allowance) crosses a high-water fraction of the effective context
 * limit; trims down to a lower target via jc_compact_trim_tool_output. Logs and
 * fires on_status when it acts. Returns the number of messages elided (0 = no-op
 * / unknown budget). Called from the agent loop after each round of tool
 * results.
 *
 * `rep->dup` receives how many of those elisions came from the
 * ZERO-LOSS superseded-read pass, as opposed to the lossy age-based one (M192).
 * The two have very different costs, so a caller reporting them -- the `compact`
 * telemetry event does -- must be able to tell them apart; the merged total alone
 * made the dedup's effectiveness unmeasurable.
 *
 * `rep->args` (M218) receives how many tool CALLS had their arguments elided by
 * jc_compact_trim_tool_args -- run only when the result-side trims left the
 * estimate above target. Counted separately so `dup + age` keeps meaning content
 * elisions: dup + (return - dup - args) + args == return.
 *
 * `dedup_hint` (M218) gates the eager superseded-read pass: pass 1 on the
 * first call of a turn (a resumed history may carry duplicates) and on any
 * round that appended a read_file result; 0 skips the pass -- a round that
 * added no read cannot have created a new superseded pair, so skipping is
 * bit-identical in outcome and avoids its per-message argument parses. The
 * budget-pressure trims below are unaffected by the hint. */
/* M323: what the pass actually achieved, so a log can distinguish "it worked"
 * from "it ran out of things to shrink and the request went out over budget".
 *
 * A 34,216-event workload (docs/analysis/2026-08-06-large-workload-telemetry.md)
 * ran 1,038 mid-turn compactions and STILL sent 3.1% of its calls over the
 * configured contextLimit -- up to 1.36x the model's declared window -- because
 * this pass's lever is LARGE tool results and that history was thousands of small
 * ones (p99 output 14 KB; one result above 100 KB in 13,783 calls). The event
 * recorded only elided/dup/age, so none of that was visible from the log.
 *
 * `pressed` is the load-bearing field: it says the high-water trigger fired, so a
 * report is emitted even when `elided` is 0 -- which is precisely the case that
 * used to be silent. `reached` says the target was met. `before`/`after`/`limit`/
 * `target` are CALIBRATED real-token terms (M77), the same units the trigger
 * compares, so they line up with the decision that was made. */
struct jc_midturn_report {
    jc_size elided;   /* total content+args elisions (== the return value)   */
    jc_size dup;      /* zero-loss superseded-read elisions (M93/M94)        */
    jc_size args;     /* tool CALLS whose arguments were elided (M218)       */
    long    before;   /* calibrated estimate before the pressure trims       */
    long    after;    /* ...and after                                        */
    long    limit;    /* effective context limit (0 => unknown, pass is a
                       * no-op beyond the eager dedup)                       */
    long    target;   /* what the trims aimed for                            */
    int     pressed;  /* the high-water trigger fired                        */
    int     reached;  /* after <= target                                     */
    /* M326y: the pass ran under pressure and the LOSSY trims found nothing to
     * elide -- distinct from `!reached`, which means it tried and fell short.
     * Measured decay of reclaim by pass index within a turn (median tokens):
     * 1st 10,324 / 2nd 1,306 / 3rd ~0 / 26th+ negative. Elided content drops
     * under ELIDE_MIN_BYTES and is never re-elided, and newer results are
     * keep-recent protected, so after two passes there is usually nothing left.
     * 174 of 593 pressured passes in the measured workload were the 26th or
     * later in their turn, each scanning the whole history to reclaim nothing.
     * A reader that cannot see this reads 98 compactions as effort.
     *
     * Defined by EFFECT: the pass ended still above the high-water, so it will
     * re-trigger on the very next round. That is not `!reached` -- a pass can
     * miss the 60% target and still drop under the 80% trigger, buying quiet
     * rounds. Counting elisions instead was the first attempt and could not
     * express it: the repeating passes do elide something, usually one small
     * item, and reclaim nothing. */
    int     unrelieved;
    jc_size preserved; /* M348: lossy elisions whose full content was written
                        * to the claim-ticket store (marker names the path) */
    int     latched;   /* M361: the lossy trims were SKIPPED this pass -- a
                        * previous pass proved the eligible range dry and the
                        * keep-recent window has not yet released a candidate */
};

/* M361: the exhaustion latch. When a pressed pass elides NOTHING (the
 * eligible range is dry -- everything old and large is already elided, the
 * rest is keep-recent protected), re-running the lossy scans every round
 * reclaims nothing: 174 of 593 pressured passes in the measured workload were
 * the 26th or later in their turn. The latch records the EXACT history length
 * at which the sliding window next releases a candidate (oldest protected
 * candidate index + keep + 1; with no candidate, len + keep + 1), and the
 * lossy trims are skipped until then. Bounded by construction: every latch
 * expires within KEEP_RECENT+1 appends, so a conservative candidate detector
 * can only delay one scan, never skip one forever -- the subtlety that kept
 * this deferred. A history that SHRANK below its latch-time length re-arms
 * immediately (indices moved; the math is stale). Owned by the agent loop,
 * one per (sub)turn; NULL disables the latch entirely. */
struct jc_midturn_latch {
    jc_size rearm_len; /* run the lossy trims again at this length; 0 = off */
    jc_size latch_len; /* history length when the latch was set             */
};

/* M361, pure: the history length at which the keep-recent window next
 * releases an elidable candidate (a tool result, or a tool call's arguments,
 * larger than min_bytes). With no protected candidate, returns
 * len + keep_recent + 1 (the horizon at which a message appended NOW would
 * unprotect). */
jc_size jc_compact_rearm_len(const struct jc_history *hist,
                             jc_size keep_recent, jc_size min_bytes);

/* `rep` may be NULL. It is zeroed on entry, so a caller can read every field
 * unconditionally afterwards. */
jc_size jc_compact_midturn(struct jc_app *app, struct jc_history *hist,
                           const struct jc_agent_callbacks *cb,
                           struct jc_midturn_report *rep,
                           int dedup_hint,
                           struct jc_midturn_latch *latch);

#ifdef __cplusplus
}
#endif
#endif /* JC_COMPACT_H */
