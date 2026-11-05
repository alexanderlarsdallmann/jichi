#!/bin/sh
# smoke: a child's failing loop is not reset by shell commands that only read
# (M886, the demos drive's N50).
#
# THE DEFECT. M869 stops a child that sends the same refused call nine times; M870's
# amendment reset that count after "an edit, or a shell command that worked (it may
# have changed anything)". Run 0104's probe ran one failing cp-and-run command 15
# times in 5 minutes and was never stopped: between the failures it read code with
# `sed -n` and `grep` through the shell, and every read reset the count.
#
# THE FIX (the operator's decision, 2026-10-03: "by the worktree's content"). While a
# child's failing run is counted, jichi fingerprints the worktree's content before and
# after each shell command, and only a change resets the count. Content, not `git
# status`: a second edit to a file already modified keeps its status line.
#
# HOW IT RUNS. Each of the child's replies carries two calls: the same failing command,
# then a read whose output differs every time (`echo $$`), so neither M829's
# same-answer stop nor M870's novelty window can end the loop -- only N50's count can.
#
#   1  a write child alternating a failing command with a read is stopped as a
#      refused-call loop
#   2  at the ninth failure, far below its iteration cap
#   3  CONTROL: a child whose in-between command appends to an already-modified file
#      each time is not stopped that way -- the content changed, so the count reset
#      (a `git status` test would have stopped it). The command also prints `$$`: one
#      that printed nothing answered the same way every time, and M829's same-answer
#      stop ended the child first (the first version of this check measured that)
#   4  CONTROL: with no git -- a subagent in a plain workspace -- the count resets on
#      every successful shell call, as before M886
#
# The markers come from jichi's output -- the lead's next request, which carries the
# results -- never from a command the mock sends.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "needs git -- the worktree is a git one"

t_plan 4
smoke_home
tmp=$(smoke_tmp)

cfg() {
    cat <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"shellFence":false,
 "maxSubagentIters":30}
EOF
}

# run_lead <mm-file> <prompt> -> WS, CAP (the lead runs to its end)
run_lead() {
    WS=$(smoke_tmp)
    seq_lines 1 40 > "$WS/seed.txt"
    CAP=$(smoke_tmp)
    sed -e "s#@TMP@#$tmp#g" "$1" > "$CAP/r.mm"
    mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
    cfg > "$CAP/cfg.json"
    ( cd "$WS" && with_deadline 300 "$BIN" --config "$CAP/cfg.json" -q --auto \
        -p "$2" < /dev/null > "$CAP/out" 2>&1 )
    mm_stop
}
seq_lines() { i=$1; while [ "$i" -le "$2" ]; do echo "line $i"; i=$((i + 1)); done; }
lead_result() { grep -l "$1" "$CAP"/cap/req.* 2>/dev/null | tail -1; }

# One reply, two calls: the same failing command, then $2 (an SSE file per variant).
two_calls() {  # <file> <second command, JSON-escaped>
    cat > "$1" <<EOF
data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","tool_calls":[{"index":0,"id":"c1","type":"function","function":{"name":"run_terminal_command","arguments":"{\\"command\\":\\"cat no-such-file.txt\\"}"}},{"index":1,"id":"c2","type":"function","function":{"name":"run_terminal_command","arguments":"{\\"command\\":\\"$2\\"}"}}]},"finish_reason":"tool_calls"}]}

data: [DONE]

EOF
}
two_calls "$tmp/read.sse" 'sed -n 1p seed.txt; echo $$'
two_calls "$tmp/edit.sse" 'echo $$ >> seed.txt; echo $$'

batch_mm() {  # <file> <lead marker> <child marker> <sse>
    cat > "$1" <<EOF
wire openai
rule
  match "$2"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "$2"
  tool spawn_parallel {"tasks":[{"task":"$3 loop over the failing check","write":true}]}
rule
  match "$3"
  sse-file @TMP@/$4
EOF
}

# --- 1-2: reads between the failures do not reset the count --------------------------
batch_mm "$tmp/reads.mm" LEADR CHILDR read.sse
run_lead "$tmp/reads.mm" "LEADR run the batch"
LR=$(lead_result '## Parallel agents')
if [ -n "$LR" ] && grep -q 'was stopped for repeating the same refused call' "$LR"; then
    t_ok "a child alternating a failing command with a read is stopped as a refused-call loop"
else
    t_fail "not stopped as a refused-call loop: $(grep -o '\[jichi\] This task[^"]*' "${LR:-/dev/null}" 2>/dev/null | head_bytes 300)"
fi
calls=$(grep -o 'stop=[a-z_]* · [0-9]* tool call' "${LR:-/dev/null}" 2>/dev/null | head -n 1 | grep -o '[0-9][0-9]*')
if [ -n "$calls" ] && [ "$calls" -ge 17 ] && [ "$calls" -le 18 ]; then
    t_ok "it was stopped at the ninth failure ($calls calls), far below its cap"
else
    t_fail "expected 17-18 calls (nine failures), got '${calls:-none}'"
fi

# --- 3: CONTROL -- each in-between command changes an already-modified file ----------
batch_mm "$tmp/edits.mm" LEADE CHILDE edit.sse
run_lead "$tmp/edits.mm" "LEADE run the batch"
LR=$(lead_result '## Parallel agents')
if [ -n "$LR" ] && grep -q 'stopped at its iteration cap' "$LR" &&
   ! grep -q 'repeating the same refused call' "$LR"; then
    t_ok "CONTROL: a change to an already-modified file resets the count (content, not status)"
else
    t_fail "CONTROL: the editing child was not left to its cap: $(grep -o '\[jichi\] This task[^"]*' "${LR:-/dev/null}" 2>/dev/null | head_bytes 300)"
fi

# --- 4: CONTROL -- no git: the count resets on every successful shell call -----------
cat > "$tmp/sub.mm" <<'EOF'
wire openai
rule
  match "LEADS"
  match "[delegate]"
  text LEAD-DONE
rule
  match "LEADS"
  tool spawn_subagent {"task":"CHILDS loop over the failing check"}
rule
  match "CHILDS"
  sse-file @TMP@/read.sse
EOF
run_lead "$tmp/sub.mm" "LEADS run the subtask"
LR=$(lead_result '\[delegate\]')
if [ -n "$LR" ] && grep -q 'stop=max_iters' "$LR" &&
   ! grep -q 'repeated the same call without progress' "$LR"; then
    t_ok "CONTROL: with no git, a read still resets the count, as before M886"
else
    t_fail "CONTROL: the subagent in a plain workspace was not left to its cap: $(grep -o '\[delegate\][^"]*' "${LR:-/dev/null}" 2>/dev/null | head_bytes 300)"
fi

t_done
