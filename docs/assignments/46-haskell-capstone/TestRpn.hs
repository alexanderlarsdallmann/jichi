-- The spec -- do NOT edit it. Make Rpn.hs pass it.
module Main where

import Rpn (rpnEval, Token(..))
import System.Exit (exitFailure, exitSuccess)

check :: (Eq a, Show a) => a -> a -> IO Bool
check got want
  | got == want = return True
  | otherwise   = putStrLn ("FAIL: got " ++ show got ++ ", want " ++ show want) >> return False

main :: IO ()
main = do
  results <-
    sequence
      [ check (rpnEval [Num 2, Num 3, Add]) 5              -- add
      , check (rpnEval [Num 4, Num 2, Sub]) 2              -- subtract, order matters
      , check (rpnEval [Num 3, Num 4, Mul]) 12             -- multiply
      , check (rpnEval [Num 2, Num 3, Num 4, Mul, Add]) 14 -- 3*4 then +2
      , check (rpnEval [Num 10, Num 2, Num 3, Add, Sub]) 5 -- 10 - (2+3)
      , check (rpnEval [Num 7]) 7                           -- a lone number
      ]
  if and results then exitSuccess else exitFailure
