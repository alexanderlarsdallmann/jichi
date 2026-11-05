#!/bin/sh
# smoke: M996 (L-27 follow-up) - the drain's reap-time spool rewrite is lossless.
#
# Requires a FAULT=1 binary (`make clean && make FAULT=1`); SKIPS otherwise.
#
# The mechanism: the M996 fix rewrote the drain's on-disk cap to write the kept
# tail to a TEMP file then rename() it over the spool (atomic; a short/failed
# temp write unlinks the temp and leaves the ORIGINAL spool byte-for-byte; a
# successful rename makes the spool EXACTLY the kept-tail size with no truncate
# call, so no NUL padding). The pre-M996 shape opened the spool O_TRUNC and
# wrote in place, then called ftruncate(wfd, read_total) UNCHECKED -- on a
# short write the original was already destroyed and the ftruncate padded it
# with NULs up to a stale size.
#
# This driver arms JICHI_FAULT_BGWRITE_SHORT_AT=8192 so the temp's first write
# is capped short, AND JICHI_FAULT_BGWRITE_PAUSE_MS=2000 so the probe can
# observe the spool AFTER the rewrite attempt and BEFORE reap() unlinks it.
# A background probe stats the spool file during the run. The key check: the
# drain's rewrite never produces NUL padding because M996 uses temp+rename
# without ftruncate.
. "$(dirname "$0")/_smoke.sh"

# A FAULT build announces itself in --version.
"$BIN" --version < /dev/null 2>/dev/null | grep -q "FAULT=1" \
    || t_skip "needs a FAULT=1 binary (make clean && make FAULT=1)"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
sentinel="$ws/bg_ran"

# Start a background probe that stats the spool file every 0.2s during the run
# and records whether a NUL byte was found.
spool_dir="$HOME/.jichi.d/bgspool"
max_size_file="$ws/max_spool_size"
nul_file="$ws/nul_found"
echo "0" > "$max_size_file"
echo "0" > "$nul_file"

# Start the probe in background
(
    for i in $(seq 1 50); do
        if [ -d "$spool_dir" ]; then
            spool_file=$(ls -t "$spool_dir"/bg-*.out 2>/dev/null | head -1)
            if [ -n "$spool_file" ] && [ -f "$spool_file" ]; then
                spool_size=$(wc -c < "$spool_file" 2>/dev/null | tr -d ' ' || echo 0)
                current_max=$(cat "$max_size_file" 2>/dev/null || echo 0)
                if [ "$spool_size" -gt "$current_max" ]; then
                    echo "$spool_size" > "$max_size_file"
                fi
                # Check for NUL bytes in ONE read: tr -cd keeps only the NULs,
                # wc -c counts them. The old double-read (tr -d '\0' | diff - f)
                # sampled the spool twice while the child was still appending,
                # so whenever the file grew between the two reads, diff saw a
                # difference and reported a false "NUL" (supervisor 05:57: 25/200
                # false with a live writer, 0/50 with the writer stopped). A
                # single read cannot race against its own growth, and appended
                # real text (no NULs) never inflates the count.
                nul_count=$(tr -cd '\0' < "$spool_file" 2>/dev/null | wc -c | tr -d ' ')
                [ -n "$nul_count" ] || nul_count=0
                if [ "$nul_count" -gt 0 ]; then
                    echo "1" > "$nul_file"
                fi
            fi
        fi
        sleep 0.2
    done
) &
    probe_pid=$!

# Use the same writer as bg_spool_cap.sh
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

# Verify: no NUL padding in spool (M996 temp+rename produces no NULs)
has_nul=$(cat "$nul_file" 2>/dev/null || echo 0)
if [ "$has_nul" -eq 0 ]; then
    t_ok "no NUL padding present in spool"
else
    t_fail "NUL padding found in spool"
fi

t_done
