#!/bin/sh
# M-L2: a model call whose stream goes DEAD -- headers, then nothing -- must be
# bounded even when the stall CAP is off (a measurement run). The drive's worst
# call ran 11,677 s with `stall 0`; nothing fenced it. The dead-stream fence
# (timeouts.deadStream, default 900) stays on because it can only fire when nothing
# is arriving. Two arms, each under a 45 s outer deadline:
#   OFF  the drive's caps-off settings (stall 0, request 0) + deadStream 5 -- the
#        FENCE must bound the dead child in ~5 s. Before M-L2 there was no deadStream
#        knob and stall 0 left it unbounded, so this arm ran to the 45 s deadline
#        (rc 124): the red half.
#   ON   stall 10 as a cap -- the control, proving the dead child (not something
#        else) is what the fence bounds.
# Promoted from the drive experiment e1_s2_dead.sh (its OFF arm now sets the knob).
. "$(dirname "$0")/_smoke.sh"

t_plan 3
smoke_home

run_arm() {
    arm="$1"; timeouts="$2"
    tmp=$(smoke_tmp); ws=$(smoke_tmp)
    cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "LEADPROMPT"
  match "\"role\":\"tool\""
  text LEADDONE
rule
  match "LEADPROMPT"
  tool spawn_parallel {"tasks":[{"task":"TASK_DEAD: answer CHILDDONE"}]}
rule
  match "TASK_DEAD"
  stall header
EOF
    mm_start_unbounded "$tmp/replies.mm" "$tmp/cap"
    cat > "$tmp/config.json" <<EOF
{"toolProfile":"full","lowResource":false,"models":[{"name":"m","provider":"openai","model":"mock",
 "apiBase":"http://127.0.0.1:$MM_PORT/v1","apiKey":"x","roles":["chat"]}],
 "snapshots":false,"repoMap":false,"references":false,"maxRetries":0,
 "parallelTaskTimeout":259200,"timeouts":$timeouts}
EOF
    t0=$(date +%s)
    ( cd "$ws" && timeout 45 "$BIN" --config "$tmp/config.json" -q --no-session --auto \
        -p "LEADPROMPT $arm" < /dev/null > "$tmp/out.txt" 2>&1; echo "$? $(date +%s)" > "$tmp/rc" )
    mm_stop
    read rc tend < "$tmp/rc"
    ARM_RC=$rc; ARM_T=$((tend - t0)); ARM_OUT=$(tr '\n' ' ' < "$tmp/out.txt" | head_bytes 400)
    echo "# arm $arm: timeouts $timeouts -> rc $rc after ${ARM_T}s"
    echo "#   output: $ARM_OUT"
}

run_arm ON '{"connect":20,"stall":10,"request":0}'
ON_RC=$ARM_RC; ON_T=$ARM_T; ON_OUT=$ARM_OUT
run_arm OFF '{"connect":20,"stall":0,"request":0,"deadStream":5}'
OFF_RC=$ARM_RC; OFF_T=$ARM_T; OFF_OUT=$ARM_OUT

# Instrument first: ON must end by itself, or the dead child is not the only thing
# holding the run and OFF's result would mean nothing.
{ [ "$ON_RC" != 124 ] && [ "$ON_T" -lt 40 ]; } \
    && t_ok "ON (control): the stall cap ended the dead child; the run ended by itself in ${ON_T}s" \
    || t_fail "ON: rc $ON_RC after ${ON_T}s -- the instrument cannot say yes"
case "$ON_OUT" in *LEADDONE*) t_ok "ON (control): the lead got the batch back and answered" ;;
    *) t_fail "ON: no final answer from the lead" ;; esac
# The fix: with the stall cap OFF, the dead-stream fence (deadStream 5) still bounds it.
{ [ "$OFF_RC" != 124 ] && [ "$OFF_T" -lt 40 ]; } \
    && t_ok "OFF: the dead-stream FENCE bounded the dead child with stall off (${OFF_T}s, rc $OFF_RC)" \
    || t_fail "OFF: nothing bounded the dead child -- ran to the 45 s deadline (rc $OFF_RC); the fence did not fire"
# (That the failure names timeouts.deadStream rather than the stall cap the operator
# never set is proven where the message is built -- test_http.c's fence_only case --
# because it is a child-side log line that does not surface in the lead's result.)
t_done
