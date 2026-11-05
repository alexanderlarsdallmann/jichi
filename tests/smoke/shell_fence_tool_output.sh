#!/bin/sh
# smoke: a fenced write child keeps its claim tickets (M857, the demos drive's N41).
#
# THE DEFECT. M831's Landlock fence binds the whole forked write child, jichi's own
# writes included, and jichi's claim-ticket store -- `$HOME/.jichi.d/tool-output/<pid>`
# (D3, M339), where an over-cap tool output and a mid-turn trim keep the text they cut
# and leave its path in the result -- is outside the worktree. On the demos drive's
# first fenced run (run 0089, 2026-10-02) every mid-turn trim lost its ticket: 29
# "tool output: could not preserve N bytes" warnings from two write children, none on
# the runs before the fence. The fallback is D3's, as designed (no path, the text
# dropped), so the model was told nothing -- and the children that trim mid-turn are
# the ones near their context limit, where a ticket is worth most.
#
# THE FIX. The child creates its store before the fence goes on and the fence grants
# that one directory. Its place is unchanged, so the path the model is given and the
# path fence's read exemption for it are too.
#
#   1  a fenced write child's over-cap output names a claim-ticket path (red before
#      M857: the spill failed under the fence and D3 fell back to no path)
#   2  the ticket is readable: the child's next command reads it back
#   3  CONTROL: the grant is this one directory -- the store's PARENT is still refused
#
# The over-cap path and the mid-turn trim write through the same function into the
# same directory, so one grant covers both; this driver exercises the over-cap one,
# which a mock can trigger exactly (runMaxBytes 200).
. "$(dirname "$0")/_smoke.sh"

probe_home=$(smoke_tmp)
HOME="$probe_home" "$BIN" doctor < /dev/null 2>&1 | grep -q 'shell fence: available' ||
    t_skip "no Landlock on this kernel (shell fence unavailable) -- doctor says so"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
WS=$(smoke_tmp)
printf 'seed\n' > "$WS/seed.txt"

# The child prints 300 lines (about 2.7 KB, over runMaxBytes 200), then reads back the
# ticket the first result named (its path is under the store; the second command finds
# it by its prefix), then tries to write the store's parent.
cat > "$tmp/r.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDT print a lot","write":true}]}
rule
  match "CHILDT"
  match "PARENT-TRY"
  text CHILD-DONE
rule
  match "CHILDT"
  match "READ-BACK"
  tool run_terminal_command {"command":"echo PARENT-TRY; echo x > \"$HOME/.jichi.d/tool-output/escape.txt\" && echo PARENT-WROTE-$((40+2))"}
rule
  match "CHILDT"
  match "\"role\":\"tool\""
  tool run_terminal_command {"command":"echo READ-BACK; f=$(ls \"$HOME\"/.jichi.d/tool-output/*/run-*.txt 2>/dev/null | head -1); grep -c line- \"$f\""}
rule
  match "CHILDT"
  tool run_terminal_command {"command":"i=0; while [ $i -lt 300 ]; do echo line-$i; i=$((i+1)); done"}
EOF
mm_start_unbounded "$tmp/r.mm" "$tmp/cap"
cat > "$tmp/cfg.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"runMaxBytes":200}
EOF
( cd "$WS" && with_deadline 120 "$BIN" --config "$tmp/cfg.json" -q --auto \
    -p "LEADX run the batch" < /dev/null > "$tmp/out" 2>&1 )
mm_stop

# the child's requests, in order: 1 the task; 2 after the big output; 3 after the
# read-back; 4 after the parent try
reqs=$(grep -l 'CHILDT' "$tmp"/cap/req.* 2>/dev/null | xargs grep -L '## Parallel agents' 2>/dev/null)
R2=$(printf '%s\n' "$reqs" | xargs grep -l '"role":"tool"' 2>/dev/null | xargs grep -L 'READ-BACK' 2>/dev/null | head -1)
R3=$(printf '%s\n' "$reqs" | xargs grep -l 'READ-BACK' 2>/dev/null | xargs grep -L 'PARENT-TRY' 2>/dev/null | head -1)
R4=$(printf '%s\n' "$reqs" | xargs grep -l 'PARENT-TRY' 2>/dev/null | head -1)
if [ -n "$R2" ] && grep -q '\.jichi\.d/tool-output/[0-9]*/run-[0-9]*\.txt' "$R2"; then
    t_ok "a fenced write child's over-cap output names a claim-ticket path"
else
    t_fail "no claim-ticket path in the over-cap result (red before M857: the spill was
 refused under the fence): $(tail -c 300 "${R2:-/dev/null}" 2>/dev/null)"
fi
# the read-back counts the lines the ticket holds: all 300 (printed by grep -c)
if [ -n "$R3" ] && grep -q 'READ-BACK\\n300\\n' "$R3"; then
    t_ok "the ticket is readable: it holds the whole output (300 lines)"
else
    t_fail "the child could not read its ticket back: $(tail -c 300 "${R3:-/dev/null}" 2>/dev/null)"
fi
if [ -n "$R4" ] && ! grep -q 'PARENT-WROTE-42' "$R4" &&
   [ ! -e "$HOME/.jichi.d/tool-output/escape.txt" ]; then
    t_ok "CONTROL: the store's parent is still refused -- the grant is one directory"
else
    t_fail "the child wrote the store's parent: the grant is wider than its own directory"
fi

t_done
