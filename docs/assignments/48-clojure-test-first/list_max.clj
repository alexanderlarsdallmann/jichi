;; The largest element of a non-empty collection. It looks right and passes on
;; the collections people usually try. It is wrong -- the same bug the C course's
;; stats_max had, in a functional coat.
(defn list-max [coll]
  (reduce max 0 coll))
