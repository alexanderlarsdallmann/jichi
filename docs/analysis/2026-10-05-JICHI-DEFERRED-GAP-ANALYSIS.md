# JICHI DEFERRED REGISTER — gap analysis (the line that rots)

## TL;DR

*Read-only survey of `docs/DEFERRED.md` at HEAD `1b71247d` (3,803 lines,
316 def-anchors, 53 Open / 46 Closed sections) cross-checked against the
source and against `tests/smoke/deferred_register_lint.sh` (M657),
2026-10-05. The register is one of the most carefully maintained documents in
the project — and it is *self-aware about its own failure mode*, to the point
that it has a lint, and a meta-deferral (`def-104`) about the class this
analysis documents. The one thing it definitely lacks is what its own lint
declines to check and cannot: **verification that a line-numbered anchor
still points at the code it cites.** The lint (check 5) resolves a
`path:line` only as "file exists and has at least that many lines"; it
deliberately does not confirm the line still *holds the subject* — and the
register carries **one open deferral whose anchor has rotted**: the M715
note's decision fact "the envelope raises the iteration cap to at least 200"
points at `src/chat/jc_agent.c:4102`, but that cap now lives at
`jc_agent.c:5276-5277` — 1,174 lines away (it was 24 lines off when the note
was written). The claim is still *true* (it moved), but a reader
following the pointer finds placeholder-re-read code, and the lint stays
green because "has ≥ 4102 lines" is all it asks.

## 1. What the register is

`docs/DEFERRED.md` is "things deliberately not done, with the reason and
where the reasoning lives" — a companion to `DECISIONS.md` (that page: what
was chosen; this one: what was consciously left). Coverage starts at M298
(2026-08-05); items are "removed when done, with the closing milestone
noted." Its census at HEAD:

- **316** `<a id="def-N">` anchors
- **53** `## Open`/`## Narrowed` sections, **46** `## Closed` sections
- **176** def-rows under Open headings, **114** under Closed, and **26** in
  the two *meta* sections (the M326b "check the checkable part" list and the
  M657 "walk of the whole register") — 176+114+26 = 316

The register is not a static list: it carries *corrections* inside rows
("SECOND CORRECTION (M713)…", "THIRD NOTE (M715)…"), a "What a walk of the
whole register found" section (M657), and — tellingly — a section
**"Check the checkable part of a reason BEFORE parking the item (M326b)"**
that records three items parked on assumptions a minute's reading would have
settled. It is a document that has *already audited itself* and written the
findings down. That is the context this analysis has to respect: the gap
found below is not carelessness, it is the specific mechanical check the
register's own lint chose to leave to a human.

## 2. The lint that exists (M657) — and what it declines

`tests/smoke/deferred_register_lint.sh` runs green (5/5) at HEAD, and its
header is unusually honest about scope. It checks **structure**:

1. the extraction reached the page (≥20 open sections, ≥45 open rows);
2. no open heading with zero rows (the M463 failure);
3. no row under an open heading struck-through or marked
   CLOSED/DONE/DECIDED/SETTLED/RESOLVED (the M492 failure, widened at M710
   when a synonym evaded the first version);
4. every platform `PLATFORMS.md` files as *Never compiled* has a row here
   (the M657/illumos failure);
5. **every `path:line` anchor resolves** — file exists and has ≥ that many
   lines.

And it states, in the header, what it does *not* check, for the exact reason
this analysis relies on:

> **NOT checked** — whether a reason is still TRUE. … A lint that tried
> would encode today's answers and rot exactly like the rows it was policing.
>
> Check 5 … verifies the anchor is **RESOLVABLE, not that the line still
> holds the subject.**

So the lint's contract is: a line number is a claim about *today's tree*, and
it checks that claim weakly (resolvable). The strong claim — "this line is
the code the row cites" — is explicitly a human's job. That division is
defensible in the general case (most reasons are judgements, not line
pointers). It is the gap in the specific case of a **factual, line-anchored
decision fact** in an *open* row: that is the one kind of line pointer whose
drift is both mechanically detectable and load-bearing.

## 3. The rotted anchor (the finding)

The M715 note, inside the M657 "walk of the whole register" section (the
item it describes — the "twenty capped `--no-session` runs" corpus — is still
owed, so the fact is live, not historical), records a decision fact for
whoever acts:

> under `--auto` the envelope raises the iteration cap to at least 200,
> whatever `maxToolIters` says (**`src/chat/jc_agent.c:4102`**; measured with
> `mockmodel`: 200 requests and 200 tool calls, then `max_iters`) — so a
> capped `--auto` one-shot is a 200-call run, never a short one.

At HEAD that line does not hold the subject:

- `src/chat/jc_agent.c:4102` is a bare `}` closing a
  placeholder-re-read (`placeholder_run`) block — unrelated to the cap.
- The cap the row cites **is** present, but at `src/chat/jc_agent.c:5276-5277`:
  `if (app->env != NULL && opts.max_iters < 200) { opts.max_iters = 200; }`,
  with a matching comment at `:3689` and `include/jc_app.h:733`. The drift
  predates the growth: at the M715 commit (2026-09-23) the file was 4,700
  lines and the cap sat at `:4126` — the `:4102` the note cites was already
  24 lines off at writing; it is now 1,174 lines off.

The claim is still **true** — the envelope does raise the cap to ≥200 under
`--auto`; the code moved. But the pointer is 1,174 lines off, in a file that
grew past the line the pointer names. Two consequences:

1. **A reader is misdirected.** Following `:4102` to confirm the decision
   fact lands in unrelated code; the confirmation requires finding the *new*
   site, which is exactly the "a minute's reading" the register's own M326b
   section warns about.
2. **The lint stays green.** Check 5 is page-wide (it greps the whole file,
   so it reaches anchors in the M657 walk section, not just `## Open` rows)
   and asks only "does `jc_agent.c` have ≥4102 lines?" (it has 5,957) — true,
   so the anchor "resolves." Check 3, which would govern an open row's
   struck/closed state, applies only under `## Open`/`## Narrowed` headings
   and never reads the line. The rotted subject is precisely the class check
   5's header says it does not verify.

## 4. The other five line-anchors (checked, and the honest read)

There are **six** `path:line` anchors in the register at HEAD. Checked each:

| Anchor | Row | Status |
|---|---|---|
| `src/chat/jc_agent.c:4102` | M715 cap fact (open) | **ROTTED** — cap now at `:5276-5277` |
| `src/main.c:4943` | `--api-base` non-TTY (def-190, **closed**) | rotted subject, but on a *closed* row the lint deliberately ignores |
| `tests/test_proc.c:353` | Hurd orphan-kill 2 s window (def-38) | holds — line 353 is `JC_CHECK(kill((pid_t)victim,0)!=0)` in the 50 ms `select` loop the row describes |
| `tests/smoke/sprintf_lint.sh:34` | C89 fallback formatter (def-75) | 1-line off — line 34 is the allow-list fixture's closing `EOF`; the cited `sprintf(tmp,"%g",d)` line is 33 (subject in the ±1 neighbourhood) |
| `tests/smoke/arena_lint.sh:101` | arena allowlist (def-75) | holds — line 101 is the `nfiles ≥ 174` floor the row names |
| `tests/smoke/run.sh:200` | smoke driver failure path | holds — line 200 is `driver_failed()` |

So **one of six open-relevant anchors is rotted, and it is the load-bearing
one** — the single line-anchored *factual decision* in an open row, as
opposed to a test-fixture citation (which moves less and is self-evidently
a fixture). The `main.c:4943` rot is on a closed row (`--api-base` was
shipped at M488, def-190 correctly moved to Closed), where the lint's
header says a closed row "is allowed to say anything" — a defensible
boundary, but it means the *closed* register silently carries a dead pointer
too. That is not a bug in the lint; it is a scope note for anyone who wants
the register's closed sections to stay accurate as history.

## 5. The meta-finding: the register already knows

Two places in the register point at exactly this class, which is what makes
the gap a *missing mechanical check* rather than a *missing awareness*:

- **`def-104`** — "Status lines in `plans/` and `proposals/` that say
  'not built' about built work. Three checked by hand; a sub-agent's survey
  counted about twenty." This is the analysis #38 finding, parked here as a
  *policy* question ("date them, maintain them, or lint them") — the same
  "the line/claim no longer holds but nothing checks it" class, one register
  over.
- **The M326b section** — three items "parked on an assumption that a
  minute's reading would have settled." The rotted cap anchor is the
  mechanical twin: a fact worth a pointer, where the pointer now needs a
  minute's reading to correct.

The register's own vocabulary says the thing the lint lacks: it *checks the
checkable part of a reason before parking*, and it has a lint for the
structural invariants — but the one check that would have caught a moved
`path:line` subject (compare the cited line's content, or a symbol/`git
blame`-stable reference) is the one its header explicitly assigns to a human.

## 6. The one thing it definitely lacks

**A subject-stability check for line-anchored facts in open rows** — or,
equivalently, a convention that makes such pointers drift-proof. Two forms,
weakest to strongest:

1. **Rewrite the load-bearing pointer to a symbol, not a line.** The cap is
   named in its own comment (`jc_agent.c:3689`, `include/jc_app.h:733`:
   "forced to ≥ 200 … whenever an envelope"); a `path:symbol` or a
   `path:line`-*plus*-"the ≥200 raise under `--auto`" phrase would survive
   the file growing. This fixes the *one* open rot in one line and matches
   how `reading_refs_lint.sh` (analysis #35) already prefers symbol anchors
   to line numbers.
2. **Extend `deferred_register_lint.sh` check 5 from "resolvable" to
   "resolvable *and the cited line (±k) still contains the row's anchor
   token**" — for rows whose `**Where:**` is a `path:line` (a small subset,
   not all 180 citations, so it would not "encode today's answers and rot"
   the way the header fears). It would go red today on `jc_agent.c:4102`
   and stay green on the four holding anchors.

The register's header already argues against a *full* truth-check of reasons
(correctly — judgements are not mechanically decidable). The narrow form (a
`path:line` in an open row should still be near the code it names) is the one
that is both detectable and worth it, and it is the one currently unguarded.

## 7. What holds (verified, not assumed)

- **The lint is green and its floors are doing work**: 5/5 at HEAD, 54 open
  sections / 230 open rows (floors 20/45); the M710 synonym-widening
  (CLOSED/DONE/DECIDED/SETTLED/RESOLVED) is present and `WITHDRAWN` is
  deliberately excluded with a stated reason.
- **The M492 `--api-base` failure was properly closed**, not just re-hidden:
  def-190 moved to a `## Closed` section with the flag confirmed present
  (`main.c:1060` parses `--api-base`, `:704` the struct field, `:430` the
  help text) and documented in `SETUP_WIZARD.md`.
- **The three other test/smoke anchors hold their subjects** (verified each
  against the cited line — one points at a fixture's closing `EOF`, one line
  past the cited string), which is what keeps the rot *isolated* to the one
  source-code decision fact rather than systemic.
- **The never-compiled platform cross-register check (check 4) is current**:
  `PLATFORMS.md`'s Never-compiled set (2 rows at HEAD) is fully carried here.

## 8. Limitations (honest)

- "Rotted" is used precisely: the *claim* (cap ≥200 under `--auto`) is still
  true; only the *pointer* is stale. The gap is misdirection + a green lint
  that cannot see it, not a false fact.
- The "one of six" count is for `path:line` anchors *in the register at
  HEAD*. Most register rows carry no line pointer (a `**Where:**` of a
  directory or a doc name), so the subject-stability check would apply to a
  small, growing subset — which is why form 2 is scoped to rows whose
  `Where` is a `path:line`, not all 180 file citations.
- Whether to *prefer* symbol pointers (form 1) or to *lint* line pointers
  (form 2) is the same policy question `def-104` parks for `plans/`/
  `proposals/`; this analysis recommends form 1 for the one load-bearing rot
  and form 2 as the class-fix, and notes the register may reasonably
  disagree.

## 9. Appendix: reproducible (all from the jichi checkout)

    # the register census
    grep -c '<a id="def-' docs/DEFERRED.md                                  # 316
    grep -cE '^## Open' docs/DEFERRED.md; grep -cE '^## Closed' docs/DEFERRED.md
    awk '/^## (Open|Narrowed)/{o=1} /^## Closed/{o=0} o&&/<a id="def-/{c++} END{print c}' docs/DEFERRED.md  # open rows

    # the lint, and its declared non-check
    sh tests/smoke/deferred_register_lint.sh                                # 1..5 ok
    grep -n 'RESOLVABLE, not that the line' tests/smoke/deferred_register_lint.sh

    # the six line-anchors
    grep -oE '`(src|tests|include|scripts|docs)/[A-Za-z0-9_/.-]+\.[a-z]+:[0-9]+`' docs/DEFERRED.md | tr -d '`' | sort -u

    # the rot: cited line vs real site
    sed -n '4102p' src/chat/jc_agent.c                                      # bare } (placeholder_run)
    grep -n 'opts.max_iters = 200' src/chat/jc_agent.c                      # 5277 (real cap)
    wc -l < src/chat/jc_agent.c                                             # 5,957 (file grew past 4102)

    # the closed-row rot (lint ignores closed sections)
    awk '/<a id="def-190"/{f=1} f&&/^#### /{print} f&&/CLOSED/{print}' docs/DEFERRED.md
    sed -n '4943p' src/main.c                                               # unrelated snprintf

    # the four holding anchors
    sed -n '353p' tests/test_proc.c; sed -n '34p' tests/smoke/sprintf_lint.sh
    sed -n '101p' tests/smoke/arena_lint.sh; sed -n '200p' tests/smoke/run.sh

    # the meta-deferral: the register already knows this class
    awk '/<a id="def-104"/{f=1} f{print} f&&/<a id="def-105"/{exit}' docs/DEFERRED.md

## Verdict

`docs/DEFERRED.md` is in **excellent, self-aware shape**: a lint covers its
structural invariants, its own defects are written into it (M326b, M657,
`def-104`), and its one rotted line-pointer is isolated to a single
load-bearing decision fact that *moved, not falsified*. The one thing the
register definitely lacks is the check its own lint's header names but
declines: **that a line-anchored fact in an open row still points at the code
it cites.** Fix form 1 (symbol or "the ≥200 raise under `--auto`" phrasing at
`jc_agent.c:4102`) closes the one rot; form 2 (subject-stability on the small
`path:line` `Where`-subset) is the class-fix that makes the next move
detectable. Revisit when: `deferred_register_lint.sh` check 5 distinguishes
"resolvable" from "still holds the subject," and the cap fact reads a symbol,
not a line.

*Analysis 39. Read-only against jichi at HEAD `1b71247d`.*
