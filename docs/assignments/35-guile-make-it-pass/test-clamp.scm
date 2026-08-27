;; The truth for this task -- do NOT edit it; fix clamp.scm until it is green.
;; Guile's test framework is SRFI-64 (the family cousin of Racket's rackunit).
;; Note the dialect seam: unlike `raco test`, srfi-64 does not set the exit code
;; for you -- the runner + final `(exit ...)` below is what makes a failing
;; check exit non-zero, so a grader (and you) can tell pass from fail.
(use-modules (srfi srfi-64) (clamp))

(define r (test-runner-create))
(test-with-runner r
  (test-begin "clamp")
  (test-equal 5 (clamp 9 0 5))    ; above hi clamps to hi
  (test-equal 0 (clamp -3 0 5))   ; below lo clamps to lo
  (test-equal 2 (clamp 2 0 5))    ; in range passes through
  (test-end "clamp"))
(exit (zero? (test-runner-fail-count r)))
