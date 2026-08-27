# Tiered model routing

A single chat model usually serves every turn. **Routing** lets you nominate a
cheap/fast model and a strong model and run **fast-first, escalating to strong
when a turn proves hard** — getting the most out of the connected models without
paying for the strong one on routine work. It's configurable from the config
file, the CLI, and the TUI, and is off until you name the two tiers.

```jsonc
// config: ~/.jichi
{
  "models": [ /* ... two or more, see Configuration ... */ ],
  "routing": {
    "fast":   "qwen3-coder",   // selector: name / 1-based index / role
    "strong": "opus",
    "escalateOnVerify": true,   // default
    "escalateOnError":  false,  // default
    "escalateOnStall":  true,   // default: escalate when the fast model stalls
    "escalateOnContext": 75     // default: % of the fast window that escalates
  }
}
```

## How it works (the design, and why)

**Fast-first, escalate on difficulty.** Each top-level turn starts on `fast`.
The run switches to `strong` when a *hard signal* fires; the escalation is
**sticky for the rest of that turn** and **resets to fast at the next turn**
(cheap follow-ups stay cheap; the signal simply re-fires if the work is still
hard). This needs no extra "classifier" model call and adds zero latency to easy
turns — the decision is made from signals the agent already produces.

**Escalation signals** (each a knob, with a chosen default):

| Signal | Config key | Default | Why |
| --- | --- | --- | --- |
| Verifier failed, agent is about to retry | `escalateOnVerify` | **on** | The fast tier couldn't produce a passing result — the clearest "this is hard" signal. Pairs with the [autonomy envelope](AUTONOMY.md)'s verify gate. |
| A tool **malfunctioned** | `escalateOnError` | **off** | Tool errors are often benign (a missing file, a non-zero grep); escalating on every one is noisy. Opt in when you want it. Since M286 this means the *tool* broke — not a command it ran correctly reporting failure; see below. |
| The model **stalled** (timed out mid-stream) | `escalateOnStall` | **on** | An unambiguous "this model can't serve this turn" — common with small/local models (see [model-call timeouts](MODELS.md), M22). The fast tier's incomplete turn is dropped and re-run on `strong`. |
| The history is **outgrowing the fast tier's window** | `escalateOnContext` | **75** (%) | The only trigger that is not a failure. Running out of room is the reason a wide-window strong tier gets configured at all, and it is the signal that was missing; see below. |

### `escalateOnError` means the tool broke, not the build (M286)

A red build or a failing test is not a tool error — it is the gate doing its job,
and inside a fix-forward loop the agent runs one on purpose. Until M286 this
signal read `is_error` alone, which conflates the two, so the flag escalated on
essentially every turn that touched a compiler. On one measured project **300 of
447 tool errors across 151 turns were build/test failures**, and the honest
response was to set `escalateOnError: false` in the config with a comment
explaining why — which in turn meant the `strong` tier was never reached at all
(`routes=0` across 174 turns, with `escalateOnVerify` and `escalateOnStall` both
on but `verify_failed=0` and `timeouts=0`).

The distinction already existed: M168 put the command's own `exit_status` on
`jc_tool_result` so telemetry could separate a red gate from a broken tool (see
[TELEMETRY.md](TELEMETRY.md#reading-a-tools-ok-rate-m168)), but only telemetry was
taught to use it. Routing now asks the same question through the pure
`jc_tool_result_is_malfunction`: `is_error`, no command reported its own exit
status, **and** it is not a policy refusal. A command not found, a timeout, bad
arguments or an unknown tool still escalate; `zig build test` returning 1 does not.

**Policy refusals do not escalate either (M291).** A path-fence denial is not a
failure of capability — the stronger model meets the identical fence, so
escalating is pure cost. This was found the first time the fence was switched on
for a real project: the very first `route` event that project had ever produced
was `reason: tool_error` on a denied read. It is the mirror image of the defect
M286 fixed one milestone earlier, and it is why the result carries a
`policy_refusal` flag rather than routing string-matching the message.

If you turned `escalateOnError` off because it fired constantly, this is the fix —
try it on again.

### `escalateOnContext`: room, not difficulty (M288)

The three triggers above all react to something going *wrong*. This one reacts to
running out of *room* — and it is the trigger whose absence left the whole feature
untested in practice: one measured project logged **`routes=0` across 174 turns**,
because the verifier never failed, nothing ever stalled, and `escalateOnError` had
to be off for red builds. A carefully configured two-tier setup had never once
executed.

It is checked **before each request is built**, so the roomier model serves the
very call that needed the room, and the default threshold (**75%** of the fast
tier's window) sits deliberately **below the compaction trigger's 80%**. That
ordering is the point: when a turn starts running out of context, jichi would
rather move to a model with more of it than start summarizing history away.

Two guards keep it from firing pointlessly:

- **The strong tier must be strictly roomier.** Escalating for room you do not
  gain is pure cost, so `jc_config_context_escalate` returns 0 when the two
  effective windows are equal.
- **It compares *effective* windows** — i.e. after a global `contextLimit`
  override. A top-level `contextLimit` overrides *every* model, so a config
  declaring a 150k `fast` and a 256k `strong` but pinning
  `"contextLimit": 128000` has told jichi that both tiers are equally roomy, and
  the trigger correctly stays inert. jichi will not quietly reinterpret an
  explicit budget; `doctor` reports the contradiction instead, naming the fix.
  (That is a real config, and the `contextLimit` was set on guidance that M286
  obsoleted — see [COMPACTION.md](COMPACTION.md).)

Set it as a percentage (`"escalateOnContext": 60`), or as a bool for convenience:
`false` disables, `true` picks the default. The estimate it compares is the
calibrated one (M286), the same quantity the compaction trigger evaluates, so 75%
really does come before 80%.

**Enabled when usable.** Routing acts only when it is `enabled` *and* both `fast`
and `strong` resolve to **distinct** configured models. `enabled` defaults to
true, so simply naming the two tiers turns it on; naming neither leaves it inert
and behavior is identical to before. If a selector doesn't resolve, or both
resolve to the same model, routing stays inert (a single model, no switching).

We deliberately do **not** auto-derive the tiers from each model's cost fields
(`inputCostPer1M`/`outputCostPer1M`): those are frequently 0/unknown, so guessing
would be surprising. Tiers are explicit. (Cost-based auto-tiering is a possible
future addition.)

**Manual override.** An explicit **`--model` on the command line pins the run**:
tiered routing is disabled for it and a one-line `[route] --model pins …` note
says so (M411). Before that, the per-turn re-route to fast silently overrode the
flag — `status --model X` reported X while the first request went to the fast
tier, which is how work addressed to a 31B model was measurably done by the coder
tier. Passing `--route-fast`/`--route-strong` *together with* `--model` keeps
routing (naming tiers is asking for it); `--model` then only picks the starting
model.

In the **TUI**, `/model` remains a live choice *within* routing: the next
turn-start re-routes to fast as documented, the `[route]` banner is visible, and
`/route off` pins — an interactive user can see and undo what a `-q` headless run
could not.

**Top-level only.** Subagents and `spawn_parallel` children are never re-routed —
they keep whatever `model` they were spawned with. Escalation is gated on the
main agent (`agent_depth == 0`), exactly like the envelope.

**Observability.** Every switch logs `[route] -> <model> (<reason>)` to stderr
(unless `-q`) and, when an [envelope](AUTONOMY.md) journal is active, emits a
`route` event `{ "to": "<model>", "reason": "turn-start"|"verify_fail"|
"tool_error"|"stall"|"context" }`. The journal is the reliable record. A stall also surfaces
in the TUI via the status line (`model stalled …` then `escalating to <strong> …`).

## Configuring it

Three surfaces, same effect; **CLI overrides config**, and the TUI changes the
live session:

- **Config file** — the `"routing"` object above. Keys: `enabled` (bool),
  `fast` / `strong` (selector strings), `escalateOnVerify` / `escalateOnError` /
  `escalateOnStall` (bools), `escalateOnContext` (percentage, or a bool).
- **CLI** —
  - `--route-fast <model>` and `--route-strong <model>` set the tiers (and enable
    routing).
  - `--no-route` disables routing for the run (pin the single active model).
  - `--route-on-stall` / `--no-route-on-stall` toggle stall escalation.
  - `--route-on-context <pct>` / `--no-route-on-context` set or disable the
    context-pressure threshold (M288).
- **TUI** — `/route`:
  - `/route` — show enabled state, the selectors and their resolved names, the
    escalation flags (`verify`/`error`/`stall`/`context`), and the current tier.
  - `/route on` / `/route off`.
  - `/route stall on` / `/route stall off` — toggle stall escalation.
  - `/route context <1-99>` / `/route context off` — set the context-pressure
    threshold as a percentage of the fast tier's window.
  - `/route fast <model>` / `/route strong <model>` — set a tier (validated).

A **selector** is a model name substring, a 1-based index into the `models`
array, or a role name (`chat`, `edit`, `summarize`, …) — the same resolution
`spawn_subagent`'s `model` argument uses.

## Defaults summary

- Routing **enabled by default**, but **inert until** `fast` and `strong` are set
  to two distinct models — so out of the box nothing changes.
- `escalateOnVerify` **on**, `escalateOnError` **off**, `escalateOnStall` **on**,
  `escalateOnContext` **75** (percent) — the last one additionally inert unless the
  strong tier's window is genuinely wider.
- Start tier is always `fast`; escalation is sticky within a turn and resets each
  turn.

## Scope / what this is not (yet)

This iteration is model routing only. Two related "compute" ideas are
intentionally deferred:

- **Local-model auto-detection** (probe a running Ollama / llama.cpp / vLLM and
  register its models) — today you point a model's `apiBase` at the local server
  manually.
- **Parallel embeddings** (fork-pool the index's embedding batches to saturate
  the CPU / a local embedding server) — today index builds embed batches
  sequentially.

## Implementation

- Config: `struct jc_routing_cfg` + the `jc_config_routing_resolve` predicate
  (`include/jc_config.h`, `src/config/jc_config.c`) — the single
  enabled-and-distinct check the loop and TUI consult; unit-tested.
- Switch: `jc_app_route_to` (`src/chat/jc_app.c`) wraps `jc_app_switch_model`,
  logs, and journals; integration-tested.
- Loop (`src/chat/jc_agent.c`): `jc_agent_run_turn` routes to fast at turn start;
  `run_agent_loop` streams against a local, re-pointable provider so it can
  escalate mid-turn (`route_escalate`) on the verify-fail, tool-error, and
  **stall** (`JC_ERR_TIMEOUT`) paths without mutating the const `opts`. The stall
  path drops the incomplete assistant turn (`jc_history_truncate`) before re-running
  on `strong`; e2e-tested (`tests/e2e/route_stall.py`).
- Surfaces: `--route-fast`/`--route-strong`/`--no-route` (`src/main.c`), the
  `routing` config block, and `/route` (`src/tui/jc_tui.c`).
