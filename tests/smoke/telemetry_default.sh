#!/bin/sh
# smoke: telemetry (metrics) is ON by default, one log per workspace, and the
# readers prefer that log (M599).
#
# THE DECISION THIS PINS. The operator, 2026-08-27: "jichi's learner must learn
# from jichi's own development, and dogfooding. Telemetry should be on by
# default, otherwise a learner forgets." Measured that day: three telemetry logs
# on the machine, all from other projects, none for jichi -- `learn analyze
# --workspace .` had nothing to read, because `logging` was off unless a config
# said otherwise and nobody's did.
#
# THREE THINGS MUST HOLD TOGETHER, or "on by default" is a leak, not a memory:
#   1. a run with no `logging` config writes metrics (check 2);
#   2. it writes them to ONE file per workspace, appended across runs, named
#      after the workspace -- not one <uuid>.jsonl per run, which would give the
#      miner (which reads one log) a one-run memory (checks 2-3);
#   3. `--log-level off` still turns it off (check 4), and the readers prefer
#      the workspace's own log over whatever file is newest (check 5) -- the
#      M533 rule, write where the reader reads, applied to a log.
# `metrics` carries no prompt, response or code (TELEMETRY.md), which is why the
# default can move without moving the privacy posture.
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
wsbase=$(basename "$ws")
tdir="$HOME/.jichi.d/telemetry"

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  text DONE
EOF

mm_start "$tmp/replies.mm" "$tmp/cap"
write_config "$tmp/config.json" "$MM_PORT"

# --- 1/2: a run with no logging config writes ONE metrics log, named for the ws
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    -q --no-session -p "say DONE" < /dev/null > /dev/null 2>"$tmp/err1"); rc=$?
if [ $rc -eq 0 ]; then
    t_ok "the run completed (rc=0)"
else
    t_fail "run rc=$rc: $(head_bytes 200 "$tmp/err1")"
fi
n=$(ls "$tdir" 2>/dev/null | grep -c '\.jsonl$')
named=$(ls "$tdir" 2>/dev/null | grep -c "^$wsbase-[0-9][0-9]*\.jsonl$")
if [ "$n" -eq 1 ] && [ "$named" -eq 1 ]; then
    t_ok "exactly one telemetry log, named after the workspace ($wsbase-<key>.jsonl)"
else
    t_fail "expected 1 log named $wsbase-<key>.jsonl, found $n log(s): $(ls "$tdir" 2>/dev/null | tr '\n' ' ')"
fi

# --- 3: a second run in the same workspace APPENDS to the same file -----------
(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    -q --no-session -p "say DONE" < /dev/null > /dev/null 2>"$tmp/err2")
n2=$(ls "$tdir" 2>/dev/null | grep -c '\.jsonl$')
starts=$(cat "$tdir"/*.jsonl 2>/dev/null | grep -c '"event":"turn_start"')
if [ "$n2" -eq 1 ] && [ "$starts" -eq 2 ]; then
    t_ok "the second run appended: still one file, two turn_start events"
else
    t_fail "after two runs: $n2 file(s), $starts turn_start event(s) -- per-run files would give the miner a one-run memory"
fi

# --- 4: --log-level off still means off ---------------------------------------
ws2=$(smoke_tmp)
(cd "$ws2" && with_deadline 60 "$BIN" --config "$tmp/config.json" \
    --log-level off -q --no-session -p "say DONE" < /dev/null > /dev/null 2>&1)
n3=$(ls "$tdir" 2>/dev/null | grep -c '\.jsonl$')
if [ "$n3" -eq 1 ]; then
    t_ok "--log-level off wrote nothing (still one file, from the first workspace)"
else
    t_fail "--log-level off still wrote a log: $(ls "$tdir" | tr '\n' ' ')"
fi
mm_stop

# --- 5: the reader prefers the workspace's own log over a newer stranger -----
# A decoy that is NEWER than the workspace log: the pre-M599 reader ("newest
# .jsonl under telemetry/") would pick it and summarize somebody else's project.
sleep 1
printf '%s\n' '{"v":1,"ts":1,"event":"turn_start","ws":"/elsewhere"}' > "$tdir/zzz-decoy.jsonl"
out=$(cd "$ws" && with_deadline 60 "$BIN" --config "$tmp/config.json" telemetry \
        < /dev/null 2>&1 | head -n 1)
case "$out" in
    *"$wsbase-"*) t_ok "\`jichi telemetry\` read the workspace's own log, not the newer decoy" ;;
    *) t_fail "the reader picked: $out" ;;
esac

t_done
