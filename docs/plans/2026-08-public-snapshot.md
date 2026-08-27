# Plan: the public git snapshot — what ships, what does not, and in what order

*Status: **EXECUTED (M620, 2026-08-27)** — the licence landed (M619, Apache-2.0)
and the first public snapshot was produced and committed the same day: every gate
in §2 held, every §5 step ran as built, and the snapshot builds and passes its
suite standalone. Publication (the push to a public remote, §5.5) remains the
operator's deliberate act. Originally planned so that when the licence answer
arrived the work would be execution rather than design — which is how it went. Companion surfaces:
[MIGRATION.md](../MIGRATION.md) (the M170 rename and the state-path move),
[EMBEDDING.md](../EMBEDDING.md) (the stability tiers a first public release
promises), [DECISIONS.md](../DECISIONS.md) and [DEFERRED.md](../DEFERRED.md) (which
ship in full — §3), [ROADMAP.md](../ROADMAP.md) ★ TODO (the checklist this serves).*

## 1. Why a fresh history, and what that costs

The development repository is a private GitLab project (`journey/jichi`, renamed in
place at M170) with a linear `master` and, as of 2026-08-11, **365 recorded
milestones** over ~92,000 lines of first-party C89. The release decision already
taken is that the public repository gets **its own curated git history, starting at
a fresh first commit** — not a filtered copy of the private one.

The reason is not tidiness. A development history contains, by its nature, things
nobody chose to publish: intermediate states of files that later moved, commit
messages written for an audience of one, and — the load-bearing risk — anything
transient that touched a secret, a private path, or a third party's material. A
fresh first commit makes the published surface **exactly the tree you inspected**,
with no archaeology available to contradict it. `git filter-repo` over 365
milestones would be the alternative, and it inverts the burden of proof: you would
have to show that nothing survived, forever, in every blob.

What that costs, stated plainly: **the commit-by-commit narrative does not travel.**
That is a real loss for a project whose history is unusually legible. It is
mitigated, and this is the crux of §3: the narrative is not *in* the commits — it is
in `ROADMAP.md` (365 entries), `DECISIONS.md` (211 rows), `DEFERRED.md`,
`ANECDOTES.md` (50 war stories), `docs/analysis/`, `docs/plans/` and
`docs/dialogues/`. Those ship in full, so the public repository carries the
reasoning **as documents rather than as commits**. A reader loses `git log`; they
gain a written, indexed, cross-linked account that a `git log` never was.

## 2. The gate: what must be true before the first public commit

Ordered, and each one checkable:

1. **The licence exists as a file.** No `LICENSE`/`COPYING` file exists today
   (verified). ~~The leaning is Apache-2.0; the open institutional question (sent
   2026-07-27) is the actual blocker.~~ **Resolved 2026-08-27 (M619): Apache-2.0,
   copyright Justus-Liebig-Universität Gießen, author Alexander-Lars Dallmann --
   applied and lint-pinned; a deliberate post-review switch to MIT stays one
   command.** No public commit happened before this, because the first commit's
   contents are what people acquire rights to.
   *(M497, 2026-08-20: everything except the answer is now built. Every source
   carries `Copyright (c) 2026 Alexander-Lars Dallmann` over an SPDX line reading
   `LicenseRef-UNDECIDED`; `scripts/set-license.sh <spdx-id>` writes `LICENSE` from
   a checksummed verbatim text, installs `NOTICE`, and sweeps all 476 headers; and
   `tests/smoke/license_lint.sh` flips to demanding that no file still says
   UNDECIDED the moment `LICENSE` appears — so a half-finished sweep cannot reach a
   public commit. This gate is now a decision plus one command; see
   [`../LICENSING.md`](../LICENSING.md).)*
2. **Every wire value and private path is deliberate.** The `jlu/…` model ids and
   `*.uni-giessen.de` hosts are *wire values* that must not be renamed (CLAUDE.md),
   so they will appear in example configs and docs. That is a choice, not an
   accident, and it must be re-read as a choice: an institution's gateway hostname
   in a public repository is a disclosure, however mild. **Decide explicitly**
   whether the shipped examples use the real HRZ host or a placeholder
   (`https://api.example.edu/v1`), with the real one documented only where a JLU
   reader needs it.
3. **No secret has ever been in the tree.** Keys live in `~/.jichi.env` and
   `local/` (git-ignored) — outside the repository by design. Confirm with a scan
   of the tree to be published (not the history — there is none).
4. **The untracked project assets are decided.** ~~`.jichi/agents/` (5 profiles) and
   `.jichi/commands/` (4 commands) are currently untracked~~ — **wrong when re-read
   (M484).** `.jichi/` holds exactly **two** agent profiles
   (`docs-reviewer-junior.md`, `docs-reviewer-tutor.md`), both **tracked**, and no
   commands at all. The decision this row asked for had already been taken by
   whoever committed them. What is left is a smaller and different question:
   `.gitignore:30-33` still says of `.jichi/` *"Agent configuration files — may hold
   API keys, never commit"*, and a tracked file is never ignored, so that guardrail
   is **inert for the two files it most obviously covers**. Both were read and are
   clean prose. Decide whether the rule or the exception is wrong, and write it down.

   *This row is why §5.3's lint is not optional. It was written to prevent exactly
   the class of error it then committed: a factual claim about the tree, stated
   confidently, false within days, and unchecked for months because nothing checked
   it. Compare the M326b rule in [DEFERRED.md](../DEFERRED.md).*
5. **The stability contract is accurate.** [EMBEDDING.md](../EMBEDDING.md) already
   states four tiers (stable / provisional / not-an-interface / how a break is
   announced). A first public release is the moment those promises begin to bind, so
   re-read that page as a promise rather than a description before publishing.
6. **The docs' internal links resolve in the published tree.** The orphan and
   reference lints cover `docs/` today, but the published tree may exclude files
   (§4); anything excluded must not be linked from anything included. This is
   mechanical — see §5's `snapshot_lint` proposal.

## 3. What ships, and the decision that makes this project unusual

**The documentation ships in full.** `docs/analysis/`, `docs/plans/`,
`docs/dialogues/`, `ANECDOTES.md`, `DECISIONS.md`, `DEFERRED.md`,
`GATE_INTEGRITY.md`, `TEST_INTEGRITY.md` — including every recorded failure,
mis-diagnosis, retraction and dead end. This was decided 2026-07-28 and is worth
restating as a *feature*, because the instinct at release time will be to prune it:

> A project that publishes only its successes teaches nothing about how software is
> actually built. jichi's honest record — the hollow gates that passed while running
> nothing, the "stderr truncation" that was a rollback eating its own log, the model
> blamed for a swallowed CLI flag, the lint blinded by its own comment — is the part
> a reader cannot get anywhere else. Pruning it would leave a competent agent with
> an implausible biography.

Concretely in scope for the first public commit: `src/`, `include/`, `tests/`
(all tiers, including `tests/bench/` and `tests/measure/`), `docs/` in full,
`examples/`, `editors/`, `completions/`, `man/`, `slides`/`docs/presentations/`,
`Makefile`, `README.md`, `CLAUDE.md`, `CONTRIBUTING.md`, `CHANGELOG.md`, and the
new `LICENSE`.

## 4. What does not ship, with the reason for each

**Superseded in mechanism by M484, and the reason is this table.** It said generated
artifacts do not ship. Three were tracked and would have shipped: two `wordtool`
binaries and a `notekeeper`, 3.9 MB, the largest of them unstripped and carrying the
build machine's absolute paths in its DWARF. A written list of what-does-not-ship is
a *second* source of truth about the tree, and it went stale without a sound.

So the rule replaced the list: **`scripts/make-snapshot.sh` extracts `git archive`,
and the git index is the manifest — if it must not ship, it must not be tracked.**
`local/`, the built binaries and the root-owned `.v6-console-results/` then cannot
travel *structurally* rather than by anyone remembering, and `git status` tells the
same truth to everyone instead of only to the snapshot script. The table below is
kept as the reasoning for each case, no longer as the mechanism.

| Not shipped | Why |
|---|---|
| The private commit history | §1 — replaced by the written record. |
| `local/` (git-ignored already) | Real endpoints, real key-env names, machine-specific sizing. |
| Generated artifacts: `jichi`, `jichi-convert`, `*.o`, `run_tests`, `docs/presentations/out/`, `tests/bench/results/`, `.v6-results/`, and the three assignment binaries untracked at M484 | Distribution is **source-only** by the checklist's own decision (no binaries, no bundled curl). |
| `~/.jichi*` state (never in the tree) | Sessions, telemetry, checkpoints, calibration — user data by design (M132 keeps them outside any workspace). |
| The operator's email drafts (`../emails/`) | Outside the repository already; correspondence with a third party. |
| Anything naming a person other than the author | Check before publishing: the dialogues and analysis notes are written to and about the operator, which is fine — but a third party's name or address is not the author's to publish. |

## 5. How to execute it, once unblocked

Deliberately boring, in this order:

1. **Write `LICENSE`** with the answer that arrives, plus the `NOTICE`/attribution
   shape the checklist's own sub-bullets already reason about (no vendored source;
   source-only distribution; credit is not a copyright line).
   **BUILT (M497): `scripts/set-license.sh <spdx-id>`** does all of it — verbatim
   `LICENSE` from `docs/licenses/`, checksum-verified; `NOTICE` when the licence
   propagates one; every SPDX header and the `JC_LICENSE_SPDX` define swept; the
   identifier row in `docs/LICENSING.md` updated; and the remaining prose listed for
   a human. `CREDITS.md` (Claude as implementing agent, not as holder; Continue as
   the specification) is already written. So this step is one command and one page to
   re-read, not an afternoon.
2. **Prepare the tree** — **BUILT (M484): `scripts/make-snapshot.sh`.** It refuses a
   destination inside the repository, refuses a dirty tree unless `--dirty` (which
   then snapshots the working tree rather than merely tolerating it), and refuses
   `--commit` while no `LICENSE` exists, so gate 1 cannot be forgotten in the
   excitement of the answer arriving. **Rehearsed**: 1,679 files, 19 MB; the tree is
   not a repository, which is the M451 condition several drivers must survive.
3. **`snapshot_lint`** — **BUILT (M484): `tests/smoke/snapshot_lint.sh`**, and wider
   than this step asked. It lints the *produced artifact* rather than a restatement
   of the selection rule, and its five content checks are **allowlists**, so the file
   is safe to publish: it never names the address or account it protects. On its
   first run against the tree this plan called clean it found **three compiled
   binaries, six copies of the author's email address, nine absolute paths naming two
   real accounts, four ssh logins naming machines on a desk, and a device's adb
   serial** — none of them predicted here, in a section written to predict them. All
   fixed in the same milestone; the lint is green and was proven red both before the
   fixes and afterwards against four planted leaks.
4. **`git init`, one commit**, message stating what this is and that the
   development history is deliberately not included, pointing at `ROADMAP.md` for
   the narrative.
5. **Push to the public remote**, then tag `v0.9.0` (or whatever `--version`
   reports at that moment — check, do not assume). *(M620: `--version` reports
   0.9.0 today; this step and the tag are the operator's, not the snapshot's.)*
6. **Keep the private repo as the working repository.** The public one receives
   curated snapshots; it is not where development happens. Write that in its README
   so a contributor is not confused about where to send a patch — and decide, before
   the first issue arrives, whether contributions are accepted at all and through
   what channel. An unanswered contribution is worse than a stated "not yet".

## 6. Recommendations

- **Do not wait for the licence to prepare everything else.** Steps 2 and 3 can be
  rehearsed today against a scratch clone; the result is a checklist with known
  timings instead of a scramble.
- **Decide the hostname question (§2.2) explicitly and write it down**, because it
  is the one item where a reasonable person could object after the fact, and the
  answer ("wire values must stay real" vs "examples use a placeholder") is a
  judgement about disclosure, not about code.
- **Ship the untracked dogfood assets** (§2.4). They are five agent profiles and
  four commands that demonstrate the feature set on the project's own code, and
  they cost nothing.
- **Restate the docs-ship-in-full decision in the public README**, in one sentence,
  so a reader knows the failures are there on purpose and looks for them.
- **Do not let the fresh history become a claim of no history.** The first commit
  message should say the development history exists and was deliberately not
  published — accurate, and it forecloses the reading that this code appeared
  fully-formed.

## 7. Open questions — answered where they landed

1. **Hostname/placeholder in shipped examples** (§2.2) — **answered (M485/M487
   era, held by the lint):** `examples/` uses placeholders only
   (`snapshot_lint` check 10), and every institutional host elsewhere in the
   snapshot is one a reader is meant to see (check 9) — the wire values
   CLAUDE.md protects, in the docs that explain them.
2. **Contributions: accepted, and how?** (§5.6) — **answered (M487, decided
   2026-08-19):** `CONTRIBUTING.md` "Where development happens": issues and bug
   reports yes and gratefully; patches read and applied by hand, attributed in
   the CHANGELOG and milestone, never merged as commits (a merge button would be
   a promise a snapshot repository cannot keep).
3. **Does the public repo carry `CLAUDE.md`?** — **yes, unchanged (M620):** it is
   tracked, so it ships by the M484 rule (the index is the manifest), and it is
   the best short description of the architecture that exists.
4. **Tag and version scheme** after 0.9.0 — still out of scope; the first tag
   implies a second, and `EMBEDDING.md`'s "how a break is announced" tier will
   need a concrete versioning rule to point at. The one still-open row.
