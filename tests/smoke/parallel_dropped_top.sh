#!/bin/sh
# M824 (N7, threadwork's weekend drive): when two write children change the SAME
# file, the first-wins merge keeps one and DROPS the other's change (it is preserved
# in the loser's worktree, but not in the tree). On the drive two workers each
# registered a test in src/root.zig; the second registration was dropped and never
# compiled until the lead happened to notice a line in the merge summary at the
# bottom. So the dropped-change list is now surfaced at the TOP of the result, next
# to Kept -- a drop nobody reads is a drop that loses work.
#
# Both children write reg.txt, so the second conflicts. The aggregated report the
# lead reads back must name the dropped change AND put it before the per-task
# "### Task" sections. Before M824 the drop was only in the "## Merge" line at the
# bottom, so the top-of-report check is the red one.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not on PATH"

t_plan 2
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "TASK_A"
  nomatch "\"role\":\"tool\""
  tool run_terminal_command {"command":"printf 'AAA\\n' >> reg.txt && echo didA"}
rule
  match "TASK_B"
  nomatch "\"role\":\"tool\""
  tool run_terminal_command {"command":"printf 'BBB\\n' >> reg.txt && echo didB"}
rule
  nomatch "TASK_A"
  nomatch "TASK_B"
  nomatch "\"role\":\"tool\""
  tool spawn_parallel {"tasks":[{"task":"TASK_A append to reg","write":true},{"task":"TASK_B append to reg","write":true}]}
rule
  text LEAD_DONE
EOF
mm_start_unbounded "$tmp/replies.mm" "$tmp/cap"
cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"toolProfile":"full",
 "maxRetries":0,"maxParallelAgents":2,"maxSubagentIters":3,"maxToolIters":4,
 "parallelTaskTimeout":120,"timeouts":{"stall":120},"preserveDiscarded":true}
EOF
( cd "$ws" && git init -q && git config user.email t@t && git config user.name t \
    && printf 'seed\n' > reg.txt && git add -A && git commit -qm init )

( cd "$ws" && exec "$BIN" --config "$tmp/config.json" -q --no-session --auto \
    --no-stdin -p "delegate two writers to the same file" > "$tmp/out.txt" 2>&1 )
mm_stop

# The report is the spawn_parallel tool result, fed back to the lead in the request
# after the tool call -- so the captured request carrying it is the ground truth.
rep=$(grep -l 'Dropped' "$tmp"/cap/req.* 2>/dev/null | head -1)
if [ -n "$rep" ] && grep -q 'Dropped 1 change' "$rep"; then
    t_ok "the report names the dropped change (Dropped 1 change)"
else
    t_fail "no 'Dropped 1 change' in any request: $(grep -o 'Dropped [0-9]* change' "$tmp"/cap/req.* 2>/dev/null | head -1)"
fi

# The dropped list must come BEFORE the first per-task section (top of report).
if [ -n "$rep" ] && awk '{ d=index($0,"Dropped"); t=index($0,"### Task 1");
                           if (d>0 && t>0 && d<t) { print "OK"; exit } }' \
                        "$rep" | grep -q OK; then
    t_ok "the dropped list is at the TOP, before the per-task sections"
else
    t_fail "the dropped list is not above '### Task 1' (still buried at the bottom)"
fi
t_done
