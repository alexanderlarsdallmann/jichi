#!/bin/sh
# The capstone: implement Rpn.hs to pass the provided suite (TestRpn.hs, the spec
# -- do not edit it), and leave a one-line DESIGN.md naming the shape of your
# solution.
cd "$(dirname "$0")" || exit 1
runghc --version >/dev/null 2>&1 || { echo "FAIL: runghc (GHC) is not usable -- install GHC (or a version-manager shim with no version selected)"; exit 1; }
runghc -i. TestRpn.hs >/dev/null 2>&1 || { echo "FAIL: rpnEval does not pass the suite"; exit 1; }
[ -f DESIGN.md ] || { echo "FAIL: DESIGN.md missing (name the shape of your solution)"; exit 1; }
grep -qiE 'stack|fold|reduce' DESIGN.md || { echo "FAIL: DESIGN.md should name the approach (a stack / a fold)"; exit 1; }
echo "PASS: the calculator works and the design is written down"
