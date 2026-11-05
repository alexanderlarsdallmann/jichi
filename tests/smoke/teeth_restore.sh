#!/bin/sh
# smoke: tests/teeth.sh restores the file it perturbed, however it is stopped
# (M770).
#
# teeth.sh exists partly to refuse one hazard by construction: an interrupted
# ritual that leaves its perturbation in the tree. Its restore was trap'd on EXIT,
# INT and TERM. At M770 a run piped into `head` died of SIGPIPE and left a smoke
# driver perturbed, noticed only because the next run of that driver went red.
# SIGHUP, a closed terminal or ssh session, is the same case. And a trapped TERM
# (or INT) ran restore and then continued, so the EXIT trap ran it a second time
# on the removed backup and printed RESTORE FAILED about a restored file.
#
# Each check runs teeth.sh on a scratch file with a command that passes on the
# original, so the baseline is green, and on the perturbed file stops teeth.sh
# the way the check names. $PPID in that command is teeth.sh, which runs it
# directly. Afterwards the file must be the original and no backup may be left.
#
# THE UNIVERSE: the signals that stop a script from outside -- PIPE, HUP, TERM --
# and the ordinary exit. INT is left out on purpose: run.sh starts every driver
# asynchronously, and an asynchronous command in a shell without job control
# ignores SIGINT, which a non-interactive shell cannot then trap. A check for it
# would pass on the old script too. Its trap is the same line as TERM's.
#
# Perturbed at M770: with teeth.sh's trap line put back to `trap restore EXIT
# INT TERM`, checks 1-3 go red and 4 stays green.
. "$(dirname "$0")/_smoke.sh"

t_plan 4
tmp=$(smoke_tmp)
TEETH="$SMOKE_ROOT/tests/teeth.sh"

# fresh N -- a new target file and backup directory for check N, so that no
# check can go red on what an earlier one left behind (the first cut shared one
# directory, and on the old script check 4 failed on checks 1-2's leftovers).
fresh() {
    f="$tmp/target.$1"
    bakdir="$tmp/bak.$1"
    mkdir -p "$bakdir"
    echo original > "$f"
}

# after LABEL OUTFILE -- the file is the original, no backup is left, and
# teeth.sh did not report a restore failure.
after() {
    _now=$(cat "$f")
    _left=$(ls "$bakdir")
    if [ "$_now" = original ] && [ -z "$_left" ] && ! grep -q 'RESTORE FAILED' "$2"; then
        t_ok "$1"
    else
        t_fail "$1 -- file: $_now; backups left: $(printf '%s' "$_left" | tr '\n' ' '); \
$(grep 'RESTORE FAILED' "$2" | head_bytes 200)"
    fi
}

# 1. SIGPIPE: the reader goes away while teeth.sh still has output to write. The
#    command writes more than a pipe holds, so it is killed first, and teeth.sh's
#    own "caught" line then meets the closed pipe.
fresh 1
TMPDIR="$bakdir" sh "$TEETH" "$f" 's/original/perturbed/' sh -c \
    'grep -q original "$1" && exit 0; i=0
     while [ $i -lt 20000 ]; do echo line; i=$((i + 1)); done; exit 1' sh "$f" \
    2> "$tmp/pipe.err" | head -n 1 > /dev/null
after "killed by SIGPIPE (piped into head), teeth.sh restores the file" "$tmp/pipe.err"

# 2. SIGHUP: the command hangs up teeth.sh while it waits.
fresh 2
TMPDIR="$bakdir" sh "$TEETH" "$f" 's/original/perturbed/' sh -c \
    'grep -q original "$1" && exit 0; kill -HUP $PPID; sleep 1; exit 1' sh "$f" \
    > "$tmp/hup.out" 2>&1
after "hung up (SIGHUP), teeth.sh restores the file" "$tmp/hup.out"

# 3. SIGTERM: restored once, and no RESTORE FAILED from a second restore.
fresh 3
TMPDIR="$bakdir" sh "$TEETH" "$f" 's/original/perturbed/' sh -c \
    'grep -q original "$1" && exit 0; kill -TERM $PPID; sleep 1; exit 1' sh "$f" \
    > "$tmp/term.out" 2>&1
after "terminated (SIGTERM), teeth.sh restores the file once" "$tmp/term.out"

# 4. The ordinary path is unchanged: caught, exit 0, restored.
fresh 4
TMPDIR="$bakdir" sh "$TEETH" "$f" 's/original/perturbed/' sh -c \
    'grep -q original "$1"' sh "$f" > "$tmp/ok.out" 2>&1
st=$?
if [ "$st" -eq 0 ] && grep -q 'teeth: OK' "$tmp/ok.out"; then
    after "an ordinary run still reports the catch and restores the file" "$tmp/ok.out"
else
    t_fail "an ordinary run exited $st: $(head_bytes 300 "$tmp/ok.out" | tr '\n' ' ')"
fi

t_done
