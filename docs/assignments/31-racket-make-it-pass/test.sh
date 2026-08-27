#!/bin/sh
# Run the rackunit tests in clamp.rkt. Exit 0 iff every check passes.
# The test module is the truth -- fix the FUNCTION, not the tests.
cd "$(dirname "$0")" || exit 1
raco -h >/dev/null 2>&1 || { echo "FAIL: raco (Racket) is not usable (or a version-manager shim with no version selected)"; exit 1; }
raco test clamp.rkt
