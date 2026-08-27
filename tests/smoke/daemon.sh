#!/bin/sh
# smoke: the warm-process daemon (M100). It starts, serves control
# requests over an AF_UNIX socket (newline-framed JSON, one request per
# connection), and shuts down. Offline: ping/bad-request/prompt
# error-relay/shutdown against an unreachable model (maxRetries 0 so a
# turn fails instantly). The socket client is the test-only `sockq`
# helper (POSIX sh has no AF_UNIX).
# (Port of tests/e2e/daemon.py, M214.)
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
sock="$tmp/d.sock"
JQ="$SMOKE_TOOLS/jsonq"
SOCKQ="$SMOKE_TOOLS/sockq"

cat > "$tmp/config.json" <<'EOF'
{"lowResource":false,"models":[{"name":"e2e","provider":"openai","model":"m",
 "apiBase":"http://127.0.0.1:9/v1"}],
 "snapshots":false,"repoMap":false,"maxRetries":0,
 "timeouts":{"connect":2,"request":5,"stall":5}}
EOF

(cd "$tmp" && exec "$BIN" --config "$tmp/config.json" daemon --socket "$sock" \
    < /dev/null > /dev/null 2>&1) &
DPID=$!
# wait for the socket to appear (no fractional sleep)
i=0
while [ ! -S "$sock" ]; do
    kill -0 "$DPID" 2>/dev/null || { t_fail "daemon exited during startup"; \
        t_fail -; t_fail -; t_fail -; t_fail -; t_fail -; t_fail -; t_fail -; t_done; }
    i=$((i + 1)); [ $i -gt 10 ] && break
    sleep 1
done
if [ -S "$sock" ]; then
    t_ok "the daemon created its socket"
else
    t_fail "daemon never created its socket"
fi

# ping -> pong
out=$(printf '{"type":"ping"}\n' | "$SOCKQ" --deadline 15 "$sock")
case "$out" in
    *pong*) t_ok "ping returns pong" ;;
    *) t_fail "ping got: $out" ;;
esac

# a malformed request -> an error, not a crash
out=$(printf 'not json\n' | "$SOCKQ" --deadline 15 "$sock")
case "$out" in
    *error*|*"bad request"*) t_ok "a malformed request returns an error" ;;
    *) t_fail "malformed request got: $out" ;;
esac

# a plain-text prompt whose model call fails must relay the error
out=$(printf '{"type":"prompt","prompt":"hi","cwd":"%s","format":"text"}\n' \
        "$tmp" | "$SOCKQ" --deadline 25 "$sock")
case "$out" in
    *[Ee]rror*) t_ok "a failed text turn relays an error (not empty output)" ;;
    *) t_fail "text prompt relay got: $(printf '%s' "$out" | head_bytes 120)" ;;
esac

# a jsonl prompt failure: every line valid JSON, terminal done/error
out=$(printf '{"type":"prompt","prompt":"hi","cwd":"%s","format":"jsonl"}\n' \
        "$tmp" | "$SOCKQ" --deadline 25 "$sock")
printf '%s\n' "$out" | grep . > "$tmp/jl"
jsonl_ok=1
while IFS= read -r line; do
    printf '%s' "$line" | "$JQ" -q '.' 2>/dev/null || jsonl_ok=0
done < "$tmp/jl"
last=$(tail -1 "$tmp/jl")
if [ $jsonl_ok -eq 1 ] \
   && [ "$(printf '%s' "$last" | "$JQ" '.type')" = "done" ] \
   && [ "$(printf '%s' "$last" | "$JQ" '.stop_reason')" = "error" ]; then
    t_ok "a failed jsonl turn is a clean stream ending in done/error"
else
    t_fail "jsonl failure stream wrong: $(head_bytes 150 "$tmp/jl")"
fi

# --- M431g: `format":"json"` -- one terminal object, not text -----------------
# THE DEFECT. The wire `format` was a BOOLEAN (text or jsonl) and the client built
# its request with `args->output_json == 2`, so `--connect --output json` sent
# format:"text" and was served as TEXT. The single-object contract that
# docs/EMBEDDING.md lists as **Stable** was simply unavailable over the daemon --
# silently, which is the part that makes it a broken promise rather than a gap.
#
# Two-sided: pre-fix the daemon has no "json" value at all, so this request is
# served as text and neither assertion below can hold.
out=$(printf '{"type":"prompt","prompt":"hi","cwd":"%s","format":"json"}\n' \
        "$tmp" | "$SOCKQ" --deadline 25 "$sock")
printf '%s\n' "$out" | grep . > "$tmp/js"

# EXACTLY one object: that is what distinguishes json from jsonl, and from text.
njs=$(grep -c . "$tmp/js" 2>/dev/null || echo 0)
if [ "$njs" = "1" ] && "$JQ" -q '.' "$tmp/js" 2>/dev/null; then
    t_ok "format:json returns exactly one JSON object"
else
    t_fail "expected 1 parseable object, got $njs line(s): $(head_bytes 150 "$tmp/js")"
fi

# And it is the DONE object, with the terminal fields a supervisor routes on.
if [ "$("$JQ" '.type' "$tmp/js" 2>/dev/null)" = "done" ] &&
   "$JQ" -q '.stop_reason' "$tmp/js" 2>/dev/null; then
    t_ok "the object is the terminal done, carrying stop_reason"
else
    t_fail "not a terminal done object: $(head_bytes 150 "$tmp/js")"
fi

# shutdown -> bye, and the process exits
out=$(printf '{"type":"shutdown"}\n' | "$SOCKQ" --deadline 15 "$sock")
i=0
while kill -0 "$DPID" 2>/dev/null; do
    i=$((i + 1)); [ $i -gt 10 ] && break
    sleep 1
done
case "$out" in
    *bye*)
        if kill -0 "$DPID" 2>/dev/null; then
            kill "$DPID" 2>/dev/null
            t_fail "daemon said bye but did not exit"
        else
            t_ok "shutdown returns bye and the daemon exits"
        fi ;;
    *) kill "$DPID" 2>/dev/null; t_fail "shutdown got: $out" ;;
esac
wait "$DPID" 2>/dev/null

t_done
