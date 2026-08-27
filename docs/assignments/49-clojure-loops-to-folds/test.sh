#!/bin/sh
# Passes iff BOTH: the tests are green, AND the mutation is gone.
# Clojure's mutation escape hatch is the atom; the refactor must change HOW, not
# WHAT -- behaviour identical, the atom/swap! smell removed.
cd "$(dirname "$0")" || exit 1
clojure -h >/dev/null 2>&1 || { echo "FAIL: clojure is not usable -- install Clojure (or a version-manager shim with no version selected)"; exit 1; }
clojure test_squares.clj >/dev/null 2>&1 || { echo "FAIL: the tests are not green"; exit 1; }
# Strip ; comments first, then reject the mutation escape hatch (atom/swap!/reset!)
# -- so a note that merely mentions the atom is not a false hit.
code=$(sed 's/;.*//' squares.clj)
if printf '%s\n' "$code" | grep -nE '(^|[^A-Za-z0-9_])atom([^A-Za-z0-9_]|$)|swap!|reset!'; then
  echo "FAIL: still mutating -- drop the atom; use reduce/->> with map/filter"
  exit 1
fi
# ...and it must actually use a functional combinator.
printf '%s\n' "$code" | grep -qE '(^|[^A-Za-z0-9_])(reduce|map|filter)([^A-Za-z0-9_]|$)|->>' || {
  echo "FAIL: no combinator in sight -- reach for reduce/->> with map/filter"; exit 1; }
echo "PASS: pure pipeline, and the tests are green"
