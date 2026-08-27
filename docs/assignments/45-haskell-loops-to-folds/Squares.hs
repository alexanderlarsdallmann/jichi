module Squares (sumOfEvenSquares) where

-- Sum the squares of the even numbers. It works -- and it is written the C way:
-- hand-rolled recursion with an accumulator threaded through a `where` helper.
-- In Haskell recursion is a first-class tool, but for a simple aggregate like
-- this the declarative pipeline (filter/map/sum) is the idiom. Refactor to it,
-- with the tests still green.
sumOfEvenSquares :: [Int] -> Int
sumOfEvenSquares = go 0
  where
    go acc [] = acc
    go acc (x:xs)
      | even x    = go (acc + x * x) xs
      | otherwise = go acc xs
