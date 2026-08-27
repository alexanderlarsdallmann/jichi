#!/bin/sh
# Passes iff BOTH: the tests are green, AND the manual recursion is gone.
# Elixir has no mutable variable to strip -- the imperative smell here is the
# hand-rolled recursion (a private helper destructuring [h | t]). The refactor
# must change HOW, not WHAT: behaviour identical, reached with Enum.
cd "$(dirname "$0")" || exit 1
elixir --version >/dev/null 2>&1 || { echo "FAIL: elixir is not usable -- install Elixir/OTP (or a version-manager shim with no version selected)"; exit 1; }
elixir test_squares.exs >/dev/null 2>&1 || { echo "FAIL: the tests are not green"; exit 1; }
# Strip # comments first, so a note that mentions recursion is not a false hit.
code=$(sed 's/#.*//' squares.exs)
# The smell: a private helper (defp) or a cons-pattern function head [x | xs]
# -- the fingerprints of hand-rolled list recursion.
if printf '%s\n' "$code" | grep -nE '(^|[^A-Za-z0-9_])defp([^A-Za-z0-9_]|$)|def.*\[[^]]*\|[^]]*\]'; then
  echo "FAIL: still hand-rolling recursion -- use an Enum pipeline (filter |> map |> sum)"
  exit 1
fi
# ...and it must actually use Enum (evidence of the functional refactor).
printf '%s\n' "$code" | grep -q 'Enum\.' || {
  echo "FAIL: no Enum in sight -- reach for Enum.filter/Enum.map/Enum.sum"; exit 1; }
echo "PASS: pure Enum pipeline, and the tests are green"
