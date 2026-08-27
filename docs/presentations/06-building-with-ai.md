---
marp: true
title: jichi — building it with AI
theme: default
paginate: true
---

<!-- _class: lead -->

# Building jichi

### One developer, assisted by an AI agent

A retrospective on the *method* — the numbers, the delivery models, and what the
human actually did. Full data: `docs/PROJECT_TIMELINE.md`.

---

# The first 37 days, in numbers

*A retrospective of one window, not a running total — the table is stamped, and
the project has kept going (as of 2026-08-11: M390, over 90,000 lines of C89,
over 11,000 unit checks, over 160 smoke drivers).*

| Metric | Value |
|---|---|
| Calendar span | 2026-06-18 → 2026-07-24 (**37 days**, **19 active**) |
| Commits | **378** |
| Milestones | **M1 – M173** |
| Source (`src`+`include`) | **~70,400 lines** C89 |
| Tests | **~43,300 lines**, **10,000+ assertions** |
| Documentation | **~24,600 lines**, 122 files |
| Quality gates | `-Werror` (gcc+clang), ASan/UBSan, valgrind, fuzz, e2e |

A **1 : 0.36 : 0.35** code : test : docs ratio — tests and docs were part of
every milestone's definition of done.

---

# Six phases

```
P0  Foundation            Jun 18-24   M1-M20    core substrate, providers, loop
P1  Retrieval & protocols Jun 24-26   M21-M50   cache, compaction, RAG, MCP
P2  Integrations/autonomy Jun 26-Jul1 M50-M80   LSP, ACP, envelope, subagents
P3  Hardening/self-improve Jul 1-10   M80-M106  dogfood -> grounded guardrails
P4  Suite & release        Jul 13-15  M107-M134 suite, fuzzing, security band
P5  Post-release depth     Jul 23-27  M135-M173 loops, control, robotics,
                                     rename, curriculum
```

A 9-day gap split release-hardening (P4) from a distinct **post-release
capability wave** (P5): autonomy ops, observability, the control channel, and
the reach into embodied/robotic use.

---

# Development intensity (commits/active day)

```
Jun18 ## 6      Jun30 ############ 24
Jun19 ########## 20   Jul01 #### 8
Jun22 ########## 20   Jul02 ###### 12
Jun23 ################## 36   Jul06 ###### 12
Jun24 ############### 31   Jul07 ##### 9
Jun25 ################## 36   Jul08 ################# 33
Jun26 ######## 16   Jul09 ################# 33
                    Jul10 ##### 10
                    Jul13 ################## 36
                    Jul14 #### 8
                    Jul23 ########## 20
                    Jul24 #### 8      -> 378 total
```

High commit rate stayed **safe** because the quality gate was automated and hard.

---

# The milestone loop (the reusable artifact)

```
requirement  ->  design (seam + pure core + thin shell; often a proposal)
             ->  implement in C89
             ->  tests (pure-core unit + e2e/PTY smoke)
             ->  gate: -Werror + ASan/UBSan + valgrind + e2e  --(red)--> implement
             ->  docs + ROADMAP note
             ->  scoped, verdict-gated commit  ->  (next)
```

Nearly **400** milestones, each a small designed unit (the figure on slide 2 is
the dated one). The history reads as a narrative *because* of this discipline —
and it is what kept the AI correct across **800+** commits.

---

# Four ways to deliver the same scope

Human effort for the same **M1–M173** scope (person-months, midpoint):

```
AI-assisted (1 dev + AI)  | ~1    <- actual (~6 weeks, 20 active days)
Expert solo               |#################### ~21   (~20-26 months)
Team of ~6                |###################################### ~40  (~5-6 months)
Junior solo               |######################################################## ~60 (~5-6 yrs)
```

- **AI-assisted** = *human* supervision time (design + review + steering);
  excludes model compute.
- **Team** costs more *total* effort than expert-solo (coordination tax) but far
  less *calendar* — the effort-vs-schedule trade.
- **Junior** carries a completeness risk, not just slowness: several subsystems
  are hard to reach this quality without mentorship.

---

# Where the human's time went

```
Direction + requirements (what to build)      ##############  30%
Design review + approving proposals           ############    25%
Reviewing diffs + steering mid-run             ############    25%
Deciding priorities / sequencing bands         ######          12%
Verifying results / reading CI                  ####            8%
```

The developer wrote **almost no C**. The value moved *up the stack*: choosing the
next band, approving a design, catching a wrong assumption, pausing a run.

---

# Why the hygiene mattered *more*, not less

The same disciplines that let a **team** scale are what made **AI assistance**
trustworthy:

- **Tight milestones** → reviewable units, a clean history.
- **A design note before code** → the human reviews *intent*, cheaply, first.
- **Pure-core testability** → 10,000+ offline assertions; fast, hermetic CI.
- **A hard quality gate** → regressions caught at once; a high commit rate is safe.
- **A doc + commit per unit** → the work is legible and resumable.

> AI didn't replace engineering discipline — it **raised the return** on it.

---

# Honest limits

- The **person-month** figures for the three human models are estimates (±40%),
  bottom-up from delivered scope — not measured runs.
- The **AI-assisted** figure is the one measured directly (calendar + active
  days), but counts only *human* time; **model compute is a real cost** paid in
  tokens.
- A **reference implementation** (the Continue CLI) cut requirements risk in
  P0–P4; the novel P5 bands had none — which is why each opened with a proposal.
- One project is one data point. The transferable claim is the **method**, not a
  universal multiplier.

---

<!-- _class: lead -->

# The takeaway

**AI assistance compresses implementation, testing, and documentation.
Design and supervision stay human — and set the ceiling on quality.**

Invest the saved time where it now matters most: deciding *what* to build, and
judging *whether the result is right*.

`docs/PROJECT_TIMELINE.md` · `docs/ROADMAP.md` · `docs/ANECDOTES.md`
