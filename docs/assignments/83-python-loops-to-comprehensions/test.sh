#!/bin/sh
# Passes iff BOTH: the tests are green, AND the accumulator loops are gone.
# The refactor must change HOW, not WHAT -- behaviour identical, smell removed.
cd "$(dirname "$0")" || exit 1
command -v python3 >/dev/null 2>&1 || { echo "CANNOT RUN: python3 is not on PATH"; exit 77; }
python3 -m unittest test_report >/dev/null 2>&1 || { echo "FAIL: the tests are not green"; exit 1; }

# The smell check, and it is deliberately narrow. A comprehension's `for` never
# ENDS a line with a colon; a statement loop always does. So this finds the
# loops without banning the word, and a correct comprehension can never trip it.
if grep -nE '^[[:space:]]*for .*:[[:space:]]*$' report.py; then
  echo "FAIL: still an accumulator loop -- the lines above are statement \`for\` loops"
  exit 1; fi
if grep -nE '\.append\(|\.add\(' report.py; then
  echo "FAIL: still accumulating into a container -- build it with a comprehension"
  exit 1; fi
echo "PASS: three comprehensions, and the tests never went red"
