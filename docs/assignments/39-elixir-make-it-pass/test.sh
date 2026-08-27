#!/bin/sh
# Run the ExUnit tests in test_clamp.exs. Exit 0 iff every check passes.
# The test file is the truth -- fix the FUNCTION (clamp.exs), not the tests.
cd "$(dirname "$0")" || exit 1
elixir --version >/dev/null 2>&1 || { echo "FAIL: elixir is not usable -- install Elixir/OTP (or a version-manager shim with no version selected)"; exit 1; }
elixir test_clamp.exs >/dev/null 2>&1 || { echo "FAIL: a check in test_clamp.exs did not pass"; exit 1; }
echo "PASS: clamp is correct"
