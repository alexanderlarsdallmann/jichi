#!/bin/sh
# The make-it-fail-first task, in C++. Passes only when ALL hold:
#   1. you wrote test_list_max.cpp, and it compiles, runs under ASan, and exits 0;
#   2. it actually tests something (>= 3 assert()s -- not a hollow suite);
#   3. an independent acceptance probe confirms the bug is really fixed.
cd "$(dirname "$0")" || exit 1
trap 'rm -f lmtest lmaccept _accept.cpp' EXIT
CXX=${CXX:-g++}
command -v "$CXX" >/dev/null 2>&1 || CXX=clang++
command -v "$CXX" >/dev/null 2>&1 || { echo "FAIL: no C++ compiler (g++/clang++) on PATH"; exit 1; }
FLAGS="-std=c++17 -Wall -Wextra -fsanitize=address -fno-sanitize-recover=all"

if [ ! -f test_list_max.cpp ]; then
    echo "FAIL: write the failing test first -- test_list_max.cpp is missing"; exit 1; fi
$CXX $FLAGS -o lmtest test_list_max.cpp 2>/dev/null || { echo "FAIL: test_list_max.cpp does not compile"; exit 1; }
./lmtest >/dev/null 2>&1 || { echo "FAIL: your tests do not pass"; exit 1; }
n=$(grep -cE 'assert[[:space:]]*\(' test_list_max.cpp)
[ "$n" -ge 3 ] || { echo "FAIL: only $n assert()s -- pin the behaviour with at least 3"; exit 1; }

# Independent acceptance: the all-negative vector is the case the bug hides in.
cat > _accept.cpp <<'ACC'
#include "list_max.hpp"
#include <cassert>
int main() {
    assert(list_max({-3, -1, -7}) == -1);
    assert(list_max({5, 2, 9, 1}) == 9);
    return 0;
}
ACC
$CXX $FLAGS -o lmaccept _accept.cpp 2>/dev/null
rc=$?
if [ $rc -eq 0 ]; then ./lmaccept >/dev/null 2>&1; rc=$?; fi
[ $rc -eq 0 ] || { echo "FAIL: list_max is still wrong on an all-negative vector"; exit 1; }
echo "PASS: the failing test was written, and the bug is fixed"
