;; The spec -- do NOT edit it. Make rpn.scm pass it.
(use-modules (srfi srfi-64) (rpn))

(define r (test-runner-create))
(test-with-runner r
  (test-begin "rpn")
  (test-equal 5  (rpn-eval '(2 3 +)))       ; add
  (test-equal 2  (rpn-eval '(4 2 -)))       ; subtract, order matters
  (test-equal 12 (rpn-eval '(3 4 *)))       ; multiply
  (test-equal 14 (rpn-eval '(2 3 4 * +)))   ; 3*4 then +2
  (test-equal 5  (rpn-eval '(10 2 3 + -)))  ; 10 - (2+3)
  (test-equal 7  (rpn-eval '(7)))           ; a lone number
  (test-end "rpn"))
(exit (zero? (test-runner-fail-count r)))
