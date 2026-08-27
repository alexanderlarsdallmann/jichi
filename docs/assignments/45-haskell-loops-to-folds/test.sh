#!/bin/sh
# Passes iff BOTH: the tests are green, AND the hand-rolled recursion is gone.
# Haskell has no mutable variable, and recursion is a first-class tool -- but for
# a simple aggregate the declarative pipeline is the idiom. The refactor must
# change HOW, not WHAT: behaviour identical, reached with combinators.
cd "$(dirname "$0")" || exit 1
runghc --version >/dev/null 2>&1 || { echo "FAIL: runghc (GHC) is not usable -- install GHC (or a version-manager shim with no version selected)"; exit 1; }
runghc -i. TestSquares.hs >/dev/null 2>&1 || { echo "FAIL: the tests are not green"; exit 1; }
# Mask '::' (type sigs) and strip -- comments, then look for a cons-pattern
# equation like (x:xs) -- the fingerprint of hand-rolled list recursion.
code=$(sed 's/::/@@/g; s/--.*//' Squares.hs)
if printf '%s\n' "$code" | grep -nE '\([a-z_]+ *: *[a-z_]+\)'; then
  echo "FAIL: still hand-rolling recursion -- use a pipeline (filter/map/sum or a fold)"
  exit 1
fi
# ...and it must actually use a combinator (evidence of the functional refactor).
printf '%s\n' "$code" | grep -qE '(^|[^A-Za-z0-9_])(map|filter|foldr|foldl|sum)([^A-Za-z0-9_]|$)' || {
  echo "FAIL: no combinator in sight -- reach for filter/map/sum or a fold"; exit 1; }
echo "PASS: pure combinator pipeline, and the tests are green"
