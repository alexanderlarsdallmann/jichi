# Embedding jichi as a component

> **Also read:** [`SCRIPTING.md`](SCRIPTING.md) (the headless/jsonl mechanics),
> [`DAEMON.md`](DAEMON.md), [`ACP.md`](ACP.md), [`CONTROL.md`](CONTROL.md),
> [`AUTONOMOUS_LOOPS.md`](AUTONOMOUS_LOOPS.md), and
> [`WEB_FRONTEND.md`](WEB_FRONTEND.md). This page is the layer above them: *who*
> embeds jichi, and *what is promised* to them.

## The finding, up front

**jichi is already a component.** Writing this page was supposed to be the design
of an embedding interface; measuring what exists first showed there was very little
to design. `jichi describe --output json` already emits a machine-readable contract
covering output formats, exit codes, the jsonl event schema, stop reasons, the
daemon protocol, key flags, subcommands, and every tool with its `readonly` flag.
The ACP server, the control socket, `--output jsonl`, `--heartbeat`, and the JSON
projections of `ls`/`export`/`status`/`doctor` are all in place.

What was missing was not capability. It was **a statement of which parts are a
promise.** A consumer had to infer stability from the ROADMAP, which is a design
history, not a contract. So this page is the contract, and §4 is the substance of
it.

Two defects surfaced from simply comparing the contract's own two renderings
against each other (M301):

- The text form listed exit code **143** and the JSON omitted it — and 143 exists
  precisely so a supervisor can tell a graceful `SIGTERM` from a crash (M146). The
  machine contract was missing the entry that exists for machines.
- The `heartbeat` event's `fields` array contained `"(only"`, `"with"`,
  `"--heartbeat"` — prose passed where a field list was expected and split on
  spaces. Any consumer generating types from the contract would have generated
  nonsense.

Both are fixed, and `tests/smoke/describe.sh` now checks that the two renderings
agree and that a field array holds identifiers only.

---

## 1. Which surface for which job

| You are building | Use | Why |
|---|---|---|
| CI reviewer / gate | `-p … --output jsonl --auto` + exit code | One turn, machine-readable progress, a definite verdict |
| IDE / editor sidecar | **ACP** (`jichi serve`) | Bidirectional: permission round-trips, unsaved buffers, terminals |
| Batch migration over a work-list | `-p … --auto --edit-scope` per item, `--journal` | Bounded per item; the journal is the audit trail |
| Long unattended loop | a supervisor + `--control` | Steer without killing: `status`/`inject`/`pause`/`abort` |
| Warm interactive front-end | **daemon** (`--connect`) | Skips per-turn startup; the M165 bridge does this |
| Teaching / grading harness | `jichi assign` + `grade` + `--output json` | Structural grading with no toolchain but jichi |
| Edge / embedded deliberative layer | `-p` on-device, model over the network | jichi is the seconds-scale layer; reflexes live below it ([ROBOTICS](ROBOTICS.md)) |

The rule of thumb: **if you need to answer jichi mid-turn, use ACP or the control
socket. If you only need to watch, use `--output jsonl`.** Everything else is a
convenience on top of those two facts.

## 2. The three integration shapes

**One-shot.** `jichi -p "task" --output jsonl` — a process per turn. Simplest,
most isolated, and the right default. Costs process startup and a cold prompt
cache per turn.

**Warm.** `jichi daemon` + `--connect <socket>` — newline-framed JSON, one request
per connection. Keeps the process (and its caches) alive. Use it when turn latency
matters more than isolation.

**Server.** `jichi serve` — ACP over stdin/stdout. The only shape where *jichi asks
you* things: tool permission, file reads through your editor's buffers, terminal
execution on your side.

## 3. What a consumer must handle

Not optional, in rough order of how often each is skipped:

1. **Exit codes**, all five: `0` ok, `1` runtime error, `2` usage/config,
   `130` SIGINT, `143` graceful SIGTERM. Treating a non-zero as a crash will
   misread both `130` and `143`.
2. **`stop_reason` on the terminal event** — `done`, `interrupted`, `timeout`,
   `budget`, `verify_failed`, `error`. A budget stop is *not* a failure: M80 keeps
   the work, and a supervisor that discards it is throwing away good output.
3. **Unknown event types.** The jsonl stream gains events (`heartbeat` arrived at
   M165). Ignore what you do not recognise; do not treat it as a protocol error.
4. **Liveness.** A long model call produces no output. Use `--heartbeat <secs>` to
   tell "wedged" from "thinking", or you will kill healthy runs.
5. **Bounded runs.** Under `--auto`, always set at least one of
   `--budget-tokens` / `--deadline` / `--max-tool-calls`, and an `--edit-scope`.
   An unbounded autonomous agent with a shell is not a component, it is a hazard.
6. **stdout is the answer; stderr is diagnostics.** Never parse stderr. `-q`
   silences it.

## 4. The stability contract

This is the part that did not exist before M301. Versions are semver-ish, pre-1.0
(see [`CHANGELOG.md`](../CHANGELOG.md)); `jichi --version` and the `version` field
of `describe --output json` tell you where you stand.

> **If you read only one line of this section:** pin a jichi version, and **diff
> `jichi describe --output json` between versions in CI**. That is the mechanical
> way to be told about a change instead of discovering one. "semver-ish" is not a
> promise you should lean on by itself.

> **A protocol proposal was checked against this list, not around it.**
> [`proposals/2026-08-jichi-protocol.md`](proposals/2026-08-jichi-protocol.md)
> (M525) §9 states what a clean-sheet coordination protocol would do to every
> surface below. The answer is "nothing breaks", and the reason is written down —
> including the half that was luck rather than design.

### Stable — changes are additive, or they are breaking changes with a version bump

- **Exit codes.** Existing codes keep their meaning. New codes may be added.
- **`--output jsonl` event objects.** Every object carries `v` (schema version) and
  `type`. **Existing fields keep their name and meaning**; new fields and new event
  types may appear. Consumers must ignore unknown types and fields.
- **`stop_reason` values.** Existing values keep their meaning; new ones may appear.
- **`--output json` terminal object** — same rules as jsonl's `done`.
- **The daemon request/response shapes** (`prompt`, `ping`, `shutdown`).
- **The core headless flags** in `describe`'s `key_flags`: `-p/--print`,
  `--output`, `--auto`, `-q`, `--config`, `--session`/`-c`, `--no-session`,
  `--connect`, `--heartbeat`.
- **`describe --output json` itself**, including its own `v`. It is the
  introspection point; if it moved, nothing else could be discovered.
- **The JSON projections** of `ls`, `export`, `status`, `doctor`.
- **ACP** — as far as the protocol is specified externally; jichi follows it, and
  does not define it. jichi's side is described in [`ACP.md`](ACP.md); the
  protocol itself is not ours to promise.

### Provisional — expect change, pin your jichi version

- **`describe`'s prose** — every `summary` string. Read them; do not match on them.
- **The tool set and tool descriptions.** Tools are added, renamed (with aliases,
  M219), and re-scoped by the `toolProfile`. `readonly` flags are stable *per tool*.
- **The control-socket verbs** beyond the current six (`status`, `inject`, `pause`,
  `resume`, `abort`, and `mode` — added by M304; this said "five" until M431), and
  the journal's event fields.
- **Telemetry JSONL** (`~/.jichi.d/telemetry/`). It is an *observability* sink for
  offline analysis, deliberately freer to change; the readers (`telemetry`, `runs`,
  `audit`) are the supported way to consume it.
- **The daemon's `assignment.*` verbs** (M529) — same reasoning as `hello` below:
  new, and the verdict object is expected to grow fields (a `checks` array is
  specified but not emitted). The *error codes* are the part to rely on least
  loosely: `assignment.not_gradeable` will keep meaning "this is not a grade".
- **The daemon's `hello` / `hello.ok` exchange** (M528). Deliberately Provisional
  rather than Stable: it is one milestone old, and the protocol proposal it comes
  from ([`proposals/2026-08-jichi-protocol.md`](proposals/2026-08-jichi-protocol.md))
  expects a `groups` list and a `limits` object to grow. Two things about it will
  not change: it is **optional** (the Stable `prompt`/`ping`/`shutdown` shapes work
  with no handshake, forever), and `auth.peercred` will not say `true` until a peer
  check actually exists.
- **Anything under `.jichi/`** — assets, board, memory format.

### Not an interface at all

- **stderr text**, log lines, TUI rendering, colours, spinners.
- **The session store's on-disk JSON.** Use `export --output json` instead; it is
  the supported projection and exists for this reason.
- **Internal C symbols.** jichi is a program, not a library. There is no `libjichi`
  and no stable ABI — embedding means *driving the process*, not linking it.

### How a break is announced

A breaking change to anything in *Stable* gets a `CHANGELOG.md` entry under
**Changed** or **Removed**, a MINOR bump pre-1.0, and — where a consumer could
detect it — a bump of the relevant `v`. `describe --output json` is the thing to
diff between versions in CI if you want to be told mechanically.

## 5. The honest limits

- **No library.** C89, one dependency, and a process boundary. If you want
  in-process embedding, you want a different tool.
- **No sandbox.** jichi's gates (path fence, edit scope, approval modes,
  privileged/kinetic gates) constrain the *agent*, not the *process*. A component
  that runs untrusted tasks should add OS-level isolation of its own; see
  [`HARDENING.md`](HARDENING.md).
- **No concurrency inside one workspace.** Two jichi runs writing one tree will
  fight. Serialise per workspace (the M165 bridge uses a per-workspace mutex).
- **The model is not deterministic.** Neither is a run's tool sequence. Assert on
  outcomes, never on the path taken — the same rule the bench learned (M167f).
- **Prompt injection is mitigated, not solved.** External content is fenced as data
  (M300), which helps and does not substitute for your own boundaries.
