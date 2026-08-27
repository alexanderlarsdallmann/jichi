#!/bin/sh
# The make-it-fail-first task. Passes only when ALL hold:
#   1. you wrote test-list-max.rkt, and it exists, runs, and passes;
#   2. it actually tests something (>= 3 checks -- a hollow suite is not proof);
#   3. an independent acceptance probe confirms the bug is really fixed.
cd "$(dirname "$0")" || exit 1
raco -h >/dev/null 2>&1 || { echo "FAIL: raco (Racket) is not usable (or a version-manager shim with no version selected)"; exit 1; }

if [ ! -f test-list-max.rkt ]; then
  echo "FAIL: write the failing test first -- test-list-max.rkt is missing"; exit 1; fi
raco test test-list-max.rkt >/dev/null 2>&1 || { echo "FAIL: your tests do not pass"; exit 1; }
n=$(grep -cE '\(check-' test-list-max.rkt)
[ "$n" -ge 3 ] || { echo "FAIL: only $n checks -- pin the behaviour with at least 3"; exit 1; }

# Independent acceptance: the all-negative list is the case the bug hides in.
cat > _accept.rkt <<'ACC'
#lang racket/base
(require "list-max.rkt")
(module+ test
  (require rackunit)
  (check-equal? (list-max '(-3 -1 -7)) -1 "all-negative")
  (check-equal? (list-max '(5 2 9 1))  9  "positive"))
ACC
raco test _accept.rkt >/dev/null 2>&1
rc=$?
rm -f _accept.rkt
[ $rc -eq 0 ] || { echo "FAIL: list-max is still wrong on an all-negative list"; exit 1; }
echo "PASS: the failing test was written, and the bug is fixed"
