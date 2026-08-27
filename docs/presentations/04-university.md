---
marp: true
title: jichi in the university
theme: default
paginate: true
---

<!-- _class: lead -->

# jichi at university

### Research, coursework, reproducibility

---

# Why it fits academic settings

- **Runs on the hardware you have** — a shared login node, an old lab box, a
  Raspberry Pi, a phone in Termux. ~1.2 MB, ~10–17 MB RSS, talks to a remote or
  local model.
- **No vendor lock-in** — point it at a department LLM server, a local
  llama.cpp/vLLM, or a commercial API. Keyless local servers just work.
- **Auditable** — it's C89 with a test suite; students can *read* the agent, not
  just use it.
- **Cost-aware** — prompt caching + live `/cost`; budgets cap an autonomous run.

---

# For research software

- **Understand a legacy codebase fast** — `@folder:` maps + `codebase_search` +
  `search_docs` over the paper's reference material.
- **Reproducible runs** — headless `-p` + `--output json`, driven from a Makefile
  or a Slurm job; the JSONL journal is your provenance record.
- **Autonomy with a leash** — `--auto --verify "make test" --budget-*` so an
  overnight refactor can't run away or silently break the build.
- **Case study: the zigodot rewrite** — a large, autonomous language port driven
  entirely by jichi, with telemetry showing exactly where effort went.
- **Embodied / robotics research** — jichi as a robot's *deliberative* layer
  (sensors + actuators as tools, a kinetic-safety gate, sound I/O); a hardware-free
  simulator ships in `examples/robot-sim/` (`docs/ROBOTICS.md`).
- **Software-engineering & PM as a subject** — `docs/PROJECT_TIMELINE.md` is a
  data-grounded retrospective (phases, intensity, four delivery models incl. one
  developer + AI) usable in a project-management or SE course.

---

# For teaching a course

- One-command setup: **`jichi setup --preset learner`** (study) or
  **`--preset instructor`** (author + grade). A learner loads a brief with
  `/assignment`, pulls graded **`/hint`**s, and self-checks with **`/grade`** —
  the model turns into a **tutor that guides, never solves**.
- Instructors author briefs + rubrics + a hint ladder; the reference solution is
  withheld; grading is **read-only** and rubric-keyed. Consistent across TAs.
- **The agent is legible** — an assignment on "how does an agent loop work?" can
  point at *this* codebase's `jc_agent_run_turn`.
- A **self-learner-first curriculum** is designed (intro course that opens with
  *compiling from source*, a block course, a 14-week semester) — see
  `docs/proposals/2026-07-curriculum.md`.

---

# Reproducibility & provenance

```sh
jichi --auto \
  --verify "pytest -q" \
  --budget-tokens 300k \
  --journal artifacts/run-$(date +%s).jsonl \
  --output json \
  -p "implement the FFT variant from the spec and pass the tests" \
  > artifacts/result.json
```

Every model call, tool call, cost, and outcome is captured — attach it to the
lab notebook or the paper's artifact.

---

# What a captured campaign looks like

`docs/case-studies/` is one, kept whole: **four tasks, eleven runs, two models**,
each bundle holding the assignment *as authored* (defects included), the gate
*as proven red*, the reference solution, the agent's solution, and the run
journals.

It is study material because it is unflattering. Three findings a benchmark
table cannot show:

- **"Coding model" is not one skill.** The 31B instruction-tuned model wrote the
  best teaching prose and could not perform a simple code insertion; the coder
  model implemented cleanly and, as an *author*, shipped a gate that could not
  fail.
- **The fence is part of the model choice.** Same model, same task: unfenced it
  gamed the gate at 5,215k tokens; fenced to one file it did the work at 504k.
- **Author and solver sharing a model share its blind spots** — the solver
  rebuilt the API its own author had hallucinated.

**n=1 codebase.** Case studies, not benchmarks — the limits are stated on the
page, which is the point of publishing them.

---

# Low-resource & remote

- **`--lite`** turns off the heavy subsystems in one flag (snapshots, repo map,
  parallel) for a tiny footprint on constrained nodes.
- **SSH + tmux** — start a long `--auto` run on the GPU box, detach, reattach
  later (`docs/REMOTE_SSH.md`, `docs/TMUX.md`).
- **The daemon** — a warm process on a lab server serves many quick requests
  without re-loading config/index each time.

---

# Privacy & data governance

- Runs against a **self-hosted** model, so student code and research data need
  never leave the institution.
- The **path fence** and **edit-scope** bound what an autonomous run can touch;
  **reference roots** allow read-only access to shared corpora without write risk.
- Secrets come from `apiKeyEnv` env vars — never written to config, redacted from
  logs.

---

# A concrete syllabus slot

> *"Week 9: agentic tools."* Students read `jc_agent_run_turn`, add a new built-in
> tool behind the registry, and watch the model call it. The whole loop is ~one
> function; the tool interface is a struct. It demystifies "AI agents."

---

# Built here, meant to be shared

**jichi was developed at the Justus-Liebig-Universität Gießen** and is intended
for a first **open-source release** — usable by anyone who needs AI-assisted
coding, on a constrained machine or a powerful one.

- **No third-party source is vendored** — the tree is original code; the one
  dependency, libcurl, is linked, not shipped. So the licence choice is
  unconstrained by any inbound licence.
- **Distributed as source** — users compile it themselves (comprehensive build
  docs ship with it); no binaries to redistribute.
- **Credit** — the licence names the developer, **Alexander-Lars Dallmann**, and
  the acknowledgements credit **Claude (Anthropic)** and the projects it learns
  from (Continue, opencode).
- **Leaning Apache-2.0** (explicit patent grant + a NOTICE file for attribution)
  — not yet finalised.

> **Open question for the university:** does the JLU have any rights, policy, or
> preferred process for releasing software built here? That answer is welcome —
> and may pick the licence for us.

---

<!-- _class: lead -->

# Start here

```sh
jichi setup --preset developer      # build/edit code
jichi setup --preset learner        # study, with tutor + hints
jichi setup --preset instructor     # author + grade assignments
```

First time compiling from source? `docs/PREPARE_AND_BUILD.md` (Linux / macOS /
Windows-WSL). Then `docs/TUTORIAL_BEGINNER.md`, `docs/TEACHING_ASSIGNMENTS.md`.
