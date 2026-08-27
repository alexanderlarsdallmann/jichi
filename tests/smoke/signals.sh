#!/bin/sh
# smoke: the signal exit-code contract a supervisor trusts (src/main.c
# help text): SIGTERM mid-call exits 143 (graceful + PROMPT, M146), SIGINT
# exits 130. The mock stalls mid-stream and the stall window is 60s, so
# the signal provably lands during an in-flight model call and a prompt
# exit proves the graceful path, not the stall timeout firing.
# (Absorbs tests/e2e/sigterm.py, M211.)
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  stall mid
EOF

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT"

# SIGTERM -> 143, promptly
(cd "$ws" && exec "$BIN" --config "$tmp/config.json" --no-session -q \
    --timeout-stall 60 -p "hang" < /dev/null > /dev/null 2>&1) &
pid=$!
sleep 3
if kill -0 "$pid" 2>/dev/null; then
    t_ok "agent is held mid-call by the frozen stream"
else
    wait "$pid"
    t_fail "agent exited before the signal (rc=$?)"
fi
t0=$(date +%s)
kill -TERM "$pid" 2>/dev/null
wait "$pid"; rc=$?
t1=$(date +%s)
if [ $rc -eq 143 ]; then
    t_ok "SIGTERM mid-call exits 143"
else
    t_fail "SIGTERM rc=$rc (want 143)"
fi
if [ $((t1 - t0)) -le 15 ]; then
    t_ok "graceful exit was prompt ($((t1 - t0))s <= 15s), not the stall window"
else
    t_fail "took $((t1 - t0))s after SIGTERM -- the graceful path is wedged"
fi

# SIGINT -> 130
(cd "$ws" && exec "$BIN" --config "$tmp/config.json" --no-session -q \
    --timeout-stall 60 -p "hang" < /dev/null > /dev/null 2>&1) &
pid=$!
sleep 3
kill -INT "$pid" 2>/dev/null
wait "$pid"; rc=$?
mm_stop
if [ $rc -eq 130 ]; then
    t_ok "SIGINT mid-call exits 130"
else
    t_fail "SIGINT rc=$rc (want 130)"
fi

t_done
