#!/bin/sh
# M812 (P3): a DEAD parent leaves orphaned worktrees (no teardown ran, M811 gave
# them a run-id base + a free lock + a manifest). The NEXT spawn_parallel batch must
# SALVAGE them on start: preserve each orphan tree as `parallel-orphaned` (recoverable
# via jichi attempts/recover), name it from the manifest, remove the base, and say so
# at the top of its result. And it must be IDEMPOTENT -- running salvage twice yields
# ONE ref, not two (review T-new-3). Before M812 an orphan base just accumulated and
# its work was lost: the red half.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
marker="ORPHWORK_z9_kept"

# ---- run 1: create an orphan (kill the batch mid-run, no teardown) ------------
tmp=$(smoke_tmp); ws=$(smoke_tmp)
cat > "$tmp/r1.mm" <<EOF
wire openai
rule
  match "LEADPROMPT"
  tool spawn_parallel {"tasks":[{"task":"TASK_ORPH: write then wait","write":true}]}
rule
  match "TASK_ORPH"
  tool run_terminal_command {"command":"printf '$marker\\n' > orph.txt && sleep 30"}
EOF
mm_start_unbounded "$tmp/r1.mm" "$tmp/cap1"
cat > "$tmp/cfg.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"preserveDiscarded":true}
EOF
( cd "$ws" && exec setsid "$BIN" --config "$tmp/cfg.json" -q --no-session --auto \
    -p "LEADPROMPT run1" < /dev/null > "$tmp/out1.txt" 2>&1 ) &
lead=$!
# M1112-i (SV:1938): wait for the WORKER's orph.txt marker, not the wt-0 dir -- the
# worker's own mock-model turn sits between the two, and a kill landing before the
# worker's printf left an orphan whose tree had NO marker: check 2 then salvages
# (refs=1) with recovered=0, reading the fixture race as a product failure.
# M1112-j (SV:2220 #3): key the wait on the marker being WRITTEN (orph.txt
# non-empty), not merely existing -- a kill landing between printf's open and
# its write leaves an EMPTY orph.txt that an existence check would pass.
n=0
marker_at_kill=$(find "$HOME/.jichi.d/worktrees" -name 'orph.txt' 2>/dev/null | head -1)
while { [ -z "$marker_at_kill" ] || [ ! -s "$marker_at_kill" ]; } && [ "$n" -lt 200 ]; do
    n=$((n+1)); sleep 0.2
    marker_at_kill=$(find "$HOME/.jichi.d/worktrees" -name 'orph.txt' 2>/dev/null | head -1)
done
# Instrument (SV:1938): is the marker WRITTEN AT the kill moment? Match it against
# recovered over the in-suite runs -- present=yes with recovered=0 would refute it.
# M1112-j (SV:2220 #2): report a clean yes/no, never the path (the old
# ${marker_at_kill:-no} expanded to the path when set).
echo "# at the kill: orph.txt present=$([ -n "$marker_at_kill" ] && [ -s "$marker_at_kill" ] && echo yes || echo no) (waited n=$n)"
pgid=$(ps -o pgid= -p "$lead" 2>/dev/null | tr -d ' ')
[ -n "$pgid" ] && kill -KILL -"$pgid" 2>/dev/null
kill -KILL "$lead" 2>/dev/null; sleep 1; mm_stop 2>/dev/null
orphbase=$(find "$HOME/.jichi.d/worktrees" -maxdepth 1 -mindepth 1 -type d -name 'par-*' 2>/dev/null | head -1)
[ -n "$orphbase" ] && [ -e "$orphbase/wt-0" ] \
    && t_ok "instrument: run 1 left an orphan base with a worktree" \
    || t_fail "instrument: no orphan base (got '$orphbase')"

shadow=$(find "$HOME/.jichi.d" -maxdepth 3 -name HEAD -path '*checkpoints*' 2>/dev/null | head -1); shadow="${shadow%/HEAD}"
orph_refs() { git --git-dir "$shadow" for-each-ref 'refs/jichi/discarded/parallel-orphaned/**' --format='%(refname)' 2>/dev/null | grep -c .; }
orph_first() { git --git-dir "$shadow" for-each-ref 'refs/jichi/discarded/parallel-orphaned/**' --format='%(refname)' 2>/dev/null | head -1; }

# ---- run 2: a normal batch salvages the orphan on start ----------------------
salvage_batch() {
    # Same workspace as run 1 -> same shadow repo, so this batch can preserve run
    # 1's orphaned worktree (the real container case: every run is the same project).
    _t=$(smoke_tmp)
    cat > "$_t/mm" <<EOF
wire openai
rule
  match "LEADPROMPT"
  match "\"role\":\"tool\""
  text LEADDONE
rule
  match "LEADPROMPT"
  tool spawn_parallel {"tasks":[{"task":"TASK_T: just echo","write":false}]}
rule
  match "TASK_T"
  match "\"role\":\"tool\""
  text TDONE
rule
  match "TASK_T"
  tool run_terminal_command {"command":"echo hi"}
EOF
    mm_start_unbounded "$_t/mm" "$_t/cap"
    cat > "$_t/cfg.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"preserveDiscarded":true}
EOF
    ( cd "$ws" && with_deadline 60 "$BIN" --config "$_t/cfg.json" -q --no-session --auto \
        -p "LEADPROMPT salvage" < /dev/null > "$_t/out.txt" 2>&1 )
    mm_stop 2>/dev/null
    SALV_CAP="$_t/cap"
}
salvage_batch
after2_refs=$(orph_refs)
ref=$(orph_first)
recovered=0
[ -n "$ref" ] && git --git-dir "$shadow" show "$ref:orph.txt" 2>/dev/null | grep -q "$marker" && recovered=1
salv_reported=$(grep -l 'Salvaged' "$SALV_CAP"/* 2>/dev/null | head -1)
base_gone=1; [ -e "$orphbase" ] && base_gone=0
echo "# after run 2: parallel-orphaned refs=$after2_refs; orphan work recovered=$recovered; base removed=$base_gone; result reported Salvaged=$([ -n "$salv_reported" ] && echo yes || echo no)"

[ "$after2_refs" -ge 1 ] && [ "$recovered" = 1 ] \
    && t_ok "run 2 salvaged the orphan: its tree is preserved under parallel-orphaned and recoverable" \
    || t_fail "run 2 did not salvage the orphan (refs=$after2_refs recovered=$recovered)"
[ "$base_gone" = 1 ] \
    && t_ok "the salvaged orphan base was removed (does not accumulate)" \
    || t_fail "the orphan base still exists after salvage ($orphbase)"
[ -n "$salv_reported" ] \
    && t_ok "the batch result reports the salvage at the top (recoverable ref surfaced)" \
    || t_fail "the result did not report the salvage"
# the store sentence: "jichi's own attempt store under ... not in this workspace"
salv_store_hint=$(grep -h 'Salvaged' "$SALV_CAP"/* 2>/dev/null | grep -c 'jichi.*attempt store.*not in this workspace')
[ "$salv_store_hint" -ge 1 ] \
    && t_ok "the Salvaged section says the ref lives in jichi's attempt store (not the workspace)" \
    || t_fail "the Salvaged section gives no store location hint: $(grep -h 'Salvaged' "$SALV_CAP"/* 2>/dev/null | head -3)"

# ---- run 3: idempotency -- two salvages must yield ONE ref -------------------
salvage_batch
after3_refs=$(orph_refs)
echo "# after run 3 (idempotency): parallel-orphaned refs=$after3_refs (want same as after run 2 = $after2_refs)"
[ "$after3_refs" = "$after2_refs" ] \
    && t_ok "a second salvage created no new ref (idempotent: two salvages -> one ref)" \
    || t_fail "idempotency broken: refs went $after2_refs -> $after3_refs"
t_done
