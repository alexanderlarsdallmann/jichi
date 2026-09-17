#!/bin/sh
# Re-probe every URL in docs/BIBLIOGRAPHY.md and report what answers today.
#
# The bibliography's entries carry a check marker and a DATE (see its "How every
# entry was checked" table). A date is a claim with a shelf life.
#
# READ THE OUTPUT AS A MOMENT, NOT A VERDICT. On 2026-09-16 c-faq.com refused the
# connection here and was written up as dead; hours later it answered 200 three
# times in a row. A non-2xx row means "did not answer this time" -- re-run before
# you change a page because of it. That is why this script reports and does not
# gate.
#
# NETWORK, and therefore NOT in any gate. `make smoke` runs the OFFLINE half
# (tests/smoke/bibliography_lint.sh: shape, counts, markers); this is the half a
# human runs deliberately, on a machine with a route out. Exit status is 0 when
# every URL answers 2xx, 1 otherwise, so it can be wired into something that
# reports rather than blocks.
#
#   sh scripts/check-bibliography.sh            # probe all, print a table
#   sh scripts/check-bibliography.sh --quiet    # only the failures
#
# A 403 is reported, not excused: two entries (ACM's DOI, ISO's catalogue) are
# cited BECAUSE they refuse automated requests, and the page says so. If a 403
# appears on a URL the page does not already describe that way, that is a change
# to look at.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
doc="$root/docs/BIBLIOGRAPHY.md"
quiet=0
[ "${1:-}" = "--quiet" ] && quiet=1

if [ ! -f "$doc" ]; then
    echo "check-bibliography: no $doc" >&2
    exit 2
fi
if ! command -v curl >/dev/null 2>&1; then
    echo "check-bibliography: curl not found -- nothing to probe with" >&2
    exit 2
fi

# The extraction is anchored on the page's own citation form, <https://...>,
# which is why the page uses it consistently. A bare link in prose is not a
# citation and is deliberately not probed.
#
# grep -o, NOT sed. The first spelling was
#     sed -n 's/.*<\(https:\/\/[^>]*\)>.*/\1/p'
# whose leading .* is greedy, so on any line carrying TWO citations -- the
# floating and pinned Zig references, the WG14 root and N3220 -- it captured only
# the second and silently dropped the first. It reported 30 URLs where the page
# has 33, and every one it dropped was a line this page deliberately
# double-cited. An extraction that loses exactly the interesting rows is the
# shape CLAUDE.md warns about: read the extracted SET, not its size.
urls=$(grep -o '<https://[^>]*>' "$doc" | tr -d '<>' | sort -u)
n=$(printf '%s\n' "$urls" | grep -c . )

if [ "$n" -lt 30 ]; then
    echo "check-bibliography: extracted only $n URLs from $doc -- the citation" >&2
    echo "  form changed and this script is now probing almost nothing." >&2
    exit 2
fi

[ "$quiet" -eq 1 ] || echo "check-bibliography: $n URLs, $(date +%Y-%m-%d)"
bad=0
for u in $urls; do
    code=$(curl -sSL -o /dev/null -w '%{http_code}' --max-time 25 "$u" 2>/dev/null)
    case "$code" in
        2*) [ "$quiet" -eq 1 ] || printf '  %-4s %s\n' "$code" "$u" ;;
        *)  printf '  %-4s %s\n' "${code:-ERR}" "$u"; bad=$((bad + 1)) ;;
    esac
done

if [ "$bad" -eq 0 ]; then
    [ "$quiet" -eq 1 ] || echo "check-bibliography: all $n reachable"
    exit 0
fi
echo "check-bibliography: $bad of $n did not answer 2xx." >&2
echo "  Update the entry, move it to a live mirror, or record the refusal in the" >&2
echo "  page's own terms -- do not leave a date standing over a dead link." >&2
exit 1
