/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_envelope.c - the autonomy envelope (see jc_envelope.h).
 *
 * Pure helpers (parse / glob / scope / budget) carry no state; the verifier
 * runner and journal are POSIX-backed (fork/exec/pipe, the same primitive the
 * snapshot and MCP layers use). All logic is opt-in: a NULL envelope on jc_app
 * leaves the agent loop unchanged. */

#include "jc_envelope.h"
#include "jc_platform.h"
#include "jc_log.h"
#include "jc_proc.h"
#include "jc_snprintf.h"
#include "jc_str.h"
#include "jc_version.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>

int jc_env_parse_size(const char *s, double *out)
{
    char *end;
    double v;

    if (s == NULL || *s == '\0') {
        return -1;
    }
    v = strtod(s, &end);
    if (end == s) {
        return -1;
    }
    if (*end == 'k' || *end == 'K') {
        v *= 1000.0;
        end++;
    } else if (*end == 'm' || *end == 'M') {
        v *= 1000000.0;
        end++;
    }
    if (*end != '\0' || v < 0.0) {
        return -1;
    }
    *out = v;
    return 0;
}

int jc_env_parse_duration(const char *s, long *out)
{
    char *end;
    double v;

    if (s == NULL || *s == '\0') {
        return -1;
    }
    v = strtod(s, &end);
    if (end == s) {
        return -1;
    }
    if (*end == 's' || *end == 'S') {
        end++;
    } else if (*end == 'm' || *end == 'M') {
        v *= 60.0;
        end++;
    } else if (*end == 'h' || *end == 'H') {
        v *= 3600.0;
        end++;
    } else if (*end == 'd' || *end == 'D') {
        /* Days (M158): `audit --since 7d`, `--deadline 1d`. */
        v *= 86400.0;
        end++;
    }
    if (*end != '\0' || v < 0.0) {
        return -1;
    }
    *out = (long)v;
    return 0;
}

int jc_glob_match(const char *pat, const char *path)
{
    while (*pat != '\0') {
        if (*pat == '*') {
            if (pat[1] == '*') {
                /* "**" matches a run that may cross '/'. */
                const char *np = pat + 2;
                if (*np == '/') {
                    np++; /* a double-star then slash may match zero dirs */
                }
                if (*np == '\0') {
                    return 1; /* trailing ** matches the rest entirely */
                }
                for (;;) {
                    if (jc_glob_match(np, path)) {
                        return 1;
                    }
                    if (*path == '\0') {
                        return 0;
                    }
                    path++;
                }
            } else {
                /* "*" matches a run that does not cross '/'. */
                const char *np = pat + 1;
                for (;;) {
                    if (jc_glob_match(np, path)) {
                        return 1;
                    }
                    if (*path == '\0' || *path == '/') {
                        return 0;
                    }
                    path++;
                }
            }
        } else if (*pat == '?') {
            if (*path == '\0' || *path == '/') {
                return 0;
            }
            pat++;
            path++;
        } else {
            if (*pat != *path) {
                return 0;
            }
            pat++;
            path++;
        }
    }
    return *path == '\0';
}

const char *jc_env_relpath(const char *root, const char *path)
{
    const char *p;

    if (path == NULL) {
        return NULL;
    }
    p = path;
    /* Relativize an absolute path under the workspace `root` to a root-relative
     * path, so the file tools' absolute paths still match the workspace-relative
     * editScope globs (otherwise an in-scope absolute path is wrongly refused). */
    if (root != NULL && root[0] != '\0') {
        jc_size rl = (jc_size)strlen(root);
        while (rl > 0 && root[rl - 1] == '/') {
            rl--;                       /* ignore trailing slash(es) on root */
        }
        /* Require a path boundary after the prefix (a '/' or end), so a sibling
         * directory sharing the root's name -- e.g. root "/w" vs "/wsrc/x" --
         * isn't mis-stripped into a garbage relative path that could wrongly
         * match a glob (the fence is security-relevant). */
        if (rl > 0 && strncmp(p, root, rl) == 0 &&
            (p[rl] == '/' || p[rl] == '\0')) {
            p += rl;
            while (*p == '/') {
                p++;
            }
        }
    }
    if (p[0] == '.' && p[1] == '/') {
        p += 2;
    }
    return p;
}

/* M501: the write set and the scope check MUST agree on what a path looks like,
 * or the intersection between "changed since baseline" and "written by this run"
 * is empty and the revert silently becomes a no-op -- a safety feature disarmed
 * by a formatting mismatch. One function, both callers. */
int jc_env_wrote(const struct jc_envelope *e, const char *root,
                 const char *path)
{
    jc_size i;
    const char *p;

    if (e == NULL || path == NULL) {
        return 0;
    }
    p = jc_env_relpath(root, path);
    for (i = 0; i < e->wrote.len; i++) {
        const char *w = *(char **)jc_vec_at((struct jc_vec *)&e->wrote, i);
        if (w != NULL && strcmp(w, p) == 0) {
            return 1;
        }
    }
    return 0;
}

void jc_env_wrote_mark(struct jc_envelope *e, const char *root,
                       const char *path)
{
    const char *p;
    char *dup;

    if (e == NULL || path == NULL || path[0] == '\0') {
        return;
    }
    if (jc_env_wrote(e, root, path)) {
        return;                          /* already recorded */
    }
    p = jc_env_relpath(root, path);
    dup = jc_strdup(p);
    if (dup != NULL) {
        jc_vec_push(&e->wrote, &dup);
    }
}

int jc_env_path_in_scope(const struct jc_envelope *e, const char *root,
                         const char *path)
{
    jc_size i;
    const char *p;

    if (e == NULL || e->edit_scope.len == 0) {
        return 1;
    }
    if (path == NULL) {
        return 0;
    }
    p = jc_env_relpath(root, path);
    for (i = 0; i < e->edit_scope.len; i++) {
        const char *pat = *(char **)jc_vec_at((struct jc_vec *)&e->edit_scope,
                                              i);
        if (pat != NULL && jc_glob_match(pat, p)) {
            return 1;
        }
    }
    return 0;
}

enum jc_env_budget jc_env_over_budget(const struct jc_envelope *e, long now)
{
    if (e == NULL) {
        return JC_BUDGET_NONE;
    }
    if (e->budget_tokens > 0.0 && e->tokens_used >= e->budget_tokens) {
        return JC_BUDGET_TOKENS;
    }
    if (e->deadline_secs > 0 &&
        (now - e->start_time - e->deadline_credit) >= e->deadline_secs) {
        /* deadline_credit (M162): seconds an operator explicitly credited
         * back via `control pause --extend`; zero on every other path. */
        return JC_BUDGET_DEADLINE;
    }
    if (e->max_tool_calls > 0 && e->tool_calls >= e->max_tool_calls) {
        return JC_BUDGET_TOOLCALLS;
    }
    if (e->max_reads > 0 && e->reads >= e->max_reads) {
        return JC_BUDGET_READS;
    }
    return JC_BUDGET_NONE;
}

enum jc_env_budget jc_env_budget_notice_due(const struct jc_envelope *e,
                                            long now)
{
    if (e == NULL) {
        return JC_BUDGET_NONE;
    }
    /* Four fifths of each armed cap, in jc_env_over_budget's order. Integer
     * arithmetic (x*5 >= cap*4) so the crossing is exact: with a 5-call cap
     * the notice lands after call 4, never "somewhere past 3.99". */
    if (e->budget_tokens > 0.0 &&
        e->tokens_used * 5.0 >= e->budget_tokens * 4.0) {
        return JC_BUDGET_TOKENS;
    }
    if (e->deadline_secs > 0 &&
        (now - e->start_time - e->deadline_credit) * 5 >=
            e->deadline_secs * 4) {
        return JC_BUDGET_DEADLINE;
    }
    if (e->max_tool_calls > 0 &&
        e->tool_calls * 5 >= e->max_tool_calls * 4) {
        return JC_BUDGET_TOOLCALLS;
    }
    if (e->max_reads > 0 && e->reads * 5 >= e->max_reads * 4) {
        return JC_BUDGET_READS;
    }
    return JC_BUDGET_NONE;
}

/* M431f: is the panel due? Every `every`-th tool call, PLUS each quintile crossing
 * of the token budget so the reading never skips a fifth of the run. Throttled by
 * panel_last_call so one boundary cannot render twice.
 *
 * The quintile term is what answers M347's objection mechanically: on a short run
 * this fires once or twice, on a long one a dozen times -- not per round, which is
 * the nag M323 measured. Pure; unit-tested. */
int jc_env_panel_due(const struct jc_envelope *e, int every)
{
    long since;
    int q_now;
    int q_then;

    if (e == NULL || !e->budget_panel || every <= 0) {
        return 0;
    }
    if (e->tool_calls <= 0) {
        return 0;   /* nothing has happened yet; the flight plan just stated the caps */
    }
    if (e->panel_last_call == e->tool_calls) {
        return 0;   /* already rendered at this boundary */
    }
    since = e->tool_calls - e->panel_last_call;
    if (since >= (long)every) {
        return 1;
    }
    /* A quintile crossing of the token budget, when one is armed. */
    if (e->budget_tokens > 0.0) {
        q_now = (int)(e->tokens_used * 5.0 / e->budget_tokens);
        q_then = (int)(e->panel_tokens * 5.0 / e->budget_tokens);
        if (q_now > q_then) {
            return 1;
        }
    }
    return 0;
}

/* M431f: the reading itself. Only ARMED budgets appear -- the M347/M355 rule that a
 * limit which does not exist is not a fact about this run. The rate and the
 * projection are the point; both are omitted rather than guessed when there is no
 * data to derive them from. Pure; unit-tested. */
void jc_env_panel_render(const struct jc_envelope *e, long now, struct jc_sb *sb)
{
    char part[160];
    double rate = 0.0;
    int first = 1;

    if (e == NULL || sb == NULL) {
        return;
    }
    if (e->model_calls > 0) {
        rate = e->tokens_used / (double)e->model_calls;
    }
    jc_sb_append(sb, "[envelope] ");
    if (e->budget_tokens > 0.0) {
        jc_snprintf(part, sizeof part, "%.0f/%.0f tokens (%d%%)",
                    e->tokens_used, e->budget_tokens,
                    (int)(e->tokens_used * 100.0 / e->budget_tokens));
        jc_sb_append(sb, part);
        first = 0;
    }
    if (e->max_tool_calls > 0) {
        jc_snprintf(part, sizeof part, "%s%d/%d tool calls",
                    first ? "" : " . ", e->tool_calls, e->max_tool_calls);
        jc_sb_append(sb, part);
        first = 0;
    }
    if (e->max_reads > 0) {
        jc_snprintf(part, sizeof part, "%s%d/%d reads",
                    first ? "" : " . ", e->reads, e->max_reads);
        jc_sb_append(sb, part);
        first = 0;
    }
    if (e->deadline_secs > 0) {
        long el = now - e->start_time - e->deadline_credit;
        if (el < 0) {
            el = 0;
        }
        jc_snprintf(part, sizeof part, "%s%ld/%ld seconds",
                    first ? "" : " . ", el, e->deadline_secs);
        jc_sb_append(sb, part);
        first = 0;
    }
    if (rate > 0.0) {
        jc_snprintf(part, sizeof part, "%s~%.0f tokens/call", first ? "" : " . ",
                    rate);
        jc_sb_append(sb, part);
        first = 0;
        /* The projection, which is the whole reason the rate is here: a count of
         * calls is actionable where a count of tokens is not. Stated as
         * approximate because the rate RISES within a run on a cacheless backend
         * (measured 24.8k -> 35.9k across 56 calls), so this is an upper bound on
         * what remains, not a promise. */
        if (e->budget_tokens > 0.0 && e->tokens_used < e->budget_tokens) {
            double left = (e->budget_tokens - e->tokens_used) / rate;
            jc_snprintf(part, sizeof part, " . ~%d calls left at this rate",
                        (int)left);
            jc_sb_append(sb, part);
        }
    }
    jc_sb_append(sb,
        ". Pace the work: prefer finishing a smaller complete thing over starting a "
        "larger unfinished one, and write the deliverable before the budget ends the "
        "run where it stands.");
}

void jc_env_budget_notice_render(const struct jc_envelope *e, long now,
                                 struct jc_sb *sb)
{
    char part[112];
    int first = 1;

    if (e == NULL || sb == NULL) {
        return;
    }
    jc_sb_append(sb, "[envelope] budget check: ");
    if (e->budget_tokens > 0.0) {
        jc_snprintf(part, sizeof part,
                    "%.0f of %.0f budget tokens used (%d%%)",
                    e->tokens_used, e->budget_tokens,
                    (int)(e->tokens_used * 100.0 / e->budget_tokens));
        jc_sb_append(sb, part);
        first = 0;
    }
    if (e->deadline_secs > 0) {
        long el = now - e->start_time - e->deadline_credit;
        if (el < 0) {
            el = 0;
        }
        jc_snprintf(part, sizeof part, "%s%ld of %ld deadline seconds",
                    first ? "" : "; ", el, e->deadline_secs);
        jc_sb_append(sb, part);
        first = 0;
    }
    if (e->max_tool_calls > 0) {
        jc_snprintf(part, sizeof part, "%s%d of %d tool calls",
                    first ? "" : "; ", e->tool_calls, e->max_tool_calls);
        jc_sb_append(sb, part);
        first = 0;
    }
    if (e->max_reads > 0) {
        jc_snprintf(part, sizeof part, "%s%d of %d reads",
                    first ? "" : "; ", e->reads, e->max_reads);
        jc_sb_append(sb, part);
        first = 0;
    }
    jc_sb_append(sb,
        ". A budget stop ends the run as it stands: finish the step in "
        "flight, then write the deliverable and your final answer before "
        "starting anything new.");
}

int jc_env_budget_rollback_decision(int rollback_on_fail, int has_green,
                                    int has_verifier, int verify_code)
{
    if (!rollback_on_fail || !has_green) {
        return 0; /* rollback disabled or no green checkpoint to return to */
    }
    if (!has_verifier) {
        return 0; /* M80: no gate to declare the tree broken -> keep the work */
    }
    return verify_code != 0; /* roll back iff the verifier is red */
}

int jc_env_analysis_starved(enum jc_env_outcome outcome, int snapshots_on,
                            int made_edits)
{
    /* Only meaningful on a budget stop (a clean completion set JC_ENV_OK). */
    if (outcome != JC_ENV_BUDGET_EXHAUSTED) {
        return 0;
    }
    /* We must be able to trust "no edits": absent snapshots, green_commit is
     * NULL even for an edit run, so we can't distinguish -> stay quiet. */
    if (!snapshots_on) {
        return 0;
    }
    /* No edit was ever made this run => nothing was written to disk (M80 kept
     * nothing), so the deliverable is only the final answer, which the budget
     * stop truncated -- the "all reads, no synthesis" failure. */
    return !made_edits;
}

void jc_env_out_of_scope_paths(const struct jc_envelope *e, const char *root,
                               const char *const *paths, int n,
                               struct jc_vec *out)
{
    int i;
    if (e == NULL || out == NULL || e->edit_scope.len == 0) {
        return; /* no fence -> nothing is out of scope */
    }
    for (i = 0; i < n; i++) {
        if (paths[i] == NULL || paths[i][0] == '\0') {
            continue;
        }
        /* jc_env_path_in_scope returns 1 when in scope (or unfenced); a changed
         * path that is NOT in scope was introduced outside the edit fence. */
        if (!jc_env_path_in_scope(e, root, paths[i])) {
            jc_vec_push(out, &paths[i]);
        }
    }
}

int jc_env_should_verify_now(int verify_every, long tool_calls, long last)
{
    if (verify_every <= 0) {
        return 0;
    }
    return (tool_calls - last) >= (long)verify_every;
}

int jc_env_verify_kind_parse(const char *s, int *out)
{
    if (s == NULL || out == NULL) {
        return 0;
    }
    if (strcmp(s, "invariant") == 0) {
        *out = JC_VERIFY_KIND_INVARIANT;
        return 1;
    }
    if (strcmp(s, "goal") == 0) {
        *out = JC_VERIFY_KIND_GOAL;
        return 1;
    }
    return 0;
}

const char *jc_env_verify_kind_name(int kind)
{
    switch (kind) {
    case JC_VERIFY_KIND_INVARIANT: return "invariant";
    case JC_VERIFY_KIND_GOAL:      return "goal";
    default:                       return "unset";
    }
}

enum jc_env_baseline_verdict jc_env_baseline_check(int kind, int exit_code)
{
    if (kind == JC_VERIFY_KIND_GOAL) {
        /* A goal gate is red before the work by construction, so red is the
         * expected state and green is the alarm: the gate can be satisfied
         * without the work happening. */
        return (exit_code == 0) ? JC_BASELINE_FORCES_NOTHING
                                : JC_BASELINE_EXPECTED_RED;
    }
    /* UNSET and INVARIANT share a table on purpose: the pre-M343 behaviour was
     * written for invariants, so an undeclared gate keeps it unchanged. */
    return (exit_code == 0) ? JC_BASELINE_OK : JC_BASELINE_NOT_KNOWN_GOOD;
}

const char *jc_env_budget_name(enum jc_env_budget b)
{
    switch (b) {
    case JC_BUDGET_TOKENS:    return "tokens";
    case JC_BUDGET_DEADLINE:  return "deadline";
    case JC_BUDGET_TOOLCALLS: return "tool_calls";
    case JC_BUDGET_READS:     return "reads";
    default:                  return "none";
    }
}

const char *jc_env_outcome_name(enum jc_env_outcome o)
{
    switch (o) {
    case JC_ENV_OK:               return "ok";
    case JC_ENV_VERIFY_FAILED:    return "verify_failed";
    case JC_ENV_BUDGET_EXHAUSTED: return "budget_exhausted";
    case JC_ENV_SCOPE_TAINTED:    return "scope_tainted";
    default:                      return "running";
    }
}

const char *jc_env_disposition_name(enum jc_env_outcome o, int rolled_back)
{
    switch (o) {
    case JC_ENV_OK:
        return "completed";
    case JC_ENV_VERIFY_FAILED:
        return rolled_back ? "verify_failed (rolled back)"
                           : "verify_failed (kept)";
    case JC_ENV_BUDGET_EXHAUSTED:
        return rolled_back ? "budget_exhausted (rolled back)"
                           : "budget_exhausted (work kept)";
    case JC_ENV_SCOPE_TAINTED:
        /* Never rolled back by this path -- the verdict is refused, the work is
         * kept for review. Saying "(work kept)" unconditionally would be a claim
         * about a variable this case does not set, so it is left out. */
        return "scope_tainted";
    default:
        return "running";
    }
}

int jc_env_summarize_paths(const char *names, int max, struct jc_sb *out)
{
    int count;
    int shown;
    const char *p;
    const char *start;

    if (names == NULL) {
        return 0;
    }
    count = 0;
    shown = 0;
    start = names;
    for (p = names;; p++) {
        if (*p == '\n' || *p == '\0') {
            jc_size len = (jc_size)(p - start);
            if (len > 0) {
                count++;
                if (out != NULL && shown < max) {
                    char buf[512];
                    jc_size n = (len < sizeof(buf) - 1) ? len : sizeof(buf) - 1;
                    if (shown > 0) {
                        jc_sb_append(out, ", ");
                    }
                    memcpy(buf, start, n);
                    buf[n] = '\0';
                    jc_sb_append(out, buf);
                    shown++;
                }
            }
            if (*p == '\0') {
                break;
            }
            start = p + 1;
        }
    }
    if (out != NULL && count > shown) {
        jc_sb_append_fmt(out, " (+%d more)", count - shown);
    }
    return count;
}

/* M88: does `s` contain an assertion marker (so it reads like test-check text)? */
static int m88_has_assertion(const char *s)
{
    static const char *marks[] = {
        "expect", "assert", "EXPECT_", "ASSERT_", "testing.", "GDTEST",
        "toEqual", "toBe", NULL
    };
    int i;
    if (s == NULL) {
        return 0;
    }
    for (i = 0; marks[i] != NULL; i++) {
        if (strstr(s, marks[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

int jc_env_test_assertion_edit(const char *path, const char *old_string,
                               const char *new_string)
{
    if (path == NULL || old_string == NULL || new_string == NULL) {
        return 0;
    }
    if (strstr(path, "test") == NULL && strstr(path, "Test") == NULL &&
        strstr(path, "spec") == NULL && strstr(path, "Spec") == NULL) {
        return 0; /* not a test file */
    }
    if (!m88_has_assertion(old_string) || !m88_has_assertion(new_string)) {
        return 0; /* neither side reads like an assertion */
    }
    if (strstr(new_string, old_string) != NULL) {
        return 0; /* pure addition (old text preserved) -- adding a test, not moving one */
    }
    return 1;
}

int jc_env_fail_signature(const char *output, char *buf, jc_size cap)
{
    const char *p;
    const char *end;
    jc_size n;

    if (buf == NULL || cap == 0) {
        return 0;
    }
    buf[0] = '\0';
    if (output == NULL) {
        return 0;
    }
    p = strstr(output, "error:");
    if (p == NULL) {
        return 0;
    }
    end = strchr(p, '\n');
    if (end == NULL) {
        end = p + strlen(p);
    }
    /* Trim trailing whitespace/CR. */
    while (end > p && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
        end--;
    }
    n = (jc_size)(end - p);
    if (n > cap - 1) {
        n = cap - 1;
    }
    memcpy(buf, p, n);
    buf[n] = '\0';
    return n > 0 ? 1 : 0;
}

int jc_env_note_failure(struct jc_envelope *e, const char *output)
{
    char sig[JC_ENV_SIG_MAX];
    jc_size n;

    if (e == NULL) {
        return 0;
    }
    if (!jc_env_fail_signature(output, sig, sizeof sig) || sig[0] == '\0') {
        e->last_fail_sig[0] = '\0';
        e->repeat_fails = 0;
        return 0;
    }
    if (e->last_fail_sig[0] != '\0' && strcmp(sig, e->last_fail_sig) == 0) {
        e->repeat_fails += 1;
    } else {
        e->repeat_fails = 1;
    }
    n = (jc_size)strlen(sig);
    if (n >= sizeof e->last_fail_sig) {
        n = sizeof e->last_fail_sig - 1;
    }
    memcpy(e->last_fail_sig, sig, n);
    e->last_fail_sig[n] = '\0';
    return e->repeat_fails;
}

int jc_env_note_blocked(struct jc_envelope *e, const char *tool,
                        const char *target)
{
    char sig[JC_ENV_SIG_MAX];

    if (e == NULL) {
        return 0;
    }
    /* (tool, target) rather than the whole command: the measured case varied the
     * command while aiming at the same forbidden path, and a model that switches
     * from the shell to write_file for the same target is repeating itself in the
     * only sense that matters here. */
    jc_snprintf(sig, sizeof(sig), "%s|%s",
                (tool != NULL) ? tool : "",
                (target != NULL) ? target : "");
    if (e->last_block_sig[0] != '\0' && strcmp(sig, e->last_block_sig) == 0) {
        e->repeat_blocks += 1;
    } else {
        e->repeat_blocks = 1;
    }
    jc_snprintf(e->last_block_sig, sizeof(e->last_block_sig), "%s", sig);
    return e->repeat_blocks;
}

int jc_env_is_test_path(const char *path)
{
    if (path == NULL) {
        return 0;
    }
    return strstr(path, "test") != NULL || strstr(path, "Test") != NULL ||
           strstr(path, "spec") != NULL || strstr(path, "Spec") != NULL;
}

enum jc_verify_sanity jc_env_verify_sanity(int count_now, int prev_max,
                                           int test_edit)
{
    if (count_now == 0) {
        return JC_VERIFY_NO_TESTS;
    }
    if (count_now > 0 && prev_max > 0 && count_now < prev_max) {
        return JC_VERIFY_FEWER_TESTS;
    }
    /* A test file was written and the gate ran no MORE tests than before: the new
     * test is probably not reachable from whatever the gate compiles. Requires a
     * known prior count, so the first green of a run cannot trip it. */
    if (test_edit && count_now > 0 && prev_max > 0 && count_now <= prev_max) {
        return JC_VERIFY_TESTS_NOT_WIRED;
    }
    return JC_VERIFY_SANE;
}

void jc_env_sanity_note(enum jc_verify_sanity v, int count_now, int prev_max,
                        struct jc_sb *out)
{
    char part[120];

    if (out == NULL || v == JC_VERIFY_SANE) {
        return;
    }
    jc_sb_append(out, "[envelope] the verifier just PASSED");
    if (v == JC_VERIFY_NO_TESTS) {
        jc_sb_append(out,
            " while running 0 tests. This green verifies nothing: do not "
            "build on it as tested work. Check the verify/test command (a "
            "build-only or mis-scoped gate is the usual cause); if you "
            "cannot fix it, say plainly in your final answer that the "
            "result is unverified.");
        return;
    }
    if (v == JC_VERIFY_FEWER_TESTS) {
        jc_snprintf(part, sizeof(part),
                    " but ran %d tests where an earlier green ran %d.",
                    count_now, prev_max);
        jc_sb_append(out, part);
        jc_sb_append(out,
            " Part of the suite has stopped running; treat this green as "
            "weaker than it looks and find what stopped being compiled or "
            "collected before building further.");
        return;
    }
    /* JC_VERIFY_TESTS_NOT_WIRED */
    jc_snprintf(part, sizeof(part),
                ", a test file was edited, and the observed test count did "
                "not grow (%d, was %d).", count_now, prev_max);
    jc_sb_append(out, part);
    jc_sb_append(out,
        " The new test likely never runs; make it reachable from what the "
        "gate compiles before trusting it.");
}

enum jc_verify_consistency jc_env_verify_consistency(int exit_code, int passed,
                                                     int failed, int prev_max)
{
    if (exit_code == 0) {
        /* Green belongs to jc_env_verify_sanity (M86); answering here too would
         * warn twice about one verify. */
        return JC_VERIFY_AGREES;
    }
    /* A red verify that parsed no counts at all is a compile error, which is both
     * normal and the commonest failing verify there is. Silence is correct. */
    if (passed <= 0) {
        return JC_VERIFY_AGREES;
    }
    if (failed == 0) {
        /* Tests ran, none failed, and the gate still says no. The fault is in the
         * harness -- a lint step, a build-system quirk, a wrapper's exit code --
         * not in the code under test. This is the finding worth interrupting for,
         * so it outranks the count check below when both apply. */
        return JC_VERIFY_HOLLOW_RED;
    }
    if (prev_max > 0 && passed < prev_max) {
        return JC_VERIFY_RED_TESTS_GONE;
    }
    return JC_VERIFY_AGREES;
}

int jc_env_refuse_green(int strict_green, int out_of_scope_seen,
                        enum jc_env_outcome outcome)
{
    return strict_green && out_of_scope_seen > 0 && outcome == JC_ENV_OK;
}

jc_status jc_env_init(struct jc_envelope *e, struct jc_arena *a,
                      const char *run_id, const char *journal_path)
{
    memset(e, 0, sizeof(*e));
    jc_vec_init(&e->edit_scope, sizeof(char *));
    jc_vec_init(&e->oos_reported, sizeof(char *)); /* M289 */
    jc_vec_init(&e->wrote, sizeof(char *));        /* M501 */
    e->verify_retries = 3;
    e->retries_left = 3;
    e->verify_max_tests = -1;
    e->rollback_on_fail = 1;
    e->outcome = JC_ENV_RUNNING;
    e->start_time = (long)time(NULL);
    e->run_id = (run_id != NULL) ? jc_arena_strdup(a, run_id) : NULL;
    e->journal = NULL;
    if (journal_path != NULL && journal_path[0] != '\0') {
        e->journal = fopen(journal_path, "a");
        if (e->journal != NULL) {
            jc_make_private(journal_path); /* owner-only (M132) */
            /* M472: a model-issued shell was measured holding this descriptor
             * writable, and `echo ... >&3` forged a record into the middle of
             * this very journal -- the one `jichi runs` and `doctor
             * --unattended` read to gate an unattended loop. */
            jc_fd_cloexec(fileno(e->journal));
        }
        if (e->journal == NULL) {
            jc_logf(JC_LOG_WARN, "envelope: cannot open journal %s",
                    journal_path);
        }
    }
    /* M438: write a line the instant the journal exists, so the file is never
     * 0 bytes while the process is alive.
     *
     * The `start` record is emitted by jc_agent_run_turn -- AFTER config load,
     * per-server reachability probes, MCP connect and the repo-map build. Any of
     * those can take minutes or hang, and during that window a supervisor tailing
     * the journal sees an empty file, which is exactly what a process that died
     * before creating it looks like. A supervisor's only honest liveness signal is
     * a file that appears, and an empty one does not qualify.
     *
     * Deliberately minimal: the pid (so a supervisor can check the process
     * directly) and the build. Every budget/scope figure stays on `start`, where a
     * reader already looks for it -- this record answers one question, "the run
     * exists", and answering more here would duplicate a record that follows. */
    if (e->journal != NULL) {
        cJSON *o = jc_env_journal_begin(e, "open");
        if (o != NULL) {
            cJSON_AddNumberToObject(o, "pid", (double)getpid());
        }
        jc_env_journal_end(e, o);
    }
    return JC_OK;
}

void jc_env_free(struct jc_envelope *e)
{
    if (e == NULL) {
        return;
    }
    if (e->journal != NULL) {
        fclose(e->journal);
        e->journal = NULL;
    }
    jc_vec_free(&e->edit_scope);
    {   /* M289: the reported-path set owns its strings. */
        jc_size i;
        for (i = 0; i < e->oos_reported.len; i++) {
            free(*(char **)jc_vec_at(&e->oos_reported, i));
        }
        jc_vec_free(&e->oos_reported);
        for (i = 0; i < e->wrote.len; i++) {
            free(*(char **)jc_vec_at(&e->wrote, i));
        }
        jc_vec_free(&e->wrote);
    }
}

int jc_env_oos_reported(const struct jc_envelope *e, const char *path)
{
    jc_size i;
    if (e == NULL || path == NULL) {
        return 0;
    }
    for (i = 0; i < e->oos_reported.len; i++) {
        const char *p = *(char **)jc_vec_at((struct jc_vec *)&e->oos_reported, i);
        if (p != NULL && strcmp(p, path) == 0) {
            return 1;
        }
    }
    return 0;
}

void jc_env_oos_mark(struct jc_envelope *e, const char *path)
{
    char *copy;
    if (e == NULL || path == NULL || jc_env_oos_reported(e, path)) {
        return;
    }
    copy = jc_strdup(path);
    if (copy != NULL && jc_vec_push(&e->oos_reported, &copy) != JC_OK) {
        free(copy);
    }
}

void jc_env_record_tokens(struct jc_envelope *e, double in_tok, double out_tok)
{
    if (e == NULL) {
        return;
    }
    /* M329: the single chokepoint every model call's usage passes through, at every
     * depth -- so this is where "a call happened after the envelope concluded" is
     * cheapest to notice, and it catches paths nobody has thought of yet rather than
     * only the one that motivated it (M328's learn-on-stop). The outcome is set once,
     * at the end of the run; tokens arriving afterwards are spend the journal's `end`
     * event has already failed to count. */
    if (e->outcome != JC_ENV_RUNNING) {
        e->post_outcome_calls++;
        e->post_outcome_tokens += in_tok + out_tok;
        if (!e->post_outcome_warned) {
            cJSON *jo;
            e->post_outcome_warned = 1;
            /* Warned on the FIRST occurrence, not at teardown: the process may not
             * reach teardown, and one line naming the outcome is what an operator
             * needs to act. The tally keeps accumulating for anyone who asks later. */
            jc_logf(JC_LOG_WARN,
                    "envelope: a model call was metered AFTER the run ended (%s) -- "
                    "its tokens are outside the run's accounting and will not appear "
                    "in the journal or `jichi runs`",
                    jc_env_outcome_name(e->outcome));
            jo = jc_env_journal_begin(e, "post_outcome");
            if (jo != NULL) {
                cJSON_AddStringToObject(jo, "outcome",
                                        jc_env_outcome_name(e->outcome));
            }
            jc_env_journal_end(e, jo);
        }
    }
    e->tokens_used += in_tok + out_tok;
    /* M431f: the denominator of the panel's rate. Counted HERE because this is the
     * one chokepoint every model call passes through, at every depth -- a rate that
     * ignored a subagent's calls while counting its tokens would read low exactly
     * when a run is delegating hardest. */
    e->model_calls++;
}

int jc_env_run_verify(const char *cmd, const char *cwd, struct jc_sb *out,
                      volatile int *abort, long timeout_secs)
{
    int fds[2];
    pid_t pid;
    int status = 0;
    char buf[1024];
    double deadline;
    int killed = 0;

    if (cmd == NULL || cmd[0] == '\0') {
        return -1;
    }
    if (jc_pipe_cloexec(fds) != 0) {
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        dup2(fds[1], STDOUT_FILENO);
        dup2(fds[1], STDERR_FILENO);
        close(fds[0]);
        close(fds[1]);
        if (cwd != NULL && cwd[0] != '\0') {
            if (chdir(cwd) != 0) {
                _exit(126);
            }
        }
        jc_proc_scrub_secret_env(); /* the verifier command must not see keys */
        jc_proc_child_close_fds(); /* M472: and not our fds */
        jc_proc_child_sigreset(); /* M461 */
        execl(jc_shell_path(), "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    close(fds[1]);

    /* Read in 200ms slices so the timeout and abort flag are honoured even
     * while the verifier is producing no output.
     *
     * M368: the deadline is MONOTONIC (jc_now_millis), not time(NULL). The
     * kill deadline must never sit on a steppable clock: a backward step
     * (VM guest time-sync, NTP) landing inside the window makes it
     * unreachable, the child runs to completion, and the runner reports the
     * child's exit instead of TIMEOUT -- observed once as a one-in-four
     * flake of the sleep-5-vs-1s unit check during a minutes-long valgrind
     * run on a VirtualBox guest, where the exposure is maximal. Whole-second
     * time() granularity also made a 1s timeout mean anything up to ~2s. */
    deadline = (timeout_secs > 0)
                   ? jc_now_millis() + (double)timeout_secs * 1000.0
                   : 0.0;
    for (;;) {
        fd_set rf;
        struct timeval tv;
        int rc;
        ssize_t n;

        if ((abort != NULL && *abort) ||
            (deadline > 0.0 && jc_now_millis() > deadline)) {
            kill(pid, SIGKILL);
            killed = 1;
            break;
        }
        FD_ZERO(&rf);
        FD_SET(fds[0], &rf);
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        rc = select(fds[0] + 1, &rf, NULL, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (rc == 0) {
            continue;
        }
        n = read(fds[0], buf, sizeof(buf));
        if (n > 0) {
            if (out != NULL) {
                jc_sb_append_n(out, buf, (jc_size)n);
            }
        } else {
            break; /* EOF or read error */
        }
    }
    close(fds[0]);
    waitpid(pid, &status, 0);
    if (killed) {
        return JC_VERIFY_TIMEOUT;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

cJSON *jc_env_journal_begin(struct jc_envelope *e, const char *event)
{
    cJSON *o;

    if (e == NULL || e->journal == NULL) {
        return NULL;
    }
    o = cJSON_CreateObject();
    if (o == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(o, "ts", (double)time(NULL));
    if (e->run_id != NULL) {
        cJSON_AddStringToObject(o, "run", e->run_id);
    }
    cJSON_AddStringToObject(o, "event", event != NULL ? event : "");
    /* M290: the build, on the run-level `start` record only -- one journal is one
     * run, so repeating it per event would be noise (unlike the telemetry log,
     * which mixes runs and gets filtered). */
    if (event != NULL && strcmp(event, "start") == 0) {
        cJSON_AddStringToObject(o, "jichi", JC_VERSION);
    }
    return o;
}

void jc_env_journal_end(struct jc_envelope *e, cJSON *o)
{
    if (o == NULL) {
        return;
    }
    if (e != NULL && e->journal != NULL) {
        char *s = jc_json_print(o);
        if (s != NULL) {
            fputs(s, e->journal);
            fputc('\n', e->journal);
            fflush(e->journal);
            free(s);
        }
    }
    cJSON_Delete(o);
}


void jc_env_test_edit_note(int nth, const char *path, char *out, jc_size cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    if (nth <= 1) {
        jc_snprintf(out, cap,
            "\n\n[jichi] NOTE: that edit changed a TEST ASSERTION in %s, and it is "
            "recorded. If the goal was to make a failing gate pass, it does not count "
            "-- the gate is how this work is judged, not part of it. If the test "
            "itself was genuinely wrong, that is a fair fix: say so in your final "
            "answer, naming what the correct expectation is and why.",
            (path != NULL) ? path : "a test file");
        return;
    }
    jc_snprintf(out, cap,
        /* Counted, not ordinal: "%dth" produces "the 3th", which is the bug
         * jc_toolloop_render already made and fixed the same way. Caught here by
         * this note's own unit test, which asserts the absence. */
        "\n\n[jichi] NOTE: this run has now changed %d test assertions "
        "(latest: %s). Every one is recorded and reported with the result. If the "
        "code cannot satisfy the tests as written, say that plainly in your final "
        "answer instead of adjusting more expectations -- a green reached by moving "
        "the goalpost is reported as tainted, not as success.",
        nth, (path != NULL) ? path : "a test file");
}
