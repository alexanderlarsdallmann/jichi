# Proposal: a self-hosting development pack — jichi developing jichi

*Status: partially built (2026-08-02/03). Steps 1–3 shipped as M239/M241
(`examples/self-hosting/`: both configs, 5 of 7 agents, 4 commands). Still
open: the `AGENTS.md` digest, the five skills, the `smoke-driver-author`/
`lint-author` agents, the `/red-first`/`/milestone` commands, build-order
steps 4–5 (chore queue, per-role bench), and promotion to a compiled pack —
gated on the two dogfooding runs in `examples/self-hosting/README.md`
§"Promotion path".*

## The ask

jichi is currently developed with **Claude Code + Opus/Fable** (frontier
models). The request: ship a **scaffold pack + setup preset** that configures a
*compiled jichi instance* to develop **jichi's own source**, driven by whatever
models are available (the JLU HRZ models, a local GPU, LM Studio/LocalAI, …) —
a specialized set of **agents, skills, and tool sets** for the job. This is the
ultimate dogfood: the tool that helps build software, helping build itself.

## The honest framing (read this first)

A design that oversells this would be worse than none, so state the ceiling up
front, grounded in what we already measured.

1. **Model tier is the binding constraint, not the harness.** The local-GPU
   bench (`docs/BENCH_LOCAL_GPU.md`, `tests/bench/`) already measured what small
   models do on this codebase: they call tools correctly and make *mechanical*
   edits, but they do **not** design milestones, hold 79k lines of context, or
   reason across subsystems. HRZ's `gemma-4-31b` is more capable but is still
   not a frontier model. **Design and novel work stay with the strong model
   (today, Claude Code).** This pack is for the *mechanical, review, test, and
   documentation tail* — which is large and real, but is not "jichi writes its
   own milestones."
2. **The value is threefold, and none of it requires (1) to be false:**
   - **Dogfooding surfaces jichi's own defects.** The bench already found real
     bugs this way (M166/M168/M172, the empty-assistant-turn bug). A self-hosted
     dev loop is a continuous version of that.
   - **Offloading the well-scoped tail** — apply a lint's findings, write a
     shown-red-first test for a described change, update ROADMAP/CHANGELOG,
     draft a commit message, review a diff against the house rules — to a
     cheaper self-hosted loop, keeping the frontier model for design.
   - **A teaching artifact** — the project visibly develops itself, which is the
     curriculum's whole thesis made literal.
3. **Safety is not optional here.** An agent editing the agent's own source,
   unsupervised, against local models that make mistakes, is exactly the case
   the autonomy envelope exists for. The pack must lean on it hard (below), and
   must **never** let the self-hosted loop edit its *own* safety code
   (the envelope, the path fence, the privileged/kinetic gates) unsupervised.

## What ships

A new scaffold pack **`jichi-dev`** (compiled-in tables in
`src/scaffold/jc_scaffold.c`, like the existing `c-cli`/`sdlc`/… packs) and a
matching **setup preset** `jichi-dev` (`src/setup/jc_setup.c`), plus a
`config.example.json` the maintainer merges into the global config. It is
`init`/`setup`-only assets — no new C runtime beyond the pack/preset tables and
their tests. Concretely, under `.jichi/`:

### AGENTS.md — the house rules, distilled and *pointed*, not duplicated

The canonical rules already live in `CLAUDE.md` and `CONTRIBUTING.md`. The
pack's `AGENTS.md` is a short, high-signal digest that *points* at them and
foregrounds the five that agents violate most: **C89/pedantic zero-warnings**,
the **three-arena lifetime** model, **errors-as-values (`jc_status`)**,
**prefer a lint to an audit**, and **a new test must be shown to fail without
its fix**. It also states the working loop (design → implement → test → doc →
commit) and the commit/milestone conventions. (Duplication is a maintenance
trap; this file is a map, not a copy.)

### Agents — tool-fenced subagent profiles (`.jichi/agents/*.md`)

Each is a `spawn_subagent` profile with a `tools:` allow-list (the M-era tool
fence) and a persona. Proposed set, smallest-blast-radius first:

| Agent | Role | `readonly` | Tools |
|---|---|---|---|
| `c89-reviewer` | review a diff for C89/pedantic/house-rule violations; cite `file:line` | yes | `read_file`, `search_code`, `git_diff`, `grep` |
| `arena-auditor` | flag arena-lifetime misuse (the M197–M199 bug class) in a diff | yes | `read_file`, `search_code`, `git_diff` |
| `test-author` | write a unit/smoke test for a described change, **shown red first** | no | `read_file`, `write_file`, `run_tests`, `run_terminal_command` |
| `smoke-driver-author` | write a POSIX-sh smoke driver in the `tests/smoke/` pattern | no | `read_file`, `write_file`, `run_terminal_command` |
| `lint-author` | turn a would-be audit into a re-running lint (the house principle) | no | `read_file`, `write_file`, `run_terminal_command` |
| `doc-updater` | update ROADMAP/CHANGELOG/CLAUDE.md for a milestone, in-style | no | `read_file`, `edit_file` |
| `committer` | craft a commit message in the house format (incl. `Co-Authored-By`) | no | `git_diff`, `git_status` |

The review agents (`readonly`) are the safe entry point: they *cannot* mutate
the tree, so they can run against the least-trusted models with no downside.

### Skills — the workflow encoded (`.jichi/skills/*/SKILL.md`)

Progressive-disclosure instruction sets the agent loads on demand:

- `build-and-test` — the make targets and what each gates (`make`, `make test`,
  `make smoke`, `make check-target`, `make ci`; `WERROR=1`, `SAN=1`), and the
  rule that **first-party code must compile warning-free under
  `-std=c89 -pedantic -Wall -Wextra`**.
- `shown-red-first` — the test-integrity ritual (`docs/TEST_INTEGRITY.md`):
  revert the guard, run, confirm the failure count, restore.
- `two-sided-grader` — how curriculum graders are proven (pristine fails,
  reference passes), if the loop ever touches assignments.
- `three-arena` — the session/turn/tool-call lifetime rules and the arena lint.
- `milestone-ritual` — the ROADMAP `### M###` entry + CHANGELOG entry + banner
  bump + the commit convention.
- `supervise-long-command` — **already ships** (M226); the pack reuses it so a
  hung `make ci` is backgrounded, not blocking.

### Commands — bounded slash-command workflows (`.jichi/commands/*.md`)

- `/review-diff` — run `c89-reviewer` + `arena-auditor` over the working diff.
- `/red-first <test>` — prove a test fails without its fix (drives the ritual).
- `/milestone <desc>` — a `subtask:`-scoped chore: implement a *well-specified*
  change, author its test shown-red-first, update the docs, stage a commit —
  under the envelope. (For *specified* work; design happens before this.)

### config.example.json — many models, the right roles, the guardrails on

The heart of "various models (whatever is available)". A merge-in example, not
a live config (never a literal key — `apiKeyEnv` only):

- **Models + roles + tiers.** Register every reachable server (HRZ, a local
  GPU, LM Studio) and assign roles: a **`strong`** model for review/design
  tasks, a **`fast`** model for mechanical edits, a **small-local** as
  `fallback`. Roles: `chat`/`edit`/`apply`/`summarize` on the working models,
  `embed`/`rerank` for codebase RAG over jichi's own tree.
- **Routing + fallback.** `routing: {fast, strong, escalateOnVerify,
  escalateOnError, escalateOnStall}` so a mechanical turn runs on `fast` and
  *escalates* to `strong` when a verify fails or the model stalls; a
  `fallback` chain so an unreachable server degrades to the next
  (`jc_net_reachable`). This is exactly what "whatever is available" needs.
- **The autonomy envelope, armed** (the load-bearing safety):
  - `editScope`: `src/**`, `include/**`, `tests/**`, `docs/**` — and **not**
    the Makefile-critical build wiring, **not** `~/.jichi*`, **not** the
    envelope's own C (`src/chat/jc_envelope.c`, `src/util/jc_path.c`) without a
    human.
  - `verify`: `make check-target` (full portable gate) or `make test` (fast),
    with `verifyRetries` for fix-forward and `--verify-every` for periodic
    banking; a red verify rolls back to the last green checkpoint.
  - `budget`: token/wall-clock/tool-call caps (`--budget-*`).
  - `revertOutOfScope: true`, `selfReview: 1`, `snapshots: true`,
    `pathFence: 1`, `privilegedCommands: deny`.
  - `journal`: on — the run journal is the audit trail (`runs`/`audit` readers).
  - `learnOnStop: true` — after a clean run, draft lessons
    (`docs/LEARNING.md`), so the loop *improves* across runs.
  - `toolProfile`/`contextLimit` sized to the working model, `promptCache` on,
    calibration on (a small-context model self-tunes, M77).
- **Run on a branch/worktree, never master.** The pack's docs mandate a
  `git worktree` or feature branch, so `spawn_parallel`-style isolation and a
  clean `undo`/rollback story hold, and master is never touched by the loop.

## How it composes with what already exists

This pack is **scaffolding that points existing machinery at jichi-on-jichi**;
it invents little new mechanism:

- **The autonomy envelope** (`docs/AUTONOMY.md`) is the safety substrate.
- **`docs/AUTONOMOUS_LOOPS.md`** already describes running jichi as an
  unattended loop over a **task queue** (tmux/systemd/cron supervisor,
  file/DB/HTTP reporting, threat model). The pack supplies the *task menu* and
  the agents; that doc supplies the supervisor. `examples/autonomous-loop/` is
  the runnable reference.
- **The learn loop** (`docs/LEARNING.md`, `learn analyze` → `/learn` →
  `learn apply`) closes the improvement loop: the self-hosted runs' telemetry
  becomes durable lessons and skills.
- **`docs/SELF_IMPROVEMENT.md`** (the M100+ band: daemon, assignment/eval
  harness, "dream" consolidation, the workflow DSL) is the long-range home; this
  pack is a concrete, near-term first step toward it.
- **The bench** (`tests/bench/`) is how we *measure* whether a given model is
  good enough for a given agent's job before trusting it.

## A realistic task menu, by model tier

The pack's docs should say plainly what to delegate to what:

- **Small local model (≤8B-ish):** apply a lint's mechanical findings; run the
  suite and report; reformat; rename across files with a clear spec; write a
  boilerplate test skeleton. Tool-call reliability is the bar, and the bench
  says it clears it.
- **Mid model (HRZ `gemma-4-31b` class):** review a diff with `c89-reviewer`;
  write a real unit/smoke test for a *described* change; update ROADMAP/CHANGELOG
  in-style; draft a commit message; triage a failing test.
- **Strong model (frontier, today Claude Code):** design a milestone, reason
  across subsystems, make the judgment calls. **The pack does not pretend a
  local model does this.** Its honesty about that boundary is a feature.

## Recommended build order

1. **Config + the two `readonly` review agents** (`c89-reviewer`,
   `arena-auditor`) and `/review-diff`. Zero write-risk; validate them against
   HRZ on a real diff, compare to a human review. *(Highest value / lowest risk;
   ship this first even alone.)*
2. **`test-author` + `shown-red-first` skill + `/red-first`**, envelope-gated
   (`verify: make test`, `editScope: tests/**`). Bounded writes, verifiable.
3. **`doc-updater` + `committer` + the `milestone-ritual` skill.** Low-risk
   prose/edits with a clear template.
4. **The autonomous-loop integration**: a small, curated **chore queue** (apply
   pending lint findings, refresh a stale doc number, add a missing test for an
   under-covered pure function) run under the supervisor from
   `docs/AUTONOMOUS_LOOPS.md`, reporting to a file the maintainer reviews.
5. **Measure + learn**: bench the models per agent role; turn on `learnOnStop`;
   feed telemetry back. Only widen `editScope` to `src/**` after (1)–(4) have a
   track record.

## Risks and limits (state them in the shipped docs)

- **Capability:** local models can't design jichi; the pack is a tail-worker,
  not a replacement. Overtrusting it produces plausible-but-wrong diffs — which
  is exactly why the verify gate + shown-red-first + `make ci` are mandatory,
  not optional.
- **The meta-risk:** the loop must not edit its *own* guardrails unsupervised.
  *Implementation note (M241):* `editScope` turned out to be a **positive
  allow-list with no exclusion syntax**, which made this cleaner than the
  original plan — scoping writes to `tests/` + `docs/` leaves `src/` (and the
  envelope's own C) simply **unreachable** by the edit tools, so no exclusion
  rule is needed; a shell-introduced `src/` change is caught by
  `revertOutOfScope`. Keep the envelope's C off the delegable list by keeping
  `src/` out of `editScope`.
- **Context size:** 79k lines exceed a small model's window; the repo map + RAG
  + tool profile mitigate for *local* tasks, but cross-subsystem work needs the
  strong model. Size `contextLimit` honestly per model (M73/M77).
- **Bootstrap quality:** jichi editing jichi can regress jichi. The guardrail is
  the same wall the project already trusts — `make ci` (gcc+clang `-Werror`,
  ASan/UBSan, valgrind, smoke, e2e) as the verify command for anything touching
  `src/`. Never merge a self-hosted diff that CI hasn't passed.

## Decisions (resolved during implementation)

1. **Scope of the first cut — done.** Both slices were built: the read-only
   review slice (M239, `c89-reviewer` + `arena-auditor` + `/review-diff`) and
   the write-enabled slice (M241, `test-author`/`doc-updater`/`committer` fenced
   to `tests/`+`docs/` under the envelope). Steps 1–3 of the build order
   shipped; write access to `src/` remains a deliberately-later, more careful
   slice.

2. **Where the pack lives — DECIDED (2026-08-02): stay in
   `examples/self-hosting/` until it has been *exercised*, then promote.** A
   compiled `jichi-dev` pack — baked into the binary as chunked C literals, so
   `jichi init jichi-dev` works from just the executable — is the eventual home.
   But promotion costs a `jc_scaffold.c` / `jc_setup.c` / scaffold-test change
   and **freezes the assets into 509-char C literals that are expensive to
   iterate**, whereas a loose reference set (a `cp` into `.jichi/`) is a
   one-line edit to change. So it stays a reference set while the shape is still
   moving. **Promote only when it has earned its shape**, concretely — all three:
   - the read-only reviewers have produced *useful* findings on several real
     diffs (real `file:line` issues, not invented nitpicks, not missed planted
     violations);
   - a write agent has completed a real test-authoring / doc-update task under
     the envelope end to end — which needs a **responsive** model: the M239
     HRZ-latency finding is the current blocker, and a faster or local reviewer
     clears it;
   - the agent set, tool fences, and config defaults have stopped churning.

   Until all three hold, iterating on files beats re-splitting C tables. This is
   the same prototype-in-`examples/`-then-promote path every other scaffold pack
   took.

## Open questions still for the maintainer

3. **Model policy:** which servers to register by default in the example config
   (HRZ only, or HRZ + a local placeholder), and the default role→model map.
4. **Reporting surface:** file (simplest), the run-journal readers
   (`runs`/`audit`), or an HTTP sink per `AUTONOMOUS_LOOPS.md`.
