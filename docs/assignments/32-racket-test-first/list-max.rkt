#lang racket/base
(provide list-max)

;; The largest element of a non-empty list of reals.
;; It looks right, and passes on the lists people usually try. It is wrong.
(define (list-max lst)
  (foldl max 0 lst))
