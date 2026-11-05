#!/bin/sh
# smoke: a delegate child that ends on its own with reasoning and no answer
# text is asked ONCE for its final answer (M937, the zigodot drive's D3).
#
# THE GAP. The drive's reviewer ended on its own with reasoning and no answer
# text three times (19, 1,181 and 28 output tokens of reasoning). jichi warned
# about it on stderr ("model returned reasoning but no answer text, ending on
# its own") and ACCEPTED the empty review as the child's result: the delegate
# report said "stop=no_answer ... produced no final answer", and a run owed an
# independent review finished the phase without one, and nothing said
# "you are running with the review missing".
#
# THE FIX. A separate cause, kept distinct from M859's cut and M147's prose
# call (the honesty rule): a delegate (`agent_depth >= 1`) whose reply ended on
# its own (NOT cut at the ceiling), spent output on reasoning, and gave no text
# and no tool call is asked once for the final answer. One nudge per run
# (`empty_nudged`): a second reasoning-only reply ends with M167's
# warning, not a loop. M965 (D3, widened from M937): the scope is every
# delegate (`agent_depth >= 1`) AND a headless top-level lead
# (`agent_depth == 0 && headless_run`) -- an INTERACTIVE top level is still out
# of scope (B, rejected on the record): a person can say "continue" to their
# own empty reply.
#
# DETERMINISM. mockmodel replays fixed SSE. The child's reasoning-only reply is
# an sse-file (one reasoning_content delta, finish stop, no text, no tool
# call). The rule that serves the child's RECOVERY reply matches jichi's OWN
# nudge text, so the child's answer reaches the lead only if the nudge fired.
# The lead and the child both carry the spawn's task text ("CHILDR"), so the
# child's rules use `nomatch "LEADV"` (the lead's prompt, which the child is
# never seeded with) to stay apart -- the delegate_report.sh discipline.
#
# CHECKS (each red under its own perturbation -- see the t_fail reasons):
#   1  a delegate that ends on reasoning with no answer is asked once for it,
#      and the nudge's reply is the child's final answer (stop=done)
#   2  the child is asked exactly ONCE -- a second reasoning-only reply ends
#      the child with stop=no_answer and M167's warning (no loop)
#   3  a HEADLESS top level IS asked once (D3, M965): a headless lead that ends
#      on reasoning with no answer gets the nudge once (interactive: B,
#      rejected -- a person can say "continue")
#   4  a DELEGATE whose reply is empty WITHOUT reasoning is still not nudged
#      (the saw_reasoning condition is the teeth, not the scope guard), and
#      M167 warns for it -- the coarser "empty answer" stays its own cause
#   5  telemetry records the nudge (empty_reasoning fired + recovered)
#   6  no ceiling warning for a self-ended reply (finish stop) -- the cut
#      cause stays ordered
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)

# The child's reasoning-only reply: one reasoning delta, finish stop, no text,
# no tool call. finish stop -> not truncated; the provider tracks saw_reasoning.
cat > "$tmp/reason.sse" <<'EOF'
data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","reasoning_content":"Let me think through this..."},"finish_reason":null}]}

data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{},"finish_reason":"stop"},"usage":{"prompt_tokens":12,"completion_tokens":9}}]}

data: [DONE]

EOF

# An empty reply with NO reasoning at all (the M167 shape, check 4).
cat > "$tmp/empty.sse" <<'EOF'
data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{},"finish_reason":"stop"},"usage":{"prompt_tokens":12,"completion_tokens":0}}]}

data: [DONE]

EOF

cfg() {  # $1 = extra JSON members (with a leading comma, or empty)
    cat <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0$1}
EOF
}
run() {  # <mm> <extra-config> <prompt> -> CAP, OUT, ERR, TEL
    WS=$(smoke_tmp)
    printf 'seed\n' > "$WS/seed.txt"
    CAP=$(smoke_tmp)
    sed -e "s#@TMP@#$tmp#g" "$1" > "$CAP/r.mm"
    mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
    cfg "$2" > "$CAP/cfg.json"
    OUT="$CAP/out"
    ERR="$CAP/err"
    TEL="$CAP/tel.jsonl"
    # No `-q`: the checks read M167's once-per-run warning on stderr, and `-q`
    # suppresses it (the nudge itself is jichi's text in the captured request,
    # not stderr, so dropping -q does not change what the nudge checks read).
    ( cd "$WS" && with_deadline 120 "$BIN" --config "$CAP/cfg.json" --no-session \
        --auto --log "$TEL" --log-level full -p "$3" < /dev/null > "$OUT" 2>"$ERR" )
    rc=$?
    mm_stop
}
# The lead's FINAL request: it carries the lead's prompt (LEADV, which the
# child is never seeded with) and the spawn_subagent's tool result (either the
# plain answer on success, or "[delegate] stop=no_answer" when there was none).
lead_final() {
    for f in "$CAP"/cap/req.*; do
        [ -f "$f" ] || continue
        grep -q 'LEADV' "$f" && grep -q '"role":"tool"' "$f" && { echo "$f"; return; }
    done
}
# A request is a nudge request iff it carries jichi's own nudge text.
nudge_reqs() {
    grep -l 'ended your last reply with reasoning' "$CAP"/cap/req.* 2>/dev/null \
        | wc -l | tr -d ' '
}

# --- 1+2: a delegate that ends on reasoning is asked once, and recovers --------
cat > "$tmp/recover.mm" <<'EOF'
wire openai
rule
  match "LEADV"
  match "\"role\":\"tool\""
  text LEAD-DONE
rule
  match "LEADV"
  tool spawn_subagent {"task":"CHILDR review the notes"}
rule
  match "CHILDR"
  nomatch "LEADV"
  match "ended your last reply with reasoning"
  text REASON-ANSWER the review is complete
rule
  match "CHILDR"
  nomatch "LEADV"
  sse-file @TMP@/reason.sse
EOF
run "$tmp/recover.mm" "" "LEADV delegate a review"
LF=$(lead_final)
N=$(nudge_reqs)
if [ "$N" = "1" ] && [ -n "$LF" ] && grep -q 'REASON-ANSWER' "$LF" &&
   [ "$rc" -eq 0 ]; then
    t_ok "a delegate that ends on reasoning with no answer is asked once for it, and the nudge's reply is its final answer"
else
    t_fail "the delegate was not continued for its answer (nudge=$N rc=$rc lr=$(head_bytes 300 "${LF:-/dev/null}" 2>/dev/null))"
fi

# --- 2: a second reasoning-only reply ends the child (M167), no loop -------------
cat > "$tmp/twice.mm" <<'EOF'
wire openai
rule
  match "LEADV"
  match "\"role\":\"tool\""
  text LEAD-DONE2
rule
  match "LEADV"
  tool spawn_subagent {"task":"CHILDT review the notes"}
rule
  match "CHILDT"
  nomatch "LEADV"
  sse-file @TMP@/reason.sse
EOF
run "$tmp/twice.mm" "" "LEADV delegate a review twice"
LF=$(lead_final)
N=$(nudge_reqs)
if [ "$N" = "1" ] && [ -n "$LF" ] && grep -q 'stop=no_answer' "$LF" &&
   grep -q 'no tool call and' "$ERR"; then
    t_ok "the child is asked exactly once -- a second reasoning-only reply ends it (M167 warns), no loop"
else
    t_fail "the twice-reasoning child was asked $N time(s) or ended without M167: lr=$(head_bytes 300 "${LF:-/dev/null}" 2>/dev/null) err=$(tail -c 200 "$ERR")"
fi

# --- 3: a headless top level IS asked once (D3, widened to every delegate) ------
cat > "$tmp/top.mm" <<'EOF'
wire openai
rule
  match "LEADT"
  sse-file @TMP@/reason.sse
EOF
run "$tmp/top.mm" "" "LEADT think hard and answer"
N=$(nudge_reqs)
if [ "$N" = "1" ]; then
    t_ok "a headless top level is asked once: a headless lead that ends on reasoning with no answer gets the nudge once"
else
    t_fail "the headless top level was not nudged (nudge=$N) -- D3 widening failed"
fi

# --- 4: a DELEGATE empty WITHOUT reasoning is still not nudged; M167 warns --------
# A delegate (agent_depth >= 1) whose reply is empty but carried no reasoning:
# the nudge's teeth are the saw_reasoning condition, not the scope guard, so
# this child must NOT be asked -- the "empty answer without reasoning" cause
# stays M167's, not M937's.
cat > "$tmp/empty.mm" <<'EOF'
wire openai
rule
  match "LEADE"
  match "\"role\":\"tool\""
  text LEAD-DONE4
rule
  match "LEADE"
  tool spawn_subagent {"task":"CHILDE: answer briefly"}
rule
  match "CHILDE"
  nomatch "LEADE"
  sse-file @TMP@/empty.sse
EOF
run "$tmp/empty.mm" "" "LEADE answer"
N=$(nudge_reqs)
if [ "$N" = "0" ] && grep -q 'no tool call and' "$ERR"; then
    t_ok "a delegate empty WITHOUT reasoning is still not nudged, and M167 warns for it"
else
    t_fail "the non-reasoning empty delegate was nudged (nudge=$N) or M167 was silent: err=$(tail -c 200 "$ERR")"
fi

# --- 5: telemetry records the nudge ------------------------------------------------
run "$tmp/recover.mm" "" "LEADV delegate a review"
if grep '"event":"empty_reasoning"' "$TEL" 2>/dev/null | grep -q '"phase":"fired"' &&
   grep '"event":"empty_reasoning"' "$TEL" 2>/dev/null | grep -q '"phase":"recovered"'; then
    t_ok "telemetry records the nudge (empty_reasoning fired + recovered)"
else
    t_fail "no empty_reasoning telemetry: $(grep -o '"event":"empty_reasoning"[^}]*}' "$TEL" 2>/dev/null | head -2)"
fi

# --- 6: no ceiling warning for a self-ended reply (finish stop) -------------------
# $ERR is the recover run's stderr (check 5); the child used reason.sse (finish
# stop), so the cause must read "ending on its own", never the output ceiling.
if grep -q 'OUTPUT CEILING' "$ERR" 2>/dev/null || \
   grep -q 'cut at its' "$ERR" 2>/dev/null; then
    t_fail "a self-ended reasoning reply (finish stop) blamed the ceiling -- the cause is not ordered: $(tail -c 200 "$ERR")"
else
    t_ok "no ceiling warning for a self-ended reasoning reply (finish stop) -- the cut cause stays ordered"
fi

t_done
