#!/bin/sh
# smoke lint: the DRIVEN task has exactly one definition (M676).
#
# WHAT `Driven` IS. PLATFORMS.md carries a fourth verdict beyond Verified: a row
# where jichi has actually called a model and run a tool there, rather than
# passing gates that never open a socket. It is the verdict the whole project
# exists to earn, and it is only comparable ACROSS rows if every row ran the
# same task. Two rows driven with different prompts are two anecdotes.
#
# THE DEFECT THIS PREVENTS, stated as the history rather than as a worry. By the
# time the third rig had a live step, the code had been copied twice --
# `_rig_ship.sh` already records the rule ("a second rig is where drift starts,
# not the fourth") from the milestone where four rigs computed one multiplier
# four ways and two were wrong. Measured before extracting: the three copies
# still agreed on the task and differed only in filenames, two note lines and
# one reworded message. They had not drifted YET, which is the only moment at
# which extracting is cheap.
#
# So: a rig may not carry the task's literal text. It sources _rig_live.sh and
# calls jc_rig_live. The next platform row is then a copy of four lines, which
# is what DEFERRED.md item 6 means by "a copy, not a design".
# Compiles nothing and runs no jichi (hence *_lint.sh).
. "$(dirname "$0")/_smoke.sh"

t_plan 6
tmp=$(smoke_tmp)
SC="$SMOKE_ROOT/scripts"
LIVE="$SC/_rig_live.sh"

# --- 1: the one definition exists and carries both turns ---------------------
# A helper with only the wire turn would pass every other check here while
# proving nothing a plain `curl` could not.
if [ -f "$LIVE" ] \
   && grep -q 'reply with OK' "$LIVE" \
   && grep -q 'read_file tool' "$LIVE" \
   && grep -q 'jc_rig_live()' "$LIVE"; then
    t_ok "_rig_live.sh defines jc_rig_live with both turns (the wire, then the loop)"
else
    t_fail "scripts/_rig_live.sh is missing, or does not define jc_rig_live with
both turns. The first turn proves the provider, the request and the SSE framing;
the SECOND is the one the Driven verdict is named for, because every documented
failure in this area lives past the first."
fi

# --- 2: the assertion is a per-run random phrase -----------------------------
# A fixed phrase is a phrase a cached answer could carry, and then the row
# proves something about last week. A quoted sentence drifts -- measured, a
# model quoted three passages and dropped an article from one.
if grep -q 'urandom' "$LIVE" && grep -q 'note.txt' "$LIVE"; then
    t_ok "the agentic assertion is a per-run random phrase read from a file"
else
    t_fail "_rig_live.sh no longer generates a per-run phrase, or no longer puts
it in a file for the tool to read. Both halves are the evidence: a token
generated this second, in a place only a tool call reaches."
fi

# --- 3: no rig carries its own copy of the task ------------------------------
# THE UNIVERSE IS EVERY RIG, and getting that wrong is the defect this check
# itself shipped with (M676, found at M677 one day later). The first version
# skipped any rig that did not already call jc_rig_live -- so its universe was
# *the rigs that already comply*, and the one rig carrying a fourth copy of the
# task, `tier-b-device.sh`, was invisible to it. It reported "all 3 driven rigs
# ... none re-states the task" while a fourth copy sat in the same directory.
#
# A check whose universe is the set of things that already pass cannot fail.
# The universe is now every tier-*/jhub-* script, and the rule is the property
# itself: do not carry the task's literal text. A rig that has not adopted the
# helper is exactly the rig this must name.
: > "$tmp/copies"
n=0
for f in "$SC"/tier-*.sh "$SC"/jhub-*.sh; do
    [ -f "$f" ] || continue
    n=$((n+1))
    if grep -q 'reply with OK' "$f" || grep -q 'read_file tool to read' "$f"; then
        basename "$f" >> "$tmp/copies"
    fi
done
if [ "$n" -ge 10 ] && [ ! -s "$tmp/copies" ]; then
    t_ok "none of the $n rigs re-states the driven task"
else
    t_fail "rig(s) carrying their own copy of the driven task: $(tr '\n' ' ' < "$tmp/copies")
(scanned $n rigs; under 10 means the glob broke)
Source scripts/_rig_live.sh and use its task functions instead. Two rows driven
with different prompts are two anecdotes, not a matrix -- and a rig that has not
adopted it is precisely the one this check exists to name, so its universe is
every rig rather than every rig that already complies."
fi

# --- 4: a rig that cannot drive says so, rather than staying silent ----------
# This is the half that is easy to drop and impossible to notice: without a
# --live-port the step must announce that it did NOT run. A rig that is silent
# here is how the matrix came to look emptier than the work actually done --
# and, worse, how a row could be read as driven because nothing said otherwise.
: > "$tmp/silent"
nd=0
for f in "$SC"/tier-*.sh "$SC"/jhub-*.sh; do
    [ -f "$f" ] || continue
    # A rig that can drive is one that uses ANY of the task functions -- not
    # just the VM-shaped jc_rig_live. tier-b-device.sh drives physical boards
    # with its own transport and would have been skipped by the narrower test,
    # which is the same universe mistake check 3 shipped with.
    grep -q 'jc_rig_live' "$f" || continue
    nd=$((nd+1))
    grep -qE 'jc_rig_live_skip|skip "live turn not attempted' "$f" \
        || basename "$f" >> "$tmp/silent"
done
if [ "$nd" -ge 4 ] && [ ! -s "$tmp/silent" ]; then
    t_ok "all $nd rigs that can drive announce the case where the step did not run"
else
    t_fail "rig(s) that can drive but never say when they did NOT ($nd scanned):
$(tr '\n' ' ' < "$tmp/silent")
Without --live-port the step must SAY it was not attempted. Verified and Driven
are different verdicts, and silence reads as the better one."
fi

# --- 5: a reverse forward that cannot bind must FAIL the command -------------
# THE DEFECT, measured on the Pi 400 at M677 and caught only by reading the log
# of a row that had already reported two green live turns. `ssh -R` whose
# forward cannot bind prints
#
#     Warning: remote port forwarding failed for listen port 1234
#
# and then RUNS THE COMMAND ANYWAY, exit 0. On that board a stale forward from
# an ssh session more than a day old was still holding the port, so the turn
# reached the model through something nobody knew was there, the rig's own
# transport was never exercised, and the row said Driven. The model calls were
# real -- which is exactly why this is dangerous rather than merely wrong: the
# capability result was true and the evidence for it was not.
#
# `ExitOnForwardFailure=yes` turns that warning into a non-zero exit, which the
# rigs already read as a failed turn. Every -R in the tier must carry it.
: > "$tmp/nofail"
nr=0
for f in "$SC"/tier-*.sh "$SC"/jhub-*.sh "$SC"/_rig_*.sh; do
    [ -f "$f" ] || continue
    grep -q -- '-R "' "$f" || continue
    nr=$((nr+1))
    grep -q 'ExitOnForwardFailure=yes' "$f" || basename "$f" >> "$tmp/nofail"
done
if [ "$nr" -ge 4 ] && [ ! -s "$tmp/nofail" ]; then
    t_ok "all $nr rigs with a reverse forward fail when it cannot bind"
else
    t_fail "rig(s) whose -R can silently fail open ($nr scanned): $(tr '\n' ' ' < "$tmp/nofail")
Add -o ExitOnForwardFailure=yes. Without it ssh WARNS and runs the command
anyway, so the turn reaches whatever else happens to hold that port -- and a
green row then proves nothing about the transport it claims to test."
fi

# --- 6: a rig that advertises --live-port can actually PARSE it -------------
# THE DEFECT, and it shipped for a day (M677 -> M678). `tier-v-vm.sh` parses its
# arguments with `for arg in "$@"`, where `$2` is the SCRIPT's second argument
# and `shift` does not skip an iteration. The live flags were wired in with the
# `"$2"; shift` idiom the OTHER rigs use, so `--live-port 1234` set the port to
# whatever happened to be argument two and then died on `1234` as an unknown
# option. Nothing caught it because no check had ever passed the flag, and the
# rig's own --host-secs had the right shape all along, two lines below.
#
# So: ask each rig, in DRY-RUN, to accept the flag. This runs no VM and boots
# nothing -- every rig here short-circuits on --dry-run -- and it is the whole
# distance between "the flag exists in a case arm" and "the flag works".
: > "$tmp/unparsed"
np=0
for f in "$SC"/tier-*.sh "$SC"/jhub-*.sh; do
    [ -f "$f" ] || continue
    grep -q 'live-port' "$f" || continue
    np=$((np+1))
    # Each rig needs its own required arguments before --dry-run will get far;
    # supply the minimum and look only for the usage exit (2) with a complaint
    # naming the flag or its value.
    case $(basename "$f") in
        tier-v-vm.sh)      _args="v2e --dry-run" ;;
        tier-b-device.sh)  _args="user@host --ref-secs 7 --dry-run" ;;
        *)                 _args="--ref-secs 7 --dry-run" ;;
    esac
    # shellcheck disable=SC2086
    _out=$(sh "$f" $_args --live-port 1234 --live-model test/model 2>&1); _rc=$?
    if [ "$_rc" -eq 2 ] && printf '%s' "$_out" | grep -qE '1234|test/model|live-port|live-model'; then
        printf '%s: %s\n' "$(basename "$f")" \
            "$(printf '%s' "$_out" | grep -iE 'unknown|usage|needs' | head -1)" \
            >> "$tmp/unparsed"
    fi
done
if [ "$np" -ge 4 ] && [ ! -s "$tmp/unparsed" ]; then
    t_ok "all $np rigs advertising --live-port parse it (checked in --dry-run)"
else
    t_fail "rig(s) that advertise --live-port and reject it ($np scanned):
$(cat "$tmp/unparsed")
A case arm is not a parser. tier-v-vm.sh uses \`for arg in \"\$@\"\`, where
\$2 is the script's second argument and shift does not skip an iteration --
the \"\$2\"; shift idiom the other rigs use is wrong there. (Under 4 rigs
means the search broke.)"
fi

t_done
