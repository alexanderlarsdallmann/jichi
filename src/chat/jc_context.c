/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_context.c - context-budget breakdown (see jc_context.h). */

#include "jc_context.h"
#include "jc_app.h"
#include "jc_compact.h"
#include "jc_meminfo.h"
#include "jc_autocontext.h"
#include "jc_sysmsg.h"
#include "jc_tool.h"
#include "jc_skill.h"
#include "jc_output_style.h"
#include "jc_str.h"
#include "jc_json.h"
#include "jc_telemetry.h"

#include <stdlib.h>
#include <string.h>

/* M312: output_style_tokens() and skills_tokens() lived here, each re-rendering
 * one section to measure it. Both are gone: jc_sysmsg_build_parts reports every
 * section's size as it builds, so a second description of the prompt -- the kind
 * that drifts when a section is added to the builder alone -- is no longer
 * needed for any of them. */

/* The tool array as the model will receive it.
 *
 * M310: this MUST apply the resolved tool profile's fence, because the reports
 * built on it answer "where is my context window going" and the window is spent
 * on what is actually sent. Before M310 the caller built the unfenced array, so
 * under `toolProfile: core` -- which `auto` resolves to by itself under --lite or
 * an effective context below JC_TOOL_PROFILE_AUTO_BELOW -- it over-reported the
 * one line the user came to read. The gauge was wrong in exactly the
 * configuration a user adopts to fix the problem the gauge exists to diagnose.
 *
 * Same call, same limit, as jc_agent_run_turn: one resolution, so the reports
 * cannot drift from the request. Factored out at M313 so the summary line and the
 * per-tool view share that one resolution -- the M311 lesson about two copies of
 * the same decision, applied before there could be two.
 *
 * Caller owns the returned cJSON (may be NULL). *core_out reports whether the
 * fence applied, so a caller can say so: a count dropping 16 -> 7 unexplained is
 * its own puzzle. */
static cJSON *advertised_tools(struct jc_app *app, int *core_out)
{
    struct jc_vec core_allow;
    const struct jc_vec *allow = NULL;
    cJSON *arr;

    if (core_out != NULL) {
        *core_out = 0;
    }
    if (app->tools == NULL) {
        return NULL;
    }
    jc_vec_init(&core_allow, sizeof(const char *));
    if (jc_config_tool_profile_core(&app->config,
                                    jc_compact_context_limit(app))) {
        jc_tool_core_allow(&core_allow);
        allow = &core_allow;
        if (core_out != NULL) {
            *core_out = 1;
        }
    }
    /* Depth 0: /context and the `context` subcommand report the TOP-LEVEL
     * agent's window, which is the one the operator is budgeting. */
    arr = jc_tool_build_neutral_ex(app->tools, 1, &app->config.permissions,
                                   NULL, NULL, allow, 0);
    jc_vec_free(&core_allow);
    return arr;
}

static long tool_tokens(struct jc_app *app, int *ntools, int *core_out)
{
    cJSON *arr;
    long t = 0;
    if (ntools != NULL) {
        *ntools = 0;
    }
    arr = advertised_tools(app, core_out);
    if (arr == NULL) {
        return 0;
    }
    {
        char *s = jc_json_print(arr);
        t = jc_compact_estimate_text_cal(app, s);
        free(s);
    }
    if (ntools != NULL) {
        *ntools = cJSON_GetArraySize(arr);
    }
    cJSON_Delete(arr);
    return t;
}

/* The system prompt's sections, non-zero only, largest first (M312).
 *
 * Largest-first rather than prompt order because the report answers "what do I
 * cut", not "what is in here" -- a user who opens it has a full window, not
 * curiosity. Non-zero only because fourteen zeroes on a typical project bury the
 * two lines that matter. There is no unnamed remainder: the parts sum to the
 * built prompt by construction (see jc_sysmsg.h), which is the whole point --
 * the pre-M312 line named six sub-parts and left "the base persona + section
 * headers" unaccounted, so on a graded attempt (rules skipped, no repo map) it
 * explained nothing at all.
 *
 * O(n^2) selection over 16 slots, once per report. */
static void append_sysmsg_parts(struct jc_app *app,
                                const struct jc_sysmsg_parts *parts,
                                struct jc_sb *out)
{
    int shown[JC_SYSPART_COUNT];
    int i, j;

    for (i = 0; i < JC_SYSPART_COUNT; i++) {
        shown[i] = 0;
    }
    for (i = 0; i < JC_SYSPART_COUNT; i++) {
        int best = -1;
        jc_size best_n = 0;
        for (j = 0; j < JC_SYSPART_COUNT; j++) {
            if (!shown[j] && parts->bytes[j] > best_n) {
                best = j;
                best_n = parts->bytes[j];
            }
        }
        if (best < 0) {
            break;              /* every remaining slot is zero */
        }
        shown[best] = 1;
        jc_sb_append_fmt(out, "    %-16s~%ld\n", jc_sysmsg_part_name(best),
                         jc_compact_estimate_bytes_cal(app,
                                                       parts->bytes[best]));
    }
    if (parts->total == 0) {
        jc_sb_append(out, "    (empty)\n");
    }
}

void jc_context_report(struct jc_app *app, const struct jc_history *hist,
                       struct jc_sb *out)
{
    long limit = jc_compact_context_limit(app);
    long sys, tools, history, total;
    int ntools = 0;
    int core_profile = 0;
    struct jc_sysmsg_parts parts;
    jc_size nmsg = (hist != NULL)
                       ? jc_history_len((struct jc_history *)hist)
                       : 0;
    const char *model = (app->config.model.name != NULL)
                            ? app->config.model.name
                            : app->config.model.model;

    sys = jc_compact_estimate_text_cal(app, jc_sysmsg_build_parts(app, &parts));
    tools = tool_tokens(app, &ntools, &core_profile);
    history = (hist != NULL) ? jc_compact_estimate_tokens_cal(app, hist) : 0;
    total = sys + tools + history;

    jc_sb_append_fmt(out, "Context window: model %s, limit ~%ld tokens\n\n",
                     model != NULL ? model : "?", limit);

    jc_sb_append_fmt(out, "  system prompt     ~%ld\n", sys);
    append_sysmsg_parts(app, &parts, out);
    jc_sb_append_fmt(out, "  tool definitions  ~%ld  (%d tools%s)\n", tools,
                     ntools, core_profile ? ", core profile" : "");
    jc_sb_append_fmt(out, "  history           ~%ld  (%lu message%s)\n",
                     history, (unsigned long)nmsg, nmsg == 1 ? "" : "s");
    jc_sb_append(out, "  ----\n");
    jc_sb_append_fmt(out, "  total             ~%ld", total);
    if (limit > 0) {
        jc_sb_append_fmt(out, "  (%ld%% of limit)", total * 100 / limit);
    }
    jc_sb_append(out, "\n\n");

    jc_sb_append_fmt(out,
        "Auto-compaction: %s; triggers when the conversation reaches ~80%% of "
        "the limit (~%ld tokens).\n",
        app->config.auto_compact ? "on" : "off", limit * 4 / 5);

    /* M536: the number the trigger and the prompt badge ACTUALLY evaluate,
     * printed beside the breakdown above -- because they are not the same
     * quantity and pretending otherwise is how this page and the badge came to
     * disagree by the whole system prompt.
     *
     * The breakdown is a FRESH estimate: it rebuilds the system message and
     * re-counts the tool schemas right now. The trigger uses the non-history
     * cost MEASURED on the last real model call (M286) and scales the sum by the
     * model's learned calibration ratio (M77). Same intent, different evidence,
     * so the two percentages differ legitimately -- but only if a reader can see
     * both. One number on screen and a different one governing the machine is
     * exactly the seam this milestone was about. */
    /* hist may be NULL -- `jichi describe` and the headless one-shot paths call
     * this with no history at all, which is why the `history` row above is
     * guarded. The first cut of this line was not, and segfaulted in
     * sysmsg_env.sh: the same "a neighbour reads a guard I did not" shape this
     * milestone is about, committed three lines below the guard itself. */
    if (limit > 0 && hist != NULL) {
        long eff = jc_compact_effective_est(app, hist);
        jc_sb_append_fmt(out,
            "  the trigger evaluates ~%ld (%ld%% of limit): the calibrated "
            "history plus the non-history cost measured on the last call, "
            "which is also what the prompt's ctx%% badge shows.\n",
            eff, eff * 100 / limit);
    }

    /* Auto-context (M61): retrieved chunks ride on the user message, so they
     * are already counted in `history` above; report the per-turn budget. */
    if (app->config.auto_context) {
        jc_sb_append_fmt(out,
            "Auto-context: on; up to ~%ld tokens of retrieved code/docs are "
            "injected per turn (already counted in history).\n",
            jc_autocontext_budget(limit, app->config.auto_context_max_tokens));
    }

    /* Process memory (M140): the two arenas' footprints, so a growing session
     * arena is visible from the TUI instead of only to a heap profiler. The
     * numbers are the arenas only (history is malloc'd and shown above as
     * tokens); cap >= used, the gap being block tails kept for reuse. */
    {
        jc_size acap = 0, scap = 0;
        jc_size aused = jc_arena_used(app->arena, &acap);
        jc_size sused = jc_arena_used(app->scratch, &scap);
        long rss_kb = 0;
        long hwm_kb = 0;
        jc_sb_append_fmt(out,
            "Arenas: session %lu KB used (%lu KB reserved)",
            (unsigned long)(aused / 1024), (unsigned long)(acap / 1024));
        if (app->scratch != NULL) {
            jc_sb_append_fmt(out, "; turn scratch %lu KB used (%lu KB reserved)",
                             (unsigned long)(sused / 1024),
                             (unsigned long)(scap / 1024));
        }
        /* M199: the per-tool-call arena. Normally near zero here -- it is reset
         * before every tool call -- so a large number means a single tool call
         * read something huge, which is exactly what one wants to see. */
        if (app->tool_scratch != NULL) {
            jc_size tcap = 0;
            jc_size tused = jc_arena_used(app->tool_scratch, &tcap);
            jc_sb_append_fmt(out, "; tool scratch %lu KB used (%lu KB reserved)",
                             (unsigned long)(tused / 1024),
                             (unsigned long)(tcap / 1024));
        }
        jc_sb_append(out, ".\n");
        /* M180: the whole process, not just the arenas -- RSS now, and the
         * high-water mark since start. The gap between RSS and the arenas
         * is history (malloc'd), buffers, and the C library. */
        if (jc_meminfo_self(&rss_kb, &hwm_kb)) {
            jc_sb_append_fmt(out,
                "Process: %ld KB resident (peak %ld KB).\n", rss_kb, hwm_kb);
        }
    }
}

/* --- `context tools` (M313) --------------------------------------------------
 *
 * Per-tool sizes, largest first. See jc_context.h for why this is a separate
 * view rather than more lines in the main report. Selection sort over the array
 * (tens of entries, once), so no allocation beyond the serialized entries. */
/* Calls recorded for `name` in a telemetry summary, or -1 when the summary has
 * no entry for it at all. The distinction is load-bearing: "recorded, zero calls"
 * and "not in this log" are different facts, and only the first is evidence. Both
 * render as 0 here because an advertised tool absent from the log was, in that
 * log, not called -- but the caller states the evidence base so the reader can
 * weigh it. Joined by NAME, which is what keeps jc_telemetry registry-unaware. */
static long telem_calls(const struct jc_telemetry_summary *t, const char *name)
{
    jc_size i;
    if (t == NULL || name == NULL) {
        return -1;
    }
    for (i = 0; i < t->tools.len; i++) {
        const struct jc_telem_tool *e =
            (const struct jc_telem_tool *)jc_vec_at((struct jc_vec *)&t->tools, i);
        if (strcmp(e->name, name) == 0) {
            return e->calls;
        }
    }
    return -1;
}

int jc_context_tool_use(struct jc_app *app,
                        const struct jc_telemetry_summary *telem,
                        struct jc_tooluse_stats *out)
{
    cJSON *arr;
    int i, n;
    jc_size k;
    long unused_bytes = 0;

    if (out == NULL) {
        return 0;
    }
    memset(out, 0, sizeof(*out));
    if (telem == NULL) {
        return 0;
    }
    arr = advertised_tools(app, NULL);
    if (arr == NULL) {
        return 0;
    }
    n = cJSON_GetArraySize(arr);
    for (i = 0; i < n; i++) {
        cJSON *e = cJSON_GetArrayItem(arr, i);
        cJSON *fn = (e != NULL) ? cJSON_GetObjectItem(e, "function") : NULL;
        cJSON *nm = (fn != NULL) ? cJSON_GetObjectItem(fn, "name")
                                 : ((e != NULL) ? cJSON_GetObjectItem(e, "name")
                                                : NULL);
        const char *name = (nm != NULL && nm->valuestring != NULL)
                               ? nm->valuestring : "?";
        out->advertised++;
        if (telem_calls(telem, name) <= 0) {
            char *sz = (e != NULL) ? jc_json_print(e) : NULL;
            out->unused++;
            unused_bytes += (sz != NULL) ? (long)strlen(sz) : 0;
            free(sz);
        }
    }
    cJSON_Delete(arr);
    out->unused_tokens = jc_compact_estimate_bytes_cal(app,
                                                       (jc_size)unused_bytes);
    out->sessions = (int)telem->sessions.len;
    for (k = 0; k < telem->tools.len; k++) {
        const struct jc_telem_tool *e =
            (const struct jc_telem_tool *)
            jc_vec_at((struct jc_vec *)&telem->tools, k);
        out->calls += e->calls;
    }
    out->enough = (out->sessions >= JC_TOOLUSE_MIN_SESSIONS &&
                   out->calls >= JC_TOOLUSE_MIN_CALLS);
    return 1;
}

void jc_context_tools_report(struct jc_app *app,
                             const struct jc_telemetry_summary *telem,
                             const char *label, struct jc_sb *out)
{
    cJSON *arr;
    int core_profile = 0;
    int n, i, j;
    long *sizes;
    char **names;
    int *taken;
    long total = 0;
    long cum = 0;
    long core_bytes = 0;
    int core_count = 0;
    struct jc_tooluse_stats use;    /* M316: the shared join, for the footer */

    arr = advertised_tools(app, &core_profile);
    n = (arr != NULL) ? cJSON_GetArraySize(arr) : 0;
    if (n <= 0) {
        jc_sb_append(out, "No tool definitions are advertised.\n");
        if (arr != NULL) {
            cJSON_Delete(arr);
        }
        return;
    }

    sizes = (long *)malloc(sizeof(long) * (unsigned long)n);
    names = (char **)malloc(sizeof(char *) * (unsigned long)n);
    taken = (int *)malloc(sizeof(int) * (unsigned long)n);
    if (sizes == NULL || names == NULL || taken == NULL) {
        free(sizes); free(names); free(taken);
        cJSON_Delete(arr);
        jc_sb_append(out, "(out of memory sizing the tool definitions)\n");
        return;
    }

    /* Each entry measured on its own, so the numbers are per tool rather than a
     * share of the whole. That leaves the array's own framing (brackets and
     * commas) charged to no tool -- 17 bytes for 16 tools, ~5 tokens. It gets no
     * line of its own, because a line for 5 tokens is noise; but the header
     * states the WHOLE-array figure, the same number jc_context_report prints,
     * and names the framing as the difference. Two reports on the same thing must
     * not show two totals: that is the drift M310/M311/M312 each fixed, and it
     * would be a poor joke to reintroduce it in the report about tool sizes. */
    for (i = 0; i < n; i++) {
        cJSON *e = cJSON_GetArrayItem(arr, i);
        cJSON *fn = (e != NULL) ? cJSON_GetObjectItem(e, "function") : NULL;
        cJSON *nm = (fn != NULL) ? cJSON_GetObjectItem(fn, "name")
                                 : ((e != NULL) ? cJSON_GetObjectItem(e, "name")
                                                : NULL);
        char *s = (e != NULL) ? jc_json_print(e) : NULL;
        sizes[i] = (s != NULL) ? (long)strlen(s) : 0;
        names[i] = (nm != NULL && nm->valuestring != NULL) ? nm->valuestring
                                                           : (char *)"?";
        taken[i] = 0;
        total += sizes[i];
        if (jc_tool_is_core(names[i])) {
            core_bytes += sizes[i];
            core_count++;
        }
        free(s);
    }

    {
        char *whole = jc_json_print(arr);
        long array_tok = (whole != NULL)
                             ? jc_compact_estimate_text_cal(app, whole)
                             : 0;
        long sum_tok = jc_compact_estimate_bytes_cal(app, (jc_size)total);
        free(whole);
        jc_sb_append_fmt(out,
            "Tool definitions: %d advertised, ~%ld tokens (%s profile)\n",
            n, array_tok, core_profile ? "core" : "full");
        if (array_tok > sum_tok) {
            jc_sb_append_fmt(out,
                "  (~%ld of that is JSON array framing, charged to no tool)\n",
                array_tok - sum_tok);
        }
        jc_sb_append(out, "\n");
    }
    jc_sb_append(out, (telem != NULL)
                     ? "  tokens   share   cum.   core  calls  tool\n"
                     : "  tokens   share   cum.   core  tool\n");

    for (i = 0; i < n; i++) {
        int best = -1;
        long best_n = -1;
        for (j = 0; j < n; j++) {
            if (!taken[j] && sizes[j] > best_n) {
                best = j;
                best_n = sizes[j];
            }
        }
        if (best < 0) {
            break;
        }
        taken[best] = 1;
        cum += sizes[best];
        jc_sb_append_fmt(out, "  %6ld  %5ld%%  %4ld%%    %s ",
                         jc_compact_estimate_bytes_cal(app,
                                                       (jc_size)sizes[best]),
                         (total > 0) ? sizes[best] * 100 / total : 0,
                         (total > 0) ? cum * 100 / total : 0,
                         jc_tool_is_core(names[best]) ? "*" : " ");
        if (telem != NULL) {
            long c = telem_calls(telem, names[best]);
            jc_sb_append_fmt(out, "%6ld ", (c > 0) ? c : 0);
        }
        jc_sb_append_fmt(out, "  %s\n", names[best]);
    }

    /* The cumulative column exists because the FLATNESS is the finding (top 5 is
     * ~49%, largest ~12%): without it a reader sees 12% at the top and looks for
     * a fat tool to remove. The footer answers the question that actually
     * follows -- what would --tool-profile core save me -- rather than leaving it
     * to be cross-referenced from docs/analysis/. */
    jc_sb_append(out, "\n");
    if (core_profile) {
        jc_sb_append(out,
            "The core profile is active: these are the tools the model is sent. "
            "Set toolProfile / --tool-profile full to advertise all of them.\n");
    } else {
        jc_sb_append_fmt(out,
            "* = kept by the core profile: %d of %d tools, ~%ld tokens (%ld%% of "
            "the above). --tool-profile core drops the rest.\n",
            core_count, n, jc_compact_estimate_bytes_cal(app,
                                                         (jc_size)core_bytes),
            (total > 0) ? core_bytes * 100 / total : 0);
    }

    /* M325b: MCP tools are the one part of a live turn's toolset this report
     * cannot show, because discovering them means connecting to every configured
     * server and a read-only report must not. Say so rather than let the count
     * look complete -- the same rule M314 applied to absent telemetry. */
    if (app->config.mcp_servers.len > 0) {
        jc_sb_append_fmt(out,
            "Not counted: tools from %lu configured MCP server(s) -- listing them "
            "would require connecting. A live turn advertises those too.\n",
            (unsigned long)app->config.mcp_servers.len);
    }

    /* --- cost against use (M314) --------------------------------------------
     *
     * Five ways this could lie, and what each costs here (see
     * docs/proposals/2026-08-tool-cost-vs-use.md):
     *
     *   no log        -> a stated absence, never a column of zeroes. Telemetry is
     *                    OFF by default, so zeroes would read as "you use none of
     *                    these" to the majority of users.
     *   a short log   -> the evidence base is printed next to the conclusion, so
     *                    "1 turn" can be discounted by the reader.
     *   another
     *   project's log -> the caller filters by workspace; the label says so.
     *   rare != useless -> this DESCRIBES and never advises. Advice is doctor's
     *                    job.
     *   uncallable    -> a tool denied by permissions, or one the model cannot
     *                    figure out, looks identical to an unwanted one here.
     *                    The report cannot tell them apart, so it says it cannot.
     */
    if (telem == NULL) {
        jc_sb_append(out,
            "\nUse: no telemetry log for this workspace, so which of these were "
            "actually called is unknown.\n"
            "     Run with --log-level metrics to start recording, then re-run "
            "this.\n");
    } else {
        /* Total recorded tool calls: summed from the per-tool vector, since the
         * summary keeps no scalar for it (`telem->tools` IS that vector). */
        long ncalls = 0;
        jc_size k;
        for (k = 0; k < telem->tools.len; k++) {
            const struct jc_telem_tool *e =
                (const struct jc_telem_tool *)
                jc_vec_at((struct jc_vec *)&telem->tools, k);
            ncalls += e->calls;
        }
        jc_sb_append_fmt(out, "\nUse: %s, %ld turn%s, %ld tool call%s.\n",
                         (label != NULL) ? label : "telemetry",
                         telem->turns, telem->turns == 1 ? "" : "s",
                         ncalls, ncalls == 1 ? "" : "s");
        /* M316: the CLAIM comes from jc_context_tool_use, the same join the
         * doctor check uses -- the rows above are display, but two computations
         * of "how many are unused" would be two definitions of it. */
        jc_context_tool_use(app, telem, &use);
        if (use.unused > 0) {
            jc_sb_append_fmt(out,
                "%d advertised tool%s never called in it, costing ~%ld tokens on "
                "every model call.\n",
                use.unused, use.unused == 1 ? " was" : "s were",
                use.unused_tokens);
            jc_sb_append(out,
                "A tool can be rare and still right, and this cannot see a tool "
                "the model was never\nable to call -- treat it as a question, not "
                "a verdict.\n");
        } else {
            jc_sb_append(out, "Every advertised tool was called at least once.\n");
        }
    }

    free(sizes);
    free(names);
    free(taken);
    cJSON_Delete(arr);
}

/* --- `context history` (M315) ------------------------------------------------
 *
 * See jc_context.h. Bounded scratch: one entry per distinct tool name (capped)
 * and a fixed top-N table, so a 10,000-message history costs no allocation
 * beyond those. */
#define JC_HIST_MAX_TOOLS 48
#define JC_HIST_TOP_N 5

struct hist_tool {
    const char *name;
    long tokens;
    long calls;
};

/* The tool name that produced the result in message `i`, by matching its
 * tool_call_id against the tool calls of the assistant messages BEFORE it.
 *
 * Searched BACKWARDS from `i`, and that is not an optimisation. A tool_call_id is
 * only unique within one provider response: a local backend that numbers its
 * calls per response emits the same id ("c1", "call_0") on every turn, and the
 * smoke tier's mock model does exactly that. A forward scan then charges every
 * result in the session to whichever tool the FIRST such call happened to be --
 * which is what the first run of tests/smoke/context_history.sh reported (two
 * `read_file` calls, no `search_code`), and it would have been an invisible
 * mis-attribution on any real backend with per-response ids.
 *
 * NULL when no preceding call carries the id -- legitimate after compaction
 * dropped the prefix, so the caller buckets it visibly rather than losing the
 * tokens. */
static const char *result_tool_name(struct jc_history *h, jc_size i)
{
    struct jc_message *r = jc_history_get(h, i);
    jc_size j, k, n;
    if (r == NULL || r->tool_call_id == NULL) {
        return NULL;
    }
    for (j = i; j > 0; j--) {
        struct jc_message *m = jc_history_get(h, j - 1);
        if (m == NULL || m->role != JC_ROLE_ASSISTANT) {
            continue;
        }
        n = jc_msg_tool_call_count(m);
        for (k = 0; k < n; k++) {
            struct jc_tool_call *tc = jc_msg_tool_call_at(m, k);
            if (tc->id != NULL && strcmp(tc->id, r->tool_call_id) == 0) {
                return tc->name;
            }
        }
    }
    return NULL;
}

void jc_context_history_report(struct jc_app *app,
                               const struct jc_history *hist,
                               const char *label, struct jc_sb *out)
{
    struct jc_history *h = (struct jc_history *)hist;
    jc_size n = (h != NULL) ? jc_history_len(h) : 0;
    jc_size i;
    long role_tok[4];
    long role_n[4];
    long total = 0;
    struct hist_tool tools[JC_HIST_MAX_TOOLS];
    int ntools = 0;
    long tool_total = 0;
    /* Top-N largest messages, kept by insertion so no sort is needed. */
    long top_tok[JC_HIST_TOP_N];
    jc_size top_idx[JC_HIST_TOP_N];
    int ntop = 0;
    int r, t;

    for (r = 0; r < 4; r++) {
        role_tok[r] = 0;
        role_n[r] = 0;
    }

    if (n == 0) {
        jc_sb_append_fmt(out, "History: %s is empty.\n",
                         (label != NULL) ? label : "this session");
        return;
    }

    for (i = 0; i < n; i++) {
        struct jc_message *m = jc_history_get(h, i);
        long tok;
        if (m == NULL) {
            continue;
        }
        tok = jc_compact_estimate_message_cal(app, m);
        total += tok;
        if ((int)m->role >= 0 && (int)m->role < 4) {
            role_tok[(int)m->role] += tok;
            role_n[(int)m->role]++;
        }
        if (m->role == JC_ROLE_TOOL) {
            const char *nm = result_tool_name(h, i);
            if (nm == NULL) {
                nm = "(unknown)";
            }
            tool_total += tok;
            for (t = 0; t < ntools; t++) {
                if (strcmp(tools[t].name, nm) == 0) {
                    break;
                }
            }
            if (t == ntools && ntools < JC_HIST_MAX_TOOLS) {
                tools[ntools].name = nm;
                tools[ntools].tokens = 0;
                tools[ntools].calls = 0;
                ntools++;
            }
            if (t < ntools) {
                tools[t].tokens += tok;
                tools[t].calls++;
            }
        }
        /* Insertion into the top-N table. */
        for (t = 0; t < ntop; t++) {
            if (tok > top_tok[t]) {
                break;
            }
        }
        if (t < JC_HIST_TOP_N) {
            int s2;
            for (s2 = (ntop < JC_HIST_TOP_N ? ntop : JC_HIST_TOP_N - 1);
                 s2 > t; s2--) {
                top_tok[s2] = top_tok[s2 - 1];
                top_idx[s2] = top_idx[s2 - 1];
            }
            top_tok[t] = tok;
            top_idx[t] = i;
            if (ntop < JC_HIST_TOP_N) {
                ntop++;
            }
        }
    }

    jc_sb_append_fmt(out, "History: %s, %lu message%s, ~%ld tokens\n",
                     (label != NULL) ? label : "this session",
                     (unsigned long)n, n == 1 ? "" : "s", total);

    /* The role block sums to the total EXACTLY -- same per-message term the
     * compaction trigger sums, so this cannot drift from the line it explains. */
    jc_sb_append(out, "\n  by role\n");
    {
        static const int order[3] = { (int)JC_ROLE_TOOL, (int)JC_ROLE_ASSISTANT,
                                      (int)JC_ROLE_USER };
        static const char *const rn[3] = { "tool results", "assistant", "user" };
        int q;
        for (q = 0; q < 3; q++) {
            int ri = order[q];
            if (role_n[ri] == 0) {
                continue;
            }
            jc_sb_append_fmt(out, "    %-16s~%-8ld (%ld%%, %ld message%s)\n",
                             rn[q], role_tok[ri],
                             (total > 0) ? role_tok[ri] * 100 / total : 0,
                             role_n[ri], role_n[ri] == 1 ? "" : "s");
        }
        if (role_n[(int)JC_ROLE_SYSTEM] > 0) {
            jc_sb_append_fmt(out, "    %-16s~%-8ld (%ld%%, %ld messages)\n",
                             "system", role_tok[(int)JC_ROLE_SYSTEM],
                             (total > 0)
                                 ? role_tok[(int)JC_ROLE_SYSTEM] * 100 / total
                                 : 0,
                             role_n[(int)JC_ROLE_SYSTEM]);
        }
    }

    /* Per-tool: a SUBSET (tool results only), so it states its own base rather
     * than quietly sharing the block above's denominator. */
    if (ntools > 0) {
        int shown[JC_HIST_MAX_TOOLS];
        int k;
        for (k = 0; k < ntools; k++) {
            shown[k] = 0;
        }
        jc_sb_append_fmt(out,
            "\n  tool output, largest first (of ~%ld tokens of tool results)\n",
            tool_total);
        for (k = 0; k < ntools; k++) {
            int best = -1;
            long best_n = -1;
            for (t = 0; t < ntools; t++) {
                if (!shown[t] && tools[t].tokens > best_n) {
                    best = t;
                    best_n = tools[t].tokens;
                }
            }
            if (best < 0) {
                break;
            }
            shown[best] = 1;
            jc_sb_append_fmt(out, "    %-20s~%-8ld (%ld%%, %ld call%s)\n",
                             tools[best].name, tools[best].tokens,
                             (tool_total > 0)
                                 ? tools[best].tokens * 100 / tool_total : 0,
                             tools[best].calls,
                             tools[best].calls == 1 ? "" : "s");
        }
    }

    /* Often one message IS the story, and then the fix is that message rather
     * than a policy. Indices are printed so it can be found in
     * `export --output json`. */
    jc_sb_append(out, "\n  largest single messages\n");
    for (t = 0; t < ntop; t++) {
        struct jc_message *m = jc_history_get(h, top_idx[t]);
        const char *what = "message";
        const char *nm = "";
        if (m != NULL) {
            if (m->role == JC_ROLE_TOOL) {
                what = "tool result";
                nm = result_tool_name(h, top_idx[t]);
                if (nm == NULL) {
                    nm = "(unknown)";
                }
            } else if (m->role == JC_ROLE_ASSISTANT) {
                what = "assistant";
            } else if (m->role == JC_ROLE_USER) {
                what = "user";
            }
        }
        jc_sb_append_fmt(out, "    ~%-8ld %-12s %-20s (message %lu)\n",
                         top_tok[t], what, nm, (unsigned long)top_idx[t]);
    }
}

long jc_context_prefix_tokens(struct jc_app *app)
{
    cJSON *arr;
    char *json;
    long tools_tok = 0;
    long sys_tok = 0;
    const char *sys;

    if (app == NULL || app->tools == NULL) {
        return -1;
    }
    arr = advertised_tools(app, NULL);
    if (arr != NULL) {
        json = cJSON_PrintUnformatted(arr);
        if (json != NULL) {
            tools_tok = jc_compact_estimate_text_cal(app, json);
            free(json);
        }
        cJSON_Delete(arr);
    }
    sys = jc_sysmsg_build(app);
    if (sys != NULL) {
        sys_tok = jc_compact_estimate_text_cal(app, sys);
    }
    return tools_tok + sys_tok;
}
