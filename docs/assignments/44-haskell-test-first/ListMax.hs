module ListMax (listMax) where

-- The largest element of a non-empty list. It looks right and passes on the
-- lists people usually try. It is wrong -- the same bug the C course's
-- stats_max had, in a functional coat.
listMax :: [Int] -> Int
listMax = foldl max 0
