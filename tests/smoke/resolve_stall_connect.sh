#!/bin/sh
# smoke: M1062-e (SV:1657, card 97) -- the resolve-timeout HANG shape (rc=28,
# connect_time 0, request_size 0, no socket) is a real e2e tooth.
#
# THE FINDING. `resolve_timeout_connect.sh` (M1062-c) drives this exact shape,
# but only where a private netns exists: it t_skips on any box where `unshare
# -Urmn` is blocked (the gate host / lead sandbox), so on that host the hang
# shape -- the one that was RED on the unfixed -b (read as a reused stream
# death) -- is never actually exercised. This driver makes the same call FAIL
# THE SAME WAY without any netns at all: the FAULT=1 resolver-start hook
# (JICHI_FAULT_RESOLVE_STALL_MS) sleeps inside libcurl's resolver long enough
# that timeouts.connect fires first, giving rc=28 with nothing connected and
# nothing sent -- the HANG shape, deterministically, on any host with a FAULT
# build. It runs under `make ci`'s smoke-faults stage.
#
# THE REGRESSION GUARD (check 3, the load-bearing assertion): on the pre-M1062-c
# tree (the -b, jc_http_reused_conn reading empty-watch + connect_time 0 as a
# REUSED stream death) the SAME run reads "connected, then the stream went dead
# -- nothing arrived (timeouts.deadStream)", fail_class timeout-stall. So the
# fail message here must read as a CONNECT failure (it names timeouts.connect
# and says the request was never sent), and must NOT name the dead-stream
# fence: the phrases "stream went dead" and "timeouts.deadStream" are ABSENT.
# Check 3 is red-first ON THIS CONFIG: the teeth probe reproduced the exact
# "not ok 3" line with the -b helper behaviour on this measurement config
# (reports/2026-10-27-m1062-e-teeth.md, section 3b), and SV:1141 measured the
# same dead-stream wording on the netns cell with stall:0. With stall UNSET the
# same -b error would instead read "stream stalled (timeouts.stall)" (the 30 s
# builtin default) and check 3 would be BLIND to it -- that is why the config
# pins stall:0 (SV:1859).
#
# WHY NO NETWORK / NO NETNS / NO HOST ENTRY. The FAULT hook IS the stall: it
# sleeps JICHI_FAULT_RESOLVE_STALL_MS (3000) in the resolver-start callback, so
# the 1 s connect bound fires before resolution completes regardless of whether
# probeblack.invalid is (correctly) unresolvable or the box is offline. The
# driver needs no UDP tools, no unshare, no user namespaces, and must not add a
# host entry -- probeblack.invalid is an RFC-reserved name (.invalid) that no
# resolver answers.
#
# Requires a FAULT=1 binary (`make clean && make FAULT=1`); SKIPS otherwise.
. "$(dirname "$0")/_smoke.sh"

# A FAULT build announces itself in --version -- a far more reliable probe
# than inferring it from injected behaviour.
"$BIN" --version < /dev/null 2>/dev/null | grep -q "FAULT=1" \
    || t_skip "needs a FAULT=1 binary (make clean && make FAULT=1)"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# An unresolvable host (RFC-reserved .invalid, port 1234), no retries, and the
# MEASUREMENT config (SV:1859: connect 1, stall 0, request 0, deadStream 5 --
# the values under which the -b red was measured; the netns cell
# resolve_timeout_connect.sh uses the same shape, connect 8). connect 1 is the
# bound that WILL fire at 1 s on the fixed tree; stall 0 and request 0 turn
# those caps off; deadStream 5 is the design value: with stall:0 the fence_only
# flag is true, so on the -b's reading the fence is the ONLY thing armed to
# fire, and deadStream 5 bounds the fence's low-speed window to 5 s so a
# perturbed/hung run still fails as a timeout. The -b misclassification
# therefore reads in the dead-stream wording -- the phrases check 3 greps.
cat > "$tmp/config.json" <<EOF
{"lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
"apiBase":"http://probeblack.invalid:1234/v1","apiKey":"x","roles":["chat"]}],
"snapshots":false,"repoMap":false,"references":false,
"maxRetries":0,"timeouts":{"connect":1,"stall":0,"request":0,"deadStream":5}}
EOF

# The FAULT hook sleeps 3000 ms in the resolver-start callback; the 1 s connect
# bound fires first -> rc=28, nothing sent, no socket. with_deadline 60 keeps a
# regression that HANGS instead of failing a RED (the deadline kills it), not a
# hang of the whole stage.
(cd "$ws" && with_deadline 60 env JICHI_FAULT_RESOLVE_STALL_MS=3000 \
    "$BIN" --config "$tmp/config.json" --no-session -p "hi" \
    < /dev/null > "$tmp/out" 2> "$tmp/err"); rc=$?

# 1: the connect gave up (a nonzero exit, NOT a success).
if [ "$rc" -ne 0 ]; then
    t_ok "the stalled resolve call gave up nonzero (rc=$rc)"
else
    t_fail "rc=0 -- the call succeeded; the resolver-stall hook did not fire (FAULT build? JICHI_FAULT_RESOLVE_STALL_MS not reaching jichi?)"
fi

# 2: the fail message reads as a CONNECT failure, naming the bound that fired.
if grep -q "timeouts.connect" "$tmp/err" 2>/dev/null; then
    t_ok "stderr names the connect bound (timeouts.connect)"
else
    t_fail "stderr does not name timeouts.connect: $(head_bytes 200 < "$tmp/err")"
fi

# 3: THE REGRESSION GUARD -- the message does NOT name the dead-stream fence.
# On the unfixed -b this line is RED: the same call reads "connected, then the
# stream went dead (timeouts.deadStream)" instead.
if grep -q "stream went dead" "$tmp/err" 2>/dev/null || \
   grep -q "timeouts.deadStream" "$tmp/err" 2>/dev/null; then
    t_fail "the message names the dead-stream fence (RED: the -b read the resolve stall as a reused stream death; M1062-c not fixed): $(head_bytes 200 < "$tmp/err")"
else
    t_ok "the message does not name the dead-stream fence (no 'stream went dead', no timeouts.deadStream)"
fi

# 4: the message is honest that nothing was sent.
if grep -q "the request was never sent" "$tmp/err" 2>/dev/null; then
    t_ok "stderr says the request was never sent"
else
    t_fail "stderr does not say the request was never sent: $(head_bytes 200 < "$tmp/err")"
fi

t_done
