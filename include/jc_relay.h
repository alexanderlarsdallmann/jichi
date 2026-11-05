/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_relay.h -- the context relay's decision core (M920, proposal
 * docs/proposals/2026-10-context-relay.md, section 6.2 and 6.13).
 *
 * THE PROBLEM, MEASURED (Review 2): a headless run is one long turn, so the
 * between-turn summary never runs in it (0 times in 366 runs); elision holds
 * a long loop in a sawtooth at 60-80% of the window, and seven loops ended on
 * the gateway's hard limit. When the window is TRULY exhausted, jichi today
 * ends the run on an HTTP 400 with stop_reason: error -- no notes, no way to
 * continue. The relay is the different mechanism the code already named at the
 * `unrelieved` field: a leg ends at a clean round boundary when exhaustion is
 * MEASURED, and (from the next milestone) hands a baton to a fresh leg.
 *
 * WHY THIS MODULE IS PURE (Q7): the decision is the part a wrong call costs the
 * most -- a handover is a cold start worth about seven fresh model calls
 * (Review 2 P5) -- so it must be a total, side-effect-free truth table unit
 * tested exactly like jc_toolloop and the envelope, and the loop must only
 * translate its verdict. Nothing here touches app, history or the clock.
 *
 * WHO PICKS THE MOMENT (section 6.1, choice 2): jichi, from numbers the round
 * has already measured -- never the model, which in the drives CLAIMED a
 * pressure signal that never came (P8).
 */
#ifndef JC_RELAY_H
#define JC_RELAY_H

#include <jc_platform.h>   /* jc_size, jc_status */
#include <jc_str.h>        /* jc_sb */

#ifdef __cplusplus
extern "C" {
#endif

/* What the loop must do at this round's end. */
enum relay_action {
    JC_RELAY_CONTINUE = 0, /* keep working; nothing to do            */
    JC_RELAY_HAND_OVER,    /* end this leg here (reason says why)    */
    JC_RELAY_STOP_STALLED  /* end the RELAY: no further legs         */
};

/* The five exhaustion triggers (section 6.2) plus the stall conditions
 * (section 6.13). The strings are the operator's explanation of a handover,
 * so they name the measurement, not the mechanism. */
enum relay_reason {
    JC_RELAY_NONE = 0,
    JC_RELAY_OVERFLOW,    /* E4: the server refused a request as too long */
    JC_RELAY_HARD_LIMIT,  /* E3: the server's count reached the hard limit */
    JC_RELAY_UNRELIEVED,  /* E1: two consecutive passes left it unrelieved */
    JC_RELAY_LATCHED,     /* E2: the lossy trims are skipped; nothing left */
    JC_RELAY_SHORT,       /* the "short" trigger: pressed, target missed   */
    JC_RELAY_LEG_BUDGET,  /* E5: this leg's tool-call budget is used       */
    JC_RELAY_MAX_LEGS,    /* the relay's leg ceiling                        */
    JC_RELAY_NO_PROGRESS  /* two legs in a row made no forward progress     */
};

/* The round's measurements -- the signals the loop already has at the
 * mid-turn compaction point. Every field is a fact, not a forecast (section
 * 6.2 rejects option 4 for exactly that reason). */
struct relay_signal {
    int     pressed;      /* the high-water trigger fired this pass     */
    int     reached;      /* the pass got to its target                 */
    int     unrelieved;   /* pass ran under pressure, found nothing     */
    int     latched;      /* the lossy trims were SKIPPED this pass     */
    int     overflow;     /* a request was refused as too long (E4)     */
    long    server_tokens;/* the SERVER's count of the last prompt      */
    long    limit;        /* the effective window (0 = unknown)         */
    jc_size leg_calls;    /* tool calls this leg has made (E5)          */
};

/* The leg's configuration. Defaults are the proposal's recommendations:
 * trigger "exhausted", E3 at 90% of the window. */
struct relay_cfg {
    int     relay_on;       /* 0: the core never fires (the default off) */
    int     trigger;        /* 0 = exhausted (E1-E4), 1 = short          */
    int     hard_pct;       /* E3's percentage, 50..95 (default 90)      */
    jc_size leg_calls_max;  /* E5; 0 = unset (no leg budget)             */
    int     legs_max;       /* the relay's leg ceiling (section 6.13)    */
};

/* The relay's running state: what the loop carries across rounds and legs. */
struct relay_state {
    int unrelieved_streak; /* consecutive unrelieved pressed passes      */
    int legs;              /* legs started in this relay (topology B)    */
    int no_progress_legs;  /* consecutive legs with no forward progress  */
};

/* Section 6.2, as a total function: no I/O, no clock, every branch a
 * measurement. The reason that fired is returned by name so a handover can
 * be explained from its numbers (section 6.1's "why" and 6.14's journal).
 *
 * The ORDER is the pseudocode's and matters: overflow and the hard limit are
 * the safety nets (they fire even when elision is still working), the
 * unrelieved streak is the default exhaustion, the latch is its sibling,
 * "short" is the operator's cheaper-on-cache choice, and the leg budget is
 * last because it is the only trigger that can fire on a round that measured
 * NO pressure at all. */
enum relay_action relay_decide(const struct relay_signal *sig,
                               const struct relay_cfg *cfg,
                               struct relay_state *state,
                               enum relay_reason *reason);

/* Section 6.13: checked once before a NEXT leg starts (topology B, later
 * milestone), so a relay cannot run away: a leg ceiling and a stall
 * detector (two legs with no forward progress). The caller reports progress
 * between checks; this function only enforces the bounds. */
enum relay_action relay_may_continue(const struct relay_state *state,
                                     const struct relay_cfg *cfg,
                                     enum relay_reason *reason);

/* M951: the V2 leg-budget controller's constants (proposal 6.10). TIGHTEN-ONLY
 * -- B_max is the operator's configured cap, so the controller can only shorten
 * a leg, never lengthen one, and it needs no config key of its own. */
#define JC_RELAY_BUDGET_MIN      25L   /* a leg never gets fewer than this  */
#define JC_RELAY_BUDGET_HIGH_PCT 80L   /* end the leg at 80% of the window  */
#define JC_RELAY_BUDGET_RESERVE  4000L /* tokens held for the handover call */

/* M951 (V2, proposal 6.10): the leg's budget controller. Given what the leg
 * that JUST ENDED measured, compute how many tool calls the NEXT leg may make
 * before its prompt reaches the high-water fraction of the window.
 *
 *   P0      the leg's first call's server prompt count (0 = not measured)
 *   g       the median per-tool-call prompt growth (0 = not growing)
 *   W       the effective window (0 = unknown)
 *   P0_next the NEXT leg's first prompt (P0 plus the baton's notes: bigger)
 *   cap     the operator's configured leg cap (B_max; 0 = no leg budget)
 *   fence   the relay's remaining call fence (0 = none set)
 *
 * n_fit = floor( (HIGH_PCT*W/100 - P0_next - RESERVE) / g );
 * B = clamp(n_fit, BUDGET_MIN, cap), then B = min(B, fence) -- the fence is
 * applied LAST and wins over the floor, so a relay with 3 calls left gives a
 * 3-call leg, not a 25-call one that would run the relay out.
 *
 * A TOTAL function -- no I/O, no clock, every branch a defined answer:
 *   cap == 0   -> out_B = 0 (no leg budget; the caller means "E5 off")
 *   g <= 0     -> not growing: the window is not filling, B = cap (no tighten)
 *   W <= 0     -> no window to measure against: B = cap
 *   num <= 0   -> P0_next alone is past the high-water: B = BUDGET_MIN (the
 *                 next leg must end very soon, but on a defined floor)
 * A P0_next smaller than P0 is a caller error (notes only grow the prompt), so
 * it is clamped UP to P0 and the answer stays defined. */
void relay_leg_budget(long P0, long g, long W, long P0_next,
                      jc_size cap, long fence, jc_size *out_B);

/* The name for a reason, for the log and the journal ("handover, reason:
 * unrelieved x2"). Never NULL. */
const char *relay_reason_name(enum relay_reason r);

/* --- M921: the baton (the notes the next leg starts from) --------------- */

/* How many entries of each top-N list the renderer prints (proposal 6.4:
 * "bounded by construction, top-N lists"; the lists are never cut, only the
 * model's part is). 20 keeps a ten-thousand-file run under the default
 * 8 KB cap with room for the account. */
#define JC_RELAY_BATON_TOPN 20

/* The mechanical facts jichi writes into the notes' section 2. Every field is
 * a record the run already holds -- the renderer re-derives nothing, so it can
 * be wrong only where jichi's own record was (which is the point of section
 * 6.4's split: what jichi KNEW, labelled, next to what the model CLAIMED).
 *
 * The top-N lists are pointers the caller owns (a scratch-arena copy of the
 * changed-since list, the uncommitted names, this leg's commits, the last
 * verify's failing files); the renderer borrows and prints, never frees. A
 * NULL array (or count 0) omits the line. `account` is the model's part:
 * NULL or "" makes section 3 the labelled "no account" placeholder. */
struct relay_baton_facts {
    const char *relay_id;   /* the relay's id (6.6: the store's dir name)   */
    int         leg;        /* this leg's number within the relay           */
    const char *reason;     /* the trigger's name (relay_reason_name)       */
    const char *task;       /* section 1: the original prompt, verbatim     */
    const char *commit;     /* the HEAD short-hash the run ended at         */
    int         dirty;      /* the tree had uncommitted changes at exit     */
    long        leg_calls;  /* tool calls this leg made                     */
    long        relay_calls;/* tool calls across the relay (== leg in A)    */
    long        fence_total;/* the relay's call fence (0 = none set)        */
    /* M951: the NEXT leg's tool-call budget, measured from this leg's growth
     * (relay_leg_budget at the handover). 0 = not measured (no series, no
     * cap, or the relay is off) -- the next leg then falls back to the
     * operator's configured cap. Carried across the process by the baton's
     * "Next-leg budget:" line, parsed by --handoff-in. */
    long        next_leg_budget;
    long        peak_prompt;/* this leg's largest prompt the server counted */
    /* the changed-since-relay-began list (paths, top-N)                    */
    const char **changed; int changed_n;
    /* the uncommitted list (paths, top-N)                                  */
    const char **uncommitted; int uncommitted_n;
    /* this leg's commits ("<sha> \"<subject>\"", top-N)                    */
    const char **commits; int commits_n;
    const char *todo;       /* the todo list as last written (or NULL)      */
    const char *last_verify;/* "green" / "red, N failures" / NULL           */
    const char **verify_fail; int verify_fail_n; /* the red's failing files  */
    /* the last tool calls, "<name> <ok|err>" pre-rendered by the caller,
     * most recent last (top-N)                                             */
    const char **last_calls; int last_calls_n;
    const char *account;    /* section 3: the model's part (or NULL)        */
    /* the model part's byte budget: cut first, at a UTF-8 boundary, with a
     * visible note (6.4's "Size and cuts"). 0 = no cut. The caller derives
     * this from the notes' cap (relayNotesBytes, sub-slice B) and the
     * rendered mechanical part's size -- the renderer itself only ever cuts
     * the account. */
    jc_size account_max;
};

/* Render the baton into `out` (a jc_sb the caller owns), proposal 6.4's
 * template: the header (relay id, leg, the trigger's name), section 1 the
 * task verbatim, section 2 the mechanical facts (the top-N lists, each line
 * omitted when empty), section 3 the model's part (the labelled cut when
 * account_max bites, the labelled placeholder when there is no account),
 * section 4 the fixed "how to use these notes" text.
 *
 * PURE: no clock, no I/O, no redaction (the caller runs the finished text
 * through jc_redact_secrets before it lands -- the M802 session-save
 * idiom -- so this renderer stays unit-testable without the global table).
 * Returns JC_OK on success, JC_ERR_OOM when a jc_sb grow failed OR when
 * the unticket pass failed to allocate (the caller then writes nothing --
 * the notes are all-or-nothing, never a half-baton). M1014c clarifies: the
 * unticket pass returns JC_ERR_OOM on malloc failure, not the "no match"
 * NULL, so an OOM degrades to writing the note with live ticket paths. The
 * renderer bounds the account but trusts every mechanical line to be short:
 * the top-N bound IS the construction, and a path longer than a line is a
 * caller defect. The notes' cap (relayNotesBytes) is the CALLER's job
 * (sub-slice B): render once without the account to size the mechanical part,
 * then derive account_max = cap - that size - the section-4 block (170) - a
 * headroom of 256 (the cut note is at most 44; each ticket-mark the renderer
 * replaces in the account costs +9), so the finished note never outruns the
 * cap. */
jc_status relay_baton_render(const struct relay_baton_facts *f,
                             struct jc_sb *out);

#ifdef __cplusplus
}
#endif

#endif /* JC_RELAY_H */
