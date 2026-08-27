# Contributing to jichi

Two halves: **how to send something** (below) and **the C89 rules any code must
follow** ([jump](#c89-coding-rules)). The second half is the older and the longer
one; for a long time it was the *only* one, which meant this file explained how to
write a change and never how to offer it.

## Where development happens

**This public repository receives curated snapshots. It is not where the work is
done.** Development is in a private repository with a long milestone history, and
each public release is a fresh tree published from it, deliberately without that
history — the reasoning is in
[`docs/plans/2026-08-public-snapshot.md`](docs/plans/2026-08-public-snapshot.md).

That has one consequence worth stating plainly, because it decides what is worth
your time:

| | |
|---|---|
| **Issues, questions, bug reports** | **Yes, please.** These are the most useful thing you can send, and the thing this project cannot generate for itself: it has one author and one main development machine, so almost every defect found on a platform, a libc or a workflow that is not this one arrived from outside. |
| **Patches and pull requests** | **Read, and applied by hand when they are right — but not merged as commits.** A patch here cannot become a commit in the private history, so a merge button would be a promise this repository cannot keep. Send the diff or the branch; if it lands, the change is attributed to you in `CHANGELOG.md` and in the milestone that carries it. |
| **A pull request left open for weeks** | Should not happen. An unanswered contribution is worse than a stated "not yet", which is why this section exists at all. |

## A good bug report

The house style, and it is not ceremony — this project's own register is
**measured, or it says it is not**:

1. **What you ran**, exactly, and on what. `jichi doctor` prints the version, the
   platform, the resolved config and the health of every subsystem; its output is
   the single most useful thing to paste, and it **never prints secrets** (keys are
   reported present or absent).
2. **What happened**, in its own words. Paste the error rather than a paraphrase of
   it — stderr is where jichi's diagnostics go, and the exact wording is often the
   whole diagnosis.
3. **What you expected**, if it is not obvious.
4. **Whether it reproduces**, and how reliably. "Once, and I cannot get it back" is
   still worth reporting; say so and it will be recorded as that.

If it is a **portability** report — a platform, a libc, a shell, an architecture —
say which, and include `make info`. [`docs/PLATFORMS.md`](docs/PLATFORMS.md) records
every verdict as Verified / Partly verified / Never compiled, and a new row from
someone else's machine is the single highest-value contribution to this project:
every non-Linux platform brought so far has found real defects that were present on
*all* of them and only became visible elsewhere.

**Security issues go to [`SECURITY.md`](SECURITY.md), not to a public issue.**

## Before you send code

```sh
make WERROR=1 test     # the unit suite; must be 0 failures
make smoke             # the POSIX-sh tier; needs no python3
make ci                # everything: two compilers, ASan/UBSan, valgrind, e2e
```

`make check-target` (`test` + `smoke`) is the portable gate and is what to run on a
platform that cannot manage the rest. Then three expectations, each of which this
project has broken and written up:

- **Zero warnings**, every translation unit, under `-std=c89 -pedantic -Wall
  -Wextra`. No exemptions exist, not even for `src/json/`.
- **A new test must be shown to fail without its fix.** Revert the fix, run, watch
  the failure count change, restore. A test never observed failing has never been
  observed working — [`docs/TEST_INTEGRITY.md`](docs/TEST_INTEGRITY.md) is a
  catalogue of this project's own suites passing while measuring nothing.
- **Say what you did not verify.** An honest gap is worth more than a confident
  guess, and it is the register the rest of the documentation is written in.

[`docs/SESSION_RUNBOOK.md`](docs/SESSION_RUNBOOK.md) is the order the maintainer
works in, and every rule in it was written after breaking it.

## C89 coding rules

All first-party code **must compile cleanly under `-std=c89 -pedantic -Wall
-Wextra`**. C89 is stricter than most C you have written; this is the
cheat-sheet of what bites in practice.

## Language

- **Comments:** `/* ... */` only. No `//`.
- **Declarations first:** every block declares all its locals *before* any
  statement. No `for (int i = 0; ...)`. Hoist the counter:
  `int i; for (i = 0; ...)`. Open an inner `{ }` block if you need a late
  declaration scoped tightly.
- **No designated initializers / compound literals.** Zero with
  `memset(&x, 0, sizeof x)` then assign fields, or use positional
  initializers in a fixed, documented field order (tool tables do this).
- **No `//`-style booleans:** there is no `bool` / `<stdbool.h>`. Use the
  project `jc_bool` with `JC_TRUE` / `JC_FALSE`, or plain `int`.
- **No trailing commas** in enum or initializer lists.
- **No `long long`, no `<stdint.h>`.** Use the typedefs in `jc_platform.h`.
  We avoid 64-bit integers entirely: token counts and costs are `double`,
  sizes are `size_t` / `long`.
- **No `inline`, no `__func__`, no variable-length arrays.**
- **No variadic macros.** Logging is a variadic *function* (`jc_logf`); that
  is fine. `__VA_ARGS__` is not.
- **String literals** are only guaranteed to hold 509 chars and logical lines
  4095 chars. Split long literals via adjacent-literal concatenation
  (`"part one " "part two"`), e.g. the system prompt and tool descriptions.

## Library

- **No `snprintf` / `vsnprintf`** in strict C89. Use `jc_snprintf` /
  `jc_vsnprintf` (and `jc_sb_append_fmt`) from `jc_snprintf.h`. **Never call
  `sprintf`** anywhere; there is no vendored code to exempt.
- **Format size_t as `%lu`** with an `(unsigned long)` cast. Never `%zu`.
- **No `strdup`** (POSIX, not C89). Use `jc_strdup` (heap) or
  `jc_arena_strdup` (arena).
- **POSIX calls** (`opendir`, `mkdir`, `stat`, `popen`, `termios`,
  `TIOCGWINSZ`, ...) are allowed but confined to a few translation units.
  Those files rely on `-D_POSIX_C_SOURCE` (set globally in the Makefile). Keep
  POSIX usage localised so the bulk of the tree stays portable C89.

## Memory & errors

- Fallible functions return a `jc_status`; outputs go through pointers. There
  are no exceptions.
- **Tool execution errors are values**, not control flow: a failing tool
  returns its message as the tool result (fed back to the model), it does not
  abort the turn.
- Prefer the **arena** (`jc_mem.h`) for per-turn allocations; prefer **`jc_sb`**
  (`jc_str.h`) for unbounded growing buffers. Every `cJSON_Parse` is matched
  by a `cJSON_Delete`.

## Signals (TUI)

- Signal handlers may touch only a `volatile sig_atomic_t` flag. Never call
  `malloc`, `printf`, or `tcsetattr` from a handler — do that work back in the
  main loop after observing the flag.

## Tests

- Add a `void test_<name>(void)` to a `tests/test_<name>.c`, declare it in
  `tests/jc_test.h`, and call it from `tests/test_main.c`. `make test` builds
  and runs everything. Tests must not require network access.
- E2E drivers live in `tests/e2e/`, use `_e2e.py`, and must be listed by name in
  `tests/e2e/run.sh`. `sh tests/e2e/run.sh --lite` re-runs the whole suite with
  the resource-lean defaults; if a driver checks something `--lite` disables,
  call `_e2e.skip_if_lite("<feature>")` rather than weakening the assertion.
- Measurements (not gates) live in `tests/measure/`. They may be slow, may need
  a real model, and are never wired into `make ci` — the `tests/bench`
  precedent.

### Verify a new gate can fail

A test that has never been observed to fail is a guess. Before landing one,
break the thing it guards and confirm it goes red — then restore. Two M197/M198
gates were validated this way (reverting one line each produced 4 failures and a
`fifo: HUNG`), and it is the cheapest possible check on a test's wiring.

### Fixture proportionality (M198)

**For every quantity a harness measures, name what it scales with, then check
the fixture is within an order of magnitude of a realistic value.**

This is not pedantry. `tests/measure/soak.py` measured per-turn memory retention
against a **13-byte** fixture while hunting a defect proportional to *file size*.
It reported ~6 KB/turn and the conclusion was "no leak". The fixture was the only
thing wrong: raising it to 200 KB moved the measured slope to 218 KB/turn, a 34×
swing, and the bug had been live for four milestones. See
`docs/analysis/2026-07-29-session-arena.md`.

So:

- Ask what the measured effect is proportional to — input size, file count,
  history length, store size, iteration count.
- If the fixture is deliberately small for speed, **expose it as a flag with the
  small value as the default** (`soak.py --fixture-bytes`, `session_scan.py
  --files/--bytes`). A knob is auditable; a constant buried in a helper is not.
- Say in the harness's docstring what its numbers scale with, so the next person
  does not have to re-derive it.

Related: a leak checker cannot see an *arena-lifetime* bug — ASan, LSan and
`valgrind --leak-check` all report zero when memory is reachable until exit, as
jichi's arenas are. `make ci` green is not evidence of absence for that class;
use a footprint assertion (`test_session_footprint`), massif's peak, or
`jc_arena_used` via `/context`.

## Versioning & the changelog (M178)

- The version is defined **once**, in `include/jc_version.h` (`JC_VERSION`),
  and printed by `-V/--version` (both binaries), the top of `doctor`, and
  `describe`. Never define a version string anywhere else.
- Semantic Versioning, pre-1.0: **1.0.0 = the first public release**. Until
  then, MINOR = a completed capability cluster **or any breaking change**
  (config keys, CLI flags/subcommands, wire or JSONL contracts); PATCH =
  fixes.
- `CHANGELOG.md` is the **user-facing** record — written for someone who
  does not read our git history: plain descriptions of what changed and why
  it matters to them, with mermaid diagrams and code examples where they
  genuinely clarify (not decoratively). It is not a commit list;
  `docs/ROADMAP.md` remains the engineering log.
- Every user-visible change lands with an entry under `## [Unreleased]`
  **in the same commit**. Cutting a release = retitle `[Unreleased]` to
  `[X.Y.Z] — <date> — <name>` and bump `JC_VERSION`, together.

## The quality gate (`make ci`)

Before a commit, run **`make ci`** — it encodes the full bar: build + `make test`
under **both gcc and clang** with `-Werror`, an **ASan/UBSan** test run
(`SAN=1`), and **valgrind** (`--error-exitcode=1`). Each sub-build starts from
`clean`. Knobs are also usable directly: `make WERROR=1`, `make SAN=1 test`.
(`.github/workflows/ci.yml` runs the same target if a remote is added.) Building
under clang as well as gcc matters — clang's stricter checks have caught real
portability bugs the gcc-only build missed.

`make e2e` runs the offline (network-free) interactive/CLI checks under
`tests/e2e/` (line-editor wrap redraw, Tab completion, `doctor` on an unreachable
fixture, flag parsing) — Python-stdlib PTY drivers, no model required. Set
`JC_E2E_MODEL=<id>` (and optionally `JC_E2E_REAL_CONFIG=<path>`) to also run the
live-model headless-cleanliness check. `make ci` includes `make e2e`.

### Test tiers (M209)

| tier | command | language / deps | runs where | gates a change? |
|---|---|---|---|---|
| unit | `make test` | C89, nothing beyond libc | everywhere | yes |
| smoke | `make smoke` | POSIX sh + 4 test-only C89 helpers (`tests/tools/`: mockmodel/ptydrive/jsonq/sockq) | everywhere — **no python3** | yes (part of `make ci`) |
| e2e (residual) | `make e2e` | python3 (stdlib only) — **optional**, skips loudly if absent (M217) | dev + CI machines | yes (part of `make ci`) — only a permanently-Python residual: `redraw`'s VT emulator, the `stress`/`web_bridge` example products, `curriculum_graders` (needs cc), `rig_lint`, + model-gated live checks |
| bench / measure | `tests/bench/`, `tests/measure/` | python3, live model | dev machines | **never** — measurements |

Since the M209–M217 port, the smoke tier carries the bulk of what was the
Python e2e suite: the headless round trip, `--output json/jsonl`, stall/signal
exit codes, sessions, the media/embeddings/routing/posture surfaces, the
AF_UNIX daemon + control channel, the MCP + ACP stdio protocols, the PTY line
editor, and the `spawn_parallel` fork pool — 68 drivers, all Python-free. What
remains under `make e2e` is a small permanently-Python residual (see the table
above), so **`make check-target` (= `test` + `smoke`) is now a full build gate
on any POSIX box**, and `make e2e` is optional (it skips loudly without
python3). Smoke drivers live in `tests/smoke/`, source `_smoke.sh`, emit TAP,
must be listed in `tests/smoke/run.sh`, and are linted by
`tests/smoke/smoke_lint.sh` (strict POSIX sh: no `local`, no `[[`, no
python3/nc/curl, always `"$BIN"`). The port's design + decisions are in
`docs/plans/2026-07-python-free-testing.md`.
