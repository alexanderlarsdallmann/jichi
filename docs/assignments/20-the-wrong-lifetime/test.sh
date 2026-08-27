#!/bin/sh
# Two-sided by construction: the pristine keeper puts request-scoped data on
# the process-lived arena, so batch 2 costs as much again as batch 1 and the
# flatness check fails. Any correct-lifetime fix (a request arena that is
# reset, plain malloc/free, a stack buffer) passes -- the check reads the
# program's own gauge, not the strategy.
cd "$(dirname "$0")" || exit 1
cc --version >/dev/null 2>&1 || { echo "FAIL: a C compiler (cc) is is not usable -- install one (build-essential / gcc) (or a version-manager shim with no version selected)"; exit 1; }

cc -std=c89 -pedantic -Wall -Wextra -Werror -o notekeeper \
    arena.c notekeeper.c || exit 1

out=$(./notekeeper) || exit 1

echo "$out" | grep -q '^requests=1000 words=10000$' || {
    echo "FAIL: wrong totals (behavior must not change):"; echo "$out"; exit 1; }

b1=$(echo "$out" | sed -n 's/^arena_after_batch1=//p')
b2=$(echo "$out" | sed -n 's/^arena_after_batch2=//p')
[ -n "$b1" ] && [ -n "$b2" ] || { echo "FAIL: gauge lines missing"; exit 1; }

if [ "$b2" -gt "$b1" ]; then
    echo "FAIL: the footprint grew across identical batches" \
         "(batch1=$b1, batch2=$b2) -- request-scoped data is living on a" \
         "process-lived arena"
    exit 1
fi
echo "PASS: totals correct, footprint flat (batch1=$b1, batch2=$b2)"
exit 0
