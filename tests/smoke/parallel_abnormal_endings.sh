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

t_plan 15
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

# --- M1149: the per-worker stop-task / inject-task / status-batch verbs ----------
# Cells 8-15 start the lead WITH a control socket and drive the new verbs over it
# while a spawn_parallel batch is live (the behaviour list of notes/M1149.md, the
# driver section; the proposal's section 4, cell by cell). Cell 15 is the
# pipe-size-free reachability teeth for the per-worker inject pool guard
# (jc_tool_parallel.c:1596) -- its reachability is now a MEASUREMENT, not a
# reading: the two probes of 2026-11-08 (SV:0837.1, corrected by SV:1138, built
# per SV:1237) perturbed the guard and nothing the guard guards turned red, so
# the guard was reachable in principle but no tooth REACHED IT BY REFUSAL.
# Cell 15 does that: several under-cap notes to a child in one long tool call
# fill the per-task queue past the cap and the guard refuses, on any pipe size.
#
# THE RED IS BY CONSTRUCTION. The verbs do not exist on this tree: the codec's
# verb list is the six (jc_control_verb_list) and the shipped `jichi control`
# client refuses a seventh verb CLIENT-side (it checks jc_control_type_parse
# before connecting), so the cells send the verbs over the raw-socket `sockq`
# client (as control.sh does) and get the SERVER's codec refusal back: a reply
# carrying "unknown command" and the six-verb list. That refusal -- together with
# the ABSENCE of the effect (no "stopped by the supervisor" line, no marker in a
# request, no "batch" object, no "not live" refusal, no "queue is full") -- is
# the t_fail committed here. Wave 2 (the verbs in src/) turns each effect green.
JQ="$SMOKE_TOOLS/jsonq"
SOCKQ="$SMOKE_TOOLS/sockq"

# cs <sock> <json-line> -> reply on stdout (raw socket: one line in, one out)
cs() { printf '%s\n' "$2" | "$SOCKQ" --deadline 30 "$1" 2>&1; }
# cs_status <sock> -> the current status reply (ok:true, or an error)
cs_status() { cs "$1" '{"v":1,"type":"status"}'; }
# cs_abort <sock> -> abort the whole run (a known verb, accepted; unblocks a
# child the mock still holds on a `delay`, so the lead and the mock exit cleanly
# on a cell whose assertion has already been made).
cs_abort() { cs "$1" '{"v":1,"type":"abort"}' > /dev/null 2>&1; }
# ctl_wait_batch <sock> -> block until a status reply says last_tool is
# spawn_parallel (the batch is LIVE). The socket is also served at the boundary
# before spawn_parallel returns, so wait for that specific tool, not just "a
# reply". Echoes the live status.
ctl_wait_batch() {
    _wl_s="$1"
    _wl_r=""
    _wl_i=0
    while [ "$_wl_i" -lt 60 ]; do
        _wl_r=$(cs_status "$_wl_s")
        _wl_lt=$(printf '%s' "$_wl_r" | "$JQ" '.last_tool' 2>/dev/null)
        [ "$_wl_lt" = "spawn_parallel" ] && break
        _wl_i=$((_wl_i + 1))
        sleep 0.5
    done
    printf '%s' "$_wl_r"
}
# start_lead_ctl <mm-file> <extra-config> <sock> [dump-dir]: like start_lead, but
# the lead runs with `--control <sock>` (and, when a dump dir is given,
# `--dump-requests <dir>`, which the forked children inherit COW so each child's
# model requests are witnessed on disk -- the gate's own evidence, per M341).
# Sets WS, CAP, OUT, LPID, SOCK (and DUMP).
start_lead_ctl() {
    WS=$(smoke_tmp)
    printf 'seed\n' > "$WS/seed.txt"
    CAP=$(smoke_tmp)
    sed -e "s#@CAP@#$CAP#g" -e "s#@TMP@#$tmp#g" "$1" > "$CAP/r.mm"
    mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
    cfg "$2" > "$CAP/cfg.json"
    OUT="$CAP/out"; SOCK="$3"; DUMP="${4:-}"
    rm -f "$SOCK"
    ( cd "$WS" && with_deadline 120 "$BIN" --config "$CAP/cfg.json" \
        --control "$SOCK" ${DUMP:+--dump-requests "$DUMP"} -q --auto \
        -p "LEADX run the batch" < /dev/null > "$OUT" 2>&1 ) &
    LPID=$!
}
# finish_lead_ctl: wait the lead, stop the mock, unlink the socket.
finish_lead_ctl() { wait "$LPID" 2>/dev/null; mm_stop; rm -f "${SOCK:-}" 2>/dev/null; }
# wait_sock <path> -> 0 once the unix socket exists (control.sh's idiom).
wait_sock() {
    _ws_i=0
    while [ ! -S "$1" ] && [ "$_ws_i" -lt 80 ]; do sleep 0.5; _ws_i=$((_ws_i + 1)); done
    [ -S "$1" ]
}

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

# ============================================================================
# --- M1149, cells 8-14: the per-worker stop-task / inject-task / status batch ---
#
# Each starts the lead WITH a control socket (start_lead_ctl), waits for the
# socket, waits until a batch is LIVE (status's last_tool == spawn_parallel),
# sends the new verb over the raw socket (cs -- the shipped `jichi control`
# client refuses these verbs CLIENT-side on this tree), and asserts the EFFECT.
# RED BY CONSTRUCTION: the reply is the codec's unknown-command refusal (it
# names the six verbs) and the effect is absent, so every assertion below is
# t_fail. Wave 2 (the verbs in src/) makes them green.
#
# DUMP-REQUESTS NOTE (cell 10/14): the task said to witness the inject with
# `--dump-requests`, but that channel is unusable for a FORK POOL: its file
# counter is a per-process `static int nreq` (jc_toolout.c), so every forked
# child (and the lead) writes req-0001.json over the shared dir. The mock's
# GLOBAL capture (`cap/req.N`, one per served request across all clients) is
# the reliable per-request witness here -- the same instrument `lead_result`
# already uses -- and each child's request is attributable by its task prompt
# (CHILDT1/CHILDT0), which no other request carries. The isolation assertion
# (marker in task 1's next request, in NO other task's request) is preserved.

# --- 8: stop-task 1 (fixture), BY EFFECT ------------------------------------------------
# The JSON lists the read-only task first: argument index 0 = CHILDT1 (answers),
# argument index 1 = CHILDT0 (write, shell). The pool launches in ARGUMENT order
# (M1149-b2 removed the write-first reorder), so the verb targets the write/shell
# task at index 1. That task loops run_terminal_command (a plain `match CHILDT0`
# rule, so it calls the shell on its FIRST request too; max_tool_calls 6 bounds
# the loop to ~6 s, well inside the 120 s deadline). The stop must SIGTERM the
# task at argument index 1 and its report line must name the reason; the task at
# index 0 (CHILDT1) answers normally.
cat > "$tmp/s8.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  nomatch "## Parallel agents"
  tool spawn_parallel {"tasks":[{"task":"CHILDT1 one","write":false},{"task":"CHILDT0 zero","write":true,"max_tool_calls":6}]}
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "CHILDT0"
  delay 1000
  tool run_terminal_command {"command":"echo a-loop"}
rule
  match "CHILDT1"
  text CHILDT1-ANSWER
EOF
start_lead_ctl "$tmp/s8.mm" ',"shellFence":false' "$tmp/s8.sock"
if ! wait_sock "$tmp/s8.sock"; then
    finish_lead_ctl
    t_fail "cell 8: control socket never appeared"
else
    ctl_wait_batch "$tmp/s8.sock" > /dev/null
    REPLY=$(cs "$tmp/s8.sock" '{"v":1,"type":"stop-task","task":1,"reason":"fixture"}')
    finish_lead_ctl
    LR=$(lead_result)
    if grep -q 'stopped by the supervisor (SIGTERM): fixture' "${LR:-/dev/null}" 2>/dev/null \
       && grep -q 'CHILDT1-ANSWER' "${LR:-/dev/null}" 2>/dev/null; then
        t_ok "stop-task 1 SIGTERMs the write task at argument index 1; the report names the reason, the index-0 task is normal"
    else
        t_fail "stop-task 1 had no effect (reply: $(printf '%s' "$REPLY" | head_bytes 200)); the batch report lacks 'stopped by the supervisor (SIGTERM): fixture' or the index-0 task's answer:
 $(head_bytes 500 "${LR:-/dev/null}" 2>/dev/null)"
    fi
fi

# --- 9: stop-task 5, REFUSED BY NAME ------------------------------------------------------
# A two-task batch; stop-task 5 must name task 5 as not live and signal nothing
# (both tasks finish normally). RED: the reply is the unknown-command error.
cat > "$tmp/s9.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  nomatch "## Parallel agents"
  tool spawn_parallel {"tasks":[{"task":"CHILDT1 one","write":false},{"task":"CHILDT0 zero","write":true,"max_tool_calls":2}]}
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "CHILDT0"
  delay 1000
  tool run_terminal_command {"command":"echo a-loop"}
rule
  match "CHILDT1"
  text CHILDT1-ANSWER
EOF
start_lead_ctl "$tmp/s9.mm" ',"shellFence":false' "$tmp/s9.sock"
if ! wait_sock "$tmp/s9.sock"; then
    finish_lead_ctl
    t_fail "cell 9: control socket never appeared"
else
    ctl_wait_batch "$tmp/s9.sock" > /dev/null
    REPLY=$(cs "$tmp/s9.sock" '{"v":1,"type":"stop-task","task":5,"reason":"fixture"}')
    finish_lead_ctl
    LR=$(lead_result)
    # task 0 is the LOOPING shell child (no answer to grep for; its normal
    # ending is the max_tool_calls stop), so "no child is signalled" is
    # asserted NEGATIVELY: the report must name no supervisor stop, and
    # task 1's answer must be present.
    if printf '%s' "$REPLY" | grep -q 'not live' \
       && grep -q 'CHILDT1-ANSWER' "${LR:-/dev/null}" 2>/dev/null \
       && ! grep -q 'stopped by the supervisor' "${LR:-/dev/null}" 2>/dev/null; then
        t_ok "stop-task 5 is refused by name ('not live'); no child is signalled (no supervisor stop in the report), task 1 answers"
    else
        t_fail "stop-task 5 was not refused by name (reply: $(printf '%s' "$REPLY" | head_bytes 200)); both tasks' answers:
 $(head_bytes 400 "${LR:-/dev/null}" 2>/dev/null)"
    fi
fi

# --- 10: inject-task 1 (MARKER) -- the ISOLATION cell -------------------------------------
# Argument index 0 = CHILDT0 (write, shell, loops run_terminal_command),
# index 1 = CHILDT1 (read-only). Task 1 makes a SECOND model call (read_file),
# giving the inject a "next request" to land in. The child folds a queued
# inject only at its OWN tool-round boundary, and the parent hands the bytes
# to the child's pipe at that wake -- so the child's read_file call carries a
# 2 s delay (M1149-b2): the parent's wake fires and writes the M1149-MARKER
# to task 1's pipe BEFORE task 1's read_file boundary folds, and the marker
# lands in task 1's next model request (the CHILDT1-ANSWER one). Attributed by
# REQUEST CONTENT (each child's prompt, which no other request carries): the
# marker must appear in task 1's request and in NO other task's request.
cat > "$tmp/s10.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  nomatch "## Parallel agents"
  tool spawn_parallel {"tasks":[{"task":"CHILDT0 zero","write":true,"max_tool_calls":2},{"task":"CHILDT1 one","max_tool_calls":8}]}
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "CHILDT1"
  match "\"role\":\"tool\""
  text CHILDT1-ANSWER
rule
  match "CHILDT1"
  delay 2000
  tool read_file {"path":"seed.txt"}
rule
  match "CHILDT1"
  match "\"role\":\"tool\""
  text CHILDT1-ANSWER
rule
  match "CHILDT0"
  delay 1000
  tool run_terminal_command {"command":"echo a-loop"}
EOF
start_lead_ctl "$tmp/s10.mm" ',"shellFence":false' "$tmp/s10.sock"
if ! wait_sock "$tmp/s10.sock"; then
    finish_lead_ctl
    t_fail "cell 10: control socket never appeared"
else
    ctl_wait_batch "$tmp/s10.sock" > /dev/null
    REPLY=$(cs "$tmp/s10.sock" '{"v":1,"type":"inject-task","task":1,"text":"M1149-MARKER"}')
    finish_lead_ctl
    T1=0; LEAK=0
    for f in "$CAP"/cap/req.*; do
        [ -f "$f" ] || continue
        grep -q 'M1149-MARKER' "$f" 2>/dev/null || continue
        if grep -q 'CHILDT1' "$f" 2>/dev/null; then T1=$((T1+1)); else LEAK=$((LEAK+1)); fi
    done
    if [ "$T1" -ge 1 ] && [ "$LEAK" -eq 0 ]; then
        t_ok "inject-task 1 lands in task 1's next model request and in NO other task's (isolation)"
    else
        t_fail "inject-task 1 had no effect (reply: $(printf '%s' "$REPLY" | head_bytes 200)); marker in task1-req=$T1, in other-req=$LEAK (want >=1 / 0)"
    fi
fi

# --- 11: inject-task 0 into a READ-ONLY task ----------------------------------------------
# Pins the second-pipe design: a read-only child has no writable temp dir, so the
# temp-dir file channel cannot serve this case. The marker must still reach the
# read-only child's next model request.
cat > "$tmp/s11.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  nomatch "## Parallel agents"
  tool spawn_parallel {"tasks":[{"task":"CHILDR zero read-only","max_tool_calls":8},{"task":"CHILDT1 one","write":false}]}
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "CHILDR"
  match "\"role\":\"tool\""
  text CHILDR-ANSWER
rule
  match "CHILDR"
  delay 2000
  tool read_file {"path":"seed.txt"}
rule
  match "CHILDT1"
  text CHILDT1-ANSWER
EOF
start_lead_ctl "$tmp/s11.mm" ',"shellFence":false' "$tmp/s11.sock"
if ! wait_sock "$tmp/s11.sock"; then
    finish_lead_ctl
    t_fail "cell 11: control socket never appeared"
else
    ctl_wait_batch "$tmp/s11.sock" > /dev/null
    REPLY=$(cs "$tmp/s11.sock" '{"v":1,"type":"inject-task","task":0,"text":"M1149-RO-MARKER"}')
    finish_lead_ctl
    RO=0
    for f in "$CAP"/cap/req.*; do
        [ -f "$f" ] || continue
        grep -q 'M1149-RO-MARKER' "$f" 2>/dev/null && grep -q 'CHILDR' "$f" 2>/dev/null && RO=$((RO+1))
    done
    if [ "$RO" -ge 1 ]; then
        t_ok "inject-task 0 lands in the READ-ONLY child's next model request (the temp-dir channel cannot serve it)"
    else
        t_fail "inject-task 0 did not reach the read-only child (reply: $(printf '%s' "$REPLY" | head_bytes 200)); marker in CHILDR-req=$RO (want >=1)"
    fi
fi

# --- 12: status's batch object -------------------------------------------------------------
# TWO lead runs (M1149-b2; the worker's single lead put a `delay 3000` INSIDE
# the lead's first model call, and there is no control serve point during a
# model call's HTTP wait -- so the first status answer came at the post-spawn
# boundary, already live, and the 'no batch key when idle' half was
# structurally red). One t_ok/t_fail for the whole cell (the plan stays 14);
# the t_fail names which half failed.
# (a) the LIVE half: a lead whose LEADX rule spawns the batch. The batch's
# status is served by the POOL's own 200 ms wake (jc_control_poll_during_call,
# M801) while the pool runs, so `ctl_wait_batch` (last_tool == spawn_parallel,
# which is set before the pool runs) is answered WITH the batch live. Task 0
# (write, shell) carries a 5 s model delay with max_tool_calls 1, so it is
# WEDGED inside one model call across both reads: idle_s (last_ms fixed at
# fork, no progress line yet) grows monotonically 3 s apart. The idle_s
# comparison is a FLOAT compare (dash's `[ -gt ]` rejects decimals), done in
# awk. The reply must carry "batch" and per-task pid.
# (b) the IDLE half: a SECOND lead that NEVER calls spawn_parallel. Its only
# model call answers with a `sleep 3` shell tool, so the lead sits inside a
# shell call -- and M801 (jc_control_poll_during_call, jc_app.c:1090) serves
# the control socket every 200 ms during that tool. No batch is ever live in
# this process (pool_ops NULL), so the reply carries NO "batch" key: the
# byte-identical run-level reply. Deterministic (the sleep window is 3 s, the
# read is taken 2 s in), and no model-call serve point is needed.
cat > "$tmp/s12.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  nomatch "## Parallel agents"
  tool spawn_parallel {"tasks":[{"task":"CHILDT0 zero","write":true,"max_tool_calls":1},{"task":"CHILDT1 one","write":false}]}
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "CHILDT0"
  delay 5000
  tool run_terminal_command {"command":"echo a-loop"}
rule
  match "CHILDT1"
  text CHILDT1-ANSWER
EOF
# (b) the no-batch lead: LEADX answers with a `sleep` shell tool (never
# spawn_parallel), then plain text once the shell result is back.
cat > "$tmp/s12b.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  nomatch "\"role\":\"tool\""
  tool run_terminal_command {"command":"sleep 3"}
rule
  match "LEADX"
  match "\"role\":\"tool\""
  text NO-BATCH-DONE
EOF
HALF=""
# (a) the live half
start_lead_ctl "$tmp/s12.mm" ',"shellFence":false' "$tmp/s12.sock"
if ! wait_sock "$tmp/s12.sock"; then
    finish_lead_ctl
    HALF=live
    LIVE1ERR="control socket never appeared"
else
    ctl_wait_batch "$tmp/s12.sock" > /dev/null
    ST_LIVE1=$(cs_status "$tmp/s12.sock")            # batch live, task 0 mid-delay
    sleep 3
    ST_LIVE2=$(cs_status "$tmp/s12.sock")            # 3 s later: idle_s must have grown
    finish_lead_ctl
    HAS_BATCH=$(printf '%s' "$ST_LIVE1" | grep -c '"batch"' 2>/dev/null)
    LIVE_FIELDS=$(printf '%s' "$ST_LIVE1" | grep -c '"pid"' 2>/dev/null)
    IDLE1=$(printf '%s' "$ST_LIVE1" | "$JQ" '.batch.tasks[0].idle_s' 2>/dev/null)
    IDLE2=$(printf '%s' "$ST_LIVE2" | "$JQ" '.batch.tasks[0].idle_s' 2>/dev/null)
    IDLE_GROW=$(printf '%s %s\n' "${IDLE1:-0}" "${IDLE2:-0}" \
        | awk '{if ($2+0 > $1+0) print 1; else print 0}' 2>/dev/null)
    if [ "$HAS_BATCH" -ge 1 ] 2>/dev/null && [ "$LIVE_FIELDS" -ge 1 ] 2>/dev/null \
       && [ "$IDLE_GROW" = "1" ]; then
        :   # live half green
    else
        HALF=live
        LIVE1ERR="status has no batch object / no per-task pid / idle_s did not grow (live: $(printf '%s' "$ST_LIVE1" | head_bytes 240)); batch-present=$HAS_BATCH pid=$LIVE_FIELDS idle ${IDLE1:-?}->${IDLE2:-?}"
    fi
fi
# (b) the idle half
start_lead_ctl "$tmp/s12b.mm" ',"shellFence":false' "$tmp/s12b.sock"
if ! wait_sock "$tmp/s12b.sock"; then
    finish_lead_ctl
    IDLE1ERR="control socket never appeared"
else
    sleep 2   # into the 3 s sleep window, where M801 serves the socket
    ST_IDLE=$(cs_status "$tmp/s12b.sock")            # no batch is ever live
    finish_lead_ctl
    if [ -z "${ST_IDLE:-}" ]; then
        IDLE1ERR="no status reply (the no-batch lead was not serving the socket in the sleep window)"
    elif printf '%s' "$ST_IDLE" | grep -q '"batch"'; then
        IDLE1ERR="the no-batch lead's status carries a 'batch' key (want the byte-identical run-level reply, no batch key): $(printf '%s' "$ST_IDLE" | head_bytes 160)"
    fi
fi
if [ -n "$HALF" ] || [ -n "${IDLE1ERR:-}" ]; then
    if [ -n "$HALF" ]; then
        t_fail "cell 12 (live half): $LIVE1ERR; (idle half: ${IDLE1ERR:-ok})"
    else
        t_fail "cell 12 (idle half): $IDLE1ERR"
    fi
else
    t_ok "status while a batch is live carries the batch object (per-task pid, and idle_s grows for a wedged child); a lead that never spawns a batch answers with no 'batch' key (byte-identical run-level reply)"
fi

# --- 13: the record cell --------------------------------------------------------------------
# The stop appears in the lead's BATCH REPORT (the part this tree can check). The
# stop and inject are also journaled, but the run id is not discoverable from the
# driver's config, so the journal greps are a TODO (wave 2) and the batch-report
# line is the assertion that stands today.
#
# TODO(wave 2): journal half, once the run id is discoverable --
#   grep -l '"cmd":"stop-task"'  "$HOME/.jichi.d/runs/"*.jsonl | xargs grep -l 'fixture'
#   grep -l '"cmd":"inject-task"' "$HOME/.jichi.d/runs/"*.jsonl | xargs grep -l 'M1149-REC'
cat > "$tmp/s13.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  nomatch "## Parallel agents"
  tool spawn_parallel {"tasks":[{"task":"CHILDT0 zero","write":true,"max_tool_calls":6},{"task":"CHILDT1 one","write":false}]}
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "CHILDT0"
  delay 1000
  tool run_terminal_command {"command":"echo a-loop"}
rule
  match "CHILDT1"
  text CHILDT1-ANSWER
EOF
start_lead_ctl "$tmp/s13.mm" ',"shellFence":false' "$tmp/s13.sock"
if ! wait_sock "$tmp/s13.sock"; then
    finish_lead_ctl
    t_fail "cell 13: control socket never appeared"
else
    ctl_wait_batch "$tmp/s13.sock" > /dev/null
    cs "$tmp/s13.sock" '{"v":1,"type":"stop-task","task":0,"reason":"fixture"}' > /dev/null 2>&1
    cs "$tmp/s13.sock" '{"v":1,"type":"inject-task","task":1,"text":"M1149-REC"}' > /dev/null 2>&1
    finish_lead_ctl
    LR=$(lead_result)
    if grep -q 'stopped by the supervisor (SIGTERM): fixture' "${LR:-/dev/null}" 2>/dev/null; then
        t_ok "the stop-task record: the lead's batch report names task 0's stop with its reason"
    else
        t_fail "no stop-task record in the lead's batch report (want 'stopped by the supervisor (SIGTERM): fixture'):
 $(head_bytes 500 "${LR:-/dev/null}" 2>/dev/null)"
    fi
fi

# --- 14: the per-task queue cap -------------------------------------------------------------
# Argument index 0 = CHILDT0 (write, shell, the run_terminal_command loop,
# max_tool_calls 8), index 1 = CHILDT1 (read-only). The M1045 cap is
# CONTROL_PENDING_MAX = 4 * JC_CONTROL_LINE_MAX = 65536 bytes PER TASK. A
# ~70000-byte inject-task 0 must be REFUSED ('queue is full'); a subsequent
# short inject-task 0 ('M1149-AFTER-CAP') still lands in task 0's request. The
# shipped ctl client cannot carry 70000 bytes, so sockq sends it. The child
# folds a queued inject only at its OWN tool-round boundary, and the parent
# hands the bytes to the child's pipe at that wake -- so task 0's shell call
# carries a 2 s delay (M1149-b2): the parent's wake writes M1149-AFTER-CAP to
# task 0's pipe BEFORE task 0's next boundary folds, and the marker lands in
# task 0's request.
cat > "$tmp/s14.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  nomatch "## Parallel agents"
  tool spawn_parallel {"tasks":[{"task":"CHILDT0 zero","write":true,"max_tool_calls":8},{"task":"CHILDT1 one","write":false}]}
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "CHILDT0"
  delay 2000
  tool run_terminal_command {"command":"echo a-loop"}
rule
  match "CHILDT1"
  text CHILDT1-ANSWER
EOF
BIG=$(head_bytes 70000 < /dev/zero | tr '\0' 'x')
printf '{"v":1,"type":"inject-task","task":0,"text":"%s"}\n' "$BIG" > "$tmp/s14.big"
start_lead_ctl "$tmp/s14.mm" ',"shellFence":false' "$tmp/s14.sock"
if ! wait_sock "$tmp/s14.sock"; then
    finish_lead_ctl
    t_fail "cell 14: control socket never appeared"
else
    ctl_wait_batch "$tmp/s14.sock" > /dev/null
    REPLYBIG=$(cat "$tmp/s14.big" | "$SOCKQ" --deadline 30 "$tmp/s14.sock" 2>&1)
    REPLY=$(cs "$tmp/s14.sock" '{"v":1,"type":"inject-task","task":0,"text":"M1149-AFTER-CAP"}')
    finish_lead_ctl
    # The marker count must NOT reuse $CAP (the mock's directory path, set by
    # start_lead_ctl) -- the wave-2 worker's `CAP=0` clobbered it and the glob
    # below then matched nothing (M1149-b2).
    NMARK=0
    for f in "$CAP"/cap/req.*; do
        [ -f "$f" ] || continue
        grep -q 'M1149-AFTER-CAP' "$f" 2>/dev/null && grep -q 'CHILDT0' "$f" 2>/dev/null && NMARK=$((NMARK+1))
    done
    REFUSED=$(printf '%s' "$REPLYBIG" | grep -c 'queue is full' 2>/dev/null)
    if [ "$REFUSED" -ge 1 ] 2>/dev/null && [ "$NMARK" -ge 1 ]; then
        t_ok "inject-task over the per-task cap is refused ('queue is full'); a subsequent short inject-task still lands in task 0's request"
    else
        t_fail "queue cap not enforced (oversized reply: $(printf '%s' "$REPLYBIG" | head_bytes 200); short reply: $(printf '%s' "$REPLY" | head_bytes 160)); M1149-AFTER-CAP in task0-req=$NMARK (want >=1)"
    fi
fi

# --- 15: the inject pool guard, REACHED BY REFUSAL (the pipe-size-free tooth) ---
# SV:1439's final shape: a LOOPING child (small max_tool_calls, short tool
# calls), not a child wedged in one 90 s tool call. SV:1439 named the two open
# points of the single-read child the fc5610a7 cell had: (1) the child read its
# inject pipe only ONCE, so a note after the first never reached any request and
# the refused-marker check NREF could not fire whether the guard stored the
# refused note or dropped it, and N1 (the first marker lands in the child's
# request) was a READING, never perturbed; (2) the drain's decrement
# (jc_tool_parallel.c:1645) had no tooth -- a child that reads once and ends
# never drains-then-accepts a SECOND note. SV:1439's one shape closes both:
# after the refusal, keep the child REACHING BOUNDARIES until every accepted
# note has folded (the lead keeps feeding the pipe: pool_drain_injects runs on
# every pass of the pool loop, :1873), then send one more after-drain note and
# assert it is ACCEPTED. The guard, the refusal, and the decrement are all in
# the teeth now. def-373 STAYS struck (do not reopen it).
#
# The guard (jc_tool_parallel.c:1596, `if (t->injq_len + add >
# JC_CONTROL_QUEUE_MAX)`) bounds the lead's OWN per-task queue (injq), not the
# pipe: the child reads its inject pipe only at a boundary (jc_control.c:597),
# so notes keep landing in the queue; the pool's wake drains what fits into the
# pipe and keeps the overflow in `t->injq`, counting it in `injq_len` -- on
# any pipe size. Several under-cap ~40,000-byte notes push `injq_len` past
# JC_CONTROL_QUEUE_MAX (65536) and the guard refuses: the refusal is the
# measurement (SV:0837.1, corrected by SV:1138: a green perturbation is a
# reading, not a reachability fact -- the probes perturbed the guard and the
# decrement and turned nothing red, because the single-read child never drained
# then accepted a second note).
#
# THE LOOPING CHILD (the idiom of cells 10 and 14): the child must run SEVERAL
# short tool calls, and a plain `match "CHILDT0"` shell rule (cell 14's) is
# not enough to END the loop -- the mock is first-match-wins and every one of
# the child's requests from the second on carries "role":"tool" (the
# accumulating tool results), so a separate text rule would match request 2
# and the child would answer after ONE call. s15.mm therefore steps the loop
# with one rule per call: call k echoes c15-k, and the rule that issues call
# k+1 matches the result c15-k while nomatch-ing c15-(k+1) (request k+1 is
# the last one containing c15-k that does not yet contain the next marker).
# 6 short calls (2000 ms mock delay + an instant shell), max_tool_calls 8
# (2 spare), then the final rule (match c15-6, the last result) answers
# CHILDT0-ANSWER. Each tool-call end is a boundary: the child reads its
# inject pipe there (its 512-byte-chunk drain) and folds every
# NEWLINE-COMPLETE line as ONE [operator] message into its history
# (jc_control.c:597), and its history ACCUMULATES -- so the child's LAST
# request carries EVERY folded note. That is the "after the refusal, every
# accepted note folds" the single-read child lacked. The 2000 ms step delay
# (the spec's 1000-2000 ms band) spaces the child's boundaries ~2.5 s apart;
# the drain completes within ~2 s of the last accepted note entering the pipe,
# so the poll (below) ends as soon as every accepted marker is witnessed and
# the child then ends on the after-drain note. The lead's 120 s deadline is
# far beyond the whole cell.
#
# THE DRAIN WITNESS (the poll below, SV:1538 point 1) REPLACES the fixed
# `sleep 8`: the poll WAITS ON THE CONDITION, not on a fixed margin -- a fixed
# wait that is too short on a slow host reports a broken decrement (NAFTER
# false-red). The witness: each accepted note is NEWLINE-terminated in the queue
# (the drain writes every queued byte, the newline framing keeps a split note
# whole), and the child's boundary drain (jc_control.c:597) folds only
# NEWLINE-COMPLETE lines, keeping a partial line in its process-static buffer.
# So the LAST accepted note can fold only once EVERY queued byte of it has been
# written to the pipe; and the drain decrements injq_len by the bytes written
# (jc_tool_parallel.c:1645). Hence: every accepted marker appearing in >= 1
# CHILDT0 request proves the queue was fully drained (injq_len = 0) -- the
# after-drain note is then sent into an EMPTY queue, and the mock's
# after-drain rule (match INJ15-AFTERDRAIN -> CHILDT0-ANSWER) ends the child in
# the SAME request that witnessed the fold. The poll's bound is a FIXTURE margin
# (32 iters x 0.5 s = 16 s base), scaled by JC_SMOKE_TIMEOUT_MULT the way
# with_deadline scales a bound (read _smoke.sh:570); wait_sock, ctl_wait_batch
# and this poll are plain-seconds loops, so the scale is applied here -- the
# bound is not a cap on the run. If the bound passes with a marker still
# missing the poll t_fails with ITS OWN message.
#
# THE FENCE (SV:1538 point 2): the child's max_tool_calls is 20 (was 8), well
# above the ~2-3 boundary-drains the ~120000-byte queue needs (a 65536-byte
# pipe drains per pass). If the child hits the fence WITHOUT the after-drain
# marker folded, it ends on the cap and the poll / NAFTER go red on their own
# terms -- the fence bounds the child, not the run.
#
# TERMINATED MARKERS (SV:1538 point 4): the numbered markers END WITH A COLON
# (INJ15-1: .. INJ15-N:) and every grep over markers uses the terminated form,
# so no grep prefix-collides: `grep -q INJ15-1` would also match INJ15-10 ..
# INJ15-19; on a host whose pipe accepts ten or more notes before the refusal a
# later note would stand in for note 1. $ACCEPTED / $REFUSED_MARKER carry the
# terminated strings, so the greps and the displayed counts stay consistent.
# (INJ15-FIRST and INJ15-AFTERDRAIN have no numeric-suffix family -- safe as-is.)
#
# THE WITNESS (the mock's GLOBAL capture, the same instrument as cell 14 and the
# DUMP-REQUESTS NOTE above, NOT --dump-requests): folded notes land in the
# child's requests, $CAP/cap/req.N, attributable by the child's task prompt
# CHILDT0 (the LEADX requests carry none of the INJ15 markers). The cell sends
# ONE SHORT note, INJ15-FIRST (a handful of bytes), FIRST: a write <= PIPE_BUF
# is atomic (POSIX pipe(7)), so it is whole in the pipe on ANY pipe size and
# folds at the child's first boundary -- the deterministic "something accepted"
# (N1, the child's-request proof: INJ15-FIRST in >= 1 CHILDT0 request and in NO
# request without CHILDT0). The REFUSED note never enters the queue at all, so
# its marker is in NO request (NREF, pipe-size-free -- the "queue the note
# before refusing it" shape SV:1439 names, perturbed below). After the drain
# poll (every accepted marker in a CHILDT0 request), an after-drain
# ~40,000-byte note is sent and must be ACCEPTED
# (NAFTER, the :1645 decrement's tooth: with :1645 removed, `injq_len` stays at
# ~120,027 and this note is refused). NEVERY (every accepted marker is in a
# CHILDT0 request) closes the "the child reads its pipe exactly once, only note
# 1 folds" reading SV:1439 flagged: the LOOPING child folds EVERY accepted note
# into its accumulating history, so each accepted marker lands in >= 1 CHILDT0
# request.
#
# THE THREE PERTURBATION TEETH (each reverted before the next; git diff empty on
# src/ at the end):
#   (N1) fold off: jc_control.c:597 `if (0 && app->pool_inject_fd_valid && hist
#       != NULL)` -> no CHILDT0 request carries INJ15-FIRST (and NO request at
#       all does), the child's-request proof.
#   (NREF) store-on-refusal: pool_op_inject's guard branch (:1596) keeps the
#       refusal reply but ALSO enqueues the refused note (append to t->injq,
#       injq_len += add, as the accept path does) -> the refused marker reaches
#       the child's pipe and folds into a CHILDT0 request (NREF > 0).
#   (NAFTER) decrement off: comment out :1645 `t->injq_len -= (jc_size)w;` ->
#       injq_len never drops and the after-drain note is refused (NAFTER = 0).
cat > "$tmp/s15.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  nomatch "## Parallel agents"
  tool spawn_parallel {"tasks":[{"task":"CHILDT0 zero","write":true,"max_tool_calls":20}]}
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
# THE AFTER-DRAIN RULE (SV:1538 point 2): first-match-wins, placed BEFORE the
# c15-* loop rules so it outranks them. Inert until a request carries the folded
# INJ15-AFTERDRAIN note: then it ends the child with CHILDT0-ANSWER in the SAME
# request that witnessed the fold (the child's history accumulates, so that
# request also carries the c15-* results -- this rule wins). max_tool_calls is
# raised to 20 in the spawn below, well above the ~2-3 boundary-drains the queue
# needs, as a FENCE: if the child hits the fence without the marker folded, it
# ends and the poll/NAFTER go red on their own terms.
rule
  match "CHILDT0"
  match "INJ15-AFTERDRAIN"
  text CHILDT0-ANSWER
rule
  match "CHILDT0"
  nomatch "c15-1"
  delay 2000
  tool run_terminal_command {"command":"echo c15-1"}
rule
  match "CHILDT0"
  match "c15-1"
  nomatch "c15-2"
  delay 2000
  tool run_terminal_command {"command":"echo c15-2"}
rule
  match "CHILDT0"
  match "c15-2"
  nomatch "c15-3"
  delay 2000
  tool run_terminal_command {"command":"echo c15-3"}
rule
  match "CHILDT0"
  match "c15-3"
  nomatch "c15-4"
  delay 2000
  tool run_terminal_command {"command":"echo c15-4"}
rule
  match "CHILDT0"
  match "c15-4"
  nomatch "c15-5"
  delay 2000
  tool run_terminal_command {"command":"echo c15-5"}
rule
  match "CHILDT0"
  match "c15-5"
  nomatch "c15-6"
  delay 2000
  tool run_terminal_command {"command":"echo c15-6"}
# FINAL RULE (SV:1538 point 2): the child does NOT end here -- it keeps looping
# on c15-6 (self-loop: request k carries c15-6 and does not yet carry
# c15-7, so this rule matches every request from the 7th on) until the
# after-drain note folds and the AFTERDRAIN rule above ends the child. If the
# after-drain note never folds, the child ends on the max_tool_calls fence (20).
rule
  match "CHILDT0"
  match "c15-6"
  nomatch "c15-7"
  delay 2000
  tool run_terminal_command {"command":"echo c15-6"}
EOF
# (No "timeouts":{"stall":0} in the cell config: the default 30 s model-stall
# watchdog fires only on a FROZEN stream, and the child's longest model call
# is a 2 s delay -- the fc5610a7 90 s shape is gone.)
start_lead_ctl "$tmp/s15.mm" ',"shellFence":false' "$tmp/s15.sock"
if ! wait_sock "$tmp/s15.sock"; then
    finish_lead_ctl
    t_fail "cell 15: control socket never appeared"
else
    # The batch is live with its single looping child (total:1, done:0) --
    # ctl_wait_batch's last_tool == spawn_parallel predicate already says the
    # pool is running; assert the one-child shape too so a mis-shaped batch is
    # caught here, not in the marker count.
    BATCH=$(ctl_wait_batch "$tmp/s15.sock")
    # Send ONE SHORT note (INJ15-FIRST, well under PIPE_BUF) first. A write <=
    # PIPE_BUF is atomic (POSIX pipe(7)), so it lands whole in the pipe on ANY
    # pipe size and folds at the child's first boundary -- the deterministic
    # "something accepted". Sent via sockq exactly like the ~40,000-byte notes.
    FIRST_SHORT="INJ15-FIRST"
    printf '{"v":1,"type":"inject-task","task":0,"text":"%s"}\n' \
        "$FIRST_SHORT" | "$SOCKQ" --deadline 30 "$tmp/s15.sock" > /dev/null 2>&1
    # The send loop: ~40,000-byte notes (INJ15-1..N, cell 14's BIG idiom, each
    # under the single-note parse bound of 65536) until the guard refuses.
    # pool_op_inject drains at once, so on this host (pipe 65536) notes 1,2,3
    # are accepted and note 4 refused -- but the loop DISCOVERS the refusal
    # dynamically, it does not hard-code "4".
    N=1; ACCEPTED=""; REFUSED_N=""; REFUSED_MARKER=""
    REFUSED_REPLY=""; NO_REFUSAL=""
    while :; do
        # SV:1538 point 4: the numbered marker ENDS WITH A COLON (INJ15-1: ..
        # INJ15-N:) so no grep prefix-collides: `grep -q INJ15-1` would also
        # match INJ15-10 .. INJ15-19, and on a host whose pipe accepts ten or
        # more notes before the refusal a later note would stand in for note 1.
        M="INJ15-$N:"
        NOTE="$M$(head_bytes 40000 < /dev/zero | tr '\0' 'x')"
        REPLY=$(printf '{"v":1,"type":"inject-task","task":0,"text":"%s"}\n' \
            "$NOTE" | "$SOCKQ" --deadline 30 "$tmp/s15.sock" 2>&1)
        case $REPLY in
            *"queue is full"*)
                REFUSED_N=$N; REFUSED_MARKER=$M; REFUSED_REPLY=$REPLY; break ;;
        esac
        ACCEPTED="$ACCEPTED $M"
        N=$((N + 1))
        # The pipe-size-free safety bound: far above pipe-max-size + the cap and
        # still no refusal means the queue grew unbounded -- the guard did not
        # fire. That IS the failure this cell exists to measure (probe a's tooth
        # produces exactly this), so t_fail rather than loop forever.
        [ "$N" -gt 40 ] && { NO_REFUSAL=1; break; }
    done
    # THE DRAIN POLL (SV:1538 point 1, REPLACES the fixed `sleep 8`): wait on
    # the WITNESS, not a margin -- the queue is fully drained iff EVERY
    # accepted marker (the terminated form) is in >= 1 CHILDT0 request. The
    # witness argument (jc_tool_parallel.c:1620-1645 + jc_control.c:597): each
    # accepted note is newline-terminated in the queue; the drain writes every
    # queued byte to the pipe (memmove, decrementing injq_len at :1645); the
    # child's boundary fold reads until EAGAIN and folds only newline-complete
    # lines (keeping a partial line in its static buffer). So the LAST accepted
    # note can fold only once EVERY queued byte of it has been written -- and
    # every accepted marker then appearing in a CHILDT0 request (its history
    # accumulates) proves injq_len has been driven to 0. The mock writes the
    # capture files as requests arrive, so the witness is readable from $CAP.
    # The bound is a FIXTURE margin (32 iters x 0.5 s = 16 s base), scaled by
    # JC_SMOKE_TIMEOUT_MULT the way with_deadline scales a bound
    # (_smoke.sh:570); wait_sock / ctl_wait_batch / this poll are plain-seconds
    # loops, so the scale is applied here. It is NOT a cap on the run.
    PDBOUND=${JC_SMOKE_TIMEOUT_MULT:-${JC_E2E_TIMEOUT_MULT:-1}}
    case $PDBOUND in ''|*[!0-9]*) PDBOUND=1 ;; esac
    [ "$PDBOUND" -lt 1 ] 2>/dev/null && PDBOUND=1
    _pd_bound=$((32 * PDBOUND))
    _pd_i=0; POLLFAIL=""
    while [ "$_pd_i" -lt "$_pd_bound" ]; do
        _pd_ok=1; POLLFAIL=""
        for _m in $ACCEPTED; do
            _pd_seen=0
            for f in "$CAP"/cap/req.*; do
                [ -f "$f" ] || continue
                if grep -q 'CHILDT0' "$f" 2>/dev/null &&
                   grep -q "$_m" "$f" 2>/dev/null; then
                    _pd_seen=1; break
                fi
            done
            [ "$_pd_seen" -eq 1 ] || { _pd_ok=0; POLLFAIL="$POLLFAIL $_m"; }
        done
        [ "$_pd_ok" -eq 1 ] && break
        _pd_i=$((_pd_i + 1))
        sleep 0.5
    done
    # THE AFTER-DRAIN NOTE (NAFTER, the :1645 decrement's tooth): sent even if
    # the poll's bound passed (finish_lead_ctl would otherwise wait on the
    # still-running child and the lead's 120 s deadline would fire) -- it must
    # be ACCEPTED when the queue is drained (injq_len = 0 with :1645 alive);
    # with :1645 removed, injq_len stays frozen at ~120,000 (the notes never
    # decremented) and this note's ~40,000 bytes push it far past the 65,536
    # cap: REFUSED. The mock's after-drain rule ends the child in the SAME
    # request that witnesses the fold (max_tool_calls 20 is the fence if the
    # note never folds).
    AFTERDRAIN="INJ15-AFTERDRAIN"
    REPLY=$(printf '{"v":1,"type":"inject-task","task":0,"text":"%s%s"}\n' \
        "$AFTERDRAIN" "$(head_bytes 40000 < /dev/zero | tr '\0' 'x')" \
        | "$SOCKQ" --deadline 30 "$tmp/s15.sock" 2>&1)
    AFTERDRAIN_REFUSED=""
    case $REPLY in
        *"queue is full"*) AFTERDRAIN_REFUSED=1 ;;
    esac
    finish_lead_ctl
    if [ -n "$NO_REFUSAL" ]; then
        t_fail "cell 15: no refusal after 40 notes of ~40000 bytes each (far above pipe-max-size + the cap) -- the guard did not fire (the per-task queue grew unbounded)"
    else
        if [ -n "$BATCH" ]; then
            BT=$(printf '%s' "$BATCH" | "$JQ" '.batch.total' 2>/dev/null)
            BD=$(printf '%s' "$BATCH" | "$JQ" '.batch.done' 2>/dev/null)
        fi
        if [ -n "$POLLFAIL" ]; then
            # SV:1538 point 1: a fixed wait that is too short reports a broken
            # decrement; the poll WAITS on the condition and, if its fixture
            # bound passes with a marker still missing, fails with ITS OWN
            # message -- NOT as a NAFTER failure. (With the fold dead, nothing
            # folds, so this is now the N1 tooth.)
            t_fail "cell 15: the queue did not drain (every accepted marker must be in a CHILDT0 request before the after-drain note; the poll bound is a fixture margin scaled by JC_SMOKE_TIMEOUT_MULT, not a cap on the run); still missing:$POLLFAIL; refusal at note $REFUSED_N, accepted markers$ACCEPTED, after-drain refused=$([ -n "$AFTERDRAIN_REFUSED" ] && echo yes || echo no)"
        else
        N1=0; N1LEAK=0; NREF=0; NAFTER=0; NLEAK=0; C0=0; FOUND=""; MISSING=""
        for f in "$CAP"/cap/req.*; do
            [ -f "$f" ] || continue
            if ! grep -q 'CHILDT0' "$f" 2>/dev/null; then
                # N1: the short first note is in the child's request(s) -- and
                # the child's-request proof: in NO request without CHILDT0
                # (the LEADX files never carry any INJ15 marker).
                grep -q "$FIRST_SHORT" "$f" 2>/dev/null && N1LEAK=$((N1LEAK + 1))
                # NEVERY (leak half): no accepted marker in a non-CHILDT0
                # request.
                for _m in $ACCEPTED; do
                    grep -q "$_m" "$f" 2>/dev/null && NLEAK=$((NLEAK + 1))
                done
            else
                C0=$((C0 + 1))
                grep -q "$FIRST_SHORT" "$f" 2>/dev/null && N1=$((N1 + 1))
                # NAFTER: the after-drain note is in >= 1 CHILDT0 request.
                grep -q "$AFTERDRAIN" "$f" 2>/dev/null && NAFTER=$((NAFTER + 1))
                # NEVERY (presence half): record each accepted marker that is
                # in THIS CHILDT0 request; a marker is present if any one is.
                # (The LEADX files are NOT checked for presence -- a note
                # folds only into the child's accumulating history, never the
                # lead's request.)
                for _m in $ACCEPTED; do
                    grep -q "$_m" "$f" 2>/dev/null && FOUND="$FOUND $_m"
                done
            fi
        done
        for _m in $ACCEPTED; do
            case " $FOUND " in
                *" $_m "*) : ;;
                *) MISSING="$MISSING $_m" ;;
            esac
        done
        # NREF: the refused marker is in NO request file at all (checked over
        # all files above, including the lead's).
        for f in "$CAP"/cap/req.*; do
            [ -f "$f" ] || continue
            grep -q "$REFUSED_MARKER" "$f" 2>/dev/null && NREF=$((NREF + 1))
        done
        if [ -n "$REFUSED_N" ] && [ "$BT" = "1" ] && [ "$BD" = "0" ] &&
           [ "$N1" -ge 1 ] && [ "$N1LEAK" -eq 0 ] && [ -z "$MISSING" ] &&
           [ "$NLEAK" -eq 0 ] && [ "$NREF" -eq 0 ] && [ "$NAFTER" -ge 1 ] &&
           [ -z "$AFTERDRAIN_REFUSED" ]; then
            t_ok "cell 15 (SV:1538/SV:1439): the DRAIN POLL (replacing the fixed sleep 8) waited for every accepted marker to be witnessed in a CHILDT0 request -- the queue is fully drained iff every accepted note is folded (each note is newline-terminated; the drain writes every queued byte, :1645 decrements injq_len, the child's boundary fold reads until EAGAIN and folds only newline-complete lines, so the last accepted note can fold only once every queued byte has been written); refusal at note $REFUSED_N (notes$ACCEPTED accepted, terminated so no grep prefix-collides: INJ15-1 vs INJ15-10..19, $C0 CHILDT0 requests); N1: the short first note INJ15-FIRST (whole in the pipe by PIPE_BUF atomicity) is in the CHILDT0 child's request(s) and in no request without CHILDT0 (the child's-request proof); NEVERY: every accepted marker is in a CHILDT0 request (the looping child folds each accepted note into its accumulating history, which carries into its later requests); NREF: the refused marker INJ15-$REFUSED_N: is in NO request (the guard does not store a refused note -- pipe-size-free); NAFTER: the after-drain note INJ15-AFTERDRAIN is accepted and lands in a CHILDT0 request (the :1645 decrement's tooth -- the drain-then-accept phase) and the mock's after-drain rule ends the child in the SAME request that witnessed the fold; max_tool_calls 20 (was 8) is the FENCE -- well above the ~2-3 boundary-drains the ~120000-byte queue needs, so a child that hits it without the marker folded ends and the poll/NAFTER go red on their own terms; the poll bound is a fixture margin scaled by JC_SMOKE_TIMEOUT_MULT (the wait_sock/ctl_wait_batch idiom), not a cap; the guard at jc_tool_parallel.c:1596 is reachable by several under-cap notes on any pipe size; def-373 retired"
        else
            t_fail "cell 15 (SV:1538/SV:1439): refusal? note=${REFUSED_N:-none}, accepted=$ACCEPTED, after-drain refused=$([ -n "$AFTERDRAIN_REFUSED" ] && echo yes || echo no); N1 INJ15-FIRST in CHILDT0-req=$N1 (want >=1) / in non-CHILDT0-req=$N1LEAK (want 0); NEVERY accepted markers missing from all CHILDT0-req=${MISSING:-none} / in non-CHILDT0-req=$NLEAK (want 0); NREF refused marker in req.*=$NREF (want 0); NAFTER INJ15-AFTERDRAIN in CHILDT0-req=$NAFTER (want >=1); CHILDT0-req total=$C0; batch total=${BT:-?} done=${BD:-?}; first refused reply: $(printf '%s' "$REFUSED_REPLY" | head_bytes 200)"
        fi
        fi
    fi
fi

t_done
