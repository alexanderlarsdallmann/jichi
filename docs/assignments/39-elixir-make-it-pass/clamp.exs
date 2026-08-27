defmodule Clamp do
  # Clamp x into the inclusive range [lo, hi].
  def clamp(x, lo, _hi) when x < lo, do: lo
  def clamp(x, _lo, hi) when x > hi, do: x      # <-- one of these lines is wrong
  def clamp(x, _lo, _hi), do: x
end
