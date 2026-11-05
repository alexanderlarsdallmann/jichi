#!/bin/sh
# smoke: a relay handover writes the baton -- the notes the next leg starts
# from (M921, sub-slice B; proposal docs/proposals/2026-10-context-relay.md
# 6.4/6.6). This is the MECHANICAL baton: jichi's own records rendered into
# the fixed template, written at the headless relay exit, off by default
# (the relay is the M920 config key; without it the run never hands over, and
# without a handover no notes are written).
#
# THE RIG: the same exhaustion rig as relay_handover_off_default -- the mock
# reports prompt_tokens 1000 on every request, the model declares
# contextLength 1000, and every request answers with one read_file call.
# E3 (the server's count at or above 90% of the window) fires; with the relay
# ON the run ends at the round boundary as "handover", and the headless tail
# then writes ~/.jichi.d/relay/<session-id>/leg-01.md from the facts the run
# already held. HOME is isolated by smoke_home, so the store lands under the
# driver's temp HOME and the driver can read it back.
#
# WHAT IT HOLDS, each against the effect:
#   1. the rig is real: the relay-ON run produced a JSON "handover" AND a
#      notes file appeared (0600, non-empty) under the store.
#   2. THE TOOTH: the notes hold the four template pieces the mechanical
#      baton always writes -- the header, the task verbatim (section 1),
#      "Where the work stands" (section 2) and "How to use these notes"
#      (section 4).
#   3. the header names the trigger that fired (hard_limit) -- the decision
#      and the notes agree on why the leg ended (6.1: from numbers).
#   4. the log line names the written path ([relay] ... notes written to)
#      -- an operator can find the notes without opening the store.
#   5. off by default / no handover, no notes: the relay-OFF run ends at its
#      cap, and the relay store holds nothing under the OFF run's own session
#      id (the standing positive control: a note is written ONLY on a handover).
#   6. the mechanical part leaves headroom for the account (6.4's cap
#      formula). The mechanical sections are bounded by construction (top-N
#      lists); what is NOT bounded yet is the model's account, which sub-slice
#      C writes and cuts to relayNotesBytes minus the mechanical bytes. So B
#      pins only the half it can: the mechanical note stays well under the
#      default cap, leaving room for C's bounded account. The end-to-end
#      "finished note <= relayNotesBytes" assertion is C's tooth (a mechanical
#      note CAN exceed the cap today, which is exactly the case C closes).
#
# NOTE: the runs are NOT --no-session. The headless JSON carries session_id
# only when a session was used, and check 5 needs the OFF run's id to prove its
# relay directory was never created. smoke_home isolates HOME, so both sessions
# and the relay store land under the driver's temp HOME.
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
# contextLength 1000 -> E3 at 1000 >= 900. maxToolIters 3 caps the OFF run.
MODEL_X='"contextLength":1000'
TOP_X='"maxToolIters":3'
write_config "$tmp/config_on.json" "$MM_PORT" \
    "$TOP_X,\"relay\":true" "$MODEL_X"
write_config "$tmp/config_off.json" "$MM_PORT" "$TOP_X" "$MODEL_X"

store_on="$HOME/.jichi.d/relay"
# The relay store: one directory per session id; the notes are leg-NN.md.
notes() { ls "$store_on"/*/leg-*.md 2>/dev/null | head -n 1; }

# The relay-ON run (hands over -> writes the baton).
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config_on.json" \
    --output json -p "read the file and tell me what it says" \
    < /dev/null > "$tmp/on.json" 2> "$tmp/on.err"); rc_on=$?
n_on=$(notes)
# The relay-OFF run (same config minus "relay" -> never hands over).
(cd "$ws" && with_deadline 90 "$BIN" --config "$tmp/config_off.json" \
    --output json -p "read the file and tell me what it says" \
    < /dev/null > "$tmp/off.json" 2> "$tmp/off.err"); rc_off=$?
n_off=$(notes)
mm_stop

stop_of() { "$JQ" '.stop_reason' "$1" 2>/dev/null; }
# Portable mode probe: ls -l's permission string (rw------- for 0600). stat
# -c is GNU-only (portability_lint's corpus is POSIX), so a driver that must
# run on a BSD reads the mode from ls.
# (ls -l's field 1 is "<type><perms>", e.g. -rw-------; strip the type char.)
mode_of() { ls -l "$1" 2>/dev/null | awk '{ print substr($1, 2) }'; }

# 1: the floor -- a handover happened AND a notes file appeared (0600, non-empty)
if [ "$(stop_of "$tmp/on.json")" = "handover" ] && [ -n "$n_on" ] && \
   [ -s "$n_on" ] && [ "$(mode_of "$n_on")" = "rw-------" ]; then
    t_ok "the relay handover wrote a baton ($n_on), 0600 and non-empty"
else
    t_fail "no baton from the handover: stop='$(stop_of "$tmp/on.json")', \
note='$n_on' (size $(wc -c < "$n_on" 2>/dev/null || echo 0), mode \
$(mode_of "$n_on")) -- the relay ended but wrote nothing a successor \
could find"
fi

# 2: THE TOOTH -- the notes hold the template's four mechanical pieces
if [ -n "$n_on" ] && grep -q '^# Relay notes' "$n_on" && \
   grep -q '^## 1\. The task' "$n_on" && \
   grep -q 'read the file and tell me what it says' "$n_on" && \
   grep -q '^## 2\. Where the work stands' "$n_on" && \
   grep -q '^## 4\. How to use these notes' "$n_on"; then
    t_ok "the baton holds the header, the task verbatim, and sections 2 and 4"
else
    t_fail "the baton is missing a template piece (task verbatim / a section): \
$(head_bytes 400 "$n_on" 2>/dev/null || echo 'no note')"
fi

# 3: the header names the trigger that fired -- the decision and the notes agree
if [ -n "$n_on" ] && grep -q '^Previous leg ended: hard_limit' "$n_on"; then
    t_ok "the baton's header names the trigger that ended the leg (hard_limit)"
else
    t_fail "the baton does not name the trigger: \
$(grep '^Previous leg ended' "$n_on" 2>/dev/null || echo 'no header line')"
fi

# 4: the operator surface names the written path
if grep -q '\[relay\] handover notes written to' "$tmp/on.err"; then
    t_ok "the log names the relay notes' path for the operator"
else
    t_fail "no [relay] notes-written line in the log: $(head_bytes 300 "$tmp/on.err")"
fi

# 5: off by default / no handover, no notes -- the standing positive control.
# The OFF run ends at its cap (not a handover), and the relay store holds
# nothing under the OFF run's OWN session id: a note is written only on a
# handover, so the off run's id must have no relay directory at all.
off_stop=$(stop_of "$tmp/off.json")
off_id=$("$JQ" '.session_id' "$tmp/off.json" 2>/dev/null)
if [ -n "$off_id" ] && [ "$off_stop" = "max_iters" ] && \
   [ ! -e "$store_on/$off_id" ]; then
    t_ok "with the relay off, the run ends at its cap ($off_stop) and wrote no baton (no relay dir under its session id)"
else
    t_fail "the relay-off run left a baton behind: off_stop='$off_stop', \
session='$off_id', relay dir '$store_on/$off_id' exists"
fi

# 6: the mechanical part leaves headroom for the account. Default cap 8192;
# the mechanical note must stay under half of it (4096), so C can cut the
# account to (8192 - mechanical) with real room, not zero.
headroom=4096
sz=$(wc -c < "$n_on" 2>/dev/null || echo 0)
sz=$((sz + 0))
if [ -n "$n_on" ] && [ "$sz" -le "$headroom" ]; then
    t_ok "the mechanical note is under the headroom bound ($sz <= $headroom), leaving room for the account"
else
    t_fail "the mechanical note is too large for C's cap formula: $sz bytes > $headroom -- the account would get no room under an 8192 cap"
fi

t_done
