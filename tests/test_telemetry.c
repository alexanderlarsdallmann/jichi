/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_telemetry.c - offline telemetry-log summary (jc_telemetry). */

#include "jc_test.h"
#include "jc_telemetry.h"
#include "jc_str.h"
#include "jc_snprintf.h"

#include <string.h>

/* Split into two literals (each within the C89 509-char limit) and fed in
 * sequence; the summary accumulates across both feeds. */
static const char *LOG1 =
    "{\"event\":\"turn_start\",\"model\":\"M1\"}\n"
    "{\"event\":\"model_call\",\"model\":\"M1\",\"ok\":true,\"in_tok\":100,"
    "\"out_tok\":10,\"cache_read_in\":300,\"cache_write_in\":20,"
    "\"cost_usd\":0.5,\"latency_ms\":200}\n"
    "{\"event\":\"tool_call\",\"name\":\"read_file\",\"ok\":true,"
    "\"duration_ms\":5,\"output_bytes\":1000}\n"
    "{\"event\":\"model_retry\"}\n"
    "not valid json -- skipped\n";
static const char *LOG2 =
    "{\"event\":\"model_call\",\"model\":\"M1\",\"ok\":false,"
    "\"result\":\"timeout\",\"latency_ms\":50}\n"
    "{\"event\":\"route\"}\n"
    "{\"event\":\"compact\"}\n"
    "{\"event\":\"tool_call\",\"name\":\"read_file\",\"ok\":false,"
    "\"duration_ms\":3,\"output_bytes\":50}\n"
    "{\"event\":\"turn_end\"}\n";

/* M326x: the three shapes a `compact` event can take, for the pressure/short
 * counting rule. Until M326x the emitter wrote `short` as `!reached` without
 * consulting `pressed`, so an eager zero-loss dedup -- which has no target to
 * miss -- logged short:true and the summary rendered "requests went out over
 * the configured contextLimit" about requests that had not. Every one of the 19
 * such events in a measured 36,925-event workload was a false positive.
 *
 * A long-lived log spans that fix, so the reader must handle both shapes: trust
 * `pressed` when present, and otherwise infer it the way the pass itself does
 * (the lossy age/args trims run ONLY past the high-water mark). */
/* C89 caps a string literal at 509 chars AFTER concatenation, and this one
 * reached 517. It compiled for months because tests/test_telemetry.o was
 * never rebuilt under WERROR=1 -- see M332. Split in two and joined at the
 * call site, which keeps the fixture byte-identical. */
static const char *const COMPACT_SHAPES_A =
    /* new shape, pressed and short -- the real thing, counted */
    "{\"v\":1,\"event\":\"compact\",\"phase\":\"midturn\",\"pressed\":true,"
    "\"short\":true,\"dup\":0,\"age\":3,\"args\":0}\n"
    /* new shape, pressed but reached -- counted as pressure, not as short */
    "{\"v\":1,\"event\":\"compact\",\"phase\":\"midturn\",\"pressed\":true,"
    "\"short\":false,\"dup\":1,\"age\":2,\"args\":0}\n"
    /* new shape, NOT pressed: the eager dedup. Neither pressure nor short,
     * whatever `short` says -- this is the case that was miscounted. */
    "{\"v\":1,\"event\":\"compact\",\"phase\":\"midturn\",\"pressed\":false,"
    "\"short\":true,\"dup\":1,\"age\":0,\"args\":0}\n"
    /* OLD shape (no `pressed`) with lossy work => inferred pressed, short kept */
    "{\"v\":1,\"event\":\"compact\",\"phase\":\"midturn\","
    "\"short\":true,\"dup\":0,\"age\":4,\"args\":0}\n";

static const char *const COMPACT_SHAPES_B =
    /* OLD shape, dedup only => inferred NOT pressed, short ignored */
    "{\"v\":1,\"event\":\"compact\",\"phase\":\"midturn\","
    "\"short\":true,\"dup\":2,\"age\":0,\"args\":0}\n"
    /* a between-turn compaction: never mid-turn, never pressure */
    "{\"v\":1,\"event\":\"compact\",\"phase\":\"between\",\"dup\":0,\"age\":9}\n";

/* M599: the per-workspace default log. One derivation for the writer and every
 * reader; the name is the workspace's basename (sanitised) plus the same key the
 * checkpoint store and the lease use, so a run writes where `learn analyze`
 * reads. */
static void test_telemetry_default_path(void)
{
    char a[512];
    char b[512];
    char c[512];

    jc_telemetry_default_path("/home/u", "/home/u/dev/jichi", a, sizeof(a));
    JC_CHECK(strncmp(a, "/home/u/.jichi.d/telemetry/jichi-", 33) == 0);
    JC_CHECK(strstr(a, ".jsonl") != NULL);
    /* Deterministic: the same workspace names the same file. */
    jc_telemetry_default_path("/home/u", "/home/u/dev/jichi", b, sizeof(b));
    JC_CHECK(strcmp(a, b) == 0);
    /* Two workspaces with one basename get two files (the key differs). */
    jc_telemetry_default_path("/home/u", "/srv/other/jichi", c, sizeof(c));
    JC_CHECK(strncmp(c, "/home/u/.jichi.d/telemetry/jichi-", 33) == 0);
    JC_CHECK(strcmp(a, c) != 0);
    /* A trailing slash does not change the basename; odd bytes are sanitised. */
    jc_telemetry_default_path("/h", "/x/My Project (v2)/", b, sizeof(b));
    JC_CHECK(strstr(b, "/telemetry/My_Project__v2_-") != NULL);
    /* NULLs read as ".": no crash, a well-formed path. */
    jc_telemetry_default_path(NULL, NULL, b, sizeof(b));
    JC_CHECK(strncmp(b, "./.jichi.d/telemetry/", 21) == 0);
}

void test_telemetry(void)
{
    struct jc_telemetry_summary s;
    struct jc_telem_model *m;
    struct jc_telem_tool *t;
    struct jc_sb out;

    jc_telemetry_summarize(LOG1, &s);
    jc_telemetry_feed(&s, LOG2);

    /* Top-level counters (the malformed line is skipped). */
    JC_CHECK(s.events == 9);
    JC_CHECK(s.turns == 1);
    JC_CHECK(s.retries == 1);
    JC_CHECK(s.routes == 1);
    JC_CHECK(s.compacts == 1);
    JC_CHECK(s.errors == 1); /* one failed model_call */
    JC_CHECK(s.timeouts == 1); /* ...which was a stall (result=="timeout") */

    /* Per-model aggregate. */
    JC_CHECK(s.models.len == 1);
    m = (struct jc_telem_model *)jc_vec_at(&s.models, 0);
    JC_CHECK_STR(m->name, "M1");
    JC_CHECK(m->calls == 2 && m->errors == 1 && m->timeouts == 1);
    JC_CHECK(m->in_tok == 100.0 && m->out_tok == 10.0);
    JC_CHECK(m->cache_read == 300.0 && m->cache_write == 20.0);
    JC_CHECK(m->cost == 0.5);
    JC_CHECK(m->lat_n == 2 && m->lat_sum == 250.0 && m->lat_max == 200.0);

    /* Per-tool aggregate. */
    JC_CHECK(s.tools.len == 1);
    t = (struct jc_telem_tool *)jc_vec_at(&s.tools, 0);
    JC_CHECK_STR(t->name, "read_file");
    JC_CHECK(t->calls == 2 && t->ok == 1);
    JC_CHECK(t->dur_max == 5.0 && t->out_bytes == 1050.0);

    /* Render mentions the model and tool. */
    jc_sb_init(&out);
    jc_telemetry_render(&s, &out);
    JC_CHECK(out.data != NULL && strstr(out.data, "M1") != NULL);
    JC_CHECK(out.data != NULL && strstr(out.data, "read_file") != NULL);
    /* The cache line appears (hit-rate = 300 / (100 + 300) = 75.0%). */
    JC_CHECK(out.data != NULL && strstr(out.data, "hit-rate=75.0%") != NULL);
    {
        struct jc_telemetry_summary cs;
        {
            char shapes[1024];
            jc_snprintf(shapes, sizeof shapes, "%s%s",
                        COMPACT_SHAPES_A, COMPACT_SHAPES_B);
            jc_telemetry_summarize(shapes, &cs);
        }
        JC_CHECK(cs.compacts == 6);
        JC_CHECK(cs.compact_midturn == 5);   /* the "between" one is not */
        /* pressed: 2 explicit + 1 inferred from age>0 on an old-shape event */
        JC_CHECK(cs.compact_pressed == 3);
        /* short: only the pressed ones. The two dedup-only events say
         * short:true and MUST NOT be counted -- that was the defect. */
        JC_CHECK(cs.compact_short == 2);
        jc_telemetry_summary_free(&cs);
    }
    jc_sb_free(&out);

    jc_telemetry_summary_free(&s);

    /* Empty / NULL input is safe. */
    jc_telemetry_summarize("", &s);
    JC_CHECK(s.events == 0 && s.models.len == 0);
    jc_telemetry_summary_free(&s);

    /* M56: the workspace filter counts only events stamped with a matching
     * "ws"; events without one (or with a different ws) are skipped. */
    {
        static const char *const MIX =
            "{\"event\":\"turn_start\",\"ws\":\"/proj/a\"}\n"
            "{\"event\":\"turn_start\",\"ws\":\"/proj/b\"}\n"
            "{\"event\":\"turn_start\"}\n"
            "{\"event\":\"turn_start\",\"ws\":\"/proj/a\"}\n";
        struct jc_telemetry_summary fs;
        jc_telemetry_summary_init(&fs);
        jc_snprintf(fs.ws_filter, sizeof(fs.ws_filter), "%s", "/proj/a");
        jc_telemetry_feed(&fs, MIX);
        JC_CHECK(fs.turns == 2);   /* only the two /proj/a turns */
        JC_CHECK(fs.events == 2);  /* untagged + /proj/b skipped */
        jc_telemetry_summary_free(&fs);

        /* No filter => all four events counted. */
        jc_telemetry_summarize(MIX, &fs);
        JC_CHECK(fs.turns == 4 && fs.events == 4);
        jc_telemetry_summary_free(&fs);

        /* M59: per-workspace breakdown -- 3 distinct workspaces (a, b,
         * unattributed), with the right per-ws turn/event counts, and the
         * render shows a "By workspace" section listing the attributed roots. */
        jc_telemetry_summarize(MIX, &fs);
        JC_CHECK(fs.workspaces.len == 3);
        {
            jc_size i;
            long a_turns = -1;
            for (i = 0; i < fs.workspaces.len; i++) {
                struct jc_telem_ws *w =
                    (struct jc_telem_ws *)jc_vec_at(&fs.workspaces, i);
                if (strcmp(w->ws, "/proj/a") == 0) {
                    a_turns = w->turns;
                    JC_CHECK(w->events == 2);
                }
            }
            JC_CHECK(a_turns == 2);
        }
        {
            struct jc_sb r;
            jc_sb_init(&r);
            jc_telemetry_render(&fs, &r);
            JC_CHECK(r.data != NULL &&
                     strstr(r.data, "By workspace:") != NULL &&
                     strstr(r.data, "/proj/a") != NULL &&
                     strstr(r.data, "/proj/b") != NULL);
            jc_sb_free(&r);
        }
        jc_telemetry_summary_free(&fs);
    }

    /* M82: per-session timeline -- aggregate per `sid`, in first-seen order,
     * with the per-call input ramp (peak_in). */
    {
        static const char *const SESS =
            "{\"event\":\"model_call\",\"model\":\"M\",\"ok\":true,"
            "\"in_tok\":100,\"cache_read_in\":50,\"out_tok\":10,"
            "\"cost_usd\":0.1,\"sid\":\"sessAAAA\"}\n"
            "{\"event\":\"tool_call\",\"name\":\"read_file\",\"ok\":true,"
            "\"sid\":\"sessAAAA\"}\n"
            "{\"event\":\"compact\",\"sid\":\"sessAAAA\"}\n"
            "{\"event\":\"model_call\",\"model\":\"M\",\"ok\":true,"
            "\"in_tok\":500,\"out_tok\":20,\"cost_usd\":0.2,"
            "\"sid\":\"sessBBBB\"}\n"
            "{\"event\":\"tool_call\",\"name\":\"read_file\",\"ok\":false,"
            "\"sid\":\"sessBBBB\"}\n";
        struct jc_telemetry_summary ss;
        jc_size i;
        struct jc_telem_session *a = NULL, *b = NULL;
        jc_telemetry_summarize(SESS, &ss);
        JC_CHECK(ss.sessions.len == 2);
        for (i = 0; i < ss.sessions.len; i++) {
            struct jc_telem_session *e =
                (struct jc_telem_session *)jc_vec_at(&ss.sessions, i);
            if (strcmp(e->sid, "sessAAAA") == 0) a = e;
            if (strcmp(e->sid, "sessBBBB") == 0) b = e;
        }
        JC_CHECK(a != NULL && b != NULL);
        if (a != NULL) {
            JC_CHECK(a->order == 0);          /* first seen */
            JC_CHECK(a->calls == 1);
            JC_CHECK(a->in_tok == 150.0);     /* 100 in + 50 cache */
            JC_CHECK(a->peak_in == 150.0);
            JC_CHECK(a->tools == 1 && a->tool_ok == 1);
            JC_CHECK(a->compacts == 1);
        }
        if (b != NULL) {
            JC_CHECK(b->order == 1);
            JC_CHECK(b->in_tok == 500.0 && b->peak_in == 500.0);
            JC_CHECK(b->tools == 1 && b->tool_ok == 0);
            JC_CHECK(b->compacts == 0);
        }
        {
            struct jc_sb r;
            jc_sb_init(&r);
            jc_telemetry_render(&ss, &r);
            JC_CHECK(r.data != NULL &&
                     strstr(r.data, "Sessions (timeline):") != NULL &&
                     strstr(r.data, "sessAAAA") != NULL);
            jc_sb_free(&r);
        }
        jc_telemetry_summary_free(&ss);
    }

    /* M92: autonomy-envelope outcome breakdown from turn_end events. A
     * budget_exhausted turn splits kept vs reverted by `rolled_back`, so a
     * banked-green run isn't counted (or rendered) as a failure. */
    {
        static const char *const OUT =
            "{\"event\":\"turn_end\",\"outcome\":\"budget_exhausted\","
            "\"rolled_back\":false}\n"
            "{\"event\":\"turn_end\",\"outcome\":\"budget_exhausted\","
            "\"rolled_back\":false}\n"
            "{\"event\":\"turn_end\",\"outcome\":\"budget_exhausted\","
            "\"rolled_back\":true}\n"
            "{\"event\":\"turn_end\",\"outcome\":\"ok\"}\n"
            "{\"event\":\"turn_end\",\"outcome\":\"verify_failed\"}\n"
            "{\"event\":\"turn_end\"}\n"; /* no envelope -> uncounted */
        struct jc_telemetry_summary os;
        struct jc_sb r;
        jc_telemetry_summarize(OUT, &os);
        JC_CHECK(os.out_budget_kept == 2);
        JC_CHECK(os.out_budget_reverted == 1);
        JC_CHECK(os.out_completed == 1);
        JC_CHECK(os.out_verify_failed == 1);
        jc_sb_init(&r);
        jc_telemetry_render(&os, &r);
        JC_CHECK(r.data != NULL &&
                 strstr(r.data, "Outcomes:") != NULL &&
                 strstr(r.data, "kept=2 reverted=1") != NULL);
        jc_sb_free(&r);
        jc_telemetry_summary_free(&os);

        /* No envelope outcomes -> the Outcomes line is omitted entirely. */
        jc_telemetry_summarize("{\"event\":\"turn_start\"}\n", &os);
        jc_sb_init(&r);
        jc_telemetry_render(&os, &r);
        JC_CHECK(r.data != NULL && strstr(r.data, "Outcomes:") == NULL);
        jc_sb_free(&r);
        jc_telemetry_summary_free(&os);
    }

    /* M167: nudge (M147) + args_repair (M148) counters. These events were
     * emitted from the start but had no reader, so two rows of the small-model
     * measurement plan could not be read from a log. */
    {
        struct jc_telemetry_summary ns;
        struct jc_sb r;
        const char *log =
            "{\"event\":\"nudge\",\"phase\":\"fired\",\"tool\":\"edit_file\"}\n"
            "{\"event\":\"nudge\",\"phase\":\"fired\",\"tool\":\"read_file\"}\n"
            "{\"event\":\"nudge\",\"phase\":\"recovered\"}\n"
            "{\"event\":\"args_repair\",\"tool\":\"edit_file\",\"ok\":true}\n"
            "{\"event\":\"args_repair\",\"tool\":\"edit_file\",\"ok\":true}\n"
            "{\"event\":\"args_repair\",\"tool\":\"write_file\",\"ok\":false}\n";
        jc_telemetry_summarize(log, &ns);
        JC_CHECK(ns.nudge_fired == 2);
        JC_CHECK(ns.nudge_recovered == 1);
        JC_CHECK(ns.repair_total == 3);
        JC_CHECK(ns.repair_ok == 2);
        jc_sb_init(&r);
        jc_telemetry_render(&ns, &r);
        /* 1 of 2 fires recovered = 50%; 2 of 3 repairs ok = 66%. */
        JC_CHECK(r.data != NULL &&
                 strstr(r.data, "Self-correction:") != NULL &&
                 strstr(r.data, "fired=2 recovered=1 (50%)") != NULL &&
                 strstr(r.data, "ok=2/3 (66%)") != NULL);
        jc_sb_free(&r);
        jc_telemetry_summary_free(&ns);
    }

    /* M417: the moved-goalpost event. M88's warning fired ten times on a real
     * run and no offline reader could see it -- the learn loop's analyze pass
     * mined the same log and never mentioned the richest lesson in it. */
    {
        struct jc_telemetry_summary ns;
        struct jc_sb r;
        const char *log =
            "{\"event\":\"test_edit\",\"tool\":\"edit_file\","
            "\"path\":\"tests/test_gate.sh\"}\n"
            "{\"event\":\"test_edit\",\"tool\":\"apply_patch\","
            "\"path\":\"src/x_test.zig\"}\n";
        jc_telemetry_summarize(log, &ns);
        JC_CHECK(ns.test_edits == 2);
        jc_sb_init(&r);
        jc_telemetry_render(&ns, &r);
        JC_CHECK(r.data != NULL &&
                 strstr(r.data, "test-assertion edit") != NULL);
        jc_sb_free(&r);
        jc_telemetry_summary_free(&ns);

        /* An unknown phase is ignored rather than miscounted. */
        jc_telemetry_summarize("{\"event\":\"nudge\",\"phase\":\"weird\"}\n", &ns);
        JC_CHECK(ns.nudge_fired == 0 && ns.nudge_recovered == 0);
        jc_telemetry_summary_free(&ns);

        /* M168: a red command (non-zero `exit`) is not a tool failure. Without
         * this split, a fix-forward loop that deliberately runs a red gate reads
         * as an unreliable tool -- real dogfood data showed run_tests at 73%
         * "ok" whose tool-level success rate was 97%. */
        {
            const char *cmdlog =
                "{\"event\":\"tool_call\",\"name\":\"run_tests\",\"ok\":false,"
                "\"exit\":1}\n"
                "{\"event\":\"tool_call\",\"name\":\"run_tests\",\"ok\":false,"
                "\"exit\":1}\n"
                "{\"event\":\"tool_call\",\"name\":\"run_tests\",\"ok\":true,"
                "\"exit\":0}\n"
                /* a genuine tool failure: not-found, so no exit status at all */
                "{\"event\":\"tool_call\",\"name\":\"run_tests\",\"ok\":false}\n"
                /* a non-command tool never carries `exit` */
                "{\"event\":\"tool_call\",\"name\":\"edit_file\",\"ok\":false}\n";
            const struct jc_telem_tool *tt;
            jc_size k;
            jc_telemetry_summarize(cmdlog, &ns);
            for (k = 0; k < ns.tools.len; k++) {
                tt = (const struct jc_telem_tool *)jc_vec_at(&ns.tools, k);
                if (strcmp(tt->name, "run_tests") == 0) {
                    JC_CHECK(tt->calls == 4);
                    JC_CHECK(tt->ok == 1);
                    JC_CHECK(tt->cmd_fail == 2);   /* the two red gates */
                } else if (strcmp(tt->name, "edit_file") == 0) {
                    /* no `exit` field -> not attributable to a command */
                    JC_CHECK(tt->cmd_fail == 0);
                }
            }
            jc_sb_init(&r);
            jc_telemetry_render(&ns, &r);
            /* raw ok-rate stays 1/4 (25%); tool-level becomes 3/4 (75%) */
            JC_CHECK(r.data != NULL &&
                     strstr(r.data, "ok=1/4 (25%)") != NULL &&
                     strstr(r.data, "2 were red commands") != NULL &&
                     strstr(r.data, "tool-level ok=3/4 (75%)") != NULL);
            /* edit_file has no red commands, so it gets no extra line */
            JC_CHECK(r.data != NULL &&
                     strstr(r.data, "edit_file") != NULL);
            jc_sb_free(&r);
            jc_telemetry_summary_free(&ns);
        }

        /* A pre-M168 log carries no `exit` field at all, so nothing is
         * reclassified and the report is byte-identical to before. */
        {
            const struct jc_telem_tool *tt;
            jc_telemetry_summarize(
                "{\"event\":\"tool_call\",\"name\":\"run_tests\",\"ok\":false}\n",
                &ns);
            tt = (const struct jc_telem_tool *)jc_vec_at(&ns.tools, 0);
            JC_CHECK(tt->cmd_fail == 0);
            jc_sb_init(&r);
            jc_telemetry_render(&ns, &r);
            JC_CHECK(r.data != NULL && strstr(r.data, "red commands") == NULL);
            jc_sb_free(&r);
            jc_telemetry_summary_free(&ns);
        }

        /* A log with neither event omits the whole block (clean log unchanged). */
        jc_telemetry_summarize("{\"event\":\"turn_start\"}\n", &ns);
        jc_sb_init(&r);
        jc_telemetry_render(&ns, &r);
        JC_CHECK(r.data != NULL && strstr(r.data, "Self-correction:") == NULL);
        jc_sb_free(&r);
        jc_telemetry_summary_free(&ns);
    }

    /* M192: input attribution + compaction reclaim. */
    {
        struct jc_telemetry_summary ns;
        struct jc_sb r;

        /* Two calls carrying the attribution fields: sums, per-call means, and
         * the observed real/estimated ratio (here in=8000 vs est=4000 => 2.00x,
         * the byte/4 optimism M77 calibrates for). */
        jc_telemetry_summarize(
            "{\"event\":\"model_call\",\"model\":\"m1\",\"ok\":true,"
            "\"in_tok\":8000,\"sys_tok\":1000,\"tools_tok\":1000,"
            "\"hist_tok\":2000}\n"
            "{\"event\":\"model_call\",\"model\":\"m1\",\"ok\":true,"
            "\"in_tok\":8000,\"sys_tok\":1000,\"tools_tok\":1000,"
            "\"hist_tok\":2000}\n"
            "{\"event\":\"compact\",\"phase\":\"midturn\",\"elided\":5,"
            "\"dup\":4,\"age\":1}\n",
            &ns);
        {
            const struct jc_telem_model *mm =
                (const struct jc_telem_model *)jc_vec_at(&ns.models, 0);
            JC_CHECK(mm->attr_n == 2);
            JC_CHECK(mm->sys_tok == 2000.0);
            JC_CHECK(mm->tools_tok == 2000.0);
            JC_CHECK(mm->hist_tok == 4000.0);
            JC_CHECK(mm->drift_n == 2);
        }
        JC_CHECK(ns.compact_dup == 4);
        JC_CHECK(ns.compact_age == 1);
        jc_sb_init(&r);
        jc_telemetry_render(&ns, &r);
        JC_CHECK(r.data != NULL);
        /* per-call means: sys=1000 (25%), tools=1000 (25%), history=2000 (50%) */
        JC_CHECK(strstr(r.data, "input/call (est)") != NULL);
        JC_CHECK(strstr(r.data, "history=2000 (50%)") != NULL);
        JC_CHECK(strstr(r.data, "est vs real: 2.00x") != NULL);
        /* dup + age == elided, and the zero-loss share is called out. */
        JC_CHECK(strstr(r.data, "dup=4 (80%, zero-loss)") != NULL);
        JC_CHECK(strstr(r.data, "age=1 (20%, lossy)") != NULL);
        jc_sb_free(&r);
        jc_telemetry_summary_free(&ns);

        /* A pre-M192 log carries none of those fields, so both lines are absent
         * and the report is exactly what it was before this milestone. */
        jc_telemetry_summarize(
            "{\"event\":\"model_call\",\"model\":\"m1\",\"ok\":true,"
            "\"in_tok\":8000}\n"
            "{\"event\":\"compact\",\"phase\":\"midturn\",\"elided\":5}\n",
            &ns);
        {
            const struct jc_telem_model *mm =
                (const struct jc_telem_model *)jc_vec_at(&ns.models, 0);
            JC_CHECK(mm->attr_n == 0);
            JC_CHECK(mm->drift_n == 0);
        }
        JC_CHECK(ns.compacts == 1);
        JC_CHECK(ns.compact_dup == 0 && ns.compact_age == 0);
        jc_sb_init(&r);
        jc_telemetry_render(&ns, &r);
        JC_CHECK(r.data != NULL);
        JC_CHECK(strstr(r.data, "input/call") == NULL);
        JC_CHECK(strstr(r.data, "est vs real") == NULL);
        JC_CHECK(strstr(r.data, "Compaction reclaim") == NULL);
        jc_sb_free(&r);
        jc_telemetry_summary_free(&ns);
    }

    /* --- M286: the --since window. A log outlives the code it describes: one
     * 34 MB dogfood log spanned six weeks and crossed three fixes, so its single
     * aggregate ok-rate mixed pre- and post-fix events and read as a live defect
     * that had been fixed weeks earlier. --- */
    {
        struct jc_telemetry_summary ws;
        struct jc_sb r;
        const char *log =
            "{\"event\":\"tool_call\",\"ts\":1000,\"name\":\"old\",\"ok\":false}\n"
            "{\"event\":\"tool_call\",\"ts\":5000,\"name\":\"new\",\"ok\":true}\n"
            "{\"event\":\"tool_call\",\"name\":\"undatable\",\"ok\":true}\n";

        /* No window: everything counts, including the event with no `ts`. */
        jc_telemetry_summarize(log, &ws);
        JC_CHECK(ws.events == 3);
        JC_CHECK(ws.tools.len == 3);
        jc_telemetry_summary_free(&ws);

        /* Windowed: only the event at or after the cutoff. An event that
         * carries no timestamp cannot be placed in time, so it is excluded --
         * a window that admitted undatable events would defeat its purpose. */
        jc_telemetry_summary_init(&ws);
        ws.min_ts = 4000.0;
        jc_telemetry_feed(&ws, log);
        JC_CHECK(ws.events == 1);
        JC_CHECK(ws.tools.len == 1);
        t = (struct jc_telem_tool *)jc_vec_at(&ws.tools, 0);
        JC_CHECK_STR(t->name, "new");
        /* A partial summary must SAY it is partial, or the number gets quoted
         * as though it covered everything. */
        jc_sb_init(&r);
        jc_telemetry_render(&ws, &r);
        JC_CHECK(r.data != NULL);
        JC_CHECK(strstr(r.data, "window:") != NULL);
        jc_sb_free(&r);
        jc_telemetry_summary_free(&ws);

        /* The boundary is inclusive, and an unset window renders no notice. */
        jc_telemetry_summary_init(&ws);
        ws.min_ts = 5000.0;
        jc_telemetry_feed(&ws, log);
        JC_CHECK(ws.events == 1);
        jc_telemetry_summary_free(&ws);

        jc_telemetry_summarize(log, &ws);
        jc_sb_init(&r);
        jc_telemetry_render(&ws, &r);
        JC_CHECK(r.data != NULL);
        JC_CHECK(strstr(r.data, "window:") == NULL);
        jc_sb_free(&r);
        jc_telemetry_summary_free(&ws);
    }

    /* --- M290: which BUILD produced these numbers. A log outlives the code it
     * describes, and reading one era's rates as current is a mistake this project
     * made twice in one session (run_tests 75%, format_file 0/3 -- both reported
     * as live defects, both fixed weeks earlier). Nothing in the log said so, and
     * `v` -- the event SCHEMA -- looks enough like a version that nobody asks. --- */
    {
        struct jc_telemetry_summary vs;
        struct jc_sb r;
        const char *one =
            "{\"v\":1,\"jichi\":\"0.9.0\",\"event\":\"turn_start\"}\n"
            "{\"v\":1,\"jichi\":\"0.9.0\",\"event\":\"turn_end\"}\n";
        const char *mixed =
            "{\"v\":1,\"jichi\":\"0.8.4\",\"event\":\"turn_start\"}\n"
            "{\"v\":1,\"jichi\":\"0.9.0\",\"event\":\"turn_start\"}\n"
            "{\"v\":1,\"jichi\":\"0.8.4\",\"event\":\"turn_end\"}\n";

        /* One build: tallied, and stated as a plain fact. */
        jc_telemetry_summarize(one, &vs);
        JC_CHECK(vs.versions.len == 1);
        /* Guarded: indexing an empty vec would crash precisely when the tally is
         * broken -- the failure this test exists to report (ANECDOTES #29). */
        if (vs.versions.len == 1) {
            const struct jc_telem_version *v0 = (const struct jc_telem_version *)
                jc_vec_at(&vs.versions, 0);
            JC_CHECK_STR(v0->ver, "0.9.0");
            JC_CHECK(v0->events == 2);
        }
        jc_sb_init(&r);
        jc_telemetry_render(&vs, &r);
        JC_CHECK(r.data != NULL && strstr(r.data, "jichi: 0.9.0") != NULL);
        /* One build is NOT a warning. */
        JC_CHECK(r.data != NULL && strstr(r.data, "BUILDS") == NULL);
        jc_sb_free(&r);
        jc_telemetry_summary_free(&vs);

        /* Two builds: counted per build, in first-seen order, and the report says
         * outright that every rate below mixes them. */
        jc_telemetry_summarize(mixed, &vs);
        JC_CHECK(vs.versions.len == 2);
        if (vs.versions.len == 2) {
            const struct jc_telem_version *v0 = (const struct jc_telem_version *)
                jc_vec_at(&vs.versions, 0);
            const struct jc_telem_version *v1 = (const struct jc_telem_version *)
                jc_vec_at(&vs.versions, 1);
            JC_CHECK_STR(v0->ver, "0.8.4");   /* first seen */
            JC_CHECK(v0->events == 2);
            JC_CHECK_STR(v1->ver, "0.9.0");
            JC_CHECK(v1->events == 1);
        }
        jc_sb_init(&r);
        jc_telemetry_render(&vs, &r);
        JC_CHECK(r.data != NULL);
        JC_CHECK(strstr(r.data, "2 BUILDS") != NULL);
        JC_CHECK(strstr(r.data, "0.8.4") != NULL);
        JC_CHECK(strstr(r.data, "0.9.0") != NULL);
        JC_CHECK(strstr(r.data, "--since") != NULL); /* names the way out */
        jc_sb_free(&r);
        jc_telemetry_summary_free(&vs);

        /* A pre-M290 log carries no `jichi` field: nothing tallied, and the
         * report is byte-identical to before (no version line at all). */
        jc_telemetry_summarize(
            "{\"v\":1,\"event\":\"turn_start\"}\n", &vs);
        JC_CHECK(vs.versions.len == 0);
        jc_sb_init(&r);
        jc_telemetry_render(&vs, &r);
        JC_CHECK(r.data != NULL && strstr(r.data, "jichi") == NULL);
        jc_sb_free(&r);
        jc_telemetry_summary_free(&vs);

        /* The window and the version filter compose: an excluded event's build
         * is not tallied either, so a windowed report names only the era shown. */
        jc_telemetry_summary_init(&vs);
        vs.min_ts = 1785000000.0;
        jc_telemetry_feed(&vs,
            "{\"v\":1,\"jichi\":\"0.8.4\",\"ts\":1780000000,"
            "\"event\":\"turn_start\"}\n"
            "{\"v\":1,\"jichi\":\"0.9.0\",\"ts\":1785900000,"
            "\"event\":\"turn_start\"}\n");
        JC_CHECK(vs.versions.len == 1);
        if (vs.versions.len == 1) {
            const struct jc_telem_version *v0 = (const struct jc_telem_version *)
                jc_vec_at(&vs.versions, 0);
            JC_CHECK_STR(v0->ver, "0.9.0");
        }
        jc_telemetry_summary_free(&vs);
    }

    /* --- M289: group model rows by the WIRE id, not the config name. Renaming a
     * model split its history into two reader rows -- one real rename showed 777
     * calls under the new name and 4585 under the old, each with its own
     * est-vs-real ratio computed on a fraction of the data -- while
     * calibration.json, which keys by wire id, correctly kept one entry. --- */
    {
        struct jc_telemetry_summary ms;
        const struct jc_telem_model *mm;
        const char *log =
            "{\"event\":\"model_call\",\"model\":\"jlu/qwen3-coder-next\","
            "\"model_id\":\"jlu/qwen3-coder-next\",\"ok\":true,"
            "\"in_tok\":100,\"out_tok\":10}\n"
            "{\"event\":\"model_call\",\"model\":\"fast\","
            "\"model_id\":\"jlu/qwen3-coder-next\",\"ok\":true,"
            "\"in_tok\":200,\"out_tok\":20}\n";

        jc_telemetry_summarize(log, &ms);
        /* ONE row, despite two different config names. */
        JC_CHECK(ms.models.len == 1);
        mm = (const struct jc_telem_model *)jc_vec_at(&ms.models, 0);
        JC_CHECK(mm->calls == 2);
        JC_CHECK(mm->in_tok == 300.0);
        /* Displayed under its CURRENT name (the most recent seen). */
        JC_CHECK_STR(mm->name, "fast");
        jc_telemetry_summary_free(&ms);
    }

    /* A pre-M289 log carries no model_id, so it keys by name exactly as before --
     * two names stay two rows, and old reports are byte-identical. */
    {
        struct jc_telemetry_summary ms;
        const char *log =
            "{\"event\":\"model_call\",\"model\":\"old\",\"ok\":true,"
            "\"in_tok\":100,\"out_tok\":10}\n"
            "{\"event\":\"model_call\",\"model\":\"new\",\"ok\":true,"
            "\"in_tok\":200,\"out_tok\":20}\n";
        jc_telemetry_summarize(log, &ms);
        JC_CHECK(ms.models.len == 2);
        jc_telemetry_summary_free(&ms);
    }

    /* --- M286 cross-instrument lint: the reader's drift metric and the ratio
     * the agent LEARNS must be the same arithmetic on the same basis.
     *
     * They were not: jc_calib_observe used `history + 2000` while this reader
     * summed the measured system + tools + history, so one model persisted 2.717
     * where the reader read 1.17 from the same events -- a 2.3x disagreement
     * inside one program, with the wrong number driving every context decision.
     * Nothing compared them, so nothing caught it. This pins the definition:
     * drift == real / (sys_tok + tools_tok + hist_tok), which is exactly the
     * basis jc_agent.c now hands to jc_calib_observe (it passes these same three
     * values, so the two cannot drift apart without this check failing).
     *
     * What this does NOT prove: that jc_agent.c wires the right variables in --
     * a unit test cannot reach into stream_once. It proves the CONTRACT both
     * sides are written against, which is the part that was ambiguous. --- */
    {
        struct jc_telemetry_summary cs;
        const struct jc_telem_model *cm;
        double expect;
        /* sys 6340 + tools 4827 + history 59806 = 70973 estimated; the provider
         * reported 84458 real -- the shape of a real post-M284 call. */
        const char *log =
            "{\"event\":\"model_call\",\"model\":\"fast\",\"ok\":true,"
            "\"in_tok\":84458,\"out_tok\":100,\"sys_tok\":6340,"
            "\"tools_tok\":4827,\"hist_tok\":59806}\n";

        jc_telemetry_summarize(log, &cs);
        JC_CHECK(cs.models.len == 1);
        cm = (const struct jc_telem_model *)jc_vec_at(&cs.models, 0);
        JC_CHECK(cm->drift_n == 1);
        expect = 84458.0 / (6340.0 + 4827.0 + 59806.0);
        JC_CHECK(cm->drift_sum > expect - 1e-9 && cm->drift_sum < expect + 1e-9);
        /* And the honest basis lands near 1.19, not the 2.7 the old basis
         * produced from these very numbers (84458 / (59806 + 2000) = 1.37, and
         * worse the smaller the history -- which is what made the old "constant"
         * a function of workload shape). */
        JC_CHECK(cm->drift_sum > 1.15 && cm->drift_sum < 1.25);
        jc_telemetry_summary_free(&cs);
    }
    /* M599 */
    test_telemetry_default_path();
}
