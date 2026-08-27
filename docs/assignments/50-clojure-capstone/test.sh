#!/bin/sh
# The capstone: implement rpn.clj to pass the provided suite (test_rpn.clj, the
# spec -- do not edit it), and leave a one-line DESIGN.md naming the shape.
cd "$(dirname "$0")" || exit 1
clojure -h >/dev/null 2>&1 || { echo "FAIL: clojure is not usable -- install Clojure (or a version-manager shim with no version selected)"; exit 1; }
clojure test_rpn.clj >/dev/null 2>&1 || { echo "FAIL: rpn-eval does not pass the suite"; exit 1; }
[ -f DESIGN.md ] || { echo "FAIL: DESIGN.md missing (name the shape of your solution)"; exit 1; }
grep -qiE 'stack|fold|reduce' DESIGN.md || { echo "FAIL: DESIGN.md should name the approach (a stack / a fold)"; exit 1; }
echo "PASS: the calculator works and the design is written down"
