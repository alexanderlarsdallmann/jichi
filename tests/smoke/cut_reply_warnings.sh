#!/bin/sh
# smoke: the two warnings a cut, reasoning-only reply produces name the cut,
# not a cause that did not happen (D2, the zigodot drive 2026-10-11).
#
# THE GAP. Run 0008's stderr, in order: (1) the ceiling warning told the
# operator to "declare a larger contextLength (max_tokens defaults to a fifth
# of it)" -- but the context was 196,608, so the fifth (39,321) was ALREADY
# past the 16,384 cap: the advice was false, and the remedy the drive actually
# needed (continueCutReply) was not named. (2) The cut's next shape -- no tool
# call, no text -- fired the M167 warning: "usually means the request was
# rejected or misread". The cause was the cut; the misread text asserted a
# cause that did not happen. A classifier's else-branch must not be a finding
# (docs/TEST_INTEGRITY.md); a diagnostic's branch must not be one either.
#
# THE FIX. The ceiling warning names the cut and the cap the request actually
# sent (jc_config_effective_max_tokens, the M858 ceiling), and points at the
# two knobs that change the outcome (maxTokens, continueCutReply). The
# M167 site orders the causes: if the previous reply was cut, it says so and
# points at the ceiling warning; only a reply that was NOT cut gets the
# "rejected or misread" text (which stays for its own case -- M166, six
# silent runs).
#
# DETERMINISM. mockmodel replays a fixed SSE: one reasoning_content delta,
# then finish_reason length (no usage -- the M334 path, so truncated is set
# from the finish reason alone). write_config advertises tools, so the
# no-tool-no-text branch is reached; the model returns no tool call and no
# answer text, exactly the cut's shape. continueCutReply defaults to
# "delegates", so at the top level of a headless run nothing continues --
# the turn ends on the warning, which is what this driver reads.
#
# CHECKS (each red under its own perturbation -- see the t_fail reasons):
#   1  the ceiling warning names the output ceiling (it still fires)
#   2  it names the cap the request actually sent (39,321 for the 196,608 context)
#   3  it names continueCutReply, the remedy the drive needed
#   4  the no-tool-no-text warning names the CUT as the cause
#   5  it does NOT assert "rejected or misread" for a cut (the D2 defect)
#   6  CONTROL: a self-terminated reasoning-only reply (finish stop) gets no
#      ceiling warning -- the cause stays ordered, both ways
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# mk_sse <path> <finish_reason> -- one reasoning delta, then the finish.
mk_sse() {
    {
        printf 'data: {"choices":[{"index":0,"delta":'
        printf '{"reasoning_content":"Let me think"},"finish_reason":null}]}\n\n'
        printf 'data: {"choices":[{"index":0,"delta":{},"finish_reason":"%s"}]}' "$2"
        printf '\n\n'
        printf 'data: [DONE]\n\n'
    } > "$1"
}

run_case() {  # run_case <sse> <errfile> -- headless, tools advertised
    cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  sse-file $1
EOF
    mm_start "$tmp/replies.mm" "$tmp"
    write_config "$tmp/config.json" "$MM_PORT"
    (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
        --no-session -p "think" < /dev/null > /dev/null 2>"$2")
    mm_stop
}

mk_sse "$tmp/cut.sse"  length
mk_sse "$tmp/self.sse" stop
run_case "$tmp/cut.sse"  "$tmp/err.cut"
run_case "$tmp/self.sse" "$tmp/err.self"

if ! grep -q "reasoning but no answer text" "$tmp/err.cut"; then
    t_fail "no ceiling warning at all on a length-finished reasoning reply --
the checks below would pass over silence: $(tail -c 200 "$tmp/err.cut")"
    exit
fi
t_ok "the ceiling warning still fires on the cut reply"

# The mock config declares neither maxTokens nor contextLength, so the
# request's cap is JC_DEFAULT_MAX_TOKENS (4096): this pins that the number
# printed is the one SENT, not a guessed one.
if grep -q "cut at its 4096 max_tokens cap" "$tmp/err.cut"; then
    t_ok "and it names the cap the request actually sent (4096, the default)"
elif grep -q "max_tokens cap" "$tmp/err.cut"; then
    t_fail "the ceiling warning names no cap: $(tail -c 300 "$tmp/err.cut")"
else
    t_fail "the ceiling warning does not name the cut (D2's first defect): $(tail -c 300 "$tmp/err.cut")"
fi

if grep -q "continueCutReply" "$tmp/err.cut"; then
    t_ok "and it names the remedy the drive needed (continueCutReply)"
else
    t_fail "the ceiling warning points at no remedy: $(tail -c 300 "$tmp/err.cut")"
fi

if grep -q "previous reply was cut at the output ceiling" "$tmp/err.cut"; then
    t_ok "the no-tool-no-text warning names the CUT as the cause"
else
    t_fail "the no-tool-no-text warning does not order the causes (D2's second defect): $(tail -c 300 "$tmp/err.cut")"
fi

if grep -q "rejected or misread" "$tmp/err.cut"; then
    t_fail "a cut reply still asserts \"rejected or misread\" -- the misdiagnosis D2 exists to remove: $(tail -c 300 "$tmp/err.cut")"
else
    t_ok "and it does NOT assert \"rejected or misread\" for a cut"
fi

if grep -q "OUTPUT CEILING" "$tmp/err.self"; then
    t_fail "a self-terminated reasoning reply (finish stop) blamed the ceiling -- the cause is not ordered in both directions: $(tail -c 300 "$tmp/err.self")"
else
    t_ok "CONTROL: a self-terminated reply gets no ceiling warning"
fi
