#!/bin/sh
# smoke: the model is told when its own edit moved the goalpost (M435).
#
# THE MEASURED GAP. M88 has detected a test-assertion edit since long before this, and
# routed the finding to four places: the journal, telemetry (M417), a WARN on stderr, and
# the run's verdict, which M410 renders as TAINTED. Every one of those four is read by
# the OPERATOR, after the fact. The party that just moved the goalpost -- the model --
# was told nowhere. In ANECDOTES #51 that is exactly what happened: the WARN fired TEN
# times in one run while the model kept adjusting expectations, because nothing in the
# tool result it read said anything had been noticed.
#
# So this is the same asymmetry the whole M431 line is about: jichi shows the model its
# failures and shows the human its false successes. One sentence in the tool result costs
# nothing and lands at the moment of the act.
#
# WHY THE NOTE IS NOT A REFUSAL. Fixing a genuinely wrong test is fair work, and a
# refusal would teach the model to route around the file tools -- an `edit_file` becomes
# a `sed` under run_terminal_command, which the M83 out-of-scope guard notices only at
# turn end, and which M88 does not see at all. The note says RECORDED and DOES NOT COUNT
# and offers the honest exit; check 4 pins that, as an absence.
#
# TWO-SIDED, MEASURED. With the two `if (gpnote[0] ...)` appends stubbed out, checks 2, 3,
# 4 and 6 fail and 1 and 5 pass -- 1 because the edit still applies and 5 because the
# journal event is M88's and predates this. Four of six, not five: the number here is the
# one observed, since a "shown to fail" claim with a guessed count is not a measurement.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# A test file, so the M88 path filter (`test`/`spec` in the path) matches, holding an
# assertion whose expected value the model will then change. Both sides must read like
# an assertion and the new text must not CONTAIN the old, or M88 treats it as a test
# being added rather than moved -- which is the correct distinction and the reason this
# fixture replaces a value instead of appending a case.
mkdir -p "$ws/tests"
cat > "$ws/tests/test_sum.c" <<'EOF'
void test_sum(void)
{
    assert(sum(2, 2) == 4);
    assert(sum(0, 0) == 0);
}
EOF

# Two assertion edits, so both the first-time note and the escalation are exercised in
# one run. The second names a count, which is what makes the pattern legible.
# The first call must be a READ: edit_file honours the read-before-edit guard, so an
# edit to an unread file is refused and no M88 detection happens at all. The first cut of
# this driver skipped it and all six checks failed for that reason rather than the one
# under test -- a fixture failing for the wrong reason is a green waiting to happen.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"tests/test_sum.c"}
rule
  count 2
  tool edit_file {"path":"tests/test_sum.c","old_string":"assert(sum(2, 2) == 4);","new_string":"assert(sum(2, 2) == 5);"}
rule
  count 3
  tool edit_file {"path":"tests/test_sum.c","old_string":"assert(sum(0, 0) == 0);","new_string":"assert(sum(0, 0) == 1);"}
rule
  text GOALPOST_DONE
EOF

mm_start "$tmp/replies.mm" "$tmp/cap" 9
write_config "$tmp/config.json" "$MM_PORT"

(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" \
    -q --no-session --auto --budget-tokens 200k \
    --journal "$tmp/j.jsonl" \
    -p "make the tests pass" < /dev/null) > /dev/null 2>&1; rc=$?
mm_stop

# --- 1: advisory -- the edit is applied and the run completes -------------------
# The note must not be a refusal in behaviour either, not merely in wording.
if [ "$rc" -eq 0 ] && grep -q "== 5" "$ws/tests/test_sum.c"; then
    t_ok "the edit is applied and the run completes: the note is advisory"
else
    t_fail "rc=$rc, file now: $(head -4 "$ws/tests/test_sum.c" | tr '\n' ' ')"
fi

# --- 2: the MODEL is told, in the tool result ----------------------------------
# The tool result travels inside the NEXT request body, so the captured requests are the
# only ground truth for "the model was told" -- the M429 lesson, and the reason this
# does not assert on stderr, which is where M88 already spoke to the operator.
if grep -l "TEST ASSERTION" "$tmp"/cap/req.* >/dev/null 2>&1; then
    t_ok "the tool result tells the model it changed a test assertion"
else
    t_fail "no note in any of $(ls "$tmp"/cap/req.* 2>/dev/null | wc -l) captured requests"
fi

# --- 3: it names the file, and says the change is recorded ---------------------
if grep -l "tests/test_sum.c" "$tmp"/cap/req.* >/dev/null 2>&1 &&
   grep -l "does not count" "$tmp"/cap/req.* >/dev/null 2>&1; then
    t_ok "it names the file and says the change does not count"
else
    t_fail "note incomplete: $(grep -ho 'NOTE: that edit[^"]\{0,120\}' "$tmp"/cap/req.* 2>/dev/null | head -1)"
fi

# --- 4: it is NOT a prohibition ------------------------------------------------
# Checked as an absence, because the wording is the whole difference between a rule the
# model can comply with honestly and one it routes around with a shell command.
if ! grep -l "forbidden" "$tmp"/cap/req.* >/dev/null 2>&1 &&
   grep -l "genuinely wrong" "$tmp"/cap/req.* >/dev/null 2>&1; then
    t_ok "the legitimate case is left open (no prohibition, an honest exit offered)"
else
    t_fail "the note reads as a refusal, which invites routing around the file tools"
fi

# --- 5: the operator still gets the journal event (M88, unchanged) -------------
# The factoring in M435 moved five side effects into one reporter; this proves none was
# dropped on the way. Two edits, so two events.
n=$(grep -c '"event":"test_assertion_edit"' "$tmp/j.jsonl" 2>/dev/null)
[ -n "$n" ] || n=0
if [ "$n" -ge 2 ]; then
    t_ok "both edits are still journaled for the operator ($n events)"
else
    t_fail "expected 2 test_assertion_edit events, found $n"
fi

# --- 6: the second edit escalates and names the count -------------------------
# Deliberately not once-per-turn: each assertion edit is a distinct decision rather than
# a repetition of one, so the M432 throttle does not apply here. What must hold is that
# the second note is different from the first and states the consequence.
if grep -l "changed 2 test assertions" "$tmp"/cap/req.* >/dev/null 2>&1 &&
   grep -l "tainted" "$tmp"/cap/req.* >/dev/null 2>&1; then
    t_ok "the second edit escalates, names the count, and names the tainted verdict"
else
    t_fail "no escalation: $(grep -ho 'NOTE: this run[^"]\{0,110\}' "$tmp"/cap/req.* 2>/dev/null | head -1)"
fi

t_done
