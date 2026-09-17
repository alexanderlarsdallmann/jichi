#!/bin/sh
# smoke: a verify that DECLARES it cannot run (exit 77) is a refusal on every
# grading surface -- grade, grade --expect-fail, attempt, improve --attempt --
# never a grade (M625; the row M624 deferred).
#
# THE SEAM. M502/M615 refuse a verify whose PROGRAM is unreachable, but a
# course test.sh is always reachable -- its toolchain guard ("rustc is not
# usable") used to exit 1, so a learner without the toolchain recorded
# FAIL 0% / passed:false into the permanent progress file for a property of
# the MACHINE, not their work. M615's doctrine: cannot-run is a refusal,
# never a grade. The contract: verify exit 77 (automake's SKIP convention)
# means "cannot run here". 77 and not the obvious 2, because 12 shipped specs
# use `grep`/`[` bare as the verify and both exit 2 on operational errors --
# a missing learner file must grade FAIL, not refuse.
. "$(dirname "$0")/_smoke.sh"

t_plan 10
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
mkdir -p "$ws/docs/assignments/cr"

cat > "$ws/docs/assignments/cr/test.sh" <<'EOF'
#!/bin/sh
cd "$(dirname "$0")" || exit 1
frobc --version >/dev/null 2>&1 || { echo "CANNOT RUN: frobc is not usable -- install Frob"; exit 77; }
echo "PASS: ok"
EOF
cat > "$ws/docs/assignments/cr.md" <<'EOF'
---
title: toolchain fixture
audience: student
verify: "sh docs/assignments/cr/test.sh"
points: 1
---
Needs the frobc toolchain, which does not exist anywhere.
EOF

# --- 1-3: `grade` refuses -- exit 2, the script's own words, nothing recorded ----
out=$(cd "$ws" && with_deadline 20 "$BIN" grade docs/assignments/cr.md --record \
      < /dev/null 2>&1); rc=$?
if [ "$rc" -eq 2 ] && printf '%s' "$out" | grep -q "NOT a grade"; then
    t_ok "grade exits 2 on a self-declared cannot-run (a refusal, not a grade)"
else
    t_fail "grade rc=$rc (want 2): $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 160)"
fi
case "$out" in
    *"frobc is not usable"*) t_ok "the refusal carries the script's own reason" ;;
    *) t_fail "reason not surfaced: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 160)" ;;
esac
if [ ! -f "$ws/.jichi/progress.jsonl" ]; then
    t_ok "--record wrote nothing for the refusal"
else
    t_fail "a non-grade was recorded: $(head_bytes 120 "$ws/.jichi/progress.jsonl")"
fi

# --- 4: an ordinary exit-1 FAIL still grades FAIL (the boundary holds) -----------
cat > "$ws/docs/assignments/rf.md" <<'EOF'
---
title: really failing
verify: "sh docs/assignments/cr/fail.sh"
points: 1
---
The gate fails honestly.
EOF
printf '#!/bin/sh\necho "FAIL: wrong answer"\nexit 1\n' > "$ws/docs/assignments/cr/fail.sh"
out=$(cd "$ws" && with_deadline 20 "$BIN" grade docs/assignments/rf.md --record \
      < /dev/null 2>&1); rc=$?
if [ "$rc" -eq 1 ] && printf '%s' "$out" | grep -q "FAIL" \
   && [ -f "$ws/.jichi/progress.jsonl" ]; then
    t_ok "an honest exit-1 FAIL still grades and records FAIL"
else
    t_fail "boundary moved (rc=$rc): $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 160)"
fi

# --- 5: --expect-fail refuses too -- a gate that cannot RUN proves nothing -------
# Pre-M625 this printed "RED as expected" (exit 0): the authoring red-proof
# reading a missing toolchain as evidence the gate can fail -- M624's "green
# because it never ran", mirrored.
out=$(cd "$ws" && with_deadline 20 "$BIN" grade docs/assignments/cr.md \
      --expect-fail < /dev/null 2>&1); rc=$?
if [ "$rc" -eq 2 ] && ! printf '%s' "$out" | grep -q "RED as expected"; then
    t_ok "--expect-fail refuses a cannot-run instead of calling it RED"
else
    t_fail "false red-proof (rc=$rc): $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 160)"
fi

# --- 6-8: `attempt` refuses after the run -- exit 2, said so, nothing recorded ---
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  text NOTHING_TO_DO
EOF
mm_start "$tmp/replies.mm" "$tmp/cap"
cat > "$tmp/config.json" <<EOF
{"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
"snapshots":true,"assignments":true,"repoMap":false,"references":false,
"toolProfile":"full","lowResource":false,"maxRetries":0}
EOF
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      attempt docs/assignments/cr.md --record < /dev/null 2>&1); rc=$?
if [ "$rc" -eq 2 ]; then
    t_ok "attempt exits 2 on a self-declared cannot-run"
else
    t_fail "attempt rc=$rc (want 2): $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 160)"
fi
case "$out" in
    *"NOT a grade"*) t_ok "the attempt refusal says so in M502's words" ;;
    *) t_fail "no refusal wording: $(printf '%s' "$out" | tr '\n' ' ' | head_bytes 160)" ;;
esac
# check 4 recorded rf.md, so the file exists; the refusal must not be in it.
if ! grep -q "cr.md" "$ws/.jichi/progress.jsonl" 2>/dev/null; then
    t_ok "attempt --record wrote nothing for the refusal"
else
    t_fail "a non-grade was recorded: $(head_bytes 160 "$ws/.jichi/progress.jsonl")"
fi

# --- 9-10: improve --attempt skips it BEFORE spending a model turn ---------------
ws2=$(smoke_tmp)
mkdir -p "$ws2/specs/cr"
cp "$ws/docs/assignments/cr/test.sh" "$ws2/specs/cr/test.sh"
cat > "$ws2/specs/cr.md" <<'EOF'
---
title: toolchain fixture
audience: agent
verify: "sh specs/cr/test.sh"
points: 1
---
Needs the frobc toolchain, which does not exist anywhere.
EOF
nreq_before=$(ls "$tmp/cap" 2>/dev/null | grep -cv '^\.port$' || true)
(cd "$ws2" && with_deadline 120 "$BIN" --config "$tmp/config.json" \
    improve specs --attempt < /dev/null > /dev/null 2>"$tmp/ierr"); irc=$?
mm_stop
if grep -q "cannot run" "$tmp/ierr"; then
    t_ok "improve --attempt names the cannot-run spec instead of attempting it"
else
    t_fail "no cannot-run line (rc=$irc): $(tr '\n' ' ' < "$tmp/ierr" | head_bytes 200)"
fi
nreq_after=$(ls "$tmp/cap" 2>/dev/null | grep -cv '^\.port$' || true)
if [ "$nreq_after" -eq "$nreq_before" ]; then
    t_ok "no model request was spent on the unattemptable spec"
else
    t_fail "model was called for a spec that cannot be graded here ($nreq_before -> $nreq_after)"
fi
t_done
