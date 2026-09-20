#!/bin/sh
# The make-it-fail-first task. Passes only when ALL hold:
#   1. you wrote test_read_config.py, and it exists, runs, and passes;
#   2. it actually tests something (>= 3 assertions -- a hollow suite is not
#      proof);
#   3. an independent acceptance probe confirms the bug is really fixed;
#   4. the bare `except:` is gone -- it is what hid the bug, and it also
#      swallows KeyboardInterrupt and SystemExit.
cd "$(dirname "$0")" || exit 1
command -v python3 >/dev/null 2>&1 || { echo "CANNOT RUN: python3 is not on PATH"; exit 77; }

if [ ! -f test_read_config.py ]; then
  echo "FAIL: write the failing test first -- test_read_config.py is missing"; exit 1; fi
python3 -m unittest test_read_config >/dev/null 2>&1 || {
  echo "FAIL: your tests do not pass"; exit 1; }
n=$(grep -cE 'self\.assert|assertRaises|pytest\.raises' test_read_config.py)
[ "$n" -ge 3 ] || { echo "FAIL: only $n assertion(s) -- pin the behaviour with at least 3"; exit 1; }

# Independent acceptance: the malformed line is the case the bug hides in.
cat > _accept.py <<'ACC'
import sys
from config import read_config

got = read_config(["a=1", "# a comment", "", "b = 2 "])
assert got == {"a": "1", "b": "2"}, "good lines: got %r" % (got,)
try:
    read_config(["oops"])
except ValueError as e:
    assert "oops" in str(e), "the message should name the bad line: %r" % (str(e),)
except Exception as e:
    sys.exit("a malformed line raised %s, not ValueError" % type(e).__name__)
else:
    sys.exit("a malformed line was accepted -- it must raise ValueError")
ACC
python3 _accept.py
rc=$?
rm -f _accept.py
[ $rc -eq 0 ] || { echo "FAIL: read_config still does not reject a malformed line"; exit 1; }

if grep -nE '^[[:space:]]*except[[:space:]]*:' config.py; then
  echo "FAIL: still catching everything -- name the exception you expect"; exit 1; fi
echo "PASS: the failing test was written, and the bug is fixed"
