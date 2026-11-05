#!/bin/sh
# M802b: a registered secret split across two streamed model-text deltas must not
# leak. hl_emit redacts each --output jsonl line, but a secret split across two
# deltas is a full match in NEITHER line -- only in the text a consumer reassembles
# by concatenating the deltas. The hold-back in hl_text (accumulate the streamed
# text, redact the whole buffer, and hold the last jc_redact_maxlen() bytes until a
# later delta completes any straddling secret) closes it. The canary is the mock's
# apiKey (a registered secret); a raw SSE body splits it across two content deltas.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)
canary="CANARYSECRETzq7x0123456789"
half1="CANARYSECRET"        # 12 bytes
half2="zq7x0123456789"      # 14 bytes; half1 || half2 == canary

# Raw SSE: the canary spans two content deltas. A consumer concatenating deltas
# reassembles it; each delta line alone holds only a partial (not a redactable
# match).
{
  printf 'data: {"choices":[{"delta":{"content":"answer: %s"}}]}\n\n' "$half1"
  printf 'data: {"choices":[{"delta":{"content":"%s done"}}]}\n\n' "$half2"
  printf 'data: {"choices":[{"delta":{},"finish_reason":"stop"}]}\n\n'
  printf 'data: [DONE]\n\n'
} > "$tmp/body.sse"

cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  sse-file $tmp/body.sse
EOF
mm_start "$tmp/replies.mm" "$tmp/cap" 6

cat > "$tmp/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[
 {"name":"m","provider":"openai","model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"$canary","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF

( cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" --no-session --auto \
    --output jsonl -p "go" < /dev/null > "$tmp/stream.jsonl" 2>/dev/null )
mm_stop

# Reassemble what a consumer would: concatenate every text delta's content. This is
# where a split secret leaks -- no single JSON line ever holds the whole canary.
concat=$(grep -o '"delta": *"[^"]*"' "$tmp/stream.jsonl" | sed 's/^"delta": *"//; s/"$//' | tr -d '\n')
n_text=$(grep -c '"delta"' "$tmp/stream.jsonl" 2>/dev/null)
has_canary=$(printf '%s' "$concat" | grep -c "$canary")
has_mark=$(printf '%s' "$concat" | grep -c '[*][*][*]')
echo "# text delta events: $n_text; reassembled: $(printf '%s' "$concat" | head_bytes 120)"

[ "$n_text" -ge 1 ] && t_ok "instrument: the stream carried text ($n_text delta event(s))" \
    || t_fail "instrument: no text deltas -- the scenario did not stream"
printf '%s' "$concat" | grep -q 'answer:' && t_ok "instrument: the surrounding text streamed (content flowed)" \
    || t_fail "instrument: the surrounding text is missing -- content did not flow"
[ "$has_mark" -ge 1 ] && t_ok "the split secret was redacted (the *** marker is in the reassembled text)" \
    || t_fail "no redaction marker -- a green canary check below could mean 'never streamed', not 'redacted'"
[ "$has_canary" = 0 ] && t_ok "the canary does not appear in the reassembled stream" \
    || t_fail "the canary appears in the reassembled stream -- a split secret leaked across two deltas"
