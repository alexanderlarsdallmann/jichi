#!/bin/sh
# smoke: M1083 (D-08) -- a SIGKILL mid-turn loses at most the in-flight round.
#
# THE FINDING: the session was saved ONLY at the end of a top-level turn
# (main.c, after jc_agent_run_turn returned) and never fsync'ed, so a
# SIGKILL/OOM-kill part-way through a long --auto turn left NO session for the
# in-progress work -- the store held only the previous turn's file and
# `--continue` could not resume. D-08 fixes three things and this driver holds
# all three against their effect:
#
#   1. MID-TURN PERSIST: the agent loop now saves at each tool-ROUND boundary
#      (where every call of the round has its tool result landed, so the
#      history is a wire-consistent prefix), throttled by jc_session_needs_save.
#      RED today (unfixed): after 3 tool rounds + a kill on the 4th, the store
#      holds NONE of the in-flight turn's rounds. GREEN: it holds all 3.
#   2. STALE-TMP SWEEP: a crash between open(tmp) and rename leaves
#      <id>.json.tmp<pid> in the store, invisible to every removal path (the
#      listing filters on .json). At listing time the store now sweeps those
#      whose pid is gone. Proven: a dead-pid tmp is removed on `ls`; a live-pid
#      tmp (our own in-progress write) is kept.
#   3. DURABLE WRITER: the session save routes through jc_write_file_atomic_
#      durable (fsync(fd) before rename + fsync(dir) after). Proven with an
#      LD_PRELOAD fsync counter: at least one fsync must fire on a file in the
#      session store during a completed save.
#
# The mid-turn half lifts the confirmation report's two-mock shape: turn 1
# (t1.mm: one tool round, then a text answer) establishes a saved session;
# turn 2 (kill.mm: 3 tool rounds, then a catch-all `stall header`) resumes it
# with --continue --auto and is SIGKILLed after the 3 rounds are visibly done.
. "$(dirname "$0")/_smoke.sh"

t_plan 8
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
store="$HOME/.jichi.d/sessions"
JQ="$SMOKE_TOOLS/jsonq"
trap 'mm_stop 2>/dev/null' EXIT
mkdir -p "$store"

# Turn 1: one tool round, then the text answer. Establishes a saved session.
cat > "$tmp/t1.mm" <<'EOF'
wire openai
rule
  count 1
  usage 20 5
  tool run_terminal_command {"command":"echo turn-one"}
rule
  match "\"role\":\"tool\""
  usage 20 5
  text turn-one-done
EOF
# Turn 2 (the killed turn): 3 tool rounds, then the 4th request (catch-all)
# stalls the header so jichi sits alive mid-turn.
cat > "$tmp/kill.mm" <<'EOF'
wire openai
rule
  count 1
  usage 20 5
  tool run_terminal_command {"command":"echo round-1-of-killed-turn"}
rule
  count 2
  usage 20 5
  tool run_terminal_command {"command":"echo round-2-of-killed-turn"}
rule
  count 3
  usage 20 5
  tool run_terminal_command {"command":"echo round-3-of-killed-turn"}
rule
  stall header
EOF

# --- turn 1: one tool round + answer -> a saved session ----------------------
mm_start_unbounded "$tmp/t1.mm" "$tmp/cap1"
write_config "$tmp/config.json" "$MM_PORT" '"maxToolIters":20'
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" -q \
      -p "do round one" < /dev/null); rc=$?
mm_stop

# 1: turn 1 completed and saved a session in $ws.
sid=$(cd "$ws" && "$BIN" ls --output json < /dev/null 2>/dev/null | "$JQ" '.sessions[0].id')
if [ $rc -eq 0 ] && [ -n "$sid" ] && [ -f "$store/$sid.json" ]; then
    t_ok "turn 1 saved a session in the workspace"
else
    t_fail "turn 1 did not save a session (rc=$rc): $(printf '%s' "$out" | head_bytes 160)"
fi

# --- turn 2: --continue --auto, 3 tool rounds then stalled on the 4th -------
mm_start_unbounded "$tmp/kill.mm" "$tmp/cap2"
write_config "$tmp/config2.json" "$MM_PORT" '"maxToolIters":20'
# exec so $! IS jichi (not the subshell): kill -9 $JPID then kills jichi, and
# the kill -0 liveness checks below track jichi directly (smoke_lint check 15).
( cd "$ws" && exec "$BIN" --config "$tmp/config2.json" -q --continue --auto \
      -p "keep going" < /dev/null > "$tmp/t2.out" 2> "$tmp/t2.err" ) &
JPID=$!
# Wait until the mock has CAPTURED the 4th request of turn 2: proof jichi ran
# all 3 tool rounds (each request re-sends the history, so req.N is round N-1's
# reply feeding round N) and is now sitting on the stalled header.
i=0
while [ ! -f "$tmp/cap2/req.4" ] && [ $i -lt 60 ]; do
    kill -0 "$JPID" 2>/dev/null || break
    i=$((i + 1)); sleep 1
done
# The 3 tool rounds must have been captured, and jichi must still be alive
# (stalled on req.4) -- otherwise the kill is meaningless.
saw4=no
if [ -f "$tmp/cap2/req.3" ] && [ -f "$tmp/cap2/req.4" ] && kill -0 "$JPID" 2>/dev/null; then
    saw4=yes
fi
# Stop the mock BEFORE killing jichi: jichi's req.4 reply is a stalled header
# the mock never sends, so the capture files are complete. Killing it while the
# mock is still serving would leave jichi's children (mock clients) to be
# reaped by the mock's death, and a hang there would be misattributed to the
# kill -9 below (audit of 670326d1, N1).
mm_stop
kill -9 "$JPID" 2>/dev/null
wait "$JPID" 2>/dev/null

# 2: the kill landed mid-turn -- 3 tool rounds done, stalled on the 4th.
if [ "$saw4" = yes ]; then
    t_ok "killed mid-turn: 3 tool rounds done, stalled on the 4th"
else
    t_fail "kill setup: cap2 req.3=$(ls "$tmp/cap2"/req.3 2>/dev/null) req.4=$(ls "$tmp/cap2"/req.4 2>/dev/null); alive? $(kill -0 "$JPID" 2>/dev/null && echo yes || echo no)"
fi

# 3: the session file still EXISTS after the kill (the in-progress turn did not
# leave the store empty).
if [ -n "$sid" ] && [ -f "$store/$sid.json" ]; then
    t_ok "after the kill, the session file still exists"
else
    t_fail "after the kill, NO session file for $sid in the store: $(ls -la "$store" 2>/dev/null | head_bytes 200)"
fi

# 4: the surviving session holds the PRE-kill consistent prefix -- turn 1's
# "turn-one-done" answer is still there (nothing was corrupted or lost).
sfile="$store/$sid.json"
if [ -n "$sfile" ] && [ -f "$sfile" ] && grep -q 'turn-one-done' "$sfile" 2>/dev/null; then
    t_ok "surviving session holds the pre-kill prefix (turn one)"
else
    t_fail "surviving session lost the pre-kill prefix: sfile=$sfile $(grep -c 'turn-one-done' "$sfile" 2>/dev/null)"
fi

# 5: THE D-08 FIX -- the in-flight turn's 3 tool rounds are SAVED mid-turn, so a
# kill on round 4 loses only the in-flight round, not the whole conversation.
# RED today: 0 of the 3 rounds are in the store.
rounds=$(grep -o 'round-[123]-of-killed-turn' "$sfile" 2>/dev/null | sort -u | wc -l)
if [ "$rounds" -ge 3 ]; then
    t_ok "mid-turn persist: all 3 in-flight tool rounds are saved (the killed turn is resumable)"
else
    t_fail "mid-turn persist: only $rounds/3 in-flight rounds saved (a SIGKILL on round 4 would lose the whole turn)"
fi

# --- the stale-tmp sweep -----------------------------------------------------
# 6: a dead-pid <id>.json.tmp<pid> is swept at listing time.
sleep 1 &
DEAD=$!
wait "$DEAD" 2>/dev/null
stale="$store/$sid.json.tmp$DEAD"
: > "$stale"
(cd "$ws" && "$BIN" ls --output json < /dev/null >/dev/null 2>&1)
if [ ! -e "$stale" ]; then
    t_ok "stale tmp: the dead-pid $sid.json.tmp$DEAD was swept on listing"
else
    t_fail "stale tmp: dead-pid $stale survived listing (invisible to every removal path)"
fi

# 7: a LIVE-pid tmp (our own in-progress write) is KEPT -- the sweep must not
# delete a writer that is still running.
# Use a long-lived process we control.
sleep 300 &
LIVE=$!
# Give it a moment to be fully running.
sleep 1
kill -0 "$LIVE" 2>/dev/null || {
    t_fail "live-pid test: could not start a live process"
    kill "$LIVE" 2>/dev/null; wait "$LIVE" 2>/dev/null
    t_done; exit 1
}
live="$store/$sid.json.tmp$LIVE"
: > "$live"
(cd "$ws" && "$BIN" ls --output json < /dev/null >/dev/null 2>&1)
if [ -e "$live" ]; then
    t_ok "stale tmp: the live-pid tmp (our own in-progress write) is kept"
else
    t_fail "stale tmp: the LIVE-pid $live was wrongly deleted"
fi
kill "$LIVE" 2>/dev/null; wait "$LIVE" 2>/dev/null

# --- the durable writer ------------------------------------------------------
# An LD_PRELOAD fsync counter proves the session save really fsync's a file in
# the store. Build it here (best-effort: if cc is missing, source-check that
# the save routes to the durable variant, rather than weakening the assertion
# to silence).
fswrap="$tmp/fswrap.so"
cat > "$tmp/fswrap.c" <<'EOF'
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
/* Count fsync() calls on any fd whose path is under .jichi.d/sessions/,
 * appending a line to the file named by $JICHI_M1083_FSYNC_OUT (set by the
 * driver). */
int fsync(int fd)
{
    static int (*real)(int) = 0;
    char link[64], path[4096];
    ssize_t n;
    const char *out = getenv("JICHI_M1083_FSYNC_OUT");
    if (real == 0) real = (int (*)(int))dlsym(RTLD_NEXT, "fsync");
    if (snprintf(link, sizeof link, "/proc/self/fd/%d", fd) < (int)sizeof link) {
        n = readlink(link, path, sizeof path - 1);
        if (n > 0 && out != NULL) {
            path[n] = '\0';
            if (strstr(path, ".jichi.d/sessions/") != NULL) {
                FILE *f = fopen(out, "a");
                if (f) { fputs("1\n", f); fclose(f); }
            }
        }
    }
    return real(fd);
}
EOF
if command -v cc >/dev/null 2>&1 && \
   cc -shared -fPIC -o "$fswrap" "$tmp/fswrap.c" -ldl 2>"$tmp/fswrap.build"; then
    fcount="$tmp/fsync.count"; rm -f "$fcount"
    # A completed turn ends with a real jc_session_save -- the durable call we
    # count.
    mm_start_unbounded "$tmp/t1.mm" "$tmp/cap3"
    write_config "$tmp/config3.json" "$MM_PORT" '"maxToolIters":20'
    ( cd "$ws" && JICHI_M1083_FSYNC_OUT="$fcount" LD_PRELOAD="$fswrap" \
          "$BIN" --config "$tmp/config3.json" -q --continue \
          -p "do round one" < /dev/null > "$tmp/save.out" 2> "$tmp/save.err" ); src=$?
    mm_stop
    n=$(grep -c 1 "$fcount" 2>/dev/null); [ -n "$n" ] || n=0
    if [ "$n" -ge 1 ]; then
        t_ok "durable writer: fsync fired on a session-store file (n=$n)"
    else
        t_fail "durable writer: no fsync on the session file (n=$n, src=$src) -- the save is still non-durable"
    fi
else
    if grep -q 'jc_write_file_atomic_durable' "$SMOKE_ROOT/src/session/jc_session.c"; then
        t_ok "durable writer: session save routes to jc_write_file_atomic_durable (cc absent, source-checked)"
    else
        t_fail "durable writer: session save does not route to the durable variant"
    fi
fi

t_done
