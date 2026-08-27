#lang racket/base
(provide sum-of-even-squares)

;; Sum the squares of the even numbers in lst.
;; It works -- and it is written the way you would in C: a mutable accumulator
;; and a loop that reassigns it. Make it functional (no set!, no mutation),
;; using map / filter / foldl (or for/fold), with the tests still green.
(define (sum-of-even-squares lst)
  (define total 0)
  (for ([x (in-list lst)])
    (when (even? x)
      (set! total (+ total (* x x)))))
  total)

(module+ test
  (require rackunit)
  (check-equal? (sum-of-even-squares '(1 2 3 4 5)) 20 "2^2 + 4^2")
  (check-equal? (sum-of-even-squares '())          0  "empty")
  (check-equal? (sum-of-even-squares '(2 4 6))     56 "2^2 + 4^2 + 6^2"))
