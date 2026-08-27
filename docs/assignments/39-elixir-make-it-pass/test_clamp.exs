# The truth for this task -- do NOT edit it; fix clamp.exs until it is green.
# Elixir's test framework is ExUnit. Note the dialect: ExUnit's autorun sets the
# process exit code for you (unlike Guile's SRFI-64), so no runner boilerplate.
Code.require_file("clamp.exs", __DIR__)
ExUnit.start()

defmodule ClampTest do
  use ExUnit.Case
  import Clamp

  test "above hi clamps to hi", do: assert clamp(9, 0, 5) == 5
  test "below lo clamps to lo", do: assert clamp(-3, 0, 5) == 0
  test "in range passes through", do: assert clamp(2, 0, 5) == 2
end
