#!/bin/sh
# smoke: with the relay on, an exhausted window ends the run as "handover" (M920).
#
# THE DEFECT (proposal docs/proposals/2026-10-context-relay.md, Review 2):
# when the window is exhausted, today's run grinds on -- sawtooth compaction,
# the 400 wall, stop_reason "error", no notes, no next leg. The relay's first
# shippable slice (M920) is the DECISION and the HONEST EXIT: jichi -- not
# the model, which claimed a pressure signal that never came (P8) -- measures
# the round's numbers with the pure core (src/util/jc_relay.c) and, when it
# says hand over, ends the run at the round boundary with a distinct
# stop_reason, the work kept and the history wire-valid. The baton (notes +
# next leg) is the follow-up; this milestone proves the moment is picked and
# the exit is explained, off by default.
#
# THE RIG: the mock reports prompt_tokens 1000 on every request (a usage
# line), the model declares contextLength 1000, and every request answers
# with one read_file call, so the run keeps going until something stops it.
# E3 -- the server's count at or above 90% of the window -- is the trigger:
# 1000 >= 900, measured, no forecast. With the relay ON the run ends after
# the round in which the window was measured exhausted, with
# stop_reason "handover" and the log naming the reason. With the relay OFF
# (the default) the same run ends at the maxToolIters cap,
# stop_reason "max_iters" -- the feature is inert until it is asked for.
#
# WHAT IT HOLDS, each against the effect:
#   1. the rig is real: both runs produced a JSON result with a stop_reason.
#   2. THE TOOTH: relay on -> stop_reason "handover" (pre-M920: "max_iters").
#   3. the exit is explained: the log names the relay and the reason that
#      fired (hard_limit) -- the decision is from numbers, per 6.1.
#   4. off by default: the same config WITHOUT "relay" does not hand over.
#   5. a handover keeps the work: exit 0, like max_iters (the history is
#      intact and the reason is machine-readable, not prose).
. "$(dirname "$0")/_smoke.sh"

t_plan 6
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)
JQ="$SMOKE_TOOLS/jsonq"

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  match "messages"
  usage 1000 5
  tool read_file {"path":"a.txt"}
EOF
printf 'a small file for the rig\n' > "$ws/a.txt"

mm_start "$tmp/replies.mm" "$tmp/cap"
# contextLength 1000 on the model: the window the server count is measured
# against. maxToolIters 3 caps the relay-OFF run.
MODEL_X='"contextLength":1000'
TOP_X='"maxToolIters":3'
write_config "$tmp/config_on.json" "$MM_PORT" \
    "$TOP_X,\"relay\":true" "$MODEL_X"
write_config "$tmp/config_off.json" "$MM_PORT" "$TOP_X" "$MODEL_X"

# The same run, twice: relay on, relay off (the default).
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config_on.json" \
    --no-session --output json -p "read the file" < /dev/null \
    > "$tmp/on.json" 2> "$tmp/on.err"); rc_on=$?
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config_off.json" \
    --no-session --output json -p "read the file" < /dev/null \
    > "$tmp/off.json" 2> "$tmp/off.err"); rc_off=$?
mm_stop

stop_of() {
    "$JQ" '.stop_reason' "$1" 2>/dev/null
}

# 1: the floor -- both runs finished and reported a machine-readable reason
if [ -n "$(stop_of "$tmp/on.json")" ] && [ -n "$(stop_of "$tmp/off.json")" ]; then
    t_ok "the rig ran both ways and each produced a stop_reason"
else
    t_fail "a run produced no JSON stop_reason (on='$(stop_of "$tmp/on.json")', \
off='$(stop_of "$tmp/off.json")') -- the rig is not measuring the exit"
fi

# 2: THE TOOTH -- with the relay on, exhaustion ends the run as a handover
if [ "$(stop_of "$tmp/on.json")" = "handover" ]; then
    t_ok "with the relay on, an exhausted window ends the run as handover"
else
    t_fail "relay on gave stop_reason '$(stop_of "$tmp/on.json")' (want handover): \
the window was exhausted but the run ground on to its cap"
fi

# 3: the decision is explained by its measurement, not prose
if grep -q '\[relay\] window exhausted (hard_limit)' "$tmp/on.err"; then
    t_ok "the log names the relay and the reason that fired (hard_limit)"
else
    t_fail "no [relay] explanation in the log: $(head_bytes 200 "$tmp/on.err")"
fi

# 4: off by default -- the same config without "relay" never hands over
off_stop=$(stop_of "$tmp/off.json")
if [ -n "$off_stop" ] && [ "$off_stop" != "handover" ]; then
    t_ok "with the relay off (the default), the same run does not hand over ($off_stop)"
else
    t_fail "the relay fired with the config key absent (off_stop='$off_stop')"
fi

# 5: a handover keeps the work -- exit 0, like max_iters, not an error
if [ $rc_on -eq 0 ] && [ $rc_off -eq 0 ] && [ "$(stop_of "$tmp/on.json")" != "error" ]; then
    t_ok "both exits are 0: a handover is an explained stop, not a failure"
else
    t_fail "exit codes on=$rc_on off=$rc_off (want 0 both); stop_on='$(stop_of "$tmp/on.json")'"
fi

# ---- 6 (M1048 G18 reviewer MEDIUM): a verify-armed relay HANDOVER is a stop ---
# The M1048 reach footer SPLIT the armed-but-inconclusive verifier into two
# sentences: "stopped first" (M688) vs "did not run (no snapshot / no change
# made)" (verifier_not_run_completed = armed + outcome ok + !verifier_ran +
# !turn_capped). A relay handover breaks the agent loop WITHOUT a final
# answer, so the completion-verify gate never runs and the outcome is
# promoted RUNNING->OK; turn_capped stays 0, so the field fires and the
# footer names a cause ("no snapshot / no change made") that a handover is
# not -- the run STOPPED FIRST. The only other way to reach that field is
# the cap, which the exclusion !turn_capped is precisely for.
# This run is the relay-ON leg from check 2 (handover), re-driven in TEXT
# mode (the reach footer prints only there) with --verify "true" arming the
# completion verifier. It goes RED on the current tree: the footer carries
# the dead sentence. It goes GREEN after the field and the sentence are
# removed: every armed + not-red + not-green state prints M688's "stopped
# first", and the dead sentence has no referent left to fire on. The second
# half of the assertion (the negative) is the guard against the sentence
# silently coming back.
G=/usr/bin/grep
[ -x "$G" ] || G=grep
mm_start "$tmp/replies.mm" "$tmp/cap6"
# The config was written for the FIRST mock's port; the fresh instance is on
# a new one, so rewrite it for check 6 (same content).
write_config "$tmp/config_on6.json" "$MM_PORT" \
    "$TOP_X,\"relay\":true" "$MODEL_X"
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config_on6.json" \
    --no-session --verify "true" -p "read the file" < /dev/null \
    > "$tmp/on6.out" 2> "$tmp/on6.err")
mm_stop
if $G -q 'verifier never reached a verdict' "$tmp/on6.err" &&
   $G -q 'stopped first' "$tmp/on6.err" &&
   ! $G -q 'no snapshot / no change made' "$tmp/on6.err" &&
   ! $G -q 'no verifier armed' "$tmp/on6.err"; then
    t_ok "a verify-armed relay handover is a stop: the footer says 'stopped first', never 'no snapshot / no change made'"
else
    t_fail "the relay handover's not-run verifier sentence mis-names the cause: \
stopped-first=$($G -c 'stopped first' "$tmp/on6.err"), \
no-snapshot=$($G -c 'no snapshot / no change made' "$tmp/on6.err"), \
armed=$($G -c 'no verifier armed' "$tmp/on6.err"). \
footer: $($G 'not checked' "$tmp/on6.err" | head_bytes 300)"
fi

t_done
