#!/bin/sh
# smoke: a model repeating a POLICY-BLOCKED call is told, and the table shows it (M429).
#
# THE DEFECT THIS EXISTS FOR. Measured on chrtext (probe P13, docs/analysis/
# 2026-08-13-edge-case-probes.md): under --strict-scope a model tried the same
# forbidden shell write FIVE times -- four run_terminal_command, one direct
# write_file at the out-of-scope path -- was never told it was repeating, and spent
# its ENTIRE 150k token budget on an action that could not succeed.
#
# Two things made this invisible to every existing defence:
#   * A block is journaled `blocked: true`, NOT `ok:false`, so M89's stuck-detector
#     and the planned loop detector (both keyed on failures) cannot see it.
#   * A blocked call does not count toward --max-tool-calls, so the tool-call cap
#     never fires either. The run died on tokens instead.
#
# And the standard advice would be WRONG here. M89 tells a stuck run to "try a
# DIFFERENT fix" -- which is exactly what produced five attempts. A forbidden action
# does not succeed however it is rephrased, so the notice has to say so.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# The model keeps trying the same forbidden shell write. That is the real behaviour,
# not a contrivance: P13's model did precisely this, unprompted.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool run_terminal_command {"command":"echo SHELL > outside.txt"}
rule
  count 2
  tool run_terminal_command {"command":"echo SHELL > outside.txt"}
rule
  count 3
  tool run_terminal_command {"command":"echo SHELL > outside.txt"}
rule
  count 4
  tool run_terminal_command {"command":"echo SHELL > outside.txt"}
rule
  text GAVE_UP
EOF

mm_start "$tmp/replies.mm" "$tmp/cap" 6
cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF

out=$(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
      --no-session --auto \
      --edit-scope 'allowed.md' --strict-scope \
      --journal "$tmp/run.jsonl" \
      -p "create the file" < /dev/null 2>&1); rc=$?
mm_stop

# --- 1: the blocks happened at all -------------------------------------------
nblock=$(grep -c '"blocked":true' "$tmp/run.jsonl" 2>/dev/null || true)
if [ "$nblock" -ge 3 ]; then
    t_ok "the forbidden call was blocked $nblock times"
else
    t_fail "rc=$rc only $nblock blocks journaled; out=$(printf '%s' "$out" | tail -c 150)"
fi

# --- 2: THE DEFECT -- the model is told it is repeating ----------------------
# Assert against the mock's CAPTURED REQUESTS, not the transcript: the note travels
# in the tool result inside the next request body, and never appears on stdout. A
# check on the transcript would have passed for the wrong reason (or, as it did
# here, failed while the code was correct).
if grep -rql 'FORBIDDEN by this run' "$tmp/cap" 2>/dev/null; then
    t_ok "the repeat notice reached the model (found in a captured request)"
else
    t_fail "no repeat notice in any captured request after $nblock identical blocks"
fi

# --- 3: and the notice does NOT give the wrong advice ------------------------
# M89's "try a DIFFERENT fix" is right for a fixable failure and wrong here: it is
# what produced five attempts. Pin the distinction, since a future refactor that
# shares one notice between the two paths would silently reintroduce the bug.
if grep -rql 'try a DIFFERENT fix' "$tmp/cap" 2>/dev/null; then
    t_fail "the blocked-repeat notice is giving the stuck-verify advice"
else
    t_ok "the notice does not tell the model to rephrase a forbidden action"
fi

# --- 4: and the operator's table shows it (the M422 lesson) ------------------
# A journal entry no reader surfaces is half a fix -- that was M422's whole point.
if "$BIN" runs "$tmp" 2>/dev/null | grep -q 'blocked='; then
    t_ok "runs shows blocked= for the run"
else
    t_fail "runs printed no blocked= note: $("$BIN" runs "$tmp" 2>/dev/null | tail -2 | tr '\n' ' ')"
fi

t_done
