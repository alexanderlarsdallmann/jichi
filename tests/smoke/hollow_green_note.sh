#!/bin/sh
# smoke: a hollow mid-run green is reported to the MODEL (M351).
#
# The loop feeds every RED verify back (fix-forward), so the model always
# hears its failures -- but M86's finding that a GREEN ran fewer tests than
# an earlier green went to the operator alone (log, journal, on_status),
# while the model banked the false confidence at the periodic checkpoint and
# built on "tests pass" (the 253-greens-over-six-compile-errors class). M351
# injects one [envelope] note at the periodic-verify boundary, once per run.
#
# The gate is stateful: green with "5 passed" on its first run, green with
# "2 passed" after -- so periodic verify #1 sets the high-water and #2 trips
# FEWER_TESTS. Rounds are write_file calls because the periodic verify runs
# only in a snapshotted turn (M81), hence git + snapshots on; selfReview off
# keeps the request numbering deterministic (the M349 driver's lesson).
. "$(dirname "$0")/_smoke.sh"

command -v git >/dev/null 2>&1 || t_skip "git not on PATH"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/gate.sh" <<EOF
#!/bin/sh
c=\$(cat "$tmp/n" 2>/dev/null || echo 0)
c=\$((c + 1))
echo "\$c" > "$tmp/n"
if [ "\$c" -eq 1 ]; then echo "5 passed"; else echo "2 passed"; fi
exit 0
EOF

cat > "$tmp/h.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"a.txt","content":"one\n"}
rule
  count 2
  tool write_file {"path":"b.txt","content":"two\n"}
rule
  count 3
  tool write_file {"path":"c.txt","content":"three\n"}
rule
  text HOLLOW_DONE
EOF
mm_start "$tmp/h.mm" "$tmp/cap" 6

cat > "$tmp/c.json" <<EOF
{"lowResource":false,"mode":"auto","selfReview":false,"models":[
  {"name":"m","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0}
EOF

(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/c.json" --auto --no-lite \
    --no-session --verify "sh $tmp/gate.sh" --verify-every 1 \
    --verify-retries 0 -p "write the three files" < /dev/null) \
    >/dev/null 2>"$tmp/err"
rc=$?
mm_stop

# --- 1: the first green (high-water 5) raises no note (M310 pairing) ----------
if [ -s "$tmp/cap/req.2" ] && \
   ! grep -q "the verifier just PASSED" "$tmp/cap/req.2"; then
    t_ok "a sane first green stays quiet"
else
    t_fail "request 2 missing or already carries a sanity note"
fi

# --- 2: the shrunken green tells the model ------------------------------------
if grep -q "the verifier just PASSED" "$tmp/cap/req.3" 2>/dev/null && \
   grep -q "ran 2 tests where an earlier green ran 5" "$tmp/cap/req.3" \
        2>/dev/null; then
    t_ok "the model is told: this green ran 2 tests where 5 ran before"
else
    t_fail "no hollow-green note in request 3: $(head_bytes 200 "$tmp/cap/req.3" 2>/dev/null)"
fi

# --- 3: once per run -- the third (also hollow) green does not re-inject ------
n=$(grep -o "the verifier just PASSED" "$tmp/cap/req.4" 2>/dev/null \
    | wc -l | tr -d ' ')
if [ "$n" = "1" ]; then
    t_ok "exactly one sanity note in the history (once per run)"
else
    t_fail "expected 1 note in request 4, found '$n'"
fi

# --- 4: advisory only -- the run still lands green ------------------------------
if [ "$rc" -eq 0 ]; then
    t_ok "the note changed no outcome (exit 0)"
else
    t_fail "run exited $rc: $(head_bytes 200 "$tmp/err")"
fi

# =============================================================================
# M431d: the COMPLETION green -- the case that had no next model call at all.
#
# M351 (checks 1-4 above) only reaches the model at a PERIODIC verify boundary,
# because the completion site passed hist=NULL for an honest reason: a green ends
# the run, so there was nowhere to put the note. The consequence was that WITHOUT
# --verify-every -- the default -- a hollow green was invisible to the model in
# EVERY run, and it banked "tests pass" as fact.
#
# The fix makes a next call exist: on a completion green whose gate looks hollow,
# the model gets ONE more round (latched by sanity_noticed, so it cannot loop) and
# the gate RE-RUNS on that round, because the model may edit files in it.
#
# Note NO --verify-every below. Two-sided: pre-fix no captured request carries the
# note at all, and the gate runs exactly once.
# =============================================================================

rm -f "$tmp/n2"
cat > "$tmp/gate2.sh" <<EOF
#!/bin/sh
c=\$(cat "$tmp/n2" 2>/dev/null || echo 0)
c=\$((c + 1))
echo "\$c" > "$tmp/n2"
echo "0 tests ran"
exit 0
EOF

# One tool call so the turn is snapshotted (the completion gate needs it), then
# the model answers -- which is where the completion verify fires.
cat > "$tmp/h2.mm" <<'EOF'
wire openai
rule
  count 1
  tool write_file {"path":"d.txt","content":"four"}
rule
  text COMPLETION_DONE
EOF
mm_start "$tmp/h2.mm" "$tmp/cap2" 6

# A FRESH config: mm_start allocates a new port, and reusing c.json would point
# this run at the first mock, which has already exited.
cat > "$tmp/c2.json" <<EOF
{"lowResource":false,"mode":"auto","selfReview":false,"models":[
  {"name":"m","provider":"openai","model":"mock",
   "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":true,"repoMap":false,"references":false,"maxRetries":0}
EOF

(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/c2.json" --auto --no-lite \
    --no-session --verify "sh $tmp/gate2.sh" \
    --verify-retries 0 -p "write one file" < /dev/null) \
    >/dev/null 2>"$tmp/err2"
rc2=$?
mm_stop

# --- 5: the model IS told, with no --verify-every ------------------------------
if grep -l "the verifier just PASSED" "$tmp"/cap2/req.* >/dev/null 2>&1; then
    t_ok "a hollow COMPLETION green reaches the model (no --verify-every needed)"
else
    t_fail "no note in any of $(ls "$tmp"/cap2/req.* 2>/dev/null | wc -l) requests -- the completion green stayed silent"
fi

# --- 6: it says the green verified nothing -------------------------------------
if grep -lq "while running 0 tests" "$tmp"/cap2/req.* 2>/dev/null ||
   grep -l "verifies nothing" "$tmp"/cap2/req.* >/dev/null 2>&1; then
    t_ok "the note states the green verified nothing"
else
    t_fail "note text unexpected: $(grep -ho 'the verifier just PASSED[^"]\{0,70\}' "$tmp"/cap2/req.* 2>/dev/null | head -1)"
fi

# --- 7: the extra round RE-GATES (the model may have edited in it) -------------
# Exactly two gate runs: the one that found the hollow green, and the one that
# re-checks the tree after the extra round. Skipping the second would let that
# round go ungated; a third would mean the latch failed.
g=$(cat "$tmp/n2" 2>/dev/null || echo 0)
if [ "$g" = "2" ]; then
    t_ok "the gate ran twice: the extra round is re-verified, and did not loop"
else
    t_fail "gate ran $g time(s), expected 2 (1 = no extra round, >2 = the latch failed)"
fi

# --- 8: still advisory -- the outcome is unchanged ------------------------------
if [ "$rc2" -eq 0 ]; then
    t_ok "the extra round changed no outcome (exit 0)"
else
    t_fail "run exited $rc2: $(head_bytes 200 "$tmp/err2")"
fi

t_done
