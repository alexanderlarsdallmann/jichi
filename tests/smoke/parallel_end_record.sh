#!/bin/sh
# M808 (P2): when a spawn_parallel write child's work is preserved, the kept state's
# COMMIT MESSAGE is the plan's §4.2 "end record" -- not just the task text (M803).
# A successor, and P3's salvage, read it to learn WHY the state was kept: its reason,
# the run/task-run it belongs to, its counters, and the transcript that holds its
# reasoning. Same conflict-loser scenario as parallel_preserve.sh (child B loses the
# merge and is preserved); here we read the preserved ref's message and assert the
# record's fields. Before M808 the message was the bare task text, so `reason:`,
# `run:` and `transcript:` are absent -- the red half of this.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)
printf 'base\n' > "$ws/reg.txt"
cat > "$tmp/replies.mm" <<EOF
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
cat > "$tmp/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"preserveDiscarded":true}
EOF
( cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/config.json" -q --no-session --auto \
    -p "LEADPROMPT run two writers" < /dev/null > "$tmp/out.txt" 2> "$tmp/err.txt" )
mm_stop

shadow=$(find "$HOME/.jichi.d" -maxdepth 3 -name HEAD -path '*checkpoints*' 2>/dev/null | head -1)
shadow="${shadow%/HEAD}"
ref=""; msg=""
if [ -n "$shadow" ]; then
    ref=$(git --git-dir "$shadow" for-each-ref 'refs/jichi/discarded/parallel-conflict/**' --format='%(refname)' 2>/dev/null | head -1)
    [ -n "$ref" ] && msg=$(git --git-dir "$shadow" log -1 --format=%B "$ref" 2>/dev/null)
fi
mkdir -p "${E1_RES:-$tmp}"; printf '%s\n' "$msg" > "${E1_RES:-$tmp}/end_record.txt"
echo "# preserved ref: ${ref:-none}"
echo "# --- end record ---"; printf '%s\n' "$msg" | sed 's/^/#   /'
echo "# --- end ---"

has() { printf '%s\n' "$msg" | grep -q "$1"; }
[ -n "$ref" ] && t_ok "instrument: the conflict loser was preserved (a ref + commit message exist)" \
    || t_fail "instrument: no preserved parallel-conflict ref (shadow=${shadow:-none})"
has '^discarded: parallel task 2 of 2' && t_ok "the record's header names the task's position in the batch" \
    || t_fail "no 'discarded: parallel task N of M' header"
has '^reason:.*parallel-conflict' && t_ok "the record carries the reason (parallel-conflict)" \
    || t_fail "the record has no reason: line -- it is still the bare task text (pre-M808)"
has '^run:.*task-run:' && t_ok "the record carries the run + task-run ids" \
    || t_fail "the record has no run:/task-run: line"
has '^counters:.*tool calls' && t_ok "the record carries the counters (tool calls, tokens)" \
    || t_fail "the record has no counters: line"
has '^transcript:.*attempts/.*/t2\.jsonl' && t_ok "the record points at the child's transcript" \
    || t_fail "the record has no transcript: line pointing at attempts/<run>/t2.jsonl"
t_done
