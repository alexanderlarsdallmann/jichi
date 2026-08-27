#lang racket/base
(require "rpn.rkt")
(module+ test
  (require rackunit)
  (check-equal? (rpn-eval '(2 3 +))       5  "add")
  (check-equal? (rpn-eval '(4 2 -))       2  "subtract, order matters")
  (check-equal? (rpn-eval '(3 4 *))      12  "multiply")
  (check-equal? (rpn-eval '(2 3 4 * +))  14  "3*4 then +2")
  (check-equal? (rpn-eval '(10 2 3 + -))  5  "10 - (2+3)")
  (check-equal? (rpn-eval '(7))           7  "a lone number"))
