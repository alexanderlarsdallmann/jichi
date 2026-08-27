#!/bin/sh
# smoke: `grade --expect-fail` proves an assignment's gate CAN fail (M412).
#
# THE DEFECT THIS EXISTS FOR. jichi's own curriculum proves every grader
# two-sided in CI (curriculum_graders.py: red on the untouched fixture, green
# on the reference solution). An assignment authored into ANY OTHER project
# inherits no such proof -- and the first one authored in anger (zigodot,
# 2026-08-12) shipped with `verify: zig build test`, which was ALREADY GREEN:
# `jichi grade` said PASS at 100% with the target function still panicking.
# A gate that cannot fail grades nothing (docs/TEST_INTEGRITY.md; the M86
# hollow-gate family).
#
# `grade --expect-fail <spec>` is the two-sided proof's red half, handed to any
# project as one command: run the verify on the CURRENT (untouched) tree and
# succeed only if it FAILS. Authors run it right after writing a spec, before
# a learner spends a token on it.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
cd "$tmp" || exit 1

cat > red.md <<'EOF'
---
title: red gate
verify: "test -f does-not-exist.txt"
points: 1
---
The gate demands a file nobody has created yet.
EOF

cat > green.md <<'EOF'
---
title: hollow gate
verify: "true"
points: 1
---
The gate is already green: it can never fail, so it grades nothing.
EOF

# --- 1+2: a red gate is the SUCCESS case ------------------------------------
out=$(with_deadline 40 "$BIN" grade red.md --expect-fail < /dev/null 2>&1); rc=$?
if [ "$rc" -eq 0 ]; then
    t_ok "a gate that fails on the untouched tree exits 0 under --expect-fail"
else
    t_fail "rc=$rc for a red gate: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 140)"
fi
case "$out" in
    *"RED as expected"*) t_ok "the output says the gate can fail" ;;
    *) t_fail "no 'RED as expected' line: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 140)" ;;
esac

# --- 3+4: an already-green gate is called what it is -------------------------
out=$(with_deadline 40 "$BIN" grade green.md --expect-fail < /dev/null 2>&1); rc=$?
if [ "$rc" -eq 1 ]; then
    t_ok "an already-green gate exits 1 under --expect-fail"
else
    t_fail "rc=$rc for a hollow gate (want 1)"
fi
case "$out" in
    *HOLLOW*) t_ok "the output names the hollow gate" ;;
    *) t_fail "no HOLLOW verdict: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 140)" ;;
esac

# --- 5: --record is refused -- a red-proof is not a learner grade ------------
out=$(with_deadline 40 "$BIN" grade red.md --expect-fail --record \
      < /dev/null 2>&1); rc=$?
if [ "$rc" -eq 2 ] && [ ! -e .jichi/progress.jsonl ]; then
    t_ok "--expect-fail refuses --record (nothing lands in progress.jsonl)"
else
    t_fail "rc=$rc, progress file: $(ls .jichi 2>/dev/null | tr '\n' ' ')"
fi

# ---- 6-8: A GATE THAT CANNOT RUN IS NOT A GRADE (M502) ---------------------
# THE DEFECT. `grade` ran the verify and reported the verdict, full stop. So a
# spec graded from the WRONG DIRECTORY printed `FAIL / score: 0%` -- measured:
# the shell could not open the script, exited 2, and a learner saw a failing
# grade on correct work with no way to tell the difference. Under
# --expect-fail it was worse than useless: a gate red only because its script
# is missing was reported as "RED as expected", certifying exactly the hollow
# gate that check exists to catch.
mkdir -p sub/fix
cat > sub/fix/test.sh <<'EOS'
#!/bin/sh
exit 1
EOS
cat > harness.md <<'EOS'
---
title: a gate whose script lives beside it
verify: "sh sub/fix/test.sh"
points: 1
---
The verify path is relative to the directory grade runs in.
EOS

out=$(with_deadline 40 "$BIN" grade harness.md < /dev/null 2>&1); rc=$?
if [ "$rc" -eq 1 ] && ! printf '%s' "$out" | grep -q 'cannot run from here'; then
    t_ok "from the right directory the gate still grades normally (FAIL, rc=1)"
else
    t_fail "a runnable gate was misreported (rc=$rc): \
$(printf '%s' "$out" | tr '\n' ' ' | head_bytes 160)"
fi

# The same spec, graded from one directory down: the script is unreachable.
mkdir -p elsewhere
cp harness.md elsewhere/harness.md
out=$(cd elsewhere && with_deadline 40 "$BIN" grade harness.md < /dev/null 2>&1)
rc=$?
if [ "$rc" -eq 2 ] && printf '%s' "$out" | grep -q 'cannot run from here' &&
   printf '%s' "$out" | grep -q 'NOT a grade'; then
    t_ok "an unreachable verify script is refused, not scored 0%"
else
    t_fail "grading from the wrong directory produced rc=$rc and no harness \
diagnosis -- a learner sees FAIL on correct work: \
$(printf '%s' "$out" | tr '\n' ' ' | head_bytes 200)"
fi

# And the hollow-red case: --expect-fail must not certify a missing harness.
out=$(cd elsewhere && with_deadline 40 "$BIN" grade harness.md --expect-fail \
      < /dev/null 2>&1); rc=$?
if [ "$rc" -eq 2 ] && ! printf '%s' "$out" | grep -q 'RED as expected'; then
    t_ok "--expect-fail refuses to call a missing script a proven red gate"
else
    t_fail "a gate red only because its script is absent was certified \
(rc=$rc): $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 200)"
fi

t_done
