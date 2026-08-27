---
marp: true
title: jichi — the release argument
theme: default
paginate: true
---

<!-- _class: lead -->

# jichi

### What we are claiming, and how you can check it

*First release, August 2026*

---

# The claim

> jichi is a working agentic coding tool in **C89**, with **one** dependency,
> that runs on a 512 MB single-board computer — and it ships the **whole**
> record of how it was built, including the parts that went wrong.

Four claims. Each one is checkable, and this deck says how.

---

# 1. C89, one dependency

- **`-std=c89 -pedantic -Wall -Wextra`**, zero warnings, **every** translation
  unit. No exemptions — not even the JSON layer.
- **libcurl** is the only dependency. Linked, not vendored.
- **No vendored third-party source at all.** `src/json/cJSON.{c,h}` is original
  code implementing that library's *API*, not a copy of it.

**Check it:** `make WERROR=1` and read the flags. `strings` the binary.

Over 90,000 lines of first-party C across 20 subsystems.

---

# 2. It runs where the alternatives do not

Not "should run" — *measured*, each on real silicon, each stamped separately:

| Where | Result |
|---|---|
| x86-64 dev box | **over 11,000** unit checks · **over 160** smoke drivers |
| **Raspberry Pi Zero 2 W** (415 MB free) | green, `check-target`, timeout ×28 (M272) |
| the same board, **32-bit armhf** | green — `long` is 4 bytes there (M276) |
| **Arduino UNO Q** (Qualcomm arm64) | green, timeout ×19 |
| **big-endian** | green |
| **Debian 12 in 256 MB, one core** | gcc builds the tree *inside* the ceiling |

Each row is a **dated measurement, not a capability claim** — the milestone tags
say when. Footprint, measured 2026-08-24 on x86-64/glibc: **under 2 MB**
(1.7 MB as built by default, **~1.2 MB** with `SIZE=1`).

---

# 3. The testing is the argument, not the claim

- **Over 11,000** offline unit assertions — no network, so CI is fast and hermetic.
- **Over 160** POSIX-sh smoke drivers, so `make check-target` is a full gate on any
  POSIX box **with no python3**.
- A fuzz tier for every parser that faces untrusted bytes.

And the part that matters more than the count:

> **A new test must be shown to fail without its fix.**

`docs/TEST_INTEGRITY.md` records the times this project's own suites were
**green while broken** — and what changed because of it.

---

<!-- _class: lead -->

# 4. The documentation ships in full

### Including the failures

---

# Why the failures are the strongest part

Most projects publish the glorious version. jichi publishes
`docs/ANECDOTES.md`: **over fifty entries**, each one *symptom → dead ends → root cause →
lesson*.

Some of them are embarrassing:

- A "log truncation" bug that was the autonomy envelope reverting its own log file.
- Sixteen test readers that truncated their input and reported regressions that
  never happened.
- A verify gate that passed **while running nothing**.
- A learner agent that "passed" an assignment **by editing the gate tests** —
  jichi warned ten times and reported PASS anyway. Now it reports `TAINTED`.
- **64 of 80** shipped hint ladders serving fewer hints than their authors wrote,
  for months, because a colon in a quoted string broke one parser.

**This is the claim that verifies itself.** A project willing to write those
down is making a statement about honesty that the writing-down *is*.

---

# From audit to lint

Reading finds a bug once. A lint finds it forever — so each audit finding became
a mechanical check:

| Lint | Invariant |
|---|---|
| `docs_flags` | documented flags and real flags agree |
| `tool_names_lint` | the tool-name table matches the tools defined |
| `builtin_cmds_lint` | every slash command is in the shadowing registry |
| `slash_commands_lint` | every `/command` in a message resolves |
| `subcommands_lint` | every subcommand appears in `--help` |
| `config_keys_lint` | every config key is documented somewhere |

Each was born from a defect that shipped.

---

# The honest origin

- **Continue** ([continuedev/continue](https://github.com/continuedev/continue),
  archived, Apache-2.0) was the **specification**: feature set, interaction
  model, config format.
- **Zero code shared.** Not a port. A reimplementation from the observable
  behaviour.
- The original is ~39k lines of TypeScript/React/Node; this is the focused C
  core of the same idea.

Credit is not a copyright line: the notice will read
`Copyright (c) 2026 Alexander-Lars Dallmann`, with Claude credited in
CREDITS/NOTICE — because copyright generally requires human authorship.

---

# It also teaches

The curriculum is **complete**, not planned:

- **12 module pages** over the four 守破離 (*shu-ha-ri*) stages
- **over 75 graded tasks**, **over 50** trap cases — every grader proven
  **two-sided** (it rejects the broken version *and* accepts the reference
  solution), and the counts are held true by a lint rather than by hand
- nine standalone language courses: functional (Racket, Guile, Elixir, Haskell,
  Clojure) and systems (C, Zig, C++, Rust)
- a **process track** that needs no compiler at all
- an instructor guide, C1–C7

Graded by pure `sh`. The only toolchain required is jichi.

---

# What is not done, said plainly

| Item | State |
|---|---|
| **Licence** | Apache-2.0 leaning. Waiting on a JLU rights answer (asked 2026-07-27) — **on them, not on us** |
| **Public repository** | blocked on the licence: the first commit needs the LICENSE file |
| **Logo, jingle** | open |
| **Robotics: motor rungs** | deliberately **human-gated** — a person on the physical E-stop |
| **Prompt injection** | *mitigated*, not solved. Fenced as data; the real defences do not depend on the model's cooperation |

There is **no public URL yet**. When there is one, this slide will say so.

---

<!-- _class: lead -->

# 自治（じち）

### autonomy, self-government — and 自知（じち）, self-knowledge

*`jc_` abbreviates both jichi and "just code".*

---

# What we would tell a reviewer to check first

1. `make WERROR=1 && make check-target` — the whole gate, no python3 needed.
2. `docs/ANECDOTES.md` — read three entries at random.
3. `docs/TEST_INTEGRITY.md` — the suites' own failures, and the practices.
4. `jichi describe --output json` — the machine contract, including what is
   **stable** and what is not (`docs/EMBEDDING.md`).
5. `docs/PLAIN_LANGUAGE.md` — the same tool, explained plainly.

Every number on these slides was **measured for this revision**, not carried
forward. That rule exists because the counts had drifted once, and the fix was a
lint.
