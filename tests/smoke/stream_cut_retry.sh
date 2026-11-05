#!/bin/sh
# smoke: a 200 stream cut by an in-stream engine error that emitted NO answer
# bytes is transient and is retried (M1125, SV:0825); a cut that already
# emitted answer bytes is not (the duplication guard, unchanged).
#
# THE DEFECT. Run 0145 measured two real 2026-10-17 calls (runs 0124/0127): a
# 200 stream that began with `reasoning_content` deltas (no answer text) was
# cut by an `event: error` engine message. Both failed calls reclassified as
# JC_ERR_PROVIDER with http_status left at 200 (M909, jc_agent.c:367-381), and
# `is_transient(JC_ERR_PROVIDER, 200)` returned 0 -- so no retry happened, no
# `model_retry` row was written, and the run died on attempt 0 even though
# `maxRetries` allowed more. The sole blocker was the missing arm, NOT the
# `!emitted` guard: a reasoning-only cut has content==NULL, so emitted==0.
#
# THE FIX (one arm, M1125). `is_transient` now treats the M909 in-stream shape
# (st == JC_ERR_PROVIDER with http_status == 200) as transient, so the existing
# retry machinery (:743-777) re-sends the request. The `http_status == 200` scope
# is deliberate: the ONLY other path to JC_ERR_PROVIDER is a non-streamed
# st==JC_OK && http_status>=400 reply (jc_agent.c:778-797), the over-context
# 400 that M909 intends to NOT retry -- and its status is >=400, never 200, so
# the new arm cannot reach it. `emitted` is unchanged: it is the duplication
# guard, already correct (false for the reasoning-only cut, true for a genuine
# partial answer).
#
# CHECKS (mockmodel, the two SSE fixtures, maxRetries 3):
#   1  the run survives: a 200 stream cut by the engine error AFTER
#      reasoning-only deltas (no answer bytes) is RETRIED and ends exit 0
#   2  the answer is the second reply, and TWO requests were sent
#   3  telemetry carried a model_retry row
#   4  CONTROL: a 200 stream cut AFTER real content deltas (answer already
#      emitted) is NOT retried even with maxRetries 3 -- the run ends non-zero
#   5  the control emitted exactly ONE request and no model_retry row
#   6  the engine error still surfaces on stderr (parity with stream_error 3)
#   7  the cut's model_call row still carries stream_error (parity with
#      stream_error 4)
#
# DETERMINISM. mockmodel selects per request by `count N` (1-based): leg (a)
# answers the engine-error fixture on count 1 and a clean [DONE] content stream
# on count 2, so the retry is proven by BOTH the model_retry row and the second
# captured request. Leg (b) answers the content-then-error fixture on count 1
# (emitted==1 stops the retry; no second request is ever sent).
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

# An openai-wire reasoning_content delta, then the engine-error frame that
# cuts the stream (the 0124/0127 shape: reasoning only, no answer text).
cat > "$tmp/cut-reason.sse" <<'EOF'
data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","reasoning_content":"Thinking through the request..."},"finish_reason":null}]}

event: error
data: {"error":{"message":"EngineCore encountered an issue processing this request"}}

EOF

# A clean openai-wire content stream ending in [DONE] -- the scripted second
# reply leg (a) retries into.
cat > "$tmp/ok.sse" <<'EOF'
data: {"id":"2","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","content":"CUT_ANSWER_OK"},"finish_reason":null}]}

data: {"id":"2","object":"chat.completion.chunk","choices":[{"index":0,"delta":{},"finish_reason":"stop"},"usage":{"prompt_tokens":12,"completion_tokens":9}}]

data: [DONE]

EOF

# A real content delta (answer bytes emitted), then the engine-error frame.
cat > "$tmp/cut-content.sse" <<'EOF'
data: {"id":"3","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","content":"PARTIAL_ANSWER"},"finish_reason":null}]}

event: error
data: {"error":{"message":"EngineCore encountered an issue processing this request"}}

EOF

# run NAME MM -- one headless run against that mock, maxRetries 3.
run() {
    printf 'wire openai\n%s\n' "$2" > "$tmp/$1.mm"
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

done_text() { "$JQ" .text "$1" 2>/dev/null; }
# req_count CAPDIR -- the captured request files in ONE leg's capture dir.
req_count() { set -- "$1"/req.* 2>/dev/null; if [ -e "$1" ]; then echo $#; else echo 0; fi; }

# --- leg (a): reasoning-only cut is retried; the run survives ------------------
run a 'rule
  count 1
  sse-file '"$tmp"'/cut-reason.sse
rule
  count 2
  sse-file '"$tmp"'/ok.sse'

RC_A=$(cat "$tmp/a.rc")
A_REQS=$(req_count "$tmp/a.cap")
if [ "$RC_A" -eq 0 ]; then
    t_ok "a 200 stream cut by the engine error after reasoning-only deltas is retried and the run survives (exit 0)"
else
    t_fail "leg a: exit $RC_A, stop_reason '$(grep '"event":"end"' "$tmp/a.jnl" 2>/dev/null | tail -1 | "$JQ" .stop_reason 2>/dev/null)', reqs $A_REQS"
fi
if [ "$(done_text "$tmp/a.out")" = "CUT_ANSWER_OK" ] && [ "$A_REQS" -eq 2 ]; then
    t_ok "the answer is the second reply and two requests were captured (req.1, req.2)"
else
    t_fail "leg a: done.text '$(done_text "$tmp/a.out")', captured requests $A_REQS"
fi
if [ "$RC_A" -eq 0 ] && grep -q '"event":"model_retry"' "$tmp/a.tel" 2>/dev/null; then
    t_ok "telemetry carried a model_retry row"
else
    t_fail "leg a: no model_retry row (exit $RC_A): $(grep '"event":"model_call"' "$tmp/a.tel" 2>/dev/null | head_bytes 200)"
fi

# --- leg (b): content-then-cut is NOT retried (no duplication) ------------------
run b 'rule
  count 1
  sse-file '"$tmp"'/cut-content.sse'

RC_B=$(cat "$tmp/b.rc")
B_REQS=$(req_count "$tmp/b.cap")
if [ "$RC_B" -ne 0 ]; then
    t_ok "CONTROL: a 200 stream cut after real content deltas is NOT retried even with maxRetries 3 (exit $RC_B)"
else
    t_fail "leg b: exit $RC_B -- the emitted answer was retried (duplication)"
fi
if [ "$B_REQS" -eq 1 ] && ! grep -q '"event":"model_retry"' "$tmp/b.tel" 2>/dev/null; then
    t_ok "the control emitted exactly one request and no model_retry row"
else
    t_fail "leg b: captured requests $B_REQS, model_retry present: $(grep -c '"event":"model_retry"' "$tmp/b.tel" 2>/dev/null)"
fi

# --- leg (c): the diagnosis is not swallowed ------------------------------------
if grep -q 'EngineCore encountered an issue processing this request' "$tmp/a.err" 2>/dev/null; then
    t_ok "the engine error still surfaces in stderr (parity with stream_error)"
else
    t_fail "leg a: stderr: $(head_bytes 300 < "$tmp/a.err")"
fi
if grep '"event":"model_call"' "$tmp/a.tel" 2>/dev/null | grep '"result":"error"' \
       | grep '"stream_error":"' | grep -q 'EngineCore encountered an issue'; then
    t_ok "the cut's model_call row still carries stream_error (parity with stream_error)"
else
    t_fail "leg a: model_call: $(grep '"event":"model_call"' "$tmp/a.tel" 2>/dev/null | head_bytes 300)"
fi

t_done
