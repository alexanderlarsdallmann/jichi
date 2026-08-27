;; The suite -- do NOT edit it. Keep it green while changing HOW, not WHAT.
(load-file "squares.clj")
(require '[clojure.test :refer [deftest is run-tests successful?]])

(deftest squares-test
  (is (= 20 (sum-of-even-squares [1 2 3 4 5])))   ; 2^2 + 4^2
  (is (= 0  (sum-of-even-squares [])))            ; empty
  (is (= 56 (sum-of-even-squares [2 4 6]))))      ; 2^2 + 4^2 + 6^2

(System/exit (if (successful? (run-tests)) 0 1))
