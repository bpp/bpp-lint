;;; bpp-ctl-mode.el --- Major mode for BPP control files  -*- lexical-binding: t; -*-

;; Copyright (C) 2026 Bruce Rannala

;; Author: Bruce Rannala
;; Keywords: languages, bioinformatics, bpp
;; Package-Requires: ((emacs "26.1"))
;; URL: https://github.com/bpp/bpp-lint

;;; Commentary:

;; A major mode for BPP (Bayesian Phylogenetics & Phylogeography) control
;; files (`*.ctl', `*.bpp.ctl').
;;
;; Provides:
;;
;;   * Syntax highlighting for the canonical BPP 4.x keyword set,
;;     `*'/`#' line comments, distribution names (invgamma / gamma /
;;     beta), and numeric literals.
;;
;;   * On-the-fly linting via Flymake.  The backend shells out to the
;;     `bpp-lint' binary (https://github.com/bpp/bpp-lint) on each save
;;     or idle pause, parses its `path:line:col: severity [code]:
;;     message' output, and reports diagnostics in the buffer.  Multi-
;;     line `note:' and `fix:' continuation lines are folded into the
;;     diagnostic text.
;;
;; Install by adding the following to your init file:
;;
;;     (add-to-list 'load-path "/path/to/bpp-lint/emacs")
;;     (require 'bpp-ctl-mode)
;;
;; The mode auto-activates for buffers visiting `*.ctl' or `*.bpp.ctl'.
;; `bpp-lint' must be on `exec-path' (or set `bpp-ctl-lint-program' to
;; its full path) for Flymake checking to run.

;;; Code:

(require 'flymake)
(require 'rx)

(defgroup bpp-ctl nil
  "Editing BPP control files."
  :group 'languages
  :prefix "bpp-ctl-")

(defcustom bpp-ctl-lint-program "bpp-lint"
  "Name of, or path to, the bpp-lint executable.
Resolved via `executable-find' if not absolute."
  :type 'string
  :group 'bpp-ctl)

(defcustom bpp-ctl-lint-arguments '("--color=never" "--codes" "--no-defaults")
  "Extra command-line arguments passed to `bpp-ctl-lint-program'.
`--no-defaults' is on by default to suppress the (very chatty) BPP103
\"using default\" warnings; remove it if you want them flagged inline."
  :type '(repeat string)
  :group 'bpp-ctl)


;;;; Syntax highlighting --------------------------------------------------

;; Keyword sets mirror src/keywords.c.  When that table grows, append the
;; new symbols below and rebuild `bpp-ctl-font-lock-keywords' by reloading
;; the mode.

(defconst bpp-ctl--keywords-inference
  '("seed" "arch" "nloci" "print" "model" "clock" "phase"
    "burnin" "wprior" "seqfile" "jobname" "usedata" "nsample"
    "scaling" "threads" "imapfile" "datefile" "tauprior" "heredity"
    "finetune" "sampfreq" "phiprior" "geneflow" "cleandata"
    "locusrate" "migration" "traitfile" "thetaprior" "checkpoint"
    "alphaprior" "thetamodel" "printlocus" "speciestree"
    "loadbalance" "species&tree" "constraintfile" "bayesfactorbeta"
    "debug_migration" "speciesmodelprior" "speciesdelimitation")
  "BPP 4.x inference-mode keywords.")

(defconst bpp-ctl--keywords-simulate
  '("qrates" "seqerr" "treefile" "seqdates" "basefreqs"
    "concatfile" "loci&length" "modelparafile" "alpha_siterate")
  "BPP 4.x --simulate keywords.")

(defconst bpp-ctl--distributions
  '("invgamma" "gamma" "beta" "dir" "iid")
  "Distribution / prior-shape names that appear inside value tokens.")

(defconst bpp-ctl-font-lock-keywords
  (let* ((keys (append bpp-ctl--keywords-inference
                       bpp-ctl--keywords-simulate))
         ;; regexp-opt with `symbols' wraps the alternation in symbol
         ;; boundaries, but `&' is not a symbol char by Emacs's default
         ;; rules, so we anchor manually instead.
         (keyword-re
          (concat "\\(?:^\\|[ \t]\\)"
                  "\\("
                  (mapconcat #'regexp-quote keys "\\|")
                  "\\)"
                  "[ \t]*="))
         (dist-re
          (concat "\\_<\\("
                  (mapconcat #'regexp-quote bpp-ctl--distributions "\\|")
                  "\\)\\_>")))
    `(
      ;; LHS of `=' is a known keyword.
      (,keyword-re 1 font-lock-keyword-face)

      ;; Distribution / prior shape names.
      (,dist-re 1 font-lock-function-name-face)

      ;; Numeric literals (int / float / scientific).
      ("\\_<-?[0-9]+\\(?:\\.[0-9]+\\)?\\(?:[eE][+-]?[0-9]+\\)?\\_>"
       . font-lock-constant-face)

      ;; The `=' itself, for emphasis.
      ("=" . font-lock-builtin-face)))
  "Font-lock rules for `bpp-ctl-mode'.")

(defvar bpp-ctl-mode-syntax-table
  (let ((st (make-syntax-table)))
    ;; `*' and `#' start line comments terminated by newline.
    (modify-syntax-entry ?*  "<" st)
    (modify-syntax-entry ?#  "<" st)
    (modify-syntax-entry ?\n ">" st)
    ;; Allow `&', `_', `-' inside keyword / identifier symbols.
    (modify-syntax-entry ?&  "_" st)
    (modify-syntax-entry ?_  "_" st)
    (modify-syntax-entry ?-  "_" st)
    st)
  "Syntax table for `bpp-ctl-mode'.")


;;;; Flymake backend ------------------------------------------------------

(defvar-local bpp-ctl--flymake-proc nil
  "Currently running bpp-lint process for this buffer, if any.")

(defun bpp-ctl--severity (s)
  "Map a bpp-lint severity string S onto a Flymake severity keyword."
  (pcase s
    ("error"   :error)
    ("warning" :warning)
    (_         :note)))

(defun bpp-ctl--diag-region (source line col)
  "In SOURCE buffer, return (BEG . END) for a diagnostic at LINE / COL.
END extends from COL to the next whitespace on the same line, matching
the \"token the user typed\" heuristic the linter targets."
  (with-current-buffer source
    (save-excursion
      (save-restriction
        (widen)
        (goto-char (point-min))
        (forward-line (1- line))
        (let* ((line-start (point))
               (line-end   (line-end-position))
               (beg        (min (+ line-start (1- col)) line-end))
               (end        (save-excursion
                             (goto-char beg)
                             (skip-chars-forward "^ \t\n" line-end)
                             (if (= (point) beg)
                                 (min (1+ beg) line-end)
                               (point)))))
          (cons beg end))))))

(defun bpp-ctl--parse-output (output source temp-file)
  "Parse bpp-lint OUTPUT (run against TEMP-FILE) into Flymake diagnostics.
Anchors diagnostics to positions inside SOURCE buffer.  Folds
`note:' / `fix:' continuation lines into the diagnostic message."
  (let* ((lines (split-string output "\n"))
         (n     (length lines))
         (path-re (regexp-quote temp-file))
         (line-anchored-re
          (concat "\\`" path-re ":\\([0-9]+\\):\\([0-9]+\\): "
                  "\\(error\\|warning\\|info\\)"
                  "\\(?: \\(\\[[^]]+\\]\\)\\)?: \\(.*\\)\\'"))
         (idx 0)
         (diags '()))
    (while (< idx n)
      (let ((line (nth idx lines)))
        (if (string-match line-anchored-re line)
            (let* ((ln       (string-to-number (match-string 1 line)))
                   (col      (string-to-number (match-string 2 line)))
                   (sev-str  (match-string 3 line))
                   (code     (match-string 4 line))
                   (msg      (match-string 5 line))
                   (full     (if code (concat code " " msg) msg)))
              ;; Fold continuation lines.
              (setq idx (1+ idx))
              (while (and (< idx n)
                          (let ((c (nth idx lines)))
                            (when (string-match
                                   "\\`  \\(note\\|fix\\):[ ]+\\(.*\\)\\'" c)
                              (setq full
                                    (concat full "\n  "
                                            (match-string 1 c) ": "
                                            (match-string 2 c)))
                              t)))
                (setq idx (1+ idx)))
              (let* ((region (bpp-ctl--diag-region source ln col)))
                (push (flymake-make-diagnostic
                       source (car region) (cdr region)
                       (bpp-ctl--severity sev-str)
                       full)
                      diags)))
          (setq idx (1+ idx)))))
    (nreverse diags)))

(defun bpp-ctl-flymake-backend (report-fn &rest _args)
  "Flymake backend that lints the current buffer with `bpp-ctl-lint-program'.
REPORT-FN is called with the list of diagnostics."
  (unless (executable-find bpp-ctl-lint-program)
    (error "bpp-ctl-mode: cannot find executable %S" bpp-ctl-lint-program))

  ;; Cancel an earlier in-flight check.
  (when (process-live-p bpp-ctl--flymake-proc)
    (kill-process bpp-ctl--flymake-proc))

  (let* ((source    (current-buffer))
         (temp-file (make-temp-file "bpp-ctl-flymake" nil ".ctl"))
         (out-buf   (generate-new-buffer " *bpp-ctl-flymake-out*")))
    ;; Save the buffer contents to the temp file (Flymake invokes us on
    ;; every keystroke or idle, so the on-disk file may not match).
    (save-restriction
      (widen)
      (write-region (point-min) (point-max) temp-file nil 'silent))
    (setq bpp-ctl--flymake-proc
          (make-process
           :name "bpp-ctl-flymake"
           :noquery t
           :connection-type 'pipe
           :buffer out-buf
           :command (append (list bpp-ctl-lint-program)
                            bpp-ctl-lint-arguments
                            (list temp-file))
           :sentinel
           (lambda (proc _event)
             (when (memq (process-status proc) '(exit signal))
               (unwind-protect
                   (when (with-current-buffer source
                           (eq proc bpp-ctl--flymake-proc))
                     (with-current-buffer out-buf
                       ;; bpp-lint writes diagnostics to stderr; with
                       ;; `:stderr' omitted, `make-process' merges stderr
                       ;; into the main buffer, so OUT-BUF has both.
                       (let* ((output (buffer-string))
                              (diags  (bpp-ctl--parse-output
                                       output source temp-file)))
                         (funcall report-fn diags))))
                 (kill-buffer out-buf)
                 (when (file-exists-p temp-file)
                   (delete-file temp-file)))))))))


;;;; Mode definition ------------------------------------------------------

;;;###autoload
(define-derived-mode bpp-ctl-mode prog-mode "BPP-CTL"
  "Major mode for editing BPP (Bayesian Phylogenetics) control files.

\\{bpp-ctl-mode-map}"
  :syntax-table bpp-ctl-mode-syntax-table
  ;; Case-insensitive keyword matching (BPP itself is case-insensitive on
  ;; option names).
  (setq-local font-lock-defaults '(bpp-ctl-font-lock-keywords nil t))
  (setq-local comment-start      "* ")
  (setq-local comment-end        "")
  (setq-local comment-start-skip "[*#]+[ \t]*")
  (setq-local comment-use-syntax t)
  ;; Wire bpp-lint into Flymake and enable it.
  (add-hook 'flymake-diagnostic-functions
            #'bpp-ctl-flymake-backend nil t)
  (flymake-mode 1))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.bpp\\.ctl\\'" . bpp-ctl-mode))
;;;###autoload
(add-to-list 'auto-mode-alist '("\\.ctl\\'"        . bpp-ctl-mode))

(provide 'bpp-ctl-mode)
;;; bpp-ctl-mode.el ends here
