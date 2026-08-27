#!/bin/sh
# Compile the base-only suite with a C++ compiler + AddressSanitizer and run it.
# Exit 0 iff every assert holds. Fix the FUNCTION (clamp.hpp), not the tests.
cd "$(dirname "$0")" || exit 1
trap 'rm -f ctest' EXIT
CXX=${CXX:-g++}
command -v "$CXX" >/dev/null 2>&1 || { CXX=clang++; }
command -v "$CXX" >/dev/null 2>&1 || { echo "FAIL: no C++ compiler (g++/clang++) on PATH"; exit 1; }
FLAGS="-std=c++17 -Wall -Wextra -fsanitize=address -fno-sanitize-recover=all"
$CXX $FLAGS -o ctest test_clamp.cpp 2>/dev/null || { echo "FAIL: does not compile (need g++/clang++ with ASan)"; exit 1; }
./ctest >/dev/null 2>&1 || { echo "FAIL: an assertion in test_clamp.cpp did not hold"; exit 1; }
echo "PASS: clamp is correct"
