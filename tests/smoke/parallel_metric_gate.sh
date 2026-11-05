#!/bin/sh
# smoke: the metric gate -- a moved assertion in a declared pin file merges when the
# declared metric did not fall, and is kept when it did (M875; the demos drive's §9
# (b); the operator's decision of 2026-10-02, "so either option is configurable").
#
# THE CASE (N46). Every demo the drive flipped bumped one pin, and M865 holds every
# such bump: seven of seven, measured with M865's own matcher on the real diffs. The
# hold exists for card 215, whose pin moved from 47 to 43 with the code. In "metric"
# mode the measurement decides: base and worktree each run the declared command.
#
# THE BATCH: four write children with parallelVerify on, a verifier that is always
# green, the metric `echo score: $(cat SCORE)` read after the label "score:", and the
# pins `src/score*_test.zig` (a glob).
#
#   1  UP moves its pin and raises the score: merged, and the note gives both numbers
#   2  DOWN moves its pin and lowers the score: kept as parallel-metric
#   3  OUT moves an assertion outside the pins: kept as parallel-test-edit
#   4  NONUM moves its pin and leaves no number: kept, the note says why
#   5  telemetry records the three measured verdicts (metric_gate)
#   6  CONTROL: "metric" with no pins declared holds, and the run says so
#
# Markers: every expected value is printed by arithmetic; the checks read the live
# workspace, the batch result jichi sent, stderr and telemetry.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home

setup_ws() { # -> a workspace with four pinned tests and the score at 47
    _ws=$(smoke_tmp)
    mkdir -p "$_ws/src"
    for f in score_test score2_test score3_test other_test; do
        printf 'test "pin" {\n    try std.testing.expectEqual(@as(usize, %s), count());\n}\n' \
            "$((40+7))" > "$_ws/src/$f.zig"
    done
    printf '%s\n' "$((40+7))" > "$_ws/SCORE"
    printf '%s\n' "$_ws"
}

write_mm() { # write_mm <file>
    cat > "$1" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDUP flip a demo","write":true},{"task":"CHILDDOWN lose a demo","write":true},{"task":"CHILDOUT move another assertion","write":true},{"task":"CHILDNONUM break the census","write":true}]}
rule
  match "CHILD"
  match "\"role\":\"tool\""
  text CHILD-DONE
rule
  match "CHILDUP"
  tool write_file {"path":"src/score_test.zig","content":"test \"pin\" {\n    try std.testing.expectEqual(@as(usize, 48), count());\n}\n"}
  tool write_file {"path":"SCORE","content":"48\n"}
rule
  match "CHILDDOWN"
  tool write_file {"path":"src/score2_test.zig","content":"test \"pin\" {\n    try std.testing.expectEqual(@as(usize, 43), count());\n}\n"}
  tool write_file {"path":"SCORE","content":"43\n"}
rule
  match "CHILDOUT"
  tool write_file {"path":"src/other_test.zig","content":"test \"pin\" {\n    try std.testing.expectEqual(@as(usize, 46), count());\n}\n"}
rule
  match "CHILDNONUM"
  tool write_file {"path":"src/score3_test.zig","content":"test \"pin\" {\n    try std.testing.expectEqual(@as(usize, 49), count());\n}\n"}
  tool write_file {"path":"SCORE","content":"n/a\n"}
EOF
}

run_batch() { # run_batch <ws> <metric-json> -> $CAP/out, $LR (the lead's batch result), $TEL
    _ws=$1
    CAP=$(smoke_tmp)
    write_mm "$CAP/r.mm"
    mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
    cat > "$CAP/cfg.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"shellFence":false,
 "parallelVerify":true,"verify":"true","parallelTestEdits":"metric",$2}
EOF
    TEL="$CAP/tel.jsonl"
    ( cd "$_ws" && with_deadline 180 "$BIN" --config "$CAP/cfg.json" -q --auto \
        --log "$TEL" --log-level full -p "LEADX run the batch" < /dev/null > "$CAP/out" 2>&1 )
    mm_stop
    LR=$(grep -l '## Parallel agents' "$CAP"/cap/req.* 2>/dev/null | head -1)
}

WS=$(setup_ws)
run_batch "$WS" '"parallelMetric":{"command":"echo score: $(cat SCORE)","label":"score:","pins":["src/score*_test.zig"]}'

up=$((40+8)); down=$((40+3)); base=$((40+7))
if grep -q "usize, $up" "$WS/src/score_test.zig" && grep -q "^$up\$" "$WS/SCORE" &&
   [ -n "$LR" ] &&
   grep -q -F "the metric did not fall: base $base, this task $up (higher is better)" "$LR"; then
    t_ok "a moved pin whose metric rose merges, and the note gives both numbers"
else
    t_fail "UP: pin=$(grep -o 'usize, [0-9]*' "$WS/src/score_test.zig") score=$(cat "$WS/SCORE"); $(grep -o 'Task 1[^#]*' "$LR" 2>/dev/null | head_bytes 300)"
fi
if grep -q "usize, $base" "$WS/src/score2_test.zig" &&
   grep -q -F "the metric fell: base $base, this task $down (higher is better)" "${LR:-/dev/null}" &&
   grep -q 'parallel-metric' "${LR:-/dev/null}"; then
    t_ok "a moved pin whose metric fell is kept as parallel-metric"
else
    t_fail "DOWN: pin=$(grep -o 'usize, [0-9]*' "$WS/src/score2_test.zig"); $(grep -o 'Task 2[^#]*' "$LR" 2>/dev/null | head_bytes 300)"
fi
if grep -q "usize, $base" "$WS/src/other_test.zig" &&
   grep -q -F "outside the metric's pin files (src/other_test.zig)" "${LR:-/dev/null}"; then
    t_ok "a moved assertion outside the pins is held, as in hold mode"
else
    t_fail "OUT: pin=$(grep -o 'usize, [0-9]*' "$WS/src/other_test.zig"); $(grep -o 'Task 3[^#]*' "$LR" 2>/dev/null | head_bytes 300)"
fi
if grep -q "usize, $base" "$WS/src/score3_test.zig" &&
   grep -q -F 'could not be measured in its worktree: no number after \"score:\" in its output' "${LR:-/dev/null}"; then
    t_ok "a metric that prints no number holds, and the note says why"
else
    t_fail "NONUM: $(grep -o 'Task 4[^#]*' "$LR" 2>/dev/null | head_bytes 300)"
fi
nm=$(grep -c '"event":"metric_gate"' "$TEL" 2>/dev/null)
if [ "$nm" = "3" ] &&
   grep '"event":"metric_gate"' "$TEL" | grep -q '"verdict":"merged"' &&
   grep '"event":"metric_gate"' "$TEL" | grep -q '"verdict":"held"' &&
   grep '"event":"metric_gate"' "$TEL" | grep -q '"verdict":"unknown"' &&
   "$BIN" telemetry "$TEL" < /dev/null 2>&1 |
       grep -q 'Metric gate (parallel children): 1 merged, 1 held, 1 unknown'; then
    t_ok "telemetry records the three measured verdicts, and jichi telemetry counts them"
else
    t_fail "metric_gate events: $nm: $(grep '"event":"metric_gate"' "$TEL" 2>/dev/null | head_bytes 300)"
fi

# --- 6: CONTROL -- "metric" with no pins is not a gate -------------------------------
WS2=$(setup_ws)
run_batch "$WS2" '"parallelMetric":{"command":"echo score: $(cat SCORE)","label":"score:"}'
# the warning is said at config load; -q (the batch's) hides warnings, so ask doctor
warn=$("$BIN" --config "$CAP/cfg.json" doctor < /dev/null 2>&1)
if grep -q "usize, $base" "$WS2/src/score_test.zig" &&
   printf '%s' "$warn" | grep -q 'parallelMetric declares no pins' &&
   grep -q -F 'changed an existing test assertion (src/score_test.zig)' "${LR:-/dev/null}"; then
    t_ok "CONTROL: \"metric\" with no pins declared holds, and the run says so"
else
    t_fail "no-pins control: pin=$(grep -o 'usize, [0-9]*' "$WS2/src/score_test.zig"); doctor: $(printf '%s' "$warn" | grep -i 'metric' | head -3)"
fi

t_done
