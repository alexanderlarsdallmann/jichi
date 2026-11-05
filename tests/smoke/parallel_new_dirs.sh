#!/bin/sh
# smoke: a write child's new directories are named, and a large one is held out of the
# merge (M888, the demos drive's N49 (b)).
#
# THE DEFECT. A write child's scratch in its worktree was merged into the live tree:
# run 0102 applied 194 files, among them `oracle_scratch/` -- 15 MB, 189 files of Godot
# test copies a worker made in its worktree -- and a `drive-home/` from a mistyped
# absolute path; run 0101 brought `tmp/ut0101.tsv`. The zigodot lead commits with
# `git add -A`, so only the supervisor's questions kept them out of history, three
# times in one night.
#
# THE FIX (the operator's decision, 2026-10-03: "Name all, hold large ones"). Every new
# directory a write child adds -- the highest ancestor of an added path that the live
# tree does not have -- is named in the batch result with its file count and size. One
# over 1 MB or 100 files is held out of the merge, and the task is kept
# (`parallel-new-dir`), so the lead can still take it. The rest of the task merges.
#
#   1  a new directory of 120 files is not merged
#   2  a new directory holding one 2 MB file is not merged
#   3  the batch result names both, with their counts, as HELD
#   4  a new directory of one small file is merged, and named
#   5  CONTROL: the task's edit to a tracked file merged
#   6  the task is kept, as parallel-new-dir
#   7  telemetry records the hold (new_dir_hold), and `jichi telemetry` sums it
#
# The markers come from jichi's effects (the live tree), its batch result and its
# telemetry, never from a command the mock sends.
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "needs git -- a write child runs in a worktree"

t_plan 7
smoke_home
tmp=$(smoke_tmp)
WS=$(smoke_tmp)
printf 'seed\n' > "$WS/seed.txt"
CAP=$(smoke_tmp)

cat > "$CAP/r.mm" <<'EOF'
wire openai
rule
  match "LEADN"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADN"
  tool spawn_parallel {"tasks":[{"task":"CHILDN probe the demos","write":true}]}
rule
  match "CHILDN"
  match "\"role\":\"tool\""
  text CHILD-DONE
rule
  match "CHILDN"
  tool run_terminal_command {"command":"mkdir -p oracle_scratch big tmp; i=1; while [ $i -le 120 ]; do echo $i > oracle_scratch/f$i.txt; i=$((i+1)); done; dd if=/dev/zero of=big/blob bs=1024 count=2048 2>/dev/null; echo x > tmp/ut.tsv; echo changed >> seed.txt"}
EOF
mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
cat > "$CAP/cfg.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0}
EOF
TEL="$CAP/tel.jsonl"
( cd "$WS" && with_deadline 120 "$BIN" --config "$CAP/cfg.json" -q --auto \
    --log "$TEL" --log-level full \
    -p "LEADN run the batch" < /dev/null > "$CAP/out" 2>&1 )
mm_stop
LR=$(grep -l '## Parallel agents' "$CAP"/cap/req.* 2>/dev/null | tail -1)

if [ -n "$LR" ] && [ ! -e "$WS/oracle_scratch" ]; then
    t_ok "a new directory of 120 files is not merged"
else
    t_fail "the 120-file directory reached the live tree: $(ls "$WS" | tr '\n' ' ')"
fi
if [ -n "$LR" ] && [ ! -e "$WS/big" ]; then
    t_ok "a new directory holding a 2 MB file is not merged"
else
    t_fail "the 2 MB directory reached the live tree: $(ls "$WS" | tr '\n' ' ')"
fi
if [ -n "$LR" ] && grep -q 'oracle_scratch/ (120 files, [0-9.]* [KMB]*[a-z]*, HELD)' "$LR" &&
   grep -q 'big/ (1 file, 2.0 MB, HELD)' "$LR"; then
    t_ok "the batch result names both, with their counts, as HELD"
else
    t_fail "the held directories are not named: $(grep -o 'This task added new directories[^"]*' "${LR:-/dev/null}" 2>/dev/null | head_bytes 300)"
fi
if [ -f "$WS/tmp/ut.tsv" ] && [ -n "$LR" ] && grep -q 'tmp/ (1 file, 2 bytes)' "$LR"; then
    t_ok "a new directory of one small file is merged, and named"
else
    t_fail "the small directory: merged=$([ -f "$WS/tmp/ut.tsv" ] && echo yes || echo no), named=$(grep -c 'tmp/ (1 file' "${LR:-/dev/null}" 2>/dev/null)"
fi
if grep -q 'changed' "$WS/seed.txt"; then
    t_ok "CONTROL: the task's edit to a tracked file merged"
else
    t_fail "CONTROL: the task's other change did not merge: $(cat "$WS/seed.txt")"
fi
if [ -n "$LR" ] && grep -q 'parallel-new-dir' "$LR"; then
    t_ok "the task is kept, as parallel-new-dir"
else
    t_fail "the task is not kept as parallel-new-dir: $(grep -o 'Kept[^"]*' "${LR:-/dev/null}" 2>/dev/null | head_bytes 300)"
fi
nh=$(grep -c '"event":"new_dir_hold"' "$TEL" 2>/dev/null)
if [ "$nh" = "1" ] &&
   grep '"event":"new_dir_hold"' "$TEL" | grep -q '"dirs":2' &&
   grep '"event":"new_dir_hold"' "$TEL" | grep -q '"files":121' &&
   "$BIN" telemetry "$TEL" < /dev/null 2>&1 |
       grep -q 'New directories held out of the merge (parallel children): 2 in 1 task, 121 files, 2.0 MB'; then
    t_ok "telemetry records the hold, and jichi telemetry sums it"
else
    t_fail "new_dir_hold events: $nh: $(grep '"event":"new_dir_hold"' "$TEL" 2>/dev/null | head_bytes 300)"
fi

t_done
