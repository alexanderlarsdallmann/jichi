#!/bin/sh
# smoke: a reply cut off at the output ceiling is seen, recorded, and never taken for a
# finished answer (M858, the demos drive's N42).
#
# THE DEFECT. In run 0090 a card-214 write worker's last reply came back at 16,384 output
# tokens -- jichi's own ceiling, a fifth of contextLength capped -- with no tool call and
# its text cut mid-sentence ("... Let me check if"). That text became its answer, the
# batch reported "done", and nothing said it was cut: a parallel child's stop reason never
# read M334's truncation flag. And measured on the institution's gateway, a cut TOOL-CALL
# reply ends finish_reason "tool_calls" (completion_tokens equal to max_tokens), so
# M334's cut-argument message never fired there: args_truncated 0 in 90 runs, while
# M148's generic repair ran 830 times.
#
# THE FIX.
#   (b) the provider marks a reply cut when its completion_tokens reach the max_tokens
#       the request sent, whatever its finish_reason says;
#   (c) telemetry's model_call row carries "truncated": true;
#   (a) a write child whose turn ended on a cut reply is M854's fourth ending: a note in
#       its answer, and its work merges only after a green worktree verify.
#
#   1  a write child whose last reply was cut: the batch result says so, with the ceiling
#   2  with no verifier, its work is kept, not merged
#   3  with a green worktree verify, its work merges
#   4  a cut tool call (finish_reason tool_calls, completion_tokens == maxTokens) gets
#      M334's message -- its arguments are incomplete -- not M148's
#   5  telemetry: that reply's model_call row says truncated, and args_truncated is counted
#   6  CONTROL: the same malformed arguments one token under the ceiling get M148's
#      answer, not M334's -- the usage check does not mark every reply
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)

cfg() {  # $1 = extra JSON members (with a leading comma, or empty); $2 = model extras
    cat <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]$2}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0$1}
EOF
}

# run <mm> <extra-config> <model-extras> <prompt> -> WS, CAP, OUT, TEL
run() {
    WS=$(smoke_tmp)
    printf 'seed\n' > "$WS/seed.txt"
    CAP=$(smoke_tmp)
    sed -e "s#@TMP@#$tmp#g" "$1" > "$CAP/r.mm"
    mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
    cfg "$2" "$3" > "$CAP/cfg.json"
    OUT="$CAP/out"
    TEL="$CAP/tel.jsonl"
    ( cd "$WS" && with_deadline 120 "$BIN" --config "$CAP/cfg.json" -q --auto \
        --log "$TEL" --log-level full -p "$4" < /dev/null > "$OUT" 2>&1 )
    mm_stop
}
lead_result() { grep -l '## Parallel agents' "$CAP"/cap/req.* 2>/dev/null | head -1; }

# --- 1-3: a write child's last reply is cut ----------------------------------------
cat > "$tmp/cut.sse" <<'EOF'
data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","content":"I see! There is a helper at index 87, but I do not see where it is used. Let me check if"},"finish_reason":null}]}

data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{},"finish_reason":"length"}]}

data: [DONE]

EOF
cat > "$tmp/cut.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDC write, then talk too long","write":true}]}
rule
  match "CHILDC"
  match "\"role\":\"tool\""
  sse-file @TMP@/cut.sse
rule
  match "CHILDC"
  tool write_file {"path":"cut.txt","content":"c\n"}
EOF
run "$tmp/cut.mm" "" "" "LEADX run the batch"
LR=$(lead_result)
if [ -n "$LR" ] && grep -q 'had its last reply cut off at the output ceiling' "$LR" &&
   grep -q 'the ceiling: [0-9]* output tokens' "$LR"; then
    t_ok "a write child whose last reply was cut: the batch result says so, with the ceiling"
else
    t_fail "no cut note in the batch result (red before M858: a clean 'done'):
 $(head_bytes 500 "${LR:-/dev/null}" 2>/dev/null)"
fi
if [ ! -f "$WS/cut.txt" ] && [ -n "$LR" ] && grep -q 'parallel-unverified' "$LR"; then
    t_ok "with no verifier the cut child's work is kept, not merged"
else
    t_fail "the cut child's work merged with no verify (red before M858): $(ls "$WS")"
fi
run "$tmp/cut.mm" ',"parallelVerify":true,"verify":"test -f cut.txt"' "" "LEADX run the batch"
if [ -f "$WS/cut.txt" ]; then
    t_ok "with a green worktree verify the cut child's work merges"
else
    t_fail "the cut child's work did not merge after a green verify: $(ls "$WS")"
fi

# --- 4-6: a cut tool call, as the gateway reports it --------------------------------
# maxTokens 64; the mock's tool reply carries arguments severed mid-string and a usage of
# 64 completion tokens with finish_reason "tool_calls" -- the measured shape. The lead
# answers with whatever its next request carries.
cat > "$tmp/cuttool.mm" <<'EOF'
wire openai
rule
  match "LEADT"
  match "\"role\":\"tool\""
  text LEAD-SAW-RESULT
rule
  match "LEADT"
  usage 20 64
  tool write_file {"path":"big.txt","content":"aaaaaaaaaaaaaaaa
EOF
run "$tmp/cuttool.mm" "" ',"maxTokens":64' "LEADT write a big file"
FU=$(grep -l '"role":"tool"' "$CAP"/cap/req.* 2>/dev/null | head -1)
if [ -n "$FU" ] && grep -q 'cut off at the model.s output-token limit' "$FU" &&
   ! grep -q 'could not parse tool arguments' "$FU"; then
    t_ok "a cut tool call gets M334's message (incomplete), not M148's (malformed)"
else
    t_fail "the cut tool call was not recognised as cut (red before M858: the gateway's
 finish_reason is tool_calls): $(tail -c 300 "${FU:-/dev/null}" 2>/dev/null)"
fi
if grep '"event":"model_call"' "$TEL" 2>/dev/null | grep -q '"truncated":true' &&
   grep -q '"event":"args_truncated"' "$TEL" 2>/dev/null; then
    t_ok "telemetry: the reply's model_call row says truncated, and args_truncated is counted"
else
    t_fail "telemetry does not record the cut: model_call truncated=$(grep -c '"truncated":true' "$TEL" 2>/dev/null) args_truncated=$(grep -c '"event":"args_truncated"' "$TEL" 2>/dev/null)"
fi

sed -e 's/usage 20 64/usage 20 63/' "$tmp/cuttool.mm" > "$tmp/under.mm"
run "$tmp/under.mm" "" ',"maxTokens":64' "LEADT write a big file"
FU=$(grep -l '"role":"tool"' "$CAP"/cap/req.* 2>/dev/null | head -1)
if [ -n "$FU" ] && ! grep -q 'cut off at the model.s output-token limit' "$FU"; then
    t_ok "CONTROL: one token under the ceiling the same arguments are not called cut"
else
    t_fail "a reply under the ceiling was marked cut -- the usage check is too wide"
fi

t_done
