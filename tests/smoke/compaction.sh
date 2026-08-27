#!/bin/sh
# smoke: context-aware CHUNKED auto-compaction (M30, the zigodot bug). A
# big-context chat model + a small-context summarize model whose mock
# 400s any request over 5000 bytes (as a small vLLM endpoint does).
# Resuming a large seeded session fires compaction at the turn start: the
# fixed agent chunks the prefix under the 400 threshold (several
# within-budget summarizer calls); the old one-shot path sent one
# oversized call, got the 400, and logged "summarization failed".
# (Port of tests/e2e/compaction.py, M212; the summarizer mock uses the
# new `larger` size predicate.)
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
mkdir -p "$tmp/capm" "$tmp/caps"

cat > "$tmp/main.mm" <<'EOF'
wire openai
rule
  text MAIN_OK
EOF
cat > "$tmp/sum.mm" <<'EOF'
wire openai
rule
  larger 5000
  status 400
  body {"error":{"message":"maximum context length exceeded"}}
rule
  status 200
  body {"choices":[{"index":0,"message":{"role":"assistant","content":"PARTIAL_SUMMARY"},"finish_reason":"stop"}]}
EOF

mm_start "$tmp/main.mm" "$tmp/capm"
M_PORT=$MM_PORT; M_PID=$MM_PID
mm_start "$tmp/sum.mm" "$tmp/caps"

# a large seeded history: 16 turns of ~700-byte messages, above the small
# contextLimit's compaction trigger
sid="compact00-0000-0000-0000-000000000001"
sdir="$HOME/.jichi.d/sessions"
mkdir -p "$sdir"
chunk=$(awk 'BEGIN { for (i = 0; i < 350; i++) printf "x " }')
{
    printf '{"sessionId":"%s","title":"big","workspaceDirectory":"%s",' \
        "$sid" "$ws"
    printf '"mode":"chat","history":['
    i=0
    while [ $i -lt 16 ]; do
        [ $i -gt 0 ] && printf ','
        if [ $((i % 2)) -eq 0 ]; then role=user; else role=assistant; fi
        printf '{"role":"%s","content":"msg %d %s"}' "$role" "$i" "$chunk"
        i=$((i + 1))
    done
    printf ']}'
} > "$sdir/$sid.json"

cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[
  {"name":"main","provider":"openai","model":"main",
   "apiBase":"http://127.0.0.1:$M_PORT/v1","apiKey":"x","roles":["chat"]},
  {"name":"sum","provider":"openai","model":"sum",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x",
   "roles":["summarize"],"contextLength":1200}],
 "contextLimit":3000,"snapshots":false,"repoMap":false,
 "references":false,"maxRetries":0}
EOF

out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      --no-session --session "$sid" -p "continue please" \
      < /dev/null 2>"$tmp/err")
kill "$M_PID" 2>/dev/null
wait "$M_PID" 2>/dev/null
mm_stop

case "$out" in
    *MAIN_OK*) t_ok "the turn completed on the chat model" ;;
    *) t_fail "turn incomplete: $(printf '%s' "$out" | head_bytes 120)" ;;
esac

if ! grep -q "summarization failed" "$tmp/err" \
   && grep -q "compacted" "$tmp/err"; then
    t_ok "compaction succeeded (no 'summarization failed')"
else
    t_fail "stderr: $(grep -E 'summar|compact' "$tmp/err" | head -2)"
fi

ncalls=$(ls "$tmp/caps"/req.* 2>/dev/null | wc -l)
if [ "$ncalls" -ge 2 ]; then
    t_ok "the summarizer was called $ncalls times (chunked, not one-shot)"
else
    t_fail "only $ncalls summarizer call(s) -- the one-shot path?"
fi

# at least one summarizer call fit the small budget (< 5000 bytes)
small=0
for f in "$tmp/caps"/req.*; do
    [ -f "$f" ] || continue
    if [ "$(wc -c < "$f")" -lt 5000 ]; then small=1; fi
done
if [ $small -eq 1 ]; then
    t_ok "a within-budget (chunked) summarizer call arrived"
else
    t_fail "every summarizer call was oversized"
fi

t_done
