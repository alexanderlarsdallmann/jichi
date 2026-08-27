#!/bin/sh
# Run the SRFI-64 tests in test-clamp.scm. Exit 0 iff every check passes.
# The test file is the truth -- fix the FUNCTION (clamp.scm), not the tests.
cd "$(dirname "$0")" || exit 1
guile --version >/dev/null 2>&1 || { echo "FAIL: guile (GNU Guile) is not usable -- install it (guile-3.0) (or a version-manager shim with no version selected)"; exit 1; }
guile --no-auto-compile -L . test-clamp.scm >/dev/null 2>&1
rc=$?
rm -f *.log
[ $rc -eq 0 ] || { echo "FAIL: a check in test-clamp.scm did not pass"; exit 1; }
echo "PASS: clamp is correct"
