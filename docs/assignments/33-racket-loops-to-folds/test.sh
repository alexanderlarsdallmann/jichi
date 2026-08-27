#!/bin/sh
# Passes iff BOTH: the tests are green, AND the mutation is gone.
# The refactor must change HOW, not WHAT -- behaviour identical, smell removed.
cd "$(dirname "$0")" || exit 1
raco -h >/dev/null 2>&1 || { echo "FAIL: raco (Racket) is not usable (or a version-manager shim with no version selected)"; exit 1; }
raco test squares.rkt >/dev/null 2>&1 || { echo "FAIL: the tests are not green"; exit 1; }
# The smell check: any in-place mutation fails the grade, whatever the output.
if grep -nE '(^|[^A-Za-z0-9_])set!|set-box!|vector-set!|hash-set!' squares.rkt; then
  echo "FAIL: still mutating -- remove set!/mutation; use map/filter/foldl or for/fold"
  exit 1
fi
echo "PASS: pure, and the tests are green"
