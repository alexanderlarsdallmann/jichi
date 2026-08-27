/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_runsview.c - offline summarizer for autonomy-envelope run journals
 * (M158). See jc_runsview.h. Pure: one journal's text in, one summary row out. */

#include "jc_runsview.h"
#include "jc_json.h"
#include "jc_snprintf.h"
#include "cJSON.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* M421: is `ev` a name only the ENVELOPE JOURNAL uses?
 *
 * Both sinks write JSONL objects keyed `"event"`, and since M420 both carry
 * `run` -- so "this file parsed as JSON and has a run id" no longer tells a
 * journal from a telemetry log. `runs <dir>` globs *.jsonl, so pointing
 * --journal and --log at one campaign directory rendered every run twice.
 *
 * The test is POSITIVE (does a journal-only name appear?) rather than a
 * blocklist of telemetry names, which would rot the day telemetry adds one.
 * `constraint`, `route` and `tool_call` are in BOTH vocabularies and are
 * deliberately absent below; `tests/smoke/telemetry_events_lint.sh` fails if
 * that stops being true. `start` is listed because a run killed before it could
 * write `end` has only that one line, and that row is precisely the diagnostic
 * a supervisor needs. */
static int journal_exclusive(const char *ev)
{
    static const char *NAMES[] = {
        "open", "start", "end", "verify", "budget", "budget_notice", "rollback",
        "baseline", "checkpoint", "preserved", "strict_green", "self_review",
        "ask", "control", "post_outcome", "verify_stuck",
        "test_assertion_edit", "learn_on_stop", "out_of_scope",
        "blocked_repeat", "tool_loop", NULL
    };
    int i;

    for (i = 0; NAMES[i] != NULL; i++) {
        if (strcmp(ev, NAMES[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static void feed_event(struct jc_run_summary *s, const char *line,
                       jc_size len)
{
    char *buf;
    cJSON *o;
    const char *ev;
    double ts;

    if (len == 0) {
        return;
    }
    buf = (char *)malloc(len + 1);
    if (buf == NULL) {
        return;
    }
    memcpy(buf, line, len);
    buf[len] = '\0';
    o = cJSON_Parse(buf);
    free(buf);
    if (o == NULL) {
        return;
    }

    s->events++;
    ts = jc_json_get_num(o, "ts", 0.0);
    if (ts > 0.0) {
        if (s->ts_first <= 0.0 || ts < s->ts_first) {
            s->ts_first = ts;
        }
        if (ts > s->ts_last) {
            s->ts_last = ts;
        }
    }
    if (s->run[0] == '\0') {
        jc_snprintf(s->run, sizeof(s->run), "%s",
                    jc_json_get_str(o, "run", ""));
    }

    ev = jc_json_get_str(o, "event", "");
    if (journal_exclusive(ev)) {
        s->journal_events++;
    }
    /* M420: the workspace, stamped on `start`. Captured the same way as the
     * build below (first non-empty wins) rather than gated on the event name,
     * so a journal that grows the field elsewhere still yields it. */
    if (s->ws[0] == '\0') {
        const char *wv = jc_json_get_str(o, "ws", NULL);
        if (wv != NULL && wv[0] != '\0') {
            jc_snprintf(s->ws, sizeof(s->ws), "%s", wv);
        }
    }
    /* M290: the build, stamped on the run-level `start` record. */
    if (s->jichi[0] == '\0') {
        const char *jv = jc_json_get_str(o, "jichi", NULL);
        if (jv != NULL && jv[0] != '\0') {
            jc_snprintf(s->jichi, sizeof(s->jichi), "%s", jv);
        }
    }
    if (strcmp(ev, "end") == 0) {
        jc_snprintf(s->outcome, sizeof(s->outcome), "%s",
                    jc_json_get_str(o, "outcome", "?"));
        s->rolled_back = jc_json_get_bool(o, "rolled_back", 0);
        s->tokens_used = jc_json_get_num(o, "tokens_used", 0.0);
        s->tool_calls = (long)jc_json_get_num(o, "tool_calls", 0.0);
        /* Emitted on `end`, not `budget` -- unlike `starved`, which is a
         * budget-exhaustion fact. Putting this parse in the budget branch made it
         * dead code: the journal carried no_changes and the reader never read it,
         * which is the same shape of bug as a gate that checks the wrong thing. */
        s->no_changes = jc_json_get_bool(o, "no_changes", 0);
    } else if (strcmp(ev, "verify") == 0) {
        if (jc_json_get_num(o, "exit", -1.0) == 0.0) {
            s->verify_pass++;
        } else {
            s->verify_fail++;
        }
    } else if (strcmp(ev, "budget") == 0) {
        jc_snprintf(s->budget_kind, sizeof(s->budget_kind), "%s",
                    jc_json_get_str(o, "kind", ""));
        if (jc_json_get_bool(o, "starved", 0)) {
            s->starved = 1;
        }
    } else if (strcmp(ev, "rollback") == 0) {
        s->rollbacks++;
    } else if (strcmp(ev, "ask") == 0) {
        /* M359: the dual of `control`/inject below -- the model asked the
         * human. Only the UNANSWERED ones are counted: an answered question
         * already shaped the run through the history, while an unanswered one
         * means the model guessed at a decision it judged blocking. */
        if (!jc_json_get_bool(o, "answered", 0)) {
            s->asks_unanswered++;
        }
    } else if (strcmp(ev, "control") == 0) {
        /* M161: an operator steered this run over the M159 control channel.
         * Count injects (steering text the model saw); pause/resume/abort
         * surface elsewhere (wall-time, the interrupted outcome). */
        if (strcmp(jc_json_get_str(o, "cmd", ""), "inject") == 0) {
            s->steered++;
        }
    } else if (strcmp(ev, "post_outcome") == 0) {
        /* M329: emitted once, when a model call is metered after the outcome was
         * decided. The run's own totals cannot include what came after them, so
         * this flag is the only thing that says the numbers above are short. */
        s->post_outcome++;
    } else if (strcmp(ev, "blocked_repeat") == 0) {
        /* M429: the run re-attempted a policy-forbidden action. Counting the
         * events, like verify_stuck below: each one is a repeat that was told
         * about, so the count is how many times it needed telling. */
        s->blocked_repeats++;
    } else if (strcmp(ev, "tool_loop") == 0) {
        /* M432: a tool call kept failing the same way inside one turn. Counting
         * the EVENTS for the same reason as blocked= above: the detector tells the
         * model once per (tool, cause), so the count is how many distinct loops
         * this run had to be told about. */
        s->tool_loops++;
    } else if (strcmp(ev, "verify_stuck") == 0) {
        /* M420: M89 emits this when the verify output's failure signature
         * repeats. Counting the EVENTS, not the `repeat` field's value: the
         * event fires once per repeated failure, so the count is the number of
         * times the run was told it was stuck. */
        s->verify_stuck++;
    } else if (strcmp(ev, "test_assertion_edit") == 0) {
        /* M420: M88's moved-goalpost heuristic. Advisory in the envelope (real
         * work edits tests), but a supervisor reading a GREEN row deserves to
         * know the gate moved under it. */
        s->test_edits++;
    } else if (strcmp(ev, "learn_on_stop") == 0) {
        /* M330: emitted once, after the run's `end` event, carrying the mentor's
         * token and tool-call cost. Non-zero means the run's totals are SHORT. */
        s->learn_tokens = jc_json_get_num(o, "tokens", 0.0);
        s->learn_calls = (long)jc_json_get_num(o, "tool_calls", 0.0);
        /* M598: what the draft would commit. Absent on a pre-M598 journal or when
         * the learn command declared no output -> -1, "not measured", never 0. */
        s->learn_draft_items = (long)jc_json_get_num(o, "draft_items", -1.0);
        s->learn_draft_empty =
            jc_json_get_num(o, "draft_parsed_nothing", 0.0) > 0.0 ? 1 : 0;
    } else if (strcmp(ev, "constraint") == 0) {
        /* An inferred constraint silently narrows what the run may do, and it
         * announces itself on stderr exactly once. Counting it here is what makes
         * "this run did less than I expected" answerable after the fact. */
        double n = jc_json_get_num(o, "adopted", 1.0);
        s->constraints += (long)(n > 0.0 ? n : 1.0);
    } else if (strcmp(ev, "out_of_scope") == 0) {
        double n = jc_json_get_num(o, "paths", 1.0);
        s->out_of_scope += (long)(n > 0.0 ? n : 1.0);
    }

    cJSON_Delete(o);
}

int jc_runsview_parse(const char *text, struct jc_run_summary *out)
{
    const char *p;

    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->rolled_back = -1;
    out->learn_draft_items = -1; /* M598: "not measured" until the event says */
    if (text == NULL) {
        return -1;
    }
    p = text;
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        jc_size len = (nl != NULL) ? (jc_size)(nl - p) : strlen(p);
        feed_event(out, p, len);
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    /* M421: journal-exclusive events, not merely parsed lines. A telemetry log
     * fed here parses fine and would otherwise render as an all-zero phantom run
     * wearing a real run id. */
    if (out->journal_events == 0) {
        return -1;
    }
    if (out->outcome[0] == '\0') {
        /* No `end` event: the process died or is still running. */
        jc_snprintf(out->outcome, sizeof(out->outcome), "%s", "?");
    }
    return 0;
}

static void fmt_when(double ts, char *buf, jc_size cap)
{
    time_t t = (time_t)ts;
    struct tm *tm;

    if (ts <= 0.0) {
        jc_snprintf(buf, cap, "%s", "?");
        return;
    }
    tm = localtime(&t);
    if (tm == NULL || strftime(buf, cap, "%m-%d %H:%M", tm) == 0) {
        jc_snprintf(buf, cap, "%.0f", ts);
    }
}

static void fmt_tokens(double n, char *buf, jc_size cap)
{
    if (n >= 1000000.0) {
        jc_snprintf(buf, cap, "%.1fm", n / 1000000.0);
    } else if (n >= 1000.0) {
        jc_snprintf(buf, cap, "%.1fk", n / 1000.0);
    } else {
        jc_snprintf(buf, cap, "%.0f", n);
    }
}

cJSON *jc_runsview_json(const struct jc_run_summary *s)
{
    cJSON *o;
    cJSON *verify;

    if (s == NULL) {
        return NULL;
    }
    o = cJSON_CreateObject();
    if (o == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(o, "run", s->run[0] != '\0' ? s->run : "?");
    cJSON_AddNumberToObject(o, "ts", s->ts_first);
    cJSON_AddNumberToObject(o, "ts_end", s->ts_last);
    cJSON_AddStringToObject(o, "outcome", s->outcome);
    if (s->rolled_back < 0) {
        cJSON_AddNullToObject(o, "rolled_back"); /* no `end` event: unknown */
    } else {
        cJSON_AddBoolToObject(o, "rolled_back", s->rolled_back);
    }
    cJSON_AddNumberToObject(o, "tokens_used", s->tokens_used);
    cJSON_AddNumberToObject(o, "tool_calls", (double)s->tool_calls);
    if (s->jichi[0] != '\0') {
        cJSON_AddStringToObject(o, "jichi", s->jichi); /* M290 */
    }
    if (s->budget_kind[0] != '\0') {
        cJSON_AddStringToObject(o, "budget", s->budget_kind);
    }
    if (s->starved) {
        cJSON_AddBoolToObject(o, "starved", 1);
    }
    verify = cJSON_CreateObject();
    if (verify != NULL) {
        cJSON_AddNumberToObject(verify, "pass", (double)s->verify_pass);
        cJSON_AddNumberToObject(verify, "fail", (double)s->verify_fail);
        cJSON_AddItemToObject(o, "verify", verify);
    }
    if (s->out_of_scope > 0) {
        cJSON_AddNumberToObject(o, "out_of_scope", (double)s->out_of_scope);
    }
    if (s->rollbacks > 0) {
        cJSON_AddNumberToObject(o, "rollbacks", (double)s->rollbacks);
    }
    if (s->no_changes) {
        cJSON_AddBoolToObject(o, "no_changes", 1);
    }
    if (s->constraints > 0) {
        cJSON_AddNumberToObject(o, "constraints", (double)s->constraints);
    }
    if (s->steered > 0) {
        cJSON_AddNumberToObject(o, "steered", (double)s->steered);
    }
    if (s->verify_stuck > 0) {
        cJSON_AddNumberToObject(o, "verify_stuck", (double)s->verify_stuck);
    }
    if (s->test_edits > 0) {
        cJSON_AddNumberToObject(o, "test_edits", (double)s->test_edits);
    }
    if (s->blocked_repeats > 0) {
        cJSON_AddNumberToObject(o, "blocked_repeats",
                                (double)s->blocked_repeats);
    }
    if (s->tool_loops > 0) {
        cJSON_AddNumberToObject(o, "tool_loops", (double)s->tool_loops);
    }
    if (s->ws[0] != '\0') {
        cJSON_AddStringToObject(o, "ws", s->ws);
    }
    if (s->asks_unanswered > 0) {
        cJSON_AddNumberToObject(o, "unanswered",
                                (double)s->asks_unanswered);
    }
    if (s->post_outcome > 0) {
        cJSON_AddBoolToObject(o, "post_outcome", 1);
    }
    if (s->learn_tokens > 0.0) {
        cJSON_AddNumberToObject(o, "learn_tokens", s->learn_tokens);
    }
    if (s->learn_calls > 0) {
        cJSON_AddNumberToObject(o, "learn_calls", (double)s->learn_calls);
    }
    if (s->learn_draft_items >= 0) {
        cJSON_AddNumberToObject(o, "learn_draft_items",
                                (double)s->learn_draft_items);
    }
    if (s->learn_draft_empty) {
        cJSON_AddBoolToObject(o, "learn_draft_empty", 1);
    }
    cJSON_AddNumberToObject(o, "events", (double)s->events);
    return o;
}

void jc_runsview_render_header(struct jc_sb *out)
{
    jc_sb_append_fmt(out, "%-22s %-11s %-16s %8s %6s %7s  %s\n",
                     "RUN", "WHEN", "OUTCOME", "TOKENS", "TOOLS",
                     "VERIFY", "NOTES");
}

void jc_runsview_render_row(const struct jc_run_summary *s, struct jc_sb *out)
{
    char when[16];
    char tok[16];
    char verify[16];
    char run[23];
    struct jc_sb notes;

    fmt_when(s->ts_first, when, sizeof(when));
    fmt_tokens(s->tokens_used, tok, sizeof(tok));
    if (s->verify_pass + s->verify_fail > 0) {
        jc_snprintf(verify, sizeof(verify), "%d/%d",
                    s->verify_pass, s->verify_fail);
    } else {
        jc_snprintf(verify, sizeof(verify), "%s", "-");
    }
    /* Bound the run id to its column. */
    if (strlen(s->run) > 22) {
        memcpy(run, s->run, 19);
        strcpy(run + 19, "...");
    } else {
        jc_snprintf(run, sizeof(run), "%s", s->run[0] != '\0' ? s->run : "?");
    }

    jc_sb_init(&notes);
    if (s->rolled_back == 1) {
        jc_sb_append(&notes, "rolled_back ");
    }
    if (s->budget_kind[0] != '\0') {
        jc_sb_append_fmt(&notes, "budget=%s ", s->budget_kind);
    }
    if (s->starved) {
        jc_sb_append(&notes, "starved ");
    }
    if (s->out_of_scope > 0) {
        jc_sb_append_fmt(&notes, "out_of_scope=%ld ", s->out_of_scope);
    }
    if (s->rollbacks > 0) {
        jc_sb_append_fmt(&notes, "rollbacks=%ld ", s->rollbacks);
    }
    if (s->no_changes) {
        jc_sb_append_fmt(&notes, "no_changes ");
    }
    if (s->constraints > 0) {
        jc_sb_append_fmt(&notes, "constraints=%ld ", s->constraints);
    }
    if (s->steered > 0) {
        jc_sb_append_fmt(&notes, "steered=%ld ", s->steered);
    }
    /* M420: the two quality signals. Both are printed ONLY when non-zero -- an
     * always-present "goalposts=0" would train a reader to skip the column the
     * one time it matters. `goalposts` reads as an accusation of the run, which
     * is the point: a green row wearing it should stop a supervisor. */
    if (s->verify_stuck > 0) {
        jc_sb_append_fmt(&notes, "stuck=%ld ", s->verify_stuck);
    }
    if (s->blocked_repeats > 0) {
        /* Only when non-zero, like stuck=/goalposts= -- an always-present
         * blocked=0 trains a reader to skip the column that matters. */
        jc_sb_append_fmt(&notes, "blocked=%ld ", s->blocked_repeats);
    }
    if (s->tool_loops > 0) {
        jc_sb_append_fmt(&notes, "loops=%ld ", s->tool_loops);
    }
    if (s->test_edits > 0) {
        jc_sb_append_fmt(&notes, "goalposts=%ld ", s->test_edits);
    }
    /* M598: a mentor run whose draft `learn apply` would commit nothing from.
     * Printed only when set, like goalposts= -- and it reads as an accusation of
     * the mentor run, which is the point: the row's outcome is `ok` and its
     * lesson is missing. */
    if (s->learn_draft_empty) {
        jc_sb_append(&notes, "draft=empty ");
    }
    if (s->asks_unanswered > 0) {
        jc_sb_append_fmt(&notes, "unanswered=%ld ", s->asks_unanswered);
    }
    if (s->post_outcome > 0) {
        /* Deliberately worded as a warning about the row it appears on: the tokens
         * and tool-call counts to its left are SHORT, because spend continued after
         * the run's outcome was decided and its `end` event written. */
        jc_sb_append_fmt(&notes, "post_outcome(totals_short) ");
    }
    if (s->learn_tokens > 0.0) {
        /* M330: the mentor spent tokens after the run's `end` event, so the row's
         * tokens_used/tool_calls are SHORT. */
        char tok[16];
        fmt_tokens(s->learn_tokens, tok, sizeof(tok));
        jc_sb_append_fmt(&notes, "learn_on_stop(%s) ", tok);
    }

    jc_sb_append_fmt(out, "%-22s %-11s %-16s %8s %6ld %7s  %s\n",
                     run, when, s->outcome, tok, s->tool_calls, verify,
                     notes.data != NULL ? notes.data : "");
    jc_sb_free(&notes);
}
