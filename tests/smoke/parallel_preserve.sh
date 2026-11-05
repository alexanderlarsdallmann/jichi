#!/bin/sh
# M803 (P1): a spawn_parallel write child whose work is NOT merged must have that
# work preserved, not destroyed, when its worktree is torn down. Two write children
# append to the same registry file: child A (processed first) wins the merge; child
# B conflicts on that file and is therefore NOT merged -- and B also writes a unique
# file. Before P1 the pool removed every worktree unconditionally, so B's work was
# gone; P1 preserves B's worktree under refs/jichi/discarded/parallel-conflict/...
# before removing it, and its content is recoverable. Modelled on the drive
# experiment E1-S5 (its check 5), narrowed to the unmerged child so JC_WT_MERGED
# (the merged child, correctly not preserved) does not confuse the assertion.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)
marker="BWORK_zj4_kept"
printf 'base\n' > "$ws/reg.txt"
cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  match "LEADPROMPT"
  match "\"role\":\"tool\""
  text LEADDONE
rule
  match "LEADPROMPT"
  tool spawn_parallel {"tasks":[{"task":"TASK_A: append A","write":true},{"task":"TASK_B: append B and a unique file","write":true}]}
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
  tool run_terminal_command {"command":"printf 'block B\\n' >> reg.txt && printf '$marker\\n' > unique_b.txt && echo did-B"}
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

# Find the shadow repo (the git_dir that holds refs/jichi/discarded).
shadow=$(find "$HOME/.jichi.d" -maxdepth 3 -name HEAD -path '*checkpoints*' 2>/dev/null | head -1)
shadow="${shadow%/HEAD}"
disc_refs=0; b_kept=0
if [ -n "$shadow" ]; then
    disc_refs=$(git --git-dir "$shadow" for-each-ref 'refs/jichi/discarded/parallel-conflict/**' --format='%(refname)' 2>/dev/null | grep -c .)
    ref=$(git --git-dir "$shadow" for-each-ref 'refs/jichi/discarded/parallel-conflict/**' --format='%(refname)' 2>/dev/null | head -1)
    if [ -n "$ref" ]; then
        git --git-dir "$shadow" show "$ref:unique_b.txt" 2>/dev/null | grep -q "$marker" && b_kept=1
    fi
fi
echo "# shadow=$([ -n "$shadow" ] && echo yes || echo no); parallel-conflict refs=$disc_refs; B's file recovered=$b_kept"
echo "# reg.txt: $(tr '\n' '|' < "$ws/reg.txt" 2>/dev/null)"

grep -q LEADDONE "$tmp/out.txt" && [ -n "$shadow" ] && t_ok "instrument: the batch ran and the shadow repo exists" \
    || t_fail "instrument: lead done=$(grep -c LEADDONE "$tmp/out.txt"), shadow=${shadow:-none}"
grep -q 'block A' "$ws/reg.txt" && t_ok "instrument: child A merged into the live tree (so B is the conflict loser)" \
    || t_fail "instrument: A did not merge -- reg.txt=$(tr '\n' '|' < "$ws/reg.txt")"
[ "$disc_refs" -ge 1 ] && t_ok "the unmerged child's worktree was preserved under refs/jichi/discarded/parallel-conflict" \
    || t_fail "no parallel-conflict discarded ref -- the unmerged child's work was destroyed at teardown"
[ "$b_kept" = 1 ] && t_ok "the preserved state contains the unmerged child's unique file (recoverable)" \
    || t_fail "the unmerged child's unique file is not in any preserved state -- its work was lost"
