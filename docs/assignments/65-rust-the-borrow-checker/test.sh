#!/bin/sh
# The instrument here is the COMPILER. There is no sanitizer and no assertion to
# reach first: the pristine code cannot be compiled, because returning a slice of
# a local would dangle. Rust proves memory safety at build time.
#   * pristine  -> rustc refuses to compile (E0515)  -> FAIL
#   * fix the lifetime so the slice borrows the input -> compiles AND the tests
#     pass                                             -> PASS
cd "$(dirname "$0")" || exit 1
trap 'rm -f wtest' EXIT
rustc --version >/dev/null 2>&1 || { echo "FAIL: rustc is not usable -- install Rust (rustup.rs) (or a version-manager shim with no version selected)"; exit 1; }
rustc --test --edition 2021 test_words.rs -o wtest 2>/dev/null || {
    echo "FAIL: does not compile -- the borrow checker still rejects first_word (fix the dangling return)"; exit 1; }
./wtest >/dev/null 2>&1 || { echo "FAIL: it compiles now, but a test does not pass"; exit 1; }
echo "PASS: compiles clean (no dangling borrow) and the tests pass"
