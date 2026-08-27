#!/bin/sh
# The make-it-fail-first task, in Rust. Passes only when ALL hold:
#   1. you wrote test_list_max.rs, and it compiles, runs, and passes;
#   2. it actually tests something (>= 3 assert checks -- not a hollow suite);
#   3. an independent acceptance probe confirms the bug is really fixed.
cd "$(dirname "$0")" || exit 1
trap 'rm -f lmtest lmaccept _accept.rs' EXIT
rustc --version >/dev/null 2>&1 || { echo "FAIL: rustc is not usable -- install Rust (rustup.rs) (or a version-manager shim with no version selected)"; exit 1; }

if [ ! -f test_list_max.rs ]; then
    echo "FAIL: write the failing test first -- test_list_max.rs is missing"; exit 1; fi
rustc --test --edition 2021 test_list_max.rs -o lmtest 2>/dev/null || { echo "FAIL: test_list_max.rs does not compile"; exit 1; }
./lmtest >/dev/null 2>&1 || { echo "FAIL: your tests do not pass"; exit 1; }
n=$(grep -cE 'assert(_eq)?!' test_list_max.rs)
[ "$n" -ge 3 ] || { echo "FAIL: only $n checks -- pin the behaviour with at least 3"; exit 1; }

# Independent acceptance: the all-negative slice is the case the bug hides in.
cat > _accept.rs <<'ACC'
#[path = "list_max.rs"]
mod list_max;
use list_max::list_max;
#[test]
fn accept() {
    assert_eq!(list_max(&[-3, -1, -7]), -1);
    assert_eq!(list_max(&[5, 2, 9, 1]), 9);
}
ACC
rustc --test --edition 2021 _accept.rs -o lmaccept 2>/dev/null
rc=$?
if [ $rc -eq 0 ]; then ./lmaccept >/dev/null 2>&1; rc=$?; fi
[ $rc -eq 0 ] || { echo "FAIL: list_max is still wrong on an all-negative slice"; exit 1; }
echo "PASS: the failing test was written, and the bug is fixed"
