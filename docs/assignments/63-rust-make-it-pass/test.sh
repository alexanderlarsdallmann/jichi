#!/bin/sh
# Build the test runner with `rustc --test` and run it. Exit 0 iff every test
# passes. The test file is the truth -- fix the FUNCTION (clamp.rs), not the tests.
cd "$(dirname "$0")" || exit 1
trap 'rm -f ctest' EXIT
rustc --version >/dev/null 2>&1 || { echo "FAIL: rustc is not usable -- install Rust (rustup.rs) (or a version-manager shim with no version selected)"; exit 1; }
rustc --test --edition 2021 test_clamp.rs -o ctest 2>/dev/null || { echo "FAIL: does not compile"; exit 1; }
./ctest >/dev/null 2>&1 || { echo "FAIL: a test in test_clamp.rs did not pass"; exit 1; }
echo "PASS: clamp is correct"
