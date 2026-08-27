defmodule Squares do
  # Sum the squares of the even numbers in list.
  # It works -- and it is written the C way: hand-rolled recursion with an
  # accumulator threaded through a private helper. Elixir has no set!/mutable
  # variable to remove; here the imperative smell IS the manual recursion.
  # Refactor to an Enum pipeline (filter |> map |> sum), tests still green.
  def sum_of_even_squares(list), do: loop(list, 0)

  defp loop([], acc), do: acc

  defp loop([h | t], acc) do
    if rem(h, 2) == 0 do
      loop(t, acc + h * h)
    else
      loop(t, acc)
    end
  end
end
