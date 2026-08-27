#!/bin/sh
# CI gate for the rot13 library: build, run the tests, show the result.
cd "$(dirname "$0")" || exit 1
command -v cc >/dev/null 2>&1 || { echo "FAIL: a C compiler (cc) is not on PATH -- install one (build-essential / gcc)"; exit 1; }
cc -std=c89 -o t rot13.c test_rot13.c 2> build.log
./t > result.txt 2>&1
cat result.txt
echo "gate: done"
exit 0
