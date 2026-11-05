#!/bin/sh
# smoke: a FAILED model call records a stable fail_class token (M1039, MO-2).
#
# A failed call's `model_call` row names its diagnosis only in free text: `result`
# says `error` / `timeout` / `aborted`, and `transport` carries libcurl's prose
# (M321). The prose is curl-version-dependent (e.g. "Could not resolve host"
# vs "Could not resolve host: Name or service not known"), so a grep for a
# stable token like `fail_class":"connect` cannot match across an outage's rows.
# MO-2 adds a stable `fail_class` field so an operator can grep outage logs.
#
# The vocabulary (M1039, M22c, M321, M877, M-L2, M909):
#   - "connect"    : never connected, not a timeout
#   - "timeout-connect": connect timeout before TCP
#   - "timeout-tls": TCP connected, but the TLS handshake ran out (M877)
#   - "timeout-stall": connected, then the stream went dead/stalled (M-L2)
#   - "dns"        : could not resolve host
#   - "refused"    : connection refused after DNS
#   - "http-status": a status >= 400 was received (the server spoke)
#   - "provider-error": error inside a stream that began with HTTP 200 (M909)
#   - "aborted"    : jichi abort
#
# No mock and no FAULT=1 build: runs in every build.

. "$(dirname "$0")/_smoke.sh"

t_plan 12
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

# Port 1 (tcpmux): reserved and never listening, so connect() is refused at
# once rather than timing out. A timeout would work too but would cost the
# driver its wall-clock budget for no extra coverage.
cat > "$tmp/config1.json" <<'EOF'
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:1/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":true,"maxRetries":0}
EOF

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config1.json" \
    --no-session --log "$tmp/telem1.jsonl" --log-level metrics \
    -p "hello" < /dev/null > "$tmp/out1" 2> "$tmp/err1") || true

grep '"event":"model_call"' "$tmp/telem1.jsonl" > "$tmp/calls1" 2>/dev/null || true
o1=$(cat "$tmp/calls1" | "$JQ" .ok 2>/dev/null)
if [ "$o1" = "false" ]; then
    t_ok "LEG 1: failed call logged (ok=false)"
    fc1=$(cat "$tmp/calls1" | "$JQ" .fail_class 2>/dev/null)
    if [ "$fc1" = "refused" ]; then
        t_ok "LEG 1: fail_class='refused' (connect() refused on closed port 1)"
    else
        t_fail "LEG 1: fail_class=$fc1 (expected \"refused\")"
    fi
    r1=$(cat "$tmp/calls1" | "$JQ" .result 2>/dev/null)
    if [ "$r1" = "error" ]; then
        t_ok "LEG 1: result='error' (unchanged from before M1039)"
    else
        t_fail "LEG 1: result=$r1 (expected \"error\")"
    fi
else
    t_fail "LEG 1: no failed model_call in telemetry"
fi

# Unresolvable name: DNS failure
cat > "$tmp/config2.json" <<'EOF'
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://no-such-host.invalid/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":true,"maxRetries":0}
EOF

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config2.json" \
    --no-session --log "$tmp/telem2.jsonl" --log-level metrics \
    -p "hello" < /dev/null > "$tmp/out2" 2> "$tmp/err2") || true

grep '"event":"model_call"' "$tmp/telem2.jsonl" > "$tmp/calls2" 2>/dev/null || true
o2=$(cat "$tmp/calls2" | "$JQ" .ok 2>/dev/null)
if [ "$o2" = "false" ]; then
    t_ok "LEG 2: failed call logged (ok=false)"
    fc2=$(cat "$tmp/calls2" | "$JQ" .fail_class 2>/dev/null)
    if [ "$fc2" = "dns" ]; then
        t_ok "LEG 2: fail_class='dns' (could not resolve no-such-host.invalid)"
    else
        t_fail "LEG 2: fail_class=$fc2 (expected \"dns\")"
    fi
    r2=$(cat "$tmp/calls2" | "$JQ" .result 2>/dev/null)
    if [ "$r2" = "error" ]; then
        t_ok "LEG 2: result='error' (unchanged from before M1039)"
    else
        t_fail "LEG 2: result=$r2 (expected \"error\")"
    fi
else
    t_fail "LEG 2: no failed model_call in telemetry"
fi

# Mockmodel scripted 401: HTTP status >= 400
cat > "$tmp/a401.mm" <<'EOF'
wire openai
rule
  status 401
  body {"error":{"message":"unauthorized"}}
EOF
mkdir -p "$tmp/capa"
mm_start "$tmp/a401.mm" "$tmp/capa"

A_PORT=$MM_PORT

cat > "$tmp/config3.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$A_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":true,"maxRetries":0}
EOF

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config3.json" \
    --no-session --log "$tmp/telem3.jsonl" --log-level metrics \
    -p "hello" < /dev/null > "$tmp/out3" 2> "$tmp/err3") || true

mm_stop

grep '"event":"model_call"' "$tmp/telem3.jsonl" > "$tmp/calls3" 2>/dev/null || true
o3=$(cat "$tmp/calls3" | "$JQ" .ok 2>/dev/null)
if [ "$o3" = "false" ]; then
    t_ok "LEG 3: failed call logged (ok=false)"
    fc3=$(cat "$tmp/calls3" | "$JQ" .fail_class 2>/dev/null)
    if [ "$fc3" = "http-status" ]; then
        t_ok "LEG 3: fail_class='http-status' (server replied 401)"
    else
        t_fail "LEG 3: fail_class=$fc3 (expected \"http-status\")"
    fi
    r3=$(cat "$tmp/calls3" | "$JQ" .result 2>/dev/null)
    if [ "$r3" = "error" ]; then
        t_ok "LEG 3: result='error' (unchanged from before M1039)"
    else
        t_fail "LEG 3: result=$r3 (expected \"error\")"
    fi
else
    t_fail "LEG 3: no failed model_call in telemetry"
fi

# Mockmodel error-in-stream: provider-error (M909)
# LM Studio 0.3.x 4020-token request to a 2048-context model
cat > "$tmp/lmerr.sse" <<'EOF'
event: error
data: {"error":{"message":"Engine protocol predict request returned 400: {\"error\":{\"code\":400,\"message\":\"request (4020 tokens) exceeds the available context size (2048 tokens)\",\"type\":\"exceed_context_size_error\",\"n_prompt_tokens\":4020,\"n_ctx\":2048}}"},"message":"Engine protocol predict request returned 400: {\"error\":{\"code\":400,\"message\":\"request (4020 tokens) exceeds the available context size (2048 tokens)\",\"type\":\"exceed_context_size_error\",\"n_prompt_tokens\":4020,\"n_ctx\":2048}}"}

EOF
printf 'wire openai\nrule\n  sse-file %s/lmerr.sse\n' "$tmp" > "$tmp/lmerr.mm"
mkdir -p "$tmp/caplm"
mm_start "$tmp/lmerr.mm" "$tmp/caplm"

LM_PORT=$MM_PORT

cat > "$tmp/config4.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$LM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":true,"maxRetries":0}
EOF

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config4.json" \
    --no-session --log "$tmp/telem4.jsonl" --log-level metrics \
    -p "hello" < /dev/null > "$tmp/out4" 2> "$tmp/err4") || true

mm_stop

grep '"event":"model_call"' "$tmp/telem4.jsonl" > "$tmp/calls4" 2>/dev/null || true
o4=$(cat "$tmp/calls4" | "$JQ" .ok 2>/dev/null)
if [ "$o4" = "false" ]; then
    t_ok "LEG 4: failed call logged (ok=false)"
    fc4=$(cat "$tmp/calls4" | "$JQ" .fail_class 2>/dev/null)
    if [ "$fc4" = "provider-error" ]; then
        t_ok "LEG 4: fail_class='provider-error' (error inside 200 stream, M909)"
    else
        t_fail "LEG 4: fail_class=$fc4 (expected \"provider-error\")"
    fi
    r4=$(cat "$tmp/calls4" | "$JQ" .result 2>/dev/null)
    if [ "$r4" = "error" ]; then
        t_ok "LEG 4: result='error' (unchanged from before M1039)"
    else
        t_fail "LEG 4: result=$r4 (expected \"error\")"
    fi
else
    t_fail "LEG 4: no failed model_call in telemetry"
fi

t_done
