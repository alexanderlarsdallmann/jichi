#!/bin/sh
# The capstone: implement freq.py to pass the provided suite (test_freq.py,
# the spec -- do not edit it), and leave a one-line DESIGN.md naming the shape
# of your solution.
cd "$(dirname "$0")" || exit 1
command -v python3 >/dev/null 2>&1 || { echo "CANNOT RUN: python3 is not on PATH"; exit 77; }
python3 -m unittest test_freq >/dev/null 2>&1 || { echo "FAIL: freq.py does not pass the suite"; exit 1; }
[ -f DESIGN.md ] || { echo "FAIL: DESIGN.md missing (name the shape of your solution)"; exit 1; }
grep -qiE 'dict|counter|sort|key=' DESIGN.md || {
  echo "FAIL: DESIGN.md should name the approach (a dict, a Counter, the sort key)"; exit 1; }
echo "PASS: the report works and the design is written down"
