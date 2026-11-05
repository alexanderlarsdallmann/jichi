#!/bin/sh
# smoke: D-15 state file symlink escape prevention (M990).
# Verify that .jichi state files cannot be escaped via symlinks pointing outside
# the workspace. This is the red-first check for the state-file symlink protection.
# LEG A: remember through symlinked memory.md must be BLOCKED.
# LEG B: remember into REAL memory.md must SUCCEED (control).
# LEG C: write_plan through symlinked .jichi directory must be BLOCKED.
# LEG D: write_plan when PLAN.md is a FIFO must not hang and must be BLOCKED
#        (the refusal must be REPORTED -- M997 tightened CHECK 6).
# LEG F: `grade --record` when .jichi/progress.jsonl is a symlink to an outside
#        file must be BLOCKED (M997: the appender checked only the .jichi DIR).

. "$(dirname "$0")/_smoke.sh"

t_plan 15
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# Create an outside marker file that symlinks will point to
OUTSIDE_FILE="$tmp/OUTSIDE_MARKER.txt"
printf 'EXTERNAL_DATA_MARKER_7f3a9b2c1d' > "$OUTSIDE_FILE"

# --- LEG A: remember through a symlinked memory.md (core D-15) ------------------
# Mock pattern: count 1 / tool remember {...}; count 2 / text DONE
cat > "$tmp/repliesA.mm" <<'EOF'
wire openai
rule
  count 1
  tool remember {"note":"m990 escape note"}
rule
  count 2
  text DONE
EOF

mm_start "$tmp/repliesA.mm" "$tmp/capA"
write_config "$tmp/configA.json" "$MM_PORT"

# Plant .jichi/memory.md as a symlink to the outside marker
mkdir -p "$ws/.jichi"
cp "$OUTSIDE_FILE" "$OUTSIDE_FILE.bak"
ln -sf "$OUTSIDE_FILE" "$ws/.jichi/memory.md"

# Run with --auto (NOT --plan, because remember is blocked under --plan)
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/configA.json" \
    --no-session --auto -p "remember the marker" < /dev/null > /dev/null 2>&1); rcA=$?
mm_stop

# CHECK 1: $OUTSIDE is byte-identical after the run (cmp -s against pristine)
if cmp -s "$OUTSIDE_FILE" "$OUTSIDE_FILE.bak"; then
    t_ok "Leg A: outside file unchanged after remember (symlink write blocked)"
else
    t_fail "Leg A: outside file was modified (symlink write succeeded)"
fi

# CHECK 2: the mock capture shows the tool_result (tool was denied by the guard)
# The tool_result contains "error: could not write .jichi/memory.md" in request 2
# (request 1 is the tool call, request 2 is the tool_result + the model's answer)
if grep -q 'could not write .jichi/memory.md' "$tmp/capA/req.2" 2>/dev/null; then
    t_ok "Leg A: remember refused with expected error text (seen in tool_result)"
else
    t_fail "Leg A: expected 'could not write .jichi/memory.md' not found in tool_result"
fi

# --- LEG B: CONTROL: remember into a REAL memory.md (proves the mock works) -----
ws2=$(smoke_tmp)
mkdir -p "$ws2/.jichi"

cat > "$tmp/repliesB.mm" <<'EOF'
wire openai
rule
  count 1
  tool remember {"note":"m990 escape note"}
rule
  count 2
  text DONE
EOF

mm_start "$tmp/repliesB.mm" "$tmp/capB"
write_config "$tmp/configB.json" "$MM_PORT"

# Run with --auto
(cd "$ws2" && with_deadline 60 "$BIN" --config "$tmp/configB.json" \
    --no-session --auto -p "remember the marker" < /dev/null > "$ws2/outB" 2>&1); rcB=$?
mm_stop

# CHECK 3: memory.md CONTAINS the note (proof that remember actually worked)
if grep -q 'm990 escape note' "$ws2/.jichi/memory.md"; then
    t_ok "Leg B: remember into real memory.md succeeded (note present)"
else
    t_fail "Leg B: remember into real memory.md failed (note not found)"
fi

# --- LEG C: write_plan through a symlinked .jichi directory ---------------------
ws3=$(smoke_tmp)
mkdir -p "$tmp/plan_outside"

# Symlink .jichi to outside directory
ln -sf "$tmp/plan_outside" "$ws3/.jichi"

cat > "$tmp/repliesC.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_plan {"claim":"test claim","rejected":"alt1 -- reason","falsifier":"test","not_goals":"none","touches":"none"}
rule
  count 2
  text DONE
EOF

mm_start "$tmp/repliesC.mm" "$tmp/capC"
write_config "$tmp/configC.json" "$MM_PORT"

# Run with --plan (write_plan only runs under --plan)
(cd "$ws3" && with_deadline 60 "$BIN" --config "$tmp/configC.json" \
    --no-session --plan -p "write a plan" < /dev/null > /dev/null 2>&1); rcC=$?
mm_stop

# CHECK 4: no PLAN.md created in the outside dir
if [ ! -f "$tmp/plan_outside/PLAN.md" ]; then
    t_ok "Leg C: no PLAN.md in outside dir (symlink .jichi write blocked)"
else
    t_fail "Leg C: PLAN.md was created in outside dir (symlink write succeeded)"
fi

# CHECK 5: the mock capture shows the tool_result (write_plan was denied)
if grep -E -q 'symlink|non-regular file' "$tmp/capC/req.2" 2>/dev/null; then
    t_ok "Leg C: write_plan refused with expected error text (seen in tool_result)"
else
    t_fail "Leg C: expected symlink/non-regular file error not found in tool_result"
fi

# --- LEG D: FIFO at .jichi/PLAN.md (D-19, if cheap and correct) -----------------
ws4=$(smoke_tmp)
mkdir -p "$ws4/.jichi"
mkfifo "$ws4/.jichi/PLAN.md"

cat > "$tmp/repliesD.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_plan {"claim":"test claim","rejected":"alt1 -- reason","falsifier":"test","not_goals":"none","touches":"none"}
rule
  count 2
  text DONE
EOF

mm_start "$tmp/repliesD.mm" "$tmp/capD"
write_config "$tmp/configD.json" "$MM_PORT"

# Run with --plan - should NOT hang even though PLAN.md is a FIFO. Redirect the
# run's output to a FILE (not /dev/null) so CHECK 6 can inspect it: the whole
# point is that write_plan is REFUSED, and that refusal lands on the run's stderr
# (the tool_result) and in the mock capture.
(cd "$ws4" && with_deadline 60 "$BIN" --config "$tmp/configD.json" \
    --no-session --plan -p "plan the FIFO" < /dev/null > "$tmp/runD.out" 2> "$tmp/runD.err"); rcD=$?
mm_stop

# CHECK 6: run did not hang AND write_plan was actually REFUSED. Before M997 this
# only asserted rc=0, which is ALWAYS true: jc_write_file_atomic renames over a
# FIFO and never opens it, so even a hollow build (guard removed) exits 0. The
# refusal line -- "PLAN.md is a symlink or non-regular file" in the mock
# tool_result (req.2, same target as Leg C's CHECK 5) -- only appears when the
# D-15 leaf guard in jc_tool_plan.c fires. If the guard is removed, write_plan
# succeeds (renames the temp over the FIFO) and req.2 carries "plan written to"
# instead, so this check goes red.
if [ $rcD -eq 0 ] && \
   grep -q 'PLAN.md is a symlink or non-regular file' "$tmp/capD/req.2" 2>/dev/null; then
    t_ok "Leg D: write_plan refused on the FIFO (refusal line present, no hang)"
else
    t_fail "Leg D: run rc=$rcD, but write_plan's FIFO refusal was not reported: $(grep -oE '(PLAN.md is a symlink or non-regular file|plan written to [^\"\\\\]*)' "$tmp/capD/req.2" 2>/dev/null | head -1)"
fi

# CHECK 7: PLAN.md is still a FIFO (not overwritten)
if [ -p "$ws4/.jichi/PLAN.md" ]; then
    t_ok "Leg D: PLAN.md remained a FIFO"
else
    t_fail "Leg D: PLAN.md was replaced"
fi

# --- LEG E: file-level symlink at .jichi/memory.md (test 6 from original) -------
ws5=$(smoke_tmp)
mkdir -p "$ws5/.jichi"
echo 'clean' > "$ws5/.jichi/memory.md"
rm "$ws5/.jichi/memory.md"
ln -sf "$OUTSIDE_FILE" "$ws5/.jichi/memory.md"

cat > "$tmp/repliesE.mm" <<'EOF'
wire openai
rule
  count 1
  tool remember {"note":"m990 escape note"}
rule
  count 2
  text DONE
EOF

mm_start "$tmp/repliesE.mm" "$tmp/capE"
write_config "$tmp/configE.json" "$MM_PORT"

# Run with --auto
(cd "$ws5" && with_deadline 60 "$BIN" --config "$tmp/configE.json" \
    --no-session --auto -p "try remember via file symlink" < /dev/null > /dev/null 2>&1); rcE=$?
mm_stop

# CHECK 8: file symlink should be blocked (outside file unchanged)
if cmp -s "$OUTSIDE_FILE" "$OUTSIDE_FILE.bak"; then
    t_ok "Leg E: file symlink write blocked (outside marker unchanged)"
else
    t_fail "Leg E: file symlink write succeeded"
fi

# CHECK 9: the mock capture shows the tool_result (tool was denied)
if grep -q 'could not write .jichi/memory.md' "$tmp/capE/req.2" 2>/dev/null; then
    t_ok "Leg E: remember refused with expected error text (file symlink)"
else
    t_fail "Leg E: expected 'could not write .jichi/memory.md' not found in tool_result"
fi

# --- LEG F: leaf-level symlink at .jichi/progress.jsonl (M997) ---------------
# .jichi is a REAL dir; .jichi/progress.jsonl is a symlink to an OUTSIDE file
# seeded with a known line. `grade <spec> --record` appends one JSONL line via
# jc_progress_append -- run as a plain shell command in this workspace (no mock
# needed: grade is offline). Before M997 the appender checked only the .jichi
# DIR, so fopen(path, "ab") followed the leaf symlink and appended OUTSIDE.
# A control for the spec itself: verify is `true`, so the grade PASSES and the
# run's exit code is 0 with or without the record -- a refusal to record is a
# stderr line, never a verdict change (M642's property, which CHECK 12 keeps
# true by only looking at the outside file and the stderr).
wsF=$(smoke_tmp)
mkdir -p "$wsF/.jichi"
OUT_F="$tmp/legF_outside_progress"
printf 'LEG F pristine line\n' > "$OUT_F"
cp "$OUT_F" "$OUT_F.bak"
ln -s "$OUT_F" "$wsF/.jichi/progress.jsonl"
cat > "$wsF/spec.md" <<'EOF'
---
title: M997 leaf symlink probe
verify: true
---
Do nothing; the gate is a pass.
EOF

# CHECK 12: the outside file is byte-identical after the run (no escape)
(cd "$wsF" && with_deadline 60 "$BIN" grade spec.md --record \
    < /dev/null > "$tmp/runF.out" 2> "$tmp/runF.err"); rcF=$?
if cmp -s "$OUT_F" "$OUT_F.bak"; then
    t_ok "Leg F: outside file unchanged after grade --record (leaf symlink write blocked)"
else
    t_fail "Leg F: outside progress file was modified through the leaf symlink (rc=$rcF)"
fi

# CHECK 13: the run REPORTED the refusal -- grade prints 'could not append' on
# stderr when the record could not be kept, without changing the verdict
# (rc stays the PASSing 0). A hollow build appends happily, prints nothing, and
# trips CHECK 12; this check keeps the report half true independently.
if [ $rcF -eq 0 ] && grep -q 'could not append' "$tmp/runF.err" 2>/dev/null; then
    t_ok "Leg F: the refused record was reported on stderr (grade verdict unchanged)"
else
    t_fail "Leg F: refusal not reported (rc=$rcF): $(head_bytes 160 "$tmp/runF.err" 2>/dev/null)"
fi

# --- Control check: real memory.md still works (duplicate of B, but explicit) ---
ws6=$(smoke_tmp)
mkdir -p "$ws6/.jichi"
printf 'initial content' > "$ws6/.jichi/memory.md"

# CHECK 10: real memory.md exists and is not a symlink (control)
if [ -f "$ws6/.jichi/memory.md" ] && [ ! -L "$ws6/.jichi/memory.md" ]; then
    t_ok "control: real memory.md exists and is not a symlink"
else
    t_fail "control: real memory.md setup failed"
fi

# CHECK 11: real memory.md in ws2 contains the note (proves remember worked)
if grep -q 'm990 escape note' "$ws2/.jichi/memory.md" 2>/dev/null; then
    t_ok "control: ws2 memory.md contains the remembered note"
else
    t_fail "control: ws2 memory.md missing the remembered note"
fi

# --- LEG G: board edit through symlinked .jichi (M997-follow-up) ---------------
# This verifies that `jichi board add` refuses when .jichi is a symlink to an
# outside directory, and no board.lock or board.json is created outside.
wsG=$(smoke_tmp)
mkdir -p "$tmp/legG_outside"

# Create .jichi as a symlink to the outside directory
ln -sf "$tmp/legG_outside" "$wsG/.jichi"



# Run: jichi board add "probe" (should be refused)
(cd "$wsG" && with_deadline 60 "$BIN" board add "probe" < /dev/null > "$tmp/runG.out" 2> "$tmp/runG.err"); rcG=$?

# CHECK 14: no board.lock or board.json in outside dir
if [ ! -f "$tmp/legG_outside/board.lock" ] && [ ! -f "$tmp/legG_outside/board.json" ]; then
    t_ok "Leg G: no board.lock/board.json created in outside dir (symlink .jichi blocked)"
else
    t_fail "Leg G: board.lock or board.json was created in outside dir"
fi

# CHECK 15: the run REFUSED (rc nonzero OR the refusal text appears)
# The refusal text varies by implementation but typically mentions "symlink" or "refused"
refused=0
if [ $rcG -ne 0 ]; then
    refused=1
fi
# Check for refusal text in both stdout and stderr
if grep -qE 'symlink|refused|could not' "$tmp/runG.out" "$tmp/runG.err" 2>/dev/null; then
    refused=1
fi
if [ $refused -eq 1 ]; then
    t_ok "Leg G: board add was refused (rc=$rcG, refusal text present)"
else
    t_fail "Leg G: board add was not refused (rc=$rcG)"
fi

# --- Clean up ---
rm -f "$OUTSIDE_FILE" "$OUTSIDE_FILE.bak" "$OUT_F" "$OUT_F.bak"
rm -rf "$ws/.jichi" "$ws2/.jichi" "$ws3/.jichi" "$ws4/.jichi" "$ws5/.jichi" \
       "$ws6/.jichi" "$wsF/.jichi" "$wsG/.jichi" "$tmp/legG_outside"

t_done
