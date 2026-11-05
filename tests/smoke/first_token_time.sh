#!/bin/sh
# smoke: each model call records its first-token time, `ttft_ms`, and `jichi telemetry`
# shows it beside the latency (M897, plan 2026-10-03 step 1.1).
#
# THE GAP. `model_call` carried `latency_ms`, the whole call, and nothing else about
# time. A chatbot visitor waits for the FIRST token, not the last: the plan's chatbot
# measurement (1.2) could not say where a visitor's seconds went, because the time
# before anything appeared and the time spent streaming were one number.
#
# THE FIX. The agent asks the provider after each SSE event whether the stream has
# carried output yet -- answer text, reasoning or a tool call (a vtable question, so the
# loop learns no dialect) -- and records the time from sending the request to that
# event as `ttft_ms`. A stream that began and then failed keeps it; a call that carried
# no output has none.
#
#   1  a reply held back 1500 ms records ttft_ms >= 1500 and <= latency_ms
#   2  a stream that stalls after its first delta records ttft_ms well below latency_ms
#   3  a reply whose events arrive 1000 ms apart records ttft_ms well below latency_ms
#      (the mock's `drip`, M897: the success path's first token, not its last)
#   4  a reply that is only a tool call records ttft_ms
#   5  CONTROL: an HTTP 500 records latency_ms and no ttft_ms
#   6  CONTROL: a stream whose only content is "" records no ttft_ms
#   7  `jichi telemetry` shows ttft_ms beside lat_ms
#   8  CONTROL: a log without ttft_ms renders the model line as before
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)

cfg() {
    cat <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0}
EOF
}

# run NAME MOCK [FLAGS...] -- one headless run; telemetry in $tmp/NAME.tel
run() {
    _n="$1"; _mm="$2"; shift 2
    _ws=$(smoke_tmp)
    printf 'seed\n' > "$_ws/seed.txt"
    mm_start_unbounded "$_mm" "$tmp/$_n.cap"
    cfg > "$tmp/$_n.json"
    ( cd "$_ws" && with_deadline 120 "$BIN" --config "$tmp/$_n.json" -q --auto \
        --no-session --log "$tmp/$_n.tel" "$@" -p "say hello" < /dev/null \
        > "$tmp/$_n.out" 2>&1 )
    mm_stop
}
# num FILE FIELD -- the field's value on the first model_call row, or empty
num() {
    grep '"event":"model_call"' "$1" 2>/dev/null | head -n 1 |
        sed -n "s/.*\"$2\":\\([0-9.][0-9.]*\\).*/\\1/p"
}

# --- 1: a delayed reply ---------------------------------------------------------------
cat > "$tmp/delay.mm" <<'EOF'
wire openai
rule
  delay 1500
  text HELLO
EOF
run delay "$tmp/delay.mm"
tt=$(num "$tmp/delay.tel" ttft_ms); lat=$(num "$tmp/delay.tel" latency_ms)
if [ -n "$tt" ] && [ -n "$lat" ] &&
   awk -v t="$tt" -v l="$lat" 'BEGIN{exit !(t >= 1500 && t <= l)}'; then
    t_ok "a reply held back 1500 ms records ttft_ms >= 1500 and <= latency_ms ($tt <= $lat)"
else
    t_fail "ttft_ms=[$tt] latency_ms=[$lat]: $(grep '"event":"model_call"' "$tmp/delay.tel" 2>/dev/null | head_bytes 300)"
fi

# --- 2: a stream that stalls after its first delta ------------------------------------
cat > "$tmp/stall.mm" <<'EOF'
wire openai
rule
  stall mid
EOF
run stall "$tmp/stall.mm" --timeout-stall 2
tt=$(num "$tmp/stall.tel" ttft_ms); lat=$(num "$tmp/stall.tel" latency_ms)
if [ -n "$tt" ] && [ -n "$lat" ] &&
   awk -v t="$tt" -v l="$lat" 'BEGIN{exit !(l >= 1500 && t + 1000 < l)}'; then
    t_ok "a stream that stalls after its first delta records ttft_ms well below latency_ms ($tt < $lat)"
else
    t_fail "ttft_ms=[$tt] latency_ms=[$lat]: $(grep '"event":"model_call"' "$tmp/stall.tel" 2>/dev/null | head_bytes 300)"
fi

# --- 3: a successful reply that streams slowly -------------------------------------
cat > "$tmp/drip.sse" <<'EOF'
data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","content":"HEL"},"finish_reason":null}]}

data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"content":"LO"},"finish_reason":null}]}

data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{},"finish_reason":"stop"}],"usage":{"prompt_tokens":20,"completion_tokens":2}}

data: [DONE]

EOF
sed -e "s#@TMP@#$tmp#g" > "$tmp/drip.mm" <<'EOF'
wire openai
rule
  drip 1000
  sse-file @TMP@/drip.sse
EOF
run drip "$tmp/drip.mm"
tt=$(num "$tmp/drip.tel" ttft_ms); lat=$(num "$tmp/drip.tel" latency_ms)
if grep '"event":"model_call"' "$tmp/drip.tel" 2>/dev/null | head -n 1 | grep -q '"ok":true' &&
   [ -n "$tt" ] && [ -n "$lat" ] &&
   awk -v t="$tt" -v l="$lat" 'BEGIN{exit !(l >= 2500 && t + 2000 < l)}'; then
    t_ok "a reply whose events arrive 1000 ms apart records ttft_ms well below latency_ms ($tt < $lat)"
else
    t_fail "ttft_ms=[$tt] latency_ms=[$lat]: $(grep '"event":"model_call"' "$tmp/drip.tel" 2>/dev/null | head_bytes 300)"
fi

# --- 4: a tool call alone is output ---------------------------------------------------
cat > "$tmp/tool.mm" <<'EOF'
wire openai
rule
  count 1
  delay 300
  tool read_file {"filepath":"seed.txt"}
rule
  text DONE
EOF
run tool "$tmp/tool.mm"
tt=$(num "$tmp/tool.tel" ttft_ms)
if [ -n "$tt" ] && awk -v t="$tt" 'BEGIN{exit !(t >= 300)}'; then
    t_ok "a reply that is only a tool call records ttft_ms ($tt)"
else
    t_fail "the tool-call reply's model_call has ttft_ms=[$tt]: $(grep '"event":"model_call"' "$tmp/tool.tel" 2>/dev/null | head -n 1 | head_bytes 300)"
fi

# --- 5: CONTROL -- an HTTP error carried no output ------------------------------------
cat > "$tmp/err.mm" <<'EOF'
wire openai
rule
  status 500
  body {"error":{"message":"down"}}
EOF
run err "$tmp/err.mm"
if [ -n "$(num "$tmp/err.tel" latency_ms)" ] &&
   ! grep '"event":"model_call"' "$tmp/err.tel" | grep -q '"ttft_ms"'; then
    t_ok "CONTROL: an HTTP 500 records latency_ms and no ttft_ms"
else
    t_fail "err: $(grep '"event":"model_call"' "$tmp/err.tel" 2>/dev/null | head_bytes 300)"
fi

# --- 6: CONTROL -- an empty content delta is not output -------------------------------
cat > "$tmp/empty.sse" <<'EOF'
data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant","content":""},"finish_reason":null}]}

data: {"id":"1","object":"chat.completion.chunk","choices":[{"index":0,"delta":{},"finish_reason":"stop"}],"usage":{"prompt_tokens":20,"completion_tokens":0}}

data: [DONE]

EOF
sed -e "s#@TMP@#$tmp#g" > "$tmp/empty.mm" <<'EOF'
wire openai
rule
  sse-file @TMP@/empty.sse
EOF
run empty "$tmp/empty.mm"
if grep -q '"event":"model_call"' "$tmp/empty.tel" 2>/dev/null &&
   ! grep '"event":"model_call"' "$tmp/empty.tel" | grep -q '"ttft_ms"'; then
    t_ok "CONTROL: a stream whose only content is \"\" records no ttft_ms"
else
    t_fail "empty: $(grep '"event":"model_call"' "$tmp/empty.tel" 2>/dev/null | head_bytes 300)"
fi

# --- 7: the summary shows it ----------------------------------------------------------
"$BIN" telemetry "$tmp/delay.tel" < /dev/null > "$tmp/sum.out" 2>&1
if grep -q 'lat_ms mean=[0-9]* max=[0-9]*  ttft_ms mean=[0-9]* max=[0-9]*$' "$tmp/sum.out"; then
    t_ok "jichi telemetry shows ttft_ms beside lat_ms"
else
    t_fail "summary: $(grep 'lat_ms' "$tmp/sum.out" | head_bytes 200)"
fi

# --- 8: CONTROL -- an older log renders as before -------------------------------------
grep -v '"event":"model_call"' "$tmp/delay.tel" > "$tmp/old.tel"
grep '"event":"model_call"' "$tmp/delay.tel" | sed 's/"ttft_ms":[0-9.]*,//' >> "$tmp/old.tel"
"$BIN" telemetry "$tmp/old.tel" < /dev/null > "$tmp/old.out" 2>&1
if ! grep -q '"ttft_ms"' "$tmp/old.tel" && grep -q 'lat_ms mean=[0-9]* max=[0-9]*$' "$tmp/old.out" &&
   ! grep -q 'ttft_ms' "$tmp/old.out"; then
    t_ok "CONTROL: a log without ttft_ms renders the model line as before"
else
    t_fail "old log: $(grep 'lat_ms' "$tmp/old.out" | head_bytes 200)"
fi

t_done
