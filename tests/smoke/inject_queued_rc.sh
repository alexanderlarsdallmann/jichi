#!/bin/sh
# M823 (N5, threadwork's weekend drive): the control client's exit status tells a
# supervisor script "delivered and queued, waiting" apart from "not delivered". A
# command whose connect and write both succeed but whose reply does not arrive
# within the client's wait was DELIVERED -- it is queued for the next tool boundary,
# which a run stuck in a long model call has not reached. That now exits 3, distinct
# from the 1 a failed connect returns. Before M823 both were 1, so a supervisor
# could not tell "wait and re-check" from "the run is gone".
#
# The run's first model call is delayed by the mock, so the run sits inside the call
# with its control socket listening but no boundary reached; a status with a short
# JC_CONTROL_CLIENT_SECS connects, writes, and gets no reply -> exit 3.
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  delay 8000
  text SLOWDONE
EOF
mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT"

# A run whose first (and only) model call the mock holds for 8 s. `exec` so $! is
# jichi itself (OpenBSD ksh; smoke_lint 15); the run's own --deadline bounds it and
# we kill it at the end.
( cd "$ws" && exec "$BIN" --config "$tmp/config.json" --auto \
    --control "$tmp/c.sock" --budget-tokens 100k --deadline 2m --no-session \
    -p "do the task" < /dev/null > "$tmp/run.out" 2>&1 ) &
RUN_PID=$!

# Wait for the control socket (created at run start, before the model call).
i=0
while [ ! -S "$tmp/c.sock" ]; do
    i=$((i + 1)); [ "$i" -gt 20 ] && break
    sleep 1
done
if [ -S "$tmp/c.sock" ]; then
    t_ok "the control socket is listening while the run is in its model call"
else
    kill "$RUN_PID" 2>/dev/null
    t_fail "control socket never appeared"; t_fail "skipped -- no socket"
    t_fail "skipped -- no socket"; mm_stop; t_done
fi

# Delivered but no reply within a 2 s wait: exit 3 (queued), not 1 (not delivered).
JC_CONTROL_CLIENT_SECS=2 "$BIN" control "$tmp/c.sock" status < /dev/null \
    > "$tmp/q.out" 2>&1
qrc=$?
[ "$qrc" = "3" ] \
    && t_ok "a delivered-but-unanswered command exits 3 (queued), not 1" \
    || t_fail "queued status exited $qrc, expected 3 (out: $(head_bytes 160 "$tmp/q.out"))"

# Contrast: a connect that fails (no such socket) is 'not delivered' -> exit 1.
"$BIN" control "$tmp/nope.sock" status < /dev/null > "$tmp/n.out" 2>&1
nrc=$?
[ "$nrc" = "1" ] \
    && t_ok "a failed connect stays exit 1 (not delivered) -- distinct from queued" \
    || t_fail "connect-fail exited $nrc, expected 1 (out: $(head_bytes 160 "$tmp/n.out"))"

kill "$RUN_PID" 2>/dev/null; wait "$RUN_PID" 2>/dev/null
mm_stop
t_done
