;;; jichi.el --- Emacs integration for jichi  -*- lexical-binding: t; -*-

;; Copyright (C) 2026  Alexander-Lars Dallmann
;; Author: Alexander-Lars Dallmann
;; Version: 0.1

;; License: jichi's licence is not yet chosen -- see the repository's README.
;; This header carried `SPDX-License-Identifier: MIT`, a 2025 copyright, an
;; author of "jichi contributors" (there is one) and a URL of
;; https://example.invalid/jichi. All four were placeholders from the file's
;; first draft, and together they were the only licence claim in the tree that
;; contradicted the project's stated position. A wrong SPDX tag is worse than an
;; absent one: it is machine-readable, so a scanner would have reported this
;; package as MIT-licensed with confidence.
;; Package-Requires: ((emacs "25.1"))
;; Keywords: tools, convenience, ai

;;; Commentary:

;; Send a marked region (or the whole buffer) to the `jichi' command-line
;; agent and receive the result back into Emacs -- in a side buffer, at point,
;; appended, or replacing the region.
;;
;; The integration drives jichi's *headless* mode: it composes a prompt, pipes
;; on the process's stdin (`jichi -p -'), streams the answer back from
;; stdout, and keeps diagnostics (stderr) separate.  No network code lives here;
;; jichi does the talking.
;;
;; Quick start:
;;
;;   (require 'jichi)
;;   (global-jichi-mode 1)        ; bind the commands under C-c j everywhere
;;
;; then mark a region and:
;;
;;   C-c j j   ask about it / transform it -> *jichi* buffer
;;   C-c j r   replace the region with the answer
;;   C-c j i   insert an answer at point
;;   C-c j b   send the whole buffer
;;   C-c j a   ask a question about the region
;;   C-c j t   run an agentic task (jichi may edit files on disk)
;;   C-c j C-g cancel the running request
;;
;; The text-transform commands always pass `--readonly', so jichi can read and
;; answer but never changes files on disk.  Only `jichi-task' (which asks first)
;; runs with `--auto' and may edit the project.
;;
;; See docs/EMACS.md in the jichi repository for the full guide.

;;; Code:

(require 'cl-lib)
(require 'project nil t)        ; optional: workspace-root detection
(require 'markdown-mode nil t)  ; optional: nicer *jichi* rendering

(declare-function project-current "project" (&optional maybe-prompt directory))
(declare-function project-root "project" (project))

(defgroup jichi nil
  "Drive the jichi agent from Emacs."
  :group 'tools
  :prefix "jichi-")

;;;; Customization -----------------------------------------------------------

(defcustom jichi-program "jichi"
  "Name or absolute path of the jichi executable.
Resolved with `executable-find' at call time, so a bare name on PATH works."
  :type 'string
  :group 'jichi)

(defcustom jichi-default-args '("-q")
  "Arguments always passed to jichi.
The default `-q' keeps stderr to genuine errors only."
  :type '(repeat string)
  :group 'jichi)

(defcustom jichi-model nil
  "Model selector passed via `--model', or nil for jichi's configured default."
  :type '(choice (const :tag "jichi config default" nil) string)
  :group 'jichi)

(defcustom jichi-no-session t
  "When non-nil, pass `--no-session' so editor one-shots are not persisted."
  :type 'boolean
  :group 'jichi)

(defcustom jichi-include-file-context t
  "When non-nil, tell jichi the buffer's file name and major mode."
  :type 'boolean
  :group 'jichi)

(defcustom jichi-output-buffer-name "*jichi*"
  "Name of the buffer used by the `display' disposition."
  :type 'string
  :group 'jichi)

(defcustom jichi-default-disposition 'display
  "Where `jichi-send-buffer' (and whole-buffer `jichi-dwim') put the answer."
  :type '(choice (const :tag "Side buffer" display)
                 (const :tag "At point" insert)
                 (const :tag "End of buffer" append))
  :group 'jichi)

(defcustom jichi-keymap-prefix "C-c j"
  "Prefix key for `jichi-mode' bindings.
Changing this takes effect when `jichi.el' is next loaded."
  :type 'string
  :group 'jichi)

(defface jichi-echo-face '((t :inherit shadow))
  "Face for the echoed prompt at the top of the *jichi* buffer."
  :group 'jichi)

;;;; Process registry + cancellation -----------------------------------------

(defvar jichi--active nil
  "List of live jichi processes, most-recent first.")

(defun jichi--register (proc) (push proc jichi--active))
(defun jichi--unregister (proc) (setq jichi--active (delq proc jichi--active)))

;;;###autoload
(defun jichi-cancel ()
  "Interrupt the most recent running jichi process.
Sends SIGINT, so jichi aborts cleanly (exit 130); does not kill it outright."
  (interactive)
  (let ((proc (cl-find-if #'process-live-p jichi--active)))
    (if proc
        (progn (interrupt-process proc)
               (message "jichi: interrupt sent"))
      (user-error "jichi: no active request"))))

;;;; Core async runner --------------------------------------------------------

(cl-defun jichi--run (&key prompt args directory on-delta on-finish
                           (name "jichi"))
  "Run jichi asynchronously.
PROMPT is written to the process stdin (then EOF).  ARGS is the argv tail.
DIRECTORY becomes the process working directory (jichi's workspace).  ON-DELTA,
if non-nil, is called with each stdout chunk as it arrives.  ON-FINISH is
called as (CODE STDOUT STDERR) when the process exits.  Returns the process."
  (let* ((program (or (executable-find jichi-program)
                      (user-error "jichi: program %S not found on PATH"
                                  jichi-program)))
         (default-directory (or directory default-directory))
         (stderr-buf (generate-new-buffer (format " *%s-stderr*" name)))
         (out-buf (generate-new-buffer (format " *%s-stdout*" name)))
         (proc
          (make-process
           :name name
           :command (cons program args)
           :connection-type 'pipe
           :coding '(utf-8 . utf-8)
           :noquery t
           :stderr stderr-buf
           :filter
           (lambda (_proc chunk)
             (when (buffer-live-p out-buf)
               (with-current-buffer out-buf (insert chunk)))
             (when on-delta (funcall on-delta chunk)))
           :sentinel
           (lambda (proc _event)
             (when (memq (process-status proc) '(exit signal))
               (let ((code (process-exit-status proc))
                     (out (if (buffer-live-p out-buf)
                              (with-current-buffer out-buf (buffer-string)) ""))
                     (err (if (buffer-live-p stderr-buf)
                              (with-current-buffer stderr-buf (buffer-string))
                            "")))
                 (jichi--unregister proc)
                 (unwind-protect
                     (when on-finish (funcall on-finish code out err))
                   (when (buffer-live-p out-buf) (kill-buffer out-buf))
                   (when (buffer-live-p stderr-buf)
                     (kill-buffer stderr-buf)))))))))
    (jichi--register proc)
    (process-send-string proc prompt)
    (process-send-eof proc)
    proc))

(defun jichi--build-args (&rest plist)
  "Assemble the jichi argv tail from PLIST keys and customization.
PLIST recognizes :json, :readonly, :auto, :plan, and :extra (a list of strings).
The result always ends in \"-p\" \"-\" so the prompt is read from stdin."
  (append jichi-default-args
          (when jichi-no-session '("--no-session"))
          (when jichi-model (list "--model" jichi-model))
          (when (plist-get plist :json) '("--output" "json"))
          (when (plist-get plist :readonly) '("--readonly"))
          (when (plist-get plist :auto) '("--auto"))
          (when (plist-get plist :plan) '("--plan"))
          (plist-get plist :extra)
          '("-p" "-")))

;;;; Prompt composition -------------------------------------------------------

(defun jichi--region-or-buffer ()
  "Return a cons (TEXT . BOUNDS).
TEXT is the active region, or the whole buffer when none is active.  BOUNDS is
the cons (BEG . END) for a region, or nil for the whole buffer."
  (if (use-region-p)
      (let ((beg (region-beginning)) (end (region-end)))
        (cons (buffer-substring-no-properties beg end) (cons beg end)))
    (cons (buffer-substring-no-properties (point-min) (point-max)) nil)))

(defun jichi--lang-token ()
  "Return a Markdown fence language token guessed from `major-mode'."
  (let ((name (symbol-name major-mode)))
    (cond
     ((string-match "\\`\\(.*?\\)-ts-mode\\'" name) (match-string 1 name))
     ((string-match "\\`\\(.*?\\)-mode\\'" name)
      (let ((base (match-string 1 name)))
        (cond ((string= base "emacs-lisp") "elisp")
              ((string= base "lisp-interaction") "elisp")
              ((member base '("fundamental" "text" "prog")) "")
              (t base))))
     (t ""))))

(defun jichi--compose (instruction text &optional clean)
  "Build the prompt string from INSTRUCTION and TEXT.
When CLEAN is non-nil, add a directive asking for raw output (for the replace
and insert dispositions, where the answer goes straight into the buffer)."
  (concat
   instruction "\n\n"
   (when jichi-include-file-context
     (format "Context: file %s, major mode %s.\n\n"
             (or (buffer-file-name) (buffer-name)) major-mode))
   (when clean
     (concat "Output ONLY the resulting text, no explanation, no preamble, "
             "and no surrounding Markdown code fences.\n\n"))
   (format "```%s\n%s\n```\n" (jichi--lang-token) text)))

;;;; Workspace ----------------------------------------------------------------

(defun jichi--project-root (&optional buffer)
  "Best-effort workspace directory for BUFFER (default current).
Tries project.el, then VC, then the file's directory, then `default-directory'."
  (with-current-buffer (or buffer (current-buffer))
    (or (and (fboundp 'project-current)
             (let ((proj (project-current nil)))
               (and proj (expand-file-name (project-root proj)))))
        (and (fboundp 'vc-root-dir) (vc-root-dir))
        (and buffer-file-name (file-name-directory buffer-file-name))
        default-directory)))

;;;; Output text helpers ------------------------------------------------------

(defun jichi--defence (s)
  "Return S with one surrounding code fence and a leading lead-in line removed.
Conservative: only unwraps a single fence that wraps the *whole* string, and
only drops a leading prose line when a fence immediately follows it."
  (let ((text (string-trim s)))
    ;; Drop a leading "Here is ...:" style line if a fence follows it.
    (when (string-match "\\`[^\n]*:[ \t]*\n[ \t]*```" text)
      (setq text (string-trim (substring text (1+ (string-match "\n" text))))))
    ;; Unwrap a single surrounding ```lang ... ``` fence.
    (when (string-match "\\````[^\n]*\n\\(\\(?:.\\|\n\\)*?\\)\n?```[ \t]*\\'"
                        text)
      (setq text (match-string 1 text)))
    text))

(defun jichi--report-error (code stderr)
  "Report a failed jichi run with exit CODE and STDERR text."
  (let* ((trimmed (string-trim (or stderr "")))
         (first (if (string-empty-p trimmed) "(no diagnostics)"
                  (car (split-string trimmed "\n")))))
    (when (not (string-empty-p trimmed))
      (with-current-buffer (get-buffer-create "*jichi errors*")
        (let ((inhibit-read-only t))
          (erase-buffer)
          (insert trimmed "\n"))
        (special-mode)))
    (message "jichi: failed (exit %d): %s%s" code first
             (if (string-empty-p trimmed) "" "  [see *jichi errors*]"))))

;;;; Display buffer ------------------------------------------------------------

(defvar jichi-output-mode-map
  (let ((m (make-sparse-keymap)))
    (define-key m (kbd "q") #'quit-window)
    (define-key m (kbd "g") #'jichi-cancel)
    m)
  "Keymap for `jichi-output-mode'.")

(define-derived-mode jichi-output-mode special-mode "jichi-output"
  "Read-only major mode for the *jichi* answer buffer.
When `markdown-mode' is available, its font-lock keywords are layered on for
light syntax highlighting of the answer."
  (setq buffer-read-only t)
  (when (require 'markdown-mode nil t)
    (when (boundp 'markdown-mode-font-lock-keywords)
      (setq font-lock-defaults '(markdown-mode-font-lock-keywords))
      (ignore-errors (font-lock-mode 1)))))

(defun jichi--display-buffer (echo)
  "Prepare and return the *jichi* display buffer, echoing ECHO at the top."
  (let ((buf (get-buffer-create jichi-output-buffer-name)))
    (with-current-buffer buf
      (jichi-output-mode)
      (let ((inhibit-read-only t))
        (erase-buffer)
        (insert (propertize (concat "> " echo "\n\n") 'face 'jichi-echo-face))))
    (display-buffer buf)
    buf))

(defun jichi--append-to (buf chunk)
  "Append CHUNK at the end of BUF (read-only-aware) and scroll its windows."
  (when (buffer-live-p buf)
    (with-current-buffer buf
      (let ((inhibit-read-only t))
        (save-excursion (goto-char (point-max)) (insert chunk)))
      (dolist (win (get-buffer-window-list buf nil t))
        (with-selected-window win (goto-char (point-max)))))))

;;;; Dispatch (disposition -> on-delta + on-finish) ---------------------------

(defun jichi--start (instruction disposition &rest run-keys)
  "Compose a prompt and start a jichi run for DISPOSITION.
INSTRUCTION is the user's request; the region/buffer text is appended.
RUN-KEYS are extra keys forwarded to `jichi--build-args' (e.g. :readonly t)."
  (let* ((rb (jichi--region-or-buffer))
         (text (car rb))
         (bounds (cdr rb))
         (src (current-buffer))
         (root (jichi--project-root))
         (clean (memq disposition '(replace insert append)))
         (prompt (jichi--compose instruction text clean))
         (args (apply #'jichi--build-args run-keys)))
    (when (and (eq disposition 'replace) (null bounds))
      (user-error "jichi: select a region to replace"))
    (pcase disposition
      ('display
       (let ((buf (jichi--display-buffer instruction)))
         (jichi--run
          :prompt prompt :args args :directory root
          :on-delta (lambda (chunk) (jichi--append-to buf chunk))
          :on-finish
          (lambda (code _out err)
            (cond ((zerop code) (message "jichi: done"))
                  ((= code 130) (message "jichi: interrupted"))
                  (t (jichi--report-error code err)))))))
      ((or 'insert 'append)
       (let ((marker (with-current-buffer src
                       (copy-marker (if (eq disposition 'append)
                                        (point-max) (point)) t))))
         (jichi--run
          :prompt prompt :args args :directory root
          :on-delta
          (lambda (chunk)
            (when (marker-buffer marker)
              (with-current-buffer (marker-buffer marker)
                (save-excursion (goto-char marker) (insert chunk)))))
          :on-finish
          (lambda (code _out err)
            (cond ((zerop code) (message "jichi: inserted"))
                  ((= code 130) (message "jichi: interrupted"))
                  (t (jichi--report-error code err)))
            (when (markerp marker) (set-marker marker nil))))))
      ('replace
       ;; Non-streaming: de-fencing needs the whole answer.
       (jichi--run
        :prompt prompt :args args :directory root
        :on-finish
        (lambda (code out err)
          (cond
           ((zerop code)
            (if (buffer-live-p src)
                (with-current-buffer src
                  (atomic-change-group
                    (kill-region (car bounds) (cdr bounds))
                    (goto-char (car bounds))
                    (insert (jichi--defence out)))
                  (message "jichi: replaced (yank to restore the original)"))
              (message "jichi: source buffer is gone")))
           ((= code 130) (message "jichi: interrupted"))
           (t (jichi--report-error code err))))))
      (_ (error "jichi: unknown disposition %S" disposition)))))

;;;; Commands ------------------------------------------------------------------

(defvar jichi--instruction-history nil
  "Minibuffer history for jichi instructions.")

(defun jichi--read-instruction (prompt &optional default)
  "Read an instruction from the minibuffer with PROMPT and optional DEFAULT."
  (read-string prompt nil 'jichi--instruction-history default))

;;;###autoload
(defun jichi-dwim (instruction)
  "Send the region (or whole buffer) to jichi with INSTRUCTION; show the answer.
Read-only: jichi never changes files.  The answer streams to *jichi*."
  (interactive (list (jichi--read-instruction "jichi (region/buffer): ")))
  (jichi--start instruction 'display :readonly t))

;;;###autoload
(defun jichi-ask-about-region (question)
  "Ask jichi a QUESTION about the region (or whole buffer); answer in *jichi*."
  (interactive (list (jichi--read-instruction "jichi ask: ")))
  (jichi--start question 'display :readonly t))

;;;###autoload
(defun jichi-send-buffer (instruction)
  "Send the buffer to jichi with INSTRUCTION, per `jichi-default-disposition'."
  (interactive (list (jichi--read-instruction "jichi (buffer): ")))
  (save-mark-and-excursion
    (deactivate-mark)
    (jichi--start instruction jichi-default-disposition :readonly t)))

;;;###autoload
(defun jichi-replace-region (instruction)
  "Transform the region with INSTRUCTION and replace it with jichi's answer.
The original text is pushed to the kill ring; one undo restores it.  Read-only
with respect to files on disk (the change is purely in this buffer)."
  (interactive (list (jichi--read-instruction "jichi replace: ")))
  (jichi--start instruction 'replace :readonly t))

;;;###autoload
(defun jichi-insert-at-point (instruction)
  "Generate text from INSTRUCTION (with the region as context) and insert it."
  (interactive (list (jichi--read-instruction "jichi insert: ")))
  (jichi--start instruction 'insert :readonly t))

(defun jichi--changed-buffers (snapshot)
  "Return file-backed buffers whose on-disk modtime differs from SNAPSHOT.
SNAPSHOT is an alist of (BUFFER . MODTIME) captured before the run."
  (cl-loop for (buf . mtime) in snapshot
           when (and (buffer-live-p buf) (buffer-file-name buf)
                     (file-exists-p (buffer-file-name buf))
                     (not (equal mtime (nth 5 (file-attributes
                                               (buffer-file-name buf))))))
           collect buf))

;;;###autoload
(defun jichi-task (instruction)
  "Run an agentic task: jichi may use tools and EDIT FILES on disk.
Asks for confirmation first, runs with `--auto', streams the transcript into
*jichi*, appends the tool log, and offers to revert buffers whose files moved."
  (interactive (list (jichi--read-instruction "jichi task: ")))
  (let* ((rb (jichi--region-or-buffer))
         (root (jichi--project-root))
         (prompt (jichi--compose instruction (car rb) nil))
         (args (jichi--build-args :auto t))
         (snapshot
          (cl-loop for b in (buffer-list)
                   when (and (buffer-file-name b)
                             (file-exists-p (buffer-file-name b)))
                   collect (cons b (nth 5 (file-attributes
                                           (buffer-file-name b))))))
         buf)
    (unless (yes-or-no-p
             (format "jichi-task may run tools and edit files in %s.  Go on? "
                     root))
      (user-error "jichi: cancelled"))
    (setq buf (jichi--display-buffer instruction))
    (jichi--run
     :prompt prompt :args args :directory root :name "jichi-task"
     :on-delta (lambda (chunk) (jichi--append-to buf chunk))
     :on-finish
     (lambda (code _out err)
       (when (and (stringp err) (not (string-empty-p (string-trim err))))
         (jichi--append-to buf (concat "\n\n## tool log\n\n" err)))
       (cond
        ((= code 130) (message "jichi: interrupted"))
        ((not (zerop code)) (jichi--report-error code err))
        (t
         (let ((changed (jichi--changed-buffers snapshot)))
           (if (and changed
                    (y-or-n-p (format "jichi changed %d file(s); revert? "
                                      (length changed))))
               (progn (dolist (b changed)
                        (with-current-buffer b
                          (revert-buffer 'ignore-auto 'noconfirm)))
                      (message "jichi: done; reverted %d buffer(s)"
                               (length changed)))
             (message "jichi: done")))))))))

;;;; Minor mode + keymap -------------------------------------------------------

(defvar jichi-command-map
  (let ((m (make-sparse-keymap)))
    (define-key m (kbd "j") #'jichi-dwim)
    (define-key m (kbd "r") #'jichi-replace-region)
    (define-key m (kbd "i") #'jichi-insert-at-point)
    (define-key m (kbd "b") #'jichi-send-buffer)
    (define-key m (kbd "a") #'jichi-ask-about-region)
    (define-key m (kbd "t") #'jichi-task)
    (define-key m (kbd "C-g") #'jichi-cancel)
    m)
  "Keymap of jichi commands, bound under `jichi-keymap-prefix'.")

(defvar jichi-mode-map
  (let ((m (make-sparse-keymap)))
    (define-key m (kbd jichi-keymap-prefix) jichi-command-map)
    m)
  "Keymap for `jichi-mode'.")

;;;###autoload
(define-minor-mode jichi-mode
  "Minor mode binding jichi commands under `jichi-keymap-prefix'."
  :lighter " jichi"
  :keymap jichi-mode-map)

;;;###autoload
(define-globalized-minor-mode global-jichi-mode jichi-mode
  (lambda () (jichi-mode 1)))

(provide 'jichi)

;;; jichi.el ends here
