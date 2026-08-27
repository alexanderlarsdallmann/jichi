# The spec -- do NOT edit it. Make rpn.exs pass it.
Code.require_file("rpn.exs", __DIR__)
ExUnit.start()

defmodule RpnTest do
  use ExUnit.Case
  import Rpn

  test "add", do: assert rpn_eval([2, 3, :+]) == 5
  test "subtract, order matters", do: assert rpn_eval([4, 2, :-]) == 2
  test "multiply", do: assert rpn_eval([3, 4, :*]) == 12
  test "compound (3*4 then +2)", do: assert rpn_eval([2, 3, 4, :*, :+]) == 14
  test "nested (10 - (2+3))", do: assert rpn_eval([10, 2, 3, :+, :-]) == 5
  test "a lone number", do: assert rpn_eval([7]) == 7
end
