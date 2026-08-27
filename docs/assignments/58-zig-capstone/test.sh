#!/bin/sh
# The capstone: implement rpn.zig to pass the provided suite (test_rpn.zig, the
# spec -- do not edit it), and leave a one-line DESIGN.md naming the shape.
cd "$(dirname "$0")" || exit 1
zig version >/dev/null 2>&1 || { echo "FAIL: zig is not usable -- install Zig (ziglang.org) (or a version-manager shim with no version selected)"; exit 1; }
zig test test_rpn.zig >/dev/null 2>&1 || { echo "FAIL: rpnEval does not pass the suite"; exit 1; }
[ -f DESIGN.md ] || { echo "FAIL: DESIGN.md missing (name the shape of your solution)"; exit 1; }
grep -qiE 'stack|fold|reduce' DESIGN.md || { echo "FAIL: DESIGN.md should name the approach (a stack / a fold)"; exit 1; }
echo "PASS: the calculator works and the design is written down"
