# Plan: the public git snapshot — what ships, what does not, and in what order

*Status: **EXECUTED (M620, 2026-08-27); advanced 2026-08-27 (M621–M624) and 2026-09-17 (M625–M639, public commit `0790755`, GitHub Actions run 35195662942 green first time)** — the licence landed (M619, Apache-2.0)
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

---

## 8. Discoverability of the published repositories (M648)

*The operator added topic markers to the GitHub mirror and asked which others
would help people find jichi. This section is the answer, the reasoning, and the
one command that applies it.*

### What is there now

Read from the GitHub API on 2026-09-17, thirteen topics:

    agent · ansi-c · c · c89 · c90 · cli · cpp-compilable · linux
    opensource · posix · self-learning · software-development · zig-compilable

Also observed on the same read, and mentioned because they matter more for
discovery than any topic does: **homepage empty**, **Issues disabled**,
**Discussions disabled**, wiki enabled, licence correctly detected as
Apache-2.0.

### The gap, in one sentence

**Nothing in those thirteen topics says AI, LLM, or coding agent.** The
description does, but topic pages are how GitHub's browse and filter surfaces
work, and jichi is absent from every one of them. Someone looking for an
AI coding agent — the category jichi is in — cannot currently arrive here by
topic.

### Recommended additions (seven, reaching GitHub's cap of 20)

| Topic | Why |
|---|---|
| `llm` | The single highest-volume term jichi is missing |
| `ai-agent` | The category page for this kind of tool |
| `coding-agent` | The narrower, exact category |
| `mcp` | jichi is a Model Context Protocol **client**; a real capability, and a term people filter on |
| `local-llm` | Matches jichi's actual positioning — it is built and tested against locally hosted models |
| `tui` | jichi has a real terminal UI with a line editor, not just a prompt |
| `agent-client-protocol` | jichi is an ACP **agent server**; low volume, high precision |

**Considered and rejected:** `ai` (too broad to carry signal), `lsp` (jichi is an
LSP *client*, a secondary capability, and the slot is worth more elsewhere),
`education` (`self-learning` already covers it), `from-scratch` and
`no-dependencies` (not established topic pages). **Nothing existing is proposed
for removal** — `opensource` and `software-development` carry little signal, but
they are the operator's and the cap is not yet binding.

### Applying it

The seven additions were **not applied**: the maintenance token available here
returns `403 Resource not accessible by personal access token` for the topics
endpoint, which needs repository-administration scope. To apply them, run this
with a token that has it:

```sh
T=<token with repo administration scope>
curl -X PUT \
  -H "Authorization: Bearer $T" \
  -H "Accept: application/vnd.github+json" \
  https://api.github.com/repos/alexanderlarsdallmann/jichi/topics \
  -d '{"names":["agent","ansi-c","c","c89","c90","cli","cpp-compilable","linux",
       "opensource","posix","self-learning","software-development","zig-compilable",
       "llm","ai-agent","coding-agent","mcp","tui","local-llm","agent-client-protocol"]}'
```

The endpoint **replaces** the whole set, so the thirteen existing topics are
repeated above deliberately; sending only the new seven would delete the rest.

### Two things worth more than topics, and both are decisions rather than work

1. **The homepage field is empty.** It is the second-most-clicked element on a
   repository page after the description. There is no published documentation
   site, so the honest options are to leave it empty or point it at the GitLab
   mirror.
2. **Issues and Discussions are both disabled.** That is a legitimate position
   for a project with one maintainer — an unanswered issue tracker is worse than
   none — but it means a reader who finds a defect has **no route to report
   it**, and `CONTRIBUTING.md` invites contribution. Either enabling Issues or
   naming a contact route in the README would close that, and **which one is the
   operator's call**, not a documentation fix.

### 8a. GitHub Pages — the option, and the reason to be careful with it (M654)

*Added because §8 said "there is no published documentation site" and stopped
there, which names a gap without weighing it.*

**What it would serve.** `docs/` is **466 English markdown pages**, already
cross-linked and already indexed by `docs/README.md`. That is a documentation
site that happens not to be published. `make slides` additionally renders eight
Marp decks to HTML (into a **gitignored** `docs/presentations/out/`), and those
are the single most Pages-shaped artifact in the tree: they are meant to be
*looked at*, and markdown on a repository page is a poor way to look at slides.

**The argument against, and it is this session's own lesson.** A Pages site is a
**second copy of a claim**, and M645, M646, M648 and M650 were all the same
defect: a figure, a stamp, a licence status or a citation that was correct in one
place and stale in another. A generated site inherits that risk *structurally* —
it can be built once and then serve August's numbers for a year, exactly as the
front page served M486's for 158 milestones. **A published site that is not
rebuilt from the tree on every change is a new home for a stale claim**, and this
project has now spent four milestones proving it cannot rely on noticing.

**The options, cheapest first:**

1. **Leave it.** GitHub renders markdown, so `docs/README.md` is already browsable
   and every relative link between pages works. Cost: nothing. Loss: the decks
   stay unviewable, and the homepage field stays empty.
2. **Set the homepage field to the repository's own `docs/` index.** No new
   infrastructure, no second copy, no drift by construction — the link resolves
   to the file that *is* the source. **This is the recommendation**, and it is
   one field rather than a project.
3. **Serve `docs/` directly with Pages** (GitHub's "deploy from a branch",
   folder `/docs`). No generator to maintain, and the content is the tree. Two
   real costs: Jekyll processes the folder by default, so a `.nojekyll` file or a
   theme decision is needed, and **a served page is a page people cite**, which
   raises the price of every stale figure rather than lowering it.
4. **A built site** (mdBook, or the Marp decks as HTML). The only option that
   makes the decks viewable. **It must be built by CI on every push, never by
   hand** — a hand-built site is option 3's drift risk with an extra step. Note
   `make slides` needs `npx`/`marp-cli`, which is a network dependency this
   project deliberately keeps out of `make ci` (it is a no-op with a note when
   absent), so wiring it into a publish workflow is a decision about that rule,
   not just a workflow file.

**Recommendation: option 2 now, option 4 only with the CI build.** And if option
4 is ever taken, the site must carry the same discipline the pages do — a
generated-on date visible to the reader, so that a stale site *says* it is stale
instead of looking current. That is the M391 stamp rule applied to a website.

### The homepage URL, resolved (M655)

The twenty topics of §8 were applied by the operator on 2026-09-17. The homepage
field is the remaining half, and the value to put in it is:

```
https://github.com/alexanderlarsdallmann/jichi/blob/master/docs/README.md
```

**Why that form and not the other two**, each probed on 2026-09-17 and each
answering HTTP 200:

| Candidate | What a visitor gets |
|---|---|
| `…/blob/master/docs/README.md` | **The routing table, rendered, and nothing above it.** `docs/README.md` opens "The documentation map — this is the routing table", which is exactly what the field should reach |
| `…/tree/master/docs` | The same README **below a list of 157 filenames**. GitHub renders a directory's README *after* its file listing, so the curated index is buried under the thing it exists to replace |
| `…/jichi#readme` | The project README, which the visitor is already looking at |

The default branch on the mirror is **`master`** (confirmed against the API, not
assumed), and `docs/README.md` is present there at 18,846 bytes.

**How to set it** — no token needed, and this is the escape route if the API
route refuses as it did for topics:

1. Open `https://github.com/alexanderlarsdallmann/jichi`.
2. Click the **gear icon** beside **About**, top right of the file listing.
3. Paste the URL above into **Website**. Leave *"Use your GitHub Pages website"*
   **unticked** — there is no Pages site, and §8a argues against creating one.
4. **Save changes.** The link appears in the About sidebar immediately.

The API route, for a token that carries repository-administration scope:

```sh
T=<token with repo administration scope>
curl -X PATCH \
  -H "Authorization: Bearer $T" \
  -H "Accept: application/vnd.github+json" \
  https://api.github.com/repos/alexanderlarsdallmann/jichi \
  -d '{"homepage":"https://github.com/alexanderlarsdallmann/jichi/blob/master/docs/README.md"}'
```

**The one cost, named.** A homepage pointing into the same repository is
unusual — the field normally carries an external site. It is the right choice
here precisely because there is no external site and §8a argues there should not
be one yet: this URL cannot go stale, because it *is* the source. If a built
documentation site is ever published, this field is the thing to repoint, and
that is a one-line change rather than a migration.

**Enabling Pages remains a repository setting rather than a file**, so it is not
in reach from the tree either way.
