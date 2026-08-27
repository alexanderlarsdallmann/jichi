#!/bin/sh
# smoke: what the approval prompt accepts, and what it refuses to treat as an
# answer (M564/M565).
#
# WHY ITS OWN DRIVER. These checks began life in accessible.sh, which had grown
# to twenty-five checks across TEN PTY sessions and 66 seconds. It passed
# standalone and was `Terminated` inside `make ci` -- an arm exceeding its
# deadline under load, with every assertion already green. A driver that only
# passes when the machine is idle is a driver that will lie eventually, and the
# fix is to split it rather than raise a timeout.
#
# WHAT IS UNDER TEST, and both came from the operator:
#
# M564 -- THE DIGITS. `1` yes, `0` no, `8` always, `3` edit, `5` view, accepted
# in every language alongside y/n/a/e/v. The letters are ENGLISH INITIALS, which
# is the root of everything M552-M559 worked around: `a wie immer` is false
# because `a` is not immer's initial, so the "as in" cue cannot be translated,
# DIN 5009 was needed to name the letter instead, and the German prompt came to
# ~108 columns against M554's 78-column budget. Digits have no language.
#
# M565 -- A STRAY KEY IS NOT AN ANSWER. It used to deny. That was safe and still
# wrong: it resolved a fence with a byte the user never meant and reported
# "denied by the user" to the model for a decision nobody made -- and a listener
# hears that announced with no way to know it came from a fumble. The operator's
# reason: "any other key can be hit accidentally. if 0 means no, 0 means no."
#
# THE FIXTURE READS BEFORE IT EDITS, because jichi requires it. The first version
# had the model edit a file it had never read, so the digit authorised the call
# and the tool answered "error: read the file before editing it" -- correct
# product behaviour that made the check fail for an unrelated reason. `read_file`
# is not itself gated (proved by an earlier perturbation: swapping edit_file for
# read_file made the approval prompt vanish entirely), so adding it leaves the
# key sequence untouched: exactly one prompt, for the edit.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
smoke_home
tmp=$(smoke_tmp)
ws=$(smoke_tmp)

cat > "$tmp/replies.mm" <<'EOF'
wire openai
rule
  count 1
  tool read_file {"path":"edit_me.txt"}
rule
  count 2
  tool edit_file {"path":"edit_me.txt","old_string":"quick","new_string":"slow"}
rule
  text done here
EOF

# A stray key, then a DIGIT yes: proves the re-ask happened AND that `1` allows.
cat > "$tmp/keys.pd" <<'EOF'
delay 1200
send "edit edit_me.txt\r"
expect "Allow?" 25
delay 500
send "q"
delay 700
send "1"
delay 1500
send "/exit\r"
waitexit 20
EOF

printf 'the quick brown fox\n' > "$ws/edit_me.txt"
mm_start "$tmp/replies.mm" "$tmp/cap1"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 70 "$SMOKE_TOOLS/ptydrive" \
    --deadline 65 --cols 100 --log "$tmp/keys.log" "$tmp/keys.pd" -- \
    "$BIN" --config "$tmp/config.json" --no-session --accessible \
    > /dev/null 2>&1)
mm_stop

# ---- 1: the prompt was actually reached (the denominator) ------------------
# Without this, checks 2-4 are clean whenever the tool never ran -- and a
# fixture whose mock died, or whose read-before-edit fence refused, produces
# exactly that silence.
if grep -q 'Allow?' "$tmp/keys.log"; then
    t_ok "the approval prompt was reached"
else
    t_fail "no approval prompt in the capture -- nothing below tests anything: \
$(tail -3 "$tmp/keys.log" 2>/dev/null | tr '\n' ' ' | head_bytes 200)"
fi

# ---- 2: the stray key was REFUSED as an answer, not treated as one ---------
if grep -q 'does nothing here' "$tmp/keys.log"; then
    t_ok "an unrecognised key re-asks instead of denying"
else
    t_fail "a stray keypress was accepted as an answer. The prompt should have \
said so and asked again: an accidental byte is not a decision, and denying on it \
tells the model the user refused something they never saw. Denials in the log: \
$(grep -c 'denied' "$tmp/keys.log" 2>/dev/null)"
fi

# ---- 3: and then the DIGIT allowed it -------------------------------------
# The edit must actually have HAPPENED. Asserting the echo alone would pass for
# a build that printed "allowed" and did nothing -- which is precisely what the
# read-before-edit fence produced while this fixture was wrong.
if grep -q 'the slow brown fox' "$ws/edit_me.txt" 2>/dev/null; then
    t_ok "1 is accepted as yes (the edit applied)"
else
    t_fail "the digit 1 did not authorise the edit. File now: \
$(head -1 "$ws/edit_me.txt" 2>/dev/null). M564 accepts 1/0/8/3/5 alongside \
y/n/a/e/v in every language; if this is red either the digit branch is missing \
or the tool refused for another reason -- check the log for 'error:'."
fi

# ---- 4: but the re-asking is BOUNDED -------------------------------------
# An unbounded re-ask is its own hazard: a pipe of garbage, or a wedged terminal
# sending a stream, would loop forever. UNRECOGNISED_MAX strays must end in a
# denial rather than another question.
cat > "$tmp/bound.pd" <<'EOF'
delay 1200
send "edit edit_me.txt\r"
expect "Allow?" 25
delay 400
send "q"
delay 400
send "q"
delay 400
send "q"
delay 1500
send "/exit\r"
waitexit 20
EOF
printf 'the quick brown fox\n' > "$ws/edit_me.txt"
mm_start "$tmp/replies.mm" "$tmp/cap2"
write_config "$tmp/config.json" "$MM_PORT"
(cd "$ws" && with_deadline 70 "$SMOKE_TOOLS/ptydrive" \
    --deadline 65 --cols 100 --log "$tmp/bound.log" "$tmp/bound.pd" -- \
    "$BIN" --config "$tmp/config.json" --no-session --accessible \
    > /dev/null 2>&1)
mm_stop
if grep -q 'denied' "$tmp/bound.log" &&
   ! grep -q 'the slow brown fox' "$ws/edit_me.txt"; then
    t_ok "repeated stray keys end in a denial, not an endless question"
else
    t_fail "the re-ask is unbounded, or a stray key authorised the edit. File: \
$(head -1 "$ws/edit_me.txt" 2>/dev/null); notices seen: \
$(grep -c 'does nothing here' "$tmp/bound.log" 2>/dev/null)"
fi

t_done
