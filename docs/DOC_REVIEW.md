# Reviewing the documentation — the rubric, and how to run a pass

*The instrument, written down so the next pass does not have to reinvent it. It
exists because the two most valuable findings of the M392 review were **prose that
is internally coherent and simply untrue of the program** — the class no lint in
this repository can catch. Lints own the vocabularies (25 of them, see
[TEST_INTEGRITY.md](TEST_INTEGRITY.md) §"Prefer a lint to an audit"); this page owns
what is left, which is everything a reader experiences.*

## 1. The audience is one person, and it is not you

Review for a **self-learner working alone**: no instructor, no colleague, possibly
limited prior programming experience, a Linux/macOS terminal, and **nobody to ask**.
That last clause does the work. It converts "this is a little unclear" into "this
ends the run", and it is the reason the M392 review's two hard stops were found on
the *first page* while three reviewers had walked past them before.

Every finding should be phrased against that reader: *what do they do next?* A
finding with no way forward is not a finding, it is a complaint — the same rule the
code follows for refusals (M342/M360).

## 2. The rubric — six questions per page

| | Question | The failure it catches |
|---|---|---|
| **What** | Is the thing defined before it is used? | Jargon on first use: "envelope", "fence", "posture", "role", "chokepoint", "green", "quantized", "TAP", "lint", "invariant" |
| **Where** | Which file, directory, config key, command's output? | "Set it in your config" — *which* config, at *which* path |
| **When** | When to use this, **and when it is the wrong tool**? | A page that only sells; the reader cannot tell if it applies to them |
| **How** | Copy-pasteable commands, with expected output so success is distinguishable from failure? | The command that exits 2; the sample output no build can produce |
| **By whom** | Who acts — the human, the agent, or the machinery? For safety features: who decides, who is protected, from what? | A propose-only agent whose document the reader waits for forever |
| **Decision** | Where something is recommended, is the choice stated with its **reason** and its **rejected alternative**? | Advice that cannot be evaluated, and rots invisibly when the reason changes |

Then three cross-cutting questions that produced the most valuable findings:

- **Is the safety net described as more universal than it is?** Every gap M392
  found leaned this way. Check the *conditions*: rollback needs a verifier; the
  checkpoint net excludes git-ignored files; `snapshots` is off under auto-lite.
- **Is this page reference or tutorial, and which does its reader need?** Most
  feature pages are written for someone who already knows, and are met by someone
  who does not.
- **What does the page mention but never explain?** That list is the register of
  what to write next, and it is worth more than any single fix.

## 3. How to run a pass

1. **Slice the corpus** so each reviewer reads a coherent journey, not a random
   set: the entry path *in order* (router → build → install → setup → first
   session → config → doctor); the curriculum and its tasks; the feature/reference
   pages; the newest material (most likely to be wrong, least likely to be read).
2. **Reviewers are read-only.** Say so explicitly, and say "return a report" —
   otherwise a capable reviewer starts fixing, and you lose the ability to verify.
3. **Ask for ranked findings with an anchor and a suggested fix**, capped (six per
   page). Say: *prefer ten concrete findings to forty vague ones*, and *say plainly
   where a page is already fine*. Both instructions materially improve the output.
4. **Ask what is already good, by name.** Without it, the next pass sands off the
   best writing in the corpus — and the M392 reviewers named things worth
   protecting that no author would have thought to defend.
5. **Verify every finding before acting on it.** Three of M392's reviewer claims
   needed correction; several others were *worse* than reported. Run the command,
   read the function, check the default. This is not distrust of the reviewer — it
   is the same rule the project applies to itself, and it is where the sharpest
   findings actually came from.
6. **Fix the blockers; register the rest.** A review generates more work than one
   pass can hold. Fix every hard stop, every false safety claim, every broken
   command, and every teaching defect that makes a faithful reader fail. Then write
   the remainder into [DEFERRED.md](DEFERRED.md) with reasons, and the full report
   into `docs/analysis/`. Pretending to fix it all is the one outcome worse than
   deferring honestly.

## 4. What is worth turning into a lint afterwards, and what is not

The M392 review produced two lints and one refusal, which is the right ratio:

- **Command *shapes*** are lintable — the argument parser refuses some forms
  deterministically, needing no model or network
  ([doc_commands_lint.sh](../tests/smoke/doc_commands_lint.sh), M394).
- **Documented *defaults*** are lintable — the parser's default is in the source
  next to the comment claiming it
  ([config_defaults_lint.sh](../tests/smoke/config_defaults_lint.sh), M393; and its
  own stated exclusion was where the two worst findings lived).
- **Prose is not lintable.** A fabricated `doctor` sample and a missing
  git-ignore caveat are coherent, well-written and false. Attempting a prose lint
  would produce a checker that passes exactly the defects that matter. This is why
  the pass repeats with fresh eyes instead.

## 5. When to run it again

- Before the **public release** — the first external reader gets no benefit of the
  doubt.
- After a **wave of new pages** (M386 added six tutorials; seven of their commands
  were broken, and the review was three days later).
- After any milestone that **changes a default, a refusal, or a safety condition**
  — those are the changes whose documentation silently becomes false. M375 added a
  refusal on a Tuesday; by Thursday the page telling that bug's story was still
  teaching the bug.

## 6. The worked example

[`analysis/2026-08-12-docs-review.md`](analysis/2026-08-12-docs-review.md) is a
complete pass: four reviewers, 30 pages, ~35 findings fixed, 21 register items and
six structural findings deferred with reasons, and the two findings that argued for
doing this again rather than automating it. Read it before running your own — not
for its conclusions, but for the shape of an honest report.
