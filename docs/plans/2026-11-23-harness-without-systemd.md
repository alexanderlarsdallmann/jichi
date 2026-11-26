# jichi-harness without systemd: a design

*Written 2026-11-23 by the host supervisor (Claude), on the operator's request: "jichi-harness seems to require
systemd, for now. What other options are there?", then "Yes, write the design note and DEFERRED row." Design only:
nothing here is built. Read at the harness-port drive's team head 4d77216c. Registered as
[`DEFERRED.md`](../DEFERRED.md#def-425).*

> **Who this is for.** Whoever ports `jichi-harness` to a host without systemd (a BSD, macOS, or a Linux without it,
> such as Alpine or Void), and the operator, who decides when that starts. If you only want to run a drive, read
> [`HARNESS.md`](../HARNESS.md).
>
> **When it starts.** Not on a date. The decision of 2026-11-12, amended on 2026-11-17
> ([`DECISIONS.md`](../DECISIONS.md#dec-2026-11-12-harness-linux)), keeps the harness a Linux-with-systemd program
> until someone needs to run a drive on another host. This page is ready for that day.

## 1. What systemd does for the harness today

Four jobs, all in five files.

| Job | Where | What systemd provides |
|---|---|---|
| Timers | `src/harness/jc_hp_units.c` (`push units`), `jc_hp_watchunits.c` (`watch units`) | generated user `.timer` units, the schedule from the config (`host.pushOnCalendar`); `Persistent=true` |
| Long-running services | the host's own units, named in the config (`drive.loopUnit`) | restart, start at boot (with lingering) |
| Detached gate jobs | `jc_hp_watchgate.c`, template in `jc_hp_gateunits.c` | `systemd-run --user --collect --nice=19`, unit `jhp-gate-<sha8>` |
| State queries | `jc_hp_watchgate.c`, `jc_harness_page.c` | `systemctl --user is-active`, `list-units`, `is-failed` |

Notes on each:
- **Timers.** `Persistent=true` means a tick missed while the machine was off runs once when it is back.
  `AccuracySec=30s` keeps the ticks close to their minute. Each drive's schedule is configured, so two drives on one
  host never push in the same minute.
- **Detached gate jobs.** The watcher starts the gate as a transient unit. The unit outlives the watcher that started
  it, has a name another check can ask about, runs at nice 19, and is stopped together with every process under it.
- **State queries.** "Is a gate running?" and "is a team smoke running?" decide whether the watcher may start a gate:
  two gates at once, or a gate beside a team smoke, would measure each other's load.

## 2. The seam that already exists

The watcher's and the page watcher's systemd calls are already **commands named in the config**, with systemd's as
the defaults. This is how the drive's tests run them with stand-ins, and it is most of the seam a second backend
needs.

- The gate watcher: `host.unitState` (default `systemctl --user is-active -q`), `host.unitList` (`systemctl --user
  list-units --state=active,activating --no-legend jhd-gate-* jhd-ci-* jhp-gate-* jhp-ci-*`), `host.unitRun`
  (`systemd-run --user --collect --nice=19`), and `pgrep -f tests/smoke/`, each also a flag of `watch gate`
  (`--unit-state`, `--unit-list`, `--unit-run`, `--pgrep`).
- The page watcher: `pageUnitState` (default `systemctl --user is-active <unit>`) and `pageUnitFailed`
  (`systemctl --user is-failed -q <unit>`). The H12 work in progress (park, resume, restart) adds `pageUnitRun` and
  `pagePidList` in its test driver's contract (`tests/smoke/harness_page.sh`), the same way.

What is **not** behind a command yet:
- the timers: `push units` and `watch units` write systemd units and nothing else;
- the team-smoke check: after `pgrep`, the watcher reads `/proc/<pid>/cwd` to see whether the smoke runs in the
  team's tree.

## 3. What is Linux beside systemd

Moving to another host changes these too. The 2026-11-12 decision already names the fix for the first one.
- `/proc/self/exe`, 36 mentions in the harness's sources: the harness hands its own path to git as the credential
  helper. The fix is one configured absolute path, checked against `/proc/self/exe` where that exists, in one
  function instead of the copies.
- `/proc/<pid>/cwd`: the team-smoke check above.
- `/proc/<pid>/environ`: named where the harness protects its memory from a same-user reader; the protection call
  itself is per platform already ([`HARNESS.md`](../HARNESS.md), "What it is").
- A container is a separate question: Mode A (the normal user account) runs without one; the container modes need a
  runtime the host has.

## 4. The options

- **A. Keep systemd** (the current decision). Reliable and already built. It excludes the BSDs, macOS and Linux
  without systemd.
- **B. One backend per service manager**: OpenRC, runit or s6 on Linux; rc.d and cron on the BSDs; launchd on macOS.
  Each fits its system natively. Each needs its own probe, tests and a driven host row; the 2026-11-12 decision
  rejected "one call per system" for this reason.
- **C. One portable POSIX backend** (section 5). It covers every POSIX host at once. It rebuilds by hand what systemd
  gives for free.
- **D. A built-in supervisor**: one `jichi-harness` process schedules the ticks and supervises the loop and the gates.
  It needs nothing beyond POSIX, but it is the most code, and when it dies, everything under it stops.
- **E. Run the harness host in a small Linux VM or container** on the other system. It works today with no code; the
  host is still Linux.

**Proposed:** keep A, use E if someone needs another host before the code is ready, and build C when the trigger
fires. systemd stays the default backend on Linux.

## 5. The POSIX backend, job by job

**Timers.** `push units` and `watch units` gain a cron form (for example `--cron`) that prints crontab lines instead
of units. The schedule needs one source that both forms are generated from: today it is an `OnCalendar` string, which
cron cannot read, so the config grows a neutral form (minutes past the hour, or an interval) that maps to both. cron
does not catch up a missed tick, so the tick does: it compares now with its own last-tick stamp (the push already
keeps `push-tick.state`) and runs once when one or more ticks were missed.

**Long-running services.** `drive` is already the loop. What starts it at boot is the host's own choice (`@reboot`
in cron where it exists, rc.d, launchd, OpenRC); [`HARNESS.md`](../HARNESS.md) would print the command for each. A
restart after a crash becomes a cron minute that starts the loop when its lease is missing or stale.

**Detached gate jobs.** The harness detaches the gate itself: fork, `setsid` into a new session and process group,
nice 19, stdin from `/dev/null`, output to the gate's run log, and a **lease file** under the host directory holding
the process group id, the pid and its start time. Stopping it signals the process group. The lease is removed when the
gate ends; a lease whose pid is gone, or alive with a different start time, is stale and says so.

**State queries.** The lease files answer them: "is `jhp-gate-<sha8>` active" is "does its lease exist, with a live pid
of the recorded start time"; "list the active gates" lists the live leases under the prefixes. These become the
defaults of `host.unitState`, `host.unitList` and `host.unitRun` on a host without systemd: the commands the seam
already reads.

**The team-smoke check** cannot read `/proc/<pid>/cwd` there. Two ways, to decide when the work starts:
- the team's smoke run takes a lease of its own in the team tree, which the watcher reads (a change on the team's
  side, portable everywhere);
- or each platform's own call for a process's directory (one per system: the cost B was rejected for).

### What the POSIX backend must rebuild, said plainly

- **Killing a whole tree.** A process group holds the gate's children unless one calls `setsid` itself; a cgroup holds
  them all. The lease records the group; a child that escapes it is not stopped. Measure it on the gate's real
  children before relying on it.
- **Catching up after downtime**: the last-tick stamp above.
- **Logs that outlive a crash**: plain files, appended and flushed per line.
- **Starting without a login**: systemd user units need lingering (`loginctl enable-linger`); cron does not.
- **Limits beyond nice**: none today, so nothing to rebuild yet.

## 6. How it is built, when it is

1. A red-first cell per job, run with the seam's stand-ins on Linux first: the watcher already runs its cells with
   stub commands, so the POSIX defaults can be pinned before any other host exists.
2. The `/proc/self/exe` fix of the 2026-11-12 decision, in one function.
3. A rehearsal drive on one non-systemd host, driven (a model really called there): FreeBSD is the first candidate,
   since it is already Verified for jichi itself ([`PLATFORMS.md`](../PLATFORMS.md)).
4. Only then the harness's platform claim widens, row by row.

## 7. What this page did not check

- The 36 `/proc/self/exe` mentions include comments; the count of call sites to change was not taken.
- Whether every gate child stays in the gate's process group was not measured.
- No backend was built or run.

## Read further

- [`HARNESS.md`](../HARNESS.md): the harness, its modes and its limit.
- [`DECISIONS.md`](../DECISIONS.md#dec-2026-11-12-harness-linux): why it is Linux with systemd for now.
- [`DEFERRED.md`](../DEFERRED.md#def-425): this design's register row.
- [`reading/TAZUNA.md`](../reading/TAZUNA.md): the reading guide to the harness's source.
