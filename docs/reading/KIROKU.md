# 記録（きろく）*Kiroku* — reading the record

*The fourth reading guide, and the only one whose subject is not the code. The
other three teach you the program: [案内 *Annai*](ANNAI.md) follows one request
through it, [深掘り *Fukabori*](FUKABORI.md) argues one design decision per
chapter, [追跡 *Tsuiseki*](TSUISEKI.md) quotes a recorded run byte for byte. This
one teaches you **the record** — what jichi wrote down about building itself, and
how to read 6.8 MB of it without drowning.*

## Who this is for

Someone who was not there.

That is the whole difficulty. The record was written by the two participants — one
operator, one agent — for their own future use, entry by entry, over 530
milestones. Every entry is honest and most are checkable. But honesty accumulated
in order of occurrence is an **archive**, not a record you can read, and the
difference is a door.

If you are here to use jichi, you want [`../README.md`](../README.md). If you are
here to learn the program, start with the Annai. If you are here because someone
told you this project writes down its failures, you are in the right place.

## 1. What you are facing, in numbers

| File | Size | The question it answers |
|---|---|---|
| [`../ROADMAP.md`](../ROADMAP.md) | **1.8 MB**, 27,663 lines, ~510 entries | *What happened, in order, and why* |
| [`../ANECDOTES.md`](../ANECDOTES.md) | 243 KB, 65 entries | *What went wrong, and what it taught* |
| [`../DECISIONS.md`](../DECISIONS.md) | 154 KB | *What was chosen — and what was rejected* |
| [`../DEFERRED.md`](../DEFERRED.md) | 113 KB | *What was consciously not done, and the reason* |
| [`../analysis/`](../analysis/) | 760 KB, 58 documents | *One measured investigation each* |
| [`../PROJECT_TIMELINE.md`](../PROJECT_TIMELINE.md) | 54 KB | *The shape of the whole, at a distance* |
| [`../JOURNEY.md`](../JOURNEY.md) | 8.5 KB | *The learning path (not history — a road)* |

474 markdown files, 6.86 MB in total. **Nobody has read all of it, including its
authors.** That is not a defect to apologise for; it is why this page exists.

**Do not start at the ROADMAP.** It is chronological, it is the largest file in
the repository, and its first hundred entries describe a program that no longer
exists in that shape. Start with §3 below.

## 2. How one entry is built

Every ROADMAP entry from roughly M300 onward has the same skeleton, and knowing it
lets you skim ten in the time one takes to read:

- **What was asked** — the operator's request, often quoted.
- **What was measured** — numbers, with the command that produced them. If an
  entry gives a number without saying how it was obtained, that is a defect the
  project has caught itself committing (see shape 3).
- **What was rejected** — usually with the reason. A decision with no rejected
  alternative was not a decision, and `DECISIONS.md` says so in its own header.
- **What was verified** — which gate ran, and what it said.

An anecdote has a different skeleton, and it is the older convention: *symptom →
dead ends → root cause → lesson*. The dead ends are the valuable part and they are
kept deliberately, because a corrected diagnosis teaches more than a correct one.

## 3. The nine shapes

Here is the claim this guide exists to make: **65 anecdotes are instances of about
nine shapes.** Learn the shapes and the archive becomes a taxonomy; skip them and
it stays 243 KB of other people's bad afternoons.

Every shape below cites incidents by their number in
[`../ANECDOTES.md`](../ANECDOTES.md), so you can check the claim rather than
believe it. The nine are not a designed framework — they are what is left when you
sort the incidents by cause instead of by date.

### Shape 1 — A cap that fires manufactures a plausible answer

A timeout, a token budget, a retry limit: when one fires, you do not get *no
answer*. You get **a different answer, shaped like data**, pointing at whatever
you already suspected.

Read **#64** (a `--deadline` that turned a hang into "the gateway is too slow",
which agreed with the prior), **#38** (three million tokens against a gate that
could not pass), **#43** (a cap set by the person who knew better).

The defence: caps are for *bounding duration*, fences are for *bounding blast
radius*, and the two are not interchangeable. Measurement runs go **uncapped**.
`CLAUDE.md` states this as an invariant, and it exists because the alternative was
tried.

### Shape 2 — An instrument validated on one input shape is not validated

Your measuring tool works. You have seen it work. It works on the inputs you have
given it, which share a property you never noticed.

Read **#65** (a classifier whose "native" pattern could not cross a newline, so it
reported the opposite for five capable models — while answering correctly against
a server whose JSON happened to be single-line), **#26** (a test rig reporting bugs
that never happened), **#32** (two instruments, one quantity, a 2.3× disagreement),
**#58** (a lint that broke on the platform it was written for).

The defence: run the pattern against a **recorded real** response, and make the
fallback branch say *unknown* rather than a positive claim. A classifier whose
`else` is a finding converts a matching bug into a discovery.

### Shape 3 — A check's universe is smaller than its header claims

The check is green. The check has always been green. The check covers 73% of what
its own first line says it covers, and nothing anywhere says which 73%.

Read **#24** (the audit said no and the flake said nothing), **#52** (64 of 80 hint
ladders silently short), **#20** (a self-test that tested a sibling — twice in one
day), **#17** (a gate green because `cat` succeeded).

The defence, and it is the most-used rule in the repository: **state the universe
in the check's own header, floor the extraction at today's exact count, and
enumerate the set a second way by a different route and diff.** Three consecutive
milestones found a green check covering less than it claimed; the sweep that
followed found four more and 24 live defects.

### Shape 4 — A presence check plus a silent fallback turns a fence into a denial

This one is worth learning in the abstract, because it has recurred with different
spellings and will recur again:

```
if (the key is PRESENT) {
    value = read_it(default_if_wrong_type);
}
```

Give it a value of the wrong *type* and the read fails, the default is written in,
and the presence check has converted "I could not read this" into **"the operator
explicitly asked for the default"** — which, for a fence, is an explicit request
to be unfenced.

Twice, measured: fifteen shipped example configs said `"pathFence": 1` and every
one of them ran with the path fence **off**; and a model spawning a read-only
subagent with `{"readonly": 1}` got a **writable** one. Both in the ROADMAP
(M519, M530) rather than the anecdotes — which is itself worth noticing, because
the shape's two clearest instances were never written up as stories.

Related fence incidents that *are* anecdotes: **#5** (an edit-scope fence refusing
in-scope edits), **#3** (a skill fence clamping a whole 72-iteration turn),
**#44** (a fence in the wrong place).

The defence: a boolean a *human*, a *foreign program* or a *model* writes is read
leniently — a number and the unambiguous strings are accepted, prose still falls
through — and only jichi's own sinks require a strict boolean.

### Shape 5 — A diagnostic that asserts a cause it never checked

The message is confident, specific, actionable, and names the wrong cause. It is
worse than silence, because it sends the reader to fix something that was never
broken.

Read **#4** (an "auto-context stall" that was chat-model latency — a
misattribution, corrected in place), **#33** (the 54% that was 14%, and the nudge
that cried wolf), **#53** (a report that got every fact right and the conclusion
wrong), **#55** (a supervisor accusing jichi of lying, three different ways).

The defence: a diagnostic may only name a cause it has **evidence for**. When a
reasoning model ended a turn with no answer, jichi said "the output budget was
likely exhausted — raise maxTokens" *always*; it now consults the server's own
`finish_reason` and says which of the two happened, because in one measured
afternoon both did.

### Shape 6 — A fixture broken in a way that produces a plausible finding

The test fails. The failure is interesting. The failure is in the fixture.

Read **#25** (the third time the fixture lied), **#23** (12 GB we could not
reproduce, measured with a 13-byte file), **#18** (an e2e mock truncating a slow
request under load), **#56** (a proof that tested the fix it had just removed).

The defence: when a red run surprises you, ask *why this exact error* before
believing it. Two instances this year were a missing blank line after `data:
[DONE]` (so the terminal SSE event was never dispatched, and two "product defects"
were the fixture) and an escape-attempt fixture planted one directory from where
the escape actually pointed — which made the security check pass for the wrong
reason.

### Shape 7 — A gate stage nobody runs stops being a gate

It is in the target. It is documented. It has not executed in sixteen milestones,
and no output anywhere says so, because **absence of a run produces absence of
output**.

Read **#47** (four toolchains, three broken, and the gate that would have said
so), **#45** (the first run under a new check disabled the gate to pass it),
**#42** (the gate said fail; nothing had failed).

The defence: run the whole gate before claiming a milestone, not the tiers that
answer the question you were working on. The ASan stage of this project's own
`make ci` was red for roughly sixteen milestones while `make test` and `make
smoke` were green every time.

### Shape 8 — Two paths that answer one question differently

Two functions, two readers, two validators — for one question. They agreed when
they were written. One of them learned something.

Read **#59** (a zero-length read that meant two different things), **#36** (the
mirror image of a fix shipped an hour earlier), **#57** (a variable that was set,
visible, and absent), **#42**.

The defence: one reader per question, and where two must exist, a check that
asserts they agree. Recent instances: two grading implementations where only one
had the guard that stops an unrunnable grader being scored as a *failure*; and a
diff preview that read an argument more strictly than the tool that would act on
it, so the user approved a narrower edit than the one that ran.

### Shape 9 — The answer was already in the evidence

The run wrote it down. The journal has it. The warning fired. Nobody read it, or
somebody read it and did not connect it, or the re-run destroyed it.

Read **#61** (the evidence was self-refuting, and nobody read it), **#41** (the
correct answer was ten lines above, in a function the brief named), **#19** (every
request we ever sent ended with an empty assistant turn), **#49** (I destroyed the
evidence by re-running), **#46** (I verified the alternatives and not the ground I
chose on).

The defence: three evidence sinks that live **outside** the workspace so a
rollback cannot take them, and the habit of reading a run's own journal before
theorising about it. This shape has no clever fix. It is the one that keeps
recurring.

## 4. A first afternoon

In this order. Each is short, each stands alone, and together they teach the
shapes above by example rather than by definition.

1. **#1** — the first anecdote, and still the best introduction to the whole
   project's stance: an apparent product bug that was the rollback reverting a log
   file kept inside the blast radius.
2. **#21** — a model asked for 200 lines and got 172 KB, five times. The clearest
   single case of a type mismatch silently selecting the most expensive behaviour
   available.
3. **#26** — the test rig reported bugs that never happened. Shape 2 in its purest
   form, and the origin of this project's suspicion of its own tools.
4. **#19** — every request we ever sent ended with an empty assistant turn. Read
   it for how long a defect can hide in plain output.
5. **#49** — I destroyed the evidence by re-running. Two paragraphs; the most
   transferable habit in the file.
6. **#63** — the key was capable, so I treated it as permitted. The one about
   *authority* rather than correctness, and the reason this project's rules about
   spending are written the way they are.
7. **#64 and #65** together — a cap that manufactured an answer, then an
   instrument that could not say yes. They are consecutive, they are the same
   afternoon, and reading them as a pair teaches more than either alone.
8. Then one **analysis** document end to end — [the local-model
   one](../analysis/2026-08-21-local-model-tool-calling.md) is a good first,
   because it shows four wrong answers being produced and corrected in sequence,
   with the measurement that ended each.
9. Then [`../DEFERRED.md`](../DEFERRED.md)'s opening section, which is the part of
   the record most projects do not keep at all: things consciously *not* done,
   with the reason and the condition that would revive them.

## 5. How to check a claim in the record

The record is meant to be *audited*, not trusted. Every kind of entry gives you a
handle:

- **A number** should name the command that produced it. Where one does not, that
  is a known defect — a comparison in this repository quoted file counts for years
  that could not be reproduced, because it never recorded its own `grep`.
- **A defect claim** names the file and usually the function. Anchors in the
  reading guides are `file.c:function_name` and never line numbers, because line
  numbers rot; a lint enforces that.
- **A fix claim** names the test or lint that holds it. You can run it:
  `sh tests/smoke/<name>.sh` prints TAP and exits non-zero on failure.
- **A verdict about a platform** uses a closed vocabulary — *Verified*, *Partly
  verified*, *Never compiled* — and a lint polices the phrasing, because "it
  should work on X" is the sentence that platform claims die of.
- **A retraction** is printed *beside* the claim it corrects, not instead of it.
  When you find "Corrected at M###" in an entry, the original is still above it on
  purpose.

## 6. What the record deliberately does not contain

Stated so you do not go looking:

- **No performance benchmarks against other tools.** The comparison in
  [`../COMPARED.md`](../COMPARED.md) is about features and lineage, and says
  outright that no quality, speed or cost comparison has been run.
- **No claims about tools whose source was not read.** Claude Code's config
  surface is documented because a converter parses it; its behaviour is not.
- **No successes without their cost.** Entries that shipped something usually name
  what it broke on the way, and several name work that was deleted after being
  built and verified as unnecessary (**#28**).
- **No smoothed-over first drafts.** Where a summary was sent to the operator and
  then corrected, both are in the entry.
- **Very little about the people.** It is a record of decisions and failures, not
  a diary.

## 7. Honest limits of this guide

- **The nine shapes are mine, not the project's.** They are a reading of the
  incidents, produced by sorting 65 anecdotes by cause; someone else sorting them
  would draw different lines, and two of the shapes (4 and 8) overlap more than a
  clean taxonomy would like.
- **Two of the shapes' clearest instances are not anecdotes at all** — they live in
  ROADMAP entries. The record's own conventions did not catch up with its most
  recent findings, which is a gap in the record, not in the shape.
- **Nothing here is a substitute for the primary text.** This page is a door. It
  cites so you can leave it.
- **The counts in §1 were measured on 2026-08-22** and will drift. The command is
  `find docs -name '*.md' -exec cat {} + | wc -c`.
