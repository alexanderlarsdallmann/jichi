# Structured test integration

jichi understands test output. Instead of handing the model a raw wall of
build/test logs, it parses the output into a **structured summary** — how many
tests passed/failed, and for each failure a name, a `file:line`, and a message —
and leads with that. This surfaces in three places:

- the **`run_tests`** agent tool,
- the **`test`** CLI subcommand,
- the autonomy envelope's **fix-forward** loop (see [AUTONOMY.md](AUTONOMY.md)).

## What it parses

Output is auto-detected and parsed in this order; the generic pass also runs as
a backstop so a `file:line` location is never lost:

| Format | Detected by | Notes |
| --- | --- | --- |
| **JUnit-XML** | `<testsuite>` / `<testcase>` | pytest `--junitxml`, ctest, and most runners can emit it. Reads `<failure>`/`<error>` (message attr or inner text), decodes XML entities. |
| **TAP** | a `1..N` plan or `ok`/`not ok` lines | Test Anything Protocol. `not ok` lines become failures. |
| **generic** | fallback | Heuristic line scan: `<file>:<line>[:col]:` locations plus failure markers (`FAILED`, `--- FAIL`, `not ok`, `error:`, `assert`, …). Covers pytest text, `go test`, ctest, `make`, and raw compiler errors. Pass/fail counts are read from a summary line (`N failed, M passed`, `out of K`). |

Output is bounded: at most `JC_TEST_MAX_FAILURES` (50) failures are retained
(the rest are counted as "… and K more"), and each message is length-capped.

The parser is a pure function (`src/util/jc_testparse.c`) — no I/O — and is
unit-tested offline (`tests/test_testparse.c`).

## `run_tests` tool

Advertised to the agent (mutating, since tests can build/write). Arguments:

- `command` *(optional)* — the shell command to run. Omit it to use the
  configured `testCommand`, falling back to `verify`.

It returns the parsed summary followed by a bounded raw tail and the exit
status, so the model gets the signal first and the detail on demand. Prefer it
over `run_terminal_command` for running tests.

## `test` CLI subcommand

No API key needed — handy for checking your setup and for scripting:

```sh
jichi test "make test"            # explicit command
jichi test                        # uses config testCommand, else verify
```

```
... raw test output ...
=== generic ===
Tests: 1 failed, 41 passed (of 42)
Failures:
- test_login @ tests/test_auth.py:88: AssertionError: expected 200
```

The process exits with the **test command's own exit code**, so it slots into
CI and `&&` chains.

## Configuration

```json
{
  "testCommand": "make test",
  "verify": "make && make test"
}
```

- **testCommand** — the default for `run_tests` and `test`. Keep it the test
  runner alone.
- **verify** — the autonomy gate (often build *and* test). `run_tests`/`test`
  fall back to it when `testCommand` is unset.

## How the envelope uses it

Under `--auto`, when the verifier fails, jichi parses its output and feeds
the model a focused list of failing tests with their locations (plus a short raw
tail), rather than a blind truncated tail — so fix-forward attempts target the
actual failures. The audit journal's `verify` entries gain `failed`/`passed`
counts. See [AUTONOMY.md](AUTONOMY.md).


---

## The project's own test tiers

Moved to **[TEST_TIERS.md](TEST_TIERS.md)** at M527. This page documents the
`run_tests` tool and the `test` subcommand — a *feature*; that one is contributor
reference about jichi's own tiers, and it was two thirds of this page while the
title said otherwise.
