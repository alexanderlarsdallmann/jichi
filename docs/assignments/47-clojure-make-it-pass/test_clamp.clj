;; The truth for this task -- do NOT edit it; fix clamp.clj until it is green.
;; Clojure's test framework, clojure.test, ships with the language. Note the
;; dialect seam: run-tests reports pass/fail but does not set the exit code, so
;; the final (System/exit ...) on the successful? summary is what makes a failing
;; run exit nonzero -- what a grader (and you) reads.
(load-file "clamp.clj")
(require '[clojure.test :refer [deftest is run-tests successful?]])

(deftest clamp-test
  (is (= 5 (clamp 9 0 5)))    ; above hi clamps to hi
  (is (= 0 (clamp -3 0 5)))   ; below lo clamps to lo
  (is (= 2 (clamp 2 0 5))))   ; in range passes through

(System/exit (if (successful? (run-tests)) 0 1))
