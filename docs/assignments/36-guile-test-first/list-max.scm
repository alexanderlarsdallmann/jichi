(define-module (list-max) #:export (list-max) #:use-module (srfi srfi-1))

;; The largest element of a non-empty list of reals.
;; It looks right, and passes on the lists people usually try. It is wrong --
;; the same bug the C course's stats_max had, in a functional coat.
(define (list-max lst)
  (fold max 0 lst))
