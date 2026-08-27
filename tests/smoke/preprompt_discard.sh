#!/bin/sh
# smoke: input typed BEFORE the first prompt is announced, not silently dropped
# (M503, the M464 row).
#
# THE DEFECT THIS EXISTS FOR. `jc_term_readline` enters raw mode with TCSAFLUSH
# once per prompt, so anything typed between process start and the first prompt
# is echoed by the tty and then thrown away. The flush is CORRECT -- it is what
# stops stray type-ahead from answering a y/n approval prompt nobody has read --
# but it was silent, so the user watched their line appear and vanish with no
# explanation.
#
# Measured 2026-08-17 while explaining OpenBSD's "lost first send": the window is
# under 100 ms on the development bench and OVER FIVE SECONDS on the OpenBSD
# guest, which is long enough to type a whole sentence into a void.
#
# WHY THE BYTES ARE NOT RECOVERED, asserted below: recovering them would
# reintroduce the hazard the flush exists to prevent. The honest fix is to say so
# and ask for a retype -- which is why check 2 asserts the stray text did NOT
# become a prompt.
. "$(dirname "$0")/_smoke.sh"

[ -x "$SMOKE_TOOLS/ptydrive" ] || t_skip "ptydrive not built"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "STRAY_TYPED_EARLY"
  text SAW_THE_STRAY_LINE
rule
  text CLEAN
EOF
mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT"

# The first thing the driver does is type -- before any `expect`, so the bytes
# are in the tty buffer while jichi is still starting up. That is exactly the
# window the row measured.
cat > "$tmp/early.pd" <<'EOF'
send "STRAY_TYPED_EARLY\r"
expect "discarded" 20
delay 300
send "/exit\r"
waitexit 15
EOF
# --log, not stdout: ptydrive writes the SESSION transcript there, and the
# notice is terminal output, not a diagnostic on stderr.
(cd "$ws" && "$SMOKE_TOOLS/ptydrive" --deadline 45 --cols 100 \
    --log "$tmp/out" "$tmp/early.pd" -- \
    "$BIN" --config "$tmp/config.json" > /dev/null 2>&1)
rc=$?
mm_stop

# --- 1: the discard is announced -------------------------------------------
if grep -q 'discarded' "$tmp/out"; then
    t_ok "input typed before the first prompt is reported, not silently dropped"
else
    t_fail "the line vanished with no explanation -- the M464 defect: \
$(head_bytes 300 "$tmp/out")"
fi

# --- 2: and the notice says what to do -------------------------------------
# A notice that names a cause with no way forward is the M342 class. Here the
# way forward is one word: retype.
if grep -q 'retype' "$tmp/out"; then
    t_ok "the notice tells the user to retype it"
else
    t_fail "the notice names no way forward: $(head_bytes 250 "$tmp/out")"
fi

# --- 3: the stray bytes were NOT smuggled into a turn ----------------------
# The whole reason for the flush. If the discarded text had reached the model,
# the mock would have answered SAW_THE_STRAY_LINE.
if ! grep -q 'SAW_THE_STRAY_LINE' "$tmp/out"; then
    t_ok "the discarded text did not become a prompt (the flush still holds)"
else
    t_fail "type-ahead from before the prompt was sent to the model -- the \
safety property the flush exists for is gone"
fi

t_done
