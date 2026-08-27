#lang racket/base
(provide clamp)

;; Clamp x into the inclusive range [lo, hi].
(define (clamp x lo hi)
  (cond [(< x lo) lo]
        [(> x hi) x]          ; <-- one of these lines is wrong
        [else     x]))

(module+ test
  (require rackunit)
  (check-equal? (clamp 9 0 5)  5 "above hi clamps to hi")
  (check-equal? (clamp -3 0 5) 0 "below lo clamps to lo")
  (check-equal? (clamp 2 0 5)  2 "in range passes through"))
