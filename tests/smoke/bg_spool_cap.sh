#!/bin/sh
# smoke: background spool cap (def-324, M962). A four-step mock turn: start
# a headless mockmodel run that issues a background command writing > 512 KiB
# over ~0.65 seconds (paced writes), then stays alive for several seconds.
# A background probe stats the spool file every 0.2s during the run and
# records the max size seen. Verify max size <= JC_BG_SPOOL_MAX (524288 bytes).
# The drain runs mid-write and caps the file.
# (Port of tests/e2e/bg.py, M211; def-324/curated, M962.)

. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
sentinel="$ws/bg_ran"

# Start a background probe that stats the spool file every 0.2s during the run
# and records the max size seen. This runs concurrently with the jichi run.
spool_dir="$HOME/.jichi.d/bgspool"
max_size=0
max_size_file="$ws/max_spool_size"
echo "0" > "$max_size_file"

# Start the probe in background
(
    for i in $(seq 1 30); do
        if [ -d "$spool_dir" ]; then
            spool_file=$(ls -t "$spool_dir"/bg-*.out 2>/dev/null | head -1)
            if [ -n "$spool_file" ] && [ -f "$spool_file" ]; then
                spool_size=$(wc -c < "$spool_file" 2>/dev/null | tr -d ' ' || echo 0)
                current_max=$(cat "$max_size_file" 2>/dev/null || echo 0)
                if [ "$spool_size" -gt "$current_max" ]; then
                    echo "$spool_size" > "$max_size_file"
                fi
            fi
        fi
        sleep 0.2
    done
) &
    probe_pid=$!

# Use the original writer from the smoke test
cat > "$tmp/replies.mm" <<EOF
wire openai
rule
  count 1
  tool run_terminal_command {"command":"echo started > '$sentinel'; for i in \$(seq 1 2048); do yes MARK | sed 205q; sleep 0.001; done; sleep 10","run_in_background":true}
rule
  count 2
  delay 800
  tool read_background_output {"id":1}
rule
  count 3
  delay 200
  tool read_background_output {"id":1}
rule
  text BG_DONE
EOF

mm_start "$tmp/replies.mm" "$tmp"
write_config "$tmp/config.json" "$MM_PORT"

# Save the agent's output to a file so we can check tool results
log_file="$ws/agent.log"
t0=$(date +%s)
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
      -q --no-session --auto -p "run it in the background" < /dev/null)
printf '%s' "$out" > "$log_file"
rc=$?
t1=$(date +%s)
mm_stop

# Stop the probe
kill $probe_pid 2>/dev/null
wait $probe_pid 2>/dev/null

case "$out" in
    *BG_DONE*) t_ok "the start/read/read/answer sequence completed" ;;
    *) t_fail "turn incomplete (rc=$rc): $(printf '%s' "$out" | head_bytes 120)" ;;
esac

# The agent must exit promptly
if [ $((t1 - t0)) -lt 50 ]; then
    t_ok "the background child did not hold the agent ($((t1 - t0))s)"
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

# Verify the max spool size seen during the run
max_seen=$(cat "$max_size_file" 2>/dev/null || echo 0)
if [ "$max_seen" -gt 0 ] && [ "$max_seen" -le 524288 ]; then
    t_ok "spool cap exercised mid-write: max size seen $max_seen <= JC_BG_SPOOL_MAX (524288)"
elif [ "$max_seen" -gt 524288 ]; then
    t_fail "spool cap exceeded: max size seen $max_seen > JC_BG_SPOOL_MAX (524288)"
else
    t_fail "spool cap not exercised: max size seen $max_seen (file may have been unlinked)"
fi

t_done
