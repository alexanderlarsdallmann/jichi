#!/bin/sh
# Two-sided by construction: the pristine file uses five constructs C89 does
# not have (// comments, designated initializers, for-loop declarations,
# long long, snprintf), so the strict-C89 compile fails. The port must keep
# the OUTPUT byte-identical and write PORT.md naming each construct and its
# replacement -- the port without the map is half the assignment.
cd "$(dirname "$0")" || exit 1
cc --version >/dev/null 2>&1 || { echo "FAIL: a C compiler (cc) is is not usable -- install one (build-essential / gcc) (or a version-manager shim with no version selected)"; exit 1; }

cc -std=c89 -pedantic -Wall -Wextra -Werror -o inventory inventory.c || {
    echo "FAIL: does not compile as strict C89"; exit 1; }

out=$(./inventory) || exit 1
expected="stock report
bolt     x1200  @   3 =     3600
washer   x4000  @   1 =     4000
plate    x15    @ 950 =    14250
total value: 21850 cents"
[ "$out" = "$expected" ] || {
    echo "FAIL: output changed -- the port must not change behavior"
    echo "$out"; exit 1; }

[ -f PORT.md ] || { echo "FAIL: PORT.md missing (name what you moved)"; exit 1; }
for construct in "designated" "long long" "snprintf" "declaration" "//"; do
    grep -qi -- "$construct" PORT.md || {
        echo "FAIL: PORT.md does not account for: $construct"; exit 1; }
done
grep -qiE "range|overflow|2147483647|INT_MAX|LONG_MAX" PORT.md || {
    echo "FAIL: PORT.md must name the COST of losing long long" \
         "(what range now bounds the total?)"; exit 1; }

echo "PASS: strict C89, output identical, the port is accounted for"
exit 0
