(define-module (clamp) #:export (clamp))

;; Clamp x into the inclusive range [lo, hi].
(define (clamp x lo hi)
  (cond ((< x lo) lo)
        ((> x hi) x)          ; <-- one of these lines is wrong
        (else     x)))
