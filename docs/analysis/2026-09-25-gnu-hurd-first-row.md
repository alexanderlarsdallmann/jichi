# GNU/Hurd, the first row: nothing to report at build time, and what running it found

*2026-09-25, on the workstation (M767). The operator's decision on the twelve questions, item 4:
measure the POSIX gap in one `make -k` log before fixing anything, and GNU/Hurd next. This page is
that measurement: the log, then the unit suite, the smoke tier and two live turns. Nothing here
was fixed while it was measured. The one fix it led to is M768, and its effect is section 8.*

## 0. The answer first

- **`make -k` had nothing to report.** The whole tree built with **0 errors and 0 warnings** on
  the first try, in 14 s, with gcc 15.2.0 against glibc on the Hurd. Every configure probe
  answered as it does on Linux. So the POSIX gap the plan expected to meet at build time is not
  there. What it predicted, such as no `PATH_MAX`, jichi does not depend on.
- **The gaps are at run time**, and there were three:
  1. **The TUI froze before every model call**, in 10 drivers. Bisected to **M753**, the
     milestone earlier this session that takes the terminal hold on every turn. The cause is a
     Hurd behaviour, measured by a probe: a terminal `read()` in non-canonical mode with `VMIN=0`
     and `VTIME=0` **blocks** there when no byte is waiting, where POSIX says it returns at once.
     Fixed at M768 (section 8).
  2. **The unit suite's M461 check fails.** It gives a pipeline member 2 s to vanish after its
     process group is killed. On the Hurd the member leaves the process table, but `kill(pid, 0)`
     keeps answering *exists* for tens of seconds. That is the instrument disagreeing with the
     system, not a survivor.
  3. **`turn_scratch` is intermittent**: one solo run of three stalled, and two passed in 4 s.
     Not diagnosed.
- **Driven**: both live turns passed through a reverse tunnel, with the agentic phrase
  `TIER-V-F67C82` (`google/gemma-4-12b`).
- **Found along the way, and true on every platform:** eight smoke drivers need git and fail,
  rather than skip, without it. The Hurd guest had no git on its first run, and a failing git
  shim on Linux reproduces all eight.

## 1. The guest

| | |
|---|---|
| image | `debian-hurd-amd64-20260314.img.tar.xz` from `cdimage.debian.org/cdimage/ports/latest/hurd-amd64/`, **pinned by SHA-512** `0c6151e7…5a9f6cf0` (checked) |
| system | `GNU debian 0.9 GNU-Mach 1.8+git20260224-up-amd64/Hurd-0.9 x86_64`, Debian *forky/sid* |
| machine | QEMU 8.2.2 with KVM, 1 CPU, 2 GiB, the disk image as shipped (3 GiB root, 1.5 GiB free) |
| access | a key for this rig alone, written into `/root/.ssh` with `debugfs` on the image's root partition before the first boot, so there is no console to drive. `e2fsck -fn` was clean afterwards, and ssh was forwarded to `127.0.0.1:2224` |
| toolchain | `apt-get install build-essential libcurl4-openssl-dev pkg-config` (gcc 15.2.0), then `git` (2.55.0) for the second tier run |
| tree | `git archive` of `c957502c`, the pushed tip, not a working tree |
| rig directory | `~/.cache/jichi-tier-v-hurd/` on the workstation: the image, the key, every log below |

The timeout multiplier is the measured ratio of build times: 14 s in the guest against the
bench's 6.66 s gives `JC_SMOKE_TIMEOUT_MULT=3`.

## 2. `make -k`

`make -k > make-k.log`: exit 0, 184 lines, **0 `error:`, 0 `warning:`**. `make info` in the guest:
- the dialect is `c89 (strict)`;
- `HAVE_VSNPRINTF`, `HAVE_CURL`, `HAVE_MALLOC_TRIM`, `CLOCK_GETTIME`, `LSTAT`, `MMAP`, `WINSIZE`
  and `SOCKET_LIBS` are all found;
- `STREAMS_PTY = no`;
- the hardening flags are all accepted.

The FreeMiNT template this plan follows expected a gap list here. The honest result is that the
list is empty.

## 3. The unit suite

`make test`: **13,967 checks, 1 failure**, in 20 s. Here there are 14,028 checks; the difference
is the checks that run only on Linux. The failure:

```
FAIL tests/test_proc.c:353: kill((pid_t)victim, 0) != 0
```

That is M461's check that a timed-out capture kills the whole pipeline, not only its shell. Two
probes, rebuilt from the capture's own steps, show what the Hurd does:

| Probe | Linux | GNU/Hurd |
|---|---|---|
| `kill(-pgid, SIGKILL)` on the capture's group, then poll `kill(member, 0)` | gone within 50 ms | still *exists* at 5 s and at 45 s. Gone a few minutes later |
| the member in `ps -A` just afterwards | — | **not listed**: no process, and no zombie |
| `kill(pid, 0)` for a pid that never existed, a reaped child, an orphan after it exits | `ESRCH` each time | `ESRCH` each time, the same |

So on the Hurd, liveness is readable with `kill(pid, 0)` for every case the lease uses. A
group-killed process, though, is still reported for a while after it has left the process table.
The check's 2 s allowance is a Linux figure. The member does not survive to run: nothing listed
it. This goes to DEFERRED rather than being fixed, because the right fix is a decision: a
longer allowance, or asking the process table instead of `kill`.

## 4. The smoke tier

**First run, without git** (it was not installed): 113 drivers started, 9 failed.
`approval_keys` failed for the reason in section 5. The other eight fail rather than skip when
git is missing: `hint_record`, `undo_scope`, `attempt_tainted`, `attempt_guard`,
`improve_tainted`, `grade_cannot_run`, `verify_stuck_periodic` and `verify_consistency_periodic`.
None of them checks `command -v git`, which 13 other drivers do, and a git that always fails,
put first on `PATH` on Linux, fails all eight there too. **A test-tooling gap on every
platform**, recorded in DEFERRED.

**Second run, with git**: 327 of 342 drivers started in 82 minutes, when it was stopped. **10
failed, every one a driver that works a TUI prompt**: `approval_keys`, `view_key`,
`ghost_announce`, `paste_special`, `history_check`, `prefix_churn`, `privileged`, `kinetic`,
`turn_scratch` and `paste`. The 16 not yet run were the rest of the same group. The symptom was
the same each time. The TUI showed *working...* and the mock model received **no request**
(`prefix_churn`: *model calls=0*).

## 5. The TUI's freeze, bisected and measured

**Bisected.** `prefix_churn` from the current tree, run against three binaries built in the
guest:

| Binary | Result |
|---|---|
| M752 (`cb5b7c4d`) | **5 of 5**, 9 model calls |
| M753 (`b4458cb0`) | **0 of 5**, 0 model calls |
| the tip (`c957502c`) | 0 of 5, 0 model calls |

M753 (*Esc stops a running turn*) takes the terminal *hold* on every turn. The hold has existed
since type-ahead (M254), and until M753 it was taken only when type-ahead was on, which it is
not by default. While it is held, the TUI polls for a key once per poll.

**Measured.** A probe opens a pseudo-terminal and puts it into the hold's mode: non-canonical,
no echo, `VMIN=0`, `VTIME=0`.

| | Linux | GNU/Hurd |
|---|---|---|
| `select()` with a 100 ms timeout, nothing waiting | returns after 100.1 ms | returns after 107–110 ms |
| `read()` of one byte, nothing waiting | returns 0 at once | **blocks**; the probe's 3 s alarm ended it |

POSIX says that with `MIN` and `TIME` both zero, a read returns immediately. The Hurd's term
server blocks until a byte arrives. So the TUI's first key poll of each turn, `jc_term_poll_key`,
waited for a key that never came, before the model call was sent. Drivers that pressed a key
mid-turn went on, which is why some prompt drivers passed. **M768** makes the poll ask
`select()` with a zero timeout first, and `select()` is honoured there.

## 6. Two live turns: Driven

The rigs' own task, sourced from `scripts/_rig_live.sh` unchanged, ran through an ssh reverse
tunnel to LM Studio on the workstation:

```
ok - live turn answered over the reverse tunnel (google/gemma-4-12b)
    "text":"OK"
ok - agentic turn: the model called a tool and reported TIER-V-F67C82
    the tool ran and its result was consumed by a second turn
```

## 7. What this page does not claim

- **Not a green tier.** Section 4's second run was stopped at 327 of 342 drivers. The full run
  after the fix is section 8.
- **Not a speed figure.** The guest had one CPU, and the tier is dominated by waits.
- **Not the Hurd's i386 port**: this is hurd-amd64, the 2025 port.
- **Not a diagnosis of `turn_scratch`**: one of three solo runs stalled, with no load on the
  guest.

## 8. After the fix (M768)

M768 changes one function, `jc_term_poll_key`: it asks `select()` with a zero timeout before it
reads, so it never relies on `VMIN=0`. On the Hurd, with the fixed binary:
- `prefix_churn`, the bisection's driver, passes 5 of 5 with 9 model calls, where the binary
  before the fix made none;
- the 10 drivers that had failed and the 16 not yet run, rerun four at a time, pass 25 of 26.
  The one left was `turn_scratch`, the intermittent stall of section 0;
- **a full tier, run once more from the start: `smoke: OK (342 drivers, 1,988 checks)` in
  1,229 s.** `turn_scratch` passed within it. It is 37 checks fewer than on Linux, because some
  drivers skip here.

On Linux the fix changes nothing that a test can see, since Linux honours `VMIN=0`. The 15
drivers that poll the terminal (`esc_interrupt`, the type-ahead pair, the approval prompts and
the rest) pass there before and after. No Linux test can hold this defect. Its proof is the Hurd
guest's drivers: red with M753's poll, green with M768's.

