#!/bin/sh
# smoke: a turn that stops at maxToolIters says so, and the daemon socket is
# same-user-only (M322).
#
# Two silent stops, both found by asking "what is the operator told?":
#
#   1. The iteration cap is a CIRCUIT BREAKER, not a failure: the history is
#      intact and another prompt resumes from it. Interactively you get a
#      warning. A machine driver got `stop_reason: "done"` with an EMPTY
#      answer -- indistinguishable from "finished and had nothing to say", so a
#      supervisor accepts a half-finished task as complete. Now: "max_iters".
#   2. `stop_reason` must stay "done" when a SUBAGENT caps and the top-level
#      turn finishes cleanly. This is the discriminating case: the pre-existing
#      last_run_capped flag describes whichever run ended last, so without a
#      depth guard a capped subagent makes a clean turn report max_iters.
#      Verified as a false positive before the guard existed.
#   3. The daemon socket mode IS its whole ACL, and the daemon runs tools and
#      shell commands. It used to inherit the umask (0775 under this harness),
#      so any local user could drive it.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
echo hi > "$ws/a.txt"

# --- 1: a top-level cap reports max_iters ------------------------------------
cat > "$tmp/loop.mm" <<'EOF'
wire openai
rule
  tool read_file {"path":"a.txt"}
EOF
mm_start_unbounded "$tmp/loop.mm" "$tmp/cap"
write_config "$tmp/cap.json" "$MM_PORT" '"maxToolIters":3'
out=$( (cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/cap.json" --auto \
        --no-lite --no-session --output json -p "loop" < /dev/null) 2>/dev/null )
# The exit-code run happens while the mock is STILL UP: stopping it first makes
# the run fail for lack of a model and exit 1 for an unrelated reason, which is
# how a first draft of this driver "found" a defect that was not there.
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/cap.json" --auto --no-lite \
    --no-session -q -p "loop" < /dev/null) >/dev/null 2>&1
rc=$?
mm_stop
case "$out" in
    *'"stop_reason":"max_iters"'*)
        t_ok "a capped top-level turn reports stop_reason max_iters" ;;
    *'"stop_reason":"done"'*)
        t_fail "a capped turn still reports \"done\" -- a supervisor cannot tell" ;;
    *)  t_fail "no stop_reason in the json: $(printf '%s' "$out" | head_bytes 120)" ;;
esac
# It is NOT an error: nothing failed, the work is in the history, and a nudge
# resumes. A non-zero exit would invite a supervisor to roll back valid work.
if [ "$rc" = "0" ]; then
    t_ok "a capped turn still exits 0 (a circuit breaker, not a failure)"
else
    t_fail "a capped turn exits $rc -- the cap is not a task failure"
fi

# --- 2: a subagent's cap must NOT mark the turn ------------------------------
# "focused sub-agent" appears only in jc_sysmsg_build_sub, so it tells the nested
# run from the top-level one: the subagent is fed tool calls until it caps, the
# top level gets a final answer.
cat > "$tmp/sub.mm" <<'EOF'
wire openai
rule
  match "focused sub-agent"
  tool read_file {"path":"a.txt"}
rule
  count 1
  tool spawn_subagent {"task":"look at a.txt"}
rule
  text TOP_LEVEL_CLEAN
EOF
mm_start_unbounded "$tmp/sub.mm" "$tmp/sub"
write_config "$tmp/sub.json" "$MM_PORT" \
    '"maxToolIters":25,"maxSubagentIters":2,"toolProfile":"full"'
out=$( (cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/sub.json" --auto \
        --no-lite --no-session --output json -p "delegate" < /dev/null) \
       2>"$tmp/sub.err" )
mm_stop
# The subagent must actually have capped, or this proves nothing -- the whole
# point is a capped nested run beside a clean turn.
if grep -q "max tool iterations" "$tmp/sub.err"; then
    t_ok "the subagent really did hit its cap (the case is exercised)"
else
    t_fail "no subagent cap in stderr -- this check would prove nothing"
fi
case "$out" in
    *'"stop_reason":"done"'*)
        t_ok "a subagent's cap leaves the clean turn reporting done" ;;
    *'"stop_reason":"max_iters"'*)
        t_fail "false positive: a subagent's cap marked the whole turn capped" ;;
    *)  t_fail "no stop_reason: $(printf '%s' "$out" | head_bytes 120)" ;;
esac

# --- 3: the daemon socket is same-user-only ---------------------------------
# A SHORT path, and a WRITABLE one -- in that order of discovery, not of
# importance.
#
# This read "/tmp/jc_smoke_$$.sock" until M459, justified by a comment saying
# sun_path is 108 bytes and the harness tmp dirs are long. The first half is
# true. The second was never measured, and is false: smoke_tmp yields
# $TMPDIR/jichi_smoke.XXXXXX, so the harness socket path is 30 bytes on a
# normal host and 61 on Termux -- which is why control.sh and daemon.sh have
# always put their sockets in $tmp and have always worked, Termux included.
#
# What the literal /tmp actually cost was Android, where an app uid cannot
# write it, and the driver reported only "the daemon never created its socket".
# $TMPDIR is the shortest path that is also writable everywhere (4 bytes on a
# host, 35 on Termux), so the sun_path guard is kept honestly rather than
# abandoned -- and the numbers are written down so the next reader can check
# the claim instead of inheriting it.
sock="${TMPDIR:-/tmp}/jc_smoke_$$.sock"
rm -f "$sock"
write_config "$tmp/dmn.json" 9
(cd "$ws" && exec "$BIN" --config "$tmp/dmn.json" --no-lite daemon --socket "$sock" \
    >/dev/null 2>&1) &
dpid=$!
i=0
while [ ! -S "$sock" ] && [ $i -lt 15 ]; do i=$((i+1)); sleep 1; done
mode=$(ls -l "$sock" 2>/dev/null | cut -c1-10)
kill "$dpid" 2>/dev/null; wait "$dpid" 2>/dev/null; rm -f "$sock"
case "$mode" in
    srw-------) t_ok "the daemon socket is 0600 (same-user only)" ;;
    "")         t_fail "the daemon never created its socket at $sock \
-- is it writable, and is ${#sock} under sun_path's 108 bytes?" ;;
    *)          t_fail "daemon socket mode is '$mode', not srw------- \
-- any local user can drive a process that runs shell commands" ;;
esac

t_done
