;; The suite -- do NOT edit it. Keep it green while changing HOW, not WHAT.
(use-modules (srfi srfi-64) (squares))

(define r (test-runner-create))
(test-with-runner r
  (test-begin "squares")
  (test-equal 20 (sum-of-even-squares '(1 2 3 4 5)))   ; 2^2 + 4^2
  (test-equal 0  (sum-of-even-squares '()))            ; empty
  (test-equal 56 (sum-of-even-squares '(2 4 6)))       ; 2^2 + 4^2 + 6^2
  (test-end "squares"))
(exit (zero? (test-runner-fail-count r)))
