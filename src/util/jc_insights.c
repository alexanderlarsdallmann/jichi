/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_insights.c - mine recurring problems from telemetry/history (M70). */

#include "jc_insights.h"
#include "jc_telemetry.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <string.h>

/* Flag thresholds. A tool must have been called at least MIN_CALLS times and
 * succeed less than OK_FLOOR_PCT% of the time to count as "fails often". */
#define TOOL_MIN_CALLS 3
#define TOOL_OK_FLOOR_PCT 60   /* ok-rate below 60% => flag */
#define MODEL_TIMEOUT_MIN 2
#define RETRY_MIN 3
#define ROUTE_MIN 3
#define COMPACT_MIN 3
#define JC_INSIGHT_REDO_MIN 3
#define ERROR_MIN 3         /* failed model calls (not timeout/retry) */
#define VERIFY_FAIL_MIN 2   /* autonomous runs that failed the verify gate */
#define BUDGET_REVERT_MIN 2 /* runs rolled back for hitting the budget */

static void push(struct jc_vec *out, int kind, const char *subject,
                 long count, const char *detail)
{
    struct jc_insight in;
    memset(&in, 0, sizeof(in));
    in.kind = kind;
    in.count = count;
    jc_snprintf(in.subject, sizeof(in.subject), "%s",
                subject != NULL ? subject : "");
    jc_snprintf(in.detail, sizeof(in.detail), "%s", detail != NULL ? detail : "");
    jc_vec_push(out, &in);
}

/* True when a would-be TOOL_FAIL finding for `t` should be skipped as no longer
 * current: the tool recovered (its most recent call succeeded) or went quiet (no
 * activity within `window` of the log's newest event). Gated on real recency
 * data (last_fail_ts > 0) so a summary without timestamps keeps old behavior. */
static int tool_fail_is_stale(const struct jc_telem_tool *t, double max_ts,
                              double window)
{
    if (t->last_fail_ts <= 0.0) {
        return 0; /* no recency data -> judge on cumulative counts only */
    }
    if (t->last_ok_ts >= t->last_fail_ts) {
        return 1; /* recovered: the latest call for this tool succeeded */
    }
    if (window > 0.0 && max_ts > 0.0 && t->last_ts > 0.0 &&
        t->last_ts < max_ts - window) {
        return 1; /* quiet: not exercised in the recent window */
    }
    return 0;
}

void jc_insights_from_telemetry(const struct jc_telemetry_summary *s,
                                struct jc_vec *out)
{
    jc_insights_from_telemetry_ex(s, out, 0.0);
}

void jc_insights_from_telemetry_ex(const struct jc_telemetry_summary *s,
                                   struct jc_vec *out, double recent_window_sec)
{
    jc_size i;
    char d[200];

    if (s == NULL || out == NULL) {
        return;
    }

    /* Tools that fail often (highest signal: they block real work) -- but only
     * ones still failing recently; a historically-bad-but-recovered/quiet tool
     * is skipped so stale problems don't dominate the ranking (ANECDOTES #15). */
    for (i = 0; i < s->tools.len; i++) {
        const struct jc_telem_tool *t =
            (const struct jc_telem_tool *)jc_vec_at(
                (struct jc_vec *)&s->tools, i);
        /* M286: judge the TOOL, not the commands it was asked to run. A red
         * build or a failing test is counted in `calls - ok`, but M168 splits
         * those out as `cmd_fail` -- and a fix-forward loop runs red gates on
         * purpose, so including them here would flag `run_tests` as broken
         * precisely on the runs where it was working hardest. This is the same
         * conflation the routing flag had (jc_tool_result_is_malfunction), and
         * it matters more here because these insights are what the /learn
         * mentor writes durable lessons from. */
        long tool_ok = t->ok + t->cmd_fail;
        long fails = t->calls - tool_ok;
        if (t->calls >= TOOL_MIN_CALLS &&
            tool_ok * 100 < t->calls * TOOL_OK_FLOOR_PCT &&
            !tool_fail_is_stale(t, s->max_ts, recent_window_sec)) {
            jc_snprintf(d, sizeof(d),
                        "tool '%s' failed %ld of %ld calls (%ld%% ok) -- "
                        "check its usage/args", t->name, fails, t->calls,
                        (t->calls > 0) ? (tool_ok * 100 / t->calls) : 0);
            push(out, JC_INSIGHT_TOOL_FAIL, t->name, fails, d);
        }
    }

    /* Models that stall. */
    for (i = 0; i < s->models.len; i++) {
        const struct jc_telem_model *m =
            (const struct jc_telem_model *)jc_vec_at(
                (struct jc_vec *)&s->models, i);
        if (m->timeouts >= MODEL_TIMEOUT_MIN) {
            jc_snprintf(d, sizeof(d),
                        "model '%s' stalled %ld times -- raise timeouts.stall "
                        "or enable routing to a faster tier", m->name,
                        m->timeouts);
            push(out, JC_INSIGHT_MODEL_TIMEOUT, m->name, m->timeouts, d);
        }
    }

    /* M417: moved goalposts. Threshold ONE, unlike every other rule here:
     * a single assertion edit during an autonomous run is the difference
     * between a grade and a gamed grade, and the measured case (ten warnings,
     * verdict PASS, evidence deleted) is exactly what a mentor draft should
     * turn into a durable lesson. */
    if (s->test_edits >= 1) {
        jc_snprintf(d, sizeof(d),
                    "%ld test-assertion edit(s) during autonomous runs -- a "
                    "green verify on those runs is not evidence; review the "
                    "TAINTED attempts or kept worktrees, and fence writes to "
                    "the files the task names (docs/GATE_INTEGRITY.md)",
                    s->test_edits);
        push(out, JC_INSIGHT_TEST_EDIT, "test_edit", s->test_edits, d);
    }

    /* Run-wide pressure signals. */
    if (s->retries >= RETRY_MIN) {
        jc_snprintf(d, sizeof(d),
                    "%ld model retries (transient failures) -- the endpoint may "
                    "be flaky or overloaded", s->retries);
        push(out, JC_INSIGHT_RETRY, "model_retry", s->retries, d);
    }
    if (s->routes >= ROUTE_MIN) {
        jc_snprintf(d, sizeof(d),
                    "%ld routing escalations -- the fast tier is often "
                    "insufficient for these tasks", s->routes);
        push(out, JC_INSIGHT_ROUTE, "route", s->routes, d);
    }
    if (s->compacts >= COMPACT_MIN) {
        jc_snprintf(d, sizeof(d),
                    "%ld history compactions -- turns run long; consider "
                    "tighter scoping or a larger contextLimit", s->compacts);
        push(out, JC_INSIGHT_COMPACT, "compact", s->compacts, d);
    }

    /* Autonomy-outcome signals (M134): these were collected in the telemetry
     * summary (M92) but never mined into lessons. They are the most direct
     * evidence of the agent doing the wrong thing, so a mentor should act on
     * them. */
    if (s->out_verify_failed >= VERIFY_FAIL_MIN) {
        jc_snprintf(d, sizeof(d),
            "%ld autonomous run(s) ended in verify failure -- the changes "
            "didn't pass the gate; capture the missing precondition/pattern "
            "(build/test invariant) as a lesson so it's checked up front",
            s->out_verify_failed);
        push(out, JC_INSIGHT_VERIFY_FAIL, "verify", s->out_verify_failed, d);
    }
    if (s->out_budget_reverted >= BUDGET_REVERT_MIN) {
        jc_snprintf(d, sizeof(d),
            "%ld autonomous run(s) hit the budget and were rolled back (work "
            "lost) -- scope tasks smaller, or raise --budget-tokens/-time; a "
            "recurring over-read is worth a read-discipline lesson",
            s->out_budget_reverted);
        push(out, JC_INSIGHT_BUDGET_REVERT, "budget", s->out_budget_reverted, d);
    }
    if (s->errors >= ERROR_MIN) {
        jc_snprintf(d, sizeof(d),
            "%ld model call(s) failed outright (beyond retries) -- an API/"
            "auth/quota problem or a malformed request; check the endpoint and "
            "key", s->errors);
        push(out, JC_INSIGHT_ERROR, "model_error", s->errors, d);
    }
}

void jc_insights_redo_loops(const char *const *paths, int n, struct jc_vec *out)
{
    int i, j;
    if (paths == NULL || out == NULL || n <= 0) {
        return;
    }
    for (i = 0; i < n; i++) {
        long c;
        int seen_earlier = 0;
        if (paths[i] == NULL || paths[i][0] == '\0') {
            continue;
        }
        /* Count this path; skip if an earlier identical entry already counted. */
        for (j = 0; j < i; j++) {
            if (paths[j] != NULL && strcmp(paths[j], paths[i]) == 0) {
                seen_earlier = 1;
                break;
            }
        }
        if (seen_earlier) {
            continue;
        }
        c = 0;
        for (j = i; j < n; j++) {
            if (paths[j] != NULL && strcmp(paths[j], paths[i]) == 0) {
                c++;
            }
        }
        if (c >= JC_INSIGHT_REDO_MIN) {
            char d[200];
            jc_snprintf(d, sizeof(d),
                        "'%s' was edited %ld times in one session -- a "
                        "fix/break/fix loop; capture the gotcha as a lesson",
                        paths[i], c);
            push(out, JC_INSIGHT_REDO_LOOP, paths[i], c, d);
        }
    }
}

/* Does the byte range [s, s+len) contain `needle`? */
static int line_has(const char *s, jc_size len, const char *needle)
{
    jc_size nn = (jc_size)strlen(needle);
    jc_size i, j;
    if (nn == 0 || nn > len) {
        return 0;
    }
    for (i = 0; i + nn <= len; i++) {
        for (j = 0; j < nn && s[i + j] == needle[j]; j++) {
        }
        if (j == nn) {
            return 1;
        }
    }
    return 0;
}

/* M600: is `s[0..len)` a path-shaped token -- a slash and a dotted extension,
 * no spaces? Deliberately narrow: a false "does not resolve" is a wrong
 * accusation, and prose has many slashes ("and/or") that are not paths. */
static int token_is_path(const char *s, jc_size len)
{
    jc_size i;
    int slash = 0, dot_after_slash = 0;
    if (len < 4 || len > 240) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '/') {
            slash = 1;
        } else if (c == '.' && slash && i + 1 < len) {
            dot_after_slash = 1;
        } else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c == '_' || c == '-' ||
                     c == '.')) {
            return 0;
        }
    }
    return slash && dot_after_slash && s[0] != '/' && s[len - 1] != '.';
}

void jc_insights_stale_review_ex(const char *memory,
                                 jc_insights_exists_fn exists, void *ctx,
                                 struct jc_vec *out)
{
    const char *p;
    int notes = 0;
    int cited = 0;
    int pinned = 0;
    int unresolved = 0;
    char examples[160];
    char d[200];

    examples[0] = '\0';
    if (memory == NULL || out == NULL) {
        return;
    }
    for (p = memory; *p != '\0'; ) {
        const char *nl = strchr(p, '\n');
        jc_size ll = nl != NULL ? (jc_size)(nl - p) : (jc_size)strlen(p);
        if (ll >= 2 && p[0] == '-' && p[1] == ' ') {
            notes++;
            if (line_has(p + 2, ll - 2, "line")) {
                cited++; /* cites a line/range -- most prone to drift */
            }
            if (line_has(p + 2, ll - 2, "[pins:")) {
                pinned++;
            }
            if (exists != NULL) {
                /* Walk the note's whitespace-separated tokens, stripping the
                 * punctuation prose wraps a path in (`x.c`, (x.c), x.c,). */
                jc_size i = 2;
                int flagged_this_note = 0;
                while (i < ll && !flagged_this_note) {
                    jc_size j;
                    char tok[256];
                    jc_size tl;
                    while (i < ll && (p[i] == ' ' || p[i] == '\t')) {
                        i++;
                    }
                    j = i;
                    while (j < ll && p[j] != ' ' && p[j] != '\t') {
                        j++;
                    }
                    tl = j - i;
                    while (tl > 0 && (p[i] == '`' || p[i] == '(' || p[i] == '"' ||
                                      p[i] == '\'' || p[i] == '[')) {
                        i++; tl--;
                    }
                    while (tl > 0 && (p[i + tl - 1] == '`' || p[i + tl - 1] == ')' ||
                                      p[i + tl - 1] == ',' || p[i + tl - 1] == ';' ||
                                      p[i + tl - 1] == '"' || p[i + tl - 1] == '\'' ||
                                      p[i + tl - 1] == ']' || p[i + tl - 1] == ':' ||
                                      p[i + tl - 1] == '.')) {
                        tl--;
                    }
                    if (tl > 0 && tl < sizeof(tok) && token_is_path(p + i, tl)) {
                        memcpy(tok, p + i, tl);
                        tok[tl] = '\0';
                        if (!exists(tok, ctx)) {
                            unresolved++;
                            flagged_this_note = 1;
                            if (unresolved <= 3) {
                                jc_size el = (jc_size)strlen(examples);
                                jc_snprintf(examples + el, sizeof(examples) - el,
                                            "%s%s", el > 0 ? ", " : "", tok);
                            }
                        }
                    }
                    i = j;
                }
            }
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    if (notes == 0) {
        return;
    }
    if (cited > 0) {
        jc_snprintf(d, sizeof(d),
            "%d remembered note(s) (%d cite a specific line/range) -- review "
            "against current code; supersede a now-false note via a "
            "'## Corrections' section (remove:/replace:)", notes, cited);
    } else {
        jc_snprintf(d, sizeof(d),
            "%d remembered note(s) -- review against current code; supersede a "
            "now-false note via a '## Corrections' section (remove:/replace:)",
            notes);
    }
    push(out, JC_INSIGHT_STALE_NOTE, "memory", notes, d);
    /* M600: the two mechanical facts. Pinned share always (a lesson with no
     * test to cite is visibly that); unresolved paths only when a resolver was
     * given and something failed to resolve -- an absence is not a finding. */
    jc_snprintf(d, sizeof(d),
        "%d of %d remembered note(s) are pinned to a test, lint or constraint "
        "([pins: ...]); the other %d rest on prose alone", pinned, notes,
        notes - pinned);
    push(out, JC_INSIGHT_STALE_NOTE, "memory-pins", pinned, d);
    if (unresolved > 0) {
        jc_snprintf(d, sizeof(d),
            "%d remembered note(s) name a path that no longer resolves in this "
            "workspace (%s%s) -- the note may describe code that moved; "
            "'## Corrections' can retract or restate it", unresolved, examples,
            unresolved > 3 ? ", ..." : "");
        push(out, JC_INSIGHT_STALE_NOTE, "memory-paths", unresolved, d);
    }
}

void jc_insights_stale_review(const char *memory, struct jc_vec *out)
{
    jc_insights_stale_review_ex(memory, NULL, NULL, out);
}

void jc_insights_stamp(struct jc_vec *out, jc_size from, const char *source,
                       const char *origin)
{
    jc_size i;
    if (out == NULL || source == NULL) {
        return;
    }
    for (i = from; i < out->len; i++) {
        struct jc_insight *in = (struct jc_insight *)jc_vec_at(out, i);
        if (in->source[0] != '\0') {
            continue; /* an inner scan already said something more specific */
        }
        jc_snprintf(in->source, sizeof(in->source), "%s", source);
        if (origin != NULL) {
            jc_snprintf(in->origin, sizeof(in->origin), "%s", origin);
        }
    }
}

void jc_insights_render(const struct jc_vec *findings, struct jc_sb *out)
{
    jc_size i;
    if (out == NULL) {
        return;
    }
    if (findings == NULL || findings->len == 0) {
        jc_sb_append(out, "No recurring problems found in the available "
                          "telemetry/history.\n");
        return;
    }
    jc_sb_append_fmt(out, "Recurring problems (%lu):\n\n",
                     (unsigned long)findings->len);
    for (i = 0; i < findings->len; i++) {
        const struct jc_insight *in =
            (const struct jc_insight *)jc_vec_at((struct jc_vec *)findings, i);
        /* Provenance first, so the eye reaches it before the claim (M474). An
         * unstamped finding renders exactly as it did before, which keeps every
         * other caller of this function unchanged. */
        if (in->source[0] != '\0' && in->origin[0] != '\0') {
            jc_sb_append_fmt(out, "  - [%s: %s] %s\n",
                             in->source, in->origin, in->detail);
        } else if (in->source[0] != '\0') {
            jc_sb_append_fmt(out, "  - [%s] %s\n", in->source, in->detail);
        } else {
            jc_sb_append_fmt(out, "  - %s\n", in->detail);
        }
    }
    /* One line of orientation when the list mixes sources, because "session"
     * findings come from the GLOBAL store and may concern another workspace
     * entirely -- which is the whole reason provenance was added. Only printed
     * when a session finding is actually present, so the common
     * single-source case stays terse. */
    {
        int has_session = 0;
        for (i = 0; i < findings->len; i++) {
            const struct jc_insight *in =
                (const struct jc_insight *)jc_vec_at((struct jc_vec *)findings, i);
            if (strcmp(in->source, "session") == 0) {
                has_session = 1;
                break;
            }
        }
        if (has_session) {
            jc_sb_append(out,
                "\n  [session] findings are mined from the global session store, "
                "not\n  from the telemetry named on the command line -- check the "
                "workspace\n  before acting on one.\n");
        }
    }
}
