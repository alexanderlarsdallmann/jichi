#!/bin/sh
# preflight.sh -- refuse to work in a tree that is already busy.
#
# Step 0 of docs/SESSION_RUNBOOK.md. It exists because prose discipline failed:
# in one session a `make ci` run was three times contaminated by concurrent
# builds in the same tree, and one of those made a two-sided proof report
# PASS-with-the-fix-removed -- a false negative that would have shipped.
#
# A build, a test, or an edit in a tree whose gate is running is worthless in
# BOTH directions: your result is corrupted by its `make clean`, and its result
# is corrupted by your edits.
#
# Every check here uses `pgrep -x` (exact process name) rather than `pgrep -f`
# (full command line). That is not a style preference. A `-f` pattern matches
# the shell running the check, which in this session killed two shells outright
# and made two monitors report a finished job as still running.
#
# Usage:
#   scripts/preflight.sh            # check the tree this script lives in
#   scripts/preflight.sh --wait     # wait for it to become quiet, then exit 0
#   scripts/preflight.sh --quiet    # exit status only
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
WAIT=0; QUIET=0
for a in "$@"; do
    case "$a" in
        --wait)  WAIT=1 ;;
        --quiet) QUIET=1 ;;
        --help|-h) sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "preflight: unknown option '$a'" >&2; exit 2 ;;
    esac
done

say() { [ "$QUIET" -eq 1 ] || echo "$@"; }

busy_pids() {
    # Exact names only. `make` covers make ci / make test / make smoke; the
    # compilers cover a build already in flight under any driver.
    #
    # KNOWN LIMIT, measured: `pgrep -x` compares against comm, which Linux
    # truncates to 15 characters, so a longer process name matches NOTHING --
    # `pgrep -x qemu-system-x86_64` is silently always empty. Every name below
    # is within the limit. For anything longer, check a recorded pidfile with
    # `kill -0`; do not reach for `pgrep -f`, which matches this script.
    pgrep -x make 2>/dev/null
    pgrep -x cc1 2>/dev/null
    pgrep -x cc1plus 2>/dev/null
    pgrep -x valgrind 2>/dev/null
}

report() {
    for p in $(busy_pids); do
        [ -r "/proc/$p/cmdline" ] || continue
        say "  pid $p: $(tr '\0' ' ' < "/proc/$p/cmdline" | cut -c1-60)"
    done
}

while : ; do
    n=$(busy_pids | wc -l)
    if [ "$n" -eq 0 ]; then
        say "preflight: tree is quiet -- $ROOT"
        exit 0
    fi
    say "preflight: BUSY -- $n build/test process(es) running:"
    report
    if [ "$WAIT" -eq 0 ]; then
        say ""
        say "Do not build, test, or edit here until they finish."
        say "Stop them and CONFIRM they stopped, or re-run with --wait."
        exit 1
    fi
    sleep 10
done
