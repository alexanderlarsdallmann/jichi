module Clamp (clamp) where

-- Clamp x into the inclusive range [lo, hi].
clamp :: Int -> Int -> Int -> Int
clamp x lo hi
  | x < lo    = lo
  | x > hi    = x      -- <-- one of these guards is wrong
  | otherwise = x
