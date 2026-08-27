# The suite -- do NOT edit it. Keep it green while changing HOW, not WHAT.
Code.require_file("squares.exs", __DIR__)
ExUnit.start()

defmodule SquaresTest do
  use ExUnit.Case
  import Squares

  test "odds ignored", do: assert sum_of_even_squares([1, 2, 3, 4, 5]) == 20
  test "empty", do: assert sum_of_even_squares([]) == 0
  test "all even", do: assert sum_of_even_squares([2, 4, 6]) == 56
end
