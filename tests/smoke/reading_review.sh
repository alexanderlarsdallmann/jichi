#!/bin/sh
# smoke: the reading-for-review grader (74-read-the-turn, M627) grades a
# WRITTEN reading of jichi's own source on two tiers -- a structural floor
# that runs anywhere, and an anchor-resolution gate that runs only where the
# real tree is reachable.
#
# WHY TWO TIERS. The task's subject is the real program, so the honest check
# is "do the file:symbol anchors you cite actually exist?" -- but the e2e
# two-sided proof (curriculum_graders.py) runs graders in a COPY of
# docs/assignments with no src/ beside it. A grader that refused there could
# never be proven two-sided; one that pretended to resolve anchors it cannot
# see would be a hollow gate. So the floor (sections, the named subject, both
# concrete providers, a recorded trace, enough well-formed anchors) is what the
# proof rests on, and the resolution gate STRENGTHENS the grade when src/ is
# present -- M625's "grade what you can run here", applied to one check rather
# than the whole verdict. Checks 7-8 build a fake tree to exercise the gate.
. "$(dirname "$0")/_smoke.sh"

t_plan 10
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
TASK="74-read-the-turn"
SPEC="$SMOKE_ROOT/docs/assignments/$TASK.md"
SOL="$SMOKE_ROOT/docs/assignments/$TASK.solution.md"
mkdir -p "$ws/docs/assignments/$TASK"
cp "$SPEC" "$ws/docs/assignments/" 2>/dev/null
cp "$SMOKE_ROOT/docs/assignments/$TASK/test.sh" "$ws/docs/assignments/$TASK/" 2>/dev/null
# the reference READING.md, extracted from the solution walkthrough -- the same
# extraction curriculum_graders.py performs, so the two cannot drift
sed -n '/<!-- READING.md -->/,/<!-- \/READING.md -->/p' "$SOL" 2>/dev/null \
    | sed '1d;$d' > "$tmp/reference.md"
R="$ws/docs/assignments/$TASK/READING.md"
# Two ways in. `g` is the PRODUCT (`jichi grade`) -- the verdict and exit code
# a learner sees. `t` is the grader SCRIPT itself -- its FAIL/PASS lines, which
# `grade` folds into a test report and does not echo, so a check on the
# grader's WORDING has to run the script (from the workspace root, as verify
# does). Both cd/cwd the same way, so both see the same tree.
g() { (cd "$ws" && with_deadline 20 "$BIN" grade "docs/assignments/$TASK.md" "$@" < /dev/null 2>&1); }
t() { (cd "$ws" && with_deadline 20 sh "docs/assignments/$TASK/test.sh" < /dev/null 2>&1); }

# --- 1: pristine (no READING.md) fails ----------------------------------------------
out=$(t); rc=$?
if [ "$rc" -eq 1 ] && printf '%s' "$out" | grep -q "READING.md"; then
    t_ok "no READING.md: FAIL, and the message names the deliverable"
else
    t_fail "pristine rc=$rc: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 160)"
fi

# --- 2: the authoring red-proof holds on the untouched tree -------------------------
out=$(g --expect-fail); rc=$?
if [ "$rc" -eq 0 ] && printf '%s' "$out" | grep -q "RED as expected"; then
    t_ok "--expect-fail: the gate can fail on the untouched tree"
else
    t_fail "expect-fail rc=$rc: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 160)"
fi

# --- 3: prose with none of the four sections fails, naming the first missing one ----
# long enough to clear the emptiness floor, so the SECTION check is what fires
awk 'BEGIN{for(i=0;i<14;i++)print "I read the agent loop and it seemed fine, line " i "."}' > "$R"
out=$(t); rc=$?
if [ "$rc" -eq 1 ] && printf '%s' "$out" | grep -q "Abstraction to concrete"; then
    t_ok "a sectionless reading fails and is told which section is missing"
else
    t_fail "stub rc=$rc: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 160)"
fi

# --- 4: the reference passes on the structural floor (no src/ here) ---------------
cp "$tmp/reference.md" "$R"
out=$(g); rc=$?
sout=$(t)
if [ "$rc" -eq 0 ] && printf '%s' "$out" | grep -q "PASS" && printf '%s' "$sout" | grep -q "form only"; then
    t_ok "the reference reading passes through jichi grade (no src/ beside the copy: the script says form-only)"
else
    t_fail "reference rc=$rc: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 200)"
fi

# --- 5: three readings of four is not a review --------------------------------------
sed '/^## Execution/,$d' "$tmp/reference.md" > "$R"
out=$(t); rc=$?
if [ "$rc" -eq 1 ] && printf '%s' "$out" | grep -q "Execution"; then
    t_ok "dropping the Execution reading fails (the compound grader is not hollow)"
else
    t_fail "3-of-4 rc=$rc: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 160)"
fi

# --- 6: a reading that never names the turn's entry point fails ---------------------
sed 's/jc_agent_run_turn/the_turn_function/g' "$tmp/reference.md" > "$R"
out=$(g); rc=$?
if [ "$rc" -eq 1 ]; then
    t_ok "a reading that never names jc_agent_run_turn fails"
else
    t_fail "no-subject rc=$rc: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 160)"
fi

# --- 7-8: with a tree present, cited anchors must RESOLVE ---------------------------
# A fake tree built FROM the reference's own anchors: every cited symbol is
# written into its cited file, so the reference resolves completely.
grep -oE '[A-Za-z0-9_./-]*[A-Za-z0-9_]\.[ch]:[A-Za-z_][A-Za-z0-9_]*' "$tmp/reference.md" \
    | sort -u | while IFS=: read -r f s; do
        mkdir -p "$ws/$(dirname "$f")"
        printf '%s\n' "$s" >> "$ws/$f"
    done
cp "$tmp/reference.md" "$R"
out=$(t); rc=$?
if [ "$rc" -eq 0 ] && printf '%s' "$out" | grep -q "resolved against the tree"; then
    t_ok "with src/ present the reference's anchors all resolve and it passes"
else
    t_fail "resolution pass rc=$rc: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 200)"
fi
{ cat "$tmp/reference.md"; printf '\nAlso see src/chat/jc_agent.c:no_such_symbol_xyz for the retry.\n'; } > "$R"
out=$(t); rc=$?
if [ "$rc" -eq 1 ] && printf '%s' "$out" | grep -q "no_such_symbol_xyz"; then
    t_ok "an invented anchor fails, named, when the tree is there to check it"
else
    t_fail "fake anchor rc=$rc: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 200)"
fi
# --- 9: a reading with too few anchors fails (the floor a tooth found unguarded) ----
sed 's/\.c:/.c /g; s/\.h:/.h /g' "$tmp/reference.md" > "$R"
out=$(t); rc=$?
if [ "$rc" -eq 1 ] && printf '%s' "$out" | grep -q "anchor"; then
    t_ok "stripping the file:symbol anchors fails on the anchor floor"
else
    t_fail "anchorless rc=$rc: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 160)"
fi

# --- 10: one concrete provider of two is a guess about the other -------------------
sed 's/an_build_request/the_other_one/g' "$tmp/reference.md" > "$R"
out=$(t); rc=$?
if [ "$rc" -eq 1 ] && printf '%s' "$out" | grep -q "BOTH"; then
    t_ok "naming one build_request implementation of two fails"
else
    t_fail "one-provider rc=$rc: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 160)"
fi
t_done
