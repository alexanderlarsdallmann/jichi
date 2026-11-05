# jichi platform support: the one thing it definitely lacks — continuous re-verification of the live gate

> **Date:** 2026-10-05
> **Method:** read-only analysis of the jichi source tree at
> `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) (HEAD `1b71247d`). No files were
> modified; no build or platform gate was run for this analysis (running one
> is a network/model event the register's own rule says is a *driven datum*,
> not a read). All claims cite a source; inferences are flagged.
> **Companions:** `JICHI-EDITOR-GAP-ANALYSIS.md`,
> `JICHI-PRESENTATIONS-GAP-ANALYSIS.md`, `JICHI-CURRICULUM-GAP-ANALYSIS.md`,
> `JICHI-UX-GAP-ANALYSIS.md`, `JICHI-DEBUGGER-GAP-ANALYSIS.md`.

---

## 1. TL;DR

Of the five analyses in this set, jichi's **platform support is the most
rigorously verified**. `docs/PLATFORMS.md` carries 30-some rows across Linux
distributions, five BSDs (FreeBSD/NetBSD/OpenBSD/GNU-Hurd/illumos), Guix,
Windows (WSL2/Cygwin/MSYS2), macOS, Haiku, FreeMiNT, FreeDOS, 20+
cross-compiled architectures, and a fleet of low-memory boards (a 160 MB
Debian, a 512 MB Raspberry Pi Zero 2 W, an Arduino UNO Q). Each row is a
**stamped datum** — build, unit suite, smoke tier, and — for the load-bearing
verdict — **Driven**: a two-turn live model task (`scripts/_rig_live.sh`:
"reply with OK" to prove the wire, then "read `note.txt` with the `read_file`
tool and report the pass phrase," where the phrase is a token *generated that
second*, so a pass is evidence, not an impression). 22 rows are Driven, each
named with the task and the model.

The one thing the platform surface definitely lacks is **continuous
re-verification of that live gate**. The offline gates (build, unit suite,
smoke tier) are re-run continuously on the development box and the
offline-per-platform retests are scripted (`tier-v-bsd.sh`, `tier-v-cygwin.sh`,
…) and debt-tracked (`PLATFORM_RETEST.md` §3's *coverage debt*, the "one
number that makes staleness measurable"). The **Driven** verdict, however, is
**a one-time point-in-time datum that is *cited*, not *re-run*, when the tree
moves**: the retest table says, for the three BSDs, "the Driven verdict
rests on M665/M668" — and the WSL2 row, now a "historical datum" (offline
debt 198, past 100), carries a Driven verdict that "rests on M475" (2026-08-18),
with **nothing** in `.github/`, the Makefile, or any schedule that re-drives
it. CI runs `make ci` on ubuntu-latest only, and is "NEVER RUN ON A HOSTED
RUNNER YET"; the model-gated e2e tests are skipped because `JC_E2E_MODEL` is
never set in CI.

So the gap is not "no platform has been driven" — the opposite, emphatically.
It is that **the strongest, most expensive, most product-relevant verdict in
the whole register — "the agent loop closed on this platform" — is the one
verdict no loop maintains**. A platform's "it works" is a photograph, not a
subscription: it is exactly as true as the day it was taken, and the tree has
moved since, and the only number that would say how far is the *offline*
coverage debt, which deliberately counts drivers, not live turns.

## 2. What platform support *does* have (for precision)

- **A verdict page that owns the claim** (`PLATFORMS.md:1`): "This page owns
  the verdict. Every other page … links here for platform support instead of
  stating its own" — built because "those five pages had drifted into three
  different confidence levels for the same untested platform."
- **Four strictly-defined words**: *Verified* (compiled + gates run, "by a
  person, with the numbers written down"), *Partly verified* (a named gap),
  *Never compiled* ("not 'probably fine' … unmeasured"), and *Driven* (a live
  agentic task with a real model, "Named with the task and the model, like
  every other datum here").
- **The Driven register** (added 2026-09-18): "Every platform row on this
  page appears here. A row that has not been driven says so; it is not
  omitted." 22 rows yes, a few partly/no, each with the agentic phrase and
  the model — e.g. `TIER-B-8838B9` (`google/gemma-4-12b`) on a Pi 400,
  `TIER-A-E2891D` on big-endian s390x under QEMU, `TIER-M-5D5D0C` on
  FreeMiNT cross-built and emulated.
- **A scripted, debt-tracked retest discipline** (`PLATFORM_RETEST.md`):
  "deliberately not a calendar"; triggers are (1) the tree changed under the
  row, (2) coverage debt crossed a band. Debt = "drivers in the tree today −
  drivers the row ran," "computable without re-running anything," and it
  "goes in one direction."
- **The live task is one file** (`scripts/_rig_live.sh`, M676), kept single
  by two lints (`rig_live_lint.sh`, `rig_live_commands_lint.sh`), with the
  rule "a second rig is where drift starts."

Every *mechanism* for verifying a platform exists and is rigorous. What does
not exist is a mechanism for re-verifying the *one mechanism that proves the
product actually runs* on that platform.

## 3. The gap, stated

> **jichi's platform "Driven" verdicts are one-time stamped datums, cited
> rather than re-run, and no continuous loop re-drives any platform. The
> offline gates are continuously maintained and debt-tracked; the live gate —
> the only one that exercises the agent loop, which is the product — is
> verified once per platform by a person or a milestone, and then its truth
> about *the current tree* rests on the coverage-debt number that
> deliberately does not count live turns. So a platform can be "Driven"
> (it ran the loop, on the day X) and "historical" (the tree has since moved
> past its debt band) at the same time, and nothing in the register says
> which of the two facts to trust for "does it work *now*."**

Two strength levels, as in the companion analyses:

* **(Strong, and the sharpest) The live gate is the only product-relevant
  verdict that no loop maintains.** Build/unit/smoke are offline and are
  re-run; Driven is live and is not. CI's own header admits the offline gate
  is "NEVER RUN ON A HOSTED RUNNER YET" and that its cost is "unmeasured" —
  the continuous verification jichi's platform story claims is, in fact, a
  single development box, and the *live* half of even that box's story
  (the model-gated e2e) is skipped.
* **(Weak, but real) "Driven" and "current" are orthogonal, and the register
  concedes it.** `PLATFORM_RETEST.md` §1: a row is "a stamped datum about a
  commit … It will be true forever, because it is a claim about a tree that
  still exists. What changes is not the row's truth but its *reach*." The
  WSL2 row is the concrete case: Driven at M475 (2026-08-18), now "a
  historical datum … a claim resting on it needs a new run first" — by the
  *offline* debt rule, while the Driven verdict "rests on" that same old run.

## 4. Why "definitely lacking"

| Candidate | Status |
|---|---|
| A platform never compiled / never driven | **Recorded and bounded** (FreeMiNT "Never compiled … no compiler *on* FreeMiNT"; the qemu-user rows "cannot be driven as they are built" — struck through once all 19 runnable ones were driven 2026-09-21). Honest, named, with the reason — not a hole. |
| Per-platform coverage debt (offline tier staleness) | **Recorded, computed, and debt-banded** (`PLATFORM_RETEST.md` §3: debt 90–198 across the current rows; the lint forces the table restated when a run crosses a band). The *offline* staleness is measurable. |
| Driven-ness of the matrix | **Recorded as a lookup, not a search** (the 2026-09-18 word, added after "the first count published with it was *wrong* … it counted what THIS PAGE records, and was written as though it counted what has been done"). The *absence* of a driven row is visible. |
| **Continuous re-verification of the Driven verdict** | **Not parked, not deferred, not claimed — there is no mechanism and no debt number for it.** Coverage debt "counts drivers, not risk," and a live turn is the thing no driver counts. The retest table *re-runs the offline tier* and *cites* the live verdict. |

The distinctive form, the sixth instance — and the most important inversion in
the set: every earlier analysis found a surface *never once* pointed at the
real thing. Platform support is the surface that **has** been pointed at the
real thing, exhaustively, and the gap is the *sustaining* of that pointing:
the one datum that says "the product actually runs here" is the one datum the
continuous loop does not maintain, while the datums that do not say the
product runs (build/unit/smoke) are the ones it does.

## 5. Evidence

### 5.1 The four words, and the fourth that owns the gap

`PLATFORMS.md:32`: *Driven* — "a **live agentic task with a real model** ran
on that platform: a request over the wire, an SSE stream, a tool the model
chose, the tool executed, and a second turn that used the result. Named with
the task and the model."

`PLATFORMS.md:39`: "Every gate the other three words describe is **OFFLINE**.
… A row that is *Verified* and not *Driven* says 'it compiles and its tests
pass here'; it does **not** say 'it works here', because the agent loop is the
product and an offline tier never runs it end to end."

`PLATFORMS.md:48` (the word's own confession): "**This page is where
driven-ness goes missing, and that is the actual gap.** … So the honest
statement is not 'it has never been done'. It is that **the matrix does not
carry it** … and neither could I, which is how the wrong number got
published."

The page already *names* the gap — and names it in the one place that could
own it. The gap survives the naming because the naming describes a
*lookup* problem (can't tell which rows were driven), and that was fixed
(the register exists). The *maintenance* problem (a driven row's live verdict
isn't re-run as the tree moves) is a different one, and no register for it
exists.

### 5.2 Offline re-runs; live verdicts cited

`PLATFORM_RETEST.md` §3's current table is explicit, for the three BSDs:
"the whole tier green, **offline** (the Driven verdict rests on
M665/M668)." The retest *mechanism* re-runs the offline tier and measures
debt; the live verdict is not in the re-run — it is cited as the substrate the
row "rests on." Grep of the table: "Offline; the Driven verdict rests on
…" appears for FreeBSD, NetBSD, and OpenBSD alike.

### 5.3 The concrete stale case: WSL2

`PLATFORMS.md:78` (the matrix row) and `:200` (the Driven register) both give
WSL2's Driven verdict as **M475 (2026-08-18)** — "chat, reasoning, tool use,
embeddings, and the model-gated e2e drivers `headless_model` +
`acp_terminal` (M475)." `PLATFORM_RETEST.md` §3 now lists the same row as
"**past 100 since M740, so a historical datum** … **a claim resting on it
needs a new run first**. 209 drivers ran at M475" — debt **198**, the
largest in the table. So: the row is offline-stale by the register's own
rule, its Driven verdict is the *same* 2026-08-18 run, and **no
mechanism re-drives it**. The "new run" the debt rule demands would be an
*offline* run; the Driven half is not even in scope for the retest.

### 5.4 The continuous loop, and what it is and isn't

`.github/workflows/ci.yml`: "runs the same gate as `make ci` … **NEVER RUN ON
A HOSTED RUNNER YET**." The job: `runs-on: ubuntu-latest`, `make ci`.
`make ci` (Makefile:1338): gcc/clang/g++/ASan/UBSan/valgrind/leakcheck +
smoke + e2e — **offline**. The e2e model-gated drivers are skipped: `make
e2e` → `tests/e2e/run.sh`, and `JC_E2E_MODEL` is set by **no** CI job, config,
or doc (grep: only a Makefile comment, "Set `JC_E2E_MODEL=<id>` to also run …").
So the *continuous* verification is (a) on one dev box, (b) offline, and
(c) the live e2e half of even it is off. Per-platform retests are *milestone*
events (M683, M685, M698, M736, M738, …) and *hand* events (macOS "by hand,"
Haiku "by hand," the first Hurd run "by hand, with no rig script yet"), not
scheduled ones. Grep of `.github/`, the Makefile, and the platform docs for
`cron|systemd|timer|nightly|schedule`: **nothing re-drives a platform.**

**Flagged inference:** "a claim resting on it needs a new run first" — the
*new run* the debt rule specifies is the offline tier. Whether the operator
would also re-drive (re-run the live task) when offline debt crosses 100 is
not stated; §5.3's WSL2 row is offline-stale *and* its live verdict is old,
and no text connects the two. That connection — *offline debt high ⇒ the
driven datum is the thing most in need of a fresh live run, because the
offline run says less and less* — is the gap's actual content, and it is
absent.

### 5.5 The project knows the failure mode, and it is a platform story

The C++ build (Makefile:1340 comment): "M188 kept it out on purpose … M636
found it **rotted because nothing ran it**; it rotted again within ten days."
That is the *offline* gate that nothing ran — and the fix was to gate it. The
Driven verdict is the *live* gate that nothing runs. The register has named
the rot-once for the offline gate; it has not yet named the same rot for the
live one, because the live one has no debt number to trip a band and no
schedule to re-fire. The WSL2 row is the first instance: a platform whose
"it works" rests on a 2026-08-18 live run while the tree has added ~200
drivers since.

## 6. Implications

1. **"Verified" is over-earning, "Driven" is under-maintaining.** The four
   words are *earned* by one-time events and *held* forever ("it will be
   true forever"). The offline gates are additionally *maintained* (debt,
   re-runs, lints). The Driven word is the only one that is earned by the
   most expensive event and then *not* maintained — so the register's
   strongest claim is also its least-kept promise.
2. **The coverage-debt number is precisely the wrong dial for the live
   gate.** Debt counts *offline drivers not yet run*. As debt grows, the
   offline tier says less about the platform, which is exactly when the
   *live* turn (which the offline tier does not cover at all — §5.1) matters
   more, not less. The one number that would measure live-gate staleness
   (drivers *or commits* since the last driven run, or "was the network/TLS
   path touched") does not exist.
3. **The "never run on a hosted runner" line is load-bearing and honest.**
   The continuous-verification story is, in fact, a *development-box* story.
   That is defensible (hermetic, cheap, the rigs are the multi-platform
   reach) — but it means the platform matrix's *maintenance* depends on one
   machine and one person's milestone attention, and the live half depends
   on their attention *plus* a model being reachable *plus* a rig being
   written for that platform. Each of those is a different failure mode from
   the offline half, and only the offline half has a number.
4. **The register's own worst number came from this seam.** The 2026-09-18
   Driven word was born because "the first count published … was **wrong** …
   it counted what THIS PAGE records, and was written as though it counted
   what has been done." A *maintenance* count (how stale is each driven
   datum) would be the same class of number, and it would have the same risk
   of being a count of the *record* mistaken for a count of the *platform* —
   which is why it does not exist: nobody has yet trusted themselves to
   publish it.
5. **Meta, the sixth of the same class, and the inversion.** Debugger:
   never observed a real crash. UX: never observed a non-builder.
   Curriculum: never delivered. Presentations: never presented. Editor:
   offline by design. **Platform: observed, exhaustively — and the
   observation is not re-observed.** Five gaps were "never happened once";
   this one is "happened, and the happening is a photograph." The shared
   root is the same: the check reaches exactly as far as a machine can run,
   and the live-model event is the boundary the machine does not run
   continuously. Platform support is where jichi *closest* that boundary
   (22 driven platforms, the rigs, the nonce-token task), which is exactly
   why the boundary is also where the one un-maintained datum sits.

## 7. Recommendation

1. **Give the Driven datum the same dial the offline tier already has.** A
   per-row *live debt*: not driver count (that is the offline dial), but
   "commits since the last driven run" or — better, and free — **"has the
   network path been touched since"** (files under the HTTP/SSE/`libcurl`
   surface, the `_rig_live` task, or a `JC_SMOKE_TIMEOUT_MULT`). A row whose
   live debt crosses a band is marked the way the WSL2 row is now marked
   offline: "the Driven verdict needs a fresh live run first." This makes the
   gap a *lookup with a number* instead of an *absence with a feeling* — the
   same move the coverage-debt page already made for the offline tier, and
   the same move the Driven word made for "which rows."
2. **Re-drive on the offline retest, for free.** The retest rigs
   (`tier-v-bsd.sh`, `tier-v-cygwin.sh`, …) already reach the platform and
   already run the offline tier. The live half is one `_rig_live.sh` call
   further (the rig has `--live-port`; WSL2/Cygwin/MSYS2 already "re-driven"
   when the operator ran it). Make the retest, when it fires, *drive* rather
   than only *re-run offline* — so the live datum moves with the offline one,
   and "the Driven verdict rests on M665" stops being the table's phrasing.
3. **Re-drive WSL2 first — it is the concrete stale case.** Debt 198,
   historical, live verdict on 2026-08-18. One `--live-port` run turns the
   register's own "a claim resting on it needs a new run first" into true for
   the live half, not just the offline.
4. **Then — and only then — consider a scheduled re-drive** (a nightly or
   monthly rig run on the boxes the operator can reach). Until the datum has
   a dial and the retest drives, a schedule would re-fire a number nobody
   reads; the register's own rule is *a number nobody maintains is how the
   previous convention rotted.*

Rejected, in the register's spirit:

* *Add live-model retests to `make ci` / hosted CI* — the CI header says the
  hosted cost is "unmeasured" and "NEVER RUN ON A HOSTED RUNNER YET"; a
  live-model gate on a runner is a network + cost + key event the offline
  design deliberately keeps out of PR CI. The rigs, on the operator's
  reachable boxes, are the live path.
* *Declare a row not-Driven once its offline debt crosses 100* — that would
  *under*-state: a row can be offline-stale and still driven-and-current, or
  offline-current and driven-and-stale. The two dials must be separate, which
  is the point of §7.1.
* *Overwrite the row with the new run* — the page's rule is "a re-run adds a
  row; it does not refresh one." Overwriting is how the matrix lost the
  provenance that makes each datum *checkable*; the fix is a *new stamped
  driven row* plus the live-debt dial, not a refresh.

## 8. Limitations and honest counterarguments

1. **This is the *strongest* surface, and the gap is a *maintenance* gap.**
   A reader who expects "jichi doesn't support platform X" will find the
   opposite: jichi supports and has *driven* ~22 platforms. The claim here is
   narrower and, I argue, more interesting: the live verdict is one-time, not
   continuously maintained. If the operator's intent is "driven datums are
   photographs, and the offline debt is the liveness dial," then §3 weakens
   to: *the register could state that the offline debt is the intended
   proxy for live-gate staleness* — it currently states it is "deliberately
   crude … counts drivers, not risk," without claiming it covers the live
   turn.
2. **Absence-based on the schedule.** "No loop re-drives" is proven by a
   negative grep over `.github/`, the Makefile, and the platform docs. A
   personal cron on the operator's box, or a milestone habit not yet
   codified, would be invisible here. The WSL2 row's age (M475 → now, offline
   debt 198) is the *positive* evidence the gap is real in at least one
   instance; the schedule absence is the general form.
3. **I did not run any platform gate.** Running `_rig_live.sh` or a rig is a
   network/model event; per the register's own rule, doing it *would* produce
   a driven datum and is not a read. Everything here is read from
   `PLATFORMS.md`, `PLATFORM_RETEST.md`, the rigs, and CI.
4. **"Driven = works" is the page's own caveat, and I honor it.** §5.1:
   "A pass means the loop closed *once*, here … It does **not** mean the
   platform runs long sessions, survives compaction, or handles
   concurrency." So even a *fresh* driven datum is a photograph of one
   closed loop. The gap is that the photograph is old and unmaintained; it is
   not that a fresh one would prove "works." I state the gap only as far as
   "the register cannot tell you which rows are currently driven-true for
   the *current tree*."
5. **Tree vintage.** The WSL2 datum (M475) predates the current M883-era
   driver count (407). The three BSDs' live verdicts (2026-09-19) are recent;
   the gap is *structural* (no re-drive mechanism) and *manifests* in the
   oldest rows (WSL2, the 32-bit Pi). A reader should weigh the structural
   claim (no maintenance loop) above the instance claim (WSL2 is stale),
   because the instance could be re-driven tomorrow and the structure would
   still hold.

## 9. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

```sh
# the four words, and the page that owns the verdict
sed -n '1,14p' docs/PLATFORMS.md                       # "This page owns the verdict"
sed -n '32,48p' docs/PLATFORMS.md                       # "Driven", and "the actual gap"

# the live task: one file, two turns, a generated nonce
wc -l scripts/_rig_live.sh
grep -n "reply with OK\|note.txt\|pass phrase\|generated this second" scripts/_rig_live.sh
ls tests/smoke/rig_live_lint.sh tests/smoke/rig_live_commands_lint.sh

# the Driven register: which rows, and how the stale ones are phrased
grep -n "### Driven" docs/PLATFORMS.md                       # find the header (em-dash; don't grep for '--')
sed -n '191,232p' docs/PLATFORMS.md | grep -cE "\*\*yes\*\*"   # 22 driven rows of 30
grep -n "rests on" docs/PLATFORM_RETEST.md                 # "the Driven verdict rests on ..." (BSDs)

# the concrete stale case: WSL2
grep -n "Windows 11 + WSL2" docs/PLATFORMS.md
grep -n "WSL2" docs/PLATFORM_RETEST.md | head -3           # debt 198, "historical datum", "needs a new run first"

# the offline dial: coverage debt
sed -n '/## 3\. Coverage debt/,/^## 4\./p' docs/PLATFORM_RETEST.md | head -25

# the continuous loop, and what it is NOT
grep -n "NEVER RUN ON A HOSTED RUNNER\|make ci\|runs-on" .github/workflows/ci.yml
sed -n '1338,1345p' Makefile                                # make ci: offline
grep -rn "JC_E2E_MODEL" .github/ Makefile                   # only a comment; nothing sets it

# is anything re-driven on a schedule? (negative)
grep -rn "cron\|systemd\|timer\|nightly\|periodically" docs/PLATFORM_RETEST.md docs/PLATFORMS.md .github/ | grep -iv "schedul"

# the rot-once the project already named, for the OFFLINE gate
grep -n "rotted because nothing ran it" Makefile
```

**Verdict line for the register this could seed:** *jichi's platform
"Driven" verdicts are one-time stamped datums, cited rather than re-run: the
offline gates (build/unit/smoke) are continuously re-run and debt-tracked
(`PLATFORM_RETEST.md` §3, "coverage debt … the one number that makes
staleness measurable"), but the Driven verdict — "the agent loop closed once,
here," the only datum that proves the *product* runs — is not in the re-run;
the retest table says "the Driven verdict rests on M665/M668" (the three
BSDs) and WSL2's rests on M475 (2026-08-18) while its offline debt is 198,
past 100, "a historical datum … a claim resting on it needs a new run first"
(an offline run). CI runs `make ci` on ubuntu-latest only, "NEVER RUN ON A
HOSTED RUNNER YET," with the model-gated e2e skipped (`JC_E2E_MODEL` unset);
no cron/systemd/schedule re-drives any platform. 22 rows are Driven — the
gap is not that nothing has been driven, but that the one datum saying "it
works here" is the one no loop maintains, and its staleness has no number.
Revisit when: a per-row *live* debt (commits, or network-path-touches, since
the last driven run) is computed and banded like the offline one, and a
retest drives rather than only re-runs offline — then WSL2 is the first row
the dial names.*
