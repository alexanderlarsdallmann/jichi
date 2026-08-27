/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_insights.c - recurring-problem mining (M70). */

#include "jc_test.h"
#include "jc_insights.h"
#include "jc_telemetry.h"
#include "jc_vec.h"

#include <string.h>
#include "jc_str.h"
#include "jc_snprintf.h"

static int has_kind(const struct jc_vec *f, int kind, const char *subject)
{
    jc_size i;
    for (i = 0; i < f->len; i++) {
        const struct jc_insight *in =
            (const struct jc_insight *)jc_vec_at((struct jc_vec *)f, i);
        if (in->kind == kind &&
            (subject == NULL || strcmp(in->subject, subject) == 0)) {
            return 1;
        }
    }
    return 0;
}

static void push_tool(struct jc_telemetry_summary *s, const char *name,
                      long calls, long ok)
{
    struct jc_telem_tool t;
    memset(&t, 0, sizeof(t));
    strcpy(t.name, name);
    t.calls = calls;
    t.ok = ok;
    jc_vec_push(&s->tools, &t);
}

/* Like push_tool but stamps the recency timestamps (last call / last ok / last
 * fail) so the recency-aware ranker can be exercised. */
static void push_tool_ts(struct jc_telemetry_summary *s, const char *name,
                         long calls, long ok, double last_ts,
                         double last_ok_ts, double last_fail_ts)
{
    struct jc_telem_tool t;
    memset(&t, 0, sizeof(t));
    strcpy(t.name, name);
    t.calls = calls;
    t.ok = ok;
    t.last_ts = last_ts;
    t.last_ok_ts = last_ok_ts;
    t.last_fail_ts = last_fail_ts;
    jc_vec_push(&s->tools, &t);
}

static void push_model(struct jc_telemetry_summary *s, const char *name,
                       long timeouts)
{
    struct jc_telem_model m;
    memset(&m, 0, sizeof(m));
    strcpy(m.name, name);
    m.timeouts = timeouts;
    jc_vec_push(&s->models, &m);
}

static void test_from_telemetry(void)
{
    struct jc_telemetry_summary s;
    struct jc_vec f;

    jc_telemetry_summary_init(&s);
    push_tool(&s, "edit_file", 4, 1);   /* 25% ok, >=3 calls => flag */
    push_tool(&s, "read_file", 10, 10); /* perfect => no flag        */
    push_tool(&s, "rare", 2, 0);        /* below min-calls => no flag */
    push_model(&s, "m", 2);             /* >=2 timeouts => flag       */
    s.retries = 5;                      /* >=3 => flag                */
    s.routes = 1;                       /* below threshold => no flag */

    jc_vec_init(&f, sizeof(struct jc_insight));
    jc_insights_from_telemetry(&s, &f);

    JC_CHECK(has_kind(&f, JC_INSIGHT_TOOL_FAIL, "edit_file"));
    JC_CHECK(!has_kind(&f, JC_INSIGHT_TOOL_FAIL, "read_file"));
    JC_CHECK(!has_kind(&f, JC_INSIGHT_TOOL_FAIL, "rare"));
    JC_CHECK(has_kind(&f, JC_INSIGHT_MODEL_TIMEOUT, "m"));
    JC_CHECK(has_kind(&f, JC_INSIGHT_RETRY, NULL));
    JC_CHECK(!has_kind(&f, JC_INSIGHT_ROUTE, NULL));

    jc_vec_free(&f);
    jc_telemetry_summary_free(&s);
}

/* M134: autonomy-outcome + hard-error signals are mined into lessons. */
static void test_outcome_signals(void)
{
    struct jc_telemetry_summary s;
    struct jc_vec f;

    jc_telemetry_summary_init(&s);
    s.out_verify_failed = 3;    /* >= 2 => flag */
    s.out_budget_reverted = 2;  /* >= 2 => flag */
    s.errors = 4;               /* >= 3 => flag */
    s.out_budget_kept = 9;      /* kept work is NOT a problem => no flag */

    /* M417: even ONE moved-goalpost edit is lesson-worthy -- it is the
     * difference between a grade and a gamed grade. */
    s.test_edits = 1;

    jc_vec_init(&f, sizeof(struct jc_insight));
    jc_insights_from_telemetry(&s, &f);
    JC_CHECK(has_kind(&f, JC_INSIGHT_VERIFY_FAIL, "verify"));
    JC_CHECK(has_kind(&f, JC_INSIGHT_BUDGET_REVERT, "budget"));
    JC_CHECK(has_kind(&f, JC_INSIGHT_ERROR, "model_error"));
    JC_CHECK(has_kind(&f, JC_INSIGHT_TEST_EDIT, "test_edit"));
    jc_vec_free(&f);

    /* Below thresholds => nothing flagged. */
    jc_telemetry_summary_free(&s);
    jc_telemetry_summary_init(&s);
    s.out_verify_failed = 1;
    s.out_budget_reverted = 1;
    s.errors = 2;
    jc_vec_init(&f, sizeof(struct jc_insight));
    jc_insights_from_telemetry(&s, &f);
    JC_CHECK(!has_kind(&f, JC_INSIGHT_VERIFY_FAIL, NULL));
    JC_CHECK(!has_kind(&f, JC_INSIGHT_BUDGET_REVERT, NULL));
    JC_CHECK(!has_kind(&f, JC_INSIGHT_ERROR, NULL));
    jc_vec_free(&f);
    jc_telemetry_summary_free(&s);
}

static void test_redo_loops(void)
{
    struct jc_vec f;
    const char *p3[5];
    const char *p2[3];

    /* src/a.c edited 3 times (interleaved) => flag once; b.c twice => no flag. */
    p3[0] = "src/a.c"; p3[1] = "src/b.c"; p3[2] = "src/a.c";
    p3[3] = "src/b.c"; p3[4] = "src/a.c";
    jc_vec_init(&f, sizeof(struct jc_insight));
    jc_insights_redo_loops(p3, 5, &f);
    JC_CHECK(has_kind(&f, JC_INSIGHT_REDO_LOOP, "src/a.c"));
    JC_CHECK(!has_kind(&f, JC_INSIGHT_REDO_LOOP, "src/b.c"));
    {
        /* a.c counted exactly once despite 3 occurrences (dedup). */
        jc_size i, n = 0;
        for (i = 0; i < f.len; i++) {
            const struct jc_insight *in =
                (const struct jc_insight *)jc_vec_at(&f, i);
            if (in->kind == JC_INSIGHT_REDO_LOOP) n++;
        }
        JC_CHECK(n == 1);
    }
    jc_vec_free(&f);

    /* Two distinct files, neither repeated enough => no findings. */
    p2[0] = "x"; p2[1] = "y"; p2[2] = "x";
    jc_vec_init(&f, sizeof(struct jc_insight));
    jc_insights_redo_loops(p2, 3, &f);
    JC_CHECK(f.len == 0);
    jc_vec_free(&f);

    /* Guards. */
    jc_vec_init(&f, sizeof(struct jc_insight));
    jc_insights_redo_loops(NULL, 0, &f);
    JC_CHECK(f.len == 0);
    jc_vec_free(&f);
}

/* M600: a resolver that knows exactly one path. */
static int fake_exists(const char *path, void *ctx)
{
    (void)ctx;
    return strcmp(path, "tests/smoke/known.sh") == 0;
}

static void test_stale_review(void)
{
    struct jc_vec f;

    /* Notes present, some citing a line => the advisory finding, plus (M600) the
     * pinned-share finding: two, and no paths finding without a resolver. */
    jc_vec_init(&f, sizeof(struct jc_insight));
    jc_insights_stale_review(
        "- delete_char lacks bounds checking [evidence: foo.zig line 162]\n"
        "- prefer small commits\n", &f);
    JC_CHECK(f.len == 2);
    JC_CHECK(has_kind(&f, JC_INSIGHT_STALE_NOTE, "memory"));
    JC_CHECK(has_kind(&f, JC_INSIGHT_STALE_NOTE, "memory-pins"));
    JC_CHECK(!has_kind(&f, JC_INSIGHT_STALE_NOTE, "memory-paths"));
    jc_vec_free(&f);

    /* No memory => no finding. */
    jc_vec_init(&f, sizeof(struct jc_insight));
    jc_insights_stale_review("", &f);
    jc_insights_stale_review(NULL, &f);
    JC_CHECK(f.len == 0);
    jc_vec_free(&f);

    /* M600: pinned share and unresolved paths, with a resolver that knows one
     * file. Three notes: one pinned and resolving, one naming a path the
     * resolver rejects, one plain prose (an "and/or" is not a path). */
    jc_vec_init(&f, sizeof(struct jc_insight));
    jc_insights_stale_review_ex(
        "- floor it [pins: tests/smoke/known.sh]\n"
        "- the allocator in src/gone/alloc.c leaks on line 12\n"
        "- prefer small and/or reviewable commits\n",
        fake_exists, NULL, &f);
    JC_CHECK(f.len == 3); /* review + pins + paths */
    JC_CHECK(has_kind(&f, JC_INSIGHT_STALE_NOTE, "memory"));
    JC_CHECK(has_kind(&f, JC_INSIGHT_STALE_NOTE, "memory-pins"));
    JC_CHECK(has_kind(&f, JC_INSIGHT_STALE_NOTE, "memory-paths"));
    {
        jc_size i;
        for (i = 0; i < f.len; i++) {
            const struct jc_insight *in =
                (const struct jc_insight *)jc_vec_at(&f, i);
            if (strcmp(in->subject, "memory-pins") == 0) {
                JC_CHECK(in->count == 1);
                JC_CHECK(strstr(in->detail, "1 of 3") != NULL);
            } else if (strcmp(in->subject, "memory-paths") == 0) {
                JC_CHECK(in->count == 1);
                JC_CHECK(strstr(in->detail, "src/gone/alloc.c") != NULL);
                JC_CHECK(strstr(in->detail, "known.sh") == NULL);
            }
        }
    }
    jc_vec_free(&f);

    /* No resolver: no paths finding, pins still counted (an absence is not a
     * finding; a resolver is what makes "does not resolve" a fact). */
    jc_vec_init(&f, sizeof(struct jc_insight));
    jc_insights_stale_review_ex("- x in src/gone/alloc.c\n", NULL, NULL, &f);
    JC_CHECK(f.len == 2);
    JC_CHECK(!has_kind(&f, JC_INSIGHT_STALE_NOTE, "memory-paths"));
    jc_vec_free(&f);
}

/* Recency-aware tool-fail gating: a bad cumulative ok-rate is flagged only when
 * the tool is still failing recently -- not if it recovered or went quiet. */
static void test_from_telemetry_recency(void)
{
    struct jc_telemetry_summary s;
    struct jc_vec f;
    double now = 1000000.0;
    double window = 100.0;

    jc_telemetry_summary_init(&s);
    s.max_ts = now;
    /* recent-failing: last call was a fail, within the window => FLAG. */
    push_tool_ts(&s, "still_bad", 10, 2, now - 10, now - 500, now - 10);
    /* recovered: worse cumulative rate, but the latest call SUCCEEDED => skip. */
    push_tool_ts(&s, "recovered", 36, 8, now - 5, now - 5, now - 900);
    /* quiet: bad rate, last activity older than the window => skip. */
    push_tool_ts(&s, "went_quiet", 7, 0, now - 5000, 0, now - 5000);
    /* no recency data (all ts 0): judged on cumulative counts only => FLAG. */
    push_tool(&s, "legacy_bad", 8, 1);

    /* With a window: recovered + quiet are suppressed; the other two remain. */
    jc_vec_init(&f, sizeof(struct jc_insight));
    jc_insights_from_telemetry_ex(&s, &f, window);
    JC_CHECK(has_kind(&f, JC_INSIGHT_TOOL_FAIL, "still_bad"));
    JC_CHECK(!has_kind(&f, JC_INSIGHT_TOOL_FAIL, "recovered"));
    JC_CHECK(!has_kind(&f, JC_INSIGHT_TOOL_FAIL, "went_quiet"));
    JC_CHECK(has_kind(&f, JC_INSIGHT_TOOL_FAIL, "legacy_bad"));
    jc_vec_free(&f);

    /* window == 0 (the non-_ex path): the quiet tool is NOT aged out, but the
     * recovered one still is (recovery doesn't depend on the window). */
    jc_vec_init(&f, sizeof(struct jc_insight));
    jc_insights_from_telemetry(&s, &f);
    JC_CHECK(has_kind(&f, JC_INSIGHT_TOOL_FAIL, "still_bad"));
    JC_CHECK(!has_kind(&f, JC_INSIGHT_TOOL_FAIL, "recovered"));
    JC_CHECK(has_kind(&f, JC_INSIGHT_TOOL_FAIL, "went_quiet"));
    JC_CHECK(has_kind(&f, JC_INSIGHT_TOOL_FAIL, "legacy_bad"));
    jc_vec_free(&f);

    jc_telemetry_summary_free(&s);
}

/* M286: a red gate is not a broken tool. `cmd_fail` (M168) counts the failures
 * that were a command the tool ran correctly reporting non-zero -- a red build,
 * a failing test -- and a fix-forward loop runs those on purpose. Judging the
 * tool on raw `ok` flags it hardest exactly when it is working hardest, and
 * these insights are what the /learn mentor turns into durable lessons. */
static void test_red_gates_are_not_tool_failures(void)
{
    struct jc_telemetry_summary s;
    struct jc_vec f;

    jc_telemetry_summary_init(&s);

    /* A test gate in a fix-forward loop: 331 calls, 250 clean passes, and 81
     * red runs. Raw ok-rate 75% (below the 60%? no -- but the shape matters at
     * the boundary, so use numbers that cross it). */
    {
        struct jc_telem_tool t;
        memset(&t, 0, sizeof(t));
        strcpy(t.name, "run_tests");
        t.calls = 100;
        t.ok = 20;        /* raw ok-rate 20% -> would be flagged */
        t.cmd_fail = 70;  /* ...but 70 of the 80 "failures" were red gates */
        jc_vec_push(&s.tools, &t);
    }
    /* A genuinely broken tool: every failure is a malfunction, none a red exit. */
    {
        struct jc_telem_tool t;
        memset(&t, 0, sizeof(t));
        strcpy(t.name, "format_file");
        t.calls = 10;
        t.ok = 2;
        t.cmd_fail = 0;
        jc_vec_push(&s.tools, &t);
    }

    jc_vec_init(&f, sizeof(struct jc_insight));
    jc_insights_from_telemetry(&s, &f);
    /* tool-level ok = (20 + 70)/100 = 90% -> healthy, not flagged. */
    JC_CHECK(!has_kind(&f, JC_INSIGHT_TOOL_FAIL, "run_tests"));
    /* 2/10 = 20% with no red exits to explain it -> still flagged. */
    JC_CHECK(has_kind(&f, JC_INSIGHT_TOOL_FAIL, "format_file"));
    jc_vec_free(&f);

    jc_telemetry_summary_free(&s);
}


/* M474: provenance. `learn analyze <path>` mines three places -- the telemetry file
 * you named, the global session store, and the workspace's memory.md -- and printed
 * all of it as one flat list. Measured on a real run: of two findings, the second
 * came from a DIFFERENT CHECKOUT of the same project (0 occurrences in the file
 * named on the command line, 25 in the session store). A reader would reasonably go
 * looking for a file that is not in their tree.
 *
 * The behaviour is documented; the OUTPUT was the problem. These checks pin the
 * label, the workspace that has to travel with a session finding, and -- the part
 * that keeps this from breaking every other caller -- that an unstamped finding
 * still renders exactly as before. */
static void push_bare(struct jc_vec *v, const char *detail)
{
    struct jc_insight in;
    memset(&in, 0, sizeof(in));
    in.kind = JC_INSIGHT_COMPACT;
    jc_snprintf(in.detail, sizeof(in.detail), "%s", detail);
    jc_vec_push(v, &in);
}

static void test_provenance(void)
{
    struct jc_vec f;
    struct jc_sb out;

    /* --- the stamper ------------------------------------------------------ */
    jc_vec_init(&f, sizeof(struct jc_insight));
    push_bare(&f, "first");
    push_bare(&f, "second");
    /* Stamp only from index 1: the caller stamps each source over its own range. */
    jc_insights_stamp(&f, 1, "session", "/ws/b");
    JC_CHECK_STR(((struct jc_insight *)jc_vec_at(&f, 0))->source, "");
    JC_CHECK_STR(((struct jc_insight *)jc_vec_at(&f, 1))->source, "session");
    JC_CHECK_STR(((struct jc_insight *)jc_vec_at(&f, 1))->origin, "/ws/b");

    /* An already-stamped finding is NOT overwritten: an inner scan knows more
     * (which session) than the outer sweep, and must win. */
    jc_insights_stamp(&f, 0, "telemetry", NULL);
    JC_CHECK_STR(((struct jc_insight *)jc_vec_at(&f, 0))->source, "telemetry");
    JC_CHECK_STR(((struct jc_insight *)jc_vec_at(&f, 1))->source, "session");
    JC_CHECK_STR(((struct jc_insight *)jc_vec_at(&f, 1))->origin, "/ws/b");

    /* Degenerate inputs must not crash or write past the end. */
    jc_insights_stamp(&f, 99, "x", NULL);      /* `from` past the end */
    jc_insights_stamp(NULL, 0, "x", NULL);
    jc_insights_stamp(&f, 0, NULL, NULL);
    JC_CHECK(f.len == 2);

    /* --- the rendering ---------------------------------------------------- */
    jc_sb_init(&out);
    jc_insights_render(&f, &out);
    JC_CHECK(strstr(out.data, "[telemetry] first") != NULL);
    JC_CHECK(strstr(out.data, "[session: /ws/b] second") != NULL);
    /* The orientation note fires because a session finding is present -- that is
     * the case where the reader has to check whose workspace it was. */
    JC_CHECK(strstr(out.data, "global session store") != NULL);
    jc_sb_free(&out);
    jc_vec_free(&f);

    /* A telemetry-only list stays terse: no note, because there is nothing to
     * disambiguate. A note on every run is a note people stop reading. */
    jc_vec_init(&f, sizeof(struct jc_insight));
    push_bare(&f, "only");
    jc_insights_stamp(&f, 0, "telemetry", NULL);
    jc_sb_init(&out);
    jc_insights_render(&f, &out);
    JC_CHECK(strstr(out.data, "[telemetry] only") != NULL);
    JC_CHECK(strstr(out.data, "global session store") == NULL);
    jc_sb_free(&out);
    jc_vec_free(&f);

    /* UNSTAMPED renders exactly as before -- jc_insights_render has other
     * callers, and this fix must not change what they print. */
    jc_vec_init(&f, sizeof(struct jc_insight));
    push_bare(&f, "plain finding");
    jc_sb_init(&out);
    jc_insights_render(&f, &out);
    JC_CHECK(strstr(out.data, "  - plain finding\n") != NULL);
    JC_CHECK(strstr(out.data, "[") == NULL);
    jc_sb_free(&out);
    jc_vec_free(&f);
}

void test_insights(void)
{
    test_from_telemetry();
    test_from_telemetry_recency();
    test_outcome_signals();
    test_redo_loops();
    test_stale_review();
    test_red_gates_are_not_tool_failures();
    test_provenance();
}
