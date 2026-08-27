# Learning from errors: what actually changes behaviour, and what only looks like it

*Written 2026-08-22 (M537), after a session in which I made six mistakes and fixed
four defects that were themselves records of someone else's. The question this page
answers was asked directly: "analyze your mistakes, and find out how they can be
prevented next time. What do you need to do to learn from mistakes, and errors?"*

*This is not a page about being careful. Care is the thing that was already being
applied when every mistake below happened.*

---

## 1. The session's errors, stated plainly

Six, in the order they happened. Each is stated with what I actually did, not with
what I intended.

| # | Error | Cost | Caught by |
|---|---|---|---|
| 1 | A smoke check's `awk` extracted 2 names where the branch listed 6, so a drift check compared a subset against a superset and passed trivially | 0 (caught before merge) | the perturbation run of an *adjacent* check |
| 2 | A check asserted "no stray hint log in the worktree" — and the worktree is deleted when the attempt ends, so the count could only ever read 0 | 0 | the perturbation run of the check itself |
| 3 | `find "$ws/.jichi.d"` searched a path that never existed (`.jichi.d` lives under `$HOME`) | 0 | a debug print, after error 2 |
| 4 | A "references" floor counted 9 where there were 7 call sites, because the bare symbol name also matched two comments *mentioning* it | 0 | reading the number and not believing it |
| 5 | A new line in `/context` did not repeat the `hist != NULL` guard its neighbour three lines above carries. Segfault | one gate cycle (~25 min) | `make ci` |
| 6 | Ran `jichi undo` to find out whether the subcommand existed. Reverted 768 files in the working tree | one recovery, ~15 min; nothing lost | `git status`, on a hunch |

Errors 1–4 are all one error: **a check that verifies its own conclusion rather
than its own coverage.** Error 5 and error 6 are also one error: **an action taken
without reading the context it acts in.**

## 2. The single root cause, and why "be careful" cannot touch it

Every one of the six is an instance of *verifying the proposition instead of the
precondition*.

- Error 1: verified the check passes. Did not verify the extraction sees the whole set.
- Error 2: verified the assertion holds. Did not verify the assertion can be violated.
- Error 4: verified the number clears the floor. Did not verify the number counts what its name says.
- Error 5: verified the new line computes the right value. Did not verify its input can be `NULL`.
- Error 6: verified the subcommand name resolves. Did not verify what resolving it *does*.

In all six the *intended* claim was true. What was false was a claim I had not
examined because the intended one had already come back green. **Green is the
condition under which I stop looking**, so any defect that lives in the unexamined
half is systematically invisible to effort. Applying more care to the examined half
does nothing; care was never the scarce resource.

This is not a new observation in this repository. It is written into `CLAUDE.md` as
*"audit the universe, not the result"*, and into
[`docs/reading/KIROKU.md`](../reading/KIROKU.md) as shape 3. **I have the rule, in a
file that is delivered to me on every single turn, and I violated it four times in
one session.** That fact is the actual subject of this page. A rule I can quote and
still break is not a control; it is a description.

## 3. What actually prevents recurrence, ranked by how little it depends on me

The ranking is the whole point. A prevention mechanism's value is inversely
proportional to how much it needs me to remember it at the moment of the mistake —
because the moment of the mistake is precisely the moment I am not remembering it.

**Tier 1 — fires whether or not anyone remembers (a lint, a gate, a type).**
`make ci` caught error 5 without being asked. `posix_utils_lint` has caught a
GNU-ism in a brand-new driver of mine in *four consecutive milestones* — I have
still not internalised `sed -i`, and it has still not shipped, because the lint does
not depend on my internalising it. This is the only tier that works at full
strength on a bad day.

**Tier 2 — a ritual with a mechanical output.** The red-before-green perturbation
(`tests/teeth.sh`) caught errors 1 and 2. Weaker than tier 1 because I must choose
to run it, stronger than prose because its output is a fact rather than a
judgement: the check either went red or it did not. Errors 1 and 2 were both caught
here, and error 2 was caught *inside the very run* that was testing something else
— which is what a ritual buys you.

**Tier 3 — a rule in a file delivered on every turn (`CLAUDE.md`).** Necessary,
demonstrably insufficient. It converts "I did not know" into "I knew and did not
apply", which is progress in diagnosis and not in outcome. Worth writing anyway,
because tier 1 cannot cover judgement, and because the rule is what makes the
mistake *recognisable* when it happens — errors 1, 2 and 4 were all *named* fast,
by me, using vocabulary from these files.

**Tier 4 — a narrative record (`ANECDOTES.md`, this page).** Does not prevent
anything on its own. Its function is different and real: it is where the *reasoning*
lives, so that a future reader — including a future me with no memory of today —
can tell whether a tier-1 mechanism is still earning its place, or disagree with it
on the evidence rather than guess at the intent. Cheap to write, and the only tier
that survives the mechanism being deleted.

**Tier 5 — an intention.** "I will be more careful with destructive commands."
This has no mechanism, no output, and no way to fail visibly. It is what I would
have written instead of this page if the question had not been asked directly.

## 4. So: what do I need to *do*?

Four things, each mapped to a tier, each already done or scheduled rather than
promised.

**(a) Convert every lesson to the lowest tier it will fit.** For this session:

- Error 6 → **tier 1 for the product**: `undo` now prints its blast radius (M537,
  gated by `undo_scope.sh`). This does not stop me running `undo`; it makes the
  consequence announce itself in one line rather than waiting to be discovered.
  That is the achievable win, and it protects every future operator, not just me.
- Error 6 → **tier 3 for me**: a rule in `CLAUDE.md` — classify by effect before
  probing; never answer an existence question by execution; probe in a workspace
  you are willing to lose.
- Errors 1, 2, 4 → **tier 2, made per-check rather than per-driver.** The ritual was
  being applied to *drivers* ("this driver went red") when the unit that can be
  vacuous is a *check*. Perturb once per check, and read the output for the check
  you perturbed rather than for the driver's total.
- Error 5 → nothing new needed. Tier 1 already covered it. The correct response to
  a mistake the gate catches is to note the cost and move on, **not** to add a
  process on top of a working control.

**(b) Make the failure mode cheap rather than impossible.** Errors 1–4 cost nothing
because the gate and the ritual stood between them and a merge. Error 6 cost fifteen
minutes because the push had already happened. **In every case the protection was
structural and pre-existing, and in no case was it my attention.** Therefore the
useful investment is always in more structure, and effort spent on resolutions is
effort not spent on the thing that has actually worked every time.

**(c) State the honest limit of self-correction.** I will make error 6's *class*
again: an action whose effect I did not check, in a context I read for a different
purpose. What I can change is the cost — which is what M537 does — and what I can
add is one mechanical check at the point of decision. What I cannot do is promise
the judgement, and a page that promised it would be the least honest document in
this repository.

**(d) Record the mistake at full size, including the parts that are unflattering
and the parts I got wrong twice.** In this incident I also mis-diagnosed the
recovery: I said three untracked binaries were created by the `undo`, when they were
pre-existing ignored artifacts exposed by the restored `.gitignore`. That correction
is in [ANECDOTES #66](../ANECDOTES.md) beside the main account, because a record
that quietly fixes its own errors teaches a reader to trust it exactly as much as it
deserves, which is less than a record that shows them.

## 5. The connection to what this project is for

Asked what one question's answer would solve the riddle of jichi, the operator
answered: **the record of how it was made.** This page is a test of whether that
answer is load-bearing or decorative.

If the record is the artifact, then an error is *raw material* and the only waste is
an error that leaves no mechanism behind. By that measure the session's accounting
is: six errors, four of which produced a gate or a check that did not exist before
(M535's floored drift check, M536's three new drivers), one of which produced a
product fix protecting every future operator (M537), and one — error 5 — which
produced nothing except a reminder that the existing control works. That is an
acceptable ratio, and it is only acceptable because the mechanisms were built
*before* the mistakes, by earlier sessions doing exactly this.

The uncomfortable corollary, and the reason this page is worth its length: **the
four gates I wrote this session will mostly catch someone else's mistakes, not
mine.** I will not remember writing them. That is the actual argument for the
record — not that it improves the author, but that it is the only part of the author
that persists.

## Where this fits

- [`docs/ANECDOTES.md`](../ANECDOTES.md) #66 — the incident itself, in full.
- [`docs/reading/KIROKU.md`](../reading/KIROKU.md) — the nine shapes a record's
  defects take; errors 1–4 above are shape 3, and this page is the method behind it.
- [`docs/TEST_INTEGRITY.md`](../TEST_INTEGRITY.md) — the incident register for
  checks that did not check.
- [`CLAUDE.md`](../../CLAUDE.md) — where the tier-3 rules from §4(a) actually live,
  because a rule in an analysis page is a rule nobody is served.
