#!/bin/sh
# The capstone: implement rpn.exs to pass the provided suite (test_rpn.exs, the
# spec -- do not edit it), and leave a one-line DESIGN.md naming the shape of
# your solution.
cd "$(dirname "$0")" || exit 1
elixir --version >/dev/null 2>&1 || { echo "FAIL: elixir is not usable -- install Elixir/OTP (or a version-manager shim with no version selected)"; exit 1; }
elixir test_rpn.exs >/dev/null 2>&1 || { echo "FAIL: rpn_eval does not pass the suite"; exit 1; }
[ -f DESIGN.md ] || { echo "FAIL: DESIGN.md missing (name the shape of your solution)"; exit 1; }
grep -qiE 'stack|fold|reduce' DESIGN.md || { echo "FAIL: DESIGN.md should name the approach (a stack / a fold)"; exit 1; }
echo "PASS: the calculator works and the design is written down"
