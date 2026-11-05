#!/bin/sh
# tests/teeth.sh - prove a check can fail (M381; TEST_INTEGRITY "prove the teeth").
#
# The ritual this scripts was, until now, performed by hand: perturb the thing
# a check claims to guard, watch the check go red, restore. A check never seen
# failing has never been seen working -- and the manual form has two hazards
# this script refuses by construction: a perturbation that matches NOTHING
# "proves" the teeth of a no-op (VACUOUS), and an interrupted ritual leaves
# the perturbation in the tree (the restore here is trap'd).
#
# Usage:
#   tests/teeth.sh <file> <sed-expression> <command> [args...]
#
#   tests/teeth.sh docs/REFERENCES.md 's/@rss:/@rss_X_:/g' \
#       sh tests/smoke/refs_lint.sh
#
# Exit 0  -- teeth OK: the command was GREEN before, the perturbation changed
#            the file, the command failed under it, and the file was restored.
# Exit 1  -- TOOTHLESS: the command stayed green under a real perturbation.
# Exit 2  -- usage error, or VACUOUS (the sed expression changed nothing, or the
#            command was already red before the perturbation).
#
# The baseline run (M421) closes the mirror of the hazard above. A command that
# cannot pass -- a typo, a missing interpreter, a quoted argv the shell never
# splits ("sh tests/x.sh" as ONE argument execs a program of that name) -- fails
# under the perturbation for reasons having nothing to do with it, and the script
# used to call that OK. This one bit me: it reported teeth on two checks whose
# lint had never run at all. A check never seen PASSING proves as little as one
# never seen failing, so the red must be shown to be *caused*.
set -u

if [ $# -lt 3 ]; then
    echo "usage: $0 <file> <sed-expression> <command> [args...]" >&2
    exit 2
fi
file=$1
expr=$2
shift 2

if [ ! -f "$file" ]; then
    echo "teeth: no such file: $file" >&2
    exit 2
fi

bak=$(mktemp "${TMPDIR:-/tmp}/teeth.XXXXXX") || exit 2
cp "$file" "$bak" || { rm -f "$bak"; exit 2; }
# The restore runs ONCE, from the EXIT trap or from a signal's, which then exits
# with that signal's conventional status (M770). It was trap'd on EXIT, INT and
# TERM only, so SIGPIPE -- this script piped into `head` -- and SIGHUP -- a closed
# terminal or ssh session -- killed it with the perturbation still in the tree:
# the hazard the header says this script refuses. Found by doing it, to a smoke
# driver, which the next run of that driver reported. And a trapped INT or TERM
# ran restore and then CONTINUED, so the EXIT trap ran it again on the removed
# backup and printed RESTORE FAILED about a file that had been restored.
# tests/smoke/teeth_restore.sh stops this script each of those ways.
restore() {
    [ -n "$bak" ] || return 0
    if ! cp "$bak" "$file"; then
        echo "teeth: RESTORE FAILED -- $file is still perturbed; original in $bak" >&2
        bak=
        trap - EXIT
        exit 2
    fi
    rm -f "$bak"
    bak=
}
trap restore EXIT
trap 'restore; exit 129' HUP
trap 'restore; exit 130' INT
trap 'restore; exit 141' PIPE
trap 'restore; exit 143' TERM

# --- baseline: the command must be GREEN on the unperturbed file --------------
# Run it before touching anything. Without this, an unrunnable command "proves"
# the teeth of every check it never executed.
if ! "$@" > /dev/null 2>&1; then
    echo "teeth: VACUOUS -- the command is already failing BEFORE the" >&2
    echo "  perturbation, so its red proves nothing about $file." >&2
    echo "  Fix the command first (note argv is exec'd, not shell-parsed:" >&2
    echo "  pass 'sh tests/smoke/x.sh' as two arguments, not one)." >&2
    exit 2
fi

if ! sed "$expr" "$bak" > "$file"; then
    echo "teeth: sed expression failed" >&2
    exit 2
fi
if cmp -s "$bak" "$file"; then
    echo "teeth: VACUOUS -- '$expr' changed nothing in $file (a no-op proves nothing)" >&2
    exit 2
fi

if "$@"; then
    echo "teeth: TOOTHLESS -- the command stayed green under the perturbation of $file" >&2
    exit 1
fi
echo "teeth: OK -- the perturbation of $file was caught (the command failed as demanded)"
exit 0
