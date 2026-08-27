(defn clamp [x lo hi]
  (cond (< x lo) lo
        (> x hi) x        ; <-- one of these lines is wrong
        :else    x))
