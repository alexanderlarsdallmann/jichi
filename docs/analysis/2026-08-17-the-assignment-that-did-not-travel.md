# The assignment that did not travel — one shell difference, six broken tests, one closed finding

*2026-08-17, threadwork. FreeBSD 15.1-RELEASE-p2 under KVM, reached with
`scripts/tier-v-bsd.sh`. Written because the finding it closes had been open since
M460 as "recorded as unexplained rather than closed with a plausible story", and the
cause turned out to be one line of POSIX shell whose behaviour is not portable.*

## The one fact

A variable assignment **prefixed to a shell function** is visible inside that
function as a shell variable on every shell. Whether it is **exported to processes
that function runs** is shell-dependent. Measured, with the same script on each:

```sh
f() { env | grep -q '^FOO=' && echo YES || echo NO; }
FOO=bar f
```

| shell | in the child's environment? |
|---|---|
| dash — Linux `/bin/sh` | **YES** |
| bash | **YES** |
| **FreeBSD `/bin/sh`** | **NO** |

Both shells agree on everything else: `$FOO` *is* `bar` inside the function, and
unset after it returns. Only the child's environment differs. So the bug is
invisible on every shell this project is developed on, and there is no warning.

## What it broke

`tests/smoke/setup_keyfile.sh` check 6 read:

```sh
out=$(cd "$ws2" && JICHI="$BIN" with_deadline 60 ./run.sh doctor < /dev/null 2>&1)
```

`with_deadline` is a function in `_smoke.sh`. On FreeBSD `JICHI` therefore never
reached `run.sh`, whose generated line is

```sh
JICHI="${JICHI:-jichi}"
```

so it fell back to a bare `jichi`, which is not on `PATH` in the guest. `$out` was
**22 bytes**: `exec: jichi: not found`.

That single fact explains every part of the original report:

| the report said | why |
|---|---|
| "the key did not reach jichi" **with an empty tail** | the message appends `grep -i 'api key'` of the output, and `exec: jichi: not found` has no such line |
| "every element passes when tested separately" | a prefix on an *external* command exports correctly — so testing the pieces by hand could never reproduce it |
| "wrapping the call in `timeout` changes nothing" | `timeout` was already there, inside the function; the loss happens at the function boundary, not at the timeout |

Confirmed on the platform: with the fix, `ok 6 - running through the script, jichi
finds the stored key`, driver `rc=0`, zero failures.

**A second, smaller defect in the same line:** the failure message names one cause
for a check that cannot distinguish it from "jichi never ran". A check should not
assert a diagnosis it has not established.

## Five more sites, and why they were worse than a failure

The same shape appeared five more times. None of them *failed* on FreeBSD — they
passed, while testing something other than what they claimed:

| site | what was lost | consequence on FreeBSD |
|---|---|---|
| `migration_paths.sh` | `HOME` | the driver runs against the **real** `$HOME`, not its fixture |
| `doctor_cache.sh` | `HOME` | same |
| `faults.sh` | `JICHI_FAULT_PROCFS_AFTER` | the fault never fires, so the driver asserts nothing |
| `setup_keyfile.sh` (the PTY arm) | `TERM=dumb` | the terminal under test is whatever was inherited |
| `org_mode_lint.sh` | `LC_ALL=C` | emacs runs under the ambient locale |

Two of those would have written into the developer's own `$HOME` while claiming an
isolated fixture. A test that fails is a nuisance; a test that passes without
testing is a liability.

## The fix, and why this shape and not another

`with_deadline N env NAME=value cmd ...` — `env` is POSIX, and it puts the variable
in the child's environment unambiguously, whichever shell is running the driver.
Rejected alternatives: `export` before the call (leaks into the rest of the driver,
and these fixtures are deliberately scoped), and a subshell with `export` (works,
but reads as ceremony at 210 call sites where `env` reads as intent).

Pinned by `smoke_lint` check 12, which forbids the prefix-on-`with_deadline` shape
outright rather than trying to judge whether each instance needs the child to inherit
it. `with_deadline` always runs a child, so the rule can be total — and a total rule
needs no exception list, which is the property that makes a lint trustworthy.

**Three false positives while writing that lint, all found by running it:**

1. The check's own `t_fail` message contained the literal forbidden text, so the
   lint failed on itself. Reworded rather than exempted — the
   `slash_commands_lint` lesson.
2. The check's own two calls used `export NAME=1; with_deadline …`, and the regex's
   value class allowed the `;`, so a *sequence* matched as a *prefix*. Narrowed to
   exclude `;`, `&` and `|`.
3. Before that, the check exempted itself by construction — I had written the calls
   in the prefix form because "it happens to be safe here" (it is: `with_deadline`
   reads the multiplier as a shell variable, not from a child's environment). A lint
   that exempts itself is a lint nobody believes, so the calls were rewritten.

## What this closes, and what it does not

**Closes:** FreeBSD's `setup_keyfile` check 6, open since M460. FreeBSD moves from
*Partly verified* (185 of 198 drivers) to **Verified — the full gate**: 11,643 unit
checks / 0 failures and `smoke: OK (201 drivers, 1068 checks)`, 0 failures, measured
on the platform. The 1068 against Linux's 1081 is twelve environment-gated checks
that skip there, not a gap.

**Does not close:** OpenBSD's `accessible` stop, which is a different mechanism
already explained separately
([`2026-08-17-the-lost-first-send.md`](2026-08-17-the-lost-first-send.md)) and is
about a fixed pre-send delay rather than a lost variable. OpenBSD keeps *Partly
verified*; its row has not been re-run with these fixes, and it should be —
`scripts/tier-v-openbsd.sh --ref-secs <n> --reuse` is the cheap way.

**A caution for the register:** this row was diagnosable only after the rig stopped
discarding the evidence. `tier-v-bsd.sh` captured the smoke failure as
`... | tail -5`, which for a 28-check driver is four passing checks and
`gmake: *** Error 1` — so the row recorded "did not print its OK marker" and never
which check failed. The diagnosis was being thrown away by the capture, not missing
from the run. That is fixed too, and it is the reason this took two sessions rather
than one.
