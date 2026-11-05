#!/bin/sh
# M809 (P2): the pool's aggregated result must list the KEPT (preserved, unmerged)
# states at the TOP, so the lead reads them first -- each with the ref a successor
# hands to `jichi attempts show` (a ref at the bottom of a long report is a ref
# nobody reads, plan §4.2). Same conflict-loser scenario as parallel_end_record.sh:
# child B loses the merge and is preserved. Here we read the tool result the lead
# received (the mock's capture of the follow-up request) and assert its Kept section.
# Before M809 the result had no Kept section at all -- the red half.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
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
    -p "LEADPROMPT run two writers" < "/dev/null" > "$tmp/out.txt" 2> "$tmp/err.txt" )
mm_stop

# The tool result the lead received is captured in the request(s) it sent afterwards.
kept_hdr=$(grep -l 'Kept' "$tmp/cap"/* 2>/dev/null | head -1)
ref_seen=$(grep -h 'refs.jichi.discarded.parallel-conflict' "$tmp/cap"/* 2>/dev/null | grep -c 'Kept')
# "Kept ... recover with jichi attempts show" -- the recovery hint on the same feed
hint=$(grep -h 'Kept' "$tmp/cap"/* 2>/dev/null | grep -c 'attempts show')
# the store sentence: "jichi's own attempt store under ... not in this workspace"
store_hint=$(grep -h 'Kept' "$tmp/cap"/* 2>/dev/null | grep -c 'jichi.*attempt store.*not in this workspace')
shadow=$(find "$HOME/.jichi.d" -maxdepth 3 -name HEAD -path '*checkpoints*' 2>/dev/null | head -1)
shadow="${shadow%/HEAD}"
disc=0
[ -n "$shadow" ] && disc=$(git --git-dir "$shadow" for-each-ref 'refs/jichi/discarded/parallel-conflict/**' 2>/dev/null | grep -c .)
echo "# preserved parallel-conflict refs=$disc; result has Kept header=$([ -n "$kept_hdr" ] && echo yes || echo no); Kept line names a ref=$ref_seen; recovery hint=$hint"
if [ -n "${E1_RES:-}" ]; then mkdir -p "$E1_RES"; grep -h 'Kept' "$tmp/cap"/* > "$E1_RES/kept.txt" 2>/dev/null; fi

{ grep -q LEADDONE "$tmp/out.txt" && [ "$disc" -ge 1 ]; } \
    && t_ok "instrument: the conflict loser was preserved and the lead finished" \
    || t_fail "instrument: lead done=$(grep -c LEADDONE "$tmp/out.txt"), conflict refs=$disc"
[ -n "$kept_hdr" ] && t_ok "the pool result carries a Kept section" \
    || t_fail "the pool result has no Kept section -- the preserved state is invisible to the lead"
[ "$ref_seen" -ge 1 ] && t_ok "the Kept section names the discarded ref (recoverable)" \
    || t_fail "the Kept section names no refs/jichi/discarded ref"
[ "$hint" -ge 1 ] && t_ok "the Kept section says how to recover (jichi attempts show)" \
    || t_fail "the Kept section gives no recovery command"
[ "$store_hint" -ge 1 ] && t_ok "the Kept section says the ref lives in jichi's attempt store (not the workspace)" \
    || t_fail "the Kept section gives no store location hint: $(grep -h 'Kept' "$tmp/cap"/* 2>/dev/null | head -3)"
t_done
