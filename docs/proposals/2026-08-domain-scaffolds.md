# Proposal — domain scaffolds & setup presets beyond code

*Status: proposal (2026-08-02). Design + decisions for new `jichi init` scaffold
packs and `jichi setup` presets covering the domains a **university/school**
learner and a **self-learner** actually meet — creative-technical (game design,
Blender/Krita scripting, data analysis) and self-management (project management,
scheduling, business planning, personal budgeting). Self-learner-first, with
step-by-step guidance the design bar. Author: the jichi maintainers with the
agent.*

---

## 1. Why this exists

jichi's scaffolding (`docs/SCAFFOLDING.md`) and setup wizard
(`docs/SETUP_WIZARD.md`) ship strong **code** packs — `default`, `c-cli`,
`zig-cli`, `python-cli`, `godot`, `docs`, `systems-analysis`, `assignments` — and
eight setup presets (developer, technical-writer, tester, reviewer, generic,
devops, support, data). That serves a programmer opening a repo.

But jichi's audience is wider than programmers, and deliberately so: it is a
teaching tool for **schools and universities** and, above all, for the
**self-learner alone**. Those people show up with tasks jichi can genuinely help
with *today* — a student scripting Blender for a 3D course, a hobbyist analyzing a
dataset, someone planning a small business or just trying to budget — but jichi
gives them an **empty `.jichi/`** and no guidance. A good scaffold pack turns
that blank slate into a guided bench: the right agents, the right commands, an
`AGENTS.md` that tells the model the domain's conventions, and a first
step-by-step task. This proposal designs those packs.

### The through-line: scaffolds are *guidance*, not just files

The existing packs already prove the pattern (`jc_scaffold.c`): a pack is a set
of compiled-in markdown assets — an `AGENTS.md` (domain rules the model
follows), scaffolded **agents** (personas, some readonly reviewers), **commands**
(slash workflows), **skills** (progressively-disclosed how-tos), and an inert
`config.example.json`. The setup wizard (`jc_setup.c`) wraps a pack in a
**preset**: a recipe pairing the pack with an output style, a mode, feature
flags, and a start script. **Nothing below needs a new subsystem** — every pack
is content in the existing tables, and every preset is a row in the existing
recipe table. That is the design constraint and the reason this is achievable.

---

## 2. The new packs

Grouped by the two audiences. Each row is a pack (`jichi init <pack>`) and,
where it makes sense, a matching setup preset.

### Group A — creative & technical (university / school)

| Pack | For | Scaffolds (agents · commands · skills) | Notes |
|---|---|---|---|
| **game-design** | a games course; design *before* code | `game-designer`, `mechanics-reviewer` (readonly) · `/gdd` (game-design doc), `/mechanic`, `/playtest-notes` · `game-loop`, `balancing`, `mda-framework` skills | Produces a **Game Design Document**, mechanics specs, playtest notes — the *design* half. Pairs with `godot` (the code half). |
| **game-dev** | building the game | `gameplay-programmer`, `code-reviewer` · `/feature`, `/bug`, `/build`, `/release-notes` · `entity-component`, `game-testing`, `input-handling` skills | Engine-agnostic; a `config.example.json` per engine (Godot/GDScript default, with notes for LÖVE/Pygame). |
| **blender-python** | 3D courses, procedural art | `blender-scripter`, `bpy-reviewer` (readonly, flags deprecated `bpy` API) · `/addon`, `/operator`, `/geometry-script` · `bpy-basics`, `mesh-ops`, `addon-manifest` skills | `AGENTS.md` pins the **`bpy` API version conventions** and the "run from Blender's Python console / `blender --background --python`" workflow — the thing a beginner gets wrong first. |
| **krita-python** | digital-art courses, automation | `krita-scripter` · `/plugin`, `/action`, `/batch-export` · `krita-python-api`, `docker-plugin`, `batch-processing` skills | `AGENTS.md` pins Krita's **PyKrita** conventions + the `.desktop`/manifest layout a plugin needs. |
| **data-analysis** | stats/science courses, any dataset | `data-analyst`, `methods-reviewer` (readonly, flags p-hacking / unlabeled axes) · `/explore`, `/clean`, `/analyze`, `/report` · `tidy-data`, `honest-charts`, `reproducible-notebook` skills | Python/pandas default; **teaches the method, not the library** — the `methods-reviewer` is the pack's soul: it asks "what's your hypothesis?" before "what's your chart?". |

### Group B — self-management (the self-learner's real needs)

The user is right that these are *underserved* and *essential* for someone
learning alone. A self-learner who cannot organize themselves never finishes.

| Pack | For | Scaffolds | Notes |
|---|---|---|---|
| **project-management** | running any project solo | `project-manager`, `scope-reviewer` (readonly, flags scope creep) · `/charter`, `/board`, `/standup` (a solo standup with yourself), `/retro` · `kanban`, `wip-limits`, `definition-of-done` skills | Shares the P6/P7 artifacts from the **process curriculum** proposal — this is that layer's *scaffold* form. Markdown kanban, no external tool. |
| **scheduling** | time, deadlines, routines | `planner` · `/schedule`, `/timeblock`, `/deadline-check` · `time-blocking`, `estimation`, `buffer-planning` skills | Teaches estimate-then-measure (P7's retro habit). A `PLAN.md` + a weekly `schedule.md`. |
| **organization** | notes, files, a second brain | `organizer` · `/inbox`, `/note`, `/weekly-review` · `zettelkasten-lite`, `file-conventions`, `weekly-review` skills | A learner's knowledge base as plain markdown; teaches capture → organize → review. |
| **business-plan** | a small venture, a side project | `business-planner`, `assumptions-reviewer` (readonly, flags an unstated assumption) · `/lean-canvas`, `/market`, `/pricing`, `/milestones` · `lean-canvas`, `unit-economics`, `validation` skills | Produces a **lean canvas** + a validation plan. Framed as *"test your assumptions cheaply"*, not *"write a 40-page plan"*. **Not** financial or legal advice — the pack says so. |
| **personal-finance** | budgeting for private use | `budget-coach` · `/budget`, `/track`, `/review-spending`, `/savings-goal` · `envelope-budgeting`, `50-30-20`, `emergency-fund` skills | A markdown/CSV budget the learner owns locally. **Explicitly educational, not advice**: the `AGENTS.md` states jichi is not a financial advisor, keeps everything on-disk and private, and teaches *methods* (envelopes, 50/30/20, an emergency fund) the learner applies to their own numbers. |

### Group C — my recommendations (also worth shipping)

Domains that recur in school/university and self-learning and fit the same
mechanism cleanly:

- **academic-writing** — essays, lab reports, a thesis: `/outline`, `/cite`,
  `/proofread` (the M13 audience-aware proofreaders already exist), a
  `citation-formats` skill, an `argument-structure` reviewer. Huge for students.
- **research-notes** — literature review + reading logs: `/summarize-paper`,
  `/lit-matrix`, `/annotate`; a `critical-reading` reviewer. Pairs with the
  reading guides (Annai/Fukabori) jichi already ships.
- **teaching** — for the learner who wants to *teach* what they learned (the
  curriculum's M10 lesson): `/lesson-plan`, `/worksheet`, `/rubric`. Reuses the
  assignments-pack authoring agents.
- **web-basics** — the most common self-learner first project: `/page`,
  `/style`, `/deploy-static`; an `accessibility-reviewer` (a11y is a great early
  habit).
- **cli-tool** (generalizing `python-cli`/`c-cli`) and **automation/scripting**
  — the "make my computer do the boring thing" pack a self-learner reaches for.

---

## 3. What every pack contains (the beginner-support contract)

To honor "write for beginners who need step-by-step guidance," each pack ships to
a fixed contract, mirroring the M243 review rules:

1. **`AGENTS.md`** — the domain's conventions in plain language, and crucially
   **which system does what** (e.g. blender-python: "this code runs *inside*
   Blender, launched with `blender --background --python x.py`, not with plain
   `python x.py`"). The single most common beginner confusion, named up front.
2. **A `START_HERE.md`** — a numbered, first-session walkthrough: what to type,
   what you'll see, and the first tiny win. No pack drops a beginner into a bare
   `.jichi/`.
3. **Scaffolded agents** — at least one *guide* persona (asks the questions a
   beginner doesn't know to ask) and one *readonly reviewer* (catches the domain's
   classic mistake — a deprecated `bpy` call, a misleading chart, scope creep, an
   unstated business assumption, a budget that doesn't sum).
4. **Commands** — 3–5 slash workflows covering the domain's core loop.
5. **Skills** — progressively-disclosed how-tos for the fiddly bits (a plugin
   manifest, an addon layout, envelope budgeting).
6. **An inert `config.example.json`** — the model/roles a domain wants (e.g.
   data-analysis wants an embed model for docs search; creative packs want vision
   if the model supports it), never a live `local/config.json` that shadows the
   user's config.

Every pack's `START_HERE.md` and reviewer are the "guidance and support" the
self-learner needs — the substitute for a teacher looking over their shoulder.

---

## 4. Setup presets (the guided front door)

Presets pair a pack with a role recipe so `jichi setup --preset <role>` produces
a validated, ready bench. New presets:

`game-designer`, `game-developer`, `3d-artist` (blender), `digital-artist`
(krita), `data-analyst` (exists as `data` — extend it to this pack),
`project-manager`, `student`, `entrepreneur`, `budgeter`.

The interactive `setup` (TTY) already asks a role and offers "accept the role's
defaults?" — these presets extend that menu. Flag-mode (`--preset`,
`--non-interactive`) is unchanged. Each preset's `setup_validate` runs the
offline doctor checklist, so a beginner ends setup with a *confirmed-working*
bench, not a hopeful one.

---

## 5. Design decisions

- **Content, not a new subsystem.** Every pack is asset tables in
  `jc_scaffold.c`; every preset is a recipe row in `jc_setup.c`. This keeps the
  release-critical core untouched and rides `test_scaffold.c`'s existing gate
  (every shipped asset must parse) + `test_setup.c`.
- **Guidance is the product.** A pack that is just template files fails the
  brief. The `START_HERE.md` + a reviewer agent are **required** in every pack —
  that is what makes it *support*, not scaffolding.
- **Name the system boundaries.** For scripting packs (Blender/Krita), the
  `AGENTS.md` must state *what runs where* — the #1 beginner trap. This is the
  M243 "what, on which system, by whom, when" rule applied to a new domain.
- **Educational, never professional advice.** The `business-plan` and
  `personal-finance` packs state plainly that jichi is a *learning tool*, not a
  financial/legal advisor; they teach methods and keep the user's data local and
  private. (A hard line — these domains carry real-world risk.)
- **Local + private by default.** Budgets, business numbers, notes — all plain
  files under the user's control, never sent anywhere jichi doesn't already send
  a prompt. Stated in each pack.
- **Reuse before invention.** `academic-writing`/`teaching`/`research-notes`
  reuse the existing M13 audience-aware writers and the assignments authoring
  agents; `project-management` shares artifacts with the process-curriculum
  proposal. Do not duplicate.
- **Two audiences, one mechanism.** School/university packs and self-management
  packs use the identical scaffold contract — proving the mechanism generalizes,
  and letting a learner mix them (a student runs `academic-writing` +
  `scheduling` + `data-analysis` together).

---

## 6. Docs to update (the "newer work not incorporated" note)

The proposal also flags scaffolding/setup docs that have drifted from the current
build and should be refreshed alongside the new packs:

- **`docs/SCAFFOLDING.md`** — list the current pack set and add the new packs;
  document the required-per-pack contract (§3) as the authoring standard;
  cross-link the graded courses (a scaffold pack is the *bench*, a graded course
  is the *exercises* — they compose).
- **`docs/SETUP_WIZARD.md`** — the preset table is stale relative to the newest
  work; add the new presets and the "accept the role's defaults?" beginner path.
- **`docs/TUTORIAL_BEGINNER.md`** — add a "pick your bench" step pointing a
  non-programmer at the right pack (a data student → `data-analysis`, a budgeter
  → `personal-finance`), so the first-boot experience serves the wider audience.
- Cross-reference the two new families of graded courses (functional + systems)
  from the scaffolding docs, since a `c-cli`/`zig-cli` bench now has a matching
  graded course.

---

## 7. Build plan (scoped, to the verified bar)

Ordered so each increment is shippable and gated:

1. **Pilot one pack end-to-end** — `data-analysis` (broad appeal, clean floor):
   write its assets in `jc_scaffold.c` (chunked literals), wire the pack table,
   add a `data-analyst` preset, and confirm `make test` (`test_scaffold.c` +
   `test_setup.c`) is green — the existing gate proves every asset parses.
2. **Group A packs** (game-design, game-dev, blender-python, krita-python) — same
   pattern, one commit each, each gated.
3. **Group B packs** (project-management, scheduling, organization, business-plan,
   personal-finance) — with the advice-disclaimer `AGENTS.md` reviewed carefully.
4. **Group C** (academic-writing, research-notes, teaching, web-basics) as
   follow-ups.
5. **Docs refresh** (§6) alongside, and an `init`/`setup` e2e smoke that lists
   the new packs (`tests/e2e/init.py` / `setup.py` already exist — extend the
   asset-parse assertion to cover them).

Each pack is verified the same way the graded courses were: `test_scaffold.c`
asserts every asset parses (the mechanical floor), and an `init --dry-run`/`setup
--list` smoke confirms it registers. No pack ships until that is green — the same
red-first discipline, applied to scaffolding.

**Why this is a proposal and not yet code:** the scaffold packs are compiled-in C
string tables (each file split into <509-char chunks) with a hard parse gate;
writing ~14 packs to that bar is a focused build wave, and this document is the
design that makes that wave mechanical rather than exploratory — the same way the
curriculum proposal preceded the 36 graded assignments. Ship the pilot
(`data-analysis`) first to validate the contract, then fan out.
