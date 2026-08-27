#!/bin/sh
# smoke: `prune` sweeps stale attempt/improve/workflow worktrees, never a live
# run's (M616, seam C1 of docs/analysis/2026-08-27-the-teaching-seams.md).
#
# THE SEAM. ~/.jichi.d/worktrees/{att-,imp-,wf-}<pid>... is removed only on the
# clean exit path; --keep-worktree, a SIGKILL or a deadline kill leaves the
# tree forever. `checkpoints gc` sweeps shadow repos and never touches
# worktrees/; STATE.md called the directory "rebuilt" and nothing rebuilt or
# removed it. Same growth family as dreams (M611) and the index cache (M612):
# prune now trims it by the same --keep/--older-than selectors. A directory
# whose embedded PID is a LIVE process is never a candidate -- deleting a
# running attempt's sandbox is not retention, whatever its mtime says.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
wtd="$HOME/.jichi.d/worktrees"
mkdir -p "$wtd"

seed() { # name ts
    mkdir -p "$wtd/$1/wt-0"
    printf 'x\n' > "$wtd/$1/wt-0/f.txt"
    touch -t "$2" "$wtd/$1/wt-0/f.txt" "$wtd/$1/wt-0" "$wtd/$1"
}
# Three DEAD-pid trees (beyond any pid_max), oldest -> newest; one LIVE-pid
# tree ($$ = this driver's shell), made the OLDEST of all.
seed att-99999901 202601010101
seed imp-99999902 202601020101
seed wf-99999903-0 202601030101
seed "att-$$" 202512010101

nwt() { ls -d "$wtd"/*/ 2>/dev/null | grep -c . ; }

# --- 1: dry-run reports stale worktrees and deletes none ---------------------------
out=$(with_deadline 20 "$BIN" prune --keep 1 --dry-run < /dev/null 2>&1); rc=$?
if [ "$rc" -eq 0 ] && printf '%s' "$out" | grep -q 'worktree' && [ "$(nwt)" -eq 4 ]; then
    t_ok "dry-run reports worktrees and deletes none (still 4)"
else
    t_fail "dry-run rc=$rc kept=$(nwt): $(printf '%s' "$out" | head_bytes 160)"
fi

# --- 2: --keep 1 removes the older DEAD trees, keeps the newest dead one -----------
out=$(with_deadline 20 "$BIN" prune --keep 1 < /dev/null 2>&1); rc=$?
if [ "$rc" -eq 0 ] && [ -d "$wtd/wf-99999903-0" ] \
   && [ ! -d "$wtd/att-99999901" ] && [ ! -d "$wtd/imp-99999902" ]; then
    t_ok "prune --keep 1 kept the newest dead tree and removed the two older"
else
    t_fail "rc=$rc left: $(ls "$wtd" 2>/dev/null | tr '\n' ' ')"
fi

# --- 3: the LIVE run's tree survives, though it is the oldest ----------------------
if [ -d "$wtd/att-$$" ]; then
    t_ok "the live-pid tree survived (a running attempt's sandbox is never swept)"
else
    t_fail "prune deleted a live run's worktree (att-$$)"
fi

# --- 4: the report counts worktrees distinctly --------------------------------------
if printf '%s' "$out" | grep -q 'worktree(s)'; then
    t_ok "the report counts worktrees distinctly"
else
    t_fail "no worktree count: $(printf '%s' "$out" | head_bytes 160)"
fi

# --- 5: a foreign name in the directory is not touched ------------------------------
mkdir -p "$wtd/par-12345"   # the parallel pool's trees are not in this sweep
touch -t 202001010101 "$wtd/par-12345"
out=$(with_deadline 20 "$BIN" prune --older-than 1d < /dev/null 2>&1)
if [ -d "$wtd/par-12345" ]; then
    t_ok "an unrecognised name is left alone (only att-/imp-/wf- are swept)"
else
    t_fail "prune deleted a directory it does not own the naming of"
fi
t_done
