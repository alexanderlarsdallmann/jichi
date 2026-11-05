#!/bin/sh
# M803 (P1, test T5): enumerate every code path that destroys a worktree, so a new
# one cannot be added that skips preservation. Two independent routes, floored at
# today's counts (audit the universe twice -- CLAUDE.md):
#
#   A. Callers of jc_snapshot_worktree_remove -- the preserving chokepoint. Its
#      signature REQUIRES a `why`, so every caller preserves unless it passes
#      JC_WT_MERGED. Today: 6 in src (improve, attempt, workflow, the parallel pool's
#      teardown, M812 salvage -- which preserves an orphan as "parallel-orphaned" --
#      and M875's metric-gate base worktree, which passes JC_WT_MERGED: a pristine
#      checkout of the batch's base, measured and dropped, holds no work to keep)
#      + 1 in tests (test_snapshot, which passes JC_WT_MERGED) = 7.
#   B. Every `rm -rf` argv in src/. Hand-classified today (5):
#        - src/snapshot/jc_snapshot.c  the <2.17 worktree-remove fallback, INSIDE
#          jc_snapshot_worktree_remove and AFTER preservation -- safe.
#        - src/main.c (prune_worktrees) deletes a STALE worktree left by a dead run.
#          It does NOT yet preserve (no snapshot manager in scope there); this is a
#          documented P1 follow-on ("the fifth site", plan SS4.1). Reason "pruned".
#        - src/main.c (a named dir under a root) -- NOT a worktree.
#        - src/util/jc_toolout.c x2 -- tool-output cleanup, NOT a worktree.
#
# A new call site or a new `rm -rf` trips this lint: preserve first, then bump the
# floor here with the reasoning.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"

# A. the chokepoint's callers (src + tests), excluding its own definition line.
# find -exec grep, not `grep -r --include`: BSD grep silently ignores --include
# (posix_utils_lint check 11), which would make this read the wrong file set.
CALLS=$(find src tests -name '*.c' -exec grep -n 'jc_snapshot_worktree_remove(' {} + 2>/dev/null \
        | grep -v 'jc_status jc_snapshot_worktree_remove')
callers=$(printf '%s\n' "$CALLS" | grep -c .)
echo "# jc_snapshot_worktree_remove call sites (src + tests):"
printf '%s\n' "$CALLS" | grep . | sed 's/^/#   /'

# instrument: the chokepoint (and its definition) still exists, or "0 callers"
# would be a vacuous pass.
grep -q 'jc_status jc_snapshot_worktree_remove' src/snapshot/jc_snapshot.c \
    && t_ok "instrument: the preserving chokepoint jc_snapshot_worktree_remove exists" \
    || t_fail "instrument: jc_snapshot_worktree_remove is gone -- the extraction below is meaningless"

[ "$callers" = 7 ] && t_ok "exactly 7 worktree_remove call sites (6 in src + 1 in tests), all through the chokepoint" \
    || t_fail "worktree_remove call sites = $callers, floor 7 -- a new destroyer? confirm it preserves (or passes JC_WT_MERGED), then update the floor"

# B. every rm -rf argv in src/.
RMRF=$(find src -name '*.c' -exec grep -nE '"-rf"' {} + 2>/dev/null)
rmrf=$(printf '%s\n' "$RMRF" | grep -c .)
echo "# rm -rf argv sites in src/:"
printf '%s\n' "$RMRF" | grep . | sed 's/^/#   /'
[ "$rmrf" = 5 ] && t_ok "exactly 5 rm -rf argv sites, each hand-classified in this lint's header" \
    || t_fail "rm -rf argv sites = $rmrf, floor 5 -- a new one? confirm it cannot destroy an unpreserved worktree, then update the floor and the header"

# M875: the verdict is the exit code too, not only the TAP count run.sh audits --
# without t_done this lint exited 0 with a check failing.
t_done
