# Hardware testing plan: low-memory and physical-I/O targets

*Written 2026-07-29 on the 4.9 GB reference machine; **extended 2026-08-02
(M264) with Tier V — QEMU/VirtualBox/container targets — and a step-by-step
execution order**, so most of the portability risk can be retired on threadwork
before any board is unpacked; merged 2026-07-30 from two
plans drafted in parallel for the same prompt (this file and a since-folded-in
`docs/proposals/2026-07-low-resource-hardware.md`). A plan to execute later, on
real hardware. Facts marked **[probed]** were verified on the reference machine
before writing; everything about the actual boards is a prediction until the
hardware runs it — the plan's job is to make each prediction cheap to confirm or
refute, keeping the M197–M205 discipline of separating measured fact from
assumption.*

## Execution order across all four tiers (read this first)

Cheapest evidence first, so board time is spent only on what a board can answer.

| Step | Where | Time | Gate to continue |
|---|---|---|---|
| 1 | **V1** RAM floors via cgroups (threadwork) | 30 min | floors recorded in LOW_MEMORY.md |
| 2 | **V0** aarch64 under user-mode QEMU | 20 min | the aarch64 binary runs and the unit suite is green |
| 3 | **V2a** CentOS 7 container (gcc 4.8) + **V5** Alpine/musl | 1.5 h | builds clean on the oldest toolchain and on musl |
| 4 | **V4** 32-bit (`gcc -m32`) | 1 h | word-size assumptions hold |
| 5 | **V3** big-endian (s390x) | half a day | the M136 index-endian guard is proven, not assumed |
| 6 | **physical session** — Pi 400, Pi Zero 2 W, UNO Q (§Tier B) | a day | footprint + timing + the kinetic gate on real silicon — **Zero 2 W done on both architectures (M272 aarch64, M276 armhf); UNO Q done (M282); Pi 400 not brought; kinetic rungs still outstanding (Tier C)** |
| 7 | **V6** VirtualBox terminal matrix | half a day | the TUI on real emulators and the Linux virtual console |

Steps 1–4 are one evening and retire most of the portability risk. **Do not
schedule step 6 before steps 1–3 pass** — diagnosing a build failure over ssh to
a Pi Zero costs an order of magnitude more than diagnosing it in a container.

Every step, virtual or physical, uses the **same** per-run runbook (§Tier V,
"The per-run runbook") and records the **same** row shape. That is the whole
point: numbers from a container, a VM and a Pi are only comparable if the
procedure was identical.

## What this is testing, and the one thing it is not

jichi's low-resource story (`docs/LOW_MEMORY.md`) and its robotics story
(`docs/ROBOTICS.md`) have been designed, measured *in simulation*, and
documented — but never run on constrained silicon or against real actuators. This
plan closes that gap for three target classes.

| tier | hardware | jichi's role | the question it answers |
|---|---|---|---|
| **A** | old x86-64, modern Linux, 8 GB RAM | host | does jichi hold its envelope on a slow CPU + slow storage, and do the timing-sensitive paths survive? |
| **B** | single-board Linux (Raspberry Pi class) wired to sensors/motors | host **and** deliberative controller | first aarch64 run; first real-hardware exercise of the M163 kinetic gate |
| **C** | Pico 2 / Arduino on B's serial port | peripheral (reflex firmware) | does the deliberative/reflex layering hold when the software above it fails? |
| **V** | QEMU / VirtualBox / containers on threadwork | host, emulated | portability: other architectures, **big-endian**, 32-bit, musl, old glibc/kernels, hard RAM ceilings, real terminal emulators — everything that does *not* need a board |

> **Actually-available boards (2026-08-01):** an **Arduino UNO Q** (arm64
> Linux SoC + on-board MCU — tier B and tier C in one enclosure; checklist in
> §Tier B) and a **Raspberry Pi Zero 2** (quad A53, **512 MB**, aarch64) —
> a *tier B* device, not tier C: it runs full Linux and therefore jichi,
> sitting exactly on `docs/LOW_MEMORY.md`'s Comfortable/Constrained boundary,
> which makes it the honest bottom rung for the tier's RAM claims (expect to
> need `--lite` headroom and `JC_SMOKE_TIMEOUT_MULT` for the suite). The
> Pico 2 (RP2350) analysis below stays as written — it is the tier C
> *reference point*, and its does-not-run-jichi arithmetic is unchanged.

**The load-bearing decision, stated once up front:** tiers A and B run jichi.
**Tier C does not, and cannot, run jichi** — and the plan's most useful output is
to make that conclusion explicit and turn the microcontroller into jichi's
*reflex co-processor* rather than a failed port. That is not a consolation prize;
it is `docs/ROBOTICS.md`'s layering made concrete. Detail in §Tier C.

Order of work, cheapest and least risky first, each stage gating the next:
**A (native, low risk) → B read-only → B + C reflex layer + one current-capped
actuator → motion.**

---

## Tier A — old x86-64, 8 GB RAM, modern Linux

**Reframe first, because the obvious framing is wrong: 8 GB is not the
low-memory tier.** It is *more* RAM than the current reference machine (4.9 GB).
Its value is **age** — slow cores, small caches, an older glibc/curl, possibly a
spinning disk. Treat it as the **old-hardware tier**. Genuine memory *pressure* is
tier B's job (a Pi with 1–2 GB) or is simulated on any tier with
`systemd-run --scope -p MemoryMax=64M` — do not pretend 8 GB exercises the
≤64 MB tier in `docs/LOW_MEMORY.md`.

### Design decisions

- **Run the native build first, `--lite` second.** `--lite` targets smaller
  tiers; on this box it should be unnecessary, and confirming jichi is comfortable
  *without* it is the actual result. Then run `--lite` to prove it is harmless
  overhead, not a behaviour change (the M198 `run.sh --lite` suite already asserts
  this offline — here it runs on an older toolchain).
- **Point jichi at HRZ; do not host a model here.** 8 GB cannot run both jichi
  and a useful local model. Measure jichi's *own* footprint (`~/.jichi` already
  points at HRZ). The `tests/bench` local-model bench belongs on threadwork.

### Procedure

1. Build: `make WERROR=1 test` (expect 9176 checks, 0 failures — the reference
   number). On an old toolchain watch for `HAVE_VSNPRINTF = no` (the C89 fallback
   formatter engages; functional — see INSTALL troubleshooting) and an older curl
   TLS chain.
2. Footprint: `python3 tests/measure/idle_tui.py --secs 60 -- ./jichi` — expect
   ~11 MB flat. Materially higher means the older libc/curl chain is heavier: a
   documentation fact, not a bug.
3. A real turn against HRZ: the `list_files`→`read_file`→diagnose task from the
   M196 short drive, with `--budget-tokens 50k`. Success: native tool calling,
   correct answer, `doctor --live` green.
4. `sh tests/e2e/run.sh` **and** `sh tests/e2e/run.sh --lite`. This is the test
   that matters most on this tier: ~72 drivers against behaviours a newer glibc
   might implement differently.

### What would be a genuine finding

An older glibc `getenv`/`realpath`/`nanosleep`/locale edge case; a curl too old
for the HRZ TLS; `make SIZE=1` behaving differently under an older `ld
--gc-sections`. Record in `docs/INSTALL.md`'s minimum-toolchain notes, not as a
code change unless it is a real portability bug.

---

## Tier B — single-board Linux + sensors/motors (the robotics path)

The substantive test. jichi has the whole kinetic architecture
(`docs/ROBOTICS.md`, `examples/robot-sim/`) — the kinetic gate, the E-stop
allowlist, the `jc_audit_kinetic` log, shell-command shadow-matching — but it has
only ever driven a *simulated* robot. The question is whether the **seconds-scale
deliberative layer** works when the tools actually move mass, and this is also the
first time jichi runs on aarch64 at all.

### The architectural decision this test must respect

jichi is **not** the control loop, by design (`docs/ROBOTICS.md`: *"reflexes and
emergency stop belong below it — in firmware, a microcontroller, or a ROS
node"*). The rig must be **two-layer** from the start, or it tests the wrong
thing:

- **Reflex layer (Arduino / Pico 2 — tier C):** the fast loop. Current limits,
  endstop interrupts, a watchdog that halts the motor if no heartbeat arrives
  within N ms, and the physical E-stop. This layer is *not* jichi and must stop
  the hardware with jichi **absent, wedged, or lied to**.
- **Deliberative layer (jichi on the Pi):** high-level intents as tool calls
  (`move_to`, `read_distance`, `stop_all`) that shell out to device scripts, at
  human/seconds cadence.

### Design decisions

- **`stop_all` in `kineticCommandsAllow`, always.** The E-stop tool must survive
  an unattended refuse (M163a's guarantee). The very first kinetic test: arm the
  gate in `deny`, confirm every motion tool is refused **and** `stop_all` still
  fires. If that inverts, stop and fix before any motor is energized.
- **Escalate the hardware, never skip a rung:** `read_distance` (read-only) → a
  single servo with a mechanical stop and a current-limited supply → a drive
  motor. Never wire a motor jichi can command before the reflex watchdog is proven
  to cut it.
- **The reflex watchdog is the acceptance gate, tested by *withholding* the
  heartbeat.** `kill -9` jichi mid-move; the μC must halt the motor on its own
  within the watchdog window, jichi's opinion unavailable. A robotics test that
  only checks the happy path has tested nothing that matters.
- **Every kinetic decision audited** (`~/.jichi.d/audit/kinetic.jsonl`). After a
  session, diff what jichi *tried* to actuate against what physically happened.
- **Never `kineticCommands: allow`, including "just for the demo".** `ask` with a
  human on the E-stop, or `deny`. This is the one rule with no exception.

### The ladder is the test plan

The safety ladder from `docs/ROBOTICS.md`'s deployment checklist *is* the
sequence; each rung has pass criteria and none is skipped:

1. **Sim rung (on threadwork, no hardware):** robot-sim missions pass; the kinetic
   audit log records every decision; `confirm_kinetic` prompts are never
   `always`-satisfied.
2. **Disconnected rung:** real firmware + serial link, motors replaced by
   LEDs / a logic analyzer. Tests: the user-defined serial tool round-trips;
   `jc_kinetic_shell_match` shadow-matches a shelled `./motor.sh`-style script;
   `kineticCommandsAllow` lets `stop_all` through while everything else asks.
3. **Current-limited rung:** motors on a bench supply with a hard current limit.
   The two failure-injection tests that justify the architecture:
   - **kill jichi mid-motion** → firmware watchdog stops the motors on heartbeat
     loss;
   - **unattended refuse** (`kineticCommands: ask`, no TTY) → every actuation
     refused *except* the allowlisted `stop_all`.
4. **Load rung:** only after 1–3, and only with the physical E-stop
   `ROBOTICS.md` requires as "the real last line" — a software plan never
   substitutes for it.

### What would be a genuine finding

A `jc_kinetic_shell_match` miss (fails to shadow a real device script's invocation
form); an audit-log gap; the seconds-scale cadence being too slow for a task
someone *expected* jichi to do in the loop — which is an expectations/docs fix
(jichi is deliberative, not realtime), not a code change. `examples/robot-sim/` is
the reference for the tool/device shapes.

### Tier B on-device checklist: Arduino UNO Q (M220, ready to run)

> **DONE 2026-08-04 (M282): all seven checklist steps pass.** Reached in
> Arduino's official **single-board-computer mode** — monitor, keyboard, mouse and
> a **powered USB-C dongle** — not over USB from the workstation. Results are the
> machine-stamped row in `docs/LOW_MEMORY.md`; headline: `make check-target`
> **green (9,770 checks + 96 smoke drivers)** at a *measured*
> `JC_SMOKE_TIMEOUT_MULT` of **19**, a 73 s `WERROR=1` build (1.5x faster than the
> Pi Zero 2 W's 110 s), a **byte-identical** `SIZE=1` binary to that board
> (1,052,248 B), `doctor --live` confirming **native tool calling**, and one real
> `--auto --budget-tokens 50k` turn answering correctly on 9k tokens. **3669 MB
> (the 4 GB variant), so auto-lite does NOT engage** and the *Comfortable* tier
> holds with tool profile `full` — the "measure, don't assume" question in step 5
> below, answered. It also still passes the suite and a mock `--auto` turn under
> `MemoryMax=256M`.
>
> **The two earlier failures, and why they are not jichi's (M277).** Bus-powered
> from a host USB port the board over-currents and never enumerates. The
> datasheet (`ABX00162-ABX00173`, §3.1) explains it with numbers: **USB-C VBUS 5 V
> at up to 3 A**, and over PD it *"requests only the 5 V / 3 A contract"* — a plain
> host port offers 0.5 A (USB 2) or 0.9 A (USB 3), three to six times short. It
> also has **one** USB-C connector, with video only as **DisplayPort Alt-Mode**
> through the on-board ANX7625 bridge (multiplexed with the JMEDIA MIPI-DSI
> header, so only one display is active) — **there is no HDMI socket**, which is
> why Arduino's related-products list names a *"USB-C dongle with external power
> delivery capabilities"*. Power can instead come from **VIN (7-24 V)** or the
> **5 V pin on JANALOG**, both up to 3 A, if the dongle cannot pass it through.
> Second failure: the hub's Ethernet linked at gigabit while nothing was behind
> it — a hub PHY links as soon as the hub has power, whether or not the board is
> running.
>
> **Bring-up route that worked, for repeating it:** powered dongle → monitor +
> keyboard + mouse + Ethernet; Ethernet into threadwork's second NIC with a
> NetworkManager `ipv4.method shared` profile (`10.42.0.1/24`, DHCP + NAT) plus
> `ufw allow in on <iface>` for the model server; then `systemctl enable --now
> ssh`, read `ip -br addr`, and everything after that is headless over SSH.
> Hostname **sybila**, user **arduino**, Debian 13, OpenSSH 10.0p2.
>
> **Two constraints the edge-AI plan (M278) must respect, found here:** this image
> **ships Docker** (a `docker0` bridge is up, competing for the same 4 GB), and
> `/` is a **9.8 G partition with only ~4.5 G free**, which bounds how large a
> GGUF model plus server can be. Also **no `/usr/bin/time`** on the image, so
> footprint came from jichi's own `/context` line and `/proc/<pid>/VmHWM` polling.
>
> **DANGER, recorded because an earlier draft of this plan got it wrong:** the
> debug UART console (SE4 on JMISC, 115200 bps) is **1.8 V logic**, not 3.3 V. A
> 3.3 V FTDI/CP2102 adapter — which this plan previously suggested as the
> headless fallback — is the wrong part for this board. That console is still the
> only way to see SPL/U-Boot output, which SSH and ADB cannot show.
>
> **Follow-on work, once the board is reachable:** running a small language
> model *on* the UNO Q and configuring jichi for local-plus-network operation is
> its own plan — `docs/plans/2026-08-edge-ai-uno-q.md` (M278). It depends on
> steps 1–7 below having passed first.

The UNO Q is a tier B *and* tier C device in one enclosure — an arm64 Linux SoC
(quad Cortex-A53, 2 GB RAM class) paired with an on-board MCU — so it exercises
both halves of this plan's architecture, with the MCU already positioned as the
reflex layer. jichi runs ONLY on the Linux side; the MCU boundary is Tier C's
(unchanged). Everything below is offline-preparable; run in order when the
device is on the bench, and record each number machine-stamped into
`docs/LOW_MEMORY.md` (the M217 check-target precedent). The same checklist
runs unchanged on the **Pi Zero 2** (512 MB): there `"lowResource": true`
is not a boundary case but the prescription, step 6's `MemoryMax=256M` is
close to the machine's own ceiling (the box *is* the pressure test), and the
timeout multiplier from step 3 is the number the Zero 2 exists to produce:

1. **Toolchain + deps** (Debian-family): `apt install gcc make git
   libcurl4-openssl-dev`; python3 explicitly NOT required (M217).
2. **Native build**: `make SIZE=1` then `make info` — record binary size and
   which probes fired (`HAVE_MALLOC_TRIM` should be **yes** on glibc; a musl
   userland makes jc_memtrim a documented no-op).
3. **Full validation**: `make check-target`. On A53-class silicon start with
   `JC_SMOKE_TIMEOUT_MULT=3` (M220) and record the multiplier that passes
   clean — that number IS a deliverable (the first aarch64 run of the suite).
4. **Footprint**: `/usr/bin/time -v ./jichi --version | grep Maximum`, then a
   headless mock turn; record against the LOW_MEMORY tier table. With 2 GB,
   the *Comfortable* tier should hold without `--lite`; record whether it does.
5. **Config for small models**: `"lowResource": true` is prescribed only ≤2 GB
   (this device is the boundary case — measure, don't assume) +
   `contextLimit` sized to the small model actually served (the setup wizard's
   low-res preset uses 6000).
6. **Pressure test**: `systemd-run --scope -p MemoryMax=256M ./run_tests` and
   the same for one mock `--auto` turn — the Constrained-tier claim, on real
   silicon.
7. **Soak**: `tests/measure/soak.py --profile retry` and `--profile reads`
   (needs python3, so optional on-device; the smoke tier's `turn_scratch.sh`
   gauge is the Python-free stand-in).
8. **Reflex boundary**: the MCU side stays tier C — run the `stop_all`
   / kinetic-gate rung from this plan's Tier B design before any actuator
   work, with the MCU as the watchdog layer.

### The on-threadwork test session (M230): bring-list + runbook

The session's one prerequisite: **on threadwork, each board attached and
SSH-reachable.** jichi runs *on the board's Linux* (it is not driven over USB
from threadwork); USB is only a wire to a shell, and each board is a **thin
client** — the model lives on threadwork or HRZ, never on the board. Decide the
**model endpoint before the session** (simplest: point `~/.jichi` at a
threadwork-hosted model so the board only needs to reach threadwork).

**What to bring — the boards**

- **Raspberry Pi 400** (A72, 4 GB, aarch64) — the comfortable rung.
- **Raspberry Pi Zero 2 W** (quad A53, 512 MB, aarch64) — the bottom rung.
- **Arduino UNO Q** (quad A53 Linux + on-board MCU, 2–4 GB) — tier B + C in one.

**What to bring — storage / OS**

- **2× microSD (16 GB+)** for the Pis, each flashed with **64-bit Raspberry Pi
  OS** (aarch64). Flash from threadwork with Raspberry Pi Imager; **enable SSH
  + set Wi-Fi/hostname in the imager's pre-config** so first boot is headless.
- **1× more microSD flashed with 32-bit Raspberry Pi OS (armhf)** for the
  **arm32 run** — this is a deliberate target (the 32-bit `long` path). Label
  the cards **64 / 32**.
- A **USB microSD reader** for threadwork (unless it has a slot).
- The **UNO Q** boots its own eMMC Linux — no SD; just verify Linux is flashed
  and SSH is enabled before the trip.

**What to bring — power**

- Pi 400: **USB-C PSU** (5.1 V/3 A official). *Its USB-C is power-only* — no
  USB-networking on the Pi 400.
- Pi Zero 2 W: **micro-USB PSU** (5 V/2.5 A) into the **PWR IN** port (it has
  two micro-USB ports — PWR and USB/data; don't confuse them).
- UNO Q: its **USB-C** supply — **and this is now measured, not advisory
  (M277).** Powered from a plain host USB port the board **over-currents and
  never enumerates**: `usb 3-5: device descriptor read/64, error -71`, a port
  power-cycle, `device not accepting address … error -71`, `unable to enumerate
  USB device`, then an explicit `usb usb3-port5: over-current condition`. A
  standard port offers 500 mA (USB 2) / 900 mA (USB 3) absent a USB-C PD
  negotiation, and an arm64 SoC plus an on-board MCU wants more at boot. So:
  **its own supply, or a powered hub — never bus power from the workstation.**
  The symptom is easy to misread as a bad image or a software fault; it is a
  power budget.

**What to bring — connectivity (to get an SSH shell)**

- Pi 400: **Ethernet cable** to threadwork's network, *or* Wi-Fi (set in the
  imager). No USB-net option.
- Pi Zero 2 W: **Wi-Fi** (easiest) *or* a **good micro-USB _data_ cable** from
  its USB (data) port to threadwork for **Ethernet-over-USB gadget mode** →
  `usb0`, no external network needed.
- UNO Q: **USB-C data cable** to threadwork (serial console + USB-network to
  the Linux side); Wi-Fi/Ethernet if the board exposes it. **A charge-only
  USB-C cable is indistinguishable by eye and gives exactly the same
  "nothing appeared" symptom as an unpowered board — verify the cable carries
  data before diagnosing anything else (M277).**
- **A USB hub's Ethernet port is NOT a path to the board (M277).** Attempted:
  UNO Q → USB-C → powered hub, hub → Ethernet → threadwork. threadwork saw the
  link come up at **1000 Mb/s, carrier present** and NetworkManager sat in
  `activating` — which reads as a network problem and is not one. Two separate
  facts hid behind it: (a) the default wired profile is a DHCP *client*, so the
  host must instead run `ipv4.method shared` to serve DHCP + NAT, exactly as the
  Pi's gadget link does; and (b) even with that fixed, **nothing was behind the
  hub's NIC** — an all-nodes multicast probe (`ping -6 ff02::1%<if>`) drew a
  reply only from threadwork's own address, with an empty neighbour table and no
  lease. A hub's Ethernet PHY links to the host as soon as the hub is powered;
  that proves nothing about whether the board behind it is booted or has claimed
  the NIC. **`ping -6 ff02::1%<iface>` is the cheap discovery test to reach for
  first** — any booted Linux on the segment answers it regardless of DHCP or
  IPv4 configuration, so a single reply means "nobody is there", not "addressing
  is wrong".
- **Fallback:** a **3.3 V USB-serial (FTDI/CP2102) adapter + jumper wires** for
  the Pi GPIO serial console (GPIO14/15) if networking misbehaves on first
  boot. Cheap insurance for a headless bring-up.
- If threadwork and the boards can't share a network (locked university LAN),
  bring a **small unmanaged switch or travel router** to make a private LAN —
  or lean on the USB-gadget path (Zero 2 / UNO Q), which needs no external net.

**What to bring — misc**

- A **powered USB hub** (threadwork ports get scarce fast with three boards +
  a card reader + serial adapters).
- Optional first-boot display: **micro-HDMI→HDMI** (Pi 400) / **mini-HDMI→HDMI**
  (Zero 2) + a monitor — only if you'd rather not go fully headless.

**What is NOT needed for the first session** (the numbers-gathering run):
sensors, motors, a bench PSU, a physical E-stop. Those belong to the
kinetic/motor rungs (step 8 above, Tier C) — bring them for a *second*,
supervised session. The first session's deliverables (below) need none of it.

**Runbook — order of operations on threadwork**

0. Cross-builds are staged (built on xubuntu; see the note below) — copy them
   to threadwork as a fast smoke-check that the arch binaries at least *load*
   on-device, before the slower native build.
1. Flash + first-boot each board headless; confirm SSH.
2. Per board, run **checklist steps 1–7 above** over SSH (toolchain + deps →
   native `make SIZE=1` + `make info` → `make check-target` with
   `JC_SMOKE_TIMEOUT_MULT` raised until clean → footprint → small-model config
   → `MemoryMax=` pressure test → soak). jichi (or the operator) can drive all
   of this non-interactively.
3. Record **each board's passing timeout multiplier, `check-target` result, and
   footprint**, machine-stamped into `docs/LOW_MEMORY.md` (M217/M220 precedent).
4. Repeat step 2 on the **armhf card** (Pi) for the 32-bit run — watch for any
   `%lu`/width or `char`-signedness surprise (the tasks-23/29/30 axis, now on
   real 32-bit silicon).
5. **Only in a supervised session, with the E-stop wired:** the kinetic rungs
   (step 8 / Tier C). `kineticCommands` never `allow`; a human on the E-stop.

### First physical session executed: Pi Zero 2 W (M272, 2026-08-03, threadwork)

*One board only — the Pi 400 was not on the desk, so the single 16 GB card runs
the Zero 2 first and will be re-flashed armhf for the 32-bit run later. The
UNO Q session is still ahead. Network was eduroam-only (WPA2-Enterprise), so
the board ran over **USB gadget networking** exactly as the bring-list's
fallback prescribes: one data cable = power + SSH + NAT'd internet, no external
network at all.*

**Every number the checklist asked for, measured on the board** (Debian 13
aarch64, 415 MB visible RAM, SD card):

| Checklist step | Result |
|---|---|
| 2. native `make SIZE=1` | **~1.05 MB** binary; probes correct (`HAVE_MALLOC_TRIM=yes` on glibc) |
| 2. `make WERROR=1` (serial, from scratch) | **110 s, zero warnings** → `JC_SMOKE_TIMEOUT_MULT=28` vs threadwork's 4 s |
| 3. `make check-target` at mult 28 | **GREEN — 9,722 unit checks + `smoke: OK (94 drivers, 404 checks)`** — the first full gate on physical aarch64 |
| 4. footprint | `--version` peak **8.9 MB**, `doctor` peak **16.3 MB** |
| 5. small-model config | `lowResource: true` + `contextLimit: 6000`, model on threadwork (`http://10.42.0.1:1234/v1`, LM Studio) |
| 6. pressure test | unit suite **and** a full mock `--auto` turn both complete under `MemoryMax=256M` `MemorySwapMax=0` |
| live acceptance | `doctor --live` exit 0 — **native tool calling observed** (104 real prompt tokens); then a real `--auto --budget-tokens 50k` turn: 2 `read_file` calls, correct diagnosis, 4,461 tokens, `stop_reason: done` |

**Findings from the bring-up, so the next board is cheaper:**

1. **rpi-imager 1.8.5 silently applies NO customization to 2025+ Raspberry Pi
   OS images** — those images switched to cloud-init (NoCloud on the boot
   partition: `user-data`/`meta-data`/`network-config`), which the packaged
   imager predates. The card boots with no user and no SSH. Fix: write the
   cloud-init files by hand on the FAT partition (this plan's tier-v seeds are
   the same mechanism). Check `user-data` after flashing, before first boot.
2. **The netplan-rendered gadget profile did not activate** (`usb0` stayed
   down; the host saw NO-CARRIER; the rendered profile carried an empty
   `match: {}` and lived only in `/run`). Fix that worked: an explicit NM
   keyfile bound to `usb0` (autoconnect priority, infinite DHCP retry) plus a
   conf.d `managed=1` override, written via cloud-init `write_files`; which
   half was load-bearing is deliberately unisolated (both were applied in one
   revision). A per-boot script now writes `pi-diag.txt` (ip/nmcli/NM journal)
   to the FAT partition — diagnostics readable by card-shuffle, no root, no
   serial adapter.
3. **Pin the gadget MACs in `cmdline.txt`** (`g_ether.host_addr=`/`dev_addr=`)
   so the host interface name is deterministic (`enx024a49434849`) and the NM
   shared profile + ufw rules can be created before first boot.
4. **threadwork side:** NM `ipv4.method shared` + `ufw route allow in on
   <gadget-if>` + `net/ipv4/ip_forward=1` (persisted in `/etc/ufw/sysctl.conf`)
   + `ufw allow in on <gadget-if>` for LM Studio; the kernel journal is empty
   after a hard unplug, so diagnose via the FAT diag file, not the card's
   journal.

**Staged cross-builds (built on xubuntu, 2026-08-01, ready to `scp`):**
static-musl `SIZE=1 HAVE_CURL=` binaries produced with
`zig cc -target <triple> SIZE=1 HAVE_CURL=` (zig 0.16.0) — proving both
word-sizes compile + link clean off one command (the M220 aarch64 result
re-confirmed; arm32 added):

| target | `file` | `jichi` | `run_tests` |
|---|---|---|---|
| `aarch64-linux-musl` | ELF 64-bit ARM aarch64, static, stripped | 934 KB | 1.30 MB |
| `arm-linux-musleabihf` | ELF 32-bit ARM EABI5, static, stripped | 879 KB | 1.26 MB |

**Re-staged at M268 HEAD (`7862992`, xubuntu, 2026-08-03)** — the row above
predates M262–M268, so the binaries were rebuilt from current HEAD before the
threadwork session. Same command, same `zig` 0.16.0, **zero retries** (the
recorded snap-zig transient flake did not fire; the wrapper retries 5× and `make`
is incremental, so a re-run continues rather than restarts). Staged in
`~/jichi-staged/` as `jichi-<triple>` / `run_tests-<triple>` with a `BUILT_FROM`
stamp, ready to `scp`:

| target | `jichi` | `run_tests` |
|---|---|---|
| `aarch64-linux-musl` | 943 KB | 1.28 MB |
| `arm-linux-musleabihf` | 887 KB | 1.24 MB |

**Re-staged at M271 HEAD (`9d3b16b`, 2026-08-03; 9,713 checks)**, because the row
above went stale within the day and the on-device `run_tests` count is a recorded
deliverable -- a staged binary reporting 9,693 checks against a tree that has
9,713 would make a board's row read as a regression. Zero retries again.

**Stage as the LAST step, after the final code commit.** This row was first
re-staged at `690dfef` and then invalidated minutes later by the very commit that
documented the staleness problem -- M271 added four checks to `tests/test_path.c`.
So the check is mechanical, not a matter of care: compare
`~/jichi-staged/BUILT_FROM` against `git rev-parse --short HEAD` immediately
before copying to a board, and re-run the cross-build if they differ.

| target | `jichi` | `run_tests` |
|---|---|---|
| `aarch64-linux-musl` | 944 KB | 1.28 MB |
| `arm-linux-musleabihf` | 887 KB | 1.24 MB |

(`SIZE=1` — `-Os` + section GC + strip — lands both well under M220's 11.2 MB
non-size-optimized aarch64 figure.) They can be built but **not run** on the
x86 reference box (no qemu), and being `HAVE_CURL=` they run the **offline
unit+smoke suite only** — real model turns need the **native on-device build**
with libcurl (step 2). `run_tests` is the full offline unit suite, so it is the
first thing to run on each board once copied over.

---

## Tier C — Pico 2 (RP2350): jichi does NOT run here

### The decision, and why it is arithmetic, not pessimism

| jichi needs | RP2350 (Pico 2) has |
|---|---|
| ~10–20 MB RSS floor (libcurl + TLS dominate; measured, `INSTALL.md`) | **520 KB** SRAM (+8 MB PSRAM on some boards) |
| a ~0.7–1.4 MB binary | flash XIP possible, but the RAM gap is the wall |
| **`fork()` — structurally, everywhere** | no MMU → MMU-less Linux at best, which has no `fork()` |
| a TLS 1.2+ client to reach a model endpoint | not within SRAM alongside anything else |

**The `fork()` dependency is the unfixable point, and it is [probed] real.**
`fork()` appears in **ten** translation units — `spawn_parallel` (the fork pool),
every MCP stdio server (`jc_mcp_stdio`), every git snapshot (`jc_snapshot`,
`jc_tool_git`), `jc_proc_capture` (hooks, user tools, `pdftotext`, sound),
background commands (`jc_bg`), the envelope verifier (`jc_envelope`), and the LSP
spawn (`jc_lsp`). That is not a porting task; it is a different program. An
MMU-less Linux port to the RP2350 exists as research, and jichi on it would still
fail at the first snapshot. This is not `--lite` or `SIZE=1` away — it is a
category difference.

**Rejected alternative:** jichi-on-Pico via MMU-less Linux. Rejected because the
fork dependency is structural, the RAM deficit is ~20× on the best board, and the
outcome would be a heroic demo that validates nothing about the product. The
≤64 MB tier in `docs/LOW_MEMORY.md` is already the honest lower bound of
jichi's world.

### The Pico's real role is a promotion

Make it tier B's **reflex co-processor**, the adapter `docs/ROBOTICS.md` already
names. The RP2350 is arguably a *better* reflex layer than a classic AVR Arduino:
dual cores (one for the serial protocol, one for control), PIO blocks that
generate motor PWM and read quadrature encoders in hardware, and enough SRAM for
real ring buffers.

- The Pico runs the current limit, endstop interrupts, and the **watchdog that
  halts on lost heartbeat**, exposing a tiny serial/USB line protocol.
- jichi (on the Pi) drives it through a device script — the same kinetic-tool
  path, now with a *real* reflex layer underneath instead of a simulated one.
- The E-stop is physical **and** in the Pico's firmware, so it works with jichi
  absent — the exact property tier B rung 3 tests by withholding the heartbeat.

**One firmware contract, two implementations.** A line-oriented protocol
(`STATE?`, `SET <axis> <value>`, `STOP`, heartbeat `PING`/`PONG` with a
firmware-side deadline) on both the Arduino and the Pico 2. jichi's side is
identical for either — a user-defined tool marked `kinetic: true` — so the μC
choice is a swappable detail, and building both doubles as a conformance test for
the contract. Firmware owns limits, slew rates, watchdog, E-stop; jichi owns
setpoints at seconds-scale under the ask posture.

### The one bounded experiment worth running *on* the Pico

Not "can jichi run on it" (no), but: **is any jichi pure core small and
freestanding enough to compile into the reflex firmware?** The pure cores —
`jc_testparse`, `jc_kinetic_shell_match`, the `jc_sessmeta_scan` JSON scalar
reader — are C89 with no allocation in their hot paths. The candidate is a
**shared line-protocol parser** compiled into *both* the Pico firmware and jichi's
device script, so the two agree by construction. A genuine, bounded experiment; a
full port is not. If a "pure" core turns out to need libc when compiled
freestanding, that is a real portability finding worth a fix and a test; if it
compiles clean, that is evidence the pure-core discipline is genuinely
freestanding-capable.

---

---

## Tier V — virtualized targets: QEMU and VirtualBox (M264)

*Added 2026-08-02. Tiers A–C need hardware on a desk; this tier needs only
threadwork (or the private dev box) and answers most of the same questions
first, for an hour of setup instead of a shipping delay. Everything here is a
**prediction until run**, same rule as the rest of this document.*

### What virtualization can and cannot answer

Be honest about this before spending a weekend on it:

| Question | VM/emulator | Real board |
|---|---|---|
| does it **compile** on an old toolchain / another arch? | **yes, definitively** | redundant |
| does the **byte-order** cache guard work? | **yes — and only here** (no BE hardware exists on this desk) | n/a |
| does it behave under a hard **RAM ceiling**? | yes (cgroup / `-m`) — the ceiling is real even if the CPU is not | yes |
| **32-bit** correctness (`long`, `time_t`, `off_t`)? | yes | yes |
| does it work with **musl** / no `malloc_trim`? | yes | yes |
| **wall-clock** behaviour: timeouts, stalls, spinner cadence | **no** — emulated CPUs distort every constant | **only here** |
| SD-card **I/O latency**, thermal throttling | **no** | **only here** |
| **terminal** reality: `$TERM`, UTF-8, bracketed paste, resize | partly (VirtualBox with a desktop) | yes |
| GPIO / actuators / the kinetic gate | **no** | **only here** |

So: **V-tier proves portability and correctness; A/B-tier proves timing and
physics.** A green V-tier does not license skipping the boards, and a red one
saves you from carrying a board to a desk to learn something a VM would have
told you.

### V0 — the twenty-minute smoke: aarch64 under user-mode QEMU

The cheapest possible aarch64 signal. No VM, no image, no boot: `binfmt_misc`
runs the cross-built binary directly.

```sh
sudo apt-get install -y qemu-user-static binfmt-support
make clean
make CC="zig cc -target aarch64-linux-musl" HAVE_CURL= jichi run_tests
file ./jichi                      # expect: ELF 64-bit LSB, ARM aarch64, static
./jichi --version                 # binfmt dispatches to qemu-aarch64-static
./run_tests                       # the whole unit suite, emulated
JC_SMOKE_TIMEOUT_MULT=8 make smoke
```

> **Operational note (M266):** the *snap* build of zig intermittently dies with
> `internal error ... transient scope not created in 10s` under sustained load --
> snap confinement, not zig or jichi. `make` is incremental, so wrap the build in
> a small retry loop and it completes.

**Why `HAVE_CURL=`:** cross-linking libcurl for aarch64 needs a sysroot; the
no-network build is the portability question we actually want answered here, and
it is the same build a locked-down deployment uses.

**Known caveats, so a failure is read correctly:** user-mode QEMU emulates the
CPU, not the machine — `free`, `/proc/self/status` RSS and every timing constant
are the *host's*. Do not record footprint numbers from V0. `fork`/`exec`, PTYs
and `select` all work but are slower and occasionally reorder, so treat a
`ptydrive` timeout as an emulation artefact until it reproduces on V2 or a board
(hence `JC_SMOKE_TIMEOUT_MULT=8`).

**Acceptance:** unit suite 0 failures; `make smoke` green or failing only on
PTY-timing drivers. Anything else is a portability finding.

### V1 — memory ceilings, no VM required

The cheapest *honest* memory test on Linux is a cgroup, and it needs no guest at
all. This is what turns `docs/LOW_MEMORY.md`'s tiers from design targets into
measured claims.

```sh
# One turn under a hard ceiling; the kernel OOM-kills rather than swapping.
systemd-run --user --scope -p MemoryMax=64M -p MemorySwapMax=0 \
    ./jichi --lite -p "say hi" < /dev/null

# Bisect the floor: the lowest ceiling that survives an offline subcommand,
# a headless turn, and then the full gate.
for m in 512 256 128 96 64 48 32; do
    printf '%4sM: ' "$m"
    systemd-run --user --scope -q -p MemoryMax=${m}M -p MemorySwapMax=0 \
        ./jichi doctor >/dev/null 2>&1 && echo ok || echo OOM
done

systemd-run --user --scope -p MemoryMax=256M -p MemorySwapMax=0 \
    make check-target
```

**Record three floors, they are different numbers:** offline subcommand
(`doctor`), one headless turn against a mock, and the full `check-target` (which
forks compilers and is the highest). LOW_MEMORY.md currently states tiers as
design intent; these replace them with measurements.

**Watch for:** an OOM kill that leaves a wedged child (`spawn_parallel`, MCP
stdio, a background command) — that is a genuine finding, not a test artefact.

### V2 — system-mode QEMU: the vintage matrix

This is where "various vintages" gets tested. Two layers, and choosing the right
one per question saves hours:

- **containers (podman/docker) for *toolchain* vintage** — old glibc, old gcc, a
  different libc. They share the host kernel, boot in a second, and support the
  same cgroup ceilings as V1. Use these for *most* of the matrix.
- **full VMs for *kernel* vintage, init, and whole-machine memory** — when the
  question involves `/proc` shape, an old kernel's `select`/`epoll` behaviour, or
  a machine that genuinely has 256 MB.

| # | Vintage | libc / gcc | Layer | Why this one |
|---|---|---|---|---|
| V2a | CentOS 7 (2014) | glibc 2.17, **gcc 4.8.5** | container first | the oldest realistic target; gcc 4.8 is a *true* C89 compiler and will reject anything that crept in |
| V2b | Debian 9 stretch (2017) | glibc 2.24, gcc 6 | container | the "old but not ancient" server still in the field |
| V2c | Debian 12 / Ubuntu 22.04 | glibc 2.35+ | container | the baseline everyone actually runs |
| V2d | Alpine 3.20 | **musl**, gcc 13 | container | see V5 |
| V2e | Debian 12, `-m 256 -smp 1` | — | **VM** | a whole machine with 256 MB and one core |
| V2f | Debian 9, `-m 512` | old kernel | **VM** | old kernel + old glibc together |

Container skeleton (identical per row, so results compare):

```sh
podman run --rm -it --memory 512m -v "$PWD:/src:Z" -w /src centos:7 \
    sh -c 'yum -y install gcc make libcurl-devel >/dev/null && make clean &&
           make WERROR=1 && make check-target'
```

VM skeleton:

```sh
qemu-system-x86_64 -m 256 -smp 1 -nographic \
  -drive file=debian12.qcow2,if=virtio -netdev user,id=n -device virtio-net,netdev=n
# inside: apt-get install -y build-essential libcurl4-openssl-dev git
#         make WERROR=1 && JC_SMOKE_TIMEOUT_MULT=4 make check-target
```

**Expect to find:** nothing in the C (the codebase is C89 with `-pedantic` under
two compilers already) — the likely failures are *build-system* and *test-rig*
assumptions: a `make` version, a shell builtin, a coreutils flag, `timeout(1)`
absent, `/usr/bin/time -v` absent (busybox), a `grep -E` difference. Those are
real findings for `make check-target`'s "runs on any POSIX box" claim.

### V2e/V2f — the worked runbook (M267)

*Written 2026-08-02 when the two VM rows were attempted and could not run here.
Every URL and tool below was **[probed]** on that date; the rows themselves are
still a prediction.*

**The correction first, because the previously recorded blocker was wrong.**
M265 recorded these rows as waiting on *"the `libvirt` group membership taking
effect in a fresh login"*. That is not the obstacle, and chasing it wastes an
afternoon:

- **libvirt is not involved.** The V2 skeleton above invokes bare
  `qemu-system-x86_64`, which needs no group membership at all.
- **The real constraint is that the reference box has no `/dev/kvm`.** It is
  *itself* a VirtualBox guest (`systemd-detect-virt` → `oracle`) and the outer
  hypervisor does not pass VT-x through — no `vmx` in `/proc/cpuinfo`, no
  loadable `kvm_intel`. Adding the user to `libvirt` *or* `kvm` changes nothing.
- So QEMU here can only run **TCG**, at roughly 10–30× on 3 shared vCPUs — and
  by this document's own findings table, wall-clock from an emulated CPU is
  *not a finding*. The most expensive part of each row would also be its least
  trustworthy output.

**Therefore V2e/V2f run on threadwork**, where `/dev/kvm` exists. That is the
single trigger; nothing else about the rows is blocked.

**The executable form is `scripts/tier-v-vm.sh`** — one row per invocation, so
the procedure is identical between them by construction rather than by
discipline:

```sh
scripts/tier-v-vm.sh v2e            # Debian 12, measured at -m 256 -smp 1
scripts/tier-v-vm.sh v2f            # Debian 9,  measured at -m 512
scripts/tier-v-vm.sh v2e --dry-run  # print every step, touch nothing
scripts/tier-v-vm.sh v2e --console  # interactive serial console, for debugging
```

It performs the per-run runbook below §"The per-run runbook" verbatim and writes
`results-<row>.txt`. Six decisions in it are load-bearing enough to state here,
because a future reader will otherwise re-derive them the expensive way:

1. **`genericcloud`, not `nocloud`, for V2e.** Debian's `nocloud` flavour ships
   *without* cloud-init — empty root password, serial autologin — so it can only
   be driven by screen-scraping a console, which is how unattended runs fail.
   `genericcloud` (V2e) and `openstack` (V2f) both carry cloud-init, so **one**
   mechanism seeds both: a NoCloud `CIDATA` ISO built with `xorriso`, creating a
   `tierv` user with a NOPASSWD-sudo key. Identical path for both rows.
   - V2e: `.../cloud/bookworm/latest/debian-12-genericcloud-amd64.qcow2` (333 MB)
   - V2f: `.../cloud/OpenStack/archive/9.13.9-20201210/debian-9.13.9-20201210-openstack-amd64.qcow2`
     (600 MB — note the *version-stamped* filename; the unversioned one 404s)
2. **A second disk, not a grown root.** The cloud root is 2 GB and there is no
   `growpart` step; `mkfs.ext4` on a 12 GB `/dev/vdb` mounted at `/work` avoids
   partition surgery entirely and holds build-essential + the tree + objects.
3. **The tree travels as `git ls-files` over ssh, not 9p.** Tracked files only
   (no build artefacts) but *including local uncommitted edits* — a portability
   row should test the tree you are holding, not the last commit. Using the ssh
   channel that already exists drops the guest-kernel 9p dependency, and nothing
   is ever built on a network filesystem, which would make the row's build time
   meaningless.
4. **Provision high, measure low.** dpkg unpacking is not the measurement, so
   `apt-get install` runs at `-m 1024`; the guest then reboots at the row's real
   ceiling. But the **build is attempted at the real ceiling first** — whether
   gcc survives a 256 MB machine is precisely the V2e datum. If it OOMs, the
   script records that as a finding, rebuilds at 1024 MB, and drops back to
   256 MB for the gate, saying so in the row.
5. **The multiplier is measured, never guessed.** In-guest `make WERROR=1`
   seconds ÷ the host's serial-build seconds, rounded up, floor 2 (M220
   precedent: the passing multiplier *is* a deliverable). The script times the
   host baseline itself unless given `--host-secs N`.
6. **The accelerator is stamped into the row.** `-accel kvm -cpu host` when
   `/dev/kvm` exists, else `-accel tcg`. A row that does not say which one ran
   cannot be read against the findings table.
7. **The build phase compiles everything the gate needs.** `check-target` is
   `test` + `smoke`, and both *build* before they run — so a build that fell
   back to a larger ceiling would have met the same ceiling again inside the
   gate. The build script therefore also makes `run_tests` and `smoke-tools`
   (incremental, reusing the objects), leaving the gate step to only *run*.
   `SIZE=1` is measured first and cleaned away, so the timed `make WERROR=1`
   that feeds the multiplier is a genuine from-scratch build, comparable to the
   host baseline it is divided by.

V2f additionally repoints apt at `http://archive.debian.org/debian stretch main`
with `Acquire::Check-Valid-Until "false"` — stretch left the mirrors, and the
archive was **[probed]** answering 200 for both `Release` and `Packages.gz`.

**Read V2f for what it actually buys: the kernel, not the toolchain.** V2a
(CentOS 7 — glibc 2.17, gcc 4.8.5, make 3.82) is *older userland* than stretch's
glibc 2.24 / gcc 6, and it has already run. What only V2f can give is a **4.9
kernel** under a full machine: `/proc` shape, `select`/`epoll` behaviour,
cgroup v1 only, older coreutils and `timeout(1)`. Claiming it as an
old-toolchain row would double-count V2a. (Note too that stretch's git 2.11
*has* `worktree`, so M265's finding-4 crash path will not re-fire there — that
one needs a git older than 2.5.)

### V2e/V2f executed, 2026-08-03 (M272): V2e green, V2f 83/94 + one open

The rows ran on threadwork as prescribed — twelve guest passes in total, and
the cadence is the record: **each pass marched exactly one defect further**
(the suite stops at its first failing driver). In order: the
`ask`/`websearch`/`subagent_itercap` auto-lite reshaping (→ the M272
`lowResource` tri-state product fix + pins + lints), the `sessions_footprint`
prompt-expect collision with the auto-lite notice, `git_stash` failing on
git 2.11 (product fix), the kinetic driver's dash-0.5.8 heredoc expansion,
the `spawn_parallel` worktree leak on git < 2.17 (product fix), the
supervisor driver's env not surviving a prefix-assignment-before-function
call on old dash, and `ptydrive` never scaling its inner deadlines by
`JC_SMOKE_TIMEOUT_MULT` at all (now it does — expect/waitexit/--deadline,
never delay; and the three deadline layers must keep their hierarchy under a
shared multiplier, so a too-slow machine needs a bigger MULT, not a bigger
per-driver base — `TIERV_MULT` overrides the build-ratio measurement and is
stamped MANUAL in the row). Two corrections to the M267 section above:
`-accel kvm -cpu host` is **not** universally right — Debian 9's 4.9 kernel
panics in `text_poke` on 2026 silicon (`TIERV_CPU=IvyBridge` pins an older
model; KVM speed kept, CPUID masked, stamped into the row).

**One cell stayed red overnight and was closed the next morning (M273), and
the correction matters more than the fix.** M272 recorded `turn_scratch` as a
"flow-control wedge" with two suspects; the real chain was: libcurl 7.52 sends
`Expect: 100-continue` on every request over 1 KB and waits a second for a
reply the mock never sends (**a product defect** — one dead second per model
call for anyone on an older libcurl), which made the run slow enough for
**mockmodel's own unscaled 120 s self-watchdog** to shoot the server out from
under it. Every timeout M272 raised belonged to a different layer, which is why
three increasingly generous budgets changed nothing — and why the frozen
transcript was byte-identical each time. Both are fixed; the row now passes,
and `TIERV_MULT` is no longer needed for it. Lesson recorded as
docs/ANECDOTES.md #30. Full narrative: ROADMAP M272 + M273; final numbers: the
LOW_MEMORY.md measured-target rows.

### V3 — big-endian: the one test only a VM can give us

**This is the highest-value row in the whole tier**, because jichi contains a
guard written for a machine nobody here owns. `jc_index_endian_tag` (M136) stamps
the byte order into the index manifest so that a cache written on one
architecture is *rebuilt* rather than misread on another — the shared-`$HOME`
scenario. It has never run on a big-endian machine.

```sh
sudo apt-get install -y qemu-user-static
# s390x is the practical BE target: Debian still ports it.
make clean && make CC="s390x-linux-gnu-gcc" HAVE_CURL= jichi run_tests   # or in a
                                                                          # debian s390x container
./run_tests            # via binfmt; the pure suite must be 0 failures
```

Then the actual experiment, which needs both orders:

1. on x86-64 (LE), build an index over a small fixture repo; note
   `~/.jichi.d/index/<key>/manifest.json`'s endian tag;
2. copy that whole index directory to the s390x guest (or share it);
3. run a `codebase_search` there.

**Pass:** jichi notices the foreign tag and **rebuilds** the cache. **Fail:** it
reads the blob and returns nonsense similarity scores — silent wrong answers,
the worst failure mode there is. Either outcome is worth the afternoon; a pass
retires a standing "we think this works" and a fail is a real bug found before a
user with a shared home directory finds it.

Second BE payload worth checking while you are there: `jc_uuid`, the SSE parser,
and anything that memcpy's a float — grep for `float32` handling in
`src/index/`.

### V4 — 32-bit

`long` is 32 bits, `time_t` may be, `off_t` needs `_FILE_OFFSET_BITS`, and the
codebase uses `%lu` + casts everywhere precisely so this works. Cheapest form
needs no VM:

```sh
sudo apt-get install -y gcc-multilib
make clean && make CC="gcc -m32" HAVE_CURL= WERROR=1 && ./run_tests
```

Then armv7 (Pi 1/2/Zero-W class) under user-mode QEMU or a container. **Watch
for:** any file-size or timestamp arithmetic (`jc_platform`, `jc_index` mtimes,
the session store), and the 2038 boundary if any test fabricates a far-future
mtime.

### V5 — musl / Alpine, and the `malloc_trim` probe

CLAUDE.md records a deliberate decision: `JC_HAVE_MALLOC_TRIM` is a **Makefile
compile probe**, not `#ifdef __GLIBC__`, because uClibc masquerades as glibc.
Alpine is where that decision gets tested for real — musl has no `malloc_trim`,
so the probe must simply come back "no" and the heap-return calls must compile
out cleanly.

```sh
podman run --rm -it -v "$PWD:/src:Z" -w /src alpine:3.20 \
    sh -c 'apk add build-base curl-dev >/dev/null && make clean &&
           make info && make WERROR=1 && make check-target'
```

`make info` must show the probe resolving to *absent*. If jichi still links, the
probe works; if it fails to link on `malloc_trim`, the gate is `#ifdef`-shaped
somewhere it should not be.

### V6 — VirtualBox: the terminal-reality tier

QEMU headless cannot answer the questions M254–M258 just opened, because those
live in `termios` and in what a *terminal emulator* does. VirtualBox with a
desktop is the cheap way to get several real ones.

Per guest (Ubuntu 24.04 desktop, Debian 12 + XFCE, and one deliberately old —
Ubuntu 16.04), in **each** of xterm, gnome-terminal / xfce4-terminal, and the
Linux virtual console (Ctrl-Alt-F3):

1. `TERM=$TERM ./jichi` — prompt renders, `/help` readable;
2. **type-ahead**: `--type-ahead`, start a turn, type during it — the echo must
   appear beside the working indicator and Enter must queue it;
3. **paste**: paste a three-line block into the prompt (bracketed paste on
   modern emulators, burst fallback on the VC — M156 covers both);
4. **resize** mid-turn, then again at the prompt;
5. `NO_COLOR=1` and `LC_ALL=C` runs — ASCII fallbacks, and (M257) the working
   line must still exist so type-ahead stays visible;
6. Ctrl-C mid-turn, Ctrl-C at an empty prompt twice, Ctrl-D.

**Expect to find:** the Linux virtual console is the interesting one — no
bracketed paste, limited UTF-8, 8 colours. That is a supported target
(`docs/ACCESSIBILITY.md`, the ASCII glyph fallbacks) and it has never been
exercised outside a pty harness.

### V6 executed, 2026-08-03 (M268): partial pass, and no VirtualBox needed

*The row was written as a VirtualBox matrix and a human-at-the-keyboard
procedure. It turned out to need neither — for the part of it that ran.*

**Two corrections to the row as designed.**

1. **No VirtualBox guest was needed for the modern-emulator cells, because the
   reference box *is* one of them:** Ubuntu 24.04 running XFCE on X11, with
   `xterm` (XTerm 390) and `xfce4-terminal` 1.1.3 — the desktop's stand-in for
   the row's `gnome-terminal` slot. The two guests the row *adds* (Debian 12 +
   XFCE, Ubuntu 16.04) still need VirtualBox, which needs nested virt this box
   does not have — **the same wall as V2e/V2f**, and worth stating once rather
   than discovering twice.
2. **It did not need a human either.** `tests/tools/xdrive` (new, M268) injects
   real keystrokes through X11's XTEST extension, `dlopen`ing libX11/libXtst at
   runtime so the project takes on no X build dependency. That turns V6 from a
   one-off manual pass into a repeatable one — which matters, because this
   document's own note is that the real-emulator path *"has never been exercised
   outside a pty harness"*, and an unrepeatable pass does not change that for
   long. `scripts/tier-v-terminals.sh` is the runner; `make xdrive` builds the
   tool, deliberately outside `smoke-tools`/`check-target`/`ci` (it needs a live
   X server and it takes the keyboard focus while it runs).

**Result: 24 checks, 24 pass, 0 fail** — the plan's six per emulator, both
emulators, with screenshots kept per check as evidence.

| check | xterm | xfce4-terminal |
|---|---|---|
| 1 prompt renders, `/help` readable | pass | pass |
| 2 type-ahead echoed + queued; reaches call 2 as `[operator]` | pass | pass |
| 3 real 3-line clipboard paste arrives intact, submits as one line | pass | pass |
| 4 mid-turn resize + resize at the prompt (real SIGWINCH) | pass | pass |
| 5 ASCII fallbacks under `NO_COLOR=1` + `LC_ALL=C` | pass | pass |
| 5b/5c/5d M257's working line: absent without `--type-ahead`, present with it, typing visible on it | pass | pass |
| 6 Ctrl-C mid-turn; Ctrl-C ×2 then Ctrl-D at the prompt | pass | pass |

**What the real emulators confirmed that a pty could not.** The paste check is
the one worth the tooling: a genuine X CLIPBOARD selection, pasted with a real
Ctrl-Shift-V, arrives as `ESC[200~line1\nline2\nline3 ESC[201~` — so M156's
bracketed-paste assumption is not an assumption any more, on either emulator.
Likewise the resize is a real `XResizeWindow` and the Ctrl-C is a real keyboard
interrupt, not a byte written into a pty.

**Not covered by the M268 run, stated rather than smoothed over:**

- **The Linux virtual console — the row's most interesting cell — did not run.**
  `/dev/ttyN` is `root:tty` here, this user is not in `tty`, and `openvt` needs
  root. It needs a machine where the operator has console access.
  **→ Closed at M274 (below): 11/11, in a QEMU guest rather than on a
  workstation.**
- **Debian 12 + XFCE and Ubuntu 16.04** need VirtualBox: nested virt, blocked.
  **→ Debian 12 + XFCE closed at M275 (24/24). Ubuntu 16.04 is BLOCKED by a
  kernel panic, with Debian 9 substituted for the "old stack" role — both
  recorded below. No VirtualBox was needed for either.**

### V6 desktop cells executed, 2026-08-04 (M275): the row closed

Both remaining cells ran under plain QEMU/KVM with a **real X server on a
virtual framebuffer** (Xvfb) — the row's third correction, after "no VirtualBox
needed" (M268) and "the console needs a driver, not a human" (M274). The point
of the row is *real terminal emulators* rather than a pty; Xvfb is a real X
server, so the emulators, XTEST injection, the CLIPBOARD selection and genuine
`XResizeWindow` resizes are all authentic. No check asserts on pixels, so the
absent physical display costs nothing.

| cell | stack | result |
|---|---|---|
| reference desktop (M268) | Ubuntu 24.04, xterm 390 + xfce4-terminal 1.1.3 | 24/24 |
| virtual console (M274) | the kernel's own emulator, cold VC | 11/11 |
| **Debian 12 + XFCE** | xterm 379 + xfce4-terminal (VTE 2022) | **24/24** |
| **Debian 9 (2017)** — substitution, see below | xterm **327** + VTE **0.46.1** | **24/24** |
| Ubuntu 16.04 — the named target | kernel 4.4 | **BLOCKED** |

**83 assertions, four stacks spanning 2017–2024, zero jichi defects.** The old
stack was where a finding was most plausible and none appeared: bracketed paste
predates VTE's stabilisation of it, so VTE 0.46 was the likeliest place for
M156's paste framing to break; and M257's narrow working-line behaviour, asserted
four ways, holds under an older terminfo.

**Ubuntu 16.04: blocked, and the reason is a new datum.** Its 4.4 kernel panics
in `text_poke` under KVM on this 2026 host — the same signature M272 found on
Debian 9's 4.9 kernel, except **the `-cpu IvyBridge` workaround does not rescue
4.4** (IvyBridge, qemu64, Nehalem and core2duo all panic). M272's workaround has
a floor: somewhere between 4.4 and 4.9, old-kernel-on-new-silicon stops being
maskable by CPU model. A QEMU/kernel fact, not a jichi one — and the reason this
cell is recorded as blocked rather than ticked off.

**Debian 9 substitutes for it explicitly.** Stretch is a 2017 X terminal stack
(five years older than the Debian 12 cell) on an already-provisioned guest, so
the cell's *intent* — jichi's TUI against a much older emulator generation — is
exercised. It is not the named distro, and that difference is recorded here
rather than smoothed over.

**Recipe, for repeating either cell:** in the guest, `apt install xvfb xterm
xfce4-terminal xfwm4 x11-utils imagemagick python3-tk`, then
`Xvfb :0 -screen 0 1400x1000x24 &`, `xfwm4 --daemon &`, `make jichi xdrive
smoke-tools`, and `DISPLAY=:0 scripts/tier-v-terminals.sh`. Old guests need
`-cpu IvyBridge` (4.9) and 4.4 needs something QEMU does not offer.

### V6 virtual-console cell executed, 2026-08-04 (M274): 11/11

*The cell the row calls most interesting, and the one a pty harness cannot
answer: a pty gives you a terminal emulator we wrote, while the question is what
the KERNEL's emulator does.*

New tooling, both outside `ci`/`check-target`: **`tests/tools/vtdrive`**
(`make vtdrive`) types via `/dev/uinput` and reads `/dev/vcsa<N>` — the kernel's
own screen memory — reusing ptydrive's `pd_core` script language, with the
console keymap read from the kernel (`KDGKBENT`, inverted) rather than assumed;
and **`scripts/tier-v-console.sh`**, a sibling of `tier-v-terminals.sh` running
the plan's six checks plus a self-test and both sides of type-ahead. All eleven
assertions pass on a **cold** console:

| check | result |
|---|---|
| 0 self-test: keystrokes reach the console and read back | pass |
| 1 prompt renders on the kernel emulator, `/help` readable | pass |
| 2 / 2b type-ahead echoed + queued, reaches call 2 as `[operator]` | pass |
| 2c without the flag, the typing is dropped (M257's default) | pass |
| 3 / 3b three-line **LF burst** intact, submits as ONE line (M156) | pass |
| 4 SIGWINCH mid-turn, turn still completes | pass |
| 5 / 5b `LC_ALL=C` + `NO_COLOR=1`: completes, no UTF-8 glyph on screen | pass |
| 6 Ctrl-C mid-turn, Ctrl-C ×2 + Ctrl-D → exit 0 | pass |

**Two corrections to this row as designed.** (1) It runs **in a QEMU guest**,
not on the operator's desktop: six manual host runs hit races with the desktop's
input stack, while a guest has real VCs, nothing competing for input devices, no
display to commandeer and no root on a workstation — and the kernel's console
emulation, the actual subject, is identical. Debian's *cloud* kernel ships no
`uinput`, so install a full kernel and boot it with QEMU's `-kernel`. (2) The
`setfont(8)` geometry change is deliberately **not** covered: the console font is
shared, and a test runner should not reshape the operator's consoles;
`TIOCSWINSZ` supplies a real SIGWINCH by the real path, which is what check 4
asserts.

**Cost, honestly: seven instrument defects, zero jichi defects.** A US keymap
assumption on a German console; discarded stderr; `${VAR}` expansions that POSIX
will not treat as prefix assignments; an `expect` waiting for a message before
the keypress that causes it; an absence check that passed against a blank
screen; a tree shipped without `sync`; and finally the one that cost most of the
runs — **`VT_ACTIVATE` makes a console active but does not ALLOCATE it**
(opening `/dev/ttyN` does), so keystrokes vanish behind five affirmative return
codes, and a `vhangup()` added for hermeticity then invalidated the descriptor
holding the allocation. Full story: docs/ANECDOTES.md #31. The cell now carries
its scepticism in two places — the runner's self-test and the tool's internal
warm-up handshake — so the next operator gets a tooling verdict before a product
verdict.

**Two harness defects were found by disbelieving the harness, and both are the
kind `docs/TEST_INTEGRITY.md` is about — in the direction that gets less
attention, a test reporting a failure that never happened:**

1. The first no-colour check asserted the working line must exist under
   `NO_COLOR`, and it failed. The code was right and the *test* was wrong: M257
   set `ctx.indicator = !accessible && (color || (type_ahead && is_tty))` and
   recorded the narrowness deliberately — *"WITHOUT type-ahead a NO_COLOR
   session renders exactly what it rendered before"*. Asserting only one side
   would have reported a regression against a session behaving exactly as
   designed, and "fixing" the code to satisfy it would have destroyed the
   property M257 chose. The check is now **two-sided**: absent without the flag,
   present with it.
2. The xfce4-terminal paste check failed while the paste had in fact worked
   perfectly — `script`'s buffered capture was lost when the session was killed,
   leaving a 0-byte file, and the assertion read that as "the paste never
   reached the model". The mock's captured request had all three lines. Fixed by
   `script --flush` **and** by asserting on the captured request (what the model
   received) rather than only on the render (a second-hand witness).

Each check was also shown able to fail: the paste check reports truncation when
the clipboard is cut to one line, verified by running it that way.

### Priority order (do them in this sequence)

| Order | Row | Cost | Answers |
|---|---|---|---|
| 1 | **V1** cgroup ceilings | 30 min | the RAM floors LOW_MEMORY.md asserts |
| 2 | **V0** aarch64 user-mode | 20 min | does the aarch64 binary actually run |
| 3 | **V2a** CentOS 7 container | 1 h | oldest toolchain; gcc 4.8 C89 truth |
| 4 | **V5** Alpine/musl | 30 min | the malloc_trim probe decision |
| 5 | **V3** big-endian | half a day | the M136 guard — never yet exercised |
| 6 | **V4** 32-bit | 1 h | word-size assumptions |
| 7 | **V2e/f** whole VMs | half a day | kernel vintage + a 256 MB machine |
| 8 | **V6** VirtualBox terminals | half a day | the TUI on real emulators + the VC |

Rows 1–4 are a single evening and cover most of the risk. Row 5 is the one with
a real chance of finding a bug.

### The per-run runbook (identical everywhere, so rows compare)

Run this on **every** target — V-tier or physical — and paste the output into
the results table. Differences only mean something if the procedure is the same.

```sh
# 0. identity (goes in the row verbatim)
uname -a; cat /etc/os-release | head -2
gcc --version | head -1; ldd --version 2>&1 | head -1
nproc; free -m | head -2

# 1. configure + build, both flavours
make clean && make info                 # record which probes resolved
time make WERROR=1                      # record wall-clock: this IS the CPU signal
make SIZE=1 && size jichi && ls -l jichi

# 2. the portable gate (no python3 needed)
JC_SMOKE_TIMEOUT_MULT=<see below> make check-target

# 3. footprint
/usr/bin/time -v ./jichi --version 2>&1 | grep -E "Maximum resident"
/usr/bin/time -v ./jichi doctor  2>&1 | grep -E "Maximum resident"

# 4. offline surfaces (no network, no key)
./jichi doctor; ./jichi context; ./jichi map | head -20; ./jichi describe >/dev/null

# 5. optional, only with a reachable model
./jichi -p "reply with OK" --output json
```

**Choosing the multiplier:** time `make WERROR=1` and divide by threadwork's
time. Round up to an integer and use that for `JC_SMOKE_TIMEOUT_MULT`. Record the
value in the row — a suite that needed `8` is itself a result.

### What is a finding, and what is not

| Observation | Verdict |
|---|---|
| a C compile error or `-pedantic` warning on an old gcc | **finding** — fix the code |
| `make check-target` needs a tool the target lacks (`timeout`, `time -v`) | **finding** — the portable-gate claim is too strong |
| a PTY driver times out under user-mode QEMU only | **not a finding** — emulation artefact; confirm on V2e before believing it |
| RSS higher than LOW_MEMORY.md predicts | **finding** — but re-measure with `SIZE=1` before writing it up |
| a timing driver fails without `JC_SMOKE_TIMEOUT_MULT` | **not a finding** — that knob exists for this (M220) |
| a timing driver fails *with* a generous multiplier | **finding** — a real stall or a deadline that does not scale |
| the index cache misread across endianness | **serious finding** — silent wrong answers (see V3) |
| an OOM kill leaving orphan children | **finding** — cleanup path, not a memory-size issue |

### Results: rows executed 2026-08-02 (M265/M266)

Run on the 4.9 GB / 3-core reference box, Ubuntu 24.04. Same runbook per row.

| Row | Target | Result |
|---|---|---|
| **V1** | cgroup RAM ceilings | **floors measured**: turn 2 MB · doctor 5 MB · unit suite 16 MB · smoke tier 32 MB |
| **V0** | aarch64, static musl, no curl (zig build, qemu-aarch64 exec) | **PASS** — 9,693 checks / 0 failures (8.4 s emulated); binary runs |
| **V3** | **s390x big-endian**, static musl (zig build, qemu-s390x exec) | **PASS** — 9,693 checks / 0 failures (17.3 s emulated) |
| **V4** | x86 32-bit (ILP32), static musl | **PASS** — 9,693 checks / 0 failures; binary runs |
| **V5** | Alpine 3.20 / musl, gcc 13 (container) | **PASS** — probe correctly reports no `malloc_trim`; 1 finding |
| **V2a** | CentOS 7: gcc 4.8.5, glibc 2.17, make 3.82, libcurl 7.29, git 1.8.3.1 | **PASS after 3 fixes** — 9,669 checks / 0 failures under `WERROR=1` |
| **V2e** | Debian 12 VM, 256 MB, 1 core (threadwork, KVM) | **PASS (M272)** — gcc builds the tree *inside* the 256 MB ceiling; gate green (9,722 + 94 drivers, mult 2); took 7 guest runs to get there — see below |
| **V2f** | Debian 9 VM, 512 MB, kernel 4.9, git 2.11, libcurl 7.52 (threadwork, KVM `-cpu IvyBridge`) | **PASS (M273)** — 3 product fixes proven in-row (`git_stash` pre-2.13, worktree leak pre-2.17, the `Expect: 100-continue` second-per-call) + 4 rig fixes (3× dash 0.5.8, plus mockmodel's unscaled self-watchdog — the actual cause of M272's "wedge"); needs `TIERV_CPU=IvyBridge` (4.9 panics under `-cpu host` on 2026 silicon) |
| V6 | terminal reality | **PARTIAL PASS (M268)** — 24/24 checks in xterm + xfce4-terminal on this desktop; the virtual console and the other two guests blocked (below) |

**The V3 row did what it was added for.** `jc_index_endian_tag` returns `be` on
s390x and `le` on x86-64/aarch64 (verified with a standalone probe), so
`test_endian_tag`'s `else` branch — asserting the `"be"` tag, **dead code on
every little-endian machine this project has ever run on** — executed for the
first time, and passed. The whole suite passing on a big-endian host also clears
the wider worry behind that guard: no float/`memcpy`/serialisation path in 9,693
assertions is byte-order dependent.

**Four defects found, all pre-existing, all in the V2a/V5 rows** (see ROADMAP
M265): a configure probe that compiled a different program than it claimed; a
Makefile that **would not parse** under GNU make 3.82, so jichi could not be
built on CentOS/RHEL 7 at all; a build failure against libcurl 7.29; and a test
that **segfaulted** on git < 2.5 because `JC_CHECK` records-and-continues, so a
null check was followed by a dereference.

**Cost:** about three hours, entirely on one desk, no hardware. Rows V0/V3/V4
need only `zig` (cross-build) plus `qemu-user-static` + `binfmt-support`
(execute) — no VM images, no boot.

### Recording results

One row per target in `docs/LOW_MEMORY.md`, **machine-stamped** in the style the
file already uses (`Measured on …, date, milestone`) — never overwriting an
existing row, because "it passes on a small machine" and "it passes here" are
different claims (the M259 lesson). One `docs/analysis/` write-up per tier in the
house style: predicted → observed → finding vs documentation fix.

## Portability risks: one retired, two inert, one open ([probed] where noted)

These narrow the risk for the Linux tiers and bound what a shared component faces
on the Pico.

- **`char` signedness — retired [probed].** On ARM plain `char` is unsigned; on
  x86 it is signed — the classic C89 porting-bug class (`if (c < 0)`-style
  checks). The full suite passes under `make WERROR=1 CC="gcc -funsigned-char"
  test` (9176 checks, 0 failures). Clean against ARM's convention.
- **Alignment — inert by design [probed].** `jc_arena_alloc` rounds to
  `JC_ALIGN = sizeof(union jc_align)` (`{long; double; void *}`), correct on
  aarch64/armv7.
- **Endianness — inert by design.** Every target here is little-endian, and the
  index cache carries the M136 endian tag regardless (a foreign cache is rebuilt,
  never misread).
- **32-bit armv6/v7 — RETIRED [measured, M276].** The prediction below was
  right, and it is no longer a prediction: the armhf card ran on the Pi Zero 2 W
  (`armv7l`, `LONG_BIT=32`, Raspbian 13) and **`make check-target` is green —
  9,731 unit checks + 95 smoke drivers, at a measured `JC_SMOKE_TIMEOUT_MULT` of
  26** after a 101 s `-Werror` build that raised no format or width warning.
  `SIZE=1` is **770 KB** (vs 1.05 MB on aarch64) and the RSS peaks are ~20%
  lower (**6.9 MB** `--version`, **13.1 MB** `doctor`); the unit suite and a full
  mock `--auto` turn both complete under `MemoryMax=256M`. So the
  `%lu`-with-casts convention, the `jc_size`/`long` boundary around
  `JC_READ_FILE_MAX`, index mtimes and the session store are all exercised where
  `long` is four bytes. The staged cross-builds had only proven arm32 *links*;
  this is the "one build + suite run on such a device" that sentence asked for.

  *The original reasoning, kept because it held:* the codebase is `long
  long`-free on every target that matters — the **only** `long long` is inside an
  `#if defined(__APPLE__)` block (`jc_platform_posix.c`, the `HW_MEMSIZE`
  sysctl), dead code on any Linux/ARM build.

---

## Timing-sensitive paths on slow CPUs

Two timing dependencies, both surfaced in the 2026-07 series, both of which slow
hardware stresses in ways a fast machine cannot:

- **The `input_pending` submit-vs-paste branch** (`jc_term.c:780`, M156): a
  newline submits only if no further input is buffered *in the inter-byte
  instant*. On a slow CPU under load, echo processing lags and that window changes
  shape. `paste.py` and `typed.py` pin both sides — run them early and repeatedly
  on tiers A and B. If `typed.py` flakes there, that is a **finding about the
  heuristic**, not about the test.
- **The in-suite-only e2e flake** (six hypotheses disproved; `run.sh` now
  self-classifies failures as in-suite-only vs also-alone) correlates with load on
  constrained hosts. Tiers A and B are fresh pressure environments — expect the
  classifier to finally collect its evidence there, and treat that as the point,
  not a nuisance.

**Prep task (do on threadwork, before the hardware):** add a `JC_E2E_TIMEOUT_MULT`
env knob to `tests/e2e/run.sh`, multiplying the per-driver 60 s timeout. On a Pi
Zero-class device the x86-calibrated constants produce false failures — but the
constants are *load-bearing on fast machines* (a tight timeout is what turns a
hang into a failure), so multiply at runtime rather than raising them. Small,
offline, and it unblocks running the suite on slow silicon at all.

---

## What to measure per host tier (honest fixtures)

Reuse `tests/measure/` unchanged, with the `CONTRIBUTING.md` fixture-honesty rules
(M198/M200: right size, right statistic, right **shape**):

| measurement | command | expected envelope (dev-machine baseline) |
|---|---|---|
| binary + startup | `tests/measure/startup.sh` | ~694 KB `SIZE=1` binary; ~10 MB `--version` peak |
| idle TUI | `idle_tui.py --secs 60 -- ./jichi` | flat RSS, 0.0 s idle CPU (~11.6 MB on x86 glibc) |
| per-turn slope | `soak.py --turns 60 --fixture-bytes 204800` | ≤ ~20 KB/turn (M199 level) |
| intra-turn peak | `soak.py --profile reads --fixture-bytes 1048576` | peak ≈ baseline + one file (M199) |
| store listing | `session_scan.py --files 243 --bytes 71680 --messages 200` | flat, ~14 MB peak (M202 level) |
| memory-pressure behaviour | `FAULT=1` build + `JICHI_FAULT_ALLOC_AFTER`, under `systemd-run -p MemoryMax=64M` | degrades **visibly** (M198's contract), never hangs |

ARM/musl numbers will differ (different libc retention, different page behaviour);
the **shape** assertions are the contract — flat is flat on any architecture.
Record per-tier results in `docs/analysis/` with the machine stamped, as M180/M181
did. A fixture realistic on x86 may be wrong on ARM (M200's lesson: right size,
wrong shape) — re-check the `--messages`/`--fixture-bytes` choices against each
board's real store.

---

## Cross-cutting recommendations

- **Build on threadwork, run on the target.** aarch64 static-musl cross-compiles
  today [probed] (`make HAVE_CURL= CC="zig cc -target aarch64-linux-musl" jichi`
  → an ~11 MB static ARM64 ELF, zero `WERROR=1` warnings). So a 64-bit Pi never
  needs a toolchain; `scp` a static binary. Execution is unverified here (no
  `qemu-aarch64` on the reference machine) — running it is the **first five
  minutes of tier B**. The 8 GB x86 box builds natively.
- **The no-curl core is a real fallback** [probed]: `make HAVE_CURL= test` →
  9176 checks, 0 failures. M189's note that this build had "drifted" is stale;
  retire the repair-candidate entry. A jichi that only shells out to device
  scripts (no model networking) is buildable — relevant for an offline bench.
- **`doctor` / `doctor --live` is the acceptance test on every Linux target** —
  it checks libcurl, per-server reachability, git, the PDF extractor, and (M167c)
  real tool calling. A green `doctor` is the fastest "is this environment sane"
  signal.
- **The correctness gate on-device is the offline suite, not the model.**
  `make WERROR=1 test` + `run.sh` + `run.sh --lite` are mock-based and
  network-free by design; they run identically in a basement. Live-model checks
  are exactly two and cheap: `doctor --live` and one `--auto` turn with
  `--budget-tokens 50k`. Token-hungry measurement drives (M196-style) belong on
  threadwork — the devices contribute *hardware* variance, not model variance.
- **Config on the devices is two files:** copy `~/.jichi` and `~/.jichi.env`
  (both 0600), same as any machine. Tier delta: add `"lowResource": true` only on
  a ≤2 GB board (tier B with 1–2 GB), never on the 8 GB box.
- **Never test robotics happy-paths only.** The withhold-the-heartbeat and
  refuse-but-`stop_all` tests are the ones that justify the whole kinetic
  architecture; the plan fails if they are skipped for time.

## Explicitly out of scope for the first pass

- A jichi port to any RTOS or bare-metal target (tier C's decision).
- Driving a motor before the reflex watchdog is proven (tier B rung 3).
- Running a local model on the 8 GB box (point at HRZ instead).
- Anything on threadwork as a *target* — it is the build host and main dev box,
  not a constrained device.

## Prep tasks (on threadwork, before touching hardware)

1. ~~`JC_E2E_TIMEOUT_MULT` in `run.sh`~~ — **done (M220)**, and the smoke tier
   has its own `JC_SMOKE_TIMEOUT_MULT` falling back to it. Every runbook below
   sets it.
2. ~~Retire M189's "no-curl build drifted" note~~ — **done**, [probed] false.
3. Confirm the aarch64 static binary actually *executes* — **this is now Tier V
   row V0**, a twenty-minute job on threadwork rather than a prerequisite you
   need a board to satisfy.
4. **New:** run Tier V rows 1–4 (an evening) before scheduling board time. They
   retire most of the portability risk, and a failure there is much cheaper to
   diagnose on threadwork than on a Pi over ssh.

## Deliverables from executing this plan

1. Real-hardware footprint numbers folded into `docs/LOW_MEMORY.md`, machine
   stamped.
2. A `docs/ROBOTICS.md` update: sim → first real hardware, with the
   withhold-heartbeat and refuse-`stop_all` results.
3. A short `docs/analysis/` write-up per tier, in the honest house style — what
   was predicted, what happened, what was a real finding vs a documentation fix.
4. **From Tier V:** a machine-stamped row per target in `docs/LOW_MEMORY.md`
   (including the three measured RAM floors from V1, which replace the current
   design-intent tiers), and a verdict on the M136 endian guard — the one
   invariant in the codebase that no machine on this desk can exercise.
5. The tier C conclusion recorded as a decision, not a failure: **the Pico is
   jichi's reflex layer, not its host** — with the shared-line-protocol
   experiment's result attached.
