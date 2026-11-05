#!/bin/sh
# smoke: the topology-B RSS guard (M953, invariant 2; design
# docs/internal/2026-10-14-topology-b-design.md B-2; proposal 6.8/6.11).
#
# THE RIG: the guard compares a leg's resident set to four times leg 1's, and a
# healthy ~7 MB process will never honestly exceed that, so the branch it
# guards is a SAFETY guarantee a real run cannot fire. A FAULT=1 build arms
# JICHI_FAULT_RSS_AFTER and the CURRENT-leg read reports the honest value
# four-fold, so the guard trips exactly like a leaked process. Leg 1's BASELINE
# read is taken before the site is ever consulted, so it stays honest and the
# trip is possible.
#
# THE LOOP: with relayInProcess on and relayMaxLegs 3 the rig's per-leg E3
# exhaustion would run three in-process legs (relay_topo_b.sh proves that:
# three batons, six requests). With the guard armed from the first
# continue-decision, leg 1 hands over, its baton is written, the guard trips,
# and the run ends `handover` -- so only ONE baton and TWO requests land.
#
# WHAT IT HOLDS, each against the effect:
#   1. the guarded run ended on handover (the guard's own terminal reason).
#   2. the guarded run made only TWO model requests and wrote ONE baton --
#      legs 2 and 3 were refused by the guard, not by the ceiling.
#   3. the unguarded control (same rig, no JICHI_FAULT_RSS_AFTER) made SIX
#      requests and wrote THREE batons -- proving the armed run's stop is the
#      guard, not a missing leg: the same process runs the loop to the
#      ceiling when the guard is not faked.
#
# WITHOUT the guard the armed run behaves like the control -- six requests,
# three batons -- and checks 2 and 3 (the control's) both fail. Red-first:
# build FAULT=1, comment out the relay_rss_guard call, watch checks 1 and 2
# go red (the run runs to the ceiling), restore, watch green. The driver
# SKIPS on a non-FAULT binary (the seam is compiled out of a release build).
# (The M951 lesson: do NOT set BIN here -- the smoke lib's BIN honours
# JC_SMOKE_BIN, the mutant sweep's seam.)
. "$(dirname "$0")/_smoke.sh"

"$BIN" --version < /dev/null 2>/dev/null | grep -q "FAULT=1" \
    || t_skip "needs a FAULT=1 binary (make clean && make FAULT=1)"

t_plan 3

tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

home_a=$(smoke_tmp)
home_b=$(smoke_tmp)
chmod 700 "$home_a" "$home_b" 2>/dev/null || true

printf 'a file for the rss-guard rig\n' > "$ws/a.txt"

# The per-leg E3 exhaustion pattern (relay_topo_b.sh's rig): usage at 92.5% of
# the window on every model call; one read_file tool call per leg; the
# round-boundary relay check hands over.
cat > "$tmp/rg.mm" <<'EOF'
wire openai
rule
  match "messages"
  usage 185000 5
  tool read_file {"path":"a.txt"}
EOF

# --- the guarded run: the RSS guard trips at leg 1's continue-decision ------
JICHI_FAULT_RSS_AFTER=0
export JICHI_FAULT_RSS_AFTER
mm_start "$tmp/rg.mm" "$tmp/cap"
write_config "$tmp/configg.json" "$MM_PORT" \
    '"maxToolIters":3,"relay":true,"relayInProcess":true,"relayMaxLegs":3' \
    '"contextLength":200000'
HOME="$home_a"
export HOME
(cd "$ws" && with_deadline 120 "$BIN" \
    --config "$tmp/configg.json" --output json --max-tool-calls 50 \
    -p "read the file and tell me what it says" \
    < /dev/null > "$tmp/rg.json" 2> "$tmp/rg.err"); rcg=$?
mm_stop
unset JICHI_FAULT_RSS_AFTER

stopg=$("$JQ" '.stop_reason' "$tmp/rg.json" 2>/dev/null)
reqsg=$(ls "$tmp/cap" 2>/dev/null | grep -c '^req\.')
rdg=$(ls -d "$home_a"/.jichi.d/relay/*/ 2>/dev/null | head -1)
batg=$(ls "$rdg" 2>/dev/null | grep -c '^leg-[0-9][0-9]\.md$')
b2g=$(ls "$rdg/leg-02.md" 2>/dev/null | wc -l | tr -d ' ')

# 1: the guarded run ends on the handover the guard forces.
if [ "$rcg" -eq 0 ] && [ "$stopg" = "handover" ]; then
    t_ok "the guarded run ended on handover (rc=0, stop=handover)"
else
    t_fail "guarded run: rc=$rcg stop='$stopg' (want rc=0, stop=handover)"
fi

# 2: the guard refused legs 2 and 3 -- TWO requests (one leg), ONE baton, and
# NO leg-02.md. Without the guard this run behaves like the control: six
# requests, three batons, and leg-02.md present.
if [ "$reqsg" -eq 2 ] && [ "$batg" -eq 1 ] && [ "$b2g" -eq 0 ]; then
    t_ok "the guard stopped the loop after leg 1 (two requests, one baton)"
else
    t_fail "guarded run: reqs=$reqsg batons=$batg leg-02=$b2g (want 2/1/0)"
fi

# --- the unguarded control: the same rig, no guard armed ---------------------
mm_start "$tmp/rg.mm" "$tmp/capc"
write_config "$tmp/configu.json" "$MM_PORT" \
    '"maxToolIters":3,"relay":true,"relayInProcess":true,"relayMaxLegs":3' \
    '"contextLength":200000'
HOME="$home_b"
export HOME
(cd "$ws" && with_deadline 120 "$BIN" \
    --config "$tmp/configu.json" --output json --max-tool-calls 50 \
    -p "read the file and tell me what it says" \
    < /dev/null > "$tmp/rc.json" 2> "$tmp/rc.err"); rcc=$?
mm_stop

stopc=$("$JQ" '.stop_reason' "$tmp/rc.json" 2>/dev/null)
reqsc=$(ls "$tmp/capc" 2>/dev/null | grep -c '^req\.')
rdc=$(ls -d "$home_b"/.jichi.d/relay/*/ 2>/dev/null | head -1)
batc=$(ls "$rdc" 2>/dev/null | grep -c '^leg-[0-9][0-9]\.md$')

# 3: the control runs the loop to the CEILING (three legs, six requests, three
# batons). If the armed run's stop were a missing leg rather than the guard,
# the control would show the same stop here.
if [ "$rcc" -eq 0 ] && [ "$stopc" = "handover" ] && \
   [ "$reqsc" -eq 6 ] && [ "$batc" -eq 3 ]; then
    t_ok "control (no guard): six requests, three batons -- the loop reached the ceiling"
else
    t_fail "control: rc=$rcc stop='$stopc' reqs=$reqsc batons=$batc (want 0/handover/6/3)"
fi

t_done
