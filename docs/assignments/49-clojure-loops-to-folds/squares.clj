;; Sum the squares of the even numbers in coll. It works -- and it is written the
;; C way: a mutable accumulator (an atom) that a doseq loop reassigns with swap!.
;; Clojure gives you atoms as a managed-mutation escape hatch, but reaching for
;; one to sum a list is the imperative habit. Make it functional (no atom, no
;; swap!/reset!), as a reduce/->> pipeline, with the tests still green.
(defn sum-of-even-squares [coll]
  (let [total (atom 0)]
    (doseq [x coll]
      (when (even? x)
        (swap! total + (* x x))))
    @total))
