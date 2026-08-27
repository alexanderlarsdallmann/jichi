#!/bin/sh
# The capstone: implement rpn.rs to pass the provided suite (test_rpn.rs, the
# spec -- do not edit it), and leave a one-line DESIGN.md naming the shape.
cd "$(dirname "$0")" || exit 1
trap 'rm -f rtest' EXIT
rustc --version >/dev/null 2>&1 || { echo "FAIL: rustc is not usable -- install Rust (rustup.rs) (or a version-manager shim with no version selected)"; exit 1; }
rustc --test --edition 2021 test_rpn.rs -o rtest 2>/dev/null || { echo "FAIL: does not compile"; exit 1; }
./rtest >/dev/null 2>&1 || { echo "FAIL: rpn_eval does not pass the suite"; exit 1; }
[ -f DESIGN.md ] || { echo "FAIL: DESIGN.md missing (name the shape of your solution)"; exit 1; }
grep -qiE 'stack|fold|reduce' DESIGN.md || { echo "FAIL: DESIGN.md should name the approach (a stack / a fold)"; exit 1; }
echo "PASS: the calculator works and the design is written down"
