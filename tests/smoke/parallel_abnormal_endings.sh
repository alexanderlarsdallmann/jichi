#!/bin/sh
# smoke: a write child that did not end on its own answers with a note, and its work
# merges only after a green verify (M854, the demos drive's N33 with N24(c)).
#
# THE DEFECT. Only an ANSWERED write child is merged. A child that never wrote a
# reply of its own ends "without a result", and its committed work is only kept:
#   - stopped from outside: run 0087's card-204 worker had committed its fix, looped
#     (N32), and was stopped with SIGTERM at 606 calls; the lead recovered its two
#     commits by hand from the kept ref 45 minutes later (N33);
#   - out of context: four children ended with HTTP 400 ContextWindowExceededError,
#     two of them after committing finished work (N24);
#   - capped: a child that reached maxSubagentIters having only called tools.
#
# THE FIX AND THE DECISIONS (the operator's go, 2026-10-01; DECISIONS M854):
#   - such a child answers with a note -- its last text if it wrote any, and one line
#     saying how it ended -- instead of ending "without a result";
#   - its work merges only after a green worktree verify (parallelVerify and a
#     verifier). With no verify it is KEPT, named in the batch result's Kept list
#     with the commands that recover it. This applies to a capped child that did
#     write text too: before M854 that one merged unverified;
#   - a child the BATCH stopped (the lead aborted, or the task timed out) is not one
#     of these: it is killed with its pipe closed and never reports.
#
#   1  a write child stopped with SIGTERM merges after a green worktree verify
#   2  and its answer is its last text (the done line's `last`) and a note that it
#      was stopped from outside
#   3  with no verifier, a stopped child's work is kept, not merged, and named
#   4  a capped child that only called tools merges after a green verify, with a note
#   5  a capped child that DID write text is kept without a verifier (the changed
#      behaviour: before M854 it merged unverified)
#   6  a child ended by a context-window 400 merges after a green verify, with a note
#   7  CONTROL: a child that answered on its own still merges with no verifier
#
# Markers are printed by jichi's output (the notes, the Kept list, the merged files
# in the live workspace), never spelled in a command the mock sends.
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp=$(smoke_tmp)

cfg() {  # $1 = extra JSON members (with a leading comma, or empty)
    cat <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0$1}
EOF
}

# start_lead <mm-file> <extra-config>: the lead runs in the background -> WS, CAP, OUT, LPID
start_lead() {
    WS=$(smoke_tmp)
    printf 'seed\n' > "$WS/seed.txt"
    CAP=$(smoke_tmp)
    sed -e "s#@CAP@#$CAP#g" -e "s#@TMP@#$tmp#g" "$1" > "$CAP/r.mm"
    mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
    cfg "$2" > "$CAP/cfg.json"
    OUT="$CAP/out"
    ( cd "$WS" && with_deadline 120 "$BIN" --config "$CAP/cfg.json" -q --auto \
        -p "LEADX run the batch" < /dev/null > "$OUT" 2>&1 ) &
    LPID=$!
}
finish_lead() { wait "$LPID" 2>/dev/null; mm_stop; }
run_lead() { start_lead "$1" "$2"; finish_lead; }
lead_result() { grep -l '## Parallel agents' "$CAP"/cap/req.* 2>/dev/null | head -1; }

# stop_child: wait for the child to write its pid, then SIGTERM it -- the drive's
# supervisor's move (run 0087, 20:34:48).
PIDF="$tmp/child.pid"
stop_child() {
    i=0
    while [ ! -s "$PIDF" ] && [ "$i" -lt 300 ]; do
        sleep 0.1 2>/dev/null || sleep 1; i=$((i + 1))
    done
    [ -s "$PIDF" ] && kill -TERM "$(cat "$PIDF")" 2>/dev/null
}

# --- 1-3: stopped from outside ------------------------------------------------------
# The child writes stop.txt; then, in one reply, it says something AND has its shell
# write its own pid (the shell's parent is the jichi child) -- an sse-file, since the
# mock's `text` and `tool` actions are one or the other. The mock then keeps it
# looping, three seconds a request. Its text reaches the lead only through the done
# line's `last` (its run did not end OK, so it has no `answer`).
cat > "$tmp/stop.sse" <<EOF
data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","content":"Working: the file is written, checking it next."},"finish_reason":null}]}

data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"tool_calls":[{"index":0,"id":"c1","type":"function","function":{"name":"run_terminal_command","arguments":"{\"command\":\"echo \$PPID > $PIDF\"}"}}]},"finish_reason":"tool_calls"}]}

data: [DONE]

EOF
cat > "$tmp/stop.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDS write, then loop","write":true}]}
rule
  match "CHILDS"
  match "child.pid"
  delay 3000
  tool run_terminal_command {"command":"echo still-looping"}
rule
  match "CHILDS"
  match "\"role\":\"tool\""
  sse-file @TMP@/stop.sse
rule
  match "CHILDS"
  tool write_file {"path":"stop.txt","content":"stop\n"}
EOF
rm -f "$PIDF"
start_lead "$tmp/stop.mm" ',"parallelVerify":true,"verify":"test -f stop.txt","shellFence":false'
stop_child
finish_lead
LR=$(lead_result)
if [ -f "$WS/stop.txt" ]; then
    t_ok "a write child stopped with SIGTERM merges after a green worktree verify"
else
    t_fail "the stopped child's file did not merge (red before M854: kept, unmerged): $(ls "$WS")"
fi
if [ -n "$LR" ] && grep -q 'was stopped from outside' "$LR" &&
   grep -q 'Working: the file is written' "$LR"; then
    t_ok "its answer is its last text and a note that it was stopped from outside"
else
    t_fail "no stopped note in the batch result: $(head_bytes 500 "${LR:-/dev/null}" 2>/dev/null)"
fi

rm -f "$PIDF"
start_lead "$tmp/stop.mm" ',"shellFence":false'
stop_child
finish_lead
LR=$(lead_result)
if [ ! -f "$WS/stop.txt" ] && [ -n "$LR" ] && grep -q 'parallel-unverified' "$LR" &&
   grep -q 'jichi recover' "$LR"; then
    t_ok "with no verifier a stopped child's work is kept, not merged, and named"
else
    t_fail "a stopped child merged unverified, or was not named as kept:
 merged=$([ -f "$WS/stop.txt" ] && echo yes || echo no) $(head_bytes 500 "${LR:-/dev/null}" 2>/dev/null)"
fi

# --- 4-5: capped -------------------------------------------------------------------
cat > "$tmp/cap.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDC write, then loop to the cap","write":true}]}
rule
  match "CHILDC"
  match "\"role\":\"tool\""
  tool run_terminal_command {"command":"echo cap-loop"}
rule
  match "CHILDC"
  tool write_file {"path":"cap.txt","content":"cap\n"}
EOF
run_lead "$tmp/cap.mm" ',"maxSubagentIters":4,"parallelVerify":true,"verify":"test -f cap.txt"'
LR=$(lead_result)
if [ -f "$WS/cap.txt" ] && [ -n "$LR" ] && grep -q 'stopped at its iteration cap' "$LR"; then
    t_ok "a capped child that only called tools merges after a green verify, with a note"
else
    t_fail "the capped child did not merge, or has no note: merged=$([ -f "$WS/cap.txt" ] && echo yes || echo no)
 $(head_bytes 500 "${LR:-/dev/null}" 2>/dev/null)"
fi

# A capped child that DID write text: its reply carries text and a tool call at once
# (an sse-file, since the mock's `text` and `tool` actions are one or the other).
cat > "$tmp/textcall.sse" <<'EOF'
data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","content":"Progress so far: the file is written."},"finish_reason":null}]}

data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"tool_calls":[{"index":0,"id":"c1","type":"function","function":{"name":"run_terminal_command","arguments":"{\"command\":\"echo cap-loop\"}"}}]},"finish_reason":"tool_calls"}]}

data: [DONE]

EOF
cat > "$tmp/captext.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDT write, say something, loop to the cap","write":true}]}
rule
  match "CHILDT"
  match "\"role\":\"tool\""
  sse-file @TMP@/textcall.sse
rule
  match "CHILDT"
  tool write_file {"path":"captext.txt","content":"t\n"}
EOF
run_lead "$tmp/captext.mm" ',"maxSubagentIters":4'
LR=$(lead_result)
if [ ! -f "$WS/captext.txt" ] && [ -n "$LR" ] && grep -q 'parallel-unverified' "$LR"; then
    t_ok "a capped child that wrote text is kept without a verifier (merged unverified before M854)"
else
    t_fail "a capped child with text merged with no verify: merged=$([ -f "$WS/captext.txt" ] && echo yes || echo no)
 $(head_bytes 500 "${LR:-/dev/null}" 2>/dev/null)"
fi

# --- 6: out of context -------------------------------------------------------------
cat > "$tmp/ovf.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDO write, then overflow","write":true}]}
rule
  match "CHILDO"
  match "\"role\":\"tool\""
  status 400
  body {"error":{"message":"litellm.ContextWindowExceededError: litellm.BadRequestError: the request exceeds the model's maximum context length","type":"invalid_request_error","code":400}}
rule
  match "CHILDO"
  tool write_file {"path":"ovf.txt","content":"ovf\n"}
EOF
run_lead "$tmp/ovf.mm" ',"parallelVerify":true,"verify":"test -f ovf.txt"'
LR=$(lead_result)
if [ -f "$WS/ovf.txt" ] && [ -n "$LR" ] && grep -q 'ran out of context' "$LR"; then
    t_ok "a child ended by a context-window 400 merges after a green verify, with a note"
else
    t_fail "the overflowed child did not merge, or has no note: merged=$([ -f "$WS/ovf.txt" ] && echo yes || echo no)
 $(head_bytes 500 "${LR:-/dev/null}" 2>/dev/null)"
fi

# --- 7: CONTROL -- an answered child is unchanged -----------------------------------
cat > "$tmp/ok.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDK write and answer","write":true}]}
rule
  match "CHILDK"
  match "\"role\":\"tool\""
  text CHILD-ANSWERED
rule
  match "CHILDK"
  tool write_file {"path":"ok.txt","content":"ok\n"}
EOF
run_lead "$tmp/ok.mm" ""
if [ -f "$WS/ok.txt" ]; then
    t_ok "CONTROL: a child that answered on its own still merges with no verifier"
else
    t_fail "an answered child did not merge without a verifier -- the gate reached too far"
fi

t_done
