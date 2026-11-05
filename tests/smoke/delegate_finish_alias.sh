#!/bin/sh
# smoke: a delegate that calls a `finish` tool ends its turn with its message (M853,
# the demos drive's N32).
#
# THE DEFECT. jichi has no `finish` tool: a turn ends with a reply that makes no tool
# call. Models call one anyway. The demos drive's children called `finish` 203 times
# in six runs, each carrying a finished answer -- {"task_completed": "true",
# "message": "Card 204 completed successfully. ..."} -- and each got "error: unknown
# tool 'finish'". The worker then ran `echo "Done"` until something stopped it, and a
# stopped worker's committed work does not merge (N33): run 0087's card-204 worker
# made about 430 calls after its commit that way, and was stopped at 606.
#
# THE FIX (the handoff's proposal (a), narrowed; DECISIONS M853):
#   - in a delegate (depth >= 1), an UNREGISTERED tool named finish, done, complete,
#     submit, task_complete or final_answer, whose arguments carry a non-blank
#     `message` string, ends the turn, and the message is the answer;
#   - a call with no message, and any such call at the top level, gets an error that
#     says how a turn ends: reply with the final message as plain text, no tool call.
#
#   1  a write child that calls finish {message} answers with the message
#   2  and sends no request after that call (its turn ended there)
#   3  and its file merges after a green worktree verify
#   4  the telemetry records a finish_alias event for the call
#   5  a delegate's unknown tool by any other name still gets today's error, and its
#      turn goes on
#   6  a delegate's finish with no message gets the error that names the plain-text
#      ending, and its turn goes on
#   7  at the top level, finish {message} does not end the turn: it gets that error,
#      and the lead answers with its next reply
#   8  spawn_subagent (the in-process delegate) ends the same way: the lead's tool
#      result carries the message
#
# Every marker a check greps reaches the request it is found in only through jichi's
# OUTPUT: a child's answer in the lead's batch result, or jichi's error text in a
# follow-up request. The mock's own replies that spell a marker (WENT-ON-...) are sent
# only in answer to a follow-up request, so they appear only if the turn went on.
#
# Checks 1-4's child does what the drive's workers did when refused: it calls `finish`
# again, every time (run 0087's worker called it five times, then looped on `echo`).
# A first draft let the mock child reply in text after the error -- a recovery no
# measured worker made -- and check 3 stayed green with the fix removed, because a
# child that answers in text merges anyway. maxSubagentIters 6 bounds the loop when
# the fix is missing.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)

cfg() {  # $1 = extra JSON members (with a leading comma, or empty)
    cat <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0$1}
EOF
}

# run_lead <mm-file> <extra-config> <prompt> -> WS, CAP (captures), OUT, TEL
run_lead() {
    WS=$(smoke_tmp)
    printf 'seed\n' > "$WS/seed.txt"
    CAP=$(smoke_tmp)
    cp "$1" "$CAP/r.mm"
    mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
    cfg "$2" > "$CAP/cfg.json"
    OUT="$CAP/out"
    TEL="$CAP/tel.jsonl"
    ( cd "$WS" && with_deadline 120 "$BIN" --config "$CAP/cfg.json" -q --auto \
        --log "$TEL" --log-level full -p "$3" < /dev/null > "$OUT" 2>&1 )
    mm_stop
}

# the lead's follow-up request: the one that carries the batch result
lead_result() { grep -l '## Parallel agents' "$CAP"/cap/req.* 2>/dev/null | head -1; }
# requests a delegate sent: they carry its task marker and not the lead's
delegate_reqs() { grep -l "$1" "$CAP"/cap/req.* 2>/dev/null | xargs grep -L "$2" 2>/dev/null; }

# --- 1-4: a write child ends with finish {message} -------------------------------
cat > "$tmp/fin.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDF write a file, then finish","write":true}]}
rule
  match "CHILDF"
  match "\"role\":\"tool\""
  tool finish {"task_completed":"true","message":"done: the file is written"}
rule
  match "CHILDF"
  tool write_file {"path":"fin.txt","content":"fin\n"}
EOF
run_lead "$tmp/fin.mm" ',"parallelVerify":true,"verify":"test -f fin.txt","maxSubagentIters":6' \
    "LEADX run the batch"
LR=$(lead_result)
if [ -n "$LR" ] && grep -q 'done: the file is written' "$LR"; then
    t_ok "a write child that calls finish {message} answers with the message"
else
    t_fail "the child's answer is not its finish message (red before M853: 'without a
 result', or the next reply): $(head_bytes 500 "${LR:-/dev/null}" 2>/dev/null)"
fi
n=$(delegate_reqs CHILDF LEADX | wc -l | tr -d ' ')
if [ "$n" = "2" ]; then
    t_ok "the child sent no request after its finish call (2 requests: the task, the write's result)"
else
    t_fail "the child sent $n requests, expected 2 -- its turn did not end at finish"
fi
if [ -f "$WS/fin.txt" ]; then
    t_ok "the child's file merged after a green worktree verify"
else
    t_fail "the child's file did not merge (no answer, no merge -- N33): $(ls "$WS")"
fi
if grep '"event":"finish_alias"' "$TEL" 2>/dev/null | grep -q '"name":"finish"'; then
    t_ok "the telemetry records a finish_alias event for the call"
else
    t_fail "no finish_alias event in the telemetry: $(grep -c . "$TEL" 2>/dev/null) lines"
fi

# --- 5: any other unknown name: today's error, and the turn goes on ---------------
cat > "$tmp/other.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDO call a tool that does not exist"}]}
rule
  match "CHILDO"
  match "\"role\":\"tool\""
  text WENT-ON-AFTER-OTHER
rule
  match "CHILDO"
  tool frobnicate {"message":"not an ending"}
EOF
run_lead "$tmp/other.mm" "" "LEADX run the batch"
LR=$(lead_result)
FU=$(delegate_reqs CHILDO LEADX | xargs grep -l '"role":"tool"' 2>/dev/null | head -1)
if [ -n "$FU" ] && grep -q "unknown tool 'frobnicate'" "$FU" &&
   [ -n "$LR" ] && grep -q 'WENT-ON-AFTER-OTHER' "$LR"; then
    t_ok "another unknown name still gets today's error, and the delegate's turn goes on"
else
    t_fail "an unknown tool by another name ended the turn, or lost its error:
 $(head_bytes 400 "${FU:-/dev/null}" 2>/dev/null)"
fi

# --- 6: finish with no message: the error that names the ending -------------------
cat > "$tmp/nomsg.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDN finish without a message"}]}
rule
  match "CHILDN"
  match "\"role\":\"tool\""
  text WENT-ON-AFTER-NOMSG
rule
  match "CHILDN"
  tool finish {"task_completed":"true"}
EOF
run_lead "$tmp/nomsg.mm" "" "LEADX run the batch"
LR=$(lead_result)
FU=$(delegate_reqs CHILDN LEADX | xargs grep -l '"role":"tool"' 2>/dev/null | head -1)
if [ -n "$FU" ] && grep -q "There is no 'finish' tool" "$FU" &&
   grep -q 'make no tool call' "$FU" &&
   [ -n "$LR" ] && grep -q 'WENT-ON-AFTER-NOMSG' "$LR"; then
    t_ok "finish with no message gets the error that names the plain-text ending"
else
    t_fail "finish without a message did not get the ending's error, or ended the turn:
 $(head_bytes 400 "${FU:-/dev/null}" 2>/dev/null)"
fi

# --- 7: the top level: the error, never an ending ---------------------------------
cat > "$tmp/top.mm" <<'EOF'
wire openai
rule
  match "LEADT"
  match "\"role\":\"tool\""
  text WENT-ON-AT-TOP
rule
  match "LEADT"
  tool finish {"task_completed":"true","message":"top: not the answer"}
EOF
run_lead "$tmp/top.mm" "" "LEADT answer"
FU=$(grep -l '"role":"tool"' "$CAP"/cap/req.* 2>/dev/null | head -1)
if [ -n "$FU" ] && grep -q "There is no 'finish' tool" "$FU" &&
   grep -q 'WENT-ON-AT-TOP' "$OUT"; then
    t_ok "at the top level finish gets the error, and the lead answers with its next reply"
else
    t_fail "the top level ended on finish, or lost the error: $(head_bytes 300 "$OUT")"
fi

# --- 8: spawn_subagent ends the same way -------------------------------------------
cat > "$tmp/sub.mm" <<'EOF'
wire openai
rule
  match "LEADS"
  match "\"role\":\"tool\""
  text LEAD-DONE
rule
  match "LEADS"
  tool spawn_subagent {"task":"SUBF finish with a message"}
rule
  match "SUBF"
  match "\"role\":\"tool\""
  text WENT-ON-AFTER-SUB
rule
  match "SUBF"
  tool finish {"message":"sub: the answer is 42"}
EOF
run_lead "$tmp/sub.mm" "" "LEADS delegate"
LF=$(grep -l 'LEADS' "$CAP"/cap/req.* 2>/dev/null | xargs grep -l '"role":"tool"' 2>/dev/null | head -1)
if [ -n "$LF" ] && grep -q 'sub: the answer is 42' "$LF" &&
   ! grep -q 'WENT-ON-AFTER-SUB' "$LF"; then
    t_ok "spawn_subagent's finish {message} ends its turn: the lead's tool result carries it"
else
    t_fail "spawn_subagent's answer is not its finish message: $(head_bytes 400 "${LF:-/dev/null}" 2>/dev/null)"
fi

t_done
