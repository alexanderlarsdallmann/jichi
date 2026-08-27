# Emacs org-mode for a software project

> **Prerequisite:** [PROJECT_RECORDS.md](PROJECT_RECORDS.md). That page teaches
> the practice — capture, plan, decide, defer, record, review — in plain
> markdown, and the practice is the thing that matters. This page mechanises
> the same six jobs in org-mode. If you have not kept records for a few weeks
> yet, keep them in markdown first; a tool cannot give you a habit.

Org is a plain-text format that the editor understands. The file stays a file
— you can `cat` it, `grep` it, commit it, and read it on a machine with no
Emacs at all — but inside Emacs the headings become states it can cycle, the
dates become an agenda it can build, and the drawers become properties it can
query. That is the entire pitch: **the same text, with a reader that knows
what the text means.**

## What this is not

It is not required. jichi ships no `.org` files, scaffolds none, and never
will — every pack and every example is markdown on purpose, because markdown
asks nothing of you and org asks for Emacs.

And it is not free, though it is often described that way. Org is **not** a
separate install: it has shipped *inside* Emacs since 2006, so on this machine
it is `/usr/share/emacs/29.3/lisp/org/` in the `emacs-common` package, already
there. The price is Emacs itself:

| | installed size |
| --- | --- |
| `emacs-common` + `emacs-gtk` | **~119 MB** |
| `vim` | 4.1 MB |
| `vim-tiny` | 1.8 MB |
| `nano` | 836 KB |

About **140× nano**. If you already run Emacs, org costs you nothing and this
page is pure profit. If you do not, org is not a reason to install Emacs —
the practice in [PROJECT_RECORDS.md](PROJECT_RECORDS.md) runs in `nano` on a
box with 4 GB of RAM, and it is the same practice.

| | section | you get |
| --- | --- | --- |
| §0 | [What you get unconfigured](#0-what-you-get-unconfigured) | the honest baseline, with no init file |
| §1 | [Capture](#1-capture) | one keystroke from thought to file |
| §2 | [Plan](#2-plan) | states the editor cycles, dates it can agenda |
| §3 | [Decide](#3-decide) | properties, and a register you can query |
| §4 | [Defer](#4-defer) | `WAITING`, and what unblocks it |
| §5 | [Record](#5-record) | a dated journal that files itself |
| §6 | [Review](#6-review) | the queries that replace five `grep`s |
| §7 | [Literate files](#7-literate-files-the-one-thing-markdown-cannot-do) | code that lives inside its own explanation |
| §8 | [jichi and `.org`](#8-jichi-and-org) | what the agent sees |
| §9 | [The whole init](#9-the-whole-init) | twelve lines, and why so few |
| §10 | [Troubleshooting](#10-troubleshooting) | the four errors everybody hits |

---

## 0. What you get unconfigured

Start here, because most org tutorials start with a page of configuration and
leave you unable to tell what is org and what is theirs.

Everything below is measured on **GNU Emacs 29.3 with org 9.6.15**, started as
`emacs -Q` — no init file, no packages, nothing of yours. This page's own
examples are re-measured by a test on every change, so what follows is not
remembered, it is checked.

Out of the box:

- **default states: TODO DONE** — that is the whole vocabulary. `NEXT`,
  `WAITING` and `PROJECT` are conventions from other people's configs; typed
  into a heading unconfigured they are not states at all, just words, and the
  heading stays plain.
- **org-agenda-files defaults to nil** — the agenda exists and is empty,
  because nothing has told it which files to read. This is the single most
  common "org is broken" report, and it is org doing exactly what it was told.
- **priorities A..C default B** — `[#A]` works with no setup.
- `org-capture-templates` is **not even bound** until something loads
  `org-capture`. See §10.

The good news in that list is the second half: the two things you must
configure are the two things that cannot have a sensible default, because only
you know where your files are.

---

## 1. Capture

Capture is org's answer to §1 of the practice page: a thought costs one
keystroke, lands in a known place, and does not interrupt what you were doing.

The template is three parts — a key, where it goes, and what it looks like:

<!-- fragment -->
```elisp
(setq org-capture-templates
      '(("i" "inbox" entry (file "~/records/project.org")
         "* %? \n  :PROPERTIES:\n  :Captured: %U\n  :END:")))
(global-set-key (kbd "C-c c") #'org-capture)
```

`C-c c i`, type the line, `C-c C-c`. `%?` is where the cursor lands; `%U` is
an inactive timestamp — inactive because a captured thought is not an
appointment, and an active one would show up in your agenda forever.

**The path is not relative to your project.** A bare `"project.org"` resolves
against `org-directory` (`~/org/` by default), not the directory you are in,
and the error you get is `Heading not found on level 1` — which sounds like
the heading is wrong when the *file* is wrong. Write the path out in full.
This is the first mistake I made writing this page.

### 1.1 Recommendations

- **One capture template until you resent it.** Six templates means choosing,
  and choosing is the friction capture exists to remove.
- **Capture to one file, refile later.** `C-c C-w` moves a heading elsewhere
  once you know where it belongs; deciding at capture time defeats the point.

---

## 2. Plan

A heading whose first word is a known state is a task. You declare the states
**in the file** — not in your init — so the file explains itself to anyone who
opens it, including the you who has forgotten:

<!-- fragment -->
```org
#+TODO: TODO NEXT WAITING | DONE DROPPED
```

The `|` is the only syntax here, and it is load-bearing: everything left of it
is unfinished, everything right of it is finished. That single character is
what lets org answer "what is still open" without you listing states.

Measured on the example file below:

- **states: TODO NEXT WAITING DONE DROPPED**
- **unfinished: TODO NEXT WAITING**
- **finished: DONE DROPPED**

`C-c C-t` cycles the state of the heading at point, in the declared order,
through no-state and round again:

- **cycle: NEXT → WAITING → DONE → DROPPED → (none) → TODO → NEXT**

That the empty state is *in* the cycle is deliberate: not every heading is a
task, and the way to say so is to cycle past the end.

### 2.1 Dates the editor understands

Two, and the difference is worth learning properly:

<!-- fragment -->
```org
* NEXT trailing-comma parse
  DEADLINE: <2026-08-14 Fri>
* NEXT write the launcher script
  SCHEDULED: <2026-08-11 Tue>
```

**`SCHEDULED` is when you intend to start. `DEADLINE` is when it must be
done.** Most people use `DEADLINE` for both and then find their agenda
shouting at them for a fortnight, because a deadline warns in advance by
design and a scheduled date does not.

The angle brackets make the date *active* — it appears in the agenda. Square
brackets make it inactive, which is what `CLOSED:` uses: a record, not a
summons.

### 2.2 The agenda

The agenda is a view, built on demand from the files you name. Point it at
your project and ask for a week:

<!-- fragment -->
```elisp
(setq org-agenda-files (list (expand-file-name "~/records/project.org")))
(global-set-key (kbd "C-c a") #'org-agenda)
```

For the example file, the week beginning 2026-08-10 has **3 agenda entries**
— the two scheduled items and the deadline — and a week with nothing in it
shows none, which is the boring fact that proves the number above is a
measurement rather than a constant. (Replaying this on a day that falls
*inside* that week, you may see an extra line: org repeats an upcoming
deadline as an `In N d.:` warning on *today's* line. That is the agenda
doing its job, not a fourth entry.)

### 2.3 Recommendations

- **Declare states per file with `#+TODO:`.** It works with no init file at
  all, and it travels with the file to a machine that has never seen your
  config.
- **Four states is plenty**, and one of them should be `DROPPED`. Work you
  abandoned is data; deleting the heading throws it away.
- **Use `SCHEDULED` for nearly everything.** A real deadline is rare, and a
  file full of fake ones teaches you to ignore the agenda.

---

## 3. Decide

The decision register from §3 of the practice page, with the fields as
**properties** instead of bold labels — which is the point, because a property
can be queried and a bold label can only be grepped.

<!-- fragment -->
```org
* One config file, not a directory of fragments             :decision:
  :PROPERTIES:
  :REJECTED: a conf.d/ directory hides the load order
  :END:
```

`C-c C-x p` adds a property without typing the drawer. The `:decision:` tag is
what makes the register a register: the entries can live beside the work they
describe rather than in a separate file, and the query still gathers them.

The rule from the practice page is unchanged and is the whole discipline: **an
entry without a `REJECTED` was not a decision.** Org will not enforce that for
you — no format will — but §6 shows you the query that makes the omission
visible, which is as close as tooling gets.

### 3.1 Recommendations

- **Tag the heading, do not segregate the file.** A decision recorded next to
  the task it settled gets written; one that requires opening another file
  does not.
- **Keep `REJECTED` to one line.** If it needs a paragraph, the paragraph goes
  in the body and the property holds the summary.

---

## 4. Defer

A deferral is a `WAITING` heading that names its own trigger:

<!-- fragment -->
```org
* WAITING ask Dana about the CI compiler
  :PROPERTIES:
  :UNBLOCKS: launcher script
  :END:
```

`UNBLOCKS` is the org spelling of the practice page's *Revisit when* — the
answer to "what changes if this arrives". It is a property rather than prose
for the same reason as `REJECTED`: at review time you want the list, not a
search.

Org has no built-in notion of blocking between arbitrary headings, and this
page is not going to pretend otherwise. `org-depend` (a contrib package) adds
one. Until you have felt the lack, a property and a weekly look at it is
smaller and does not need a package.

---

## 5. Record

The dated journal, and the one place org clearly beats a directory of
markdown files: it files the entry for you, in a **datetree** — year, month,
day — so you never name a file or create a heading.

<!-- fragment -->
```elisp
(setq org-capture-templates
      '(("j" "journal" entry
         (file+olp+datetree "~/records/journal.org" "Diary")
         "* %?" :immediate-finish nil)))
```

`C-c c j` writes:

<!-- fragment -->
```org
* Diary
** 2026
*** 2026-08 August
**** 2026-08-07 Friday
***** the test that passed without testing
```

Two things about that tree are worth knowing before they bite you.

**Go through capture, not by hand.** There is an inviting function called
`org-datetree-find-date-create` that makes the day heading, and if you then
insert your entry where it leaves you, the entry lands *above* the day heading
instead of under it. The file looks almost right and the agenda disagrees with
you forever. Capture places it correctly; nothing else is worth the risk.

**The day name follows your locale.** In an English locale that heading is
**2026-08-07 Friday**; in a German one it says `Freitag`. Harmless until you
write a script that greps for `Friday`, at which point it is a bug that only
happens on other people's machines.

### 5.1 The whole example file

Everything above, in one file — this is the exact file the tests measure:

<!-- file: project.org -->
```org
#+TITLE: Records
#+TODO: TODO NEXT WAITING | DONE DROPPED

* NEXT trailing-comma parse
  DEADLINE: <2026-08-14 Fri>
  :PROPERTIES:
  :Where:    src/config/
  :END:
* NEXT write the launcher script
  SCHEDULED: <2026-08-11 Tue>
* TODO drain the inbox
  SCHEDULED: <2026-08-14 Fri>
* WAITING ask Dana about the CI compiler
  :PROPERTIES:
  :UNBLOCKS: launcher script
  :END:
* DONE key moved out of the config file
  CLOSED: [2026-08-06 Thu]
* One config file, not a directory of fragments             :decision:
  :PROPERTIES:
  :REJECTED: a conf.d/ directory hides the load order
  :END:
```

Six headings, one file, no configuration except the `#+TODO:` line at the top.

---

## 6. Review

This is where org earns the 119 MB. The practice page's weekly review is five
`grep`s across five files; here it is a query language over one.

`C-c a a` gives you the week. For everything else, `org-map-entries` takes a
match string and walks the file:

<!-- fragment -->
```elisp
(org-map-entries #'org-get-heading "+decision" 'file)   ; the decision register
(org-map-entries #'org-get-heading "TODO=\"WAITING\"" 'file)  ; blocked on someone
(org-map-entries #'org-get-heading "/!" 'file)          ; everything unfinished
```

The third one is the payoff. `/!` means "any not-done state", and it knows
which states those are **because of the `|` in the `#+TODO:` line**. You never
list your states again; add a fifth one tomorrow and this query includes it.

The same matcher drives `C-c a m` (tag search) and `C-c / ` (sparse tree,
which folds the file down to just the matches in place — the single most
useful org key for reading a long file).

The review that replaces the five commands:

| ask | how |
| --- | --- |
| what is on this week | `C-c a a` |
| what am I blocked on | `TODO="WAITING"` |
| what did I decide | `+decision` |
| what is still open, at all | `/!` |
| what did I learn | read the last day heading in `journal.org` |

### 6.1 Recommendations

- **Learn `C-c /` before the agenda.** It works on one file, needs no
  configuration, and answers most questions.
- **Do not build custom agenda commands yet.** They are the most fun part of
  org and the least useful; a view you consult once a week does not need to be
  one keystroke.

---

## 7. Literate files: the one thing markdown cannot do

Everything so far markdown can approximate. This it cannot.

A source block can name a file it belongs to, and org will write it there:

<!-- file: literate.org -->
```org
* The review, explained

#+begin_src c :tangle records.c
#include <stdio.h>

int main(void)
{
    printf("read the record back\n");
    return 0;
}
#+end_src
```

`C-c C-v t` **tangles to `records.c`** — **91 bytes**, exactly the block's
contents and nothing else. The prose stays in the `.org` file; the compiler
gets a plain `.c` file and never knows.

That is the honest strongest argument for org in a software project: the
explanation and the code are one artifact, and the artifact the compiler reads
is generated rather than maintained in parallel. It is also the one with the
sharpest edge — **the generated file is not the source of truth, and anyone
who edits `records.c` directly loses their work on the next tangle.** Do not
adopt this quietly on a shared repository.

Export is the other direction: `C-c C-e m m` writes markdown, `C-c C-e h h`
HTML. Useful when the reader has no Emacs, which is most readers.

---

## 8. jichi and `.org`

Stated exactly, because "plain text" is not the same as "supported".

- **Readable.** `read_file` and `@file` references treat `.org` as text, like
  anything else ([REFERENCES.md](REFERENCES.md)).
- **Indexed.** `codebase_search` and documentation sources chunk and embed
  `.org` files — the index skips a denylist of binary-ish extensions and
  `.org` is not on it ([RAG.md](RAG.md)).
- **Not in the repository map.** The map ([REPOMAP.md](REPOMAP.md)) scans an
  *allowlist* of source extensions and extracts top-level definitions; `.org`
  is not in it. Your headings will not appear in the map jichi injects into
  its system prompt. Reference the file explicitly when you want it seen.
- **The Emacs integration knows the mode.** `editors/emacs/jichi.el`
  ([EMACS.md](EMACS.md)) names the buffer's major mode in the prompt and wraps
  the region it sends in an ```` ```org ```` fence, derived from `org-mode` by
  the same rule that turns `python-mode` into `python`. It also unwraps a
  single fence from the answer before inserting it. So the model is told it is
  looking at org — it is not *forced* to reply in org, and a model that ignores
  the context will hand you markdown headings.

The advice from [PROJECT_RECORDS.md](PROJECT_RECORDS.md) §8 is unchanged: keep
these files out of the system prompt, reference them in the turn that needs
them, and do not let the agent maintain your decision register.

---

## 9. The whole init

Everything this page uses, for someone starting from nothing:

<!-- fragment -->
```elisp
;; Org is already installed -- it ships inside Emacs. This only wires it up.
(require 'org)
(require 'org-capture)                       ; templates are unbound until this

(global-set-key (kbd "C-c a") #'org-agenda)
(global-set-key (kbd "C-c c") #'org-capture)

;; The two things that cannot have a default, because only you know them:
(setq org-agenda-files
      (list (expand-file-name "~/records/project.org")))
(setq org-capture-templates
      '(("i" "inbox" entry (file "~/records/project.org")
         "* %?\n  :PROPERTIES:\n  :Captured: %U\n  :END:")
        ("j" "journal" entry
         (file+olp+datetree "~/records/journal.org" "Diary")
         "* %?")))
```

Twelve lines, and two of them are the paths. Everything else in this page —
states, priorities, properties, tags, queries, tangling, the datetree — worked
under `emacs -Q` with no configuration at all. **Add to this only when
something has annoyed you**, and write the reason into your decision register
when you do. An org config that grew by copying other people's is the most
common way people end up unable to explain their own editor.

---

## 10. Troubleshooting

**"My agenda is empty."** `org-agenda-files` is `nil` until you set it. That
is §0's second bullet and it accounts for most first-day frustration.

**"`Symbol's value as variable is void: org-capture-templates`."** Nothing has
loaded `org-capture` yet. `(require 'org-capture)` before you `setq` it.

**"`Heading not found on level 1`."** The capture target file is not where you
think. A relative path resolves against `org-directory` (`~/org/`), not the
current directory — write the path in full.

**"`NEXT` shows up as part of the heading text."** No `#+TODO:` line in that
file, so `NEXT` is not a state. Add the line at the top and press `C-c C-c` on
it to re-read it without reopening the file.

**"My colleague's Emacs shows different states."** States are per-file when
declared with `#+TODO:` and per-config otherwise. That asymmetry is the reason
§2 recommends declaring them in the file.

**"The tangled file lost my edits."** They were edits to a generated file.
See §7 — this is the cost of literate programming, and it is why that section
warns before it recommends.

---

**Next:** [PROJECT_RECORDS.md](PROJECT_RECORDS.md) is the practice this
mechanises · [EMACS.md](EMACS.md) for driving jichi from Emacs ·
[EDITORS.md](EDITORS.md) for the other editors.
