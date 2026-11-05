#!/bin/sh
# M816 (P5): `"from":"<ref>"` in spawn_parallel RESUMES a kept attempt. The kept
# state's file changes are 3-way merged (cherry-pick --no-commit) into the resuming
# task's worktree, and the predecessor's OUTSIDE-tree effects are prepended to the
# task -- effect-classified, so in-tree file edits (which the merge already carries)
# are NOT re-listed, but a shell command's side effects ARE, as "already done".
#
# Covers the review's T-new-2 and the plan's conflict rule:
#   - a resume does not REPEAT an outside-tree effect (the merge replays files, never
#     commands, so a predecessor's side-effect counter stays at one), and
#   - a merge conflict is a TOOL-ERROR VALUE naming the files, never a crash.
#
# Instrument: run 1 is a single write task that overwrites reg.txt (-> RESUMED) in
# its worktree AND bumps an OUTSIDE-tree counter via the shell, then answers; a
# `verify:false` gate quarantines it, so its state is PRESERVED (parallel-verify-red)
# rather than merged -- the live reg.txt stays untouched and the counter reads 1.
# Run 2 resumes from that ref: the merge brings RESUMED into the live tree WITHOUT
# re-running the counter bump, and the effect note reaches the child. Run 3 resumes
# the same ref after the live tree has DIVERGED on the same line, so the 3-way merge
# conflicts and the task reports an error instead of crashing. Before M816 the `from`
# key was ignored -- the red half of checks 3, 5 and 6.
#
# shellFence:false throughout (M831): this driver's INSTRUMENT is an OUTSIDE-tree
# counter a write child bumps via its shell -- which is exactly the hazard M831's
# Landlock shell fence blocks by default. The fence is orthogonal to the resume
# machinery under test here, and leaving it on would make check 1 fail (the bump is
# refused) and check 5 pass VACUOUSLY (the fence, not the merge, keeps the counter at
# one). So the fence is turned off to keep the instrument working; that the fence
# WOULD block such a bump is covered by parallel_shell_fence.sh.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)
printf 'base\n' > "$ws/reg.txt"
cnt="$tmp/counter"
: > "$cnt"

# --- Run 1: make a change + an outside-tree effect, quarantined so it is kept. ---
cat > "$tmp/r1.mm.in" <<'EOF'
wire openai
rule
  match "LEAD1"
  match "\"role\":\"tool\""
  text LEAD1DONE
rule
  match "LEAD1"
  tool spawn_parallel {"tasks":[{"task":"TASK_R resume-me","write":true}]}
rule
  match "TASK_R"
  match "\"role\":\"tool\""
  text R1DONE
rule
  match "TASK_R"
  tool run_terminal_command {"command":"printf 'RESUMED\\n' > reg.txt && printf x >> @CNT@ && echo did-r"}
EOF
sed "s#@CNT@#$cnt#g" "$tmp/r1.mm.in" > "$tmp/r1.mm"
mm_start_unbounded "$tmp/r1.mm" "$tmp/cap1"
cat > "$tmp/cfg1.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"shellFence":false,
 "preserveDiscarded":true,"parallelVerify":true,"verify":"false"}
EOF
( cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/cfg1.json" -q --auto \
    -p "LEAD1 run one writer" < /dev/null > "$tmp/out1.txt" 2>&1 )
mm_stop

c1=$(wc -c < "$cnt" | tr -d ' ')
[ "$c1" = "1" ] \
    && t_ok "instrument: predecessor's outside-tree effect fired once (counter=1)" \
    || t_fail "instrument: counter is '$c1', expected 1 (out1: $(head_bytes 200 "$tmp/out1.txt"))"

if grep -q RESUMED "$ws/reg.txt"; then
    t_fail "instrument: live reg.txt already has RESUMED -- run 1 was not quarantined"
else
    t_ok "instrument: run 1's change was quarantined, not merged (live tree clean)"
fi

shadow=$(find "$HOME/.jichi.d" -maxdepth 3 -name HEAD -path '*checkpoints*' 2>/dev/null | head -1)
shadow="${shadow%/HEAD}"
ref=$(git --git-dir "$shadow" for-each-ref 'refs/jichi/discarded/parallel-verify-red/**' \
      --format='%(refname)' 2>/dev/null | head -1)
if [ -z "$ref" ]; then
    t_fail "resume MERGE: no preserved verify-red ref to resume (shadow=${shadow:-none})"
    t_fail "resume NOTE: skipped -- no ref"
    t_fail "resume EFFECT: skipped -- no ref"
    t_fail "resume CONFLICT: skipped -- no ref"
    t_done
fi

# The child answers SAW-RESUME-NOTE iff its prompt carried the effect-classified
# note (rule matches "OUTSIDE-tree"); the lead then echoes CHILD-SAW-NOTE. A run-3
# conflict makes the task report "resume conflict", which the lead echoes as
# CONFLICT-REPORTED. Both reach stdout at -q through the tool result.
cat > "$tmp/r2.mm.in" <<'EOF'
wire openai
rule
  match "LEAD2"
  match "SAW-RESUME-NOTE"
  text CHILD-SAW-NOTE
rule
  match "LEAD2"
  match "\"role\":\"tool\""
  text LEAD2DONE
rule
  match "LEAD2"
  tool spawn_parallel {"tasks":[{"task":"TASK_C finalize","write":true,"from":"@REF@"}]}
rule
  match "TASK_C"
  match "OUTSIDE-tree"
  text SAW-RESUME-NOTE
rule
  match "TASK_C"
  text NO-RESUME-NOTE
rule
  match "LEAD3"
  match "resume conflict"
  text CONFLICT-REPORTED
rule
  match "LEAD3"
  match "\"role\":\"tool\""
  text LEAD3DONE
rule
  match "LEAD3"
  tool spawn_parallel {"tasks":[{"task":"TASK_X finalize","write":true,"from":"@REF@"}]}
EOF
sed "s#@REF@#$ref#g" "$tmp/r2.mm.in" > "$tmp/r2.mm"
mm_start_unbounded "$tmp/r2.mm" "$tmp/cap2"
cat > "$tmp/cfg2.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"preserveDiscarded":true,
 "shellFence":false}
EOF

# --- Run 2: clean resume (live base still "base"). ---
( cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/cfg2.json" -q --auto \
    -p "LEAD2 resume the kept attempt" < /dev/null > "$tmp/out2.txt" 2>&1 )
echo "# --- run 2 output ---"; sed 's/^/#   /' "$tmp/out2.txt" | head -20; echo "# --- end ---"

grep -q RESUMED "$ws/reg.txt" \
    && t_ok "resume MERGED the kept file change into the live tree (reg.txt has RESUMED)" \
    || t_fail "resume did not apply the kept change -- from: was ignored"

grep -q CHILD-SAW-NOTE "$tmp/out2.txt" \
    && t_ok "the effect-classified predecessor note reached the successor's prompt" \
    || t_fail "the resume note did not reach the child (no CHILD-SAW-NOTE)"

c2=$(wc -c < "$cnt" | tr -d ' ')
[ "$c2" = "1" ] \
    && t_ok "resume did NOT repeat the outside-tree effect (counter still 1)" \
    || t_fail "counter is '$c2' after resume -- the merge re-ran the command"

# --- Run 3: diverge the live tree on the same line, then resume -> conflict. ---
printf 'DIVERGED\n' > "$ws/reg.txt"
( cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/cfg2.json" -q --auto \
    -p "LEAD3 resume onto a diverged tree" < /dev/null > "$tmp/out3.txt" 2>&1 )
mm_stop
echo "# --- run 3 output ---"; sed 's/^/#   /' "$tmp/out3.txt" | head -20; echo "# --- end ---"

grep -q CONFLICT-REPORTED "$tmp/out3.txt" \
    && t_ok "a resume conflict is a tool-error value (named to the model), not a crash" \
    || t_fail "conflict not reported as a value (out3: $(head_bytes 200 "$tmp/out3.txt"))"
t_done
