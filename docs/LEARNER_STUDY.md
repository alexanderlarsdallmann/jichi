# A learner study — the protocol

*Written 2026-09-25, from the operator's decision on the twelve questions, item 8: "a small
repeatable learner study (3–5 self-learners, consent recorded)"
([`plans/2026-09-24-twelve-questions.md`](plans/2026-09-24-twelve-questions.md)). It has not been
run. This page is how to run it, so that the first run and the tenth are the same study.*

## Why, and why this size

[`INTERFACE_TUTORIAL.md`](INTERFACE_TUTORIAL.md)'s checklist ends with *watch one person use it
without coaching*, and nobody has recorded doing that. The only usability evidence jichi has is
the operator's own screen-reader sittings of 2026-08-22/23. On the defects that mattered there,
the analysis's headline is that *the test suite found none*
([`analysis/2026-08-24-twenty-milestones-by-ear.md`](analysis/2026-08-24-twenty-milestones-by-ear.md)).
A person found them. That is the argument for doing it again, with people who did not build the
program.

**Three to five people is a discount method, and it can say only one kind of thing.** It finds
problems. It cannot say how often they occur, or how many people they affect. Nielsen and
Landauer's model gives the best case for five users, and Faulkner measured how much five-person
samples vary ([`BIBLIOGRAPHY.md`](BIBLIOGRAPHY.md) section 14). So the report of this study lists
problems, each with a severity. It states no percentages, and it never says "three of five
users", as if that were a rate.

## Who

- **Three to five self-learners** who have not used jichi, have built software from a terminal
  before, and are not project contributors. A JLU course is the likely source, and that is the
  operator's to arrange.
- **One facilitator**, who says as little as possible. If the facilitator is also jichi's author,
  write that down. The evaluator effect (Hertzum & Jacobsen) is larger when the evaluator has a
  stake.
- **Optionally one note-taker**, so the facilitator does not also have to write.

## Consent, before anything is recorded

A study records people, so it asks them first, in writing, and keeps the answer.

Tell each participant, and have them agree to:
- what is recorded: typed commands and the facilitator's notes, always; **screen or audio only
  if they agree separately**;
- that they are testing jichi, not being tested themselves, and that there are no wrong answers;
- that they can stop at any time without giving a reason, and that stopping costs them nothing;
- that they appear in the results only as P1 to P5, and that nothing identifying is kept beyond
  what the institution's rules require;
- where the notes are kept, who can read them, and when they are deleted.

**Check the institution's data-protection rules before the first session.** At JLU that means its
data-protection guidance and, if audio or screen is recorded, probably its data-protection
officer. This page is not legal advice, and the institution's process governs.

## The five tasks

The same five, in this order, every time. Each is written on a card and read aloud, and nothing
else is said about it.

1. **Build it.** From a fresh checkout on the participant's own machine or a clean account:
   *"Install what jichi needs and build it, using its documentation."* Done when `./jichi
   --version` prints a build line.
2. **Ask it something.** *"Connect jichi to the model you have been given, and ask it what
   this repository is."* The facilitator provides the credentials, for a free `jlu/*` model on the
   HRZ gateway or a local LM Studio server, never a priced one ([`../CLAUDE.md`](../CLAUDE.md)).
   Done when the answer describes the repository.
3. **Approve an edit.** *"Ask jichi to add a line to the README, and let it."* Done when the
   file has the line and the participant approved it at the prompt.
4. **Undo it.** *"Put the README back the way it was."* Done when `git diff` is empty. How they
   find `/undo`, or do not, is the finding.
5. **Report the platform.** *"Send the developers a report about how jichi ran on this machine."*
   Done when the participant has filled in the template in
   [`PLATFORM_TESTING.md`](PLATFORM_TESTING.md) section 5.

## During a session

- **Think aloud, and only that.** Ask the participant to say what they are looking at and what
  they are trying to do. When they go quiet, say *"keep talking"*. Never ask *"why?"*: reporting
  what you attend to leaves the task alone, and explaining it changes the task (Ericsson & Simon).
- **Do not coach.** Hints only when the participant is stuck, meaning five minutes with no
  progress or a direct request, and each one is recorded as help, with what was said.
- **Time each task** from the card being read to its "done" condition, or to giving up.
- **Record the build** (`jichi --version`'s `build:` line), so the report names the commit it
  describes.

## What is recorded, per participant, per task

| Field | What goes in it |
|---|---|
| done | yes / with help / no |
| time | minutes, from the card to done or to giving up |
| help | each hint given, word for word, and what prompted it |
| errors | each wrong turn, marked as a **slip** (the right intention carried out wrongly, such as a mistyped command) or a **mistake** (the wrong intention carried out correctly, such as looking for undo in the config) (Reason) |
| quotes | what the participant said at the moments that mattered, verbatim |
| where | the page, command or prompt each problem happened at |

## After the sessions

1. **List the problems.** One row per distinct problem, merged across participants.
2. **Give each a severity**, agreed by two people if two people were present:
   0 not a problem · 1 cosmetic · 2 minor, slows the person down · 3 major, needs help or causes
   an error · 4 blocks the task.
3. **Say where each one lives**: a document, a message, a command or a default.
4. **Write it up** as a dated analysis note in [`analysis/`](analysis/): participants as P1–P5,
   the build, the problems by severity, and for each problem either the milestone that fixes it or
   the [`DEFERRED.md`](DEFERRED.md) row that records it. State what the study could not show.
5. **Tick the checklist item** in [`INTERFACE_TUTORIAL.md`](INTERFACE_TUTORIAL.md) with a link to
   the note. That is the first time it will have been done.

## Running it again

The study is meant to be repeated after large interface changes, with new participants. The
same five cards, the same script and the same record sheet make two runs comparable in the only
way this size allows: *this problem is still there* or *this problem is gone*.
