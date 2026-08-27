# The NetBSD row: the first non-Linux kernel that could check the descriptor fence

**Date:** 2026-08-18 · **Milestone:** M480 · **Rig:** `scripts/tier-v-netbsd.sh` (new)
· **Result:** NetBSD 10.1 amd64 — build clean, **12,416 unit checks / 0 failures**,
**smoke 209 of 209 drivers / 1,109 checks / 0 failures**, all four offline surfaces.
· **Predecessor:** [`2026-08-18-openbsd-remeasure.md`](2026-08-18-openbsd-remeasure.md)

This is written for someone who has never brought a new platform up and wants to
know what the work actually consists of. The honest answer, measured here: the
*product* needed **zero changes**, and the *rig* took **six attempts**, five of
which failed for reasons that had nothing to do with jichi. That ratio is the
lesson, and §2 is the whole of it.

---

## 0. Why this row, and why it was worth a session

`docs/PLATFORMS.md` had NetBSD under **Never compiled**, with a note calling it
"the cheapest remaining row … nothing blocks it but time." Two things made it more
than a box to tick:

1. **`tests/smoke/child_fds.sh` had never run on a non-Linux kernel.** That driver
   proves M472's descriptor fence: a model-issued shell inherits *none* of jichi's
   descriptors — not the run journal, not the telemetry sink, not the live provider
   socket. Before M472 it inherited all three, and `echo {*FORGED*} >&3` could put
   a forged record in the middle of the journal that `jichi runs` and `doctor
   --unattended` read to gate an unattended loop. The driver reads
   `/proc/<pid>/fd` to assert it, so it declines on FreeBSD and OpenBSD, which have
   no procfs. **NetBSD has procfs.** A security guarantee verified on exactly one
   of five libcs was the strongest argument available for any remaining row.

2. **NetBSD's base compiler is GCC.** FreeBSD and OpenBSD are both clang, so
   `make info`'s `WARN_OPTIONAL` — `-Wlogical-op -Wduplicated-cond
   -Wjump-misses-init` — had never been applied to jichi anywhere but glibc.
   It is also the platform where M479's freshly-fixed hardening probe could be
   checked against a compiler that *honours* `-fstack-clash-protection`, rather
   than one that accepts and ignores it.

---

## 1. The result, in full

Both terms of every ratio, per the runbook's rule (copy the formula, never a row's
number):

| | value | against |
|---|---|---|
| build | `gmake WERROR=1` **clean, 7 s**, first try | OpenBSD 8 s, FreeBSD ~5 s |
| dialect | `STD_DIALECT = c89 (strict)` | same everywhere |
| libcurl | `HAVE_CURL = yes` | needs `pkg-config` — §3 |
| GCC-only warnings | `WARN_OPTIONAL = -Wlogical-op -Wduplicated-cond -Wjump-misses-init` | OpenBSD: *"(none: this compiler has no GCC-only warning flags)"* |
| unit | **12,416 checks / 0 failures** | Linux 12,422 · OpenBSD 12,415 |
| smoke | **209 of 209 drivers, 1,109 checks, 0 failures** | OpenBSD 207/209, 1,102 · FreeBSD 201, 1,068 |
| declined | **5** drivers | OpenBSD 6 — and the sixth was `child_fds` |
| multiplier | `ceil(7 s / 6.66 s) = 2` | both terms printed at every figure |
| offline surfaces | `--version`, `doctor`, `describe`, `context` — all ok | |
| compiler | `cc (nb3 20231008) 10.5.0` (GCC in base) | clang on the other two BSDs |
| `/bin/sh` | NetBSD's own `sh`, 232,680 bytes | OpenBSD's is **ksh** — that axis stays OpenBSD's |

**The headline.** `child_fds` ran, and all three of its checks passed:

    --- smoke: child_fds
    1..3
    ok 1 - the fork/exec child sees stdio only (no descriptor above 3)
    ok 2 - the run journal is not inherited
    ok 3 - the telemetry sink is not inherited

M472's descriptor fence is now verified on **two kernels** instead of one.

**Proven both ways, not assumed.** procfs is `noauto` in NetBSD's `/etc/fstab`, so
the rig mounts it — which means the row's headline depends on a step the rig
takes, and a claim like that must be tested in both directions:

    # procfs unmounted
    1..0
    # skip: needs /proc/self/fd (Linux)      <- declines
    # procfs mounted
    1..3
    ok 1 - the fork/exec child sees stdio only (no descriptor above 3)

So the rig asserts the mount as its own check, and asserts *that the driver ran*
after the tier finishes — because "209 drivers passed" and "the fence is verified"
are different claims, and only that check connects them.

**The five drivers that declined**, stated because a skip list is the only thing
separating "209 passed" from "209 ran": `faults` and `faults_net` need a `FAULT=1`
binary (a different build), `faults_net_midstream` likewise, and `pdf` /
`docs_pdf` need `pdftotext`, which the rig does not install. Same class of decline
as on every other row; none is platform-specific.

**Not claimed, because not run:** `make ci` (its multi-compiler + sanitizer +
Valgrind + fuzz stages are Linux-only), the Python e2e residual, the live model
bench.

---

## 2. Six attempts, and what each one taught

The product built clean on the first try. Everything below is about getting a
machine to a login prompt unattended — and it is the honest cost of a new row.

### Attempt 1 — a false match on `boot`

I waited for the pattern `boot` to find the loader's prompt. It matched
**"Primary Bootstrap"** and **"BIOS Boot"**, three screens too early, so
`consdev com0` and `boot` were typed into the 5-second countdown *menu*, whose
RETURNs mean "boot normally". The guest booted twice and I initially read one of
those reboots as my own fault (§2.5 shows it wasn't).

**Lesson:** a wait-for pattern must be a string only the awaited state can print.
`Choose an option` is that string; `boot` is a substring of three other things.

### Attempt 2 — the console went deaf, not dead

The discovery script held the fifo's write end on a fd and exited. That sends
**EOF to qemu's stdin**, and qemu then stops reading stdin *permanently* —
reopening the fifo later writes into a pipe nobody reads. Every subsequent
keystroke vanished and the console looked hung.

**Fix:** a holder process keeps a writer open for the VM's whole life. Its stdout
is closed too, because a background subshell inheriting stdout makes a caller's
`| tail` wait for the VM — that cost a five-minute timeout on its own.

### Attempt 3 — an `ok` that reported the send, not the state

The first rig announced `ok - console moved to com0` immediately after *writing*
the keystrokes, then timed out 240 s later at `login:` with nothing on stdout to
say why. The console held the answer:

    > consdev com

The trailing **`0` was dropped**. The loader has no flow control on serial and
loses characters written as one burst; `consdev com` reset the loader, the next
menu got nothing, and the kernel booted to a VGA console nobody was watching —
silence on serial, indistinguishable from a hang.

Two fixes, and the second matters more than the first:

* **`typeslow`** — one character at a time, 80 ms apart. This is the same rule
  `CLAUDE.md` already states for the PTY drivers ("human-scale delays between
  sends"); the loader needs it *per character*.
* **The echo is the check.** The loader echoes what it received, so
  `waitfor 'consdev com0'` distinguishes *typed* from *arrived*. The `ok` line now
  says `(echo verified, not just sent)`.

**This is M479's lesson, reproduced in new code the same day.** M479 was about a
lint that reported 6 ok while measuring nothing; within hours I wrote a rig step
that reported success for an action that had failed. The pattern is identical —
asserting the *attempt* instead of the *effect* — and knowing the lesson did not
prevent it. What caught it was that the next step was independently verified, so
the false green could not survive to the end of the run.

### Attempt 4 — winning a race by losing it

To stop losing the 5-second countdown, I sent **five** spaces instead of one.
The menu read:

    Option: [1]:    3

The *first* space is consumed as the countdown-stop key; every later one is
ordinary input on the option line. So `    3` was rejected and the menu
re-prompted — spamming the keystroke is what lost the race.

**Fix:** exactly one space, and fix the race where it actually lives — `waitfor`
now polls **four times a second** instead of once. The console is a local file;
the grep is free; a 1 Hz poll against a 5 s window loses often enough to matter.
Plus a new confirmation: `Option: [1]:` is printed *only* when the countdown was
stopped, so waiting for it separates "the keys were sent" from "the loader is
listening".

### Attempt 5 — the console switch eats a character too

With everything above fixed, `consdev com0` was verified intact — and then:

    > oot
    unknown command

`consdev` **reopens** the console and reprints the loader banner on it, and the
first character typed across that switch is lost. The loader sat at its prompt
until the row timed out.

**Fix:** let the switch settle, flush with a bare newline (harmless at that
prompt), then type `boot` and **verify its echo**, retrying up to three times. An
unknown command costs nothing here; a dropped `b` costs the row.

### Attempt 6 — the guest reboots itself, and that is by design

Now the kernel booted on serial, ran through device attach, and then:

    Growing ld0 disklabel (1907MB -> 12288MB)
    Resizing / (/dev/ld0a)
    ...
    reboot: rebooted by root
    [  11.2744110] rebooting...

The live image's own `rc` grows the root disklabel and FFS to match the disk we
resized, and then **reboots**. So the loader menu appears **twice**, and a rig that
drives only the first leaves the second to expire → VGA → silence → a timeout four
minutes later whose message says nothing about a resize.

This also retro-explains attempt 1: I had blamed *both* of those reboots on my own
keystrokes, and one of them was always going to happen.

**Fix:** `drive_menu()` is a function, called in a loop bounded at three boots,
dispatching on `waitfor2 'login:' 'rebooted by root'` — so the two outcomes are
distinguished in seconds instead of after a four-minute timeout. The console log is
rotated between boots (appended to `netbsd-console-full.log` first, because a
discarded console is how a boot-time finding gets lost) so the same simple
content greps keep working.

Provisioning then writes `/boot.cfg` with `consdev=com0`, which makes every later
boot serial-native: `--reuse` needs **no typing at all**, verified —
`ok - booted straight to login: (/boot.cfg pins consdev=com0 -- no typing)`.

### And one more, after the row was green: a check that lied in the safe direction

The from-scratch run finished **17 ok, 1 failed**, the failure being
`child_fds did not run -- procfs mount lost`. The smoke log showed it running
green three lines later. The bug:

```sh
grep -A4 '--- smoke: child_fds' "$LOG"     # pattern starts with dashes
```

grep parses a leading-dash pattern as **options**, errors, and the pipe produces
nothing. `-e` fixes it. Worth recording for the direction it failed in: this
produced a **false red**, where M479's three defects all produced false greens. A
false red costs an investigation; a false green costs a wrong published claim. If
a check must be wrong, this is the way to be wrong — and it was caught on its
first run precisely because it was loud.

---

## 3. Four platform facts that cost time and are not in any README

**`pkg-config` is a required package, not a nicety.** The Makefile's libcurl check
is a *compile probe* with no CFLAGS of its own:

```make
CURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null)
HAVE_CURL := $(shell printf '#include <curl/curl.h>...' | $(CC) -xc - $(CURL_CFLAGS) ... -lcurl ...)
```

pkgsrc installs to **`/usr/pkg`**, which NetBSD's base compiler does not search.
Without `pkg-config`, `CURL_CFLAGS` is empty, the probe fails honestly, and jichi
builds a **networkless binary** while `make info` prints only `HAVE_CURL =` with no
hint as to why. With it: `-I/usr/pkg/include` and
`-L/usr/pkg/lib -Wl,-R/usr/pkg/lib -lcurl` — rpath included, so nothing needs
`LD_LIBRARY_PATH`. This is not a jichi defect (the probe answers the question it
asks, correctly), but it is a platform where a missing *third* package silently
removes a whole subsystem, and the rig therefore installs it as a hard
requirement rather than leaving it to the reader.

**The guest's non-interactive ssh `PATH` is `/bin:/usr/bin`.** Not a footnote: it
made `pkg_add`, `useradd`, `chown`, `mount` **and** `sysctl` all report *"not
found"* while every one of them was installed. A rig that reads those as absent
tools draws false conclusions about the platform — I briefly believed procfs was
unmounted because `mount | grep -c procfs` returned 0. Every root command in the
rig now sets `PATH` explicitly.

**`useradd` puts a user in the shared `users` group.** There is no per-user group,
so `chown -R tierv:tierv` fails with *"invalid group name"*. `tierv:users`.

**The unit-check count depends on the installed tools, not just the platform.**
My hand-run measured **12,364** checks; the rig measured **12,416** on the same
commit. Rather than reason about it, I measured it on Linux:

    with git:    12422 checks, 0 failures
    without git: 12366 checks, 0 failures

**56 checks are git-gated** (`test_git.c`'s integration and mutation sections, and
the snapshot tests). My hand-run had not installed git yet. So a row that forgets
`git` publishes a smaller number that looks exactly like environment-gating — and
`12,416` vs Linux's `12,422` leaves a genuine six-check environment gap, which is
consistent with OpenBSD's seven. `git-base` is now a rig requirement, and it is
also what `attempt_tainted.sh` needs: without git, `attempt` refuses (worktree
isolation), and the failure message names *snapshots*, not git.

---

## 4. One product change, and it is a comment

The row found **no defect in jichi**. It did find a false statement in a test:

```sh
[ -d /proc/self/fd ] || t_skip "needs /proc/self/fd (Linux)"
```

That is wrong in the direction that discourages the measurement — it told a reader
this guarantee could only ever be checked on one kernel, when what it needs is
**procfs**, which NetBSD has. The skip text now names procfs and NetBSD's
`mount_procfs`, and the comment records that FreeBSD and OpenBSD still decline —
**a gap in coverage, not a pass.**

Small, but it is the same class as M479's `__stack_chk_fail`: a platform-specific
fact written down as a universal one, which then shapes what anyone bothers to
measure next.

---

## 5. Why this rig is half the size of the OpenBSD one

`tier-v-openbsd.sh` drives `autoinstall(8)` over a serial console *and* serves a
response file over HTTP from the host, because OpenBSD ships no image you can
simply boot. NetBSD ships a **live image**: a bootable disk with a full system and
sshd already enabled. So there is no installer to drive, no answer file, no host
HTTP server, and — unlike the OpenBSD rig — **no python3 host requirement**. The
install step collapses to *fetch, gunzip, convert, resize*.

What that buys is paid back at the console: with no answer file, the **ssh key is
planted by typing it at the console**, which is the one thing the FreeBSD
cloud-image rig gets free from cloud-init. Every trap in §2 lives in that one
mechanism.

---

## 6. What is still open

| Item | State |
|---|---|
| `pdf` / `docs_pdf` on this row | decline; `pdftotext` is not installed. One `pkg_add poppler` would close it |
| `faults*` drivers | need a separate `FAULT=1` build; no row runs them |
| `make ci` on NetBSD | not run. Its stages (clang, ASan/UBSan, Valgrind, fuzz) are Linux-only today |
| illumos | still Never compiled, and now the *cheapest* remaining row. Its procfs is present but different — `/proc/self/status` is a binary `pstatus_t` — so `child_fds` there would need looking at rather than assuming |
| macOS | still Never compiled, and still the one with a `#if defined(__APPLE__)` branch nobody has compiled |

---

## 7. Lessons

1. **A wait-for pattern must be a string only the awaited state can print.**
   `boot` appears in "Primary Bootstrap" and "BIOS Boot"; matching it fired three
   screens early and typed a command into a menu.
2. **Report the effect, never the attempt.** `ok - console moved to com0` after
   merely writing the keys was green on a run that had already failed. Where the
   target echoes, *the echo is the check* — it is the cheapest way to tell "typed"
   from "arrived".
3. **Knowing a lesson does not prevent repeating it.** M479's whole subject was
   assertions that measure nothing, and I wrote one within the hour. What
   contained it was the *next* step being independently verified, which is an
   argument for verifying every step rather than for trying harder.
4. **Fix a race where it lives.** Sending five keystrokes instead of one to win a
   5-second countdown is what lost it; the poll interval was the actual defect.
5. **When two of your own checks disagree, one of them is a bug — find out which
   before writing either down.** procfs mounted + smoke OK + "child_fds did not
   run" was a grep parsing `--- smoke: …` as options.
6. **A missing tool can look exactly like a platform property.** An ssh `PATH` of
   `/bin:/usr/bin` presented five installed programs as absent; a missing `git`
   presented 56 skipped checks as environment-gating.
7. **The product cost nothing; the harness cost everything.** Zero product changes,
   six rig attempts. Three non-Linux kernels now share one C89 source with no
   `#ifdef` added since FreeBSD's three — which is the actual, quiet result of
   this row.
