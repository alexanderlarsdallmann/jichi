#!/bin/sh
# smoke: a spawn_parallel task's own tool-call budget binds, is announced at four-fifths,
# and ends the task as M854's endings do; a child stopped by its share of the run's
# budget is one of those endings too (M861, the demos drive's N28).
#
# THE CASE. The drive's lead wrote "past about 150 tool calls, commit what you have,
# write REPORT.md and end" into every task text and every worker profile. The workers
# did not keep it: in run 0016 one went to 189 calls and another to 302, and in run 0017
# the first worker was at 185 after five minutes. A worker cannot see its own count. The
# only per-child cap was the envelope's slice, 1/ntasks of the run's remainder: with the
# drive's --max-tool-calls 50000, a one-task batch's child got about 49,000. And a child
# stopped by that slice merged unverified whenever it had narrated -- the branch review's
# answer to N28's merge question.
#
# THE FIX (DECISIONS M861):
#   - a per-task `max_tool_calls`, read leniently (a number, or a string of digits); any
#     other value refuses the task;
#   - it binds in the child with or without an envelope, bounded by the envelope's slice;
#   - one note at four-fifths of it, "You have used N of the M tool calls this task
#     allows", only when the task has a budget of its own (M347 keeps the RUN's notice
#     out of a delegate's history);
#   - at it the task ends (stop `task_budget`), and its work merges only after a green
#     worktree verify (M854); so does a child stopped by its slice of the run's budget.
#
#   1  a task with max_tool_calls 10 stops after 10 tool calls, with M854's note naming
#      the budget, and merges after a green verify
#   2  it was told at four-fifths, "You have used 8 of the 10 tool calls", and only once:
#      no request carries the note twice, though two rounds follow it
#   3  telemetry records the note and the stop (task_budget)
#   4  with no verifier its work is kept, not merged, and named
#   5  CONTROL: the same task without the key runs past 10 calls, to its iteration cap,
#      and is never told of a budget
#   6  a key above the task's share of the run's cap is bounded by it: the note names
#      the share (4 of the 5, not of the 20)
#   7  a child stopped by its share of the run's budget, having written text, is kept
#      without a verifier (before M861 it merged unverified)
#   8  that child's report does not say the run's budget is spent: the run had budget
#      left (M437's "do not delegate again" assumed the run's budget)
#   9  a string of digits is read as the budget; a value that is not a number refuses
#      the task
#  10  a delegate the task starts spends its budget, and the note counts those calls:
#      the child's own count leaves them out (spawn_subagent forwards it no callbacks)
#
# Markers: the notes, counts and files are jichi's own output (the batch result, the
# requests it sends, telemetry, the workspace), never spelled in a command the mock sends.
. "$(dirname "$0")/_smoke.sh"

t_plan 10
smoke_home
tmp=$(smoke_tmp)

cfg() {  # $1 = extra JSON members (with a leading comma, or empty)
    cat <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"shellFence":false$1}
EOF
}
run() {  # <mm> <extra-config> [jichi flags...] -> WS, CAP, OUT, TEL
    mm=$1
    extra=$2
    shift 2
    WS=$(smoke_tmp)
    printf 'seed\n' > "$WS/seed.txt"
    CAP=$(smoke_tmp)
    sed -e "s#@TMP@#$tmp#g" "$mm" > "$CAP/r.mm"
    mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
    cfg "$extra" > "$CAP/cfg.json"
    OUT="$CAP/out"
    TEL="$CAP/tel.jsonl"
    ( cd "$WS" && with_deadline 120 "$BIN" --config "$CAP/cfg.json" -q --auto \
        --log "$TEL" --log-level full "$@" -p "LEADX run the batch" < /dev/null \
        > "$OUT" 2>&1 )
    mm_stop
}
lead_result() { grep -l '## Parallel agents' "$CAP"/cap/req.* 2>/dev/null | head -1; }
noted() {  # <phrase> -> how many of the requests jichi sent carry it
    grep -l -F "$1" "$CAP"/cap/req.* 2>/dev/null | wc -l | tr -d ' '
}
most() {  # <phrase> -> the most times one request carries it
    m=0
    for f in "$CAP"/cap/req.*; do
        [ -f "$f" ] || continue
        c=$(grep -o -F "$1" "$f" | wc -l | tr -d ' ')
        [ "$c" -gt "$m" ] && m=$c
    done
    echo "$m"
}

# A child that writes its file, then keeps checking: it never ends on its own. Each check
# prints its own shell's pid, so no two results are alike: M829 stops a child whose same
# call returns the same result again and again, and reports that as its iteration cap.
cat > "$tmp/budget.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDB write, then keep checking","write":true,"max_tool_calls":10}]}
rule
  match "CHILDB"
  match "\"role\":\"tool\""
  tool run_terminal_command {"command":"echo checking-$$"}
rule
  match "CHILDB"
  tool write_file {"path":"budget.txt","content":"b\n"}
EOF

# --- 1-3: the budget binds, is announced, and merges after a green verify ---------------
run "$tmp/budget.mm" ',"parallelVerify":true,"verify":"test -f budget.txt"'
LR=$(lead_result)
if [ -f "$WS/budget.txt" ] && [ -n "$LR" ] &&
   grep -q -F 'reached the max_tool_calls set for it after 10 tool call(s) (max_tool_calls: 10)' "$LR"; then
    t_ok "a task with max_tool_calls 10 stops after 10 calls, with the note, and merges after a green verify"
else
    t_fail "no budget ending after 10 calls, or no merge: merged=$([ -f "$WS/budget.txt" ] && echo yes || echo no)
 $(grep -o 'This task [^.]*' "${LR:-/dev/null}" 2>/dev/null | head -2)"
fi
n=$(noted 'You have used 8 of the 10 tool calls this task allows')
m=$(most 'tool calls this task allows')
if [ "$n" -ge 1 ] && [ "$m" = "1" ]; then
    t_ok "the task was told at four-fifths of its budget, and only once"
else
    t_fail "the four-fifths note: in $n request(s) at call 8; at most $m in one request (want >=1 and 1)"
fi
if grep '"event":"task_budget"' "$TEL" 2>/dev/null | grep -q '"phase":"note"' &&
   grep '"event":"task_budget"' "$TEL" 2>/dev/null | grep -q '"phase":"stop"'; then
    t_ok "telemetry records the note and the stop (task_budget)"
else
    t_fail "no task_budget note and stop in telemetry: $(grep -c task_budget "$TEL" 2>/dev/null)"
fi

# --- 4: no verifier: kept ---------------------------------------------------------------
run "$tmp/budget.mm" ''
LR=$(lead_result)
if [ ! -f "$WS/budget.txt" ] && [ -n "$LR" ] && grep -q 'parallel-unverified' "$LR" &&
   grep -q -F 'reached the max_tool_calls set for it' "$LR"; then
    t_ok "with no verifier, a task stopped at its budget is kept, not merged, and named"
else
    t_fail "a budget-stopped task with no verifier: merged=$([ -f "$WS/budget.txt" ] && echo yes || echo no)
 $(head_bytes 400 "${LR:-/dev/null}" 2>/dev/null)"
fi

# --- 5: CONTROL: no key ------------------------------------------------------------------
sed -e 's/,"max_tool_calls":10//' "$tmp/budget.mm" > "$tmp/nokey.mm"
run "$tmp/nokey.mm" ',"maxSubagentIters":12'
LR=$(lead_result)
calls=$(grep -o 'iteration cap after [0-9]*' "${LR:-/dev/null}" 2>/dev/null | grep -o '[0-9]*$' | head -1)
if [ -n "$calls" ] && [ "$calls" -gt 10 ] && [ "$(noted 'tool calls this task allows')" = "0" ] &&
   ! grep -q -F 'max_tool_calls set for it' "$LR"; then
    t_ok "CONTROL: without the key the task runs past 10 calls ($calls), to its iteration cap, untold"
else
    t_fail "the key-less task: calls=${calls:-?}, notes=$(noted 'tool calls this task allows')"
fi

# --- 6-7: the run's share bounds the key; a share stop is an ending --------------------
# Two tasks under --max-tool-calls 11: the lead's own spawn_parallel is call 1, so the
# remaining 10 are shared, 5 each. CHILDS asks for 20 and writes text with every call;
# CHILDQ answers at once, which leaves the lead room to read the result.
cat > "$tmp/textcall.sse" <<'EOF'
data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","content":"Progress so far: the file is written."},"finish_reason":null}]}

data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"tool_calls":[{"index":0,"id":"c1","type":"function","function":{"name":"run_terminal_command","arguments":"{\"command\":\"echo share-loop\"}"}}]},"finish_reason":"tool_calls"}]}

data: [DONE]

EOF
cat > "$tmp/share.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDS write, say something, keep going","write":true,"max_tool_calls":20},{"task":"CHILDQ answer at once"}]}
rule
  match "CHILDQ"
  text QUICK-ANSWER
rule
  match "CHILDS"
  match "\"role\":\"tool\""
  sse-file @TMP@/textcall.sse
rule
  match "CHILDS"
  tool write_file {"path":"share.txt","content":"s\n"}
EOF
run "$tmp/share.mm" '' --max-tool-calls 11
LR=$(lead_result)
if [ "$(noted 'You have used 4 of the 5 tool calls this task allows')" = "1" ] &&
   [ "$(noted 'of the 20 tool calls')" = "0" ]; then
    t_ok "a key above the task's share of the run's cap is bounded by it (4 of the 5)"
else
    t_fail "the share did not bound the key: 4-of-5 notes=$(noted 'You have used 4 of the 5'), of-20 notes=$(noted 'of the 20 tool calls')"
fi
if [ ! -f "$WS/share.txt" ] && [ -n "$LR" ] &&
   grep -q -F "reached its share of the run's budget" "$LR" && grep -q 'parallel-unverified' "$LR"; then
    t_ok "a child stopped by its share of the run's budget is kept without a verifier"
else
    t_fail "a share-stopped child with text (red before M861: merged unverified): merged=$([ -f "$WS/share.txt" ] && echo yes || echo no)
 $(grep -o 'This task [^.]*' "${LR:-/dev/null}" 2>/dev/null | head -2)"
fi
# The lead had 11 calls and used 6 (its spawn_parallel, CHILDS's 5): the run went on.
if [ -n "$LR" ] && grep -q -F "the run itself has budget left" "$LR" &&
   ! grep -q -F "this RUN's budget is spent" "$LR"; then
    t_ok "the share-stopped child's report says the run has budget left, not that it is spent"
else
    t_fail "the share-stopped child's report: $(grep -o '\[delegate\] [^.]*' "${LR:-/dev/null}" 2>/dev/null | head -3)"
fi

# --- 8: the lenient read, and a refused value ----------------------------------------------
cat > "$tmp/read.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDR write, then keep checking","write":true,"max_tool_calls":"3"},{"task":"CHILDX never runs","write":true,"max_tool_calls":"a few"}]}
rule
  match "CHILDR"
  match "\"role\":\"tool\""
  tool run_terminal_command {"command":"echo still-checking"}
rule
  match "CHILDR"
  tool write_file {"path":"read.txt","content":"r\n"}
EOF
run "$tmp/read.mm" ''
LR=$(lead_result)
if [ -n "$LR" ] && grep -q -F 'after 3 tool call(s) (max_tool_calls: 3)' "$LR" &&
   grep -q -F 'max_tool_calls must be a whole number of tool calls, 1 or more' "$LR"; then
    t_ok "a string of digits is read as the budget; a value that is not a number refuses the task"
else
    t_fail "the lenient read: $(grep -o 'This task [^.]*' "${LR:-/dev/null}" 2>/dev/null | head -2)
 $(grep -o '(error: [^)]*' "${LR:-/dev/null}" 2>/dev/null | head -2)"
fi

# --- 10: a delegate's calls count, and the note says so ---------------------------------
# CHILDD writes, then starts a delegate (GRANDS) that keeps checking. The budget of 6
# runs out inside the delegate: the child itself made 2 calls.
cat > "$tmp/deleg.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDD write, then delegate the checks","write":true,"max_tool_calls":6}]}
rule
  match "CHILDD"
  match "GRANDS"
  tool run_terminal_command {"command":"echo checking-$$"}
rule
  match "CHILDD"
  match "\"role\":\"tool\""
  tool spawn_subagent {"task":"GRANDS keep checking"}
rule
  match "CHILDD"
  tool write_file {"path":"deleg.txt","content":"d\n"}
rule
  match "GRANDS"
  tool run_terminal_command {"command":"echo grand-checking-$$"}
EOF
run "$tmp/deleg.mm" ''
LR=$(lead_result)
if [ -n "$LR" ] &&
   grep -q -F "reached the max_tool_calls set for it after 6 tool call(s), its delegates' included (max_tool_calls: 6)" "$LR"; then
    t_ok "a delegate's calls spend the task's budget, and the note counts them"
else
    t_fail "the delegated budget's note (red before: the child's own 2 calls): $(grep -o 'This task [^.]*' "${LR:-/dev/null}" 2>/dev/null | head -2)"
fi

t_done
