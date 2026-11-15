#!/bin/sh
# smoke: a server that answers `doctor`'s liveness probe AFTER more than the
# probe's requested total is reachable, not unreachable (M1144, batch H6).
#
# THE CASE (2026-11-05, 15:47). The gateway's TLS handshake took 9.9/11.7/10.7 s
# from the drive host. `jc_net_reachable` probes with a 3-second TOTAL
# (CURLOPT_TIMEOUT) while its own connect fence (CURLOPT_CONNECTTIMEOUT, via
# jc_net_side_fences) is 30 s -- and per M877 the handshake counts against the
# CONNECT bound, not the total. On a slow handshake the smaller total fired
# mid-connect, libcurl timed out with no HTTP status, and `doctor` printed
# "active model's server is unreachable" (a FAIL) beside a passing `--live`
# run: a false verdict. The fix raises the probe's total to its connect fence,
# so a total can no longer undercut a fence (note M1144, B1-B4).
#
# THE STAND-IN. A real slow handshake cannot be scripted in this tier (no TLS
# fixture), so mockmodel's `delay` plays the role: it accepts the connection,
# reads the request, then sleeps 6 s before answering. The delay is pacing
# (NOT tt_mult-scaled), so 6 s is a real 6 s on every box. The unfixed probe
# (total 3 s < connect 30 s) times out at 3 s and reports unreachable; the
# fixed probe (total raised to the 30 s fence) gets the 200 at 6 s and reports
# reachable. A refused endpoint (port 9) is the control: rc 7 is instant, so
# it stays unreachable and fast either way (B2) -- the fix must not stretch a
# dead endpoint out to 30 s.
#
# `count 1`, not a path match: mockmodel's `match` tests the request BODY,
# which is empty on the GET the probe makes -- and the probe IS the first
# request `doctor` sends (the model-listed/model-limits calls follow only
# after it says reachable), so request index 1 discriminates it.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)

# Leg 1 (the red one): the probe (request 1) is answered after 6 s; the
# follow-up listing/limits calls (requests 2, 3) answer fast -- a 404 reads as
# "this server does not publish model limits", the normal OK row.
cat > "$tmp/slow.mm" <<'EOF'
wire openai
rule
  count 1
  delay 6000
  status 200
  body {"data":[{"id":"mock"}]}
rule
  status 404
EOF
mm_start "$tmp/slow.mm" "$tmp/cap"
write_config "$tmp/cfg.json" "$MM_PORT"
with_deadline 60 "$BIN" --config "$tmp/cfg.json" doctor \
    < /dev/null > "$tmp/out1" 2>&1
rc1=$?
mm_stop

if grep -q "model server reachable" "$tmp/out1" &&
   ! grep -q "unreachable" "$tmp/out1"; then
    t_ok "a server that answers at 6 s (> the 3 s requested total) is reported reachable (B1)"
else
    t_fail "doctor on the 6 s server said unreachable: $(grep -i 'unreachable' "$tmp/out1" | head_bytes 200)"
fi

if [ $rc1 -eq 0 ]; then
    t_ok "doctor exits 0 on a slow-answering server (no false FAIL)"
else
    t_fail "doctor rc=$rc1 (want 0); tail: $(grep -i 'unreachable' "$tmp/out1" | head_bytes 200)"
fi

# Leg 2 (control, B2): a refused endpoint is still unreachable -- and fast, so
# with_deadline proves the fix did not stretch a dead endpoint to the fence.
write_config "$tmp/cfg2.json" 9
with_deadline 40 "$BIN" --config "$tmp/cfg2.json" doctor \
    < /dev/null > "$tmp/out2" 2>&1
rc2=$?
if [ $rc2 -eq 1 ] && grep -q "unreachable" "$tmp/out2"; then
    t_ok "a refused endpoint (port 9) is still unreachable, and fast (B2)"
else
    t_fail "doctor on port 9: rc=$rc2; $(grep -i 'unreachable' "$tmp/out2" | head_bytes 200)"
fi

t_done
