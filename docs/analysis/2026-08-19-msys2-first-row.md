# MSYS2, the first row: one cause, seven failures, and a guarantee that quietly is not one (2026-08-19)

Layer 2 of the Windows survey (see [`PORTING_WINDOWS.md`](../PORTING_WINDOWS.md)),
second environment after Cygwin. Everything here was measured on
`MSYS_NT-10.0-26200 3.6.10-8fbd9808.x86_64`, gcc **15.3.0**, in the **MSYS**
environment (`msys-2.0.dll`, the Cygwin-derived POSIX emulation) -- not MINGW64,
which is a separate row still unmeasured.

## The short version

| Tier | Result |
|---|---|
| `make WERROR=1` | **clean in 94 s, zero warnings**, fully featured |
| `make test` | **12,440 checks / 1 failure** |
| `make smoke` (211 drivers, mult 8) | **1,157 checks / 6 failing drivers**, 31m49s |
| Product changes needed | **none** |

Seven failing assertion sites across those two tiers. **All seven have the same
single cause**, and it is not in jichi.

## The cause: `chmod` succeeds and does nothing

MSYS2 mounts its root with `noacl`:

    $ mount
    C:/msys64 on / type ntfs (binary,noacl,auto)

With `noacl`, POSIX permission bits are not mapped to Windows ACLs at all. So:

    mkstemp mode      : 0644  (group/other bits: 0044)
    after chmod 0600  : 0644  (group/other bits: 0044)

`chmod` **returns success**. The mode does not change. Nothing in the POSIX API
reports a problem, which means every layer above it -- including
`jc_make_private()` and therefore M132's privacy guarantee -- believes it has
secured the file.

The contrast that makes this a *configuration* difference rather than a Windows
limitation was measured on the **same machine, same NTFS volume, same user**:

| | `chmod 600` yields | mount options |
|---|---|---|
| **Cygwin** | **600** | `binary,auto` (ACLs active) |
| **MSYS2** | **644** | `binary,noacl` |

Windows can express this; MSYS2 as shipped chooses not to.

## What that costs, concretely

Four of the failures are **real breaches of guarantees jichi provides everywhere
else**, and two of them are security-relevant enough to name plainly:

- `setup_keyfile`: *"key file mode is -rw-r--r--, want -rw-------"*. The file
  `jichi setup` writes to hold an **API key** is readable by every local account.
- `stop_reason_capped`: *"daemon socket mode is 'srw-r--r--', not srw------- --
  any local user can drive a process that runs shell commands"*. The driver's own
  message states the consequence better than this page can.
- `privileged` and `kinetic`: the **audit log** is world-readable, which is the
  record M132 made private on purpose.
- `test_app.c:161` (unit suite): `jc_write_file_atomic` cannot carry mkstemp's
  owner-only mode.

The remaining two failures are a different shape, and they are **not** defects on
either side:

- `sessions`: *"an unreadable session store reported rc=0 … a store that cannot be
  read must not look like an empty one"*.
- `index_coverage` (four checks): *"index reported nothing about an unreadable
  directory"*, *"an unlistable root did not fail with a build error"*.

Both tests **create their negative condition with `chmod`** -- an unreadable store,
an unlistable directory. On this platform that `chmod` does nothing, so the
condition never exists, and jichi correctly reports a store it really can read.
The test cannot be *performed* here; it is not failing on the merits.

**Corrected further down**: those two ARE a defect after all -- in the
*driver*, not the platform. Both gate on `id -u` = 0 ("root ignores
permissions") where the real question is whether the host can make a
directory unreadable to its OWNER at all, which on Windows it cannot at any
privilege level. See *the two that `acl` cannot fix*.

## Why these must NOT be made to skip

This is the distinction against the Cygwin row, and it is the whole point of
reading a failure instead of counting it.

**On Cygwin** (M477) the failing tests asserted things about their *host* that were
false -- that a pid 1 exists, that `/proc/self/status` carries `VmHWM:`. The
assertions were wrong, so the tests were fixed, gated, and made to skip loudly
where the shape differs.

**On MSYS2** the failing tests assert a guarantee jichi genuinely provides on
every other row, and the platform silently declines to honour it. Making them skip
would convert a true, security-relevant red into a green, which is the exact trade
`docs/TEST_INTEGRITY.md` exists to refuse. The row is *partly verified* and says
which tiers and which guarantee; the tests stay red here.

The honest summary for a user is therefore not "MSYS2 works" but: **jichi builds
and runs on MSYS2, and jichi's file-privacy guarantees do not hold there.** If the
threat model includes other local accounts on the machine, use WSL2 or Cygwin.

That holds **as shipped**. It is not the last word: an `acl` mount clears all
four privacy failures, measured below -- so the accurate statement is that the
guarantees hold when MSYS2 is configured for them and silently do not in the
default configuration, which is the one a user meets first.

## Symlinks: a second silent no-op, this one fixable from the environment

`ln -s` also reported success and did something else:

    $ ln -s /tmp/jcsym/inside /tmp/jcsym/link   # "succeeded"
    $ test -L /tmp/jcsym/link && echo yes || echo no
    no
    $ ls -ld /tmp/jcsym/link
    drwxr-xr-x  ...                              # a real directory

Default `winsymlinks` is unset, so the link became a copy. That is why
`test_path.c:157` (resolving a not-yet-existing file *through* a symlinked
directory) failed: there was no symlink to resolve through.

Unlike the ACL problem this needs **no change to the installation**:

    export MSYS=winsymlinks:nativestrict   # or winsymlinks:lnk

Both produce a real symlink (`test -L` -> yes), and with it set the unit suite goes
from 12,437/2 to **12,440/1** -- the path failure disappears and the check count
*rises*, because the symlink branch now runs the assertions it was skipping.

## The fork penalty is not one number

This matters for `JC_SMOKE_TIMEOUT_MULT`, which is a single scalar.

| Workload | MSYS2 | WSL2 (same box) | Ratio |
|---|---|---|---|
| Unit suite, warm (one process, in-process checks) | ~45 s | ~6.3 s | **~7x** |
| `docs_flags` (spawns grep/awk per file) | **24 s** | **<1 s** | **>24x** |
| `arena_lint` | 2 s | <1 s | modest |

The penalty tracks **how many processes a driver spawns**, not wall-clock in
general. A multiplier derived from the unit suite (7-8) is therefore generous for
most drivers and tight for the process-heavy lints. Multiplier **8** was used here
and no driver timed out, so the number worked -- but it worked by being wrong in
the safe direction, which is worth writing down rather than mistaking for a
validated model.

For reference, measured warm on this machine with the binary already built:
WSL2 6.2-6.5 s, MSYS2 ~45 s, Cygwin 45-59 s. Cygwin's earlier `mult 10` came from
a comparison of two *compile-inclusive* figures (130 s vs 13 s); the ratio was
approximately right, but neither number was a runtime, and the tidier-sounding
conclusion that the multiplier had been over-stated was **wrong** -- it was checked
against WSL2 like-for-like only afterwards.

## Prerequisites a newcomer needs, that no page listed

- **`diffutils` is not installed by default.** A fresh MSYS2 has neither `diff` nor
  `cmp`. Verified with `pacman -S --print` that installing it pulls nothing else.
- `gcc`, `make`, `git`, `pkgconf` and **`libcurl-devel`** were present, and libcurl
  being available is why this row produced a fully featured build:
  `STD_DIALECT = c89 (strict)`, `HAVE_CURL = yes`, `HAVE_MALLOC_TRIM = yes`,
  `CLOCK_GETTIME = in libc`, all three GCC-only warning flags, and the full
  hardening set. That also exercises M476's probe fix on a **second** emulation
  layer: the `/dev/null` defect that made Cygwin build a maximally degraded binary
  does not recur here.
- **Do not run Cygwin and MSYS2 at the same time** (measured 2026-08-18): their
  `cygwin1.dll` / `msys-2.0.dll` shared-memory regions collide and `fork` starts
  failing in both.


## What an `acl` mount fixes, and the one thing it cannot

The open question above -- *can MSYS2 honour POSIX modes at all?* -- was then
answered by measurement rather than left hanging. One line added to `/etc/fstab`
(stock copy kept at `/etc/fstab.jichi-backup`):

    C:/msys64/tmp /tmp ntfs binary,acl 0 0

`/tmp` rather than the root mount, deliberately: it is where both tiers write --
`run.sh` puts its isolated `HOME` under `TMPDIR`, and `setup_keyfile`'s key file
lands inside that `HOME` -- so it covers the failing drivers while leaving the
installation's root mount stock. In a fresh process:

    C:/msys64/tmp on /tmp type ntfs (binary)     <- noacl gone
    chmod 600 -> 600 ;  chmod 644 -> 644 ;  chmod 700 (dir) -> 700

So **MSYS2 can honour POSIX modes; it ships with them off.** The seven failures were
one mount option, not a Windows limitation, not an NTFS limitation, and not a defect
in jichi.

Re-run under `acl`:

| | stock (`noacl`) | with `acl` |
|---|---|---|
| unit suite | 12,440 / **1 failure** | 12,437 / **0 failures** |
| `setup_keyfile` (API key file mode) | FAIL | **pass** |
| `stop_reason_capped` (daemon socket mode) | FAIL | **pass** |
| `privileged` (audit log mode) | FAIL | **pass** |
| `kinetic` (audit log mode) | FAIL | **pass** |
| `sessions` | FAIL | **still FAIL** |
| `index_coverage` | FAIL | **still FAIL** |

Four of the four *privacy* failures clear. So the practical advice for this row is
not "jichi's guarantees do not hold on MSYS2" but: **they hold if you mount with
`acl`, and they silently do not if you use the stock configuration** -- which is
what a user actually encounters, and why the row stays *partly verified*.

(The check count moves between configurations -- 12,437 here, 12,440 under
`noacl`+symlinks, 12,438 on WSL2. Some assertions are evidently gated on what the
filesystem permits. Recorded as observed and **not yet explained**, rather than
given a mechanism it has not earned.)

### The two that `acl` cannot fix, and why they are a different defect

`sessions` and `index_coverage` still fail, and the reason is not permissions
recording but permissions *enforcement against the owner*. Measured on all three
environments:

    file: chmod 000, then read it as the owner
      MSYS2 (acl)  mode 0 -> owner CAN still read it
      Cygwin       mode 0 -> owner CAN still read it
      WSL2         mode 0 -> owner CAN still read it   (but uid 0: root bypasses)

On NTFS the owner retains access to a file whose mode reads `000`. Both drivers
build their negative fixture with `chmod 000` -- an unreadable session store, an
unlistable index root -- so on Windows that fixture cannot exist and jichi is asked
to report a failure that is not happening.

`sessions.sh` states the assumption plainly, and it is the reason this is a
*portability defect in the driver* rather than a platform note:

> That needs no fault injection to reach: `chmod 000` is enough, **which means every
> platform can check it**, which is why this is an ordinary driver check.
>
> Skipped for root, which can read a 000 directory anyway.

It gates on `id -u` = 0 -- which is why the WSL2 gate passes: it **skips** there. But
"am I root" is one instance of the general question, *can this host make a directory
unreadable to its owner at all?* On Windows the answer is no at any privilege level.
This is the M477 shape exactly: an assumption about the host, written down in a
comment, never probed. The fix is the same shape too -- probe the capability, then
skip loudly with a stated reason, instead of asking whether we are root.

Keeping the two categories apart is the whole point:

- For the **privacy** failures, the platform's inability *is* the finding. Skipping
  them would convert a security-relevant truth into a green.
- For these two, the platform cannot present the condition, so jichi's behaviour is
  **untested here**, not broken. Saying "untested" is honest; a red that blames the
  program is not.

## What was NOT done

- **`/etc/fstab` WAS edited, after this row was first written**, and the
  result is the section above: MSYS2 can honour POSIX modes, and ships with
  them off. The stock file is kept at `/etc/fstab.jichi-backup`.
- **`pacman -Syu` was not run.** The upgrade backlog is unrelated to this row, and
  replacing `msys2-runtime` while MSYS processes are live is the one operation that
  genuinely needs an interactive session with nothing else attached.
- **MINGW64 is untouched.** That is the row where POSIX emulation ends and the
  native CRT begins, and it is the interesting one.

## The proposal this row argues for

`jc_make_private()` trusts `chmod`'s return value, and on this platform that return
value is a lie. Nothing in jichi can currently tell the difference between "the
file is owner-only" and "the call was accepted and ignored".

The check is cheap and platform-independent: create a file, `chmod` it, `stat` it
back, compare. `doctor` is the right home -- it already exists to tell you what your
environment will not do for you. A container on an odd filesystem, a network mount,
or an option nobody remembers setting would all hit this the same silent way.

This is the same rule the last three milestones kept arriving at from different
directions: **a call whose failure is indistinguishable from its success has not
been checked.** M476 found it in the build's feature probes, M477 in tests asserting
the shape of their host, and this row finds it in a security guarantee.

## For a learner

Six failing drivers looked like six problems. Reading the assertions rather than the
names showed one cause, and the cause was neither a bug in the program nor a
mistake in the tests -- it was the platform accepting an instruction it had no
intention of carrying out.

Two habits did the work, and both are cheap:

1. **Ask the host the diagnostic questions before compiling.** Cygwin had taught us
   to check for a pid 1 and for `VmHWM:`; both were absent here too, and knowing
   that in advance turned three would-be failures into predictions that then did not
   happen, because M477 had already fixed them. A prediction that comes true is
   worth more than a surprise that gets explained afterwards.
2. **Separate "the test failed" from "the test could not run".** Four of these
   failures are real and should stay red. Two describe a scenario the platform will
   not let a test build. Counting them together would have produced "six failures"
   and hidden both facts.
