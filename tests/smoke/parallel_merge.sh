#!/bin/sh
# smoke: spawn_parallel WRITE tasks -> isolated git worktrees -> file-level
# first-wins merge. The parent launches two write:true subtasks; each
# child's first call writes a file in its own worktree (alpha.txt=AAA /
# beta.txt=BBB), each follow-up answers. Both files must land in the
# workspace, an uncommitted file must survive, and no worktree may leak.
# Routing uses mockmodel's `nomatch` to tell a child's first write-call
# (TASK marker, no tool result) from the tool-result calls. Needs a git
# workspace; a sequential mock suffices (the merge calls are quick; M216
# found concurrent accept unneeded).
# (Port of tests/e2e/parallel_merge.py, M216.)
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not on PATH"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# TOOL is the substring that marks any request carrying a tool result. A
# child's FIRST call (write) has the TASK marker but no tool result, so it
# is routed by `match TASK_x` + `nomatch TOOL`; every later call (child
# 2nd, parent 2nd) has the tool result and falls through to "all merged".
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "TASK_A"
  nomatch "\"role\":\"tool\""
  tool write_file {"path":"alpha.txt","content":"AAA"}
rule
  match "TASK_B"
  nomatch "\"role\":\"tool\""
  tool write_file {"path":"beta.txt","content":"BBB"}
rule
  nomatch "TASK_A"
  nomatch "TASK_B"
  nomatch "\"role\":\"tool\""
  tool spawn_parallel {"tasks":[{"task":"TASK_A: create alpha.txt","write":true},{"task":"TASK_B: create beta.txt","write":true}]}
rule
  text all merged
EOF

mm_start_unbounded "$tmp/replies.mm" "$tmp"
cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"toolProfile":"full",
 "maxRetries":0,"maxParallelAgents":2,"maxSubagentIters":3,"maxToolIters":4,
 "parallelTaskTimeout":120,"timeouts":{"stall":120}}
EOF

# a git workspace with one committed seed + one UNCOMMITTED file to preserve
( cd "$ws" && git init -q && git config user.email t@t \
    && git config user.name t \
    && printf 'seed\n' > seed.txt && git add -A && git commit -qm init )
printf 'KEEP\n' > "$ws/keep.txt"

(cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/config.json" \
    -q --no-session --auto --no-stdin -p "delegate two writes" \
    > /dev/null 2>&1)
mm_stop

if [ -f "$ws/alpha.txt" ] && [ "$(cat "$ws/alpha.txt")" = "AAA" ]; then
    t_ok "child A's alpha.txt merged into the workspace (AAA)"
else
    t_fail "alpha.txt missing/wrong: $(cat "$ws/alpha.txt" 2>/dev/null)"
fi
if [ -f "$ws/beta.txt" ] && [ "$(cat "$ws/beta.txt")" = "BBB" ]; then
    t_ok "child B's beta.txt merged into the workspace (BBB)"
else
    t_fail "beta.txt missing/wrong: $(cat "$ws/beta.txt" 2>/dev/null)"
fi
if [ -f "$ws/keep.txt" ] && [ "$(cat "$ws/keep.txt")" = "KEEP" ]; then
    t_ok "the uncommitted keep.txt survived the merge"
else
    t_fail "uncommitted keep.txt was lost"
fi
wt="$HOME/.jichi.d/worktrees"
if [ ! -d "$wt" ] || [ -z "$(ls -A "$wt" 2>/dev/null)" ]; then
    t_ok "no worktree leaked"
else
    t_fail "leaked worktrees: $(ls -A "$wt")"
fi

t_done
