;;; org-checks.el --- measure the claims docs/ORG_MODE.md makes  -*- lexical-binding: t; -*-

;; Run by tests/smoke/org_mode_lint.sh against fixtures extracted from the
;; page itself:
;;
;;     emacs -Q --batch --load org-checks.el <fixture-dir> < /dev/null
;;
;; It prints KEY=VALUE lines to STDOUT and nothing else.  It deliberately
;; contains NO expected values: the expectations live in the visible prose of
;; docs/ORG_MODE.md, and the shell driver scrapes them from there.  Putting
;; them here instead would let the sentence a reader actually reads be wrong
;; while the test stayed green -- which is the whole failure this file exists
;; to prevent.
;;
;; stdout vs stderr matters: `princ' writes to stdout, `message' to stderr,
;; and org chatters on stderr (`org-todo' announces every state change).  The
;; driver reads stdout only.
;;
;; A measurement that dies must not look like a measurement that passed, so
;; every probe is wrapped and failures are printed as KEY=ERROR:... -- the
;; driver treats any of those as a failed check rather than a missing one.

(require 'cl-lib)
(require 'org)
(require 'org-agenda)
(require 'org-capture)
(require 'org-lint)
(require 'ob-tangle)

(defvar oc-dir (or (car command-line-args-left) default-directory))

;; Isolation: no persisted state, no id database, no element cache.  Without
;; this a second run can differ from the first, which would make every failure
;; here unreproducible.
(setq org-persist-directory (expand-file-name "persist" oc-dir)
      org-id-locations-file (expand-file-name "id-locations" oc-dir)
      org-element-use-cache nil
      org-confirm-babel-evaluate nil
      make-backup-files nil)

(defun oc-emit (key value)
  (princ (format "%s=%s\n" key value)))

(defmacro oc-probe (key &rest body)
  "Emit KEY from BODY, or KEY=ERROR:... if BODY signals."
  (declare (indent 1))
  `(condition-case err
       (oc-emit ,key (progn ,@body))
     (error (oc-emit ,key (format "ERROR:%s" (error-message-string err))))))

(defun oc-file (name) (expand-file-name name oc-dir))

(defun oc-words (list) (mapconcat #'identity list " "))

(defun oc-agenda-count ()
  "Entries in the current agenda buffer.  Only lines carrying an
`org-marker' are entries; headers, separators and blank lines are not."
  (with-current-buffer org-agenda-buffer-name
    (length (delq nil
                  (mapcar (lambda (l)
                            (and (> (length l) 0)
                                 (get-text-property 0 'org-marker l)
                                 l))
                          (split-string (buffer-string) "\n"))))))

;; --- what an unconfigured Emacs gives you ------------------------------------
;; Measured BEFORE any fixture is opened: these are the defaults a reader gets
;; with no init file at all, which is the premise of the page's first section.
(oc-probe "DEFAULT_KEYWORDS"
  (oc-words (cdr (car (default-value 'org-todo-keywords)))))
(oc-probe "DEFAULT_AGENDA"
  (if (default-value 'org-agenda-files) "set" "nil"))
(oc-probe "PRIO"
  (format "%c..%c default %c"
          org-highest-priority org-lowest-priority org-default-priority))

;; --- the fixture's own vocabulary --------------------------------------------
(with-current-buffer (find-file-noselect (oc-file "project.org"))
  (oc-probe "LINT" (length (org-lint)))
  (oc-probe "STATES" (oc-words org-todo-keywords-1))
  (oc-probe "NOTDONE" (oc-words org-not-done-keywords))
  (oc-probe "DONEKW" (oc-words org-done-keywords))
  ;; The ORDER, not the set: a set comparison passes on a shuffled sequence,
  ;; and the order is the thing a reader presses C-c C-t against.
  ;;
  ;; In a SCRATCH COPY, because `org-todo' edits the buffer and `save-excursion'
  ;; restores only point.  Cycling in place left the first heading parked on
  ;; WAITING and the Q_STATE probe below then counted two of them -- a
  ;; measurement corrupted by an earlier measurement, which is worse than no
  ;; measurement because it looks like a finding.
  (oc-probe "CYCLE"
    (let ((text (buffer-string)))
      (with-temp-buffer
        (insert text)
        (org-mode)
        (goto-char (point-min))
        (re-search-forward "^\\* ")
        (let (seq)
          (dotimes (_ 7)
            (push (or (org-get-todo-state) "(none)") seq)
            (org-todo))
          (mapconcat #'identity (reverse seq) ">")))))
  (oc-probe "Q_TAG"
    (length (org-map-entries #'ignore "+decision" 'file)))
  (oc-probe "Q_STATE"
    (length (org-map-entries #'ignore "TODO=\"WAITING\"" 'file)))
  (oc-probe "Q_NOTDONE"
    (length (org-map-entries #'ignore "/!" 'file)))
  (oc-probe "PROP_REJECTED"
    (save-excursion
      (goto-char (point-min))
      (re-search-forward "^\\*+ One config file")
      (or (org-entry-get (point) "REJECTED") "(none)"))))

;; --- the agenda --------------------------------------------------------------
;; AGENDA_EMPTY is the control.  Without it a broken count that always returns
;; the same number would still match the page.
;;
;; `org-today' is FROZEN to the authoring day (2026-08-07, outside both pinned
;; windows).  The window start is pinned, but org attaches deadline WARNINGS
;; and overdue carry-forwards to TODAY's line whenever the real today falls
;; inside the requested span -- so an unfrozen probe counted 3 until
;; 2026-08-09 and 4 from 2026-08-10 (the DEADLINE <2026-08-14> entry appears
;; a second time as "In 4 d.:" on today's line).  A measurement that changes
;; with the calendar is not a measurement of the page.
(setq org-agenda-files (list (oc-file "project.org")))
(cl-letf (((symbol-function 'org-today)
           (lambda () (time-to-days (encode-time 0 0 12 7 8 2026)))))
  (oc-probe "AGENDA_N"
    (progn (org-agenda-list nil "2026-08-10" 7) (oc-agenda-count)))
  (oc-probe "AGENDA_EMPTY"
    (progn (org-agenda-list nil "2026-09-14" 7) (oc-agenda-count))))

;; --- tangling ----------------------------------------------------------------
(oc-probe "SRCBLOCKS"
  (with-current-buffer (find-file-noselect (oc-file "literate.org"))
    (length (org-babel-tangle-collect-blocks))))
(oc-probe "TANGLE"
  (with-current-buffer (find-file-noselect (oc-file "literate.org"))
    (let ((default-directory oc-dir))
      (oc-words (mapcar #'file-name-nondirectory (org-babel-tangle))))))
(oc-probe "TANGLE_BYTES"
  (nth 7 (file-attributes (oc-file "records.c"))))

;; --- capture into a datetree -------------------------------------------------
;; The page warns that a diary entry must go through capture.  DATETREE_UNDER
;; is what makes that warning checkable: hand-inserting after
;; `org-datetree-find-date-create' files the entry ABOVE the day heading, and
;; only a position check can tell the two apart.
(let ((journal (oc-file "journal.org")))
  (with-temp-file journal (insert "#+TITLE: Journal\n* Diary\n"))
  (setq org-capture-templates
        (list (list "j" "journal" 'entry
                    (list 'file+olp+datetree journal "Diary")
                    "* a surprise worth keeping" :immediate-finish t)))
  (oc-probe "CAPTURE"
    (let ((org-overriding-default-time (encode-time 0 0 12 7 8 2026)))
      (org-capture nil "j")
      "ok"))
  (with-current-buffer (find-file-noselect journal)
    (oc-probe "DATETREE_DAY"
      (save-excursion
        (goto-char (point-min))
        (if (re-search-forward "^\\*\\{4\\} \\(.*\\)$" nil t)
            (match-string 1)
          "(no day heading)")))
    (oc-probe "DATETREE_UNDER"
      (save-excursion
        (goto-char (point-min))
        (if (re-search-forward "^\\*\\{4\\} [0-9]" nil t)
            (if (re-search-forward "^\\*\\{5\\} " nil t) "yes" "no")
          "no-day")))))
