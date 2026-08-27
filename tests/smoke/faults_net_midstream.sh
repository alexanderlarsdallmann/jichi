#!/bin/sh
# smoke: a MID-STREAM connection death still retries cleanly (M269).
#
# This closes the one open acceptance criterion of M227 (libcurl handle reuse,
# docs/proposals/2026-08-curl-handle-reuse.md): reusing the easy handle keeps a
# connection warm ACROSS requests, so "the connection dies after the response
# has begun" became a newly reachable failure mode -- and nothing tested it.
# faults_net.sh cannot: JC_FAULT_NET fires BEFORE any bytes move, on a
# connection that was never established.
#
# JICHI_FAULT_NET_MID_AT=0 kills the first stream transfer from inside the write
# callback -- status line and headers already received, connection warm -- which
# libcurl reports as CURLE_WRITE_ERROR with `aborted` clear. The contract under
# test: classify transient (not aborted, not a hard failure), drop the poisoned
# handle, re-issue on a FRESH connection, and answer.
#
# MID_BYTES defaults to 0 (die on the first write) deliberately: once content has
# been emitted the ladder correctly REFUSES to retry, which is a different
# contract. Honest limit: mockmodel answers `Connection: close`, so warm-socket
# reuse itself is a no-op here; what is proven is the classification, the handle
# drop, and the clean fresh-connection retry.
. "$(dirname "$0")/_smoke.sh"

"$BIN" --version < /dev/null 2>/dev/null | grep -q "FAULT=1" \
    || t_skip "needs a FAULT=1 binary (make clean && make FAULT=1)"

t_plan 5
smoke_home
ws=$(smoke_tmp)
tmp=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  text MIDSTREAM_ANSWER_OK_77
EOF
mm_start "$tmp/replies.mm" "$tmp"

# NOT write_config: its template sets maxRetries:0 and a duplicate key would
# shadow ours (first occurrence wins in the parser) -- same note as faults_net.sh.
cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,"maxRetries":2}
EOF

(cd "$ws" && with_deadline 60 env JICHI_FAULT_NET_MID_AT=0 \
    JICHI_FAULT_NET_MID_BYTES=0 \
    "$BIN" --config "$tmp/config.json" --no-session -p "hi" \
    < /dev/null > "$tmp/out" 2> "$tmp/err"); rc=$?

if [ $rc -eq 0 ]; then
    t_ok "the run recovers from a mid-stream death (rc=0)"
else
    t_fail "rc=$rc -- a mid-stream death was not recovered"
fi

if grep -q "retry 1/2 in 500ms" "$tmp/err"; then
    t_ok "classified transient -- the retry ladder engaged"
else
    t_fail "no retry on stderr: $(head_bytes 300 "$tmp/err")"
fi

if [ "$(grep -c 'retry [0-9]/2' "$tmp/err")" -eq 1 ]; then
    t_ok "exactly one attempt lost -- no over-retry"
else
    t_fail "expected 1 retry, got $(grep -c 'retry [0-9]/2' "$tmp/err")"
fi

if [ "$(grep -c 'MIDSTREAM_ANSWER_OK_77' "$tmp/out")" -eq 1 ]; then
    t_ok "the answer appears exactly once (no partial/duplicated text)"
else
    t_fail "answer count $(grep -c 'MIDSTREAM_ANSWER_OK_77' "$tmp/out") in stdout"
fi

# mockmodel captures one req.N per accepted connection, so a second file proves
# the retry re-issued the request rather than resuming the dead transfer.
if [ -f "$tmp/req.2" ]; then
    t_ok "the retry re-issued the request on a new connection"
else
    t_fail "only $(ls "$tmp" | grep -c '^req\.') request(s) captured"
fi

t_done
