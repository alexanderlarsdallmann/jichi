#!/bin/sh
# Passes iff the suite is green under std.testing.allocator -- which fails the
# test if the body leaks even one byte. There is no separate smell grep here:
# Zig's leak-detecting test allocator IS the instrument, the way ASan is for C.
#   * pristine  -> the internal scratch buffer leaks -> the test fails -> FAIL
#   * add `defer allocator.free(scratch);` -> no leak -> PASS
cd "$(dirname "$0")" || exit 1
zig version >/dev/null 2>&1 || { echo "FAIL: zig is not usable -- install Zig (ziglang.org) (or a version-manager shim with no version selected)"; exit 1; }
zig test test_shout.zig >/dev/null 2>&1 || { echo "FAIL: the suite is not green -- a leak, or a wrong result (run 'zig test test_shout.zig' to see)"; exit 1; }
echo "PASS: no leak under the testing allocator, and the output is correct"
