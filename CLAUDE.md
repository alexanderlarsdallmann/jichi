# CLAUDE.md

Guidance for working in this repository.

## Before you act

Each rule is keyed to the moment it applies. The sections below give the reasons and the incidents:
check the line, and read its section when it fires.

1. **Choosing a model** -- `jlu/*` on the HRZ gateway or a local LM Studio only. A priced model needs
   the operator's grant for that one run, asked *before* the request. (Models)
2. **Typing a cap** -- no `timeout`, `--deadline`, `--budget-tokens` or stall cap on a run that measures
   or reproduces anything, not even a generous one: a reproduction is a measurement. (Caps versus fences)
3. **Probing a command** -- classify it by its effect first. Never run what may write to learn what it
   does, and never probe in this repository. (Classify an action by its EFFECT)
4. **Publishing a command** -- run it first, in the published form, `< /dev/null` included. (Tests)
5. **Writing a test** -- watch each check go red without its fix, prove the instrument can fire, and
   make a fixture behave like the real peer. (Tests; `docs/TESTING_RUNBOOK.md`)
6. **Reaching for a mock** -- if the binary, a local model or the service can run, run it. (Tests)
7. **Believing a green** -- count what the gate reached, and check its pattern with the gate's own tool
   (`/usr/bin/grep`, not a session's `grep`). (Tests)
8. **Merging** -- only after `make ci` is green on the exact head. An agent's review is a second opinion.
9. **Writing C** -- C89 at zero warnings under `-std=c89 -pedantic -Wall -Wextra`; `jc_snprintf`, never
   `sprintf`; tool errors are values; the shortest arena that outlives the data. (Architecture)
10. **Touching a platform, or claiming one** -- start from `docs/SESSION_RUNBOOK.md`; a row is not tested
    until a model has been driven on it. (Platforms)
11. **Adding a rules file** -- never an `AGENTS.md` here: jichi reads it first, and it shadows this file.
12. **Sharing between machines** -- in `docs/internal/` (export-ignored); never a secret. (Repository)
13. **Committing** -- author, committer and the `Co-Authored-By: jichi` trailer all carry the
    maintainer's address (`docs/internal/COMMIT_IDENTITY.md`); never `noreply@jichi`, a placeholder or
    a respelling. (Repository)

## What this is

`jichi` ("just code") is a from-scratch reimplementation of the Continue CLI AI
coding agent, written in **C conforming to the C89 (ANSI C / C90) standard**. It is
Linux/POSIX-only. Two binaries are produced:

The project was renamed from `jlu_continue` at M170. The `jc_`/`JC_` symbol prefix
is unchanged and now abbreviates both *jichi* (自治（じち）, autonomy) and *just code* —
choosing a j+c name is what kept ~24k identifiers untouched. State lives in
`~/.jichi`, `~/.jichi.d/` and per-project `.jichi/`. The **wire values** must not
be renamed: the `jlu/…` HRZ model ids and `*.uni-giessen.de` are what the API
calls literally address, so changing them breaks the request. The env-var *name*
`JLU_API_KEY` is **not** a wire value — it is only a config `apiKeyEnv` string —
so the setup wizard and example configs now default to **`JICHI_API_KEY`** (M242);
`JLU_API_KEY` still works wherever a config or a user's `~/.jichi.env` names it
(back-compat, and the HRZ onboarding still uses it). "jlu" has three senses here
and only one is the project. See docs/MIGRATION.md.

- `jichi` — the agent (interactive TUI + headless `-p` mode)
- `jichi-convert` — converts a Continue `config.yaml`/`config.json` into our config

## Models: local only, and spending is an action that needs consent

**Use only locally-hosted models: the institution's `jlu/*` models on the HRZ
gateway (`https://api.hrz.uni-giessen.de/v1`), or a local LM Studio server.**
Those publish `input_cost_per_token: 0.0` or no price at all, so a run costs
wall-clock and nothing else.

**Never select a priced model** — `anthropic/*`, `openai/*`, `vertex_ai/*` and the
rest are visible from the gateway (353 model ids) and are **not** thereby
permitted. An exception must be **granted explicitly by the operator, for a single
named run**; it does not carry to the next run, and it is not implied by:

- a key being *able* to reach the model (capability is not licence);
- a default in a test harness: `tests/smoke/priced_model_lint.sh` refuses a priced
  default in any harness, and a priced id in any file that names the gateway (M544);
- the operator telling you where a key lives, in answer to a broken run;
- **an id that starts working when you strip a prefix.** `jlu/tts-1-hd` answers
  HTTP 500 and bare `tts-1-hd` answers 200 — because the bare alias routes to
  **OpenAI, priced**. A namespace separates the free from the billed, so
  stripping it to make an error go away is the same move as removing a fence to
  silence a warning. **Before any request, check the id against the free-namespace
  listing** (`GET /v1/models`, filtered to `jlu/`) — that is a grep, not a
  virtue. ANECDOTES #68, which is #63 wearing a different hat three weeks later.

**Spending is in the same class as a force-push or an `rm -rf`:** the cost lands on
someone else and cannot be taken back, so consent comes **before** the request, not
as a report afterwards. If a run is about to cost money, stop and ask — one
sentence, naming the model, the key and the expected number of runs.

**jichi does warn: read it.** `doctor` reports
`! no pricing for the active model: every cost reads $0.00` for any config that
declares neither `inputCostPer1M` nor `outputCostPer1M`, and without pricing a run
records `cost_usd: 0` while the budget drains. ~$10 of a shared key went that way,
past that very warning (ANECDOTES #63). Declare pricing for any priced model.

## Build & test

```sh
make              # build both binaries
make jichi # just the agent
make jichi-convert  # just the converter
make test         # build + run the unit suite (./run_tests)
make smoke        # POSIX-sh smoke tier (M209): validates a build with NO
                  # python3 -- tests/smoke/*.sh + the test-only C89 helpers
                  # in tests/tools/ (mockmodel/ptydrive/jsonq/sockq, `smoke-tools`)
make smoke-faults # the error-path tier: builds FAULT=1 and runs the five
                  # FAULT-gated drivers (M482; the lease race M760, the board M761).
                  # They SKIP on a normal binary,
                  # and until M482 no stage of `make ci` built one -- so they ran
                  # nowhere, including here. `make ci` now calls this; smoke_lint
                  # check 16 fails the build if a FAULT-gated driver is not named
                  # in the target.
make check-target # on-target validation for old/small systems: test + smoke
make info         # show detected toolchain features
make clean
```

Opt-in build knobs: `WERROR=1` (warnings as errors), `SAN=1` (ASan/UBSan),
`SIZE=1` (size-optimized: `-Os` + section GC + stripped, for low-resource
targets; `LTO=1` adds `-flto`). The default build passes no `-O` flag.

First-party code **must compile with zero warnings** under
`-std=c89 -pedantic -Wall -Wextra` — **every** translation unit, with no
exemptions. (`src/json/cJSON.c` used to be exempt as "vendored"; it is not
vendored and is pedantic-clean, so the exemption was removed at M171.) POSIX-only translation units rely on
`-D_POSIX_C_SOURCE=200112L` (set globally in the Makefile).

The Makefile probes the toolchain at configure time (`make info` shows the
result); the probe list, the dependency rationale and the operator-facing manuals
are in [`docs/BUILD.md`](docs/BUILD.md), and every page in the tree is indexed by
[`docs/README.md`](docs/README.md).

**Dependencies: libcurl and nothing else**, linked rather than vendored. jichi
vendors **no third-party source** — `src/json/cJSON.{c,h}` is original code
implementing the cJSON *API*, not a copy of that library (M171).

## Architecture

Request/response flow for one user turn (`jc_agent_run_turn`,
`src/chat/jc_agent.c`):

```
history + system + tools
  -> provider->build_request()            (provider-specific JSON; rebuilt per
                                           retry attempt)
  -> jc_http_stream() [libcurl]           src/net/jc_http.c
       (req.stream_body=1: the body is uploaded via a read-callback and freed
        the moment it is fully sent -- ownership transfers to jc_http -- so it
        isn't held during the response stream; M20e)
  -> jc_sse_feed() [SSE framing]          src/net/jc_sse.c
  -> provider->on_event() per SSE event   src/provider/*
       -> jc_stream_sink (text deltas to UI)
       -> accumulates text + tool calls into the assistant jc_message
  -> on done: if tool calls, execute them (jc_tool_execute), append
     tool results to history, and loop; else return the final answer
```

### Layers

The layer-by-layer reference — what each of `src/platform`, `src/util`,
`src/json`, `src/config`, `src/net`, `src/provider`, `src/tools`, `src/chat`,
`src/session`, `src/tui`, `src/index`, `src/mcp`, `src/command`, `src/lsp`,
`src/snapshot`, `src/skill`, `src/acp`, `src/scaffold` and `src/setup` holds, and
why — is **[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)**.

It lived here until M516 and moved for a measured reason: at 113 KB it was 81% of
this file, `jc_rules.c` caps the rules block at 32 KB, and the sections *after*
it — the test-integrity rules, the platform rules, the lint-auditing rules — were
therefore never delivered to any model at any context size
(`docs/analysis/2026-08-21-self-hosting-first-review.md` §5). Reference in a rules
file is charged against every request and evicts rules. Read it when you need it;
the invariants it implies are below, because those you must not violate without
noticing.
### Conventions & invariants

- Fallible functions return `jc_status`; outputs via pointers. No exceptions.
- **Never call `sprintf`** — `jc_snprintf` is bounded by construction, and
  `tests/smoke/sprintf_lint.sh` enforces it. Same for the other unbounded
  string calls (`strcpy`, `strcat`, `gets`).
- **The agent never branches on provider.** A backend is a new `jc_provider`
  vtable, not a new code path in the loop: `build_request` is the only place a
  dialect enters, and the loop hands it history/system/tools and takes back bytes
  it does not inspect. Argued in `docs/reading/fukabori-02-the-provider-abstraction.md`.
- **A boolean a human, a foreign program or a MODEL writes is read leniently.**
  People write `1` for true in a config file; `jc_json_get_bool` requires a real
  JSON bool and returns the *default* for anything else. Twice this has silently
  disabled a fence, both times because a **presence check** turned the fallback
  into an explicit denial: fifteen shipped configs saying `"pathFence": 1` ran
  with the fence **off** (M519), and `spawn_subagent`'s `{"readonly": 1}` gave a
  **writable** child to a model that asked for a read-only one (M530). The rule
  is *who wrote this JSON*: config files, a converted foreign config, an editor's
  ACP capabilities, a supervisor's control request, a session file on disk, a
  workflow map, an MCP server's `isError`, and **a model's tool arguments** all
  use `jc_json_get_bool_lenient` (`tu_arg_bool` is now that function). Only
  jichi's own sinks — telemetry, the run journals — stay strict, where a non-bool
  is a bug to see. Prose still falls through to the default, as in
  `jc_json_get_num_lenient` (M168): guessing at prose is how a typo becomes a
  policy. `tests/smoke/config_bool_lint.sh` asserts the boundary by name.
- **A preview must read every argument exactly as the executor will.** The TUI's
  diff preview read `replace_all` strictly while the tool read it leniently, so a
  model sending `"replace_all": "true"` produced a preview showing one occurrence
  replaced and an edit that replaced all of them — the user approved a narrower
  change than the one that ran (M530). Approval integrity, not a display bug.
- **Tool errors are values, never control flow.** A refused or failed tool call
  returns a result with `is_error` set, which becomes a `tool`-role message the
  model reads and can act on; `jc_status` answers "did the machinery work", the
  result answers "did the thing happen". `edit_run` does `tu_err(...)` and then
  `return JC_OK` for exactly this reason. Traced end to end in
  `docs/reading/tsuiseki-04-the-call-that-was-wrong.md`.
- **Three arenas**, by lifetime — pick the shortest one that outlives the data,
  because getting this wrong is the bug class that cost M197/M198/M199 (see
  `docs/analysis/2026-07-29-tool-arena.md`):
  1. `app->arena` — **session-lived**, freed only at process exit: config, rules,
     repo map, skill/command/agent defs, session id, the todo list's structure,
     the envelope's commits. Never put per-call or per-turn data here.
  2. `app->scratch` (`jc_app_scratch`) — **per top-level turn**, reset in
     `jc_agent_run_turn` when `agent_depth == 0`: the system message, command and
     `@`-ref expansion, and anything that must survive a **nested agent run**
     (`spawn_subagent`'s seed task and tool fence).
  3. `app->tool_scratch` (`jc_app_tool_scratch`) — **per tool call**, reset
     immediately before every `jc_tool_execute` at every depth (M199): a file's
     bytes while a tool formats, matches or uploads them. Bounds the peak of a
     200-iteration turn to one call. **Invariant:** nothing here may outlive a
     nested agent run, since that run's own tool calls reset it — use (2) if it
     must. `tests/smoke/arena_lint.sh` enforces that `src/tools/` and `src/lsp/`
     use (3) or (2), never (1), outside tool registration.
  `jc_sb` for unbounded buffers; every `cJSON_Parse` is matched by `cJSON_Delete`.
  A leak checker cannot see a lifetime bug — ASan and `valgrind --leak-check`
  report zero when memory is reachable until exit — so use a footprint assertion,
  massif's peak, or `jc_arena_used` via `/context`.
- The active model is mirrored into `config.model`; the provider holds a pointer
  to that stable address, so switching models updates it in place then rebuilds
  the provider.
- Signal handlers touch only a `volatile sig_atomic_t` flag; `tcsetattr` etc.
  run from the main loop.
- See `CONTRIBUTING.md` for the full C89 rules (declarations at block top, no
  `//`, no designated initializers, no `<stdint.h>`/`long long`, split string
  literals over 509 chars, `%lu` with casts not `%zu`, etc.).

## Tests — the rules

*Reference (what each tier is, what it holds, the incidents that shaped it):
[`docs/TEST_TIERS.md`](docs/TEST_TIERS.md) — `docs/TESTING.md` is the `run_tests`
tool, a different subject (M527) — and the doctrine in
[`docs/TEST_INTEGRITY.md`](docs/TEST_INTEGRITY.md) /
[`docs/GATE_INTEGRITY.md`](docs/GATE_INTEGRITY.md).*

- **`make ci` is the gate.** Nothing merges that it has not passed: gcc and clang
  at `-Werror`, ASan/UBSan, valgrind, the unit suite, the Python-free smoke tier,
  the e2e tier. An agent's review is a second opinion, never a verdict.
- **A new test must be shown to fail without its fix** — revert the guard, run,
  confirm the failure count, restore. `tests/teeth.sh` scripts the ritual. A test
  never observed failing has never been observed working.
- **Follow [`docs/TESTING_RUNBOOK.md`](docs/TESTING_RUNBOOK.md) when you add a
  test.** Ten steps, each naming the incident that produced it: perturb per
  CHECK, prove the instrument can fire, floor the extraction and print the set,
  enumerate the universe twice, `make smoke-mutant`, and for a measurement run
  fences on / caps off / diagnostics ON.
- **Prefer a lint to an audit.** An audit finds what it knew to look for, once; a
  lint finds it forever. When you write one: state its universe in its header,
  floor its extraction at today's exact count, and prove it red.
- **Audit the universe, not the result.** A green check tells you its universe is
  clean, not that the universe is the one you meant. Enumerate the set a *second
  way*, by a different route, and diff — that is where four consecutive gaps came
  from (M508, M510, M511). `docs/TESTING_TUTORIAL.md` §6 teaches this; the
  incident register is `TEST_INTEGRITY.md` §"Audit the universe".
- **Measure the population before building a gate.** A plan to widen the quote
  lint across `docs/` was dropped after counting: 1 verbatim quote in 110 blocks.
- **Verify a gate's pattern with the gate's own tool**, against a recorded real
  response. In an agent session bare `grep` here is a shell-function shim that read
  a lint's own pattern as matching 0 lines where `/usr/bin/grep` matched 1,443.
  Hand-checks use `sh -c '…'` or an absolute path. And **a classifier's
  else-branch must not be a finding**: a probe whose `native` pattern could not
  cross a newline reported `prose` — a positive claim — for five capable models
  (M519). Make the fallback say *unknown* — and since M628 the tool-calling
  probe does: `JC_TOOLPROBE_UNKNOWN` for an empty reply, distinct from `none`.
- **Run every command you publish**, in the form you publish it — including
  `< /dev/null` on a headless run, whose absence blocks forever (ANECDOTES #64).
- **Never mock what you can run for real**: the binary, a free local model, the service.
  Mock only what they cannot produce, and say so.
  [Why](docs/TEST_INTEGRITY.md#run-the-real-thing).
- **Classify an action by its EFFECT before choosing how to probe it.** An
  existence question is never answered by execution: `export` prints, `rewind`
  reads, `undo` **writes**, and probing the three as one set reverted 768 files of
  this tree. `--help`, `--dry-run`, `jichi describe` or the source answer it for
  free. **Probe in a workspace you are willing to lose** (`smoke_tmp`), never in
  this repository. ANECDOTES #66; `docs/analysis/2026-08-22-learning-from-errors.md`
  for why "be more careful" is not the fix.
- **Perturb per CHECK, not per driver.** The unit that can be vacuous is a single
  check: four of mine once covered less than their headers claimed, and passed
  while wrong (`docs/TESTING_RUNBOOK.md` §1 has the four). Revert the thing each
  check guards, watch **that** check go red, and read the extracted set, not its size.
- **Caps versus fences.** Fences (`--edit-scope`, `pathFence`, read-only agents,
  `--max-tool-calls`) bound blast radius and stay on. Caps (`--deadline`,
  `--budget-tokens`, `stall`/`request` timeouts) bound *duration* and stay **off
  for measurement runs** — a cap that fires does not merely hide the answer, it
  manufactures a plausible different one. Off means no wrapper, and a
  reproduction is a measurement: an unfired `timeout` flipped a crash
  (ANECDOTES #120). Keep `connect`: it bounds TCP and the TLS handshake, so a
  slow handshake trips it too (raise it; M877), never an arriving answer. **And
  keep `timeouts.deadStream` (M-L2, default 900 s):** a fence, not a cap -- it
  fires only when *nothing is arriving* on a connected stream, so it stays on
  even at `stall 0`: a model call once ran 11,677 s after its stream died.
- **Do not add an `AGENTS.md` to this repository.**
  `src/chat/jc_rules.c:add_dir_rules` tries `AGENTS.md` first and **returns** if
  it exists, so `CLAUDE.md` is only the fallback: an `AGENTS.md` here, even a
  one-line pointer, would shadow this entire file for every run in this checkout.
  `AGENTS.md` is the name jichi scaffolds into *other people's* projects.


## Platforms — the rules (read this before a portability change)

*Reference (verdicts per libc and kernel, the rig inventory, what each row
taught): [`docs/PLATFORMS.md`](docs/PLATFORMS.md); RAM tiers and hardware in
[`docs/LOW_MEMORY.md`](docs/LOW_MEMORY.md).*

- **Start every session with [`docs/SESSION_RUNBOOK.md`](docs/SESSION_RUNBOOK.md).**
  Every rule in it was written after breaking it. Step 0 is
  `scripts/preflight.sh`, which refuses a tree whose gate is already running; the
  step order does not vary. §4b is self-development, §5 is moving machines.
- **Report the effect, never the attempt.** An `ok - console moved to com0`
  printed after merely *writing* the keystrokes was green on a run where the
  loader had dropped a character. Where the target echoes, the echo is the check.
- **Rig output goes to `$TIER_V_DIR`, never the repo tree.** A rig that dirties
  the tree it tests has been three separate mistakes.
- **`JC_SMOKE_TIMEOUT_MULT` is a ratio**, device build seconds ÷ *this bench's*
  build seconds — copy the formula, never a row's number. On a new machine the
  denominator is wrong and nothing tells you.
- **A capability probe must ask the right question**, and carries
  `-Werror=implicit-function-declaration`: uClibc-ng declares `malloc_trim` only
  under `__USE_GNU`, so the symbol linked while the declaration was hidden and
  the probe said yes (M449). `CC ?= cc` means a system shipping neither `cc` nor
  `c99` reports every feature absent rather than the compiler missing — pass
  `CC=gcc` there (M458).
- **A POSIX success contract is not always "returns 0", and the platforms that
  agree will not tell you.** `uname()` is specified to return a **non-negative**
  value on success; illumos returns a positive one. Four call sites written
  `== 0` were right on Linux, three BSDs and both Windows layers and read a
  working call as a failure on the first SysV kernel they met — which left
  M695's platform-verdict table unreachable there and `doctor` telling an
  illumos user jichi had never been compiled on their system. Read the wording,
  not the six agreeing platforms; `portability_lint` check 24 enforces this one.
  ANECDOTES #97.
- **A row is not tested until a MODEL has been driven on it.** Every gate a rig
  runs is **offline**, so all of them go green on a kernel where jichi has never
  called a model. Drive one text turn and one **tool-calling** turn whose result
  the model must use, with a model `doctor --live` calls **native** (a `text`
  caller executes nothing and looks like a platform failure); reach a guest with
  `ssh -R 1234:127.0.0.1:1234`, LM Studio staying loopback-bound. Record the task,
  the model and the seconds **on the row**: `PLATFORMS.md` calls it **Driven**. A
  first count that read the matrix, not the runs, is told in `docs/DEFERRED.md`.
- **A verdict is Verified / Partly verified / Never compiled, used strictly.**
  Never write a platform claim the matrix does not support; a non-Linux row is a
  defect detector, not compatibility work — two OpenBSD findings were bugs on
  every platform.


## Roadmap

`docs/ROADMAP.md` records the design + implications of every milestone, and
the orchestration model. The four capabilities this line used to call "next
planned" — user-defined tools (M7), `@`-references (M8), autocomplete (M9) and
the ACP server (M10) — all shipped, and are recorded there as history. It's
advisory; each milestone gets its own plan.

## Repository

Git with an `origin` remote (`gitlab.hrz.uni-giessen.de:journey/jichi`);
history is linear on `master`. Build artifacts are gitignored; commit source,
tests, docs, and `src/json/cJSON.{c,h}` (ours, despite the API-compatible name).

**Every commit is the maintainer's**, author and committer, and jichi's trailer carries the same
address, because agents copy the nearest commits (`docs/analysis/COMMIT-ATTRIBUTION.md`).

**What the machines share and strangers do not goes in `docs/internal/`.** That
covers handoffs, claimed milestone ranges, procedures that name the institution's
hosts, and the small data a drive or an experiment starts from: task texts,
lists, preambles, excerpts of evidence. The directory is `export-ignore`d
(`.gitattributes`, M577), so no public snapshot carries it. Machine paths are
allowed there. Secrets and bulk data are not. `docs/internal/README.md` states the
rule and lists what is there. The drives' task texts live there because the only
other copies were deleted with a drive's data (ANECDOTES #108).

The project path was changed **in place** at M170c (`journey/jlu_continue` →
`journey/jichi`), so the full history, issues and CI config came with it and GitLab
redirects the old URL. Do **not** treat this as the public release repository: the
public snapshot gets its own curated **git history** (fresh first commit). The
license is decided (M619, 2026-08-27): **Apache-2.0**, copyright
Justus-Liebig-Universität Gießen, author Alexander-Lars Dallmann — with a possible
deliberate switch to MIT after review (`scripts/set-license.sh MIT`; the candidate
text is in the tree). The documentation, however, ships **in full**
(decided 2026-07-28): `docs/analysis/`, `docs/plans/`, `docs/dialogues/`, and the
anecdotes are deliberately part of the release — jichi's complete, honest project
documentation (requirements, decisions, failures, lessons) is a feature, not
internal residue. Publication is gated on the license: `scripts/make-snapshot.sh` refuses `--commit`
without a `LICENSE` file (M484), and `tests/smoke/snapshot_lint.sh` scans the tree
that would be published (M485/M487).

The local checkout also lives at `.../miscellaneous/jichi` now, with a deprecated
`jlu_continue -> jichi` symlink beside it; see docs/MIGRATION.md for the
workspace-keyed caches that had to move with it.

## Anecdotes

`docs/DECISIONS.md` indexes the decisions that shaped jichi, each with the
alternatives **rejected** and a pointer to the full reasoning (ROADMAP entry or
proposal). It starts at M293 and is deliberately not back-filled. Add a row when a
decision is one a future reader could reasonably disagree with; if there was no
rejected alternative, it was not a decision.

`docs/ANECDOTES.md` is a running log of debugging war stories worth remembering
(symptom → dead ends → root cause → lesson). Entry 1 is the "stderr truncation"
that turned out to be the autonomy envelope's rollback reverting a log file kept
inside the workspace — a reminder to keep observability outside the
snapshot/rollback blast radius, and to make flaky dependencies deterministic
before debugging. Add to it when an investigation teaches something durable.
