#!/bin/sh
# smoke: the read-loop kill path must return -2 deterministically (B-01, B-06).
#
# Found by B-01/B-06: when a command closes its output but keeps running,
# jichi's read-loop ends on EOF and then waits for the child with an UNBOUNDED
# wait (no timeout, no abort). This driver proves the fix.
#
# INVOCATION 1 (B-01, the timeout):
#   A command closes its output and sleeps past its 2s timeout.
#   Expected: jichi returns within ~2s (not the 20s child lifetime),
#   and the tool result mentions "timed out after 2s".
#
# INVOCATION 2 (B-06, the interrupt):
#   A command closes its output and sleeps 20s with a 30s timeout (so the
#   deadline is farther than the signal). Send SIGINT to jichi.
#   Expected: jichi exits on ONE signal (not 15s fallback),
#   and no background child survives.
#
# M998 HERMETICITY FIX: Each invocation uses a per-run unique marker embedded in
# the sleep command and checked by CHECK 5. This prevents false positives from
# unrelated processes that happen to contain the substring 'sleep 20' (e.g.
# concurrent smoke runs, supervisor shell loops). The marker is defined before
# the mock model's tool JSON heredoc, so the expansion is already baked into the
# .mm file. Check 5 matches the marker EXACTLY (case "$cmdline" in *"$MARKER"*).
#
# Red-first proof recipe (commented, NOT shipped):
#   1. Start a decoy: `sh -c 'exec sleep 20 decoy' & echo $! > /tmp/decoy.pid`
#   2. Run the OLD driver (git show HEAD:tests/smoke/run_timeout_after_eof.sh).
#      CHECK 5 must go RED: `not ok 5 - B-06: the interrupted command's child
#      was orphaned: <pid>`.
#   3. Run the NEW driver with the decoy still alive -> CHECK 5 must be GREEN.
#   4. Kill the decoy: `kill -9 $(cat /tmp/decoy.pid)`.
#
. "$(dirname "$0")/_smoke.sh"

# /proc precondition (before the plan, lint check 20): a rig with no /proc
# has no child counter -- CHECK 5 (the no-surviving-child scan) would fail,
# so SKIP before declaring the plan.
if [ ! -d /proc ]; then
    t_skip "needs /proc to assert no background child survived (CHECK 5)"
fi

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# M998: define the marker ONCE before both invocations, so both use the same one.
# This allows CHECK 5 to look for just this marker, not multiple variants.
# $$ is the driver's pid. We use it as the unique suffix after "sleep 20".
# The marker must be unique per run to avoid false positives from unrelated processes.
MARKER="sleep 20.$$"

# --- INVOCATION 1 (B-01, the timeout) ---------------------------------------
cat > "$tmp/t1.mm" <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"exec >/dev/null 2>&1; $MARKER","timeout":2}
rule
  match "\"role\":\"tool\""
  text T1_DONE
EOF
mm_start "$tmp/t1.mm" "$tmp/cap1" 3
write_config "$tmp/c1.json" "$MM_PORT"

t0=$(date +%s)
(cd "$ws" && with_deadline 40 "$BIN" --config "$tmp/c1.json" --auto --no-session -q -p "run it" </dev/null) >/dev/null 2>&1
rc1=$?
t1=$(date +%s)
mm_stop

req1="$tmp/cap1/req.2"

# CHECK 1: the tool result went back to the model
if [ -s "$req1" ]; then
    t_ok "the tool result went back to the model"
else
    t_fail "no tool result (req.2 missing or empty)"
fi

# CHECK 2 (B-01 red tooth): returned promptly (< 8s, not the 20s child lifetime)
elapsed=$((t1 - t0))
if [ "$elapsed" -lt 8 ]; then
    t_ok "returned promptly (${elapsed}s, not the 20s child lifetime)"
else
    t_fail "B-01: ran ${elapsed}s -- the post-EOF wait was unbounded"
fi

# CHECK 3 (B-01): the 2s timeout was applied and reported
if grep -q "timed out after 2s" "$req1" 2>/dev/null; then
    t_ok "the 2s timeout was applied and reported"
else
    t_fail "B-01: the 2s timeout was never applied: $(head_bytes 200 "$req1" 2>/dev/null)"
fi

# --- INVOCATION 2 (B-06, the interrupt) -------------------------------------
# Same MARKER as INVOCATION 1, so CHECK 5 can look for just one marker.
cat > "$tmp/t2.mm" <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"exec >/dev/null 2>&1; $MARKER","timeout":30}
rule
  match "\"role\":\"tool\""
  text T2_DONE
EOF
mm_start "$tmp/t2.mm" "$tmp/cap2" 3
write_config "$tmp/c2.json" "$MM_PORT"

# Launch jichi in the background and signal it after 3s
# jichi's pid is $! of the backgrounded command at the next line
(cd "$ws" && "$BIN" --config "$tmp/c2.json" --auto --no-session -q -p "run it" </dev/null) >/dev/null 2>&1 &
JPID=$!
sleep 3

# Guard: prove jichi is STILL RUNNING and had reached the tool before sending SIGINT
# If jichi exited early (bad config, crash, etc.), the post-EOF wait was never
# reached, so CHECK 4 would be vacuous. Also verify the tool request was sent.
if ! kill -0 "$JPID" 2>/dev/null; then
    t_fail "B-06: jichi exited BEFORE the SIGINT -- the interrupt tooth is vacuous (it never reached the post-EOF wait)"
    mm_stop
    t_done
    exit 0
fi
# The tool request must have been sent (req.1 is the first model request)
if [ ! -s "$tmp/cap2/req.1" ]; then
    t_fail "B-06: the tool request was never sent -- the interrupt tooth is vacuous"
    mm_stop
    t_done
    exit 0
fi

# Send SIGINT directly to jichi
# (t_fail, not t_skip: t_skip after t_plan violates lint check 20)
kill -INT "$JPID" 2>/dev/null || { t_fail "could not send SIGINT"; t_done; exit 0; }

# Bounded-wait for jichi to exit on that ONE signal:
survived=0
i=0
while kill -0 "$JPID" 2>/dev/null && [ $i -lt 15 ]; do
    i=$((i + 1)); sleep 1
done
if kill -0 "$JPID" 2>/dev/null; then
    survived=1
    kill -9 "$JPID" 2>/dev/null
fi
wait "$JPID" 2>/dev/null
mm_stop

# CHECK 4 (B-06 red tooth): jichi exited on one SIGINT
if [ "$survived" -eq 0 ]; then
    t_ok "jichi exited on one SIGINT (the post-EOF wait is interrupt-aware)"
else
    t_fail "B-06: jichi needed a second signal (survived)"
fi

# CHECK 5 (B-06 hygiene): no background child survived the interrupt.
# M998: scan for the marker EXACTLY, not the bare 'sleep 20' substring.
sleep 1
survivors=""
for d in /proc/[0-9]*; do
    [ -r "$d/cmdline" ] || continue
    cmdline=$(tr '\0' ' ' < "$d/cmdline" 2>/dev/null) || continue
    case "$cmdline" in
        *"$MARKER"*)
            # Exclude our own pid ($$) and the driver's children
            case "$d" in
                */$$*|*/$JPID*) continue ;;
            esac
            survivors="$survivors $(basename "$d")"
            ;;
    esac
done
if [ -z "$survivors" ]; then
    t_ok "no background child survived the interrupt"
else
    t_fail "B-06: the interrupted command's child was orphaned: $survivors"
fi

t_done
