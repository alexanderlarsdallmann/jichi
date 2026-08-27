(define-module (squares) #:export (sum-of-even-squares) #:use-module (srfi srfi-1))

;; Sum the squares of the even numbers in lst.
;; It works -- and it is written the way you would in C: a mutable accumulator
;; and a loop that reassigns it. Make it functional (no set!, no mutation),
;; using map / filter / fold, with the tests still green.
(define (sum-of-even-squares lst)
  (let ((total 0))
    (for-each (lambda (x)
                (when (even? x)
                  (set! total (+ total (* x x)))))
              lst)
    total))
