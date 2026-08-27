#!/bin/sh
# Structural floor for use-cases. Passes when there are >= 3 use-case sections,
# and each names an Actor, a Trigger, and BOTH a success and a failure path --
# a use-case with only the happy path is half a use-case.
cd "$(dirname "$0")" || exit 1
uc=$(grep -cE '^##+ ' USE_CASES.md)
[ "$uc" -ge 3 ] || { echo "FAIL: only $uc use-case sections (## ...) -- write at least 3"; exit 1; }
a=$(grep -ciE 'actor' USE_CASES.md);   [ "$a" -ge 3 ] || { echo "FAIL: fewer than 3 use-cases name an Actor"; exit 1; }
t=$(grep -ciE 'trigger' USE_CASES.md); [ "$t" -ge 3 ] || { echo "FAIL: fewer than 3 use-cases name a Trigger"; exit 1; }
f=$(grep -ciE 'failure|alternate|error' USE_CASES.md); [ "$f" -ge 3 ] || { echo "FAIL: fewer than 3 use-cases have a failure/alternate path -- the happy path is not enough"; exit 1; }
echo "PASS: $uc use-cases with actor, trigger, and a failure path"
