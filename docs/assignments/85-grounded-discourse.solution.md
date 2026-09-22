# 85 — grounded discourse: a worked example

*The reference `DISCOURSE.md`, with the reasoning behind each move. Read this
**after** you have written your own — a worked example read first becomes the
answer you copy, and the point of the task is the discussion you actually have.*

The subject below is a real claim this project makes about itself, and the
counter-argument is one that was actually raised and actually measured, on
2026-09-21. Nothing in it is invented for the exercise. That matters: a worked
example whose objection is a soft one teaches you to write soft objections.

**What makes this pass the grader:** five sections, `Objection:` before
`Reply:`, five `file.c:symbol` citations that resolve against this tree, a page
cited by name, and a Revision that says something the Claim did not. **What
makes it a good discussion is none of those things** — it is that the objection
was one the author did not want to be true, and that the claim came out
narrower than it went in.

---

<!-- DISCOURSE.md -->
# Does this tree obey its own rule about `sprintf`?

## 1. Claim

`CLAUDE.md` states, as an invariant: **never call `sprintf`.** My claim is the
strong reading of that sentence — that no first-party source file in this tree
calls `sprintf`, and that a lint holds the property rather than a one-off audit
having once found it true.

## 2. Grounds

- The rule is written down in `CLAUDE.md`, under "Conventions & invariants":
  *"Never call `sprintf` — `jc_snprintf` is bounded by construction, and
  `tests/smoke/sprintf_lint.sh` enforces it."*
- The bounded replacement exists and is a real function, not a macro that
  forwards to the unsafe one: declared at `include/jc_snprintf.h:jc_snprintf`
  and defined at `src/platform/jc_snprintf.c:jc_snprintf`.
- It is used, widely rather than ceremonially — 99 files call it. Two I opened
  and read: `src/util/jc_lease.c:jc_snprintf` and
  `src/chat/jc_agent.c:jc_snprintf`, both bounding into a fixed-size buffer
  with `sizeof`.
- The claim is **already false in its strong form**, and the grounds say so:
  `src/json/cJSON.c:sprintf` holds three genuine `sprintf` calls, at lines 865,
  887 and 889. They are named individually in the lint's allowlist, with their
  exact text.
- Running `sh tests/smoke/sprintf_lint.sh` here prints
  `ok 1 - scanning 327 first-party source files` and
  `ok 2 - no raw sprintf outside the audited allowlist`.

## 3. Warrant

A lint, unlike an audit, is re-run on every gate, so a green today is evidence
about **today's tree** and not only about the day somebody last looked. That is
what carries "the rule is stated" to "the rule holds": the project's own
formulation is *prefer a lint to an audit — an audit finds what it knew to look
for, once; a lint finds it forever.*

The second half of the warrant is the allowlist. Three exceptions that are
enumerated by their exact source text are not a hole in the rule, because a
fourth `sprintf` — or a change to any of those three lines — does not match and
is reported. An exception list that named only the *file* would be a hole.

## 4. Counter-argument

Objection: **a lint's green is a claim about what the lint read, not about the
tree, and I have not checked those are the same thing.** The warrant above
quietly assumes the scan covered the corpus. `sprintf_lint.sh` builds its file
list into a variable and hands it to `awk` — and if that list were ever empty,
`awk` would read standard input instead, find no `sprintf` in it, and report
`ok 2`. This is not hypothetical. Measured on 2026-09-21 against a copy of this
tree with `src/` absent, the driver **hung for 45 seconds** and then, killed,
printed exactly that `ok 2`. A `< /dev/null` guard has since been added, so run
it that way today and it finishes in under a second — and **it still prints
`ok 2`**, having read nothing. Its floor check does fire, `not ok 1 - scanned
only 0 files`, but `t_fail` records the failure and *continues*, so the vacuous
green is printed anyway, directly beneath the red that should have prevented
it. The guard fixed the hang. It did not fix the green, and the green is what
my warrant leans on.

Worse for my claim: the floor is **100** files and the real corpus is **327**.
So a loss of two thirds of the sources would pass the floor silently, and
`ok 2` would then be a true statement about a third of the tree presented as a
statement about all of it.

Reply: the objection is right about the mechanism and does not fully land on
the claim, for one checkable reason — I read the count. The driver printed
`scanning 327 first-party source files`, and 327 is the number of `.c` and `.h`
files under `src/` and `include/` in this checkout. So on *this* run the scan
and the corpus were the same set, and I know it because the check prints its
universe rather than only its verdict. What I cannot say is that this is
guaranteed: it was true here and the floor would not have told me if it were
not.

## 5. Revision

The strong claim does not survive, and the surviving one is narrower in two
places.

**No first-party file in this tree calls `sprintf`, except three enumerated
calls in `src/json/cJSON.c` that the lint allows by their exact text — and on
this run that statement covers all 327 sources, because the driver printed its
universe and the number matched the tree.**

What I withdraw is the general form. "A lint holds this property" is not
something the green establishes; it is established by the green **plus** the
printed count, and only for runs where somebody reads the count. On a run where
the corpus silently halved, the floor of 100 would pass and the same `ok 2`
would appear. The property I can defend is about this run. The property I
wanted — that the rule is *maintained* — needs the floor raised to near the
real count before the green means it on its own.
<!-- /DISCOURSE.md -->

---

## Why each move came out the way it did

**The claim was deliberately the strong reading.** "No file calls `sprintf`" is
refutable in one `grep`, and it was refuted — by the author's own grounds, in
section 2. A claim you can only support is usually one you have not made
specific enough to be worth arguing.

**Section 2 contains a fact that damages section 1, and that is the correct
place for it.** Grounds are what you found, not what helps. Putting the three
`cJSON.c` calls in the counter-argument instead would have been a small dishonesty
with a large effect: it would let the objection look like something an opponent
brought, rather than something the evidence said immediately.

**The objection is about the universe, not the result.** The weak version of
this move attacks the claim ("maybe there is a `sprintf` somewhere"). The strong
version attacks the *warrant* — it grants the green and asks what the green is
evidence of. That is the move worth practising, and the project has a name for
it: *audit the universe, not the result.*

**The reply concedes the mechanism and rescues the claim on a checkable
detail** — the printed count. Note what it does not do: it does not argue that
the empty-universe case is unlikely. Unlikeliness is not a reply to "your
evidence does not distinguish these two cases"; a number that distinguishes
them is.

**The revision is shorter than the claim and says less.** That is the usual
shape of an honest one. It also names what would make the strong claim true
again — a floor near the real count — which turns the discussion into something
someone can act on rather than a verdict.
