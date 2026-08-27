#!/bin/sh
# smoke: background commands (M26). A four-step mock turn: start a
# detached command (writes a sentinel then sleeps 100s), read its output,
# kill it, answer. Proves run_in_background detaches, read/kill work, and
# the agent exits cleanly instead of hanging on the sleeping child.
# The `delay 800` before the read is LOAD-BEARING: without it the whole
# start/read/kill sequence completes in milliseconds and kill_background
# lands before the detached shell has even exec'd -- the echo never runs
# (the sentinel check then fails; observed on the first port attempt).
# (Port of tests/e2e/bg.py, M211.)
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
sentinel="$ws/bg_ran"

cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"echo started > '$sentinel'; sleep 100","run_in_background":true}
rule
  count 2
  delay 800
  tool read_background_output {"id":1}
rule
  count 3
  tool kill_background {"id":1}
rule
  text BG_DONE
EOF

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT"

t0=$(date +%s)
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      -q --no-session --auto -p "run it in the background" < /dev/null)
rc=$?
t1=$(date +%s)
mm_stop

case "$out" in
    *BG_DONE*) t_ok "the start/read/kill/answer sequence completed" ;;
    *) t_fail "turn incomplete (rc=$rc): $(printf '%s' "$out" | head_bytes 120)" ;;
esac

# the agent must exit promptly, not wait out the child's 100s sleep
if [ $((t1 - t0)) -lt 50 ]; then
    t_ok "the sleeping child did not hold the agent ($((t1 - t0))s)"
else
    t_fail "run took $((t1 - t0))s -- the background child was not reaped"
fi

i=0
while [ ! -f "$sentinel" ] && [ $i -lt 5 ]; do
    i=$((i + 1)); sleep 1
done
if [ -f "$sentinel" ]; then
    t_ok "the background command really ran (sentinel written)"
else
    t_fail "no sentinel -- the detached command never ran"
fi

t_done
