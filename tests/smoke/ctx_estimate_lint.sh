#!/bin/sh
# smoke: every context THRESHOLD evaluates one quantity, via one function (M536).
#
# THE DEFECT. Four call sites computed "how full is the context" by hand as
#
#     (estimate_tokens(hist) + nonhist_est(app)) * calibration(app)
#
# and their comments state the invariant out loud -- "the same quantity the
# compaction trigger evaluates (M286), so the two thresholds are comparable and
# 75% really does come before 80%". So the rule was written down three times and
# enforced nowhere. The one reader that got it wrong was the TUI's ctx% badge,
# which passed only the history: it under-read by the whole system prompt plus
# every tool schema, sat in its grey band while the 80% trigger was firing, and
# disagreed with the /context line printed beside it. An instrument that reads
# comfortable while the machine acts is worse than no instrument.
#
# THE UNIVERSE THIS COVERS, and where it stops. Grep for the two halves across
# src/ and every hit falls into one of three groups:
#
#   1. THRESHOLDS -- the badge, routing escalate (75%), routing de-escalate
#      (55%), and the two compaction triggers (80%). All five must call
#      jc_compact_effective_est. Checks 1-3.
#   2. BREAKDOWNS -- /context's `history` row and telemetry's hist_tok, which
#      report the history component ALONE and are right to. Not thresholds, so
#      not covered; check 2 pins the badge specifically so the two lines of
#      /context output cannot drift apart again.
#   3. TRIM-LOOP STOP CONDITIONS -- four sites inside the elision loops using
#      `estimate_tokens(hist) + SYS_TOOLS_OVERHEAD`, i.e. the FLAT 2000 rather
#      than the measured value, and uncalibrated. These are a genuine open
#      question, deliberately NOT changed here: they compare an uncalibrated sum
#      against `target_est`, which is computed in calibrated terms, so the loops
#      stop earlier than the target implies and under-trim. Fixing that changes
#      how much a mid-turn compaction elides -- a behaviour change needing its
#      own measurement, not a drive-by at the end of a milestone about honesty.
#      Check 4 FLOORS THE COUNT AT 4 so the open question cannot quietly grow a
#      fifth member while nobody is looking. See ROADMAP M536.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
G=/usr/bin/grep
[ -x "$G" ] || G=grep

# ---- 1: nobody sums the two halves by hand ---------------------------------
# jc_compact_nonhist_est may appear in exactly two places in src/: its own
# definition, and jc_compact_effective_est's use of it. A third is a hand-rolled
# threshold -- the defect, returning.
# `-c`, NOT `-rc`. The file is named explicitly, so `-r` was doing nothing on
# GNU grep -- and on FreeBSD's grep it does one thing: `-r` implies `-H`, so the
# count came back as `src/chat/jc_compact.c:2` and the comparison against "2"
# failed on a tree where nothing was wrong. `tr -d ' '` strips spaces, not a
# path. Found by the 2026-09-18 FreeBSD row, the same text-tool family as
# illumos's `grep -o`: the portability defect is in the LINT, not the code.
n=$("$G" -c 'jc_compact_nonhist_est' src/chat/jc_compact.c | tr -d ' ')
# `find | xargs grep`, NOT `grep -r --include` (2026-09-19, OpenBSD). --include
# is a GNU extension; OpenBSD's grep does not have it, so the filter was silently
# ignored and the recursive scan walked the BUILT TREE -- this check failed there
# naming `src/chat/jc_compact.o`, an object file, as a source that uses the
# symbol. A filter nothing applies is worse than no filter: it made a
# portability defect look like a design violation.
others=$(find src -name '*.c' -type f 2>/dev/null | xargs "$G" -l 'jc_compact_nonhist_est' 2>/dev/null \
         | "$G" -v 'jc_compact\.c' | tr '\n' ' ')
if [ "$n" = "2" ] && [ -z "$others" ]; then
    t_ok "nonhist_est is used only by its definition and effective_est"
else
    t_fail "nonhist_est appears $n times in jc_compact.c (want 2) and in: \
${others:-nothing} -- a threshold is summing the halves by hand again"
fi

# ---- 2: the TUI badge uses the shared function -----------------------------
# Named explicitly because this is the site that was wrong, and because it is
# the only one of the five a human ever looks at.
if "$G" -q 'used = jc_compact_effective_est(app, hist)' src/tui/jc_tui.c; then
    t_ok "the ctx% badge evaluates the same quantity as the trigger"
else
    t_fail "the TUI badge no longer calls jc_compact_effective_est: \
$("$G" -n 'long used = ' src/tui/jc_tui.c | head -2 | tr '\n' ' ')"
fi

# ---- 3: all five thresholds go through it ----------------------------------
# The floor is today's exact CALL count, 7: the badge, routing escalate (75%),
# routing de-escalate (55%), the early compaction trigger, the mid-turn trigger,
# the post-elision recompute, and /context's reconciling line. Matching the full
# argument list rather than the bare name keeps prose out of the number -- a
# first cut of this check counted 9 because it was also counting the two comments
# that MENTION the function, which is precisely the "extract more than you meant"
# error M535's drift check made in the other direction.
calls=$(find src -name '*.c' -type f 2>/dev/null \
        | xargs "$G" -c 'jc_compact_effective_est(app, hist)' 2>/dev/null \
        | awk -F: '{s += $2} END {print s + 0}')
if [ "$calls" -ge 7 ]; then
    t_ok "$calls call sites share the estimate (floor 7, comments excluded)"
else
    t_fail "only $calls calls to jc_compact_effective_est (want >= 7) -- \
a threshold stopped using it. Fix the caller, do not lower the floor."
fi

# ---- 4: the flat-constant trim sites stay exactly four --------------------
# Not a fix, a fence around an open question. Four sites compare an
# uncalibrated `estimate + SYS_TOOLS_OVERHEAD` against a calibrated target. A
# fifth means the inconsistency spread; four means it is where M536 left it.
flat=$("$G" -c 'jc_compact_estimate_tokens(hist) + SYS_TOOLS_OVERHEAD' \
       src/chat/jc_compact.c | tr -d ' ')
if [ "$flat" = "4" ]; then
    t_ok "the 4 known flat-constant trim conditions are still exactly 4"
else
    t_fail "flat SYS_TOOLS_OVERHEAD trim conditions: $flat (want 4). More means \
the unit mismatch spread; fewer means someone fixed them -- either way this \
lint's header and ROADMAP M536 need updating with what was measured."
fi

t_done
