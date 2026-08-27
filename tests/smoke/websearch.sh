#!/bin/sh
# smoke: the web_search tool (M27). Two mock servers: the model (request 1
# calls web_search, request 2 answers) and a Tavily-style search backend
# (a plain-JSON mockmodel `status` rule). Proves the tool registered
# (search.url configured), queried the backend, parsed the results, and
# fed them back to the model.
# (Port of tests/e2e/websearch.py, M211.)
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
mkdir -p "$tmp/capm" "$tmp/caps"

cat > "$tmp/search.mm" <<'EOF'
wire openai
rule
  status 200
  body {"results":[{"title":"Result One","url":"http://example/1","content":"the first snippet"},{"title":"Result Two","url":"http://example/2","content":"the second snippet"}]}
EOF

cat > "$tmp/model.mm" <<'EOF'
wire openai
rule
  count 1
  tool web_search {"query":"hello"}
rule
  text SEARCH_DONE
EOF

mm_start "$tmp/search.mm" "$tmp/caps"
S_PORT=$MM_PORT; S_PID=$MM_PID
mm_start "$tmp/model.mm" "$tmp/capm"

cat > "$tmp/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x"}],
 "snapshots":false,"repoMap":false,"maxRetries":0,
 "search":{"url":"http://127.0.0.1:$S_PORT/search",
           "provider":"tavily","maxResults":5}}
EOF

out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      -q --no-session --auto -p "search the web" < /dev/null); rc=$?
kill "$S_PID" 2>/dev/null
wait "$S_PID" 2>/dev/null
mm_stop

if [ -f "$tmp/caps/req.1" ] && grep -q "hello" "$tmp/caps/req.1"; then
    t_ok "the search backend was queried with the model's query"
else
    t_fail "the search backend was never queried (rc=$rc)"
fi
if grep -q "Result One" "$tmp/capm/req.2" 2>/dev/null; then
    t_ok "the parsed results were fed back to the model"
else
    t_fail "results missing from the second model request"
fi
case "$out" in
    *SEARCH_DONE*) t_ok "the turn completed" ;;
    *) t_fail "turn incomplete: $(printf '%s' "$out" | head_bytes 120)" ;;
esac

t_done
