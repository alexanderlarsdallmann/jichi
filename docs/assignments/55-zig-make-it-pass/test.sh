#!/bin/sh
# Run the Zig test suite in test_clamp.zig. Exit 0 iff every test passes.
# The test file is the truth -- fix the FUNCTION (clamp.zig), not the tests.
cd "$(dirname "$0")" || exit 1
zig version >/dev/null 2>&1 || { echo "FAIL: zig is not usable -- install Zig (ziglang.org) (or a version-manager shim with no version selected)"; exit 1; }
zig test test_clamp.zig >/dev/null 2>&1 || { echo "FAIL: a test in test_clamp.zig did not pass"; exit 1; }
echo "PASS: clamp is correct"
