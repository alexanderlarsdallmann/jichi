# Robotics: driving sensors, actuators, and fleets with jichi

jichi can be the **deliberative mind** of an embodied system — read sensors,
decide, actuate (gated), speak and listen, learn from its logs, and coordinate
peers. This is the operator manual; the design rationale and the honest limits
live in [proposals/2026-07-robotics.md](proposals/2026-07-robotics.md). Try it
first against the hardware-free simulator in
[`examples/robot-sim/`](../examples/robot-sim/).

> **The one rule that matters:** jichi is the slow, legible, auditable layer
> (model-call latency = **seconds**). Reflexes, interlocks, and the real
> emergency stop belong **below** it — in firmware, a microcontroller, or a ROS
> node. Never put a safety-critical loop in jichi.

## Devices are tools

A sensor or actuator is a **user-defined tool** (a script speaking JSON) or an
**MCP server**. jichi reaches hardware only through these — it links no device
library (the M42 "shell out, don't vendor" rule). The contract (see
[USER_TOOLS.md](USER_TOOLS.md)): the model's arguments arrive as JSON on stdin
and as `JICHI_ARG_<NAME>` env vars — **never on the command line** — and provider
API keys are scrubbed from the child.

```json
"tools": [
  { "name": "read_sensors", "readonly": true,
    "command": "/opt/robot/read-sensors.sh",
    "schema": { "type": "object", "properties": {} } },
  { "name": "drive_motor", "kinetic": true,
    "command": "/opt/robot/drive-motor.sh",
    "schema": { "type": "object", "properties": {
      "left": {"type":"integer"}, "right": {"type":"integer"},
      "duration": {"type":"integer"} },
      "required": ["left","right","duration"] } }
]
```

## The kinetic gate — anything that moves mass or energy

Mark a tool `kinetic: true` (or a whole MCP server). Then, mirroring the
privileged-command gate, **every kinetic action is governed by a posture that a
blanket auto-approve cannot override**:

| `kineticCommands` | behavior |
|---|---|
| `ask` (default) | interactive → a fresh KINETIC prompt (never satisfied by "always"); **unattended → refused** |
| `deny` | always refused |
| `allow` | run — use **only while supervised** |

- **`kineticCommandsAllow`** is checked *first*, before `deny` — put your
  safe-state tool (`stop_all`) here so it stays callable when everything else
  is refused (the E-stop guarantee). A chained shell command never matches the
  allowlist.
- **Shell bypass is covered:** `run_terminal_command "./motor.sh"` is
  shadow-matched against the kinetic tools' commands (and any
  `kineticShellPrefixes` you list, e.g. `["gpioset", "ros2 topic pub"]`), so a
  device reached through the shell is gated too.
- **Every attempt is audited** to `~/.jichi.d/audit/kinetic.jsonl`
  (owner-only, always-on unless `kineticAudit: false`); read it with
  **`jichi audit`** (see [OBSERVABILITY.md](OBSERVABILITY.md)), and
  `jichi runs` flags a run whose actuation was operator-steered.
- **`doctor`** lints the posture: `kineticCommands: allow`, audit off, a
  kinetic tool marked readonly, a kinetic tool in shell form (un-shadow-matchable),
  or a kinetic script that lives *inside the workspace* (model-editable). Run
  `jichi --config … doctor` (or `doctor --unattended` to make an unsafe
  posture a non-zero exit that gates a loop).

**Defense in depth (state plainly to operators):** the gate catches a
*drifting honest* model, not an adversary with shell access. Keep device
scripts **outside the workspace** by absolute path (the path fence then blocks
edits to them); add `permissions.deny: ["run_terminal_command"]` or
`--strict-scope` on a locked-down robot; and rely on **OS device-group
permissions + a hardware E-stop** as the real last line.

## Sound: speak and listen

Configure `sound` and jichi gains `play_audio` / `record_audio` (shelling out to
`aplay`/`arecord`/`ffplay` — see [SOUND.md](SOUND.md)). Composed with the
existing `generate_audio` / `transcribe_audio`, that is a full
listen→think→speak pipeline (seconds-scale, not conversational realtime).
Playback and recording are mutating (ASK / denied in PLAN) but **not kinetic**
by default; wrap a siren in a kinetic tool if its sound *is* the actuation.

## Adapters

- **Simulator** — [`examples/robot-sim/`](../examples/robot-sim/): flat-file
  world, all safety gates real. Start here.
- **Plain Linux (Raspberry-Pi class)** — device scripts wrapping the userland
  tools: GPIO via `gpioset`/`gpioget` (libgpiod) or the sysfs lines, serial via
  a `stty` + redirect to `/dev/ttyUSB0`, audio via `aplay`/`arecord`. List the
  raw binaries in `kineticShellPrefixes` so a shelled `gpioset` is gated. These
  adapter templates are **deferred until validated on real hardware** (see the
  proposal appendix and [DEFERRED_LOCAL_GPU.md](DEFERRED_LOCAL_GPU.md) for the
  precedent).
- **ROS 2** — jichi is a cognitive node: sensor tools wrap `ros2 topic echo`
  (or a small MCP-ROS bridge), actuator tools wrap `ros2 topic pub` / `ros2
  action send_goal`, all `kinetic: true`. Documented, not built — jichi never
  links the ROS client libraries.
- **Microcontroller co-processor** — the μC runs the fast reflex loop; jichi
  supervises it over serial. The right split when timing matters.

## Fleets and remote systems

No new protocol — reuse jichi's surfaces:

- Each robot runs jichi as an [autonomous loop](AUTONOMOUS_LOOPS.md) or the warm
  [daemon](DAEMON.md).
- A robot exposes its capabilities to peers as an **MCP server**; a coordinator
  jichi consumes them as MCP tools.
- A coordinator reaches peers over **SSH to their daemon sockets** and fans work
  out with **`spawn_parallel`** or a **`workflow`** spec.
- The M159 [control channel](CONTROL.md) lets you `pause` / `inject` / `abort` a
  peer's mission mid-run; the M157 hardening (dedicated user, systemd sandbox,
  egress allowlist) applies per robot.

## Learning and correcting on-system

The robot's own logs are the training signal: write sensor/actuation events as
JSONL (the "field notes" pattern in the sim pack), point a `docs` source at
them for recall, and run **`jichi learn analyze`** over the run journals
+ telemetry to draft propose-only lessons — review, then `learn apply` to commit
memory notes and skills that steer the next mission ([LEARNING.md](LEARNING.md)).
Correction stays human-gated, exactly as for code.

## Bringing hardware: the list, and the order

Before any motor turns, read **[`ROBOTICS_BRINGLIST.md`](ROBOTICS_BRINGLIST.md)**
(M306): what to bring (safety tier first, and it is non-negotiable), what may be
substituted, and the gated order — rehearse in `examples/robot-sim/`, then bench with
motor power *disconnected*, then powered with a hand on the stop, then motion, then
the MCU reflex layer verified **with jichi switched off**.

It also records the verification that this page's central claim holds in the source:
`kineticCommands` defaults to `ask`, unattended actuation is refused, and
`kineticCommandsAllow` is checked *before* `deny` so the E-stop survives a blanket
refusal.

## Deployment checklist

- [ ] Run as a **dedicated non-root user**; device access via group
      permissions, not sudo (`privilegedCommands: "deny"`).
- [ ] `kineticCommands: "ask"` (or `deny` + `kineticCommandsAllow: ["stop_all"]`
      for an unattended patrol); keep `kineticAudit` on.
- [ ] Device scripts at **absolute paths outside the workspace**;
      `pathFence: true`.
- [ ] Bound every mission: `--budget-tokens`, `--deadline`, and `--verify` where
      a check exists; `--control` to steer.
- [ ] A **hardware** E-stop / current limit / watchdog that no software gate
      replaces.
- [ ] `jichi --config … doctor --unattended` returns 0 before the loop
      starts.

See also: [proposals/2026-07-robotics.md](proposals/2026-07-robotics.md),
[SOUND.md](SOUND.md), [AUTONOMOUS_LOOPS.md](AUTONOMOUS_LOOPS.md),
[CONTROL.md](CONTROL.md), [HARDENING.md](HARDENING.md),
[USER_TOOLS.md](USER_TOOLS.md), [OBSERVABILITY.md](OBSERVABILITY.md).
