# Keeping a project's records in plain text

> **Prerequisite:** none. A terminal, an editor, and a directory you are
> willing to keep. No jichi, no Emacs, no server, no account. If you have
> `cat`, `grep`, `sed` and `ls`, you have everything this page asks for.

Software is written twice: once as code, and once as the record of why the
code is the way it is. The second half is the one that goes missing. Six
months on, the code still compiles and nobody can say what the decision was,
what was tried first, or which of today's problems was already answered and
forgotten.

This page teaches the **practice**, not a program. Six jobs — capture, plan,
decide, defer, record, review — each with a file you can hold in your head,
a rule for what earns an entry, and a command that reads it back. The files
are markdown because markdown is a text file that has given up on being
clever, and a text file is the only format that will still open on a machine
you have not bought yet.

**Read in order the first time.** §1–§6 are the practice. §7 is honest about
what plain text cannot do, and §8 connects it to jichi.

| | section | you get |
| --- | --- | --- |
| §0 | [Why a written record](#0-why-a-written-record) | the argument, and this repository as the evidence |
| §1 | [Capture](#1-capture--inboxmd) | `INBOX.md` — nothing is lost between thought and desk |
| §2 | [Plan](#2-plan--boardmd) | `BOARD.md` — what you are doing, and only that |
| §3 | [Decide](#3-decide--decisionsmd) | `DECISIONS.md` — the choice *and the alternative* |
| §4 | [Defer](#4-defer--deferredmd) | `DEFERRED.md` — a "no" with a way back |
| §5 | [Record](#5-record--journalyyyy-mm-ddmd) | `journal/` — what broke and what it taught |
| §6 | [Review](#6-review--reading-it-back) | five commands, once a week |
| §7 | [When plain text is not enough](#7-when-plain-text-is-not-enough) | the honest limits |
| §8 | [Working with jichi](#8-working-with-jichi) | how the agent reads these files |
| §9 | [Troubleshooting](#9-troubleshooting) | when the practice stops working |

Set up the whole thing now; the rest of the page fills it in.

<!-- fragment -->
```console
$ mkdir -p records/journal && cd records
$ touch INBOX.md BOARD.md DECISIONS.md DEFERRED.md
```

---

## 0. Why a written record

Three arguments, in the order they convince people.

**You will not remember.** Not the decision — you will remember *that* you
decided. You will not remember the alternative you rejected, and that is the
part a future reader needs, because the alternative is what they are about to
propose.

**A file survives its tools.** A ticket in a tracker is readable while the
tracker is paid for, reachable, and still in business. A markdown file is
readable by `cat`, by an editor written thirty years ago, and by whoever
inherits the repository. It also lives *in* the repository, so `git log` tells
you when the decision changed and the diff tells you what it used to say.

**It is the smallest thing that works.** Everything here runs on a machine
with 4 GB of RAM and no network. That is not an aesthetic preference; it is
the same constraint that shapes jichi itself, and a project-management
practice that needs a 119 MB editor to be usable would quietly contradict it.

### 0.1 This repository is the worked example

jichi's own project management is exactly this: markdown files, each with a
stated rule for what earns an entry. Not a demonstration written for a
tutorial — the real thing, under the same discipline it teaches.

| file | job | its own stated rule |
| --- | --- | --- |
| [ROADMAP.md](ROADMAP.md) | the lab notebook | one entry per milestone; numbers are *measured, not incremented* |
| [DECISIONS.md](DECISIONS.md) | decisions + rejected alternatives | a row is earned when someone could sensibly have chosen otherwise |
| [DEFERRED.md](DEFERRED.md) | consciously not done | if new information could change the answer, it is deferred, not dropped |
| [ANECDOTES.md](ANECDOTES.md) | the diary | symptom → dead ends → root cause → lesson |
| [TEST_INTEGRITY.md](TEST_INTEGRITY.md) | how the tests have lied | every entry is a real failure, kept |

Read one of them after this page. `DECISIONS.md` is the shortest and the
clearest illustration of the rule in §3.

### 0.2 Recommendations

- **Keep the records in the repository, beside the code.** Records that live
  elsewhere drift, because updating them is a second errand.
- **Commit them with the change they describe.** The decision and the diff it
  explains belong in one commit; a reader who finds the code finds the reason.
- **Five files is the whole system.** Resist a sixth until a real entry has
  nowhere to go. The failure mode of a records practice is never too few
  files.

---

## 1. Capture — `INBOX.md`

The inbox exists so that a thought costs you nothing to have. A question,
a suspicion, an idea, a thing to ask somebody — it goes in one line and you
go back to work. **Append-only.** You never edit the inbox, you only drain it.

**What earns an entry:** anything you would otherwise hold in your head while
trying to do something else.

<!-- file: INBOX.md -->
```markdown
# Inbox

- 2026-08-05 does the config parser accept a trailing comma on purpose?
- 2026-08-05 ask Dana which compiler the CI box actually has
- 2026-08-07 the launcher could check the key is set before it starts
```

Appending without opening an editor is the point — the friction is what
decides whether you use it:

<!-- fragment -->
```console
$ printf -- '- %s %s\n' "$(date +%F)" "check whether -Os changes the timings" >> INBOX.md
```

Make that a shell function in your `.bashrc` and call it `note`. An inbox you
have to open an editor for is an inbox you will not use.

Draining is the other half, and it is a **decision per line**: it becomes a
board item (§2), a decision (§3), a deferral (§4), or you delete it. A line
that has survived three drains is telling you something — usually that it is
a deferral you have not admitted to yet.

<!-- shell -->
```console
$ grep -c '^- ' INBOX.md
3
```

### 1.1 Recommendations

- **One line per thought, dated.** Multi-line entries are already board items.
- **Drain on a fixed day**, not when it feels full. An inbox drained on a
  schedule stays a tool; one drained on impulse becomes a graveyard.
- **Never edit an inbox line.** Move it or delete it. Editing it means you
  are doing the thinking in the wrong file.

---

## 2. Plan — `BOARD.md`

Three headings, in this order: what you are doing, what is next, what is
done. The order matters, because the file should answer "what am I supposed
to be doing right now" in the first screen.

**What earns an entry:** work you have committed to, small enough that you
could finish it in a sitting. If it is bigger, it is several entries.

<!-- file: BOARD.md -->
```markdown
# Board

## Doing

- trailing-comma parse — started 2026-08-07

## Next

- write the launcher script
- ask Dana about the CI compiler

## Done

- 2026-08-05 read the config merge path end to end
- 2026-08-06 key moved out of the config file
```

**Doing holds one item.** This is the only rule on the page that is a hard
number, and it is the one that pays for itself fastest. Two things in
progress is two things unfinished; the second one is not progress, it is the
first one being avoided. The command that enforces it reads the section
between two headings:

<!-- shell -->
```console
$ sed -n '/^## Doing/,/^## Next/p' BOARD.md | grep -c '^- '
1
```

If that prints anything but `1`, you already know which item you have been
avoiding. Move it back to **Next** — that is not a failure, it is the board
doing its job.

**Done keeps its dates and never gets cleared.** It is the only honest record
of pace you will ever have, and you will want it the first time somebody asks
how long something takes.

### 2.1 Recommendations

- **Write the item as an outcome, not an activity.** "launcher script exists
  and is committed", not "work on launcher".
- **Date entries when they move to Done**, not when you added them.
- **Let Next be unordered.** Sorting a backlog is work that produces nothing;
  the top of Next is decided when you pick, from what you know then.

---

## 3. Decide — `DECISIONS.md`

The register that repays the effort. It is not a log of what you did; it is a
log of what you chose **and what you did not**.

**What earns an entry:** a choice where someone could sensibly have decided
otherwise. If there was no alternative, it was not a decision — it was a
consequence, and it does not belong here.

Three fields, and all three are load-bearing:

- **Chose** — one sentence, in the present tense.
- **Rejected** — the alternative *and why it lost*. Without this the entry is
  worthless, because the reader is holding the rejected option in their hand.
- **Where** — the file or directory the decision lives in, so a reader who
  finds the code can find the reason and a reader who finds the reason can
  find the code.

<!-- file: DECISIONS.md -->
```markdown
# Decisions

## 2026-08-05 — One config file, not a directory of fragments

**Chose:** a single `config.json`.
**Rejected:** a `conf.d/` directory — nothing needs to merge independently
yet, and a directory hides the load order from whoever is debugging it.
**Where:** `src/config/`

## 2026-08-06 — The key lives in the environment

**Chose:** read the key from `$JICHI_API_KEY` at startup.
**Rejected:** a literal in the config file — one `git add -A` and it is
public forever, and a key in a file cannot be rotated without an edit.
**Where:** `run-jichi.sh`

## 2026-08-07 — Markdown files, not a ticket tracker

**Chose:** these five files, in the repository.
**Rejected:** a hosted tracker — it lives where the code does not, it needs
an account to read, and it cannot be searched from the machine that is on
fire.
**Where:** this directory
```

Two counts must agree. Every heading is a decision; every decision names what
it rejected:

<!-- shell -->
```console
$ grep -c '^## ' DECISIONS.md
3
$ grep -c 'Rejected:' DECISIONS.md
3
```

When those numbers differ, one entry is not a decision — it is a note that
wandered in from the journal. Move it or give it its alternative.

And the register doubles as an index into the code:

<!-- shell -->
```console
$ grep 'Where:' DECISIONS.md
**Where:** `src/config/`
**Where:** `run-jichi.sh`
**Where:** this directory
```

### 3.1 Recommendations

- **Write it while the alternative is still warm.** A week later you will
  remember the winner and reconstruct a flattering reason for it. That
  reconstruction is worse than no entry, because it reads as evidence.
- **Never delete a decision; supersede it.** Add a new entry that names the
  old one. The wrong turn is half the value of the register.
- **A decision you cannot state in one sentence is two decisions.**

---

## 4. Defer — `DEFERRED.md`

A "not now" that is written down is a decision. A "not now" that is not
written down is a thing you will re-litigate every few weeks, each time from
scratch, each time reaching the same answer with no memory of the last three
times you reached it.

**What earns an entry:** something you consciously chose not to do, *where
new information could change the answer*. If nothing could change your mind,
it is not deferred — it is rejected, and it belongs in §3.

Two fields, and the second is the one everybody forgets:

- **Not now because** — the reason, as of today.
- **Revisit when** — the **trigger**. A condition the world could meet, not a
  date. "In three months" is a lie you are telling yourself; "when a second
  person asks" is a thing that can actually happen and that you will notice.

<!-- file: DEFERRED.md -->
```markdown
# Deferred

## Colour in the report

**Not now because:** nobody has asked for it, and it doubles what the tests
must cover for output that is read once and thrown away.
**Revisit when:** a second person asks for it.

## Windows support

**Not now because:** every user so far is on Linux or macOS, and the paths
would need a rewrite rather than a patch.
**Revisit when:** someone tries it and reports what actually broke.
```

Same paired count as the decisions register, for the same reason:

<!-- shell -->
```console
$ grep -c '^## ' DEFERRED.md
2
$ grep -c 'Revisit when:' DEFERRED.md
2
```

A deferral without a trigger is not deferred. It is forgotten, with extra
steps and a clear conscience.

### 4.1 Recommendations

- **Make the trigger observable.** You must be able to notice it without
  going to look. "When a second person asks" arrives in your inbox.
- **Say the cost, not just the reason.** "It doubles the test matrix" ages
  well; "no time" ages into nothing.
- **When a trigger fires, move the entry** to `DECISIONS.md` — with what you
  now know. That migration is the whole point of writing the trigger down.

---

## 5. Record — `journal/YYYY-MM-DD.md`

One file per day, and only on days that taught you something. This is the
diary: what broke, what you tried, what it actually was, and what you will do
differently. It is written for the person who hits it again — usually you.

**What earns an entry:** a surprise. If the day went as expected there is
nothing to record; the board already says what you did.

Four headings, and the order is the discipline:

<!-- file: journal/2026-08-05.md -->
```markdown
# 2026-08-05 — the config that would not load

**Symptom:** `--config ./local/config.json` behaved as if the file were
empty. No error, no warning, defaults everywhere.

**Tried:** re-reading the JSON for a syntax error (there was none), adding
prints to the parser (it was never called), suspecting the shell quoting.

**Cause:** an explicit `--config` replaces the merge entirely, and the model
I wanted was only ever defined in the *global* file. The parser was right;
my model of the precedence was wrong.

**Lesson:** when nothing in the code is wrong, the wrong thing is the model
in my head. Read what the program says it does before instrumenting it.
```

<!-- file: journal/2026-08-07.md -->
```markdown
# 2026-08-07 — the test that passed without testing

**Symptom:** a new check went green on the first run, before the fix.

**Tried:** re-running it, adding output, doubting the runner.

**Cause:** the fixture was 30 bytes, and the bound I was checking was 600.
The check could not fail, so it never had.

**Lesson:** a test never observed failing has never been observed working.
Break it on purpose once, and write down that you did.
```

Write the entry **before** you finish explaining the bug away. The honest
version — the dead ends, the wrong theory you held for an hour — is the part
with the teaching in it, and it is gone by the time the fix is committed.

<!-- shell -->
```console
$ ls journal
2026-08-05.md
2026-08-07.md
$ grep -c 'Lesson:' journal/2026-08-05.md journal/2026-08-07.md
journal/2026-08-05.md:1
journal/2026-08-07.md:1
```

One lesson per entry. Two lessons means two days, or one lesson and one
observation.

### 5.1 Recommendations

- **Name the file for the day, not the bug.** You will search by `grep`
  anyway, and dated names sort themselves.
- **Record the surprise before you explain it.** The explanation edits the
  memory of the surprise, always in the direction that flatters you.
- **Write the entry even when the cause was your own mistake.** Especially
  then. A mistake written down becomes a lesson; a mistake not written down
  becomes a habit.

---

## 6. Review — reading it back

A record nobody reads is a diary, not a practice. The review is what turns
five files into a system, and it is five commands.

Once a week, in the records directory:

<!-- shell -->
```console
$ grep -c '^- ' INBOX.md
3
$ sed -n '/^## Doing/,/^## Next/p' BOARD.md | grep -c '^- '
1
$ grep -c 'Rejected:' DECISIONS.md
3
$ grep -c 'Revisit when:' DEFERRED.md
2
$ ls journal
2026-08-05.md
2026-08-07.md
```

Each number is a question:

| the number | what it asks |
| --- | --- |
| inbox lines | is anything in here older than two drains? |
| items in Doing | is it exactly one? |
| decisions | did this week's choices get written down, or only made? |
| deferrals | has any trigger quietly fired? |
| journal files | did a week of work teach nothing, or did I not write it down? |

Take fifteen minutes. Drain the inbox, move what the triggers freed, and
close the review by writing the date into `journal/` if the review itself
taught you something.

### 6.1 Recommendations

- **Put the five commands in a script** and call it `review.sh`. The practice
  survives on the days you do not feel like it, and only if it is one command.
- **Review on a fixed weekday.** The cadence matters more than the day.
- **A quiet week is data.** Two quiet weeks in a row means the records have
  drifted from the work, and the work is what is real.

---

## 7. When plain text is not enough

Everything above is deliberately dumb, and there are four places you will
feel it.

**Dates are text.** Nothing warns you that a deadline passed, because nothing
knows a deadline is a date. You reread the file, or you do not find out.

**There is no query.** `grep` finds a string. It cannot answer "everything
open, tagged infrastructure, due this week, across four files" — that is a
join, and text files do not join.

**Nothing is aggregated.** Five files across three projects means fifteen
files, read by hand. There is no view that puts today's work in one place.

**You maintain the shape.** Nothing stops a heading from being misspelled or
a `Revisit when:` from being missing — which is why §3 and §4 pair their
counts, and why this page's own examples are checked by a test.

If those limits become expensive rather than merely present, the next step up
that is *still plain text* is Emacs org-mode: the same files, with states the
editor understands, dates it can agenda, and properties it can query.
[ORG_MODE.md](ORG_MODE.md) is the tutorial, and it is honest about the price
— org is free once you have Emacs, and Emacs is the largest thing on the
shelf. Read it when the limits above have actually cost you something, not
before. **The practice on this page is the thing that matters; org is one way
to mechanise it.**

---

## 8. Working with jichi

These files are plain text, so the agent reads them the way you do — but
what you point it at is a choice worth making deliberately.

**Reference a record in a question.** `@`-references inline a file into the
turn ([REFERENCES.md](REFERENCES.md)):

<!-- fragment -->
```console
$ jichi -p "@records/DECISIONS.md does the new cache contradict any of these?"
```

That is the highest-value use of the register: an agent that can see what you
rejected will stop proposing it.

**Keep them out of the system prompt.** It is tempting to paste the records
into `AGENTS.md` so the agent always has them. Don't — the system prompt is
sent on every turn, it competes with the code for the context window, and a
board changes several times a day. Reference the file in the turn that needs
it.

**They are not the agent's memory.** `.jichi/memory.md`
([MEMORY.md](MEMORY.md)) is what the agent writes for itself, and it is
injected into every system prompt for exactly that reason. Your records are
what *you* write, for people. The two stay separate: an agent that edits your
decision register is an agent editing the account of why it was asked to do
things.

**Session notes are neither.** A jichi session transcript
([EXPORT.md](EXPORT.md)) is raw material — the journal entry is what you
write *after* reading one.

### 8.1 Recommendations

- **Add `records/` to the repository, not to `.gitignore`.** The point is
  that they are shared and versioned.
- **Let the agent draft, and you decide.** Asking for a journal entry from a
  session transcript is useful; letting it write the decision register is
  not, because it did not make the decision.

---

## 9. Troubleshooting

**"I stopped after two weeks."** The review was the missing piece, not the
discipline. Put the five commands of §6 in a script; a practice that needs
you to remember five things is a practice that ends.

**"The inbox has forty lines."** It is not an inbox any more, it is a
backlog. Drain it once by hand: everything that is not a board item, a
decision or a deferral gets deleted. Deleting is a legitimate outcome and the
most common correct one.

**"The board and reality disagree."** Reality wins; fix the board and do not
apologise to it. If they disagree every week, the board is being written for
an imagined observer instead of for you.

**"I do not know if this is a decision or a deferral."** Ask whether new
information could change the answer. If yes, it is a deferral and it needs a
trigger. If no, it is a decision and it needs a rejected alternative.

**"There is nothing to write in the journal."** Then there is nothing to
write. A journal that fires every day is a log; the point is the surprise,
and most days do not contain one.

**"My team already uses a tracker."** These are not rivals. The tracker holds
what other people need to see; `DECISIONS.md` and the journal hold what your
future self needs to read, and no tracker has ever been a good home for
either.

---

**Next:** [ORG_MODE.md](ORG_MODE.md) if you want the same practice
mechanised and you already run Emacs · [CURRICULUM.md](CURRICULUM.md) for the
craft this sits inside · [ANECDOTES.md](ANECDOTES.md) for what a real journal
looks like after two hundred entries.
