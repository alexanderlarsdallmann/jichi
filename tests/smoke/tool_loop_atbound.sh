#!/bin/sh
# M806 (R6): the in-turn loop detector must SEE a shell command that keeps ending
# at its own `timeout` bound -- the shape that ran for three hours on the
# 2026-09-26 drive and fired nothing. Two failures made it invisible: the check was
# spelled differently each time (so the exact key never matched its own repeats),
# and half the runs were piped into `head` (exit 0, so they were "successes" that
# never reached the failure detector). This promotes the drive experiment
# e1_s3_loop.sh: one lead runs the same hanging check six ways -- four end at their
# own timeout (exit 124), two are pipe-masked (exit 0) -- and the detector must
# fire by the THIRD run and again once the pipe-masked runs are counted.
#
# The NEGATIVE case is the over-normalisation guard: two genuinely DIFFERENT
# commands, each cut off at its bound, must NOT trip -- collapsing distinct
# commands into one key would muzzle a working agent (and AT_BOUND opens no shared
# class slot, so four mixed bound-ended calls stay silent).
. "$(dirname "$0")/_smoke.sh"

t_plan 5
smoke_home
tmp=$(smoke_tmp); ws=$(smoke_tmp)

# ---- positive: one command, six spellings, all cut off at their own bound -----
H='sh -c \"sleep 10\"'
{
  echo 'wire openai'
  n=1
  for cmd in "timeout 2 $H" "cd . && timeout 2 $H" "timeout 3 $H" "timeout 2 $H 2>&1" \
             "timeout 2 $H | head -5" "cd . && timeout 2 $H | head -5"; do
      printf 'rule\n  count %d\n  tool run_terminal_command {"command":"%s"}\n' "$n" "$cmd"
      n=$((n + 1))
  done
  printf 'rule\n  text LOOPDONE\n'
} > "$tmp/replies.mm"
mm_start_unbounded "$tmp/replies.mm" "$tmp/cap"
cat > "$tmp/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF
( cd "$ws" && with_deadline 120 "$BIN" --config "$tmp/config.json" --no-session --auto \
    --log "$tmp/telemetry.jsonl" --log-level full \
    -p "run the check" < /dev/null > "$tmp/out.txt" 2> "$tmp/err.txt" )
mm_stop
warns=$(grep -c 'tool loop:' "$tmp/err.txt" 2>/dev/null)
first=$(grep 'tool loop:' "$tmp/err.txt" 2>/dev/null | head -1)
calls=$(grep -c '"event":"tool_call"' "$tmp/telemetry.jsonl" 2>/dev/null)
exits=$(grep '"event":"tool_call"' "$tmp/telemetry.jsonl" 2>/dev/null | grep -o '"exit":[0-9-]*' | sed 's/.*://' | tr '\n' ' ')
echo "# positive: $calls tool calls; exit codes: $exits; loop warnings=$warns; first: ${first:-none}"
if [ -n "${E1_RES:-}" ]; then mkdir -p "$E1_RES"; cp -p "$tmp/telemetry.jsonl" "$tmp/err.txt" "$E1_RES/" 2>/dev/null; fi

[ "$calls" = 6 ] && grep -q LOOPDONE "$tmp/out.txt" 2>/dev/null \
    && t_ok "instrument: the six scripted runs happened and the run finished" \
    || t_fail "instrument: $calls tool calls, finished=$(grep -c LOOPDONE "$tmp/out.txt" 2>/dev/null)"
[ "$warns" -ge 1 ] && t_ok "the detector fired on the bound-ended runs ($first)" \
    || t_fail "the detector never fired on six bound-ended runs"
echo "$first" | grep -q 'failed [0-3]x' \
    && t_ok "it fired BY THE THIRD run (normalised key collapses the six spellings)" \
    || t_fail "it did not fire by the third run (first: ${first:-none})"
grep 'tool loop:' "$tmp/err.txt" 2>/dev/null | grep -q 'failed [5-9]x' \
    && t_ok "the pipe-masked (exit 0) runs were counted on the same counter" \
    || t_fail "the two pipe-masked bound-ended runs were not counted"

# ---- negative: two DIFFERENT bound-ended commands must not trip ---------------
tmp2=$(smoke_tmp); ws2=$(smoke_tmp)
A='sh -c \"sleep 10\"'; B='sh -c \"sleep 20\"'
{
  echo 'wire openai'
  n=1
  for cmd in "timeout 2 $A" "timeout 2 $B" "timeout 2 $A" "timeout 2 $B"; do
      printf 'rule\n  count %d\n  tool run_terminal_command {"command":"%s"}\n' "$n" "$cmd"
      n=$((n + 1))
  done
  printf 'rule\n  text NEGDONE\n'
} > "$tmp2/replies.mm"
mm_start_unbounded "$tmp2/replies.mm" "$tmp2/cap"
cat > "$tmp2/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0}
EOF
( cd "$ws2" && with_deadline 120 "$BIN" --config "$tmp2/config.json" --no-session --auto \
    -p "run the checks" < /dev/null > "$tmp2/out.txt" 2> "$tmp2/err.txt" )
mm_stop
negwarns=$(grep -c 'tool loop:' "$tmp2/err.txt" 2>/dev/null)
echo "# negative: two distinct commands x2 each; loop warnings=$negwarns (want 0)"
[ "$negwarns" = 0 ] && t_ok "two genuinely different bound-ended commands do NOT trip the detector" \
    || t_fail "over-normalisation: distinct commands tripped the detector ($negwarns warnings)"
t_done
