#!/bin/sh
# smoke: custom-command frontmatter `model:` and `subtask:` are honored.
# Two mock model servers: a project command with `model: b` must route its
# turn to model b's server; one with `subtask: true` must still complete
# and surface the model's answer (the work runs in an isolated sub-agent).
# (Port of tests/e2e/command_fm.py, M211; the first driver to run TWO
# mockmodel instances side by side.)
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

mkdir -p "$ws/.jichi/commands" "$tmp/capa" "$tmp/capb"
printf -- '---\nmodel: b\n---\nDo the thing.\n' > "$ws/.jichi/commands/useb.md"
printf -- '---\nsubtask: true\n---\nDo the subtask.\n' > "$ws/.jichi/commands/sub.md"

cat > "$tmp/a.mm" <<'EOF'
wire openai
rule
  text ANSWER_A
EOF
cat > "$tmp/b.mm" <<'EOF'
wire openai
rule
  text ANSWER_B
EOF

mm_start "$tmp/a.mm" "$tmp/capa"
A_PORT=$MM_PORT; A_PID=$MM_PID
mm_start "$tmp/b.mm" "$tmp/capb"
B_PORT=$MM_PORT

cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[
  {"name":"a","provider":"openai","model":"model-a",
   "apiBase":"http://127.0.0.1:$A_PORT/v1","apiKey":"x"},
  {"name":"b","provider":"openai","model":"model-b",
   "apiBase":"http://127.0.0.1:$B_PORT/v1","apiKey":"x"}],
 "snapshots":false,"repoMap":false,"maxRetries":0}
EOF

out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      -q --no-session --auto -p "/useb" < /dev/null); rc=$?
if [ -f "$tmp/capb/req.1" ]; then
    t_ok "model:b routed the turn to model b's server"
else
    t_fail "model b's server never saw a request (rc=$rc)"
fi
case "$out" in
    *ANSWER_B*) t_ok "model b's answer reached stdout" ;;
    *) t_fail "expected ANSWER_B: $(printf '%s' "$out" | head_bytes 120)" ;;
esac

out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      -q --no-session --auto -p "/sub" < /dev/null); rc=$?
case "$out" in
    *ANSWER_A*) t_ok "subtask: true still completes with the answer" ;;
    *) t_fail "subtask answer missing (rc=$rc): $(printf '%s' "$out" | head_bytes 120)" ;;
esac

kill "$A_PID" 2>/dev/null
wait "$A_PID" 2>/dev/null
mm_stop

t_done
