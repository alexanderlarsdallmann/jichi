#!/bin/sh
# M801: a lead must stay reachable over --control while it is BUSY. Before M801 the
# control socket was served only between tool calls, so `status` sat unanswered for
# the length of a long call (~15 s in this driver). M801 polls a status-immediate,
# abort-deferred control service inside the shell wait loop and the pool select
# loop, so status is answered within ~1 s. This began as the drive experiment
# E1-S1. Set E1_RES to keep the probe artifacts. Arms, one probe each:
#   C  positive control: a quick tool call every ~1 s (many boundaries)
#   T  the lead is inside ONE long tool call (run_terminal_command "sleep 20")
#   P  the lead is inside spawn_parallel; its one child waits 20 s in a model call
#      (the mock's `delay`, the drive's slow/dead-call shape; a read-only child's
#      shell is refused, which made v1's P arm finish before the probe)
# At +5 s, `control status` and `control inject` are sent CONCURRENTLY; each one's
# latency and exit code is recorded, and the lead's real end time. v1 lesson: a
# latency check must require the probe to have SUCCEEDED -- a refused connection
# has latency 0 and is not an answer.
. "$(dirname "$0")/_smoke.sh"

t_plan 7
smoke_home
RES="${E1_RES:-$(smoke_tmp)}"

run_arm() {
    arm="$1"; script="$2"
    tmp=$(smoke_tmp); ws=$(smoke_tmp)
    printf '%s\n' "$script" > "$tmp/replies.mm"
    mm_start_unbounded "$tmp/replies.mm" "$tmp/cap"
    cat > "$tmp/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0,
 "parallelTaskTimeout":120,"timeouts":{"connect":20,"stall":0,"request":0}}
EOF
    t0=$(date +%s)
    ( cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config.json" -q --no-session \
        --auto --control ctl.sock --journal "$tmp/journal.jsonl" \
        -p "LEADPROMPT $arm" < /dev/null > "$tmp/out.txt" 2>&1
      echo "$? $(date +%s)" > "$tmp/rc" ) &
    lead=$!
    i=0
    while [ ! -S "$ws/ctl.sock" ] && [ "$i" -lt 50 ]; do sleep 0.2; i=$((i + 1)); done
    sock_seen=$(( $(date +%s) - t0 ))
    sleep 5
    p0=$(date +%s)
    ( cd "$ws" && timeout 60 "$BIN" control ctl.sock status > "$tmp/status.out" 2>&1
      echo "$? $(date +%s)" > "$tmp/status.rc" ) &
    sp=$!
    ( cd "$ws" && timeout 60 "$BIN" control ctl.sock inject "PING-$arm" > "$tmp/inject.out" 2>&1
      echo "$? $(date +%s)" > "$tmp/inject.rc" ) &
    ip=$!
    wait "$sp"; wait "$ip"; wait "$lead"
    mm_stop
    read s_rc s_t < "$tmp/status.rc"; read i_rc i_t < "$tmp/inject.rc"; read l_rc l_t < "$tmp/rc"
    ARM_S_RC=$s_rc; ARM_S_LAT=$((s_t - p0)); ARM_I_RC=$i_rc; ARM_I_LAT=$((i_t - p0))
    ARM_END=$((l_t - t0)); ARM_PROBE_AT=$((p0 - t0)); ARM_OUT=$(head_bytes 300 < "$tmp/out.txt")
    j_inj=$(grep -c '"cmd": *"inject"' "$tmp/journal.jsonl" 2>/dev/null)
    mkdir -p "$RES/$arm"; cp -p "$tmp/replies.mm" "$tmp/journal.jsonl" "$tmp/out.txt" \
        "$tmp/status.out" "$tmp/inject.out" "$RES/$arm/" 2>/dev/null
    echo "# arm $arm: socket seen at +${sock_seen}s, probes at +${ARM_PROBE_AT}s; status rc $s_rc in ${ARM_S_LAT}s; inject rc $i_rc in ${ARM_I_LAT}s; lead ended at +${ARM_END}s rc $l_rc; journal inject events $j_inj; mock requests $(ls "$tmp/cap" | grep -c .)"
    echo "#   status: $(tr '\n' ' ' < "$tmp/status.out" | head_bytes 200)"
    echo "#   inject: $(tr '\n' ' ' < "$tmp/inject.out" | head_bytes 160)"
    echo "#   final:  $(printf '%s' "$ARM_OUT" | tr '\n' ' ' | head_bytes 120)"
}

# A probe counts only if it SUCCEEDED and was sent while the lead was still running.
answered_fast() {  # rc lat end probe_at
    [ "$1" = 0 ] && [ "$2" -le 3 ] && [ "$3" -gt "$4" ]
}
valid_probe() {    # rc end probe_at : the probe reached a live run
    [ "$1" = 0 ] && [ "$2" -gt "$3" ]
}

C_SCRIPT='wire openai'
n=1
while [ "$n" -le 14 ]; do
    C_SCRIPT="$C_SCRIPT
rule
  count $n
  tool run_terminal_command {\"command\":\"sleep 1\"}"
    n=$((n + 1))
done
C_SCRIPT="$C_SCRIPT
rule
  text CDONE"
run_arm C "$C_SCRIPT"
C_RC=$ARM_S_RC; C_LAT=$ARM_S_LAT; C_END=$ARM_END; C_AT=$ARM_PROBE_AT

run_arm T 'wire openai
rule
  match "\"role\":\"tool\""
  text TDONE
rule
  tool run_terminal_command {"command":"sleep 20"}'
T_RC=$ARM_S_RC; T_LAT=$ARM_S_LAT; T_END=$ARM_END; T_AT=$ARM_PROBE_AT

run_arm P 'wire openai
rule
  match "LEADPROMPT"
  match "\"role\":\"tool\""
  text PDONE
rule
  match "LEADPROMPT"
  tool spawn_parallel {"tasks":[{"task":"TASK_SLOW: answer CHILDDONE"}]}
rule
  match "TASK_SLOW"
  delay 20000
  text CHILDDONE'
P_RC=$ARM_S_RC; P_LAT=$ARM_S_LAT; P_END=$ARM_END; P_AT=$ARM_PROBE_AT

answered_fast "$C_RC" "$C_LAT" "$C_END" "$C_AT" && t_ok "C: status answered in ${C_LAT}s -- the probe CAN say yes" \
    || t_fail "C: status rc $C_RC in ${C_LAT}s -- the instrument cannot say yes; nothing below means anything"
valid_probe "$T_RC" "$T_END" "$T_AT" && t_ok "T: the probe reached a live run (valid)" || t_fail "T: probe invalid (rc $T_RC, lead ended +${T_END}s, probe +${T_AT}s)"
valid_probe "$P_RC" "$P_END" "$P_AT" && t_ok "P: the probe reached a live run (valid)" || t_fail "P: probe invalid (rc $P_RC, lead ended +${P_END}s, probe +${P_AT}s)"
[ "$T_END" -ge 19 ] && t_ok "T: the lead really spent ~20 s in the tool call (ended +${T_END}s)" || t_fail "T: ended at +${T_END}s -- the long call did not happen"
[ "$P_END" -ge 19 ] && t_ok "P: the batch really took ~20 s (ended +${P_END}s)" || t_fail "P: ended at +${P_END}s -- the slow child did not happen"
answered_fast "$T_RC" "$T_LAT" "$T_END" "$T_AT" && t_ok "T: reachable during one long tool call (${T_LAT}s)" || t_fail "T: status took ${T_LAT}s -- unreachable during one long tool call"
answered_fast "$P_RC" "$P_LAT" "$P_END" "$P_AT" && t_ok "P: reachable inside spawn_parallel (${P_LAT}s)" || t_fail "P: status took ${P_LAT}s -- unreachable inside spawn_parallel"
echo "# summary: status latency C ${C_LAT}s  T ${T_LAT}s  P ${P_LAT}s"
t_done
