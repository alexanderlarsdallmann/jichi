#!/bin/sh
# smoke: a write task with "merge": false is kept and named, never merged (M890, the
# demos drive's N48).
#
# THE DEFECT. A probe's committed change merged whenever it touched no test and moved
# no count: four times in three runs on the night of 2026-10-03. Two were capped
# research probes -- "demos-prober", a profile that "changes no repository file" --
# merging after a green verify: three files, then one of two, a half-applied change no
# worktree ever built. Each was taken out by hand. jichi could not tell a probe from an
# implementer.
#
# THE FIX (the operator's decision, 2026-10-03: "Build `"merge": false`"). A write task
# with `"merge": false` is always kept and named, never merged, at any ending and after
# any verify. The field is read leniently: a model may write "false".
#
#   1  a write task with "merge": false is not merged, after a green verify
#   2  it is kept, as parallel-no-merge
#   3  its note says why
#   4  "merge": "false", a string, is read the same way
#   5  CONTROL: a write task without the field merges after the same verify
#   6  a capped probe with "merge": false is told kept-and-not-merged, not M854's promise
#      of a merge after a green verify, and its file stays out of the tree
#   7  telemetry records each kept task (no_merge_kept), and `jichi telemetry` counts them
#
# The markers come from jichi's effects (the live tree) and its batch result, never
# from a command the mock sends.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "needs git -- a write child runs in a worktree"

t_plan 8
smoke_home
WS=$(smoke_tmp)
printf 'seed\n' > "$WS/seed.txt"
CAP=$(smoke_tmp)

cat > "$CAP/r.mm" <<'EOF'
wire openai
rule
  match "LEADM"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADM"
  tool spawn_parallel {"tasks":[{"task":"CHILDA probe the census","write":true,"merge":false},{"task":"CHILDB probe again","write":true,"merge":"false"},{"task":"CHILDC fix it","write":true},{"task":"CHILDD probe until stopped","write":true,"merge":false,"max_tool_calls":3}]}
rule
  match "CHILDA"
  match "\"role\":\"tool\""
  text DONE-A
rule
  match "CHILDA"
  tool write_file {"path":"a.txt","content":"a\n"}
rule
  match "CHILDB"
  match "\"role\":\"tool\""
  text DONE-B
rule
  match "CHILDB"
  tool write_file {"path":"b.txt","content":"b\n"}
rule
  match "CHILDC"
  match "\"role\":\"tool\""
  text DONE-C
rule
  match "CHILDC"
  tool write_file {"path":"c.txt","content":"c\n"}
rule
  match "CHILDD"
  match "\"role\":\"tool\""
  tool run_terminal_command {"command":"echo probing-$$"}
rule
  match "CHILDD"
  tool write_file {"path":"d.txt","content":"d\n"}
EOF
mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
cat > "$CAP/cfg.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,
 "parallelVerify":true,"verify":"true"}
EOF
TEL="$CAP/tel.jsonl"
( cd "$WS" && with_deadline 120 "$BIN" --config "$CAP/cfg.json" -q --auto \
    --log "$TEL" --log-level full \
    -p "LEADM run the batch" < /dev/null > "$CAP/out" 2>&1 )
mm_stop
LR=$(grep -l '## Parallel agents' "$CAP"/cap/req.* 2>/dev/null | tail -1)

if [ -n "$LR" ] && [ ! -e "$WS/a.txt" ]; then
    t_ok "a write task with \"merge\": false is not merged, after a green verify"
else
    t_fail "the merge:false task's file reached the live tree: $(ls "$WS" | tr '\n' ' ')"
fi
if [ -n "$LR" ] && grep -q 'task 1 (parallel-no-merge)' "$LR"; then
    t_ok "it is kept, as parallel-no-merge"
else
    t_fail "not kept as parallel-no-merge: $(grep -o 'Kept[^"]*' "${LR:-/dev/null}" 2>/dev/null | head_bytes 300)"
fi
if [ -n "$LR" ] && grep -q 'this task was set \\"merge\\": false' "$LR"; then
    t_ok "its note says why"
else
    t_fail "no merge:false note: $(grep -o '### Task 1[^"]*' "${LR:-/dev/null}" 2>/dev/null | head_bytes 300)"
fi
# the store sentence in the no_merge note: "jichi's own attempt store under ... not in this workspace"
# The note appears in the child's followup tool result (the tool result that says why the task was kept)
no_merge_child=$(grep -l 'CHILDA' "$CAP/cap"/req.* 2>/dev/null | xargs grep -l '"role":"tool"' 2>/dev/null | head -1)
no_merge_store=$(grep -h 'jichi.*attempt store.*not in this workspace' "$no_merge_child" 2>/dev/null | grep -c .)
[ "$no_merge_store" -ge 1 ] \
    && t_ok "the no_merge note says the ref lives in jichi's attempt store (not the workspace)" \
    || t_fail "the no_merge note gives no store location hint: $(tail -c 500 "$no_merge_child" 2>/dev/null)"
if [ -n "$LR" ] && [ ! -e "$WS/b.txt" ] && grep -q 'task 2 (parallel-no-merge)' "$LR"; then
    t_ok "\"merge\": \"false\", a string, is read the same way"
else
    t_fail "the string \"false\" merged: $(ls "$WS" | tr '\n' ' ')"
fi
if [ -f "$WS/c.txt" ]; then
    t_ok "CONTROL: a write task without the field merges after the same verify"
else
    t_fail "CONTROL: the plain write task did not merge: $(ls "$WS" | tr '\n' ' ')"
fi
if [ -n "$LR" ] && [ ! -e "$WS/d.txt" ] &&
   grep -q -F 'its work is kept and not merged (\"merge\": false)' "$LR"; then
    t_ok "a capped probe with \"merge\": false is told kept-and-not-merged, and its file stays out"
else
    t_fail "the capped probe: d.txt $([ -e "$WS/d.txt" ] && echo merged || echo absent); $(grep -o 'Task 4[^#]*' "${LR:-/dev/null}" 2>/dev/null | head_bytes 300)"
fi
nk=$(grep -c '"event":"no_merge_kept"' "$TEL" 2>/dev/null)
if [ "$nk" = "3" ] &&
   "$BIN" telemetry "$TEL" < /dev/null 2>&1 |
       grep -q -F 'Kept, not merged, as asked ("merge": false, parallel children): 3'; then
    t_ok "telemetry records each kept task, and jichi telemetry counts them"
else
    t_fail "no_merge_kept events: $nk (expected 3, tasks 1, 2 and 4)"
fi

t_done
