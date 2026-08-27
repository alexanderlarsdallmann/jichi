#!/bin/sh
# Passes iff BOTH: the suite is green under ASan/LeakSanitizer, AND the raw
# owning new/delete is gone. Two paired instruments, like the C sprintf task:
# LeakSanitizer proves nothing leaks, a grep proves you reached for RAII (a
# std::vector or a smart pointer) instead of managing raw memory by hand -- so
# "add a destructor with delete[]" is not the lesson here; letting the library
# own it is.
cd "$(dirname "$0")" || exit 1
trap 'rm -f btest' EXIT
CXX=${CXX:-g++}
command -v "$CXX" >/dev/null 2>&1 || CXX=clang++
command -v "$CXX" >/dev/null 2>&1 || { echo "FAIL: no C++ compiler (g++/clang++) on PATH"; exit 1; }
FLAGS="-std=c++17 -Wall -Wextra -fsanitize=address -fno-sanitize-recover=all"

$CXX $FLAGS -o btest test_buffer.cpp 2>/dev/null || { echo "FAIL: does not compile (need g++/clang++ with ASan)"; exit 1; }
./btest >/dev/null 2>&1 || { echo "FAIL: LeakSanitizer reported a leak, or an assertion failed"; exit 1; }
# The smell check: raw ownership primitives fail the grade. Strip // comments
# first so a note that mentions new/delete is not a false hit.
code=$(sed 's://.*::' buffer.hpp)
if printf '%s\n' "$code" | grep -nE '(^|[^A-Za-z0-9_])(new|delete|malloc|free)([^A-Za-z0-9_]|$)'; then
    echo "FAIL: still managing raw memory -- own it with std::vector or a smart pointer, no raw new/delete"
    exit 1
fi
echo "PASS: no leak under LeakSanitizer, and the memory is owned by RAII"
