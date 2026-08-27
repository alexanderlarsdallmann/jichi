#!/bin/sh
# smoke: the in-turn tool-call loop detector (M432).
#
# THE GAP THIS CLOSES. Every recovery jichi had was reactive to ONE failure -- the
# retry ladder, jc_jsonrepair, verify fix-forward, compaction, rollback. Nothing
# noticed a pattern ACROSS attempts within a turn, so a model could call the same
# tool, fail the same way, and try again dozens of times without being told. Measured
# on three corpora: 25/50 error-turns on 294 Continue sessions, 4/13 (31%) on
# zigodot, 97/241 (40%) on chrtext -- worst single turns apply_patch 34 fails / 0
# successes, and one run_terminal_command repeated 59x identically.
#
# Two thirds of the shape already shipped: M89 tells a model its VERIFY keeps failing
# the same way, M429 tells it a BLOCKED call will not succeed however rephrased. Tool
# calls were the one place missing.
#
# Two-sided: pre-fix no captured request carries the note, and the journal has no
# tool_loop event -- the run behaves identically in every other respect, which is the
# point of an advisory detector.
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# Four IDENTICAL failing read_file calls, then an answer. Byte-identical arguments, so
# the exact key at threshold 3.
#
# The path's PARENT must exist. A path whose parent does not exist cannot be resolved,
# so the path fence refuses it -- "refused by safety fence (path outside workspace)" --
# and the failure is genuinely a DENIAL, not a missing file. The first cut of this
# driver used `nope/missing.txt` and then asserted not_found advice; the classifier was
# right and the fixture was wrong. `sub/` exists, so `sub/missing.txt` resolves, passes
# the fence, and fails at open with jichi's missing-file wording.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"sub/missing.txt"}
rule
  count 2
  tool read_file {"path":"sub/missing.txt"}
rule
  count 3
  tool read_file {"path":"sub/missing.txt"}
rule
  count 4
  tool read_file {"path":"sub/missing.txt"}
rule
  text LOOP_DONE
EOF

mkdir -p "$ws/sub"

mm_start "$tmp/replies.mm" "$tmp/cap" 9
write_config "$tmp/config.json" "$MM_PORT"

(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    -q --no-session --auto --budget-tokens 200k \
    --journal "$tmp/j.jsonl" \
    -p "read the file" < /dev/null) > /dev/null 2>&1; rc=$?
mm_stop

# --- 1: advisory -- the run completes regardless ------------------------------
if [ "$rc" -eq 0 ]; then
    t_ok "the run completed (rc=0): the detector is advisory"
else
    t_fail "run exited $rc -- a detector must not change the outcome"
fi

# --- 2: the model IS told, and by the exact key -------------------------------
# The note travels in the tool result inside the NEXT request body, so the captured
# requests are the only ground truth for "the model was told" (the M429 lesson).
if grep -l "SAME arguments" "$tmp"/cap/req.* >/dev/null 2>&1; then
    t_ok "the model is told it repeated the SAME arguments"
else
    t_fail "no loop note in any of $(ls "$tmp"/cap/req.* 2>/dev/null | wc -l) requests"
fi

# --- 3: the advice names the TRUE cause --------------------------------------
# A wrong cause is a loop AMPLIFIER (M342). For a missing path the advice must be
# "find its real name", NOT M89's verify advice ("try a different fix"), which would
# send the model round again.
if grep -l "does not exist as written" "$tmp"/cap/req.* >/dev/null 2>&1 &&
   ! grep -l "try a DIFFERENT fix" "$tmp"/cap/req.* >/dev/null 2>&1; then
    t_ok "the advice names the real cause, not the verify-stuck advice"
else
    t_fail "wrong advice: $(grep -ho 'NOTE: `[^"]\{0,130\}' "$tmp"/cap/req.* 2>/dev/null | head -1)"
fi

# --- 4: fired at 3, not before ------------------------------------------------
# The third identical failure is the first told; two is a legitimate retry and the 2x
# tail is thick (78 turns on chrtext). Request 3 carries results 1-2, so it must be
# clean; the note appears from request 4 on.
if [ -s "$tmp/cap/req.3" ] && ! grep -q "SAME arguments" "$tmp/cap/req.3"; then
    t_ok "silent at two failures (request 3 is clean)"
else
    t_fail "fired too early -- request 3 already carries the note"
fi

# --- 5: told ONCE, not per round ---------------------------------------------
# One note per loop, not one per key: without suppressing the class slot the fourth
# identical failure would cross the class threshold (4) and say it again.
last=$(ls "$tmp"/cap/req.* 2>/dev/null | sort -t. -k2 -n | tail -1)
n=$(grep -o "SAME arguments" "$last" 2>/dev/null | wc -l | tr -d ' ')
nany=$(grep -o "has now failed" "$last" 2>/dev/null | wc -l | tr -d ' ')
if [ "$n" = "1" ] && [ "$nany" = "1" ]; then
    t_ok "told exactly once in the whole turn (no second note by the class key)"
else
    t_fail "expected 1 note, found exact=$n any=$nany in the final history"
fi

# --- 6: the OPERATOR is told too (M422's lesson) ------------------------------
# A detector that only tells the model leaves the operator's table blank, which is how
# mid-turn thrashing stayed invisible in `runs`.
if grep -q '"event":"tool_loop"' "$tmp/j.jsonl" 2>/dev/null &&
   grep -q '"class":"not_found"' "$tmp/j.jsonl" 2>/dev/null; then
    t_ok "the journal carries a tool_loop event with its failure class"
else
    t_fail "no tool_loop event: $(grep -o '"event":"[a-z_]*"' "$tmp/j.jsonl" 2>/dev/null | sort -u | tr '\n' ' ')"
fi

# --- 7: and it reaches the operator's TABLE, not just the journal --------------
# M422's defect exactly: M89's stuck-detector had two call sites and only one
# journaled, so `runs` showed stuck= blank on the path --verify-every exists for. A
# detector whose finding never reaches the table is one nobody acts on.
if "$BIN" runs "$tmp" --output json < /dev/null 2>/dev/null \
     | grep -q '"tool_loops"'; then
    t_ok "runs --output json reports tool_loops for the run"
else
    t_fail "runs has no tool_loops: $("$BIN" runs "$tmp" --output json < /dev/null 2>&1 | head_bytes 200)"
fi

t_done
