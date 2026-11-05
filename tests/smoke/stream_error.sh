#!/bin/sh
# smoke: an error the server sends INSIDE a stream that began with HTTP 200 fails the
# model call; it is not the answer (M909).
#
# THE DEFECT. Curriculum Stage 1, 2026-10-04: LM Studio's GPU backend crashed, reloaded
# the model with an 8,192-token context, and answered the next request -- 10,644 tokens
# -- inside a 200 stream:
#     event: error
#     data: {"error":{"message":"Engine protocol predict request returned 400: ..."}}
# The same request unstreamed is a plain HTTP 400. Both providers turned the message
# into the ANSWER's text and marked the stream done, so the call was recorded as a
# success with 0 tokens and the attempt failed in 10 s on a "reply" that was an error.
#
# THE FIX. Each provider keeps the message (`stream_error` on the vtable) and emits no
# text; the agent loop asks after the stream and fails the call as it fails an HTTP
# error: no answer, no retry, the message logged, in telemetry (`stream_error` on
# model_call) and in the run's last-error report.
#
# The fixture is LM Studio's stream as captured from a real over-context request (the
# `event: error` frame below, byte for byte); the Anthropic one is that API's
# documented error event, which no free model here can produce.
#
#   1  the run that got the error fails: non-zero exit, stop_reason "error"
#   2  the error is not the answer: the done object says error, and its text is empty
#   3  stderr names the server's message
#   4  telemetry's model_call is an error and carries the message as stream_error
#   5  the Anthropic dialect's error event fails the run the same way
#   6  CONTROL: an ordinary 200 stream still answers, exit 0
#
#   7  M1142 (SV:0419): an in-stream over-context error is NOT retried with
#      maxRetries armed -- EXACTLY ONE request at the mock, no model_retry row.
#   8  M1142-b (SV:0419): an in-stream overflow that ONLY jc_http knows
#      (no jc_cli mark) is also NOT retried -- EXACTLY ONE request, no
#      model_retry row.
#
# M1142 THE DEFECT. M1125 taught the retry gate (:754) that a provider error
# that arrived inside a 200 stream (the M909 shape, st == JC_ERR_PROVIDER with
# http_status left at 200) is transient. That is right for the engine-error cut
# (stream_cut_retry.sh) but wrong for an over-context error: the LM Studio
# message in the fixture above carries NONE of jc_http_body_ctx_overflow's marks
# ("contextwindowexceeded", "context_length_exceeded", "maximum context length")
# -- only the jc_cli classifier ("n_ctx", "exceeds the available context size",
# "exceed_context_size") knows it -- so the in-stream reclassification never set
# ctx_overflow, and the gate retried a DETERMINISTIC error. Lead-measured on the
# mock with maxRetries 3: exit=1 reqs=4 model_calls=4 model_retries=3. An
# over-context call must make exactly ONE model call, because the same window
# will reject the same request again.
#
# M1142 THE FIX. The in-stream M909 block sets ctx_overflow when EITHER
# classifier matches, and the retry gate excludes !ctx_overflow. The M1125
# comment in is_transient that the over-context case is excluded by
# "http_status never 200" was FALSE for the in-stream shape -- the exclusion is
# now the ctx_overflow flag at the gate.
#
# M1142 CHECKS. Leg 7 drives the SAME lmstudio-error.sse fixture (byte for
# byte, no change to it) through run_one's machinery with maxRetries 3 (the
# config written by hand, the way stream_cut_retry.sh's run() writes its own;
# write_config hardcodes 0 and this leg must not lean on duplicate-key
# last-wins to change it). Leg 7 is one check asserting all of: the run still
# fails non-zero with stop_reason "error" (the overflow shape unchanged),
# EXACTLY ONE request was captured at the mock, and NO model_retry row was
# written. Legs 1-6 are M909's control and stay untouched.
#
# M1142-b CHECKS. Leg 8 drives a NEW openai-wire fixture
# (openai-error.sse, added alongside lmstudio-error.sse, which stays byte for
# byte) through the same run_retries helper. It pins the jc_http TERM of the
# in-stream OR: audit finding -- no in-stream fixture carried jc_http-only
# marks, so the jc_cli term alone would have stayed green. The fixture's
# message carries the one mark in jc_http_body_ctx_overflow's list that is NOT
# in jc_text_is_context_overflow's list ("contextwindowexceeded";
# "context_length_exceeded" and "maximum context length" are in BOTH lists --
# the OpenAI wording "This model's maximum context length is ..." would have
# matched jc_cli and pinned nothing) and none of jc_cli's other marks. If
# someone narrows the in-stream OR to jc_cli only, this leg goes red.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

# LM Studio 0.3.x, 2026-10-04, a 4,020-token request to a model loaded at 2,048:
cat > "$tmp/lmstudio-error.sse" <<'EOF'
event: error
data: {"error":{"message":"Engine protocol predict request returned 400: {\"error\":{\"code\":400,\"message\":\"request (4020 tokens) exceeds the available context size (2048 tokens), try increasing it\",\"type\":\"exceed_context_size_error\",\"n_prompt_tokens\":4020,\"n_ctx\":2048}}"},"message":"Engine protocol predict request returned 400: {\"error\":{\"code\":400,\"message\":\"request (4020 tokens) exceeds the available context size (2048 tokens), try increasing it\",\"type\":\"exceed_context_size_error\",\"n_prompt_tokens\":4020,\"n_ctx\":2048}}"}

EOF
cat > "$tmp/ok.sse" <<'EOF'
data: {"choices":[{"delta":{"content":"STREAM_OK"}}]}

data: [DONE]

EOF
cat > "$tmp/anthropic-error.sse" <<'EOF'
event: error
data: {"type":"error","error":{"type":"overloaded_error","message":"Overloaded"}}

EOF
# M1142-b: an openai-wire error whose message carries ONLY jc_http's
# contextwindowexceeded mark -- none of jc_cli's marks (no "n_ctx", no
# "than the context length", no "Context size has been exceeded", no
# "context_length_exceeded", no "maximum context length", no "exceeds
# the model's context", no LM Studio wording). jc_http matches, jc_cli does
# not: this fixture pins the jc_http term of the in-stream OR.
cat > "$tmp/openai-error.sse" <<'EOF'
event: error
data: {"error":{"message":"Request rejected: contextwindowexceeded -- the prompt is longer than this deployment accepts","type":"invalid_request_error","code":"contextwindowexceeded"}}

EOF

# run_one NAME SSEFILE [WIRE] -- one headless run against that stream
run_one() {
    # the mock sends an sse-file as it is, so its `wire` does not shape it
    printf 'wire openai\nrule\n  sse-file %s\n' "$2" > "$tmp/$1.mm"
    mm_start "$tmp/$1.mm" "$tmp/$1.cap"
    if [ "${3:-openai}" = anthropic ]; then
        cat > "$tmp/$1.json" <<EOF
{"models":[{"name":"m","provider":"anthropic","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"toolProfile":"core",
"lowResource":false,"maxRetries":0}
EOF
    else
        write_config "$tmp/$1.json" "$MM_PORT"
    fi
    (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/$1.json" --no-session \
        --output json --log "$tmp/$1.tel" --journal "$tmp/$1.jnl" \
        -p "say hello" < /dev/null) > "$tmp/$1.out" 2> "$tmp/$1.err"
    echo $? > "$tmp/$1.rc"
    mm_stop
}

run_one lm "$tmp/lmstudio-error.sse"
run_one ok "$tmp/ok.sse"
run_one an "$tmp/anthropic-error.sse" anthropic

# run_retries NAME SSEFILE -- one headless run, same machinery as run_one, but a
# config written by hand (stream_cut_retry.sh's run() shape, toolProfile core)
# with maxRetries 3 armed. M1142: write_config hardcodes 0, and this leg must
# not lean on duplicate-key last-wins to change it.
run_retries() {
    printf 'wire openai\nrule\n  sse-file %s\n' "$2" > "$tmp/$1.mm"
    mm_start "$tmp/$1.mm" "$tmp/$1.cap"
    cat > "$tmp/$1.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"toolProfile":"core",
"lowResource":false,"maxRetries":3}
EOF
    (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/$1.json" --no-session \
        --output json --log "$tmp/$1.tel" --journal "$tmp/$1.jnl" \
        -p "say hello" < /dev/null) > "$tmp/$1.out" 2> "$tmp/$1.err"
    echo $? > "$tmp/$1.rc"
    mm_stop
}

run_retries lr "$tmp/lmstudio-error.sse"
# M1142-b: leg 8, the same helper on the jc_http-only-mark fixture.
run_retries lo "$tmp/openai-error.sse"

end_stop() { grep '"event":"end"' "$1" 2>/dev/null | tail -1 | "$JQ" .stop_reason 2>/dev/null; }
done_text() { "$JQ" .text "$1" 2>/dev/null; }
# req_count CAPDIR -- the captured request files in ONE leg's capture dir
# (stream_cut_retry.sh's idiom: the unquoted glob lists what exists, $1 guards).
req_count() { set -- "$1"/req.* 2>/dev/null; if [ -e "$1" ]; then echo $#; else echo 0; fi; }

rc=$(cat "$tmp/lm.rc")
if [ "$rc" -ne 0 ] && [ "$(end_stop "$tmp/lm.jnl")" = "error" ]; then
    t_ok "the run that got the error fails (exit $rc, stop_reason error)"
else
    t_fail "exit $rc, stop_reason '$(end_stop "$tmp/lm.jnl")'"
fi
# a done object that says error, with no text -- not merely no output at all
if [ "$("$JQ" .stop_reason "$tmp/lm.out" 2>/dev/null)" = "error" ] \
   && [ -z "$(done_text "$tmp/lm.out")" ] && ! grep -q 'exceeds the available context' "$tmp/lm.out"; then
    t_ok "the error is not the answer (done.text is empty, stop_reason error)"
else
    t_fail "done.text: $(done_text "$tmp/lm.out" | head_bytes 160)"
fi
if grep -q 'exceeds the available context size (2048 tokens)' "$tmp/lm.err"; then
    t_ok "stderr names the server's message"
else
    t_fail "stderr: $(head_bytes 300 < "$tmp/lm.err")"
fi
if grep '"event":"model_call"' "$tmp/lm.tel" 2>/dev/null | grep '"result":"error"' \
       | grep '"stream_error":"' | grep -q 'exceeds the available context'; then
    t_ok "telemetry's model_call is an error and carries the message as stream_error"
else
    t_fail "model_call: $(grep '"event":"model_call"' "$tmp/lm.tel" 2>/dev/null | head_bytes 300)"
fi
rc=$(cat "$tmp/an.rc")
if [ "$rc" -ne 0 ] && [ -z "$(done_text "$tmp/an.out")" ] && grep -q 'Overloaded' "$tmp/an.err"; then
    t_ok "the Anthropic dialect's error event fails the run the same way (exit $rc)"
else
    t_fail "anthropic: exit $rc, done.text '$(done_text "$tmp/an.out")', stderr $(head_bytes 160 < "$tmp/an.err")"
fi
rc=$(cat "$tmp/ok.rc")
if [ "$rc" -eq 0 ] && [ "$(done_text "$tmp/ok.out")" = "STREAM_OK" ]; then
    t_ok "CONTROL: an ordinary 200 stream still answers, exit 0"
else
    t_fail "control: exit $rc, done.text '$(done_text "$tmp/ok.out")'"
fi

# --- leg 7 (M1142): the in-stream over-context error is NOT retried ----------
# One check (leg 7 is M1142's tooth): (a) the run still fails with the
# overflow shape unchanged (exit non-zero, stop_reason "error", as legs 1-2 on
# $lm); (b) EXACTLY ONE request at the mock -- a deterministic over-context
# call is not re-sent; (c) NO model_retry row in telemetry. The red measured
# 4 requests and 3 model_retry rows.
LR_RC=$(cat "$tmp/lr.rc")
LR_STOP="$(end_stop "$tmp/lr.jnl")"
LR_REQS=$(req_count "$tmp/lr.cap")
LR_RETRIES=$(grep -c '"event":"model_retry"' "$tmp/lr.tel" 2>/dev/null)
if [ "$LR_RC" -ne 0 ] && [ "$LR_STOP" = "error" ] && [ "$LR_REQS" -eq 1 ] \
   && [ "$LR_RETRIES" -eq 0 ]; then
    t_ok "M1142: an in-stream over-context error is NOT retried with maxRetries armed (exit $LR_RC, exactly 1 request, no model_retry row)"
else
    t_fail "leg 7: in-stream over-context was retried with maxRetries 3 armed -- exit $LR_RC, stop_reason '$LR_STOP', $LR_REQS requests captured, $LR_RETRIES model_retry rows"
fi

# --- leg 8 (M1142-b): the jc_http-only-mark in-stream overflow is NOT retried
# Same shape as leg 7: one check asserting (a) the run still fails with the
# overflow shape unchanged (exit non-zero, stop_reason "error"); (b) EXACTLY
# ONE request at the mock; (c) NO model_retry row. The fixture carries
# contextwindowexceeded, which only jc_http_body_ctx_overflow knows -- if the
# in-stream OR is narrowed to jc_cli only, ctx_overflow is never set here,
# the gate retries, and the red measured on the LM Studio fixture (4
# requests, 3 model_retry rows) is what this leg would print.
LO_RC=$(cat "$tmp/lo.rc")
LO_STOP="$(end_stop "$tmp/lo.jnl")"
LO_REQS=$(req_count "$tmp/lo.cap")
LO_RETRIES=$(grep -c '"event":"model_retry"' "$tmp/lo.tel" 2>/dev/null)
if [ "$LO_RC" -ne 0 ] && [ "$LO_STOP" = "error" ] && [ "$LO_REQS" -eq 1 ] \
   && [ "$LO_RETRIES" -eq 0 ]; then
    t_ok "M1142-b: an in-stream overflow that only jc_http knows (no jc_cli mark) is also not retried (exit $LO_RC, exactly 1 request, no model_retry row)"
else
    t_fail "leg 8: in-stream jc_http-only overflow was retried with maxRetries 3 armed -- exit $LO_RC, stop_reason '$LO_STOP', $LO_REQS requests captured, $LO_RETRIES model_retry rows"
fi

t_done
