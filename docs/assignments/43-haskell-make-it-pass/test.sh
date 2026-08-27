#!/bin/sh
# Run the base-only suite in TestClamp.hs. Exit 0 iff every check passes.
# The test file is the truth -- fix the FUNCTION (Clamp.hs), not the tests.
cd "$(dirname "$0")" || exit 1
runghc --version >/dev/null 2>&1 || { echo "FAIL: runghc (GHC) is not usable -- install GHC (or a version-manager shim with no version selected)"; exit 1; }
runghc -i. TestClamp.hs >/dev/null 2>&1 || { echo "FAIL: a check in TestClamp.hs did not pass"; exit 1; }
echo "PASS: clamp is correct"
