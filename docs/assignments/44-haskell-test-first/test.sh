#!/bin/sh
# The make-it-fail-first task. Passes only when ALL hold:
#   1. you wrote TestListMax.hs, and it exists, runs, and passes;
#   2. it actually tests something (>= 3 checks -- a hollow suite is not proof);
#   3. an independent acceptance probe confirms the bug is really fixed.
cd "$(dirname "$0")" || exit 1
runghc --version >/dev/null 2>&1 || { echo "FAIL: runghc (GHC) is not usable -- install GHC (or a version-manager shim with no version selected)"; exit 1; }

if [ ! -f TestListMax.hs ]; then
  echo "FAIL: write the failing test first -- TestListMax.hs is missing"; exit 1; fi
runghc -i. TestListMax.hs >/dev/null 2>&1 || { echo "FAIL: your tests do not pass"; exit 1; }
n=$(grep -cE 'check \(' TestListMax.hs)
[ "$n" -ge 3 ] || { echo "FAIL: only $n checks -- pin the behaviour with at least 3"; exit 1; }

# Independent acceptance: the all-negative list is the case the bug hides in.
cat > Accept.hs <<'ACC'
module Main where
import ListMax (listMax)
import System.Exit (exitFailure, exitSuccess)
main :: IO ()
main =
  if listMax [-3, -1, -7] == (-1) && listMax [5, 2, 9, 1] == 9
    then exitSuccess
    else exitFailure
ACC
runghc -i. Accept.hs >/dev/null 2>&1
rc=$?
rm -f Accept.hs
[ $rc -eq 0 ] || { echo "FAIL: listMax is still wrong on an all-negative list"; exit 1; }
echo "PASS: the failing test was written, and the bug is fixed"
