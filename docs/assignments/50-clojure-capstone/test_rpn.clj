;; The spec -- do NOT edit it. Make rpn.clj pass it.
(load-file "rpn.clj")
(require '[clojure.test :refer [deftest is run-tests successful?]])

(deftest rpn-test
  (is (= 5  (rpn-eval [2 3 :+])))        ; add
  (is (= 2  (rpn-eval [4 2 :-])))        ; subtract, order matters
  (is (= 12 (rpn-eval [3 4 :*])))        ; multiply
  (is (= 14 (rpn-eval [2 3 4 :* :+])))   ; 3*4 then +2
  (is (= 5  (rpn-eval [10 2 3 :+ :-])))  ; 10 - (2+3)
  (is (= 7  (rpn-eval [7]))))            ; a lone number

(System/exit (if (successful? (run-tests)) 0 1))
