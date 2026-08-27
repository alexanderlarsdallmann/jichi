#!/bin/sh
# Compile and run the stats tests. Exits 0 iff every test passes.
cd "$(dirname "$0")" || exit 1
cc --version >/dev/null 2>&1 || { echo "FAIL: a C compiler (cc) is is not usable -- install one (build-essential / gcc) (or a version-manager shim with no version selected)"; exit 1; }
cc -std=c89 -pedantic -Wall -Wextra -o test_stats stats.c test_stats.c || exit 1
./test_stats
