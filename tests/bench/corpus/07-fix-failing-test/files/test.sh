#!/bin/sh
# Build and run the unit checks. Exits non-zero on any failure.
cc -std=c89 -pedantic -Wall -o /tmp/jichi_bench_math math.c test_math.c || exit 2
exec /tmp/jichi_bench_math
