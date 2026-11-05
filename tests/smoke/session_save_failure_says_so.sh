#!/bin/sh
# smoke: a FAILED per-turn session save says so (M1082, finding F-02).
#
# The finding: every per-turn `jc_session_save` call site ignored its return,
# so a save that could not write the file left the turn's work unrecoverable
# while every channel a caller reads said success (rc=0, EMPTY stderr, the
# session file byte-identical). The confirmation report (2026-10-29-F02-
# confirm.md) reproduces it: one headless mock-model turn with the write fault
# armed -> rc=0, empty stderr, turn lost.
#
# What this driver asserts: with the monotone write fault armed (JICHI_FAULT_
# WRITE_AFTER=0), a second headless turn of an already-saved, RESUMED session
#   (a) still answers (a failed save must not eat the turn),
#   (b) names the failed save on stderr -- the session's actual file and the
#       reason, via jc_status_str,
#   (c) does it ONCE (the per-turn save fires the line; the exit save, sharing
#       the same session, must not repeat it), and
#   (d) leaves the on-disk session without the second turn's content (proving
#       the save was genuinely refused, not just misreported).
# A first, clean run of the same shape must NOT print the diagnostic.
#
# Requires a FAULT=1 binary (`make clean && make WERROR=1 FAULT=1 jichi
# smoke-tools`); it skips on any other build, so a normal `make smoke` is
# unaffected. It is named in tests/smoke/run.sh (so a plain `make smoke`
# reaches it, but it skips there on a non-FAULT build) and in the Makefile's
# smoke-faults target (M1087, SV:1804 GAP 1), which builds the FAULT=1 binary
# it needs.
. "$(dirname "$0")/_smoke.sh"

# A FAULT build announces itself in --version -- a far more reliable probe
# than inferring it from injected behaviour.
"$BIN" --version < /dev/null 2>/dev/null | grep -q "FAULT=1" \
    || t_skip "requires the FAULT=1 build (make clean && make WERROR=1 FAULT=1 jichi smoke-tools)"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
store="$HOME/.jichi.d/sessions"

# The mock: a single-shot reply, good for either run.
cat > "$tmp/mm1.mm" <<'EOF'
wire openai
rule
  count 1
  text ONE_DONE
EOF

# ---- run 1: clean, the session lands on disk --------------------------------
mm_start "$tmp/mm1.mm" "$tmp/cap1" 4
write_config "$tmp/c1.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/c1.json" -p "hello one" \
    < /dev/null > "$tmp/r1.out" 2> "$tmp/r1.err"); rc1=$?
mm_stop

# ---- run 2: the same session, resumed, with EVERY write faulted -------------
mm_start "$tmp/mm1.mm" "$tmp/cap2" 4
write_config "$tmp/c2.json" "$MM_PORT"
(cd "$ws" && with_deadline 60 env JICHI_FAULT_WRITE_AFTER=0 "$BIN" \
    --config "$tmp/c2.json" --resume -p "hello two" \
    < /dev/null > "$tmp/r2.out" 2> "$tmp/r2.err"); rc2=$?
mm_stop

# The session run 1 saved (exactly one file in the store).
sid=$(ls "$store" 2>/dev/null | head -1)

# ---- 1: run 1 answered and really saved its session --------------------------
if [ "$rc1" -eq 0 ] && grep -q ONE_DONE "$tmp/r1.out" && [ -n "$sid" ]; then
    t_ok "clean run answers and the session file landed"
else
    t_fail "clean run did not answer or save (rc=$rc1, stdout: $(head_bytes 80 "$tmp/r1.out"), store: $(ls "$store" 2>/dev/null | tr '\n' ' '))"
fi

# ---- 2: the clean run printed NO save-failure diagnostic ----------------------
if grep -q 'could not save the session' "$tmp/r1.err"; then
    t_fail "the clean run printed a save-failure diagnostic it never hit: $(grep 'could not save' "$tmp/r1.err" | head -2 | tr '\n' ' ' | head_bytes 200)"
else
    t_ok "clean run prints no save-failure diagnostic"
fi

# ---- 3: run 2 STILL answered (a failed save must not eat the turn) -----------
if [ "$rc2" -eq 0 ] && grep -q ONE_DONE "$tmp/r2.out"; then
    t_ok "fault-armed run still answers (the turn is not lost from memory)"
else
    t_fail "fault-armed run rc=$rc2, stdout: $(head_bytes 120 "$tmp/r2.out")"
fi

# ---- 4: stderr NAMES the failed save -- the session's path AND the reason ----
# The path is the session's ACTUAL file, so a diagnostic that names a guessed
# (different) file cannot pass; the reason is jc_status_str's "i/o error".
if [ -n "$sid" ] && grep -q "could not save the session" "$tmp/r2.err" \
   && grep -q "sessions/$sid" "$tmp/r2.err" \
   && grep -q "i/o error" "$tmp/r2.err"; then
    t_ok "the failed save is named: path + reason ($(grep 'could not save' "$tmp/r2.err" | head -1 | head_bytes 160))"
else
    t_fail "run 2's stderr does not name the failed save (sid=$sid): $(tr '\n' ' ' < "$tmp/r2.err" | head_bytes 300)"
fi

# ---- 5: the on-disk session still holds NO trace of the second turn ---------
# The denominator for check 4: without this, a diagnostic printed over a
# session that actually saved would pass for the wrong reason. A refused
# atomic replace leaves the old file untouched, so "hello two" is absent.
if grep -q 'hello two' "$store/$sid" 2>/dev/null; then
    t_fail "the failed save's content landed on disk anyway (the save was not really refused)"
else
    t_ok "the disk still holds only the first turn (the second was not saved)"
fi

# ---- 6: the diagnostic is ONCE per session, not once per failed save ---------
# The per-turn save fires it; the exit save (same session, this process) must
# not repeat it. (Headless makes exactly one save call per turn; the latch
# guards against any further one.)
n=$(grep -c 'could not save the session' "$tmp/r2.err" 2>/dev/null)
if [ "$n" -eq 1 ]; then
    t_ok "one diagnostic per session (the exit save did not repeat it)"
else
    t_fail "the diagnostic appeared $n times this session (want exactly 1)"
fi

t_done
