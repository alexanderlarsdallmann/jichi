# Proposal: split the rules file, because 77% of it cannot reach a model

*Status: **EXECUTED at M516** — options E then C, plus the lint, approved by the
operator. Measured result: `CLAUDE.md` 139,403 → 17,633 bytes; the assembled rules
block is no longer truncated at any window size we support (16k and above), and
`tests/smoke/rules_budget_lint.sh` fails the build if that regresses. Option A
(reorder only) was explained and deliberately skipped, since E supersedes it.
The text below is the executed design; §7's risks still apply to the follow-ups.
Measurements: [`../analysis/2026-08-21-self-hosting-first-review.md`](../analysis/2026-08-21-self-hosting-first-review.md) §5.*

## 1. The measurement that makes this necessary

`CLAUDE.md` is **139,403 bytes**. `src/chat/jc_rules.c` caps the combined rules
block at `JC_RULES_MAX` = **32 KB**, documented in
[`../RULES.md`](../RULES.md). Measured with `jichi sysmsg` and `jichi context`:

| Model window | Rules delivered | Share of the file |
| --- | --- | --- |
| 16,384 | ~4,932 tok ≈ 19.7 KB | **14.1%** |
| 32,000 | ~8,197 tok ≈ 32.8 KB | 23.5% |
| 196,608 (a real gateway window) | ~8,197 tok — **unchanged** | 23.5% |

The cap is absolute: **no window size delivers more than 32 KB.** And because
truncation is tail-first, *which rules apply is decided by line number*:

| Section | Bytes | Reaches a model? |
| --- | --- | --- |
| preamble · What this is · **Models: local only** · Build & test | 6,742 | yes |
| **Architecture** | **116,233** | cut mid-section |
| **Tests** — the test-integrity rules | 8,902 | **no** |
| **Platforms** — "read this before a portability change" | 4,182 | **no** |
| Roadmap · Repository · Anecdotes | 2,365 | **no** |

Three consequences worth stating plainly:

- The spending rule survives **because it is on line 26**, not because anyone
  decided it was the most important rule. It is, and that is luck.
- A section whose own heading is an instruction to read it before a class of
  change (`Platforms`) has never been delivered to any model.
- The "Auditing a check" rules added at M512 were unreachable the moment they
  were written, 1,756 lines in.

## 2. What the file actually contains

One section, `Architecture`, is **84%** of it: a subsystem-by-subsystem tour of
`src/`. That is *reference*, not rules. The distinction this proposal turns on:

- **A rule** is an imperative an agent must not violate, needed on every task,
  whose absence causes harm the agent cannot detect. *"Never call `sprintf`."*
  *"Local models only."* *"Declarations at block top."*
- **Reference** is a fact an agent can look up when it becomes relevant, and
  whose absence costs a `grep`. *"`jc_agent.c` holds `run_agent_loop`, which…"*

Reference in a rules file is not free: it is charged against a 32 KB budget, on
every request, and it evicts rules.

## 3. The mechanisms jichi already provides

| Mechanism | Scope it gives | Cost / catch |
| --- | --- | --- |
| **Directory walk** — every `AGENTS.md` (or `CLAUDE.md`) from the git root down to the working directory | per-area (`src/`, `tests/`, `docs/`) | **loads only if the cwd is inside that directory.** Sessions here launch from the repo root, so subdirectory rules would silently not load — the failure mode is invisible |
| **Config `instructions`** — an explicit ordered list of rules files | **per task / per profile** | a session with the wrong config gets the wrong rules; `doctor` lists what loaded |
| **Agent profiles** (`.jichi/agents/*.md`) | per perspective | **subagents receive no rules block at all** (`RULES.md`), so an agent prompt is *already* outside the cap |
| `systemPrompt` (config) | a few lines, appended | no structure, no reuse |
| **Skills** (`.jichi/skills/*/SKILL.md`) | on-demand procedure | loaded when invoked, not always-present |

The third row is the important one: **perspective is already solved** — the
self-hosting pack's `c89-reviewer` carries its own rules and never sees
`CLAUDE.md`. What is unsolved is the *top-level session's* rules.

## 4. Options

### A. Reorder only (no split)
Move `Tests` and `Platforms` above `Architecture`. Truncation is tail-first, so
those two rule sections become reachable today, in a reordering diff.
**Cost:** none. **Buys:** two of three lost rule sections. **Does not** fix the
cap, the 84% reference block, or per-task scoping. Good as an immediate
mitigation; not an answer.

### B. Split by directory (the walk)
`src/AGENTS.md`, `tests/AGENTS.md`, `docs/AGENTS.md`.
**Cost:** the catch above — a run launched from the repo root, which is how this
project works, would load *only* the root file and silently lose the rest. This
option is a trap unless sessions change where they start. **Not recommended.**

### C. Split by task, delivered via `instructions`
`rules/core.md` (always) + `rules/c.md`, `rules/tests.md`, `rules/docs.md`,
`rules/release.md`, named by each config's `instructions` list.
**Cost:** N configs, and a mis-chosen config yields the wrong rules.
**Buys:** every profile far below the cap, so nothing truncates; a reviewer
session carries reviewer rules and not the platform matrix.

### D. Push perspective into agent profiles
Keep only what a top-level session needs in the rules file; let reviewer,
test-author and doc-updater perspectives live in `.jichi/agents/*.md`, which are
already outside the cap.
**Cost:** near zero — the pack already does this. **Buys:** removes the pressure
to encode every perspective in one file.

### E. Split by lifetime: rules stay, reference moves to `docs/`
Move the `Architecture` tour into `docs/` (much of it is already duplicated in
`ARCHITECTURE_TUTORIAL.md`, `docs/reading/`, and the subsystem pages) and leave
`CLAUDE.md` as ~20 KB of imperatives with pointers.
**Cost:** a real editorial pass, and the risk of losing detail that exists only
here — so it must be a *move*, verified section by section, not a delete.
**Buys:** the whole file fits under the cap with room, so every rule reaches
every model, and the line-number lottery ends.

## 5. Recommendation

**E, then C, with D as it already is — and A today if the rest is deferred.**

1. **A now** (one diff): make `Tests` and `Platforms` reachable this afternoon.
2. **E** as the substantive work: the cut is rules-versus-reference, and it takes
   `CLAUDE.md` from 139 KB to roughly 20 KB. Verify by moving, not deleting: each
   subsection either lands in a `docs/` page or is demonstrably already there.
3. **C** as the delivery mechanism once E has made the pieces small: per-task
   `instructions` lists, so a review run does not carry the release checklist.
4. **D**: nothing to do; note in the rules file that perspective belongs in agent
   profiles, so the next contributor does not grow the monolith again.

**And gate it, whichever shape is chosen.** The reason a 139 KB rules file went
unremarked is that truncation is *silent*: nothing fails, the model simply never
sees the last 107 KB. A lint that fails the build when the assembled rules block
would exceed `JC_RULES_MAX` converts that into a red gate. Without it, any split
will silently regrow — this is the same argument as every lint added this week,
and its absence is exactly why we are here.

## 6. What must be measured, not assumed

Before and after, with the tools jichi ships:

```sh
# in the jichi checkout
jichi context                 # rules token count against the model's limit
jichi sysmsg | wc -c          # the assembled prompt
jichi sysmsg | grep -c truncated   # must be 0 when the split is done
```

The claim to be proven is exactly one sentence: **every rule in the rules files
reaches the model, at every window size we support.** Today that sentence is
false at every window size, including 196,608.

## 7. Risks, stated

- **A rule that moves to `docs/` stops being a rule.** Anything moved must be
  reference; if a sentence is load-bearing, it stays, however long the file gets.
  The test: could an agent violate it without noticing? Then it is a rule.
- **Per-task rules can be forgotten.** A config without `instructions` gets only
  `core.md`; the mitigation is that `doctor` prints what loaded, and the gate in
  §5 flags a rules block that is suspiciously small as well as too large.
- **This is the file every session reads.** A bad split degrades every future
  run, silently, which is why the shape needs sign-off and the result needs a
  number rather than a feeling.
