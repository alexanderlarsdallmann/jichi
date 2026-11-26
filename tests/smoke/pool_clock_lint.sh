#!/bin/sh
# lint: the worker pool never narrows its monotonic millisecond clock to `long`
# (def-379, M1194-d379 -- the 32-bit pool clock, design-review finding F1).
#
# THE DEFECT. The pool's monotonic clock is `double jc_now_millis()`
# (include/jc_platform.h). Past ~2^31 ms of uptime (~24.86 days) a 32-bit `long`
# reading collapses (both stamps saturate to LONG_MIN) and the watchdog's
# deadline difference reads 0, so a task timeout -- a fence, not a cap -- never
# fires and idle_s reads 0 for every task. Measured live on a -m32 build in
# notes/M1194.md MS3 (probe: docs/internal/2026-11-11-design-review/ms32-probe.c);
# the unit cell tests/test_pool_clock.c pins the divergence.
#
# THE UNIVERSE (the SIX value-narrowing sites, floored at 6; D379.3). Each
# narrows a monotonic-clock READING to `long` at the point it is written:
#
#   (1) a clock read cast to long -- `(long)jc_now_millis` (five sites):
#     src/tools/jc_tool_parallel.c:1368  t->last_ms     (the TOOL handler)
#     src/tools/jc_tool_parallel.c:1376  t->last_ms     (the TOK handler)
#     src/tools/jc_tool_parallel.c:1929  t->launch_ms   (the fork stamp)
#     src/tools/jc_tool_parallel.c:1992  s_pool_now_ms  (the snap refresh)
#     src/tools/jc_tool_parallel.c:2019  long now       (the watchdog)
#   (2) the heartbeat store of a clock read -- `batch_hb_last_ms = (long)` (one site):
#     src/chat/jc_app.c:1962             app->batch_hb_last_ms
#
# The field/local TYPES that hold these stamps (ptask.last_ms, ptask.launch_ms,
# s_pool_now_ms, jc_app.batch_hb_last_ms, the watchdog local now) are widened to
# `double` in the same commit (D379.1) so the write no longer narrows; the six
# value-writes above are what this lint floors on, because those are the
# narrowings (D379.3: "refuses any narrowing ... `(long)jc_now_millis` and the
# batch_hb_last_ms = (long) store. Green = 0.").
#
# NOT in the universe (stay `long` by D379.2): the DURATION BOUNDS, not clock
# readings -- the run_pool parameter `timeout_ms` (a bound: 300 s fits) and the
# heartbeat `interval_ms` (a bound). A comparison of a widened value against a
# bound promotes to double correctly.
#
# THE TEETH. A check that can only ever read 0 is a no-op. So before it scans
# the real tree it scans an embedded fixture -- scratch copies carrying the
# six OLD (narrowing) forms plus the two BOUND declarations that must never be
# flagged -- and refuses to run unless the fixture yields exactly 6 findings
# (the floor). That red-first is shown in the report, per
# docs/TESTING_RUNBOOK.md 1.
#
# Registration is NOT this task's: run.sh is owned by the driver task (wave C).
# This driver is red on the unfixed tree (6 findings) and green after the widen.
. "$(dirname "$0")/_smoke.sh"

t_plan 1

tmp=$(smoke_tmp)

F1="$SMOKE_ROOT/src/tools/jc_tool_parallel.c"
F2="$SMOKE_ROOT/src/chat/jc_app.c"

# scan_clock VALUE... -- the two defect forms, over VALUE files:
#   P1  a clock read narrowed to long:  (long)jc_now_millis
#   P2  the heartbeat store narrowed to long:  batch_hb_last_ms = (long)
# The two DURATION bounds (timeout_ms, interval_ms) are not clock readings and
# are deliberately not matched.
scan_clock() {
    {
        grep -hE '\(long\)[[:space:]]*jc_now_millis' "$@"
        grep -hE 'batch_hb_last_ms[[:space:]]*=[[:space:]]*\(long\)' "$@"
    } 2>/dev/null
}

# --- the teeth: the fixture must yield exactly 6 findings (the floor) ---------
cat > "$tmp/old.c" <<'EOF'
    long timeout_ms;   /* a BOUND, not a clock reading -- never flagged */
    long interval_ms;  /* a BOUND, not a clock reading -- never flagged */
    t->last_ms = (long)jc_now_millis();
    t->last_ms = (long)jc_now_millis();
    t->launch_ms = (long)jc_now_millis();
    s_pool_now_ms = (long)jc_now_millis();
    long now = (long)jc_now_millis();
    app->batch_hb_last_ms = (long)now;
EOF
scan_clock "$tmp/old.c" > "$tmp/found.fixture"
nfixture=$(grep -c . "$tmp/found.fixture" 2>/dev/null || true)
if [ "$nfixture" -eq 6 ]; then
    # The scanner has teeth: it finds the six planted defects and none of the
    # two bound declarations. Proceed to scan the real tree.
    scan_clock "$F1" "$F2" > "$tmp/found"
    n=$(grep -c . "$tmp/found" 2>/dev/null || true)
    if [ "$n" -eq 0 ]; then
        t_ok "the pool holds its monotonic clock in double (no long narrowing; the 6-site universe is wide)"
    else
        t_fail "the pool narrows its monotonic clock to long at $n site(s) (def-379: widen the values to double -- see notes/M1194.md MS3):"
        sed 's/^/# /' "$tmp/found"
    fi
else
    t_fail "lint teeth broken: the fixture yields $nfixture findings (want 6) -- the scan patterns no longer match the defect; the check would pass on a narrowed tree"
fi

t_done
