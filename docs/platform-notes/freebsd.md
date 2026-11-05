# FreeBSD — the porting history

*Moved out of [`PLATFORMS.md`](../PLATFORMS.md) at M780 so the matrix stays short and the [install page](../install/) stays a lean walkthrough. This is the developer/porting record — the defects each row found and how they were fixed — kept verbatim; `git log -S` on any phrase names the commit that wrote it.*

## The FreeBSD row, and the seven defects it found (2026-08-16)

The row's own prediction was that Linux-shaped code "simply degrades". Three of
the seven say otherwise: they are **non-POSIX symbols that four Linux libcs made
look portable**. glibc, musl, bionic and Darwin all expose them; FreeBSD hides
them behind `__BSD_VISIBLE`, and this tree compiles `-D_POSIX_C_SOURCE=200112L`.

| Symbol | Where | What it cost |
|---|---|---|
| `_SC_NPROCESSORS_ONLN` | `src/platform/jc_platform_posix.c` — **product code** | blocked the build outright; now `#ifdef`-guarded, `jc_cpu_count` degrades to 1 |
| `INADDR_LOOPBACK` | `tests/tools/mockmodel.c` | blocked the **entire** smoke tier (no mock server, no drivers) |
| `SIGWINCH` | `tests/tools/ptydrive.c` | blocked the smoke tier; costless to guard — the TUI polls `TIOCGWINSZ` and never relied on the signal |

Three more are **tests that assumed Linux**, not defects in jichi:

- `tests/test_proc.c` asserted the procfs-backed RSS watchdog returns > 0.
  FreeBSD mounts no `/proc`, so it returns 0 — which *is* the graceful
  degradation this row predicted. The fix already existed and was unused:
  `jc_have_proc_rss()`, a capability probe whose comment (M326q) says asking
  "is this Linux?" is the wrong question. The assertion now runs behind it.
- `tests/smoke/doctor.sh` check 6 read doctor's **correct** unverified-platform
  warning as a failure — in a message asserting the platform "IS the verified
  one". It now branches on `uname -s`.
- BSD `wc -l` pads its output with leading blanks, so a **string** comparison of
  two equal counts failed while printing both as `26`. Fixed with `tr -d` and a
  numeric `-eq`, which is what the comparison meant in the first place.

And one is a **genuine resource leak that Linux structurally cannot observe**:
five drivers call `mm_start` without a matching `mm_stop`, and `smoke_cleanup`
reaped only the most recent mock. GNU `timeout` returns when its direct child
exits; FreeBSD's waits for the whole process group, so a driver that finished
its work in **2.19 seconds** returned `124`. The leak had existed on Linux the
entire time and no Linux run could ever have shown it. `_smoke.sh` now tracks
every mock in `MM_PIDS` and reaps all of them.

Separately, a **performance** finding with no Linux symptom:
`tests/smoke/config_defaults_lint.sh` applied a regex with two unanchored `.*`
and nested optional groups to a 64 KB single line. GNU sed: 3 ms. BSD sed:
**27 minutes**, inside a `timeout 60` that could not kill it (same process-group
mechanism). Narrowing the input with `grep -o` first: 0.35 s. A portable script
is not just POSIX syntax — it is also not betting the run on one
implementation's backtracking.

**CLOSED at M465, and it was the harness.** `setup_keyfile` check 6 ("the key did
not reach jichi") failed inside the driver while every element passed when tested
separately — and that asymmetry was the clue, because a variable assignment
*prefixed to an external command* is exported correctly while the same prefix on a
**shell function** is not, on this platform:

| shell | does `FOO=bar f` put FOO in a child's environment? |
|---|---|
| dash (Linux `/bin/sh`) | **yes** |
| bash | **yes** |
| **FreeBSD `/bin/sh`** | **no** |

The check read `JICHI="$BIN" with_deadline 60 ./run.sh doctor`, and `with_deadline`
is a function. So `JICHI` never reached `run.sh`, whose `${JICHI:-jichi}` fell back
to a bare `jichi` that is not on `PATH`: `$out` was the 22 bytes
`exec: jichi: not found`, which contains no "api key" line — hence the failure
message with an **empty tail**, naming a cause ("the key did not reach jichi") that
the check cannot actually distinguish from "jichi never ran". Fixed by writing it
`with_deadline 60 env JICHI="$BIN" ./run.sh doctor`, verified on this platform, and
pinned by `smoke_lint` check 12 so the shape cannot return.

**Five more sites had it**, and they were worse than a failure: two lost `HOME`
isolation (so they would have run against the real `$HOME`), one lost a
fault-injection variable (so the fault never fired and the driver asserted
nothing), and two lost `TERM`/`LC_ALL`. All of them **passed** on FreeBSD while
testing something other than what they claimed. Full write-up:
[`analysis/2026-08-17-the-assignment-that-did-not-travel.md`](../analysis/2026-08-17-the-assignment-that-did-not-travel.md).

## Measurement log (moved from the PLATFORMS.md cell, M781)

- **M465 (2026-08-16)** — first run. `gmake WERROR=1` clean, `STD_DIALECT = c89
  (strict)`; **11,643 unit checks / 0 failures**; all four offline surfaces. First
  non-Linux kernel this project ran on. Needs `gmake` (FreeBSD's `make` is bmake).
- **M665 (2026-09-18) — DRIVEN**, the first live model call on this kernel. LM Studio
  loopback-bound on the host, reached over `ssh -R 1234:127.0.0.1:1234`. `google/gemma-4-12b`:
  a text turn returned `FREEBSD LIVE OK` in **4 s**; an agentic `read_file` turn returned
  `The pass phrase is BSD-PTY-42.` in **7 s**, rc=0 — request build, SSE framing, a native
  tool call, execution, and the second turn consuming the result. Smoke re-measured to
  **302 of 303 drivers, 1,727 checks** (from a 201-driver debt of 102); the one failure,
  `parallel_abort`, is measured not diagnosed. Five of six earlier failures were harness
  defects, not FreeBSD; two real defects (a roff `\e` escape mandoc needs, a ~150-column
  wizard notice) came out of it. Account:
  [`analysis/2026-09-18-freebsd-and-the-probes-i-built.md`](../analysis/2026-09-18-freebsd-and-the-probes-i-built.md).
- **M668 (release 0.9.2, commit `fb032ede`)** — the non-Linux confirmation the release rests
  on: `16 ok, 0 failed`, `smoke: OK (305 drivers, 1,742 checks)`, both live turns green
  (`TIER-V-1D0BDC`). Reproducible: `scripts/tier-v-bsd.sh --ref-secs <bench> --live-port 1234
  --live-model <id>`.
- **M683 (2026-09-20)** — regression check for five test-tooling fixes made for illumos:
  `14 ok, 0 failed`, `13,383 checks, 0 failures`, `smoke: OK (311 drivers, 1,782 checks)`,
  offline only (the Driven verdict rests on M665/M668).
- **M466 footnote** — at least one of the early 1,068 checks was hollow:
  `changelog_coverage_lint` used a GNU `\b` whose failure on BSD grep was silent. Fixed and
  banned tier-wide; a count was never a guarantee every check tested what it claimed.
- **M771 (2026-09-25)** — the `/proc`-key protection (M770) is Verified here via FreeBSD's
  `procctl(PROC_TRACE_CTL_DISABLE)`; `ps -e` is the read route, `proc_key_hidden.sh` green
  with the fix and red without it. See [`HARDENING.md`](../HARDENING.md) §2.
