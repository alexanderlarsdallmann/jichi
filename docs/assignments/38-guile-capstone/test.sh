#!/bin/sh
# The capstone: implement rpn.scm to pass the provided suite (test-rpn.scm,
# the spec -- do not edit it), and leave a one-line DESIGN.md naming the shape
# of your solution.
cd "$(dirname "$0")" || exit 1
guile --version >/dev/null 2>&1 || { echo "FAIL: guile (GNU Guile) is not usable -- install it (guile-3.0) (or a version-manager shim with no version selected)"; exit 1; }
guile --no-auto-compile -L . test-rpn.scm >/dev/null 2>&1
rc=$?; rm -f *.log
[ $rc -eq 0 ] || { echo "FAIL: rpn-eval does not pass the suite"; exit 1; }
[ -f DESIGN.md ] || { echo "FAIL: DESIGN.md missing (name the shape of your solution)"; exit 1; }
grep -qiE 'stack|fold|reduce' DESIGN.md || { echo "FAIL: DESIGN.md should name the approach (a stack / a fold)"; exit 1; }
echo "PASS: the calculator works and the design is written down"
