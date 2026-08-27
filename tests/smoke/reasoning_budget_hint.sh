#!/bin/sh
# smoke: an answerless reasoning turn is diagnosed from evidence, not assumption
# (M521).
#
# WHAT THIS EXISTS FOR. A reasoning model streams its scratchpad in
# `reasoning_content` and can end a turn with no answer text. jichi warns -- and
# the warning used to say ONE thing always: "the output budget was likely
# exhausted by reasoning -- raise the model's maxTokens". Measured 2026-08-21,
# both causes occurred in a single local bake-off:
#
#   google/gemma-4-12b   spent all 13,107 derived output tokens reasoning and
#                        answered nothing        -> the ceiling WAS the limit
#   qwen/qwen3.5-9b      ended a turn after 62 of the same 13,107 tokens with
#                        nothing left to say     -> the ceiling was irrelevant
#
# So half the time the advice sent the reader to tune a number that was never
# the limit -- and the reader was me, on the run that produced this driver. The
# provider already knows which case it is: `hit_length_cap` is set from
# finish_reason == "length" (M334). A diagnostic that asserts an unchecked cause
# is the same defect as a classifier whose else-branch is a finding
# (docs/TEST_INTEGRITY.md).
#
# WHAT IS AND IS NOT CHECKED (the M305 rule):
#   checked      -- all three branches, driven only by finish_reason and whether
#                   the server reported usage: ceiling / self-ended-after-N /
#                   self-ended-unknown. The ceiling advice must appear in the
#                   first and be ABSENT from the others.
#   NOT checked  -- that raising maxTokens actually helps. That is the model's
#                   business; this holds jichi to claiming it only with grounds.
#   NOT checked  -- the Anthropic dialect, which has no reasoning_content field.
#
# NOTE the blank line after `data: [DONE]` in every fixture below. An SSE frame
# ends on one, so a fixture without it never dispatches the terminal event --
# which is how the first version of this driver "proved" the warning never fired
# and cost an hour (M521, and now tests/smoke/stream_unterminated.sh).
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

mk_sse() {   # mk_sse <path> <finish_reason> <usage:yes|no>
    {
        printf 'data: {"choices":[{"index":0,"delta":'
        printf '{"reasoning_content":"Let me think"},"finish_reason":null}]}\n\n'
        printf 'data: {"choices":[{"index":0,"delta":{},"finish_reason":"%s"}]' "$2"
        if [ "$3" = yes ]; then
            printf ',"usage":{"prompt_tokens":10,"completion_tokens":62}'
        fi
        printf '}\n\n'
        printf 'data: [DONE]\n\n'
    } > "$1"
}

run_case() {  # run_case <sse> <errfile>
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

mk_sse "$tmp/cap.sse"   length no
mk_sse "$tmp/self.sse"  stop   yes
mk_sse "$tmp/plain.sse" stop   no
run_case "$tmp/cap.sse"   "$tmp/err.cap"
run_case "$tmp/self.sse"  "$tmp/err.self"
run_case "$tmp/plain.sse" "$tmp/err.plain"

# --- 1: not vacuous -- the warning fires in all three ------------------------
if grep -q "reasoning but no answer text" "$tmp/err.cap" &&
   grep -q "reasoning but no answer text" "$tmp/err.self" &&
   grep -q "reasoning but no answer text" "$tmp/err.plain"; then
    t_ok "all three runs produced the reasoning-only warning"
else
    t_fail "the warning did not fire, so every check below passes over silence:
 cap:   $(tail -c 150 "$tmp/err.cap")
 self:  $(tail -c 150 "$tmp/err.self")
 plain: $(tail -c 150 "$tmp/err.plain")"
fi

# --- 2-3: finish_reason length -- the ceiling advice is CORRECT here ---------
if grep -q "OUTPUT CEILING" "$tmp/err.cap"; then
    t_ok "a length-capped reasoning turn names the output ceiling"
else
    t_fail "finish_reason length did not report the ceiling: $(tail -c 200 "$tmp/err.cap")"
fi
if grep -q "maxTokens" "$tmp/err.cap"; then
    t_ok "and names the knob to turn (maxTokens / contextLength)"
else
    t_fail "the ceiling warning names no remedy: $(tail -c 200 "$tmp/err.cap")"
fi

# --- 4: usage reported, no cap -- says how many tokens, refuses the advice ---
if grep -q "OUTPUT CEILING" "$tmp/err.self"; then
    t_fail "a self-terminated turn still blamed the ceiling: $(tail -c 200 "$tmp/err.self")"
elif grep -q "after 62 output tokens" "$tmp/err.self"; then
    t_ok "a self-terminated turn reports its 62 output tokens instead"
else
    t_fail "the self-ended case does not state what it observed:
 $(tail -c 200 "$tmp/err.self")"
fi

# --- 5: and it explicitly rules the budget OUT -------------------------------
# The reader's next action is what matters: they must not go tuning maxTokens.
if grep -q "did NOT hit the output ceiling" "$tmp/err.self"; then
    t_ok "and rules the output budget out in so many words"
else
    t_fail "nothing tells the reader the budget was not the limit:
 $(tail -c 200 "$tmp/err.self")"
fi

# --- 6: no usage object -- honest about not knowing the count ---------------
if grep -q "OUTPUT CEILING" "$tmp/err.plain"; then
    t_fail "a turn with no usage object blamed the ceiling anyway:
 $(tail -c 200 "$tmp/err.plain")"
elif grep -q "did not hit the output ceiling" "$tmp/err.plain"; then
    t_ok "with no usage reported it still rules the ceiling out, without a count"
else
    t_fail "the no-usage case says nothing useful: $(tail -c 200 "$tmp/err.plain")"
fi

t_done
