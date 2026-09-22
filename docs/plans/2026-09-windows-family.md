# The Windows family — band state and handover (M695 – M698)

**Opened** 2026-09-21 on HRZNB-U020390, the machine carrying **WSL2, Cygwin and
MSYS2 at once**. A move to a Windows 11 workstation was prepared and then not
taken; the band continues here. §4 and §5 are kept as written, because the split
between portable work and work needing the Windows environments is useful whoever
picks it up.

---

## 0. Scope: this machine is the Windows-family rig

Decided 2026-09-21. HRZNB-U020390 carries **WSL2, Cygwin and MSYS2**, and that is
what it is for. illumos, the BSDs, the Pi fleet and the emulated architectures are
retested **on the workstation**, not here.

That matters for one of M696's findings in particular. A driver killed at its
deadline can **fabricate** check failures (§3), which is not a Cygwin property —
it is a property of `timeout`, a POSIX shell, and any platform slow enough to trip
a deadline. `PLATFORMS.md`'s illumos row records *"the ten that fail are named and
NOT diagnosed"*, and some of those ten are plausibly the same artifact. **That
re-reading belongs to the workstation**, with today's finding in hand, and is
recorded here as a handoff rather than chased from a machine that cannot boot the
guest.

## 1. State

| | subject | state |
|---|---|---|
| **M695** | three places answered "is jichi tested here", and none agreed | **done** — `63afab23` |
| **M696** | the Cygwin row re-measured, and the deadline that fabricated findings | **done** |
| **M697** | MSYS2 re-measured twice, and both rows driven | **done** |
| **M698** | a rig for Cygwin and MSYS2 (`DEFERRED.md` item 4) | not started |
| **(number TBD)** | UCRT64 and MINGW64: the register gap and the survey | not started -- **the number is not this machine's to mint**: the other machine's merges consume numbers first, so take the next free one after its work lands rather than assuming M699 |

**M696 was renamed, not renumbered.** The plan reserved it for *"the Cygwin and
MSYS2 rows, re-measured and driven"*, and it was held unminted while that was
incomplete. What the work turned out to be is complete and substantial on its own
terms — a row that could not be reproduced, four real defects, and a deadline that
invented three more — so the milestone is named for what happened. MSYS2 and the
driven turns move to M697, which is the honest place for them: they are the
untouched half, not a footnote to a milestone that did something else.

## 2. What landed

| commit | |
|---|---|
| `0c60ba20` | the other machine's gate fix, merged (floors `400 → 360`, `tracked_files`) |
| `658657a6` | ANECDOTES #91 — a cap on a measurement run |
| `1587d3ab` | **`ptydrive` FIONREAD** — the Cygwin tier can build again |
| `63afab23` | **M695** |
| `bac4c535` | #91 retraction — the tier does complete on this bench |
| `439dc72b` | ANECDOTES #92 — the same rule broken 90 minutes later |
| `8111990e` | **`proposals/2026-09-calibration-tier.md`** |
| `685de93b` | `accessible` at 95% of its deadline; the per-driver ratios |
| `5c7352a3` | **`analysis/2026-09-21-the-cygwin-row-remeasured.md`** |

## 3. M696 — what it measured, and what it invented

**Measured** (full detail in the analysis page): build **117 s** median against the
bench's **9.65 s**; unit suite **13,438 / 0**; the tier at shipped deadlines —
**317 drivers, 1,598 checks passing, 20 driver-level failures, 95m59s**; and the
censored re-run giving per-driver ratios of **26× to ~300×** where the build ratio
says 12.1×.

**Findings that outrank the row itself:**

1. **The tier was unbuildable here since M683** (`ptydrive`/FIONREAD). The row's
   published numbers had been unreproducible for weeks with nothing able to say so.
2. **The deadlines conceal real failures.** Three drivers fail genuine checks on
   Cygwin and pass on the bench — `bool_dialect` 4, `daemon_auth` 7 and
   `preprompt_discard` 3 — and all three were killed at 60 s in the tier *before
   emitting those lines*, so the totals showed zero TAP failures. A killed driver
   and a clean driver are indistinguishable in a tier total.
   **A fourth, `setup_keyfile` 27, turned out to be a regression M695 shipped**:
   the new partly-verified wording dropped the lowercase word *platform*, which is
   what that check counts, on every Partly verified row — Cygwin, MSYS2 and
   illumos, none of them reached by any gate. Fixed, and `portability_lint` check
   7d now pins all three verdict wordings in the source.
3. **`accessible` takes 57 s on the bench against a 60 s limit** — the whole of the
   intermittent failure ANECDOTES #91 spent a morning attributing to resource
   accumulation.
4. **A killed driver does not stop — it fabricates findings.** `timeout` TERMs the
   process group, a `$(...)` substitution in flight yields an empty string, and the
   shell — deferring the signal while it waited on that child — resumes and prints
   `not ok` lines accusing the product. Three of Cygwin's seven check failures were
   this. `run.sh` now reports KILLED rather than FAILED and marks the output not
   evidence. **This is not a Cygwin property**; see §0.
5. **"In-suite-only" was never about the suite.** A named subset gets 120 s and the
   full tier 60 s, so three drivers sitting between the two limits pass "standalone"
   and are killed in the tier. Running a driver by hand is a *more generous* test,
   not an isolated one.

**Not done, and now M697:** MSYS2 not re-run; neither row driven; no multiplier
proposed; `PLATFORMS.md` not restated. All twenty censored durations *are* measured, and the
completed set is why no multiplier is proposed: it spans **1.5× to ~300×**, and the
last driver measured explains the spread — `accessible` is wall-clock and pty
bound, so it costs 1.5×, while the lints spawn a process per file and cost 100–300×.
The ratio is a property of what a driver does, not of the host.

**Live data on the machine, not in the tree:** `~/.m696_uncensor.csv` and
`~/.unc_*.log` in the Cygwin home, `~/.m696_meas.log` (the timestamped tier run).
Every number this band has published is already in the analysis page; the raw
artefacts are not, and will be lost if the box is rebuilt. That is acceptable —
they are rig output, which does not belong in the tree — but a re-run is the only
way to recover them.

## 4. What the workstation can do

Most of the band cannot move: Cygwin, MSYS2 and this WSL2 bench are here. The
portable work is real, though, and some of it matters more than the rows.

| work | why it is portable |
|---|---|
| **Record `driver,seconds,limit,censored` in `wd`** (proposal §4) | pure runner change; would have exposed `accessible`'s 5% margin years ago, and makes censoring self-announcing |
| **Make the fork-heavy lints spawn fewer processes** | `license_lint` 283×, `posix_utils_lint` ~300× are not "Cygwin is slow" — the tier's median driver is ~1×. These drivers fork per file. Ordinary Linux work that helps every platform and would shrink a 96-minute tier |
| **The `accessible` deadline decision** (`DEFERRED.md`) | raise the limit, make it cheaper, split it, or move it to the 120 s group — a decision, and it is reproducible on Linux at 57 s |
| **The M201 retry that does not isolate** | `run.sh`'s "standalone retry" re-runs the identical command in the same process with the same `$HOME`; its `ALSO fails standalone -> a real defect` is a positive claim its mechanism cannot support |
| **`docs/PLATFORMS.md` severed cells** | two rows' tails render as prose below the table; `portability_lint` check 16 catches the opposite direction. Needs the commit history, not a plausible join |

## 5. What needs the Windows environments in place

The workstation is Windows 11 and will have Cygwin and the rest installed, so none
of this is blocked — it is sequenced behind that install. **A fresh install is an
advantage for one item:** MSYS2 on HRZNB-U020390 carries an `/etc/fstab` `acl` line
added 2026-08-19, so `chmod` is honoured there and is *not* for a new user. A stock
install gives the true `noacl` behaviour a learner meets, which is half of what the
MSYS2 row is supposed to record.

**A new bench reference must be measured first.** The 9.65 s here does not travel,
and every multiplier quoted against the wrong denominator is wrong silently.

- diagnosing `bool_dialect` 4, `daemon_auth` 7 and `preprompt_discard` 3 —
  `preprompt_discard` is a **safety property** and should be first;
- MSYS2 re-run **twice**, stock `noacl` and with `acl`, because the first is what a
  learner gets and the second is what the platform can do;
- driving both rows with a model (HRZ gateway, free `jlu/*` namespace — 8 of 376
  ids; `jlu/qwen3.8-27b` is the one `DRIVE_LOG` records getting a tool-calling task
  right in one call);
- M697's rigs, which by house rule are written *after* the rows run by hand;
- M698 entirely — UCRT64 and MINGW64 are not installed, and the register gap is
  here because `deferred_register_lint` check 4 enumerates from a table MINGW64 has
  no row in.

## 6. Bench facts worth not re-deriving

- **Never build in the Windows checkout**: `core.autocrlf=true`, every file CRLF.
  Build in WSL2 `/root/jichi`, editable from Windows as
  `\\wsl.localhost\Ubuntu\root\jichi`. Never `/mnt/c`.
- **Cygwin and MSYS2 must not run concurrently** — their DLLs' shared-memory
  regions collide and `fork` starts failing in both.
- **`scripts/preflight.sh` reports a false green on both**: no `pgrep`, so
  `busy_pids` finds nothing and prints "tree is quiet" without checking. Two
  `smoke_lint` processes survived a kill here because `timeout` had reparented to
  init.
- **The bench reference is 9.65 s** (`make clean && make WERROR=1`, median of 3).
  Quote it beside any multiplier derived here; published rows divide by 6.19 s or
  4.00 s and do not always say which.
- **`make ci` cannot run on Cygwin or MSYS2**: no valgrind, no clang.

## 7. M698 — the rigs, and the four decisions taken rather than assumed

`DEFERRED.md` recommendation 4 — *give Cygwin and MSYS2 a rig* — was the oldest
live item on that page. It stayed open under the **Guix rule**: a rig written
blind for a platform nobody can run is a never-executed artifact. Both rows have
now run by hand (M696, M697), so the rule permits writing them — and requires
that what is written be the transcript of those runs rather than a guess at them.

Four decisions were taken in the open, because each could reasonably have gone
the other way and a decision taken quietly is one nobody can revisit.

**a) The rigs are LOCAL, and call the task functions directly.** Every other
`tier-v-*` rig boots a guest and drives it over ssh, with `g`/`gl` as the remote
shells. Cygwin and MSYS2 are not guests: they are emulation layers installed on
the machine running the rig, so there is nothing to boot and nothing to reach.
`scripts/_rig_live.sh` had already anticipated this and written the answer into
its own header — *"The transport is each rig's own business… A rig with a
different shape calls the task functions directly — see tier-b-device.sh."* So
the two rigs use the shared **task** (both prompts, the nonce, the fixture, the
config), which is the only thing that makes two rows comparable, and supply a
local `sh` as the **transport**. `jc_rig_live` itself is untouched, so
`rig_live_commands_lint`'s golden-command comparison still pins it byte for byte.

**b) The shared live config gained an optional `apiKeyEnv`, rather than a second
config generator.** Every transport that existed when the task was extracted
reached a **keyless** server — LM Studio on the host's loopback — so `apiKey`
could be the literal `"unused"` and nothing noticed. These rows cannot use that
transport at all: LM Studio is not installed on this machine and the JLU instance
is unreachable from it, so they drive the **HRZ gateway over TLS**, which wants a
key. The choice was one optional third argument in `jc_rig_live_config` or a
fourth copy of the config in the Windows rigs. `_rig_ship.sh`'s header settles
it: *a second rig is where drift starts, not the fourth.* With no third argument
the function emits what it emitted before, **byte for byte** — verified — so no
existing row's config changed. It is `apiKeyEnv` and never `apiKey`: the *name*
of a variable, so the secret stays out of the config file and out of `ps`, which
is what jichi's own doctor warns about.

**c) The rigs check the free namespace before the first request.** The band's
standing constraint is the HRZ gateway's free `jlu/*` models only. That was a
rule held in a person's memory, and a rule remembered at 02:00 is a rule skipped
at 02:00. `jcw_free_or_die` refuses a model outside the prefix, refuses an
**empty key** — which otherwise 401s and makes the row read like a platform that
cannot reach a model — and refuses to drive blind if `/v1/models` cannot be
listed. Proved in all three directions before the row ran.

**d) `DEFERRED.md`'s "the reverse-tunnel arrangement is in no rig" row is
STALE, and is corrected rather than closed.** It was true when written. It stopped
being true at M677: `jc_rig_live` carries the forward, seven rigs use it, and
`rig_live_lint` check 5 now *enforces* `ExitOnForwardFailure=yes` on every one of
them — because a forward that cannot bind otherwise WARNS and runs the command
anyway, which once produced two green live turns through a stale forward nobody
knew was there. The register kept claiming a gap two milestones after it closed.
The genuine remaining gap is the other direction, and M698 closes it: the
**gateway** transport was in no rig, and now is.

### What these rigs refuse to do, and why each refusal is the feature

| Refusal | Exit | Because |
|---|---|---|
| no `--ref-secs` | 2 | A multiplier's denominator is unknown, so no number the row produces can be compared with any other row — which is the entire purpose of measuring it. |
| wrong shell (`uname -s`) | 3 | A Cygwin row measured from MSYS2 is a row about a different platform wearing this one's name. MSYS2's rig additionally refuses `ucrt64`/`mingw64`, which are the UCRT64/MINGW64 rows, not this one. |
| the other installation is running | 1 | The two runtimes' shared-memory regions collide and `fork` begins failing in **both**. A row measured through that collision is full of defects belonging to neither platform. |
| a model outside the free namespace | 1 | The failure mode of guessing is a charge; the failure mode of refusing is a message. |

### The bug that nearly shipped inside the guard

Recorded in full as **ANECDOTES #93**, because the lesson outlives the code. The
first guard tested `[ -x /c/msys64/usr/bin/bash.exe ]` — and `/c` is *MSYS2's*
mount prefix, while Cygwin's is `/cygdrive/c`. Run from Cygwin the path never
existed, the `if` never ran, and the function fell through to *"the other
emulation layer is idle"*. It would have reported a clean bill of health on every
Cygwin run forever **without ever looking** — in the same file whose comments
denounce `preflight.sh` for doing exactly that with a missing `pgrep`.

It also *started MSYS2 in order to ask whether MSYS2 was running*, and a later
version keyed on `msys-2.0.dll`, which **Git for Windows** also maps — a third
cygwin-family installation — so it refused every run on any developer machine.
The shipped version asks `ps -W` for the other **installation root**, and
distinguishes three outcomes: busy, idle, and **could not look**.

> Writing a failure mode down does not confer immunity to it. *Absence and
> success sharing a representation* is a shape, not a thing other people's code
> does.
