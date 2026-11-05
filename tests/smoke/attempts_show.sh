#!/bin/sh
# M815 (P5): `jichi attempts show <ref>` inspects one preserved state -- its §4.2 end
# record, a diffstat of what it changed against the base, and the tail of the child's
# transcript -- so an operator (or a successor) can read what an attempt did before
# reusing or discarding it. A conflict-losing write child (sessions ON, so it has a
# transcript) is preserved as parallel-conflict; then we show it. Before M815 the
# `show` word was ignored and `attempts` just listed everything -- the red half.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)
printf 'base\n' > "$ws/reg.txt"
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "LEADPROMPT"
  match "\"role\":\"tool\""
  text LEADDONE
rule
  match "LEADPROMPT"
  tool spawn_parallel {"tasks":[{"task":"TASK_A: append A","write":true},{"task":"TASK_B: append B","write":true}]}
rule
  match "TASK_A"
  match "\"role\":\"tool\""
  text ADONE
rule
  match "TASK_A"
  tool run_terminal_command {"command":"printf 'block A\\n' >> reg.txt && echo did-A"}
rule
  match "TASK_B"
  match "\"role\":\"tool\""
  text BDONE
rule
  match "TASK_B"
  tool run_terminal_command {"command":"printf 'block B\\n' >> reg.txt && echo did-B"}
EOF
mm_start_unbounded "$tmp/replies.mm" "$tmp/cap"
cat > "$tmp/cfg.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"preserveDiscarded":true}
EOF
# Sessions ON so the conflict-losing child writes a transcript.
( cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/cfg.json" -q --auto \
    -p "LEADPROMPT run two writers" < /dev/null > "$tmp/out.txt" 2>&1 )
mm_stop

shadow=$(find "$HOME/.jichi.d" -maxdepth 3 -name HEAD -path '*checkpoints*' 2>/dev/null | head -1); shadow="${shadow%/HEAD}"
ref=$(git --git-dir "$shadow" for-each-ref 'refs/jichi/discarded/parallel-conflict/**' --format='%(refname)' 2>/dev/null | head -1)
[ -n "$ref" ] && t_ok "instrument: a preserved conflict state exists to show ($ref)" \
    || t_fail "instrument: no parallel-conflict ref (shadow=${shadow:-none})"

( cd "$ws" && "$BIN" --config "$tmp/cfg.json" attempts show "$ref" > "$tmp/show.txt" 2>&1 )
echo "# --- attempts show ---"; sed 's/^/#   /' "$tmp/show.txt" | head -30; echo "# --- end ---"
grep -qE '^reason:.*parallel-conflict' "$tmp/show.txt" \
    && t_ok "show prints the end record (reason: parallel-conflict)" \
    || t_fail "show did not print the end record"
grep -q '^task:.*TASK_B' "$tmp/show.txt" \
    && t_ok "show names the task from the record" \
    || t_fail "show did not name the task"
grep -q 'reg.txt' "$tmp/show.txt" \
    && t_ok "show prints a diffstat naming the changed file (reg.txt)" \
    || t_fail "show has no diffstat -- it did not diff against the base"
grep -q 'transcript tail' "$tmp/show.txt" \
    && t_ok "show prints the transcript tail" \
    || t_fail "show has no transcript tail section"

( cd "$ws" && "$BIN" --config "$tmp/cfg.json" attempts show "$ref" --output json > "$tmp/show.json" 2>&1 )
grep -q '"record"' "$tmp/show.json" \
    && t_ok "--output json carries the record field" \
    || t_fail "--output json has no record field: $(head_bytes 120 "$tmp/show.json")"
t_done
