#!/bin/sh
# Passes iff BOTH: the tests are green, AND the mutation is gone.
# The refactor must change HOW, not WHAT -- behaviour identical, smell removed.
cd "$(dirname "$0")" || exit 1
guile --version >/dev/null 2>&1 || { echo "FAIL: guile (GNU Guile) is not usable -- install it (guile-3.0) (or a version-manager shim with no version selected)"; exit 1; }
guile --no-auto-compile -L . test-squares.scm >/dev/null 2>&1
rc=$?; rm -f *.log
[ $rc -eq 0 ] || { echo "FAIL: the tests are not green"; exit 1; }
# The smell check: any in-place mutation fails the grade, whatever the output.
# Strip comments (a ';' to end of line) first, so a note that merely mentions
# set! is not a false positive -- only mutation in the code counts.
if sed 's/;.*//' squares.scm | grep -nE '(^|[^A-Za-z0-9_])set!|set-car!|set-cdr!|vector-set!|hash-set!'; then
  echo "FAIL: still mutating -- remove set!/mutation; use map/filter/fold"
  exit 1
fi
echo "PASS: pure, and the tests are green"
