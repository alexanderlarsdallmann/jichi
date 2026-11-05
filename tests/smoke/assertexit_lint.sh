#!/bin/sh
# smoke lint: every ptydrive `waitexit` is followed by an `assertexit`.
#
# THE DEFECT THIS EXISTS FOR (M758, M776). `waitexit` records the child's exit
# status; only `assertexit` reads it. Since M758 ptydrive fails a death by a
# signal that no assertexit expected (exit 7), so a crash can no longer pass a
# waitexit-without-assertexit driver -- but a wrong exit CODE still can: a
# session that should end 0 and ends 1 (a `/exit` that returned non-zero, a
# refused startup) slips through unread. 44 of 63 waitexit sites across 26
# drivers had no assertexit; M776 added one to each, and this lint keeps it so.
#
# UNIVERSE: every tests/smoke/*.sh with a heredoc `waitexit` command -- a line
# whose first non-space token is `waitexit`. A `waitexit` inside a printf string
# (no_model_exit.sh builds a sub-script that way) does NOT start the line, so it
# is out of scope, exactly as it is for a human reading the heredoc. A `waitexit`
# must be followed, as its next non-blank line, by an `assertexit`.
#
# THE ONE EXEMPTION: no_model_exit.sh. That driver is the exit-7 tooth's own
# test -- it feeds ptydrive a sub-script with a waitexit and NO assertexit and a
# child that SIGSEGVs, and asserts ptydrive catches the unasserted signal death
# (exit 7). An assertexit there would defeat the very thing it proves, turning
# exit 7 into an exit-6 code mismatch. So its unasserted waitexit is allowed, and
# named here rather than left as a silent gap. A forgotten assertexit in ANY
# other driver still fails.
#
# Perturbed per check: check 1 fires when an assertexit is deleted from any
# driver (the site is then unread); check 2 fires when the extraction stops
# finding the waitexit sites at all (a floor of today's count, so a rename of the
# command cannot empty the universe silently).
. "$(dirname "$0")/_smoke.sh"
t_plan 2
root=$SMOKE_ROOT

# Today's exact count of heredoc waitexit sites (M776). A floor, so the universe
# cannot shrink to nothing unnoticed.
FLOOR=63

# Print "file:line" for every heredoc waitexit whose next non-blank line is not
# an assertexit, and "TOTAL <n>" on stderr for the floor. A waitexit that starts
# a line (after optional space) is a command; one inside a printf string does
# not, and is out of scope. Cross-file: a dangling pend from one file is reported
# at the next file's first line, so a waitexit at a file's end is not lost.
tmp_total=$(smoke_tmp)/total
scan() {
    awk '
        FNR == 1 && pend { print pf ":" pl; pend = 0 }
        /^[ \t]*waitexit([ \t]|$)/ {
            total++
            if (pend) print pf ":" pl
            pend = 1; pf = FILENAME; pl = FNR; next
        }
        pend && /^[ \t]*assertexit([ \t]|$)/ { pend = 0; next }
        pend && NF { print FILENAME ":" FNR " (waitexit at " pl ")"; pend = 0 }
        END { if (pend) print pf ":" pl; print "TOTAL " total > "/dev/stderr" }
    ' "$@"
}
miss=$(scan "$root"/tests/smoke/*.sh 2>"$tmp_total")
total=$(sed -n 's/^TOTAL //p' "$tmp_total")
# Drop the one exemption (the exit-7 tooth in no_model_exit.sh); anything left is
# a real forgotten assertexit.
stray=$(printf '%s\n' "$miss" | grep -v '/no_model_exit\.sh:' | grep .)

# --- 1: every waitexit is read by an assertexit (bar the exit-7 tooth) ------------
if [ -z "$stray" ]; then
    t_ok "every heredoc waitexit is followed by an assertexit ($total sites, minus no_model_exit's exit-7 tooth)"
else
    t_fail "a waitexit has no assertexit -- add one with the code that session \
should end with (0 for a clean /exit): $(printf '%s' "$stray" | tr '\n' ' ')"
fi

# --- 2: the extraction still finds the sites (floor) -----------------------------
if [ "${total:-0}" -ge "$FLOOR" ]; then
    t_ok "the scan found $total waitexit sites (floor $FLOOR)"
else
    t_fail "the scan found only ${total:-0} waitexit sites, below the floor of \
$FLOOR -- the command was renamed or the extraction broke; fix the scan, not the floor"
fi

t_done
