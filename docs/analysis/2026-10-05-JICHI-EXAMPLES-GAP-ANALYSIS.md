# JICHI examples — gap analysis

## TL;DR

*Read-only survey of `/home/u/development/journey/jichi` (`u` stands in for the home directory on the machine that did the survey) at HEAD
`1b71247d` (2026-10-04). Direct answer to "are the ready-to-use companions the
same as the scaffolding packs?": **no — there are three distinct mechanisms,
and the one place they should have been reconciled already disagrees with the
others.** The one thing definitely lacking: **the domain-bench inventory
exists in three places (the `examples/` directory, `examples/README.md`'s
tables, and a *hardcoded string literal* in the `init --list` printer) with no
parity check between them — and they have already drifted: the binary names
12 benches, the directory holds 13, and the missing one is `slm-lab`
(M764a, the newest bench). Secondary seam: the 13 bench smoke drivers count
assets ("2 agents, 4 commands, 3 skills") but none asserts the *read-only
domain-mistake reviewer* — the component the design proposal calls "the
pack's soul."*

## 1. What exists — and the answer to the question

**Three mechanisms, three homes, three maintenance paths:**

1. **Compiled-in `init` packs** — ~33 packs of C string-literal assets in
   `src/scaffold/jc_scaffold.c` (`jc_scaffold_pack_at`), written to
   `.jichi/` by `jichi init <pack>`. Mostly language/archetype (`c-cli`,
   `zig-cli`, `rust-cli`, `python-cli`, `godot`, `cpp`, …) plus archetype
   packs (`review`, `course`, `systems-analysis`, `assignments`, `sdlc`), and
   a few domain-ish ones (`data`, `music`, `log-analysis`). Shipped *inside
   the binary*.
2. **Copy-to-use domain benches** — 13 directories under `examples/`
   (academic-writing, blender-python, business-plan, data-analysis,
   game-design, game-dev, krita-python, personal-finance,
   project-management, research-notes, scheduling, slm-lab, web-basics).
   Each a full guided workspace: `START_HERE.md`, `agents/`, `commands/`,
   `skills/`, `AGENTS.md`, `config.example.json`. Deliberately **not**
   compiled in: the decision is recorded
   (`docs/proposals/2026-08-domain-scaffolds.md`; `examples/README.md`:
   "they ship here (copy-to-use) rather than compiled into the binary,
   deliberately: they are large and per-interest, and jichi targets small
   binaries on low-resource devices").
3. **Other examples** — 8 non-bench companions in the same directory
   (self-hosting, autonomous-loop, chatbot, web-bridge, robot-sim, sqlite,
   skills, stress) plus 7 `config.*.json` files and
   `workflow.review.json`, each a different kind of artifact (a Python
   sidecar, a C89 supervisor built by `make examples`, an MCP server).

**So: the companions are *not* the scaffolding packs.** The proposal's own
opening line draws the distinction — `jichi init` "ship strong **code**
packs … but jichi gives those people an empty `.jichi/` and no guidance"
(`2026-08-domain-scaffolds.md` §1) — and `init --list` prints the two sets
side by side with different verbs: "Available packs (**compiled in**)" then
"Domain benches (**copy from the jichi source tree's examples/**)"
(`src/main.c:5166,5175`).

## 2. The one thing definitely lacking

**A parity check between the three copies of the bench inventory — which has
already failed.**

The `init --list` printer contains a hand-written list of the benches
(`src/main.c:5175-5180`): "data-analysis, game-design, game-dev,
blender-python, krita-python, project-management, personal-finance,
scheduling, business-plan, academic-writing, research-notes, web-basics" —
**12 names**. The directory on disk holds **13**
(`find examples -maxdepth 2 -name START_HERE.md` → 13). The missing one:
**`slm-lab`** (the CPU language-model training bench; its own driver is
`tests/smoke/example_slm_lab.sh`, M764a; the directory is dated 2026-09-25,
after the literal was written; the `.TH` date on the man page is
2026-08-22 — same vintage as the literal's 12 names). `grep -c slm-lab
src/main.c` → **0**: the string does not exist anywhere in the binary.

Nothing guards this:

- `tests/smoke/init.sh` runs `init --list` and greps it for the *compiled
  pack names* (lines 14-23: "`--list names the default + archetype packs`")
  — it never touches the domain-bench literal at all.
- No other lint compares the literal to the directory
  (`grep -rln` for the bench set across `tests/smoke/` matches only the
  driver manifest `run.sh:264` and `docs_flags.sh`'s charlm note).
- The README's tables and the binary literal share no source: the README is
  markdown a human edits, the literal is a C string a human edits.

**The user-facing consequence:** a learner running `jichi init --list` — the
*designed discovery path* (the printer's own comment: "Point the user at them
so they are discoverable", main.c:5174) — sees 12 benches and cannot find
`slm-lab` by any path except browsing the source tree's README. The README
itself is honest about its own provenance ("prove it in `examples/` before
promoting"), so the bench set is expected to *grow* — and every growth is a
manual two-place edit (directory + README) that the binary silently
outlives.

**Secondary seam (verified, smaller):** the 13 bench drivers all assert the
same structural floor — `assets present (2 agents, 4 commands, 3 skills)`,
`config.example.json is valid JSON with a chat model` (12 of 13;
slm-lab's checks the "no literal key" variant), `doctor: asset frontmatter
valid`, and a teeth check (all 13) — but **none of the 13 asserts the
read-only domain-mistake reviewer, and 12 of 13 never reference
`START_HERE.md`.** The proposal is explicit that the reviewer is the pack's
soul ("`methods-reviewer` is the pack's soul: it asks 'what's your
hypothesis?' before 'what's your chart?'",
`2026-08-domain-scaffolds.md:58`), and every bench ships one with
`readonly: true` in frontmatter (verified for data-analysis, game-dev,
academic-writing, business-plan). A bench that silently loses its reviewer —
or flips it to write-enabled — would still pass its driver. The contract's
teeth check proves the *validator* has teeth; nothing proves the *bench*
keeps its soul.

## 3. Why "definitely lacking" — alternatives and their status

1. **"They ARE the same, just described differently."** No — the source
   proves three mechanisms with different homes, maintenance paths, and
   install verbs (compiled string vs `cp -r` vs `make examples`). The
   boundary is documented in two places that agree with each other
   (proposal §1; README opening).
2. **"The bench list drift is cosmetic."** It is the *discovery* path. The
   literal's own comment says why it exists ("Point the user at them so they
   are discoverable"). A discovery list missing a bench is not cosmetic in a
   project whose audience is, per the proposal, "the self-learner alone".
3. **"The drivers are the check."** They verify 13/13 benches *structurally*
   (strong: the shared 71-72-line driver with a negative teeth test is
   disciplined). What they do not verify is (a) the inventory parity with the
   binary/README, (b) the reviewer's existence/readonly-ness, (c) the
   START_HERE numbering. The gap is the *uncovered edges of the contract*,
   not the covered center.
4. **"Overlaps like `data` (compiled) vs `data-analysis` (bench) are the
   real problem."** There is a real boundary fuzz — the compiled `data` pack
   carries `data-analyst` + `data-flow` skills while the bench carries
   `data-analyst` + `methods-reviewer` + `tidy-data`/`honest-charts` — so a
   learner can end up with two overlapping "data" setups from two different
   commands. But the register has *recorded* this as the pending decision
   ("Whether any graduate into compiled-in `init` packs is a size tradeoff
   recorded in the proposal — for now the lean binary wins", README); the
   fuzz is a known state, whereas the inventory drift is an *unnoticed*
   one. Unnoticed outranks known.

## 4. Evidence (all verified against HEAD `1b71247d`)

- **The 13 benches** — `find examples -maxdepth 2 -name START_HERE.md` →
  exactly 13 (the 13 dirs in §1). `examples/README.md` tables list the same
  13 (three tables: Creative & technical ×5, Self-management ×4, Student ×4).
- **The 12-name literal** — `src/main.c:5175-5180`; `grep -c "slm-lab"
  src/main.c` → 0. The 13th bench exists: `examples/slm-lab/` (dir date
  2026-09-25), `tests/smoke/example_slm_lab.sh` (in the `run.sh:264` driver
  manifest), `docs_flags.sh:76` (charlm = "the slm-lab bench's own program
  (examples/slm-lab/charlm.c, M764a)").
- **The discovery-path intent** — `src/main.c:5174`: "Point the user at them
  so they are discoverable."
- **init.sh's blind spot** — `tests/smoke/init.sh:14-23`: the `--list`
  assertion loops *compiled pack names* only.
- **The driver floor** — `tests/smoke/example_web_basics.sh:17-31` (and the
  other 12, 71-72 lines each): count check, JSON check, doctor checks, teeth
  check. `grep -ln "readonly\|reviewer" tests/smoke/example_*.sh` → empty.
  `grep -ln "START_HERE" tests/smoke/example_*.sh` → only `slm_lab`.
- **The reviewer as soul** — `docs/proposals/2026-08-domain-scaffolds.md:58`;
  frontmatter `readonly: true` verified in 4 of 13 benches
  (data-analysis/methods-reviewer, game-dev/code-reviewer,
  academic-writing/argument-reviewer, business-plan/assumptions-reviewer).
- **The three mechanisms** — `src/scaffold/jc_scaffold.c` pack table (33
  names enumerated from the source); `examples/README.md` opening;
  `init --list`'s two sections (main.c:5166,5175).
- **The recorded lean-binary decision** — `examples/README.md`
  ("Whether any graduate … for now the lean binary wins") and the proposal
  ("Why this is a proposal and not yet code: the scaffold packs are
  compiled-in C").

## 5. Implications

- **The newest bench is undiscoverable through the designed path.** A
  learner following `init --list` will never learn of `slm-lab` — the bench
  with its own compiled C89 program, reproducibility checks, and baselines
  (the most engineering-heavy of the 13).
- **The drift is a *rate* problem, not a one-off.** The bench set grows by
  design ("prove it in examples/ before promoting"). Each addition is a
  directory + README edit today; a third edit (the C literal) is the one the
  process has already missed once. The register's own rule — "a preview must
  read every argument exactly as the executor will" (the M530 contract) —
  has a documentation-side twin that nobody has written: *a list must name
  exactly what exists*.
- **The soul is unguarded.** The read-only reviewer is the domain-specific
  safety component (it is what keeps `personal-finance` educational, what
  keeps `academic-writing` a coach not a ghostwriter — the README's ¹ and ²
  footnotes *rest on* the reviewer doing its job). A count check cannot
  catch its loss, and no driver can distinguish "2 agents" from "2 agents,
  one of which can write".
- **The boundary fuzz (`data` vs `data-analysis`) costs a confused learner
  a choice.** Two commands, two overlapping "data" setups, no documented
  rule for which to pick. Known, recorded, tolerable — until it isn't.

## 6. Recommendation

1. **A bench-inventory parity check** — one lint: the set of directories
   under `examples/` containing `START_HERE.md` must equal (a) the set named
   in the `init --list` literal (extracted by *running* `init --list` and
   parsing the "Domain benches" block — the same run-the-binary discipline
   the register uses for `--help`-derived checks, not grepping the C source),
   and (b) the set in `examples/README.md`'s three tables. Red today:
   `slm-lab` missing from (a). Green after: adding the name. Then every
   future bench is a two-place edit that the third place cannot fall out of.
2. **A soul check in the 13 drivers** — the shared driver asserts: an agent
   file with `readonly: true` in frontmatter exists, and `START_HERE.md`
   exists and its first step is numbered. Twelve of thirteen drivers would
   gain one grep each; the class of "bench silently loses its reviewer"
   becomes impossible.
3. **A one-line boundary rule in `docs/SCAFFOLDING.md`** — "compiled packs
   are languages and archetypes; domain benches are copy-to-use; if a domain
   bench graduates to compiled, the examples/ directory is deleted, not
   left as a twin" — so `data`/`data-analysis` gets an owner and a direction
   of travel.

Rejected, in the register's spirit:

- *Compile the benches in.* The decision is recorded and evidence-backed
  (binary size, low-resource targets, per-interest tail). Re-opening it
  would trade a parity lint for a size regression.
- *Generate the literal from the directory at build time.* Would make the
  binary depend on the source tree's contents at build time — exactly the
  "large and per-interest" coupling the proposal rejected; and `init --list`
  runs on *installed* trees where `examples/` may be absent. The parity
  check is the cheap, honest version.
- *Fix the literal and move on (no check).* The one-place fix is correct;
  the class (next bench added, literal stale again) is what the register's
  own method exists to prevent — "a gate never seen failing has never been
  seen working" cuts both ways: the fix is proven, the *recurrence* is not.

## 7. Limitations and honest counterarguments

1. **The examples surface is far stronger than this analysis's headline
   implies.** 13 benches × 13 files each (the contract set — `START_HERE.md`,
   `AGENTS.md`, `README.md`, `config.example.json`, plus 2 agents, 4 commands,
   3 skills), 13 drivers with negative
   teeth checks, a recorded design proposal, per-domain integrity footnotes,
   a "prove it in examples/ before promoting" path, and 8 additional
   companions of genuinely different kinds (the C89 supervisor, the MCP
   sqlite server, the self-hosting pack). The gaps are edges, not the
   center.
2. **`slm-lab`'s absence from `init --list` may be *in-flight*.** The bench
   landed 2026-09-25 (dir date) with its driver in the `run.sh` manifest;
   the literal may simply not have been updated yet, in the ordinary
   course of work. I claim the *lack of the check that would catch this*,
   not negligence; but an unnoticed miss in a discovery list is a miss.
3. **The "soul" framing is the proposal's, not mine.** I weight it because
   the README's educational-integrity footnotes (¹ financial, ² academic)
   depend on the reviewer's readonly-ness to be true; that dependency is
   inferred, not stated in one place.
4. **I have not run `init --list`** (read-only; running it is trivial and
   harmless, but the *claim* is the source-level one: `grep -c slm-lab
   src/main.c` → 0, and init.sh's 14-23 shows what is and isn't checked).
   The behavioral consequence ("the learner does not see slm-lab") follows
   from a 12-name literal in the only printer of that section.
5. **Three places may be the right number.** Directory = truth, README =
   humans, literal = the binary's honesty. My claim is only that the three
   should be *reconciled by a check*, since two have already disagreed once.

## 8. Appendix: the survey, reproducible

All from `/home/u/development/journey/jichi`, at HEAD `1b71247d`:

    # the three mechanisms
    grep -n "Available packs (compiled in)\|Domain benches (copy" src/main.c
    ls examples/                          # 13 bench dirs + 8 others + configs
    sed -n '1,20p' docs/proposals/2026-08-domain-scaffolds.md   # the decision's origin

    # the inventory drift: 13 on disk, 12 in the binary, 13 in the README
    find examples -maxdepth 2 -name START_HERE.md | wc -l       # 13
    sed -n '5175,5180p' src/main.c                              # 12 names
    grep -c "slm-lab" src/main.c                                # 0
    grep -c "slm-lab" examples/README.md                        # 1
    grep -n "example_slm_lab" tests/smoke/run.sh                # driver registered

    # what the check does NOT cover
    sed -n '14,23p' tests/smoke/init.sh                         # --list: compiled names only
    grep -ln "readonly\|reviewer" tests/smoke/example_*.sh      # (empty)
    grep -ln "START_HERE" tests/smoke/example_*.sh              # only slm_lab

    # the driver floor (12 x 71-72 lines, shared shape; slm_lab is 134)
    sed -n '17,31p;34,71p' tests/smoke/example_web_basics.sh

    # the soul, per the proposal + per the frontmatter
    grep -n "pack's soul" docs/proposals/2026-08-domain-scaffolds.md
    sed -n '1,8p' examples/data-analysis/agents/methods-reviewer.md

    # the boundary fuzz: compiled 'data' vs bench 'data-analysis'
    grep -o "Data / analysis[^\"]*" src/scaffold/jc_scaffold.c | head -1
    ls examples/data-analysis/agents

**Verdict line for the register this could seed:** the ready-to-use
companions are **not** the scaffolding packs — the register itself draws
the line in three agreeing places (the 2026-08 domain-scaffolds proposal:
`init` ships "strong **code** packs" in compiled C; `examples/README.md`:
the domain benches "ship here (copy-to-use) rather than compiled into the
binary, deliberately"; `init --list` prints the two sets under two verbs,
"Available packs (compiled in)" / "Domain benches (copy from the jichi
source tree's examples/)"). The one thing definitely lacking is the
**reconciliation of the three copies of the bench inventory** — the
directory (13 benches, each with `START_HERE.md`), the README tables (13),
and a **hand-written 12-name string literal** in the `init --list` printer
(`src/main.c:5175-5180`) whose stated purpose is discovery ("Point the user
at them so they are discoverable") — which have **already drifted**:
`slm-lab` (M764a, the newest bench, in the `run.sh:264` driver manifest,
named in the README, absent from the binary: `grep -c slm-lab src/main.c` →
0), and no check compares the copies (`init.sh:14-23` asserts only
*compiled* pack names against `--list`). Second seam: the 13 bench drivers
all verify the structural floor (2 agents / 4 commands / 3 skills, valid
`config.example.json`, doctor frontmatter, a negative teeth test — 13/13,
71-72 lines each) but none asserts the **read-only domain-mistake reviewer**
that the proposal calls "the pack's soul" (`2026-08-domain-scaffolds.md:58`)
and that the README's educational-integrity footnotes (¹ financial,
² academic) silently depend on — a bench can lose its soul and stay green.
Fixes inside the register's own method: a bench-inventory parity lint
(directory ↔ `init --list` output ↔ README tables — red today on `slm-lab`),
a two-line soul check in the shared driver (a `readonly: true` agent file;
a numbered `START_HERE.md`), and one boundary rule in `docs/SCAFFOLDING.md`
for the recorded-but-unowned `data`/`data-analysis` fuzz. Revisit when:
adding a bench cannot make `init --list` lie, and losing a reviewer cannot
leave a driver green — at which point the companions, the packs, and the
discovery path that joins them name exactly one inventory.
