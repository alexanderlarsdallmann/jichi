#!/bin/sh
# C-12 (M1130): credentials jichi HOLDS in the config but never registered for
# redaction. Only the model API keys and the search key went through
# jc_redact_register (src/main.c); an MCP server's `headers` value and its
# `env` value, and a user tool's `env` value, were held and used (the header
# line is sent on every MCP http call; the env is put in the spawned process)
# but never registered -- so a model-issued `cat config.json` (or a shell
# `cat ~/.jichi`) prints them into a tool result, and they reach the provider,
# the session file and full-tier telemetry UNREDACTED.
#
# This driver plants the SAME canary in all three unregistered slots (an MCP
# `Authorization: Bearer` header value, an MCP stdio `env` value, and a user
# tool `env` value), then makes the model `cat config.json` so the canary
# arrives in one tool result. A SINGLE headless run with `--output jsonl`
# captures every on-disk sink at once: the jsonl stream is stdout, and the
# telemetry log, journal and session are written alongside -- so the mock model
# answers exactly two requests (the tool call, then the finish). It asserts the
# canary is ABSENT from every sink, and -- so the absence cannot be a hollow
# "the result was never logged" green -- that a unique CREDOKMARK echoed in the
# same command DID reach the sinks (proof the result was written, hence the
# canary was scrubbed rather than merely unlogged).
#
# RED first (pre-fix): the canary appears in telemetry + session for the
# header/env/user-tool values, which were never registered. GREEN (post-fix):
# all sinks read 0 while CREDOKMARK is present.
#
# Sibling to redact_sinks.sh (which registers the canary as a MODEL apiKey, so
# it is already scrubbed); this one registers it in the unregistered slots.
# Set E1_RES to keep the sink artifacts for diagnosis.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
CANARY=CANARYSECRETzq7x0123456789
MARK=CREDOKMARK9x7
tmp=$(smoke_tmp); ws=$(smoke_tmp)
cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"echo $MARK; cat config.json"}
rule
  count 2
  text DONE
EOF
mm_start_unbounded "$tmp/replies.mm" "$tmp/cap"
cat > "$tmp/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"mockkey0123456789","roles":["chat"]}],
 "mcpServers":[{"name":"cfg","type":"http","url":"http://127.0.0.1:1/mcp",
   "headers":["Authorization: Bearer $CANARY"],"env":{"CFG_TOKEN":"$CANARY"}}],
 "tools":[{"name":"credtool","shell":"printf hi",
   "env":{"CFG_SECRET":"$CANARY"}}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF
# One run, --output jsonl: the jsonl stream is stdout, and the telemetry log,
# journal and session are written at the same time (2 model calls total).
( cd "$ws" && cp "$tmp/config.json" config.json \
    && with_deadline 90 "$BIN" --config config.json --auto --output jsonl \
        --log "$tmp/telemetry.jsonl" --log-level full --journal "$tmp/journal.jsonl" \
        -p "print it" < /dev/null > "$tmp/stream.jsonl" 2>/dev/null )
mm_stop
sess=$(find "$HOME" -path '*sessions*' -name '*.json' 2>/dev/null | head -1)
count() { if [ -n "$1" ] && [ -f "$1" ]; then grep -c "$CANARY" "$1"; else echo absent; fi; }
s=$(count "$sess"); t=$(count "$tmp/telemetry.jsonl"); j=$(count "$tmp/journal.jsonl")
st=$(count "$tmp/stream.jsonl")
# The hollow-guard: the unique marker echoed in the same command that prints
# the config must have reached the sinks -- otherwise "canary absent" is just
# "the result was never logged", which is green without any redaction.
mark_t=$(grep -c "$MARK" "$tmp/telemetry.jsonl" 2>/dev/null)
[ -n "$sess" ] && mark_s=$(grep -c "$MARK" "$sess") || mark_s=0
echo "# canary occurrences: session=$s telemetry=$t journal=$j stream(jsonl)=$st"
echo "# $MARK occurrences: telemetry=$mark_t session=$mark_s"
if [ -n "${E1_RES:-}" ]; then
    mkdir -p "$E1_RES"
    cp -p "$tmp/telemetry.jsonl" "$tmp/journal.jsonl" "$tmp/stream.jsonl" "$E1_RES/" 2>/dev/null
    [ -n "$sess" ] && cp -p "$sess" "$E1_RES/session.json"
fi
[ -n "$sess" ] && t_ok "a session was saved (the instrument has something to read)" || t_fail "no session file found under the smoke HOME"
[ "${mark_t:-0}" -ge 1 ] && t_ok "telemetry: the tool result reached the sink ($MARK present)" || t_fail "telemetry: $MARK absent -- the result was never logged, the canary checks below are hollow"
[ "$t" = 0 ] && t_ok "telemetry: the canary does not appear" || t_fail "telemetry: the canary appears $t time(s) -- an unregistered config credential leaked"
[ "$j" = 0 ] && t_ok "journal: the canary does not appear" || t_fail "journal: the canary appears $j time(s)"
[ "$st" = 0 ] && t_ok "stream (--output jsonl): the canary does not appear" || t_fail "stream (--output jsonl): the canary appears $st time(s)"
[ "$s" = 0 ] && t_ok "session: the canary is redacted" || t_fail "session: the canary appears $s time(s) -- sessions are not redacted"
t_done
