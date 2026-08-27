#!/bin/sh
# The make-it-fail-first task. Passes only when ALL hold:
#   1. you wrote test-list-max.scm, and it exists, runs, and passes;
#   2. it actually tests something (>= 3 checks -- a hollow suite is not proof);
#   3. an independent acceptance probe confirms the bug is really fixed.
cd "$(dirname "$0")" || exit 1
guile --version >/dev/null 2>&1 || { echo "FAIL: guile (GNU Guile) is not usable -- install it (guile-3.0) (or a version-manager shim with no version selected)"; exit 1; }

if [ ! -f test-list-max.scm ]; then
  echo "FAIL: write the failing test first -- test-list-max.scm is missing"; exit 1; fi
guile --no-auto-compile -L . test-list-max.scm >/dev/null 2>&1 || {
  rm -f *.log; echo "FAIL: your tests do not pass"; exit 1; }
n=$(grep -cE '\(test-(equal|assert|eqv|eq|approximate|error)' test-list-max.scm)
[ "$n" -ge 3 ] || { rm -f *.log; echo "FAIL: only $n checks -- pin the behaviour with at least 3"; exit 1; }

# Independent acceptance: the all-negative list is the case the bug hides in.
cat > _accept.scm <<'ACC'
(use-modules (srfi srfi-64) (list-max))
(define r (test-runner-create))
(test-with-runner r
  (test-begin "accept")
  (test-equal -1 (list-max '(-3 -1 -7)))
  (test-equal 9  (list-max '(5 2 9 1)))
  (test-end "accept"))
(exit (zero? (test-runner-fail-count r)))
ACC
guile --no-auto-compile -L . _accept.scm >/dev/null 2>&1
rc=$?
rm -f _accept.scm *.log
[ $rc -eq 0 ] || { echo "FAIL: list-max is still wrong on an all-negative list"; exit 1; }
echo "PASS: the failing test was written, and the bug is fixed"
