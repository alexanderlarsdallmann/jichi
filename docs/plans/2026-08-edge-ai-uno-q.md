# Plan: edge AI on the Arduino UNO Q — a local small model, with the network as the strong tier

*Written 2026-08-04 on threadwork, for the 4 GB Arduino UNO Q. A plan to execute
later, on the device. Facts marked **[probed]** were verified before writing;
everything about the UNO Q's inference performance is a **prediction until the
board runs it** — the plan's job is to make each prediction cheap to confirm or
refute, keeping the M197–M205 discipline of separating measured fact from
assumption. Companion to `docs/plans/2026-07-hardware-testing.md`, which owns the
board's bring-up (and currently blocks this: see Prerequisites).*

## The arithmetic, stated once up front

The tempting framing is "put a model on the board and jichi becomes
self-contained". The arithmetic says something more specific, and the plan is
better for facing it first.

jichi's request has a **fixed prefix** — system prompt plus the serialized tool
array — before any conversation. On the Pi Zero 2 W this session, `jichi context`
reported **~2.4k tokens for the advertised tools alone** (16 built-ins) plus the
system prompt **[probed]**; `toolProfile: core` (M74) trims the tool set to 7–8
and is the setting this board will want. A quad Cortex-A53 with no usable
inference accelerator is an **in-order CPU**: prefill is the expensive half, and
prefill of a few thousand tokens at CPU speed is measured in **tens of seconds to
minutes**, not milliseconds.

Two consequences shape everything below.

1. **Prompt caching is not an optimization here, it is the enabling
   condition.** llama.cpp's server keeps a prefix cache; jichi's M31d guard
   asserts successive `build_request`s are byte-identical, so the fixed prefix is
   prefilled **once per session** and later turns pay only for their delta. A
   config that breaks prefix stability (auto-context injected into the system
   prompt, a clock in the prompt) turns every turn back into a full prefill.
   jichi already injects retrieved context on the *user* message for exactly this
   reason (M61) — that decision pays off on this board.
2. **The local model's job is not "replace the network model".** It is to serve
   the turns where locality wins: short prompts, no tools, no waiting on a WAN.
   The interesting engineering is the **routing** between local and network, and
   jichi already has it (`routing.fast`/`strong`, `escalateOnStall`, plus
   `fallback` for reachability). This plan is mostly a test of that machinery
   under a genuinely slow fast-tier.

## The three ambitions, as a ladder

Each rung has its own pass criteria; a rung that fails does not invalidate the
one below it. Do not skip rungs — rung A is the one most likely to be *useful*,
and it needs no tool calling at all.

### Rung A — the local model as a **role server** (lowest bar, likely best value)

jichi selects models per role (`jc_app_model_for_role`). Two roles run often,
need **no tool calling**, and benefit most from locality:

- **`summarize`** — auto-compaction (M30/M76) calls it between and within turns.
  Every compaction currently costs a network round trip and remote tokens;
  on-device it costs neither.
- **`autocomplete`** — the `complete`/`fim` one-shot paths (M9b/M9c ghost text),
  which are short-prompt and latency-sensitive, the shape a local model suits.

**Pass:** compaction and `fim` work end to end against the local model, with
`chat` still on the network. **This rung is worth having even if the local model
cannot tool-call at all**, which is the likely outcome for a sub-1B model.

### Rung B — the local model as the **fast tier** in routing (the real edge-AI test)

`routing.fast` = local, `routing.strong` = network, with
`escalateOnStall`/`escalateOnError`/`escalateOnVerify` on. Each top-level turn
starts local; a stall (`JC_ERR_TIMEOUT`), a tool error or a failed verify
escalates mid-turn to the network model, dropping the incomplete assistant turn
and re-running (M23).

**Pass:** a tool-light task completes locally; a task the local model fumbles
escalates and completes remotely, with the `[route]` log line and the `route`
telemetry event both present. **This rung is the plan's centrepiece**, because it
tests a design claim — fast-first tiered routing — against a fast tier that is
slow but free and private.

### Rung C — **fully offline** agentic operation (stretch)

No network at all: local model for every role, `codebase_search` disabled (no
embed model on-device unless one is served), snapshots and the envelope
unchanged.

**Pass:** a bounded `--auto` turn edits a file and its verifier passes, entirely
offline. **Expect this to be viable only for small, tool-light tasks**, and say
so in the write-up rather than implying general capability.

## Hardware and serving stack

**Verify on the device rather than trusting this table** — the SoC's inference
story is the one thing that could change the whole plan:

```sh
lscpu | head -15; nproc; free -m            # cores, ISA, RAM (expect 4 GB here)
cat /proc/cpuinfo | grep -m1 Features       # NEON / dotprod / i8mm?
ls /dev/dri /dev/kgsl* 2>/dev/null          # GPU nodes (Adreno-class?)
vcgencmd measure_temp 2>/dev/null || cat /sys/class/thermal/thermal_zone*/temp
```

Two CPU features matter disproportionately for quantized inference: **dotprod**
(`asimddp`) and **i8mm**. Their presence or absence is worth recording in the
row, because it explains a factor-of-two in decode speed and is not a jichi
variable.

**Serving: `llama.cpp`'s `llama-server`.** It builds on aarch64 with no
accelerator dependency, speaks the **OpenAI-compatible** `/v1/chat/completions`
jichi already targets, keeps a prefix cache, and supports tool calls via
`--jinja` with the model's chat template. Rejected alternatives, with reasons:
**LM Studio** has no arm64-Linux server build (it is threadwork's role in this
setup, not the board's); **ollama** works on arm64 but adds a model-management
layer this plan does not need and makes the quantization less explicit. GPU or
DSP offload (Adreno via OpenCL, Hexagon) is **explicitly out of scope for the
first pass** — get a CPU baseline first, then decide whether an accelerator is
worth the toolchain.

## Model candidates

Sizes are for Q4_K_M-class quantization and are approximate; **all performance
figures are to be measured, none are claimed here**. The 4 GB variant must hold
the model *and* the KV cache *and* `llama-server` *and* jichi (~15 MB
**[probed]**) simultaneously.

| candidate | params | file (Q4-class) | why it is on the list |
|---|---:|---:|---|
| Qwen 3 0.6B | 0.6B | ~0.4 GB | small, tool-call-trained lineage; best odds at native calls |
| Llama 3.2 1B Instruct | 1B | ~0.7 GB | widely tested; known tool-call template |
| Gemma 3 1B | 1B | ~0.8 GB | matches the family already served on threadwork |
| SmolLM2 360M | 0.36B | ~0.25 GB | floor case: is a *tiny* model useful for `summarize` only? |
| TinyLlama 1.1B | 1.1B | ~0.7 GB | baseline reference, no tool-call training |

Pick **two** for the first pass: one ~1B instruct model for rungs B/C, and
SmolLM2 360M as the rung-A floor case. Chasing five models before any is
measured is how a plan turns into a hobby.

## The configuration — the centrepiece

This is what the user asked for: **one config that serves the local model and the
network models together.** Every key below already exists; nothing here needs new
code.

```json
{
  "models": [
    {
      "name": "local/slm",
      "provider": "openai",
      "model": "qwen3-0.6b",
      "apiBase": "http://127.0.0.1:8080/v1",
      "apiKey": "none",
      "contextLength": 4096,
      "roles": ["chat", "summarize", "autocomplete"],
      "fallback": "threadwork/strong",
      "promptCache": true
    },
    {
      "name": "threadwork/strong",
      "provider": "openai",
      "model": "google/gemma-4-e4b",
      "apiBase": "http://10.42.0.1:1234/v1",
      "apiKey": "lm-studio",
      "contextLength": 32768,
      "roles": ["chat", "edit", "summarize", "embed"]
    }
  ],
  "routing": {
    "fast": "local/slm",
    "strong": "threadwork/strong",
    "escalateOnStall": true,
    "escalateOnError": true,
    "escalateOnVerify": true
  },
  "timeouts": { "connect": 10, "stall": 180, "request": 900 },
  "toolProfile": "core",
  "contextLimit": 3500,
  "lowResource": false,
  "maxParallelAgents": 1,
  "maxSubagentDepth": 0,
  "autoContext": false
}
```

Why each choice, because a config without reasons rots:

- **`fallback` on the local model** (M-era multi-server resolution): if
  `llama-server` is not running, resolution walks to the network model instead of
  failing the run. That is the "works whether or not the local server is up"
  property, and it costs one reachability probe.
- **`routing` fast/strong** gives the rung-B behaviour. **`escalateOnStall`
  matters most here**: on a slow board, "stall" is the normal failure mode, and
  `timeouts.stall` is the knob that decides how long to wait before spending
  network tokens. 180 s is a starting guess **to be replaced by a measurement**
  (decode speed × expected answer length).
- **`toolProfile: core`** trims ~16 tool definitions to 7–8. On this board that
  is not a context nicety, it is prefill seconds.
- **`contextLimit: 3500`** below the model's declared 4096: jichi's byte/4
  estimate runs optimistic, and `jc_calib` (M77) will self-tune the ratio from
  the server's real `prompt_tokens` within the first turn. Watch it converge in
  `/context`.
- **`lowResource: false` deliberately** — 4 GB is not the constrained tier, and
  auto-lite would switch off snapshots and the repo map. Set it explicitly so the
  M272 auto-detection does not decide for us. (On the 2 GB variant, revisit.)
- **`autoContext: false`** — retrieval injects on the user message (M61), so it
  would not break prefix stability, but it *does* add prefill tokens per turn,
  which is the scarce resource. Turn it on only after measuring.
- **`embed` stays on the network.** An embedding model on-device is a second
  server in the same 4 GB; not first-pass work.
- **`maxParallelAgents: 1`, `maxSubagentDepth: 0`** — one model server, one
  inference at a time. Fan-out on a single A53 cluster is negative value.

**Config coverage** can be scored offline before any of this runs: `jichi
confbench` (M113) grades a project's configuration against the best-practice
checklist, and `jichi doctor` lints it (including the caching-vs-pricing warning,
which for a free local model is expected noise).

## Use cases — what a local model beside a network model is actually *for*

Written 2026-08-04, after the board's Tier B run. **Every scenario below is a
claim to be tested, not a capability jichi advertises today** — no local model has
yet run on this board. Each one states what it needs, which existing jichi feature
serves it, the honest limitation, and how you would know it worked. Ordered by how
likely they are to survive contact with a quad-A53 CPU.

### 1. Compaction offload — the highest-value, lowest-risk case

**What:** the `summarize` role runs on-device while `chat` stays on the network.
Auto-compaction (M30/M76) calls the summarizer constantly on long sessions;
mid-turn compaction can fire several times in one turn.

**Serves it:** per-role model selection (`jc_app_model_for_role`), already there.
**Needs no tool calling**, which is the capability a sub-1B model is least likely
to have.

**Limitation:** summary *quality* bounds it. A bad summary silently degrades every
later turn, and that failure is invisible — it looks like the agent getting
vaguer, not like an error.

**Verify:** run a session long enough to compact, then read the injected summary
by hand. Compare against the same session compacted by the network model. If the
local summary loses the thread, this case fails no matter how fast it was.

### 2. Ghost text and FIM locally — latency where latency is felt

**What:** the `autocomplete` role serves Ctrl-G suggestions and the `fim`
subcommand from the board.

**Serves it:** the one-shot `complete`/`fim` paths (M9b/M9c). Short prompts, so
the prefill problem barely applies — this is the shape a small model fits best.

**Limitation:** documented in `docs/AUTOCOMPLETE.md` — whether you get a
*continuation* rather than an answer is a property of the model. A small
instruct-tuned model may be *worse* at this than a base/FIM-tuned one of the same
size, so pick for the task, not the parameter count.

**Verify:** the M280 few-shot prompt plus `jc_suggest_clean` should yield
continuations; if the ghost text is mostly conversational replies, the model is
wrong for the role.

### 3. Cost and quota control via tiered routing

**What:** `routing.fast` = local, `routing.strong` = network. Turns that the local
model finishes never touch the paid endpoint; those it fumbles escalate.

**Serves it:** `routing` with `escalateOnStall`/`escalateOnError`/
`escalateOnVerify`, and the `route` telemetry event that makes the split
*countable*.

**Limitation:** escalation is not free — a failed local attempt costs its own
wall-clock before the network model even starts, so a low local success rate makes
turns slower *and* no cheaper. The break-even is measurable and worth measuring.

**Verify:** `jichi telemetry` after a working session: how many turns completed on
the fast tier (cost 0) versus escalated. Below some ratio, this case is a loss.

### 4. Degraded-mode resilience — the network is optional, not required

**What:** when the network model is unreachable, jichi keeps working on the local
one instead of failing the run.

**Serves it:** the `fallback` selector plus `jc_net_reachable` probing —
resolution walks the chain to the first reachable model.

**Limitation:** "keeps working" means *reduced*, not equivalent. An honest
deployment says which tasks still work offline rather than implying parity.

**Verify:** unplug the Ethernet mid-session and continue. The `[fallback]` log line
should appear and the turn should complete locally.

### 5. Air-gapped and field work

**What:** the board in a workshop, lab or vehicle with no internet at all: read a
config, summarize a log, answer a question about a file, make a small edit.

**Serves it:** rung C — every role local, `codebase_search` off (no embed model),
snapshots and the autonomy envelope unchanged.

**Limitation:** expect tool-light tasks only, and say so. A 200-tool-call
refactoring turn on an A53 is not a thing; a "read these two files and tell me
what changed" turn plausibly is.

**Verify:** a bounded `--auto` turn that edits a file and passes its verifier,
with the network physically disconnected.

### 6. Edge triage — filter locally, escalate what matters

**What:** the board watches its own logs or sensor files, summarizes them locally
at intervals, and only involves the network model when something looks anomalous.

**Serves it:** user-defined tools (`tools[]`) for the sensor/log readers, the
`summarize` role locally, and routing to escalate. This is the classic edge-AI
shape — cheap filter at the edge, expensive judgement in the centre.

**Limitation:** the *decision* to escalate is itself a model judgement, and a
small model is exactly the wrong thing to trust with "is this anomalous?".
Prefer a mechanical trigger (threshold, regex, exit code) for escalation and use
the local model only to *describe* what it found.

**Verify:** feed a known-anomalous log; confirm escalation happened for the right
reason (the mechanical trigger fired), not because the small model felt uneasy.

### 7. Robotics: the deliberative layer keeps thinking when the WAN drops

**What:** the UNO Q is Tier B *and* Tier C in one enclosure (`docs/ROBOTICS.md`).
A local model means the seconds-scale deliberative layer survives a network
outage.

**Serves it:** the same routing/fallback machinery, plus the kinetic gate
(`kineticCommands`, `kineticCommandsAllow`, `jc_audit_kinetic`).

**Limitation, and it is the important one:** *do not* infer that a local model
should command actuators. The gate posture is unchanged — `kineticCommands` is
never `allow`, `stop_all` stays allowlisted, and the **MCU's watchdog is what
actually keeps the hardware safe**, independent of which model is talking. The
honest split is: local model for status description and logging, network model
for planning, reflexes in firmware where they always belonged.

**Verify:** the Tier C rungs unchanged — kill jichi mid-motion and confirm the MCU
halts on its own; confirm an unattended refuse blocks everything except
`stop_all`. Neither test should care which model was in use.

### 8. Teaching and lab use — many boards, one shared strong model

**What:** a classroom of boards, each self-sufficient for the cheap roles, sharing
one network model for the expensive ones.

**Serves it:** per-project config (`./local/config.json` beats `~/.jichi`), so each
board or student can differ without touching the others.

**Limitation:** one shared endpoint is a contention point; measure concurrent
turns before assuming it scales to a room.

**Verify:** two boards driving one endpoint simultaneously, with `telemetry`
showing both.

### 9. The operator is on another machine — and that is the normal case

**What:** jichi runs *on* the edge device; the human sits at a workstation and
works over SSH. This is not a variation on the scenarios above, it is the setting
for all of them — it is how every measurement in the Tier B row was taken.

**Serves it:** nothing special is needed, which is the point. jichi is a terminal
program; `ssh board 'jichi -p …'` and an interactive TUI over SSH both work, and
`--output jsonl` plus `--heartbeat` exist for when a supervisor script drives it
instead of a person.

**Limitation, and it is a human one rather than a technical one:** two machines
means two of everything — two configs, two binaries, two PATHs — and the failure
mode is confusion rather than error. Today's session lost real time to exactly
that: a five-day-old binary on the workstation's `PATH` made five milestones look
missing (`docs/INSTALL.md`, the shadowing trap). Anything teaching this topology
must teach *which host am I on, which binary am I running, which config is in
effect* as a first-class skill.

**Verify:** `command -v jichi` and `ls -l $(command -v jichi)` on each host before
believing anything about behaviour; `jichi doctor` on the device rather than on the
workstation, since it is the device's view that matters.

### 10. What is *missing* for a privacy story, stated as a gap rather than a feature

The obvious wish — "sensitive files never leave the device" — is **not** something
jichi implements. There is no per-path routing policy: the *session* chooses a
model, not the file. Today the honest version is "run this project against a
local-only config", which is a per-project choice (`./local/config.json`), not a
guarantee about content.

A real feature would be a **content-routing policy**: paths or globs that force
the local tier and refuse escalation. That composes with the existing path fence
and edit scope, and it would need its own design — including what happens when a
turn legitimately needs both a fenced file and the strong model. **Recorded here
as a gap so nobody documents it as though it exists.**

## Execution order — cheapest evidence first

| step | where | why here |
|---|---|---|
| 1 | **threadwork**: serve the *same* model + quant under llama.cpp, run `doctor --live` and `tests/bench` | separates *model capability* from *board speed*. A model that cannot tool-call on a fast x86 box will not learn to on an A53. |
| 2 | **device**: `llama-server` builds and runs; `--version`/`doctor` still ~15 MB | the board's own baseline, no jichi model calls yet |
| 3 | **device**: raw inference numbers (prefill tok/s, decode tok/s, RSS, temperature) | the numbers that decide whether rungs B/C are worth attempting |
| 4 | **device**: `doctor --live` → native/text/none, and `jichi context` for the real prefix size | jichi's own capability probe (M167c), on the real endpoint |
| 5 | **device**: rung A (summarize + fim local, chat remote) | first useful configuration |
| 6 | **device**: rung B (routing, then failover tests) | the centrepiece |
| 7 | **device**: rung C offline, and a soak for thermal decay | the stretch, plus sustained-load reality |

Step 1 is the gate: **if the chosen model reports `none` from `doctor --live` on
threadwork, rung B is off the table for that model** and the honest move is to
keep it at rung A rather than fight it.

## What to measure

Record per model, machine-stamped, in the house style:

| measurement | how | note |
|---|---|---|
| prefill / decode tok/s | `llama-server` logs or `llama-bench` | at thermal steady state, not from cold |
| the real fixed prefix | `jichi context` on-device | with `toolProfile: core`; this × prefill speed is your per-session startup cost |
| session-2 prefill | second turn in the same session | proves the prefix cache is working; if it does not drop, stop and find out why |
| tool calling | `jichi doctor --live` | `native` / `text` / `none` (M167c) |
| agentic capability | `tests/bench` (11 specs, 21 points) via `JC_BENCH_MODEL`/`JC_BENCH_API_BASE` | the same harness the local-GPU bench uses, so scores are comparable |
| footprint together | `free -m` with server + jichi + a turn in flight | the 4 GB question |
| escalation | `[route]` log + `route` telemetry event + `runs` reader | rung B's evidence |
| offline | unplug the network mid-session | `fallback` + rung C |
| thermal decay | a soak; sample `thermal_zone*/temp` and tok/s over ~30 min | fanless board, sustained load |
| tokens + cost | `jichi telemetry` | cost is 0 locally — the point of the exercise |

## What would be a genuine finding, and what would not

| observation | verdict |
|---|---|
| the model reports `none` for tool calling on **both** threadwork and the board | **not a jichi finding** — a model capability fact; record it and drop that model to rung A |
| `native` on threadwork but `none` on the board | **finding** — the endpoint or template differs (llama.cpp `--jinja`?), and the M167c advice deliberately names the REQUEST before the model (ANECDOTES #19/#20) |
| the second turn re-prefills the whole prefix | **finding** — prefix stability broken; check whether anything varying entered the system prompt |
| decode too slow to be useful | **not a finding, a measurement** — it decides which rung is the product's honest claim |
| jichi's estimate vs the server's `prompt_tokens` far apart after a few turns | **finding** — `jc_calib` should converge; if it does not, that is a real bug on a new model family |
| escalation never fires despite a stalled local call | **finding** — `timeouts.stall` / `escalateOnStall` wiring, testable offline too |
| OOM with server + jichi + a turn | **finding for the docs** — the 4 GB budget needs a smaller model or a smaller KV cache, and LOW_MEMORY.md should say so |

## Non-goals, stated so they are not quietly attempted

- **No new jichi code is planned.** Every configuration surface this needs
  already exists; if the plan turns out to need code, that is itself a finding
  worth its own milestone rather than a quiet extension of this one.
- **No model ships in the repo.** jichi vendors no third-party source and will
  not start with GGUF weights; the plan records model names, quantizations and
  where they came from.
- **The MCU is untouched.** Edge *AI* here means the Linux side. The on-board
  microcontroller remains Tier C's reflex layer, and the robotics rules are
  unchanged: `kineticCommands` never `allow`, `stop_all` allowlisted, and the
  withhold-the-heartbeat test before any actuator moves.
- **No accelerator work in the first pass** (Adreno/OpenCL, Hexagon). CPU
  baseline first.

## Deliverables

1. A machine-stamped row per model in `docs/LOW_MEMORY.md` (footprint under a
   local server) and a short `docs/analysis/` write-up in the honest house style:
   predicted → observed → finding vs documentation fix.
2. **A worked, committed example config** for the local-plus-network arrangement
   (the JSON above, corrected by what the device teaches) under `examples/`, so
   the next person starts from a configuration that ran.
3. A statement of **which rung is the honest claim** for this board — the
   equivalent of the hardware plan's "the Pico is jichi's reflex layer, not its
   host": a decision recorded as a decision, not a disappointment.
4. `tests/bench` scores for the local model beside the threadwork model, so
   "how much capability does locality cost" has a number rather than an
   impression.

## Prerequisites

- **UNOBLOCKED 2026-08-04 (M282).** The board is reachable over ssh (hostname
  **sybila**) and **Tier B checklist steps 1–7 all pass** — so this
  plan's stated prerequisite is met and it can start whenever there is time.
  jichi builds clean, the full gate is green at multiplier 19, `doctor --live`
  reports **native tool calling**, and a real budget-bounded turn works against a
  threadwork-hosted model.
- **What the bring-up measured that changes this plan's arithmetic:**
  - **It is the 4 GB variant: 3669 MB total, ~2.3 GB available.** Auto-lite does
    NOT engage and the tool profile stays `full`, so the ~2.4k-token fixed prefix
    discussed above is the *full* toolset unless `toolProfile: core` is set
    explicitly. Setting it is still the right call here, but it is now a choice
    rather than something the RAM tier does for you.
  - **Docker is running on this image** (`docker0` bridge up), competing for the
    same 4 GB as a model server plus jichi. Budget for it, or stop it during a
    measurement run and say which.
  - **`/` is a 9.8 G partition with ~4.5 G free.** That is the real ceiling on
    model choice: a Q4 1B model (~0.7 GB) fits comfortably, several do not, and
    llama.cpp plus its build tree needs room too. Check free space before
    downloading, not after.
  - **No `/usr/bin/time`** on the image — use jichi's own `/context` line and
    `/proc/<pid>/VmHWM` polling for footprint, as the Tier B row did.
