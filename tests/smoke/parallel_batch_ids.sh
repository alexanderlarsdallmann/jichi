#!/bin/sh
# smoke: every spawn_parallel batch has its own id -- two batches of one session never
# share a task_run, a seq or a base, and a batch started inside a write child leaves the
# lead's live base alone (M860, the demos drive's N26 and the branch review's CTL-5).
#
# THE DEFECTS. A batch's run id was the session's telemetry id, so every batch of one
# process shared it:
#   - N26: in run 0015 two batches of one task each gave their children the same
#     task_run (`<sid>-t1`) and the same seq band; 453 seq values occurred twice in one
#     telemetry file, and a reader keyed on task_run saw one child of 1,682 calls;
#   - CTL-5: a write child may call spawn_parallel (it is not main-agent-only). Its
#     batch reused the lead's LIVE base `par-<sid>`: it could not take the lead's lock,
#     overwrote the lead's manifest, collided with the lead's `wt-0`, and at teardown
#     deleted the lead's lock and manifest -- so a later run salvaged the lead's live
#     worktree as orphaned and removed it.
#
# THE FIX. A batch's id is `<base>-b<k>` -- the session id at the top level, the child's
# own task_run inside a child -- and its children take consecutive seq bands; a batch
# removes its base's lock and manifest only when it held the lock.
#
#   1  two batches of one session give their children different task_run values
#   2  and no seq value repeats among the children's events
#   3  each batch writes its transcripts under its own id
#   4  a batch inside a write child leaves the lead's manifest as the lead wrote it
#   5  and the lead's lock file in place
#   6  with telemetry off and the envelope armed, a nested batch and the lead's next
#      batch still write their transcripts under different ids
#
# Check 6 came after M860's first gate (2026-10-02): the nested batch's base was read
# back from the telemetry sink, which is NULL with `--log -`, so the base fell back to
# the envelope's run id -- the lead's own -- and the nested batch took the id of the
# lead's next batch: both wrote one transcript file. Without the envelope the fallback
# is a fresh UUID per batch, so the check arms one (--max-tool-calls).
#
# Markers: task_run, seq and paths are jichi's own output (telemetry, the filesystem);
# the manifest check reads the lead's batch.json through the child's shell (cat ..).
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)

cfg() {
    cat <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"shellFence":false}
EOF
}
run() {  # <mm> <prompt> [notel] -> WS, CAP, OUT, TEL
    WS=$(smoke_tmp)
    printf 'seed\n' > "$WS/seed.txt"
    CAP=$(smoke_tmp)
    cp "$1" "$CAP/r.mm"
    mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
    cfg > "$CAP/cfg.json"
    OUT="$CAP/out"
    TEL="$CAP/tel.jsonl"
    if [ "${3:-}" = notel ]; then
        ( cd "$WS" && with_deadline 120 "$BIN" --config "$CAP/cfg.json" -q --auto \
            --log - --max-tool-calls 1000 -p "$2" < /dev/null > "$OUT" 2>&1 )
    else
        ( cd "$WS" && with_deadline 120 "$BIN" --config "$CAP/cfg.json" -q --auto \
            --log "$TEL" --log-level full -p "$2" < /dev/null > "$OUT" 2>&1 )
    fi
    mm_stop
}

# --- 1-3: two batches in one session ------------------------------------------------
cat > "$tmp/two.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "CHILD2-DONE"
  text LEAD-DONE
rule
  match "LEADX"
  match "CHILD1-DONE"
  tool spawn_parallel {"tasks":[{"task":"CHILD2 the second batch","write":true}]}
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILD1 the first batch","write":true}]}
rule
  match "CHILD1"
  match "\"role\":\"tool\""
  text CHILD1-DONE
rule
  match "CHILD1"
  tool write_file {"path":"one.txt","content":"1\n"}
rule
  match "CHILD2"
  match "\"role\":\"tool\""
  text CHILD2-DONE
rule
  match "CHILD2"
  tool write_file {"path":"two.txt","content":"2\n"}
EOF
run "$tmp/two.mm" "LEADX two batches, one after the other"
runs=$(grep -o '"task_run":"[^"]*"' "$TEL" 2>/dev/null | sort -u)
nruns=$(printf '%s\n' "$runs" | grep -c . )
if [ "$nruns" = "2" ] && printf '%s\n' "$runs" | grep -q -- '-b1-t1"' &&
   printf '%s\n' "$runs" | grep -q -- '-b2-t1"'; then
    t_ok "two batches of one session give their children different task_run values"
else
    t_fail "the children's task_run values (red before M860: one shared <sid>-t1):
 $(printf '%s' "$runs" | head_bytes 300)"
fi
dups=$(grep '"task_run":' "$TEL" 2>/dev/null | grep -o '"seq":[0-9]*' | sort | uniq -d | head -3)
nchild=$(grep -c '"task_run":' "$TEL" 2>/dev/null)
if [ -z "$dups" ] && [ "${nchild:-0}" -gt 2 ]; then
    t_ok "no seq value repeats among the children's events ($nchild events)"
else
    t_fail "child events repeat a seq (red before M860: both bands start at 1000000): $dups"
fi
ndirs=$(ls -d "$HOME"/.jichi.d/attempts/*-b1 "$HOME"/.jichi.d/attempts/*-b2 2>/dev/null | wc -l | tr -d ' ')
if [ "$ndirs" = "2" ]; then
    t_ok "each batch writes its transcripts under its own id"
else
    t_fail "the batches' transcripts are not under per-batch ids: $(ls "$HOME"/.jichi.d/attempts 2>/dev/null)"
fi

# --- 4-5: a batch inside a write child ----------------------------------------------
# CHILDN, a write child, starts a batch of its own (one write task), then reads its
# lead's base -- the parent directory of its worktree -- through its shell. Its rules
# come before GRANDC's: its follow-up carries the nested result, which names GRANDC.
cat > "$tmp/nested.mm" <<'EOF'
wire openai
rule
  match "LEADN"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADN"
  tool spawn_parallel {"tasks":[{"task":"CHILDN start a batch of your own","write":true}]}
rule
  match "CHILDN"
  match "LEAD-BASE-SEEN"
  text CHILDN-DONE
rule
  match "CHILDN"
  match "## Parallel agents"
  tool run_terminal_command {"command":"echo LEAD-BASE-SEEN; echo MANIFEST-NESTED:$(grep -c GRANDC ../batch.json) MANIFEST-LEAD:$(grep -c CHILDN ../batch.json); [ -e ../.lock ] && echo LOCK-HERE-$((40+2))"}
rule
  match "CHILDN"
  tool spawn_parallel {"tasks":[{"task":"GRANDC a nested task","write":true}]}
rule
  match "GRANDC"
  match "\"role\":\"tool\""
  text GRANDC-DONE
rule
  match "GRANDC"
  tool write_file {"path":"grand.txt","content":"g\n"}
EOF
run "$tmp/nested.mm" "LEADN a batch whose child starts one"
SEEN=$(grep -l 'LEAD-BASE-SEEN' "$CAP"/cap/req.* 2>/dev/null | xargs grep -l 'CHILDN' 2>/dev/null |
       xargs grep -L 'LEADN' 2>/dev/null | head -1)
# What the child's shell found in its lead's base, printed by the shell itself: a count
# of the nested task in the lead's manifest, of the lead's own task, and the lock.
if [ -n "$SEEN" ] && grep -q 'MANIFEST-NESTED:0 MANIFEST-LEAD:1' "$SEEN"; then
    t_ok "a batch inside a write child leaves the lead's manifest as the lead wrote it"
else
    t_fail "the lead's manifest was overwritten by the nested batch (red before M860):
 $(grep -o 'MANIFEST-NESTED:[0-9]* MANIFEST-LEAD:[0-9]*' "${SEEN:-/dev/null}" 2>/dev/null | head -1)"
fi
if [ -n "$SEEN" ] && grep -q 'LOCK-HERE-42' "$SEEN"; then
    t_ok "and the lead's lock file is in place"
else
    t_fail "the lead's lock file is gone after the nested batch"
fi

# --- 6: the same with telemetry off ---------------------------------------------------
# CHILDZ nests a batch (GRANDZ); then the lead runs a second batch (CHILDY). Telemetry
# off, the envelope armed. Each write child's transcript is its batch dir's t<task>.jsonl.
cat > "$tmp/notel.mm" <<'EOF'
wire openai
rule
  match "LEADZ"
  match "CHILDY-DONE"
  text LEAD-DONE
rule
  match "LEADZ"
  match "## Parallel agents"
  tool spawn_parallel {"tasks":[{"task":"CHILDY the lead's second batch","write":true}]}
rule
  match "LEADZ"
  tool spawn_parallel {"tasks":[{"task":"CHILDZ start a batch of your own","write":true}]}
rule
  match "CHILDZ"
  match "## Parallel agents"
  text CHILDZ-DONE
rule
  match "CHILDZ"
  tool spawn_parallel {"tasks":[{"task":"GRANDZ a nested task","write":true}]}
rule
  match "GRANDZ"
  match "\"role\":\"tool\""
  text GRANDZ-DONE
rule
  match "GRANDZ"
  tool write_file {"path":"grandz.txt","content":"g\n"}
rule
  match "CHILDY"
  match "\"role\":\"tool\""
  text CHILDY-DONE
rule
  match "CHILDY"
  tool write_file {"path":"childy.txt","content":"y\n"}
EOF
run "$tmp/notel.mm" "LEADZ two batches, the first nesting one" notel
# The nested task's own transcript names GRANDZ and not CHILDZ (CHILDZ's names both:
# it carries the nested result); the second batch's names CHILDY. A file naming both
# GRANDZ and CHILDY is one transcript written by two batches.
gz=""; cy=""; mixed=""
for f in "$HOME"/.jichi.d/attempts/*/t*.jsonl; do
    [ -f "$f" ] || continue
    g=0; y=0
    grep -q 'GRANDZ' "$f" && g=1
    grep -q 'CHILDY' "$f" && y=1
    [ "$g$y" = "11" ] && mixed="$f"
    [ "$g" = 1 ] && ! grep -q 'CHILDZ' "$f" && gz="$f"
    [ "$y" = 1 ] && cy="$f"
done
if [ -n "$gz" ] && [ -n "$cy" ] && [ -z "$mixed" ] && [ "${gz%/*}" != "${cy%/*}" ]; then
    t_ok "with telemetry off, a nested batch and the lead's next batch keep their own ids"
else
    t_fail "one id for two batches with telemetry off (red before M860's amendment):
 nested=${gz#"$HOME"/} second=${cy#"$HOME"/} both=${mixed#"$HOME"/}"
fi

t_done
