#!/bin/sh
# The capstone: implement rpn.hpp to pass the provided suite (test_rpn.cpp, the
# spec -- do not edit it) under ASan, and leave a one-line DESIGN.md naming the
# shape.
cd "$(dirname "$0")" || exit 1
trap 'rm -f rtest' EXIT
CXX=${CXX:-g++}
command -v "$CXX" >/dev/null 2>&1 || CXX=clang++
command -v "$CXX" >/dev/null 2>&1 || { echo "FAIL: no C++ compiler (g++/clang++) on PATH"; exit 1; }
FLAGS="-std=c++17 -Wall -Wextra -fsanitize=address -fno-sanitize-recover=all"

$CXX $FLAGS -o rtest test_rpn.cpp 2>/dev/null || { echo "FAIL: does not compile (need g++/clang++ with ASan)"; exit 1; }
./rtest >/dev/null 2>&1 || { echo "FAIL: rpn_eval does not pass the suite"; exit 1; }
[ -f DESIGN.md ] || { echo "FAIL: DESIGN.md missing (name the shape of your solution)"; exit 1; }
grep -qiE 'stack|fold|reduce' DESIGN.md || { echo "FAIL: DESIGN.md should name the approach (a stack / a fold)"; exit 1; }
echo "PASS: the calculator works and the design is written down"
