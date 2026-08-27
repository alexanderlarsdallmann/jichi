#!/bin/sh
# Run the clojure.test suite in test_clamp.clj. Exit 0 iff every check passes.
# The test file is the truth -- fix the FUNCTION (clamp.clj), not the tests.
cd "$(dirname "$0")" || exit 1
clojure -h >/dev/null 2>&1 || { echo "FAIL: clojure is not usable -- install Clojure (or a version-manager shim with no version selected)"; exit 1; }
clojure test_clamp.clj >/dev/null 2>&1 || { echo "FAIL: a check in test_clamp.clj did not pass"; exit 1; }
echo "PASS: clamp is correct"
