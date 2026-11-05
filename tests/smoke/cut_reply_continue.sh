#!/bin/sh
# smoke: a reply cut at the output ceiling is continued once -- in a delegate by default,
# at a headless top level when configured (M859, the demos drive's N42 (d); the
# operator's choice of 2026-10-02).
#
# THE CASE. Run 0090's card-214 worker made 331 tool calls, and its last reply came back
# at the output ceiling, cut mid-sentence, with no tool call: its turn ended there, with
# work unfinished that one more reply might have finished. M858 made such an ending
# visible (a note, a verify-gated merge); this asks once to continue first.
#
# THE DECISION. `continueCutReply`:
#   "delegates" (the default) -- a delegate is asked once; nobody else can say
#       "continue" to it. The top level is not: in chat a person can, in ACP the editor
#       is the person, and a headless run already reports answer_capped;
#   "headless" -- a headless top-level run is asked once too;
#   "off" -- never.
# A second cut ends the turn; a parallel write child then gets M858's note.
#
#   1  a delegate whose reply is cut is asked once to continue, and its next reply is
#      its answer (no cut note: its final reply was not cut)
#   2  a delegate cut twice is asked only once, and ends with M858's cut note
#   3  the default leaves a headless top level alone: no continuation request
#   4  "headless" asks a headless top level once
#   5  explicit "delegates" restores the old default: a headless top level is not continued
#   6  "off" leaves a delegate alone too: its cut ending stands
#   7  telemetry records the continuation (cut_continue)
#
# Markers: jichi's continuation request ("[jichi] Your last reply was cut off") is
# jichi's own text; the mock's CONT-ANSWER is sent only in reply to that request, so it
# reaches the lead only if the continuation happened.
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
run() {  # <mm> <extra-config> <prompt> -> WS, CAP, OUT, TEL
    WS=$(smoke_tmp)
    printf 'seed\n' > "$WS/seed.txt"
    CAP=$(smoke_tmp)
    sed -e "s#@TMP@#$tmp#g" "$1" > "$CAP/r.mm"
    mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
    cfg "$2" > "$CAP/cfg.json"
    OUT="$CAP/out"
    TEL="$CAP/tel.jsonl"
    ( cd "$WS" && with_deadline 120 "$BIN" --config "$CAP/cfg.json" -q --auto \
        --log "$TEL" --log-level full -p "$3" < /dev/null > "$OUT" 2>&1 )
    mm_stop
}
lead_result() { grep -l '## Parallel agents' "$CAP"/cap/req.* 2>/dev/null | head -1; }
asked() { grep -l 'Your last reply was cut off at the output ceiling' "$CAP"/cap/req.* 2>/dev/null | wc -l | tr -d ' '; }

cat > "$tmp/cut.sse" <<'EOF'
data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","content":"Still working: the next step is to check whether"},"finish_reason":null}]}

data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{},"finish_reason":"length"}]}

data: [DONE]

EOF

# --- 1: a delegate, cut once, continued --------------------------------------------
cat > "$tmp/once.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDO write, run long, then finish","write":true}]}
rule
  match "CHILDO"
  match "Your last reply was cut off"
  text CONT-ANSWER the file is written and checked
rule
  match "CHILDO"
  match "\"role\":\"tool\""
  sse-file @TMP@/cut.sse
rule
  match "CHILDO"
  tool write_file {"path":"once.txt","content":"o\n"}
EOF
run "$tmp/once.mm" "" "LEADX run the batch"
LR=$(lead_result)
n=$(asked)
if [ "$n" = "1" ] && [ -n "$LR" ] && grep -q 'CONT-ANSWER' "$LR" &&
   ! grep -q 'cut off at the output ceiling after' "$LR" && [ -f "$WS/once.txt" ]; then
    t_ok "a delegate cut once is asked once to continue, and its next reply is its answer"
else
    t_fail "the delegate was not continued (asked=$n) or ended with the cut note:
 $(head_bytes 500 "${LR:-/dev/null}" 2>/dev/null)"
fi
if grep -q '"event":"cut_continue"' "$TEL" 2>/dev/null; then
    t_ok "telemetry records the continuation (cut_continue)"
else
    t_fail "no cut_continue event in the telemetry"
fi

# --- 2: a delegate cut twice: asked once, then M858's note -----------------------------
cat > "$tmp/twice.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDT write, then run long twice","write":true}]}
rule
  match "CHILDT"
  match "\"role\":\"tool\""
  sse-file @TMP@/cut.sse
rule
  match "CHILDT"
  match "Your last reply was cut off"
  sse-file @TMP@/cut.sse
rule
  match "CHILDT"
  tool write_file {"path":"twice.txt","content":"t\n"}
EOF
run "$tmp/twice.mm" "" "LEADX run the batch"
LR=$(lead_result)
n=$(asked)
if [ "$n" = "1" ] && [ -n "$LR" ] && grep -q 'had its last reply cut off at the output ceiling' "$LR"; then
    t_ok "a delegate cut twice is asked only once, and ends with M858's cut note"
else
    t_fail "a twice-cut delegate was asked $n time(s), or has no cut note:
 $(head_bytes 400 "${LR:-/dev/null}" 2>/dev/null)"
fi

# --- 3-4: the top level, headless ------------------------------------------------------
cat > "$tmp/top.mm" <<'EOF'
wire openai
rule
  match "LEADT"
  match "Your last reply was cut off"
  text TOP-CONTINUED
rule
  match "LEADT"
  sse-file @TMP@/cut.sse
EOF
run "$tmp/top.mm" "" "LEADT answer at length"
n=$(asked)
if [ "$n" = "1" ] && grep -q 'TOP-CONTINUED' "$OUT"; then
    t_ok "the default asks a headless top level once: the run shows the continuation request"
else
    t_fail "the default did not continue the top level (asked=$n): $(head_bytes 300 "$OUT")"
fi
run "$tmp/top.mm" ',"continueCutReply":"headless"' "LEADT answer at length"
n=$(asked)
if [ "$n" = "1" ] && grep -q 'TOP-CONTINUED' "$OUT"; then
    t_ok "\"headless\" asks a headless top level once"
else
    t_fail "\"headless\" did not continue the top level (asked=$n): $(head_bytes 300 "$OUT")"
fi

# --- 5: explicit "delegates" -----------------------------------------------------------------------------
run "$tmp/top.mm" ',"continueCutReply":"delegates"' "LEADT answer at length"
n=$(asked)
if [ "$n" = "0" ]; then
    t_ok 'explicit "delegates" restores the old default: a headless top level is not continued'
else
    t_fail 'explicit "delegates" still continued the top level (asked=$n)'
fi

# --- 6: off --------------------------------------------------------------------------------
run "$tmp/once.mm" ',"continueCutReply":"off"' "LEADX run the batch"
LR=$(lead_result)
n=$(asked)
if [ "$n" = "0" ] && [ -n "$LR" ] && grep -q 'had its last reply cut off at the output ceiling' "$LR"; then
    t_ok "\"off\" leaves a delegate alone: its cut ending stands"
else
    t_fail "\"off\" still continued (asked=$n), or the cut note is missing"
fi

t_done
