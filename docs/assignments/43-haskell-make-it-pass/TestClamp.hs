-- The truth for this task -- do NOT edit it; fix Clamp.hs until it is green.
-- Real Haskell projects use HUnit / hspec / QuickCheck, but those need a package
-- manager. This course needs only `runghc`, so the suite is a tiny base-only
-- harness: a `check` that exits nonzero on any mismatch. (That a test is just a
-- program returning a failing exit code is the whole idea a framework wraps.)
module Main where

import Clamp (clamp)
import System.Exit (exitFailure, exitSuccess)

check :: (Eq a, Show a) => a -> a -> IO Bool
check got want
  | got == want = return True
  | otherwise   = putStrLn ("FAIL: got " ++ show got ++ ", want " ++ show want) >> return False

main :: IO ()
main = do
  results <-
    sequence
      [ check (clamp 9 0 5) 5      -- above hi clamps to hi
      , check (clamp (-3) 0 5) 0   -- below lo clamps to lo
      , check (clamp 2 0 5) 2      -- in range passes through
      ]
  if and results then exitSuccess else exitFailure
