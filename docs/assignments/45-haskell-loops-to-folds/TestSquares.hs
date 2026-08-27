-- The suite -- do NOT edit it. Keep it green while changing HOW, not WHAT.
module Main where

import Squares (sumOfEvenSquares)
import System.Exit (exitFailure, exitSuccess)

check :: (Eq a, Show a) => a -> a -> IO Bool
check got want
  | got == want = return True
  | otherwise   = putStrLn ("FAIL: got " ++ show got ++ ", want " ++ show want) >> return False

main :: IO ()
main = do
  results <-
    sequence
      [ check (sumOfEvenSquares [1, 2, 3, 4, 5]) 20   -- 2^2 + 4^2
      , check (sumOfEvenSquares []) 0                 -- empty
      , check (sumOfEvenSquares [2, 4, 6]) 56         -- 2^2 + 4^2 + 6^2
      ]
  if and results then exitSuccess else exitFailure
