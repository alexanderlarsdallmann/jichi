#!/bin/sh
# smoke: a FAILED model call records how big the request was (M326v).
#
# `in_tok` is read out of the response body, so a call that never got a response
# logs 0 -- and every failed call therefore claimed to have sent nothing. In a
# 36,925-event workload that hid the cost of 2,437 retried attempts entirely
# (~211 MB of request bodies re-sent, recorded as zero) and made it impossible
# to test whether failures correlated with request size: the field that would
# answer it was the field the failure zeroed. `req_bytes` is the size we know
# exactly at the moment of sending, so it survives the absence of a reply.
#
# No mock and no FAULT=1 build: a closed port refuses instantly, which is a
# genuine transport failure and the fastest one to provoke. That also keeps this
# running in EVERY build -- faults_net.sh, the semantically closest driver,
# skips without FAULT=1 and so would never have covered this in the gate.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

# Port 1 (tcpmux): reserved and never listening, so connect() is refused at
# once rather than timing out. A timeout would work too but would cost the
# driver its wall-clock budget for no extra coverage.
cat > "$tmp/config.json" <<'EOF'
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:1/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF

(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    --no-session --log "$tmp/telem.jsonl" --log-level metrics \
    -p "hello" < /dev/null > "$tmp/out" 2> "$tmp/err") || true

# --- 1: the run produced telemetry at all (the denominator) ------------------
if [ -s "$tmp/telem.jsonl" ]; then
    t_ok "the failed run still wrote telemetry ($(grep -c . "$tmp/telem.jsonl") events)"
else
    t_fail "no telemetry written -- nothing below can be trusted"
fi

# --- 2: the failed model call is recorded ------------------------------------
grep '"event":"model_call"' "$tmp/telem.jsonl" > "$tmp/calls" 2>/dev/null || true
failed=""
while IFS= read -r line; do
    o=$(printf '%s' "$line" | "$JQ" .ok 2>/dev/null)
    [ "$o" = "false" ] && { failed="$line"; break; }
done < "$tmp/calls"

if [ -n "$failed" ]; then
    t_ok "the failed model call is logged (ok=false)"
else
    t_fail "no failed model_call in telemetry: $(head_bytes 200 "$tmp/calls")"
fi

# --- 3: it carries the request size ------------------------------------------
# The defect itself. A pre-M326v binary logs no req_bytes at all, so jsonq
# returns empty and this fails.
rb=$(printf '%s' "$failed" | "$JQ" .req_bytes 2>/dev/null)
case "$rb" in
    ''|null|0|0.*) t_fail "failed call records req_bytes='$rb' -- the retry cost is invisible again" ;;
    *)             t_ok "the failed call records req_bytes=$rb" ;;
esac

# --- 4: and in_tok is still 0, which is why req_bytes had to exist -----------
# Paired presence/absence: asserting req_bytes alone would pass on a build that
# had somehow learned to fill in_tok, hiding that these are different facts --
# one measured on the way out, one only available on the way back.
it=$(printf '%s' "$failed" | "$JQ" .in_tok 2>/dev/null)
if [ "$it" = "0" ] || [ -z "$it" ]; then
    t_ok "in_tok is still absent/0 on a failed call (req_bytes is the answer, not a duplicate)"
else
    t_fail "in_tok=$it on a failed call -- unexpected; re-check what req_bytes is for"
fi

t_done
