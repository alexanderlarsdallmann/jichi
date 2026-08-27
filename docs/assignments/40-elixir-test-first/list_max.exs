defmodule ListMax do
  # The largest element of a non-empty list of numbers.
  # It looks right and passes on the lists people usually try. It is wrong --
  # the same bug the C course's stats_max had, in a functional coat.
  def list_max(list), do: Enum.reduce(list, 0, &max/2)
end
