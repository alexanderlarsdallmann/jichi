#!/bin/sh
# smoke: the retry ladder, driven deterministically (M219). The analyzed
# unattended workload logged ~2,400 transient transport failures; until now
# the ladder (backoff schedule, per-attempt request rebuild, give-up) was
# only ever exercised by a genuinely flaky server. JC_FAULT_NET fails
# jc_http_stream as JC_ERR_HTTP BEFORE any bytes move, so no server -- not
# even a mock -- is needed, and the sequence is exact. Requires a FAULT=1
# binary; SKIPS otherwise (the faults.sh pattern).
. "$(dirname "$0")/_smoke.sh"

"$BIN" --version < /dev/null 2>/dev/null | grep -q "FAULT=1" \
    || t_skip "needs a FAULT=1 binary (make clean && make FAULT=1)"

t_plan 4
smoke_home
ws=$(smoke_tmp)
tmp=$(smoke_tmp)
# Port 9 (discard) -- never reached: the fault fires before connect.
# NOT write_config: its template already sets maxRetries:0 and a duplicate
# key would shadow ours (first occurrence wins in the parser).
cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:9/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"maxRetries":2}
EOF

(cd "$ws" && with_deadline 30 env JICHI_FAULT_NET_AFTER=0 \
    "$BIN" --config "$tmp/config.json" --no-session -p "hi" \
    < /dev/null > "$tmp/out" 2> "$tmp/err"); rc=$?

if [ $rc -ne 0 ]; then
    t_ok "exhausted retries end in a nonzero exit (rc=$rc)"
else
    t_fail "rc=0 after every model call failed"
fi
# The full ladder, in order, with the doubling backoff on stderr.
if grep -q "retry 1/2 in 500ms" "$tmp/err" &&
   grep -q "retry 2/2 in 1000ms" "$tmp/err"; then
    t_ok "both retries fired with the doubling backoff (500ms, 1000ms)"
else
    t_fail "retry ladder missing from stderr: $(grep -c retry "$tmp/err") line(s)"
fi
if [ "$(grep -c 'retry [0-9]/2' "$tmp/err")" -eq 2 ]; then
    t_ok "exactly maxRetries retries -- no under- or over-retry"
else
    t_fail "expected exactly 2 retries, saw $(grep -c 'retry [0-9]/2' "$tmp/err")"
fi
# The user sees a diagnostic, not silence (the M198 bar).
if grep -qi "error" "$tmp/err"; then
    t_ok "a final error diagnostic reaches stderr"
else
    t_fail "no error text on stderr after giving up"
fi

t_done
