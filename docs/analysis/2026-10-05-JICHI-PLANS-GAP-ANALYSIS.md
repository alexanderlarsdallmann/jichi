# JICHI PLANS CORPUS — gap analysis (the status-lint blind spot)

## TL;DR

*Read-only survey of `docs/plans/` at HEAD `1b71247d` (42 entries: 41 `.md`
plans + 1 `.scm`), cross-checked against `docs/ROADMAP.md` and the git
history, 2026-10-05. The question: does the plan corpus drift from shipped
reality the way three other jichi inventories already have? **Yes — and the
drift is provable, small, and exactly the class the project's own lints were
built to catch.** Of 11 plans that assert their own status, 5 say *shipped*
and 5 say *not built*. **Four of the five "not built" plans are stale against
the tree they sit in**: `M92`, `M93`, `M94-M95` (each cites a milestone the
ROADMAP already marks done — M92 done 2026-07-14, M93/M94 done 2026-08-10)
and `files-and-structures` (the ROADMAP *names the file* and says "complete:
76-80 shipped as the plan named them"). The fifth, `tui-fence-grant`, is a
**frozen-but-true control**: its M388 shipped the *design decisions* (the
plan's own scope), not the implementation, so "DESIGNED, not built" is
accurate. None of the 5 "shipped" claims is wrong — every cited milestone
exists and is marked done. The gap: there is **no plan-status lint**. The
project runs `milestone_currency_lint.sh`, `changelog_coverage_lint.sh`,
`reading_refs_lint.sh` and `docs_counts_lint.sh` precisely because
self-asserted "how current are we?" prose froze before — yet the one
inventory written *before* the code and left unedited (the plans' own words)
is the one with no check.

## 1. What the corpus is

`docs/plans/` holds 42 entries. 41 are `.md` design/plan documents (dated
2026-07 through 2026-10-03) plus three named by milestone (`M92-…`, `M93-…`,
`M94-M95-…`), and 1 is a Guix S-expression
(`2026-08-guix-bench-system.scm`). Each plan carries, in its header, one of:
an explicit `**Status:** …` line, a prose status, or nothing. The plans are
written *before* the code by the project's own loop ("this doc committed
before implementation, per the loop") — so they are the natural place a
"planned → done" transition should be recorded, and the natural place it
gets forgotten.

## 2. The census (every claim verified against ROADMAP + git)

### 2.1 The 11 status-bearing plans

| Plan | Status line says | ROADMAP/git says | Verdict |
|---|---|---|---|
| `2026-07-python-free-testing` | COMPLETE (M209, B1–B6, M210–M217) | all 11 milestones `— done` | **true** |
| `2026-08-data-seams-first-slice` | done (M420) | M420 `— done` | **true** |
| `2026-08-reading-guides` | COMPLETE — M222–M225 shipped | M222 `— done` (+ M223-225) | **true** |
| `2026-08-trace-chapters` | COMPLETE — all four shipped (M508…) | M508 `— done` | **true** |
| `2026-08-public-snapshot` | EXECUTED (M620); advanced M621–M624 | M620 `— done` | **true** |
| `M92-budget-outcome-clarity` | **planned** (committed before impl) | M92 `— done`, impl 2026-07-14; plan last touched **2026-07-27** | **STALE** |
| `M93-superseded-read-elision` | **planned** | M93 `— done`, impl 2026-08-10; plan last touched **2026-07-06** | **STALE** |
| `M94-M95-followups` | **planned** | M94 `— done` (2026-08-10); M95 impl 2026-07-06 (`036eb485`); plan last touched **2026-07-06** | **STALE** |
| `2026-09-files-and-structures` | **planned, not built** (2026-09-16) | ROADMAP *names the file*: "the files-and-structures course is complete: 76-80 shipped as the plan named them" | **STALE** |
| `2026-08-tui-fence-grant` | **DESIGNED, not built** | M385 (the *what*) `— done`; **M388** (2026-08-11) "the TUI fence-grant affordance: comprehensive design decisions… — done"; plan last touched 2026-08-11 | **frozen-but-true** (M388 shipped the design, not the code) |
| `2026-08-19-merging-hrz-model-info` | "coordination document, written from master at 24e462c" | — (not a build claim) | n/a |

### 2.2 The 29 no-status plans

The remaining 29 carry no `Status:` line at all — their state is only
reconstructable from prose or ROADMAP back-references. Two controls bound
the risk:

- **Freshest, correct by freshness**: `2026-10-03-learners-models-and-drives`
  says "Nothing proposed here has been built" and, *in the same file*,
  "Added on 2026-10-04: step 1.1 is done. M897 records `ttft_ms`… Step 1.2
  is not done." Plans written recently are self-correcting while they are
  current.
- **Oldest, stale by age**: the 2026-07 `M92/M93/M94-M95` set — written
  before implementation by the project's own loop and never revisited *for
  status* (M92's only post-ship touch is the M170 rename, §3's footnote).

The pattern is **aging, not carelessness**: the drift concentrates in the
plans old enough that their milestone shipped and the plan was left as a
dated artifact. The newest plan updates its own status inline.

## 3. Why this is "definitely lacking" (alternatives and their status)

- **"The plans are frozen artifacts; status is deliberately not updated."**
  Rejected — `files-and-structures` is *named in the ROADMAP as complete
  while its own header still says planned, not built*. A frozen-artifact
  doctrine would still let the ROADMAP point at the file with the truth.
  The 2026-10-03 plan also updates its own status inline, so the house
  practice is "correct it when you are near it" — which means the drift
  is an omission, not a convention.
- **"milestone_currency_lint already covers this."** Rejected — it checks
  ROADMAP vs the *highest milestone other docs cite*, i.e. currency of the
  ROADMAP. It never opens `docs/plans/*` and never reads a `Status:` line.
  (Its own header comment names the M463 defect: four lints resolving to
  the same expression, all green while the ground truth froze — this is
  the same failure mode, one inventory over.)
- **"Fix the four files and move on."** Rejected as the *whole* fix — it
  patches instances, not the class. The class-fix is the lint: a plan that
  cites milestone M as its payload must not say "planned/not built" once
  the ROADMAP says "M — done" (the `files-and-structures` case needs the
  stronger form: ROADMAP names the plan file).
- **"The 29 no-status plans are the bigger gap."** Partially true, but
  unprovable as an error: absence of a status is not a false status. The
  lint can *recommend* a status field for plans whose payload milestones
  are all done; it cannot assert drift without the claim.

One footnote the table earns: `M92-budget-outcome-clarity` was last touched
**2026-07-27 — 13 days *after* M92 shipped (2026-07-14)** — and still says
"planned (this doc committed before implementation, per the loop)." The
touch was the M170 project rename (2026-07-27): a mechanical sweep, not a
status review — which is exactly what a header check that survives
reformatting is for.

## 4. The one thing the plan corpus definitely lacks

**A plan-status currency lint.** A shell script — the corpus is 41 `.md`
headers, so this needs no new machinery — that:

1. For every `docs/plans/*.md` whose header matches
   `\*\*Status:\*\* (planned|not built|DESIGNED[^,]*, not built)`,
   extract the milestone(s) the header cites (e.g. "M92", "M636d").
2. Check the ROADMAP: if that milestone is marked `— done`, the plan
   must carry a supersession note (a later "Status" line, an inline
   "Added on …: step N is done", or a ROADMAP pointer). Fail otherwise.
3. The `files-and-structures` form: if the ROADMAP names the plan file
   *and* says "complete/shipped", the file's first header status must not
   still say planned/not built.
4. Report-only for the 29 no-status plans whose payload milestones are
   all done — a recommendation, not a failure, since absence is not a
   false claim.

It would go red today on exactly four files (M92, M93, M94-M95,
files-and-structures), white on tui-fence-grant (M388 shipped the *design*
— the lint must key on the plan's own scope, not any adjacent milestone),
and green on the five shipped-claims.

## 5. What this means for the other lints

The M463 defect story (three lints resolving to the same ROADMAP
expression, all green while reality moved) is now a *pattern of three*:
the ROADMAP froze (M463, fixed by currency lint), the reading-guide
anchors froze (fixed by `reading_refs_lint`, but with the call-site blind
spot this register documented in analysis 35), and the plan headers froze
(not yet fixed). Each was caught by a read, not a lint. The plan-status
lint is the third instance of a two-line class: *an inventory that
asserts its own state has no check against the state it asserts*. The
register's honest position, after 36 audits, is that this class has
exactly these three known members and a known detector.

## 6. The corrections (ready to apply)

1. `M92-budget-outcome-clarity.md` — append: "*Status updated:* M92
   shipped 2026-07-14 (see ROADMAP); this plan is kept as the pre-build
   design record."
2. `M93-superseded-read-elision.md` — same, M93 shipped 2026-08-10.
3. `M94-M95-followups.md` — same; M94 2026-08-10, M95 2026-07-06.
4. `2026-09-files-and-structures.md` — same; ROADMAP names the course
   complete ("76-80 shipped as the plan named them").
5. `tests/smoke/plan_status_lint.sh` — the class-fix above (green after
   1–4; red without them).

## 7. Limitations (honest)

- The stale-set is bounded by what is *asserted in the header*. A plan
  with no status field and no milestone in its header cannot be checked
  mechanically; those stay prose-reconstruction (29 files).
- The tui-fence-grant judgment ("M388 shipped the design, not the code")
  rests on reading M388's ROADMAP entry as scoping the *design
  decisions*; if M388 was later superseded by an implementation milestone
  this analysis missed, the verdict flips — the absence of any
  `fence_grant` symbol in `src/`/`include/` is the check that would
  catch that, and it passed on 2026-10-05.
- "The 29 no-status plans" risk understates real staleness: a plan that
  quietly shipped has no false claim to lint, but a reader cannot tell.
  The report-only mode (rule 4) is the mitigation, not the answer.
- All ROADMAP citations were matched as `### Mnn — … — done` lines in
  `docs/ROADMAP.md` at HEAD; milestone numbering is the project's own
  (M1–M1001+), and the "76-80" in the files-and-structures entry refers
  to curriculum pages, not milestones — the quotation is used verbatim
  because the ROADMAP naming the plan file is the load-bearing fact.

## 8. Appendix: reproducible (all from the jichi checkout)

    # the census: status-bearing vs not (42 entries = 41 .md + 1 .scm)
    ls docs/plans/ | wc -l
    for f in docs/plans/*.md; do grep -qiE '^\s*\*{0,2}status' "$f" || echo "$f"; done | wc -l   # 29

    # the four stale headers
    grep -H 'Status' docs/plans/M92-budget-outcome-clarity.md docs/plans/M93-superseded-read-elision.md docs/plans/M94-M95-followups.md docs/plans/2026-09-files-and-structures.md

    # the ROADMAP says done
    grep -E '^### M92 —|^### M93 —|^### M94 ' docs/ROADMAP.md
    grep -n "files-and-structures course is complete" docs/ROADMAP.md

    # the implementation dates (plan vs shipped)
    git log -1 --format="%ad" --date=short -- docs/plans/M92-budget-outcome-clarity.md
    git log --format="%ad %s" --date=short --all --grep="M95" | head -3
    git log --format="%ad %s" --date=short --all --grep="M94" | head -3

    # the frozen-but-true control: no implementation symbol anywhere
    grep -rn "fence_grant\|fence-grant" src/ include/ | grep -v "design\|plan" | wc -l   # 0
    grep -E '^### M388 ' docs/ROADMAP.md

    # the no-plan-lint claim
    ls tests/smoke/ | grep -i plan        # plan_artifact.sh exists; check what it guards:
    head -8 tests/smoke/plan_artifact.sh     # the M631 .jichi/PLAN.md tool reconciliation --
                                          # not the docs/plans/ corpus: no check reads a plan header's Status

## 9. Verdict

`docs/plans/` is in **good condition**: all five *shipped* claims are
exactly right; of the five *not-built* claims, four are stale in a way that
is provable in under a minute with two greps, and one (tui-fence-grant) is
exactly right *as written* — M388 shipped the design, not the code. The staleness is aging plus one case of *touched-but-not-corrected*
(a mechanical rename commit is the only touch after ship — the 13-day
footnote holds in revised form: M92's 2026-07-27 touch was the M170
project rename, not a status review). The one thing the corpus definitely
lacks is the two-line class-fix: **a plan-status currency lint**, the
third known member of the self-asserting-inventory pattern the register
has now measured three times (ROADMAP@M463, reading anchors@#35,
plan headers@here). Applying §6's five corrections closes the drift and
makes the class detectable. Revisit when: `plan_status_lint.sh` is green
and the four headers carry supersession notes.

*Analysis 37. Read-only against jichi at HEAD `1b71247d`.*
