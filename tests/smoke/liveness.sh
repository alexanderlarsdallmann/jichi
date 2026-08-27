#!/bin/sh
# smoke: liveness before the first tool boundary (M438).
#
# THE MEASURED PROBLEM, in three parts. A supervisor's only honest liveness signal is a
# file that appears or a socket that answers, and jichi gave it neither early enough:
#
#  1. The JOURNAL was opened by jc_env_init and stayed 0 BYTES until the `start` record,
#     which jc_agent_run_turn writes only AFTER config load, per-server reachability
#     probes, MCP connect and the repo-map build. Any of those can take minutes or hang.
#     An empty file is exactly what a process that died before creating it looks like.
#
#  2. The CONTROL SOCKET was served only at the END of a tool round, so a supervisor that
#     connected while the first model call was in flight got no answer to `status` until
#     the whole round finished -- against a client wait hard-coded to 300s.
#
#  3. Within a round, one service point TOTAL. A round with five calls where one is a
#     build left `status` and `abort` unanswered for that build's whole duration.
#
# WHAT IS NOT FIXED, and why: the socket is still not served DURING a model call. Serving
# it there means running the poll from inside libcurl's progress callback, where a `pause`
# would block the HTTP read and could time the request out. That is a real limit, stated
# here rather than left for someone to discover.
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

# FOUR calls in ONE round (MM_MAX_TOOLS is 4, the mock's per-rule ceiling). This shape is what makes part 3 observable, and getting
# it wrong is instructive: the first cut used two calls and asserted only that `status`
# eventually answered. It passed with the fix REVERTED, because the client waits up to
# 300s -- so an answer served at the end of the round still counts as an answer. The
# property is LATENCY, not arrival, and it needs a round long enough for the difference
# to exceed timing noise.
#
# Four 3s calls make a ~12s round, with service points at t=3/6/9/12. A status issued at
# t=4 is answered at ~t=6 with the between-call poll (2s) and only at ~t=12 without it
# (8s) -- a margin wide enough that scheduling noise cannot cross it.
cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "\"role\":\"tool\""
  text LIVE_DONE
rule
  tool run_terminal_command {"command":"sleep 3"}
  tool run_terminal_command {"command":"sleep 3"}
  tool run_terminal_command {"command":"sleep 3"}
  tool run_terminal_command {"command":"sleep 3"}
EOF

wait_file() { # wait_file PATH SECS -- non-empty, not merely present
    _i=0
    while [ ! -s "$1" ]; do
        _i=$((_i + 1)); [ "$_i" -gt "$2" ] && return 1
        sleep 1
    done
    return 0
}
wait_sock() {
    _i=0
    while [ ! -S "$1" ]; do
        _i=$((_i + 1)); [ "$_i" -gt 20 ] && return 1
        sleep 1
    done
    return 0
}

mm_start "$tmp/replies.mm" "$tmp/cap" 9
write_config "$tmp/config.json" "$MM_PORT"

(cd "$ws" && exec "$BIN" --config "$tmp/config.json" --auto \
    --control "$tmp/c.sock" --journal "$tmp/j.jsonl" \
    --budget-tokens 100k --deadline 2m --no-session \
    -p "run the task" < /dev/null > "$tmp/out" 2>"$tmp/err") &
RUN_PID=$!

# --- 1: the journal is NON-EMPTY almost immediately ---------------------------
# `-s`, not `-f`: a present-but-empty file is the state this exists to eliminate.
#
# Honest about what this check can and cannot prove: against a LOCAL mock, `start` also
# lands within 6s, so this passes with the `open` record reverted. The defect it belongs
# to needs a slow startup (reachability probes, MCP connect, a large repo map) to
# reproduce, and reproducing that here would mean building a slow dependency into a
# smoke test. Checks 2 and 6 are the ones with teeth -- they pin WHICH record comes
# first, which is what a supervisor tails for.
if wait_file "$tmp/j.jsonl" 6; then
    t_ok "the journal carries a line within seconds of the run starting"
else
    t_fail "journal still empty after 6s ($(ls -l "$tmp/j.jsonl" 2>/dev/null))"
fi

# --- 2: that first line is the `open` record, with the pid -------------------
# The pid is the point: it lets a supervisor check the process directly rather than
# inferring life from file growth.
first=$(head -1 "$tmp/j.jsonl" 2>/dev/null)
if printf '%s' "$first" | grep -q '"event":"open"' &&
   printf '%s' "$first" | grep -q '"pid":'; then
    t_ok "the first journal line is the open record and names the pid"
else
    t_fail "first line: $(printf '%s' "$first" | head_bytes 160)"
fi

# --- 3: `status` is answered PROMPTLY mid-round, not at the round's end -------
# Part 3 of the defect, and the check that has to measure elapsed time. Pre-M438 the
# round had ONE service point, at its end, so a status issued 4s into a 12s round was
# answered ~8s later. The client's 300s wait means it still got an answer -- which is why
# asserting arrival alone proves nothing.
if ! wait_sock "$tmp/c.sock"; then
    kill "$RUN_PID" 2>/dev/null
    t_fail "control socket never appeared"; t_fail -; t_fail -; t_fail -; t_done
fi
sleep 4                                  # inside the second of four calls
t0=$(date +%s)
st=$("$BIN" control "$tmp/c.sock" status < /dev/null 2>&1)
el=$(( $(date +%s) - t0 ))
# 4s: above one 3s call plus scheduling, well below the 8s a round-end-only service
# point costs here.
if printf '%s' "$st" | grep -q '"ok":true' && [ "$el" -le 4 ]; then
    t_ok "status answered mid-round in ${el}s (between tool calls, not at the end)"
else
    t_fail "waited ${el}s for status: $(printf '%s' "$st" | head_bytes 140)"
fi

# --- 4: and the answer is about THIS run, not a canned reply -----------------
# A status that answered without reading the run's state would satisfy check 3.
if printf '%s' "$st" | grep -q '"tool_calls"' &&
   printf '%s' "$st" | grep -q '"budget_tokens"'; then
    t_ok "the status reflects the live run (tool_calls + budget)"
else
    t_fail "status lacks run state: $(printf '%s' "$st" | head_bytes 200)"
fi

wait "$RUN_PID" 2>/dev/null; rc=$?
mm_stop

# --- 5: the run completes unchanged -----------------------------------------
# Three new service points, none of which may alter the run.
if [ "$rc" -eq 0 ] && grep -q "LIVE_DONE" "$tmp/out"; then
    t_ok "the run completes unchanged (rc=0)"
else
    t_fail "rc=$rc out: $(head_bytes 160 "$tmp/out")"
fi

# --- 6: `open` did not displace `start` -------------------------------------
# The new record answers one question ("the run exists"). Every budget and scope figure
# stays on `start`, where a reader already looks -- so both must be present, in order,
# and `start` must still carry its fields.
if grep -q '"event":"start"' "$tmp/j.jsonl" &&
   grep -q '"budget_tokens"' "$tmp/j.jsonl" &&
   [ "$(grep -n '"event":"open"' "$tmp/j.jsonl" | head -1 | cut -d: -f1)" = "1" ]; then
    t_ok "open precedes start, and start still carries the run's limits"
else
    t_fail "records wrong: $(grep -o '"event":"[a-z_]*"' "$tmp/j.jsonl" | head -4 | tr '\n' ' ')"
fi

t_done
