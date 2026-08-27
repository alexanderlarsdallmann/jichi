(define-module (rpn) #:export (rpn-eval) #:use-module (srfi srfi-1))

;; Evaluate a reverse-Polish (postfix) expression given as a list of tokens:
;; numbers and the symbols '+ '- '* . Example: '(2 3 +) => 5, '(2 3 4 * +) => 14.
;;
;; TODO: implement this. The provided suite in test-rpn.scm is the spec --
;; do NOT edit it. A one-line design note (DESIGN.md) is part of the task.
;; (srfi-1's `fold` is already imported above, for when you want it.)
(define (rpn-eval tokens)
  0)   ; <-- replace with a real implementation
