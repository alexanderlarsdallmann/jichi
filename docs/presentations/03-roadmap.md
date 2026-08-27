---
marp: true
title: jichi — roadmap
theme: default
paginate: true
---

<!-- _class: lead -->

# jichi

### Where it's been, where it's going, and how it was built

---

# How it was built

- **Milestones, not a big bang.** M1 (skeleton) → a steady capability climb →
  **M173** today, across six phases (foundation → protocols → autonomy →
  hardening → release → post-release depth).
- Each milestone is a focused plan; designs + implications recorded in
  `docs/ROADMAP.md` (with a **thematic index** up top) and, for novel work, a
  `docs/proposals/*.md`.
- Every milestone lands with **tests** and **zero warnings** — the suite is
  **over 11,000 checks** plus **over 160** POSIX-sh smoke drivers, and it grows with each feature, behind a verdict-gated `make ci`.

---

# The capability map (all shipped)

- **Core & context:** loop, two providers, compaction, calibration, prompt cache.
- **Editing:** resilient/fuzzy patches, atomic multi-edit, unified diffs.
- **Knowledge:** hybrid RAG (BM25 + embeddings + rerank), docs index, PDFs, RSS.
- **Autonomy:** envelope (budgets/verify/edit-scope/journal), snapshots, rewind.
- **Scale:** subagents (2-deep), parallel worktrees, a warm daemon + worker pool.
- **Integrations:** MCP client, ACP server, LSP nav + refactors, editors.
- **Media & sound:** image/audio generation, transcription, play/record.
- **Safety:** path fence, privileged-command gate, kinetic gate — all audited.

---

# The autonomous-operations arc (M157–M162)

An unattended run is now **bounded, observed, gated, steered, and queryable**:

| Band | What landed |
|---|---|
| **M157** loops | tmux/systemd/cron supervisor over a task queue + reference pack |
| **M158** observability | `runs` / `audit` readers; `doctor --unattended` gate; docs↔flags lint |
| **M159/M162** control | mid-run unix socket: `status` / `inject` / `pause[--extend]` / `resume` / `abort` |
| **M160** machine-readable | `--since` windows + `--output json` for `runs`/`audit` |
| **M161** provenance | `runs` flags an operator-**steered** run |

Every intervention leaves a trace; every trace has a reader.

---

# The reach into the physical world (M163)

jichi as the **deliberative mind** of a robot — sensors, actuators, sound, fleets:

- **Devices are tools** (user-tools / MCP servers speaking JSON); jichi links no
  device library (shell-out, like `pdftotext`).
- **Kinetic gate** — anything that moves mass/energy is `kinetic: true`, gated
  **below the permission verdict** (a blanket auto-approve can't satisfy it),
  allowlist-first for the E-stop, shell-bypass shadow-matched, always audited.
- **Sound I/O** — `play_audio` / `record_audio` via configured commands.
- **Honest scope:** jichi is the **seconds-scale** layer; reflexes + the real
  E-stop live below it in firmware. Proven against a simulator; hardware deferred.

---

# The mid-July → late-July bands (M135–M156)

| Theme | What landed |
|---|---|
| **Speak your language** | `language` key + localized approval prompt (en/de/es/ja/zh) |
| **Integrity** | `apply_patch` write-phase rollback; out-of-scope auto-revert |
| **Resources** | arena-lifetime fixes, `/context` gauge, mmap'd index, `--lite` release |
| **Small models** | native tool-calling nudges, arg repair, small-local preset + packs |
| **Privileged safety** | sudo/doas gate below the verdict + always-on audit (M152–M155) |
| **Input** | multiline paste into the TUI prompt (M156) |

---

# The self-improvement band (M100+)

Designed in `docs/SELF_IMPROVEMENT.md`:

- **Daemon** — warm process, bounded worker pool.
- **Assign/grade harness** — machine-checkable evals.
- **Dream** — propose-only "sleep consolidation" over telemetry when idle.
- **Learning loop** — feed the agent's own logs back as durable lessons.
- **Synthesis loop** — tie them together.

The theme: a coding agent that gets *measurably* better at *this* project.

---

# What we learned by dogfooding

From `docs/ANECDOTES.md` — each a bug that taught a durable lesson:

- Keep observability **outside** the snapshot/rollback blast radius.
- Budget exhaustion should **stop**, not **discard** — verify, then keep green.
- A "can't do that" is often a missing tool **advertisement**, not a missing
  ability (the toolProfile gate).
- **Verdict-gate the commit, not the log tail** — a green-looking commit once hid
  a red suite (ANECDOTES #17); commits are now `make ci && …`.

---

# How this was built — four delivery models

Human effort for the **same M1–M173 scope** (person-months; see
`docs/PROJECT_TIMELINE.md`):

```
AI-assisted (1 dev + AI)  |  ~1    (actual -- ~6 weeks, 20 active days)
Expert solo               |####################  ~21   (~20-26 months)
Team of ~6                |######################################  ~40  (~5-6 months)
Junior solo               |#########################################################  ~60  (~5-6 yrs)
```

The AI-assisted bar is **human** supervision time (design, review, steering) —
it excludes model compute. The lesson isn't raw speed; it's *where the human
time goes*.

---

# What the human actually did (AI-assisted)

The developer wrote almost no C. The scarce resource moved **up the stack**:

- **Direction + requirements** — choosing the next band (~30%).
- **Design review** — approving each `docs/proposals/*.md` before code (~25%).
- **Reviewing diffs + steering mid-run** — catching wrong assumptions (~25%).
- **Sequencing + priorities** (~12%) · **verifying CI** (~8%).

> The AI compressed *implementation + tests + docs*; **design and supervision
> stayed human — and set the ceiling on quality.**

---

# Since then: the bands to M390

| Band | What it is |
|---|---|
| **Python-free test tier** (M209–M217) | the whole portable e2e suite ported to POSIX-sh drivers + four C89 helpers, so `make check-target` gates a build on any POSIX box with **no python3** |
| **The notice family** (M347–M361) | the run tells the model what happened instead of letting it guess: budget notice, elision claim-ticket, undo and resume-drift notices, hollow-green warning, a clock, the flight plan |
| **Gate integrity** (M331–M343) | REFUSE-THE-GREEN, DECLARE-THE-GATE, `--verify-kind`; a green earned after an out-of-scope change is refused — false-positive rate **measured 0/21** |
| **The registry series** (M262–M390) | every user-facing vocabulary gets an owner *lint*: flags, subcommands, tool names, slash commands, config keys (incl. nested), telemetry events, session fields, notice tags, keybindings, `@`-references, example configs, asset frontmatter, and the unit suite's own wiring — **25 lint drivers** |
| **Fence honesty** (M383–M389) | a second unfenced read closed; the shell's reach past the edit scope stated to model *and* operator; mid-run fence exceptions designed with silence-has-no-clock |
| **Teaching layer** (M222–M225, M386) | two source-reading guides, then six reading-first tutorials for the craft *around* the code: tests, pseudocode, UML, use cases, domain modelling, architecture |

The through-line of the late bands: **a claim that outlives its verification is a
defect**, so the fix is usually a lint rather than a patch.

---

# Guiding principles for what's next

1. **Correctness first, then cost.** Verify aggressively; roll back only red.
2. **Small surfaces, composed.** New features reuse existing chokepoints.
3. **Everything testable offline.** Pure cores + thin I/O shells.
4. **Learn from the logs.** The agent's own telemetry is the roadmap's input.
5. **Novelty gets a proposal.** No reference to copy → design it in writing first.

---

# Toward the first release (August 2026)

The engineering loop is healthy; the release checklist is the current focus.

| Item | State |
|---|---|
| **Rename → `jichi`** ("just code" · 自治（じち）, *autonomy*) | ✔ done, end to end |
| **Curriculum** (self-learner-first) | ✔ **complete** — 12 modules, **over 75 graded tasks**, C1–C7, plus six reading-first design tutorials |
| **Open-source licence** | Apache-2.0 leaning; **waiting on an answer, not on us** (JLU rights question, asked 2026-07-27) |
| **Public snapshot** | blocked on the licence — but now **planned in writing**: what ships, what does not, and the order (`docs/plans/2026-08-public-snapshot.md`) |
| **Slides · logo** | the only items not blocked externally |

Recent hardening came from **driving jichi on real work**: a local-GPU small-model
bench and telemetry from a downstream rewrite surfaced twelve dogfood-driven fixes
by mid-July, and the stream has not stopped since — each one guarded by a test, and
increasingly by a *lint*, because a bug found by reading tends to come back.

---

<!-- _class: lead -->

# It's advisory

The roadmap records *designs*, not promises. Each capability gets its own plan
when it's picked up.

`docs/ROADMAP.md` · `docs/PROJECT_TIMELINE.md` · `docs/SELF_IMPROVEMENT.md`
