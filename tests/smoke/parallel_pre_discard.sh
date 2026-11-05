#!/bin/sh
# smoke: a write child's git command that discards work or moves HEAD runs only after
# jichi has kept the worktree's state, and the tool result names it (M863, the demos
# drive's N44 (a)).
#
# THE CASE. Run 0090's write workers discarded their own work with git, against task
# texts that forbade it: worker 2 ran `git checkout <file>` 7 times (discarding its
# uncommitted fix) and `git reset --hard 9b2f3b0`; worker 3 checked out jichi's own
# checkpoint commit twice, and git warned "you are leaving 1 commit behind" -- its own.
# Each wanted to compare its fix with the base, and the quickest way it knew was to take
# the fix out of its tree. M855 keeps what `git stash` takes; nothing kept the rest.
#
# THE FIX (DECISIONS M863; N44's (a), the operator's order of 2026-10-02):
#   - before checkout, switch, restore, reset, clean or rebase in a write child
#     (harmless forms excepted: a new branch, unstaging, a dry run), jichi pins the
#     worktree's files -- tracked and untracked -- on its HEAD as
#     refs/jichi/discarded/pre-discard/<time>-<pid>-<n>, built in a temporary index, so the
#     command still does exactly what it would have done;
#   - the tool result names the ref and how to use it; the task's part of the batch
#     result lists the refs; nothing is pinned when there is nothing to lose.
#
#   1  a child's `git checkout -- <file>` after an uncommitted edit is told the ref
#   2  and the command still did its job: the file is back as it was
#   3  the ref holds the discarded edit
#   4  `git clean -fd` after making a file: the ref holds the untracked file
#   5  `git checkout HEAD~1` after a commit: the ref holds the committed file
#   6  CONTROL: `git status`, a bare `git reset` (it only unstages), and a checkout
#      with nothing to lose pin nothing and say nothing (`restore --staged`, the other
#      unstaging form, is a git 2.23 verb; the unit test holds it)
#   7  the batch result's task sections name the refs
#   8  telemetry records the kept states (pre_discard)
#
# Markers: file contents are printed by arithmetic; the checks read the shadow repo's
# objects, the requests jichi sent, and telemetry.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)

WS=$(smoke_tmp)
printf 'orig-%s\n' "$((6*7))" > "$WS/seed.txt"
CAP=$(smoke_tmp)
cat > "$CAP/r.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDA edit, then checkout","write":true},{"task":"CHILDB make a file, then clean","write":true},{"task":"CHILDC commit, then move HEAD","write":true},{"task":"CHILDD look around","write":true}]}
rule
  match "CHILDA"
  match "A-STEP-3"
  text A-DONE
rule
  match "CHILDA"
  match "A-STEP-2"
  tool run_terminal_command {"command":"cat seed.txt; echo A-STEP-$((1+2))"}
rule
  match "CHILDA"
  match "\"role\":\"tool\""
  tool run_terminal_command {"command":"git checkout -- seed.txt; echo A-STEP-$((1+1))"}
rule
  match "CHILDA"
  tool run_terminal_command {"command":"printf 'edited-%s\\n' $((7*11)) > seed.txt"}
rule
  match "CHILDB"
  match "B-STEP-2"
  text B-DONE
rule
  match "CHILDB"
  match "\"role\":\"tool\""
  tool run_terminal_command {"command":"git clean -fd; echo B-STEP-$((1+1))"}
rule
  match "CHILDB"
  tool run_terminal_command {"command":"printf 'made-%s\\n' $((8*11)) > made.txt"}
rule
  match "CHILDC"
  match "C-STEP-2"
  text C-DONE
rule
  match "CHILDC"
  match "C-STEP-1"
  tool run_terminal_command {"command":"git checkout -q HEAD~1; echo C-STEP-$((1+1))"}
rule
  match "CHILDC"
  tool run_terminal_command {"command":"printf 'committed-%s\\n' $((9*11)) > c.txt && git add c.txt && git -c user.name=c -c user.email=c@c commit -q -m c-work; echo C-STEP-$((0+1))"}
rule
  match "CHILDD"
  match "D-STEP-2"
  text D-DONE
rule
  match "CHILDD"
  match "D-STEP-1"
  tool run_terminal_command {"command":"git checkout -- seed.txt; echo D-STEP-$((1+1))"}
rule
  match "CHILDD"
  tool run_terminal_command {"command":"git status --short; git reset; echo D-STEP-$((0+1))"}
EOF
mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
cat > "$CAP/cfg.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"maxSubagentIters":8}
EOF
TEL="$CAP/tel.jsonl"
( cd "$WS" && with_deadline 180 "$BIN" --config "$CAP/cfg.json" -q --auto \
    --log "$TEL" --log-level full -p "LEADX run the batch" < /dev/null > "$CAP/out" 2>&1 )
mm_stop

# the request a child sent after a given step, by its output marker
after() { grep -l -F "$1" "$CAP"/cap/req.* 2>/dev/null | head -1; }
shadow=$(find "$HOME/.jichi.d" -maxdepth 3 -name HEAD -path '*checkpoints*' 2>/dev/null | head -1)
shadow="${shadow%/HEAD}"
kept() {  # <ref> <path> -> the file as the ref kept it
    git --git-dir "$shadow" show "$1:$2" 2>/dev/null
}

RA=$(after 'A-STEP-2')
refa=$(grep -o 'refs/jichi/discarded/pre-discard/[0-9-]*' "${RA:-/dev/null}" 2>/dev/null | head -1)
if [ -n "$refa" ] && grep -q -F 'Before this command, jichi kept your worktree' "$RA"; then
    t_ok "a checkout after an uncommitted edit is told the ref it was kept under"
else
    t_fail "no kept ref in the checkout's result (red before M863)"
fi
RA3=$(after 'A-STEP-3')
if [ -n "$RA3" ] && grep -q -F 'orig-42' "$RA3"; then
    t_ok "and the checkout still did its job: the file is back as it was"
else
    t_fail "the checkout did not restore the file (a keep that commits in the tree would)"
fi
if [ -n "$refa" ] && [ "$(kept "$refa" seed.txt)" = "edited-77" ]; then
    t_ok "the ref holds the discarded edit"
else
    t_fail "the ref does not hold the edit: '$(kept "${refa:-none}" seed.txt)' (shadow=${shadow:-none})"
fi
RB=$(after 'B-STEP-2')
refb=$(grep -o 'refs/jichi/discarded/pre-discard/[0-9-]*' "${RB:-/dev/null}" 2>/dev/null | head -1)
if [ -n "$refb" ] && [ "$(kept "$refb" made.txt)" = "made-88" ]; then
    t_ok "git clean -fd: the ref holds the untracked file"
else
    t_fail "the clean's ref (${refb:-none}) does not hold made.txt"
fi
RC=$(after 'C-STEP-2')
refc=$(grep -o 'refs/jichi/discarded/pre-discard/[0-9-]*' "${RC:-/dev/null}" 2>/dev/null | head -1)
if [ -n "$refc" ] && [ "$(kept "$refc" c.txt)" = "committed-99" ]; then
    t_ok "git checkout HEAD~1 after a commit: the ref holds the committed file"
else
    t_fail "the move's ref (${refc:-none}) does not hold c.txt"
fi
RD=$(after 'D-STEP-2')
if [ -n "$RD" ] && ! grep -q -F 'kept your worktree' "$RD"; then
    t_ok "CONTROL: status, a bare reset, and a checkout with nothing to lose pin nothing"
else
    t_fail "the control child was told of a kept state, or did not finish"
fi
LR=$(after '## Parallel agents')
n=$(grep -o 'its worktree was kept before each: refs/jichi/discarded/pre-discard/' "${LR:-/dev/null}" 2>/dev/null | wc -l | tr -d ' ')
if [ "$n" = "3" ]; then
    t_ok "the batch result's task sections name the refs (3 tasks)"
else
    t_fail "the batch result names kept refs for $n task(s), want 3"
fi
k=$(grep '"event":"pre_discard"' "$TEL" 2>/dev/null | grep -c '"kept":"yes"')
if [ "$k" -ge 3 ]; then
    t_ok "telemetry records the kept states ($k)"
else
    t_fail "telemetry: $k pre_discard kept"
fi

t_done
