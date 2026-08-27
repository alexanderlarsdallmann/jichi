#!/bin/sh
# Two-sided by construction: the pristine tool has no vowels feature, so the
# expected output fails. The floor also requires the new code to BE C++: a
# .cpp source must exist and build.sh must compile it -- a vowel counter
# written in C passes the output check and fails this one.
cd "$(dirname "$0")" || exit 1

command -v c++ >/dev/null 2>&1 || {
    echo "FAIL: this extra needs a C++ compiler (c++/g++/clang++ -- see CPP_INTEROP.md)"
    exit 1; }

sh build.sh || { echo "FAIL: build.sh failed"; exit 1; }

out=$(./wordtool "the quick brown fox") || exit 1
[ "$out" = "words=4 longest=5 vowels=5" ] || {
    echo "FAIL: expected 'words=4 longest=5 vowels=5', got: $out"; exit 1; }
out=$(./wordtool "xylyl pfft") || exit 1
[ "$out" = "words=2 longest=5 vowels=0" ] || {
    echo "FAIL: expected 'words=2 longest=5 vowels=0', got: $out"; exit 1; }

ls ./*.cpp >/dev/null 2>&1 || {
    echo "FAIL: no .cpp source -- the feature must be implemented in C++"
    exit 1; }
grep -q "c++" build.sh || {
    echo "FAIL: build.sh never invokes the C++ compiler"; exit 1; }

rm -f wordtool ./*.o
echo "PASS: the C tool grew a C++-implemented feature behind its C header"
exit 0
