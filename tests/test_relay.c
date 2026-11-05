/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 Justus-Liebig-Universität Gießen
 * Author: Alexander-Lars Dallmann */
/* test_relay.c -- the context relay's decision core (M920), as a total
 * truth table: every trigger, its order, its reset, and the off-by-default
 * guarantee that keeps this feature inert until it is turned on. M921 adds
 * the baton renderer's checks (proposal 10.4): the bound at 10,000 touched
 * files, the model part cut first at a UTF-8 boundary with the note, the
 * ticket path removed, and the mechanical part's top-N cut. */

#include "jc_test.h"
#include "jc_relay.h"
#include <string.h>
#include <jc_snprintf.h>

static void test_relay_baton(void);   /* M921, below test_relay */
static void test_relay_leg_budget(void); /* M951: the V2 controller, below */

static struct relay_signal sig_init(void)
{
    struct relay_signal s;
    memset(&s, 0, sizeof s);
    return s;
}

static struct relay_cfg cfg_init(void)
{
    struct relay_cfg c;
    memset(&c, 0, sizeof c);
    c.relay_on = 1;
    c.trigger = 0;         /* exhausted: the default */
    c.hard_pct = 90;       /* E3 at 90% of the window: the default */
    return c;
}

void test_relay(void)
{
    struct relay_signal s;
    struct relay_cfg c;
    struct relay_state st;
    enum relay_reason r;
    enum relay_action a;

    /* --- off by default: the core NEVER fires ----------------------------
     * The feature is inert until relay_on is set, on EVERY signal: a
     * refused request, a hard limit, an unrelieved streak, a used leg
     * budget. One of these returning a handover while off would turn every
     * run into a relay. */
    c = cfg_init(); c.relay_on = 0;
    memset(&st, 0, sizeof st);
    s = sig_init(); s.overflow = 1;
    a = relay_decide(&s, &c, &st, &r);
    JC_CHECK(a == JC_RELAY_CONTINUE && r == JC_RELAY_NONE);
    s = sig_init(); s.server_tokens = 100000; s.limit = 100000;
    a = relay_decide(&s, &c, &st, &r);
    JC_CHECK(a == JC_RELAY_CONTINUE && r == JC_RELAY_NONE);
    s = sig_init(); s.pressed = 1; s.unrelieved = 1;
    st.unrelieved_streak = 5;
    a = relay_decide(&s, &c, &st, &r);
    JC_CHECK(a == JC_RELAY_CONTINUE && r == JC_RELAY_NONE);
    s = sig_init(); s.leg_calls = 100; c.leg_calls_max = 10;
    a = relay_decide(&s, &c, &st, &r);
    JC_CHECK(a == JC_RELAY_CONTINUE && r == JC_RELAY_NONE);
    c = cfg_init();   /* relay_on = 1 again */

    /* --- E4: an overflow refuses, and it outranks everything -------------
     * The strongest evidence: the server said no. It fires on the FIRST
     * round (no streak to build) and before the hard limit. */
    memset(&st, 0, sizeof st);
    s = sig_init(); s.overflow = 1;
    s.server_tokens = 10; s.limit = 1000;   /* far from the hard limit */
    a = relay_decide(&s, &c, &st, &r);
    JC_CHECK(a == JC_RELAY_HAND_OVER && r == JC_RELAY_OVERFLOW);
    JC_CHECK_STR(relay_reason_name(r), "overflow");
    /* still a handover when the streak is already built: overflow is first */
    st.unrelieved_streak = 3;
    a = relay_decide(&s, &c, &st, &r);
    JC_CHECK(a == JC_RELAY_HAND_OVER && r == JC_RELAY_OVERFLOW);

    /* --- E3: the hard limit, at and above the percentage -----------------
     * server_tokens * 100 >= limit * hard_pct: at 90% of a 100k window,
     * 90000 fires, 89999 does not. No measurement means no safety net. */
    memset(&st, 0, sizeof st);
    s = sig_init(); s.limit = 100000;
    s.server_tokens = 90000;
    a = relay_decide(&s, &c, &st, &r);
    JC_CHECK(a == JC_RELAY_HAND_OVER && r == JC_RELAY_HARD_LIMIT);
    s.server_tokens = 89999;
    a = relay_decide(&s, &c, &st, &r);
    JC_CHECK(a == JC_RELAY_CONTINUE && r == JC_RELAY_NONE);
    s.server_tokens = 0;      /* the server sent no count: the net is blind */
    a = relay_decide(&s, &c, &st, &r);
    JC_CHECK(a == JC_RELAY_CONTINUE && r == JC_RELAY_NONE);
    s = sig_init(); s.server_tokens = 90000; s.limit = 0;  /* window unknown */
    a = relay_decide(&s, &c, &st, &r);
    JC_CHECK(a == JC_RELAY_CONTINUE && r == JC_RELAY_NONE);

    /* --- E1: the unrelieved streak, counted and reset by state -----------
     * ONE unrelieved pass does not hand over; TWO consecutive do. A round
     * WITHOUT pressure resets the streak; a pressured round that reached its
     * target does not count and does not reset (the window is filling but
     * elision is still working). */
    memset(&st, 0, sizeof st);
    s = sig_init(); s.pressed = 1; s.unrelieved = 1;
    a = relay_decide(&s, &c, &st, &r);
    JC_CHECK(a == JC_RELAY_CONTINUE && st.unrelieved_streak == 1);
    a = relay_decide(&s, &c, &st, &r);    /* the second, consecutive one */
    JC_CHECK(a == JC_RELAY_HAND_OVER && r == JC_RELAY_UNRELIEVED);
    memset(&st, 0, sizeof st);
    s = sig_init(); s.pressed = 1; s.unrelieved = 1;
    relay_decide(&s, &c, &st, &r);        /* streak: 1 */
    s = sig_init(); s.pressed = 0;        /* a quiet round: reset to 0 */
    relay_decide(&s, &c, &st, &r);
    JC_CHECK(st.unrelieved_streak == 0);
    s = sig_init(); s.pressed = 1; s.unrelieved = 1;
    relay_decide(&s, &c, &st, &r);        /* streak: 1 again */
    s = sig_init(); s.pressed = 1; s.reached = 1; s.unrelieved = 0;
    relay_decide(&s, &c, &st, &r);        /* working: neither counts nor resets */
    JC_CHECK(st.unrelieved_streak == 1);
    s = sig_init(); s.pressed = 1; s.unrelieved = 1;
    a = relay_decide(&s, &c, &st, &r);    /* second unrelieved: fires */
    JC_CHECK(a == JC_RELAY_HAND_OVER && r == JC_RELAY_UNRELIEVED);

    /* --- E2: the latch, armed while pressed ------------------------------
     * The trims were skipped BECAUSE a previous pass proved the range dry,
     * and the window is still over the high-water. One such round fires --
     * it is stronger than a single unrelieved pass. Latched without
     * pressure (an eager, unpressured dedup) must not. */
    memset(&st, 0, sizeof st);
    s = sig_init(); s.pressed = 1; s.latched = 1;
    a = relay_decide(&s, &c, &st, &r);
    JC_CHECK(a == JC_RELAY_HAND_OVER && r == JC_RELAY_LATCHED);
    s = sig_init(); s.pressed = 0; s.latched = 1;
    a = relay_decide(&s, &c, &st, &r);
    JC_CHECK(a == JC_RELAY_CONTINUE);

    /* --- the "short" trigger is OFF by default and ON when chosen ---------
     * Pressed and the target missed is a handover ONLY when trigger == 1;
     * under the default "exhausted" it keeps working (a short pass is
     * served normally -- Review 2 R4: 0 rejections at 86% of the window). */
    memset(&st, 0, sizeof st);
    c = cfg_init(); c.trigger = 0;
    s = sig_init(); s.pressed = 1; s.reached = 0; s.unrelieved = 0;
    a = relay_decide(&s, &c, &st, &r);
    JC_CHECK(a == JC_RELAY_CONTINUE);
    c.trigger = 1;
    a = relay_decide(&s, &c, &st, &r);
    JC_CHECK(a == JC_RELAY_HAND_OVER && r == JC_RELAY_SHORT);
    /* and "short" does not fire when the target was reached */
    s = sig_init(); s.pressed = 1; s.reached = 1;
    a = relay_decide(&s, &c, &st, &r);
    JC_CHECK(a == JC_RELAY_CONTINUE);
    c = cfg_init();

    /* --- E5: the leg budget, and its ORDER --------------------------------
     * At or over the cap, a round with NO pressure hands over (the only
     * trigger that can fire unpressured). But a pressured round reports the
     * pressure, not the counter: the budget is checked last. */
    memset(&st, 0, sizeof st);
    c = cfg_init(); c.leg_calls_max = 50;
    s = sig_init(); s.leg_calls = 50;
    a = relay_decide(&s, &c, &st, &r);
    JC_CHECK(a == JC_RELAY_HAND_OVER && r == JC_RELAY_LEG_BUDGET);
    s.leg_calls = 49;
    a = relay_decide(&s, &c, &st, &r);
    JC_CHECK(a == JC_RELAY_CONTINUE);
    s = sig_init(); s.leg_calls = 100; s.pressed = 1; s.latched = 1;
    a = relay_decide(&s, &c, &st, &r);
    JC_CHECK(a == JC_RELAY_HAND_OVER && r == JC_RELAY_LATCHED);  /* not budget */
    /* no cap set: never a budget handover */
    c.leg_calls_max = 0;
    s = sig_init(); s.leg_calls = 100000;
    a = relay_decide(&s, &c, &st, &r);
    JC_CHECK(a == JC_RELAY_CONTINUE);

    /* --- relay_may_continue: the runaway bounds (section 6.13) ------------
     * A leg ceiling and a two-leg stall stop the RELAY, not just one leg.
     * With no state at all it refuses to speculate -- it stops. */
    c = cfg_init(); c.legs_max = 4;
    memset(&st, 0, sizeof st); st.legs = 3; st.no_progress_legs = 0;
    a = relay_may_continue(&st, &c, &r);
    JC_CHECK(a == JC_RELAY_CONTINUE);
    st.legs = 4;
    a = relay_may_continue(&st, &c, &r);
    JC_CHECK(a == JC_RELAY_STOP_STALLED && r == JC_RELAY_MAX_LEGS);
    st.legs = 1; st.no_progress_legs = 2;
    a = relay_may_continue(&st, &c, &r);
    JC_CHECK(a == JC_RELAY_STOP_STALLED && r == JC_RELAY_NO_PROGRESS);
    st.no_progress_legs = 1;
    a = relay_may_continue(&st, &c, &r);
    JC_CHECK(a == JC_RELAY_CONTINUE);
    a = relay_may_continue((struct relay_state *)0, &c, &r);
    JC_CHECK(a == JC_RELAY_STOP_STALLED);

    /* --- the reason names: every value has one, never NULL ---------------- */
    JC_CHECK_STR(relay_reason_name(JC_RELAY_NONE), "none");
    JC_CHECK_STR(relay_reason_name(JC_RELAY_UNRELIEVED), "unrelieved");
    JC_CHECK_STR(relay_reason_name(JC_RELAY_LEG_BUDGET), "leg_budget");
    JC_CHECK(relay_reason_name(JC_RELAY_MAX_LEGS) != NULL);

    test_relay_baton();
    test_relay_leg_budget();
}

/* M951: the V2 leg-budget controller (proposal 6.10) as a total truth table.
 * The arithmetic under test:
 *   n_fit = floor( (80/100 * W - P0_next - 4000) / g )
 *   B     = clamp( n_fit, 25, cap ), then B = min( B, fence )
 * with the degeneracies (no growth, no window, no cap, past-high-water) each
 * a DEFINED answer, and the tighten-only guarantee: B never exceeds cap, and
 * the fence applied last can beat even the floor. */
static void test_relay_leg_budget(void)
{
    jc_size B;

    /* --- no cap: E5 is off, out_B 0 -- total, it must still write -------- */
    B = 999;   /* a stale value: the branch must overwrite it */
    relay_leg_budget(1000, 500, 200000, 2000, 0, 0, &B);
    JC_CHECK(B == 0);
    /* cap 0 wins even with growth and a window present */
    relay_leg_budget(1000, 500, 200000, 2000, 0, 50, &B);
    JC_CHECK(B == 0);

    /* --- no growth (g = 0): the window is not filling; the cap stands ---- */
    relay_leg_budget(1000, 0, 200000, 2000, 100, 0, &B);
    JC_CHECK(B == 100);
    /* negative g is a caller error and takes the same defined answer */
    relay_leg_budget(1000, -5, 200000, 2000, 100, 0, &B);
    JC_CHECK(B == 100);

    /* --- no window (W = 0): unknown, so the cap stands ------------------- */
    relay_leg_budget(1000, 500, 0, 2000, 100, 0, &B);
    JC_CHECK(B == 100);
    /* W = 0: unknown window -> the cap stands as the base, but the fence is
     * applied on EVERY path now (6.10's last line), so min(100, 30) = 30. */
    relay_leg_budget(1000, 500, 0, 2000, 100, 30, &B);
    JC_CHECK(B == 30);

    /* --- n_fit strictly between B_min and cap: the measured budget -------
     * W=200000: 80% = 160000. P0_next=20000, reserve 4000: num = 136000.
     * g=1000 -> n_fit = 136. cap 1000, no fence -> B = 136. */
    relay_leg_budget(10000, 1000, 200000, 20000, 1000, 0, &B);
    JC_CHECK(B == 136);

    /* --- n_fit above the cap: tighten-only means the cap, not n_fit ------
     * Same numbers, g=50 -> n_fit = 2720 > cap 1000 -> B = 1000. */
    relay_leg_budget(10000, 50, 200000, 20000, 1000, 0, &B);
    JC_CHECK(B == 1000);

    /* --- n_fit below B_min: the floor, not 0 ------------------------------
     * W=200000, P0_next=155000 -> num = 1000. g=200 -> n_fit = 5 < 25. */
    relay_leg_budget(150000, 200, 200000, 155000, 1000, 0, &B);
    JC_CHECK(B == 25);

    /* --- num <= 0: P0_next alone is past the high-water; still 25 --------
     * W=200000 -> 80% = 160000. P0_next = 165000 -> num < 0. */
    relay_leg_budget(160000, 1000, 200000, 165000, 1000, 0, &B);
    JC_CHECK(B == 25);
    /* exactly at the high-water (num = 0) is the same answer, not a crash */
    relay_leg_budget(150000, 1000, 200000, 156000, 1000, 0, &B);
    JC_CHECK(B == 25);

    /* --- P0_next < P0: a caller error clamps UP to P0 ---------------------
     * P0=100000, P0_next given as 10000 (impossible: notes only grow the
     * prompt): the budget is computed from P0_next = 100000, which gives
     * num = 160000-100000-4000 = 56000; g=1000 -> n_fit 56 -> B 56. */
    relay_leg_budget(100000, 1000, 200000, 10000, 1000, 0, &B);
    JC_CHECK(B == 56);
    /* ...where an honest P0_next ABOVE a small P0 (P0=10000) gives 136: the
     * clamp-up only moves P0_next toward P0 when it was BELOW it, so this row
     * is unaffected by the clamp. */
    relay_leg_budget(10000, 1000, 200000, 20000, 1000, 0, &B);
    JC_CHECK(B == 136);

    /* --- the fence: applied after the clamp, so a measured budget shrinks
     * to what the relay has left ------------------------------------------ */
    relay_leg_budget(10000, 1000, 200000, 20000, 1000, 50, &B);
    JC_CHECK(B == 50);   /* n_fit 136 -> fence 50 */

    /* --- THE FENCE BEATS THE FLOOR (the order that had to stay right) -----
     * num <= 0 would give the 25 floor, but the relay has 3 calls left: the
     * next leg gets 3, never 25 (a 25-call leg would run the relay out). */
    relay_leg_budget(160000, 1000, 200000, 165000, 1000, 3, &B);
    JC_CHECK(B == 3);

    /* --- the fence never RAISES a tightened budget ------------------------
     * n_fit 136 < fence 500 -> the measurement stands, the fence is no-op. */
    relay_leg_budget(10000, 1000, 200000, 20000, 1000, 500, &B);
    JC_CHECK(B == 136);

    /* --- the cap can be exactly B_min (tighten-only at the floor) ---------
     * cap 25, n_fit 136 -> 25, not 136. */
    relay_leg_budget(10000, 1000, 200000, 20000, 25, 0, &B);
    JC_CHECK(B == 25);
    /* and below the floor: cap 10, n_fit 136 -> 10 (the cap wins, the floor
     * cannot raise it). */
    relay_leg_budget(10000, 1000, 200000, 20000, 10, 0, &B);
    JC_CHECK(B == 10);
}

/* M921: the baton renderer (proposal 6.4's template, 10.4's checks). */
static void baton_init(struct relay_baton_facts *f)
{
    memset(f, 0, sizeof *f);
    f->relay_id = "7f3c";
    f->leg = 3;
    f->reason = "unrelieved";
    f->task = "make the vm pass its test suite";
    f->commit = "1a2b3c4d";
    f->dirty = 1;
    f->leg_calls = 412;
    f->relay_calls = 1033;
    f->fence_total = 5000;
    f->peak_prompt = 91234;
    f->todo = "[done] repro; [ ] census";
    f->last_verify = "red, 2 failures";
}

static void test_relay_baton(void)
{
    /* 10,000 changed-file paths, "src/file_%05d.c" (24 bytes each) in a
     * static buffer, indexed by `many`. 240,000 bytes -- the 10.4(1)
     * universe, held once. */
    static char buf[256 * 1024];
    static const char *many[10000];
    struct relay_baton_facts f;
    struct jc_sb out;
    const char *acc = NULL;
    int i;

    for (i = 0; i < 10000; i++) {
        char *p = buf + i * 24;
        jc_snprintf(p, 24, "src/file_%05d.c", i);
        many[i] = p;
    }

    /* --- 10.4 (1): the mechanical part fits its bound at 10,000 files ----
     * top-N IS the bound: 10,000 paths render as JC_RELAY_BATON_TOPN plus a
     * "more" count, and the whole note stays under the default 8 KB cap
     * with room for the account. */
    baton_init(&f);
    f.changed = many; f.changed_n = 10000;
    f.uncommitted = many; f.uncommitted_n = 10000;
    f.commits = many; f.commits_n = 10000;
    jc_sb_init(&out);
    JC_CHECK(relay_baton_render(&f, &out) == JC_OK);
    JC_CHECK(out.data != NULL);
    JC_CHECK(out.len > 0 && out.len < 8192);
    JC_CHECK(strstr(out.data, "(+9980 more)") != NULL);
    JC_CHECK(strstr(out.data, "## 2. Where the work stands") != NULL);
    jc_sb_free(&out);
    f.changed = f.uncommitted = f.commits = NULL;
    f.changed_n = f.uncommitted_n = f.commits_n = 0;

    /* --- the template: every section present, in order ------------------- */
    baton_init(&f);
    jc_sb_init(&out);
    JC_CHECK(relay_baton_render(&f, &out) == JC_OK);
    {
        const char *s1 = strstr(out.data, "## 1. The task");
        const char *s2 = strstr(out.data, "## 2. Where the work stands");
        const char *s3 = strstr(out.data, "## 3. The previous leg's own account");
        const char *s4 = strstr(out.data, "## 4. How to use these notes");
        JC_CHECK(s1 != NULL && s2 != NULL && s3 != NULL && s4 != NULL);
        JC_CHECK(s1 < s2 && s2 < s3 && s3 < s4);
        JC_CHECK(strstr(out.data, "leg 3 of relay 7f3c") != NULL);
        JC_CHECK(strstr(out.data, "make the vm pass its test suite") != NULL);
        JC_CHECK(strstr(out.data, "1a2b3c4d") != NULL);
        JC_CHECK(strstr(out.data, "dirty (uncommitted changes)") != NULL);
        JC_CHECK(strstr(out.data, "412 in this leg, 1033 in the relay") != NULL);
        JC_CHECK(strstr(out.data, "1033 of 5000") != NULL);
    }
    jc_sb_free(&out);

    /* --- 10.4 (2): the model part is cut first, at a UTF-8 boundary, with
     * a visible note. 100 x U+00DF (0xC3 0x9F, 2 bytes each); account_max
     * 99: the cut must walk back over the trailing continuation byte to
     * byte 98 -- 49 whole characters, never a dangling 0x9F -- and print
     * the note. The mechanical part is NOT cut. */
    baton_init(&f);
    {
        static char accbuf[201];
        for (i = 0; i < 100; i++) {
            accbuf[2 * i] = (char)0xC3;
            accbuf[2 * i + 1] = (char)0x9F;
        }
        accbuf[200] = '\0';
        acc = accbuf;
    }
    f.account = acc;
    f.account_max = 99;
    jc_sb_init(&out);
    JC_CHECK(relay_baton_render(&f, &out) == JC_OK);
    JC_CHECK(strstr(out.data, "[account cut: 98 of 200 bytes shown]")
             != NULL);
    {
        const char *p = strstr(out.data, "before relying on it)\n");
        int nchars = 0;
        JC_CHECK(p != NULL);
        if (p != NULL) {
            p += strlen("before relying on it)\n");
            while (p[0] == (char)0xC3 && p[1] == (char)0x9F) {
                nchars++;
                p += 2;
            }
            JC_CHECK(nchars == 49);
            JC_CHECK(p[0] == '\n');   /* then the note, not a continuation */
        }
    }
    jc_sb_free(&out);
    f.account = NULL; f.account_max = 0;

    /* --- 10.4 (3): a claim-ticket path is removed from BOTH parts --------
     * R01-F4 / 6.4's last bullet: the ticket store dies with the process,
     * so a note that outlives it must not cite one. */
    baton_init(&f);
    f.account = "the log is at /tmp/.jichi.d/tool-output/1234/5.txt now";
    {
        static char ucl[96];
        static const char *ucp[1];
        jc_snprintf(ucl, sizeof ucl, "/tmp/.jichi.d/tool-output/99/r.txt");
        ucp[0] = ucl;
        f.uncommitted = ucp; f.uncommitted_n = 1;
    }
    jc_sb_init(&out);
    JC_CHECK(relay_baton_render(&f, &out) == JC_OK);
    JC_CHECK(strstr(out.data, "tool-output/") == NULL);
    {
        const char *h = strstr(out.data, "[ticket path omitted");
        JC_CHECK(h != NULL);
        JC_CHECK(strstr(h + 1, "[ticket path omitted") != NULL);
    }
    jc_sb_free(&out);
    f.account = NULL; f.uncommitted = NULL; f.uncommitted_n = 0;

    /* --- no account: the labelled placeholder, never an empty section ---- */
    baton_init(&f);
    jc_sb_init(&out);
    JC_CHECK(relay_baton_render(&f, &out) == JC_OK);
    JC_CHECK(strstr(out.data, "(no account:") != NULL);
    jc_sb_free(&out);

    /* --- M1029 (D-14 residual, board 61): exact-size ASan tooth ---------
     * This test case is placed BEFORE the D-14 50-ticket case and is
     * designed to make the pre-fix in-place overflow ASan-visible.
     *
     * The pre-fix relay_baton_unticket uses in-place right-shift via
     * malloc+memcpy, writing past the allocation end for notes that grow
     * beyond the capacity. The gap is that the existing D-14 50-ticket
     * fixture (~1250 bytes, cap 2048) stays inside the buffer even with
     * the 9-byte growth per ticket; only the len check goes red.
     *
     * This tooth creates a note where the single ticket sits so close to
     * the end that the shift writes past the buffer's capacity.
     *
     * Measurements (on the fixed code):
     * - Empty account ("" with account_max=0): note_len = 825 bytes
     *   (has placeholder "(no account: ...)\n" = 102 bytes)
     * - With "tool-output/9/x.txt": note_len = 752 bytes
     *   (has "[ticket path omitted]9/x.txt\n" = 29 bytes)
     * - Base (sections 1-2, 4, no account): 723 bytes
     * - Note length with P bytes padding: 752 + P bytes
     * - Capacity doubles: 64, 128, 256, 512, 1024, 2048, 4096, ...
     *
     * Design:
     * - Target L = 2056 (exact boundary for cap 2048 + 9-byte OOB)
     * - P = L - 752 = 2056 - 752 = 1304 bytes of padding (13 more than before)
     * - Single ticket 'tool-output/9/x.txt' at end of account
     * - The tail (everything after "tool-output/") includes section 4 and
     *   the newline after the account, causing heap overflow when shifted.
     *
     * Geometry (pre-fix):
     * - note_len after unticket = 2056 (723 base + 1332 account + 1 newline)
     * - pre_len before unticket = 2047 (2056 - 9)
     * - cap (power of two >= 2048) = 2048
     * - the 179-byte tail shift (memcpy(hit+21, tmp, n+1)) lands at
     *   indices 1878..2056 (last index 2056, the NUL terminator)
     * - the allocation is 2048 bytes (indices 0..2047)
     * - so indices 2048..2056 are out of bounds: 9 OOB bytes (2056 - 2047),
     *   the first OOB byte at index 2048 -- triggering ASan's
     *   heap-buffer-overflow (lead-verified: WRITE of size 179, #1 in
     *   relay_baton_unticket, "0 bytes after 2048-byte region").
     */
    baton_init(&f);
    {
        static char accbuf[2200];  /* static so f.account remains valid after block */
        int P = 1304;  /* padding to reach target note_len of 2056 */
        int i;
        
        /* Build padding + 'tool-output/9/x.txt' */
        for (i = 0; i < P; i++) {
            accbuf[i] = 'p';
        }
        /* Suffix: 'tool-output/9/x.txt' - use snprintf for correctness */
        jc_snprintf(accbuf + P, sizeof(accbuf) - P, "tool-output/9/x.txt");
        
        f.account = accbuf;
        f.account_max = 0;  /* no cut - account is rendered verbatim */
    }
    jc_sb_init(&out);
    JC_CHECK(relay_baton_render(&f, &out) == JC_OK);
    /* The pre-fix code corrupts out.len; assert failures before ASan are OK */
    /* The fixed code keeps out.len == strlen(data) */
    JC_CHECK(out.len == strlen(out.data));
    /* Verify no "tool-output/" remains (replaced by mark) */
    JC_CHECK(strstr(out.data, "tool-output/") == NULL);
    /* Exactly one mark for the single ticket */
    {
        int mark_count = 0;
        const char *p = out.data;
        while ((p = strstr(p, "[ticket path omitted")) != NULL) {
            mark_count++;
            p += 21;
        }
        JC_CHECK(mark_count == 1);
    }
    /* The note grew by exactly 9 bytes (one ticket): the mark "[ticket path omitted]" (21) */
    /* replaces "tool-output/" (12), so growth is 9 bytes per ticket. The final note length */
    /* is 2056 (723 base + 1332 account after unticket + 1 newline). */
    /* Note: with P=1304 padding and suffix "tool-output/9/x.txt" (19 bytes), the account */
    /* is 1323 bytes. After replacement, it becomes 1332 bytes. Note length = 723 + 1332 + 1 = 2056. */
    JC_CHECK(out.len == strlen(out.data));
    JC_CHECK(out.len == 2056);  /* base(723) + account_after_unticket(1332) + newline(1) */
    jc_sb_free(&out);
    f.account = NULL;

    /* --- D-14 (M1014): 50 ticket paths in account and uncommitted lists ---
     * The unfixed code shifts each occurrence right in place, writing 9 bytes
     * past the end for each occurrence (heap overflow). The fixed version
     * rebuilds into a fresh buffer, keeping len == strlen(data).
     *
     * Note: uncommitted list uses top-N (20) limit, so only 20 of the 50
     * uncommitted paths are actually rendered. Total: 50 + 20 = 70 marks. */
    baton_init(&f);
    {
        static char accbuf[1536];
        static char uclbuf[1536];
        static const char *ucp[50];
        int i;
        /* Build a single NUL-terminated string with 50 tool-output paths,
         * each separated by a space. Each path is ~24 bytes, so 50 paths
         * need about 1200 bytes + 49 spaces + 1 NUL = ~1250 bytes. */
        accbuf[0] = '\0';
        for (i = 0; i < 50; i++) {
            char tmp[32];
            jc_snprintf(tmp, sizeof tmp, "x/tool-output/%d/y_%02d.txt", i, i);
            if (i > 0) {
                strncat(accbuf, " ", sizeof(accbuf) - strlen(accbuf) - 1);
            }
            strncat(accbuf, tmp, sizeof(accbuf) - strlen(accbuf) - 1);
        }
        /* Build 50 distinct tool-output paths in the uncommitted list.
         * Only 20 will be rendered due to top-N limit. */
        for (i = 0; i < 50; i++) {
            ucp[i] = uclbuf + i * 24;
            jc_snprintf(uclbuf + i * 24, 24, "y/tool-output/%d/z_%02d.txt", i, i);
        }
        f.account = accbuf;
        f.uncommitted = ucp; f.uncommitted_n = 50;
    }
    jc_sb_init(&out);
    JC_CHECK(relay_baton_render(&f, &out) == JC_OK);
    /* Verify no "tool-output/" remains (all replaced) */
    JC_CHECK(strstr(out.data, "tool-output/") == NULL);
    /* Count the mark occurrences - account has 50, uncommitted has 20 (top-N) = 70 */
    {
        int mark_count = 0;
        const char *p = out.data;
        while ((p = strstr(p, "[ticket path omitted")) != NULL) {
            mark_count++;
            p += 21;
        }
        JC_CHECK(mark_count == 70);
    }
    /* Verify len == strlen(data) (the fixed version keeps them in sync) */
    JC_CHECK(out.len > 0);
    JC_CHECK(out.len == strlen(out.data));
    jc_sb_free(&out);
    f.account = NULL; f.uncommitted = NULL; f.uncommitted_n = 0;

    /* --- the invalid-argument case: defined, no crash -------------------- */
    baton_init(&f);
    jc_sb_init(&out);
    JC_CHECK(relay_baton_render(NULL, &out) == JC_ERR_INVALID);
    JC_CHECK(relay_baton_render(&f, NULL) == JC_ERR_INVALID);
    jc_sb_free(&out);
}
