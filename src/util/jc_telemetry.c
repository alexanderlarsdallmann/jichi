/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* jc_telemetry.c - offline telemetry-log summary (see jc_telemetry.h). */

#include "jc_telemetry.h"
#include "jc_json.h"
/* M591: the reader aggregates by the CANONICAL tool name. The one function it
 * needs from the registry is a pure string map; jc_tool.h pulls in only
 * jc_platform/jc_vec/cJSON, all of which this file already has. */
#include "jc_tool.h"
#include "jc_str.h"
#include "jc_snprintf.h"
#include "jc_snapshot.h"   /* jc_workspace_key: one derivation, three stores (M599) */

#include <stdlib.h>
#include <string.h>

void jc_telemetry_summary_init(struct jc_telemetry_summary *s)
{
    memset(s, 0, sizeof(*s));
    /* M585: -2 so the FIRST nameless call always opens a burst. A zeroed
     * struct would read as (sid "", turn 0), and a real first call in turn 0 of
     * a session whose sid did not parse would then match it and be counted as a
     * continuation of a burst that never started. */
    s->nameless_turn = -2;
    jc_vec_init(&s->models, sizeof(struct jc_telem_model));
    jc_vec_init(&s->tools, sizeof(struct jc_telem_tool));
    jc_vec_init(&s->workspaces, sizeof(struct jc_telem_ws));
    jc_vec_init(&s->sessions, sizeof(struct jc_telem_session));
    jc_vec_init(&s->versions, sizeof(struct jc_telem_version)); /* M290 */
    jc_vec_init(&s->aliases, sizeof(struct jc_telem_alias)); /* M591 */
}

void jc_telemetry_summary_free(struct jc_telemetry_summary *s)
{
    jc_vec_free(&s->versions); /* M290 */
    jc_vec_free(&s->models);
    jc_vec_free(&s->tools);
    jc_vec_free(&s->workspaces);
    jc_vec_free(&s->sessions);
    jc_vec_free(&s->aliases); /* M591 */
}

/* Per-session timeline entry, keyed by `sid`, created in first-seen order (M82). */
static struct jc_telem_session *session_for(struct jc_telemetry_summary *s,
                                            const char *sid)
{
    struct jc_telem_session se;
    jc_size i;
    for (i = 0; i < s->sessions.len; i++) {
        struct jc_telem_session *e =
            (struct jc_telem_session *)jc_vec_at(&s->sessions, i);
        if (strcmp(e->sid, sid) == 0) {
            return e;
        }
    }
    memset(&se, 0, sizeof(se));
    jc_snprintf(se.sid, sizeof(se.sid), "%s", sid);
    se.order = (long)s->sessions.len;
    jc_vec_push(&s->sessions, &se);
    return (struct jc_telem_session *)jc_vec_at(&s->sessions,
                                                s->sessions.len - 1);
}

static struct jc_telem_ws *ws_for(struct jc_telemetry_summary *s,
                                  const char *ws)
{
    struct jc_telem_ws w;
    jc_size i;
    for (i = 0; i < s->workspaces.len; i++) {
        struct jc_telem_ws *e =
            (struct jc_telem_ws *)jc_vec_at(&s->workspaces, i);
        if (strcmp(e->ws, ws) == 0) {
            return e;
        }
    }
    memset(&w, 0, sizeof(w));
    jc_snprintf(w.ws, sizeof(w.ws), "%s", ws);
    jc_vec_push(&s->workspaces, &w);
    return (struct jc_telem_ws *)jc_vec_at(&s->workspaces,
                                           s->workspaces.len - 1);
}

/* Find or create the row for one model. `key` groups (the wire id when the event
 * carries one, else the display name); `name` is what the report shows, refreshed
 * on every event so a renamed model reads under its current name (M289). */
static struct jc_telem_model *model_for(struct jc_telemetry_summary *s,
                                        const char *key, const char *name)
{
    struct jc_telem_model m;
    jc_size i;
    for (i = 0; i < s->models.len; i++) {
        struct jc_telem_model *e =
            (struct jc_telem_model *)jc_vec_at(&s->models, i);
        if (strcmp(e->key, key) == 0) {
            jc_snprintf(e->name, sizeof(e->name), "%s", name);
            return e;
        }
    }
    memset(&m, 0, sizeof(m));
    jc_snprintf(m.key, sizeof(m.key), "%s", key);
    jc_snprintf(m.name, sizeof(m.name), "%s", name);
    jc_vec_push(&s->models, &m);
    return (struct jc_telem_model *)jc_vec_at(&s->models, s->models.len - 1);
}

static struct jc_telem_tool *tool_for(struct jc_telemetry_summary *s,
                                      const char *name)
{
    struct jc_telem_tool t;
    jc_size i;
    for (i = 0; i < s->tools.len; i++) {
        struct jc_telem_tool *e =
            (struct jc_telem_tool *)jc_vec_at(&s->tools, i);
        if (strcmp(e->name, name) == 0) {
            return e;
        }
    }
    memset(&t, 0, sizeof(t));
    jc_snprintf(t.name, sizeof(t.name), "%s", name);
    jc_vec_push(&s->tools, &t);
    return (struct jc_telem_tool *)jc_vec_at(&s->tools, s->tools.len - 1);
}

static struct jc_telem_alias *alias_for(struct jc_telemetry_summary *s,
                                       const char *requested,
                                       const char *canonical)
{
    struct jc_telem_alias a;
    jc_size i;
    for (i = 0; i < s->aliases.len; i++) {
        struct jc_telem_alias *e =
            (struct jc_telem_alias *)jc_vec_at(&s->aliases, i);
        if (strcmp(e->requested, requested) == 0) {
            return e;
        }
    }
    memset(&a, 0, sizeof(a));
    jc_snprintf(a.requested, sizeof(a.requested), "%s", requested);
    jc_snprintf(a.canonical, sizeof(a.canonical), "%s", canonical);
    jc_vec_push(&s->aliases, &a);
    return (struct jc_telem_alias *)jc_vec_at(&s->aliases, s->aliases.len - 1);
}

static void feed_event(struct jc_telemetry_summary *s, cJSON *o)
{
    const char *ev = jc_json_get_str(o, "event", "");
    /* Workspace filter (M56): when set, count only events stamped with a
     * matching "ws". An event without a "ws" can't be attributed -> skipped. */
    if (s->ws_filter[0] != '\0') {
        const char *ws = jc_json_get_str(o, "ws", NULL);
        if (ws == NULL || strcmp(ws, s->ws_filter) != 0) {
            return;
        }
    }
    /* Time window (M286): when set, count only events at or after `min_ts`. An
     * event without a usable "ts" can't be placed in time, so it is skipped for
     * the same reason an unattributed event is skipped by the workspace filter
     * above -- a window that silently admitted undatable events would defeat its
     * own purpose. */
    if (s->min_ts > 0.0 && jc_json_get_num(o, "ts", 0.0) < s->min_ts) {
        return;
    }
    s->events++;
    {   /* M290: which build produced it. Absent on pre-M290 logs -> not tallied,
         * so those reports are byte-identical. */
        const char *ver = jc_json_get_str(o, "jichi", NULL);
        if (ver != NULL && ver[0] != '\0') {
            jc_size i;
            struct jc_telem_version *hit = NULL;
            for (i = 0; i < s->versions.len; i++) {
                struct jc_telem_version *e =
                    (struct jc_telem_version *)jc_vec_at(&s->versions, i);
                if (strcmp(e->ver, ver) == 0) {
                    hit = e;
                    break;
                }
            }
            if (hit == NULL &&
                s->versions.len < 32) { /* a log spanning 32 builds is enough */
                struct jc_telem_version nv;
                memset(&nv, 0, sizeof(nv));
                jc_snprintf(nv.ver, sizeof(nv.ver), "%s", ver);
                if (jc_vec_push(&s->versions, &nv) == JC_OK) {
                    hit = (struct jc_telem_version *)
                          jc_vec_at(&s->versions, s->versions.len - 1);
                }
            }
            if (hit != NULL) {
                hit->events++;
            }
        }
    }
    {
        /* Track the newest event timestamp as the recency reference (used by the
         * insight ranker to age out tools that stopped failing). */
        double ts = jc_json_get_num(o, "ts", 0.0);
        if (ts > s->max_ts) {
            s->max_ts = ts;
        }
    }
    {
        /* Per-workspace tally (M59): attribute by the "ws" stamp (M56). */
        struct jc_telem_ws *w =
            ws_for(s, jc_json_get_str(o, "ws", "(unattributed)"));
        w->events++;
        if (strcmp(ev, "turn_start") == 0) {
            w->turns++;
        }
    }
    if (strcmp(ev, "turn_start") == 0) {
        s->turns++;
    } else if (strcmp(ev, "model_call") == 0) {
        const char *mname = jc_json_get_str(o, "model", "?");
        const char *mid = jc_json_get_str(o, "model_id", NULL);
        struct jc_telem_model *m =
            model_for(s, (mid != NULL && mid[0] != '\0') ? mid : mname, mname);
        double lat = jc_json_get_num(o, "latency_ms", 0.0);
        double in_tok = jc_json_get_num(o, "in_tok", 0.0);
        double cache = jc_json_get_num(o, "cache_read_in", 0.0) +
                       jc_json_get_num(o, "cache_write_in", 0.0);
        double out_tok = jc_json_get_num(o, "out_tok", 0.0);
        double cost = jc_json_get_num(o, "cost_usd", 0.0);
        m->calls++;
        if (!jc_json_get_bool(o, "ok", 0)) {
            m->errors++;
            s->errors++;
            if (strcmp(jc_json_get_str(o, "result", ""), "timeout") == 0) {
                m->timeouts++;
                s->timeouts++;
            }
            /* M321: a transport failure never reached HTTP, so it has no status
             * to group by. Split it by the recorded diagnosis instead: a
             * connect-phase failure means the request was never sent and there
             * is a knob to turn, which is exactly what a 15%-failure workload
             * could not learn from its own log. */
            {
                const char *tr = jc_json_get_str(o, "transport", "");
                if (tr[0] != '\0') {
                    s->transport_fail++;
                    if (strstr(tr, "timeouts.connect") != NULL) {
                        s->connect_fail++;
                    }
                }
            }
        }
        m->in_tok += in_tok;
        m->out_tok += out_tok;
        m->cache_read += jc_json_get_num(o, "cache_read_in", 0.0);
        m->cache_write += jc_json_get_num(o, "cache_write_in", 0.0);
        m->cost += cost;
        m->lat_sum += lat;
        m->lat_n++;
        if (lat > m->lat_max) {
            m->lat_max = lat;
        }
        {   /* M192 input attribution. Present only on post-M192 events, so
             * `attr_n` gates the rendered line and old logs are unchanged. */
            cJSON *sysf = cJSON_GetObjectItem(o, "sys_tok");
            if (sysf != NULL) {
                double sy = jc_json_get_num(o, "sys_tok", 0.0);
                double to = jc_json_get_num(o, "tools_tok", 0.0);
                double hi = jc_json_get_num(o, "hist_tok", 0.0);
                double est = sy + to + hi;
                m->sys_tok += sy;
                m->tools_tok += to;
                m->hist_tok += hi;
                m->attr_n++;
                /* Real vs estimated, i.e. the observed M77 ratio. Only for
                 * calls that reported a real prompt count. */
                if (est > 0.0 && in_tok > 0.0) {
                    m->drift_sum += in_tok / est;
                    m->drift_n++;
                }
            }
        }
        {   /* M82 per-session timeline. */
            struct jc_telem_session *se = session_for(s,
                jc_json_get_str(o, "sid", "(no-session)"));
            double total_in = in_tok + cache;
            se->calls++;
            se->in_tok += total_in;
            /* M592: kept separately so a per-session hit-rate is computable.
             * `in_tok` above is the TOTAL (cached included), which cannot be
             * divided back out. */
            se->cache_read += jc_json_get_num(o, "cache_read_in", 0.0);
            se->uncached += in_tok;
            se->out_tok += out_tok;
            se->cost += cost;
            if (total_in > se->peak_in) {
                se->peak_in = total_in;
            }
        }
    } else if (strcmp(ev, "model_retry") == 0) {
        s->retries++;
    } else if (strcmp(ev, "tool_call") == 0) {
        /* M591: one tool, one row. The log keeps the raw wire name (M532's
         * decision, unchanged); the reader groups by what actually RAN, so an
         * aliased call lands in its tool's row instead of starting a new one.
         * Retroactive by construction -- it needs no new field, so it also fixes
         * the logs already on disk. */
        const char *raw = jc_json_get_str(o, "name", "?");
        const char *tname = jc_tool_canonical_name(raw);
        struct jc_telem_tool *t;
        struct jc_telem_alias *al;
        struct jc_telem_session *se = session_for(s,
            jc_json_get_str(o, "sid", "(no-session)"));
        double d = jc_json_get_num(o, "duration_ms", 0.0);
        double ts = jc_json_get_num(o, "ts", 0.0);
        if (tname == NULL) {
            tname = raw;
        }
        if (strcmp(raw, tname) != 0) {
            al = alias_for(s, raw, tname);
            if (al != NULL) {
                al->calls++;
            }
        }
        t = tool_for(s, tname);
        t->calls++;
        se->tools++;
        /* M585: a nameless call, and whether it STARTED a burst. A burst is a run
         * of consecutive nameless calls inside one (sid, turn) -- the shape a
         * model makes when the error it was given does not fit the mistake it
         * made. Tracked with the previous nameless call's coordinates rather
         * than a per-turn table, because the log is read in order and a burst is
         * by definition consecutive. */
        if (tname != NULL && tname[0] == '\0') {
            const char *sid = jc_json_get_str(o, "sid", "");
            long turn = (long)jc_json_get_num(o, "turn", -1);
            s->tool_nameless++;
            if (s->nameless_turn != turn ||
                strncmp(s->nameless_sid, sid, sizeof(s->nameless_sid) - 1) != 0) {
                s->tool_nameless_bursts++;
            }
            s->nameless_turn = turn;
            jc_snprintf(s->nameless_sid, sizeof(s->nameless_sid), "%s", sid);
        }
        if (ts > t->last_ts) {
            t->last_ts = ts;
        }
        if (jc_json_get_bool(o, "ok", 0)) {
            t->ok++;
            se->tool_ok++;
            if (ts > t->last_ok_ts) {
                t->last_ok_ts = ts;
            }
        } else if (ts > t->last_fail_ts) {
            t->last_fail_ts = ts;
        }
        t->dur_sum += d;
        if (d > t->dur_max) {
            t->dur_max = d;
        }
        t->out_bytes += jc_json_get_num(o, "output_bytes", 0.0);
        /* M168: a non-zero `exit` means the command reported failure, not that
         * the tool broke. Absent on pre-M168 logs and on non-command tools. */
        if (!jc_json_get_bool(o, "ok", 0) &&
            jc_json_get_num(o, "exit", 0.0) > 0.0) {
            t->cmd_fail++;
        }
    } else if (strcmp(ev, "nudge") == 0) {
        /* M167: the M147 prose-tool-call nudge. `phase` is "fired" when a
         * narrated call was detected, "recovered" when the corrective retry
         * produced a native call. */
        const char *phase = jc_json_get_str(o, "phase", "");
        if (strcmp(phase, "fired") == 0) {
            s->nudge_fired++;
        } else if (strcmp(phase, "recovered") == 0) {
            s->nudge_recovered++;
        }
    } else if (strcmp(ev, "test_edit") == 0) {
        /* M417: the moved-goalpost heuristic fired (M88). One is already
         * lesson-worthy; see jc_insights. */
        s->test_edits++;
    } else if (strcmp(ev, "hook") == 0) {
        /* M584: emitted since M326v, read by nothing until now. The outcome
         * vocabulary is bounded by jc_hooks.c and stays that way (D7: a
         * classifier output, never a message). `not_runnable` is the one that
         * matters most and was the last to be emitted at all -- a hook whose
         * script is missing exits 127, which means THE CHECK NEVER RAN, and
         * which is indistinguishable from "ran and complained" unless the
         * outcome says so. */
        const char *oc = jc_json_get_str(o, "outcome", "");
        if (strcmp(oc, "start_failed") == 0)      { s->hook_start_failed++; }
        else if (strcmp(oc, "timeout") == 0)      { s->hook_timeout++; }
        else if (strcmp(oc, "not_runnable") == 0) { s->hook_not_runnable++; }
        else                                      { s->hook_nonzero++; }
    } else if (strcmp(ev, "privileged") == 0) {
        const char *d = jc_json_get_str(o, "decision", "");
        s->privileged_total++;
        if (strstr(d, "approved") == NULL) { s->privileged_refused++; }
    } else if (strcmp(ev, "kinetic") == 0) {
        const char *d = jc_json_get_str(o, "decision", "");
        s->kinetic_total++;
        if (strstr(d, "approved") == NULL) { s->kinetic_refused++; }
    } else if (strcmp(ev, "prefix_churn") == 0) {
        long st = (long)jc_json_get_num(o, "streak", 0);
        s->prefix_churn++;
        if (st > s->prefix_churn_max) { s->prefix_churn_max = st; }
    } else if (strcmp(ev, "retrieve") == 0) {
        s->retrieve_calls++;
        s->retrieve_blocks += (long)jc_json_get_num(o, "blocks", 0);
        s->retrieve_tokens += (long)jc_json_get_num(o, "tokens", 0);
    } else if (strcmp(ev, "args_truncated") == 0) {
        s->args_truncated++;
    } else if (strcmp(ev, "history_check") == 0) {
        s->history_checks++;
    } else if (strcmp(ev, "constraint") == 0) {
        s->constraints++;
    } else if (strcmp(ev, "constraint_exempt") == 0) {
        s->constraint_exempts++;
    } else if (strcmp(ev, "args_repair") == 0) {
        /* M167: the M148 malformed-arguments repair. Counted either way, so the
         * success share is readable. */
        s->repair_total++;
        if (jc_json_get_bool(o, "ok", 0)) {
            s->repair_ok++;
        }
    } else if (strcmp(ev, "route") == 0) {
        s->routes++;
    } else if (strcmp(ev, "compact") == 0) {
        s->compacts++;
        session_for(s, jc_json_get_str(o, "sid", "(no-session)"))->compacts++;
        /* M192: only post-M192 events carry the split; absent => both stay 0
         * and the reclaim line is not rendered. */
        s->compact_dup += (long)jc_json_get_num(o, "dup", 0.0);
        s->compact_age += (long)jc_json_get_num(o, "age", 0.0);
        /* M323: absent on pre-M323 events => stays 0 and the line is omitted.
         *
         * M326x: `short` alone over-counts. Until M326x the emitter wrote
         * `!reached` without consulting `pressed`, so every eager zero-loss
         * dedup -- which has no target to miss -- logged short:true, and this
         * counter drove a rendered claim that "requests went out over the
         * configured contextLimit" about requests that had not. All 19 such
         * events in the measured workload were false positives.
         *
         * A long-lived log spans that fix (the M286 problem), so read both
         * shapes: trust `pressed` when the event carries it, and otherwise
         * infer pressure the way the pass itself decides it -- the lossy
         * age/args trims run ONLY past the high-water mark. */
        if (strcmp(jc_json_get_str(o, "phase", ""), "midturn") == 0) {
            int pressed;
            s->compact_midturn++;
            /* Pressure is a MID-TURN concept -- the high-water trigger belongs
             * to jc_compact_midturn, and the between-turn pass has no such
             * notion. Scoping matters for the old-shape inference below: a
             * between-turn event also carries `age`, so an unscoped rule counts
             * it as pressure. (Caught by its own unit test, which is why the
             * fixture still contains one.) */
            if (cJSON_GetObjectItem(o, "pressed") != NULL) {
                pressed = jc_json_get_bool(o, "pressed", 0);
            } else {
                /* Pre-M326x event: infer pressure the way the pass decides it
                 * -- the lossy age/args trims run ONLY past the high-water. */
                pressed = ((long)jc_json_get_num(o, "age", 0.0)
                           + (long)jc_json_get_num(o, "args", 0.0)) > 0;
            }
            if (pressed) {
                s->compact_pressed++;
                if (jc_json_get_bool(o, "short", 0)) {
                    s->compact_short++;
                }
                /* NOT inferred on older events. It is a fact about the
                 * estimate AFTER the pass, and a log without before/after
                 * simply does not contain it -- guessing from elision counts
                 * was tried and is wrong (the repeating passes do elide
                 * something; they just achieve nothing). */
                if (jc_json_get_bool(o, "unrelieved", 0)) {
                    s->compact_unrelieved++;
                }
            }
        }
    } else if (strcmp(ev, "turn_end") == 0) {
        /* M92: tally the autonomy-envelope outcome. Only turns run under an
         * envelope carry `outcome`; a budget stop splits kept vs reverted by
         * `rolled_back` so a banked-green run isn't counted as a failure. */
        const char *out = jc_json_get_str(o, "outcome", "");
        if (strcmp(out, "ok") == 0) {
            s->out_completed++;
        } else if (strcmp(out, "verify_failed") == 0) {
            s->out_verify_failed++;
        } else if (strcmp(out, "budget_exhausted") == 0) {
            if (jc_json_get_bool(o, "rolled_back", 0)) {
                s->out_budget_reverted++;
            } else {
                s->out_budget_kept++;
            }
        }
    }
}

void jc_telemetry_feed(struct jc_telemetry_summary *s, const char *text)
{
    const char *p = text;
    if (text == NULL) {
        return;
    }
    while (*p != '\0') {
        const char *nl = strchr(p, '\n');
        jc_size len = (nl != NULL) ? (jc_size)(nl - p) : (jc_size)strlen(p);
        if (len > 0) {
            char *line = (char *)malloc(len + 1);
            if (line != NULL) {
                cJSON *o;
                memcpy(line, p, len);
                line[len] = '\0';
                o = jc_json_parse(line);
                if (o != NULL) {
                    feed_event(s, o);
                    cJSON_Delete(o);
                }
                free(line);
            }
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
}

void jc_telemetry_summarize(const char *text, struct jc_telemetry_summary *out)
{
    jc_telemetry_summary_init(out);
    jc_telemetry_feed(out, text);
}

void jc_telemetry_render(const struct jc_telemetry_summary *s, struct jc_sb *out)
{
    jc_size i;
    if (s->ws_filter[0] != '\0') {
        jc_sb_append_fmt(out, "workspace: %s\n", s->ws_filter);
    }
    /* Say so when a window is in force (M286): an unlabelled partial summary is
     * exactly the kind of number that gets quoted as if it covered everything. */
    if (s->min_ts > 0.0) {
        jc_sb_append_fmt(out, "window: events at or after ts=%.0f\n", s->min_ts);
    }
    /* M290: which build(s) produced these numbers. One build is a one-line fact;
     * SEVERAL is a warning, because every rate below then mixes eras -- the shape
     * of two live-defect reports in this project that were both fixed weeks
     * before the log was read. Silent on pre-M290 logs (no `jichi` field). */
    if (s->versions.len == 1) {
        const struct jc_telem_version *v0 = (const struct jc_telem_version *)
            jc_vec_at((struct jc_vec *)&s->versions, 0);
        jc_sb_append_fmt(out, "jichi: %s\n", v0->ver);
    } else if (s->versions.len > 1) {
        jc_size i;
        jc_sb_append_fmt(out,
            "jichi: %lu BUILDS in this log -- every rate below mixes them; "
            "window with --since to read one era\n",
            (unsigned long)s->versions.len);
        for (i = 0; i < s->versions.len; i++) {
            const struct jc_telem_version *vi =
                (const struct jc_telem_version *)
                jc_vec_at((struct jc_vec *)&s->versions, i);
            jc_sb_append_fmt(out, "  %-12s %ld event(s)\n", vi->ver,
                             vi->events);
        }
    }
    jc_sb_append_fmt(out,
        "events=%ld turns=%ld retries=%ld routes=%ld compacts=%ld errors=%ld "
        "(timeouts=%ld)\n",
        s->events, s->turns, s->retries, s->routes, s->compacts, s->errors,
        s->timeouts);

    /* M321: transport failures, named. These never reached HTTP, so they carry no
     * status to group by and used to disappear into `errors`. A connect-phase
     * failure is the one with a knob: the request was never sent, so no tokens
     * were spent -- only wall clock, and 6.5 hours of it in the workload that
     * prompted this line. Rendered only when present, so existing reports are
     * unchanged. */
    if (s->transport_fail > 0) {
        jc_sb_append_fmt(out,
            "Transport failures: %ld model call(s) never reached HTTP",
            s->transport_fail);
        if (s->connect_fail > 0) {
            jc_sb_append_fmt(out,
                " -- %ld could not CONNECT (no request sent; raise "
                "timeouts.connect)", s->connect_fail);
        }
        jc_sb_append(out, "\n");
    }

    /* Compaction reclaim (M192): which mechanism did the eliding. The dedup pass
     * (M93/M94) is zero-loss -- it drops read_file results superseded by a later
     * read of the same file -- while the age-based fallback discards the middle of
     * output the agent may still need. A report dominated by `age` means the
     * dedup is not catching the duplication, which is a fixable defect rather
     * than a fact of the workload. Rendered only when the split is present. */
    /* M323: the pass ran under pressure and could not get under its target, so
     * the request went out over the operator's stated contextLimit. Rendered
     * before the reclaim split because it is the more urgent fact, and only when
     * present -- pre-M323 logs have no `short` field and are unchanged. */
    /* M326x: how much of the compaction count was actually under pressure. The
     * raw total counts the eager zero-loss dedup too, which runs every round
     * with a new read result and is routine housekeeping -- 44% of one
     * workload's 1,057 mid-turn events. Without this split "1,057 compactions"
     * reads as alarm at more than twice the true rate. */
    if (s->compact_midturn > 0 && s->compact_pressed > 0) {
        jc_sb_append_fmt(out,
            "Compaction pressure: %ld of %ld mid-turn pass(es) ran with the "
            "high-water trigger fired (%ld%%); the rest were the eager "
            "zero-loss dedup.\n",
            s->compact_pressed, s->compact_midturn,
            s->compact_pressed * 100 / s->compact_midturn);
    }
    /* M326y: the repeating failure. Eliding cannot help a turn once the old
     * large results are already elided (they fall under ELIDE_MIN_BYTES and are
     * never re-elided) and the rest is keep-recent protected -- so the trigger
     * fires every round for nothing. Measured decay per pass within a turn:
     * 1st ~10,300 tokens reclaimed, 2nd ~1,300, 3rd onward ~0. */
    if (s->compact_unrelieved > 0 && s->compact_pressed > 0) {
        jc_sb_append_fmt(out,
            "Compaction UNRELIEVED: %ld of %ld pressured pass(es) ended still "
            "above the high-water (%ld%%), so each re-triggered on the next "
            "round. Eliding cannot relieve those turns -- the lever is SMALLER "
            "tool output, not a lower threshold.\n",
            s->compact_unrelieved, s->compact_pressed,
            s->compact_unrelieved * 100 / s->compact_pressed);
    }
    if (s->compact_short > 0) {
        jc_sb_append_fmt(out,
            "Compaction SHORT: %ld mid-turn pass(es) could not reach the target "
            "-- requests went out over the configured contextLimit. The lever is "
            "LARGE tool results; a history of many small ones leaves nothing to "
            "elide.\n", s->compact_short);
    }
    if (s->compact_dup + s->compact_age > 0) {
        long tot = s->compact_dup + s->compact_age;
        jc_sb_append_fmt(out,
            "Compaction reclaim: %ld elision(s) -- dup=%ld (%ld%%, zero-loss) "
            "age=%ld (%ld%%, lossy)\n",
            tot, s->compact_dup, s->compact_dup * 100 / tot,
            s->compact_age, s->compact_age * 100 / tot);
    }

    /* M92: autonomy-envelope outcome breakdown -- shown only when at least one
     * turn ran under an envelope. Splits `budget_exhausted` into work-kept (the
     * normal terminal state of a budget-sized increment, per M80) vs rolled-back,
     * so a shelf of successful bank-and-commit runs doesn't read as failures. */
    if (s->out_completed + s->out_verify_failed + s->out_budget_kept +
        s->out_budget_reverted > 0) {
        jc_sb_append_fmt(out,
            "Outcomes: completed=%ld  budget_exhausted: kept=%ld reverted=%ld  "
            "verify_failed=%ld\n",
            s->out_completed, s->out_budget_kept, s->out_budget_reverted,
            s->out_verify_failed);
    }

    /* M584 (seams D6): the events that were emitted every run and read by
     * nothing. Each line prints ONLY when its count is non-zero, so a log that
     * never exercised a feature stays silent rather than growing a row of
     * zeros -- an absence stated as a zero reads as a measurement, and these
     * counts are mostly "the feature was off", not "the feature was clean".
     *
     * Ordered by what a reader should act on first: a check that did not run,
     * then a privilege decision, then cost. */
    if (s->hook_not_runnable > 0) {
        jc_sb_append_fmt(out,
            "Hooks NOT RUNNABLE: %ld hook invocation(s) exited 126/127 -- the "
            "command was not found or was not executable, so THE CHECK DID NOT "
            "RUN. A hook configured and missing is worse than no hook: the "
            "project believes it is guarded. Check the `shell`/`command` paths "
            "in the `hooks` config block.\n", s->hook_not_runnable);
    }
    if (s->hook_start_failed + s->hook_timeout > 0) {
        jc_sb_append_fmt(out,
            "Hooks failed: %ld never started, %ld killed at their timeout -- "
            "each is a hook that did not do its job, and neither aborts the "
            "run.\n", s->hook_start_failed, s->hook_timeout);
    }
    if (s->hook_nonzero > 0) {
        jc_sb_append_fmt(out,
            "Hooks advisory: %ld nonzero exit(s) that were neither a block "
            "(exit 2) nor a start failure -- the hook RAN and complained, and "
            "jichi ignored it by design.\n", s->hook_nonzero);
    }
    if (s->privileged_total > 0) {
        jc_sb_append_fmt(out,
            "Privileged commands: %ld proposed, %ld refused -- a sudo/doas "
            "escalation reached the fence this many times.\n",
            s->privileged_total, s->privileged_refused);
    }
    if (s->kinetic_total > 0) {
        jc_sb_append_fmt(out,
            "Kinetic actions: %ld proposed, %ld refused -- tools that move "
            "mass or energy (M163a).\n", s->kinetic_total, s->kinetic_refused);
    }
    if (s->prefix_churn > 0) {
        jc_sb_append_fmt(out,
            "Prompt-cache prefix CHURN: fired %ld time(s), longest streak %ld "
            "turn(s) -- the system prompt changed between calls, so the cached "
            "prefix is re-billed every time. On a backend where one call costs "
            "tens of thousands of input tokens this is the largest single cost "
            "lever; see /cache and the cached= token line.\n",
            s->prefix_churn, s->prefix_churn_max);
    }
    if (s->retrieve_calls > 0) {
        jc_sb_append_fmt(out,
            "Auto-context: %ld retrieval(s) attached %ld block(s), %ld token(s) "
            "-- averaging %ld tokens per retrieval. This is the number that "
            "answers whether retrieval earns its tokens, which is why it is "
            "off by default.\n",
            s->retrieve_calls, s->retrieve_blocks, s->retrieve_tokens,
            s->retrieve_tokens / (s->retrieve_calls > 0 ? s->retrieve_calls : 1));
    }
    if (s->args_truncated > 0) {
        jc_sb_append_fmt(out,
            "Arguments TRUNCATED: %ld tool call(s) hit the output-token limit "
            "mid-arguments -- distinct from a repair, because nothing can be "
            "recovered. The lever is smaller writes per call.\n",
            s->args_truncated);
    }
    if (s->history_checks > 0) {
        jc_sb_append_fmt(out,
            "History contract VIOLATED: %ld time(s) (M364) -- the message "
            "sequence sent to the provider broke its own invariant.\n",
            s->history_checks);
    }
    if (s->tool_nameless > 0) {
        jc_sb_append_fmt(out,
            "Tool calls with NO NAME: %ld, in %ld burst(s) -- the model emitted "
            "a tool call whose name never arrived. Before M585 jichi answered "
            "\"unknown tool ''\", which invites it to correct a name it never "
            "sent; the bursts are what that looks like from the outside. Each "
            "one costs a full round-trip.\n",
            s->tool_nameless, s->tool_nameless_bursts);
    }
    if (s->constraints + s->constraint_exempts > 0) {
        jc_sb_append_fmt(out,
            "Constraints: %ld inferred, %ld exempted.\n",
            s->constraints, s->constraint_exempts);
    }

    /* Per-workspace breakdown (M59): shown when not already filtered to one and
     * at least one event carried a "ws" stamp -- so a shared log makes clear
     * which projects it mixes. Old (pre-M56) logs are all "(unattributed)" and
     * are not worth a section. */
    if (s->ws_filter[0] == '\0') {
        int attributed = 0;
        for (i = 0; i < s->workspaces.len; i++) {
            const struct jc_telem_ws *w =
                (const struct jc_telem_ws *)jc_vec_at(
                    (struct jc_vec *)&s->workspaces, i);
            if (strcmp(w->ws, "(unattributed)") != 0) {
                attributed = 1;
                break;
            }
        }
        if (attributed) {
            jc_sb_append(out, "\nBy workspace:\n");
            for (i = 0; i < s->workspaces.len; i++) {
                const struct jc_telem_ws *w =
                    (const struct jc_telem_ws *)jc_vec_at(
                        (struct jc_vec *)&s->workspaces, i);
                jc_sb_append_fmt(out, "  %-40s events=%ld turns=%ld\n",
                                 w->ws, w->events, w->turns);
            }
        }
    }

    jc_sb_append(out, "\nModels:\n");
    if (s->models.len == 0) {
        jc_sb_append(out, "  (none)\n");
    }
    for (i = 0; i < s->models.len; i++) {
        const struct jc_telem_model *m =
            (const struct jc_telem_model *)jc_vec_at(
                (struct jc_vec *)&s->models, i);
        double mean = (m->lat_n > 0) ? m->lat_sum / (double)m->lat_n : 0.0;
        jc_sb_append_fmt(out,
            "  %-24s calls=%ld err=%ld  in=%.0f out=%.0f  cost=$%.4f  "
            "lat_ms mean=%.0f max=%.0f\n",
            m->name, m->calls, m->errors, m->in_tok, m->out_tok, m->cost,
            mean, m->lat_max);
        if (m->timeouts > 0) {
            jc_sb_append_fmt(out, "  %-24s   (of which %ld stalled/timed out)\n",
                             "", m->timeouts);
        }
        /* Prompt-cache line (M31a): only when caching was observed, so reports
         * for non-caching backends are unchanged. hit-rate = cached reads over
         * total input tokens (cached reads + uncached in_tok). */
        if (m->cache_read > 0.0 || m->cache_write > 0.0) {
            double total_in = m->in_tok + m->cache_read;
            double rate = (total_in > 0.0)
                          ? (m->cache_read * 100.0 / total_in) : 0.0;
            jc_sb_append_fmt(out,
                "  %-24s   cache read=%.0f write=%.0f  hit-rate=%.1f%%\n",
                "", m->cache_read, m->cache_write, rate);
        }
        /* Input attribution (M192): where the prompt goes. Only for logs whose
         * events carry the fields, so pre-M192 reports are byte-identical.
         * History is the only part that GROWS within a turn, so its share is
         * the one that decides whether compaction or read discipline is the
         * lever; system+tools are a fixed per-call toll. */
        if (m->attr_n > 0) {
            double n = (double)m->attr_n;
            double est = m->sys_tok + m->tools_tok + m->hist_tok;
            double pct_sys = (est > 0.0) ? m->sys_tok * 100.0 / est : 0.0;
            double pct_too = (est > 0.0) ? m->tools_tok * 100.0 / est : 0.0;
            double pct_his = (est > 0.0) ? m->hist_tok * 100.0 / est : 0.0;
            jc_sb_append_fmt(out,
                "  %-24s   input/call (est): sys=%.0f (%.0f%%) tools=%.0f (%.0f%%) "
                "history=%.0f (%.0f%%)\n",
                "", m->sys_tok / n, pct_sys, m->tools_tok / n, pct_too,
                m->hist_tok / n, pct_his);
            /* The estimate is the byte/4 heuristic every context decision keys
             * off; printing the observed real/estimated ratio says how far off
             * it runs for THIS model, which is what M77 calibrates against. */
            if (m->drift_n > 0) {
                jc_sb_append_fmt(out,
                    "  %-24s   est vs real: %.2fx (byte/4 under-estimates; "
                    "M77 calibration target)\n",
                    "", m->drift_sum / (double)m->drift_n);
            }
        }
    }

    jc_sb_append(out, "\nTools:\n");
    if (s->tools.len == 0) {
        jc_sb_append(out, "  (none)\n");
    }
    for (i = 0; i < s->tools.len; i++) {
        const struct jc_telem_tool *t =
            (const struct jc_telem_tool *)jc_vec_at(
                (struct jc_vec *)&s->tools, i);
        double mean = (t->calls > 0) ? t->dur_sum / (double)t->calls : 0.0;
        long pct = (t->calls > 0) ? (t->ok * 100 / t->calls) : 0;
        jc_sb_append_fmt(out,
            "  %-24s calls=%ld ok=%ld/%ld (%ld%%)  dur_ms mean=%.1f max=%.1f  "
            "out=%.0f B\n",
            t->name, t->calls, t->ok, t->calls, pct, mean, t->dur_max,
            t->out_bytes);
        /* M168: split out non-zero command exits. Printed only when some
         * occurred, so a tool that runs no command (and every pre-M168 log)
         * renders exactly as before. The derived rate is the one to judge the
         * TOOL by; the raw ok-rate above answers "did the command succeed",
         * which for a gate in a fix-forward loop is a different question. */
        if (t->cmd_fail > 0) {
            long tool_ok = t->ok + t->cmd_fail;
            jc_sb_append_fmt(out,
                "  %-24s   of which %ld were red commands (non-zero exit), not "
                "tool failures -> tool-level ok=%ld/%ld (%ld%%)\n",
                "", t->cmd_fail, tool_ok, t->calls,
                (t->calls > 0) ? (tool_ok * 100 / t->calls) : 0);
        }
    }

    /* M591: which spellings arrived, and how often. Printed only when an alias
     * was actually used, so a log where every call named its tool renders
     * exactly as before. The rows above already count these calls under the
     * tool that ran; this says what the model asked for -- which is the half a
     * maintainer can act on, by renaming the tool, by teaching the name, or by
     * promoting the alias. */
    if (s->aliases.len > 0) {
        jc_sb_append(out, "\nNames the model reached for (resolved by alias):\n");
        for (i = 0; i < s->aliases.len; i++) {
            const struct jc_telem_alias *a =
                (const struct jc_telem_alias *)jc_vec_at(
                    (struct jc_vec *)&s->aliases, i);
            jc_sb_append_fmt(out, "  %-24s -> %-20s calls=%ld\n",
                             a->requested, a->canonical, a->calls);
        }
    }

    if (s->test_edits > 0) {
        /* M417: loud on purpose -- a green gate after an assertion edit is not
         * evidence, and this line is how an offline reader learns it happened. */
        jc_sb_append_fmt(out, "Goalposts: %ld test-assertion edit(s) during "
                         "autonomous runs -- verify green on those runs is not "
                         "evidence; review the TAINTED attempts / kept "
                         "worktrees\n", s->test_edits);
    }

    /* Self-correction (M167): the nudge (M147) and argument-repair (M148)
     * counters. Printed only when something fired, so a clean log is unchanged.
     * The recovery share is the useful reading: a nudge that fires often and
     * recovers rarely means the model is probably not emitting native tool calls
     * at all -- and before blaming the model, check that jichi's request is
     * well-formed (docs/LOCAL_MODELS.md, "When the model calls no tool at all"). */
    if (s->nudge_fired > 0 || s->nudge_recovered > 0 || s->repair_total > 0) {
        jc_sb_append(out, "\nSelf-correction:\n");
        if (s->nudge_fired > 0 || s->nudge_recovered > 0) {
            long pct = (s->nudge_fired > 0)
                       ? (s->nudge_recovered * 100 / s->nudge_fired) : 0;
            jc_sb_append_fmt(out,
                "  prose-call nudge         fired=%ld recovered=%ld (%ld%%)\n",
                s->nudge_fired, s->nudge_recovered, pct);
        }
        if (s->repair_total > 0) {
            jc_sb_append_fmt(out,
                "  argument repair          ok=%ld/%ld (%ld%%)\n",
                s->repair_ok, s->repair_total,
                s->repair_ok * 100 / s->repair_total);
        }
    }

    /* Per-session timeline (M82): one line per run/phase, in first-seen order,
     * so a multi-run log shows where the tokens/cost went, the per-call input
     * ramp (peak_in -- dominant when the backend has no prompt cache), and which
     * runs leaned on mid-turn compaction. */
    if (s->sessions.len > 0) {
        jc_sb_append(out, "\nSessions (timeline):\n");
        for (i = 0; i < s->sessions.len; i++) {
            const struct jc_telem_session *se =
                (const struct jc_telem_session *)jc_vec_at(
                    (struct jc_vec *)&s->sessions, i);
            char sid8[9];
            long tpct = (se->tools > 0) ? (se->tool_ok * 100 / se->tools) : 0;
            double cache_total = se->cache_read + se->uncached;
            jc_snprintf(sid8, sizeof(sid8), "%s", se->sid);
            jc_sb_append_fmt(out,
                "  %-8s calls=%ld  in=%.2fM out=%.1fk cost=$%.4f  "
                "peak_in=%.0fk  tools=%ld/%ld (%ld%%)  compact=%ld",
                sid8, se->calls, se->in_tok / 1e6, se->out_tok / 1e3, se->cost,
                se->peak_in / 1e3, se->tool_ok, se->tools, tpct, se->compacts);
            /* M592: the per-session hit-rate, printed only when this session had
             * enough input to judge. The aggregate across a log is a trap when a
             * server-side setting changed mid-log -- one 2026-08-25 drive read
             * 8.0% overall while its five sessions read 0,0,0,0 and 92.4%. */
            if (cache_total >= 2000.0) {
                jc_sb_append_fmt(out, "  cache=%ld%%",
                    (long)((se->cache_read * 100.0) / cache_total));
            }
            jc_sb_append(out, "\n");
        }
    }
}

/* M599: see jc_telemetry.h. */
void jc_telemetry_default_path(const char *home, const char *workspace,
                               char *buf, jc_size cap)
{
    char base[48];
    const char *ws = (workspace != NULL && workspace[0] != '\0') ? workspace : ".";
    const char *p;
    const char *last;
    jc_size o = 0;

    /* The last non-empty path component. */
    last = ws;
    for (p = ws; *p != '\0'; p++) {
        if (*p == '/' && p[1] != '\0') {
            last = p + 1;
        }
    }
    for (p = last; *p != '\0' && *p != '/' && o + 1 < sizeof(base) && o < 40;
         p++) {
        unsigned char c = (unsigned char)*p;
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        base[o++] = ok ? (char)c : '_';
    }
    if (o == 0) {
        base[o++] = 'w';
        base[o++] = 's';
    }
    base[o] = '\0';
    jc_snprintf(buf, cap, "%s/.jichi.d/telemetry/%s-%lu.jsonl",
                (home != NULL && home[0] != '\0') ? home : ".", base,
                jc_workspace_key(ws));
}
