# What this bench can still settle — a measured inventory and a ranked plan

*2026-08-16. Every state below was **measured today**, not recalled. The point of
the exercise is to separate "not done" from "cannot be done here", because the
two have been quietly conflated in the register.*

## 1. What is actually connected, right now

All six were probed in one sweep this afternoon; all six answered.

| Target | Reached by | Kernel / arch | Cores | RAM | Axis it uniquely holds |
|---|---|---|---|---:|---|
| Raspberry Pi 400 | ssh over the LAN | 6.18 aarch64 | 4 | 4 GB | Cortex-A72, the *comfortable* board |
| Arduino UNO Q | ssh over the LAN | 6.16 aarch64 | 4 | 4 GB | a vendor board image, Docker competing for RAM |
| Lenovo TB336FU / Termux | ssh to a forwarded local port | 5.15 android13 aarch64 | 8 | 7.6 GB | **bionic**, on-device toolchain, non-FHS `$PREFIX` |
| Lenovo / proot Debian 13 | ssh to a forwarded local port, uid 0 | PRoot aarch64 | 8 | 7.6 GB | **glibc on an Android kernel**; uid 0 |
| **Pi Zero 2 W (armhf card)** | ssh over USB gadget net | 6.18 **armv7l** | 4 | **425 MB** | **the only 32-bit target, and the only MINIMAL tier** |
| Archos 101b Copper | adb over USB | Android **4.4.2** armv7 | — | 965 MB | the no-`/bin/sh`, no-`/tmp` extreme |

*The concrete inventory — each target's account, address or device serial — is
machine-local and deliberately not in the repository, for the reason
`scripts/tier-b-device.sh` takes its host as a required flag rather than a
default: a coordinate baked into a committed file is unusable on every host
but the one that wrote it, and is a map of somebody's desk to everyone else.*

**Virtualisation is fully enabled and was not before:** `qemu-system-x86_64`,
`qemu-system-aarch64`, `qemu-img`, `qemu-aarch64-static` all present,
**`/dev/kvm` writable**, 221 GB free, and four `tier-v-*.sh` rigs in the tree.

Two facts worth pulling out of that table:

- **The Pi Zero 2 is currently carrying its armhf card.** That is `LONG_BIT=32`
  and 425 MB presented — the 32-bit word size *and* the sub-512 MB resource tier
  in one box, live, with no setup. Its recorded row (M276, multiplier 26) was
  measured against `threadwork`'s 4.00 s reference and has never been
  re-anchored on this bench. Swapping to the aarch64 card is a **physical** act
  and therefore needs a human; the 32-bit row does not.
- **Both Android devices are attached at once.** The 4.4.2 Archos is the only
  place the `/bin/sh`-absent and `/tmp`-absent findings live.

## 2. The one row this system can close that nothing else can

### BSD — the only "never compiled" row that is not hardware-blocked

`PLATFORMS.md` carries four non-verified rows. Three are blocked on hardware
nobody here owns (macOS: no Mac; WSL2: no Windows; *a phone*: both Android
devices are tablets). **The fourth is BSD, and it is blocked on nothing** —
QEMU, KVM and the disk are all present.

It is also, by some distance, the **highest-information row remaining**, because
it is the first **non-Linux kernel** this project would ever have run on, and
jichi's portability claim is POSIX, not Linux:

| What it would exercise | Why Linux never tests it |
|---|---|
| `/proc/self/status`, `/proc/self/stat`, `/proc/self/exe` — four source files | FreeBSD does not mount procfs by default; these paths simply are not there |
| A **fifth libc** (FreeBSD libc) | glibc, musl, bionic and uClibc are all Linux libcs |
| `-std=c89 -pedantic` against FreeBSD headers | the M459 dialect probe has only ever chosen between glibc and bionic |
| `HAVE_MALLOC_TRIM`, `HAVE_CLOCK`, `HAVE_VSNPRINTF` probes | every probe result to date is from a Linux libc |
| `pkg-config` discovery of a non-Linux libcurl | — |

The register's own words are that "anything Linux-shaped that leaked in
(procfs, `/proc/self/statm` for the memory watchdog) simply degrades. **Untried.**"
Four files read `/proc`. *Degrades* is a prediction, and the row is what turns it
into a measurement.

**Shape of the work.** `tier-v-vm.sh` is row-keyed and Debian-specific, so this
needs a sibling rig: same download → seed → boot → provision-over-ssh →
identical in-guest runbook → `results-*.txt` shape, and the same exit-code
contract including *3 = never reached userspace*. FreeBSD first (best-documented
VM images, `pkg` carries libcurl); OpenBSD second if the first row is cheap.

**Correction to an earlier draft of this plan, which claimed
`scripts/tier-v-guix.sh` as the precedent: no such script exists.** M458's Guix
row was driven by hand from a committed image definition
(`docs/plans/2026-08-guix-bench-system.scm`) booted under KVM with sshd and a
serial console. So the precedent for *reproducibility* is real — commit the
machine definition so the row can be rebuilt — but the precedent for a *rig
script* is `tier-v-vm.sh` alone. The false claim was written into this very
plan by its author while documenting the habit of writing plausible specific
falsehoods; it is corrected here rather than quietly deleted, because the
correction is the more useful record.

**Predictions to record before running, so the row can refute them:** the build
succeeds with zero diagnostics; `HAVE_MALLOC_TRIM` probes absent; the memory
watchdog degrades rather than crashing; `doctor` reports a machine profile with
no RAM figure. Any of those being wrong is the row paying for itself.

## 3. What the live hardware can settle cheaply

Ranked by information per hour, all runnable today with `scripts/tier-b-device.sh`.

1. **Pi Zero 2 armhf, re-anchored.** The only 32-bit target and the only
   MINIMAL-tier device, live now. Its multiplier is stale against a bench that
   no longer exists. One `tier-b-device.sh` run.
2. **Arduino UNO Q, re-anchored.** Same argument, weaker: M282 is green and the
   board adds a vendor image rather than a new axis. Cheap because it is
   already reachable.
3. **A five-device concurrent fleet run.** `fleet-run.sh` has only ever driven
   **two** devices at once. Five would exercise the thing a fleet is for and the
   thing that has already bitten once: a shared model server divides its context
   across parallel slots, so five clients get a fifth of the window each. This
   is the cheapest way to find the next capacity surprise.
4. **Archos 4.4.2** — only worth re-running when something changes in the
   `/bin/sh` or `$TMPDIR` handling, since its findings are recorded and stable.

## 4. What is blocked, and should stay recorded as blocked

Not doing these is the correct outcome, and saying so is the point:

- **macOS / Darwin** — no Mac. The register already notes the sharper fact: the
  one Darwin code path (`sysctl(HW_MEMSIZE)`) *could not have compiled* under
  this project's own flags until it was fixed and linted.
- **Windows + WSL2** — no Windows machine.
- **A phone** — both Android devices are tablets. The untested axes are thermal
  envelope and screen geometry, not the libc, and no amount of tablet work
  closes it.
- **zigodot's agent server port to `std.Io`** — not blocked on hardware but on
  *capability*: `MODEL_KNOWLEDGE.md` measures the available models at Zig 0.11
  dialect, 0/4 probes compiling under 0.16.

## 5. Development items this bench has made concrete

These are not device rows; they are what the rows *asked for*.

1. **A workload pressing a correctly declared context window.** The 7/7
   `unrelieved` measurement stands confounded: the limit was under-declared 8×
   (32 000 declared, `max_model_len` 256 000). Re-run with the true window and a
   workload that genuinely exceeds it — that is the evidence the (a) drop /
   (b) summarize / (c) refuse decision has never had.
2. **Gate rehearsal.** The open `DEFERRED` row, and this session is its best
   argument: a `py_compile` gate certified a conversion that had not happened,
   and a `command -v` gate certified a toolchain that could not run. jichi
   already runs verifiers and journals their exit codes; what is missing is
   running the gate against the *pre-change* tree once and recording that it
   was red.
3. **Ship the diff as a headless run's primary artifact.** 3/3 agent runs
   produced a false claim in prose and a correct change in the diff.
4. **Ask jichi's own version of the zigodot question:** which targets does
   `make ci` never compile? zigodot's agent server rotted through a toolchain
   migration because nothing in its gate ever built it, and its own
   `conformance.zig` documents the identical decay. Twice in one repository
   makes it structural, and the question generalises.

## 6. Recommended order

1. **FreeBSD row** — the only unblocked never-compiled platform, first non-Linux
   kernel, four real code paths, a fifth libc. Highest information, and the
   thing this machine can uniquely do.
2. **Pi Zero 2 armhf re-anchor** — cheapest real row, the only 32-bit and only
   MINIMAL-tier target, live right now.
3. **Five-device fleet run** — first genuine multi-device concurrency test.
4. **The correctly-declared-window measurement** — unblocks a three-way design
   decision that has never had a valid instance.
5. Arduino re-anchor, and OpenBSD if FreeBSD proved cheap.

## 7. What this plan does not claim

- That the re-anchored rows will find anything. They mostly confirm; the value
  is a comparable denominator, and that is worth stating as a modest claim
  rather than dressed up as discovery.
- That BSD will work. The honest prediction is a build that needs one or two
  small fixes, and the row is worth running whether or not that prediction
  holds.
- That the device list is stable. Five of the six are on cables or a LAN that
  changed twice during this session; every row must re-measure reachability
  rather than trust this table.
