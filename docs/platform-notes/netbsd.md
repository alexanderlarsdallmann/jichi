# NetBSD — the porting history

*Moved out of [`PLATFORMS.md`](../PLATFORMS.md) at M781 so the matrix stays short and the
[install page](../install/netbsd.md) stays a lean walkthrough. Developer/porting record.*

NetBSD **10.1** amd64 under KVM (`scripts/tier-v-netbsd.sh`). Booted from the vendor **live
image**, so there is no installer to drive — but the ssh key has to be typed at the serial
console, and every trap in that mechanism is in
[`analysis/2026-08-18-netbsd-first-row.md`](../analysis/2026-08-18-netbsd-first-row.md) (six rig
attempts; **zero product changes**).

## What it uniquely exercises

- **procfs.** It is the only non-Linux kernel that can run `tests/smoke/child_fds.sh`, the driver
  proving M472's descriptor fence (a model-issued shell inherits neither the run journal, nor the
  telemetry sink, nor the provider socket) — so that guarantee is verified on **two** kernels, not
  one. procfs is `noauto` in `/etc/fstab`; the rig mounts it and asserts *that the driver ran*.
- **GCC in base.** The other two BSDs are clang, so `WARN_OPTIONAL` is non-empty here for the first
  time off glibc, and unlike OpenBSD's clang it honours `-fstack-clash-protection` — making this the
  first non-Linux row where `harden_flags_lint` finds all three detectors discriminating and
  **verifies** rather than reporting "cannot verify".
- **`pkg-config` is required, not a nicety:** pkgsrc installs to `/usr/pkg`, which the base compiler
  does not search, so without it the libcurl probe answers no and jichi builds a networkless binary
  while `make info` prints only `HAVE_CURL =`.

## Measurement log

- **M480 (2026-08-18)** — first row. `gmake WERROR=1` clean in **7 s**, first try, c89 strict,
  `HAVE_CURL = yes`; **12,416 unit checks / 0 failures** (Linux 12,422 — a six-check environment
  gap); `smoke: OK, 209 of 209 drivers, 1,118 checks`; all four offline surfaces. Three drivers
  decline — the `FAULT=1` tier, covered by `make smoke-faults` on Linux. `pdftotext` installed, so
  the PDF shell-out path is exercised on a non-Linux userland (M482a).
- **M480 — DRIVEN, 2026-09-19, first try:** `ok - live turn answered over the reverse tunnel` and
  `ok - agentic turn: the model called a tool and reported TIER-V-B34D04`. Its single smoke failure
  is `parallel_abort` check 1 — *the same check and symptom FreeBSD has*: the mock records the
  parent's turn plus one `TASK_` request where Linux records two, so one fork-pool child does not
  reach it. Two independent BSD kernels reproducing it makes it a behaviour to diagnose, not a quirk.
- **M685 (2026-09-20) — fully green, the cleanest row in the matrix:** build 8 s (multiplier 2),
  `13,384 checks, 0 failures`, `smoke: OK (312 drivers, 1,795 checks)` — the whole tier, no
  exceptions. `child_fds` ran again. Also verified M685's bounded `pkg_add` by effect. Offline only,
  so the Driven verdict rests on the 2026-09-19 run.
