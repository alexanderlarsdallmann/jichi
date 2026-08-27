#!/bin/sh
# The make-it-fail-first task. Passes only when ALL hold:
#   1. you wrote test_list_max.exs, and it exists, runs, and passes;
#   2. it actually tests something (>= 3 asserts -- a hollow suite is not proof);
#   3. an independent acceptance probe confirms the bug is really fixed.
cd "$(dirname "$0")" || exit 1
elixir --version >/dev/null 2>&1 || { echo "FAIL: elixir is not usable -- install Elixir/OTP (or a version-manager shim with no version selected)"; exit 1; }

if [ ! -f test_list_max.exs ]; then
  echo "FAIL: write the failing test first -- test_list_max.exs is missing"; exit 1; fi
elixir test_list_max.exs >/dev/null 2>&1 || { echo "FAIL: your tests do not pass"; exit 1; }
n=$(grep -cE '(^|[^A-Za-z0-9_])assert([^A-Za-z0-9_]|$)' test_list_max.exs)
[ "$n" -ge 3 ] || { echo "FAIL: only $n asserts -- pin the behaviour with at least 3"; exit 1; }

# Independent acceptance: the all-negative list is the case the bug hides in.
cat > _accept.exs <<'ACC'
Code.require_file("list_max.exs", __DIR__)
ExUnit.start()
defmodule AcceptTest do
  use ExUnit.Case
  import ListMax
  test "all-negative", do: assert list_max([-3, -1, -7]) == -1
  test "positive", do: assert list_max([5, 2, 9, 1]) == 9
end
ACC
elixir _accept.exs >/dev/null 2>&1
rc=$?
rm -f _accept.exs
[ $rc -eq 0 ] || { echo "FAIL: list_max is still wrong on an all-negative list"; exit 1; }
echo "PASS: the failing test was written, and the bug is fixed"
