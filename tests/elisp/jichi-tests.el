;;; jichi-tests.el --- ERT tests for jichi.el  -*- lexical-binding: t; -*-

;; Offline tests for the Emacs integration.  They exercise the pure prompt /
;; argument / de-fence helpers, and drive each disposition against `jichi-stub.sh'
;; (a stand-in binary) so no real jichi and no network are involved.
;;
;; Run with:
;;   emacs -Q --batch -L editors/emacs -L tests/elisp \
;;         -l tests/elisp/jichi-tests.el -f ert-run-tests-batch-and-exit

;;; Code:

(require 'ert)
(require 'jichi)

(defconst jichi-test--stub
  (expand-file-name "jichi-stub.sh"
                    (file-name-directory (or load-file-name buffer-file-name)))
  "Absolute path to the stub binary.")

(defmacro jichi-test--with-stub (mode &rest body)
  "Run BODY with `jichi-program' bound to the stub in MODE (a string)."
  (declare (indent 1))
  `(let ((jichi-program jichi-test--stub)
         (jichi-no-session t)
         (jichi-default-args '("-q"))
         (jichi-model nil)
         (process-environment (cons (concat "JICHI_STUB_MODE=" ,mode)
                                    process-environment)))
     ,@body))

(defun jichi-test--drain (proc)
  "Pump events until PROC dies and its sentinel has had a chance to run."
  (while (process-live-p proc) (accept-process-output proc 0.05))
  (dotimes (_ 20) (accept-process-output nil 0.02) (sit-for 0)))

;;;; Pure helpers -------------------------------------------------------------

(ert-deftest jichi-test-build-args ()
  (let ((jichi-default-args '("-q")) (jichi-no-session t) (jichi-model nil))
    (let ((a (jichi--build-args :readonly t)))
      (should (equal (last a 2) '("-p" "-")))      ; always ends in -p -
      (should (member "--readonly" a))
      (should (member "--no-session" a))
      (should (member "-q" a))
      (should-not (member "--auto" a)))
    (let ((a (jichi--build-args :auto t :json t)))
      (should (member "--auto" a))
      (should (member "--output" a))
      (should-not (member "--readonly" a)))
    (let ((jichi-model "fast") (jichi-no-session nil))
      (let ((a (jichi--build-args)))
        (should (equal (member "--model" a) '("--model" "fast" "-p" "-")))
        (should-not (member "--no-session" a))))))

(ert-deftest jichi-test-compose ()
  (with-temp-buffer
    (emacs-lisp-mode)
    (let ((jichi-include-file-context t))
      (let ((p (jichi--compose "do it" "(foo)" nil)))
        (should (string-prefix-p "do it" p))
        (should (string-match-p "major mode emacs-lisp-mode" p))
        (should (string-match-p "```elisp\n(foo)\n```" p))
        (should-not (string-match-p "Output ONLY" p)))     ; not clean
      (let ((p (jichi--compose "do it" "(foo)" t)))
        (should (string-match-p "Output ONLY" p))))         ; clean directive
    (let ((jichi-include-file-context nil))
      (should-not (string-match-p "major mode"
                                  (jichi--compose "x" "y" nil))))))

(ert-deftest jichi-test-defence ()
  ;; Unwrap a single surrounding fence.
  (should (equal (jichi--defence "```elisp\n(a)\n```") "(a)"))
  (should (equal (jichi--defence "```\nplain\n```") "plain"))
  ;; Drop a leading lead-in line when a fence follows.
  (should (equal (jichi--defence "Here is the code:\n```\n(b)\n```") "(b)"))
  ;; Leave plain text untouched.
  (should (equal (jichi--defence "just text") "just text"))
  ;; Leave a mid-block fence (not a whole-string wrapper) intact.
  (let ((s "line\n```\ncode\n```\nmore"))
    (should (equal (jichi--defence s) s))))

(ert-deftest jichi-test-lang-token ()
  (with-temp-buffer (emacs-lisp-mode)
    (should (equal (jichi--lang-token) "elisp")))
  (with-temp-buffer (fundamental-mode)
    (should (equal (jichi--lang-token) ""))))

;;;; Dispositions (against the stub) ------------------------------------------

(ert-deftest jichi-test-replace ()
  (jichi-test--with-stub "answer"
    (with-temp-buffer
      (emacs-lisp-mode)
      (insert "OLD-TEXT")
      (set-mark (point-min)) (goto-char (point-max))
      (activate-mark)
      (let ((proc (jichi--start "rewrite" 'replace :readonly t)))
        (jichi-test--drain proc))
      ;; The stub's fenced answer is de-fenced into the buffer.
      (should (equal (string-trim (buffer-string)) "(stub-answer)")))))

(ert-deftest jichi-test-display ()
  (jichi-test--with-stub "answer"
    (with-temp-buffer
      (emacs-lisp-mode)
      (insert "context")
      (let ((proc (jichi--start "explain" 'display :readonly t)))
        (jichi-test--drain proc)))
    (with-current-buffer jichi-output-buffer-name
      (should (string-match-p "(stub-answer)" (buffer-string)))
      (should (string-match-p "explain" (buffer-string))))))  ; echoed prompt

(ert-deftest jichi-test-insert ()
  (jichi-test--with-stub "answer"
    (with-temp-buffer
      (emacs-lisp-mode)
      (insert "AB")
      (goto-char (1+ (point-min)))            ; between A and B
      (let ((proc (jichi--start "gen" 'insert :readonly t)))
        (jichi-test--drain proc))
      ;; Inserted between A and B; insert does not de-fence.
      (should (string-match-p "A.*stub-answer.*B"
                              (replace-regexp-in-string "\n" " "
                                                        (buffer-string)))))))

(ert-deftest jichi-test-error-path ()
  (jichi-test--with-stub "error"
    (with-temp-buffer
      (emacs-lisp-mode)
      (insert "KEEP")
      (set-mark (point-min)) (goto-char (point-max)) (activate-mark)
      (let ((proc (jichi--start "rewrite" 'replace :readonly t)))
        (jichi-test--drain proc))
      ;; A failed run must not corrupt the buffer.
      (should (equal (buffer-string) "KEEP")))
    (should (get-buffer "*jichi errors*"))))

(ert-deftest jichi-test-stdin-reaches-binary ()
  ;; Echo mode returns the prompt; prove the composed prompt reached stdin.
  (jichi-test--with-stub "echo"
    (with-temp-buffer
      (emacs-lisp-mode)
      (insert "PAYLOAD-TOKEN")
      (let ((proc (jichi--start "ask" 'display :readonly t)))
        (jichi-test--drain proc)))
    (with-current-buffer jichi-output-buffer-name
      (should (string-match-p "PAYLOAD-TOKEN" (buffer-string)))
      (should (string-match-p "ask" (buffer-string))))))

(provide 'jichi-tests)

;;; jichi-tests.el ends here
