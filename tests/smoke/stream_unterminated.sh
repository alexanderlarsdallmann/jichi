#!/bin/sh
# smoke: a stream that ends without its terminal event must not lose the answer
# (M521).
#
# WHAT THIS EXISTS FOR. jichi flushed the accumulated assistant message only on
# the provider's TERMINAL event (`data: [DONE]` for OpenAI). An SSE frame is
# terminated by a blank line, so a final frame that arrives without one is never
# dispatched at all -- and neither is a stream cut by a dropped connection or a
# server that simply omits the event. The text had already gone to the SINK: the
# user watched it print. Then it was dropped.
#
# Measured on the STABLE `--output json` contract: a run that printed HELLO
# reported `"text": "", "stop_reason": "done"`. An empty answer carrying a
# SUCCESS verdict is worse than an error, because nothing downstream can tell the
# difference -- and the run's own history loses what the user saw.
#
# The fix is a `stream_end` vtable hook (the agent does not learn a dialect) that
# both providers implement by flushing what the stream produced.
#
# WHAT IS AND IS NOT CHECKED (the M305 rule):
#   checked      -- the same fixture with and without the terminating blank line
#                   must yield the SAME answer text on --output json; and the
#                   terminated case must still work (a control, so a fix that
#                   broke the normal path could not pass).
#   NOT checked  -- mid-JSON truncation (a half-written delta). That is the M334
#                   length-cap path and has its own handling.
#   NOT checked  -- the Anthropic dialect. It implements the same hook through
#                   the same jc_prov_flush, but this tier drives the OpenAI wire.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# Identical bodies; the ONLY difference is the blank line after [DONE].
mk_sse() {   # mk_sse <path> <terminated:yes|no>
    {
        printf 'data: {"choices":[{"index":0,"delta":{"content":"HELLO"},'
        printf '"finish_reason":null}]}\n\n'
        printf 'data: {"choices":[{"index":0,"delta":{},'
        printf '"finish_reason":"stop"}]}\n\n'
        if [ "$2" = yes ]; then printf 'data: [DONE]\n\n'; else printf 'data: [DONE]\n'; fi
    } > "$1"
}

run_case() {  # run_case <sse> <outfile>
    cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  sse-file $1
EOF
    mm_start "$tmp/replies.mm" "$tmp"
    write_config "$tmp/config.json" "$MM_PORT"
    (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
        --no-session --output json -p "hi" < /dev/null) > "$2" 2>"$2.err"
    mm_stop
}

mk_sse "$tmp/term.sse" yes
mk_sse "$tmp/unterm.sse" no
run_case "$tmp/term.sse"   "$tmp/term.json"
run_case "$tmp/unterm.sse" "$tmp/unterm.json"

# --- 1: the control -- a normal stream still carries its answer --------------
if grep -q '"text":"HELLO"' "$tmp/term.json"; then
    t_ok "a terminated stream reports its answer text"
else
    t_fail "the CONTROL failed -- a normal stream lost its answer, so nothing
 below means anything: $(head_bytes 200 "$tmp/term.json")"
fi

# --- 2: the defect itself ----------------------------------------------------
if grep -q '"text":"HELLO"' "$tmp/unterm.json"; then
    t_ok "an UNTERMINATED stream reports the same answer text"
else
    t_fail "an unterminated final event lost the answer: $(head_bytes 200 "$tmp/unterm.json").
 The text was streamed to the user and then dropped from the message"
fi

# --- 3: and the verdict is not a lie either ---------------------------------
if grep -q '"stop_reason":"done"' "$tmp/unterm.json"; then
    t_ok "the run still reports stop_reason done (the answer is complete)"
else
    t_fail "stop_reason changed: $(head_bytes 200 "$tmp/unterm.json")"
fi

# --- 4: no empty-answer warning, because the answer is not empty ------------
# The M167 warning fires on an assistant message with no text. Before the fix it
# fired here -- correctly describing what jichi had RECORDED while contradicting
# what the user had just SEEN.
if grep -q "no tool call and no text" "$tmp/unterm.json.err"; then
    t_fail "jichi still warns that the model returned no text, while --output
 json carries HELLO: the message and the diagnostic disagree"
else
    t_ok "no empty-answer warning (the message and the diagnostic agree)"
fi

# --- 5: the two runs agree, which is the whole invariant --------------------
a=$(sed 's/.*"text":"\([^"]*\)".*/\1/' "$tmp/term.json")
b=$(sed 's/.*"text":"\([^"]*\)".*/\1/' "$tmp/unterm.json")
if [ -n "$a" ] && [ "$a" = "$b" ]; then
    t_ok "terminated and unterminated streams yield identical text ($a)"
else
    t_fail "the terminating blank line changes the answer: '$a' vs '$b'"
fi

t_done
