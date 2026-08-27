#!/bin/sh
# The make-it-fail-first task. Passes only when ALL hold:
#   1. you wrote test_list_max.clj, and it exists, runs, and passes;
#   2. it actually tests something (>= 3 (is ...) checks -- not a hollow suite);
#   3. an independent acceptance probe confirms the bug is really fixed.
cd "$(dirname "$0")" || exit 1
clojure -h >/dev/null 2>&1 || { echo "FAIL: clojure is not usable -- install Clojure (or a version-manager shim with no version selected)"; exit 1; }

if [ ! -f test_list_max.clj ]; then
  echo "FAIL: write the failing test first -- test_list_max.clj is missing"; exit 1; fi
clojure test_list_max.clj >/dev/null 2>&1 || { echo "FAIL: your tests do not pass"; exit 1; }
n=$(grep -cE '\(is ' test_list_max.clj)
[ "$n" -ge 3 ] || { echo "FAIL: only $n checks -- pin the behaviour with at least 3"; exit 1; }

# Independent acceptance: the all-negative list is the case the bug hides in.
cat > _accept.clj <<'ACC'
(load-file "list_max.clj")
(System/exit (if (and (= -1 (list-max [-3 -1 -7])) (= 9 (list-max [5 2 9 1]))) 0 1))
ACC
clojure _accept.clj >/dev/null 2>&1
rc=$?
rm -f _accept.clj
[ $rc -eq 0 ] || { echo "FAIL: list-max is still wrong on an all-negative list"; exit 1; }
echo "PASS: the failing test was written, and the bug is fixed"
