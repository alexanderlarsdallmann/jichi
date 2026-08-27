# The last two red checks: `grep -o '[0-9]*'` prints nothing on OpenBSD

**Date:** 2026-08-18 · **Milestone:** M481 · **Result:** OpenBSD goes from
*Partly verified — 207 of 209* to **Verified — the full gate**: smoke **OK, 209 of
209 drivers, 1,108 checks, 0 failures**. Every measured platform in the matrix is
now green.
· **Predecessors:** [`2026-08-18-openbsd-remeasure.md`](2026-08-18-openbsd-remeasure.md)
(which narrowed these two and deliberately stopped),
[`2026-08-18-netbsd-first-row.md`](2026-08-18-netbsd-first-row.md) (which supplied the
instrument that solved them).

---

## 0. The state this started from

`sessions_footprint` and `turn_scratch` had failed on OpenBSD for months, across
three sessions, with the same message:

    not ok 2 - could not read the /context arena gauge (before='' after='')
    not ok 2 - could not read the /context turn-scratch gauge

M479 narrowed them and then **stopped on purpose**, because that milestone's own
subject was writing down a mechanism that fits a symptom without testing it. What
it established:

* `jc_context.c:200` emits `"Arenas: session %lu KB used"` **unconditionally** —
  no platform guard, so the product cannot be silently omitting it;
* `ptydrive`'s own `expect "Arenas: session" 10` **succeeds twice** in the same
  run, so the string demonstrably reached the terminal;
* the driver's grep of that same transcript finds nothing.

So the defect was somewhere between "ptydrive saw it" and "grep didn't", and the
product was exonerated. That is where it sat.

---

## 1. What changed: a second BSD that passes

M480 brought up NetBSD, and **both drivers pass there** — same source, same
architecture, a sibling BSD. That converts a months-old mystery into a diff, and
the diff is short because both drivers share one pipeline, character for character:

```sh
smoke_plain "$tmp/fp.log" \
    | grep -o 'Arenas: session [0-9]* KB' \
    | grep -o '[0-9]*' > "$tmp/kb"
```

One probe settled it — no driver run, no PTY, no transcript:

```
$ printf "Arenas: session 123 KB\n" | grep -o "Arenas: session [0-9]* KB" | grep -o "[0-9]*"
Linux    (GNU grep 3.11)      -> 123
NetBSD   (GNU grep 2.5.1a nb1) -> 123
OpenBSD  (grep version 0.9)    -> <nothing>
```

**`[0-9]*` is zero-or-more, so it matches the empty string at every position.** GNU
grep skips empty matches and reports the digits. OpenBSD's grep prints nothing —
**and exits 0.** The pipeline produced an empty file, `before` and `after` were
empty, and the driver said what it was written to say when the gauge is missing.

The first-stage grep, `'Arenas: session [0-9]* KB'`, works fine on all three,
because it carries mandatory atoms around the starred one. The hazard is a pattern
that is nullable **as a whole**.

**Why NetBSD passing is the interesting half.** It is a BSD whose userland ships
*GNU* grep, so it does not exercise this axis at all. Two BSDs are not two samples
of "BSD userland" — this one differs from OpenBSD in exactly the tool under
suspicion. Had NetBSD shipped a BSD grep, the row would have failed the same two
drivers and I would have learned nothing new from it.

---

## 2. The fix, and why it is not a one-character fix

`grep -o '[0-9][0-9]*'` works everywhere and was verified on all three platforms.
It was not what shipped. Both sites now do one pass with no `-o` at all:

```sh
smoke_plain "$tmp/fp.log" \
    | sed -n 's/.*Arenas: session \([0-9][0-9]*\) KB.*/\1/p' > "$tmp/kb"
```

One process instead of two, POSIX BRE, and — the reason for preferring it — the
nullable-pattern class is *removed from the site* rather than made one character
safer. A future edit that widens the digit class cannot reintroduce the bug.

**Proof, before and after, on the platform that failed:**

```
BEFORE   not ok 2 - could not read the /context arena gauge (before='' after='')
         not ok 2 - could not read the /context turn-scratch gauge
AFTER    ok 2 - 10x /sessions grew the session arena by 0 KB (<= 128)
         ok 2 - turn scratch after the turn: 5 KB (<= 64)
```

Those two numbers are the point of the exercise. **0 KB** and **5 KB** are M197's
session-arena fix and M218's per-turn scratch discipline, *measured on OpenBSD* —
two memory-lifetime guarantees that this platform had never been able to report,
both green with a wide margin. The drivers were not merely repaired; the
assertions they exist to make finally happened there.

---

## 3. The lint, and the two ways it was wrong first

Per `TEST_INTEGRITY.md`'s standing rule — prefer a lint to an audit —
`posix_utils_lint.sh` gains checks **15 and 16**, joining the family that already
bans `grep -P`, `\|`, `\b`, `\xNN`, `head -c`, `--include=`, `sed -i` and friends.
15 is a planted-positive self-test; 16 is the corpus assertion.

**Its scope is stated rather than implied.** It flags a pattern that is a *single
starred atom* — `'[0-9]*'`, `'.*'`, `'[a-z]*'`. A compound nullable pattern such as
`'[0-9]*[a-z]*'` slips through: a general nullability test needs a regex parser,
and this catches the spelling that occurred plus its neighbours with **no false
positives on the ~40 other `grep -o` uses in the tier**, every one of which is
correct because it carries a mandatory atom.

Shown to fail without its fix, which is the project's rule for a new test: with the
old pipeline restored, `not ok 16` names the exact `file:line`; restored, 16/16 green.

Then it was wrong twice, both times about itself:

1. **My ERE contained the two-character sequence that `smoke_lint` bans.**
   `\[[^]]*\]` puts two adjacent open-brackets in the file, and `smoke_lint`'s
   bashism check is deliberately blunt — it flags that pair anywhere in a driver
   unless a colon follows. The literal `[` is now spliced in from a variable.
2. **Then my *comment* explaining that fix contained the pair too**, and
   `smoke_lint` does not skip comments. This is M466's lesson in miniature — a
   finding whose own report is corrupted by the thing it reports (there, a stray
   `\b` printed as a backspace and vanished from its own diagnostic).

Both were caught by the **OpenBSD full-tier run**, not locally, because I had
re-run only the lint I had edited. The tier is the unit of verification, not the
driver.

---

## 4. What this says about the other 1,108 checks

Uncomfortable, and worth stating plainly: this defect had the shape the project
already knows. M466 recorded that `changelog_coverage_lint` used a GNU `\b` whose
failure on a BSD grep was **silent**, so it "reported a number while ignoring most
of its input", and PLATFORMS.md carries that as a footnote against FreeBSD's row:
*the count was never a guarantee that every check tested what it claimed.*

This is the same family — a GNU-only regex behaviour, silent on BSD, exit 0 — and
it survived the sweep that followed M466 because that sweep looked for *flags*
(`-P`, `--color=`, `head -c`) and this is not a flag. Nothing about `grep -o` is
non-POSIX; only the emptiness of the pattern is.

So the honest reading of "209 of 209, 1,108 checks" is: every check now *runs* and
*passes* on OpenBSD, and the class of defect that makes a check hollow rather than
red has been caught three times by three different mechanisms (M466 by reading,
M479 by a re-measure, M481 by a cross-platform diff). Checks 15–16 close one more
spelling. There is no argument available that the remaining ones are all sound —
only the observation that each new platform has found fewer.

---

## 5. The row, in full

| | OpenBSD 7.9 (M481) | was (M479) |
|---|---|---|
| build | `gmake WERROR=1` clean, **8 s** | 8 s |
| unit | **12,415 checks / 0 failures** | 12,415 / 0 |
| smoke | **OK — 209 of 209 drivers, 1,108 checks, 0 failures** | 207 of 209, 1,102 |
| declined | 6 (`faults`×3, `child_fds`, `pdf`, `docs_pdf`) | 6 |
| offline surfaces | all four ok | all four |
| verdict | **Verified — the full gate** | Partly verified |

`child_fds` still declines here — OpenBSD has no procfs — so M472's descriptor
fence remains verified on Linux and NetBSD only. That is the one coverage gap this
row does not close, and it is a property of the kernel, not of jichi.

NetBSD reports 1,109 checks against OpenBSD's 1,108; the composition differs
(child_fds contributes three there and none here, and other drivers vary in the
other direction), so the totals are not directly comparable and neither is a
subset of the other.

**Measurement provenance.** The pre-commit verification ran with `--dirty` (the
working tree), since that is the only way to test a portability fix before
committing it, and the before/after proof in §2 was taken the same way. The row
published above is the **clean** run: `git archive HEAD` at commit `a06f613`, 13 ok /
0 failed, build 8 s, 12,415 unit checks / 0 failures, smoke OK 209/209 with 1,108
checks.

The first clean attempt (at `e4a579c`) failed one driver, and it is worth recording
because the cause was my own verification habit rather than any platform. `docs_flags`
reported *"unknown flag --include"*: the M481 addition to `TEST_INTEGRITY.md`'s lint
inventory named `--include=` to describe what `posix_utils_lint` bans, and that lint
checks every `--flag` token in `docs/` against the real parser — so a grep flag
documented in a jichi doc resolved to nothing a user can type. It **failed locally
too**, and I did not see it, because I had verified with a glob (`*_lint.sh` plus
`smoke_lint.sh`) and `docs_flags.sh` keeps its e2e-era name — a fact stated in the
very file I was editing. The `--dirty` run had passed 209/209 because it ships
tracked files with working-tree content, and the analysis doc was untracked then.

So this milestone learned "the tier is the unit of verification, not the driver"
twice, and the second time the unit that was too small was my own glob of the tier.
`make smoke` now reports 209 drivers / 1,124 checks / 0 failures locally.

---

## 6. Lessons

1. **Diagnose by difference.** A second platform that *passes* is worth more than
   any amount of re-reading the failing one: it turns "why does this fail?" into
   "what differs?", and here the answer was one probe long. M480's row was
   justified on other grounds entirely and paid for itself this way.
2. **A tool that exits 0 and prints nothing is the worst failure mode available.**
   Empty output is indistinguishable from an honest "the thing you looked for is
   not there" — which is exactly how the message read, for months.
3. **A ban list of flags does not cover a ban-worthy *shape*.** `grep -o` is
   POSIX; only a nullable pattern is the hazard. The M466 sweep looked for flags
   and could not have found this.
4. **Remove the class from the site, not one character of it.** `[0-9][0-9]*`
   would have worked; a one-pass `sed -n 's/…\(…\)…/\1/p'` cannot regress.
5. **Run the tier, not the driver you edited.** Both of my own lint defects were
   found by the full OpenBSD run after I had confirmed "the lint passes".
6. **A comment can trip the rule it explains.** Twice now in this project a
   diagnostic was corrupted by the construct it was reporting.
