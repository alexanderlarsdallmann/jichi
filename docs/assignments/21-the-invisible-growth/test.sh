#!/bin/sh
# Two-sided by construction: the pristine spooler's one warm buffer grows to
# hold the ~512 KB outlier and buf_clear keeps that capacity for the life of
# the process, so the idle gauge reads >512 KB and the bound fails. Any fix
# that releases outsized capacity (shrink-on-clear, free/init per message)
# passes; the checksum pins the OUTPUT, so a fix may change memory behavior
# but never what the spooler produced.
cd "$(dirname "$0")" || exit 1
cc --version >/dev/null 2>&1 || { echo "FAIL: a C compiler (cc) is is not usable -- install one (build-essential / gcc) (or a version-manager shim with no version selected)"; exit 1; }

cc -std=c89 -pedantic -Wall -Wextra -Werror -o spooler buf.c spooler.c \
    || exit 1

out=$(./spooler) || exit 1

echo "$out" | grep -q "^checksum=3618681471\$" || {
    echo "FAIL: output changed (the fix must not alter what is spooled):"
    echo "$out"; exit 1; }

idle=$(echo "$out" | sed -n 's/^idle_live_bytes=//p')
[ -n "$idle" ] || { echo "FAIL: gauge line missing"; exit 1; }
if [ "$idle" -gt 8192 ]; then
    echo "FAIL: $idle bytes still live while idle -- one outlier message" \
         "pinned its capacity for the process lifetime"
    exit 1
fi
echo "PASS: checksum stable, idle footprint $idle bytes (<= 8192)"
exit 0
