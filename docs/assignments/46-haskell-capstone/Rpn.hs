module Rpn (rpnEval, Token(..)) where

-- A postfix token is either a number or an operator. A Haskell sum type makes
-- that explicit -- and a pattern match on it total (miss a case and the compiler
-- warns): the reading track's "make illegal states unrepresentable". The Token
-- type and its export are given; implement rpnEval against them.
data Token = Num Int | Add | Sub | Mul
  deriving (Show, Eq)

-- Evaluate a reverse-Polish (postfix) expression, e.g.
--   rpnEval [Num 2, Num 3, Add] == 5
-- TODO: implement this. The provided suite in TestRpn.hs is the spec -- do NOT
-- edit it. A one-line design note (DESIGN.md) is part of the task.
rpnEval :: [Token] -> Int
rpnEval _ = 0
