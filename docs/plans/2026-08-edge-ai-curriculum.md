# Plan (TRACKED, NOT IMPLEMENTED): edge AI as a reading chapter, plus a few graded tasks

*Written 2026-08-04 on threadwork, **reframed the same day** after three pieces of
direction: (1) **jichi serves self-learners first** — no second person, no assumed
hardware; (2) the expected topology is **jichi on the edge device, driven over SSH
from another machine**, not someone sitting at the board; (3) this is probably
**"a reading task with some extra curriculum tasks"** rather than a fourteen-task
graded set. The sketch below is rewritten accordingly, and the leading candidate is
now a reading-guide chapter. **Nothing here is built.** This file exists so
the idea is recorded with its design tensions and its cost, and a decision to
implement (or not) can be made later on evidence rather than enthusiasm. No
assignment specs, graders, docs or ROADMAP claims have been added; `make
check-target` is unaffected, and `docs/CURRICULUM.md` still says 74 graded tasks
because that is still true.*

## Context — why this might be worth building

`docs/CURRICULUM.md` teaches supervised agentic development through **74
mechanically graded tasks** across four stages, with 46 trap cases and every
grader two-sided. It teaches the loop: *prompt → diff → approve → grade*.

Nothing in it teaches the **model-topology** half of using jichi well: which model
serves which role, when a local model is worth its slowness, how to route between
a cheap-and-private tier and a capable-and-remote one, and how to tell whether any
of it is actually helping. That skill set became concrete on 2026-08-04, when the
Arduino UNO Q passed Tier B (`docs/plans/2026-07-hardware-testing.md`, M282) and
the edge-AI plan (`docs/plans/2026-08-edge-ai-uno-q.md`, M278) became runnable.

The audience is real: anyone deploying jichi on constrained hardware, in a lab, in
the field, or under a token budget — and the skills transfer to *any* setup with
more than one model available, which is most of them.

## The design tension that decides everything

**The existing curriculum grades mechanically and offline.** That property is why
it can be trusted: `jichi grade` runs a `verify:` shell command, and every grader
was proven to reject a wrong answer as well as accept a right one. An edge-AI
curriculum threatens that in two ways:

1. **It wants a model server.** Rung-A/B/C exercises imply `llama-server` plus a
   GGUF file — a multi-hundred-megabyte download, an architecture-specific build,
   and a machine that can run it.
2. **Its interesting outcomes are not deterministic.** "How many tokens per
   second?" and "did the local model summarize well?" have no pass/fail line that
   would survive a different model, a different board, or a warm cache.

**Proposed resolution, and it is the load-bearing decision in this plan:**

- **Grade configuration and reasoning, not inference.** Most of the skill is in
  the *config* and the *interpretation* — both fully checkable offline. "Write a
  config where the local model is the fast tier, the network model is strong,
  escalation happens on stall, and a missing local server falls back rather than
  failing" is graded by parsing JSON with `jsonq` and asserting the shape. No
  model needed.
- **For the few tasks that need a live model, grade the *artifact*, not the
  *number*.** "Measure prefill and decode tokens/s and record them in
  `results.md` with the model, quantization and thermal state" is graded on the
  presence and shape of a well-formed measurement — not on the value, which is
  hardware-specific. This keeps the grader two-sided (an empty or malformed
  record fails) without pretending a throughput figure is a right answer.
- **Make the hardware optional.** Tasks should work on any machine that can run
  `llama-server`, including a laptop. The UNO Q is the *motivating* case, not a
  prerequisite — a curriculum that needs a specific 4 GB board has a tiny
  audience.

## The shape this should probably take (revised)

**A reading-guide chapter first, a handful of graded tasks second.** `docs/reading/`
already has the genre and the audience: ANNAI (案内) for beginners, FUKABORI
(深掘り) for the deep dive, ten chapters each, *something to do* at the end of every
chapter — and, the pattern that matters most here, **Appendix A's read-only twin
for every experiment**, so a reader without a bench is still served.

That pattern is the answer to this material's biggest problem: **almost nobody has
an UNO Q.** A chapter can teach model topology to anyone with a laptop, and offer
the board only as the vivid case. A fourteen-task graded set cannot.

**Two machines are the setting, not an aside.** The expected deployment is jichi
*on* the edge device with the human on a workstation over SSH. So the chapter
should teach that discipline explicitly — which host am I on, which binary am I
running, which config is in effect — because those are exactly the questions that
cost real time on 2026-08-04 (see `docs/INSTALL.md`'s shadowing-trap section, and
M281). A single-machine tutorial would teach the wrong reflexes for this subject.

**Provisional titles**, to be argued about later: an ANNAI chapter *"when the model
lives on the machine you are ssh'd into"*, and a FUKABORI counterpart on routing,
prefix caching and the prefill arithmetic.

## Sketch: what would be *read*, and what would be *graded*

Kept from the first draft but re-cut: most of this is prose with an experiment at
the end, and only a few items become graded specs. Every task below is a sketch,
not a spec.

**Module D1 — model topology (offline, no server needed).** ~4 tasks.
- Read a config and say which model answers a chat turn, which summarizes, and
  why. *Graded:* a written answer file matching a mechanical check on the key
  facts.
- Write a two-model config with roles split so compaction is local.
  *Graded:* `jsonq` assertions on the parsed config.
- Add a `fallback` so a missing local server does not fail the run; explain what
  changes and what does not. *Graded:* config shape + `jichi doctor` exit code.
- Diagnose a deliberately broken config (the local model holds `chat` but has a
  4k context and no `contextLimit`). *Graded:* the fix must both parse and pass a
  `doctor` lint.

**Module D2 — the cost of a fixed prefix (offline).** ~3 tasks.
- Use `jichi context` to measure your own configuration's fixed prefix, then
  reduce it (`toolProfile: core`, `repoMap: false`) and measure again.
  *Graded:* a recorded before/after with the numbers, and a config that actually
  produces the smaller one.
- Explain, in writing, why prompt-cache prefix stability matters more on a slow
  machine than a fast one, and name one config change that breaks it.
  *Graded:* mechanical check for the key terms plus a human-review note — or drop
  to a self-check if that cannot be graded honestly (see Open questions).

**Module D3 — routing under a slow fast tier (needs a server).** ~3 tasks.
- Configure routing with escalation, then *make the fast tier fail on purpose*
  and show escalation firing. *Graded:* the presence of a `route` event in a
  recorded telemetry JSONL — a real artifact, mechanically checkable.
- Measure the escalation cost: wall-clock for a turn that escalates versus one
  that goes straight to the strong model. *Graded:* well-formed record.
- Decide, from your own numbers, whether routing is a win for your hardware and
  justify it. *Graded:* the record must contain a decision and the figures it
  rests on.

**Module D4 — a role server in practice (needs a server).** ~2 tasks.
- Point `summarize` at a local model, force a compaction, and read the summary.
  *Graded:* a compaction actually occurred (telemetry `compact` event) and the
  student recorded a judgement of quality.
- Do the same for `autocomplete`/Ctrl-G, and record whether the model returns
  continuations or answers — the M279/M280 lesson, learned by observation rather
  than being told.

**Module D5 — offline and degraded operation (needs a server).** ~2 tasks.
- Complete a bounded `--auto` turn with the network disconnected.
  *Graded:* the run's own JSON output (`stop_reason`, `tool_calls`) plus the
  edited file's verifier.
- Pull the network *mid-session* and show `fallback` engaging.
  *Graded:* `[fallback]` in a captured log.

**Module D6 — the honest write-up (offline).** 1 capstone task.
- Produce a short report in the house style: predicted → observed → finding vs
  documentation fix, naming which rung of the edge-AI ladder your hardware
  actually supports. *Graded:* structural — the sections exist, the numbers are
  present, and a claim without a number fails.

## What implementing it would cost

- **Specs and graders:** ~14 markdown specs with `verify:` commands, in the shape
  of `docs/assignments/*.md` (frontmatter: `title`, `audience`, `phase`,
  `difficulty`, `points`, `verify`, `hints`). Each needs the project's two-sided
  proof — a reference solution *and* a demonstrated rejection, as
  `tests/bench/check_graders.py` does for the bench.
- **Fixtures:** a broken config to diagnose, a telemetry sample, a small
  workspace. Small.
- **Docs:** a `docs/CURRICULUM.md` section, and the **counts in
  `tests/smoke/docs_counts_lint.sh` must be updated in the same commit** — that
  lint asserts the documented graded-task count matches the assignments, so a
  half-landed set breaks the gate (which is the point).
- **A server-dependent tier marker.** Six of the fourteen tasks need
  `llama-server`. The curriculum currently assumes nothing but a jichi build, so
  these need to be clearly labelled and skippable, or the set splits into
  D-offline and D-live.
- **Rough total:** a day's work for the offline modules with graders proven both
  ways; the live modules depend on the edge-AI plan having produced real numbers
  first, since a curriculum that teaches unmeasured claims is worse than none.

## What changed in the estimate

A chapter plus ~4 graded tasks is **much cheaper** than fourteen specs with
two-sided graders, and it front-loads the part that is useful to everyone. The
offline modules (D1/D2/D6) map almost directly onto chapter sections with an
experiment each; the server-dependent ones (D3–D5) become *optional* experiments
with a read-only twin, which is how ANNAI already handles readers who cannot run
something.

## Open questions for the decision

1. **Prerequisite or not?** Should set D require the edge-AI plan (M278) to have
   *run* first? Recommendation: **yes for the live modules, no for the offline
   ones.** D1/D2/D6 can land as soon as someone wants them; D3–D5 should wait for
   measured numbers so the exercises rest on facts.
2. **How to grade a written judgement honestly.** Several tasks ask for
   interpretation, which mechanical graders handle badly. Options: grade structure
   only (sections present, numbers present), use the existing `improve`/`grade`
   record for self-assessment, or leave judgement tasks explicitly ungraded with a
   rubric. **Grading prose by keyword is the failure mode to avoid** — it teaches
   students to write for the grader.
3. ~~**Does this belong in the same curriculum at all?**~~ **Answered
   2026-08-04: mostly no.** The existing four stages teach *supervised
   development*; model topology is operations. It belongs in `docs/reading/` as a
   chapter with experiments, with only a few graded tasks attached — which is both
   cheaper and a better fit for a self-learner who may not own the hardware. What
   remains open is *which* guide (ANNAI, FUKABORI, or one chapter in each at
   different depths) and whether the graded tasks live in set C or a small set D.
4. **Is the audience real yet?** Today exactly one person has an UNO Q running
   jichi. But the *skill* — deciding which model serves which role, and whether a
   cheap tier is worth its slowness — applies to anyone with two models
   configured, which is a much larger group, and to anyone driving any machine
   over SSH. Write for them; use the board as the illustration.
5. **What does the read-only twin look like here?** ANNAI's Appendix A gives every
   experiment a version that needs no bench. For this material the twin is
   probably: a captured `telemetry` JSONL and a captured `jichi context` output,
   shipped as fixtures, so a reader with no model server can still *do* the
   interpretation exercises. That is worth prototyping before committing to it.

## Status

**Tracked, not scheduled — and to be settled by prototyping rather than argument.**
His words, and they apply to curriculum design as much as to code: try a few
things first. A plausible first prototype is *one* ANNAI section with *one*
experiment and *one* graded task, shown to work end to end for a reader without
the board, before anything larger is committed to.

**Tracked, not scheduled.** Depends on `docs/plans/2026-08-edge-ai-uno-q.md`
(M278) for the live modules. No code, no specs, no doc counts touched. Revisit
when either (a) the edge-AI plan has produced measured numbers, or (b) someone
other than the author wants to learn this.
