#!/bin/sh
# Structural floor for a README a stranger can follow: an install section, a
# run/usage section, and at least one WORKED EXAMPLE (a code block). Whether the
# docs are CLEAR is your judgment; whether the essentials are present is checkable.
cd "$(dirname "$0")" || exit 1
grep -qiE 'install' README.md || { echo "FAIL: no install section -- a stranger needs to know how to install it"; exit 1; }
grep -qiE 'usage|run|getting started|quick ?start' README.md || { echo "FAIL: no run/usage section -- how does someone actually run it?"; exit 1; }
[ "$(grep -cE '^```' README.md)" -ge 2 ] || { echo "FAIL: no worked example -- include a fenced code block a reader can copy and run"; exit 1; }
echo "PASS: install + run + a worked example are all present"
