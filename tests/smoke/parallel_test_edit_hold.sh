#!/bin/sh
# smoke: a write child that changed an existing test assertion is kept, not merged, even
# after a green verify (M865; the demos drive's §9, "the verify trusts a test the child
# may edit"; the operator's decision of 2026-10-02, "Hold the merge").
#
# THE CASE. Card 215's worker (run 0090) ran its suite six times and got "expected 47,
# found 43" each time. The L1 pin lived in a test file its task allowed it to edit.
# Setting the pin to 43 would have turned its verify green, and the merge would have
# taken the drive's tree from 47 to 43; only the lead's own census stood in the way.
# The `test_edit` event and the envelope's warning were telemetry and stderr only.
#
# THE FIX (DECISIONS M865). Before a write child's changes merge, the parent diffs each
# file the task MODIFIED (not one it created) against the base; a hunk whose removed
# and added lines both carry an assertion, the old not kept (M88's predicate), holds
# the merge: the work is kept as parallel-test-edit, and the task's note names the
# files. Found from the diff, so a shell rewrite counts as much as edit_file.
#
#   1  an edit_file that moves an expected value is held, though the verify is green
#   2  a shell rewrite of the same kind is held too
#   3  CONTROL: a pure addition to an existing test file merges
#   4  CONTROL: a new test file merges
#   5  telemetry records the two holds (test_edit_hold)
#
# Markers: the expected values are printed by arithmetic; the checks read the live
# workspace, the batch result jichi sent, and telemetry.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)

WS=$(smoke_tmp)
mkdir -p "$WS/src"
for f in pin_test pin2_test add_test; do
    printf 'test "pin" {\n    try std.testing.expectEqual(@as(usize, %s), count());\n}\n' \
        "$((40+7))" > "$WS/src/$f.zig"
done
CAP=$(smoke_tmp)
cat > "$CAP/r.mm" <<'EOF'
wire openai
rule
  match "LEADX"
  match "## Parallel agents"
  text LEAD-DONE
rule
  match "LEADX"
  tool spawn_parallel {"tasks":[{"task":"CHILDP move the pin","write":true},{"task":"CHILDS rewrite the pin","write":true},{"task":"CHILDA add a check","write":true},{"task":"CHILDN a new test file","write":true}]}
rule
  match "CHILDP"
  match "usize, 43"
  text CHILD-DONE
rule
  match "CHILDP"
  match "\"role\":\"tool\""
  tool edit_file {"path":"src/pin_test.zig","old_string":"@as(usize, 47)","new_string":"@as(usize, 43)"}
rule
  match "CHILDP"
  tool read_file {"path":"src/pin_test.zig"}
rule
  match "CHILD"
  match "\"role\":\"tool\""
  text CHILD-DONE
rule
  match "CHILDS"
  tool run_terminal_command {"command":"printf 'test \"pin\" {\\n    try std.testing.expectEqual(@as(usize, %s), count());\\n}\\n' $((40+3)) > src/pin2_test.zig"}
rule
  match "CHILDA"
  tool write_file {"path":"src/add_test.zig","content":"test \"pin\" {\n    try std.testing.expectEqual(@as(usize, 47), count());\n    try std.testing.expectEqual(@as(usize, 1), extra());\n}\n"}
rule
  match "CHILDN"
  tool write_file {"path":"src/new_test.zig","content":"test \"new\" {\n    try std.testing.expect(true);\n}\n"}
EOF
mm_start_unbounded "$CAP/r.mm" "$CAP/cap"
cat > "$CAP/cfg.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai",
 "model":"mock","apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0,"shellFence":false,
 "parallelVerify":true,"verify":"true"}
EOF
TEL="$CAP/tel.jsonl"
( cd "$WS" && with_deadline 120 "$BIN" --config "$CAP/cfg.json" -q --auto \
    --log "$TEL" --log-level full -p "LEADX run the batch" < /dev/null > "$CAP/out" 2>&1 )
mm_stop
LR=$(grep -l '## Parallel agents' "$CAP"/cap/req.* 2>/dev/null | head -1)

if grep -q '47' "$WS/src/pin_test.zig" && [ -n "$LR" ] &&
   grep -q -F 'changed an existing test assertion (src/pin_test.zig)' "$LR" &&
   grep -q -F 'take it on purpose if the change is right]' "$LR" &&
   grep -q 'parallel-test-edit' "$LR"; then
    t_ok "an edit that moves an expected value is held, though the verify is green"
else
    t_fail "the moved pin merged, or is not named (red before M865): $(grep -o 'usize, [0-9]*' "$WS/src/pin_test.zig")"
fi
if grep -q '47' "$WS/src/pin2_test.zig" && grep -q -F '(src/pin2_test.zig)' "${LR:-/dev/null}"; then
    t_ok "a shell rewrite of the same kind is held too"
else
    t_fail "the shell rewrite merged: $(grep -o 'usize, [0-9]*' "$WS/src/pin2_test.zig")"
fi
if grep -q 'extra()' "$WS/src/add_test.zig"; then
    t_ok "CONTROL: a pure addition to an existing test file merges"
else
    t_fail "the pure addition was held"
fi
if [ -f "$WS/src/new_test.zig" ]; then
    t_ok "CONTROL: a new test file merges"
else
    t_fail "the new test file was held"
fi
n=$(grep -c '"event":"test_edit_hold"' "$TEL" 2>/dev/null)
if [ "$n" = "2" ]; then
    t_ok "telemetry records the two holds"
else
    t_fail "telemetry: $n test_edit_hold"
fi

t_done
