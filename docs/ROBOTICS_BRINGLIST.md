# Robotics bring-list, and the order to do things in

> Companion to [`ROBOTICS.md`](ROBOTICS.md) (the architecture) and
> [`plans/2026-07-hardware-testing.md`](plans/2026-07-hardware-testing.md) (Tier B/C).
> This page is the **shopping and sequencing** document: what to bring, what each
> item is for, what may be substituted, and what must not be.

## Read this first: what jichi is, and is not

jichi is the **seconds-scale deliberative layer**. Every actuation costs a model
round-trip, so the fastest possible decision loop is *seconds*. That is not a
limitation to engineer around — it is the reason the rest of this list exists.

**Reflexes and the emergency stop live below jichi, in hardware and in a
microcontroller.** Anything that must react in milliseconds — a limit switch, a
current trip, a bumper, an estop — must work with jichi switched off, crashed, or
mid-sentence. If a safety property depends on jichi noticing something, it is not a
safety property.

This was **re-verified against the source** before this page was written, because it
is the claim that makes everything below safe (M306):

| Claim in `ROBOTICS.md` | Verified |
|---|---|
| `kineticCommands` defaults to `ask` | `jc_config.c`: `JC_PRIVPOL_ASK` |
| unattended actuation is **refused** | `jc_agent.c`: `unattended_refused` |
| `kineticCommandsAllow` is checked **before** `deny` — the E-stop survives a blanket refusal | `jc_agent.c`: the allowlist branch precedes the deny branch |
| a shell bypass (`./motor.sh`) is shadow-matched | covered |
| every attempt is audited, owner-only | `jc_audit_kinetic`, mode 0600 |

All eight properties also pass end-to-end in `tests/smoke/kinetic.sh`, including
*"the allowlisted motor ran unattended (the E-stop survives)"* and *"the chained
command was not allowlisted"*.

**Defence in depth, stated plainly:** the gate catches a *drifting honest* model, not
an adversary with shell access. The real last line is OS device permissions and a
hardware E-stop.

---

## The bring-list

### 1. Safety tier — **non-negotiable**

Buy or borrow these first. If any is missing, do not power a motor.

| Item | What it is for | Substitutions |
|---|---|---|
| **A real E-stop in the power path** | A mushroom-head latching switch that physically breaks motor power. Not a GPIO pin, not a software call. | A latching switch or a pulled connector will do. **Not** substitutable by software. |
| **Bench PSU with an adjustable current limit** | Set the limit just above the motor's stall-free draw. A wiring mistake then trips the supply instead of melting something. | A fused battery pack + inline fuse, sized deliberately. Not a phone charger. |
| **Something soft to hit** | Foam board, a cushion, a cardboard wall. The first motion command *will* be wrong. | Anything soft and bigger than the robot's reach. |
| **A clamp or a jig** | So the platform cannot drive off the bench. | Weights, a vice, a box that boxes it in. |
| **Safety glasses** | An arm or a coupling can throw a fastener. | None. |

**Why the E-stop cannot be software:** jichi's `stop_all` on
`kineticCommandsAllow` is a *good-citizen* stop — it survives a `deny` posture,
which is genuinely useful. But it needs jichi alive, a model reachable, and a turn in
flight. The physical stop needs none of those. Keep both; trust only one.

### 2. Deliberative host — **already proven, bring what you have**

| Item | Notes |
|---|---|
| **Raspberry Pi Zero 2 W** or **Arduino UNO Q** | Both are green on the full `make check-target` (M272/M276/M282). The UNO Q is the more comfortable rung (3669 MB, multiplier 19) and carries an on-board MCU — see tier 3. |
| PSU for the host, **separate from motor power** | Shared rails brown out the host when a motor starts, and a host that reboots mid-turn looks like a jichi bug. |
| microSD / eMMC already flashed and `check-target`-green | Do this at a desk, not next to a motor. |
| Network to the model | The board is a **thin client**: the model stays on threadwork or HRZ. Ethernet via a powered hub is the least surprising option (M282). |

**Do not host the model on the board.** 0.5–4 GB is not a model server, and the
attempt turns a robotics session into a debugging session.

### 3. Actuation tier — **start smaller than you want to**

| Item | Notes | Defer? |
|---|---|---|
| **One** small geared DC motor or hobby servo | One axis, one command, one log line. Two motors double the failure modes before you have read the first log. | No — but one is enough for Tier B motion. |
| Motor driver appropriate to it (e.g. an H-bridge board) | Must have its own power input, separate from the host. | No. |
| A wheeled base or a single-joint arm | Only after the bare motor has moved on the bench. | **Yes, defer.** |
| A second axis | After the first is boring. | **Yes, defer.** |

### 4. Sensing minimum — **enough to know a command did what it said**

This is the tier people skip, and skipping it is why a robotics session becomes
guesswork. The requirement is not "perception"; it is **feedback that a command took
effect**.

| Item | What it answers |
|---|---|
| **An encoder, a limit switch, or a hall sensor** | "Did it move, and how far?" Without this, jichi's log says *commanded* and nothing says *happened*. |
| A voltage/current readout (PSU display is fine) | "Is it stalling?" |
| **A phone on a tripod, recording** | The cheapest and most honest instrument on this list. Every surprise is reviewable afterwards, and the recording is the pre-mortem's evidence ([`ANECDOTES.md`](ANECDOTES.md)). |

### 5. Wiring and workshop

Dupont jumpers and a small screwdriver set; a multimeter (continuity and volts is
enough); heat-shrink or tape; a notebook. Label the motor leads *before* connecting
them.

---

## The order of the run

Each step is a gate. Do not start the next one because the last one nearly worked.

### Step 0 — Rehearse with no hardware at all

```sh
cd examples/robot-sim && make reset
jichi --config config.json doctor          # read the kinetic lints
jichi --config config.json --auto -p "patrol the corridor"
```

The simulator's devices are the *real* gate with fake hardware. Rehearse the whole
sequence here — including reading `jichi audit` afterwards — before anything is
plugged in.

> **One finding from doing exactly this (M306):** run from inside the sim directory,
> `doctor` reports *"a kinetic tool's command lives inside the workspace
> (model-editable script)"*. That is the lint working, and it is the single most
> important thing the rehearsal teaches: **on a real robot the device scripts must
> live outside the workspace, referenced by absolute path**, so the path fence blocks
> the model from editing the very scripts that move the motor. The sim ships them
> in-tree because it is a sim; your robot must not.

### Step 1 — Bench, power to the host only

Motor power **disconnected**. Run the mission. Read `jichi audit` and the actuation
log. You are checking that jichi *decides* correctly — nothing should move because
nothing can.

Gate: the command log matches what you would have wanted to happen.

### Step 2 — Bench, motors powered, E-stop in hand

Platform clamped, soft thing in front of it, current limit set, one hand on the stop.
`kineticCommands: ask`, and **you** answer each prompt.

Gate: every prompt was one you understood before you answered it, and the sensor
agreed with the command afterwards.

### Step 3 — Motion, still supervised

Unclamp. Same posture: `ask`, human present, stop in hand. Short runs.

Gate: nothing surprised you twice.

### Step 4 — Tier C, the reflex layer

Only now does the MCU get the fast loop: limit switches, current trip, a
watchdog that stops the motors if the host stops talking. Verify each **with jichi
switched off**.

Gate: every reflex works with the deliberative layer dead.

### What is never a step

**`kineticCommands: allow` unattended.** Not in this sequence, not as a shortcut at
the end of a long day. If the run must be unattended, the safe answer is that it must
not actuate.

---

## What remains human-required

Recorded so nobody mistakes this page for the work being done: steps 1–4 need **a
person at the bench with a hand on the stop**. That is why M230's kinetic rungs are
still open and are not counted as done. This page and the verification above are
everything that could honestly be finished without hardware in the room.
