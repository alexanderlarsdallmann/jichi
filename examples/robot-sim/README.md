# Simulated robot — reference example

A hardware-free robot jichi can actually drive, so the embodied design
([docs/ROBOTICS.md](../../docs/ROBOTICS.md)) is provable in software. The
"world" is flat files under `state/`; the devices are POSIX-sh scripts. Nothing
here touches real hardware — but every safety gate is the real one.

| Piece | What it is |
|-------|------------|
| `devices/read-sensors.sh` | read-only sensor → JSON (pose, battery, obstacle) |
| `devices/drive-motor.sh`, `move-arm.sh` | **kinetic** actuators; integrate the sim + append `actuation.log` |
| `devices/stop-all.sh` | **kinetic** safe-state tool, on `kineticCommandsAllow` (the E-stop) |
| `devices/set-led.sh` | a **non-kinetic** actuator, for contrast |
| `devices/mock-play.sh`, `mock-record.sh` | stand-ins for `aplay`/`arecord` |
| `devices/world-tick.sh` | optional world simulator (drift + obstacles) |
| `config.example.json` | inert; the hardened posture + all the tools |
| `missions/*.md` | patrol, inspect-and-report, field-notes (learning) |

> **Going to real hardware?** Read
> [`docs/ROBOTICS_BRINGLIST.md`](../../docs/ROBOTICS_BRINGLIST.md) first. Note that
> this example deliberately ships its device scripts **inside** the tree, so
> `doctor` will report *"a kinetic tool's command lives inside the workspace"* — that
> lint is correct, and on a real robot the scripts must live outside the workspace by
> absolute path so the path fence stops the model editing what moves the motor.

## Try it

```sh
chmod +x devices/*.sh
make reset                          # seed the world
cp config.example.json config.json  # then add a real model block
jichi --config config.json doctor   # read the kinetic lints
jichi --config config.json --auto \
    --budget-tokens 200k --deadline 10m \
    -p "$(cat missions/patrol.md)"
cat actuation.log                   # the proof of (simulated) motion
```

## Safety walkthrough (watch the gate work)

- Run the patrol **unattended** with the default `kineticCommands: "ask"`: every
  `drive_motor` is **refused** (an unattended agent won't actuate), but
  `stop_all` still runs — it's allowlisted. Check
  `~/.jichi.d/audit/kinetic.jsonl` (or `jichi audit`): every
  attempt is recorded.
- Try to bypass the flag: a mission that calls
  `run_terminal_command "./devices/drive-motor.sh 1 1 1"` is **also refused** —
  the gate shadow-matches the shell command against the kinetic tools' commands.
- To let it move, either watch it interactively (approve each `KINETIC` prompt),
  or set `--kinetic-commands allow` **only while supervised**.
- `doctor` warns that these device scripts live *inside* the workspace
  (model-editable). On a real robot, put them at an absolute path outside the
  workspace so the path fence protects them.

The supervisor loop, systemd/cron units, and `--control` steering from
[`examples/autonomous-loop/`](../autonomous-loop/) apply here unchanged — a
mission is just a bounded, steerable, audited run.
